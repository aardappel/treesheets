/* The evaluation types for a cell.
CT_DATA: "Data"
CT_CODE: "Operation"
CT_VARD: "Variable Assign"
CT_VARU: "Variable Read"
CT_VIEWH: "Horizontal View"
CT_VIEWV: "Vertical View"
*/
enum { CT_DATA = 0, CT_CODE, CT_VARD, CT_VIEWH, CT_VARU, CT_VIEWV };

/* The drawstyles for a cell:

*/
enum { DS_GRID, DS_BLOBSHIER, DS_BLOBLINE };

/**
    The Cell structure represents the editable cells in the sheet.

    They are mutable structures containing a text and grid object. Along with
    formatting information.
*/
struct Cell {
    Cell *parent;
    int sx {0};
    int sy {0};
    int ox {0};
    int oy {0};
    int minx {0};
    int miny {0};
    int ycenteroff {0};
    int txs {0};
    int tys {0};
    int celltype;
    Text text;
    Grid *grid;
    uint cellcolor {g_cellcolor_default};
    uint actualcellcolor {g_cellcolor_default};
    uint textcolor {g_textcolor_default};
    bool tiny {false};
    bool verticaltextandgrid {true};
    wxUint8 drawstyle {DS_GRID};
    wxString note;

    Cell(Cell *_p = nullptr, const Cell *_clonefrom = nullptr, int _ct = CT_DATA,
         Grid *_g = nullptr)
        : parent(_p), celltype(_ct), grid(_g) {
        text.cell = this;
        if (_g) _g->cell = this;
        if (_p) {
            text.relsize = _p->text.relsize;
            verticaltextandgrid = _p->verticaltextandgrid;
        }
        if (_clonefrom) CloneStyleFrom(_clonefrom);
    }

    ~Cell() { DELETEP(grid); }
    void Clear() {
        DELETEP(grid);
        text.t.Clear();
        text.image = nullptr;
        Reset();
    }

    bool HasText() const { return !text.t.empty(); }
    bool HasTextSize() const { return HasText() || text.relsize; }
    bool HasTextState() const { return HasTextSize() || text.image; }
    bool HasHeader() const { return HasText() || text.image; }
    bool HasContent() const { return HasHeader() || grid; }
    bool GridShown(Document *doc) const {
        return grid && (!grid->folded || this == doc->currentdrawroot);
    }
    int MinRelsize()  // the smallest relsize is actually the biggest text
    {
        int rs = INT_MAX;
        if (grid) {
            rs = grid->MinRelsize(rs);
        } else if (HasText()) {
            // the "else" causes oversized titles but a readable grid when you zoom, if only
            // the grid has been shrunk
            rs = text.MinRelsize(rs);
        }
        return rs;
    }

    size_t EstimatedMemoryUse() {
        return sizeof(Cell) + text.EstimatedMemoryUse() + (grid ? grid->EstimatedMemoryUse() : 0);
    }

    void Layout(Document *doc, wxDC &dc, int depth, int maxcolwidth, bool forcetiny) {
        tiny = text.filtered && !grid || forcetiny ||
               doc->PickFont(dc, depth, text.relsize, text.stylebits);
        int ixs = 0, iys = 0;
        if (!tiny) sys->ImageSize(text.DisplayImage(), ixs, iys);
        int leftoffset = 0;
        if (!HasText()) {
            if (!ixs || !iys) {
                sx = sy = tiny ? 1 : dc.GetCharHeight();
            } else {
                leftoffset = dc.GetCharHeight();
            }
        } else {
            text.TextSize(dc, sx, sy, tiny, leftoffset, maxcolwidth);
        }
        if (ixs && iys) {
            sx += ixs + 2;
            sy = max(iys + 2, sy);
        }
        text.extent = sx + depth * dc.GetCharHeight();
        txs = sx;
        tys = sy;
        if (GridShown(doc)) {
            if (HasHeader()) {
                if (verticaltextandgrid) {
                    int osx = sx;
                    if (drawstyle == DS_BLOBLINE && !tiny) sy += 4;
                    grid->Layout(doc, dc, depth, sx, sy, leftoffset, sy, tiny || forcetiny);
                    sx = max(sx, osx);
                } else {
                    int osy = sy;
                    if (drawstyle == DS_BLOBLINE && !tiny) sx += 18;
                    grid->Layout(doc, dc, depth, sx, sy, sx, 0, tiny || forcetiny);
                    sy = max(sy, osy);
                }
            } else
                tiny = grid->Layout(doc, dc, depth, sx, sy, 0, 0, forcetiny);
        }
        ycenteroff = !verticaltextandgrid ? (sy - tys) / 2 : 0;
        if (!tiny) {
            sx += g_margin_extra * 2;
            sy += g_margin_extra * 2;
        }
    }

