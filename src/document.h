#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION __DATE__
#endif

struct UndoItem {
    Vector<Selection> path, selpath;
    Selection sel;
    unique_ptr<Cell> clone;
    size_t estimated_size;
    uintptr_t cloned_from;  // May be dead.

    UndoItem() : estimated_size(0) {}
};

struct Document {
    TSCanvas *sw;

    Cell *rootgrid;
    Selection hover, selected, begindrag;
    int isctrlshiftdrag;

    int originx, originy, maxx, maxy, centerx, centery;
    int layoutxs, layoutys, hierarchysize, fgutter;
    int lasttextsize, laststylebits;
    Cell *curdrawroot;  // for use during Render() calls

    Vector<UndoItem *> undolist, redolist;
    Vector<Selection> drawpath;
    int pathscalebias;

    wxString filename;

    long lastmodsinceautosave, undolistsizeatfullsave, lastsave;
    bool modified, tmpsavesuccess;

    wxDataObjectComposite *dataobjc;
    wxTextDataObject *dataobjt;
    wxBitmapDataObject *dataobji;
    wxFileDataObject *dataobjf;
    //wxHTMLDataObject *dataobjh;
    //wxRichTextBufferDataObject *dataobjr;

    struct MyPrintout : wxPrintout {
        Document *doc;
        MyPrintout(Document *d) : doc(d), wxPrintout(L"printout") {}

        bool OnPrintPage(int page) {
            wxDC *dc = GetDC();
            if (!dc) return false;
            doc->Print(*dc, *this);
            return true;
        }

        bool OnBeginDocument(int startPage, int endPage) {
            return wxPrintout::OnBeginDocument(startPage, endPage);
        }

        void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo) {
            *minPage = 1;
            *maxPage = 1;
            *selPageFrom = 1;
            *selPageTo = 1;
        }

