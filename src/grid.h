
struct Grid {
    // owning cell.
    Cell *cell;
    // subcells
    Cell **cells;
    // widths for each column
    int *colwidths;
    // xsize, ysize
    int xs, ys;
    int view_margin, view_grid_outer_spacing, user_grid_outer_spacing, cell_margin;
    int bordercolor;
    bool horiz;
    bool tinyborder;
    bool folded;

    Cell *&C(int x, int y) const {
        ASSERT(x >= 0 && y >= 0 && x < xs && y < ys);
        return cells[x + y * xs];
    }

    #define foreachcell(c)                \
        for (int y = 0; y < ys; y++)      \
            for (int x = 0; x < xs; x++)  \
                for (bool _f = true; _f;) \
                    for (Cell *&c = C(x, y); _f; _f = false)
    #define foreachcellrev(c)                 \
        for (int y = ys - 1; y >= 0; y--)     \
            for (int x = xs - 1; x >= 0; x--) \
                for (bool _f = true; _f;)     \
                    for (Cell *&c = C(x, y); _f; _f = false)
    #define foreachcelly(c)           \
        for (int y = 0; y < ys; y++)  \
            for (bool _f = true; _f;) \
                for (Cell *&c = C(0, y); _f; _f = false)
    #define foreachcellcolumn(c)          \
        for (int x = 0; x < xs; x++)      \
            for (int y = 0; y < ys; y++)  \
                for (bool _f = true; _f;) \
                    for (Cell *&c = C(x, y); _f; _f = false)
    #define foreachcellinsel(c, s)                 \
        for (int y = s.y; y < s.y + s.ys; y++)     \
            for (int x = s.x; x < s.x + s.xs; x++) \
                for (bool _f = true; _f;)          \
                    for (Cell *&c = C(x, y); _f; _f = false)
    #define foreachcellinselrev(c, s)                   \
        for (int y = s.y + s.ys - 1; y >= s.y; y--)     \
            for (int x = s.x + s.xs - 1; x >= s.x; x--) \
                for (bool _f = true; _f;)               \
                    for (Cell *&c = C(x, y); _f; _f = false)
    #define foreachcellingrid(c, g)         \
        for (int y = 0; y < g->ys; y++)     \
            for (int x = 0; x < g->xs; x++) \
                for (bool _f = true; _f;)   \
                    for (Cell *&c = g->C(x, y); _f; _f = false)

    Grid(int _xs, int _ys, Cell *_c = nullptr)
        : cell(_c),
          xs(_xs),
          ys(_ys),
          cells(new Cell *[_xs * _ys]),
          colwidths(nullptr),
          user_grid_outer_spacing(3),
          bordercolor(0xA0A0A0),
          horiz(false),
          folded(false) {
        foreachcell(c) c = nullptr;
        InitColWidths();
        SetOrient();
    }

    ~Grid() {
        foreachcell(c) if (c) delete c;
        delete[] cells;
        delete[] colwidths;
    }

    void InitCells(Cell *clonestylefrom = nullptr) {
        foreachcell(c) c = new Cell(cell, clonestylefrom);
    }
    void CloneStyleFrom(Grid *o) {
        bordercolor = o->bordercolor;
        // TODO: what others?
    }

    void InitColWidths() {
        if (colwidths) delete[] colwidths;
        colwidths = new int[xs];
        int cw = cell ? cell->ColWidth() : sys->defaultmaxcolwidth;
        loop(x, xs) colwidths[x] = cw;
    }

    /* Clones g into this grid. This mutates the grid this function is called on. */
    void Clone(Grid *g) {
        g->bordercolor = bordercolor;
        g->user_grid_outer_spacing = user_grid_outer_spacing;
        g->folded = folded;
        foreachcell(c) g->C(x, y) = c->Clone(g->cell).release();
        loop(x, xs) g->colwidths[x] = colwidths[x];
    }

    unique_ptr<Cell> CloneSel(const Selection &s) {
        auto cl = make_unique<Cell>(nullptr, s.g->cell, CT_DATA, new Grid(s.xs, s.ys));
        foreachcellinsel(c, s) cl->grid->C(x - s.x, y - s.y) = c->Clone(cl.get()).release();
        return cl;
    }

    size_t EstimatedMemoryUse() {
        size_t sum = 0;
        foreachcell(c) sum += c->EstimatedMemoryUse();
        return sizeof(Grid) + xs * ys * sizeof(Cell *) + sum;
    }

    void SetOrient() {
        if (xs > ys) horiz = true;
        if (ys > xs) horiz = false;
    }

