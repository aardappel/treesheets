struct TSCanvas : public wxScrolledCanvas {
    TSFrame *frame;
    Document *doc {nullptr};
    int mousewheelaccum {0};
    bool lastrmbwaswithctrl {false};
    wxPoint lastmousepos;

    TSCanvas(TSFrame *fr, wxWindow *parent, const wxSize &size = wxDefaultSize)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, size,
                           wxScrolledWindowStyle | wxWANTS_CHARS),
          frame(fr) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        DisableKeyboardScrolling();
        // Without this, ScrolledWindow does its own scrolling upon mousewheel events, which
        // interferes with our own.
        EnableScrolling(false, false);
    }

    ~TSCanvas() {
        DELETEP(doc);
        frame = nullptr;
    }

    void OnPaint(wxPaintEvent &event) {
        #ifndef __WXMSW__
            wxPaintDC dc(this);
        #else
            auto sz = GetClientSize();
            if (sz.GetX() <= 0 || sz.GetY() <= 0) return;
            wxBitmap buffer(sz.GetX(), sz.GetY(), 24);
            wxBufferedPaintDC dc(this, buffer);
        #endif
        doc->Draw(dc);
    };

    void RefreshHover(int mx, int my) {
        doc->mx = mx;
        doc->my = my;
        doc->updatehover = true;
        doc->Refresh();
    }

    void OnMotion(wxMouseEvent &me) {
        if (me.LeftIsDown() || me.RightIsDown()) {
            if (me.AltDown() && me.ShiftDown()) {
                RefreshHover(me.GetX(), me.GetY());
                doc->Copy(A_DRAGANDDROP);
            } else {
                doc->drag = true;
                RefreshHover(me.GetX(), me.GetY());
            }
        } else if (me.MiddleIsDown()) {
            wxPoint p = me.GetPosition() - lastmousepos;
            CursorScroll(-p.x, -p.y);
        }
        lastmousepos = me.GetPosition();
    }

    void SelectClick(int mx, int my, bool right, int isctrlshift) {
        if (mx < 0 || my < 0)
            return;  // for some reason, using just the "menu" key sends a right-click at (-1, -1)
        doc->selectclick = true;
        doc->clickright = right;
        doc->isctrlshiftdrag = isctrlshift;
        RefreshHover(mx, my);
    }

    void OnLeftDown(wxMouseEvent &me) {
        #ifndef __WXMSW__
        // seems to not want to give the sw focus otherwise (thinks its already in focus
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
            doc->selectup = true;
            RefreshHover(me.GetX(), me.GetY());
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
        doc->doubleclick = true;
        RefreshHover(me.GetX(), me.GetY());
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
        Status(doc->Key(ce.GetUnicodeKey(), ce.GetKeyCode(), ce.AltDown(), ce.CmdDown(),
                        ce.ShiftDown(), unprocessed));
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
            Status(doc->Wheel(steps, me.AltDown(), ctrl, me.ShiftDown()));
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

    void Status(const wxChar *msg = nullptr) {
        if (frame->GetStatusBar() && (!msg || *msg)) frame->SetStatusText(msg ? msg : L"", 0);
    }

    DECLARE_EVENT_TABLE()
};