    void Render(Document *doc, int bx, int by, wxDC &dc, int depth, int ml, int mr, int mt, int mb,
                int maxcolwidth, int cell_margin) {
        // Choose color from celltype (program operations)
        switch (celltype) {
            case CT_VARD: actualcellcolor = 0xFF8080; break;
            case CT_VARU: actualcellcolor = 0xFFA0A0; break;
            case CT_VIEWH:
            case CT_VIEWV: actualcellcolor = 0x80FF80; break;
            case CT_CODE: actualcellcolor = 0x8080FF; break;
            default: actualcellcolor = cellcolor; break;
        }
        uint parentcolor = doc->Background();
        if (parent && this != doc->currentdrawroot) {
            Cell *p = parent;
            while (p && p->drawstyle == DS_BLOBLINE)
                p = p == doc->currentdrawroot ? nullptr : p->parent;
            if (p) parentcolor = p->actualcellcolor;
        }

        if (sys->darkennonmatchingcells && !text.IsInSearch()) {
            auto cp = (uchar *)&actualcellcolor;
            loop(i, 4) cp[i] = cp[i] * 800 / 1000;
        }

        if (drawstyle == DS_GRID && actualcellcolor != parentcolor) {
            DrawRectangle(dc, actualcellcolor, bx - ml, by - mt, sx + ml + mr, sy + mt + mb);
        }
        if (drawstyle != DS_GRID && HasContent() && !tiny) {
            if (actualcellcolor == parentcolor) {
                auto cp = (uchar *)&actualcellcolor;
                loop(i, 4) cp[i] = cp[i] * 850 / 1000;
            }
            dc.SetBrush(wxBrush(LightColor(actualcellcolor)));
            dc.SetPen(wxPen(LightColor(actualcellcolor)));

            if (drawstyle == DS_BLOBSHIER)
                dc.DrawRoundedRectangle(bx - cell_margin, by - cell_margin, minx + cell_margin * 2,
                                        miny + cell_margin * 2, sys->roundness);
            else if (HasHeader())
                dc.DrawRoundedRectangle(bx - cell_margin + g_margin_extra / 2,
                                        by - cell_margin + ycenteroff + g_margin_extra / 2,
                                        txs + cell_margin * 2 + g_margin_extra,
                                        tys + cell_margin * 2 + g_margin_extra, sys->roundness);
            // FIXME: this half a g_margin_extra is a bit of hack
        }
        dc.SetTextBackground(wxColour(LightColor(actualcellcolor)));
        int xoff = verticaltextandgrid ? 0 : text.extent - depth * dc.GetCharHeight();
        int yoff = text.Render(doc, bx, by + ycenteroff, depth, dc, xoff, maxcolwidth);
        yoff = verticaltextandgrid ? yoff : 0;
        if (GridShown(doc)) grid->Render(doc, bx, by, dc, depth, sx - xoff, sy - yoff, xoff, yoff);

        if (!note.IsEmpty() && !tiny && this != doc->currentdrawroot) {
            wxPoint points[3];
            int size = 6;
            int right = bx + sx + mr;
            int top = by - mt;
            points[0] = wxPoint(right, top);
            points[1] = wxPoint(right, top + size);
            points[2] = wxPoint(right - size, top);
            dc.SetBrush(*wxBLACK_BRUSH);
            dc.SetPen(*wxBLACK_PEN);
            dc.DrawPolygon(3, points);
        }
    }

    void CloneStyleFrom(Cell const *o) {
        cellcolor = o->cellcolor;
        textcolor = o->textcolor;
        verticaltextandgrid = o->verticaltextandgrid;
        drawstyle = o->drawstyle;
        text.stylebits = o->text.stylebits;
        note = o->note;
    }

    unique_ptr<Cell> Clone(Cell *_parent) const {
        auto c = make_unique<Cell>(_parent, this, celltype,
                                   grid ? new Grid(grid->xs, grid->ys) : nullptr);
        c->text = text;
        c->text.cell = c.get();
        c->note = note;
        if (grid) { grid->Clone(c->grid); }
        return c;
    }

