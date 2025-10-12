class Selection {
    bool textedit {false};

    public:
    Grid *grid;
    int x;
    int y;
    int xs;
    int ys;
    int cursor {0};
    int cursorend {0};
    int firstdx {0};
    int firstdy {0};

    Selection(Grid *_grid = nullptr, int _x = 0, int _y = 0, int _xs = 0, int _ys = 0)
        : grid(_grid), x(_x), y(_y), xs(_xs), ys(_ys) {}

    void SelAll() {
        if (textedit) {
            cursor = 0;
            cursorend = MaxCursor();
        } else {
            x = y = 0;
            xs = grid->xs;
            ys = grid->ys;
        }
    }

    Cell *GetCell() const { return grid && xs == 1 && ys == 1 ? grid->C(x, y) : nullptr; }
    Cell *GetFirst() const { return grid && xs >= 1 && ys >= 1 ? grid->C(x, y) : nullptr; }
    bool EqLoc(const Selection &s) {
        return grid == s.grid && x == s.x && y == s.y && xs == s.xs && ys == s.ys;
    }
    bool operator==(Selection &s) {
        return EqLoc(s) && cursor == s.cursor && cursorend == s.cursorend;
    }
    bool Thin() const { return !(xs * ys); }
    bool IsAll() const { return xs == grid->xs && ys == grid->ys; }
    void SetCursorEdit(Document *doc, bool edit) {
        wxCursor c(edit ? wxCURSOR_IBEAM : wxCURSOR_ARROW);
        #ifdef WIN32
        // this changes the cursor instantly, but gets overridden by the local window cursor
        ::SetCursor((HCURSOR)c.GetHCURSOR());
        #endif
        // this doesn't change the cursor immediately, only on mousemove:
        doc->canvas->SetCursor(c);

        firstdx = firstdy = 0;
    }

    bool TextEdit() { return textedit; }
    void EnterEditOnly(Document *doc) {
        textedit = true;
        SetCursorEdit(doc, true);
    }
    void EnterEdit(Document *doc, int cursor = 0, int cursorend = 0) {
        EnterEditOnly(doc);
        this->cursor = cursor;
        this->cursorend = cursorend;
    }
    void ExitEdit(Document *doc) {
        textedit = false;
        cursor = cursorend = 0;
        SetCursorEdit(doc, false);
    }

    bool IsInside(Selection &o) {
        if (!o.grid || !grid) return false;
        if (grid != o.grid)
            return grid->cell->parent && grid->cell->parent->grid->FindCell(grid->cell).IsInside(o);
        return x >= o.x && y >= o.y && x + xs <= o.x + o.xs && y + ys <= o.y + o.ys;
    }

    void Merge(const Selection &a, const Selection &b) {
        textedit = false;
        if (a.grid == b.grid) {
            if (a.GetCell() == b.GetCell() && a.GetCell() && (a.textedit || b.textedit)) {
                if (a.cursor != a.cursorend) {
                    Selection c = b;
                    a.GetCell()->text.SelectWord(c);
                    cursor = min(a.cursor, c.cursor);
                    cursorend = max(a.cursorend, c.cursorend);
                } else {
                    cursor = min(a.cursor, b.cursor);
                    cursorend = max(a.cursor, b.cursor);
                }
                textedit = true;
            } else {
                cursor = cursorend = 0;
            }
        } else {
            auto at = a.GetCell();
            auto bt = b.GetCell();
            int ad = at->Depth();
            int bd = bt->Depth();
            int i = 0;
            while (i < ad && i < bd && at->Parent(ad - i) == bt->Parent(bd - i)) i++;
            auto g = at->Parent(ad - i + 1)->grid;
            Merge(g->FindCell(at->Parent(ad - i)), g->FindCell(bt->Parent(bd - i)));
            return;
        }
        grid = a.grid;
        x = min(a.x, b.x);
        y = min(a.y, b.y);
        xs = abs(a.x - b.x) + 1;
        ys = abs(a.y - b.y) + 1;
    }

    int MaxCursor() { return int(GetCell()->text.t.Len()); }

    inline bool IsWordSep(wxChar ch) {
        // represents: !"#$%&'()*+,-./    :;<=>?@    [\]^    {|}~    `
        return 32 < ch && ch < 48 || 57 < ch && ch < 65 || 90 < ch && ch < 95 ||
               122 < ch && ch < 127 || ch == 96;
    }

    inline int CharType(wxChar ch) {
        if (wxIsspace(ch)) return TEXT_SPACE;
        if (IsWordSep(ch)) return TEXT_SEP;
        return TEXT_CHAR;
    }

    void Dir(Document *doc, bool ctrl, bool shift, int dx, int dy, int &v, int &vs, int &ovs,
             bool notboundaryperp, bool notboundarypar, bool exitedit) {
        if (ctrl && !textedit) {
            grid->cell->AddUndo(doc);

            grid->Move(dx, dy, *this);
            x = (x + dx + grid->xs) % grid->xs;
            y = (y + dy + grid->ys) % grid->ys;
            if (x + xs > grid->xs || y + ys > grid->ys) grid = nullptr;

            // FIXME: this is null in the case of a whole column selection, and doesn't do the right
            // thing.
            if (grid) grid->cell->ResetChildren();
            doc->RefreshMove();
        } else {
            if (ctrl && dx)  // implies textedit
            {
                if (cursor == cursorend) firstdx = dx;
                int &curs = firstdx < 0 ? cursor : cursorend;
                int c = curs + dx;
                wxChar ch;
                if (c >= 0 && c <= MaxCursor()) {
                    ch = GetCell()->text.t[min(c, curs)];
                    // TEXT_SPACE > TEXT_SEP > TEXT_CHAR > 0.
                    // Accepts smaller or equal type when positive, only equal when negative.
                    // in regex terms (space/sep/char = s/p/c): match (s+p*|s+c*|p+c*|c+)
                    int allowed = CharType(ch);
                    curs = c;
                    for (;;) {
                        c += dx;
                        if (c < 0 || c > MaxCursor()) break;
                        ch = GetCell()->text.t[min(c, curs)];
                        int chtype = CharType(ch);
                        // type increase when positive or type change when negative => break
                        if (chtype > allowed && chtype != -allowed) break;
                        curs = c;
                        // type decrease when positive => negate
                        if (chtype < allowed) allowed = -chtype;
                    }
                }
                if (shift) {
                    if (cursorend < cursor) swap_(cursorend, cursor);
                } else
                    cursorend = cursor = curs;
            } else if (shift) {
                if (textedit) {
                    if (cursor == cursorend) firstdx = dx;
                    (firstdx < 0 ? cursor : cursorend) += dx;
                    if (cursor < 0) cursor = 0;
                    if (cursorend > MaxCursor()) cursorend = MaxCursor();
                } else {
                    if (!xs) firstdx = 0;  // redundant: just in case someone else changed it
                    if (!ys) firstdy = 0;
                    if (!firstdx) firstdx = dx;
                    if (!firstdy) firstdy = dy;
                    if (firstdx < 0) {
                        x += dx;
                        xs += -dx;
                    } else
                        xs += dx;
                    if (firstdy < 0) {
                        y += dy;
                        ys += -dy;
                    } else
                        ys += dy;
                    if (x < 0) {
                        x = 0;
                        xs--;
                    }
                    if (y < 0) {
                        y = 0;
                        ys--;
                    }
                    if (x + xs > grid->xs) xs--;
                    if (y + ys > grid->ys) ys--;
                    if (!xs) firstdx = 0;
                    if (!ys) firstdy = 0;
                    if (!xs && !ys) grid = nullptr;
                }
            } else {
                if (vs) {
                    if (ovs)  // (multi) cell selection
                    {
                        bool intracell = true;
                        if (textedit && !exitedit && GetCell()) {
                            if (dy) {
                                cursorend = cursor;
                                auto &text = GetCell()->text;
                                int maxcolwidth = GetCell()->parent->grid->colwidths[x];

                                int i = 0;
                                int laststart, lastlen;
                                int nextoffset = -1;
                                for (int l = 0;; l++) {
                                    int start = i;
                                    auto ls = text.GetLine(i, maxcolwidth);
                                    auto len = static_cast<int>(ls.Len());
                                    int end = start + len;

                                    if (len && nextoffset >= 0) {
                                        cursor = cursorend =
                                            start + (nextoffset > len ? len : nextoffset);
                                        intracell = false;
                                        break;
                                    }

                                    if (cursor >= start && cursor <= end) {
                                        if (dy < 0) {
                                            if (l != 0) {
                                                cursor = cursorend =
                                                    laststart + (cursor - start > lastlen
                                                                     ? lastlen
                                                                     : cursor - start);
                                                intracell = false;
                                            }
                                            break;
                                        } else {
                                            nextoffset = cursor - start;
                                        }
                                    }

                                    laststart = start;
                                    lastlen = len;

                                    if (!len) break;
                                }
                            } else {
                                intracell = false;
                                if (cursor != cursorend) {
                                    if (dx < 0)
                                        cursorend = cursor;
                                    else
                                        cursor = cursorend;
                                } else {
                                    if ((dx < 0 && cursor) || (dx > 0 && MaxCursor() > cursor))
                                        cursorend = cursor += dx;
                                }
                            }
                        }

                        if (intracell) {
                            if (sys->thinselc) {
                                if (dx + dy > 0) v += vs;
                                vs = 0;  // make it a thin selection, in direction
                                ovs = 1;
                            } else {
                                if (x + dx >= 0 && x + dx + xs <= grid->xs && y + dy >= 0 &&
                                    y + dy + ys <= grid->ys) {
                                    x += dx;
                                    y += dy;
                                }
                            }
                            ExitEdit(doc);
                        }
                    } else if (notboundarypar)  // thin selection, moving in parallel direction
                    {
                        v += dx + dy;
                    }
                } else if (notboundaryperp)  // thin selection, moving in perpendicular direction
                {
                    if (dx + dy < 0) v--;
                    vs = 1;  // make it a cell selection
                } else {     // selection cycle, jump to the opposite side of the grid
                    if (y + dy > grid->ys) {
                        y = 0;
                        vs = 1;
                    } else if (y + dy < 0) {
                        y = grid->ys - 1;
                        vs = 1;
                    } else if (x + dx > grid->xs) {
                        x = 0;
                        vs = 1;
                    } else if (x + dx < 0) {
                        x = grid->xs - 1;
                        vs = 1;
                    }
                };
            }
            doc->RefreshMove();
        };
    }

    void Cursor(Document *doc, int action, bool ctrl, bool shift, bool exitedit = false) {
        switch (action) {
            case A_UP: Dir(doc, ctrl, shift, 0, -1, y, ys, xs, y != 0, y != 0, exitedit); break;
            case A_DOWN:
                Dir(doc, ctrl, shift, 0, 1, y, ys, xs, y < grid->ys, y < grid->ys - 1, exitedit);
                break;
            case A_LEFT: Dir(doc, ctrl, shift, -1, 0, x, xs, ys, x != 0, x != 0, exitedit); break;
            case A_RIGHT:
                Dir(doc, ctrl, shift, 1, 0, x, xs, ys, x < grid->xs, x < grid->xs - 1, exitedit);
                break;
        }
    }

    void Next(Document *doc, bool backwards) {
        ExitEdit(doc);
        if (backwards) {
            if (x > 0)
                x--;
            else if (y > 0) {
                y--;
                x = grid->xs - 1;
            } else {
                x = grid->xs - 1;
                y = grid->ys - 1;
            }
        } else {
            if (x < grid->xs - 1)
                x++;
            else if (y < grid->ys - 1) {
                y++;
                x = 0;
            } else
                x = y = 0;
        }
        EnterEdit(doc, 0, MaxCursor());
        doc->RefreshMove();
    }

    const wxChar *Wrap(Document *doc) {
        if (Thin()) return doc->NoThin();
        grid->cell->AddUndo(doc);
        auto np = grid->CloneSel(*this).release();
        grid->C(x, y)->text.t = ".";  // avoid this cell getting deleted
        if (xs > 1) {
            Selection s(grid, x + 1, y, xs - 1, ys);
            grid->MultiCellDeleteSub(doc, s);
        }
        if (ys > 1) {
            Selection s(grid, x, y + 1, 1, ys - 1);
            grid->MultiCellDeleteSub(doc, s);
        }
        auto old = grid->C(x, y);
        np->text.relsize = old->text.relsize;
        np->CloneStyleFrom(old);
        grid->ReplaceCell(old, np);
        np->parent = grid->cell;
        delete old;
        xs = ys = 1;
        EnterEdit(doc, MaxCursor(), MaxCursor());
        doc->canvas->Refresh();
        return nullptr;
    }

    Cell *ThinExpand(Document *doc, bool jumptofirst = false) {
        if (Thin()) {
            if (xs) {
                grid->cell->AddUndo(doc);
                grid->InsertCells(-1, y, 0, 1);
                ys = 1;
                if (jumptofirst) x = 0;
            } else {
                grid->cell->AddUndo(doc);
                grid->InsertCells(x, -1, 1, 0);
                xs = 1;
                if (jumptofirst) y = 0;
            }
        }
        return GetCell();
    }

    void HomeEnd(Document *doc, bool ishome) {
        xs = ys = 1;
        if (ishome)
            x = y = 0;
        else {
            x = grid->xs - 1;
            y = grid->ys - 1;
        }
        doc->RefreshMove();
    }
};
