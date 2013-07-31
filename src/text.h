
struct Text
{
    Cell *cell;

    wxString t;
    int relsize, stylebits, extent;

    Image *image;

    wxDateTime lastedit;
    bool filtered;

    Text() : cell(NULL), t(wxEmptyString), relsize(0), stylebits(0), extent(0), image(NULL), filtered(false) { WasEdited(); }

    double GetNum() { wxChar *end; double r = wcstod(t.c_str(), &end); /*HUGE_VAL*/ return r; }

    Cell *SetNum(double d)
    {
        t = wxString::Format(L"%.15f", d);
        const wxChar *dot = wcschr(t.c_str(), L'.');
        if(dot)
        {
            const wxChar *zeroes = wcsstr(dot, L"000000");       // FIXME: this is retarded... any better way?
            int i = (int)t.size();
            if(zeroes) i = zeroes-t.c_str();
            else
            {
                const wxChar *nines = wcsstr(dot, L"999999");
                if(nines)
                {
                    i = nines-t.c_str();
                    wxUniCharRef c = t[i-(t[i-1]==L'.' ? 2 : 1)];
                    c = c.GetValue()+1;
                }
            }
            if(t[i-1]==L'.') i--;
            t.Truncate(i);
        }
        return cell;
    }

    wxString htmlify(wxString &str)
    {
        wxString r;
        loop(i, str.Len())
        {
            switch(str[i].GetValue())
            {
                case '&': r += L"&amp;"; break;
                case '<': r += L"&lt;"; break;
                case '>': r += L"&gt;"; break;
                default: r += str[i];
            }
        }
        return r;
    }

    wxString ToText(int indent, const Selection &s, int format)
    {
         wxString str = s.cursor!=s.cursorend ? t.Mid(s.cursor, s.cursorend-s.cursor) : t;
         if(format==A_EXPXML || format==A_EXPHTMLT || format==A_EXPHTMLO) str = htmlify(str);
         return str;
    };

    void WasEdited() { lastedit = wxDateTime::Now(); }

    int MinRelsize(int rs) { return min(relsize, rs); }
    void RelSize(int dir, int zoomdepth) { relsize = max(min(relsize+dir, g_deftextsize-g_mintextsize()+zoomdepth), g_deftextsize-g_maxtextsize()-zoomdepth); }

    bool IsWord(wxChar c) { return wxIsalnum(c) || wxStrchr(L"_\"\'()", c); }

    wxString GetLinePart(int &i, int p, int l)
    {
        int start = i;
        i = p;
        
        while(i<l && t[i]!=L' ' && !IsWord(t[i])) { i++; p++; };  // gobble up any trailing punctuation
        if (i != start && i<l && (t[i]=='\"' || t[i]=='\'')) { i++; p++; } // special case: if punctuation followed by quote, quote is meant to be part of word
        
        while(i<l && t[i]==L' ')    // gobble spaces, but do not copy them
        {
            i++;
            if(i==l) p = i; // happens with a space at the last line, user is most likely about to type another word, so need to show space. Alternatively could check if the cursor is actually on this spot. Simply showing a blank new line would not be a good idea, unless the cursor is here for sure, and even then, placing the cursor there again after deselect may be hard.
        }

        ASSERT(start!=i);
        
        return t.Mid(start, p-start);
    }

    wxString GetLine(int &i, int maxcolwidth)
    {
        int l = (int)t.Len();
    
        if(i>=l) return wxEmptyString;
        
        if(!i && l  <=maxcolwidth) { i = l; return t; }    // subsumed by the case below, but this case happens 90% of the time, so more optimal
        if(      l-i<=maxcolwidth) return GetLinePart(i, l, l);
        
        for(int p = i+maxcolwidth; p>=i; p--) if(!IsWord(t[p])) return GetLinePart(i, p, l);
        for(int p = i+maxcolwidth; p<l;  p++) if(!IsWord(t[p])) return GetLinePart(i, p, l);  // we arrive here only if a single word is too big for maxcolwidth, so simply return that word          
        
        return GetLinePart(i, l, l);     // big word was the last one
    }

    void TextSize(wxDC &dc, int &sx, int &sy, bool tiny, int &leftoffset, int maxcolwidth)
    {
        sx = sy = 0;
        int i = 0;
        for(;;)
        {
            wxString curl = GetLine(i, maxcolwidth);
            if(!curl.Len()) break;
            int x, y;
            if(tiny) { x = (int)curl.Len(); y = 1; } else dc.GetTextExtent(curl, &x, &y);
            sx = max(x, sx);
            sy += y;
            leftoffset = y;
        }
        if(!tiny) sx += 4;
    }

    bool IsInSearch() { return sys->searchstring.Len() && t.Lower().Find(sys->searchstring)>=0; }

