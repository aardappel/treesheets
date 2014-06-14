
class Selection
{
    bool textedit;

    public:
    Grid *g;
    int x, y, xs, ys;
    int cursor, cursorend;
    int firstdx, firstdy;

    Selection() { memset(this, 0, sizeof(Selection)); }
    Selection(Grid *_g, int _x, int _y, int _xs, int _ys) : g(_g), x(_x), y(_y), xs(_xs), ys(_ys), cursor(0), cursorend(0), textedit(false), firstdx(0), firstdy(0) {}

    void SelAll()
    {
        x = y = 0;
        xs = g->xs;
        ys = g->ys; 
    }

    Cell *GetCell()  const { return g && xs==1 && ys==1 ? g->C(x, y) : NULL; }
    Cell *GetFirst() const { return g && xs>=1 && ys>=1 ? g->C(x, y) : NULL; }

    bool EqLoc(const Selection &s) { return g==s.g && x==s.x && y==s.y && xs==s.xs && ys==s.ys; }
    bool operator==(const Selection &s) { return EqLoc(s) && cursor==s.cursor && cursorend==s.cursorend; }

    bool Thin() const { return !(xs*ys); }

    bool IsAll() const { return xs==g->xs && ys==g->ys; }

    void SetCursorEdit(Document *doc, bool edit)
    {
        wxCursor c(edit ? wxCURSOR_IBEAM : wxCURSOR_ARROW);
        #ifdef WIN32
        //this changes the cursor instantly, but gets overridden by the local window cursor
        ::SetCursor((HCURSOR)c.GetHCURSOR());
        #endif
        //this doesn't change the cursor immediately, only on mousemove:
        doc->sw->SetCursor(c);

        firstdx = firstdy = 0;
    }

    bool TextEdit() { return textedit; }
    void EnterEditOnly(Document *doc) { textedit = true; SetCursorEdit(doc, true); }
    void EnterEdit(Document *doc, int c = 0, int ce = 0) { EnterEditOnly(doc); cursor = c; cursorend = ce; }
    void ExitEdit(Document *doc) { textedit = false; cursor = cursorend = 0; SetCursorEdit(doc, false); }

    bool IsInside(Selection &o)
    {
        if(!o.g || !g) return false;
        if(g!=o.g) return g->cell->parent && g->cell->parent->grid->FindCell(g->cell).IsInside(o);
        return x>=o.x && y>=o.y && x+xs<=o.x+o.xs && y+ys<=o.y+o.ys;
    }

    void Merge(const Selection &a, const Selection &b)
    {
        textedit = false;
        if(a.g==b.g)
        {
            if(a.GetCell()==b.GetCell() && a.GetCell() && (a.textedit || b.textedit))
            {
                if(a.cursor!=a.cursorend)
                {
                    Selection c = b;
                    a.GetCell()->text.SelectWord(c);
                    cursor = min(a.cursor, c.cursor);
                    cursorend = max(a.cursorend, c.cursorend);
                }
                else
                {
                    cursor = min(a.cursor, b.cursor);
                    cursorend = max(a.cursor, b.cursor);
                }
                textedit = true;
            }
            else
            {
                cursor = cursorend = 0;
            }
        }
        else
        {
            Cell *at = a.GetCell();
            Cell *bt = b.GetCell();
            int ad = at->Depth();
            int bd = bt->Depth();
            int i = 0;
            while(i<ad && i<bd && at->Parent(ad-i)==bt->Parent(bd-i)) i++;
            Grid *g = at->Parent(ad-i+1)->grid;
            Merge(g->FindCell(at->Parent(ad-i)), g->FindCell(bt->Parent(bd-i)));
            return;
        }
        g = a.g;
        x = min(a.x, b.x);
        y = min(a.y, b.y);
        xs = abs(a.x-b.x)+1;
        ys = abs(a.y-b.y)+1;
    }

    int MaxCursor() { return int(GetCell()->text.t.Len()); }