    bool Layout(Document *doc, wxDC &dc, int depth, int &sx, int &sy, int startx, int starty,
                bool forcetiny) {
        int *xa = new int[xs];
        int *ya = new int[ys];
        loop(i, xs) xa[i] = 0;
        loop(i, ys) ya[i] = 0;
        tinyborder = true;
        foreachcell(c) {
            c->LazyLayout(doc, dc, depth + 1, colwidths[x], forcetiny);
            tinyborder = c->tiny && tinyborder;
            xa[x] = max(xa[x], c->sx);
            ya[y] = max(ya[y], c->sy);
        }
        view_grid_outer_spacing =
            tinyborder || cell->drawstyle != DS_GRID ? 0 : user_grid_outer_spacing;
        view_margin = tinyborder || cell->drawstyle != DS_GRID ? 0 : g_grid_margin;
        cell_margin = tinyborder ? 0 : (cell->drawstyle == DS_GRID ? 0 : g_cell_margin);
        sx = (xs + 1) * g_line_width + xs * cell_margin * 2 +
             2 * (view_grid_outer_spacing + view_margin) + startx;
        sy = (ys + 1) * g_line_width + ys * cell_margin * 2 +
             2 * (view_grid_outer_spacing + view_margin) + starty;
        loop(i, xs) sx += xa[i];
        loop(i, ys) sy += ya[i];
        int cx = view_grid_outer_spacing + view_margin + g_line_width + cell_margin + startx;
        int cy = view_grid_outer_spacing + view_margin + g_line_width + cell_margin + starty;
        if (!cell->tiny) {
            cx += g_margin_extra;
            cy += g_margin_extra;
        }
        foreachcell(c) {
            c->ox = cx;
            c->oy = cy;
            if (c->drawstyle == DS_BLOBLINE && !c->grid) {
                assert(c->sy <= ya[y]);
                c->ycenteroff = (ya[y] - c->sy) / 2;
            }
            c->sx = xa[x];
            c->sy = ya[y];
            cx += xa[x] + g_line_width + cell_margin * 2;
            if (x == xs - 1) {
                cy += ya[y] + g_line_width + cell_margin * 2;
                cx = view_grid_outer_spacing + view_margin + g_line_width + cell_margin + startx;
                if (!cell->tiny) cx += g_margin_extra;
            }
        }
        delete[] xa;
        delete[] ya;
        return tinyborder;
    }

