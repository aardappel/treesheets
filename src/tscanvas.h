struct TSCanvas : public wxScrolledCanvas {
    TSFrame *frame;
    Document *doc {nullptr};
    int mousewheelaccum {0};
    bool lastrmbwaswithctrl {false};
    wxPoint lastmousepos;

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
    }

    ~TSCanvas() {
        DELETEP(doc);
        frame = nullptr;
    }

    void OnPaint(wxPaintEvent &event) {
        #ifdef __WXMSW__
            auto sz = GetClientSize();
            if (sz.GetX() <= 0 || sz.GetY() <= 0) return;
            wxBitmap bmp;
            auto sf = GetDPIScaleFactor();
            bmp.CreateWithDIPSize(sz, sf, 24);
            wxBufferedPaintDC dc(this, bmp);
        #else
            wxPaintDC dc(this);
        #endif
        DoPrepareDC(dc);
        doc->Draw(dc);
    };

    void OnScrollToSelectionRequest(wxCommandEvent &event) {
        doc->ScrollIfSelectionOutOfView(doc->selected);
        #ifdef __WXMAC__
            Update();
        #endif
    }

    void OnMotion(wxMouseEvent &me) {
        wxClientDC dc(this);  // TODO: replace with wxInfoDC starting wxWidgets 3.3.0
        doc->UpdateHover(dc, me.GetX(), me.GetY());
        if (me.LeftIsDown() || me.RightIsDown()) {
            if (me.AltDown() && me.ShiftDown()) {
                doc->Copy(A_DRAGANDDROP);
                Refresh();
            } else {
                if (doc->isctrlshiftdrag) {
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
            sys->frame->UpdateStatus(doc->selected);
        } else if (me.MiddleIsDown()) {
            wxPoint p = me.GetPosition() - lastmousepos;
            CursorScroll(-p.x, -p.y);
        } else {
            if (doc->hover != doc->prev && !doc->hover.Thin()) sys->frame->UpdateStatus(doc->hover);
        }
        lastmousepos = me.GetPosition();
    }

    void SelectClick(int mx, int my, bool right, int isctrlshift) {
        wxClientDC dc(this);  // TODO: replace with wxInfoDC starting wxWidgets 3.3.0
        if (mx < 0 || my < 0)
            return;  // for some reason, using just the "menu" key sends a right-click at (-1, -1)
        doc->isctrlshiftdrag = isctrlshift;
        doc->UpdateHover(dc, mx, my);
        doc->SelectClick(right);
        sys->frame->UpdateStatus(doc->selected);
        Refresh();
    }

    void OnLeftDown(wxMouseEvent &me) {
        #ifndef __WXMSW__
        // seems to not want to give the canvas focus otherwise (thinks its already in focus
        // when its not?)
        if (frame->filter) frame->filter->SetFocus();
        #endif
        SetFocus();
        if (me.ShiftDown())
            OnMotion(me);
        else
            SelectClick(me.GetX(), me.GetY(), false, me.CmdDown() + me.AltDown() * 2);
    }

    void OnLeftUp(wxMouseEvent &me) {
        if (me.CmdDown() || me.AltDown()) {
            wxClientDC dc(this);  // TODO: replace with wxInfoDC starting wxWidgets 3.3.0
            doc->UpdateHover(dc, me.GetX(), me.GetY());
            doc->SelectUp();
            sys->frame->UpdateStatus(doc->selected);
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
        wxClientDC dc(this);  // TODO: replace with wxInfoDC starting wxWidgets 3.3.0
        doc->UpdateHover(dc, me.GetX(), me.GetY());
        doc->DoubleClick();
        sys->frame->UpdateStatus(doc->selected);
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
        if (unprocessed) ce.Skip();
    }

    void OnMouseWheel(wxMouseEvent &me) {
        bool ctrl = me.CmdDown();
        if (sys->zoomscroll) ctrl = !ctrl;
        if (me.AltDown() || ctrl || me.ShiftDown()) {
            mousewheelaccum += me.GetWheelRotation();
            int steps = mousewheelaccum / me.GetWheelDelta();
            if (!steps) return;
            mousewheelaccum -= steps * me.GetWheelDelta();
            sys->frame->SetStatus(doc->Wheel(steps, me.AltDown(), ctrl, me.ShiftDown()));
        } else if (me.GetWheelAxis()) {
            CursorScroll(me.GetWheelRotation() * g_scrollratewheel, 0);
        } else {
            CursorScroll(0, -me.GetWheelRotation() * g_scrollratewheel);
        }
    }

    void OnSize(wxSizeEvent &se) {}
    void OnContextMenuClick(wxContextMenuEvent &cme) {
        if (lastrmbwaswithctrl) {
            auto tagmenu = make_unique<wxMenu>();
            doc->RecreateTagMenu(*tagmenu);
            PopupMenu(tagmenu.get());
        } else {
            PopupMenu(frame->editmenupopup);
        }
    }

    void OnScrollWin(wxScrollWinEvent &swe) {
        // This only gets called when scrolling using the scroll bar, not with mousewheel.
        swe.Skip();  // Use default scrolling behavior.
    }

    void CursorScroll(int dx, int dy) {
        int x, y;
        GetViewStart(&x, &y);
        x += dx;
        y += dy;
        // EnableScrolling(true, true);
        Scroll(x, y);
        // EnableScrolling(false, false);
    }

    DECLARE_EVENT_TABLE()
};
