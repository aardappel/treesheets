struct TSCanvas : public wxScrolledCanvas {
    TSFrame *frame;
    unique_ptr<Document> doc {nullptr};
    int mousewheelaccum {0};
    bool lastrmbwaswithctrl {false};
    wxPoint lastmousepos;
    double zoomgesturebase {1.0};
    // Scrolls while a selection is dragged beyond the edge of the canvas, which the pointer
    // standing still out there doesn't report motion events for.
    wxTimer autoscrolltimer;
    // Only a drag started on the canvas autoscrolls, not one that resizes the window.
    bool pressedoncanvas {false};

    TSCanvas(TSFrame *fr, wxWindow *parent, const wxSize &size = wxDefaultSize)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, size,
                           wxScrolledWindowStyle | wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
          frame(fr),
          autoscrolltimer(this) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        DisableKeyboardScrolling();
        // Without this, canvas does its own scrolling upon mousewheel events, which
        // interferes with our own.
        EnableScrolling(false, false);
        // Enable pinch-to-zoom (magnify) gestures where the platform supports them
        // (macOS trackpads, touch screens). A harmless no-op elsewhere.
        EnableTouchEvents(wxTOUCH_ZOOM_GESTURE);

        Bind(wxEVT_MOUSEWHEEL, &TSCanvas::OnMouseWheel, this);
        Bind(wxEVT_GESTURE_ZOOM, &TSCanvas::OnZoomGesture, this, wxID_ANY);
        Bind(wxEVT_PAINT, &TSCanvas::OnPaint, this);
        Bind(wxEVT_MOTION, &TSCanvas::OnMotion, this);
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &me) {
            if (DragsBeyondEdge(me)) {
                StartAutoScroll();
            } else {
                doc->SetHoverShade(wxRect());
            }
            me.Skip();
        });
        Bind(wxEVT_TIMER, &TSCanvas::OnAutoScroll, this);
        Bind(wxEVT_LEFT_DOWN, &TSCanvas::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &TSCanvas::OnLeftUp, this);
        Bind(wxEVT_RIGHT_DOWN, &TSCanvas::OnRightDown, this);
        Bind(wxEVT_LEFT_DCLICK, &TSCanvas::OnLeftDoubleClick, this);
        Bind(wxEVT_CHAR, &TSCanvas::OnChar, this);
        Bind(wxEVT_KEY_DOWN, &TSCanvas::OnKeyDown, this);
        Bind(wxEVT_KEY_UP, &TSCanvas::OnKeyUp, this);
        Bind(wxEVT_KILL_FOCUS, &TSCanvas::OnKillFocus, this);
        Bind(wxEVT_CONTEXT_MENU, &TSCanvas::OnContextMenuClick, this);
        Bind(wxEVT_SIZE, &TSCanvas::OnSize, this);
        Bind(wxEVT_SCROLL_THUMBTRACK, &TSCanvas::OnScroll, this);
        Bind(wxEVT_SCROLLWIN_THUMBTRACK, &TSCanvas::OnScrollWin, this);
    }

    ~TSCanvas() override { frame = nullptr; }

    void OnPaint(wxPaintEvent &event) {
        wxAutoBufferedPaintDC dc(this);
        // Layout already supplies our bounds. Avoid measuring text again after drawing it.
        dc.DisableAutomaticBoundingBoxUpdates();
        doc->Draw(dc);
        // Don't touch other widgets such as the status bar from here: on GTK their update
        // redraws them synchronously from within this draw handler and crashes in cairo.
    };

    void OnMotion(wxMouseEvent &me) {
        auto beyondedge = DragsBeyondEdge(me);
        wxInfoDC dc(this);
        if (!beyondedge) { doc->UpdateHover(dc, me.GetX(), me.GetY()); }
        if (me.LeftIsDown() || me.RightIsDown()) {
            AltWithMouse(me);
            if (me.AltDown() && me.ShiftDown()) {
                doc->Copy(A_DRAGANDDROP);
                Refresh();
            } else if (beyondedge) {
                StartAutoScroll();
            } else {
                DragToHover();
            }
        } else if (me.MiddleIsDown()) {
            wxPoint p = me.GetPosition() - lastmousepos;
            CursorScroll(-p.x, -p.y);
        } else {
            pressedoncanvas = false;
            if (doc->hover != doc->prev && !doc->hover.Thin()) {
                sys->frame->UpdateStatus(doc->hover, false);
            }
        }
        lastmousepos = me.GetPosition();
    }

    // Extends the dragged selection, or moves the target of a ctrl/alt drag, to the hovered
    // cell.
    void DragToHover() {
        if (doc->isctrlshiftdrag != 0) {
            doc->begindrag = doc->hover;
        } else if (!doc->hover.Thin()) {
            if (doc->begindrag.Thin() || doc->selected.Thin()) {
                doc->SetSelect(doc->hover);
                doc->ResetCursor();
                Refresh();
            } else {
                Selection old = doc->selected;
                doc->selected.Merge(doc->begindrag, doc->hover);
                if (!(old == doc->selected)) {
                    doc->ResetCursor();
                    Refresh();
                }
            }
        }
        sys->frame->UpdateStatus(doc->selected, true);
    }

    // Dragging beyond this rectangle scrolls. It leaves out a strip along the edges of the
    // canvas, since a maximized window leaves no room beyond them, and overlay scrollbars
    // cover them.
    wxRect AutoScrollFreeRect() const { return GetClientRect().Deflate(8); }

    // Beyond the edge, OnAutoScroll() hovers and drags to the cell nearest to the pointer
    // instead. Hovering what's under the pointer as well would flip the hover shadow between
    // the two on every motion event, repainting large parts of the canvas.
    bool DragsBeyondEdge(const wxMouseEvent &me) const {
        return pressedoncanvas && (me.LeftIsDown() || me.RightIsDown()) &&
               !(me.AltDown() && me.ShiftDown()) &&
               !AutoScrollFreeRect().Contains(me.GetPosition());
    }

    void StartAutoScroll() {
        if (!autoscrolltimer.IsRunning()) { autoscrolltimer.Start(30); }
    }

    // Scrolls by how far the pointer is beyond AutoScrollFreeRect(), and drags the selection
    // to the cell nearest to it in that rectangle.
    void OnAutoScroll(wxTimerEvent &) {
        auto state = wxGetMouseState();
        auto p = ScreenToClient(state.GetPosition());
        auto r = AutoScrollFreeRect();
        auto dx = p.x < r.GetLeft() ? p.x - r.GetLeft() : max(0, p.x - r.GetRight());
        auto dy = p.y < r.GetTop() ? p.y - r.GetTop() : max(0, p.y - r.GetBottom());
        if (!(state.LeftIsDown() || state.RightIsDown())) { pressedoncanvas = false; }
        if (!pressedoncanvas || (dx == 0 && dy == 0)) {
            if (!GetClientRect().Contains(p)) { doc->SetHoverShade(wxRect()); }
            autoscrolltimer.Stop();
            return;
        }
        // Start at a speed of its own, the pointer can't get far beyond a canvas at the edge of
        // the screen, and speed up with the distance from there.
        auto speed = [](int d) { return d == 0 ? 0 : d + (d > 0 ? 8 : -8); };
        CursorScroll(speed(dx), speed(dy));
        // Once scrolled to the end, the edge of the canvas shows the margin around the cells.
        // Stay clear of that, and of the cells' edges, where a thin selection would be hovered.
        auto *root = doc->currentdrawroot;
        if (root != nullptr && root->grid && doc->currentviewscale == 1.0) {
            auto &grid = root->grid;
            wxRect cells = grid->GetRect(doc.get(), Selection(grid, 0, 0, grid->xs, grid->ys));
            cells.Deflate(grid->cell_margin + g_selmargin + 1);
            CalcScrolledPosition(cells.x, cells.y, &cells.x, &cells.y);
            cells.Offset(doc->centerx, doc->centery);
            if (cells.Intersects(r)) { r.Intersect(cells); }
        }
        wxInfoDC dc(this);
        doc->UpdateHover(dc, std::clamp(p.x, r.GetLeft(), r.GetRight()),
                         std::clamp(p.y, r.GetTop(), r.GetBottom()));
        DragToHover();
    }

    void SelectClick(int mx, int my, bool right, int isctrlshift) {
        wxInfoDC dc(this);
        if (mx < 0 || my < 0) {
            return;  // for some reason, using just the "menu" key sends a right-click at (-1, -1)
        }
        doc->isctrlshiftdrag = isctrlshift;
        doc->UpdateHover(dc, mx, my);
        doc->SelectClick(right);
        sys->frame->UpdateStatus(doc->selected, true);
        Refresh();
    }

    void OnLeftDown(wxMouseEvent &me) {
        AltWithMouse(me);
        #ifndef __WXMSW__
        // seems to not want to give the canvas focus otherwise (thinks its already in focus
        // when its not?)
        if (frame->filter != nullptr) { frame->filter->SetFocus(); }
        #endif
        SetFocus();
        pressedoncanvas = true;
        if (me.ShiftDown()) {
            OnMotion(me);
        } else {
            SelectClick(me.GetX(), me.GetY(), false,
                        static_cast<int>(me.CmdDown()) + static_cast<int>(me.AltDown()) * 2);
        }
    }

    void OnLeftUp(wxMouseEvent &me) {
        AltWithMouse(me);
        pressedoncanvas = false;
        if (me.CmdDown() || me.AltDown()) {
            wxInfoDC dc(this);
            doc->UpdateHover(dc, me.GetX(), me.GetY());
            doc->SelectUp();
            sys->frame->UpdateStatus(doc->selected, true);
            Refresh();
        }
    }

    void OnRightDown(wxMouseEvent &me) {
        AltWithMouse(me);
        SetFocus();
        pressedoncanvas = true;
        SelectClick(me.GetX(), me.GetY(), true, 0);
        lastrmbwaswithctrl = me.CmdDown();
        #ifndef __WXMSW__
        me.Skip();  // otherwise EVT_CONTEXT_MENU won't be triggered?
        #endif
    }

    void OnLeftDoubleClick(wxMouseEvent &me) {
        wxInfoDC dc(this);
        doc->UpdateHover(dc, me.GetX(), me.GetY());
        doc->DoubleClick();
        sys->frame->UpdateStatus(doc->selected, true);
        Refresh();
    }

    void OnKeyDown(wxKeyEvent &ce) { ce.Skip(); }
    // Text size / column width changes made with Shift/Alt+wheel skip relayouting until the
    // modifier is released.
    void OnKeyUp(wxKeyEvent &ke) {
        int code = ke.GetKeyCode();
        if (code == WXK_SHIFT || code == WXK_ALT) { doc->FlushPendingLayout(); }
        ke.Skip();
    }
    void OnKillFocus(wxFocusEvent &fe) {
        if (doc) { doc->FlushPendingLayout(); }
        fe.Skip();
    }
    void OnChar(wxKeyEvent &ce) {
        /*
        if (sys->insidefiledialog)
        {
            ce.Skip();
            return;
        }
        */
        #ifndef __WXMAC__
            // Without this check, Alt+[Alphanumericals], Alt+Shift+[Alphanumericals] and
            // Alt+[Shift]+cursor (scrolling) don't work. The 128 makes sure unicode entry on e.g.
            // Polish keyboards still works. (on Linux in particular).
            if ((ce.GetModifiers() == wxMOD_ALT || ce.GetModifiers() == (wxMOD_ALT | wxMOD_SHIFT)) &&
                (ce.GetUnicodeKey() < 128)) {
                ce.Skip();
                return;
            }
        #endif

        bool unprocessed = false;
        sys->frame->SetStatus(doc->Key(ce.GetUnicodeKey(), ce.GetKeyCode(), ce.AltDown(),
                                       ce.CmdDown(), ce.ShiftDown(), unprocessed));
        // Typing moves or collapses the text selection.
        sys->frame->UpdateAmountStatus(doc->selected);
        if (unprocessed) { ce.Skip(); }
    }

    // Keeps releasing Alt after using it with the mouse from opening the menu bar, see
    // TSFrame::altwithmouse.
    void AltWithMouse(const wxMouseEvent &me) {
        #ifdef __WXMSW__
            if (me.AltDown()) { frame->altwithmouse = true; }
        #endif
    }

    void OnMouseWheel(wxMouseEvent &me) {
        AltWithMouse(me);
        bool ctrl = me.CmdDown();
        if (sys->zoomscroll) { ctrl = !ctrl; }
        if (me.AltDown() || ctrl || me.ShiftDown()) {
            mousewheelaccum += me.GetWheelRotation();
            int steps = mousewheelaccum / me.GetWheelDelta();
            if (steps == 0) { return; }
            mousewheelaccum -= steps * me.GetWheelDelta();
            bool deferlayout = me.AltDown() || me.ShiftDown();
            sys->frame->SetStatus(
                doc->Wheel(steps, me.AltDown(), ctrl, me.ShiftDown(), true, deferlayout));
        } else if (me.GetWheelAxis() != 0U) {
            CursorScroll(me.GetWheelRotation() * g_scrollratewheel, 0);
        } else {
            CursorScroll(0, -me.GetWheelRotation() * g_scrollratewheel);
        }
    }

    void OnZoomGesture(wxZoomGestureEvent &ge) {
        // A pinch maps to the same hierarchical zoom as Ctrl+mousewheel and the
        // Zoom In / Zoom Out menu items. wxZoomGestureEvent reports a cumulative
        // factor that is 1.0 when the gesture starts, grows as the fingers spread
        // (zoom in) and shrinks as they pinch together (zoom out). We turn that
        // continuous factor into discrete zoom steps each time it crosses a fixed
        // ratio threshold, similar to how mousewheelaccum batches wheel events.
        if (ge.IsGestureStart()) { zoomgesturebase = 1.0; }
        double factor = ge.GetZoomFactor();
        if (factor <= 0.0) { return; }
        const double stepfactor = 1.4;  // pinch ratio required per zoom step
        int steps = 0;
        while (factor / zoomgesturebase >= stepfactor) {
            steps++;
            zoomgesturebase *= stepfactor;
        }
        while (zoomgesturebase / factor >= stepfactor) {
            steps--;
            zoomgesturebase /= stepfactor;
        }
        // Zoom at the fingers: on a touch screen, they don't move the mouse pointer.
        if (steps != 0) {
            sys->frame->SetStatus(
                doc->Wheel(steps, false, true, false, true, false, ge.GetPosition()));
        }
    }

    void OnSize(wxSizeEvent &se) {
        doc->ResetAnchor();
        doc->UpdateLayout();
        Refresh();
        se.Skip();
    }
    void OnContextMenuClick(wxContextMenuEvent &cme) {
        if (lastrmbwaswithctrl) {
            auto tagmenu = make_unique<wxMenu>();
            doc->RecreateTagMenu(*tagmenu);
            PopupMenu(tagmenu.get());
        } else {
            PopupMenu(frame->editmenupopup);
        }
    }

    void OnScroll(wxScrollEvent &se) {
        se.Skip();  // Use default scrolling behavior.
    }

    void OnScrollWin(wxScrollWinEvent &se) {
        se.Skip();  // Use default scrolling behavior.
    }

    void CursorScroll(int dx, int dy) { doc->ScrollBy(dx, dy); }

    #if defined(__WXGTK3__) && defined(TREESHEETS_USE_PANGO)
    // wxGTK 3.3 subtracts overlay scrollbars from the client size, although they are drawn on
    // top of the canvas, and by a width that changes without a size event as they react to
    // the pointer. Zooming out to a view that needs a scrollbar, and then moving the pointer,
    // thereby shifted the centered document sideways by a few pixels. That's fixed in
    // wxWidgets master (wxWidgets/wxWidgets#26889), but not in a release yet, so this does
    // the same.
    static bool UsesOverlayScrollbars(GtkWidget *widget) {
        if (!GTK_IS_SCROLLED_WINDOW(widget) || gtk_check_version(3, 16, 0) != nullptr) {
            return false;
        }
        if (!gtk_scrolled_window_get_overlay_scrolling(GTK_SCROLLED_WINDOW(widget))) {
            return false;
        }
        auto *settings = gtk_widget_get_settings(widget);
        if (settings != nullptr &&
            g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-overlay-scrolling")) {
            gboolean enabled = TRUE;
            g_object_get(settings, "gtk-overlay-scrolling", &enabled, nullptr);
            if (!enabled) { return false; }
        }
        static const bool disabledinenv = g_strcmp0(getenv("GTK_OVERLAY_SCROLLING"), "0") == 0;
        return !disabledinenv;
    }

    void DoGetClientSize(int *width, int *height) const override {
        if (!UsesOverlayScrollbars(GetHandle())) {
            wxScrolledCanvas::DoGetClientSize(width, height);
            return;
        }
        wxSize size = GetSize() - GetWindowBorderSize();
        if (width != nullptr) { *width = max(0, size.x); }
        if (height != nullptr) { *height = max(0, size.y); }
    }
    #endif
};