        bool HasPage(int pageNum) { return pageNum == 1; }
    };

    bool while_printing;
    wxPrintData printData;
    wxPageSetupDialogData pageSetupData;
    uint printscale;

    bool blink;

    bool redrawpending;

    bool scaledviewingmode;
    double currentviewscale;

    bool searchfilter;

    std::map<wxString, bool> tags;

    int editfilter;

    Vector<Cell *> itercells;

    wxDateTime lastmodificationtime;

    #define loopcellsin(par, c) \
        CollectCells(par);      \
        loopv(_i, itercells) for (Cell *c = itercells[_i]; c; c = nullptr)
    #define loopallcells(c)     \
        CollectCells(rootgrid); \
        loopv(_i, itercells) for (Cell *c = itercells[_i]; c; c = nullptr)
    #define loopallcellssel(c, rec) \
        CollectCellsSel(rec);     \
        loopv(_i, itercells) for (Cell *c = itercells[_i]; c; c = nullptr)

    Document()
        : sw(nullptr),
          rootgrid(nullptr),
          pathscalebias(0),
          filename(L""),
          lastmodsinceautosave(0),
          undolistsizeatfullsave(0),
          lastsave(wxGetLocalTime()),
          modified(false),
          centerx(0),
          centery(0),
          while_printing(false),
          printscale(0),
          blink(true),
          redrawpending(false),
          scaledviewingmode(false),
          currentviewscale(1),
          editfilter(0),
          searchfilter(false),
          tmpsavesuccess(true),
          fgutter(6) {
        dataobjc = new wxDataObjectComposite();  // deleted by DropTarget
        dataobjc->Add(dataobji = new wxBitmapDataObject());
        dataobjc->Add(dataobjt = new wxTextDataObject());
        dataobjc->Add(dataobjf = new wxFileDataObject());
        //dataobjc->Add(dataobjh = new wxHTMLDataObject(), true);  // Prefer HTML over text, doesn't seem to work.
        //dataobjc->Add(dataobjr = new wxRichTextBufferDataObject());
        ResetFont();
        pageSetupData = printData;
        pageSetupData.SetMarginTopLeft(wxPoint(15, 15));
        pageSetupData.SetMarginBottomRight(wxPoint(15, 15));
    }

    ~Document() {
        itercells.setsize_nd(0);
        DELETEP(rootgrid);
    }

    uint Background() { return rootgrid ? rootgrid->cellcolor : 0xFFFFFF; }

    void InitWith(Cell *r, wxString filename) {
        rootgrid = r;
        selected = Selection(r->grid, 0, 0, 1, 1);
        ChangeFileName(filename, false);
    }

    void UpdateFileName(int page = -1) {
        sys->frame->SetPageTitle(filename, modified ? (lastmodsinceautosave ? L"*" : L"+") : L"",
                                 page);
    }

    void ChangeFileName(const wxString &fn, bool checkext) {
        filename = fn;
        if (checkext) {
            wxFileName wxfn(filename);
            if (!wxfn.HasExt()) filename.Append(L".cts");
        }
        UpdateFileName();
    }

    const wxChar *SaveDB(bool *success, bool istempfile = false, int page = -1) {
        if (filename.empty()) return _(L"Save cancelled.");

        auto start_saving_time = wxGetLocalTimeMillis();

        {  // limit destructors
            wxBusyCursor wait;
            if (!istempfile && sys->makebaks && ::wxFileExists(filename)) {
                ::wxRenameFile(filename, sys->BakName(filename));
            }
            wxString sfn = istempfile ? sys->TmpName(filename) : filename;
            wxFFileOutputStream fos(sfn);
            if (!fos.IsOk()) {
                if (!istempfile)
                    wxMessageBox(
                        _(L"Error writing TreeSheets file! (try saving under new filename)."),
                        sfn.wx_str(), wxOK, sys->frame);
                return _(L"Error writing to file.");
            }
            wxDataOutputStream sos(fos);
            fos.Write("TSFF", 4);
            char vers = TS_VERSION;
            fos.Write(&vers, 1);
            loopv(i, sys->imagelist) sys->imagelist[i]->trefc = 0;
            rootgrid->ImageRefCount();
            int realindex = 0;
            loopv(i, sys->imagelist) {
                Image &image = *sys->imagelist[i];
                if (image.trefc) {
                    fos.Write("I", 1);
                    sos.WriteDouble(image.display_scale);
                    if (image.png_data.empty()) {
                        wxImage im = image.bm_orig.ConvertToImage();
                        im.SaveFile(fos, wxBITMAP_TYPE_PNG);
                    } else {
                        // We have a copy of the PNG data loaded.. this is WAY faster
                        // than recompressing (~30x on image heavy files).
                        fos.Write(image.png_data.data(), image.png_data.size());
                    }
                    image.savedindex = realindex++;
                }
            }
            fos.Write("D", 1);
            wxZlibOutputStream zos(fos, 9);
            if (!zos.IsOk()) return _(L"Zlib error while writing file.");
            wxDataOutputStream dos(zos);
            rootgrid->Save(dos);
            for (auto tagit = tags.begin(); tagit != tags.end(); ++tagit) {
                dos.WriteString(tagit->first);
            }
            dos.WriteString(wxEmptyString);
        }
        lastmodsinceautosave = 0;
        lastsave = wxGetLocalTime();
        auto end_saving_time = wxGetLocalTimeMillis();

        if (!istempfile) {
            undolistsizeatfullsave = undolist.size();
            modified = false;
            tmpsavesuccess = true;
            sys->FileUsed(filename, this);
            if (::wxFileExists(sys->TmpName(filename))) ::wxRemoveFile(sys->TmpName(filename));
        }
        if (sys->autohtmlexport) { ExportFile(sys->ExtName(filename, L".html"), A_EXPHTMLT, false); }
        UpdateFileName(page);
        if (success) *success = true;

        sw->Status(
            wxString::Format(_(L"Saved %s successfully (in %d milliseconds)."),
                             filename.c_str(), (int)((end_saving_time - start_saving_time).GetValue()))
                .c_str());

        return _(L"");
    }

    void DrawSelect(wxDC &dc, Selection &s, bool refreshinstead = false, bool cursoronly = false) {
        #ifdef SIMPLERENDER
        if (refreshinstead) {
            Refresh();
            return;
        }
        #endif
        if (!s.g) return;
        ResetFont();
        s.g->DrawSelect(this, dc, s, cursoronly);
    }

    void DrawSelectMove(wxDC &dc, Selection &s, bool refreshalways = false,
                        bool refreshinstead = true) {
        if (ScrollIfSelectionOutOfView(dc, s)) return;
        if (refreshalways)
            RefreshReset();
        else
            DrawSelect(dc, s, refreshinstead);
    }

    bool ScrollIfSelectionOutOfView(wxDC &dc, Selection &s, bool refreshalways = false) {
        if (!scaledviewingmode) {
            // required, since sizes of things may have been reset by the last editing operation
            Layout(dc);
            int canvasw, canvash;
            sw->GetClientSize(&canvasw, &canvash);
            if ((layoutys > canvash || layoutxs > canvasw) && s.g) {
                wxRect r = s.g->GetRect(this, s, true);
                if (r.y < originy || r.y + r.height > maxy || r.x < originx ||
                    r.x + r.width > maxx) {
                    int curx, cury;
                    sw->GetViewStart(&curx, &cury);
                    sw->SetScrollbars(1, 1, layoutxs, layoutys,
                                      r.width > canvasw || r.x < originx
                                          ? r.x
                                          : r.x + r.width > maxx ? r.x + r.width - canvasw : curx,
                                      r.height > canvash || r.y < originy
                                          ? r.y
                                          : r.y + r.height > maxy ? r.y + r.height - canvash : cury,
                                      true);
                    RefreshReset();
                    return true;
                }
            }
        }
        if (refreshalways) Refresh();
        return refreshalways;
    }

    void ScrollOrZoom(wxDC &dc, bool zoomiftiny = false) {
        if (!selected.g) return;
        Cell *drawroot = WalkPath(drawpath);
        // If we jumped to a cell which may be insided a folded cell, we have to unfold it
        // because the rest of the code doesn't deal with a selection that is invisible :)
        for (Cell *cg = selected.g->cell; cg; cg = cg->parent) {
            // Unless we're under the drawroot, no need to unfold further.
            if (cg == drawroot) break;
            if (cg->grid->folded) {
                cg->grid->folded = false;
                cg->ResetLayout();
                cg->ResetChildren();
            }
        }
        for (Cell *cg = selected.g->cell; cg; cg = cg->parent)
            if (cg == drawroot) {
                if (zoomiftiny) ZoomTiny(dc);
                DrawSelectMove(dc, selected, true);
                return;
            }
        Zoom(-100, dc, false, false);
        if (zoomiftiny) ZoomTiny(dc);
        DrawSelectMove(dc, selected, true);
    }

    void ZoomTiny(wxDC &dc) {
        Cell *c = selected.GetCell();
        if (c && c->tiny) {
            Zoom(1, dc);  // seems to leave selection box in a weird location?
            if (selected.GetCell() != c) ZoomTiny(dc);
        }
    }

    void Blink() {
        if (redrawpending) return;
        #ifndef SIMPLERENDER
        wxClientDC dc(sw);
        sw->DoPrepareDC(dc);
        ShiftToCenter(dc);
        DrawSelect(dc, selected, false, true);
        blink = !blink;
        DrawSelect(dc, selected, true, true);
        #endif
    }

    void ResetCursor() {
        if (selected.g) selected.SetCursorEdit(this, selected.TextEdit());
    }

    void Hover(int x, int y, wxDC &dc) {
        if (redrawpending) return;
        ShiftToCenter(dc);
        ResetFont();
        Selection prev = hover;
        hover = Selection();
        auto drawroot = WalkPath(drawpath);
        if (drawroot->grid) drawroot->grid->FindXY(this, x - centerx / currentviewscale - hierarchysize,
                                                   y - centery / currentviewscale - hierarchysize, dc);
        if (!(prev == hover)) {
            if (prev.g) prev.g->DrawHover(this, dc, prev);
            if (hover.g) hover.g->DrawHover(this, dc, hover);
        }
        sys->UpdateStatus(hover);
    }

    void Select(wxDC &dc, bool right, int isctrlshift) {
        begindrag = Selection();
        if (right && hover.IsInside(selected)) return;
        ShiftToCenter(dc);
        DrawSelect(dc, selected);
        if (selected.GetCell() == hover.GetCell() && hover.GetCell()) hover.EnterEditOnly(this);
        selected = hover;
        begindrag = hover;
        isctrlshiftdrag = isctrlshift;
        DrawSelectMove(dc, selected);
        ResetCursor();
        return;
    }

    void SelectUp() {
        if (!isctrlshiftdrag || isctrlshiftdrag == 3 || begindrag.EqLoc(selected)) return;
        Cell *c = selected.GetCell();
        if (!c) return;
        Cell *tc = begindrag.ThinExpand(this);
        selected = begindrag;
        if (tc) {
            auto is_parent = tc->IsParentOf(c);
            auto tc_parent = tc->parent;  // tc may be deleted.
            tc->Paste(this, c, begindrag);
            // If is_parent, c has been deleted already.
            if (isctrlshiftdrag == 1 && !is_parent) {
                c->parent->AddUndo(this);
                Selection cs = c->parent->grid->FindCell(c);
                c->parent->grid->MultiCellDeleteSub(this, cs);
            }
            hover = selected = tc_parent ? tc_parent->grid->FindCell(tc) : Selection();
        }
        Refresh();
    }

    void Drag(wxDC &dc) {
        if (!selected.g || !hover.g || !begindrag.g) return;
        if (isctrlshiftdrag) {
            begindrag = hover;
            return;
        }
        if (hover.Thin()) return;
        ShiftToCenter(dc);
        if (begindrag.Thin() || selected.Thin()) {
            DrawSelect(dc, selected);
            begindrag = selected = hover;
            DrawSelect(dc, selected, true);
        } else {
            Selection old = selected;
            selected.Merge(begindrag, hover);
            if (!(old == selected)) {
                DrawSelect(dc, old);
                DrawSelect(dc, selected, true);
            }
        }
        ResetCursor();
        return;
    }

    void Zoom(int dir, wxDC &dc, bool fromroot = false, bool selectionmaybedrawroot = true) {
        int len = max(0, (fromroot ? 0 : drawpath.size()) + dir);
        if (!len && !drawpath.size()) return;
        if (dir > 0) {
            if (!selected.g) return;
            Cell *c = selected.GetCell();
            CreatePath(c && c->grid ? c : selected.g->cell, drawpath);
        } else if (dir < 0) {
            Cell *drawroot = WalkPath(drawpath);
            if (drawroot->grid && drawroot->grid->folded && selectionmaybedrawroot)
                selected = drawroot->parent->grid->FindCell(drawroot);
        }
        while (len < drawpath.size()) drawpath.remove(0);
        Cell *drawroot = WalkPath(drawpath);
        if (selected.GetCell() == drawroot && drawroot->grid) {
            // We can't have the drawroot selected, so we must move the selection to the children.
            selected = Selection(drawroot->grid, 0, 0, drawroot->grid->xs, drawroot->grid->ys);
        }
        drawroot->ResetLayout();
        drawroot->ResetChildren();
        Layout(dc);
        DrawSelectMove(dc, selected, true, false);
    }

    const wxChar *NoSel()   { return _(L"This operation requires a selection."); }
    const wxChar *OneCell() { return _(L"This operation works on a single selected cell only."); }
    const wxChar *NoThin()  { return _(L"This operation doesn't work on thin selections."); }
    const wxChar *NoGrid()  { return _(L"This operation requires a cell that contains a grid."); }

    const wxChar *Wheel(wxDC &dc, int dir, bool alt, bool ctrl, bool shift,
                      bool hierarchical = true) {
        if (!dir) return nullptr;
        ShiftToCenter(dc);
        if (alt) {
            if (!selected.g) return NoSel();
            if (selected.xs > 0) {
                if (!LastUndoSameCellAny(selected.g->cell)) selected.g->cell->AddUndo(this);
                selected.g->ResizeColWidths(dir, selected, hierarchical);
                selected.g->cell->ResetLayout();
                selected.g->cell->ResetChildren();
                sys->UpdateStatus(selected);
                Refresh();
                return dir > 0 ? _(L"Column width increased.") : _(L"Column width decreased.");
            }
            return L"nothing to resize";
        } else if (shift) {
            if (!selected.g) return NoSel();
            selected.g->cell->AddUndo(this);
            selected.g->ResetChildren();
            selected.g->RelSize(-dir, selected, pathscalebias);
            sys->UpdateStatus(selected);
            Refresh();
            return dir > 0 ? _(L"Text size increased.") : _(L"Text size decreased.");
        } else if (ctrl) {
            int steps = abs(dir);
            dir = sign(dir);
            loop(i, steps) Zoom(dir, dc);
            return dir > 0 ? _(L"Zoomed in.") : _(L"Zoomed out.");
        } else {
            ASSERT(0);
            return nullptr;
        }
    }

    void Layout(wxDC &dc) {
        ResetFont();
        dc.SetUserScale(1, 1);
        curdrawroot = WalkPath(drawpath);
        int psb = curdrawroot == rootgrid ? 0 : curdrawroot->MinRelsize();
        if (psb < 0 || psb == INT_MAX) psb = 0;
        if (psb != pathscalebias) curdrawroot->ResetChildren();
        pathscalebias = psb;
        curdrawroot->LazyLayout(this, dc, 0, curdrawroot->ColWidth(), false);
        ResetFont();
        PickFont(dc, 0, 0, 0);
        hierarchysize = 0;
        for (Cell *p = curdrawroot->parent; p; p = p->parent)
            if (p->text.t.Len()) hierarchysize += dc.GetCharHeight();
        hierarchysize += fgutter;
        layoutxs = curdrawroot->sx + hierarchysize + fgutter;
        layoutys = curdrawroot->sy + hierarchysize + fgutter;
    }

    void ShiftToCenter(wxDC &dc) {
        int dlx = dc.DeviceToLogicalX(0);
        int dly = dc.DeviceToLogicalY(0);
        dc.SetDeviceOrigin(dlx > 0 ? -dlx : centerx, dly > 0 ? -dly : centery);
        dc.SetUserScale(currentviewscale, currentviewscale);
    }

    void Render(wxDC &dc) {
        ResetFont();
        PickFont(dc, 0, 0, 0);
        dc.SetTextForeground(*wxLIGHT_GREY);
        int i = 0;
        for (Cell *p = curdrawroot->parent; p; p = p->parent)
            if (p->text.t.Len()) {
                int off = hierarchysize - dc.GetCharHeight() * ++i;
                wxString s = p->text.t;
                if ((int)s.Len() > sys->defaultmaxcolwidth) {
                    // should take the width of these into account for layoutys, but really, the
                    // worst that can happen on a thin window is that its rendering gets cut off
                    s = s.Left(sys->defaultmaxcolwidth) + L"...";
                }
                dc.DrawText(s, off, off);
            }
        dc.SetTextForeground(*wxBLACK);
        curdrawroot->Render(this, hierarchysize, hierarchysize, dc, 0, 0, 0, 0, 0,
                            curdrawroot->ColWidth(), 0);
    }

    void Draw(wxDC &dc) {
        redrawpending = false;
        dc.SetBackground(wxBrush(wxColor(Background())));
        dc.Clear();
        if (!rootgrid) return;
        sw->GetClientSize(&maxx, &maxy);
        Layout(dc);
        double xscale = maxx / (double)layoutxs;
        double yscale = maxy / (double)layoutys;
        currentviewscale = min(xscale, yscale);
        if (currentviewscale > 5)
            currentviewscale = 5;
        else if (currentviewscale < 1)
            currentviewscale = 1;
        if (scaledviewingmode && currentviewscale > 1) {
            sw->SetVirtualSize(maxx, maxy);
            dc.SetUserScale(currentviewscale, currentviewscale);
            maxx /= currentviewscale;
            maxy /= currentviewscale;
            originx = originy = 0;
        } else {
            currentviewscale = 1;
            dc.SetUserScale(1, 1);
            int drx = max(layoutxs, maxx);
            int dry = max(layoutys, maxy);
            sw->SetVirtualSize(drx, dry);
            sw->CalcUnscrolledPosition(0, 0, &originx, &originy);
            maxx += originx;
            maxy += originy;
        }
        centerx = sys->centered && !originx && maxx > layoutxs
                      ? (maxx - layoutxs) / 2 * currentviewscale
                      : 0;
        centery = sys->centered && !originy && maxy > layoutys
                      ? (maxy - layoutys) / 2 * currentviewscale
                      : 0;
        sw->DoPrepareDC(dc);
        ShiftToCenter(dc);
        Render(dc);
        DrawSelect(dc, selected);
        if (hover.g) hover.g->DrawHover(this, dc, hover);
        if (scaledviewingmode) { dc.SetUserScale(1, 1); }
    }

    void Print(wxDC &dc, wxPrintout &po) {
        Layout(dc);
        maxx = layoutxs;
        maxy = layoutys;
        originx = originy = 0;
        po.FitThisSizeToPage(printscale ? wxSize(printscale, 1) : wxSize(maxx, maxy));
        wxRect fitRect = po.GetLogicalPageRect();
        wxCoord xoff = (fitRect.width - maxx) / 2;
        wxCoord yoff = (fitRect.height - maxy) / 2;
        po.OffsetLogicalOrigin(xoff, yoff);
        while_printing = true;
        Render(dc);
        while_printing = false;
    }

    int TextSize(int depth, int relsize) {
        return max(g_mintextsize(), g_deftextsize - depth - relsize + pathscalebias);
    }

    bool FontIsMini(int textsize) { return textsize == g_mintextsize(); }

    bool PickFont(wxDC &dc, int depth, int relsize, int stylebits) {
        int textsize = TextSize(depth, relsize);
        if (textsize != lasttextsize || stylebits != laststylebits) {
            wxFont font(textsize - (while_printing || scaledviewingmode),
                   stylebits & STYLE_FIXED ? wxFONTFAMILY_TELETYPE : wxFONTFAMILY_DEFAULT,
                   stylebits & STYLE_ITALIC ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
                   stylebits & STYLE_BOLD ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
                   (stylebits & STYLE_UNDERLINE) != 0,
                   stylebits & STYLE_FIXED ? wxString(L"") : sys->defaultfont);
            if (stylebits & STYLE_STRIKETHRU) font.SetStrikethrough(true);
            dc.SetFont(font);
            lasttextsize = textsize;
            laststylebits = stylebits;
        }
        return FontIsMini(textsize);
    }

    void ResetFont() {
        lasttextsize = INT_MAX;
        laststylebits = -1;
    }

    void RefreshReset() {
        Refresh();
    }

    void Refresh() {
        hover.g = nullptr;
        RefreshHover();
    }

    void RefreshHover() {
        redrawpending = true;
        #ifndef __WXMSW__
        if (sw) sw->Refresh(false);
        #endif
        sys->UpdateStatus(selected);
        sys->frame->nb->Refresh(false);
    }

    void ClearSelectionRefresh() {
        selected.g = nullptr;
        Refresh();
    }

    bool CheckForChanges() {
        if (modified) {
            ThreeChoiceDialog tcd(sys->frame, filename,
                                  _(L"Changes have been made, are you sure you wish to continue?"),
                                  _(L"Save and Close"), _(L"Discard Changes"), _(L"Cancel"));
            switch (tcd.Run()) {
                case 0: {
                    bool success = false;
                    Save(false, &success);
                    return !success;
                }
                case 1: return false;
                default:
                case 2: return true;
            }
        }
        return false;
    }

    bool CloseDocument() {
        bool keep = CheckForChanges();
        if (!keep && !filename.empty() && ::wxFileExists(sys->TmpName(filename)))
            ::wxRemoveFile(sys->TmpName(filename));
        return keep;
    }

    const wxChar *DoubleClick(wxDC &dc) {
        if (!selected.g) return nullptr;
        ShiftToCenter(dc);
        Cell *c = selected.GetCell();
        if (selected.Thin()) {
            selected.SelAll();
            Refresh();
        } else if (c) {
            DrawSelect(dc, selected);
            if (selected.TextEdit()) {
                c->text.SelectWord(selected);
                begindrag = selected;
            } else {
                selected.EnterEditOnly(this);
            }
            DrawSelect(dc, selected, true);
        }
        return nullptr;
    }

    const wxChar *Export(const wxChar *fmt, const wxChar *pat, const wxChar *msg, int k) {
        wxString fn = ::wxFileSelector(msg, L"", L"", fmt, pat,
                                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (fn.empty()) return _(L"Export cancelled.");
        return ExportFile(fn, k, true);
    }

    const wxChar *ExportFile(const wxString &fn, int k, bool currentview) {
        auto root = currentview ? curdrawroot : rootgrid;
        if (k == A_EXPCSV) {
            int maxdepth = 0, leaves = 0;
            root->MaxDepthLeaves(0, maxdepth, leaves);
            if (maxdepth > 1)
                return _(L"Cannot export grid that is not flat (zoom the view to the desired grid, and/or use Flatten).");
        }
        if (k == A_EXPIMAGE) {
            maxx = layoutxs;
            maxy = layoutys;
            originx = originy = 0;
            wxBitmap bm(maxx, maxy, 24);
            wxMemoryDC mdc(bm);
            DrawRectangle(mdc, Background(), 0, 0, maxx, maxy);
            Render(mdc);
            Refresh();
            if (!bm.SaveFile(fn, wxBITMAP_TYPE_PNG)) return _(L"Error writing PNG file!");
        } else {
            wxFFileOutputStream fos(fn, L"w+b");
            if (!fos.IsOk()) {
                wxMessageBox(_(L"Error exporting file!"), fn.wx_str(), wxOK, sys->frame);
                return _(L"Error writing to file!");
            }
            wxTextOutputStream dos(fos);
            wxString content = root->ToText(0, Selection(), k, this);
            switch (k) {
                case A_EXPXML:
                    dos.WriteString(
                        L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        L"<!DOCTYPE cell [\n"
                        L"<!ELEMENT cell (grid)>\n"
                        L"<!ELEMENT grid (row*)>\n"
                        L"<!ELEMENT row (cell*)>\n"
                        L"]>\n");
                    dos.WriteString(content);
                    break;
                case A_EXPHTMLT:
                case A_EXPHTMLB:
                case A_EXPHTMLO:
                    dos.WriteString(
                        L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n"
                        L"<html>\n<head>\n<style>\n"
                        L"body { font-family: sans-serif; }\n"
                        L"table, th, td { border: 1px solid grey; border-collapse: collapse;"
                        L" padding: 3px; }\n"
                        L"li { }\n</style>\n"
                        L"<title>export of TreeSheets file ");
                    dos.WriteString(filename);
                    dos.WriteString(
                        L"</title>\n<meta http-equiv=\"Content-Type\" content=\"text/html; "
                        L"charset=UTF-8\" "
                        L"/>\n</head>\n<body>\n");
                    dos.WriteString(content);
                    dos.WriteString(L"</body>\n</html>\n");
                    break;
                case A_EXPCSV:
                case A_EXPTEXT: dos.WriteString(content); break;
            }
        }
        return _(L"File exported successfully.");
    }

    const wxChar *Save(bool saveas, bool *success = nullptr) {
        if (!saveas && !filename.empty()) { return SaveDB(success); }
        wxString fn =
            ::wxFileSelector(_(L"Choose TreeSheets file to save:"), L"", L"", L"cts",
                             L"TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*",
                             wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (fn.empty()) return _(L"Save cancelled.");  // avoid name being set to ""
        ChangeFileName(fn, true);
        return SaveDB(success);
    }

    void AutoSave(bool minimized, int page) {
        if (sys->autosave && tmpsavesuccess && !filename.empty() && lastmodsinceautosave &&
            (lastmodsinceautosave + 60 < wxGetLocalTime() || lastsave + 300 < wxGetLocalTime() ||
             minimized)) {
            tmpsavesuccess = false;
            SaveDB(&tmpsavesuccess, true, page);
        }
    }

    const wxChar *Key(wxDC &dc, wxChar uk, int k, bool alt, bool ctrl, bool shift,
                    bool &unprocessed) {
        if (uk == WXK_NONE || (k < ' ' && k)) {
            switch (k) {
                case WXK_BACK:  // no menu shortcut available in wxwidgets
                    return Action(dc, A_BACKSPACE);
                case WXK_RETURN: return Action(dc, shift ? A_ENTERGRID : A_ENTERCELL);
                case WXK_ESCAPE:  // docs say it can be used as a menu accelerator, but it does not
                                  // trigger from there?
                    return Action(dc, A_CANCELEDIT);
                #ifdef WIN32  // works fine on Linux, not sure OS X
                case WXK_PAGEDOWN: sw->CursorScroll(0, g_scrollratecursor); return nullptr;
                case WXK_PAGEUP: sw->CursorScroll(0, -g_scrollratecursor); return nullptr;
                #endif
                #ifdef __WXGTK__
                // should not be needed... on Windows / OS X they arrive as menu event and never
                // arrive here, on Linux
                // we have to process these manually?
                case WXK_LEFT:
                    return Action(dc,
                                  shift ? (ctrl ? A_SCLEFT : A_SLEFT) : (ctrl ? A_MLEFT : A_LEFT));
                case WXK_RIGHT:
                    return Action(
                        dc, shift ? (ctrl ? A_SCRIGHT : A_SRIGHT) : (ctrl ? A_MRIGHT : A_RIGHT));
                case WXK_UP:
                    return Action(dc, shift ? (ctrl ? A_SCUP : A_SUP) : (ctrl ? A_MUP : A_UP));
                case WXK_DOWN:
                    return Action(dc,
                                  shift ? (ctrl ? A_SCDOWN : A_SDOWN) : (ctrl ? A_MDOWN : A_DOWN));
                case WXK_HOME:
                    return Action(dc,
                                  shift ? (ctrl ? A_SHOME : A_SHOME) : (ctrl ? A_CHOME : A_HOME));
                case WXK_END:
                    return Action(dc, shift ? (ctrl ? A_SEND : A_SEND) : (ctrl ? A_CEND : A_END));
                case WXK_TAB:
                    if (ctrl && !shift) {
                        // WXK_CONTROL_I (italics) arrives as the same keycode as WXK_TAB + ctrl on
                        // Linux??
                        // They're both keycode 9 in defs.h
                        // We ignore it here, such that CTRL+I works, but it means only
                        // CTRL+SHIFT+TAB works on Linux as
                        // a way to switch tabs.
                        // Also, even though we ignore CTRL+TAB, and it is not assigned in the
                        // menus, it still has the
                        // effect of de-selecting
                        // the current tab (requires a click to re-activate). FIXME??
                        break;
                    }
                    return Action(
                        dc, shift ? (ctrl ? A_PREVFILE : A_PREV) : (ctrl ? A_NEXTFILE : A_NEXT));
                #endif
            }
        } else if (uk >= ' ') {
            if (!selected.g) return NoSel();
            Cell *c = selected.ThinExpand(this);
            if (!c) return OneCell();
            ShiftToCenter(dc);
            c->AddUndo(this);  // FIXME: not needed for all keystrokes, or at least, merge all
                               // keystroke undos within same cell
            c->text.Key(this, uk, selected);
            ScrollIfSelectionOutOfView(dc, selected, true);
            return nullptr;
        }
        unprocessed = true;
        return nullptr;
    }

    const wxChar* CopyImageToClipboard(Cell *cell) {
        if (wxTheClipboard->Open()) {
            #ifdef __WXGTK__
            if (!cell->text.image->png_data.empty()) {
                wxCustomDataObject *pngimage = new wxCustomDataObject(wxDF_BITMAP);
                pngimage->SetData(cell->text.image->png_data.size(), cell->text.image->png_data.data());
                wxTheClipboard->SetData(pngimage);
            } else
            #endif
            {
                wxTheClipboard->SetData(new wxBitmapDataObject(cell->text.image->bm_orig));    
            }
            wxTheClipboard->Close();
        }
        return _(L"Image copied to clipboard");
    }

    const wxChar *Action(wxDC &dc, int k) {
        ShiftToCenter(dc);

        switch (k) {
            case A_RUN:
                sys->ev.Eval(rootgrid);
                rootgrid->ResetChildren();
                ClearSelectionRefresh();
                return _(L"Evaluation finished.");

            case A_UNDO:
                if (undolist.size()) {
                    Undo(dc, undolist, redolist);
                    return nullptr;
                } else {
                    return _(L"Nothing more to undo.");
                }

            case A_REDO:
                if (redolist.size()) {
                    Undo(dc, redolist, undolist, true);
                    return nullptr;
                } else {
                    return _(L"Nothing more to redo.");
                }

            case A_SAVE: return Save(false);
            case A_SAVEAS: return Save(true);

            case A_EXPXML: return Export(L"xml", L"*.xml", _(L"Choose XML file to write"), k);
            case A_EXPHTMLT:
            case A_EXPHTMLB:
            case A_EXPHTMLO: return Export(L"html", L"*.html", _(L"Choose HTML file to write"), k);
            case A_EXPTEXT: return Export(L"txt", L"*.txt", _(L"Choose Text file to write"), k);
            case A_EXPIMAGE: return Export(L"png", L"*.png", _(L"Choose PNG file to write"), k);
            case A_EXPCSV: return Export(L"csv", L"*.csv", _(L"Choose CSV file to write"), k);

            case A_IMPXML:
            case A_IMPXMLA:
            case A_IMPTXTI:
            case A_IMPTXTC:
            case A_IMPTXTS:
            case A_IMPTXTT: return sys->Import(k);

            case A_OPEN: {
                wxString fn =
                    ::wxFileSelector(_(L"Please select a TreeSheets file to load:"), L"", L"",
                                     L"cts", L"TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*",
                                     wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
                return sys->Open(fn);
            }

            case A_CLOSE: {
                if (sys->frame->nb->GetPageCount() <= 1) {
                    sys->frame->fromclosebox = false;
                    sys->frame->Close();
                    return nullptr;
                }

                if (!CloseDocument()) {
                    int p = sys->frame->nb->GetSelection();
                    // sys->frame->nb->AdvanceSelection();
                    sys->frame->nb->DeletePage(p);
                    sys->RememberOpenFiles();
                }
                return nullptr;
            }

            case A_NEW: {
                int size = (int)::wxGetNumberFromUser(_(L"What size grid would you like to start with?"),
                                                 _(L"size:"), _(L"New Sheet"), 10, 1, 25, sys->frame);
                if (size < 0) return _(L"New file cancelled.");
                sys->InitDB(size);
                sys->frame->GetCurTab()->doc->Refresh();
                return nullptr;
            }

            case A_ABOUT: {
                wxAboutDialogInfo info;
                info.SetName(L"TreeSheets");
                info.SetVersion(wxT(PACKAGE_VERSION));
                info.SetCopyright(L"(C) 2009 Wouter van Oortmerssen");
                wxString desc = wxString::Format(L"%s\n\n%s " wxVERSION_STRING,
                                     _(L"The Free Form Hierarchical Information Organizer"), _(L"Uses"));
                info.SetDescription(desc);
                wxAboutBox(info);
                return nullptr;
            }

            case A_HELPI: sys->LoadTut(); return nullptr;

            case A_HELP_OP_REF: sys->LoadOpRef(); return nullptr;

            case A_HELP:
                #ifdef __WXMAC__
                wxLaunchDefaultBrowser(L"file://" +
                                       sys->frame->GetDocPath(L"docs/tutorial.html"));  // RbrtPntn
                #else
                wxLaunchDefaultBrowser(sys->frame->GetDocPath(L"docs/tutorial.html"));
                #endif
                return nullptr;

            case A_ZOOMIN:
                return Wheel(dc, 1, false, true,
                             false);  // Zoom( 1, dc); return "zoomed in (menu)";
            case A_ZOOMOUT:
                return Wheel(dc, -1, false, true,
                             false);  // Zoom(-1, dc); return "zoomed out (menu)";
            case A_INCSIZE: return Wheel(dc, 1, false, false, true);
            case A_DECSIZE: return Wheel(dc, -1, false, false, true);
            case A_INCWIDTH: return Wheel(dc, 1, true, false, false);
            case A_DECWIDTH: return Wheel(dc, -1, true, false, false);
            case A_INCWIDTHNH: return Wheel(dc, 1, true, false, false, false);
            case A_DECWIDTHNH: return Wheel(dc, -1, true, false, false, false);

            case A_DEFFONT: {
                wxFontData fdat;
                fdat.SetInitialFont(wxFont(g_deftextsize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                                           wxFONTWEIGHT_NORMAL, false, sys->defaultfont));
                wxFontDialog fd(sys->frame, fdat);
                if (fd.ShowModal() == wxID_OK) {
                    wxFont font = fd.GetFontData().GetChosenFont();
                    sys->defaultfont = font.GetFaceName();
                    g_deftextsize = min(20, max(10, font.GetPointSize()));
                    sys->cfg->Write(L"defaultfont", sys->defaultfont);
                    sys->cfg->Write(L"defaultfontsize", g_deftextsize);
                    // rootgrid->ResetChildren();
                    sys->frame->TabsReset();  // ResetChildren on all
                    Refresh();
                }
                return nullptr;
            }

            case A_PRINT: {
                wxPrintDialogData printDialogData(printData);
                wxPrinter printer(&printDialogData);
                MyPrintout printout(this);
                if (printer.Print(sys->frame, &printout, true)) {
                    printData = printer.GetPrintDialogData().GetPrintData();
                }
                return nullptr;
            }

            case A_PRINTSCALE: {
                printscale = (uint)::wxGetNumberFromUser(
                    _(L"How many pixels wide should a page be? (0 for auto fit)"), _(L"scale:"),
                    _(L"Set Print Scale"), 0, 0, 5000, sys->frame);
                return nullptr;
            }

            case A_PREVIEW: {
                wxPrintDialogData printDialogData(printData);
                wxPrintPreview *preview = new wxPrintPreview(
                    new MyPrintout(this), new MyPrintout(this), &printDialogData);
                wxPreviewFrame *pframe = new wxPreviewFrame(preview, sys->frame, _(L"Print Preview"),
                                                            wxPoint(100, 100), wxSize(600, 650));
                pframe->Centre(wxBOTH);
                pframe->Initialize();
                pframe->Show(true);
                return nullptr;
            }

            case A_PAGESETUP: {
                pageSetupData = printData;
                wxPageSetupDialog pageSetupDialog(sys->frame, &pageSetupData);
                pageSetupDialog.ShowModal();
                printData = pageSetupDialog.GetPageSetupDialogData().GetPrintData();
                pageSetupData = pageSetupDialog.GetPageSetupDialogData();
                return nullptr;
            }

            case A_NEXTFILE: sys->frame->CycleTabs(1); return nullptr;
            case A_PREVFILE: sys->frame->CycleTabs(-1); return nullptr;

            case A_CUSTCOL: {
                uint c = PickColor(sys->frame, sys->customcolor);
                if (c != (uint)-1) sys->customcolor = c;
                return nullptr;
            }

            case A_DEFBGCOL: {
                uint oldbg = Background();
                uint c = PickColor(sys->frame, oldbg);
                if (c != (uint)-1) {
                    rootgrid->AddUndo(this);
                    loopallcells(lc) {
                        if (lc->cellcolor == oldbg && (!lc->parent || lc->parent->cellcolor == c))
                            lc->cellcolor = c;
                    }
                    Refresh();
                }
                return nullptr;
            }

            case A_SEARCHNEXT: {
                return SearchNext(dc);
            }

            case A_ROUND0:
            case A_ROUND1:
            case A_ROUND2:
            case A_ROUND3:
            case A_ROUND4:
            case A_ROUND5:
            case A_ROUND6:
                sys->cfg->Write(L"roundness", long(sys->roundness = k - A_ROUND0));
                Refresh();
                return nullptr;

            case A_OPENCELLCOLOR:
                if (sys->frame->celldd) sys->frame->celldd->ShowPopup();
                break;
            case A_OPENTEXTCOLOR:
                if (sys->frame->textdd) sys->frame->textdd->ShowPopup();
                break;
            case A_OPENBORDCOLOR:
                if (sys->frame->borddd) sys->frame->borddd->ShowPopup();
                break;

            case A_REPLACEALL: {
                if (!sys->searchstring.Len()) return _(L"No search.");
                rootgrid->AddUndo(this);  // expensive?
                rootgrid->FindReplaceAll(sys->frame->replaces->GetValue());
                rootgrid->ResetChildren();
                Refresh();
                return nullptr;
            }

            case A_SCALED:
                scaledviewingmode = !scaledviewingmode;
                rootgrid->ResetChildren();
                Refresh();
                return scaledviewingmode ? _(L"Now viewing TreeSheet to fit to the screen exactly, press F12 to return to normal.")
                                         : _(L"1:1 scale restored.");

            case A_FILTER5:
                editfilter = 5;
                ApplyEditFilter();
                return nullptr;
            case A_FILTER10:
                editfilter = 10;
                ApplyEditFilter();
                return nullptr;
            case A_FILTER20:
                editfilter = 20;
                ApplyEditFilter();
                return nullptr;
            case A_FILTER50:
                editfilter = 50;
                ApplyEditFilter();
                return nullptr;
            case A_FILTERM:
                editfilter++;
                ApplyEditFilter();
                return nullptr;
            case A_FILTERL:
                editfilter--;
                ApplyEditFilter();
                return nullptr;
            case A_FILTERS: SetSearchFilter(true); return nullptr;
            case A_FILTEROFF: SetSearchFilter(false); return nullptr;

            case A_CUSTKEY: {
                wxArrayString strs;
                wxArrayString keys;
                for (auto it = sys->frame->menustrings.begin(); it != sys->frame->menustrings.end();
                     ++it) {
                    strs.push_back(it->first);
                    keys.push_back(it->second);
                }
                wxSingleChoiceDialog choice(
                    sys->frame, _(L"Please pick a menu item to change the key binding for"),
                    _(L"Key binding"), strs);
                choice.SetSize(wxSize(500, 700));
                choice.Centre();
                if (choice.ShowModal() == wxID_OK) {
                    int sel = choice.GetSelection();
                    wxTextEntryDialog textentry(sys->frame,
                                                "Please enter the new key binding string",
                                                "Key binding", keys[sel]);
                    if (textentry.ShowModal() == wxID_OK) {
                        wxString key = textentry.GetValue();
                        sys->frame->menustrings[strs[sel]] = key;
                        sys->cfg->Write(strs[sel], key);
                        return _(L"NOTE: key binding will take effect next run of TreeSheets.");
                    }
                }
                return _(L"Keybinding cancelled.");
            }
        }

        if (!selected.g) return NoSel();

        Cell *c = selected.GetCell();

        switch (k) {
            case A_BACKSPACE:
                if (selected.Thin()) {
                    if (selected.xs)
                        DelRowCol(selected.y, 0, selected.g->ys, 1, -1, selected.y - 1, 0, -1);
                    else
                        DelRowCol(selected.x, 0, selected.g->xs, 1, selected.x - 1, -1, -1, 0);
                } else if (c && selected.TextEdit()) {
                    if (selected.cursorend == 0) return nullptr;
                    c->AddUndo(this);
                    c->text.Backspace(selected);
                    Refresh();
                } else
                    selected.g->MultiCellDelete(this, selected);
                ZoomOutIfNoGrid(dc);
                return nullptr;

            case A_DELETE:
                if (selected.Thin()) {
                    if (selected.xs)
                        DelRowCol(selected.y, selected.g->ys, selected.g->ys, 0, -1, selected.y, 0,
                                  -1);
                    else
                        DelRowCol(selected.x, selected.g->xs, selected.g->xs, 0, selected.x, -1, -1,
                                  0);
                } else if (c && selected.TextEdit()) {
                    if (selected.cursor == c->text.t.Len()) return nullptr;
                    c->AddUndo(this);
                    c->text.Delete(selected);
                    Refresh();
                } else
                    selected.g->MultiCellDelete(this, selected);
                ZoomOutIfNoGrid(dc);
                return nullptr;

            case A_DELETE_WORD:
                if (c && selected.TextEdit()) {
                    if (selected.cursor == c->text.t.Len()) return nullptr;
                    c->AddUndo(this);
                    c->text.DeleteWord(selected);
                    Refresh();
                }
                ZoomOutIfNoGrid(dc);
                return nullptr;

            case A_COPYCT:
            case A_CUT:
            case A_COPY:
                sys->clipboardcopy = wxEmptyString;
                if (selected.Thin()) return NoThin();

                if (selected.TextEdit()) {
                    if (selected.cursor == selected.cursorend) return _(L"No text selected.");
                    sys->cellclipboard = nullptr;
                } else if (k != A_COPYCT) {
                    sys->cellclipboard = c ? c->Clone(nullptr) : selected.g->CloneSel(selected);
                }

                const wxChar *returnmessage;
                if(c and c->text.image and !c->text.t) {
                    returnmessage = CopyImageToClipboard(c);
                } else {
                    if (wxTheClipboard->Open()) {
                        wxString s;
                        if (k == A_COPYCT) {
                            loopallcellssel(c, true) if (c->text.t.Len()) s += c->text.t + " ";
                        } else {
                            s = selected.g->ConvertToText(selected, 0, A_EXPTEXT, this);
                        }
                        sys->clipboardcopy = s;
                        wxTheClipboard->SetData(new wxTextDataObject(s));
                        wxTheClipboard->Close();
                        returnmessage = _(L"Text copied to clipboard");
                    }
                }

                if (k == A_CUT) {
                    if (!selected.TextEdit()) {
                        selected.g->cell->AddUndo(this);
                        selected.g->MultiCellDelete(this, selected);
                    } else if (c) {
                        c->AddUndo(this);
                        c->text.Backspace(selected);
                    }
                    Refresh();
                }
                ZoomOutIfNoGrid(dc);
                return returnmessage;

            case A_SELALL:
                selected.SelAll();
                Refresh();
                return nullptr;

            case A_UP:
            case A_DOWN:
            case A_LEFT:
            case A_RIGHT:
                selected.Cursor(this, k, false, false, dc);
                return nullptr;

            case A_MUP:
            case A_MDOWN:
            case A_MLEFT:
            case A_MRIGHT:
                selected.Cursor(this, k - A_MUP + A_UP, true, false, dc);
                return nullptr;

            case A_SUP:
            case A_SDOWN:
            case A_SLEFT:
            case A_SRIGHT:
                selected.Cursor(this, k - A_SUP + A_UP, false, true, dc);
                return nullptr;

            case A_SCLEFT:
            case A_SCRIGHT:
                selected.Cursor(this, k - A_SCUP + A_UP, true, true, dc);
                return nullptr;

            case A_BOLD:
                selected.g->SetStyle(this, selected, STYLE_BOLD);
                return nullptr;
            case A_ITALIC:
                selected.g->SetStyle(this, selected, STYLE_ITALIC);
                return nullptr;
            case A_TT:
                selected.g->SetStyle(this, selected, STYLE_FIXED);
                return nullptr;
            case A_UNDERL:
                selected.g->SetStyle(this, selected, STYLE_UNDERLINE);
                return nullptr;
            case A_STRIKET:
                selected.g->SetStyle(this, selected, STYLE_STRIKETHRU);
                return nullptr;

            case A_MARKDATA:
            case A_MARKVARD:
            case A_MARKVARU:
            case A_MARKVIEWH:
            case A_MARKVIEWV:
            case A_MARKCODE: {
                int newcelltype;
                switch (k) {
                    case A_MARKDATA: newcelltype = CT_DATA; break;
                    case A_MARKVARD: newcelltype = CT_VARD; break;
                    case A_MARKVARU: newcelltype = CT_VARU; break;
                    case A_MARKVIEWH: newcelltype = CT_VIEWH; break;
                    case A_MARKVIEWV: newcelltype = CT_VIEWV; break;
                    case A_MARKCODE: newcelltype = CT_CODE; break;
                }
                selected.g->cell->AddUndo(this);
                loopallcellssel(c, false) {
                    c->celltype =
                        (newcelltype == CT_CODE) ? sys->ev.InferCellType(c->text) : newcelltype;
                    Refresh();
                }
                return nullptr;
            }

            case A_CANCELEDIT:
                if (selected.TextEdit()) break;
                if (selected.g->cell->parent) {
                    selected = selected.g->cell->parent->grid->FindCell(selected.g->cell);
                } else {
                    selected.SelAll();
                }
                ScrollOrZoom(dc);
                return nullptr;

            case A_NEWGRID:
                if (!(c = selected.ThinExpand(this))) return OneCell();
                if (c->grid) {
                    selected = Selection(c->grid, 0, c->grid->ys, 1, 0);
                    ScrollOrZoom(dc, true);
                } else {
                    c->AddUndo(this);
                    c->AddGrid();
                    selected = Selection(c->grid, 0, 0, 1, 1);
                    DrawSelectMove(dc, selected, true);
                }
                return nullptr;

            case A_PASTE:
                if (!(c = selected.ThinExpand(this))) return OneCell();
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->GetData(*dataobjc);
                    PasteOrDrop();
                    wxTheClipboard->Close();
                } else if (sys->cellclipboard) {
                    c->Paste(this, sys->cellclipboard.get(), selected);
                    Refresh();
                }
                return nullptr;

            case A_PASTESTYLE:
                if (!sys->cellclipboard) return _(L"No style to paste.");
                selected.g->cell->AddUndo(this);
                selected.g->SetStyles(selected, sys->cellclipboard.get());
                selected.g->cell->ResetChildren();
                Refresh();
                return nullptr;

            case A_ENTERCELL:
            case A_PROGRESSCELL: {
                if (!(c = selected.ThinExpand(this))) return OneCell();
                if (selected.TextEdit()) {
                    selected.Cursor(this, (k==A_ENTERCELL ? A_DOWN : A_RIGHT), false, false, dc, true);
                } else {
                    selected.EnterEdit(this, 0, (int)c->text.t.Len());
                    DrawSelectMove(dc, selected, true);
                }
                return nullptr;
            }

            case A_IMAGE: {
                if (!(c = selected.ThinExpand(this))) return OneCell();
                wxString fn =
                    ::wxFileSelector(_(L"Please select an image file:"), L"", L"", L"", L"*.*",
                                     wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
                c->AddUndo(this);
                LoadImageIntoCell(fn, c, sys->frame->csf);
                Refresh();
                return nullptr;
            }

            case A_IMAGER: {
                selected.g->cell->AddUndo(this);
                selected.g->ClearImages(selected);
                selected.g->cell->ResetChildren();
                Refresh();
                return nullptr;
            }

            case A_SORTD: return Sort(true);
            case A_SORT: return Sort(false);

            case A_REPLACEONCE:
            case A_REPLACEONCEJ: {
                if (!sys->searchstring.Len()) return _(L"No search.");
                selected.g->ReplaceStr(this, sys->frame->replaces->GetValue(), selected);
                if (k == A_REPLACEONCEJ) return SearchNext(dc);
                return nullptr;
            }

            case A_SCOLS:
                selected.y = 0;
                selected.ys = selected.g->ys;
                Refresh();
                return nullptr;

            case A_SROWS:
                selected.x = 0;
                selected.xs = selected.g->xs;
                Refresh();
                return nullptr;

            case A_BORD0:
            case A_BORD1:
            case A_BORD2:
            case A_BORD3:
            case A_BORD4:
            case A_BORD5:
                selected.g->cell->AddUndo(this);
                selected.g->SetBorder(k - A_BORD0 + 1, selected);
                selected.g->cell->ResetChildren();
                Refresh();
                return nullptr;

            case A_TEXTGRID: return layrender(-1, true, true);

            case A_V_GS: return layrender(DS_GRID, true);
            case A_V_BS: return layrender(DS_BLOBSHIER, true);
            case A_V_LS: return layrender(DS_BLOBLINE, true);
            case A_H_GS: return layrender(DS_GRID, false);
            case A_H_BS: return layrender(DS_BLOBSHIER, false);
            case A_H_LS: return layrender(DS_BLOBLINE, false);
            case A_GS: return layrender(DS_GRID, true, false, true);
            case A_BS: return layrender(DS_BLOBSHIER, true, false, true);
            case A_LS: return layrender(DS_BLOBLINE, true, false, true);

            case A_WRAP: return selected.Wrap(this);

            case A_RESETSIZE:
            case A_RESETWIDTH:
            case A_RESETSTYLE:
            case A_RESETCOLOR:
            case A_LASTCELLCOLOR:
            case A_LASTTEXTCOLOR:
            case A_LASTBORDCOLOR:
                selected.g->cell->AddUndo(this);
                loopallcellssel(c, true) switch (k) {
                    case A_RESETSIZE:
                        c->text.relsize = 0;
                        break;
                    case A_RESETWIDTH:
                        if (c->grid) c->grid->InitColWidths();
                        break;
                    case A_RESETSTYLE:
                        c->text.stylebits = 0;
                        break;
                    case A_RESETCOLOR:
                        c->cellcolor = 0xFFFFFF;
                        c->textcolor = 0;
                        if (c->grid) c->grid->bordercolor = 0xA0A0A0;
                        break;
                    case A_LASTCELLCOLOR:
                        c->cellcolor = sys->lastcellcolor;
                        break;
                    case A_LASTTEXTCOLOR:
                        c->textcolor = sys->lasttextcolor;
                        break;
                    case A_LASTBORDCOLOR:
                        if (c->grid) c->grid->bordercolor = sys->lastbordcolor;
                        break;
                }
                selected.g->cell->ResetChildren();
                Refresh();
                return nullptr;

            case A_MINISIZE: {
                selected.g->cell->AddUndo(this);
                CollectCellsSel(false);
                Vector<Cell *> outer;
                outer.append(itercells);
                loopv(i, outer) {
                    Cell *o = outer[i];
                    if (o->grid) {
                        loopcellsin(o, c) if (_i) {
                            c->text.relsize = g_deftextsize - g_mintextsize() - c->Depth();
                        }
                    }
                }
                outer.setsize_nd(0);
                selected.g->cell->ResetChildren();
                Refresh();
                return nullptr;
            }

            case A_FOLD:
            case A_FOLDALL:
            case A_UNFOLDALL:
                loopallcellssel(c, k != A_FOLD) if (c->grid) {
                    c->AddUndo(this);
                    c->grid->folded = k == A_FOLD ? !c->grid->folded : k == A_FOLDALL;
                    c->ResetChildren();
                }
                Refresh();
                return nullptr;

            case A_HOME:
            case A_END:
            case A_CHOME:
            case A_CEND:
                if (selected.TextEdit()) break;
                selected.HomeEnd(this, dc, k == A_HOME || k == A_CHOME);
                return nullptr;


            case A_IMAGESCP:
            case A_IMAGESCF: {
                long v = 0;
                loopallcellssel(c, true) {
                    if(c->text.image) {
                        if(!v) {
                            v = wxGetNumberFromUser(
                                _(L"Please enter the percentage you want the image scaled by:"), L"%",
                                _(L"Image Resize"), 50, 5, 400, sys->frame);
                            if (v < 0) return nullptr;
                        }
                        auto sc = v / 100.0;
                        if (k == A_IMAGESCP) {
                            c->text.image->BitmapScale(sc);
                        } else {
                            c->text.image->DisplayScale(sc);
                        }
                        c->ResetLayout();
                    }
                }
                Refresh();
                return nullptr;
            }
        

            case A_IMAGESCN: {
                loopallcellssel(c, true)
                    if (c->text.image) {
                        c->text.image->ResetScale(sys->frame->csf);
                        c->ResetChildren();
                        c->ResetLayout();
                    }
                Refresh();
                return nullptr;
            }

            case A_BROWSE: {
                const wxChar *returnmessage = nullptr;
                int counter = 0;
                loopallcellssel(c, true) {
                    if (counter >= MAX_LAUNCHES) {
                        returnmessage = _(L"Maximum number of launches reached.");
                        break;
                    }
                    if (!wxLaunchDefaultBrowser(c->text.ToText(0, selected, A_EXPTEXT))) {
                        returnmessage = _(L"The browser could not open at least one link.");
                    } else {
                        counter++;
                    }
                }
                return returnmessage;
            }

            case A_BROWSEF: {
                const wxChar *returnmessage = nullptr;
                int counter = 0;
                loopallcellssel(c, true) {
                    if (counter >= MAX_LAUNCHES) {
                        returnmessage = _(L"Maximum number of launches reached.");
                        break;
                    }
                    wxString f = c->text.ToText(0, selected, A_EXPTEXT);
                    wxFileName fn(f);
                    if (fn.IsRelative()) fn.MakeAbsolute(wxFileName(filename).GetPath());
                    if (!wxLaunchDefaultApplication(fn.GetFullPath())) {
                        returnmessage = _(L"At least one file could not be opened.");
                    } else {
                        counter++;
                    }
                }
                return returnmessage;
            }
        }

        if (c || (!c && selected.IsAll())) {
            Cell *ac = c ? c : selected.g->cell;
            switch (k) {
                case A_TRANSPOSE:
                    if (ac->grid) {
                        ac->AddUndo(this);
                        ac->grid->Transpose();
                        ac->ResetChildren();
                        selected = ac->parent ? ac->parent->grid->FindCell(ac) : Selection();
                        Refresh();
                        return nullptr;
                    } else
                        return NoGrid();

                case A_HIFY:
                    if (!ac->grid) return NoGrid();
                    if (!ac->grid->IsTable())
                        return _(L"Selected grid is not a table: cells must not already have sub-grids.");
                    ac->AddUndo(this);
                    ac->grid->Hierarchify(this);
                    ac->ResetChildren();
                    ClearSelectionRefresh();
                    return nullptr;

                case A_FLATTEN: {
                    if (!ac->grid) return NoGrid();
                    ac->AddUndo(this);
                    int maxdepth = 0, leaves = 0;
                    ac->MaxDepthLeaves(0, maxdepth, leaves);
                    Grid *g = new Grid(maxdepth, leaves);
                    g->InitCells();
                    ac->grid->Flatten(0, 0, g);
                    DELETEP(ac->grid);
                    ac->grid = g;
                    g->ReParent(ac);
                    ac->ResetChildren();
                    ClearSelectionRefresh();
                    return nullptr;
                }
            }
        }

        if (!c) return OneCell();

        switch (k) {
            case A_NEXT: selected.Next(this, dc, false); return nullptr;
            case A_PREV: selected.Next(this, dc, true); return nullptr;

            case A_IMAGECPY: {
                if (selected.Thin()) return NoThin();
                if (!c->text.image) return _(L"No image in this cell.");
                return CopyImageToClipboard(c);
            }            

            case A_ENTERGRID:
                if (!c->grid) Action(dc, A_NEWGRID);
                selected = Selection(c->grid, 0, 0, 1, 1);
                ScrollOrZoom(dc, true);
                return nullptr;

            case A_LINK:
            case A_LINKREV: {
                if (!c->text.t.Len()) return _(L"No text in this cell.");
                bool t1 = false, t2 = false;
                Cell *link = rootgrid->FindLink(selected, c, nullptr, t1, t2, k == A_LINK);
                if (!link || !link->parent) return _(L"No matching cell found!");
                selected = link->parent->grid->FindCell(link);
                ScrollOrZoom(dc, true);
                return nullptr;
            }

            case A_COLCELL: sys->customcolor = c->cellcolor; return nullptr;

            case A_HSWAP: {
                Cell *pp = c->parent->parent;
                if (!pp) return _(L"Cannot move this cell up in the hierarchy.");
                if (pp->grid->xs != 1 && pp->grid->ys != 1)
                    return _(L"Can only move this cell into a Nx1 or 1xN grid.");
                if (c->parent->grid->xs != 1 && c->parent->grid->ys != 1)
                    return _(L"Can only move this cell from a Nx1 or 1xN grid.");
                pp->AddUndo(this);
                selected = pp->grid->HierarchySwap(c->text.t);
                pp->ResetChildren();
                pp->ResetLayout();
                Refresh();
                return nullptr;
            }

            case A_TAGADD:
                if (!c->text.t.Len()) return _(L"Empty strings cannot be tags.");
                tags[c->text.t] = true;
                Refresh();
                return nullptr;

            case A_TAGREMOVE:
                tags.erase(c->text.t);
                Refresh();
                return nullptr;

            case A_FILTERBYCELLBG: 
                loopallcells(ci) ci->text.filtered = ci->cellcolor != c->cellcolor;
                rootgrid->ResetChildren();
                Refresh();
                return nullptr;

            case A_FILTERMATCHNEXT:
                bool lastsel = true;
                Cell *next =
                    rootgrid->FindNextFilterMatch(nullptr, selected.GetCell(), lastsel);
                if (!next) return _(L"No matches for filter.");
                if (next->parent) selected = next->parent->grid->FindCell(next);
                sw->SetFocus();
                ScrollOrZoom(dc, true);
                return nullptr;
        }

        if (!selected.TextEdit()) return _(L"only works in cell text mode");

        switch (k) {
            case A_CANCELEDIT:
                if (LastUndoSameCellTextEdit(c))
                    Undo(dc, undolist, redolist);
                else
                    Refresh();
                selected.ExitEdit(this);
                return nullptr;

            case A_BACKSPACE_WORD:
                if (selected.cursorend == 0) return nullptr;
                c->AddUndo(this);
                c->text.BackspaceWord(selected);
                Refresh();
                ZoomOutIfNoGrid(dc);
                return nullptr;

            // FIXME: this functionality is really SCHOME, SHOME should be within line
            case A_SHOME:
                DrawSelect(dc, selected);
                selected.cursor = 0;
                DrawSelectMove(dc, selected);
                return nullptr;
            case A_SEND:
                DrawSelect(dc, selected);
                selected.cursorend = (int)c->text.t.Len();
                DrawSelectMove(dc, selected);
                return nullptr;

            case A_CHOME:
                DrawSelect(dc, selected);
                selected.cursor = selected.cursorend = 0;
                DrawSelectMove(dc, selected);
                return nullptr;
            case A_CEND:
                DrawSelect(dc, selected);
                selected.cursor = selected.cursorend = selected.MaxCursor();
                DrawSelectMove(dc, selected);
                return nullptr;

            case A_HOME:
                DrawSelect(dc, selected);
                c->text.HomeEnd(selected, true);
                DrawSelectMove(dc, selected);
                return nullptr;
            case A_END:
                DrawSelect(dc, selected);
                c->text.HomeEnd(selected, false);
                DrawSelectMove(dc, selected);
                return nullptr;
            default: return _(L"Internal error: unimplemented operation!");
        }
    }

    const wxChar *SearchNext(wxDC &dc) {
        if (!sys->searchstring.Len()) return _(L"No search string.");
        bool lastsel = true;
        Cell *next =
            rootgrid->FindNextSearchMatch(sys->searchstring, nullptr, selected.GetCell(), lastsel);
        if (!next) return _(L"No matches for search.");
        selected = next->parent->grid->FindCell(next);
        sw->SetFocus();
        ScrollOrZoom(dc, true);
        return nullptr;
    }

    uint PickColor(wxFrame *fr, uint defcol) {
        wxColour col = wxGetColourFromUser(fr, wxColour(defcol));
        if (col.IsOk())
            return (col.Blue() << 16) + (col.Green() << 8) + col.Red();
        return -1;
    }

    const wxChar *layrender(int ds, bool vert, bool toggle = false, bool noset = false) {
        if (selected.Thin()) return NoThin();
        selected.g->cell->AddUndo(this);
        bool v = toggle ? !selected.GetFirst()->verticaltextandgrid : vert;
        if (ds >= 0 && selected.IsAll()) selected.g->cell->drawstyle = ds;
        selected.g->SetGridTextLayout(ds, v, noset, selected);
        selected.g->cell->ResetChildren();
        Refresh();
        return nullptr;
    }

    void ZoomOutIfNoGrid(wxDC &dc) {
        if (!WalkPath(drawpath)->grid) Zoom(-1, dc);
    }

    void PasteSingleText(Cell *c, const wxString &t) { c->text.Insert(this, t, selected); }

    void PasteOrDrop() {
        Cell *c = selected.ThinExpand(this);
        if (!c) return;
        wxBusyCursor wait;
        switch (dataobjc->GetReceivedFormat().GetType()) {
            case wxDF_FILENAME: {
                const wxArrayString &as = dataobjf->GetFilenames();
                if (as.size()) {
                    if (as.size() > 1) sw->Status(_(L"Cannot drag & drop more than 1 file."));
                    c->AddUndo(this);
                    if (!LoadImageIntoCell(as[0], c, sys->frame->csf)) PasteSingleText(c, as[0]);
                    Refresh();
                }
                break;
            }
            case wxDF_BITMAP:
            case wxDF_DIB:
            case wxDF_TIFF:
            #ifdef __WXMSW__
            case wxDF_PNG:
            #endif
                if (dataobji->GetBitmap().GetRefData() != wxNullBitmap.GetRefData()) {
                    c->AddUndo(this);
                    SetImageBM(c, dataobji->GetBitmap().ConvertToImage(), sys->frame->csf);
                    dataobji->SetBitmap(wxNullBitmap);
                    c->Reset();
                    Refresh();
                }
                break;
            /*
            case wxDF_HTML: {
                auto s = dataobjh->GetHTML();
                // Would have to somehow parse HTML here to get images and styled text.
                break;
            }
            case wxDF_RTF: {
                // Would have to somehow parse RTF here to get images and styled text.
                break;
            }
            */
            default:  // several text formats
                if (dataobjt->GetText() != wxEmptyString) {
                    wxString s = dataobjt->GetText();
                    if ((sys->clipboardcopy == s) && sys->cellclipboard) {
                        c->Paste(this, sys->cellclipboard.get(), selected);
                        Refresh();
                    } else {
                        const wxArrayString &as = wxStringTokenize(s, LINE_SEPERATOR);
                        if (as.size()) {
                            if (as.size() <= 1) {
                                c->AddUndo(this);
                                PasteSingleText(c, as[0]);
                            } else {
                                c->parent->AddUndo(this);
                                c->ResetLayout();
                                DELETEP(c->grid);
                                sys->FillRows(c->AddGrid(), as, sys->CountCol(as[0]), 0, 0);
                                if (!c->HasText())
                                    c->grid->MergeWithParent(c->parent->grid, selected);
                            }
                            Refresh();
                        }
                    }
                    dataobjt->SetText(wxEmptyString);
                }
                break;
        }
    }

    const wxChar *Sort(bool descending) {
        if (selected.xs != 1 && selected.ys <= 1)
            return _(L"Can't sort: make a 1xN selection to indicate what column to sort on, and what rows to affect");
        selected.g->cell->AddUndo(this);
        selected.g->Sort(selected, descending);
        Refresh();
        return nullptr;
    }

    void DelRowCol(int &v, int e, int gvs, int dec, int dx, int dy, int nxs, int nys) {
        if (v != e) {
            selected.g->cell->AddUndo(this);
            if (gvs == 1) {
                selected.g->DelSelf(this, selected);
            } else {
                selected.g->DeleteCells(dx, dy, nxs, nys);
                v -= dec;
            }
            Refresh();
        }
    }

    void CreatePath(Cell *c, Vector<Selection> &path) {
        path.setsize(0);
        while (c->parent) {
            const Selection &s = c->parent->grid->FindCell(c);
            ASSERT(s.g);
            path.push() = s;
            c = c->parent;
        }
    }

    Cell *WalkPath(Vector<Selection> &path) {
        Cell *c = rootgrid;
        loopvrev(i, path) {
            Selection &s = path[i];
            Grid *g = c->grid;
            if (!g) return c;
            ASSERT(g && s.x < g->xs && s.y < g->ys);
            c = g->C(s.x, s.y);
        }
        return c;
    }

    bool LastUndoSameCellAny(Cell *c) {
        return undolist.size() && undolist.size() != undolistsizeatfullsave &&
               undolist.last()->cloned_from == (uintptr_t)c;
    }

    bool LastUndoSameCellTextEdit(Cell *c) {
        // hacky way to detect word boundaries to stop coalescing, but works, and
        // not a big deal if selected is not actually related to this cell
        return undolist.size() && !c->grid && undolist.size() != undolistsizeatfullsave &&
               undolist.last()->sel.EqLoc(c->parent->grid->FindCell(c)) &&
               (!c->text.t.EndsWith(" ") || c->text.t.Len() != selected.cursor);
    }

    void AddUndo(Cell *c) {
        redolist.setsize(0);
        lastmodsinceautosave = wxGetLocalTime();
        if (!modified) {
            modified = true;
            UpdateFileName();
        }
        if (LastUndoSameCellTextEdit(c)) return;
        UndoItem *ui = new UndoItem();
        undolist.push() = ui;
        ui->clone = c->Clone(nullptr);
        ui->estimated_size = c->EstimatedMemoryUse();
        ui->sel = selected;
        ui->cloned_from = (uintptr_t)c;
        CreatePath(c, ui->path);
        if (selected.g) CreatePath(selected.g->cell, ui->selpath);
        size_t total_usage = 0;
        size_t old_list_size = undolist.size();
        // Cull undolist. Always at least keeps last item.
        for (int i = (int)undolist.size() - 1; i >= 0; i--) {
            // Cull old items if using more than 100MB or 1000 items, whichever comes first.
            // TODO: make configurable?
            if (total_usage < 100 * 1024 * 1024 && undolist.size() - i < 1000) {
                total_usage += undolist[i]->estimated_size;
            } else {
                undolist.remove(0, i + 1);
                break;
            }
        }
        size_t items_culled = old_list_size - undolist.size();
        undolistsizeatfullsave -= items_culled;  // Allowed to go < 0
    }

    void Undo(wxDC &dc, Vector<UndoItem *> &fromlist, Vector<UndoItem *> &tolist,
              bool redo = false) {
        Selection beforesel = selected;
        Vector<Selection> beforepath;
        if (beforesel.g) CreatePath(beforesel.g->cell, beforepath);
        UndoItem *ui = fromlist.pop();
        Cell *c = WalkPath(ui->path);
        auto clone = ui->clone.release();
        ui->clone.reset(c);
        if (c->parent && c->parent->grid) {
            c->parent->grid->ReplaceCell(c, clone);
            clone->parent = c->parent;
        } else
            rootgrid = clone;
        clone->ResetLayout();
        selected = ui->sel;
        if (selected.g) selected.g = WalkPath(ui->selpath)->grid;
        begindrag = selected;
        ui->sel = beforesel;
        ui->selpath.setsize(0);
        ui->selpath.append(beforepath);
        tolist.push() = ui;
        if (undolistsizeatfullsave > undolist.size())
            undolistsizeatfullsave = -1;  // gone beyond the save point, always modified
        modified = undolistsizeatfullsave != undolist.size();
        if (selected.g)
            ScrollOrZoom(dc);
        else
            Refresh();
        UpdateFileName();
    }

    void ColorChange(int which, int idx) {
        if (!selected.g) return;
        auto col = idx == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[idx];
        switch (which) {
            case A_CELLCOLOR: sys->lastcellcolor = col; break;
            case A_TEXTCOLOR: sys->lasttextcolor = col; break;
            case A_BORDCOLOR: sys->lastbordcolor = col; break;
        }
        selected.g->ColorChange(this, which, col, selected);
    }

    void SetImageBM(Cell *c, const wxImage &im, double sc) {
        vector<uint8_t> empty;
        c->text.image = sys->imagelist[sys->AddImageToList(im, sc, std::move(empty))];
    }

    bool LoadImageIntoCell(const wxString &fn, Cell *c, double sc) {
        if (fn.empty()) return false;
        wxImage im;
        if (!im.LoadFile(fn)) return false;
        SetImageBM(c, im, sc);
        c->Reset();
        return true;
    }

    void ImageChange(wxString &fn, double sc) {
        if (!selected.g) return;
        selected.g->cell->AddUndo(this);
        loopallcellssel(c, false) LoadImageIntoCell(fn, c, sc);
        Refresh();
    }

    void RecreateTagMenu(wxMenu &menu) {
        int i = A_TAGSET;
        for (auto tagit = tags.begin(); tagit != tags.end(); ++tagit) { menu.Append(i++, tagit->first); }
    }

    const wxChar *TagSet(int tagno) {
        int i = 0;
        for (auto tagit = tags.begin(); tagit != tags.end(); ++tagit)
            if (i++ == tagno) {
                Cell *c = selected.GetCell();
                if (!c) return OneCell();
                c->AddUndo(this);
                c->text.Clear(this, selected);
                c->text.Insert(this, tagit->first, selected);
                Refresh();
                return nullptr;
            }
        ASSERT(0);
        return nullptr;
    }

    void CollectCells(Cell *c) {
        itercells.setsize_nd(0);
        c->CollectCells(itercells);
    }

    void CollectCellsSel(bool recurse) {
        itercells.setsize_nd(0);
        if (selected.g) selected.g->CollectCellsSel(itercells, selected, recurse);
    }

    static int _timesort(const Cell **a, const Cell **b) {
        return ((*a)->text.lastedit < (*b)->text.lastedit) * 2 - 1;
    }

    void ApplyEditFilter() {
        searchfilter = false;
        editfilter = min(max(editfilter, 1), 99);
        CollectCells(rootgrid);
        itercells.sort((void *)(int(__cdecl *)(const void *, const void *))_timesort);
        loopv(i, itercells) itercells[i]->text.filtered = i > itercells.size() * editfilter / 100;
        rootgrid->ResetChildren();
        Refresh();
    }

    void SetSearchFilter(bool on) {
        searchfilter = on;
        loopallcells(c) c->text.filtered = on && !c->text.IsInSearch();
        rootgrid->ResetChildren();
        Refresh();
    }
};
