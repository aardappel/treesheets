struct UndoItem {
    vector<Selection> path;
    vector<Selection> selpath;
    Selection sel;
    unique_ptr<Cell> clone;
    size_t estimated_size {0};
    uintptr_t cloned_from;  // May be dead.
    int generation {0};
};

struct Document {
    TSCanvas *canvas {nullptr};
    Cell *root {nullptr};
    Selection prev;
    Selection hover;
    Selection selected;
    Selection begindrag;
    int isctrlshiftdrag;
    int scrollx;
    int scrolly;
    int maxx;
    int maxy;
    int centerx {0};
    int centery {0};
    int layoutxs;
    int layoutys;
    int hierarchysize;
    int fgutter {6};
    int lasttextsize;
    int laststylebits;
    Cell *currentdrawroot;  // for use during Render() calls
    vector<unique_ptr<UndoItem>> undolist;
    vector<unique_ptr<UndoItem>> redolist;
    vector<Selection> drawpath;
    int pathscalebias {0};
    wxString filename {L""};
    long lastmodsinceautosave {0};
    long undolistsizeatfullsave {0};
    long lastsave {wxGetLocalTime()};
    bool modified {false};
    bool tmpsavesuccess {true};
    wxDataObjectComposite *dndobjc {new wxDataObjectComposite()};
    wxTextDataObject *dndobjt {new wxTextDataObject()};
    wxBitmapDataObject *dndobji {new wxBitmapDataObject()};
    wxFileDataObject *dndobjf {new wxFileDataObject()};

    struct Printout : wxPrintout {
        Document *doc;
        Printout(Document *d) : wxPrintout(L"printout"), doc(d) {}

