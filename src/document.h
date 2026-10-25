struct UndoItem {
    vector<Selection> path;
    vector<Selection> selpath;
    Selection sel;
    unique_ptr<Cell> clone;
    bool textedit {false};  // A gridless snapshot; undo swaps only the Text state.
    // A partial snapshot (see Document::AddUndo()) leaves out the cells of its grid that are null,
    // and the grids of the cells there that have none: for each, reuse is the position of the
    // cell in the grid of the cell it replaces when undone that they are taken from, or -1.
    bool partial {false};
    vector<int> reuse;
    // Until Document::FinishUndo(), the left out cells are those of the document in pending
    // instead, where the cells left out (null for the others) are, and the grids are shared.
    vector<Cell *> pending;
    unordered_map<Cell *, int> pendingpos;  // made by Document::RescueForUndo()
    size_t estimated_size {0};
    uintptr_t cloned_from {};  // May be dead.
    int generation {0};

    ~UndoItem() { Document::ForgetPendingUndo(this); }
};

// Caches the result of locating the thin (non-range) text cursor within its cell's
// wrapped lines -- see Text::DrawCursor. That lookup is redrawn on every repaint while
// a cell is being edited, not just on edits, and its expensive part (scanning wrapped
// lines and measuring them with GetTextExtent) is a pure function of the text, cursor
// index, font, column width and alignment. The cell's actual screen position and row height are
// deliberately *not* cached here (they can shift for reasons unrelated to this cell)
// and are recomputed fresh on every use instead.
struct CursorPosCache {
    Cell *cell {nullptr};
    Image *image {nullptr};
    wxString text;
    std::vector<TextRun> runs;
    std::vector<TextLink<wxString>> links;
    int cursor {-1};
    int stylebits {-1};
    int relsize {INT_MIN};
    int maxcolwidth {-1};
    int align {-1};
    int alignwidth {-1};  // Cell::TextAlignWidth(), which aligned text depends on
    bool leading {false};  // drawn before the character at the cursor, see Selection
    bool found {false};
    int localdx {0};  // horizontal offset from the cell's own origin
    int line {0};      // which wrapped line the cursor is on
};

// Every field Cell::Reset() zeroes out. Document::Key()'s fast path (see
// FastRelayoutAfterEdit) needs to undo that reset precisely for the cells it decides
// not to relayout, or their cached geometry is left zeroed/stale -- see
// FastRelayoutAfterEdit for which fields that applies to for which cell.
struct CellGeom {
    int sx, sy, ox, oy, minx, miny, ycenteroff;
};

struct Document {
    TSCanvas *canvas {nullptr};
    // Set while Wheel(..., deferlayout = true) changed sizes/widths but left the cached cell
    // geometry untouched (so paints and hover stay consistent); UpdateLayout() resets it.
    bool layoutpending {false};
    // Identifies the last undo item added by AddResizeUndo(), so that further steps of a deferred
    // burst (which the user can't see individually), or of column width changes, don't each add
    // another one, unless they change cells outside of its scope. While it is the last one,
    // nothing else changed the document since.
    Cell *pendingundocell {nullptr};
    UndoItem *pendingundoitem {nullptr};
    Selection pendingundoscope;
    bool pendingundorecursive {false};
    bool pendingundowidth {false};
    // The cells whose size the burst changed, to lay out again, or all cells (no grid).
    Selection pendingsel;
    bool pendinghierarchical {false};
    unique_ptr<Cell> root {nullptr};
    Selection prev;
    Selection hover;
    // The fold icon under the pointer, in document coordinates, or empty.
    wxRect hoverfoldicon;
    wxRect hovernote;        // The note triangle under the pointer, likewise.
    wxRect hoverlink;        // The part of a link under the pointer, likewise.
    int hoverlinkstart {0};  // where that link starts in the text of the hovered cell
    bool hoverknob {false};  // The pointer is on the knob at the selection's bottom-right corner.
    bool knobdrag {false};   // The mouse button went down on that knob and is still held.
    // The pointer for the knob or a link, or wxCURSOR_NONE. See SetHoverCursor().
    wxStockCursor hovercursor {wxCURSOR_NONE};
    // While exporting to HTML, the anchors of the cells that internal links go to.
    std::map<const Cell *, int> htmlanchors;
    // While exporting to PDF, the parts of links that were drawn.
    struct DrawnLink {
        wxRect rect;
        const Cell *cell;
        const TextLink<wxString> *link;
    };
    vector<DrawnLink> *drawnlinks {nullptr};
    // With the hover shadow option: the shaded area of the hovered cell, in document
    // coordinates, or empty. A rect rather than `hover`, as that may point into a grid that
    // has been deleted since.
    wxRect hovershade;
    Selection selected;
    Selection begindrag;
    // The multi-cell selection that a ctrl(+shift)+click inside it changed, which the double-click
    // it may start folds as a whole.
    Selection foldsel;
    // The grids that the press on a fold icon unfolded, while the button is held.
    vector<shared_ptr<Grid>> foldpeek;
    wxLongLong foldpeekstart;
    int isctrlshiftdrag {0};
    // The cells added to the selection with ctrl+click, for the actions in MultiSelAction(). They
    // last while the selection stays where the last ctrl+click left it (multiselanchor).
    vector<Selection> multisel;
    Selection multiselanchor;
    Selection ctrlclickfrom;  // the selection before the ctrl+click
    int undogroup {0};  // see ForEachSelection()
    int scrollx {0};
    int scrolly {0};
    int maxx {0};
    int maxy {0};
    int centerx {0};
    int centery {0};
    // With hover zoom, zooming with the mouse wheel keeps the point of the cell under the
    // pointer where it is (see ZoomAtPointer). That can take scrolling beyond the edges of the
    // document, which the scrollbars can't. (anchorx, anchory) is the pixel offset of the
    // document that makes up for that. While anchored, it replaces the centering offset.
    bool anchored {false};
    int anchorx {0};
    int anchory {0};
    int layoutxs {0};
    int layoutys {0};
    int layouts {0};  // how often Layout() laid out cells, see RefreshSelect()
    int hierarchysize {0};
    int fgutter {6};
    int lasttextsize {0};
    int laststylebits {0};
    // dc.GetCharHeight() of the font PickFont() last selected, or -1 if not measured yet. See
    // CharHeight().
    int lastcharheight {-1};
    // Only screen DCs share fonts, and their heights once measured (-1 before). Printer/export
    // DCs select their own resources.
    map<pair<int, int>, pair<wxFont, int>> fontcache;
    int *lastfontcacheheight {nullptr};
    wxString fontcacheface, fontcachefixedface;
    wxSize fontcachedpi;
    double fontcachescale {0};
    int fontcachebasesize {0};
    bool usescreenfonts {false};
    Cell *currentdrawroot {nullptr};  // for use during Render() calls
    CursorPosCache cursorposcache;
    vector<unique_ptr<UndoItem>> undolist;
    vector<unique_ptr<UndoItem>> redolist;
    vector<Selection> drawpath;
    int pathscalebias {0};
    wxString filename {""};
    long lastmodsinceautosave {0};
    long undolistsizeatfullsave {0};
    long lastsave {wxGetLocalTime()};
    bool modified {false};
    bool tmpsavesuccess {true};
    unique_ptr<Encryption> encryption;  // Set if the document is saved with a password.
    // Exports made along with each save: A_AUTOEXPORT_HTML_* - A_AUTOEXPORT_HTML_NONE, and PDF.
    int autohtmlexport {0};
    bool autopdfexport {false};
    wxDataObjectComposite *dndobjc {new wxDataObjectComposite()};
    wxTextDataObject *dndobjt {new wxTextDataObject()};
    wxBitmapDataObject *dndobji {new wxBitmapDataObject()};
    wxFileDataObject *dndobjf {new wxFileDataObject()};

    struct Printout : wxPrintout {
        Document *doc;
        Printout(Document *d) : wxPrintout("printout"), doc(d) {}

        bool OnPrintPage(int page) override {
            auto *dc = GetDC();
            if (dc == nullptr) { return false; }
            doc->Print(*dc, *this);
            return true;
        }

        bool OnBeginDocument(int startPage, int endPage) override {
            return wxPrintout::OnBeginDocument(startPage, endPage);
        }

        void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo) override {
            *minPage = 1;
            *maxPage = 1;
            *selPageFrom = 1;
            *selPageTo = 1;
        }

