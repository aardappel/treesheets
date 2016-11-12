struct DropTarget : wxDropTarget {
    DropTarget(wxDataObject *data) : wxDropTarget(data){};

    wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) {
        TSCanvas *sw = sys->frame->GetCurTab();
        wxClientDC dc(sw);
        sw->UpdateHover(x, y, dc);
        return sw->doc->hover.g ? wxDragCopy : wxDragNone;
    }

    bool OnDrop(wxCoord x, wxCoord y) { return sys->frame->GetCurTab()->doc->hover.g != NULL; }
    wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) {
        GetData();
        TSCanvas *sw = sys->frame->GetCurTab();
        sw->SelectClick(x, y, false, 0);
        sw->doc->PasteOrDrop();
        return wxDragCopy;
    }
};

struct BlinkTimer : wxTimer {
    void Notify() {
        TSCanvas *tsc = sys->frame->GetCurTab();
        if (tsc) tsc->doc->Blink();
    }
};

struct ThreeChoiceDialog : public wxDialog {
    ThreeChoiceDialog(wxWindow *parent, const wxString &title, const wxString &msg,
                      const wxString &ch1, const wxString &ch2, const wxString &ch3)
        : wxDialog(parent, wxID_ANY, title) {
        wxBoxSizer *bsv = new wxBoxSizer(wxVERTICAL);
        bsv->Add(new wxStaticText(this, -1, msg), 0, wxALL, 5);
        wxBoxSizer *bsb = new wxBoxSizer(wxHORIZONTAL);
        bsb->Prepend(new wxButton(this, 2, ch3), 0, wxALL, 5);
        bsb->PrependStretchSpacer(1);
        bsb->Prepend(new wxButton(this, 1, ch2), 0, wxALL, 5);
        bsb->PrependStretchSpacer(1);
        bsb->Prepend(new wxButton(this, 0, ch1), 0, wxALL, 5);
        bsv->Add(bsb, 1, wxEXPAND);
        SetSizer(bsv);
        bsv->SetSizeHints(this);
    }

    void OnButton(wxCommandEvent &ce) { EndModal(ce.GetId()); }
    int Run() { return ShowModal(); }
    DECLARE_EVENT_TABLE()
};

struct ColorPopup : wxVListBoxComboPopup {
    ColorPopup(wxWindow *parent) {
    }

    void OnComboDoubleClick() {
        sys->frame->GetCurTab()->doc->ColorChange(m_combo->GetId(), GetSelection());
    }
};

struct ColorDropdown : wxOwnerDrawnComboBox {
    ColorDropdown(wxWindow *parent, wxWindowID id, int sel = 0) {
        wxArrayString as;
        as.Add(L"", sizeof(celltextcolors) / sizeof(uint));
        Create(parent, id, L"", wxDefaultPosition, wxSize(44, 24), as,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ColorPopup(this));
        SetSelection(sel);
        SetPopupMaxHeight(2000);
    }

    wxCoord OnMeasureItem(size_t item) const { return 24; }
    wxCoord OnMeasureItemWidth(size_t item) const { return 40; }
    void OnDrawBackground(wxDC &dc, const wxRect &rect, int item, int flags) const {
        DrawRectangle(dc, 0xFFFFFF, rect.x, rect.y, rect.width, rect.height);
    }

    void OnDrawItem(wxDC &dc, const wxRect &rect, int item, int flags) const {
        DrawRectangle(dc, item == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[item],
                      rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2);
        if (item == CUSTOMCOLORIDX) {
            dc.SetTextForeground(*wxBLACK);
            dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                              false, L""));
            dc.DrawText(L"Custom", rect.x + 1, rect.y + 1);
        }
    }
};

struct ImagePopup : wxVListBoxComboPopup {
    void OnComboDoubleClick() {
        wxString s = GetString(GetSelection());
        sys->frame->GetCurTab()->doc->ImageChange(s);
    }
};

struct ImageDropdown : wxOwnerDrawnComboBox {
    Vector<wxBitmap *> bitmaps;  // FIXME: delete these somewhere
    wxArrayString as;

    ImageDropdown(wxWindow *parent, wxString &path) {
        wxString f = wxFindFirstFile(path + L"*.*");
        while (!f.empty()) {
            wxBitmap *bm = new wxBitmap();
            if (bm->LoadFile(f, wxBITMAP_TYPE_PNG)) {
                bitmaps.push() = bm;
                as.Add(f);
            }
            f = wxFindNextFile();
        }
        Create(parent, A_DDIMAGE, L"", wxDefaultPosition, wxSize(44, 24), as,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ImagePopup());
        SetSelection(0);
        SetPopupMaxHeight(2000);
    }

    wxCoord OnMeasureItem(size_t item) const { return 22; }
    wxCoord OnMeasureItemWidth(size_t item) const { return 22; }
    void OnDrawBackground(wxDC &dc, const wxRect &rect, int item, int flags) const {
        DrawRectangle(dc, 0xFFFFFF, rect.x, rect.y, rect.width, rect.height);
    }

    void OnDrawItem(wxDC &dc, const wxRect &rect, int item, int flags) const {
        dc.DrawBitmap(*bitmaps[item], rect.x + 3, rect.y + 3);
    }
};