        bool OnPrintPage(int page) {
            auto dc = GetDC();
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

    bool while_printing {false};
    wxPrintData printData;
    wxPageSetupDialogData pageSetupData;
    uint printscale {0};
    bool scaledviewingmode {false};
    bool paintscrolltoselection {true};
    double currentviewscale {1.0};
    bool searchfilter {false};
    int editfilter {0};
    wxDateTime lastmodificationtime;
    map<wxString, uint> tags;
    vector<Cell *> itercells;

    #define loopcellsin(par, c) \
        CollectCells(par);      \
        loopv(_i, itercells) for (auto c = itercells[_i]; c; c = nullptr)
    #define loopallcells(c)     \
        CollectCells(root); \
        for (auto c : itercells)
    #define loopallcellssel(c, rec) \
        CollectCellsSel(rec);     \
        for (auto c : itercells)

    Document() {
        ResetFont();
        pageSetupData = printData;
        pageSetupData.SetMarginTopLeft(wxPoint(15, 15));
        pageSetupData.SetMarginBottomRight(wxPoint(15, 15));
        dndobjc->Add(dndobjt);
        dndobjc->Add(dndobji);
        dndobjc->Add(dndobjf);
    }

    ~Document() { DELETEP(root); }

    uint Background() { return root ? root->cellcolor : 0xFFFFFF; }

    void InitCellSelect(Cell *initialselected, int xsize, int ysize) {
        if (!initialselected) {
            SetSelect(Selection(root->grid, 0, 0, 1, 1));
            return;
        }
        SetSelect(initialselected->parent->grid->FindCell(initialselected));
        selected.xs = xsize;
        selected.ys = ysize;
        sys->frame->UpdateStatus(selected, true);
    }

    void InitWith(Cell *root, const wxString &filename, Cell *initialselected, int xsize, int ysize) {
        this->root = root;
        InitCellSelect(initialselected, xsize, ysize);
        ChangeFileName(filename, false);
    }

    void UpdateFileName(int page = -1) {
        sys->frame->SetPageTitle(filename, modified ? (lastmodsinceautosave ? L"*" : L"+") : L"",
                                 page);
    }

    void ChangeFileName(const wxString &newfilename, bool checkext) {
        filename = newfilename;
        if (checkext) {
            wxFileName wxfn(filename);
            if (!wxfn.HasExt()) filename.Append(L".cts");
        }
        UpdateFileName();
    }

    const wxChar *SaveDB(bool *success, bool istempfile = false, int page = -1) {
        if (filename.empty()) return _(L"Save cancelled.");
        auto *ocs = selected.xs == 0 && selected.ys == 0 ? nullptr
                                                         : selected.grid->C(selected.x, selected.y);
        auto start_saving_time = wxGetLocalTimeMillis();

        {  // limit destructors
            wxBusyCursor wait;
            if (!istempfile && sys->makebaks && ::wxFileExists(filename)) {
                ::wxRenameFile(filename, sys->BakName(filename));
            }
            auto savefilename = istempfile ? sys->TmpName(filename) : filename;
            wxFFileOutputStream fos(savefilename);
            if (!fos.IsOk()) {
                if (!istempfile)
                    wxMessageBox(
                        _(L"Error writing TreeSheets file! (try saving under new filename)."),
                        savefilename.wx_str(), wxOK, sys->frame);
                return _(L"Error writing to file.");
            }

            wxDataOutputStream sos(fos);
            fos.Write("TSFF", 4);
            char vers = TS_VERSION;
            fos.Write(&vers, 1);
            sos.Write8(selected.xs);
            sos.Write8(selected.ys);
            sos.Write8(ocs ? drawpath.size() : 0);  // zoom level
            RefreshImageRefCount(true);
            int realindex = 0;
            loopv(i, sys->imagelist) {
                if (auto &image = *sys->imagelist[i]; image.trefc) {
                    fos.PutC(image.type);
                    sos.WriteDouble(image.display_scale);
                    wxInt64 imagelen(image.data.size());
                    sos.Write64(imagelen);
                    fos.Write(image.data.data(), imagelen);
                    image.savedindex = realindex++;
                }
            }

            fos.Write("D", 1);
            wxZlibOutputStream zos(fos, 9);
            if (!zos.IsOk()) return _(L"Zlib error while writing file.");
            wxDataOutputStream dos(zos);
            root->Save(dos, ocs);
            for (auto &[tag, color] : tags) {
                dos.WriteString(tag);
                dos.Write32(color);
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
        if (sys->autohtmlexport) {
            ExportFile(sys->ExtName(filename, L".html"),
                       sys->autohtmlexport == A_AUTOEXPORT_HTML_WITH_IMAGES - A_AUTOEXPORT_HTML_NONE
                           ? A_EXPHTMLTE
                           : A_EXPHTMLT,
                       false);
        }
        UpdateFileName(page);
        if (success) *success = true;

        sys->frame->SetStatus(wxString::Format(_(L"Saved %s successfully (in %lld milliseconds)."),
                                               filename.c_str(),
                                               end_saving_time - start_saving_time)
                                  .c_str());

        return _(L"");
    }

    void DrawSelect(wxDC &dc, Selection &s) {
        if (!s.grid) return;
        ResetFont();
        s.grid->DrawSelect(this, dc, s);
    }

    void UpdateHover(wxDC &dc, int mx, int my) {
        ResetFont();
        int x, y;
        canvas->CalcUnscrolledPosition(mx, my, &x, &y);
        prev = hover;
        hover = Selection();
        auto drawroot = WalkPath(drawpath);
        if (drawroot->grid)
            drawroot->grid->FindXY(
                this, x / currentviewscale - centerx / currentviewscale - hierarchysize,
                y / currentviewscale - centery / currentviewscale - hierarchysize, dc);
    }

    void ScrollIfSelectionOutOfView(Selection &sel) {
        if (!scaledviewingmode) {
            // required, since sizes of things may have been reset by the last editing operation
            int canvasw, canvash;
            canvas->GetClientSize(&canvasw, &canvash);
            if ((layoutys > canvash || layoutxs > canvasw) && sel.grid) {
                wxRect r = sel.grid->GetRect(this, sel);
                if (r.y < scrolly || r.y + r.height > maxy || r.x < scrollx || r.x + r.width > maxx) {
                    canvas->Scroll(r.width > canvasw || r.x < scrollx ? r.x
                                   : r.x + r.width > maxx             ? r.x + r.width - canvasw
                                                                      : scrollx,
                                   r.height > canvash || r.y < scrolly ? r.y
                                   : r.y + r.height > maxy             ? r.y + r.height - canvash
                                                                       : scrolly);
                }
            }
        }
    }

    void ScrollOrZoom(bool zoomiftiny = false) {
        if (!selected.grid) return;
        auto drawroot = WalkPath(drawpath);
        // If we jumped to a cell which may be insided a folded cell, we have to unfold it
        // because the rest of the code doesn't deal with a selection that is invisible :)
        for (auto cg = selected.grid->cell; cg; cg = cg->parent) {
            // Unless we're under the drawroot, no need to unfold further.
            if (cg == drawroot) break;
            if (cg->grid->folded) {
                cg->grid->folded = false;
                cg->ResetLayout();
                cg->ResetChildren();
            }
        }
        for (auto cg = selected.grid->cell; cg; cg = cg->parent)
            if (cg == drawroot) {
                if (zoomiftiny) ZoomTiny();
                paintscrolltoselection = true;
                canvas->Refresh();
                return;
            }
        Zoom(-100, false);
        if (zoomiftiny) ZoomTiny();
    }

    void ZoomTiny() {
        if (auto c = selected.GetCell(); c && c->tiny) {
            Zoom(1);  // seems to leave selection box in a weird location?
            if (selected.GetCell() != c) ZoomTiny();
        }
    }

    void ResetCursor() {
        if (selected.grid) selected.SetCursorEdit(this, selected.TextEdit());
    }

    void SetSelect(const Selection &sel = Selection()) {
        selected = sel;
        begindrag = sel;
    }

    void SelectUp() {
        if (!isctrlshiftdrag || isctrlshiftdrag == 3 || begindrag.EqLoc(selected)) return;
        auto cell = selected.GetCell();
        if (!cell) return;
        auto targetcell = begindrag.ThinExpand(this);
        selected = begindrag;
        if (targetcell) {
            auto is_parent = targetcell->IsParentOf(cell);
            auto targetcell_parent = targetcell->parent;  // targetcell may be deleted.
            targetcell->Paste(this, cell, begindrag);
            // If is_parent, cell has been deleted already.
            if (isctrlshiftdrag == 1 && !is_parent) {
                cell->parent->AddUndo(this);
                Selection cellselection = cell->parent->grid->FindCell(cell);
                cell->parent->grid->MultiCellDeleteSub(this, cellselection);
            }
            hover = targetcell_parent ? targetcell_parent->grid->FindCell(targetcell) : Selection();
            SetSelect(hover);
            wxClientDC dc(canvas); // TODO: replace with wxInfoDC starting wxWidgets 3.3.0
            Layout(dc);
        }
    }

    void DoubleClick() {
        SetSelect(hover);
        if (selected.Thin() && selected.grid) {
            selected.SelAll();
        } else if (Cell *c = selected.GetCell()) {
            selected.EnterEditOnly(this);
            c->text.SelectWord(selected);
            begindrag = selected;
        }
    }

    void Drop() {
        switch (dndobjc->GetReceivedFormat().GetType()) {
            case wxDF_BITMAP: PasteOrDrop(*dndobji); break;
            case wxDF_FILENAME: PasteOrDrop(*dndobjf); break;
            case wxDF_TEXT:
            case wxDF_UNICODETEXT: PasteOrDrop(*dndobjt);
            default:;
        }
    }

    auto CopyEntireCells(wxString &s, int action) {
        sys->clipboardcopy = s;
        auto html =
            selected.grid->ConvertToText(selected, 0, action == A_COPYWI ? A_EXPHTMLTI : A_EXPHTMLT,
                                         this, false, currentdrawroot);
        return new wxHTMLDataObject(html);
    }

    void Copy(int action) {
        auto c = selected.GetCell();
        sys->clipboardcopy = wxEmptyString;

        switch (action) {
            case A_DRAGANDDROP: {
                sys->cellclipboard = c ? c->Clone(nullptr) : selected.grid->CloneSel(selected);
                wxDataObjectComposite dragdata;
                if (c && !c->text.t && c->text.image) {
                    auto image = c->text.image;
                    if (!image->data.empty()) {
                        auto &[it, mime] = imagetypes.at(image->type);
                        auto bitmap = ConvertBufferToWxBitmap(image->data, it);
                        dragdata.Add(new wxBitmapDataObject(bitmap));
                    }
                } else {
                    auto s = selected.grid->ConvertToText(selected, 0, A_EXPTEXT, this, false,
                                                          currentdrawroot);
                    dragdata.Add(new wxTextDataObject(s));
                    if (!selected.TextEdit()) {
                        auto htmlobj = CopyEntireCells(s, wxID_COPY);
                        dragdata.Add(htmlobj);
                    }
                }
                wxDropSource dragsource(dragdata, canvas);
                dragsource.DoDragDrop(true);
                break;
            }
            case A_COPYCT: {
                sys->cellclipboard = nullptr;
                auto clipboardtextdata = new wxDataObjectComposite();
                wxString s = "";
                loopallcellssel(c, true) if (c->text.t.Len()) s += c->text.t + " ";
                if (!selected.TextEdit()) sys->clipboardcopy = s;
                clipboardtextdata->Add(new wxTextDataObject(s));
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(clipboardtextdata);
                    wxTheClipboard->Close();
                }
                break;
            }
            case wxID_COPY:
            case A_COPYWI:
            default: {
                sys->cellclipboard = c ? c->Clone(nullptr) : selected.grid->CloneSel(selected);
                if (c && !c->text.t && c->text.image) {
                    auto image = c->text.image;
                    if (!image->data.empty() && wxTheClipboard->Open()) {
                        auto &[it, mime] = imagetypes.at(image->type);
                        auto bitmap = ConvertBufferToWxBitmap(image->data, it);
                        wxTheClipboard->SetData(new wxBitmapDataObject(bitmap));
                        wxTheClipboard->Close();
                    }
                } else {
                    auto clipboarddata = new wxDataObjectComposite();
                    auto s = selected.grid->ConvertToText(selected, 0, A_EXPTEXT, this, false,
                                                          currentdrawroot);
                    clipboarddata->Add(new wxTextDataObject(s));
                    if (!selected.TextEdit()) {
                        auto htmlobj = CopyEntireCells(s, action);
                        clipboarddata->Add(htmlobj);
                    }
                    if (wxTheClipboard->Open()) {
                        wxTheClipboard->SetData(clipboarddata);
                        wxTheClipboard->Close();
                    }
                }
                break;
            }
        }
        return;
    }

    void ZoomSetDrawPath(int dir, bool fromroot = true) {
        int len = max(0, (fromroot ? 0 : drawpath.size()) + dir);
        if (!len && drawpath.empty()) return;
        if (dir > 0) {
            if (!selected.grid) return;
            auto c = selected.GetCell();
            CreatePath(c && c->grid ? c : selected.grid->cell, drawpath);
        } else if (dir < 0) {
            auto drawroot = WalkPath(drawpath);
            if (drawroot->grid && drawroot->grid->folded)
                SetSelect(drawroot->parent->grid->FindCell(drawroot));
        }
        if (auto diff = static_cast<int>(drawpath.size()) - max(0, len); diff > 0)
            drawpath.erase(drawpath.begin(), drawpath.begin() + diff);
    }

    void Zoom(int dir, bool fromroot = false) {
        ZoomSetDrawPath(dir, fromroot);
        auto drawroot = WalkPath(drawpath);
        if (selected.GetCell() == drawroot && drawroot->grid) {
            // We can't have the drawroot selected, so we must move the selection to the children.
            SetSelect(Selection(drawroot->grid, 0, 0, drawroot->grid->xs, drawroot->grid->ys));
        }
        drawroot->ResetLayout();
        drawroot->ResetChildren();
        paintscrolltoselection = true;
        canvas->Refresh();
    }

    const wxChar *NoSel() { return _(L"This operation requires a selection."); }
    const wxChar *OneCell() { return _(L"This operation works on a single selected cell only."); }
    const wxChar *NoThin() { return _(L"This operation doesn't work on thin selections."); }
    const wxChar *NoGrid() { return _(L"This operation requires a cell that contains a grid."); }

    const wxChar *Wheel(int dir, bool alt, bool ctrl, bool shift, bool hierarchical = true) {
        if (!dir) return nullptr;
        if (alt) {
            if (!selected.grid) return NoSel();
            if (selected.xs > 0) {
                if (!LastUndoSameCellAny(selected.grid->cell)) selected.grid->cell->AddUndo(this);
                selected.grid->ResizeColWidths(dir, selected, hierarchical);
                selected.grid->cell->ResetLayout();
                selected.grid->cell->ResetChildren();
                paintscrolltoselection = true;
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, false);
                return dir > 0 ? _(L"Column width increased.") : _(L"Column width decreased.");
            }
            return L"nothing to resize";
        } else if (shift) {
            if (!selected.grid) return NoSel();
            selected.grid->cell->AddUndo(this);
            selected.grid->ResetChildren();
            selected.grid->RelSize(-dir, selected, pathscalebias);
            paintscrolltoselection = true;
            canvas->Refresh();
            return dir > 0 ? _(L"Text size increased.") : _(L"Text size decreased.");
        } else if (ctrl) {
            int steps = abs(dir);
            dir = sign(dir);
            loop(i, steps) Zoom(dir);
            return dir > 0 ? _(L"Zoomed in.") : _(L"Zoomed out.");
        } else {
            ASSERT(0);
            return nullptr;
        }
    }

    void Layout(wxDC &dc) {
        ResetFont();
        dc.SetUserScale(1, 1);
        currentdrawroot = WalkPath(drawpath);
        int psb = currentdrawroot == root ? 0 : currentdrawroot->MinRelsize();
        if (psb < 0 || psb == INT_MAX) psb = 0;
        if (psb != pathscalebias) currentdrawroot->ResetChildren();
        pathscalebias = psb;
        currentdrawroot->LazyLayout(this, dc, 0, currentdrawroot->ColWidth(), false);
        ResetFont();
        PickFont(dc, 0, 0, 0);
        hierarchysize = 0;
        for (Cell *p = currentdrawroot->parent; p; p = p->parent)
            if (p->text.t.Len()) hierarchysize += dc.GetCharHeight();
        hierarchysize += fgutter;
        layoutxs = currentdrawroot->sx + hierarchysize + fgutter;
        layoutys = currentdrawroot->sy + hierarchysize + fgutter;
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
        for (auto p = currentdrawroot->parent; p; p = p->parent)
            if (p->text.t.Len()) {
                int off = hierarchysize - dc.GetCharHeight() * ++i;
                auto s = p->text.t;
                if (static_cast<int>(s.Len()) > sys->defaultmaxcolwidth) {
                    // should take the width of these into account for layoutys, but really, the
                    // worst that can happen on a thin window is that its rendering gets cut off
                    s = s.Left(sys->defaultmaxcolwidth) + L"...";
                }
                dc.DrawText(s, off, off);
            }
        dc.SetTextForeground(sys->darkmode ? *wxWHITE : *wxBLACK);
        currentdrawroot->Render(this, hierarchysize, hierarchysize, dc, 0, 0, 0, 0, 0,
                                currentdrawroot->ColWidth(), 0);
    }

    void SelectClick(bool right = false) {
        begindrag = Selection();
        if (!(right && hover.IsInside(selected))) {
            if (selected.GetCell() == hover.GetCell() && hover.GetCell())
                hover.EnterEditOnly(this);
            else
                hover.ExitEdit(this);
            SetSelect(hover);
        }
    }

    void Draw(wxDC &dc) {
        if (!root) return;
        canvas->GetClientSize(&maxx, &maxy);
        Layout(dc);
        dc.SetBackground(wxBrush(wxColor(LightColor(Background()))));
        dc.Clear();
        double xscale = maxx / static_cast<double>(layoutxs);
        double yscale = maxy / static_cast<double>(layoutys);
        currentviewscale = min(xscale, yscale);
        if (currentviewscale > 5)
            currentviewscale = 5;
        else if (currentviewscale < 1)
            currentviewscale = 1;
        if (scaledviewingmode && currentviewscale > 1) {
            dc.SetUserScale(currentviewscale, currentviewscale);
            canvas->SetVirtualSize(maxx, maxy);
            maxx /= currentviewscale;
            maxy /= currentviewscale;
            scrollx = scrolly = 0;
        } else {
            currentviewscale = 1;
            dc.SetUserScale(1, 1);
            canvas->SetVirtualSize(layoutxs, layoutys);
            canvas->GetViewStart(&scrollx, &scrolly);
            maxx += scrollx;
            maxy += scrolly;
        }
        centerx = sys->centered && !scrollx && maxx > layoutxs
                      ? (maxx - layoutxs) / 2 * currentviewscale
                      : 0;
        centery = sys->centered && !scrolly && maxy > layoutys
                      ? (maxy - layoutys) / 2 * currentviewscale
                      : 0;
        ShiftToCenter(dc);
        Render(dc);
        DrawSelect(dc, selected);
        if (paintscrolltoselection) {
            wxTheApp->CallAfter([this](){
                ScrollIfSelectionOutOfView(selected);
                #ifdef __WXMAC__
                    canvas->Refresh();
                #endif
            });
            paintscrolltoselection = false;
        }
        if (scaledviewingmode) { dc.SetUserScale(1, 1); }
    }

    void Print(wxDC &dc, wxPrintout &po) {
        Layout(dc);
        maxx = layoutxs;
        maxy = layoutys;
        scrollx = scrolly = 0;
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
                        stylebits & STYLE_FIXED ? sys->defaultfixedfont : sys->defaultfont);
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

    void RemoveTmpFile() {
        if (!filename.empty() && ::wxFileExists(sys->TmpName(filename)))
            ::wxRemoveFile(sys->TmpName(filename));
    }

    bool CloseDocument() {
        bool keep = CheckForChanges();
        if (!keep) RemoveTmpFile();
        return keep;
    }

    const wxChar *Export(const wxChar *fmt, const wxChar *pat, const wxChar *message, int action) {
        wxFileName tsfn(filename);
        auto exportfilename = ::wxFileSelector(message, tsfn.GetPath(), tsfn.GetName(), fmt, pat,
                                               wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (exportfilename.empty()) return _(L"Export cancelled.");
        wxFileName expfn(exportfilename);
        if (!expfn.HasExt()) {
            expfn.SetExt(fmt);
            exportfilename = expfn.GetFullPath();
        }
        return ExportFile(exportfilename, action, true);
    }

    wxBitmap GetBitmap() {
        maxx = layoutxs;
        maxy = layoutys;
        scrollx = scrolly = 0;
        wxBitmap bm(maxx, maxy, 24);
        wxMemoryDC mdc(bm);
        DrawRectangle(mdc, Background(), 0, 0, maxx, maxy);
        Layout(mdc);
        Render(mdc);
        return bm;
    }

    wxBitmap GetSubBitmap(const Selection &sel) {
        wxRect r = sel.grid->GetRect(this, sel, true);
        return GetBitmap().GetSubBitmap(r);
    }

    void RefreshImageRefCount(bool includefolded) {
        loopv(i, sys->imagelist) sys->imagelist[i]->trefc = 0;
        root->ImageRefCount(includefolded);
    }

    const wxChar *ExportFile(const wxString &filename, int action, bool currentview) {
        Cell *exportroot = currentview ? currentdrawroot : root;
        if (action == A_EXPIMAGE) {
            auto bitmap = GetBitmap();
            canvas->Refresh();
            if (!bitmap.SaveFile(filename, wxBITMAP_TYPE_PNG)) return _(L"Error writing PNG file!");
        } else {
            wxFFileOutputStream fos(filename, L"w+b");
            if (!fos.IsOk()) {
                wxMessageBox(_(L"Error exporting file!"), filename.wx_str(), wxOK, sys->frame);
                return _(L"Error writing to file!");
            }
            wxTextOutputStream dos(fos);
            wxString content = exportroot->ToText(0, Selection(), action, this, true, exportroot);
            switch (action) {
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
                case A_EXPHTMLTI:
                case A_EXPHTMLTE:
                case A_EXPHTMLB:
                case A_EXPHTMLO:
                    dos.WriteString(
                        L"<!DOCTYPE html>\n"
                        L"<html>\n<head>\n<style>\n"
                        L"body { font-family: sans-serif; }\n"
                        L"table, th, td { border: 1px solid #A0A0A0; border-collapse: collapse;"
                        L" padding: 3px; vertical-align: top; }\n"
                        L"li { }\n</style>\n"
                        L"<title>export of TreeSheets file ");
                    dos.WriteString(this->filename);
                    dos.WriteString(
                        L"</title>\n<meta charset=\"UTF-8\" />\n"
                        L"</head>\n<body>\n");
                    dos.WriteString(content);
                    dos.WriteString(L"</body>\n</html>\n");
                    break;
                case A_EXPCSV:
                case A_EXPTEXT: dos.WriteString(content); break;
            }
            if (action == A_EXPHTMLTE) ExportAllImages(filename, exportroot);
        }
        return _(L"File exported successfully.");
    }

    const wxChar *Save(bool saveas, bool *success = nullptr) {
        if (!saveas && !filename.empty()) { return SaveDB(success); }
        auto filename = ::wxFileSelector(_(L"Choose TreeSheets file to save:"), L"", L"", L"cts",
                                         _(L"TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*"),
                                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (filename.empty()) return _(L"Save cancelled.");  // avoid name being set to ""
        ChangeFileName(filename, true);
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

    const wxChar *Key(int uk, int k, bool alt, bool ctrl, bool shift, bool &unprocessed) {
        if (uk == WXK_NONE || k < ' ' && k || k == WXK_DELETE) {
            switch (k) {
                case WXK_BACK:  // no menu shortcut available in wxwidgets
                    if (!ctrl) return Action(A_BACKSPACE);
                    break;  // Prevent Ctrl+H from being treated as Backspace
                case WXK_RETURN:
                    return Action(shift  ? A_ENTERGRID
                                  : ctrl ? A_ENTERCELL_JUMPTOSTART
                                         : A_ENTERCELL);
                case WXK_ESCAPE:  // docs say it can be used as a menu accelerator, but it does not
                                  // trigger from there?
                    return Action(A_CANCELEDIT);
                #ifdef WIN32  // works fine on Linux, not sure OS X
                case WXK_PAGEDOWN: canvas->CursorScroll(0, g_scrollratecursor); return nullptr;
                case WXK_PAGEUP: canvas->CursorScroll(0, -g_scrollratecursor); return nullptr;
                #endif
                #ifdef __WXGTK__
                // Due to limitations within GTK, wxGTK does not support specific keycodes 
                // as accelerator keys for menu items. See wxWidgets documentation for the 
                // wxMenuItem class in order to obtain more details. This is why we implement 
                // the missing handling of these accelerator keys in the following section.
                // Please be aware that the custom implementation has the downside of these
                // "accelerator keys" being suppressed in the menu items on wxGTK.
                    case WXK_DELETE: return Action(A_DELETE);
                    case WXK_LEFT:
                        return Action(shift ? (ctrl ? A_SCLEFT : A_SLEFT)
                                            : (ctrl ? A_MLEFT : A_LEFT));
                    case WXK_RIGHT:
                        return Action(shift ? (ctrl ? A_SCRIGHT : A_SRIGHT)
                                            : (ctrl ? A_MRIGHT : A_RIGHT));
                    case WXK_UP:
                        return Action(shift ? (ctrl ? A_SCUP : A_SUP) : (ctrl ? A_MUP : A_UP));
                    case WXK_DOWN:
                        return Action(shift ? (ctrl ? A_SCDOWN : A_SDOWN)
                                            : (ctrl ? A_MDOWN : A_DOWN));
                    case WXK_HOME:
                        return Action(shift ? (ctrl ? A_SHOME : A_SHOME)
                                            : (ctrl ? A_CHOME : A_HOME));
                    case WXK_END:
                        return Action(shift ? (ctrl ? A_SEND : A_SEND) : (ctrl ? A_CEND : A_END));
                    case WXK_TAB:
                        if (ctrl && !shift) {
                            // WXK_CONTROL_I (italics) arrives as the same keycode as WXK_TAB + ctrl
                            // on Linux?? They're both keycode 9 in defs.h We ignore it here, such
                            // that CTRL+I works, but it means only CTRL+SHIFT+TAB works on Linux as
                            // a way to switch tabs.
                            // Also, even though we ignore CTRL+TAB, and it is not assigned in the
                            // menus, it still has the
                            // effect of de-selecting
                            // the current tab (requires a click to re-activate). FIXME??
                            break;
                        }
                        return Action(shift ? (ctrl ? A_PREVFILE : A_PREV)
                                            : (ctrl ? A_NEXTFILE : A_NEXT));
                    case WXK_PAGEUP:
                        if (ctrl) return Action(alt ? A_INCWIDTHNH : A_ZOOMIN);
                        if (shift) return Action(A_INCSIZE);
                        if (!alt) canvas->CursorScroll(0, -g_scrollratecursor);
                        return nullptr;
                    case WXK_PAGEDOWN:
                        if (ctrl) return Action(alt ? A_DECWIDTHNH : A_ZOOMOUT);
                        if (shift) return Action(A_DECSIZE);
                        if (!alt) canvas->CursorScroll(0, g_scrollratecursor);
                        return nullptr;
                #endif
            }
        } else if (uk >= ' ') {
            if (!selected.grid) return NoSel();
            auto c = selected.ThinExpand(this);
            if (!c) {
                selected.Wrap(this);
                c = selected.GetCell();
            }
            c->AddUndo(this);  // FIXME: not needed for all keystrokes, or at least, merge all
                               // keystroke undos within same cell
            c->text.Key(this, uk, selected);
            paintscrolltoselection = true;
            canvas->Refresh();
            canvas->Update();
            return nullptr;
        }
        unprocessed = true;
        return nullptr;
    }

    const wxChar *Action(int action) {
        switch (action) {
            case wxID_EXECUTE:
                root->AddUndo(this);
                sys->evaluator.Eval(root);
                root->ResetChildren();
                selected = Selection();
                begindrag = Selection();
                canvas->Refresh();
                return _(L"Evaluation finished.");

            case wxID_UNDO:
                if (undolist.size()) {
                    Undo(undolist, redolist);
                    return nullptr;
                } else {
                    return _(L"Nothing more to undo.");
                }

            case wxID_REDO:
                if (redolist.size()) {
                    Undo(redolist, undolist, true);
                    return nullptr;
                } else {
                    return _(L"Nothing more to redo.");
                }

            case wxID_SAVE: return Save(false);
            case wxID_SAVEAS: return Save(true);
            case A_SAVEALL: sys->SaveAll(); return nullptr;

            case A_EXPXML: return Export(L"xml", L"*.xml", _(L"Choose XML file to write"), action);
            case A_EXPHTMLT:
            case A_EXPHTMLTE:
            case A_EXPHTMLB:
            case A_EXPHTMLO:
                return Export(L"html", L"*.html", _(L"Choose HTML file to write"), action);
            case A_EXPTEXT:
                return Export(L"txt", L"*.txt", _(L"Choose Text file to write"), action);
            case A_EXPIMAGE:
                return Export(L"png", L"*.png", _(L"Choose PNG file to write"), action);
            case A_EXPCSV: {
                int maxdepth = 0, leaves = 0;
                currentdrawroot->MaxDepthLeaves(0, maxdepth, leaves);
                if (maxdepth > 1)
                    return _(
                        L"Cannot export grid that is not flat (zoom the view to the desired grid, and/or use Flatten).");
                return Export(L"csv", L"*.csv", _(L"Choose CSV file to write"), action);
            }

            case A_IMPXML:
            case A_IMPXMLA:
            case A_IMPTXTI:
            case A_IMPTXTC:
            case A_IMPTXTS:
            case A_IMPTXTT: {
                wxArrayString filenames;
                GetFilesFromUser(filenames, sys->frame, _(L"Please select file(s) to import:"),
                                 _(L"*.*"));
                const wxChar *message = nullptr;
                for (auto &filename : filenames) message = sys->Import(filename, action);
                return message;
            }

            case wxID_OPEN: {
                wxArrayString filenames;
                GetFilesFromUser(filenames, sys->frame,
                                 _(L"Please select TreeSheets file(s) to load:"),
                                 _(L"TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*"));
                const wxChar *message = nullptr;
                for (auto &filename : filenames) message = sys->Open(filename);
                return message;
            }

            case wxID_CLOSE: {
                if (sys->frame->notebook->GetPageCount() <= 1) {
                    sys->frame->fromclosebox = false;
                    sys->frame->Close();
                    return nullptr;
                }

                if (!CloseDocument()) {
                    int pagenumber = sys->frame->notebook->GetSelection();
                    // sys->frame->notebook->AdvanceSelection();
                    sys->frame->notebook->DeletePage(pagenumber);
                }
                return nullptr;
            }

            case wxID_NEW: {
                int size = static_cast<int>(
                    ::wxGetNumberFromUser(_(L"What size grid would you like to start with?"),
                                          _(L"size:"), _(L"New Sheet"), 10, 1, 25, sys->frame));
                if (size < 0) return _(L"New file cancelled.");
                sys->InitDB(size);
                sys->frame->GetCurrentTab()->Refresh();
                return nullptr;
            }

            case wxID_ABOUT: {
                wxAboutDialogInfo info;
                info.SetName(L"TreeSheets");
                info.SetVersion(wxT(PACKAGE_VERSION));
                info.SetCopyright(L"(C) 2026 Wouter van Oortmerssen and Tobias Predel");
                auto desc = wxString::Format(L"%s\n\n%s " wxVERSION_STRING,
                                             _(L"The Free Form Hierarchical Information Organizer"),
                                             _(L"Uses"));
                info.SetDescription(desc);
                wxAboutBox(info);
                return nullptr;
            }

            case wxID_HELP: sys->LoadTutorial(); return nullptr;

            case A_HELP_OP_REF: sys->LoadOpRef(); return nullptr;

            case A_TUTORIALWEBPAGE:
                #ifdef __WXMAC__
                    wxLaunchDefaultBrowser(L"file://" +
                                       sys->frame->app->GetDocPath(L"docs/tutorial.html"));  // RbrtPntn
                #else
                    wxLaunchDefaultBrowser(sys->frame->app->GetDocPath(L"docs/tutorial.html"));
                #endif
                return nullptr;

            #ifdef ENABLE_LOBSTER
                case A_SCRIPTREFERENCE:
                    #ifdef __WXMAC__
                        wxLaunchDefaultBrowser(L"file://" +
                                           sys->frame->app->GetDocPath(L"docs/script_reference.html"));  // RbrtPntn
                    #else
                        wxLaunchDefaultBrowser(sys->frame->app->GetDocPath(L"docs/script_reference.html"));
                    #endif
                    return nullptr;
            #endif

            case A_ZOOMIN:
                return Wheel(1, false, true,
                             false);  // Zoom( 1, dc); return "zoomed in (menu)";
            case A_ZOOMOUT:
                return Wheel(-1, false, true,
                             false);  // Zoom(-1, dc); return "zoomed out (menu)";
            case A_INCSIZE: return Wheel(1, false, false, true);
            case A_DECSIZE: return Wheel(-1, false, false, true);
            case A_INCWIDTH: return Wheel(1, true, false, false);
            case A_DECWIDTH: return Wheel(-1, true, false, false);
            case A_INCWIDTHNH: return Wheel(1, true, false, false, false);
            case A_DECWIDTHNH: return Wheel(-1, true, false, false, false);

            case wxID_SELECT_FONT:
            case A_SET_FIXED_FONT: {
                wxFontData fdat;
                fdat.SetInitialFont(wxFont(
                    g_deftextsize,
                    action == wxID_SELECT_FONT ? wxFONTFAMILY_DEFAULT : wxFONTFAMILY_TELETYPE,
                    wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false,
                    action == wxID_SELECT_FONT ? sys->defaultfont : sys->defaultfixedfont));
                if (wxFontDialog fd(sys->frame, fdat); fd.ShowModal() == wxID_OK) {
                    wxFont font = fd.GetFontData().GetChosenFont();
                    g_deftextsize = min(20, max(10, font.GetPointSize()));
                    sys->cfg->Write(L"defaultfontsize", g_deftextsize);
                    switch (action) {
                        case wxID_SELECT_FONT:
                            sys->defaultfont = font.GetFaceName();
                            sys->cfg->Write(L"defaultfont", sys->defaultfont);
                            break;
                        case A_SET_FIXED_FONT:
                            sys->defaultfixedfont = font.GetFaceName();
                            sys->cfg->Write(L"defaultfixedfont", sys->defaultfixedfont);
                            break;
                    }
                    // root->ResetChildren();
                    sys->frame->TabsReset();  // ResetChildren on all
                    canvas->Refresh();
                }
                return nullptr;
            }

            case wxID_PRINT: {
                wxPrintDialogData printDialogData(printData);
                wxPrinter printer(&printDialogData);
                Printout printout(this);
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

            case wxID_PREVIEW: {
                wxPrintDialogData printDialogData(printData);
                auto preview =
                    new wxPrintPreview(new Printout(this), new Printout(this), &printDialogData);
                auto pframe = new wxPreviewFrame(preview, sys->frame, _(L"Print Preview"),
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

            case A_DEFBGCOL: {
                auto oldbg = Background();
                if (auto color = PickColor(sys->frame, oldbg); color != (uint)-1) {
                    root->AddUndo(this);
                    loopallcells(c) {
                        if (c->cellcolor == oldbg && (!c->parent || c->parent->cellcolor == color))
                            c->cellcolor = color;
                    }
                    canvas->Refresh();
                }
                return nullptr;
            }

            case A_DEFCURCOL: {
                if (auto color = PickColor(sys->frame, sys->cursorcolor); color != (uint)-1) {
                    sys->cfg->Write(L"cursorcolor", sys->cursorcolor = color);
                    canvas->Refresh();
                }
                return nullptr;
            }

            case A_SEARCHNEXT:
            case A_SEARCHPREV: {
                if (sys->searchstring.Len()) return SearchNext(false, true, action == A_SEARCHPREV);
                if (auto c = selected.GetCell()) {
                    auto s = c->text.ToText(0, selected, A_EXPTEXT);
                    if (!s.Len()) return _(L"No text to search for.");
                    sys->frame->filter->SetFocus();
                    sys->frame->filter->SetValue(s);
                    return nullptr;
                } else {
                    return _(L"You need to select one cell if you want to search for its text.");
                }
            }

            case A_CASESENSITIVESEARCH: {
                sys->casesensitivesearch = !(sys->casesensitivesearch);
                sys->cfg->Write(L"casesensitivesearch", sys->casesensitivesearch);
                sys->searchstring = (sys->casesensitivesearch)
                                        ? sys->frame->filter->GetValue()
                                        : sys->frame->filter->GetValue().Lower();
                auto message = SearchNext(false, false, false);
                canvas->Refresh();
                return message;
            }

            case A_ROUND0:
            case A_ROUND1:
            case A_ROUND2:
            case A_ROUND3:
            case A_ROUND4:
            case A_ROUND5:
            case A_ROUND6:
                sys->cfg->Write(L"roundness", long(sys->roundness = action - A_ROUND0));
                canvas->Refresh();
                return nullptr;

            case A_OPENCELLCOLOR:
                if (sys->frame->cellcolordropdown) sys->frame->cellcolordropdown->ShowPopup();
                break;
            case A_OPENTEXTCOLOR:
                if (sys->frame->textcolordropdown) sys->frame->textcolordropdown->ShowPopup();
                break;
            case A_OPENBORDCOLOR:
                if (sys->frame->bordercolordropdown) sys->frame->bordercolordropdown->ShowPopup();
                break;
            case A_OPENIMGDROPDOWN:
                if (sys->frame->imagedropdown) sys->frame->imagedropdown->ShowPopup();
                break;

            case A_REPLACEONCE:
            case A_REPLACEONCEJ:
            case A_REPLACEALL: {
                if (!sys->searchstring.Len()) return _(L"No search.");
                auto replaces = sys->frame->replaces->GetValue();
                auto lreplaces =
                    sys->casesensitivesearch ? (wxString)wxEmptyString : replaces.Lower();
                if (action == A_REPLACEALL) {
                    root->AddUndo(this);  // expensive?
                    root->FindReplaceAll(replaces, lreplaces);
                    root->ResetChildren();
                    canvas->Refresh();
                } else {
                    loopallcellssel(c, true) if (c->text.IsInSearch()) c->AddUndo(this);
                    selected.grid->ReplaceStr(this, replaces, lreplaces, selected);
                    if (action == A_REPLACEONCEJ) return SearchNext(false, true, false);
                }
                return _(L"Text has been replaced.");
            }

            case A_CLEARREPLACE: {
                sys->frame->replaces->Clear();
                canvas->SetFocus();
                return nullptr;
            }

            case A_CLEARSEARCH: {
                sys->frame->filter->Clear();
                canvas->SetFocus();
                return nullptr;
            }

            case A_SCALED:
                scaledviewingmode = !scaledviewingmode;
                root->ResetChildren();
                canvas->Refresh();
                return scaledviewingmode ? _(L"Now viewing TreeSheet to fit to the screen exactly, press F12 to return to normal.")
                                         : _(L"1:1 scale restored.");

            case A_FILTERRANGE: {
                DateTimeRangeDialog rd(sys->frame);
                if (rd.Run() == wxID_OK) ApplyEditRangeFilter(rd.begin, rd.end);
                return nullptr;
            }

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
                wxArrayString strs, keys;
                for (auto &[s, k] : sys->frame->menustrings) {
                    strs.push_back(s);
                    keys.push_back(k);
                }
                wxSingleChoiceDialog choice(
                    sys->frame, _(L"Please pick a menu item to change the key binding for"),
                    _(L"Key binding"), strs);
                choice.SetSize(wxSize(500, 700));
                choice.Centre();
                if (choice.ShowModal() == wxID_OK) {
                    int sel = choice.GetSelection();
                    wxTextEntryDialog textentry(sys->frame,
                                                _(L"Please enter the new key binding string"),
                                                _(L"Key binding"), keys[sel]);
                    if (textentry.ShowModal() == wxID_OK) {
                        auto key = textentry.GetValue();
                        sys->frame->menustrings[strs[sel]] = key;
                        sys->cfg->Write(strs[sel], key);
                        return _(L"NOTE: key binding will take effect next run of TreeSheets.");
                    }
                }
                return _(L"Keybinding cancelled.");
            }

            case A_SETLANG: {
                auto trans = wxTranslations::Get();
                if (!trans) return _(L"Failed to get translation.");
                wxArrayString langs = trans->GetAvailableTranslations(L"ts");
                langs.Insert(wxEmptyString, 0);
                wxSingleChoiceDialog choice(
                    sys->frame, _(L"Please select the language for the interface (requires restart). Please select the empty row if you want to use the default language."),
                    _(L"Available languages"), langs);
                if (choice.ShowModal() == wxID_OK) {
                    sys->cfg->Write(L"defaultlang", choice.GetStringSelection());
                }
                return nullptr;
            }
        }

        if (!selected.grid) return NoSel();

        auto cell = selected.GetCell();

        switch (action) {
            case A_BACKSPACE:
                if (selected.Thin()) {
                    if (selected.xs)
                        DelRowCol(selected.y, 0, selected.grid->ys, 1, -1, selected.y - 1, 0, -1);
                    else
                        DelRowCol(selected.x, 0, selected.grid->xs, 1, selected.x - 1, -1, -1, 0);
                } else if (cell && selected.TextEdit()) {
                    if (selected.cursorend == 0) return nullptr;
                    cell->AddUndo(this);
                    cell->text.Backspace(selected);
                    canvas->Refresh();
                } else {
                    selected.grid->MultiCellDelete(this, selected);
                    SetSelect(selected);
                }
                ZoomOutIfNoGrid();
                return nullptr;

            case A_DELETE:
                if (selected.Thin()) {
                    if (selected.xs)
                        DelRowCol(selected.y, selected.grid->ys, selected.grid->ys, 0, -1,
                                  selected.y, 0, -1);
                    else
                        DelRowCol(selected.x, selected.grid->xs, selected.grid->xs, 0, selected.x,
                                  -1, -1, 0);
                } else if (cell && selected.TextEdit()) {
                    if (selected.cursor == cell->text.t.Len()) return nullptr;
                    cell->AddUndo(this);
                    cell->text.Delete(selected);
                    canvas->Refresh();
                } else {
                    selected.grid->MultiCellDelete(this, selected);
                    SetSelect(selected);
                }
                ZoomOutIfNoGrid();
                return nullptr;

            case A_DELETE_WORD:
                if (cell && selected.TextEdit()) {
                    if (selected.cursor == cell->text.t.Len()) return nullptr;
                    cell->AddUndo(this);
                    cell->text.DeleteWord(selected);
                    canvas->Refresh();
                }
                ZoomOutIfNoGrid();
                return nullptr;

            case wxID_CUT:
            case wxID_COPY:
            case A_COPYWI:
            case A_COPYCT:
                if (selected.Thin()) return NoThin();
                if (selected.TextEdit()) {
                    if (selected.cursor == selected.cursorend) return _(L"No text selected.");
                }
                Copy(action);
                if (action == wxID_CUT) {
                    if (!selected.TextEdit()) {
                        selected.grid->cell->AddUndo(this);
                        selected.grid->MultiCellDelete(this, selected);
                        SetSelect(selected);
                    } else if (cell) {
                        cell->AddUndo(this);
                        cell->text.Backspace(selected);
                    }
                    canvas->Refresh();
                }
                ZoomOutIfNoGrid();
                return nullptr;

            case A_COPYBM:
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxBitmapDataObject(GetSubBitmap(selected)));
                    wxTheClipboard->Close();
                    return _(L"Bitmap copied to clipboard");
                }
                return nullptr;

            case A_COLLAPSE: {
                if (selected.xs * selected.ys == 1)
                    return _(L"More than one cell must be selected.");
                auto fc = selected.GetFirst();
                wxString ct = "";
                loopallcellssel(ci, true) if (ci != fc && ci->text.t.Len()) ct += " " + ci->text.t;
                if (!fc->HasContent() && !ct.Len()) return _(L"There is no content to collapse.");
                fc->parent->AddUndo(this);
                fc->text.t += ct;
                loopallcellssel(ci, false) if (ci != fc) ci->Clear();
                Selection deletesel(selected.grid,
                                    selected.x + int(selected.xs > 1),  // sidestep is possible?
                                    selected.y + int(selected.ys > 1),
                                    selected.xs - int(selected.xs > 1),
                                    selected.ys - int(selected.ys > 1));
                selected.grid->MultiCellDeleteSub(this, deletesel);
                SetSelect(Selection(selected.grid, selected.x, selected.y, 1, 1));
                fc->ResetLayout();
                canvas->Refresh();
                return nullptr;
            }

            case wxID_SELECTALL:
                selected.SelAll();
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, true);
                return nullptr;

            case A_UP:
            case A_DOWN:
            case A_LEFT:
            case A_RIGHT: selected.Cursor(this, action, false, false); return nullptr;

            case A_MUP:
            case A_MDOWN:
            case A_MLEFT:
            case A_MRIGHT:
                selected.Cursor(this, action - A_MUP + A_UP, true, false);
                return nullptr;

            case A_SUP:
            case A_SDOWN:
            case A_SLEFT:
            case A_SRIGHT:
                selected.Cursor(this, action - A_SUP + A_UP, false, true);
                return nullptr;

            case A_SCLEFT:
            case A_SCRIGHT:
                if (!selected.TextEdit() && action == A_SCLEFT) {
                    selected.xs = selected.Thin() ? selected.x : selected.x + 1;
                    selected.x = 0;
                    sys->frame->UpdateStatus(selected, true);
                    canvas->Refresh();
                    return nullptr;
                }
                if (!selected.TextEdit() && action == A_SCRIGHT) {
                    selected.xs = selected.grid->xs - selected.x;
                    sys->frame->UpdateStatus(selected, true);
                    canvas->Refresh();
                    return nullptr;
                }
                selected.Cursor(this, action - A_SCUP + A_UP, true, true);
                return nullptr;

            case A_SCUP:
            case A_SCDOWN:
                if (!selected.TextEdit() && action == A_SCUP) {
                    selected.ys = selected.Thin() ? selected.y : selected.y + 1;
                    selected.y = 0;
                    sys->frame->UpdateStatus(selected, true);
                    canvas->Refresh();
                }
                if (!selected.TextEdit() && action == A_SCDOWN) {
                    selected.ys = selected.grid->ys - selected.y;
                    sys->frame->UpdateStatus(selected, true);
                    canvas->Refresh();
                }
                return nullptr;

            case wxID_BOLD: selected.grid->SetStyle(this, selected, STYLE_BOLD); return nullptr;
            case wxID_ITALIC: selected.grid->SetStyle(this, selected, STYLE_ITALIC); return nullptr;
            case A_TT: selected.grid->SetStyle(this, selected, STYLE_FIXED); return nullptr;
            case wxID_UNDERLINE:
                selected.grid->SetStyle(this, selected, STYLE_UNDERLINE);
                return nullptr;
            case wxID_STRIKETHROUGH:
                selected.grid->SetStyle(this, selected, STYLE_STRIKETHRU);
                return nullptr;

            case A_MARKDATA:
            case A_MARKVARD:
            case A_MARKVARU:
            case A_MARKVIEWH:
            case A_MARKVIEWV:
            case A_MARKCODE: {
                int newcelltype;
                switch (action) {
                    case A_MARKDATA: newcelltype = CT_DATA; break;
                    case A_MARKVARD: newcelltype = CT_VARD; break;
                    case A_MARKVARU: newcelltype = CT_VARU; break;
                    case A_MARKVIEWH: newcelltype = CT_VIEWH; break;
                    case A_MARKVIEWV: newcelltype = CT_VIEWV; break;
                    case A_MARKCODE: newcelltype = CT_CODE; break;
                }
                selected.grid->cell->AddUndo(this);
                loopallcellssel(c, false) {
                    c->celltype = (newcelltype == CT_CODE) ? sys->evaluator.InferCellType(c->text)
                                                           : newcelltype;
                    canvas->Refresh();
                }
                return nullptr;
            }

            case A_CANCELEDIT:
                if (selected.TextEdit()) break;
                if (selected.grid->cell->parent) {
                    SetSelect(selected.grid->cell->parent->grid->FindCell(selected.grid->cell));
                } else {
                    selected.SelAll();
                }
                ScrollOrZoom();
                return nullptr;

            case A_NEWGRID:
                if (!(cell = selected.ThinExpand(this))) return OneCell();
                if (cell->grid) {
                    SetSelect(Selection(cell->grid, 0, cell->grid->ys, 1, 0));
                    ScrollOrZoom(true);
                } else {
                    cell->AddUndo(this);
                    cell->AddGrid();
                    SetSelect(Selection(cell->grid, 0, 0, 1, 1));
                    paintscrolltoselection = true;
                    canvas->Refresh();
                }
                return nullptr;

            case wxID_PASTE:
                if (!(cell = selected.ThinExpand(this))) return OneCell();
                if (wxTheClipboard->Open()) {
                    if (wxTheClipboard->IsSupported(wxDF_BITMAP)) {
                        wxBitmapDataObject bdo;
                        wxTheClipboard->GetData(bdo);
                        PasteOrDrop(bdo);
                    } else if (wxTheClipboard->IsSupported(wxDF_FILENAME)) {
                        wxFileDataObject fdo;
                        wxTheClipboard->GetData(fdo);
                        PasteOrDrop(fdo);
                    } else if (wxTheClipboard->IsSupported(wxDF_TEXT) ||
                               wxTheClipboard->IsSupported(wxDF_UNICODETEXT)) {
                        wxTextDataObject tdo;
                        wxTheClipboard->GetData(tdo);
                        PasteOrDrop(tdo);
                    }
                    wxTheClipboard->Close();
                    canvas->Refresh();
                } else if (sys->cellclipboard) {
                    cell->Paste(this, sys->cellclipboard.get(), selected);
                    canvas->Refresh();
                }
                return nullptr;

            case A_EDITNOTE: {
                if (!(cell = selected.ThinExpand(this))) return OneCell();

                wxDialog dlg(sys->frame, wxID_ANY, _(L"Note"), wxDefaultPosition,
                             wxSize(sys->notesizex, sys->notesizey),
                             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
                auto *sizer = new wxBoxSizer(wxVERTICAL);
                wxTextCtrl text(&dlg, wxID_ANY, cell->note, wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE);
                sizer->Add(&text, 1, wxEXPAND | wxALL, 10);
                auto *btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
                sizer->Add(btns, 0, wxALIGN_CENTER | wxBOTTOM, 10);
                dlg.SetSizer(sizer);

                text.SetFocus();

                if (dlg.ShowModal() == wxID_OK) {
                    if (cell->note != text.GetValue()) {
                        cell->AddUndo(this);
                        cell->note = text.GetValue();
                        canvas->Refresh();
                    }
                    sys->notesizex = dlg.GetSize().x;
                    sys->notesizey = dlg.GetSize().y;
                }
                return nullptr;
            }

            case A_PASTESTYLE:
                if (!sys->cellclipboard) return _(L"No style to paste.");
                selected.grid->cell->AddUndo(this);
                selected.grid->SetStyles(selected, sys->cellclipboard.get());
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                return nullptr;

            case A_ENTERCELL:
            case A_ENTERCELL_JUMPTOEND:
            case A_ENTERCELL_JUMPTOSTART:
            case A_PROGRESSCELL: {
                if (!(cell = selected.ThinExpand(this, action == A_ENTERCELL_JUMPTOSTART)))
                    return OneCell();
                if (selected.TextEdit()) {
                    selected.Cursor(this, action == A_PROGRESSCELL ? A_RIGHT : A_DOWN, false, false,
                                    true);
                } else {
                    selected.EnterEdit(
                        this,
                        action == A_ENTERCELL_JUMPTOEND ? static_cast<int>(cell->text.t.Len()) : 0,
                        static_cast<int>(cell->text.t.Len()));
                    paintscrolltoselection = true;
                    canvas->Refresh();
                }
                return nullptr;
            }

            case A_IMAGE: {
                if (!(cell = selected.ThinExpand(this))) return OneCell();
                auto filename =
                    ::wxFileSelector(_(L"Please select an image file:"), L"", L"", L"", L"*.*",
                                     wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
                cell->AddUndo(this);
                LoadImageIntoCell(filename, cell, sys->frame->FromDIP(1.0));
                canvas->Refresh();
                return nullptr;
            }

            case A_IMAGER: {
                selected.grid->cell->AddUndo(this);
                selected.grid->ClearImages(selected);
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                return nullptr;
            }

            case A_SORTD: return Sort(true);
            case A_SORT: return Sort(false);

            case A_SCOLS:
                selected.y = 0;
                selected.ys = selected.grid->ys;
                canvas->Refresh();
                return nullptr;

            case A_SROWS:
                selected.x = 0;
                selected.xs = selected.grid->xs;
                canvas->Refresh();
                return nullptr;

            case A_BORD0:
            case A_BORD1:
            case A_BORD2:
            case A_BORD3:
            case A_BORD4:
            case A_BORD5:
                selected.grid->cell->AddUndo(this);
                selected.grid->SetBorder(action - A_BORD0 + 1, selected);
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
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
            case A_LASTIMAGE:
                selected.grid->cell->AddUndo(this);
                loopallcellssel(c, true) switch (action) {
                    case A_RESETSIZE: c->text.relsize = 0; break;
                    case A_RESETWIDTH:
                        for (int x = selected.x; x < selected.x + selected.xs; x++)
                            selected.grid->colwidths[x] = sys->defaultmaxcolwidth;
                        selected.grid->cell->ResetLayout();
                        break;
                    case A_RESETSTYLE: c->text.stylebits = 0; break;
                    case A_RESETCOLOR:
                        if (c->IsTag(this)) {
                            tags[c->text.t] = g_tagcolor_default;
                        } else {
                            c->textcolor = g_textcolor_default;
                        }
                        c->cellcolor = g_cellcolor_default;
                        if (c->grid) c->grid->bordercolor = g_bordercolor_default;
                        break;
                    case A_LASTCELLCOLOR: c->cellcolor = sys->lastcellcolor; break;
                    case A_LASTTEXTCOLOR: c->textcolor = sys->lasttextcolor; break;
                    case A_LASTBORDCOLOR:
                        if (c->parent && c->parent->grid)
                            c->parent->grid->bordercolor = sys->lastbordcolor;
                        break;
                    case A_LASTIMAGE:
                        if (sys->lastimage) c->text.image = sys->lastimage;
                        break;
                }
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, false);
                return nullptr;

            case A_MINISIZE: {
                selected.grid->cell->AddUndo(this);
                CollectCellsSel(false);
                vector<Cell *> outer;
                outer.insert(outer.end(), itercells.begin(), itercells.end());
                for (auto o : outer) {
                    if (o->grid) {
                        loopcellsin(o, c) if (_i) {
                            c->text.relsize = g_deftextsize - g_mintextsize() - c->Depth();
                        }
                    }
                }
                outer.clear();
                selected.grid->cell->ResetChildren();
                canvas->Refresh();
                return nullptr;
            }

            case A_FOLD:
            case A_FOLDALL:
            case A_UNFOLDALL:
                loopallcellssel(c, action != A_FOLD) if (c->grid) {
                    c->AddUndo(this);
                    c->grid->folded = action == A_FOLD ? !c->grid->folded : action == A_FOLDALL;
                    c->ResetChildren();
                }
                canvas->Refresh();
                return nullptr;

            case A_HOME:
            case A_END:
            case A_CHOME:
            case A_CEND:
                if (selected.TextEdit()) break;
                selected.HomeEnd(this, action == A_HOME || action == A_CHOME);
                return nullptr;

            case A_IMAGESCP:
            case A_IMAGESCW:
            case A_IMAGESCF: {
                std::set<Image *> imagestomanipulate;
                long v = 0.0;
                loopallcellssel(c, true) {
                    if (c->text.image) { imagestomanipulate.insert(c->text.image); }
                }
                if (imagestomanipulate.empty()) return nullptr;
                if (action == A_IMAGESCW) {
                    v = wxGetNumberFromUser(_(L"Please enter the new image width:"), _(L"Width"),
                                            _(L"Image Resize"), 500, 10, 4000, sys->frame);
                } else {
                    v = wxGetNumberFromUser(
                        _(L"Please enter the percentage you want the image scaled by:"), L"%",
                        _(L"Image Resize"), 50, 5, 400, sys->frame);
                }
                if (v < 0) return nullptr;
                for (auto image : imagestomanipulate) {
                    if (action == A_IMAGESCW) {
                        int pw = image->pixel_width;
                        if (pw)
                            image->ImageRescale(static_cast<double>(v) / static_cast<double>(pw));
                    } else if (action == A_IMAGESCP) {
                        image->ImageRescale(v / 100.0);
                    } else {
                        image->DisplayScale(v / 100.0);
                    }
                }
                currentdrawroot->ResetChildren();
                currentdrawroot->ResetLayout();
                canvas->Refresh();
                return nullptr;
            }

            case A_IMAGESCN: {
                loopallcellssel(c, true) if (c->text.image) {
                    c->text.image->ResetScale(sys->frame->FromDIP(1.0));
                }
                currentdrawroot->ResetChildren();
                currentdrawroot->ResetLayout();
                canvas->Refresh();
                return nullptr;
            }

            case A_IMAGESVA: {
                set<Image *> imagestosave;
                loopallcellssel(c, true) if (auto image = c->text.image)
                    imagestosave.insert(image);
                if (imagestosave.empty()) return _(L"There are no images in the selection.");
                wxString filename = ::wxFileSelector(
                    _(L"Choose image file to save:"), L"", L"", L"",
                    _(L"PNG file (*.png)|*.png|JPEG file (*.jpg)|*.jpg|All Files (*.*)|*.*"),
                    wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
                if (filename.empty()) return _(L"Save cancelled.");
                auto i = 0;
                for (auto image : imagestosave) {
                    wxFileName fn(filename);
                    wxString finalfilename = fn.GetPathWithSep() + fn.GetName() +
                                             (i == 0 ? wxString() : wxString::Format(L"%d", i)) +
                                             image->GetFileExtension();
                    wxFFileOutputStream os(finalfilename, L"w+b");
                    if (!os.IsOk()) {
                        wxMessageBox(
                            _(L"Error writing image file! (try saving under new filename)."),
                            finalfilename.wx_str(), wxOK, sys->frame);
                        return _(L"Error writing to file.");
                    }
                    os.Write(image->data.data(), image->data.size());
                    i++;
                }
                return _(L"Image(s) have been saved to disk.");
            }

            case A_SAVE_AS_JPEG:
            case A_SAVE_AS_PNG:
                loopallcellssel(c, true) {
                    auto image = c->text.image;
                    if (action == A_SAVE_AS_JPEG && image && image->type == 'I') {
                        auto transferimage = ConvertBufferToWxImage(image->data, wxBITMAP_TYPE_PNG);
                        image->data = ConvertWxImageToBuffer(transferimage, wxBITMAP_TYPE_JPEG);
                        image->type = 'J';
                        return _(L"Images in selected cells have been converted to JPEG format.");
                    }
                    if (action == A_SAVE_AS_PNG && image && image->type == 'J') {
                        auto transferimage =
                            ConvertBufferToWxImage(image->data, wxBITMAP_TYPE_JPEG);
                        image->data = ConvertWxImageToBuffer(transferimage, wxBITMAP_TYPE_PNG);
                        image->type = 'I';
                        return _(L"Images in selected cells have been converted to PNG format.");
                    }
                }

            case A_BROWSE: {
                const wxChar *returnmessage = nullptr;
                int counter = 0;
                loopallcellssel(c, false) {
                    if (counter >= g_max_launches) {
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
                loopallcellssel(c, false) {
                    if (counter >= g_max_launches) {
                        returnmessage = _(L"Maximum number of launches reached.");
                        break;
                    }
                    auto f = c->text.ToText(0, selected, A_EXPTEXT);
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

            case A_TAGADD: {
                loopallcellssel(c, false) {
                    if (!c->text.t.Len()) continue;
                    tags[c->text.t] = g_tagcolor_default;
                }
                canvas->Refresh();
                return nullptr;
            }

            case A_TAGREMOVE: {
                loopallcellssel(c, false) tags.erase(c->text.t);
                canvas->Refresh();
                return nullptr;
            }

            case A_TRANSPOSE: {
                if (selected.Thin()) return NoThin();
                auto ac = selected.grid->cell;
                ac->AddUndo(this);
                if (selected.IsAll()) {
                    ac->grid->Transpose();
                    SetSelect(ac->parent ? ac->parent->grid->FindCell(ac) : Selection());
                } else {
                    loopallcellssel(c, false) if (c->grid) c->grid->Transpose();
                }
                ac->ResetChildren();
                canvas->Refresh();
                return nullptr;
            }
        }

        if (cell || (!cell && selected.IsAll())) {
            auto ac = cell ? cell : selected.grid->cell;
            switch (action) {
                case A_HIFY:
                    if (!ac->grid) return NoGrid();
                    if (!ac->grid->IsTable())
                        return _(
                            L"Selected grid is not a table: cells must not already have sub-grids.");
                    ac->AddUndo(this);
                    ac->grid->Hierarchify(this);
                    ac->ResetChildren();
                    selected = Selection();
                    begindrag = Selection();
                    canvas->Refresh();
                    return nullptr;

                case A_FLATTEN: {
                    if (!ac->grid) return NoGrid();
                    ac->AddUndo(this);
                    int maxdepth = 0, leaves = 0;
                    ac->MaxDepthLeaves(0, maxdepth, leaves);
                    auto g = new Grid(maxdepth, leaves);
                    g->InitCells();
                    ac->grid->Flatten(0, 0, g);
                    DELETEP(ac->grid);
                    ac->grid = g;
                    g->ReParent(ac);
                    ac->ResetChildren();
                    selected = Selection();
                    begindrag = Selection();
                    canvas->Refresh();
                    return nullptr;
                }
            }
        }

        if (!cell) return OneCell();

        switch (action) {
            case A_NEXT: selected.Next(this, false); return nullptr;
            case A_PREV: selected.Next(this, true); return nullptr;

            case A_ENTERGRID:
                if (!cell->grid) Action(A_NEWGRID);
                SetSelect(Selection(cell->grid, 0, 0, 1, 1));
                ScrollOrZoom(true);
                return nullptr;

            case A_LINK:
            case A_LINKIMG:
            case A_LINKREV:
            case A_LINKIMGREV: {
                if ((action == A_LINK || action == A_LINKREV) && !cell->text.t.Len())
                    return _(L"No text in this cell.");
                if ((action == A_LINKIMG || action == A_LINKIMGREV) && !cell->text.image)
                    return _(L"No image in this cell.");
                bool t1 = false, t2 = false;
                auto link = root->FindLink(selected, cell, nullptr, t1, t2,
                                           action == A_LINK || action == A_LINKIMG,
                                           action == A_LINKIMG || action == A_LINKIMGREV);
                if (!link || !link->parent) return _(L"No matching cell found!");
                SetSelect(link->parent->grid->FindCell(link));
                ScrollOrZoom(true);
                return nullptr;
            }

            case A_COLCELL: sys->customcolor = cell->cellcolor; return nullptr;

            case A_HSWAP: {
                auto pp = cell->parent->parent;
                if (!pp) return _(L"Cannot move this cell up in the hierarchy.");
                if (pp->grid->xs != 1 && pp->grid->ys != 1)
                    return _(L"Can only move this cell into a Nx1 or 1xN grid.");
                if (cell->parent->grid->xs != 1 && cell->parent->grid->ys != 1)
                    return _(L"Can only move this cell from a Nx1 or 1xN grid.");
                pp->AddUndo(this);
                SetSelect(pp->grid->HierarchySwap(cell->text.t));
                pp->ResetChildren();
                pp->ResetLayout();
                canvas->Refresh();
                return nullptr;
            }

            case A_FILTERBYCELLBG:
                loopallcells(ci) ci->text.filtered = ci->cellcolor != cell->cellcolor;
                root->ResetChildren();
                canvas->Refresh();
                return nullptr;

            case A_FILTERNOTE:
                loopallcells(ci) ci->text.filtered = ci->note.IsEmpty();
                root->ResetChildren();
                canvas->Refresh();
                return nullptr;

            case A_FILTERMATCHNEXT:
                bool lastsel = true;
                Cell *next = root->FindNextFilterMatch(nullptr, selected.GetCell(), lastsel);
                if (!next) return _(L"No matches for filter.");
                if (next->parent) SetSelect(next->parent->grid->FindCell(next));
                canvas->SetFocus();
                ScrollOrZoom(true);
                return nullptr;
        }

        if (!selected.TextEdit()) return _(L"only works in cell text mode");

        switch (action) {
            case A_CANCELEDIT:
                if (LastUndoSameCellTextEdit(cell))
                    Undo(undolist, redolist);
                else
                    canvas->Refresh();
                selected.ExitEdit(this);
                return nullptr;

            case A_BACKSPACE_WORD:
                if (selected.cursorend == 0) return nullptr;
                cell->AddUndo(this);
                cell->text.BackspaceWord(selected);
                canvas->Refresh();
                ZoomOutIfNoGrid();
                return nullptr;

            case A_SHOME:
            case A_SEND:
            case A_CHOME:
            case A_CEND:
            case A_HOME:
            case A_END: {
                switch (action) {
                    case A_SHOME:  // FIXME: this functionality is really SCHOME, SHOME should be
                                   // within line
                        selected.cursor = 0;
                        break;
                    case A_SEND: selected.cursorend = static_cast<int>(cell->text.t.Len()); break;
                    case A_CHOME: selected.cursor = selected.cursorend = 0; break;
                    case A_CEND: selected.cursor = selected.cursorend = selected.MaxCursor(); break;
                    case A_HOME: cell->text.HomeEnd(selected, true); break;
                    case A_END: cell->text.HomeEnd(selected, false); break;
                }
                paintscrolltoselection = true;
                canvas->Refresh();
                return nullptr;
            }
            default: return _(L"Internal error: unimplemented operation!");
        }
    }

    const wxChar *SearchNext(bool focusmatch, bool jump, bool reverse) {
        if (!root) return nullptr;  // fix crash when opening new doc
        if (!sys->searchstring.Len()) return _(L"No search string.");
        bool lastsel = true;
        Cell *next = root->FindNextSearchMatch(sys->searchstring, nullptr, selected.GetCell(),
                                               lastsel, reverse);
        if (!next) return _(L"No matches for search.");
        if (!jump) return nullptr;
        SetSelect(next->parent->grid->FindCell(next));
        if (focusmatch) canvas->SetFocus();
        ScrollOrZoom(true);
        return nullptr;
    }

    const wxChar *layrender(int ds, bool vert, bool toggle = false, bool noset = false) {
        if (selected.Thin()) return NoThin();
        selected.grid->cell->AddUndo(this);
        bool v = toggle ? !selected.GetFirst()->verticaltextandgrid : vert;
        if (ds >= 0 && selected.IsAll()) selected.grid->cell->drawstyle = ds;
        selected.grid->SetGridTextLayout(ds, v, noset, selected);
        selected.grid->cell->ResetChildren();
        canvas->Refresh();
        return nullptr;
    }

    void ZoomOutIfNoGrid() {
        if (!WalkPath(drawpath)->grid) Zoom(-1);
    }

    void PasteSingleText(Cell *c, const wxString &s) { c->text.Insert(this, s, selected, false); }

    // Polymorphism with wxDataObjectSimple does not work on Windows; bitmap format seems to not be
    // recognized.

    void PasteOrDrop(const wxFileDataObject &filedataobject) {
        const wxArrayString &filenames = filedataobject.GetFilenames();
        if (filenames.size() != 1) {
            sys->frame->SetStatus(_(L"Can paste or drop only exactly one file."));
            return;
        }
        Cell *cell = selected.ThinExpand(this);
        wxString filename = filenames[0];
        wxFFileInputStream fileinputstream(filename);
        if (fileinputstream.IsOk()) {
            char buffer[4];
            fileinputstream.Read(buffer, 4);
            if (!strncmp(buffer, "TSFF", 4)) {
                ThreeChoiceDialog askuser(
                    sys->frame, filename,
                    _(L"It seems that you are about to paste or drop a TreeSheets file. "
                      L"What would you like to do?"),
                    _(L"Open TreeSheets file"), _(L"Paste file path"), _(L"Cancel"));
                switch (askuser.Run()) {
                    case 0: sys->frame->SetStatus(sys->LoadDB(filename));
                    case 2: return;
                    default:
                    case 1:;
                }
            }
        }
        if (!cell) return;
        cell->AddUndo(this);
        if (!LoadImageIntoCell(filename, cell, sys->frame->FromDIP(1.0)))
            PasteSingleText(cell, filename);
    }

    void PasteOrDrop(const wxTextDataObject &textdataobject) {
        if (textdataobject.GetText() != wxEmptyString) {
            Cell *cell = selected.ThinExpand(this);
            auto text = textdataobject.GetText();
            if ((sys->clipboardcopy == text) && sys->cellclipboard) {
                cell->Paste(this, sys->cellclipboard.get(), selected);
            } else {
                const wxArrayString &lines = wxStringTokenize(text, LINE_SEPARATOR);
                if (lines.size() == 1) {
                    cell->AddUndo(this);
                    cell->ResetLayout();
                    PasteSingleText(cell, lines[0]);
                } else if (lines.size() > 1) {
                    cell->parent->AddUndo(this);
                    cell->ResetLayout();
                    DELETEP(cell->grid);
                    sys->FillRows(cell->AddGrid(), lines, sys->CountCol(lines[0]), 0, 0);
                    if (!cell->HasText())
                        cell->grid->MergeWithParent(cell->parent->grid, selected, this);
                }
            }
        }
    }

    void PasteOrDrop(const wxBitmapDataObject &bitmapdataobject) {
        if (bitmapdataobject.GetBitmap().GetRefData() != wxNullBitmap.GetRefData()) {
            Cell *cell = selected.ThinExpand(this);
            cell->AddUndo(this);
            auto image = bitmapdataobject.GetBitmap().ConvertToImage();
            vector<uint8_t> buffer = ConvertWxImageToBuffer(image, wxBITMAP_TYPE_PNG);
            SetImageBM(cell, std::move(buffer), 'I', sys->frame->FromDIP(1.0));
            cell->Reset();
        }
    }

    const wxChar *Sort(bool descending) {
        if (selected.xs != 1 && selected.ys <= 1)
            return _(
                L"Can't sort: make a 1xN selection to indicate what column to sort on, and what rows to affect");
        selected.grid->cell->AddUndo(this);
        selected.grid->Sort(selected, descending);
        canvas->Refresh();
        return nullptr;
    }

    void DelRowCol(int &v, int e, int gvs, int dec, int dx, int dy, int nxs, int nys) {
        if (v != e) {
            selected.grid->cell->AddUndo(this);
            if (gvs == 1) {
                selected.grid->DelSelf(this, selected);
            } else {
                selected.grid->DeleteCells(dx, dy, nxs, nys);
                v -= dec;
            }
            canvas->Refresh();
        }
    }

    void CreatePath(Cell *c, auto &path) {
        path.clear();
        while (c->parent) {
            const Selection &s = c->parent->grid->FindCell(c);
            ASSERT(s.grid);
            path.push_back(s);
            c = c->parent;
        }
    }

    Cell *WalkPath(auto &path) {
        Cell *c = root;
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
               undolist.back()->cloned_from == (uintptr_t)c;
    }

    bool LastUndoSameCellTextEdit(Cell *c) {
        // hacky way to detect word boundaries to stop coalescing, but works, and
        // not a big deal if selected is not actually related to this cell
        return undolist.size() && !c->grid && undolist.size() != undolistsizeatfullsave &&
               undolist.back()->sel.EqLoc(c->parent->grid->FindCell(c)) &&
               (!c->text.t.EndsWith(" ") || c->text.t.Len() != selected.cursor);
    }

    void AddUndo(Cell *c, bool newgeneration = true) {
        redolist.clear();
        lastmodsinceautosave = wxGetLocalTime();
        if (!modified) {
            modified = true;
            UpdateFileName();
        }
        if (LastUndoSameCellTextEdit(c)) return;
        auto ui = make_unique<UndoItem>();
        ui->clone = c->Clone(nullptr);
        ui->estimated_size = c->EstimatedMemoryUse();
        ui->sel = selected;
        ui->cloned_from = (uintptr_t)c;
        if (undolist.size()) ui->generation = undolist.back()->generation + (newgeneration ? 1 : 0);
        CreatePath(c, ui->path);
        if (selected.grid) CreatePath(selected.grid->cell, ui->selpath);
        undolist.push_back(std::move(ui));
        size_t total_usage = 0;
        size_t old_list_size = undolist.size();
        // Cull undolist. Always at least keeps last item.
        for (auto i = static_cast<int>(undolist.size()) - 1; i >= 0; i--) {
            // Cull old items if using more than 100MB or 1000 items, whichever comes first.
            // TODO: make configurable?
            if (total_usage < 100 * 1024 * 1024 && undolist.size() - i < 1000) {
                total_usage += undolist[i]->estimated_size;
            } else {
                undolist.erase(undolist.begin(), undolist.begin() + i + 1);
                break;
            }
        }
        size_t items_culled = old_list_size - undolist.size();
        undolistsizeatfullsave -= items_culled;  // Allowed to go < 0
    }

    void Undo(auto &fromlist, auto &tolist, bool redo = false) {
        for (bool next = true; next; ) {
            UndoEach(fromlist, tolist, redo);
            next = (fromlist.size() && tolist.size() &&
                    fromlist.back()->generation == tolist.back()->generation)
                       ? true
                       : false;
        }
        if (selected.grid)
            ScrollOrZoom();
        else
            canvas->Refresh();
        UpdateFileName();
    }

    void UndoEach(auto &fromlist, auto &tolist, bool redo = false) {
        auto beforesel = selected;
        vector<Selection> beforepath;
        if (beforesel.grid) CreatePath(beforesel.grid->cell, beforepath);
        auto ui = std::move(fromlist.back());
        fromlist.pop_back();
        auto c = WalkPath(ui->path);
        auto clone = ui->clone.release();
        ui->clone.reset(c);
        if (c->parent && c->parent->grid) {
            c->parent->grid->ReplaceCell(c, clone);
            clone->parent = c->parent;
        } else
            root = clone;
        clone->ResetLayout();
        SetSelect(ui->sel);
        if (selected.grid) selected.grid = WalkPath(ui->selpath)->grid;
        begindrag = selected;
        ui->sel = beforesel;
        ui->selpath = std::move(beforepath);
        tolist.push_back(std::move(ui));
        if (undolistsizeatfullsave > undolist.size())
            undolistsizeatfullsave = -1;  // gone beyond the save point, always modified
        modified = undolistsizeatfullsave != undolist.size();
    }

    void ColorChange(int which, int idx) {
        if (!selected.grid) return;
        auto col = idx == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[idx];
        switch (which) {
            case A_CELLCOLOR: sys->lastcellcolor = col; break;
            case A_TEXTCOLOR: sys->lasttextcolor = col; break;
            case A_BORDCOLOR: sys->lastbordcolor = col; break;
        }
        selected.grid->ColorChange(this, which, col, selected);
    }

    void SetImageBM(Cell *c, auto &&data, char type, double scale) {
        c->text.image = sys->lastimage =
            sys->imagelist[sys->AddImageToList(scale, std::move(data), type)].get();
    }

    bool LoadImageIntoCell(const wxString &filename, Cell *c, double scale) {
        if (filename.empty()) return false;
        auto pnghandler = wxImage::FindHandler(wxBITMAP_TYPE_PNG);
        auto jpghandler = wxImage::FindHandler(wxBITMAP_TYPE_JPEG);
        wxImageHandler *activeHandler = nullptr;
        if (pnghandler && pnghandler->CanRead(filename)) {
            activeHandler = pnghandler;
        } else if (jpghandler && jpghandler->CanRead(filename)) {
            activeHandler = jpghandler;
        }
        if (activeHandler) {
            wxFile fn(filename);
            if (!fn.IsOpened()) return false;
            vector<uint8_t> buffer(fn.Length());
            fn.Read(buffer.data(), buffer.size());
            SetImageBM(c, std::move(buffer), (activeHandler == pnghandler) ? 'I' : 'J', scale);
        } else {
            wxImage image;
            if (!image.LoadFile(filename)) return false;
            auto buffer = ConvertWxImageToBuffer(image, wxBITMAP_TYPE_PNG);
            SetImageBM(c, std::move(buffer), 'I', scale);
        }
        c->Reset();
        return true;
    }

    void ImageChange(wxString &filename, double scale) {
        if (!selected.grid) return;
        selected.grid->cell->AddUndo(this);
        loopallcellssel(c, false) LoadImageIntoCell(filename, c, scale);
        canvas->Refresh();
    }

    void RecreateTagMenu(wxMenu &menu) {
        int i = A_TAGSET;
        for (auto &[tag, color] : tags) { menu.Append(i++, tag); }
        if (tags.size()) menu.AppendSeparator();
        menu.Append(A_TAGADD, _(L"&Add Cell Text as Tag"));
        menu.Append(A_TAGREMOVE, _(L"&Remove Cell Text from Tags"));
    }

    const wxChar *TagSet(int tagno) {
        int i = 0;
        for (auto &[tag, color] : tags)
            if (i++ == tagno) {
                selected.grid->cell->AddUndo(this);
                loopallcellssel(c, false) {
                    c->text.Clear(this, selected);
                    c->text.Insert(this, tag, selected, true);
                }
                selected.grid->cell->ResetChildren();
                selected.grid->cell->ResetLayout();
                canvas->Refresh();
                return nullptr;
            }
        ASSERT(0);
        return nullptr;
    }

    void CollectCells(Cell *c) {
        itercells.clear();
        c->CollectCells(itercells);
    }

    void CollectCellsSel(bool recurse) {
        itercells.clear();
        if (selected.grid) selected.grid->CollectCellsSel(itercells, selected, recurse);
    }

    void ApplyEditFilter() {
        searchfilter = false;
        paintscrolltoselection = true;
        editfilter = min(max(editfilter, 1), 99);
        CollectCells(root);
        ranges::sort(itercells, [](auto a, auto b) {
            // sort in descending order
            return a->text.lastedit > b->text.lastedit;
        });
        loopv(i, itercells) itercells[i]->text.filtered = i > itercells.size() * editfilter / 100;
        root->ResetChildren();
        canvas->Refresh();
    }

    void ApplyEditRangeFilter(wxDateTime &rangebegin, wxDateTime &rangeend) {
        searchfilter = false;
        paintscrolltoselection = true;
        CollectCells(root);
        for (auto c : itercells) {
            c->text.filtered = !c->text.lastedit.IsBetween(rangebegin, rangeend);
        }
        root->ResetChildren();
        canvas->Refresh();
    }

    wxDateTime ParseDateTimeString(const wxString &s) {
        wxDateTime dt;
        wxString::const_iterator end;
        if (!dt.ParseDateTime(s, &end)) dt = wxInvalidDateTime;
        return dt;
    }

    void SetSearchFilter(bool on) {
        searchfilter = on;
        paintscrolltoselection = true;
        loopallcells(c) c->text.filtered = on && !c->text.IsInSearch();
        root->ResetChildren();
        canvas->Refresh();
    }

    void ExportAllImages(const wxString &filename, Cell *exportroot) {
        std::set<Image *> exportimages;
        CollectCells(exportroot);
        for (auto c : itercells)
            if (c->text.image) exportimages.insert(c->text.image);
        wxFileName fn(filename);
        auto directory = fn.GetPathWithSep();
        for (auto image : exportimages)
            if (!image->ExportToDirectory(directory)) break;
    }
};