    char *Dir(Document *doc, bool ctrl, bool shift, wxDC &dc, int dx, int dy, int &v, int &vs, int &ovs, bool notboundaryperp, bool notboundarypar, bool exitedit)
    {
        if(ctrl && !textedit)
        {
            g->cell->AddUndo(doc);

            g->Move(dx, dy, *this);
            x = (x+dx+g->xs)%g->xs;
            y = (y+dy+g->ys)%g->ys;
            if(x+xs>g->xs || y+ys>g->ys) g = NULL;

            doc->ScrollIfSelectionOutOfView(dc, *this, true);
        }
        else
        {
            doc->DrawSelect(dc, *this);
            if(ctrl && dx)    // implies textedit
            {
                if(cursor==cursorend) firstdx = dx;
                int &curs = firstdx<0 ? cursor : cursorend;
                for(int c = curs, start = curs;;)
                {
                    c += dx;
                    if(c<0 || c>MaxCursor()) break;
                    wxChar ch = GetCell()->text.t[(c+curs)/2];
                    if(!wxIsalnum(ch) && curs!=start) break;
                    curs = c;
                    if(!wxIsalnum(ch) && !wxIsspace(ch)) break;
                }
                if(shift) { if(cursorend<cursor) swap_(cursorend, cursor); }
                else cursorend = cursor = curs;
            }
            else if(shift)
            {
                if(textedit)
                {
                    if(cursor==cursorend) firstdx = dx;
                    (firstdx<0 ? cursor : cursorend) += dx;
                    if(cursor<0) cursor = 0;
                    if(cursorend>MaxCursor()) cursorend = MaxCursor();
                }
                else
                {
                    if(!xs) firstdx = 0;    // redundant: just in case someone else changed it
                    if(!ys) firstdy = 0;
                    if(!firstdx) firstdx = dx;
                    if(!firstdy) firstdy = dy;
                    if(firstdx<0) { x += dx; xs += -dx; } else xs += dx;
                    if(firstdy<0) { y += dy; ys += -dy; } else ys += dy;
                    if(x<0) { x = 0; xs--; }
                    if(y<0) { y = 0; ys--; }
                    if(x+xs>g->xs) xs--;
                    if(y+ys>g->ys) ys--;
                    if(!xs) firstdx = 0;
                    if(!ys) firstdy = 0;
                    if(!xs && !ys) g = NULL;
                }
            }
            else
            {
                if(vs)
                {
                    if(ovs)                 // (multi) cell selection
                    {
                        bool intracell = true;
                        if(textedit && !exitedit && GetCell())
                        {
                            if (dy)
                            {
                                cursorend = cursor;
                                Text &text = GetCell()->text;
                                int maxcolwidth = GetCell()->parent->grid->colwidths[x];

                                int i = 0;
                                int laststart, lastlen;
                                int nextoffset = -1;
                                for(int l = 0; ; l++)
                                {
                                    int start = i;
                                    wxString ls = text.GetLine(i, maxcolwidth);
                                    int len = (int)ls.Len();
                                    int end = start+len;

                                    if (len && nextoffset>=0)
                                    {
                                        cursor = cursorend = start + (nextoffset > len ? len : nextoffset);
                                        intracell = false;
                                        break;
                                    }

                                    if(cursor>=start && cursor<=end)
                                    {
                                        if (dy<0)
                                        {
                                            if (l != 0)
                                            {
                                                cursor = cursorend = laststart + (cursor - start > lastlen ? lastlen : cursor - start);
                                                intracell = false;
                                            }
                                            break;
                                        }
                                        else
                                        {
                                            nextoffset = cursor - start;
                                        }
                                    }

                                    laststart = start;
                                    lastlen = len;

                                    if(!len) break;
                                }
                            }
                            else
                            {
                                intracell = false;
                                if(cursor!=cursorend)
                                {
                                    if(dx<0) cursorend = cursor;
                                    else cursor = cursorend;
                                }
                                else
                                {
                                    if((dx<0 && cursor) || (dx>0 && MaxCursor()>cursor)) cursorend = cursor += dx;
                                }
                            }
                        }
                        
                        if (intracell)
                        {
                            if(sys->thinselc)
                            {
                                if(dx+dy>0) v += vs;
                                vs = 0;             // make it a thin selection, in direction
                                ovs = 1;
                            }
                            else
                            {
                                if(x+dx >= 0 && x+dx+xs <= g->xs && y+dy >= 0 && y+dy+ys <= g->ys)
                                {
                                    x += dx;
                                    y += dy;
                                }
                            }
                            ExitEdit(doc);
                        }
                    }
                    else if(notboundarypar) // thin selection, moving in parallel direction
                    {
                        v += dx+dy;
                    }
                }
                else if(notboundaryperp)    // thin selection, moving in perpendicular direction
                {
                    if(dx+dy<0) v--;
                    vs = 1;                     // make it a cell selection
                };
            }
            doc->DrawSelectMove(dc, *this);
        };
        return NULL;
    }

