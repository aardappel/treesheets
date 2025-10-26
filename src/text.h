struct Text {
    Cell *cell {nullptr};
    Image *image {nullptr};
    wxString t {wxEmptyString};
    int relsize {0};
    int stylebits {0};
    int extent {0};
    wxDateTime lastedit;
    bool filtered {false};

    void WasEdited() { lastedit = wxDateTime::Now(); }

    Text() { WasEdited(); }

    wxBitmap *DisplayImage() {
        return cell->grid && cell->grid->folded ? &sys->frame->foldicon
                                                : (image ? &image->Display() : nullptr);
    }

    size_t EstimatedMemoryUse() {
        ASSERT(wxUSE_UNICODE);
        return sizeof(Text) + t.Length() * sizeof(wchar_t);
    }

    double GetNum() {
        std::wstringstream ss(t.ToStdWstring());
        double r;
        ss >> r;
        return r;
    }

    void SetNum(double d) {
        std::wstringstream ss;
        ss << std::fixed;

        // We're going to use at most 19 digits after '.'. Add small value round remainder.
        size_t max_significant = 10;
        d += 0.00000000005;

        ss << d;

        auto s = ss.str();
        // First trim whatever lies beyond the precision to avoid garbage digits.
        max_significant += 2;  // "0."
        if (s[0] == '-') max_significant++;
        if (s.length() > max_significant) s.erase(max_significant);
        // Now strip unnecessary trailing zeroes.
        while (s.back() == '0') s.pop_back();
        // If there were only zeroes, remove '.'.
        if (s.back() == '.') s.pop_back();

        t = s;
    }

    wxString htmlify(wxString &str) {
        wxString r;
        for (auto cref : str) {
            switch (wxChar c = cref.GetValue()) {
                case '&': r += L"&amp;"; break;
                case '<': r += L"&lt;"; break;
                case '>': r += L"&gt;"; break;
                default: r += c;
            }
        }
        return r;
    }

    wxString ToText(int indent, const Selection &s, int format) {
        wxString str = s.cursor != s.cursorend ? t.Mid(s.cursor, s.cursorend - s.cursor) : t;
        if (format == A_EXPXML || format == A_EXPHTMLT || format == A_EXPHTMLTI ||
            format == A_EXPHTMLTE || format == A_EXPHTMLO || format == A_EXPHTMLB)
            str = htmlify(str);
        if (format == A_EXPHTMLTI && image)
            str.Prepend(L"<img src=\"data:" + imagetypes.at(image->type).second + ";base64," +
                        wxBase64Encode(image->data.data(), image->data.size()) + "\" />");
        else if (format == A_EXPHTMLTE && image) {
            wxString relsize = wxString::Format(
                "%d%%", static_cast<int>(100.0 * sys->frame->FromDIP(1.0) / image->display_scale));
            str.Prepend(L"<img src=\"" + wxString::Format("%llu", image->hash) +
                        image->GetFileExtension() + L"\" width=\"" + relsize + L"\" height=\"" +
                        relsize + L"\" />");
        }
        return str;
    };

    auto MinRelsize(int rs) { return min(relsize, rs); }
    auto RelSize(int dir, int zoomdepth) {
        relsize = max(min(relsize + dir, g_deftextsize - g_mintextsize() + zoomdepth),
                      g_deftextsize - g_maxtextsize() - zoomdepth);
    }

    auto IsWord(wxChar c) { return wxIsalnum(c) || wxStrchr(L"_\"\'()", c) || wxIspunct(c); }
    auto GetLinePart(int &currentpos, int breakpos, int limitpos) {
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
            if (currentpos == limitpos)
                breakpos = currentpos;  // happens with a space at the last line, user is most
                                        // likely about to type another word, so
            // need to show space. Alternatively could check if the cursor is actually on this spot.
            // Simply
            // showing a blank new line would not be a good idea, unless the cursor is here for
            // sure, and
            // even then, placing the cursor there again after deselect may be hard.
        }

        ASSERT(startpos != currentpos);

        return t.Mid(startpos, breakpos - startpos);
    }

    wxString GetLine(auto &i, auto maxcolwidth) {
        auto l = static_cast<int>(t.Len());

        if (i >= l) return wxEmptyString;

        if (!i && l <= maxcolwidth) {
            i = l;
            return t;
        }  // subsumed by the case below, but this case happens 90% of the time, so more optimal
        if (l - i <= maxcolwidth) return GetLinePart(i, l, l);

        for (auto p = i + maxcolwidth; p >= i; p--)
            if (!IsWord(t[p])) return GetLinePart(i, p, l);

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

    void TextSize(wxDC &dc, int &sx, int &sy, int tiny, int &leftoffset, int maxcolwidth) {
        sx = sy = 0;
        auto i = 0;
        for (;;) {
            auto curl = GetLine(i, maxcolwidth);
            if (!curl.Len()) break;
            int x, y;
            if (tiny) {
                x = static_cast<int>(curl.Len());
                y = 1;
            } else
                dc.GetTextExtent(curl, &x, &y);
            sx = max(x, sx);
            sy += y;
            leftoffset = y;
        }
        if (!tiny) sx += 4;
    }

    bool IsInSearch() {
        return sys->searchstring.Len() &&
               (sys->casesensitivesearch ? t.Find(sys->searchstring)
                                         : t.Lower().Find(sys->searchstring)) >= 0;
    }

    int Render(Document *doc, int bx, int by, int depth, wxDC &dc, int &leftoffset,
               int maxcolwidth) {
        auto ixs = 0, iys = 0;
        if (!cell->tiny) sys->ImageSize(DisplayImage(), ixs, iys);

        if (ixs && iys) {
            sys->ImageDraw(DisplayImage(), dc, bx + 1 + g_margin_extra,
                           by + (cell->tys - iys) / 2 + g_margin_extra);
            ixs += 2;
            iys += 2;
        }

        if (t.empty()) return iys;

        doc->PickFont(dc, depth, relsize, stylebits);

        auto h = cell->tiny ? 1 : dc.GetCharHeight();
        leftoffset = h;
        auto i = 0;
        auto lines = 0;
        auto searchfound = IsInSearch();
        auto istag = cell->IsTag(doc);
        if (cell->tiny) {
            if (searchfound)
                dc.SetPen(*wxRED_PEN);
            else if (filtered)
                dc.SetPen(*wxLIGHT_GREY_PEN);
            else if (istag)
                dc.SetPen(wxColour(doc->tags[t]));
            else
                dc.SetPen(sys->pen_tinytext);
        }
        for (;;) {
            auto curl = GetLine(i, maxcolwidth);
            if (!curl.Len()) break;
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
                            if (word)
                                dc.DrawLine(bx + p - word + ixs, by + lines * h, bx + p,
                                            by + lines * h);
                            word = 0;
                        } else
                            word++;
                    }
                }
            } else {
                if (searchfound)
                    dc.SetTextForeground(*wxRED);
                else if (filtered)
                    dc.SetTextForeground(*wxLIGHT_GREY);
                else if (istag)
                    dc.SetTextForeground(wxColour(doc->tags[t]));
                else if (cell->textcolor)
                    dc.SetTextForeground(cell->textcolor);  // FIXME: clean up
                auto tx = bx + 2 + ixs;
                auto ty = by + lines * h;
                dc.DrawText(curl, tx + g_margin_extra, ty + g_margin_extra);
                if (searchfound || filtered || istag || cell->textcolor)
                    dc.SetTextForeground(*wxBLACK);
            }
            lines++;
        }

        return max(lines * h, iys);
    }

    void FindCursor(Document *doc, int bx, int by, wxDC &dc, Selection &s, int maxcolwidth) {
        bx -= g_margin_extra;
        by -= g_margin_extra;

        auto ixs = 0, iys = 0;
        if (!cell->tiny) sys->ImageSize(DisplayImage(), ixs, iys);
        if (ixs) ixs += 2;

        doc->PickFont(dc, cell->Depth() - doc->drawpath.size(), relsize, stylebits);

        auto i = 0, linestart = 0;
        auto line = by / dc.GetCharHeight();
        wxString ls;

        loop(l, line + 1) {
            linestart = i;
            ls = GetLine(i, maxcolwidth);
        }

        for (;;) {
            auto x = 0, y = 0;
            dc.GetTextExtent(ls, &x, &y);  // FIXME: can we do this more intelligently?
            if (x <= bx - ixs + 2 || !x) break;
            ls.Truncate(ls.Len() - 1);
        }

        s.cursor = s.cursorend = linestart + static_cast<int>(ls.Len());
        ASSERT(s.cursor >= 0 && s.cursor <= static_cast<int>(t.Len()));
    }

    void DrawCursor(Document *doc, wxDC &dc, Selection &s, bool full, uint color, int maxcolwidth) {
        auto ixs = 0, iys = 0;
        if (!cell->tiny) sys->ImageSize(DisplayImage(), ixs, iys);
        if (ixs) ixs += 2;
        doc->PickFont(dc, cell->Depth() - doc->drawpath.size(), relsize, stylebits);
        auto h = dc.GetCharHeight();
        {
            auto i = 0;
            for (auto l = 0;; l++) {
                auto start = i;
                auto ls = GetLine(i, maxcolwidth);
                auto len = static_cast<int>(ls.Len());
                auto end = start + len;

                if (s.cursor != s.cursorend) {
                    if (s.cursor <= end && s.cursorend >= start) {
                        ls.Truncate(min(s.cursorend, end) - start);
                        auto x1 = 0, x2 = 0;
                        dc.GetTextExtent(ls, &x2, nullptr);
                        ls.Truncate(max(s.cursor, start) - start);
                        dc.GetTextExtent(ls, &x1, nullptr);
                        if (x1 != x2) {
                            int startx = cell->GetX(doc) + x1 + 2 + ixs + g_margin_extra;
                            int starty =
                                cell->GetY(doc) + l * h + 1 + cell->ycenteroff + g_margin_extra;
                            DrawRectangle(dc, color, startx, starty, x2 - x1, h - 1, true);
                            HintIMELocation(doc, startx, starty, h - 1, stylebits);
                        }
                    }
                } else if (s.cursor >= start && s.cursor <= end) {
                    ls.Truncate(s.cursor - start);
                    auto x = 0;
                    dc.GetTextExtent(ls, &x, nullptr);
                    int startx = cell->GetX(doc) + x + 1 + ixs + g_margin_extra;
                    int starty = cell->GetY(doc) + l * h + 1 + cell->ycenteroff + g_margin_extra;
                    DrawRectangle(dc, color, startx, starty, 2, h - 2);
                    HintIMELocation(doc, startx, starty, h - 2, stylebits);
                    break;
                }

                if (!len) break;
            }
        }
    }

    void ExpandToWord(Selection &s) {
        if (!wxIsalnum(t[s.cursor])) return;
        while (s.cursor > 0 && wxIsalnum(t[s.cursor - 1])) s.cursor--;
        while (s.cursorend < static_cast<int>(t.Len()) && wxIsalnum(t[s.cursorend])) s.cursorend++;
    }

    void SelectWord(Selection &s) {
        if (s.cursor >= static_cast<int>(t.Len())) return;
        s.cursorend = s.cursor + 1;
        ExpandToWord(s);
    }

    void SelectWordBefore(Selection &s) {
        if (s.cursor <= 1) return;
        s.cursorend = s.cursor--;
        ExpandToWord(s);
    }

    bool RangeSelRemove(Selection &s) {
        WasEdited();
        if (s.cursor != s.cursorend) {
            t.Remove(s.cursor, s.cursorend - s.cursor);
            s.cursorend = s.cursor;
            return true;
        }
        return false;
    }

    void SetRelSize(Selection &s) {
        if (t.Len() || !cell->parent) return;
        int dd[] = {0, 1, 1, 0, 0, -1, -1, 0};
        for (auto i = 0; i < 4; i++) {
            auto x = max(0, min(s.x + dd[i * 2], s.grid->xs - 1));
            auto y = max(0, min(s.y + dd[i * 2 + 1], s.grid->ys - 1));
            auto c = s.grid->C(x, y);
            if (c->text.t.Len()) {
                relsize = c->text.relsize;
                break;
            }
        }
    }

    auto Insert(Document *doc, const auto &ins, Selection &s, bool keeprelsize) {
        auto prevl = t.Len();
        if (!s.TextEdit()) Clear(doc, s);
        RangeSelRemove(s);
        if (!prevl && !keeprelsize) SetRelSize(s);
        t.insert(s.cursor, ins);
        s.cursor = s.cursorend = s.cursor + static_cast<int>(ins.Len());
    }
    void Key(Document *doc, int k, Selection &s) {
        wxString ins;
        ins += k;
        Insert(doc, ins, s, false);
    }

    void Delete(Selection &s) {
        if (!RangeSelRemove(s))
            if (s.cursor < static_cast<int>(t.Len())) { t.Remove(s.cursor, 1); };
    }
    void Backspace(Selection &s) {
        if (!RangeSelRemove(s))
            if (s.cursor > 0) {
                t.Remove(--s.cursor, 1);
                --s.cursorend;
            };
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
                // does this need WasEdited()?
                i += j;
                t.Remove(i, sys->searchstring.Len());
                t.insert(i, str);
                i += str.Len();
            }
        } else {
            auto lowert = t.Lower();
            for (auto i = 0, j = 0; (j = lowert.Mid(i).Find(sys->searchstring)) >= 0;) {
                // does this need WasEdited()?
                i += j;
                lowert.Remove(i, sys->searchstring.Len());
                t.Remove(i, sys->searchstring.Len());
                lowert.insert(i, lstr);
                t.insert(i, str);
                i += str.Len();
            }
        }
    }

    void Clear(Document *doc, Selection &s) {
        t.Clear();
        s.EnterEdit(doc);
    }

    void HomeEnd(Selection &s, bool home) {
        auto i = 0;
        auto cw = cell->ColWidth();
        auto findwhere = home ? s.cursor : s.cursorend;
        for (;;) {
            auto start = i;
            auto curl = GetLine(i, cw);
            if (!curl.Len()) break;
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
        dos.Write32(image ? image->savedindex : -1);
        dos.Write32(stylebits);
        wxLongLong le = lastedit.GetValue();
        dos.Write64(&le, 1);
    }

    void Load(wxDataInputStream &dis) {
        t = dis.ReadString();

        // if (t.length() > 10000)
        //    printf("");

        if (sys->versionlastloaded <= 11) dis.Read32();  // numlines

        relsize = dis.Read32();

        int i = dis.Read32();
        image = i >= 0 ? sys->imagelist[sys->loadimageids[i]].get() : nullptr;

        if (sys->versionlastloaded >= 7) stylebits = dis.Read32();

        wxLongLong time;
        if (sys->versionlastloaded >= 14) {
            dis.Read64(&time, 1);
        } else {
            time = sys->fakelasteditonload--;
        }
        lastedit = wxDateTime(time);
    }

    auto Eval(auto &ev) const {
        switch (cell->celltype) {
            // Load variable's data.
            case CT_VARU: {
                auto v = ev.Lookup(t);
                if (!v) {
                    v = cell->Clone(nullptr);
                    v->celltype = CT_DATA;
                    v->text.t = "**Variable Load Error**";
                }
                return v;
            }

            // Return our current data.
            case CT_DATA: return cell->Clone(nullptr);

            default: return unique_ptr<Cell>();
        }
    }
};
