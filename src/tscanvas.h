struct TSCanvas : public wxScrolledCanvas {
    TSFrame *frame;
    unique_ptr<Document> doc {nullptr};
    int mousewheelaccum {0};
    bool lastrmbwaswithctrl {false};
    wxPoint lastmousepos;
    double zoomgesturebase {1.0};

    TSCanvas(TSFrame *fr, wxWindow *parent, const wxSize &size = wxDefaultSize)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, size,
                           wxScrolledWindowStyle | wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
          frame(fr) {
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
        Bind(wxEVT_LEFT_DOWN, &TSCanvas::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &TSCanvas::OnLeftUp, this);
        Bind(wxEVT_RIGHT_DOWN, &TSCanvas::OnRightDown, this);
        Bind(wxEVT_LEFT_DCLICK, &TSCanvas::OnLeftDoubleClick, this);
        Bind(wxEVT_CHAR, &TSCanvas::OnChar, this);
        Bind(wxEVT_KEY_DOWN, &TSCanvas::OnKeyDown, this);
        Bind(wxEVT_CONTEXT_MENU, &TSCanvas::OnContextMenuClick, this);
        Bind(wxEVT_SIZE, &TSCanvas::OnSize, this);
        Bind(wxEVT_SCROLL_THUMBTRACK, &TSCanvas::OnScroll, this);
        Bind(wxEVT_SCROLLWIN_THUMBTRACK, &TSCanvas::OnScrollWin, this);
    }

    ~TSCanvas() override { frame = nullptr; }

    void OnPaint(wxPaintEvent &event) {
        wxAutoBufferedPaintDC dc(this);
        doc->Draw(dc);
    };

    void OnMotion(wxMouseEvent &me) {
        wxInfoDC dc(this);
        doc->UpdateHover(dc, me.GetX(), me.GetY());
        if (me.LeftIsDown() || me.RightIsDown()) {
            if (me.AltDown() && me.ShiftDown()) {
                doc->Copy(A_DRAGANDDROP);
                Refresh();
            } else {
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
            }
            sys->frame->UpdateStatus(doc->selected, true);
        } else if (me.MiddleIsDown()) {
            wxPoint p = me.GetPosition() - lastmousepos;
            CursorScroll(-p.x, -p.y);
        } else {
            if (doc->hover != doc->prev && !doc->hover.Thin()) {
                sys->frame->UpdateStatus(doc->hover, false);
            }
        }
        lastmousepos = me.GetPosition();
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
        #ifndef __WXMSW__
        // seems to not want to give the canvas focus otherwise (thinks its already in focus
        // when its not?)
        if (frame->filter != nullptr) { frame->filter->SetFocus(); }
        #endif
        SetFocus();
        if (me.ShiftDown()) {
            OnMotion(me);
        } else {
            SelectClick(me.GetX(), me.GetY(), false,
                        static_cast<int>(me.CmdDown()) + static_cast<int>(me.AltDown()) * 2);
        }
    }

    void OnLeftUp(wxMouseEvent &me) {
        if (me.CmdDown() || me.AltDown()) {
            wxInfoDC dc(this);
            doc->UpdateHover(dc, me.GetX(), me.GetY());
            doc->SelectUp();
            sys->frame->UpdateStatus(doc->selected, true);
            Refresh();
        }
    }

    void OnRightDown(wxMouseEvent &me) {
        SetFocus();
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
        if (unprocessed) { ce.Skip(); }
    }

    void OnMouseWheel(wxMouseEvent &me) {
        bool ctrl = me.CmdDown();
        if (sys->zoomscroll) { ctrl = !ctrl; }
        if (me.AltDown() || ctrl || me.ShiftDown()) {
            mousewheelaccum += me.GetWheelRotation();
            int steps = mousewheelaccum / me.GetWheelDelta();
            if (steps == 0) { return; }
            mousewheelaccum -= steps * me.GetWheelDelta();
            sys->frame->SetStatus(doc->Wheel(steps, me.AltDown(), ctrl, me.ShiftDown()));
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
        if (steps != 0) { sys->frame->SetStatus(doc->Wheel(steps, false, true, false)); }
    }

    void OnSize(wxSizeEvent &se) {
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

    void CursorScroll(int dx, int dy) {
        int x = 0;
        int y = 0;
        GetViewStart(&x, &y);
        x += dx;
        y += dy;
        // EnableScrolling(true, true);
        Scroll(x, y);
        // EnableScrolling(false, false);
    }
};