    char *Cursor(Document *doc, int k, bool ctrl, bool shift, wxDC &dc, bool exitedit = false)
    {
        switch(k)
        {
            case A_UP:    return Dir(doc, ctrl, shift, dc,  0, -1, y, ys, xs, y!=0,    y!=0,      exitedit);
            case A_DOWN:  return Dir(doc, ctrl, shift, dc,  0,  1, y, ys, xs, y<g->ys, y<g->ys-1, exitedit);
            case A_LEFT:  return Dir(doc, ctrl, shift, dc, -1,  0, x, xs, ys, x!=0,    x!=0,      exitedit);
            case A_RIGHT: return Dir(doc, ctrl, shift, dc,  1,  0, x, xs, ys, x<g->xs, x<g->xs-1, exitedit);
        }
        return NULL;
    }

    void Next(Document *doc, wxDC &dc, bool backwards)
    {
        doc->DrawSelect(dc, *this);
        ExitEdit(doc);
        if(backwards)
        {
            if(x>0) x--;
            else if(y>0) { y--; x = g->xs-1; }
            else { x = g->xs-1; y = g->ys-1; }
        }
        else
        {
            if(x<g->xs-1) x++;
            else if(y<g->ys-1) { y++; x = 0; }
            else x = y = 0;
        }
        EnterEdit(doc, 0, MaxCursor());
        doc->DrawSelectMove(dc, *this);
    }
    
    const char *Wrap(Document *doc)
    {
        if(Thin()) return doc->NoThin();
        g->cell->AddUndo(doc);
        Cell *np = g->CloneSel(*this);
        g->C(x, y)->text.t = "."; // avoid this cell getting deleted
        if(xs>1) { Selection s(g, x+1, y,   xs-1, ys  ); g->MultiCellDeleteSub(doc, s); }
        if(ys>1) { Selection s(g, x,   y+1, 1,    ys-1); g->MultiCellDeleteSub(doc, s); }
        Cell *old = g->C(x, y);
        np->text.relsize = old->text.relsize;
        np->CloneStyleFrom(old);
        g->ReplaceCell(old, np);
        np->parent = g->cell;
        delete old;
        xs = ys = 1;
        EnterEdit(doc, MaxCursor(), MaxCursor());
        doc->Refresh();
        return NULL;     
    }

    Cell *ThinExpand(Document *doc)
    {
        if(Thin())
        {
            if(xs) { g->cell->AddUndo(doc); g->InsertCells(-1, y, 0, 1); ys = 1; }
            else   { g->cell->AddUndo(doc); g->InsertCells(x, -1, 1, 0); xs = 1; }
        }
        return GetCell();
    }

    const char *HomeEnd(Document *doc, wxDC &dc, bool ishome)
    {
        doc->DrawSelect(dc, *this);
        xs = ys = 1;
        if(ishome) x = y = 0;
        else { x = g->xs-1; y = g->ys-1; }
        doc->DrawSelectMove(dc, *this);
        return NULL;
    }
};