    bool IsInside(int x, int y) const { return x >= 0 && y >= 0 && x < sx && y < sy; }
    int GetX(Document *doc) const { return ox + (parent ? parent->GetX(doc) : doc->hierarchysize); }
    int GetY(Document *doc) const { return oy + (parent ? parent->GetY(doc) : doc->hierarchysize); }
    int Depth() const { return parent ? parent->Depth() + 1 : 0; }
    Cell *Parent(int i) { return i ? parent->Parent(i - 1) : this; }
    Cell *SetParent(Cell *g) {
        parent = g;
        return this;
    }
    bool IsParentOf(const Cell *c) {
        return c->parent == this || (c->parent && IsParentOf(c->parent));
    }

    uint SwapColor(uint c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c & 0xFF0000) >> 16); }
    wxString ToText(int indent, const Selection &sel, int format, Document *doc, bool inheritstyle,
                    Cell *root) {
        wxString str = text.ToText(indent, sel, format);
        if ((format == A_EXPHTMLT || format == A_EXPHTMLTI || format == A_EXPHTMLTE) &&
            (text.stylebits & (STYLE_UNDERLINE | STYLE_STRIKETHRU)) && this != root &&
            !str.IsEmpty()) {
            wxString spanstyle = L"text-decoration:";
            spanstyle += (text.stylebits & STYLE_UNDERLINE) ? L" underline" : wxEmptyString;
            spanstyle += (text.stylebits & STYLE_STRIKETHRU) ? L" line-through" : wxEmptyString;
            spanstyle += L";";
            str.Prepend(L"<span style=\"" + spanstyle + L"\">");
            str.Append(L"</span>");
        }
        if (format == A_EXPCSV) {
            if (grid) return grid->ToText(indent, sel, format, doc, inheritstyle, root);
            str.Replace(L"\"", L"\"\"");
            return L"\"" + str + L"\"";
        }
        if (sel.cursor != sel.cursorend) return str;
        str.Append(LINE_SEPARATOR);
        if (grid) str.Append(grid->ToText(indent, sel, format, doc, inheritstyle, root));
        if (format == A_EXPXML) {
            str.Prepend(L">");
            if (text.relsize) {
                str.Prepend(L"\"");
                str.Prepend(wxString() << -text.relsize);
                str.Prepend(L" relsize=\"");
            }
            if (text.stylebits) {
                str.Prepend(L"\"");
                str.Prepend(wxString() << text.stylebits);
                str.Prepend(L" stylebits=\"");
            }
            if (cellcolor != 0xFFFFFF) {
                str.Prepend(L"\"");
                str.Prepend(wxString::Format(L"0x%06X", cellcolor));
                str.Prepend(L" colorbg=\"");
            }
            if (textcolor != 0x000000) {
                str.Prepend(L"\"");
                str.Prepend(wxString::Format(L"0x%06X", textcolor));
                str.Prepend(L" colorfg=\"");
            }
            if (celltype != CT_DATA) {
                str.Prepend(L"\"");
                str.Prepend(wxString() << celltype);
                str.Prepend(L" type=\"");
            }
            str.Prepend(L"<cell");
            str.Append(L' ', indent);
            str.Append(L"</cell>\n");
        } else if ((format == A_EXPHTMLT || format == A_EXPHTMLTI || format == A_EXPHTMLTE) &&
                   this != root) {
            wxString style;
            if (!inheritstyle || !parent ||
                (text.stylebits & STYLE_BOLD) != (parent->text.stylebits & STYLE_BOLD))
                style +=
                    text.stylebits & STYLE_BOLD ? L"font-weight: bold;" : L"font-weight: normal;";
            if (!inheritstyle || !parent ||
                (text.stylebits & STYLE_ITALIC) != (parent->text.stylebits & STYLE_ITALIC))
                style +=
                    text.stylebits & STYLE_ITALIC ? L"font-style: italic;" : L"font-style: normal;";
            if (!inheritstyle || !parent ||
                (text.stylebits & STYLE_FIXED) != (parent->text.stylebits & STYLE_FIXED))
                style += text.stylebits & STYLE_FIXED ? L"font-family: monospace;"
                                                      : L"font-family: sans-serif;";
            if (!inheritstyle || cellcolor != (parent ? parent->cellcolor : doc->Background()))
                style += wxString::Format(L"background-color: #%06X;", SwapColor(cellcolor));
            auto exporttextcolor = IsTag(doc) ? doc->tags[text.t] : textcolor;
            auto parenttextcolor =
                parent ? parent->IsTag(doc) ? doc->tags[parent->text.t] : parent->textcolor
                       : 0x000000;
            if (!inheritstyle || exporttextcolor != parenttextcolor)
                style += wxString::Format(L"color: #%06X;", SwapColor(exporttextcolor));
            str.Prepend(style.IsEmpty() ? wxString(L"<td>")
                                        : wxString(L"<td style=\"") + style + wxString(L"\">"));
            str.Append(L' ', indent);
            str.Append(L"</td>\n");
        } else if (format == A_EXPHTMLB && (text.t.Len() || grid) && this != root) {
            str.Prepend(L"<li>");
            str.Append(L' ', indent);
            str.Append(L"</li>\n");
        } else if (format == A_EXPHTMLO && text.t.Len()) {
            wxString h = wxString(L"h") + wxChar(L'0' + indent / 2) + L">";
            str.Prepend(L"<" + h);
            str.Append(L' ', indent);
            str.Append(L"</" + h + L"\n");
        }
        str.Pad(indent, L' ', false);
        return str;
    }

    void RelSize(int dir, int zoomdepth) {
        text.RelSize(dir, zoomdepth);
        if (grid) grid->RelSize(dir, zoomdepth);
    }

    void Reset() { ox = oy = sx = sy = minx = miny = ycenteroff = 0; }
    void ResetChildren() {
        Reset();
        if (grid) grid->ResetChildren();
    }

    void ResetLayout() {
        Reset();
        if (parent) parent->ResetLayout();
    }

    void LazyLayout(Document *doc, wxDC &dc, int depth, int maxcolwidth, bool forcetiny) {
        if (sx == 0) {
            Layout(doc, dc, depth, maxcolwidth, forcetiny);
            minx = sx;
            miny = sy;
        } else {
            sx = minx;
            sy = miny;
        }
    }

    void AddUndo(Document *doc) {
        ResetLayout();
        doc->AddUndo(this);
    }

    void Save(wxDataOutputStream &dos, Cell *ocs) const {
        dos.Write8(celltype);
        dos.Write32(cellcolor);
        dos.Write32(textcolor);
        dos.Write8(drawstyle);
        dos.WriteString(note);
        uint cellflags = this == ocs ? TS_SELECTION_MASK : 0;
        if (HasTextState()) {
            cellflags |= grid ? TS_BOTH : TS_TEXT;
            dos.Write8(cellflags);
            text.Save(dos);
            if (grid) grid->Save(dos, ocs);
        } else if (grid) {
            cellflags |= TS_GRID;
            dos.Write8(cellflags);
            grid->Save(dos, ocs);
        } else {
            cellflags |= TS_NEITHER;
            dos.Write8(cellflags);
        }
    }

    Grid *AddGrid(int x = 1, int y = 1) {
        if (!grid) {
            grid = new Grid(x, y, this);
            grid->InitCells(this);
            if (parent) grid->CloneStyleFrom(parent->grid);
        }
        return grid;
    }

    Cell *LoadGrid(wxDataInputStream &dis, int &numcells, int &textbytes, Cell *&ics) {
        int xs = dis.Read32();
        auto g = new Grid(xs, dis.Read32());
        grid = g;
        g->cell = this;
        if (!g->LoadContents(dis, numcells, textbytes, ics)) return nullptr;
        return this;
    }

    static Cell *LoadWhich(wxDataInputStream &dis, Cell *_p, int &numcells, int &textbytes, Cell *&ics) {
        auto c = new Cell(_p, nullptr, dis.Read8());
        numcells++;
        if (sys->versionlastloaded >= 8) {
            c->cellcolor = dis.Read32() & 0xFFFFFF;
            c->textcolor = dis.Read32() & 0xFFFFFF;
        }
        if (sys->versionlastloaded >= 15) c->drawstyle = dis.Read8();
        if (sys->versionlastloaded >= 25) c->note = dis.ReadString();
        int ts = dis.Read8();
        if (ts & TS_SELECTION_MASK) {
            ics = c;
            ts &= ~TS_SELECTION_MASK;
        }
        switch (ts) {
            case TS_BOTH:
            case TS_TEXT:
                c->text.Load(dis);
                textbytes += c->text.t.Len();
                if (ts == TS_TEXT) return c;
            case TS_GRID: return c->LoadGrid(dis, numcells, textbytes, ics);
            case TS_NEITHER: return c;
            default: return nullptr;
        }
    }

    unique_ptr<Cell> Eval(auto &ev) const {
        // Evaluates the internal grid if it exists, otherwise, evaluate the text.
        return grid ? grid->Eval(ev) : text.Eval(ev);
    }

    void Paste(Document *document, const Cell *original, Selection &selection) {
        parent->AddUndo(document);
        ResetLayout();
        if (!HasText() || !selection.TextEdit()) {
            note = original->note;
        }
        if (original->HasText()) {
            if (!HasText() || !selection.TextEdit()) {
                cellcolor = original->cellcolor;
                textcolor = original->textcolor;
                text.stylebits = original->text.stylebits;
            }
            text.Insert(document, original->text.t, selection, false);
        }
        if (original->text.image) text.image = original->text.image;
        if (original->grid) {
            auto gridclone = new Grid(original->grid->xs, original->grid->ys);
            gridclone->cell = this;
            original->grid->Clone(gridclone);
            // Note: deleting grid may invalidate c if its a child of grid, so clear it.
            original = nullptr;
            DELETEP(grid);  // FIXME: could merge instead?
            grid = gridclone;
            if (!HasText())
                grid->MergeWithParent(parent->grid, selection, document);  // deletes grid/this.
        }
    }

    Cell *FindNextSearchMatch(const wxString &s, Cell *best, Cell *selected, bool &lastwasselected,
                              bool reverse) {
        if (reverse && grid)
            best = grid->FindNextSearchMatch(s, best, selected, lastwasselected, reverse);
        if ((sys->casesensitivesearch ? text.t.Find(s) : text.t.Lower().Find(s)) >= 0) {
            if (lastwasselected) best = this;
            lastwasselected = false;
        }
        if (selected == this) lastwasselected = true;
        if (!reverse && grid)
            best = grid->FindNextSearchMatch(s, best, selected, lastwasselected, reverse);
        return best;
    }

    Cell *FindNextFilterMatch(Cell *best, Cell *selected, bool &lastwasselected) {
        if (!text.filtered) {
            if (lastwasselected) best = this;
            lastwasselected = false;
        }
        if (selected == this) lastwasselected = true;
        if (grid) best = grid->FindNextFilterMatch(best, selected, lastwasselected);
        return best;
    }

    Cell *FindLink(const Selection &sel, Cell *link, Cell *best, bool &lastthis, bool &stylematch,
                   bool forward, bool image) {
        if (grid) best = grid->FindLink(sel, link, best, lastthis, stylematch, forward, image);
        if (link == this) {
            lastthis = true;
            return best;
        }
        if (image ? link->text.image == text.image
                  : link->text.ToText(0, sel, A_EXPTEXT) == text.t) {
            if (link->text.stylebits != text.stylebits || link->cellcolor != cellcolor ||
                link->textcolor != textcolor) {
                if (!stylematch) best = nullptr;
                stylematch = true;
            } else if (stylematch) {
                return best;
            }
            if (!best || lastthis) {
                lastthis = false;
                return this;
            }
        }
        return best;
    }

    void FindReplaceAll(const wxString &s, const wxString &ls) {
        if (grid) grid->FindReplaceAll(s, ls);
        text.ReplaceStr(s, ls);
    }

    Cell *FindExact(const wxString &s) {
        return text.t == s ? this : (grid ? grid->FindExact(s) : nullptr);
    }

    void ImageRefCount(bool includefolded) {
        if (grid) grid->ImageRefCount(includefolded);
        if (text.image) text.image->trefc++;
    }

    void SetBorder(int width) {
        if (grid) grid->user_grid_outer_spacing = width;
    }

    void ColorChange(Document *doc, int which, uint color) {
        switch (which) {
            case A_CELLCOLOR: cellcolor = color; break;
            case A_TEXTCOLOR:
                if (IsTag(doc)) {
                    doc->tags[text.t] = color;
                } else {
                    textcolor = color;
                }
                break;
            case A_BORDCOLOR:
                if (parent && parent->grid) parent->grid->bordercolor = color;
                break;
        }
        text.WasEdited();
    }

    void SetGridTextLayout(int ds, bool vert, bool noset) {
        if (!noset) verticaltextandgrid = vert;
        if (ds != -1) drawstyle = ds;
        if (grid) grid->SetGridTextLayout(ds, vert, noset, grid->SelectAll());
    }

    bool IsTag(Document *doc) { return doc->tags.contains(text.t); }
    void MaxDepthLeaves(int curdepth, int &maxdepth, int &leaves) {
        if (curdepth > maxdepth) maxdepth = curdepth;
        if (grid)
            grid->MaxDepthLeaves(curdepth + 1, maxdepth, leaves);
        else
            leaves++;
    }

    int ColWidth() {
        return parent ? parent->grid->colwidths[parent->grid->FindCell(this).x]
                      : sys->defaultmaxcolwidth;
    }

    void CollectCells(auto &itercells, bool recurse = true) {
        itercells.push_back(this);
        if (grid && recurse) grid->CollectCells(itercells);
    }

    Cell *Graph() {
        auto n = text.GetNum();
        text.t.Clear();
        text.t.Append(L'|', n);
        return this;
    }
};