    int Render(Document *doc, int bx, int by, int depth, wxDC &dc, int &leftoffset, int maxcolwidth)
    {
        int ixs = 0, iys = 0;
        if(!cell->tiny) sys->ImageSize(image, ixs, iys);

        if(ixs && iys)
        {
            sys->ImageDraw(image, dc, bx+1+g_margin_extra, by+(cell->tys-iys)/2+g_margin_extra, ixs, iys);
            ixs += 2;
            iys += 2;
        }

        if(t.empty()) return iys;

        doc->PickFont(dc, depth, relsize, stylebits);
        bool tiny = cell->tiny;
        
        int h = tiny ? 1 : dc.GetCharHeight();
        leftoffset = h;
        int i = 0;
        int lines = 0;
        bool searchfound = IsInSearch();
        bool istag = cell->IsTag(doc);
        if(tiny)
        {
            if(searchfound)   dc.SetPen(*wxRED_PEN);
            else if(filtered) dc.SetPen(*wxLIGHT_GREY_PEN);
            else if(istag)    dc.SetPen(*wxBLUE_PEN);
            else              dc.SetPen(sys->pen_tinytext);
        }
        for(;;)
        {
            wxString curl = GetLine(i, maxcolwidth);
            if(!curl.Len()) break;
            if(tiny)
            {
                if(sys->fastrender)
                {
                    dc.DrawLine(bx+ixs, by+lines*h, bx+ixs+curl.Len(), by+lines*h);
                }
                else
                {
                    int word = 0;
                    loop(p, (int)curl.Len()+1)
                    {
                        if((int)curl.Len()<=p || curl[p]==' ')
                        {
                            if(word) dc.DrawLine(bx+p-word+ixs, by+lines*h, bx+p, by+lines*h);
                            word = 0;
                        }
                        else word++;
                    }                
                }
            }
            else
            {
                if(searchfound) dc.SetTextForeground(*wxRED);
                else if(filtered) dc.SetTextForeground(*wxLIGHT_GREY);
                else if(istag)  dc.SetTextForeground(*wxBLUE);
                else if(cell->textcolor) dc.SetTextForeground(cell->textcolor);     // FIXME: clean up
                int tx = bx+2+ixs;
                int ty = by+lines*h;
                MyDrawText(dc, curl, tx+g_margin_extra, ty+g_margin_extra, cell->sx, h);
                if(stylebits&STYLE_STRIKETHRU)
                {
                    int txs, tys;
                    dc.GetTextExtent(curl, &txs, &tys);
                    dc.SetPen(*wxGREY_PEN);
                    dc.DrawLine(tx, ty+h/2, tx+txs, ty+h/2);
                }
                if(searchfound || filtered || istag || cell->textcolor) dc.SetTextForeground(*wxBLACK);
            }
            lines++;
        }

        return max(lines*h, iys);
    }

    void FindCursor(Document *doc, int bx, int by, wxDC &dc, Selection &s, int maxcolwidth)
    {
        bx -= g_margin_extra;
        by -= g_margin_extra;

        int ixs = 0, iys = 0;
        if(!cell->tiny) sys->ImageSize(image, ixs, iys);
        if(ixs) ixs += 2;
        
        doc->PickFont(dc, cell->Depth()-doc->drawpath.size(), relsize, stylebits);
        
        int i = 0, linestart = 0;
        int line = by/dc.GetCharHeight();
        wxString ls;
        
        loop(l, line+1)
        {
            linestart = i;
            ls = GetLine(i, maxcolwidth);
        }
        
        for(;;)
        {
            int x, y;
            dc.GetTextExtent(ls, &x, &y);   // FIXME: can we do this more intelligently?
            if(x<=bx-ixs+2 || !x) break;
            ls.Truncate(ls.Len()-1);
        }
        
        s.cursor = s.cursorend = linestart+(int)ls.Len();
        ASSERT(s.cursor>=0 && s.cursor<=(int)t.Len());
    }

    void DrawCursor(Document *doc, wxDC &dc, Selection &s, bool full, uint color, bool cursoronly, int maxcolwidth)
    {
        int ixs = 0, iys = 0;
        if(!cell->tiny) sys->ImageSize(image, ixs, iys);
        if(ixs) ixs += 2;
        doc->PickFont(dc, cell->Depth()-doc->drawpath.size(), relsize, stylebits);
        int h = dc.GetCharHeight();
        {
            int i = 0;
            for(int l = 0; ; l++)
            {
                int start = i;
                wxString ls = GetLine(i, maxcolwidth);
                int len = (int)ls.Len();
                int end = start+len;
                
                if(s.cursor!=s.cursorend)
                {
                    if(s.cursor<=end && s.cursorend>=start && !cursoronly)
                    {
                        ls.Truncate(min(s.cursorend, end)-start);
                        int x1, x2;
                        dc.GetTextExtent(ls, &x2, NULL);
                        ls.Truncate(max(s.cursor, start)-start);
                        dc.GetTextExtent(ls, &x1, NULL);
                        if(x1!=x2) DrawRectangle(dc, color, cell->GetX(doc)+x1+2+ixs+g_margin_extra, cell->GetY(doc)+l*h+1+cell->ycenteroff+g_margin_extra, x2-x1, h-1
                        #ifdef SIMPLERENDER
                            , true
                        #endif
                        );
                    }
                }
                else if(s.cursor>=start && s.cursor<=end)
                {
                    ls.Truncate(s.cursor-start);
                    int x;
                    dc.GetTextExtent(ls, &x, NULL);
                    if(doc->blink)
                    #ifdef SIMPLERENDER
                        DrawRectangle(dc, color, cell->GetX(doc)+x+1+ixs+g_margin_extra, cell->GetY(doc)+l*h+1+cell->ycenteroff+g_margin_extra, 2, h-2);
                    #else
                        DrawLine(dc, 0xFFFFFF, cell->GetX(doc)+x+2+ixs+g_margin_extra, cell->GetY(doc)+l*h+cell->ycenteroff+g_margin_extra, 0, h);
                    #endif
                    break;
                }
                
                if(!len) break;
            }
        }
    }

