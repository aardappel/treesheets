struct Text {
    Cell *cell {nullptr};
    Image *image {nullptr};
    // Never modify `t` other than appending at the end without going through the methods below
    // (SetText, Clear, InsertText, RemoveText, ReplaceText), or `runs` gets out of sync.
    wxString t {wxEmptyString};
    int relsize {0};
    int stylebits {0};
    // Rich text: ranges of `t` that deviate from the base style (`stylebits`, cell->textcolor).
    TextRuns runs;
    int extent {0};
    wxDateTime lastedit;
    bool filtered {false};
    // The raw (per-cell) filter match result, before "show entire row on match" expansion
    // is applied. Kept separate from `filtered` so that toggling that option can recompute
    // the displayed `filtered` flag without having to re-run the underlying filter.
    bool filteredraw {false};

    void WasEdited() { lastedit = wxDateTime::Now(); }

    void SetText(const wxString &str) {
        t = str;
        runs.clear();
    }

    void InsertText(int pos, const wxString &ins) {
        t.insert(pos, ins);
        runs.Insert(pos, static_cast<int>(ins.Len()));
        CheckRuns();
    }

    void RemoveText(int pos, int len) {
        t.Remove(pos, len);
        runs.Remove(pos, len, stylebits);
        CheckRuns();
    }

    // The replacement takes the style of the first replaced character.
    void ReplaceText(int pos, int len, const wxString &str) {
        t.Remove(pos, len);
        t.insert(pos, str);
        runs.Replace(pos, len, static_cast<int>(str.Len()), stylebits);
        CheckRuns();
    }

    void CheckRuns() const {
#ifdef _DEBUG
        ASSERT(runs.Valid(static_cast<int>(t.Len())));
#endif
    }

    Text() { WasEdited(); }

    wxBitmap *DisplayImage() const {
        if (cell->grid && cell->grid->folded) {
            auto *tab = sys->frame->GetCurrentTab();
            if (!tab) { return &sys->frame->foldicon; }
            auto *doc = tab->doc.get();
            return sys->frame->GetFoldIcon(
                doc->TextSize(cell->Depth() - doc->drawpath.size(), relsize));
        }
        return image != nullptr ? &image->Display() : nullptr;
    }

    size_t EstimatedMemoryUse() const {
        ASSERT(wxUSE_UNICODE);
        return sizeof(Text) + t.Length() * sizeof(wchar_t) + runs.v.capacity() * sizeof(TextRun);
    }

    double GetNum() const {
        std::wstringstream ss(t.ToStdWstring());
        double r = NAN;
        ss >> r;
        return r;
    }

    void SetNum(double d) {
        std::wstringstream ss;
        // Fixed notation with a limited number of decimals, so we don't show garbage digits
        // beyond the precision, and never resort to an exponent.
        ss << std::fixed << std::setprecision(10) << d;

        auto s = ss.str();
        // Strip unnecessary trailing zeroes. This stops at the '.', so it can't eat into the
        // integer part.
        while (s.back() == '0') { s.pop_back(); }
        // If there were only zeroes, remove '.'.
        if (s.back() == '.') { s.pop_back(); }

        SetText(s);
    }

    static wxString htmlify(wxString str) {
        str.Replace("&", "&amp;");
        str.Replace("<", "&lt;");
        str.Replace(">", "&gt;");
        str.Replace("\"", "&quot;");
        return str;
    }

    static bool IsRichFormat(int format) {
        return format == A_EXPXML || format == A_EXPHTMLT || format == A_EXPHTMLTI ||
               format == A_EXPHTMLTE;
    }

    // The text range [from, to) as XML (<run> elements) or HTML (<span> elements), escaped.
    // Text with the base style is left unmarked; the cell's own markup covers that.
    wxString RichMarkup(int from, int to, int format) const {
        wxString out;
        ForEachSegment(from, to - from, [&](int s, int l, int sb, bool hascolor, uint color) {
            auto seg = htmlify(t.Mid(s, l));
            if (format == A_EXPXML) {
                if (sb == stylebits && !hascolor) {
                    out += seg;
                    return;
                }
                out += wxString::Format("<run stylebits=\"%d\"", sb);
                if (hascolor) { out += wxString::Format(" colorfg=\"0x%06X\"", color); }
                out += ">" + seg + "</run>";
                return;
            }
            // In the HTML cell markup only the differences to the cell's style are needed, except
            // for text decorations, which can't be undone by a nested element, so Cell::ToText
            // leaves them to us for cells with runs.
            wxString style;
            if (((sb ^ stylebits) & STYLE_BOLD) != 0) {
                style += (sb & STYLE_BOLD) != 0 ? "font-weight: bold;" : "font-weight: normal;";
            }
            if (((sb ^ stylebits) & STYLE_ITALIC) != 0) {
                style += (sb & STYLE_ITALIC) != 0 ? "font-style: italic;" : "font-style: normal;";
            }
            if (((sb ^ stylebits) & STYLE_FIXED) != 0) {
                style += "font-family: '";
                style += (sb & STYLE_FIXED) != 0 ? sys->defaultfixedfont + "', monospace;"
                                                 : sys->defaultfont + "', sans-serif;";
            }
            if ((sb & (STYLE_UNDERLINE | STYLE_STRIKETHRU)) != 0) {
                style += "text-decoration:";
                style += (sb & STYLE_UNDERLINE) != 0 ? " underline" : "";
                style += (sb & STYLE_STRIKETHRU) != 0 ? " line-through" : "";
                style += ";";
            }
            if (hascolor) { style += wxString::Format("color: #%06X;", SwapColor(color)); }
            out += style.IsEmpty() ? seg : "<span style=\"" + style + "\">" + seg + "</span>";
        });
        return out;
    }

    wxString ToText(int indent, const Selection &s, int format) const {
        auto range = s.cursor != s.cursorend;
        wxString str;
        if (!runs.empty() && IsRichFormat(format)) {
            auto len = static_cast<int>(t.Len());
            str = range ? RichMarkup(max(s.cursor, 0), min(s.cursorend, len), format)
                        : RichMarkup(0, len, format);
        } else {
            str = range ? t.Mid(s.cursor, s.cursorend - s.cursor) : t;
            if (format == A_EXPXML || format == A_EXPHTMLT || format == A_EXPHTMLTI ||
                format == A_EXPHTMLTE || format == A_EXPHTMLO || format == A_EXPHTMLB) {
                str = htmlify(str);
            }
        }
        if (format == A_EXPTEXT && image != nullptr) str.Append(" ");
        if (format == A_EXPHTMLTI && image != nullptr) {
            str.Prepend("<img src=\"data:" + imagetypes.at(image->type).second + ";base64," +
                        wxBase64Encode(image->data.data(), image->data.size()) + "\" />");
        } else if (format == A_EXPHTMLTE && image != nullptr) {
            wxString relsize = wxString::Format(
                "%d%%", static_cast<int>(100.0 * sys->frame->FromDIP(1.0) / image->display_scale));
            str.Prepend("<img src=\"" + wxString::Format("%llu", image->hash) +
                        image->GetFileExtension() + "\" width=\"" + relsize + "\" height=\"" +
                        relsize + "\" />");
        }
        return str;
    };

    auto MinRelsize(int rs) const { return min(relsize, rs); }
    auto RelSize(int dir, int zoomdepth) {
        relsize = max(min(relsize + dir, g_deftextsize - g_mintextsize() + zoomdepth),
                      g_deftextsize - g_maxtextsize() - zoomdepth);
    }

    static auto IsWord(wxChar c) {
        return wxIsalnum(c) || wxStrchr(L"_\"\'()", c) != nullptr || wxIspunct(c);
    }

    auto GetLinePart(int &currentpos, int breakpos, int limitpos) const {
        auto startpos = currentpos;
        currentpos = breakpos;

        for (auto j = t.begin() + startpos; (j != t.end()) && !wxIsspace(*j) && !IsWord(*j); j++) {
            currentpos++;
            breakpos++;
        }
        // gobble up any trailing punctuation
        if (currentpos != startpos && currentpos < limitpos &&
            (t[currentpos] == '\"' || t[currentpos] == '\'')) {
            currentpos++;
            breakpos++;
        }  // special case: if punctuation followed by quote, quote is meant to be part of word

        for (auto k = t.begin() + currentpos; (k != t.end()) && wxIsspace(*k); k++) {
            // gobble spaces, but do not copy them
            currentpos++;
            if (currentpos == limitpos) {
                breakpos = currentpos;  // happens with a space at the last line, user is most
                                        // likely about to type another word, so
            }
            // need to show space. Alternatively could check if the cursor is actually on this spot.
            // Simply
            // showing a blank new line would not be a good idea, unless the cursor is here for
            // sure, and
            // even then, placing the cursor there again after deselect may be hard.
        }

        ASSERT(startpos != currentpos);

        return t.Mid(startpos, breakpos - startpos);
    }

    wxString GetLine(int &i, int maxcolwidth) const {
        auto l = static_cast<int>(t.Len());

        if (i >= l) { return wxEmptyString; }

        if (i == 0 && l <= maxcolwidth) {
            i = l;
            return t;
        }  // subsumed by the case below, but this case happens 90% of the time, so more optimal
        if (l - i <= maxcolwidth) { return GetLinePart(i, l, l); }

        for (auto p = i + maxcolwidth; p >= i; p--) {
            if (!IsWord(t[p])) { return GetLinePart(i, p, l); }
        }

        // A single word is > maxcolwidth. We split it up anyway.
        // This happens with long urls and e.g. Japanese text without spaces.
        // Should really do proper unicode linebreaking instead (see libunibreak),
        // but for now this is better than the old code below which allowed for arbitrary long
        // words.
        return GetLinePart(i, min(i + maxcolwidth, l), l);

        // for(int p = i+maxcolwidth; p<l;  p++) if (!IsWord(t[p])) return GetLinePart(i, p, l);  //
        // we arrive here only
        // if a single word is too big for maxcolwidth, so simply return that word
        // return GetLinePart(i, l, l);     // big word was the last one
    }

    // Calls f(start, len, stylebits, hascolor, color) for consecutive segments of the text range
    // [start, start + len) that each have a single style.
    template<typename F> void ForEachSegment(int start, int len, F f) const {
        auto pos = start;
        auto end = start + len;
        for (const auto &r : runs.v) {
            if (r.end() <= pos) { continue; }
            if (r.start >= end) { break; }
            if (r.start > pos) {
                f(pos, r.start - pos, stylebits, false, 0U);
                pos = r.start;
            }
            auto segend = min(r.end(), end);
            f(pos, segend - pos, r.stylebits, r.hascolor, r.color);
            pos = segend;
        }
        if (pos < end) { f(pos, end - pos, stylebits, false, 0U); }
    }

    // Rich text lines have one uniform height: every style's font is aligned on a common
    // baseline, and the line is as tall as that takes.
    // Whether every character in [from, to) has all the style bits in `bit`.
    bool HasStyle(int bit, int from, int to) const {
        auto all = true;
        ForEachSegment(from, to - from, [&](int, int, int sb, bool, uint) {
            if ((sb & bit) != bit) { all = false; }
        });
        return all;
    }

    // Whether all the text has the style bit. For an empty text that is the base style.
    bool HasStyleAll(int bit) const {
        return t.IsEmpty() ? (stylebits & bit) == bit
                           : HasStyle(bit, 0, static_cast<int>(t.Len()));
    }

    // Toggles a style bit on the text range [from, to): removes it if the whole range has it,
    // adds it otherwise.
    void ToggleStyle(int bit, int from, int to) {
        auto len = static_cast<int>(t.Len());
        from = max(from, 0);
        to = min(to, len);
        if (from >= to) { return; }
        auto set = !HasStyle(bit, from, to);
        runs.Modify(from, to, len, stylebits, [&](TextRun &r) {
            r.stylebits = set ? (r.stylebits | bit) : (r.stylebits & ~bit);
        });
        CheckRuns();
    }

    // Sets or clears a style bit on the whole text, i.e. the base style and all runs.
    void SetStyleAll(int bit, bool set) {
        auto apply = [&](int &sb) { sb = set ? (sb | bit) : (sb & ~bit); };
        apply(stylebits);
        if (runs.empty()) { return; }
        for (auto &r : runs.v) { apply(r.stylebits); }
        runs.Normalize(stylebits);
        CheckRuns();
    }

    void SetRunColor(uint color, int from, int to) {
        auto len = static_cast<int>(t.Len());
        runs.Modify(from, to, len, stylebits, [&](TextRun &r) {
            r.hascolor = true;
            r.color = color & 0xFFFFFF;
        });
        CheckRuns();
    }

    // Makes the whole text use the cell's text color again.
    void ClearRunColors() {
        for (auto &r : runs.v) { r.hascolor = false; }
        runs.Normalize(stylebits);
    }

    // Resets the whole text to the plain base style.
    void ResetStyle() {
        stylebits = 0;
        runs.clear();
    }

    struct LineMetrics {
        int height {0};
        int ascent {0};
    };

    // Leaves the base font selected.
    template<typename DC> LineMetrics GetLineMetrics(Document *doc, DC &dc, int depth) const {
        doc->PickFont(dc, depth, relsize, stylebits);
        LineMetrics lm;
        lm.height = doc->CharHeight(dc);
        if (runs.empty()) { return lm; }
        lm.ascent = dc.GetFontMetrics().ascent;
        auto maxdescent = lm.height - lm.ascent;
        auto last = stylebits;
        for (const auto &r : runs.v) {
            if (r.stylebits == last) { continue; }
            last = r.stylebits;
            doc->PickFont(dc, depth, relsize, last);
            auto ascent = dc.GetFontMetrics().ascent;
            lm.ascent = max(lm.ascent, ascent);
            maxdescent = max(maxdescent, doc->CharHeight(dc) - ascent);
        }
        lm.height = lm.ascent + maxdescent;
        doc->PickFont(dc, depth, relsize, stylebits);
        return lm;
    }

    // Width of the text range [start, start + len), which must not span lines. Without runs
    // this expects the base font to be selected already; with runs it leaves it selected.
    template<typename DC>
    int RangeWidth(Document *doc, DC &dc, int depth, int start, int len) const {
        auto total = 0;
        if (runs.empty()) {
            dc.GetTextExtent(t.Mid(start, len), &total, nullptr);
            return total;
        }
        ForEachSegment(start, len, [&](int s, int l, int sb, bool, uint) {
            doc->PickFont(dc, depth, relsize, sb);
            auto w = 0;
            dc.GetTextExtent(t.Mid(s, l), &w, nullptr);
            total += w;
        });
        doc->PickFont(dc, depth, relsize, stylebits);
        return total;
    }

    template<typename DC>
    void TextSize(DC &dc, int &sx, int &sy, int tiny, int &leftoffset, int maxcolwidth,
                  Document *doc, int depth) const {
        sx = sy = 0;
        auto i = 0;
        auto rich = tiny == 0 && !runs.empty();
        auto lm = rich ? GetLineMetrics(doc, dc, depth) : LineMetrics();
        for (;;) {
            auto start = i;
            auto curl = GetLine(i, maxcolwidth);
            if (curl.IsEmpty()) { break; }
            int x = 0;
            int y = 0;
            if (tiny != 0) {
                x = static_cast<int>(curl.Len());
                y = 1;
            } else if (rich) {
                x = RangeWidth(doc, dc, depth, start, static_cast<int>(curl.Len()));
                y = lm.height;
            } else {
                dc.GetTextExtent(curl, &x, &y);
            }
            sx = max(x, sx);
            sy += y;
            leftoffset = y;
        }
        if (tiny == 0) { sx += 4; }
    }

    // The direction of a character for picking the alignment of automatically aligned text:
    // 1 for right-to-left letters, -1 for other letters, 0 for characters without a direction
    // of their own (digits, punctuation, spaces, symbols, marks).
    static int StrongDirection(uint c) {
        if (c < 0x80) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z' ? -1 : 0; }
        if ((c >= 0x0660 && c <= 0x066C) || (c >= 0x06F0 && c <= 0x06F9)) {
            return 0;  // Arabic digits and separators
        }
        if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) ||
            (c >= 0xFE70 && c <= 0xFEFF) || (c >= 0x10800 && c <= 0x10FFF) ||
            (c >= 0x1E800 && c <= 0x1EFFF)) {
            return 1;  // Hebrew, Arabic, Syriac, Thaana, N'Ko and the like
        }
        if (c < 0xC0 || c == 0xD7 || c == 0xF7 || (c >= 0x0300 && c <= 0x036F) ||
            (c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) ||
            (c >= 0xD800 && c <= 0xF8FF) || (c >= 0xFE00 && c <= 0xFE0F) ||
            (c >= 0xFF00 && c <= 0xFF20) || c >= 0x1F000) {
            return 0;
        }
        return -1;
    }

    // Whether the first character with a direction is a right-to-left one, like HTML's
    // dir="auto".
    bool IsRightToLeft() const {
        auto len = static_cast<int>(t.Len());
        for (auto i = 0; i < len; i++) {
            auto c = static_cast<uint>(t[i].GetValue());
            // Where wxString is UTF-16, combine surrogate pairs.
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len) {
                auto lo = static_cast<uint>(t[i + 1].GetValue());
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    i++;
                }
            }
            if (auto d = StrongDirection(c); d != 0) { return d > 0; }
        }
        return false;
    }

    // How far a line of width `w` is moved right from the left edge of the text, by the
    // alignment (see Cell::TextAlign).
    int AlignOffset(int align, int w, int ixs) const {
        if (align == TEXTALIGN_LEFT || cell->tiny) { return 0; }
        auto room = cell->TextAlignWidth(ixs) - w;
        if (room <= 0) { return 0; }
        return align == TEXTALIGN_CENTER ? room / 2 : room;
    }

    bool IsInSearch() const {
        return !sys->searchstring.IsEmpty() &&
               (sys->casesensitivesearch ? t.Find(sys->searchstring)
                                         : t.Lower().Find(sys->searchstring)) >= 0;
    }

    template<typename DC>
    int Render(Document *doc, int bx, int by, int depth, DC &dc, int &leftoffset,
               int maxcolwidth) const {
        auto ixs = 0;
        auto iys = 0;
        if (!cell->tiny) { treesheets::System::ImageSize(DisplayImage(), ixs, iys); }

        if (ixs != 0 && iys != 0) {
            treesheets::System::ImageDraw(DisplayImage(), dc, bx + 1 + g_margin_extra,
                                          by + (cell->tys - iys) / 2 + g_margin_extra,
                                          (cell->grid && cell->grid->folded) ? nullptr : image);
            ixs += 2;
            iys += 2;
        }

        if (t.empty()) { return iys; }

        doc->PickFont(dc, depth, relsize, stylebits);

        auto rich = !cell->tiny && !runs.empty();
        auto lm = rich ? GetLineMetrics(doc, dc, depth) : LineMetrics();
        auto h = cell->tiny ? 1 : (rich ? lm.height : doc->CharHeight(dc));
        leftoffset = h;
        auto i = 0;
        auto lines = 0;
        auto searchfound = IsInSearch();
        auto istag = cell->IsTag(doc);
        auto align = cell->tiny ? TEXTALIGN_LEFT : cell->TextAlign();
        if (cell->tiny) {
            if (searchfound) {
                dc.SetPen(*wxRED_PEN);
            } else if (filtered) {
                dc.SetPen(*wxLIGHT_GREY_PEN);
            } else if (istag) {
                dc.SetPen(wxPen(LightColor(doc->tags[t].second)));
            } else {
                dc.SetPen(sys->pen_tinytext);
            }
        }
        for (;;) {
            auto start = i;
            auto curl = GetLine(i, maxcolwidth);
            if (curl.IsEmpty()) { break; }
            if (cell->tiny) {
                if (sys->fastrender) {
                    dc.DrawLine(bx + ixs, by + lines * h, bx + ixs + static_cast<int>(curl.Len()),
                                by + lines * h);
                    /*
                    wxPoint points[] = { wxPoint(bx + ixs, by + lines * h), wxPoint(bx + ixs +
                    curl.Len(), by + lines * h) }; dc.DrawLines(1, points, 0, 0);
                     */
                } else {
                    auto word = 0;
                    loop(p, static_cast<int>(curl.Len()) + 1) {
                        if (static_cast<int>(curl.Len()) <= p || curl[p] == ' ') {
                            if (word != 0) {
                                dc.DrawLine(bx + p - word + ixs, by + lines * h, bx + p,
                                            by + lines * h);
                            }
                            word = 0;
                        } else {
                            word++;
                        }
                    }
                }
            } else if (rich) {
                auto x = bx + 2 + ixs + g_margin_extra;
                if (align != TEXTALIGN_LEFT) {
                    auto w = RangeWidth(doc, dc, depth, start, static_cast<int>(curl.Len()));
                    x += AlignOffset(align, w, ixs);
                }
                auto ty = by + lines * h + g_margin_extra;
                ForEachSegment(start, static_cast<int>(curl.Len()),
                               [&](int s, int l, int sb, bool hascolor, uint color) {
                    doc->PickFont(dc, depth, relsize, sb);
                    if (searchfound) {
                        dc.SetTextForeground(*wxRED);
                    } else if (filtered) {
                        dc.SetTextForeground(*wxLIGHT_GREY);
                    } else if (istag) {
                        dc.SetTextForeground(LightColor(doc->tags[t].second));
                    } else if (hascolor) {
                        dc.SetTextForeground(LightColor(color));
                    } else if (cell->textcolor != 0U) {
                        dc.SetTextForeground(LightColor(cell->textcolor));
                    } else {
                        dc.SetTextForeground(sys->rubberbandcolor);
                    }
                    auto str = t.Mid(s, l);
                    auto w = 0;
                    dc.GetTextExtent(str, &w, nullptr);
                    DrawText(dc, str, x, ty + lm.ascent - dc.GetFontMetrics().ascent);
                    x += w;
                });
                doc->PickFont(dc, depth, relsize, stylebits);
                dc.SetTextForeground(sys->rubberbandcolor);
            } else {
                if (searchfound) {
                    dc.SetTextForeground(*wxRED);
                } else if (filtered) {
                    dc.SetTextForeground(*wxLIGHT_GREY);
                } else if (istag) {
                    dc.SetTextForeground(LightColor(doc->tags[t].second));
                } else if (cell->textcolor != 0U) {
                    dc.SetTextForeground(LightColor(cell->textcolor));  // FIXME: clean up
                }
                auto tx = bx + 2 + ixs;
                if (align != TEXTALIGN_LEFT) {
                    auto w = 0;
                    dc.GetTextExtent(curl, &w, nullptr);
                    tx += AlignOffset(align, w, ixs);
                }
                auto ty = by + lines * h;
                DrawText(dc, curl, tx + g_margin_extra, ty + g_margin_extra);
                if (searchfound || filtered || istag || cell->textcolor != 0U) {
                    dc.SetTextForeground(sys->rubberbandcolor);
                }
            }
            lines++;
        }

        return max(lines * h, iys);
    }

    void FindCursor(Document *doc, int bx, int by, wxReadOnlyDC &dc, Selection &s, int maxcolwidth) const {
        bx -= g_margin_extra;
        by -= g_margin_extra;

        auto ixs = 0;
        auto iys = 0;
        if (!cell->tiny) { treesheets::System::ImageSize(DisplayImage(), ixs, iys); }
        if (ixs != 0) { ixs += 2; }

        auto depth = cell->Depth() - static_cast<int>(doc->drawpath.size());
        doc->PickFont(dc, depth, relsize, stylebits);

        auto i = 0;
        auto linestart = 0;
        auto line = by / (runs.empty() ? doc->CharHeight(dc) : GetLineMetrics(doc, dc, depth).height);
        wxString ls;

        loop(l, line + 1) {
            linestart = i;
            ls = GetLine(i, maxcolwidth);
        }

        if (auto align = cell->TextAlign(); align != TEXTALIGN_LEFT) {
            auto w = RangeWidth(doc, dc, depth, linestart, static_cast<int>(ls.Len()));
            bx -= AlignOffset(align, w, ixs);
        }

        for (;;) {
            auto x = 0;
            if (runs.empty()) {
                dc.GetTextExtent(ls, &x, nullptr);
            } else {
                x = RangeWidth(doc, dc, depth, linestart, static_cast<int>(ls.Len()));
            }
            // FIXME: can we do this more intelligently?
            if (x <= bx - ixs + 2 || x == 0) { break; }
            ls.Truncate(ls.Len() - 1);
        }

        s.cursor = s.cursorend = linestart + static_cast<int>(ls.Len());
        ASSERT(s.cursor >= 0 && s.cursor <= static_cast<int>(t.Len()));
    }

    template<typename DC>
    void DrawCursor(Document *doc, DC &dc, Selection &s, bool full, uint color,
                    int maxcolwidth) const {
        auto ixs = 0;
        auto iys = 0;
        if (!cell->tiny) { treesheets::System::ImageSize(DisplayImage(), ixs, iys); }
        if (ixs != 0) { ixs += 2; }
        auto depth = cell->Depth() - static_cast<int>(doc->drawpath.size());
        doc->PickFont(dc, depth, relsize, stylebits);
        auto h = runs.empty() ? doc->CharHeight(dc) : GetLineMetrics(doc, dc, depth).height;
        auto align = cell->TextAlign();
        auto lineoffset = [&](int start, int len) {
            if (align == TEXTALIGN_LEFT) { return 0; }
            return AlignOffset(align, RangeWidth(doc, dc, depth, start, len), ixs);
        };

        if (s.cursor != s.cursorend) {
            // A range selection can span multiple lines (one rectangle drawn per line
            // below); it changes far less often than the plain typing caret below, so
            // it isn't worth caching.
            auto i = 0;
            for (auto l = 0;; l++) {
                auto start = i;
                auto ls = GetLine(i, maxcolwidth);
                auto len = static_cast<int>(ls.Len());
                auto end = start + len;
                if (s.cursor <= end && s.cursorend >= start) {
                    auto x2 = RangeWidth(doc, dc, depth, start, min(s.cursorend, end) - start);
                    auto x1 = RangeWidth(doc, dc, depth, start, max(s.cursor, start) - start);
                    if (x1 != x2) {
                        int startx = cell->GetX(doc) + x1 + 2 + ixs + g_margin_extra +
                                     lineoffset(start, len);
                        int starty =
                            cell->GetY(doc) + l * h + 1 + cell->ycenteroff + g_margin_extra;
                        DrawRectangle(dc, color, startx, starty, x2 - x1, h - 1, true);
                        HintIMELocation(doc, startx, starty, h - 1, stylebits);
                    }
                }
                if (len == 0) { break; }
            }
            return;
        }

        // Thin cursor (the common case: redrawn on every repaint while a cell is being
        // edited, including repaints the edit itself didn't cause, e.g. a resize or an
        // edit elsewhere). Which wrapped line it's on and its horizontal offset from
        // the cell's own origin are a deterministic function of the text, cursor
        // index, font, column width and alignment alone, so cache those and skip the line scan
        // and GetTextExtent measurement -- the dominant cost here -- when none of them
        // changed since the last time we drew it. The cell's actual screen position
        // and row height are recomputed fresh below regardless (cheap, and can shift
        // for reasons unrelated to this cell, e.g. a sibling growing).
        auto &cc = doc->cursorposcache;
        if (cc.cell != cell || cc.image != image || cc.cursor != s.cursor ||
            cc.stylebits != stylebits || cc.relsize != relsize || cc.maxcolwidth != maxcolwidth ||
            cc.align != align ||
            (align != TEXTALIGN_LEFT && cc.alignwidth != cell->TextAlignWidth(ixs)) ||
            cc.text != t || cc.runs != runs.v) {
            cc.cell = cell;
            cc.image = image;
            cc.text = t;
            cc.runs = runs.v;
            cc.cursor = s.cursor;
            cc.stylebits = stylebits;
            cc.relsize = relsize;
            cc.maxcolwidth = maxcolwidth;
            cc.align = align;
            cc.alignwidth = cell->TextAlignWidth(ixs);
            cc.found = false;
            auto i = 0;
            for (auto l = 0;; l++) {
                auto start = i;
                auto ls = GetLine(i, maxcolwidth);
                auto len = static_cast<int>(ls.Len());
                auto end = start + len;
                if (s.cursor >= start && s.cursor <= end) {
                    auto x = RangeWidth(doc, dc, depth, start, s.cursor - start);
                    cc.localdx = x + 1 + ixs + g_margin_extra + lineoffset(start, len);
                    cc.line = l;
                    cc.found = true;
                    break;
                }
                if (len == 0) { break; }
            }
        }
        if (cc.found) {
            int startx = cell->GetX(doc) + cc.localdx;
            int starty = cell->GetY(doc) + cc.line * h + 1 + cell->ycenteroff + g_margin_extra;
            DrawRectangle(dc, color, startx, starty, 2, h - 2);
            HintIMELocation(doc, startx, starty, h - 2, stylebits);
        }
    }

    void ExpandToWord(Selection &s) {
        if (!wxIsalnum(t[s.cursor])) { return; }
        while (s.cursor > 0 && wxIsalnum(t[s.cursor - 1])) { s.cursor--; }
        while (s.cursorend < static_cast<int>(t.Len()) && wxIsalnum(t[s.cursorend])) {
            s.cursorend++;
        }
    }

    void SelectWord(Selection &s) {
        if (s.cursor >= static_cast<int>(t.Len())) { return; }
        s.cursorend = s.cursor + 1;
        ExpandToWord(s);
    }

    void SelectWordBefore(Selection &s) {
        if (s.cursor <= 1) { return; }
        s.cursorend = s.cursor--;
        ExpandToWord(s);
    }

    bool RangeSelRemove(Selection &s) {
        WasEdited();
        if (s.cursor != s.cursorend) {
            RemoveText(s.cursor, s.cursorend - s.cursor);
            s.cursorend = s.cursor;
            return true;
        }
        return false;
    }

    void SetRelSize(Selection &s) {
        if (!t.IsEmpty() || cell->parent == nullptr) { return; }
        std::array dd = {0, 1, 1, 0, 0, -1, -1, 0};
        for (auto i = 0; i < 4; i++) {
            auto x = max(0, min(s.x + dd[i * 2], s.grid->xs - 1));
            auto y = max(0, min(s.y + dd[i * 2 + 1], s.grid->ys - 1));
            auto *c = s.grid->C(x, y).get();
            if (!c->text.t.IsEmpty()) {
                relsize = c->text.relsize;
                break;
            }
        }
    }

    // After inserting the text of `src` at pos, gives it the styling it had there.
    void OverlayRuns(int pos, const Text &src) {
        auto len = static_cast<int>(t.Len());
        src.ForEachSegment(0, static_cast<int>(src.t.Len()),
                           [&](int s, int l, int sb, bool hascolor, uint color) {
            runs.Modify(pos + s, pos + s + l, len, stylebits, [&](TextRun &r) {
                r.stylebits = sb;
                r.hascolor = hascolor;
                r.color = color;
            });
        });
        CheckRuns();
    }

    // `src`: the Text that `ins` was taken from, if its runs should come along.
    auto Insert(Document *doc, const wxString &ins, Selection &s, bool keeprelsize,
                const Text *src = nullptr) {
        auto prevl = t.Len();
        if (!s.TextEdit()) { Clear(doc, s); }
        RangeSelRemove(s);
        if (prevl == 0U && !keeprelsize) { SetRelSize(s); }
        InsertText(s.cursor, ins);
        if (src != nullptr && !src->runs.empty() && src->t == ins) { OverlayRuns(s.cursor, *src); }
        s.cursor = s.cursorend = s.cursor + static_cast<int>(ins.Len());
    }

    void Key(Document *doc, int k, Selection &s) {
        wxString ins;
        ins += k;
        Insert(doc, ins, s, false);
    }

    void Delete(Selection &s) {
        if (!RangeSelRemove(s)) {
            if (s.cursor < static_cast<int>(t.Len())) { RemoveText(s.cursor, 1); };
        }
    }
    void Backspace(Selection &s) {
        if (!RangeSelRemove(s)) {
            if (s.cursor > 0) {
                RemoveText(--s.cursor, 1);
                --s.cursorend;
            }
        }
    }
    void DeleteWord(Selection &s) {
        SelectWord(s);
        Delete(s);
    }
    void BackspaceWord(Selection &s) {
        SelectWordBefore(s);
        Backspace(s);
    }

    void ReplaceStr(const wxString &str, const wxString &lstr) {
        if (sys->casesensitivesearch) {
            for (auto i = 0, j = 0; (j = t.Mid(i).Find(sys->searchstring)) >= 0;) {
                WasEdited();
                i += j;
                ReplaceText(i, static_cast<int>(sys->searchstring.Len()), str);
                i += str.Len();
            }
        } else {
            auto lowert = t.Lower();
            for (auto i = 0, j = 0; (j = lowert.Mid(i).Find(sys->searchstring)) >= 0;) {
                WasEdited();
                i += j;
                lowert.Remove(i, sys->searchstring.Len());
                ReplaceText(i, static_cast<int>(sys->searchstring.Len()), str);
                lowert.insert(i, lstr);
                i += str.Len();
            }
        }
    }

    void Clear(Document *doc, Selection &s) {
        t.Clear();
        runs.clear();
        s.EnterEdit(doc);
    }

    void HomeEnd(Selection &s, bool home) const {
        auto i = 0;
        auto cw = cell->ColWidth();
        auto findwhere = home ? s.cursor : s.cursorend;
        for (;;) {
            auto start = i;
            auto curl = GetLine(i, cw);
            if (curl.IsEmpty()) { break; }
            auto end = i == t.Len() ? i : i - 1;
            if (findwhere >= start && findwhere <= end) {
                s.cursor = s.cursorend = home ? start : end;
                break;
            }
        }
    }

    void Save(wxDataOutputStream &dos) const {
        dos.WriteString(t.wx_str());
        dos.Write32(relsize);
        dos.Write32(image != nullptr ? image->savedindex : -1);
        dos.Write32(stylebits);
        wxLongLong le = lastedit.GetValue();
        dos.Write64(&le, 1);
        // Rich text runs (file version 27). Almost always zero. Positions are counted in
        // Unicode code points, so files don't depend on how this platform stores the text.
        dos.Write32(static_cast<wxUint32>(runs.v.size()));
        CodePointCursor cursor {[&](int i) { return static_cast<uint>(t[i].GetValue()); },
                                static_cast<int>(t.Len())};
        for (const auto &r : runs.v) {
            auto start = cursor.CodePointAt(r.start);
            auto end = cursor.CodePointAt(r.end());
            dos.Write32(start);
            dos.Write32(end - start);
            dos.Write32(r.stylebits);
            dos.Write32((r.color & 0xFFFFFF) | (r.hascolor ? TS_RUN_HASCOLOR : 0));
        }
    }

    void Load(wxDataInputStream &dis) {
        t = dis.ReadString();

        // if (t.length() > 10000)
        //    printf("");

        if (sys->versionlastloaded <= 11) {
            dis.Read32();  // numlines
        }

        relsize = dis.Read32();

        int i = dis.Read32();
        image = i >= 0 && i < static_cast<int>(sys->loadimageids.size())
                    ? sys->imagelist[sys->loadimageids[i]].get()
                    : nullptr;

        if (sys->versionlastloaded >= 7) { stylebits = dis.Read32(); }

        wxLongLong time;
        if (sys->versionlastloaded >= 14) {
            dis.Read64(&time, 1);
        } else {
            time = sys->fakelasteditonload--;
        }
        lastedit = wxDateTime(time);

        runs.clear();
        if (sys->versionlastloaded >= 27) {
            auto numruns = dis.Read32();
            // A run is at least one character, so more runs than characters means a corrupt file.
            if (numruns <= t.Len()) {
                runs.v.reserve(numruns);
                auto getunit = [&](int i) { return static_cast<uint>(t[i].GetValue()); };
                auto len = static_cast<int>(t.Len());
                auto numcodepoints = CodePointCursor {getunit, len}.CodePointAt(len);
                CodePointCursor cursor {getunit, len};
                auto pos = 0;  // in code points
                for (wxUint32 i = 0; i < numruns; i++) {
                    TextRun r;
                    // Positions in the file are in code points, see Save.
                    auto start = static_cast<int>(dis.Read32());
                    auto length = static_cast<int>(dis.Read32());
                    r.stylebits = static_cast<int>(dis.Read32());
                    auto col = dis.Read32();
                    r.color = col & 0xFFFFFF;
                    r.hascolor = (col & TS_RUN_HASCOLOR) != 0;
                    // Defend against corrupt data: keep runs sorted, disjoint and inside the text.
                    start = max(start, pos);
                    length = min(length, numcodepoints - start);
                    if (length > 0) {
                        pos = start + length;
                        r.start = cursor.UnitAt(start);
                        r.len = cursor.UnitAt(pos) - r.start;
                        if (r.len > 0) { runs.v.push_back(r); }
                    }
                }
                runs.Normalize(stylebits);
            }
        }
        CheckRuns();
    }

    auto Eval(Evaluator &ev) const {
        switch (cell->celltype) {
            // Load variable's data.
            case CT_VARU: {
                auto v = ev.Lookup(t);
                if (!v) {
                    v = cell->Clone(nullptr);
                    v->celltype = CT_DATA;
                    v->text.SetText("**Variable Load Error**");
                }
                return v;
            }

            // Return our current data.
            case CT_DATA: return cell->Clone(nullptr);

            default: return unique_ptr<Cell>();
        }
    }
};