        bool HasPage(int pageNum) override { return pageNum == 1; }
    };

    bool while_printing {false};
    wxPrintData printData;
    wxPageSetupDialogData pageSetupData;
    uint printscale {0};
    bool scaledviewingmode {false};
    double currentviewscale {1.0};
    int editfilter {0};
    bool formulasdirty {true};  // see OnIdle()
    // The variables that the last run assigned, for formulas (see Evaluator).
    std::unordered_map<wxString, unique_ptr<Cell>> vars;
    // The cell whose text was being edited at the last OnIdle(), and the cell that was typed in
    // last. Either may have been deleted since.
    Cell *editcell {nullptr};
    const Cell *textedited {nullptr};
    wxDateTime lastmodificationtime;
    map<wxString, pair<uint, uint>> tags;
    set<wxString> filtertags;
    vector<Cell *> itercells;

    #define loopcellsin(par, c) \
        CollectCells(par);      \
        loopv(_i, itercells) for (auto c = itercells[_i]; c; c = nullptr)
    #define loopallcells(c)     \
        CollectCells(root.get()); \
        for (auto c : itercells)
    #define loopallcellssel(c, rec) \
        CollectCellsSel(rec);     \
        for (auto c : itercells)

    Document() : pageSetupData(printData) {
        ResetFont();
        pageSetupData.SetMarginTopLeft(wxPoint(15, 15));
        pageSetupData.SetMarginBottomRight(wxPoint(15, 15));
        dndobjc->Add(dndobjt);
        dndobjc->Add(dndobji);
        dndobjc->Add(dndobjf);
    }

    uint Background() const { return root ? root->cellcolor : 0xFFFFFF; }

    void InitCellSelect(Cell *initialselected, int xsize, int ysize) {
        if (initialselected == nullptr || initialselected->parent == nullptr) {
            SetSelect(Selection(root->grid, 0, 0, 1, 1));
            return;
        }
        SetSelect(initialselected->parent->grid->FindCell(initialselected));
        if (selected.grid == nullptr) { return; }
        selected.xs = std::clamp(xsize, 1, selected.grid->xs - selected.x);
        selected.ys = std::clamp(ysize, 1, selected.grid->ys - selected.y);
        sys->frame->UpdateStatus(selected, true);
    }

    void InitWith(unique_ptr<Cell> root, const wxString &filename, Cell *initialselected, int xsize, int ysize) {
        this->root = std::move(root);
        InitCellSelect(initialselected, xsize, ysize);
        ChangeFileName(filename, false);
    }

    void UpdateFileName(int page = -1) const {
        sys->frame->SetPageTitle(filename, modified ? (lastmodsinceautosave != 0 ? "*" : "+") : "",
                                 page);
    }

    void ChangeFileName(const wxString &newfilename, bool checkext) {
        filename = newfilename;
        if (checkext) {
            wxFileName wxfn(filename);
            if (!wxfn.HasExt()) { filename.Append(".cts"); }
        }
        UpdateFileName();
    }

    wxString SaveDB(bool *success, bool istempfile = false, int page = -1) {
        if (filename.empty()) { return _("Save cancelled."); }
        Cell *ocs = nullptr;
        if (selected.xs != 0 || selected.ys != 0) {
            ocs = selected.grid->C(
                selected.x != selected.grid->xs ? selected.x : selected.x - 1,
                selected.y != selected.grid->ys ? selected.y : selected.y - 1)
            .get();
        }
        auto start_saving_time = wxGetLocalTimeMillis();

        auto targetfilename = istempfile ? treesheets::System::TmpName(filename) : filename;
        auto savefilename = treesheets::System::NewName(targetfilename);

        {  // limit destructors
            wxBusyCursor wait;
            wxFFileOutputStream fos(savefilename);
            if (!fos.IsOk()) {
                if (!istempfile) {
                    auto answer =
                        wxMessageBox(_("Error writing TreeSheets file! Would you like to "
                                       "save at a different location?"),
                                     savefilename, wxYES_NO | wxICON_QUESTION, sys->frame);
                    switch (answer) {
                        case wxYES: return Save(true);
                        default: return _("Save cancelled.");
                    }
                }
                return _("Error writing to file.");
            }

            fos.Write("TSFF", 4);
            char vers = TS_VERSION;
            fos.Write(&vers, 1);
            fos.PutC(encryption ? Encryption::method : 0);
            // With a password, the rest is encrypted as a whole once written.
            wxMemoryOutputStream mos;
            auto &os = encryption ? static_cast<wxOutputStream &>(mos) : fos;
            wxDataOutputStream sos(os);
            sos.Write8(selected.xs);
            sos.Write8(selected.ys);
            sos.Write8(ocs != nullptr ? drawpath.size() : 0);  // zoom level
            sos.Write8(autohtmlexport | static_cast<int>(autopdfexport) << 2);
            RefreshImageRefCount(true);
            int realindex = 0;
            loopv(i, sys->imagelist) {
                if (auto &image = *sys->imagelist[i]; image.trefc) {
                    os.PutC(image.type);
                    sos.WriteDouble(image.display_scale);
                    wxInt64 imagelen(image.data.size());
                    sos.Write64(imagelen);
                    os.Write(image.data.data(), imagelen);
                    image.savedindex = realindex++;
                }
            }

            os.Write("D", 1);
            {
                wxZlibOutputStream zos(os, 9);
                if (!zos.IsOk()) { return _("Zlib error while writing file."); }
                wxDataOutputStream dos(zos);
                root->Save(dos, ocs);
                for (auto &[tag, colors] : tags) {
                    auto &[cellcolor, textcolor] = colors;
                    dos.WriteString(tag);
                    dos.Write32(cellcolor);
                    dos.Write32(textcolor);
                }
                dos.WriteString(wxEmptyString);
            }
            if (encryption) {
                vector<uint8_t> body(mos.GetLength());
                mos.CopyTo(body.data(), body.size());
                if (!encryption->Write(fos, body)) { return _("Error encrypting file."); }
            }
        }

        if (!istempfile && ::wxFileExists(filename)) {
            // On the first save with a password, a backup would keep the contents readable.
            if (encryption && !Encryption::IsEncrypted(filename)) {
                ::wxRemoveFile(treesheets::System::BakName(filename));
            } else if (sys->makebaks) {
                ::wxRemoveFile(treesheets::System::BakName(filename));
                ::wxRenameFile(filename, treesheets::System::BakName(filename));
            }
        }

        if (!::wxRenameFile(savefilename, targetfilename, true)) {
            return _("Error renaming temporary file.");
        }

        lastmodsinceautosave = 0;
        lastsave = wxGetLocalTime();
        auto end_saving_time = wxGetLocalTimeMillis();

        if (!istempfile) {
            undolistsizeatfullsave = undolist.size();
            modified = false;
            tmpsavesuccess = true;
            sys->FileUsed(filename, this);
            if (::wxFileExists(treesheets::System::TmpName(filename))) {
                ::wxRemoveFile(treesheets::System::TmpName(filename));
            }
        }
        if (autohtmlexport != 0) {
            ExportFile(treesheets::System::ExtName(filename, ".html"),
                       autohtmlexport == A_AUTOEXPORT_HTML_WITH_IMAGES - A_AUTOEXPORT_HTML_NONE
                           ? A_EXPHTMLTE
                           : A_EXPHTMLT,
                       false);
        }
        #ifdef ENABLE_WXPDFDOC
            if (autopdfexport) {
                ExportFile(treesheets::System::ExtName(filename, ".pdf"), A_EXPPDF, false);
            }
        #endif
        UpdateFileName(page);
        if (success != nullptr) { *success = true; }
        return wxString::Format(_("Saved %s successfully (in %lld milliseconds)."), filename,
                                end_saving_time - start_saving_time);
    }

    template<typename DC> void DrawSelect(DC &dc, Selection &s, bool primary = true) {
        if (s.grid == nullptr) { return; }
        ResetFont(dc);
        s.grid->DrawSelect(this, dc, s, primary);
    }

    template<typename DC> void UpdateHover(DC &dc, int mx, int my) {
        ResetFont(dc);
        int x = 0;
        int y = 0;
        canvas->CalcUnscrolledPosition(mx, my, &x, &y);
        prev = hover;
        hover = Selection();
        auto prevfoldicon = hoverfoldicon;
        auto prevnote = hovernote;
        hoverfoldicon = hovernote = hoverlink = wxRect();
        auto px = x / currentviewscale - centerx / currentviewscale;
        auto py = y / currentviewscale - centery / currentviewscale;
        auto *drawroot = WalkPath(drawpath);
        if (drawroot->grid) {
            drawroot->grid->FindXY(this, px - hierarchysize, py - hierarchysize, dc);
        }
        auto knob = false;
        if (selected.grid != nullptr && !selected.Thin()) {
            auto g = selected.grid->GetRect(this, selected);
            knob = wxRect(g.x + g.width - 4, g.y + g.height - 4, 9, 9).Contains(px, py);
        }
        hoverknob = knob;
        // Not while dragging a selection over it, only when it can be grabbed.
        auto ms = wxGetMouseState();
        SetHoverCursor(knobdrag || (knob && !ms.LeftIsDown() && !ms.RightIsDown())
                           ? wxCURSOR_SIZENWSE
                       : !hoverlink.IsEmpty() && ms.CmdDown() ? wxCURSOR_HAND
                                                              : wxCURSOR_NONE);
        wxRect shade;
        if (sys->hovershadow && hover.grid != nullptr && !hover.Thin()) {
            shade = hover.grid->GetRect(this, hover);
        }
        SetHoverShade(shade);
        auto foldicon = hoverfoldicon;  // as FindXY() found them
        auto note = hovernote;
        hoverfoldicon = prevfoldicon;
        hovernote = prevnote;
        SetHighlight(hoverfoldicon, foldicon);
        SetHoverNote(note);
    }

    void SetHoverCursor(wxStockCursor c) {
        if (c == hovercursor) { return; }
        hovercursor = c;
        canvas->SetCursor(c != wxCURSOR_NONE    ? wxCursor(c)
                          : selected.TextEdit() ? wxCursor(wxCURSOR_IBEAM)
                                                : wxNullCursor);
    }

    // Hovers what's under the mouse pointer, after the cells moved under it.
    void HoverUnderPointer() {
        auto p = canvas->ScreenToClient(wxGetMousePosition());
        if (!canvas->GetClientRect().Contains(p)) { return; }
        wxInfoDC dc(canvas);
        UpdateHover(dc, p.x, p.y);
    }

    // Moves the hover shadow, repainting just the cells it leaves and enters.
    // Moves a highlighted area to r, repainting just the old and the new one.
    void SetHighlight(wxRect &area, const wxRect &r) {
        if (r == area) { return; }
        if (!area.IsEmpty()) { RefreshDocRect(area); }
        if (!r.IsEmpty()) { RefreshDocRect(r); }
        area = r;
    }

    void SetHoverShade(const wxRect &shade) { SetHighlight(hovershade, shade); }

    // Highlights the note triangle at r, of the hovered cell, and shows its note as tooltip.
    void SetHoverNote(const wxRect &r) {
        if (r == hovernote) { return; }
        if (r.IsEmpty()) {
            canvas->UnsetToolTip();
        } else {
            canvas->SetToolTip(hover.GetCell()->note);
        }
        SetHighlight(hovernote, r);
    }

    // Draws color translucently over r: the hover shadow, whose default gray darkens light and
    // lightens dark cell colors, and the highlight of the fold icon under the pointer.
    template<typename DC> void DrawShade(DC &dc, const wxRect &r, const wxColour &color) {
        if (auto *gc = dc.GetGraphicsContext(); gc != nullptr) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(color));
            gc->DrawRectangle(r.x, r.y, r.width, r.height);
            return;
        }
        #ifdef __WXMSW__
            // A GDI DC can't blend. Draw with GDI+ on its HDC instead, in device coordinates:
            // wx keeps the DC's origin and scale to itself rather than in the HDC.
            if (unique_ptr<wxGraphicsContext> gc(
                    wxGraphicsContext::CreateFromNativeHDC(dc.GetHDC()));
                gc) {
                auto x0 = dc.LogicalToDeviceX(r.x);
                auto y0 = dc.LogicalToDeviceY(r.y);
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(color));
                gc->DrawRectangle(x0, y0, dc.LogicalToDeviceX(r.x + r.width) - x0,
                                  dc.LogicalToDeviceY(r.y + r.height) - y0);
                return;
            }
        #endif
        // No alpha at all: XOR a dark gray, like TreeSheets used to.
        dc.SetLogicalFunction(wxXOR);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(0x14, 0x10, 0x10)));
        dc.DrawRectangle(r);
        dc.SetLogicalFunction(wxCOPY);
    }

    void ScrollIfSelectionOutOfView() {
        // required, since sizes of things may have been reset by the last editing operation
        int cw = 0;
        int ch = 0;
        canvas->GetClientSize(&cw, &ch);
        int sx = 0;
        int sy = 0;
        canvas->GetViewStart(&sx, &sy);
        if ((anchored || layoutys * currentviewscale > ch || layoutxs * currentviewscale > cw) &&
            selected.grid != nullptr) {
            wxRect r = selected.grid->GetRect(this, selected);
            // A selection larger than the view can't be shown whole. Show the corner that
            // shift+cursor keys extend it at instead, so the view follows its growing edge.
            if (!selected.Thin() && (r.width > cw || r.height > ch)) {
                Selection lead(selected.grid,
                               selected.firstdx > 0 ? selected.x + selected.xs - 1 : selected.x,
                               selected.firstdy > 0 ? selected.y + selected.ys - 1 : selected.y,
                               1, 1);
                wxRect lr = selected.grid->GetRect(this, lead);
                if (r.width > cw) {
                    r.x = lr.x;
                    r.width = lr.width;
                }
                if (r.height > ch) {
                    r.y = lr.y;
                    r.height = lr.height;
                }
            }
            if (anchored) {
                // The view starts at the scroll position minus the anchor offset.
                int vx = sx - anchorx;
                int vy = sy - anchory;
                ScrollBy(ScrollPosToShow(r.x, r.width, vx, cw) - vx,
                         ScrollPosToShow(r.y, r.height, vy, ch) - vy);
            } else {
                canvas->Scroll(ScrollPosToShow(r.x, r.width, sx, cw),
                               ScrollPosToShow(r.y, r.height, sy, ch));
            }
        }
    }

    void ResetAnchor() {
        if (!anchored) { return; }
        anchored = false;
        anchorx = anchory = 0;
        canvas->Refresh();
    }

    void SetAnchor(int x, int y) {
        anchored = true;
        // Also takes effect before the next paint recomputes the offset, e.g. in UpdateHover().
        centerx = anchorx = x;
        centery = anchory = y;
    }

    // The largest scroll positions: the scrollbars only cover the document.
    wxSize MaxScroll() const {
        int cw = 0;
        int ch = 0;
        canvas->GetClientSize(&cw, &ch);
        return {max(0, static_cast<int>(layoutxs * currentviewscale) - cw),
                max(0, static_cast<int>(layoutys * currentviewscale) - ch)};
    }

    // Moves the view along one axis from scroll position s and anchor offset o by d pixels.
    // The offset only shrinks: it takes up what the scrollbar can't, towards the document,
    // but never goes further beyond its edges.
    static void ScrollAxis(int d, int &s, int &o, int smax) {
        int v = s - o + d;
        int no = std::clamp(v, 0, smax) - v;
        o = o >= 0 ? std::clamp(no, 0, o) : std::clamp(no, o, 0);
        s = std::clamp(v + o, 0, smax);
    }

    // Scrolls the view by (dx, dy) pixels, using up the anchor offset first.
    void ScrollBy(int dx, int dy) {
        int sx = 0;
        int sy = 0;
        canvas->GetViewStart(&sx, &sy);
        if (!anchored) {
            canvas->Scroll(sx + dx, sy + dy);
            return;
        }
        auto smax = MaxScroll();
        int ox = anchorx;
        int oy = anchory;
        ScrollAxis(dx, sx, ox, smax.x);
        ScrollAxis(dy, sy, oy, smax.y);
        canvas->Scroll(sx, sy);
        if (ox != anchorx || oy != anchory) {
            SetAnchor(ox, oy);
            canvas->Refresh();
        }
    }

    // The area of a cell in document coordinates.
    wxRect CellRect(Cell *c) { return {c->GetX(this), c->GetY(this), c->sx, c->sy}; }

    // Zooms like Zoom() on the cell under the pointer at p (in window coordinates), and
    // scrolls such that the same point of that cell stays under the pointer. Where that
    // would take scrolling beyond the edges of the document, it is moved by the anchor
    // offset, and not centered.
    void ZoomAtPointer(int dir, wxPoint p) {
        {
            wxInfoDC dc(canvas);
            UpdateHover(dc, p.x, p.y);
        }
        if (hover.grid == nullptr) {
            Zoom(dir);
            return;
        }
        auto *c = hover.Thin() ? hover.grid->cell : hover.GetCell();
        int sx = 0;
        int sy = 0;
        canvas->GetViewStart(&sx, &sy);
        auto before = CellRect(c);
        // The pointer in document coordinates, as a fraction of the cell's size.
        double docx = (p.x + sx - centerx) / currentviewscale;
        double docy = (p.y + sy - centery) / currentviewscale;
        double fx = before.width > 0 ? (docx - before.x) / before.width : 0.0;
        double fy = before.height > 0 ? (docy - before.y) / before.height : 0.0;
        SetSelect(hover);
        // Hovered below, once the view is where it stays.
        if (!Zoom(dir, false, false)) { return; }
        auto after = CellRect(c);
        // Where the view has to start for that point to be under the pointer again.
        int vx = lround((after.x + fx * after.width) * currentviewscale) - p.x;
        int vy = lround((after.y + fy * after.height) * currentviewscale) - p.y;
        auto smax = MaxScroll();
        canvas->Scroll(std::clamp(vx, 0, smax.x), std::clamp(vy, 0, smax.y));
        // wx may clamp the scroll position differently, so take the offset from where it went.
        canvas->GetViewStart(&sx, &sy);
        SetAnchor(sx - vx, sy - vy);
        canvas->Refresh();
        wxInfoDC dc(canvas);
        UpdateHover(dc, p.x, p.y);
    }

    // The scroll position along one axis that brings [pos, pos + size) into a view of
    // viewsize starting at start, scrolling as little as possible.
    static int ScrollPosToShow(int pos, int size, int start, int viewsize) {
        if (size > viewsize || pos < start) { return pos; }
        if (pos + size > start + viewsize) { return pos + size - viewsize; }
        return start;
    }

    // Converts a rectangle from document (unscrolled content) coordinates -- the space
    // Cell::ox/oy and Grid::GetRect() use -- into window coordinates, and refreshes just
    // that area instead of the whole visible viewport. Centering is just a constant
    // (centerx, centery) pixel offset on top of the scroll position (see ShiftToCenter),
    // so it's cheap to replicate here. The scaled-viewing-mode zoom instead scales the
    // rect itself, which is more involved for a mode that's rarer and has less on
    // screen anyway, so that one still falls back to a full refresh.
    void RefreshDocRect(wxRect r) {
        if (currentviewscale != 1.0) {
            canvas->Refresh();
            return;
        }
        r.Inflate(4, 4);  // slack for borders, the cursor caret, antialiasing
        int devx = 0, devy = 0;
        canvas->CalcScrolledPosition(r.x, r.y, &devx, &devy);
        canvas->RefreshRect(wxRect(devx + centerx, devy + centery, r.width, r.height), false);
    }

    // Where DrawSelect() draws s. The line of a thin selection runs along its whole grid.
    wxRect SelectRect(const Selection &s) {
        if (s.grid == nullptr) { return {}; }
        auto r = s.grid->GetRect(this, s);
        if (s.Thin()) {
            auto *c = s.grid->cell;
            if (s.xs == 0) {
                r.y = c->GetY(this);
                r.height = c->sy;
            } else {
                r.x = c->GetX(this);
                r.width = c->sx;
            }
            r.Inflate(g_line_width + g_cell_margin);
        }
        return r;
    }

    // Taken before a change of the selection, for RefreshSelect() after it.
    struct SelectBefore {
        wxRect rect;
        int layouts;
        wxPoint view;
    };

    SelectBefore BeforeSelect() {
        // Cells waiting to be laid out may change their place, as may a deferred layout.
        auto *drawroot = WalkPath(drawpath);
        bool current = !layoutpending && drawroot == currentdrawroot && drawroot->sx != 0;
        return {current ? SelectRect(selected) : wxRect(), current ? layouts : -1,
                canvas->GetViewStart()};
    }

    // Repaints just the old and the new selection if nothing else changed since BeforeSelect():
    // no layout and no scrolling. All that depends on the selection is drawn by DrawSelect().
    void RefreshSelect(const SelectBefore &before) {
        if (before.layouts != layouts || before.view != canvas->GetViewStart() ||
            currentdrawroot->sx == 0) {
            canvas->Refresh();
            return;
        }
        if (!before.rect.IsEmpty()) { RefreshDocRect(before.rect); }
        if (auto r = SelectRect(selected); !r.IsEmpty()) { RefreshDocRect(r); }
    }

    // Grid::Layout() has to revisit every cell in a grid on every call, even when only
    // one cell actually changed, because that's the only way it can tell whether the
    // edited cell's column/row is still governed by some other (unchanged) cell's size.
    // For a single text edit we can usually answer that in O(1) instead: if the edited
    // cell's new size still fits within its column/row's cached max (Grid::colmaxcache/
    // rowmaxcache, kept current by the last full Layout()), then by construction no
    // other cell's position could have shifted, and the whole document-wide relayout
    // (UpdateLayout(), which Grid::Layout() is called from) can be skipped entirely.
    // Unless the cell shrank and was the one setting that max: then the column/row may
    // shrink with it.
    // `oldsizes` is the (cell, old geometry) chain AddUndo()'s ResetLayout() zeroed out
    // -- via Cell::Reset(), which clears sx/sy/ox/oy/minx/miny/ycenteroff -- for
    // `editedsel`'s cell and its ancestors up to currentdrawroot (see Key()); since
    // nothing changed, restoring those exact values undoes that reset correctly. The
    // edited cell itself (oldsizes[0]) is the exception: its sx/sy/ox/oy/ycenteroff are
    // still restored (they equal what they were before -- the column/row max didn't
    // change, which is what "fits in the cache" below means), but its minx/miny must be
    // left alone, since LazyLayout() just correctly recomputed them for the new text;
    // restoring the old ones would silently revert the edit the next time this cell's
    // cached (non-zero sx) branch of LazyLayout() is taken.
    // Returns false (having changed nothing but the edited cell's own now-current
    // natural size and cache, harmlessly pre-computed for whichever path ends up doing
    // the full layout) whenever the fast path doesn't apply, so the caller can fall
    // back to the normal UpdateLayout().
    bool FastRelayoutAfterEdit(const Selection &editedsel,
                               const vector<pair<Cell *, CellGeom>> &oldsizes) {
        Grid *g = editedsel.grid.get();
        if (g->cell->tiny) { return false; }
        if (g->colmaxcache.size() != static_cast<size_t>(g->xs) ||
            g->rowmaxcache.size() != static_cast<size_t>(g->ys)) {
            return false;
        }
        wxInfoDC dc(canvas);
        ResetFont(dc);
        dc.SetUserScale(1, 1);
        Cell *c = g->C(editedsel.x, editedsel.y).get();
        int celldepth = c->Depth() - drawpath.size();
        c->LazyLayout(this, dc, celldepth, g->colwidths[editedsel.x], false);
        auto &prev = oldsizes[0].second;
        auto colmax = g->colmaxcache[editedsel.x], rowmax = g->rowmaxcache[editedsel.y];
        if (c->sx > colmax || c->sy > rowmax || (c->sx < prev.minx && prev.minx >= colmax) ||
            (c->sy < prev.miny && prev.miny >= rowmax)) {
            return false;
        }
        for (size_t i = 0; i < oldsizes.size(); i++) {
            auto &[p, old] = oldsizes[i];
            p->sx = old.sx;
            p->sy = old.sy;
            p->ox = old.ox;
            p->oy = old.oy;
            p->ycenteroff = old.ycenteroff;
            if (i != 0) {
                p->minx = old.minx;
                p->miny = old.miny;
            }
        }
        // LazyLayout() laid out its grid again for its own width, not the column's.
        c->AlignContent(this);
        return true;
    }

    void ScrollOrZoom(bool zoomiftiny = false) {
        if (selected.grid == nullptr) { return; }
        auto *drawroot = WalkPath(drawpath);
        // If the selection is outside of the view, zoom out only as far as needed to show it: to
        // the deepest cell on the view's path that contains it.
        auto *view = drawroot;
        while (view != selected.grid->cell && !view->IsParentOf(selected.grid->cell)) {
            view = view->parent;
        }
        // If we jumped to a cell which may be insided a folded cell, we have to unfold it
        // because the rest of the code doesn't deal with a selection that is invisible :)
        for (auto *cg = selected.grid->cell; cg != view; cg = cg->parent) {
            if (cg->grid->folded) {
                cg->grid->folded = false;
                cg->ResetLayout();  // the cells inside kept their layout while folded
            }
        }
        if (view == drawroot) {
            if (zoomiftiny) { ZoomTiny(); }
            UpdateLayout();
            ScrollIfSelectionOutOfView();
            canvas->Refresh();
        } else {
            Zoom(view->Depth() - static_cast<int>(drawpath.size()));
            if (zoomiftiny) { ZoomTiny(); }
        }
    }

    void ZoomTiny() {
        if (auto *c = selected.GetCell(); c != nullptr && c->tiny) {
            Zoom(1);  // seems to leave selection box in a weird location?
            if (selected.GetCell() != c) { ZoomTiny(); }
        }
    }

    void ResetCursor() {
        if (selected.grid != nullptr) { selected.SetCursorEdit(this, selected.TextEdit()); }
    }

    void SetSelect(const Selection &sel = Selection()) {
        selected = sel;
        begindrag = sel;
    }

    void SelectUp() {
        if (isctrlshiftdrag == 1 && begindrag.EqLoc(selected)) {
            CtrlClick();
            return;
        }
        if (isctrlshiftdrag == 0 || isctrlshiftdrag == 3 || begindrag.EqLoc(selected)) { return; }
        ClearMultiSel();
        auto *cell = selected.GetCell();
        if (cell == nullptr) { return; }
        int pastemode = begindrag.PasteMode();
        auto *targetcell = begindrag.ThinExpand(this);
        selected = begindrag;
        if (targetcell != nullptr) {
            auto is_parent = targetcell->IsParentOf(cell);
            auto *targetcell_parent = targetcell->parent;  // targetcell may be deleted.
            targetcell->Paste(this, cell, begindrag, pastemode, cell->ColWidth());
            // If is_parent, cell has been deleted already.
            if (isctrlshiftdrag == 1 && !is_parent) {
                cell->parent->AddUndo(this);
                Selection cellselection = cell->parent->grid->FindCell(cell);
                cell->parent->grid->MultiCellDeleteSub(this, cellselection);
            }
            // Moving the cell out can leave the grid it was dropped into empty, which deletes it
            // together with targetcell, so select the cell that held the grid instead.
            if (targetcell_parent != nullptr && !targetcell_parent->grid) {
                targetcell = targetcell_parent;
                targetcell_parent = targetcell->parent;
            }
            hover = targetcell_parent != nullptr ? targetcell_parent->grid->FindCell(targetcell)
                                                 : Selection();
            SetSelect(hover);
            wxInfoDC dc(canvas);
            Layout(dc);
        }
    }

    // A ctrl+click without a drag adds the cell to the selection, or takes it out again.
    void CtrlClick() {
        auto *c = selected.GetCell();
        auto prev = ctrlclickfrom;
        if (!MultiSelFor(prev) && prev.GetCell() == c) { return; }  // keeps editing it
        if (c == nullptr || prev.grid == nullptr || prev.Thin()) {
            ClearMultiSel();
            return;
        }
        if (selected.TextEdit()) { selected.ExitEdit(this); }
        if (prev.TextEdit()) { prev.ExitEdit(this); }
        auto it = std::ranges::find_if(multisel, [&](auto &s) { return s.GetCell() == c; });
        if (prev.GetCell() == c) {
            selected = multisel.back();
            multisel.pop_back();
        } else if (it != multisel.end()) {
            multisel.erase(it);
            selected = prev;
        } else {
            multisel.push_back(prev);
        }
        begindrag = multiselanchor = selected;
        sys->frame->UpdateStatus(selected, true);
        canvas->Refresh();
    }

    // Whether the cells added with ctrl+click still go with s. Drops them if s has moved on, or
    // one of them is no longer in the document.
    bool MultiSelFor(const Selection &s) {
        if (multisel.empty()) { return false; }
        auto indoc = [&](const Selection &m) {
            if (!m.grid || m.x + m.xs > m.grid->xs || m.y + m.ys > m.grid->ys ||
                m.grid->cell->grid != m.grid) {
                return false;
            }
            for (auto *c = m.grid->cell; c != root.get(); c = c->parent) {
                if (!c->parent || !c->parent->grid || !c->parent->grid->FindCell(c).grid) {
                    return false;
                }
            }
            return true;
        };
        if (s.EqLoc(multiselanchor) && indoc(s) && std::ranges::all_of(multisel, indoc)) {
            return true;
        }
        ClearMultiSel();
        return false;
    }

    bool MultiSel() { return MultiSelFor(selected); }

    void ClearMultiSel() {
        if (multisel.empty()) { return; }
        multisel.clear();
        canvas->Refresh();
    }

    // Calls f with each cell added with ctrl+click as the selection, and then with the selection
    // itself, so that one undo reverts what they all did.
    template<typename F> void ForEachSelection(F f) {
        auto primary = selected;
        auto extra = MultiSel() ? std::move(multisel) : vector<Selection>();
        multisel.clear();
        undogroup = 1;
        for (auto &s : extra) {
            selected = s;
            f();
        }
        selected = primary;
        f();
        undogroup = 0;
        multisel = std::move(extra);
    }

    // The actions that apply to the cells added with ctrl+click as well; any other drops them.
    wxString MultiSelAction(int action, bool recursive) {
        int style = action == wxID_BOLD            ? STYLE_BOLD
                    : action == wxID_ITALIC        ? STYLE_ITALIC
                    : action == A_TT               ? STYLE_FIXED
                    : action == wxID_UNDERLINE     ? STYLE_UNDERLINE
                    : action == wxID_STRIKETHROUGH ? STYLE_STRIKETHRU
                                                   : 0;
        // As within one selection, a style goes on unless all cells have it already.
        auto all = style != 0 && selected.grid->AllHaveStyle(selected, style, recursive) &&
                   std::ranges::all_of(multisel, [&](auto &s) {
                       return s.grid->AllHaveStyle(s, style, recursive);
                   });
        wxString status;
        ForEachSelection([&] {
            if (style == 0 || all || !selected.grid->AllHaveStyle(selected, style, recursive)) {
                status = Action(action, recursive);
            }
        });
        return status;
    }

    void KeepFoldSel(bool ctrl) {
        foldsel = ctrl && selected.xs * selected.ys > 1 && hover.IsInside(selected) ? selected
                                                                                    : Selection();
    }

    bool AnyUnfoldedInSelection() {
        loopallcellssel(c, false) if (c->grid && !c->grid->folded) return true;
        return false;
    }

    void DoubleClick(bool fold, bool recursive) {
        auto foldselected = [&] {
            Action(!recursive ? A_FOLD : AnyUnfoldedInSelection() ? A_FOLDALL : A_UNFOLDALL);
        };
        if (fold && foldsel.grid != nullptr && hover.IsInside(foldsel)) {
            hover.ExitEdit(this);
            SetSelect(foldsel);
            foldselected();
            return;
        }
        if (fold && hover.grid != nullptr) {
            // Toggles the hovered cell's grid, or else folds the grid the pointer is in.
            Cell *c = hover.GetCell();
            if (c == nullptr || !c->grid) { c = hover.grid->cell; }
            if (c != WalkPath(drawpath)) {
                hover.ExitEdit(this);
                SetSelect(c->parent->grid->FindCell(c));
                foldselected();
                return;
            }
        }
        SetSelect(hover);
        if (selected.Thin() && selected.grid != nullptr) {
            selected.SelAll();
        } else if (Cell *c = selected.GetCell()) {
            selected.EnterEditOnly(this);
            c->text.SelectWord(selected);
            begindrag = selected;
        }
    }

    void Drop() {
        int pastemode = selected.PasteMode();
        switch (dndobjc->GetReceivedFormat().GetType()) {
            case wxDF_BITMAP: PasteOrDrop(*dndobji); break;
            case wxDF_FILENAME: PasteOrDrop(*dndobjf); break;
            case wxDF_TEXT:
            case wxDF_UNICODETEXT: PasteOrDrop(*dndobjt, pastemode);
            default:;
        }
    }

    auto CopyEntireCells(wxString &s, int action) {
        sys->clipboardcopy = s;
        auto html =
            selected.grid->ConvertToText(selected, 0, action == A_COPYWI ? A_EXPHTMLTI : A_EXPHTMLT,
                                         this, false, currentdrawroot, sys->flatcopy);
        return new wxHTMLDataObject(html);
    }

    // Cells without sub-grids are copied as tab separated text, which spreadsheets understand,
    // and as indented text otherwise.
    wxString CopyText() {
        auto tree = selected.TextEdit();
        if (!tree && !sys->flatcopy) {
            loopallcellssel(c, false) tree = tree || c->grid;
        }
        return selected.grid->ConvertToText(selected, 0, tree ? A_EXPTEXT : A_EXPTSV, this, false,
                                            currentdrawroot, sys->flatcopy);
    }

    // Keeps just the selected text of cell c in the cell clipboard, if it has styles or links,
    // which then come along when it is pasted.
    bool CopyTextRange() {
        auto &cc = *sys->cellclipboard;
        auto len = static_cast<int>(cc.text.t.Len());
        auto from = max(selected.cursor, 0);
        auto to = min(selected.cursorend, len);
        if (from >= to || !cc.text.Rich()) { return false; }
        cc.text.RemoveText(to, len - to);
        cc.text.RemoveText(0, from);
        if (!cc.text.Rich()) { return false; }
        cc.celltype = CT_DATA;
        cc.formula = nullptr;
        cc.grid = nullptr;
        cc.text.image = nullptr;
        cc.note.Clear();
        sys->cellclipboardcolwidth = 0;
        return true;
    }

    void CopyToCellClipboard(Cell *c) {
        sys->cellclipboard = c != nullptr ? c->Clone(nullptr) : selected.grid->CloneSel(selected);
        sys->cellclipboardcolwidth = c != nullptr ? c->ColWidth() : 0;
    }

    void Copy(int action) {
        if (selected.grid == nullptr || selected.Thin()) { return; }
        auto *c = selected.GetCell();
        sys->clipboardcopy = wxEmptyString;

        switch (action) {
            case A_DRAGANDDROP: {
                CopyToCellClipboard(c);
                wxDataObjectComposite dragdata;
                if (c != nullptr && !c->text.t && c->text.image != nullptr) {
                    auto *image = c->text.image;
                    if (!image->data.empty()) {
                        auto &it = imagetypes.at(image->type).first;
                        auto bitmap = ConvertBufferToWxBitmap(image->data, it);
                        dragdata.Add(new wxBitmapDataObject(bitmap));
                    }
                } else {
                    auto s = CopyText();
                    dragdata.Add(new wxTextDataObject(s));
                    if (!selected.TextEdit()) {
                        auto *htmlobj = CopyEntireCells(s, wxID_COPY);
                        dragdata.Add(htmlobj);
                    }
                }
                wxDropSource dragsource(dragdata, canvas);
                dragsource.DoDragDrop(wxDrag_CopyOnly);
                break;
            }
            case A_COPYCT: {
                sys->cellclipboard = nullptr;
                auto clipboardtextdata = make_unique<wxDataObjectComposite>();
                wxString s = "";
                loopallcellssel(c, !sys->flatcopy) {
                    if (c->text.t.IsEmpty() || (sys->flatcopy && c->TinyOrFiltered())) { continue; }
                    s += c->text.t + " ";
                }
                if (!selected.TextEdit()) { sys->clipboardcopy = s; }
                clipboardtextdata->Add(new wxTextDataObject(s));
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(clipboardtextdata.release());
                    wxTheClipboard->Close();
                }
                break;
            }
            case wxID_COPY:
            case A_COPYWI:
            default: {
                CopyToCellClipboard(c);
                auto clipboarddata = make_unique<wxDataObjectComposite>();
                auto s = CopyText();
                clipboarddata->Add(new wxTextDataObject(s));
                if (!selected.TextEdit()) {
                    auto *htmlobj = CopyEntireCells(s, action);
                    clipboarddata->Add(htmlobj);
                } else if (CopyTextRange()) {
                    sys->clipboardcopy = s;
                }
                if (c != nullptr && !c->text.t && c->text.image != nullptr) {
                    auto *image = c->text.image;
                    auto &it = imagetypes.at(image->type).first;
                    auto bitmap = ConvertBufferToWxBitmap(image->data, it);
                    auto *bmpobj = new wxBitmapDataObject(bitmap);
                    clipboarddata->Add(bmpobj);
                }
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(clipboarddata.release());
                    wxTheClipboard->Close();
                }
                break;
            }
        }
    }

    bool ZoomSetDrawPath(int dir, bool fromroot = true) {
        int oldlen = drawpath.size();
        int targetlen = max(0, (fromroot ? 0 : oldlen) + dir);
        if (targetlen == 0 && drawpath.empty()) { return false; }
        if (dir > 0) {
            if (selected.grid == nullptr) { return false; }
            auto *c = selected.GetCell();
            CreatePath(c != nullptr && c->grid ? c : selected.grid->cell, drawpath);
        } else if (dir < 0) {
            auto *drawroot = WalkPath(drawpath);
            if (drawroot->grid && drawroot->grid->folded) {
                SetSelect(drawroot->parent->grid->FindCell(drawroot));
            }
        }
        int tail = static_cast<int>(drawpath.size()) - targetlen;
        if (tail > 0) { drawpath.erase(drawpath.begin(), drawpath.begin() + tail); }
        return drawpath.size() != oldlen;
    }

    bool Zoom(int dir, bool fromroot = false, bool hoverpointer = true) {
        if (!ZoomSetDrawPath(dir, fromroot)) { return false; }
        ResetAnchor();
        auto *drawroot = WalkPath(drawpath);
        if (selected.GetCell() == drawroot && drawroot->grid) {
            // We can't have the drawroot selected, so we must move the selection to the children.
            SetSelect(Selection(drawroot->grid, 0, 0, drawroot->grid->xs, drawroot->grid->ys));
        }
        drawroot->ResetLayout();
        drawroot->ResetChildren();
        UpdateLayout();
        ScrollIfSelectionOutOfView();
        canvas->Refresh();
        // The new layout is centered differently, so don't wait for the paint to know where.
        UpdateViewport();
        if (hoverpointer) { HoverUnderPointer(); }
        return true;
    }

    static wxString NoSel() { return _("This operation requires a selection."); }
    // Opens file f, relative to the document, in the application for its type.
    bool OpenFile(const wxString &f) const {
        wxFileName fn(f);
        if (fn.IsRelative()) { fn.MakeAbsolute(wxFileName(filename).GetPath()); }
        return wxLaunchDefaultApplication(fn.GetFullPath());
    }

    // Goes to the cell that internal link k in cell c goes to, or opens the URL or file of an
    // external one.
    wxString FollowLink(Cell *c, const TextLink<wxString> &k) {
        if (k.internal) {
            auto *target = FormulaEvaluator::LinkTarget(c, k.target);
            if (target == nullptr) { return _("No matching cell found!"); }
            SetSelect(target->parent->grid->FindCell(target));
            ScrollOrZoom(true);
            return wxEmptyString;
        }
        auto opened =
            Text::HasURLScheme(k.target) ? wxLaunchDefaultBrowser(k.target) : OpenFile(k.target);
        return opened ? wxString() : _("The link could not be opened.");
    }

    // Where an external link to `target` goes in an export: a URL as is, and a file as a file
    // URL, or else relative to the export, which is usually next to the document.
    static wxString LinkURL(const wxString &target) {
        if (Text::HasURLScheme(target)) { return target; }
        wxFileName fn(target);
        return fn.IsAbsolute() ? wxFileName::FileNameToURL(fn) : fn.GetFullPath(wxPATH_UNIX);
    }

    // Where link k in cell c goes in HTML: its URL, the anchor of its target, or nowhere if that
    // isn't exported.
    wxString LinkHref(const Cell *c, const TextLink<wxString> &k) const {
        if (!k.internal) { return LinkURL(k.target); }
        auto it = htmlanchors.find(FormulaEvaluator::LinkTarget(c, k.target));
        return it != htmlanchors.end() ? wxString::Format("#cell%d", it->second) : wxString();
    }

    // Whether cell c is drawn in the current view, if it fits in the window.
    bool InView(const Cell *c) const {
        for (auto *p = c->parent; p != nullptr; p = p->parent) {
            if (p == currentdrawroot) { return true; }
            if (p->grid->folded) { return false; }
        }
        return false;
    }

    static wxString OneCell() { return _("This operation works on a single selected cell only."); }
    static wxString NoThin() { return _("This operation doesn't work on thin selections."); }
    static wxString NoGrid() { return _("This operation requires a cell that contains a grid."); }

    bool InDeferredBurst() const {
        return layoutpending && !undolist.empty() && undolist.back().get() == pendingundoitem;
    }

    // Like Cell::AddUndo(), without zeroing the cached geometry. The steps of a burst are undone
    // together, and so are consecutive column width changes.
    void AddResizeUndo(Cell *c, Selection &scope, bool recursive, bool width) {
        bool merge = !undolist.empty() && undolist.back().get() == pendingundoitem &&
                     pendingundocell == c && undolist.size() != undolistsizeatfullsave &&
                     (layoutpending || width && pendingundowidth);
        if (merge && scope.IsInside(pendingundoscope) && (pendingundorecursive || !recursive)) {
            return;
        }
        AddUndo(c, !merge, false, &scope, recursive);
        pendingundocell = c;
        pendingundoitem = undolist.back().get();
        pendingundoscope = scope;
        pendingundorecursive = recursive;
        pendingundowidth = width;
    }

    // Adds sel to the cells the deferred burst has to lay out again, or makes that all of them for
    // cells of another grid.
    void AddPendingCells(const Selection &sel, bool hierarchical) {
        if (!layoutpending) {
            pendingsel = sel;
            pendinghierarchical = hierarchical;
        } else if (pendingsel.grid != sel.grid) {
            pendingsel = Selection();
        } else if (pendingsel.grid) {
            auto &p = pendingsel;
            int x1 = max(p.x + p.xs, sel.x + sel.xs);
            int y1 = max(p.y + p.ys, sel.y + sel.ys);
            p.x = min(p.x, sel.x);
            p.y = min(p.y, sel.y);
            p.xs = x1 - p.x;
            p.ys = y1 - p.y;
            pendinghierarchical |= hierarchical;
        }
    }

    // Performs the relayout and redraw that Wheel(..., deferlayout = true) postponed.
    void FlushPendingLayout() {
        if (!layoutpending) { return; }
        UpdateLayout();  // also clears layoutpending and resets the stale geometry
        ScrollIfSelectionOutOfView();
        canvas->Refresh();
        sys->frame->UpdateStatus(selected, false);
    }

    // `at` is where a zoom gesture happened, in window coordinates. Otherwise the zoom follows the
    // mouse pointer.
    wxString Wheel(int dir, bool alt, bool ctrl, bool shift, bool hierarchical = true,
                   bool deferlayout = false, optional<wxPoint> at = nullopt) {
        if (dir == 0) { return wxEmptyString; }
        if (alt) {
            if (selected.grid == nullptr) { return NoSel(); }
            if (selected.xs > 0) {
                // The cells of the columns, and if hierarchical those inside them, get a width.
                Selection cols(selected.grid, selected.x, 0, selected.xs, selected.grid->ys);
                AddResizeUndo(selected.grid->cell, cols, hierarchical, true);
                selected.grid->ResizeColWidths(dir, selected, hierarchical);
                if (deferlayout) {
                    AddPendingCells(cols, hierarchical);
                    layoutpending = true;
                } else {
                    selected.grid->cell->ResetLayout();
                    selected.grid->ResetResized(cols, hierarchical);
                    UpdateLayout();
                    ScrollIfSelectionOutOfView();
                    canvas->Refresh();
                }
                sys->frame->UpdateStatus(selected, false);
                return dir > 0 ? _("Column width increased.") : _("Column width decreased.");
            }
            return _("nothing to resize");
        } else if (shift) {
            if (selected.grid == nullptr) { return NoSel(); }
            if (deferlayout) {
                AddResizeUndo(selected.grid->cell, selected, hierarchical, false);
                selected.grid->RelSize(-dir, selected, pathscalebias, hierarchical);
                AddPendingCells(selected, hierarchical);
                layoutpending = true;
            } else {
                selected.grid->cell->AddUndo(this, selected, hierarchical);
                selected.grid->RelSize(-dir, selected, pathscalebias, hierarchical);
                selected.grid->ResetResized(selected, hierarchical);
                UpdateLayout();
                ScrollIfSelectionOutOfView();
                canvas->Refresh();
            }
            sys->frame->UpdateStatus(selected, false);
            return dir > 0 ? _("Text size increased.") : _("Text size decreased.");
        } else if (ctrl) {
            int steps = abs(dir);
            dir = sign(dir);
            // With hover zoom, the wheel zooms at the pointer, if it's over the canvas.
            auto pointer = at.value_or(canvas->ScreenToClient(wxGetMousePosition()));
            bool atpointer = sys->hoverzoom && canvas->GetClientRect().Contains(pointer);
            loop(i, steps) {
                if (atpointer) {
                    ZoomAtPointer(dir, pointer);
                } else {
                    if (sys->hoverzoom && hover.grid != nullptr) { SetSelect(hover); }
                    Zoom(dir);
                }
            }
            return dir > 0 ? _("Zoomed in.") : _("Zoomed out.");
        } else {
            ASSERT(0);
            return wxEmptyString;
        }
    }

    template<typename DC> void Layout(DC &dc) {
        ResetFont(dc);
        dc.SetUserScale(1, 1);
        currentdrawroot = WalkPath(drawpath);
        int psb = currentdrawroot == root.get() ? 0 : currentdrawroot->MinRelsize();
        if (psb < 0 || psb == INT_MAX) { psb = 0; }
        if (psb != pathscalebias) { currentdrawroot->ResetChildren(); }
        pathscalebias = psb;
        if (currentdrawroot->sx == 0) { layouts++; }
        currentdrawroot->LazyLayout(this, dc, 0, currentdrawroot->ColWidth(), false);
        currentdrawroot->AlignContent(this);
        ResetFont(dc);
        PickFont(dc, 0, 0, 0);
        hierarchysize = 0;
        for (Cell *p = currentdrawroot->parent; p != nullptr; p = p->parent) {
            if (!p->text.t.IsEmpty()) { hierarchysize += dc.GetCharHeight(); }
        }
        hierarchysize += fgutter;
        layoutxs = currentdrawroot->sx + hierarchysize + fgutter;
        layoutys = currentdrawroot->sy + hierarchysize + fgutter;
    }

    template<typename DC> void ShiftToCenter(DC &dc) const {
        int dlx = dc.DeviceToLogicalX(0);
        int dly = dc.DeviceToLogicalY(0);
        dc.SetDeviceOrigin((dlx > 0 ? -dlx : 0) + centerx, (dly > 0 ? -dly : 0) + centery);
    }

    template<typename DC> void Render(DC &dc) {
        ResetFont(dc);
        PickFont(dc, 0, 0, 0);
        dc.SetTextForeground(*wxLIGHT_GREY);
        int i = 0;
        for (auto *p = currentdrawroot->parent; p != nullptr; p = p->parent) {
            if (!p->text.t.IsEmpty()) {
                int off = hierarchysize - dc.GetCharHeight() * ++i;
                auto s = p->text.t;
                if (static_cast<int>(s.Len()) > sys->defaultmaxcolwidth) {
                    // should take the width of these into account for layoutys, but really, the
                    // worst that can happen on a thin window is that its rendering gets cut off
                    s = s.Left(sys->defaultmaxcolwidth) + "...";
                }
                DrawText(dc, s, off, off);
            }
        }
        dc.SetTextForeground(sys->rubberbandcolor);
        currentdrawroot->Render(this, hierarchysize, hierarchysize, dc, 0, 0, 0, 0, 0,
                                currentdrawroot->ColWidth(), 0);
        ClearUnusedBitmaps();
    }

    void ClearUnusedBitmaps() {
        for (const auto &image : sys->imagelist) {
            if (!image->displayed) image->ClearBitmap();
            image->displayed = false;
        }
    }

    void SelectClick(bool right = false, bool recursive = false) {
        if (!right && hoverknob && isctrlshiftdrag == 0) {
            // Dragging the knob moves the bottom-right corner, keeping the top-left one.
            begindrag = Selection(selected.grid, selected.x, selected.y, 1, 1);
            knobdrag = true;
            SetHoverCursor(wxCURSOR_SIZENWSE);
            return;
        }
        if (!right && isctrlshiftdrag == 1 && !hoverlink.IsEmpty()) {
            // Ctrl+click follows a link, after the click, as the browser or file would take the
            // mouse button release.
            isctrlshiftdrag = 0;
            auto *c = hover.GetCell();
            auto start = hoverlinkstart;
            canvas->CallAfter([this, c, start] {
                if (auto *k = c->text.links.At(start)) { sys->frame->SetStatus(FollowLink(c, *k)); }
            });
            return;
        }
        begindrag = Selection();
        if (isctrlshiftdrag == 1 && !right) {
            ctrlclickfrom = selected;
        } else if (!right) {
            ClearMultiSel();
        }
        KeepFoldSel(isctrlshiftdrag == 1);
        if (!right && !hovernote.IsEmpty() && isctrlshiftdrag == 0) {
            // After the click, as the dialog would take the mouse button release.
            hover.ExitEdit(this);
            SetSelect(hover);
            canvas->CallAfter([this] { sys->frame->SetStatus(Action(A_EDITNOTE)); });
            return;
        }
        if (!right && !hoverfoldicon.IsEmpty() && isctrlshiftdrag == 0) {
            // Unfolds for as long as the button is held, see EndFoldPeek().
            auto *c = hover.GetCell();
            if (recursive) {
                CollectCells(c);
            } else {
                itercells = {c};
            }
            for (auto *d : itercells) {
                if (d->grid && d->grid->folded) {
                    d->grid->folded = false;
                    d->ResetLayout();  // the cells inside kept their layout while folded
                    foldpeek.push_back(d->grid);
                }
            }
            foldpeekstart = wxGetLocalTimeMillis();
            hover.ExitEdit(this);
            SetSelect(hover);
            UpdateLayout();
            return;
        }
        if (!(right && hover.IsInside(selected))) {
            if (hover.GetCell() != nullptr && selected.GetCell() == hover.GetCell()) {
                hover.EnterEditOnly(this);
            } else {
                hover.ExitEdit(this);
            }
            SetSelect(hover);
        }
    }

    // A click on a fold icon keeps the grids it unfolded unfolded, undoably. A long press, or
    // anything else happening while the button is held, folds them again.
    void EndFoldPeek(bool released = false) {
        if (foldpeek.empty()) { return; }
        auto peek = std::move(foldpeek);
        foldpeek.clear();
        for (auto &g : peek) {
            g->folded = true;
            g->cell->ResetLayout();
        }
        auto *c = peek[0]->cell;
        if (released && wxGetLocalTimeMillis() - foldpeekstart < 500) {
            c->AddUndo(this);
            for (auto &g : peek) { g->folded = false; }
        } else if (auto *sc = selected.grid ? selected.grid->cell : nullptr;
                   sc && c->parent && (sc == c || c->IsParentOf(sc))) {
            SetSelect(c->parent->grid->FindCell(c));
        }
        UpdateLayout();
        canvas->Refresh();
    }

    void UpdateLayout() {
        if (!root) return;
        // Cells may move: drop the shadow and highlight until the pointer moves again.
        SetHoverShade(wxRect());
        SetHighlight(hoverfoldicon, wxRect());
        SetHoverNote(wxRect());
        if (layoutpending) {
            // Deferred Wheel() changes left the cached geometry stale (deliberately not reset):
            // just that of the changed cells, if nothing else changed the document since.
            if (pendingsel.grid && InDeferredBurst()) {
                pendingundocell->ResetLayout();
                pendingsel.grid->ResetResized(pendingsel, pendinghierarchical);
            } else {
                root->ResetChildren();
            }
            layoutpending = false;
            pendingsel = pendingundoscope = Selection();
        }
        {
            wxInfoDC dc(canvas);
            Layout(dc);
        }
        if (layoutxs <= 0 || layoutys <= 0) return;
        int clientx = 0;
        int clienty = 0;
        canvas->GetClientSize(&clientx, &clienty);
        #ifdef __WXGTK__
            // Needed for the scrollbar workaround at the end
            int gtkscrollx = 0;
            int gtkscrolly = 0;
            canvas->GetViewStart(&gtkscrollx, &gtkscrolly);
        #endif

        double xscale = clientx / static_cast<double>(layoutxs);
        double yscale = clienty / static_cast<double>(layoutys);
        currentviewscale = scaledviewingmode 
                            ? std::clamp(std::min(xscale, yscale), 1.0, 5.0)
                            : 1.0;

        canvas->SetVirtualSize(layoutxs * currentviewscale, layoutys * currentviewscale);
        #ifdef __WXGTK__
            // When scrollbar was active before and with the set virtual size
            // is not needed anymore, a stale scrollbar artifact is left on wxGTK,
            // allowing weird scrolling. Manually reset the scrollbar to workaround
            // this bug.
        canvas->SetScrollbars(
            layoutxs < clientx ? 0 : 1, layoutys < clienty ? 0 : 1,
            layoutxs < clientx ? 0 : layoutxs, layoutys < clienty ? 0 : layoutys,
            layoutxs < clientx ? 0 : gtkscrollx - max(0, gtkscrollx + clientx - layoutxs),
            layoutys < clienty ? 0 : gtkscrolly - max(0, gtkscrolly + clienty - layoutys));
        #endif
    }

    // The part of the document Draw() shows, scrollx/scrolly to maxx/maxy, and the offset it
    // shifts it by: the anchor, or what centers the document in the window when it is smaller.
    void UpdateViewport() {
        int clientx = 0;
        int clienty = 0;
        canvas->GetClientSize(&clientx, &clienty);
        if (currentviewscale > 1.0) {
            scrollx = scrolly = 0;
            maxx = clientx / currentviewscale;
            maxy = clienty / currentviewscale;
        } else {
            canvas->GetViewStart(&scrollx, &scrolly);
            maxx = clientx + scrollx;
            maxy = clienty + scrolly;
        }
        if (anchored) {
            centerx = anchorx;
            centery = anchory;
            // Shifted towards the top left, the document shows more of itself at the bottom
            // right. Grid::Render() culls cells against maxx/maxy.
            maxx -= min(0, centerx) / currentviewscale;
            maxy -= min(0, centery) / currentviewscale;
        } else {
            centerx = sys->centered && scrollx == 0 && maxx > layoutxs
                          ? (maxx - layoutxs) / 2 * currentviewscale
                          : 0;
            centery = sys->centered && scrolly == 0 && maxy > layoutys
                          ? (maxy - layoutys) / 2 * currentviewscale
                          : 0;
        }
    }

    template<typename DC> void Draw(DC &dc) {
        if (!root) return;
        if (layoutxs <= 0 || layoutys <= 0) return;
        if (currentviewscale <= 1.0) { canvas->PrepareDC(dc); }
        int oldcenterx = centerx;
        int oldcentery = centery;
        UpdateViewport();
        // The centering offset can change without a full repaint. What is already on screen
        // was then drawn at the old offset, and repainting just part of it (the hover shadow,
        // a partial expose) would leave that part shifted against the rest. E.g. wxGTK 3.3
        // changes the client size by the width of an overlay scrollbar without a size event
        // (see TSCanvas::DoGetClientSize).
        if ((centerx != oldcenterx || centery != oldcentery) &&
            !canvas->GetUpdateRegion().GetBox().Contains(canvas->GetClientRect())) {
            canvas->Refresh();
        }

        // Restrict actual drawing to the area wx says needs repainting (which reflects
        // both an explicit RefreshRect() and a real, WM-driven partial expose), instead
        // of always redoing the entire visible viewport. Grid::Render already culls
        // cells against scrollx/scrolly/maxx/maxy, so narrowing those means cells
        // outside the dirty area skip their (Pango-backed, and measured to be the
        // dominant per-paint cost) DrawText call entirely, rather than being drawn and
        // then clipped away. This is always safe: callers that still invalidate the
        // whole window (the vast majority today) get an update region that already
        // covers the full viewport, so nothing narrows and behaviour is unchanged.
        // The clipping region is set before ShiftToCenter(), in window coordinates plus the
        // scroll position, while Render() culls in document coordinates, which are offset by
        // the centering on top. Scaled, maxx/maxy are in document units, and the whole viewport
        // is redrawn anyway.
        if (currentviewscale == 1.0) {
            wxRect clip(scrollx, scrolly, maxx - scrollx, maxy - scrolly);
            wxRect updatebox = canvas->GetUpdateRegion().GetBox();
            if (!updatebox.IsEmpty()) {
                int ux0 = 0, uy0 = 0, ux1 = 0, uy1 = 0;
                canvas->CalcUnscrolledPosition(updatebox.GetLeft(), updatebox.GetTop(), &ux0,
                                                &uy0);
                canvas->CalcUnscrolledPosition(updatebox.GetRight() + 1, updatebox.GetBottom() + 1,
                                                &ux1, &uy1);
                clip.Intersect(wxRect(ux0, uy0, ux1 - ux0, uy1 - uy0));
                scrollx = max(scrollx, ux0 - centerx);
                scrolly = max(scrolly, uy0 - centery);
                maxx = min(maxx, ux1 - centerx);
                maxy = min(maxy, uy1 - centery);
            }
            dc.SetClippingRegion(clip);
        }
        dc.SetBackground(wxBrush(LightColor(Background())));
        dc.Clear();

        ShiftToCenter(dc);
        dc.SetUserScale(currentviewscale, currentviewscale);
        Render(dc);
        if (sys->hovershadow && !hovershade.IsEmpty()) {
            const wxColour base(LightColor(sys->hovershadowcolor));
            DrawShade(dc, hovershade, wxColour(base.Red(), base.Green(), base.Blue(), 56));
        }
        if (!hoverfoldicon.IsEmpty()) {
            const auto base = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
            DrawShade(dc, hoverfoldicon, wxColour(base.Red(), base.Green(), base.Blue(), 96));
        }
        if (!hovernote.IsEmpty()) {
            Cell::DrawNote(dc, hovernote, wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
        }
        DrawSelect(dc, selected);
        // While a ctrl+click is held, the selection is already on the cell under the pointer, so
        // the one it came from is drawn as it will be added on release (unless it's a drag).
        auto ctrlheld = canvas->pressedoncanvas && isctrlshiftdrag == 1;
        if (ctrlheld && selected.EqLoc(begindrag) && !ctrlclickfrom.EqLoc(selected)) {
            DrawSelect(dc, ctrlclickfrom, false);
        }
        if (MultiSelFor(ctrlheld ? ctrlclickfrom : selected)) {
            for (auto &s : multisel) { DrawSelect(dc, s, false); }
        }

        if (currentviewscale != 1.0) { dc.SetUserScale(1.0, 1.0); }
        dc.DestroyClippingRegion();
    }

    void Print(wxDC &dc, wxPrintout &po) {
        // Cell sizes are cached, so they have to be thrown away to lay the document out with the
        // font metrics of the printer instead of those of the screen.
        currentdrawroot->ResetChildren();
        Layout(dc);
        maxx = layoutxs;
        maxy = layoutys;
        scrollx = scrolly = 0;
        po.FitThisSizeToPage(printscale != 0U ? wxSize(printscale, 1) : wxSize(maxx, maxy));
        wxRect fitRect = po.GetLogicalPageRect();
        wxCoord xoff = (fitRect.width - maxx) / 2;
        wxCoord yoff = (fitRect.height - maxy) / 2;
        po.OffsetLogicalOrigin(xoff, yoff);
        while_printing = true;
        Render(dc);
        while_printing = false;
        currentdrawroot->ResetChildren();
        UpdateLayout();
    }

    int TextSize(int depth, int relsize) const {
        if (!sys->shrinktext) { depth = 0; }
        return std::clamp(g_deftextsize - depth - relsize + pathscalebias, g_mintextsize(),
                          g_maxtextsize());
    }

    static bool FontIsMini(int textsize) { return textsize == g_mintextsize(); }

    template<typename DC> bool PickFont(DC &dc, int depth, int relsize, int stylebits) {
        int textsize = TextSize(depth, relsize);
        if (textsize != lasttextsize || stylebits != laststylebits) {
            auto key = make_pair(textsize, stylebits & (STYLE_BOLD | STYLE_ITALIC | STYLE_FIXED |
                                                       STYLE_UNDERLINE | STYLE_STRIKETHRU));
            auto it = fontcache.find(key);
            lastfontcacheheight = nullptr;
            if (usescreenfonts && it != fontcache.end()) {
                dc.SetFont(it->second.first);
                lastfontcacheheight = &it->second.second;
            } else {
                wxFont font(
                    textsize - static_cast<int>(while_printing),
                    (stylebits & STYLE_FIXED) != 0 ? wxFONTFAMILY_TELETYPE : wxFONTFAMILY_DEFAULT,
                    (stylebits & STYLE_ITALIC) != 0 ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
                    (stylebits & STYLE_BOLD) != 0 ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
                    (stylebits & STYLE_UNDERLINE) != 0,
                    (stylebits & STYLE_FIXED) != 0 ? sys->defaultfixedfont : sys->defaultfont);
                if ((stylebits & STYLE_STRIKETHRU) != 0) { font.SetStrikethrough(true); }
                dc.SetFont(font);
                // Retain the resource after wx has adjusted it to the window's DPI.
                if (usescreenfonts) {
                    it = fontcache.emplace(key, make_pair(dc.GetFont(), -1)).first;
                    lastfontcacheheight = &it->second.second;
                }
            }
            lasttextsize = textsize;
            laststylebits = stylebits;
            lastcharheight = lastfontcacheheight ? *lastfontcacheheight : -1;
        }
        return FontIsMini(textsize);
    }

    // Same as dc.GetCharHeight() for the font PickFont() selected, but only measured once per
    // font change: with wxGCDC (GTK, macOS), every call lays out and measures a string.
    template<typename DC> int CharHeight(DC &dc) {
        if (lastcharheight < 0) {
            lastcharheight = dc.GetCharHeight();
            if (lastfontcacheheight) { *lastfontcacheheight = lastcharheight; }
        }
        return lastcharheight;
    }

    void ResetFont() {
        lasttextsize = INT_MAX;
        laststylebits = -1;
        lastcharheight = -1;
        lastfontcacheheight = nullptr;
    }

    template<typename DC> void ResetFont(DC &dc) {
        ResetFont();
        usescreenfonts = dc.GetWindow() == canvas && canvas != nullptr && !while_printing;
        if (!usescreenfonts) { return; }
        auto dpi = canvas->GetDPI();
        auto scale = dc.GetContentScaleFactor();
        if (fontcachedpi != dpi || fontcachescale != scale ||
            fontcacheface != sys->defaultfont || fontcachefixedface != sys->defaultfixedfont ||
            fontcachebasesize != g_deftextsize) {
            fontcache.clear();
            fontcachedpi = dpi;
            fontcachescale = scale;
            fontcacheface = sys->defaultfont;
            fontcachefixedface = sys->defaultfixedfont;
            fontcachebasesize = g_deftextsize;
        }
        // TextSize clamps the size range, and the key has only five style bits.
        // Clearing on a base-size change keeps the cache bounded across zooms.
    }

    bool CheckForChanges() {
        if (modified) {
            ThreeChoiceDialog tcd(sys->frame, filename,
                                  _("Changes have been made, are you sure you wish to continue?"),
                                  _("Save and Close"), _("Discard Changes"), _("Cancel"));
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

    void RemoveTmpFile() const {
        if (!filename.empty() && ::wxFileExists(treesheets::System::TmpName(filename))) {
            ::wxRemoveFile(treesheets::System::TmpName(filename));
        }
    }

    bool CloseDocument() {
        bool keep = CheckForChanges();
        if (!keep) { RemoveTmpFile(); }
        return keep;
    }

    wxString Export(const wxString &fmt, const wxString &pat, const wxString &message, int action) {
        wxFileName tsfn(filename);
        auto exportfilename = ::wxFileSelector(message, tsfn.GetPath(), tsfn.GetName(), fmt, pat,
                                               wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (exportfilename.empty()) { return _("Export cancelled."); }
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
        DrawView(mdc);
        return bm;
    }

    // wxSVGBitmapEmbedHandler sizes images by their pixels, not by the scale they are drawn at.
    struct SVGImageHandler : wxSVGBitmapHandler {
        bool ProcessBitmap(const wxBitmap &bmp, wxCoord x, wxCoord y,
                           wxOutputStream &stream) const override {
            wxMemoryOutputStream mem;
            bmp.ConvertToImage().SaveFile(mem, wxBITMAP_TYPE_PNG);
            auto s = wxString::Format(
                "  <image x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" "
                "xlink:href=\"data:image/png;base64,%s\"/>\n",
                x, y, wxRound(bmp.GetLogicalWidth()), wxRound(bmp.GetLogicalHeight()),
                wxBase64Encode(mem.GetOutputStreamBuffer()->GetBufferStart(), mem.GetSize()));
            auto buf = s.utf8_str();
            stream.Write(buf.data(), buf.length());
            return stream.IsOk();
        }
    };

    template<typename F> wxString GetSVG(const wxRect &r, F draw) {
        scrollx = r.x;
        scrolly = r.y;
        maxx = r.GetRight() + 1;
        maxy = r.GetBottom() + 1;
        wxSVGFileDC sdc(r.GetSize());
        sdc.SetBitmapHandler(new SVGImageHandler());
        sdc.SetDeviceOrigin(-r.x, -r.y);
        draw(sdc);
        // wxSVGFileDC has each line stretched to the width measured here (textLength). Inkscape
        // does that by spacing the letters apart, which tears right-to-left text apart, so leave
        // it out there.
        auto svg = sdc.GetSVGDocument();
        for (size_t pos = 0; (pos = svg.find("<text ", pos)) != wxString::npos;) {
            auto end = svg.find("</text>", pos);
            auto content = svg.substr(pos, end - pos).AfterFirst('>');
            if (any_of(content.begin(), content.end(), [](wxUniChar c) {
                    auto k = bidi::Classify(c.GetValue());
                    return k == bidi::R || k == bidi::AL;
                })) {
                auto a = svg.find(" textLength=\"", pos);
                if (a < end) { svg.erase(a, svg.find('"', a + 13) + 1 - a); }
            }
            pos = end;
        }
        return svg;
    }

    // Only the selected cells, on the color they show on, without the cells around them.
    wxString GetSubSVG(const Selection &sel) {
        auto r = sel.grid->GetRect(this, sel, true);
        return GetSVG(r, [&](wxSVGFileDC &dc) {
            auto *p = sel.grid->cell;
            while (p != nullptr && p->drawstyle == DS_BLOBLINE) {
                p = p == currentdrawroot ? nullptr : p->parent;
            }
            // wxSVGFileDC writes the clip rectangle as is, outside of the device origin.
            dc.SetClippingRegion(wxRect(r.GetSize()));
            auto bg = p != nullptr ? p->actualcellcolor : Background();
            // Unless all the cells cover it with their own color (see Cell::Render()).
            bool covered = true;
            loop(y, sel.ys) loop(x, sel.xs) {
                auto &c = sel.grid->C(sel.x + x, sel.y + y);
                covered &= c->drawstyle == DS_GRID && c->actualcellcolor != bg;
            }
            if (!covered) { DrawRectangle(dc, bg, r.x, r.y, r.width, r.height); }
            ResetFont(dc);
            auto *c = sel.grid->cell;
            c->grid->Render(this, c->GetX(this), c->GetY(this), dc,
                            c->Depth() - currentdrawroot->Depth(), 0, 0, 0, 0);
        });
    }

    #ifdef ENABLE_WXPDFDOC
        bool DrawPDF(const wxString &expfilename) {
            {
                wxPdfDocument measurePdf;
                measurePdf.AddPage();

                {
                    wxPdfDC measureDC(&measurePdf, 0, 0);
                    if (!measureDC.StartDoc(wxEmptyString)) return false;
                    currentdrawroot->ResetChildren();
                    Layout(measureDC);
                    measureDC.EndDoc();
                }

                maxx = layoutxs;
                maxy = layoutys;
                scrollx = scrolly = 0;
            }

            wxPdfDocument pdf(wxPORTRAIT, maxx, maxy, wxT("pt"));
            pdf.SetTitle(wxEmptyString);
            pdf.AddPage();

            {
                wxPdfDC dc(&pdf, maxx, maxy);
                if (!dc.StartDoc(wxEmptyString)) return false;
                vector<DrawnLink> links;
                drawnlinks = &links;
                DrawView(dc);
                drawnlinks = nullptr;
                // The page is in the points that the view was drawn in.
                for (const auto &[r, c, k] : links) {
                    if (!k->internal) {
                        pdf.Link(r.x, r.y, r.width, r.height, wxPdfLink(LinkURL(k->target)));
                        continue;
                    }
                    auto *target = FormulaEvaluator::LinkTarget(c, k->target);
                    if (target == nullptr || !InView(target)) { continue; }
                    auto link = pdf.AddLink();
                    pdf.SetLink(link, target->GetY(this));
                    pdf.Link(r.x, r.y, r.width, r.height, wxPdfLink(link));
                }
                dc.EndDoc();
            }

            bool saved = pdf.SaveAsFile(expfilename);

            currentdrawroot->ResetChildren();
            UpdateLayout();
            return saved;
        }
    #endif

    template<typename DC> void DrawView(DC &dc) {
            DrawRectangle(dc, Background(), 0, 0, maxx, maxy);
            Render(dc);
    }

    wxBitmap GetSubBitmap(const Selection &sel) {
        wxRect r = sel.grid->GetRect(this, sel, true);
        return GetBitmap().GetSubBitmap(r);
    }

    void RefreshImageRefCount(bool includefolded) const {
        loopv(i, sys->imagelist) sys->imagelist[i]->trefc = 0;
        root->ImageRefCount(includefolded);
    }

    wxString ExportFile(const wxString &filename, int action, bool currentview) {
        Cell *exportroot = currentview ? currentdrawroot : root.get();
        if (action == A_EXPIMAGE) {
            auto image = GetBitmap().ConvertToImage();
            canvas->Refresh();
            auto type = wxBITMAP_TYPE_PNG;
            #if wxUSE_LIBWEBP
                if (wxFileName(filename).GetExt().Lower() == "webp") {
                    // Lossless suits the sharp text and flat colors best, and is still small.
                    image.SetOption(wxIMAGE_OPTION_WEBP_FORMAT, wxWebPImageFormat::Lossless);
                    type = wxBITMAP_TYPE_WEBP;
                }
            #endif
            if (!image.SaveFile(filename, type)) { return _("Error writing to file."); }
        } else if (action == A_EXPSVG) {
            auto svg =
                GetSVG(wxRect(0, 0, layoutxs, layoutys), [&](wxSVGFileDC &dc) { DrawView(dc); });
            wxFile f(filename, wxFile::write);
            if (!f.IsOpened() || !f.Write(svg, wxConvUTF8)) {
                return _("Error exporting to SVG file!");
            }
            canvas->Refresh();
        } 
        #ifdef ENABLE_WXPDFDOC
            else if (action == A_EXPPDF) {
                if (!DrawPDF(filename)) { return _("Error exporting to PDF file!"); }
                canvas->Refresh();
            }
        #endif
        else {
            wxFFileOutputStream fos(filename, "w+b");
            if (!fos.IsOk()) {
                wxMessageBox(_("Error exporting file!"), filename.wx_str(), wxOK, sys->frame);
                return _("Error writing to file!");
            }
            wxTextOutputStream dos(fos);
            // The cells that internal links go to get an anchor, see Cell::ToText().
            if (Text::IsHTMLFormat(action)) {
                exportroot->ForCells(true, [&](Cell *c) {
                    for (const auto &k : c->text.links.v) {
                        auto *target =
                            k.internal ? FormulaEvaluator::LinkTarget(c, k.target) : nullptr;
                        if (target != nullptr && exportroot->IsParentOf(target)) {
                            htmlanchors.emplace(target, static_cast<int>(htmlanchors.size()));
                        }
                    }
                });
            }
            wxString content = exportroot->ToText(0, Selection(), action, this, true, exportroot);
            htmlanchors.clear();
            switch (action) {
                case A_EXPXML:
                    dos.WriteString(
                        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<!DOCTYPE cell [\n"
                        "<!ELEMENT cell (grid)>\n"
                        "<!ELEMENT grid (row*)>\n"
                        "<!ELEMENT row (cell*)>\n"
                        "]>\n");
                    dos.WriteString(content);
                    break;
                case A_EXPHTMLT:
                case A_EXPHTMLTI:
                case A_EXPHTMLTE:
                case A_EXPHTMLB:
                case A_EXPHTMLO: {
                    wxString output;
                    output
                        << "<!DOCTYPE html>\n"
                        << "<html>\n<head>\n<style>\n"
                        << "body { font-family: '" << sys->defaultfont << "', sans-serif; }\n"
                        << "table, th, td { border: 1px solid #A0A0A0; border-collapse: collapse;"
                        << " padding: 3px; vertical-align: top; }\n"
                        << "@media (prefers-color-scheme: dark) {\n"
                        << "  html { filter: invert(1); }\n"
                        << "  img { filter: invert(1); }\n"
                        << "}\n"
                        << "li { }\n</style>\n"
                        << "<title>export of TreeSheets file " << this->filename
                        << "</title>\n<meta charset=\"UTF-8\" />\n"
                        << "</head>\n<body style=\""
                        << wxString::Format("background-color: #%06X;", SwapColor(root->cellcolor))
                        << "\">" << content << "</body>\n</html>\n";
                    dos.WriteString(output);
                    break;
                }
                case A_EXPCSV:
                case A_EXPTSV:
                case A_EXPTEXT: dos.WriteString(content); break;
            }
            if (action == A_EXPHTMLTE) { ExportAllImages(filename, exportroot); }
        }
        return _("File exported successfully.");
    }

    // Takes effect with the next save. An empty password removes it.
    bool SetPassword(const wxString &password) {
        if (password.empty()) {
            if (!encryption) { return true; }
            encryption.reset();
        } else {
            auto e = make_unique<Encryption>();
            if (!e->SetPassword(password)) { return false; }
            encryption = std::move(e);
        }
        ModifiedWithoutUndo();
        return true;
    }

    // html is A_AUTOEXPORT_HTML_* - A_AUTOEXPORT_HTML_NONE.
    void SetAutoExport(int html, bool pdf) {
        if (html == autohtmlexport && pdf == autopdfexport) { return; }
        autohtmlexport = html;
        autopdfexport = pdf;
        ModifiedWithoutUndo();
    }

    // For changes to the document settings, which aren't undoable but need saving.
    void ModifiedWithoutUndo() {
        undolistsizeatfullsave = -1;
        lastmodsinceautosave = wxGetLocalTime();
        modified = true;
        UpdateFileName();
    }

    wxString Save(bool saveas, bool *success = nullptr) {
        if (!saveas && !filename.empty()) { return SaveDB(success); }
        auto filename = ::wxFileSelector(_("Choose TreeSheets file to save:"), "", "", "cts",
                                         _("TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*"),
                                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
        if (filename.empty()) { return _("Save cancelled."); }  // avoid name being set to ""
        ChangeFileName(filename, true);
        return SaveDB(success);
    }

    void AutoSave(bool minimized, int page) {
        if (sys->autosave && tmpsavesuccess && !filename.empty() && lastmodsinceautosave != 0 &&
            (lastmodsinceautosave + 60 < wxGetLocalTime() || lastsave + 300 < wxGetLocalTime() ||
             minimized)) {
            tmpsavesuccess = false;
            SaveDB(&tmpsavesuccess, true, page);
        }
    }

    wxString Key(int uk, int k, bool alt, bool ctrl, bool shift, bool &unprocessed) {
        ClearMultiSel();
        if (uk == WXK_NONE || k < ' ' && k != 0 || k == WXK_DELETE) {
            switch (k) {
                case WXK_BACK:  // no menu shortcut available in wxwidgets
                    if (!ctrl) { return Action(A_BACKSPACE); }
                    break;  // Prevent Ctrl+H from being treated as Backspace
                case WXK_RETURN:
                    return Action(shift  ? alt    ? A_SPLITCELL
                                           : ctrl ? A_ENTERGRIDN
                                                  : A_ENTERGRID
                                  : ctrl ? A_ENTERCELL_JUMPTOSTART
                                         : A_ENTERCELL);
                case WXK_ESCAPE:  // docs say it can be used as a menu accelerator, but it does not
                                  // trigger from there?
                    return Action(A_CANCELEDIT);
                #ifdef WIN32  // works fine on Linux, not sure OS X
                case WXK_PAGEDOWN: canvas->CursorScroll(0, g_scrollratecursor); return wxEmptyString;
                case WXK_PAGEUP: canvas->CursorScroll(0, -g_scrollratecursor); return wxEmptyString;
                #endif
                #ifdef __WXGTK__
                // Due to limitations within GTK, wxGTK does not support specific keycodes 
                // as accelerator keys for menu items. See wxWidgets documentation for the 
                // wxMenuItem class in order to obtain more details. This is why we implement 
                // the missing handling of these accelerator keys in the following section.
                // Please be aware that the custom implementation has the downside of these
                // "accelerator keys" being suppressed in the menu items on wxGTK.
                    case WXK_DELETE: return Action(shift ? A_DELGRID : A_DELETE);
                    case WXK_LEFT:
                        return Action(shift ? (ctrl ? A_SCLEFT : A_SLEFT)
                                            : (ctrl ? A_MLEFT : A_LEFT));
                    case WXK_RIGHT:
                        return Action(shift ? (ctrl ? A_SCRIGHT : A_SRIGHT)
                                            : (ctrl ? A_MRIGHT : A_RIGHT));
                    case WXK_UP:
                        if (ctrl && alt && shift) { return Action(A_INCWIDTHNH); }
                        return Action(shift ? (ctrl ? A_SCUP : A_SUP) : (ctrl ? A_MUP : A_UP));
                    case WXK_DOWN:
                        if (ctrl && alt && shift) { return Action(A_DECWIDTHNH); }
                        return Action(shift ? (ctrl ? A_SCDOWN : A_SDOWN)
                                            : (ctrl ? A_MDOWN : A_DOWN));
                    case WXK_HOME:
                        return Action(shift ? (ctrl ? A_SHOME : A_SHOME)
                                            : (ctrl ? A_CHOME : A_HOME));
                    case WXK_END:
                        return Action(shift ? (ctrl ? A_SEND : A_SEND) : (ctrl ? A_CEND : A_END));
                    case WXK_TAB:
                        // CTRL+I (italics) arrives as the same keycode as CTRL+TAB, which
                        // TSCanvas::OnKeyDown() handles instead.
                        if (ctrl) { break; }
                        return Action(shift ? A_PREV : A_NEXT);
                    case WXK_PAGEUP:
                        if (ctrl) {
                            return Action(alt ? A_INCWIDTHNH : shift ? A_INCSIZENH : A_ZOOMIN);
                        }
                        if (shift) { return Action(A_INCSIZE); }
                        if (!alt) { canvas->CursorScroll(0, -g_scrollratecursor); }
                        return wxEmptyString;
                    case WXK_PAGEDOWN:
                        if (ctrl) {
                            return Action(alt ? A_DECWIDTHNH : shift ? A_DECSIZENH : A_ZOOMOUT);
                        }
                        if (shift) { return Action(A_DECSIZE); }
                        if (!alt) { canvas->CursorScroll(0, g_scrollratecursor); }
                        return wxEmptyString;
                #endif
            }
        } else if (uk >= ' ') {
            if (selected.grid == nullptr) { return NoSel(); }

            // Capture enough state before the edit to tell, after laying it out,
            // whether it's safe to repaint just the edited cell's area instead of the
            // whole visible viewport. "Safe" means: the selection didn't need a row or
            // column inserted (a structural change, handled below), and no cell from
            // the edited one up to the draw root changed size -- if none did, nothing
            // else could have shifted position either, so nothing else needs repainting.
            bool safe = !selected.Thin();
            wxRect oldrect;
            vector<pair<Cell *, CellGeom>> oldsizes;
            if (safe) {
                oldrect = selected.grid->GetRect(this, selected);
                for (Cell *p = selected.GetCell(); p != nullptr; p = p->parent) {
                    oldsizes.emplace_back(
                        p, CellGeom {p->sx, p->sy, p->ox, p->oy, p->minx, p->miny, p->ycenteroff});
                    if (p == currentdrawroot) break;
                }
            }

            auto *c = selected.ThinExpand(this);
            if (c == nullptr) {
                selected.Wrap(this);
                c = selected.GetCell();
                safe = false;
            }
            Selection editedsel = selected;

            c->AddUndo(this, true);
            c->text.Key(this, uk, selected);
            if (!safe || !FastRelayoutAfterEdit(editedsel, oldsizes)) { UpdateLayout(); }

            int vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;
            canvas->GetViewStart(&vx0, &vy0);
            ScrollIfSelectionOutOfView();
            canvas->GetViewStart(&vx1, &vy1);
            safe = safe && vx0 == vx1 && vy0 == vy1;

            for (auto &[p, oldsz] : oldsizes) {
                if (p->sx != oldsz.sx || p->sy != oldsz.sy) {
                    safe = false;
                    break;
                }
            }

            // Let the event loop combine paints when several text events are queued.
            if (safe) {
                wxRect newrect = editedsel.grid->GetRect(this, editedsel);
                newrect.Union(oldrect);
                RefreshDocRect(newrect);
            } else {
                canvas->Refresh();
            }
            return wxEmptyString;
        }
        unprocessed = true;
        return wxEmptyString;
    }

    // With recursive (Shift on a toolbar button), styles and alignments also apply to all cells
    // nested inside the selected ones.
    wxString Action(int action, bool recursive = false) {
        EndFoldPeek();
        if (MultiSel()) {
            switch (action) {
                case wxID_BOLD:
                case wxID_ITALIC:
                case A_TT:
                case wxID_UNDERLINE:
                case wxID_STRIKETHROUGH:
                case A_ALIGNAUTO:
                case A_ALIGNLEFT:
                case A_ALIGNCENTER:
                case A_ALIGNRIGHT:
                case A_VALIGNAUTO:
                case A_ALIGNTOP:
                case A_ALIGNMIDDLE:
                case A_ALIGNBOTTOM:
                case A_LASTCELLCOLOR:
                case A_LASTTEXTCOLOR:
                case A_LASTBORDCOLOR: return MultiSelAction(action, recursive);
                default: ClearMultiSel();
            }
        }
        switch (action) {
            case wxID_EXECUTE: {
                root->AddUndo(this);
                auto &ev = sys->evaluator;
                vars = ev.Eval(root.get());
                formulasdirty = true;
                root->ResetChildren();
                selected = Selection();
                begindrag = Selection();
                // Select the cell where the first error happened, if it is still there.
                loopallcells(c) if (c == ev.failedcell) {
                    SetSelect(c->parent->grid->FindCell(c));
                    break;
                }
                UpdateLayout();
                if (selected.grid) { ScrollOrZoom(); }
                canvas->Refresh();
                switch (ev.failure) {
                    case treesheets::Evaluator::FAIL_UNKNOWNOP:
                        return wxString::Format(_("Evaluation error: unknown operation \"%s\""),
                                                ev.failedname);
                    case treesheets::Evaluator::FAIL_NOINPUT:
                        return wxString::Format(
                            _("Evaluation error: nothing for \"%s\" to work on"), ev.failedname);
                    case treesheets::Evaluator::FAIL_NOARGUMENT:
                        return wxString::Format(
                            _("Evaluation error: \"%s\" is missing an argument"), ev.failedname);
                    case treesheets::Evaluator::FAIL_NOVARIABLE:
                        return wxString::Format(
                            _("Evaluation error: the variable \"%s\" has no value"), ev.failedname);
                    default: return _("Evaluation finished.");
                }
            }

            case A_CLRVIEW: {
                root->AddUndo(this);
                loopallcells(c) {
                    if (c->celltype == CT_VIEWH || c->celltype == CT_VIEWV) { c->Clear(); }
                }
                root->ResetChildren();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case wxID_UNDO:
                if (!undolist.empty()) {
                    Undo(undolist, redolist);
                    return wxEmptyString;
                } else {
                    return _("Nothing more to undo.");
                }

            case wxID_REDO:
                if (!redolist.empty()) {
                    Undo(redolist, undolist, true);
                    return wxEmptyString;
                } else {
                    return _("Nothing more to redo.");
                }

            case wxID_SAVE: return Save(false);
            case wxID_SAVEAS: return Save(true);
            case A_SAVEALL: sys->SaveAll(); return wxEmptyString;
            case A_SETPASSWORD: {
                wxPasswordEntryDialog dlg(sys->frame,
                                          _("Password to encrypt this document with when saving "
                                            "(leave empty to save it unencrypted):"),
                                          _("Set Password"));
                if (dlg.ShowModal() != wxID_OK) { return _("Password unchanged."); }
                auto password = dlg.GetValue();
                if (password.empty()) {
                    if (!encryption) { return _("Password unchanged."); }
                } else {
                    wxPasswordEntryDialog confirm(sys->frame, _("Repeat the password:"),
                                                  _("Set Password"));
                    if (confirm.ShowModal() != wxID_OK) { return _("Password unchanged."); }
                    if (confirm.GetValue() != password) { return _("The passwords differ."); }
                }
                wxBusyCursor wait;
                if (!SetPassword(password)) { return _("Error encrypting file."); }
                return encryption ? _("The document will be encrypted when saved.")
                                  : _("The document will be saved unencrypted.");
            }

            case A_AUTOEXPORT_HTML_NONE:
            case A_AUTOEXPORT_HTML_WITH_IMAGES:
            case A_AUTOEXPORT_HTML_WITHOUT_IMAGES:
                SetAutoExport(action - A_AUTOEXPORT_HTML_NONE, autopdfexport);
                return wxEmptyString;
            #ifdef ENABLE_WXPDFDOC
                case A_AUTOEXPORT_PDF:
                    SetAutoExport(autohtmlexport, !autopdfexport);
                    return wxEmptyString;
            #endif

            case A_EXPXML: return Export("xml", "*.xml", _("Choose XML file to write"), action);
            case A_EXPHTMLT:
            case A_EXPHTMLTE:
            case A_EXPHTMLB:
            case A_EXPHTMLO:
                return Export("html", "*.html", _("Choose HTML file to write"), action);
            case A_EXPTEXT: return Export("txt", "*.txt", _("Choose Text file to write"), action);
            case A_EXPIMAGE:
                #if wxUSE_LIBWEBP
                    return Export("png", "PNG (*.png)|*.png|WebP (*.webp)|*.webp",
                                  _("Choose image file to save:"), action);
                #else
                    return Export("png", "*.png", _("Choose PNG file to write"), action);
                #endif
            case A_EXPSVG: return Export("svg", "*.svg", _("Choose SVG file to write"), action);
            #ifdef ENABLE_WXPDFDOC
                case A_EXPPDF: return Export("pdf", "*.pdf", _("Choose PDF file to write"), action); 
            #endif
            case A_EXPCSV: return Export("csv", "*.csv", _("Choose CSV file to write"), action);
            case A_EXPTSV: return Export("tsv", "*.tsv", _("Choose TSV file to write"), action);

            case A_IMPXML:
            case A_IMPXMLA:
            case A_IMPTXTI:
            case A_IMPTXTC:
            case A_IMPTXTS:
            case A_IMPTXTT: {
                wxArrayString filenames;
                GetFilesFromUser(filenames, sys->frame, _("Please select file(s) to import:"),
                                 _("*.*"));
                wxString message = "";
                for (auto &filename : filenames) { message = sys->Import(filename, action); }
                return message;
            }

            case wxID_OPEN: {
                wxArrayString filenames;
                wxFileName fn(sys->lastopenfile);
                wxString lastdir = fn.DirExists() ? fn.GetPath() : wxString("");
                GetFilesFromUser(filenames, sys->frame,
                                 _("Please select TreeSheets file(s) to load:"),
                                 _("TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*"), lastdir);
                wxString message = "";
                for (auto &filename : filenames) { message = sys->Open(filename); }
                return message;
            }

            case wxID_CLOSE: {
                if (sys->frame->notebook->GetPageCount() <= 1) {
                    sys->frame->fromclosebox = false;
                    sys->frame->Close();
                    return wxEmptyString;
                }

                if (!CloseDocument()) {
                    int pagenumber = sys->frame->notebook->GetSelection();
                    // sys->frame->notebook->AdvanceSelection();
                    sys->frame->notebook->DeletePage(pagenumber);
                    sys->PurgeUnusedImages();
                }
                return wxEmptyString;
            }

            case wxID_NEW: {
                GridSizeDialog dialog(sys->frame, _("New Sheet"),
                                      _("What size grid would you like to start with?"), 10, 25);
                if (dialog.ShowModal() != wxID_OK) { return _("New file cancelled."); }
                sys->InitDB(dialog.columns->GetValue(), dialog.rows->GetValue());
                sys->frame->GetCurrentTab()->doc->UpdateLayout();
                sys->frame->GetCurrentTab()->Refresh();
                return wxEmptyString;
            }

            case wxID_ABOUT: {
                wxAboutDialogInfo info;
                info.SetName("TreeSheets");
                info.SetVersion(wxT(PACKAGE_VERSION));
                info.SetCopyright("(C) 2026 Wouter van Oortmerssen and Tobias Predel");
                // The libraries that CMake builds in, see CMakeLists.txt and cmake/*.cmake.
                wxString libs = wxVERSION_STRING " (wxWindows Library Licence)";
                #ifdef ENABLE_LOBSTER
                    libs += "\nLobster (Apache License 2.0)";
                #endif
                #ifdef ENABLE_WXPDFDOC
                    libs += "\nwxPdfDocument (wxWindows Library Licence)";
                #endif
                libs += "\nMonocypher (BSD 2-Clause License)\nlibunibreak (zlib License)";
                auto desc = wxString::Format(
                    "%s\n\n%s:\n%s\n\n%s",
                    _("The Free Form Hierarchical Information Organizer"), _("Uses"), libs,
                    _("Some icons are based on vscode-icons by Microsoft (CC BY 4.0):") +
                        "\nhttps://github.com/microsoft/vscode-icons"
                        "\nhttps://creativecommons.org/licenses/by/4.0/");
                info.SetDescription(desc);
                wxAboutBox(info);
                return wxEmptyString;
            }

            case wxID_HELP: sys->LoadTutorial(); return wxEmptyString;

            case A_CHECKUPDATES: return sys->frame->CheckForUpdates();

            case A_HELP_OP_REF: sys->LoadOpRef(); return wxEmptyString;

            case A_HELP_EXAMPLES: {
                wxArrayString filenames;
                GetFilesFromUser(filenames, sys->frame, _("Please select example file(s) to load:"),
                                 _("TreeSheets Files (*.cts)|*.cts|All Files (*.*)|*.*"),
                                 sys->frame->app->GetDocPath("examples"));
                wxString message = "";
                for (auto &filename : filenames) { message = sys->Open(filename); }
                return message;
            }

            case A_TUTORIALWEBPAGE: {
                wxTranslations *trans = wxTranslations::Get();
                wxString lang = trans != nullptr && sys->defaultlang != "en"
                                    ? trans->GetBestTranslation("ts")
                                    : wxString("");
                wxString tutorialpath;

                for (const wxString &suffix : {"-" + lang, "-" + lang.Left(2), wxString("")}) {
                    if (suffix.Length() == 1) { continue; }

                    wxString candidate =
                        sys->frame->app->GetDocPath("docs/tutorial" + suffix + ".html");
                    if (::wxFileExists(candidate)) {
                        tutorialpath = candidate;
                        break;
                    }
                }

                if (!tutorialpath.IsEmpty()) {
                    #ifdef __WXMAC__
                        wxLaunchDefaultBrowser("file://" + tutorialpath);
                    #else
                        wxLaunchDefaultBrowser(tutorialpath);
                    #endif
                    return wxEmptyString;
                } else {
                    return _("Tutorial web page could not be found.");
                }
            }

            #ifdef ENABLE_LOBSTER
                case A_SCRIPTREFERENCE:
                    #ifdef __WXMAC__
                    wxLaunchDefaultBrowser(
                        "file://" +
                        sys->frame->app->GetDocPath("docs/script_reference.html"));  // RbrtPntn
                    #else
                    wxLaunchDefaultBrowser(
                        sys->frame->app->GetDocPath("docs/script_reference.html"));
                    #endif
                        return wxEmptyString;
            #endif

            case A_ZOOMIN:
                return Wheel(1, false, true,
                             false);  // Zoom( 1, dc); return "zoomed in (menu)";
            case A_ZOOMOUT:
                return Wheel(-1, false, true,
                             false);  // Zoom(-1, dc); return "zoomed out (menu)";
            case A_INCSIZE: return Wheel(1, false, false, true);
            case A_DECSIZE: return Wheel(-1, false, false, true);
            case A_INCSIZENH: return Wheel(1, false, false, true, false);
            case A_DECSIZENH: return Wheel(-1, false, false, true, false);
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
                    g_deftextsize_default = g_deftextsize =
                        std::clamp(font.GetPointSize(), g_min_deftextsize, g_max_deftextsize);
                    sys->cfg->Write("defaultfontsize", g_deftextsize);
                    switch (action) {
                        case wxID_SELECT_FONT:
                            sys->defaultfont = font.GetFaceName();
                            sys->cfg->Write("defaultfont", sys->defaultfont);
                            break;
                        case A_SET_FIXED_FONT:
                            sys->defaultfixedfont = font.GetFaceName();
                            sys->cfg->Write("defaultfixedfont", sys->defaultfixedfont);
                            break;
                    }
                    sys->frame->TabsReset();  // ResetChildren, UpdateLayout and Refresh on all
                }
                return wxEmptyString;
            }

            case A_INCFONTSIZE:
            case A_DECFONTSIZE: {
                g_deftextsize = std::clamp(g_deftextsize + (action == A_INCFONTSIZE ? 1 : -1),
                                           g_min_deftextsize, g_max_deftextsize);
                sys->frame->TabsReset();
                return wxEmptyString;
            }

            case A_RESETFONTSIZE: {
                g_deftextsize = g_deftextsize_default;
                sys->frame->TabsReset();
                return wxEmptyString;
            }

            case wxID_PRINT: {
                wxPrintDialogData printDialogData(printData);
                wxPrinter printer(&printDialogData);
                Printout printout(this);
                if (printer.Print(sys->frame, &printout, true)) {
                    printData = printer.GetPrintDialogData().GetPrintData();
                }
                return wxEmptyString;
            }

            case A_PRINTSCALE: {
                printscale = static_cast<uint>(::wxGetNumberFromUser(
                    _("How many pixels wide should a page be? (0 for auto fit)"), _("scale:"),
                    _("Set Print Scale"), 0, 0, 5000, sys->frame));
                return wxEmptyString;
            }

            case wxID_PREVIEW: {
                wxPrintDialogData printDialogData(printData);
                auto *preview =
                    new wxPrintPreview(new Printout(this), new Printout(this), &printDialogData);
                auto *pframe = new wxPreviewFrame(preview, sys->frame, _("Print Preview"),
                                                  wxPoint(100, 100), wxSize(600, 650));
                pframe->Centre(wxBOTH);
                pframe->Initialize();
                pframe->Show(true);
                return wxEmptyString;
            }

            case A_PAGESETUP: {
                pageSetupData = printData;
                wxPageSetupDialog pageSetupDialog(sys->frame, &pageSetupData);
                pageSetupDialog.ShowModal();
                printData = pageSetupDialog.GetPageSetupDialogData().GetPrintData();
                pageSetupData = pageSetupDialog.GetPageSetupDialogData();
                return wxEmptyString;
            }

            case A_NEXTFILE: sys->frame->CycleTabs(1); return wxEmptyString;
            case A_PREVFILE: sys->frame->CycleTabs(-1); return wxEmptyString;

            case A_DEFBGCOL: {
                auto oldbg = Background();
                if (auto color = PickColor(sys->frame, oldbg); color != static_cast<uint>(-1)) {
                    root->AddUndo(this);
                    loopallcells(c) {
                        if (c->cellcolor == oldbg &&
                            (c->parent == nullptr || c->parent->cellcolor == color)) {
                            c->cellcolor = color;
                        }
                    }
                    canvas->Refresh();
                }
                return wxEmptyString;
            }

            case A_SEARCHNEXT:
            case A_SEARCHPREV: {
                if (!sys->searchstring.IsEmpty()) {
                    return SearchNext(false, true, action == A_SEARCHPREV);
                }
                if (auto *c = selected.GetCell()) {
                    auto s = c->text.ToText(0, selected, A_EXPTEXT);
                    if (s.IsEmpty()) { return _("No text to search for."); }
                    sys->frame->filter->SetFocus();
                    sys->frame->filter->SetValue(s);
                    return wxEmptyString;
                } else {
                    return _("You need to select one cell if you want to search for its text.");
                }
            }

            case A_CASESENSITIVESEARCH: {
                sys->casesensitivesearch = !(sys->casesensitivesearch);
                sys->cfg->Write("casesensitivesearch", sys->casesensitivesearch);
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
                sys->cfg->Write("roundness", static_cast<long>(sys->roundness = action - A_ROUND0));
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;

            case A_OPENCELLCOLOR:
                if (sys->frame->cellcolordropdown != nullptr) {
                    sys->frame->cellcolordropdown->ShowPopup();
                }
                return wxEmptyString;
            case A_OPENTEXTCOLOR:
                if (sys->frame->textcolordropdown != nullptr) {
                    sys->frame->textcolordropdown->ShowPopup();
                }
                return wxEmptyString;
            case A_OPENBORDCOLOR:
                if (sys->frame->bordercolordropdown != nullptr) {
                    sys->frame->bordercolordropdown->ShowPopup();
                }
                return wxEmptyString;
            case A_OPENIMGDROPDOWN:
                if (sys->frame->imagedropdown != nullptr) {
                    sys->frame->imagedropdown->ShowPopup();
                }
                return wxEmptyString;
            case A_REPLACEONCE:
            case A_REPLACEONCEJ:
            case A_REPLACEALL: {
                if (sys->searchstring.IsEmpty()) { return _("No search."); }
                auto replaces = sys->frame->replaces->GetValue();
                Text::FilterControls(replaces);
                auto lreplaces =
                    sys->casesensitivesearch ? wxString(wxEmptyString) : replaces.Lower();
                if (action == A_REPLACEALL) {
                    Cell *replaceroot = sys->restrictview ? currentdrawroot : root.get();
                    replaceroot->AddUndo(this);  // expensive?
                    replaceroot->FindReplaceAllStart(replaces, lreplaces, sys->restrictview);
                    replaceroot->ResetChildren();
                    UpdateLayout();
                    canvas->Refresh();
                } else {
                    if (selected.grid == nullptr) { return NoSel(); }
                    selected.grid->ReplaceStr(this, replaces, lreplaces, selected);
                    if (action == A_REPLACEONCEJ) { return SearchNext(false, true, false); }
                }
                return _("Text has been replaced.");
            }

            case A_CLEARREPLACE: {
                sys->frame->replaces->Clear();
                canvas->SetFocus();
                return wxEmptyString;
            }

            case A_CLEARSEARCH: {
                sys->frame->filter->Clear();
                canvas->SetFocus();
                return wxEmptyString;
            }

            case A_SCALED:
                scaledviewingmode = !scaledviewingmode;
                root->ResetChildren();
                UpdateLayout();
                canvas->Refresh();
                return scaledviewingmode ? _("Now viewing TreeSheet to fit to the screen exactly, press F12 to return to normal.")
                                         : _("1:1 scale restored.");

            case A_FILTERRANGE: {
                DateTimeRangeDialog rd(sys->frame);
                if (rd.Run() == wxID_OK) { ApplyEditRangeFilter(rd.begin, rd.end); }
                return wxEmptyString;
            }

            case A_FILTER5:
                editfilter = 5;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTER10:
                editfilter = 10;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTER20:
                editfilter = 20;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTER50:
                editfilter = 50;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTERM:
                editfilter++;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTERL:
                editfilter--;
                ApplyEditFilter();
                return wxEmptyString;
            case A_FILTERS: SetSearchFilter(true); return wxEmptyString;
            case A_FILTEROFF: SetSearchFilter(false); return wxEmptyString;
            case A_FILTERBYTAGS: {
                if (tags.empty()) { return _("There are no tags in this document."); }
                wxArrayString choices;
                wxArrayInt checked;
                for (auto &[tag, colors] : tags) {
                    if (filtertags.contains(tag)) { checked.Add(choices.size()); }
                    choices.Add(tag);
                }
                if (wxGetSelectedChoices(checked, _("Show cells with any of these tags:"),
                                         _("Filter by tags"), choices, sys->frame) <= 0) {
                    return wxEmptyString;
                }
                filtertags.clear();
                for (auto i : checked) { filtertags.insert(choices[i]); }
                ApplyFilter(
                    [&] { loopallcells(c) c->text.filteredraw = !filtertags.contains(c->text.t); });
                return wxEmptyString;
            }
            case A_FILTERUNION:
                sys->cfg->Write("filterunion", sys->filterunion = !sys->filterunion);
                return wxEmptyString;

            case A_FILTERSHOWROWS: {
                sys->cfg->Write("filtershowrows", sys->filtershowrows = !sys->filtershowrows);
                ApplyRowFilterExpansion();
                root->ResetChildren();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_FILTERSHOWSUBGRIDS: {
                sys->cfg->Write("filtershowsubgrids",
                                sys->filtershowsubgrids = !sys->filtershowsubgrids);
                ApplyRowFilterExpansion();
                root->ResetChildren();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_CUSTKEY: {
                wxArrayString strs;
                wxArrayString keys;
                for (auto &[s, k] : sys->frame->menustrings) {
                    strs.push_back(s);
                    keys.push_back(k);
                }
                wxSingleChoiceDialog choice(
                    sys->frame, _("Please pick a menu item to change the key binding for"),
                    _("Key binding"), strs);
                choice.SetSize(wxSize(500, 700));
                choice.Centre();
                if (choice.ShowModal() == wxID_OK) {
                    int sel = choice.GetSelection();
                    wxTextEntryDialog textentry(sys->frame,
                                                _("Please enter the new key binding string"),
                                                _("Key binding"), keys[sel]);
                    if (textentry.ShowModal() == wxID_OK) {
                        auto key = textentry.GetValue();
                        sys->frame->menustrings[strs[sel]] = key;
                        sys->cfg->Write(strs[sel], key);
                        return _("NOTE: key binding will take effect next run of TreeSheets.");
                    }
                }
                return _("Keybinding cancelled.");
            }

            case A_SETLANG: {
                auto *trans = wxTranslations::Get();
                if (trans == nullptr) { return _("Failed to get translation."); }
                wxArrayString catcodes = trans->GetAvailableTranslations("ts");
                wxArrayString labels;
                wxArrayString codes;
                const wxString syslang = _("System Default");
                const wxString en = _("English");
                labels.Add(syslang);
                codes.Add(wxEmptyString);
                labels.Add(en);
                codes.Add("en");
                for (const auto &catcode : catcodes) {
                    if (catcode.StartsWith("en")) continue;

                    const wxLanguageInfo *info = wxLocale::FindLanguageInfo(catcode);
                    if (info) {
                        labels.Add(info->Description);
                    } else {
                        labels.Add(catcode);
                    }
                    codes.Add(catcode);
                }
                wxSingleChoiceDialog choice(
                    sys->frame,
                    _("Please select the language for the interface (requires restart)."),
                    _("Available languages"), labels);

                if (choice.ShowModal() == wxID_OK) {
                    int i = choice.GetSelection();
                    if (i != wxNOT_FOUND) {
                        wxString code = codes[i];
                        sys->cfg->Write("defaultlang", code);
                    }
                }
                return wxEmptyString;
            }
        }

        if (selected.grid == nullptr) { return NoSel(); }

        auto *cell = selected.GetCell();

        switch (action) {
            case A_BACKSPACE:
                if (selected.Thin()) {
                    if (selected.xs != 0) {
                        DelRowCol(selected.y, 0, selected.grid->ys, 1, -1, selected.y - 1, 0, -1);
                    } else {
                        DelRowCol(selected.x, 0, selected.grid->xs, 1, selected.x - 1, -1, -1, 0);
                    }
                } else if (cell != nullptr && selected.TextEdit()) {
                    if (selected.cursorend == 0) { return wxEmptyString; }
                    cell->AddUndo(this, true);
                    cell->text.Backspace(selected);
                    UpdateLayout();
                    canvas->Refresh();
                } else {
                    selected.grid->MultiCellDelete(this, selected);
                    SetSelect(selected);
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case A_DELETE:
                if (selected.Thin()) {
                    if (selected.xs != 0) {
                        DelRowCol(selected.y, selected.grid->ys, selected.grid->ys, 0, -1,
                                  selected.y, 0, -1);
                    } else {
                        DelRowCol(selected.x, selected.grid->xs, selected.grid->xs, 0, selected.x,
                                  -1, -1, 0);
                    }
                } else if (cell != nullptr && selected.TextEdit()) {
                    if (selected.cursor == cell->text.t.Len()) { return wxEmptyString; }
                    cell->AddUndo(this, true);
                    cell->text.Delete(selected);
                    UpdateLayout();
                    canvas->Refresh();
                } else {
                    selected.grid->MultiCellDelete(this, selected);
                    SetSelect(selected);
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case A_DELETE_WORD:
                if (cell != nullptr && selected.TextEdit()) {
                    if (selected.cursor == cell->text.t.Len()) { return wxEmptyString; }
                    cell->AddUndo(this, true);
                    cell->text.DeleteWord(selected);
                    UpdateLayout();
                    canvas->Refresh();
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case wxID_CUT:
            case wxID_COPY:
            case A_COPYWI:
            case A_COPYCT:
                if (selected.Thin()) {
                    return NoThin();
                }
                if (selected.TextEdit()) {
                    if (selected.cursor == selected.cursorend) { return _("No text selected."); }
                }
                Copy(action);
                if (action == wxID_CUT) {
                    if (!selected.TextEdit()) {
                        selected.grid->MultiCellDelete(this, selected);
                        SetSelect(selected);
                    } else if (cell != nullptr) {
                        cell->AddUndo(this, true);
                        cell->text.Backspace(selected);
                    }
                    UpdateLayout();
                    canvas->Refresh();
                }
                ZoomOutIfNoGrid();
                return wxEmptyString;

            case A_COPYBM:
                if (selected.Thin()) { return NoThin(); }
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxBitmapDataObject(GetSubBitmap(selected)));
                    wxTheClipboard->Close();
                    return _("Bitmap copied to clipboard");
                }
                return wxEmptyString;

            case A_COPYSVG: {
                if (selected.Thin()) { return NoThin(); }
                if (!wxTheClipboard->Open()) { return wxEmptyString; }
                auto svg = GetSubSVG(selected);
                auto buf = svg.utf8_str();
                #ifdef __WXMAC__
                    auto *svgobj = new wxCustomDataObject(wxDataFormat("public.svg-image"));
                #else
                    auto *svgobj = new wxCustomDataObject(wxDataFormat("image/svg+xml"));
                #endif
                svgobj->SetData(buf.length(), buf.data());
                auto *data = new wxDataObjectComposite();
                data->Add(svgobj, true);
                data->Add(new wxTextDataObject(svg));
                wxTheClipboard->SetData(data);
                wxTheClipboard->Close();
                return _("SVG copied to clipboard");
            }

            case A_COLLAPSE: {
                if (selected.Thin()) { return NoThin(); }
                if (selected.xs * selected.ys == 1) {
                    return _("More than one cell must be selected.");
                }
                auto *fc = selected.GetFirst();
                wxString ct = "";
                loopallcellssel(ci, true) if (ci != fc && !ci->text.t.IsEmpty()) {
                    ct += " " + ci->text.t;
                }
                if (!fc->HasContent() && ct.IsEmpty()) {
                    return _("There is no content to collapse.");
                }
                fc->parent->AddUndo(this);
                fc->text.t += ct;
                loopallcellssel(ci, false) if (ci != fc) { ci->Clear(); }
                Selection deletesel(
                    selected.grid,
                    selected.x + static_cast<int>(selected.xs > 1),  // sidestep is possible?
                    selected.y + static_cast<int>(selected.ys > 1),
                    selected.xs - static_cast<int>(selected.xs > 1),
                    selected.ys - static_cast<int>(selected.ys > 1));
                selected.grid->MultiCellDeleteSub(this, deletesel);
                SetSelect(Selection(selected.grid, selected.x, selected.y, 1, 1));
                fc->ResetLayout();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case wxID_SELECTALL:
                selected.SelAll();
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, true);
                return wxEmptyString;

            case A_UP:
            case A_DOWN:
            case A_LEFT:
            case A_RIGHT:
                // Left on the top thin line or Up on the leftmost one, at its start, goes to the
                // parent, as there's nowhere else to go.
                if (selected.Thin() && selected.x == 0 && selected.y == 0 &&
                    selected.grid->cell->parent != nullptr &&
                    (action == A_LEFT ? selected.ys == 0 : action == A_UP && selected.xs == 0)) {
                    return Action(A_CANCELEDIT);
                }
                selected.Cursor(this, action, false, false);
                return wxEmptyString;

            case A_MUP:
            case A_MDOWN:
            case A_MLEFT:
            case A_MRIGHT:
                selected.Cursor(this, action - A_MUP + A_UP, true, false);
                return wxEmptyString;

            case A_SUP:
            case A_SDOWN:
            case A_SLEFT:
            case A_SRIGHT:
                selected.Cursor(this, action - A_SUP + A_UP, false, true);
                return wxEmptyString;

            case A_SCLEFT:
            case A_SCRIGHT:
            case A_SCUP:
            case A_SCDOWN:
                if (!selected.TextEdit()) {
                    bool horiz = (action == A_SCLEFT || action == A_SCRIGHT);
                    bool ismin = (action == A_SCLEFT || action == A_SCUP);

                    int &pos = horiz ? selected.x : selected.y;
                    int &ext = horiz ? selected.xs : selected.ys;
                    int gridmax = horiz ? selected.grid->xs : selected.grid->ys;

                    if (ismin) {
                        // Keep the far end of the selection where it is, and leave a thin
                        // selection at the first row/column alone: it has nothing to extend
                        // over, and would end up pointing at no cells at all.
                        if (auto newext = pos + ext; newext > 0) {
                            ext = newext;
                            pos = 0;
                        }
                    } else {
                        ext = gridmax - pos;
                    }

                    sys->frame->UpdateStatus(selected, true);
                    canvas->Refresh();
                } else if (action == A_SCLEFT || action == A_SCRIGHT) {
                    selected.Cursor(this, action - A_SCUP + A_UP, true, true);
                }
                return wxEmptyString;

            case wxID_BOLD:
                selected.grid->SetStyle(this, selected, STYLE_BOLD, recursive);
                return wxEmptyString;
            case wxID_ITALIC:
                selected.grid->SetStyle(this, selected, STYLE_ITALIC, recursive);
                return wxEmptyString;
            case A_TT:
                selected.grid->SetStyle(this, selected, STYLE_FIXED, recursive);
                return wxEmptyString;
            case wxID_UNDERLINE:
                selected.grid->SetStyle(this, selected, STYLE_UNDERLINE, recursive);
                return wxEmptyString;
            case wxID_STRIKETHROUGH:
                selected.grid->SetStyle(this, selected, STYLE_STRIKETHRU, recursive);
                return wxEmptyString;

            case A_ALIGNAUTO:
            case A_ALIGNLEFT:
            case A_ALIGNCENTER:
            case A_ALIGNRIGHT:
                selected.grid->SetTextAlign(this, selected, action - A_ALIGNAUTO + TEXTALIGN_AUTO,
                                            recursive);
                return wxEmptyString;

            case A_VALIGNAUTO:
            case A_ALIGNTOP:
            case A_ALIGNMIDDLE:
            case A_ALIGNBOTTOM:
                selected.grid->SetVertAlign(this, selected, action - A_VALIGNAUTO + VERTALIGN_AUTO,
                                            recursive);
                return wxEmptyString;

            case A_MARKDATA:
            case A_MARKVARD:
            case A_MARKVARU:
            case A_MARKVIEWH:
            case A_MARKVIEWV:
            case A_MARKCODE:
            case A_MARKFORMULA: {
                int newcelltype = MarkCellType(action);
                selected.grid->cell->AddUndo(this);
                loopallcellssel(c, false) {
                    c->celltype = (newcelltype == CT_CODE) ? sys->evaluator.InferCellType(c->text)
                                                           : newcelltype;
                    c->SyncFormula();
                }
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_CANCELEDIT:
                if (selected.TextEdit()) { break; }
                if (selected.grid->cell->parent != nullptr) {
                    SetSelect(selected.grid->cell->parent->grid->FindCell(selected.grid->cell));
                } else if (selected.IsAll() && sys->frame->IsFullScreen()) {
                    // Nothing left to escape from: a way out for those who forget F11.
                    sys->frame->ShowFullScreen(false);
                    return wxEmptyString;
                } else {
                    selected.SelAll();
                }
                ScrollOrZoom();
                return wxEmptyString;

            case A_ENTERGRID:
            case A_ENTERGRIDN: {
                if ((cell = selected.ThinExpand(this)) == nullptr) { return OneCell(); }
                if (cell->grid) {
                    SetSelect(Selection(cell->grid, 0, 0, 1, 1));
                    ScrollOrZoom(true);
                    return wxEmptyString;
                }
                auto columns = 1;
                auto rows = 1;
                if (action == A_ENTERGRIDN) {
                    GridSizeDialog dialog(sys->frame, _("New subgrid"),
                                          _("What subgrid size would you like to start with?"), 10,
                                          25);
                    if (dialog.ShowModal() != wxID_OK) { return _("No subgrid created."); }
                    columns = dialog.columns->GetValue();
                    rows = dialog.rows->GetValue();
                }
                cell->AddUndo(this);
                cell->AddGrid(columns, rows);
                SetSelect(Selection(cell->grid, 0, 0, 1, 1));
                UpdateLayout();
                ScrollIfSelectionOutOfView();
                canvas->Refresh();
                return wxEmptyString;
            }

            case wxID_PASTE:
            case A_PASTETEXT:
            case A_PASTETSV: {
                if (!selected.Thin() && selected.GetCell() == nullptr) {
                    if (PasteFill(action)) {
                        UpdateLayout();
                        canvas->Refresh();
                        return wxEmptyString;
                    }
                    selected.xs = selected.ys = 1;  // pasted at the top left instead
                }
                int pastemode = selected.PasteMode();
                if ((cell = selected.ThinExpand(this)) == nullptr) { return OneCell(); }

                if (wxTheClipboard->Open()) {
                    if (wxTextDataObject tdo;
                        wxTheClipboard->GetData(tdo) && tdo.GetText().Len() > 0) {
                        PasteOrDrop(tdo, pastemode, action != wxID_PASTE, action == A_PASTETSV);
                    } else if (action != wxID_PASTE) {
                        // Only text can be pasted as text.
                    } else if (wxFileDataObject fdo;
                               wxTheClipboard->GetData(fdo) && fdo.GetFilenames().GetCount() > 0) {
                        PasteOrDrop(fdo);
                    #ifdef __WXMAC__
                        } else if (auto [img, scale] = GetImageFromMacClipboard(); img.IsOk()) {
                            PasteOrDrop(img, scale);
                    #endif
                    } else if (wxBitmapDataObject bdo;
                               wxTheClipboard->GetData(bdo) && bdo.GetBitmap().IsOk()) {
                        PasteOrDrop(bdo);
                    }
                    wxTheClipboard->Close();
                    UpdateLayout();
                    ScrollIfSelectionOutOfView();
                    canvas->Refresh();
                } else if (sys->cellclipboard && action == wxID_PASTE) {
                    cell->Paste(this, sys->cellclipboard.get(), selected, pastemode,
                                sys->cellclipboardcolwidth);
                    UpdateLayout();
                    ScrollIfSelectionOutOfView();
                    canvas->Refresh();
                }
                return wxEmptyString;
            }

            case A_EDITNOTE: {
                // A thin selection gets its new cell only once a note is actually entered.
                cell = selected.GetCell();
                if (cell == nullptr && !selected.Thin()) { return OneCell(); }

                wxDialog dlg(sys->frame, wxID_ANY, _("Note"), wxDefaultPosition,
                             wxSize(sys->notesizex, sys->notesizey),
                             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
                auto *sizer = new wxBoxSizer(wxVERTICAL);
                wxTextCtrl text(&dlg, wxID_ANY, cell ? cell->note : wxString(), wxDefaultPosition,
                                wxDefaultSize, wxTE_MULTILINE);
                sizer->Add(&text, 1, wxEXPAND | wxALL, 10);
                auto *btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
                sizer->Add(btns, 0, wxALIGN_CENTER | wxBOTTOM, 10);
                dlg.SetSizer(sizer);

                text.SetFocus();

                if (dlg.ShowModal() == wxID_OK) {
                    if (cell ? cell->note != text.GetValue() : !text.GetValue().IsEmpty()) {
                        if (cell == nullptr) {
                            cell = selected.ThinExpand(this);
                        } else {
                            cell->AddUndo(this);
                        }
                        cell->note = text.GetValue();
                        UpdateLayout();
                        canvas->Refresh();
                    }
                    sys->notesizex = dlg.GetSize().x;
                    sys->notesizey = dlg.GetSize().y;
                }
                return wxEmptyString;
            }

            case A_PASTESTYLE:
                if (!sys->cellclipboard) { return _("No style to paste."); }
                selected.grid->cell->AddUndo(this, selected, false);
                selected.grid->SetStyles(selected, sys->cellclipboard.get());
                loopallcellssel(c, false) c->ResetLayout();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;

            case A_ENTERCELL:
            case A_ENTERCELL_JUMPTOEND:
            case A_ENTERCELL_JUMPTOSTART:
            case A_PROGRESSCELL: {
                if ((cell = selected.ThinExpand(this, action == A_ENTERCELL_JUMPTOSTART)) ==
                    nullptr) {
                    return OneCell();
                }
                if (selected.TextEdit()) {
                    selected.Cursor(this, action == A_PROGRESSCELL ? A_RIGHT : A_DOWN, false, false,
                                    true);
                } else {
                    selected.EnterEdit(
                        this,
                        action == A_ENTERCELL_JUMPTOEND ? static_cast<int>(cell->text.t.Len()) : 0,
                        static_cast<int>(cell->text.t.Len()));
                    UpdateLayout();
                    ScrollIfSelectionOutOfView();
                    canvas->Refresh();
                }
                return wxEmptyString;
            }

            case A_IMAGE: {
                // A thin selection gets its new cell only once an image is actually chosen.
                cell = selected.GetCell();
                if (cell == nullptr && !selected.Thin()) { return OneCell(); }
                auto filename =
                    ::wxFileSelector(_("Please select an image file:"), "", "", "", "*.*",
                                     wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
                if (filename.empty()) { return wxEmptyString; }
                if (cell == nullptr) {
                    cell = selected.ThinExpand(this);
                } else {
                    cell->AddUndo(this);
                }
                LoadImageIntoCell(filename, cell, sys->frame->FromDIP(1.0));
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_IMAGER: {
                selected.grid->cell->AddUndo(this, selected, false);
                selected.grid->ClearImages(selected);
                loopallcellssel(c, false) c->ResetLayout();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_SORTD: return Sort(true);
            case A_SORT: return Sort(false);

            case A_SCOLS:
                selected.y = 0;
                selected.ys = selected.grid->ys;
                canvas->Refresh();
                return wxEmptyString;

            case A_SROWS:
                selected.x = 0;
                selected.xs = selected.grid->xs;
                canvas->Refresh();
                return wxEmptyString;

            case A_BORD0:
            case A_BORD1:
            case A_BORD2:
            case A_BORD3:
            case A_BORD4:
            case A_BORD5:
                selected.grid->cell->AddUndo(this, Selection(), false);
                selected.grid->SetBorder(action - A_BORD0 + 1);
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;

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
            case A_LASTIMAGE:
                selected.grid->cell->AddUndo(this, selected, true);
                loopallcellssel(c, true) switch (action) {
                    case A_RESETSIZE: c->text.relsize = 0; break;
                    case A_RESETWIDTH:
                        for (int x = selected.x; x < selected.x + selected.xs; x++) {
                            selected.grid->colwidths[x] = sys->defaultmaxcolwidth;
                        }
                        selected.grid->cell->ResetLayout();
                        break;
                    case A_RESETSTYLE:
                        c->text.ResetStyle();
                        c->textalign = TEXTALIGN_AUTO;
                        c->vertalign = VERTALIGN_AUTO;
                        break;
                    case A_RESETCOLOR:
                        if (c->IsTag(this)) {
                            tags[c->text.t] = {g_cellcolor_default, g_tagtextcolor_default};
                        } else {
                            c->textcolor = g_textcolor_default;
                            c->text.ClearRunColors();
                        }
                        c->cellcolor = g_cellcolor_default;
                        if (c->grid) { c->grid->bordercolor = g_bordercolor_default; }
                        break;
                    case A_LASTIMAGE:
                        if (sys->lastimage != nullptr) { c->text.image = sys->lastimage; }
                        break;
                }
                // A width applies to all cells of the column.
                if (action == A_RESETWIDTH) {
                    selected.grid->ResetResized(
                        Selection(selected.grid, selected.x, 0, selected.xs, selected.grid->ys),
                        false);
                } else {
                    loopallcellssel(c, false) c->ResetChildren();
                }
                UpdateLayout();
                canvas->Refresh();
                sys->frame->UpdateStatus(selected, false);
                return wxEmptyString;

            // Like picking the color from the dropdown: the selected cells, not the ones inside.
            case A_LASTCELLCOLOR:
                selected.grid->ColorChange(this, A_CELLCOLOR, sys->lastcellcolor, selected, false);
                return wxEmptyString;
            case A_LASTTEXTCOLOR:
                selected.grid->ColorChange(this, A_TEXTCOLOR, sys->lasttextcolor, selected, false);
                return wxEmptyString;
            case A_LASTBORDCOLOR:
                selected.grid->ColorChange(this, A_BORDCOLOR, sys->lastbordcolor, selected, false);
                return wxEmptyString;

            case A_MINISIZE: {
                selected.grid->cell->AddUndo(this, selected, true);
                CollectCellsSel(false);
                vector<Cell *> outer;
                outer.insert(outer.end(), itercells.begin(), itercells.end());
                for (auto *o : outer) {
                    if (o->grid) {
                        loopcellsin(o, c) if (_i != 0) {
                            c->text.relsize =
                                g_deftextsize - g_mintextsize() - (sys->shrinktext ? c->Depth() : 0);
                        }
                    }
                }
                outer.clear();
                loopallcellssel(c, false) c->ResetChildren();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_FOLD:
            case A_FOLDALL:
            case A_UNFOLDALL: {
                // Toggling folds all of the selected grids, unless they are all folded already.
                auto fold = action == A_FOLDALL || (action == A_FOLD && AnyUnfoldedInSelection());
                loopallcellssel(c, action != A_FOLD) if (c->grid) {
                    c->AddUndo(this, Selection(), false);
                    c->grid->folded = fold;
                }
                UpdateLayout();
                ScrollIfSelectionOutOfView();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_DELGRID: {
                CollectCellsSel(false);
                if (std::ranges::none_of(itercells, [](Cell *c) { return c->grid != nullptr; })) {
                    return NoGrid();
                }
                selected.grid->cell->AddUndo(this, selected, false);
                for (auto c : itercells) {
                    c->grid = nullptr;
                    c->ResetLayout();
                }
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_HOME:
            case A_END:
            case A_CHOME:
            case A_CEND:
                if (selected.TextEdit()) { break; }
                selected.HomeEnd(this, action == A_HOME || action == A_CHOME);
                return wxEmptyString;

            case A_IMAGESCP:
            case A_IMAGESCW:
            case A_IMAGESCF: {
                long v = 0.0;
                if (!AnyImagesInSelection()) { return wxEmptyString; }
                if (action == A_IMAGESCW) {
                    v = wxGetNumberFromUser(_("Please enter the new image width:"), _("Width"),
                                            _("Image Resize"), 500, 10, 4000, sys->frame);
                } else {
                    v = wxGetNumberFromUser(
                        _("Please enter the percentage you want the image scaled by:"), "%",
                        _("Image Resize"), 50, 5, 400, sys->frame);
                }
                if (v < 0) { return wxEmptyString; }
                ReplaceSelectedImages([&](const Image &image) -> Image * {
                    if (action == A_IMAGESCF) {
                        return NewImage(image.display_scale / (v / 100.0),
                                        vector<uint8_t>(image.data), image.type);
                    }
                    if (action == A_IMAGESCW && image.pixel_width == 0) { return nullptr; }
                    auto scale = action == A_IMAGESCW
                                     ? static_cast<double>(v) / image.pixel_width
                                     : v / 100.0;
                    return NewImage(image.display_scale, image.RescaledData(scale), image.type);
                });
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_IMAGESCN: {
                ReplaceSelectedImages([](const Image &image) {
                    return NewImage(sys->frame->FromDIP(1.0), vector<uint8_t>(image.data),
                                    image.type);
                });
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_IMAGESVA: {
                set<Image *> imagestosave;
                loopallcellssel(c, true) if (auto *image = c->text.image) {
                    imagestosave.insert(image);
                }
                if (imagestosave.empty()) { return _("There are no images in the selection."); }
                wxString filename = ::wxFileSelector(
                    _("Choose image file to save:"), "", "", "",
                    _("PNG file (*.png)|*.png|JPEG file (*.jpg)|*.jpg|All Files (*.*)|*.*"),
                    wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
                if (filename.empty()) { return _("Save cancelled."); }
                auto i = 0;
                for (auto *image : imagestosave) {
                    wxFileName fn(filename);
                    wxString finalfilename = fn.GetPathWithSep() + fn.GetName() +
                                             (i == 0 ? wxString() : wxString::Format("%d", i)) +
                                             image->GetFileExtension();
                    wxFFileOutputStream os(finalfilename, "w+b");
                    if (!os.IsOk()) {
                        wxMessageBox(
                            _("Error writing image file! (try saving under new filename)."),
                            finalfilename.wx_str(), wxOK, sys->frame);
                        return _("Error writing to file.");
                    }
                    os.Write(image->data.data(), image->data.size());
                    i++;
                }
                return _("Image(s) have been saved to disk.");
            }

            case A_IMAGECOPY:
                loopallcellssel(c, true) if (auto *image = c->text.image) {
                    if (!wxTheClipboard->Open()) { return wxEmptyString; }
                    wxTheClipboard->SetData(new wxBitmapDataObject(
                        ConvertBufferToWxBitmap(image->data, imagetypes.at(image->type).first)));
                    wxTheClipboard->Close();
                    return _("Bitmap copied to clipboard");
                }
                return _("There are no images in the selection.");

            case A_SAVE_AS_JPEG:
            case A_SAVE_AS_PNG: {
                char newtype = action == A_SAVE_AS_JPEG ? 'J' : 'I';
                bool converted = false;
                ReplaceSelectedImages([&](const Image &image) -> Image * {
                    if (image.type == newtype) { return nullptr; }
                    converted = true;
                    return NewImage(image.display_scale, image.ConvertedData(newtype), newtype);
                });
                if (!converted) { return _("No image found to convert"); }
                UpdateLayout();
                canvas->Refresh();
                return action == A_SAVE_AS_JPEG
                           ? _("Images in selected cells have been converted to JPEG format.")
                           : _("Images in selected cells have been converted to PNG format.");
            }

            case A_BROWSE: {
                // The link at the cursor, or the first one of the selected cell, if any.
                if (auto *c = selected.GetCell(); c != nullptr && !c->text.links.empty()) {
                    auto *k = selected.TextEdit() ? c->text.links.At(selected.cursor)
                                                  : &c->text.links.v[0];
                    if (k == nullptr && selected.cursor > 0) {
                        k = c->text.links.At(selected.cursor - 1);
                    }
                    if (k != nullptr) { return FollowLink(c, *k); }
                }
                wxString returnmessage = "";
                int counter = 0;
                loopallcellssel(c, false) {
                    if (counter >= g_max_launches) {
                        returnmessage = _("Maximum number of launches reached.");
                        break;
                    }
                    if (!wxLaunchDefaultBrowser(c->text.ToText(0, selected, A_EXPTEXT))) {
                        returnmessage = _("The browser could not open at least one link.");
                    } else {
                        counter++;
                    }
                }
                return returnmessage;
            }

            case A_BROWSEF: {
                wxString returnmessage = "";
                int counter = 0;
                loopallcellssel(c, false) {
                    if (counter >= g_max_launches) {
                        returnmessage = _("Maximum number of launches reached.");
                        break;
                    }
                    if (!OpenFile(c->text.ToText(0, selected, A_EXPTEXT))) {
                        returnmessage = _("At least one file could not be opened.");
                    } else {
                        counter++;
                    }
                }
                return returnmessage;
            }

            case A_TAGADD: {
                loopallcellssel(c, false) {
                    if (c->text.t.IsEmpty()) { continue; }
                    tags[c->text.t] = {c->cellcolor, c->textcolor};
                }
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_TAGREMOVE: {
                loopallcellssel(c, false) tags.erase(c->text.t);
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_TRANSPOSE: {
                if (selected.Thin()) { return NoThin(); }
                auto *ac = selected.grid->cell;
                ac->AddUndo(this);
                if (selected.IsAll()) {
                    ac->grid->Transpose();
                    SetSelect(ac->parent != nullptr ? ac->parent->grid->FindCell(ac) : Selection());
                } else {
                    loopallcellssel(c, false) if (c->grid) { c->grid->Transpose(); }
                }
                ac->ResetChildren();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }
        }

        if (cell != nullptr || (cell == nullptr && selected.IsAll())) {
            auto *ac = cell != nullptr ? cell : selected.grid->cell;
            switch (action) {
                case A_HIFY:
                    if (!ac->grid) { return NoGrid(); }
                    if (!ac->grid->IsTable()) {
                        return _(
                            "Selected grid is not a table: cells must not already have sub-grids.");
                    }
                    ac->AddUndo(this);
                    ac->grid->Hierarchify(this);
                    ac->ResetChildren();
                    selected = Selection();
                    begindrag = Selection();
                    UpdateLayout();
                    canvas->Refresh();
                    return wxEmptyString;

                case A_FLATTEN: {
                    if (!ac->grid) { return NoGrid(); }
                    ac->AddUndo(this);
                    int maxdepth = 0;
                    int leaves = 0;
                    ac->MaxDepthLeaves(0, maxdepth, leaves);
                    auto g = make_shared<Grid>(maxdepth, leaves);
                    g->InitCells();
                    ac->grid->Flatten(0, 0, g.get());
                    ac->grid = g;
                    g->ReParent(ac);
                    ac->ResetChildren();
                    selected = Selection();
                    begindrag = Selection();
                    UpdateLayout();
                    canvas->Refresh();
                    return wxEmptyString;
                }
            }
        }

        if (cell == nullptr) { return OneCell(); }

        switch (action) {
            case A_NEXT: selected.Next(this, false); return wxEmptyString;
            case A_PREV: selected.Next(this, true); return wxEmptyString;

            case A_LINK:
            case A_LINKIMG:
            case A_LINKREV:
            case A_LINKIMGREV: {
                if ((action == A_LINK || action == A_LINKREV) && cell->text.t.IsEmpty()) {
                    return _("No text in this cell.");
                }
                if ((action == A_LINKIMG || action == A_LINKIMGREV) &&
                    cell->text.image == nullptr) {
                    return _("No image in this cell.");
                }
                bool t1 = false;
                bool t2 = false;
                auto *link = root->FindLink(selected, cell, nullptr, t1, t2,
                                            action == A_LINK || action == A_LINKIMG,
                                            action == A_LINKIMG || action == A_LINKIMGREV);
                if (link == nullptr || link->parent == nullptr) {
                    return _("No matching cell found!");
                }
                SetSelect(link->parent->grid->FindCell(link));
                ScrollOrZoom(true);
                return wxEmptyString;
            }

            case A_HYPERLINK: {
                auto &text = cell->text;
                auto len = static_cast<int>(text.t.Len());
                auto from = 0;
                auto to = 0;
                auto *old = SelectedLink(cell, from, to);
                if (from >= to) { return _("No text in this cell."); }
                // What the text links to already, or else the text itself, as a URL or as the
                // text of the nearest cell with it.
                auto selection = text.t.Mid(from, to - from);
                auto url = Text::HasURLScheme(selection);
                auto internal =
                    old ? old->internal
                        : !url && FormulaEvaluator::LinkTarget(cell, selection) != nullptr;
                wxDialog dlg(sys->frame, wxID_ANY, _("Link"), wxDefaultPosition, wxDefaultSize,
                             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
                auto *sizer = new wxBoxSizer(wxVERTICAL);
                const wxString kinds[] = {_("Web page or file"), _("Nearest cell with this text")};
                wxRadioBox kind(&dlg, wxID_ANY, _("Link to"), wxDefaultPosition, wxDefaultSize, 2,
                                kinds, 1);
                kind.SetSelection(internal ? 1 : 0);
                sizer->Add(&kind, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
                wxTextCtrl target(&dlg, wxID_ANY,
                                  old               ? old->target
                                  : url || internal ? selection
                                                    : wxString(),
                                  wxDefaultPosition, wxSize(dlg.FromDIP(400), -1));
                target.SetHint(_("Empty to remove the link"));
                sizer->Add(&target, 0, wxEXPAND | wxALL, 10);
                sizer->Add(dlg.CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER | wxBOTTOM,
                           10);
                dlg.SetSizerAndFit(sizer);
                target.SetFocus();
                target.SelectAll();
                if (dlg.ShowModal() != wxID_OK) { return wxEmptyString; }
                auto value = target.GetValue().Trim(true).Trim(false);
                internal = kind.GetSelection() == 1;
                cell->AddUndo(this);
                text.links.Set(from, to, len, internal, value);
                text.CheckRuns();
                cell->ResetLayout();
                UpdateLayout();
                canvas->Refresh();
                return !internal || value.IsEmpty() || FormulaEvaluator::LinkTarget(cell, value)
                           ? wxString()
                           : _("No matching cell found!");
            }

            case A_COLCELL: sys->customcolor = cell->cellcolor; return wxEmptyString;

            case A_HSWAP: {
                auto *pp = cell->parent->parent;
                if (pp == nullptr) { return _("Cannot move this cell up in the hierarchy."); }
                // Moving an empty cell up can leave an empty one in its place, which would be found
                // and moved up again, forever.
                if (cell->text.t.IsEmpty()) { return _("Can only swap a cell that has text."); }
                if (pp->grid->xs != 1 && pp->grid->ys != 1) {
                    return _("Can only move this cell into a Nx1 or 1xN grid.");
                }
                if (cell->parent->grid->xs != 1 && cell->parent->grid->ys != 1) {
                    return _("Can only move this cell from a Nx1 or 1xN grid.");
                }
                pp->AddUndo(this);
                SetSelect(pp->grid->HierarchySwap(cell->text.t));
                pp->ResetChildren();
                pp->ResetLayout();
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_FILTERBYCELLBG:
                ApplyFilter([&] {
                    loopallcells(ci) ci->text.filteredraw = ci->cellcolor != cell->cellcolor;
                });
                return wxEmptyString;

            case A_FILTERBYSTYLE:
                ApplyFilter([&] {
                    loopallcells(ci) ci->text.filteredraw =
                        ci->text.stylebits != cell->text.stylebits;
                });
                return wxEmptyString;

            case A_FILTERBYTEXT:
                ApplyFilter(
                    [&] { loopallcells(ci) ci->text.filteredraw = ci->text.t != cell->text.t; });
                return wxEmptyString;

            case A_FILTERNOTE:
                ApplyFilter([&] { loopallcells(ci) ci->text.filteredraw = ci->note.IsEmpty(); });
                return wxEmptyString;

            case A_FILTERMATCHNEXT: {
                bool lastsel = true;
                Cell *next = root->FindNextFilterMatch(nullptr, selected.GetCell(), lastsel);
                if (next == nullptr) { return _("No matches for filter."); }
                if (next->parent != nullptr) { SetSelect(next->parent->grid->FindCell(next)); }
                canvas->SetFocus();
                ScrollOrZoom(true);
                return wxEmptyString;
            }
        }

        if (!selected.TextEdit()) { return _("only works in cell text mode"); }

        switch (action) {
            case A_SPLITCELL: {
                cell->AddUndo(this);
                auto part = [&](int from, int to) {
                    auto c = make_unique<Cell>(nullptr, cell);
                    c->text = cell->text;
                    c->text.cell = c.get();
                    c->text.RemoveText(to, static_cast<int>(c->text.t.Len()) - to);
                    c->text.RemoveText(0, from);
                    return c;
                };
                auto first = part(0, selected.cursor);
                auto second = part(selected.cursor, static_cast<int>(cell->text.t.Len()));
                second->text.image = nullptr;
                // An existing subgrid goes next to the one holding the split text.
                auto existing = cell->grid;
                cell->Clear();
                auto *target = cell;
                if (existing) {
                    target = cell->AddGrid(2, 1)->C(0, 0).get();
                    auto *holder = cell->grid->C(1, 0).get();
                    holder->grid = existing;
                    existing->ReParent(holder);
                }
                auto *g = target->AddGrid(2, 1);
                g->C(0, 0).reset(first.release()->SetParent(target));
                g->C(1, 0).reset(second.release()->SetParent(target));
                cell->ResetChildren();
                SetSelect(Selection(target->grid, 1, 0, 1, 1));
                selected.EnterEdit(this);
                UpdateLayout();
                ScrollIfSelectionOutOfView();
                canvas->Refresh();
                return wxEmptyString;
            }

            case A_CANCELEDIT:
                if (LastUndoSameCellTextEdit(cell)) {
                    Undo(undolist, redolist);
                } else {
                    UpdateLayout();
                    canvas->Refresh();
                }
                selected.ExitEdit(this);
                return wxEmptyString;

            case A_BACKSPACE_WORD:
                if (selected.cursorend == 0) { return wxEmptyString; }
                cell->AddUndo(this, true);
                cell->text.BackspaceWord(selected);
                UpdateLayout();
                canvas->Refresh();
                ZoomOutIfNoGrid();
                return wxEmptyString;

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
                ScrollIfSelectionOutOfView();
                canvas->Refresh();
                return wxEmptyString;
            }
            default: return _("Internal error: unimplemented operation!");
        }
    }

    wxString SearchNext(bool focusmatch, bool jump, bool reverse) {
        if (root == nullptr || currentdrawroot == nullptr) {
            return wxEmptyString;  // fix crash when opening new doc
        }
        if (sys->searchstring.IsEmpty()) { return _("No search string."); }
        bool lastsel = true;
        Cell *searchroot = sys->restrictview ? currentdrawroot : root.get();
        Cell *next = searchroot->FindNextSearchMatchStart(
            sys->searchstring, nullptr, selected.GetCell(), lastsel, reverse, sys->restrictview);
        if (next == nullptr || next->parent == nullptr) { return _("No matches for search."); }
        if (!jump) { return wxEmptyString; }
        SetSelect(next->parent->grid->FindCell(next));
        if (focusmatch) { canvas->SetFocus(); }
        ScrollOrZoom(true);
        return wxEmptyString;
    }

    wxString layrender(int ds, bool vert, bool toggle = false, bool noset = false) {
        if (selected.Thin()) { return NoThin(); }
        // With Shift held, only the selected cells, not the ones inside.
        auto recursive = !wxGetKeyState(WXK_SHIFT);
        selected.grid->cell->AddUndo(this, selected, recursive);
        bool v = toggle ? !selected.GetFirst()->verticaltextandgrid : vert;
        if (ds >= 0 && selected.IsAll()) { selected.grid->cell->drawstyle = ds; }
        selected.grid->SetGridTextLayout(ds, v, noset, selected, recursive);
        selected.grid->ResetResized(selected, recursive);
        UpdateLayout();
        canvas->Refresh();
        return wxEmptyString;
    }

    // Once the events are processed: a cell whose editing ended becomes a formula if its text
    // is one now, and data if not anymore, a formula cell shows its formula while it's edited,
    // and the formulas are recalculated after changes.
    void OnIdle() {
        auto *c = selected.TextEdit() ? selected.GetCell() : nullptr;
        if (c != editcell) {
            if (editcell != nullptr && editcell == textedited) {
                CollectCells(root.get());
                if (std::ranges::find(itercells, editcell) != itercells.end()) {
                    EndFormulaEdit(editcell);
                }
                textedited = nullptr;
            } else if (editcell != nullptr) {
                formulasdirty = true;  // an edited formula shows its result again
            }
            editcell = c;
            if (c != nullptr) { BeginFormulaEdit(c); }
        }
        if (formulasdirty) { RecalcFormulas(); }
    }

    // Shows the formula instead of the result, unless undo brought back a formula being typed.
    void BeginFormulaEdit(Cell *c) {
        if (c->celltype != CT_FORMULA || !c->formula || c->text.t.StartsWith("=")) { return; }
        c->text.SetText(*c->formula);
        int len = c->text.t.Len();
        selected.cursor = selected.cursor != selected.cursorend ? 0 : len;
        selected.cursorend = len;
        c->ResetLayout();
        UpdateLayout();
        canvas->Refresh();
    }

    void EndFormulaEdit(Cell *c, bool undo = true) {
        if (c->celltype != CT_DATA && c->celltype != CT_FORMULA) { return; }
        bool isformula = Cell::IsFormulaText(c->text.t);
        if (isformula ? c->formula && *c->formula == c->text.t : c->celltype == CT_DATA) {
            formulasdirty = true;  // unchanged, a formula shows its result again
            return;
        }
        if (undo) { AddUndo(c, false); }  // undone together with the typing
        c->celltype = isformula ? CT_FORMULA : CT_DATA;
        c->formula = nullptr;
        c->SyncFormula();
    }

    // Recalculates the results of all formulas, and lays out those that changed.
    void RecalcFormulas() {
        formulasdirty = false;
        if (!sys->formulasseen) { return; }
        FormulaEvaluator fe {editcell, vars};
        loopallcells(c) {
            if (c->celltype == CT_FORMULA) {
                c->SyncFormula();
                fe.Update(c);
            }
        }
        if (fe.changed.empty()) { return; }
        for (auto *c : fe.changed) { c->ResetLayout(); }
        UpdateLayout();
        canvas->Refresh();
    }

    // From the formula bar: makes the selected cell a formula if s starts with "=", and data with
    // text s otherwise.
    wxString SetFormulaBarText(const wxString &s) {
        auto *c = selected.GetCell();
        if (c == nullptr) { return OneCell(); }
        selected.ExitEdit(this);
        editcell = nullptr;
        c->AddUndo(this);
        c->text.SetText(s);
        if (Cell::IsFormulaText(s)) {
            c->celltype = CT_FORMULA;
        } else if (c->celltype == CT_FORMULA) {
            c->celltype = CT_DATA;
        }
        c->formula = nullptr;
        c->SyncFormula();
        RecalcFormulas();
        UpdateLayout();
        canvas->Refresh();
        return wxEmptyString;
    }

    void ZoomOutIfNoGrid() {
        if (!WalkPath(drawpath)->grid) { Zoom(-1); }
    }

    // Fills a selection of several cells with the copied cell or line of text, or repeats the
    // copied cells over it if they fit a whole number of times. False if they don't.
    bool PasteFill(int action) {
        wxString text;
        auto opened = wxTheClipboard->Open();
        if (opened) {
            if (wxTextDataObject tdo; wxTheClipboard->GetData(tdo)) { text = tdo.GetText(); }
            wxTheClipboard->Close();
        }
        auto &g = selected.grid;
        if (action == wxID_PASTE && sys->cellclipboard &&
            (!opened || (!text.IsEmpty() && text == sys->clipboardcopy))) {
            const Cell *cc = sys->cellclipboard.get();
            Grid *block = cc->grid && !cc->HasText() ? cc->grid.get() : nullptr;
            int bx = block ? block->xs : 1;
            int by = block ? block->ys : 1;
            if (selected.xs % bx || selected.ys % by) { return false; }
            g->cell->AddUndo(this);
            loop(y, selected.ys) loop(x, selected.xs) {
                const Cell *src = block ? block->C(x % bx, y % by).get() : cc;
                g->C(selected.x + x, selected.y + y) = src->Clone(g->cell);
            }
            loop(x, selected.xs) {
                auto &width = g->colwidths[selected.x + x];
                width = max(width, block ? block->colwidths[x % bx] : sys->cellclipboardcolwidth);
            }
        } else {
            const wxArrayString &lines = wxStringTokenize(text, LINE_DELIMITERS);
            if (action == A_PASTETSV || lines.size() != 1 || treesheets::System::IsTSV(lines)) {
                return false;
            }
            g->cell->AddUndo(this, selected, false);
            loop(y, selected.ys) loop(x, selected.xs) {
                auto *c = g->C(selected.x + x, selected.y + y).get();
                c->text.SetText(lines[0]);
                EndFormulaEdit(c, false);  // as if typed, so "=..." becomes a formula
                c->Reset();
            }
        }
        return true;
    }

    void PasteSingleText(Cell *c, wxString s) {
        Text::FilterControls(s);
        c->text.Insert(this, s, selected, false);
    }

    // Polymorphism with wxDataObjectSimple does not work on Windows; bitmap format seems to not be
    // recognized.

    void PasteOrDrop(const wxFileDataObject &filedataobject) {
        const wxArrayString &filenames = filedataobject.GetFilenames();
        if (filenames.size() != 1) {
            sys->frame->SetStatus(_("Can paste or drop only exactly one file."));
            return;
        }
        Cell *cell = selected.ThinExpand(this);
        const wxString &filename = filenames[0];
        wxFFileInputStream fileinputstream(filename);
        if (fileinputstream.IsOk()) {
            char buffer[4];
            fileinputstream.Read(buffer, 4);
            if (strncmp(buffer, "TSFF", 4) == 0) {
                ThreeChoiceDialog askuser(
                    sys->frame, filename,
                    _("It seems that you are about to paste or drop a TreeSheets file. "
                      "What would you like to do?"),
                    _("Open TreeSheets file"), _("Paste file path"), _("Cancel"));
                switch (askuser.Run()) {
                    case 0: sys->frame->SetStatus(sys->LoadDB(filename));
                    case 2: return;
                    default:
                    case 1:;
                }
            }
        }
        if (cell == nullptr) { return; }
        cell->AddUndo(this);
        if (!LoadImageIntoCell(filename, cell, sys->frame->FromDIP(1.0))) {
            PasteSingleText(cell, filename);
        }
    }

    void PasteOrDrop(const wxTextDataObject &textdataobject, int pastemode = PASTE_FIT,
                     bool astext = false, bool astsv = false) {
        if (textdataobject.GetText() != wxEmptyString) {
            Cell *cell = selected.ThinExpand(this);
            auto text = textdataobject.GetText();
            // The cells copied last, with their styles, unless only their text is wanted.
            if (!astext && sys->clipboardcopy == text && sys->cellclipboard) {
                cell->Paste(this, sys->cellclipboard.get(), selected, pastemode,
                            sys->cellclipboardcolwidth);
            } else {
                const wxArrayString &lines = wxStringTokenize(text, LINE_DELIMITERS);
                astsv = astsv || ((lines.size() > 1 || !selected.TextEdit()) &&
                                  treesheets::System::IsTSV(lines));
                if (lines.size() == 1 && !astsv) {
                    cell->AddUndo(this);
                    cell->ResetLayout();
                    PasteSingleText(cell, lines[0]);
                    textedited = cell;  // as if typed, so "=..." becomes a formula
                } else if (!lines.empty()) {
                    cell->parent->AddUndo(this);
                    cell->ResetLayout();
                    cell->grid = nullptr;
                    if (astsv) {
                        cell->AddGrid(1, static_cast<int>(lines.size()))->CSVImport(lines, L'\t');
                    } else {
                        sys->FillRows(cell->AddGrid(), lines,
                                      treesheets::System::CountCol(lines[0]), 0, 0);
                    }
                    if (!cell->HasText()) {
                        cell->grid->MergeWithParent(cell->parent->grid, selected, this, pastemode);
                    }
                }
            }
        }
    }

    void PasteOrDrop(const wxBitmapDataObject &bitmapdataobject) {
        if (bitmapdataobject.GetBitmap().GetRefData() != wxNullBitmap.GetRefData()) {
            Cell *cell = selected.ThinExpand(this);
            cell->AddUndo(this);
            auto image = bitmapdataobject.GetBitmap().ConvertToImage();
            bool ispng = sys->defaultimageformat == 0;
            vector<uint8_t> buffer =
                ConvertWxImageToBuffer(image, ispng ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_JPEG);
            SetImageBM(cell, std::move(buffer), ispng ? 'I' : 'J', sys->frame->FromDIP(1.0));
            cell->Reset();
        }
    }

    #ifdef __WXMAC__
    void PasteOrDrop(const wxImage &img, double scale) {
        Cell *cell = selected.ThinExpand(this);
        cell->AddUndo(this);
        bool ispng = sys->defaultimageformat == 0;
        vector<uint8_t> buffer =
            ConvertWxImageToBuffer(img, ispng ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_JPEG);
        SetImageBM(cell, std::move(buffer), ispng ? 'I' : 'J', scale);
        cell->Reset();
    }
    #endif

    wxString Sort(bool descending) {
        if (selected.xs != 1 && selected.ys <= 1) {
            return _(
                "Can't sort: make a 1xN selection to indicate what column to sort on, and what rows to affect");
        }
        selected.grid->cell->AddUndo(this, Selection(), false);
        selected.grid->Sort(selected, descending);
        UpdateLayout();
        canvas->Refresh();
        return wxEmptyString;
    }

    void DelRowCol(int &v, int e, int gvs, int dec, int dx, int dy, int nxs, int nys) {
        if (v != e) {
            selected.grid->cell->AddUndo(this, Selection(), false);
            if (gvs == 1) {
                selected.grid->DelSelf(this, selected);
            } else {
                selected.grid->DeleteCells(dx, dy, nxs, nys);
                v -= dec;
            }
            UpdateLayout();
            canvas->Refresh();
        }
    }

    static void CreatePath(Cell *c, vector<Selection> &path) {
        path.clear();
        while (c->parent != nullptr) {
            const Selection &s = c->parent->grid->FindCell(c);
            ASSERT(s.grid != nullptr);
            path.push_back(s);
            c = c->parent;
        }
    }

    Cell *WalkPath(vector<Selection> &path) const {
        Cell *c = root.get();
        loopvrev(i, path) {
            Selection &s = path[i];
            Grid *g = c->grid.get();
            if (g == nullptr) { return c; }
            ASSERT(g && s.x < g->xs && s.y < g->ys);
            c = g->C(s.x, s.y).get();
        }
        return c;
    }

    bool LastUndoSameCellTextEdit(Cell *c) {
        // hacky way to detect word boundaries to stop coalescing, but works, and
        // not a big deal if selected is not actually related to this cell
        return !undolist.empty() && undolist.back()->textedit &&
               undolist.back()->cloned_from == (uintptr_t)c && c->parent != nullptr &&
               undolist.size() != undolistsizeatfullsave &&
               undolist.back()->sel.EqLoc(c->parent->grid->FindCell(c)) &&
               (!c->text.t.EndsWith(" ") || c->text.t.Len() != selected.cursor);
    }

    // With a scope, a selection in the grid of c, the snapshot is partial, for an action that
    // changes nothing but c, its grid (the order of its cells included) and the cells of scope,
    // and with recursive what is inside them: the other cells may be moved or deleted, and the
    // grids of the cells of scope dropped, but not changed. What isn't copied is shared with the
    // document, and taken back from it when this is undone.
    void AddUndo(Cell *c, bool newgeneration = true, bool textedit = false,
                 const Selection *scope = nullptr, bool recursive = false) {
        FinishUndo();
        EndFoldPeek();
        formulasdirty = true;
        pendingundoitem = nullptr;  // it may be freed, see AddResizeUndo()
        if (textedit) { textedited = c; }
        redolist.clear();
        lastmodsinceautosave = wxGetLocalTime();
        if (!modified) {
            modified = true;
            UpdateFileName();
        }
        if (textedit && LastUndoSameCellTextEdit(c)) { return; }
        auto ui = make_unique<UndoItem>();
        ui->textedit = textedit;
        if (textedit) {
            // Keep a cell snapshot so image lifetime accounting also sees its text image.
            // Text edits never modify the nested grid, which can be much larger than the text.
            ui->clone = make_unique<Cell>(nullptr, nullptr, c->celltype);
            ui->clone->text = c->text;
            ui->clone->text.cell = ui->clone.get();
            ui->clone->formula = c->formula;  // also restored, typing over a formula clears it
            ui->estimated_size = ui->clone->EstimatedMemoryUse();
        } else if (scope != nullptr && c->grid) {
            auto *g = c->grid.get();
            ui->partial = true;
            ui->clone = c->Clone(nullptr, true);
            ui->clone->grid = make_shared<Grid>(g->xs, g->ys, ui->clone.get());
            g->CloneFields(ui->clone->grid.get());
            ui->pending.resize(g->cells.size());
            ui->estimated_size = sizeof(Cell) + c->text.EstimatedMemoryUse() + sizeof(Grid);
            foreachcellingrid(gc, g) {
                if (x >= scope->x && x < scope->x + scope->xs && y >= scope->y &&
                    y < scope->y + scope->ys) {
                    auto &k = ui->clone->grid->C(x, y);
                    k = gc->Clone(ui->clone.get(), !recursive);
                    ui->estimated_size += recursive ? k->EstimatedMemoryUse()
                                                    : sizeof(Cell) + k->text.EstimatedMemoryUse();
                } else {
                    ui->pending[x + y * g->xs] = gc.get();
                    gc->reusedbyundo = true;
                }
            }
            pendingundo.push_back(ui.get());
        } else {
            ui->clone = c->Clone(nullptr);
            ui->estimated_size = ui->clone->EstimatedMemoryUse();
        }
        ui->sel = selected;
        ui->cloned_from = (uintptr_t)c;
        if (undogroup == 2) { newgeneration = false; }
        if (undogroup == 1) { undogroup = 2; }
        if (!undolist.empty()) {
            ui->generation = undolist.back()->generation + (newgeneration ? 1 : 0);
        }
        CreatePath(c, ui->path);
        if (selected.grid != nullptr) { CreatePath(selected.grid->cell, ui->selpath); }
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

    // The partial snapshots whose left out cells are still those of their document, see
    // FinishUndo().
    static inline vector<UndoItem *> pendingundo;

    // The cells that a partial snapshot leaves out may be deleted by the action it is for: then
    // they are copied before they are gone, as they weren't changed.
    static void RescueForUndo(Cell *c) {
        for (auto *ui : pendingundo) {
            if (ui->pendingpos.empty()) {
                loopv(i, ui->pending) if (ui->pending[i]) { ui->pendingpos[ui->pending[i]] = i; }
            }
            if (auto it = ui->pendingpos.find(c); it != ui->pendingpos.end()) {
                ui->clone->grid->cells[it->second] = c->Clone(ui->clone.get());
                ui->pending[it->second] = nullptr;
                return;
            }
        }
    }

    static void ForgetPendingUndo(UndoItem *ui) {
        if (std::erase(pendingundo, ui) == 0) { return; }
        for (auto *c : ui->pending) {
            if (c != nullptr) { c->reusedbyundo = false; }
        }
        ui->pending.clear();
        ui->pendingpos.clear();
    }

    // Once the action of a partial snapshot is done, before anything else can change the
    // document, what it shares with the document is looked up by position in the grid of the
    // cell it is for. What isn't in that grid anymore is copied, unless the action deleted it.
    void FinishUndo() {
        if (pendingundo.empty()) { return; }
        for (auto &ui : undolist) {
            if (std::ranges::find(pendingundo, ui.get()) == pendingundo.end()) { continue; }
            auto *g = WalkPath(ui->path)->grid.get();
            unordered_map<void *, int> pos;
            if (g != nullptr) {
                loopv(j, g->cells) {
                    pos[g->cells[j].get()] = j;
                    if (g->cells[j]->grid) { pos[g->cells[j]->grid.get()] = j; }
                }
            }
            auto *k = ui->clone.get();
            ui->reuse.assign(k->grid->cells.size(), -1);
            loopv(i, ui->reuse) {
                auto &kc = k->grid->cells[i];
                if (auto *c = ui->pending[i]) {
                    if (auto it = pos.find(c); it != pos.end()) {
                        ui->reuse[i] = it->second;
                    } else {
                        kc = c->Clone(k);
                    }
                } else if (kc->grid) {
                    if (auto it = pos.find(kc->grid.get()); it != pos.end()) {
                        ui->reuse[i] = it->second;
                        kc->grid = nullptr;
                    } else if (kc->grid.use_count() > 1) {
                        kc = kc->Clone(k);
                    } else {
                        Adopt(kc.get());
                    }
                }
            }
            ForgetPendingUndo(ui.get());
            ui->estimated_size = k->EstimatedMemoryUse() + ui->reuse.size() * sizeof(int);
        }
    }

    // Makes c the cell of the grid it got from another cell.
    static void Adopt(Cell *c) {
        c->grid->cell = c;
        for (auto &gc : c->grid->cells) gc->parent = c;
    }

    // Moves what the partial snapshot of ui leaves out from the cell c it is about to replace into
    // it, and leaves in ui->reuse where it is in c, for when c replaces it again.
    static void TakeBackReused(UndoItem &ui, Cell *c) {
        auto *k = ui.clone.get();
        vector<int> back(c->grid ? c->grid->cells.size() : 0, -1);
        loopv(i, ui.reuse) if (auto j = ui.reuse[i]; j >= 0) {
            auto &kc = k->grid->cells[i];
            auto &cc = c->grid->cells[j];
            if (kc) {
                kc->grid = std::move(cc->grid);
                Adopt(kc.get());
                // Under a cell that may be tiny now or no longer, see Cell::Layout().
                kc->grid->ResetChildren();
            } else {
                kc = std::move(cc);
                kc->parent = k;
                // Its layout holds unless it gets another column width.
                if (c->grid->colwidths[j % c->grid->xs] != k->grid->colwidths[i % k->grid->xs]) {
                    kc->Reset();
                }
            }
            back[j] = i;
        }
        ui.reuse = std::move(back);
    }

    void Undo(vector<unique_ptr<UndoItem>> &fromlist, vector<unique_ptr<UndoItem>> &tolist,
              bool redo = false) {
        FinishUndo();
        for (bool next = true; next; ) {
            UndoEach(fromlist, tolist, redo);
            next = !fromlist.empty() && !tolist.empty() &&
                   fromlist.back()->generation == tolist.back()->generation;
        }
        if (selected.grid != nullptr) {
            ScrollOrZoom();
        } else {
            UpdateLayout();
            canvas->Refresh();
        }
        UpdateFileName();
    }

    void UndoEach(vector<unique_ptr<UndoItem>> &fromlist, vector<unique_ptr<UndoItem>> &tolist,
                  bool redo = false) {
        auto beforesel = selected;
        vector<Selection> beforepath;
        if (beforesel.grid) {
            CreatePath(beforesel.grid->cell, beforepath);
        }

        auto ui = std::move(fromlist.back());
        fromlist.pop_back();

        Cell *c = WalkPath(ui->path);

        formulasdirty = true;
        if (ui->textedit) {
            std::swap(ui->clone->celltype, c->celltype);
            std::swap(ui->clone->formula, c->formula);
            std::swap(ui->clone->text, c->text);
            textedited = c;  // as if typed, also when its editing ends
            c->text.cell = c;
            ui->clone->text.cell = ui->clone.get();
        } else {
            if (ui->partial) { TakeBackReused(*ui, c); }
            if (c->parent != nullptr && c->parent->grid) {
                Grid *g = c->parent->grid.get();
                Selection s = g->FindCell(c);
                std::swap(ui->clone, g->C(s.x, s.y));
                c = g->C(s.x, s.y).get();
                c->parent = ui->clone->parent;
            } else {
                std::swap(ui->clone, root);
                c = root.get();
                c->parent = nullptr;
            }
        }
        c->ResetLayout();

        SetSelect(ui->sel);
        if (selected.grid != nullptr) { selected.grid = WalkPath(ui->selpath)->grid; }

        begindrag = selected;
        ui->sel = beforesel;
        ui->selpath = std::move(beforepath);
        tolist.push_back(std::move(ui));

        if (undolistsizeatfullsave > static_cast<int>(undolist.size())) {
            undolistsizeatfullsave = -1;
        }
        modified = undolistsizeatfullsave != static_cast<int>(undolist.size());
    }

    void ColorChange(int which, int idx) {
        if (selected.grid == nullptr) { return; }
        if (idx < 0 || idx >= static_cast<int>(celltextcolors.size())) { return; }
        auto col = idx == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[idx];
        switch (which) {
            case A_CELLCOLOR: sys->lastcellcolor = col; break;
            case A_TEXTCOLOR: sys->lasttextcolor = col; break;
            case A_BORDCOLOR: sys->lastbordcolor = col; break;
        }
        // With Shift held, also color everything inside the selected cells.
        auto recursive = wxGetKeyState(WXK_SHIFT);
        ForEachSelection(
            [&] { selected.grid->ColorChange(this, which, col, selected, recursive); });
    }

    static Image *NewImage(double scale, vector<uint8_t> &&data, char type) {
        return sys->imagelist[sys->AddImageToList(scale, std::move(data), type)].get();
    }

    // Whether all the text a style command would change has the style: the selected range while
    // editing text, otherwise the text of all selected cells.
    bool SelectionHasStyle(int bit) {
        if (selected.grid == nullptr || selected.Thin()) { return false; }
        if (selected.TextEdit() && selected.cursor != selected.cursorend) {
            auto *c = selected.GetCell();
            return c != nullptr && c->text.HasStyle(bit, selected.cursor, selected.cursorend);
        }
        return selected.grid->AllHaveStyle(selected, bit);
    }

    bool SelectionHasTextAlign(int align) {
        if (selected.grid == nullptr || selected.Thin()) { return false; }
        return selected.grid->AllHaveTextAlign(selected, align);
    }

    bool SelectionHasVertAlign(int align) {
        if (selected.grid == nullptr || selected.Thin()) { return false; }
        return selected.grid->AllHaveVertAlign(selected, align);
    }

    // The text that the link dialog links in cell c, the selected cell: the selected text, all of
    // it, or the link at the cursor. Returns the first link in it, if any.
    const TextLink<wxString> *SelectedLink(Cell *c, int &from, int &to) const {
        auto &links = c->text.links;
        from = selected.TextEdit() ? selected.cursor : 0;
        to = selected.TextEdit() ? selected.cursorend : static_cast<int>(c->text.t.Len());
        if (from == to) {
            auto *k = links.At(from);
            if (k == nullptr && from > 0) { k = links.At(from - 1); }
            if (k != nullptr) {
                from = k->start;
                to = k->end();
            }
        }
        for (const auto &k : links.v) {
            if (k.end() > from && k.start < to) { return &k; }
        }
        return nullptr;
    }

    bool SelectionHasLink() {
        auto *c = selected.GetCell();
        auto from = 0;
        auto to = 0;
        return c != nullptr && SelectedLink(c, from, to) != nullptr;
    }

    bool SelectionHasNote() {
        auto *c = selected.GetCell();
        return c != nullptr && !c->note.IsEmpty();
    }

    // The cell type a Program > Mark as menu item marks cells as.
    static int MarkCellType(int action) {
        switch (action) {
            case A_MARKVARD: return CT_VARD;
            case A_MARKVARU: return CT_VARU;
            case A_MARKVIEWH: return CT_VIEWH;
            case A_MARKVIEWV: return CT_VIEWV;
            case A_MARKCODE: return CT_CODE;
            case A_MARKFORMULA: return CT_FORMULA;
            default: return CT_DATA;
        }
    }

    bool SelectionHasCellType(int celltype) {
        if (selected.grid == nullptr || selected.Thin()) { return false; }
        return selected.grid->AllHaveCellType(selected, celltype);
    }

    bool AnyImagesInSelection() {
        loopallcellssel(c, true) if (c->text.image != nullptr) { return true; }
        return false;
    }

    // An image is shared by every cell holding the same picture, in this document and in any
    // other open one, so changing one means handing the cells it applies to a new image.
    void ReplaceSelectedImages(const std::function<Image *(const Image &)> &transform) {
        if (!AnyImagesInSelection()) { return; }
        selected.grid->cell->AddUndo(this, selected, true);
        std::map<Image *, Image *> replacements;
        loopallcellssel(c, true) {
            auto *image = c->text.image;
            if (image == nullptr) { continue; }
            auto [it, inserted] = replacements.try_emplace(image, nullptr);
            if (inserted) { it->second = transform(*image); }
            if (it->second != nullptr) {
                c->text.image = it->second;
                c->ResetLayout();
            }
        }
    }

    static void SetImageBM(Cell *c, vector<uint8_t> &&data, char type, double scale) {
        c->text.image = sys->lastimage = NewImage(scale, std::move(data), type);
    }

    static bool LoadImageIntoCell(const wxString &filename, Cell *c, double scale) {
        if (filename.empty()) { return false; }
        auto *pnghandler = wxImage::FindHandler(wxBITMAP_TYPE_PNG);
        auto *jpghandler = wxImage::FindHandler(wxBITMAP_TYPE_JPEG);
        wxImageHandler *activeHandler = nullptr;
        if (pnghandler != nullptr && pnghandler->CanRead(filename)) {
            activeHandler = pnghandler;
        } else if (jpghandler != nullptr && jpghandler->CanRead(filename)) {
            activeHandler = jpghandler;
        }
        if (activeHandler != nullptr) {
            wxFile fn(filename);
            if (!fn.IsOpened()) { return false; }
            vector<uint8_t> buffer(fn.Length());
            fn.Read(buffer.data(), buffer.size());
            SetImageBM(c, std::move(buffer), (activeHandler == pnghandler) ? 'I' : 'J', scale);
        } else {
            wxImage image;
            if (!image.LoadFile(filename)) { return false; }
            auto buffer = ConvertWxImageToBuffer(image, wxBITMAP_TYPE_PNG);
            SetImageBM(c, std::move(buffer), 'I', scale);
        }
        c->Reset();
        return true;
    }

    void ImageChange(const EmbeddedFile &image, double scale) {
        if (selected.grid == nullptr) { return; }
        selected.grid->cell->AddUndo(this, selected, false);
        loopallcellssel(c, false) {
            SetImageBM(c, vector<uint8_t>(image.data, image.data + image.size), 'I', scale);
            c->Reset();
        }
        UpdateLayout();
        canvas->Refresh();
    }

    void RecreateTagMenu(wxMenu &menu) const {
        int i = A_TAGSET;
        for (const auto &[tag, colors] : tags) { menu.Append(i++, tag); }
        if (!tags.empty()) { menu.AppendSeparator(); }
        menu.Append(A_TAGADD, _("&Add Cell Text as Tag"));
        menu.Append(A_TAGREMOVE, _("&Remove Cell Text from Tags"));
    }

    wxString TagSet(int tagno) {
        if (selected.grid == nullptr) { return NoSel(); }
        int i = 0;
        for (auto &[tag, colors] : tags) {
            if (i++ == tagno) {
                selected.grid->cell->AddUndo(this, selected, false);
                loopallcellssel(c, false) {
                    c->text.SetText(tag);
                    c->text.WasEdited();
                    c->ResetLayout();
                }
                selected.ExitEdit(this);
                UpdateLayout();
                canvas->Refresh();
                return wxEmptyString;
            }
        }
        ASSERT(0);
        return wxEmptyString;
    }

    void CollectCells(Cell *c) {
        itercells.clear();
        c->CollectCells(itercells);
    }

    // Derives the displayed `filtered` flag from the raw per-cell `filteredraw` match
    // result. When "show entire row on match" is on, any row containing a match (a cell
    // with filteredraw == false) has its filtered flag cleared for the whole row, so it
    // displays normally rather than tagged as filtered. Recurses into sub-grids so nested
    // tables get the same treatment. When "show sub-grids on match" is on, everything inside
    // a cell displayed normally is displayed normally too (showall). Always deriving from
    // filteredraw (rather than mutating filtered in place) keeps this idempotent, so toggling
    // an option can simply re-run it.
    void RecomputeFilteredDisplay(Grid *g, bool showall = false) {
        for (int y = 0; y < g->ys; y++) {
            bool showrow = showall;
            if (sys->filtershowrows && !showrow) {
                for (int x = 0; x < g->xs; x++) {
                    if (!g->C(x, y)->text.filteredraw) {
                        showrow = true;
                        break;
                    }
                }
            }
            for (int x = 0; x < g->xs; x++) {
                Cell *c = g->C(x, y).get();
                c->text.filtered = showrow ? false : c->text.filteredraw;
            }
        }
        for (int y = 0; y < g->ys; y++) {
            for (int x = 0; x < g->xs; x++) {
                if (Cell *c = g->C(x, y).get(); c->grid) {
                    RecomputeFilteredDisplay(c->grid.get(),
                                             sys->filtershowsubgrids && !c->text.filtered);
                }
            }
        }
    }

    void ApplyRowFilterExpansion() {
        if (root->grid) { RecomputeFilteredDisplay(root->grid.get()); }
    }

    void CollectCellsSel(bool recurse) {
        itercells.clear();
        if (selected.grid != nullptr) {
            selected.grid->CollectCellsSel(itercells, selected, recurse);
        }
    }

    // Sets the raw filter result of all cells with `filter` and updates the display. With "Keep
    // previous filter matches" on, the cells shown by the active filter (if any) stay shown.
    void ApplyFilter(auto &&filter, bool keep = true) {
        vector<Cell *> shown;
        if (keep && sys->filterunion) {
            loopallcells(c) if (!c->text.filteredraw) shown.push_back(c);
            if (shown.size() == itercells.size()) { shown.clear(); }
        }
        filter();
        for (auto *c : shown) { c->text.filteredraw = false; }
        ApplyRowFilterExpansion();
        root->ResetChildren();
        UpdateLayout();
        ScrollIfSelectionOutOfView();
        canvas->Refresh();
    }

    void ApplyEditFilter() {
        editfilter = std::clamp(editfilter, 1, 99);
        ApplyFilter([&] {
            CollectCells(root.get());
            ranges::sort(itercells, [](auto a, auto b) {
                // sort in descending order
                return a->text.lastedit > b->text.lastedit;
            });
            loopv(i, itercells) itercells[i]->text.filteredraw =
                i > itercells.size() * editfilter / 100;
        });
    }

    void ApplyEditRangeFilter(wxDateTime &rangebegin, wxDateTime &rangeend) {
        ApplyFilter([&] {
            loopallcells(c) c->text.filteredraw = !c->text.lastedit.IsBetween(rangebegin, rangeend);
        });
    }

    static wxDateTime ParseDateTimeString(const wxString &s) {
        wxDateTime dt;
        wxString::const_iterator end;
        if (!dt.ParseDateTime(s, &end)) { dt = wxInvalidDateTime; }
        return dt;
    }

    void SetSearchFilter(bool on) {
        ApplyFilter([&] { loopallcells(c) c->text.filteredraw = on && !c->text.IsInSearch(); }, on);
    }

    void ExportAllImages(const wxString &filename, Cell *exportroot) {
        std::set<Image *> exportimages;
        CollectCells(exportroot);
        for (auto *c : itercells) {
            if (c->text.image != nullptr) { exportimages.insert(c->text.image); }
        }
        wxFileName fn(filename);
        auto directory = fn.GetPathWithSep();
        for (auto *image : exportimages) {
            if (!image->ExportToDirectory(directory)) { break; }
        }
    }
};