    void SelectWord(Selection &s)
    {
        if(s.cursor>=(int)t.Len()) return;
        s.cursorend = s.cursor+1;
        if(!wxIsalnum(t[s.cursor])) return;
        while(s.cursor>0 && wxIsalnum(t[s.cursor-1])) s.cursor--;
        while(s.cursorend<(int)t.Len() && wxIsalnum(t[s.cursorend])) s.cursorend++;
    }

    #define ifrangeselremove if(WasEdited(), s.cursor!=s.cursorend) { t.Remove(s.cursor, s.cursorend-s.cursor); s.cursorend = s.cursor; }
    #define setrelsize if(t.Len()==0 && cell->parent) cell->parent->grid->IdealRelSize(relsize);

    void Insert(Document *doc, const wxString &ins, Selection &s)
    {
        if(!s.TextEdit()) Clear(doc, s);
        ifrangeselremove;
        setrelsize;
        t.insert(s.cursor, ins);
        s.cursor = s.cursorend = s.cursor + ins.Len();
    }

    void Delete(Selection &s)             { ifrangeselremove else if(s.cursor<(int)t.Len()) { t.Remove(s.cursor, 1); }; }
    void Backspace(Selection &s)          { ifrangeselremove else if(s.cursor>0)            { t.Remove(--s.cursor, 1); --s.cursorend; }; }
    void Key(int k, Selection &s)         { ifrangeselremove; setrelsize; t.insert(s.cursor++, 1, k); s.cursorend = s.cursor; }

    void ReplaceStr(const wxString &str)
    {
        for(int i = 0, j; (j = t.Mid(i).Lower().Find(sys->searchstring))>=0; )
        {
            // does this need WasEdited()?
            i += j;
            t.Remove(i, sys->searchstring.Len());
            t.insert(i, str);
            i += str.Len();
        }
    }

    void Clear(Document *doc, Selection &s)              { t.Clear(); s.EnterEdit(doc); }

    void HomeEnd(Selection &s, bool home)
    {
        int i = 0;
        int cw = cell->ColWidth();
        int findwhere = home ? s.cursor : s.cursorend;
        for(;;)
        {
            int start = i;
            wxString curl = GetLine(i, cw);
            if(!curl.Len()) break;
            int end = i==t.Len() ? i : i-1;
            if(findwhere >= start && findwhere <= end)
            {
                s.cursor = s.cursorend = home ? start : end;
                break;
            }
        }
    }

    void Save(wxDataOutputStream &dos) const
    {
        dos.WriteString(t.wx_str());
        dos.Write32(relsize);
        dos.Write32(image ? image->savedindex : -1);
        dos.Write32(stylebits);
        wxLongLong le = lastedit.GetValue();
        dos.Write64(&le, 1);
    }

    void Load(wxDataInputStream &dis)
    {
        t = dis.ReadString();

        //if (t.length() > 10000)
        //    printf("");
        
        if(sys->versionlastloaded<=11) dis.Read32();        // numlines
        
        relsize = dis.Read32();
        
        int i = dis.Read32();
        image = i>=0 ? sys->imagelist[sys->loadimageids[i]] : NULL;
        
        if(sys->versionlastloaded>=7) stylebits = dis.Read32();
        
        wxLongLong time;
        if(sys->versionlastloaded>=14)
        {
            dis.Read64(&time, 1);
        }
        else
        {
            time = sys->fakelasteditonload--;
        }
        lastedit = wxDateTime(time);
    }

    Cell *Eval(Evaluator &ev)
    {
        switch(cell->celltype)
        {
            // Load variable's data.
            case CT_VARU:
            {
                Cell* temp = ev.Lookup(t);

                if (!temp)
                {
                    temp = cell->Clone(NULL);
                    temp->celltype = CT_DATA;
                    temp->text.t = "**Variable Load Error**";
                }

                return temp;
            }

            // Return our current data.
            case CT_DATA: return cell->Clone(NULL);

            default:      return NULL;
        }
    }
    
    Cell *Graph()
    {
        Cell *c = new Cell();
        c->text.t.Append(L'|', (int)GetNum());
        return c;
    }
};