    void Render(Document *doc, int bx, int by, wxDC &dc, int depth, int sx, int sy, int xoff,
                int yoff) {
        xoff = C(0, 0)->ox - view_margin - view_grid_outer_spacing - 1;
        yoff = C(0, 0)->oy - view_margin - view_grid_outer_spacing - 1;
        int maxx = C(xs - 1, 0)->ox + C(xs - 1, 0)->sx;
        int maxy = C(0, ys - 1)->oy + C(0, ys - 1)->sy;
        if (tinyborder || cell->drawstyle == DS_GRID) {
            int ldelta = view_grid_outer_spacing != 0;
            auto drawlines = [&]() {
                for (int x = ldelta; x <= xs - ldelta; x++) {
                    int xl = (x == xs ? maxx : C(x, 0)->ox - g_line_width) + bx;
                    if (xl >= doc->originx && xl <= doc->maxx) loop(line, g_line_width) {
                            dc.DrawLine(
                                xl + line, max(doc->originy, by + yoff + view_grid_outer_spacing),
                                xl + line, min(doc->maxy, by + maxy + g_line_width) + view_margin);
                        }
                }
                for (int y = ldelta; y <= ys - ldelta; y++) {
                    int yl = (y == ys ? maxy : C(0, y)->oy - g_line_width) + by;
                    if (yl >= doc->originy && yl <= doc->maxy) loop(line, g_line_width) {
                            dc.DrawLine(max(doc->originx,
                                            bx + xoff + view_grid_outer_spacing + g_line_width),
                                        yl + line, min(doc->maxx, bx + maxx) + view_margin,
                                        yl + line);
                        }
                }
            };
            if (!sys->fastrender && view_grid_outer_spacing && cell->cellcolor != 0xFFFFFF) {
                dc.SetPen(*wxWHITE_PEN);
                drawlines();
            }
            // dotted lines result in very expensive drawline calls
            dc.SetPen(view_grid_outer_spacing && !sys->fastrender ? sys->pen_gridlines
                                                                  : sys->pen_tinygridlines);
            drawlines();
        }

        foreachcell(c) {
            int cx = bx + c->ox;
            int cy = by + c->oy;
            if (cx < doc->maxx && cx + c->sx > doc->originx && cy < doc->maxy &&
                cy + c->sy > doc->originy) {
                c->Render(doc, cx, cy, dc, depth + 1, (x == 0) * view_margin,
                          (x == xs - 1) * view_margin, (y == 0) * view_margin,
                          (y == ys - 1) * view_margin, colwidths[x], cell_margin);
            }
        }

        if (cell->drawstyle == DS_BLOBLINE && !tinyborder && cell->HasHeader() && !cell->tiny) {
            const int arcsize = 8;
            int srcy = by + cell->ycenteroff +
                       (cell->verticaltextandgrid ? cell->tys + 2 : cell->tys / 2) + g_margin_extra;
            // fixme: the 8 is chosen to fit the smallest text size, not very portable
            int srcx = bx + (cell->verticaltextandgrid ? 8 : cell->txs + 4) + g_margin_extra;
            int destyfirst = -1, destylast = -1;
            dc.SetPen(*wxGREY_PEN);
            foreachcelly(c) if (c->HasContent() && !c->tiny) {
                int desty = c->ycenteroff + by + c->oy + c->tys / 2 + g_margin_extra;
                int destx = bx + c->ox - 2 + g_margin_extra;
                bool visible = srcx < doc->maxx && destx > doc->originx &&
                               desty - arcsize < doc->maxy && desty + arcsize > doc->originy;
                if (abs(srcy - desty) < arcsize && !cell->verticaltextandgrid) {
                    if (destyfirst < 0) destyfirst = desty;
                    destylast = desty;
                    if (visible) dc.DrawLine(srcx, desty, destx, desty);
                } else {
                    if (desty < srcy) {
                        if (destyfirst < 0) destyfirst = desty + arcsize;
                        destylast = desty + arcsize;
                        if (visible) dc.DrawBitmap(sys->frame->line_nw, srcx, desty, true);
                    } else {
                        destylast = desty - arcsize;
                        if (visible)
                            dc.DrawBitmap(sys->frame->line_sw, srcx, desty - arcsize, true);
                        desty--;
                    }
                    if (visible) dc.DrawLine(srcx + arcsize, desty, destx, desty);
                }
            }
            if (cell->verticaltextandgrid) {
                if (destylast > 0) dc.DrawLine(srcx, srcy, srcx, destylast);
            } else {
                if (destyfirst >= 0 && destylast >= 0 && destyfirst < destylast) {
                    destyfirst = min(destyfirst, srcy);
                    destylast = max(destylast, srcy);
                    dc.DrawLine(srcx, destyfirst, srcx, destylast);
                }
            }
        }
        if (view_grid_outer_spacing && cell->drawstyle == DS_GRID) {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(wxColour(bordercolor)));
            loop(i, view_grid_outer_spacing - 1) {
                dc.DrawRoundedRectangle(
                    bx + xoff + view_grid_outer_spacing - i,
                    by + yoff + view_grid_outer_spacing - i,
                    maxx - xoff - view_grid_outer_spacing + 1 + i * 2 + view_margin,
                    maxy - yoff - view_grid_outer_spacing + 1 + i * 2 + view_margin,
                    sys->roundness + i);
            }
        }
    }

    void FindXY(Document *doc, int px, int py, wxDC &dc) {
        foreachcell(c) {
            int bx = px - c->ox;
            int by = py - c->oy;
            if (bx >= 0 && by >= -g_line_width - g_selmargin && bx < c->sx && by < g_selmargin) {
                doc->hover = Selection(this, x, y, 1, 0);
                return;
            }
            if (bx >= 0 && by >= c->sy - g_selmargin && bx < c->sx &&
                by < c->sy + g_line_width + g_selmargin) {
                doc->hover = Selection(this, x, y + 1, 1, 0);
                return;
            }
            if (bx >= -g_line_width - g_selmargin && by >= 0 && bx < g_selmargin && by < c->sy) {
                doc->hover = Selection(this, x, y, 0, 1);
                return;
            }
            if (bx >= c->sx - g_selmargin && by >= 0 && bx < c->sx + g_line_width + g_selmargin &&
                by < c->sy) {
                doc->hover = Selection(this, x + 1, y, 0, 1);
                return;
            }
            if (c->IsInside(bx, by)) {
                if (c->GridShown(doc)) c->grid->FindXY(doc, bx, by, dc);
                if (doc->hover.g) return;
                doc->hover = Selection(this, x, y, 1, 1);
                if (c->HasText()) {
                    c->text.FindCursor(doc, bx, by - c->ycenteroff, dc, doc->hover, colwidths[x]);
                }
                return;
            }
        }
    }

    Cell *FindLink(Selection &s, Cell *link, Cell *best, bool &lastthis, bool &stylematch,
                   bool forward) {
        if (forward) {
            foreachcell(c) best = c->FindLink(s, link, best, lastthis, stylematch, forward);
        } else {
            foreachcellrev(c) best = c->FindLink(s, link, best, lastthis, stylematch, forward);
        }
        return best;
    }

    Cell *FindNextSearchMatch(wxString &search, Cell *best, Cell *selected, bool &lastwasselected) {
        foreachcell(c) best = c->FindNextSearchMatch(search, best, selected, lastwasselected);
        return best;
    }

    Cell *FindNextFilterMatch(Cell *best, Cell *selected, bool &lastwasselected) {
        foreachcell(c) best = c->FindNextFilterMatch(best, selected, lastwasselected);
        return best;
    }

    void FindReplaceAll(const wxString &str) { foreachcell(c) c->FindReplaceAll(str); }
    void ReplaceCell(Cell *o, Cell *n) { foreachcell(c) if (c == o) c = n; }
    Selection FindCell(Cell *o) {
        foreachcell(c) if (c == o) return Selection(this, x, y, 1, 1);
        return Selection();
    }

    Selection SelectAll() { return Selection(this, 0, 0, xs, ys); }
    void ImageRefCount() { foreachcell(c) c->ImageRefCount(); }
    void DrawHover(Document *doc, wxDC &dc, Selection &s) {
        #ifndef SIMPLERENDER
        #ifdef __WXMAC__
        const uint thincol = 0xFFFFFF;
        const uint bgcol = 0xFFFFFF;
        #else
        const uint thincol = 0x555555;
        const uint bgcol = 0x101014;
        #endif
        dc.SetLogicalFunction(wxXOR);
        if (s.Thin()) {
            DrawInsert(doc, dc, s, thincol);
        } else {
            Cell *c = C(s.x, s.y);
            DrawRectangle(dc, bgcol, c->GetX(doc) - cell_margin, c->GetY(doc) - cell_margin,
                          c->sx + cell_margin * 2, c->sy + cell_margin * 2);
        }
        dc.SetLogicalFunction(wxCOPY);
        #endif
    }

    void DrawCursor(Document *doc, wxDC &dc, Selection &s, bool full, uint color, bool cursoronly) {
        Cell *c = s.GetCell();
        if (c && !c->tiny && (c->HasText() || !c->grid))
            c->text.DrawCursor(doc, dc, s, full, color, cursoronly, colwidths[s.x]);
    }

    void DrawInsert(Document *doc, wxDC &dc, Selection &s, uint colour) {
        dc.SetPen(sys->pen_thinselect);
        if (!s.xs) {
            Cell *c = C(s.x - (s.x == xs), s.y);
            int x = c->GetX(doc) + (c->sx + g_line_width + cell_margin) * (s.x == xs) -
                    g_line_width - cell_margin;
            loop(line, g_line_width)
                dc.DrawLine(x + line, max(cell->GetY(doc), doc->originy), x + line,
                            min(cell->GetY(doc) + cell->sy, doc->maxy));
            DrawRectangle(dc, colour, x - 1, c->GetY(doc), g_line_width + 2, c->sy);
        } else {
            Cell *c = C(s.x, s.y - (s.y == ys));
            int y = c->GetY(doc) + (c->sy + g_line_width + cell_margin) * (s.y == ys) -
                    g_line_width - cell_margin;
            loop(line, g_line_width)
                dc.DrawLine(max(cell->GetX(doc), doc->originx), y + line,
                            min(cell->GetX(doc) + cell->sx, doc->maxx), y + line);
            DrawRectangle(dc, colour, c->GetX(doc), y - 1, c->sx, g_line_width + 2);
        }
    }

    wxRect GetRect(Document *doc, Selection &s, bool minimal = false) {
        if (s.Thin()) {
            if (s.xs) {
                if (s.y < ys) {
                    Cell *tl = C(s.x, s.y);
                    return wxRect(tl->GetX(doc), tl->GetY(doc), tl->sx, 0);
                } else {
                    Cell *br = C(s.x, ys - 1);
                    return wxRect(br->GetX(doc), br->GetY(doc) + br->sy, br->sx, 0);
                }
            } else {
                if (s.x < xs) {
                    Cell *tl = C(s.x, s.y);
                    return wxRect(tl->GetX(doc), tl->GetY(doc), 0, tl->sy);
                } else {
                    Cell *br = C(xs - 1, s.y);
                    return wxRect(br->GetX(doc) + br->sx, br->GetY(doc), 0, br->sy);
                }
            }
        } else {
            Cell *tl = C(s.x, s.y);
            Cell *br = C(s.x + s.xs - 1, s.y + s.ys - 1);
            wxRect r(tl->GetX(doc) - cell_margin, tl->GetY(doc) - cell_margin,
                     br->GetX(doc) + br->sx - tl->GetX(doc) + cell_margin * 2,
                     br->GetY(doc) + br->sy - tl->GetY(doc) + cell_margin * 2);
            if (minimal && tl == br) r.width -= tl->sx - tl->minx;
            return r;
        }
    }

    void DrawSelect(Document *doc, wxDC &dc, Selection &s, bool cursoronly) {
        #ifndef SIMPLERENDER
        dc.SetLogicalFunction(wxINVERT);
        #endif
        if (s.Thin()) {
            if (!cursoronly) DrawInsert(doc, dc, s, 0);
        } else {
            if (!cursoronly) {
                dc.SetBrush(*wxBLACK_BRUSH);
                dc.SetPen(*wxBLACK_PEN);
                wxRect g = GetRect(doc, s);
                int lw = g_line_width;
                int te = s.TextEdit();
                dc.DrawRectangle(g.x - 1 - lw, g.y - 1 - lw, g.width + 2 + 2 * lw, 2 + lw - te);
                dc.DrawRectangle(g.x - 1 - lw, g.y - 1 + g.height + te, g.width + 2 + 2 * lw - 5,
                                 2 + lw - te);

                dc.DrawRectangle(g.x - 1 - lw, g.y + 1 - te, 2 + lw - te, g.height - 2 + 2 * te);
                dc.DrawRectangle(g.x - 1 + g.width + te, g.y + 1 - te, 2 + lw - te,
                                 g.height - 2 + 2 * te - 2 - te);

                dc.DrawRectangle(g.x + g.width, g.y + g.height - 2, lw + 2, lw + 4);
                dc.DrawRectangle(g.x + g.width - lw - 1, g.y + g.height - 2 + 2 * te, lw + 1,
                                 lw + 4 - 2 * te);
            }
            #ifndef SIMPLERENDER
            dc.SetLogicalFunction(wxXOR);
            #endif
            if (s.TextEdit())
            #ifdef SIMPLERENDER
                DrawCursor(doc, dc, s, true, 0x00FF00, cursoronly);
            #else
                DrawCursor(doc, dc, s, true, 0xFFFF, cursoronly);
            #endif
        }
        #ifndef SIMPLERENDER
        dc.SetLogicalFunction(wxCOPY);
        #endif
    }

    void DeleteCells(int dx, int dy, int nxs, int nys) {
        Cell **ncells = new Cell *[(xs + nxs) * (ys + nys)];
        Cell **ncp = ncells;
        int *ncw = new int[xs + nxs];
        int *ncwp = ncw;
        foreachcell(c) if (x == dx || y == dy) DELETEP(c) else *ncp++ = c;
        loop(x, xs) if (x != dx) *ncwp++ = colwidths[x];
        delete[] cells;
        cells = ncells;
        delete[] colwidths;
        colwidths = ncw;
        xs += nxs;
        ys += nys;
        SetOrient();
    }

    void MultiCellDelete(Document *doc, Selection &s) {
        cell->AddUndo(doc);
        MultiCellDeleteSub(doc, s);
        doc->Refresh();
    }

    void MultiCellDeleteSub(Document *doc, Selection &s) {
        foreachcellinsel(c, s) c->Clear();
        bool delhoriz = true, delvert = true;
        foreachcell(c) {
            if (c->HasContent()) {
                if (y >= s.y && y < s.y + s.ys) delhoriz = false;
                if (x >= s.x && x < s.x + s.xs) delvert = false;
            }
        }
        if (delhoriz && (!delvert || s.xs >= s.ys)) {
            if (s.ys == ys) {
                DelSelf(doc, s);
            } else {
                loop(i, s.ys) DeleteCells(-1, s.y, 0, -1);
                s.ys = 0;
                s.xs = 1;
            }
        } else if (delvert) {
            if (s.xs == xs) {
                DelSelf(doc, s);
            } else {
                loop(i, s.xs) DeleteCells(s.x, -1, -1, 0);
                s.xs = 0;
                s.ys = 1;
            }
        } else {
            Cell *c = s.GetCell();
            if (c) s.EnterEdit(doc);
        }
    }

    void DelSelf(Document *doc, Selection &s) {
        if (!doc->drawpath.empty() && doc->drawpath.last().g == this) {
            doc->drawpath.pop();
            doc->curdrawroot = doc->WalkPath(doc->drawpath);
        }
        if (!cell->parent) return;  // FIXME: deletion of root cell, what would be better?
        s = cell->parent->grid->FindCell(cell);
        Grid *&pthis = cell->grid;
        DELETEP(pthis);
    }

    void InsertCells(int dx, int dy, int nxs, int nys, Cell *nc = nullptr) {
        assert(((dx < 0) == (nxs == 0)) &&
               ((dy < 0) == (nys == 0)));
        assert(nxs + nys == 1);
        Cell **ocells = cells;
        cells = new Cell *[(xs + nxs) * (ys + nys)];
        int *ocw = colwidths;
        colwidths = new int[xs + nxs];
        xs += nxs;
        ys += nys;
        Cell **ncp = ocells;
        SetOrient();
        foreachcell(c) if (x == dx || y == dy) {
            if (nc)
                c = nc;
            else {
                Cell *colcell = ocells[(nxs ? min(dx, xs - nxs - 1) : x) +
                                       (nxs ? y : min(dy, ys - nys - 1)) * (xs - nxs)];
                c = new Cell(cell, colcell);
                c->text.relsize = colcell->text.relsize;
            }
        }
        else c = *ncp++;
        int *cwp = ocw;
        loop(x, xs) colwidths[x] = x == dx ? cell->ColWidth() : *cwp++;
        delete[] ocells;
        delete[] ocw;
    }

    void Save(wxDataOutputStream &dos) const {
        dos.Write32(xs);
        dos.Write32(ys);
        dos.Write32(bordercolor);
        dos.Write32(user_grid_outer_spacing);
        dos.Write8(cell->verticaltextandgrid);
        dos.Write8(folded);
        loop(x, xs) dos.Write32(colwidths[x]);
        foreachcell(c) c->Save(dos);
    }

    bool LoadContents(wxDataInputStream &dis, int &numcells, int &textbytes) {
        if (sys->versionlastloaded >= 10) {
            bordercolor = dis.Read32() & 0xFFFFFF;
            user_grid_outer_spacing = dis.Read32();
            if (sys->versionlastloaded >= 11) {
                cell->verticaltextandgrid = dis.Read8() != 0;
                if (sys->versionlastloaded >= 13) {
                    if (sys->versionlastloaded >= 16) {
                        folded = dis.Read8() != 0;
                        if (folded && sys->versionlastloaded <= 17) {
                            // Before v18, folding would use the image slot. So if this cell
                            // contains an image, clear it.
                            cell->text.image = nullptr;
                        }
                    }
                    loop(x, xs) colwidths[x] = dis.Read32();
                }
            }
        }
        foreachcell(c) if (!(c = Cell::LoadWhich(dis, cell, numcells, textbytes))) return false;
        return true;
    }

    void Formatter(wxString &r, int format, int indent, const wxChar *xml, const wxChar *html,
                   const wxChar *htmlb) {
        if (format == A_EXPXML) {
            r.Append(L' ', indent);
            r.Append(xml);
        } else if (format == A_EXPHTMLT) {
            r.Append(L' ', indent);
            r.Append(html);
        } else if (format == A_EXPHTMLB && *htmlb) {
            r.Append(L' ', indent);
            r.Append(htmlb);
        }
    }

    wxString ToText(int indent, const Selection &s, int format, Document *doc) {
        return ConvertToText(SelectAll(), indent + 2, format, doc);
    };

    wxString ConvertToText(const Selection &s, int indent, int format, Document *doc) {
        wxString r;
        const int root_grid_spacing = 2;  // Can't be adjusted in editor, so use a default.
        const int font_size = 14 - indent / 2;
        const int grid_border_width =
            cell == doc->rootgrid ? root_grid_spacing : user_grid_outer_spacing - 1;
        Formatter(r, format, indent,
                  L"<grid>\n",
                  wxString::Format(L"<table style=\"border-width: %dpt; font-size: %dpt;\">\n",
                      grid_border_width, font_size).wc_str(),
                  wxString::Format(L"<ul style=\"font-size: %dpt;\">\n",
                      font_size).wc_str());
        foreachcellinsel(c, s) {
            if (x == 0) Formatter(r, format, indent, L"<row>\n", L"<tr valign=top>\n", L"");
            r.Append(c->ToText(indent, s, format, doc));
            if (format == A_EXPCSV) r.Append(x == xs - 1 ? '\n' : ',');
            if (x == xs - 1) Formatter(r, format, indent, L"</row>\n", L"</tr>\n", L"");
        }
        Formatter(r, format, indent, L"</grid>\n", L"</table>\n", L"</ul>\n");
        return r;
    }

    void RelSize(int dir, int zoomdepth) { foreachcell(c) c->RelSize(dir, zoomdepth); }
    void RelSize(int dir, Selection &s, int zoomdepth) {
        foreachcellinsel(c, s) c->RelSize(dir, zoomdepth);
    }
    void SetBorder(int width, Selection &s) { foreachcellinsel(c, s) c->SetBorder(width); }
    int MinRelsize(int rs) {
        foreachcell(c) {
            int crs = c->MinRelsize();
            rs = min(rs, crs);
        }
        return rs;
    }

    void ResetChildren() {
        cell->Reset();
        foreachcell(c) c->ResetChildren();
    }

    void Move(int dx, int dy, Selection &s) {
        if (dx < 0 || dy < 0)
            foreachcellinsel(c, s) swap_(c, C((x + dx + xs) % xs, (y + dy + ys) % ys));
        else
            foreachcellinselrev(c, s) swap_(c, C((x + dx + xs) % xs, (y + dy + ys) % ys));
    }

    void Add(Cell *c) {
        if (horiz)
            InsertCells(xs, -1, 1, 0, c);
        else
            InsertCells(-1, ys, 0, 1, c);
        c->parent = cell;
    }

    void MergeWithParent(Grid *p, Selection &s) {
        cell->grid = nullptr;
        foreachcell(c) {
            if (x + s.x >= p->xs) p->InsertCells(p->xs, -1, 1, 0);
            if (y + s.y >= p->ys) p->InsertCells(-1, p->ys, 0, 1);
            Cell *pc = p->C(x + s.x, y + s.y);
            if (pc->HasContent()) {
                if (x) p->InsertCells(s.x + x, -1, 1, 0);
                pc = p->C(x + s.x, y + s.y);
                if (pc->HasContent()) {
                    if (y) p->InsertCells(-1, s.y + y, 0, 1);
                    pc = p->C(x + s.x, y + s.y);
                }
            }
            delete pc;
            p->C(x + s.x, y + s.y) = c;
            c->parent = p->cell;
            c = nullptr;
        }
        s.g = p;
        s.xs += xs - 1;
        s.ys += ys - 1;
        delete this;
    }

    void SetStyle(Document *doc, Selection &s, int sb) {
        cell->AddUndo(doc);
        cell->ResetChildren();
        foreachcellinsel(c, s) c->text.stylebits ^= sb;
        doc->Refresh();
    }

    void ColorChange(Document *doc, int which, uint color, Selection &s) {
        cell->AddUndo(doc);
        cell->ResetChildren();
        foreachcellinsel(c, s) c->ColorChange(which, color);
        doc->Refresh();
    }

    void ReplaceStr(Document *doc, const wxString &str, Selection &s) {
        cell->AddUndo(doc);
        cell->ResetChildren();
        foreachcellinsel(c, s) c->text.ReplaceStr(str);
        doc->Refresh();
    }

    void CSVImport(const wxArrayString &as, wxChar sep) {
        int cy = 0;
        loop(y, (int)as.size()) {
            wxString s = as[y];
            wxString word;
            for (int x = 0; s[0]; x++) {
                if (s[0] == '\"') {
                    word = L"";
                    for (int i = 1;; i++) {
                        if (!s[i]) {
                            if (y < (int)as.size() - 1) {
                                s = as[++y];
                                i = 0;
                            } else {
                                s = L"";
                                break;
                            }
                        } else if (s[i] == '\"') {
                            if (s[i + 1] == '\"')
                                word += s[++i];
                            else {
                                s = s.size() == i + 1 ? wxString(L"") : s.Mid(i + 2);
                                break;
                            }
                        } else
                            word += s[i];
                    }
                } else {
                    int pos = s.Find(sep);
                    if (pos < 0) {
                        word = s;
                        s = L"";
                    } else {
                        word = s.Left(pos);
                        s = s.Mid(pos + 1);
                    }
                }
                if (x >= xs) InsertCells(x, -1, 1, 0);
                Cell *c = C(x, cy);
                c->text.t = word;
            }
            cy++;
        }

        ys = cy;  // throws memory away, but doesn't matter
    }

    unique_ptr<Cell> EvalGridCell(Evaluator &ev, Cell *&c, unique_ptr<Cell> acc, int &x, int &y,
                                  bool &alldata, bool vert) {
        int ct = c->celltype;  // Type of subcell being evaluated
        // Update alldata condition (variable reads act like data)
        alldata = alldata && (ct == CT_DATA || ct == CT_VARU);
        ev.vert = vert;                  // Inform evaluatour of vert status. (?)
        switch (ct) {
            // Var assign
            case CT_VARD: {
                if (vert) return acc;  // (Reject vertical assignments)
                // If we have no data, lets see if we can generate something useful from the
                // subgrid.
                if (!acc->grid && acc->text.t.IsEmpty()) {
                    acc = c->Eval(ev);
                    if (!acc) { return nullptr; }
                }
                // Assign the current data temporary to the text
                ev.Assign(c, acc.get());
                // Pass the original data onwards
                return acc;
            }
            // View
            case CT_VIEWV:
            case CT_VIEWH:
                if (vert ? ct == CT_VIEWH : ct == CT_VIEWV) {
                    return c->Clone(nullptr);
                }
                delete c;
                c = acc ? acc->Clone(cell).release() : new Cell(cell);
                c->celltype = ct;
                return acc;
            // Operation
            case CT_CODE: {
                Operation *op = ev.FindOp(c->text.t);
                switch (op ? strlen(op->args) : -1) {
                    default:
                        return nullptr;
                    case 0:
                        return ev.Execute(op);
                    case 1:
                        return acc ? ev.Execute(op, move(acc)) : nullptr;
                    case 2:
                        if (vert) {
                            if (acc && y + 1 < ys) {
                                return ev.Execute(op, move(acc), C(x, ++y));
                            } else {
                                return nullptr;
                            }
                        } else {
                            if (acc && x + 1 < xs) {
                                return ev.Execute(op, move(acc), C(++x, y));
                            } else {
                                return nullptr;
                            }
                        }
                    case 3:
                        if (vert) {
                            if (acc && y + 2 < ys) {
                                y += 2;
                                return ev.Execute(op, move(acc), C(x, y - 1), C(x, y));
                            } else {
                                return nullptr;
                            }
                        } else {
                            if (acc && x + 2 < xs) {
                                x += 2;
                                return ev.Execute(op, move(acc), C(x - 1, y), C(x, y));
                            } else {
                                return nullptr;
                            }
                        }
                }
            }
            // Var read, Data
            default:
                return c->Eval(ev);
        }
    }

    unique_ptr<Cell> Eval(Evaluator &ev) {
        unique_ptr<Cell> acc;  // Actual/Accumulating data temporary
        bool alldata = true;  // Is the grid all data?
        // Do left to right processing
        if (xs > 1 || ys == 1) foreachcell(c) {
                if (x == 0) acc.reset();
                acc = EvalGridCell(ev, c, move(acc), x, y, alldata, false);
            }
        // Do top to bottom processing
        if (ys > 1) foreachcellcolumn(c) {
                if (y == 0) acc.reset();
                acc = EvalGridCell(ev, c, move(acc), x, y, alldata, true);
            }
        // If all data is true then we can exit now.
        if (alldata) {
            auto result = cell->Clone(nullptr);  // Potential result if all data.
            foreachcellingrid(c, result->grid) {
                auto temp = c->Eval(ev);
                DELETEP(c);
                c = temp.release();
            }
            return result;
        }
        return acc;
    }

    void Split(Vector<Grid *> &gs, bool vert) {
        loop(i, vert ? xs : ys) gs.push() = new Grid(vert ? 1 : xs, vert ? ys : 1);
        foreachcell(c) {
            Grid *g = gs[vert ? x : y];
            g->cells[vert ? y : x] = c->SetParent(g->cell);
            c = nullptr;
        }
    }

    unique_ptr<Cell> Sum() {
        double total = 0;
        foreachcell(c) {
            if (c->HasText()) total += c->text.GetNum();
        }
        auto c = make_unique<Cell>();
        c->text.SetNum(total);
        return c;
    }

    void Transpose() {
        Cell **tr = new Cell *[xs * ys];
        foreachcell(c) tr[y + x * ys] = c;
        delete[] cells;
        cells = tr;
        swap_(xs, ys);
        SetOrient();
        InitColWidths();
    }

    static int sortfunc(const Cell **a, const Cell **b) {
        loop(i, sys->sortxs) {
            int off = (i + sys->sortcolumn) % sys->sortxs;
            int cmp = (*(a + off))->text.t.CmpNoCase((*(b + off))->text.t);
            if (cmp) return sys->sortdescending ? -cmp : cmp;
        }
        return 0;
    }

    void Sort(Selection &s, bool descending) {
        sys->sortcolumn = s.x;
        sys->sortxs = xs;
        sys->sortdescending = descending;
        qsort(cells + s.y * xs, s.ys, sizeof(Cell *) * xs,
              (int(__cdecl *)(const void *, const void *))sortfunc);
    }

    Cell *FindExact(wxString &s) {
        foreachcell(c) {
            Cell *f = c->FindExact(s);
            if (f) return f;
        }
        return nullptr;
    }

    Selection HierarchySwap(wxString tag) {
        Cell *selcell = nullptr;
        bool done = false;
        lookformore:
        foreachcell(c) if (c->grid && !done) {
            Cell *f = c->grid->FindExact(tag);
            if (f) {
                // add all parent tags as extra hierarchy inside the cell
                for (Cell *p = f->parent; p != cell; p = p->parent) {
                    // Special case check: if parents have same name, this would cause infinite
                    // swapping.
                    if (p->text.t == tag) done = true;
                    Cell *t = new Cell(f, p);
                    t->text = p->text;
                    t->text.cell = t;
                    t->grid = f->grid;
                    if (t->grid) t->grid->ReParent(t);
                    f->grid = new Grid(1, 1);
                    f->grid->cell = f;
                    *f->grid->cells = t;
                }
                // remove cell from parent, recursively if parent becomes empty
                for (Cell *r = f; r && r != cell; r = r->parent->grid->DeleteTagParent(r, cell, f))
                    ;
                // merge newly constructed hierarchy at this level
                if (!*cells) {
                    *cells = f;
                    f->parent = cell;
                    selcell = f;
                } else {
                    MergeTagCell(f, selcell);
                }
                goto lookformore;
            }
        }
        ASSERT(selcell);
        return FindCell(selcell);
    }

    void ReParent(Cell *p) {
        cell = p;
        foreachcell(c) c->parent = p;
    }

    Cell *DeleteTagParent(Cell *tag, Cell *basecell, Cell *found) {
        ReplaceCell(tag, nullptr);
        if (xs * ys == 1) {
            if (cell != basecell) {
                cell->grid = nullptr;
                delete this;
            }
            Cell *next = tag->parent;
            if (tag != found) delete tag;
            return next;
        } else
            foreachcell(c) if (c == nullptr) {
                if (ys > 1)
                    DeleteCells(-1, y, 0, -1);
                else
                    DeleteCells(x, -1, -1, 0);
                return nullptr;
            }
        ASSERT(0);
        return nullptr;
    }

    void MergeTagCell(Cell *f, Cell *&selcell) {
        foreachcell(c) if (c->text.t == f->text.t) {
            if (!selcell) selcell = c;

            if (f->grid) {
                if (c->grid) {
                    f->grid->MergeTagAll(c);
                } else {
                    c->grid = f->grid;
                    c->grid->ReParent(c);
                    f->grid = nullptr;
                }
                delete f;
            }
            return;
        }
        if (!selcell) selcell = f;
        Add(f);
    }

    void MergeTagAll(Cell *into) {
        foreachcell(c) {
            into->grid->MergeTagCell(c, into /*dummy*/);
            c = nullptr;
        }
    }

    void SetGridTextLayout(int ds, bool vert, bool noset, const Selection &s) {
        foreachcellinsel(c, s) c->SetGridTextLayout(ds, vert, noset);
    }

    bool IsTable() {
        foreachcell(c) if (c->grid) return false;
        return true;
    }

    void Hierarchify(Document *doc) {
        loop(y, ys) {
            unique_ptr<Cell> rest;
            if (xs > 1) {
                Selection s(this, 1, y, xs - 1, 1);
                rest = CloneSel(s);
            }
            Cell *c = C(0, y);
            loop(prevy, y) {
                Cell *prev = C(0, prevy);
                if (prev->text.t == c->text.t) {
                    if (rest) {
                        ASSERT(prev->grid);
                        prev->grid->MergeRow(rest->grid);
                        rest.reset();
                    }

                    Selection s(this, 0, y, xs, 1);
                    MultiCellDeleteSub(doc, s);
                    y--;

                    goto done;
                }
            }
            if (rest) {
                swap_(c->grid, rest->grid);
                c->grid->ReParent(c);
            }
            done:;
        }
        Selection s(this, 1, 0, xs - 1, ys);
        MultiCellDeleteSub(doc, s);
        foreachcell(c) if (c->grid && c->grid->xs > 1) c->grid->Hierarchify(doc);
    }

    void MergeRow(Grid *tm) {
        ASSERT(xs == tm->xs && tm->ys == 1);
        InsertCells(-1, ys, 0, 1, nullptr);
        loop(x, xs) {
            swap_(C(x, ys - 1), tm->C(x, 0));
            C(x, ys - 1)->parent = cell;
        }
    }

    void MaxDepthLeaves(int curdepth, int &maxdepth, int &leaves) {
        foreachcell(c) c->MaxDepthLeaves(curdepth, maxdepth, leaves);
    }

    int Flatten(int curdepth, int cury, Grid *g) {
        foreachcell(c) if (c->grid) { cury = c->grid->Flatten(curdepth + 1, cury, g); }
        else {
            Cell *ic = c;
            for (int i = curdepth; i >= 0; i--) {
                Cell *dest = g->C(i, cury);
                dest->text = ic->text;
                dest->text.cell = dest;
                ic = ic->parent;
            }
            cury++;
        }
        return cury;
    }

    void ResizeColWidths(int dir, const Selection &s, bool hierarchical) {
        for (int x = s.x; x < s.x + s.xs; x++) {
            colwidths[x] += dir * 5;
            if (colwidths[x] < 5) colwidths[x] = 5;
            loop(y, ys) {
                Cell *c = C(x, y);
                if (c->grid && hierarchical)
                    c->grid->ResizeColWidths(dir, c->grid->SelectAll(), hierarchical);
            }
        }
    }

    void CollectCells(Vector<Cell *> &itercells) { foreachcell(c) c->CollectCells(itercells); }
    void CollectCellsSel(Vector<Cell *> &itercells, Selection &s, bool recurse) {
        foreachcellinsel(c, s) c->CollectCells(itercells, recurse);
    }

    void SetStyles(Selection &s, Cell *o) {
        foreachcellinsel(c, s) {
            c->cellcolor = o->cellcolor;
            c->textcolor = o->textcolor;
            c->text.stylebits = o->text.stylebits;
            c->text.image = o->text.image;
        }
    }

    void ClearImages(Selection &s) { foreachcellinsel(c, s) c->text.image = nullptr; }
};
