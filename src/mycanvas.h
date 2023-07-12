struct TSCanvas : public wxScrolledWindow {
    MyFrame *frame;
    Document *doc;

    int mousewheelaccum;
    bool lastrmbwaswithctrl;

    wxPoint lastmousepos;

    TSCanvas(MyFrame *fr, wxWindow *parent, const wxSize &size = wxDefaultSize)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, size,
                           wxScrolledWindowStyle | wxWANTS_CHARS),
          frame(fr),
          doc(nullptr),
          mousewheelaccum(0),
          lastrmbwaswithctrl(false) {
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
        #ifdef __WXMAC__
            wxPaintDC dc(this);
        #elif __WXGTK__
            wxPaintDC dc(this);
        #else
            auto sz = GetClientSize();
            if (sz.GetX() <= 0 || sz.GetY() <= 0) return;
            wxBitmap buffer(sz.GetX(), sz.GetY(), 24);
            wxBufferedPaintDC dc(this, buffer);
        #endif
        // DoPrepareDC(dc);
        doc->Draw(dc);
        // Display has been re-layouted, compute hover selection again.
        // TODO: lastmousepos doesn't seem correct anymore after a scroll operation in latest wxWidgets.
        /*
        doc->Hover(lastmousepos.x / doc->currentviewscale,
                   lastmousepos.y / doc->currentviewscale,
                   dc);
        */
    };

    void UpdateHover(int mx, int my, wxDC &dc) {
        int x, y;
        CalcUnscrolledPosition(mx, my, &x, &y);
        DoPrepareDC(dc);
        doc->Hover(x / doc->currentviewscale, y / doc->currentviewscale, dc);
    }

    void OnMotion(wxMouseEvent &me) {
        wxClientDC dc(this);
        UpdateHover(me.GetX(), me.GetY(), dc);
        if (me.LeftIsDown() || me.RightIsDown()) {
            if(me.AltDown() && me.ShiftDown()) {
                doc->Copy(DRAGANDDROP);
            } else {
                doc->Drag(dc);
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
        wxClientDC dc(this);
        UpdateHover(mx, my, dc);
        doc->Select(dc, right, isctrlshift);
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
        if (me.CmdDown() || me.AltDown()) doc->SelectUp();
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
        wxClientDC dc(this);
        UpdateHover(me.GetX(), me.GetY(), dc);
        Status(doc->DoubleClick(dc));
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

        // Without this check, Alt+F (keyboard menu nav) Alt+1..6 (style changes), Alt+cursor
        // (scrolling) don't work.
        // The 128 makes sure unicode entry on e.g. Polish keyboards still works.
        // (on Linux in particular).
        if ((ce.GetModifiers() == wxMOD_ALT) && (ce.GetUnicodeKey() < 128)) {
            ce.Skip();
            return;
        }

        wxClientDC dc(this);
        DoPrepareDC(dc);
        bool unprocessed = false;
        Status(doc->Key(dc, ce.GetUnicodeKey(), ce.GetKeyCode(), ce.AltDown(), ce.CmdDown(),
                        ce.ShiftDown(), unprocessed));
        if (unprocessed) ce.Skip();
    }

    void OnMouseWheel(wxMouseEvent &me) {
        bool ctrl = me.CmdDown();
        if (sys->zoomscroll) ctrl = !ctrl;
        wxClientDC dc(this);
        if (me.AltDown() || ctrl || me.ShiftDown()) {
            mousewheelaccum += me.GetWheelRotation();
            int steps = mousewheelaccum / me.GetWheelDelta();
            if (!steps) return;
            mousewheelaccum -= steps * me.GetWheelDelta();

            UpdateHover(me.GetX(), me.GetY(), dc);
            Status(doc->Wheel(dc, steps, me.AltDown(), ctrl, me.ShiftDown()));
        } else if (me.GetWheelAxis()) {
            CursorScroll(me.GetWheelRotation() * g_scrollratewheel, 0);
            UpdateHover(me.GetX(), me.GetY(), dc);
        } else {
            CursorScroll(0, -me.GetWheelRotation() * g_scrollratewheel);
            UpdateHover(me.GetX(), me.GetY(), dc);
        }
    }

    void OnSize(wxSizeEvent &se) { doc->Refresh(); }
    void OnContextMenuClick(wxContextMenuEvent &cme) {
        if (lastrmbwaswithctrl) {
            wxMenu *tagmenu = new wxMenu();
            doc->RecreateTagMenu(*tagmenu);
            PopupMenu(tagmenu);
            delete tagmenu;
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
        if (frame->GetStatusBar() && (!msg || *msg))
            frame->SetStatusText(msg ? msg : L"", 0);
    }

    DECLARE_EVENT_TABLE()
};
