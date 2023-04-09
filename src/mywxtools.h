
static void DrawRectangle(wxDC &dc, uint c, int x, int y, int xs, int ys, bool outline = false) {
    if (outline)
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    else
        dc.SetBrush(wxBrush(wxColour(c)));
    dc.SetPen(wxPen(wxColour(c)));
    dc.DrawRectangle(x, y, xs, ys);
}

struct DropTarget : wxDropTarget {
    DropTarget(wxDataObject *data) : wxDropTarget(data){};

    wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) {
        TSCanvas *sw = sys->frame->GetCurTab();
        wxClientDC dc(sw);
        sw->UpdateHover(x, y, dc);
        return sw->doc->hover.g ? wxDragCopy : wxDragNone;
    }

    bool OnDrop(wxCoord x, wxCoord y) { return sys->frame->GetCurTab()->doc->hover.g != nullptr; }
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

    ColorDropdown(wxWindow *parent, wxWindowID id, int sel) {
        wxArrayString as;
        as.Add(L"", sizeof(celltextcolors) / sizeof(uint));
        Create(parent, id, L"", wxDefaultPosition, FromDIP(wxSize(44, 22)), as,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ColorPopup(this));
        SetSelection(sel);
        SetPopupMaxHeight(wxDisplay().GetGeometry().GetHeight() * 3 / 4);
    }

    wxCoord OnMeasureItem(size_t item) const { return FromDIP(22); }
    wxCoord OnMeasureItemWidth(size_t item) const { return FromDIP(40); }
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

#define dd_icon_res_scale 3.0

struct ImagePopup : wxVListBoxComboPopup {
    void OnComboDoubleClick() {
        wxString s = GetString(GetSelection());
        sys->frame->GetCurTab()->doc->ImageChange(s, dd_icon_res_scale);
    }
};

struct ImageDropdown : wxOwnerDrawnComboBox {
    // FIXME: delete these somewhere
    Vector<wxBitmap *> bitmaps_display;
    wxArrayString as;
    const int image_space = 22;

    ImageDropdown(wxWindow *parent, wxString &path) {
        wxString f = wxFindFirstFile(path + L"*.*");
        while (!f.empty()) {
            wxBitmap bm;
            if (bm.LoadFile(f, wxBITMAP_TYPE_PNG)) {
                auto dbm = new wxBitmap();
                ScaleBitmap(bm, FromDIP(1.0) / dd_icon_res_scale, *dbm);
                bitmaps_display.push() = dbm;
                as.Add(f);
            }
            f = wxFindNextFile();
        }
        Create(parent, A_DDIMAGE, L"", wxDefaultPosition,
               FromDIP(wxSize(image_space * 2, image_space)), as,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ImagePopup());
        SetSelection(0);
        SetPopupMaxHeight(wxDisplay().GetGeometry().GetHeight() * 3 / 4);
    }

    wxCoord OnMeasureItem(size_t item) const { return FromDIP(image_space); }
    wxCoord OnMeasureItemWidth(size_t item) const { return FromDIP(image_space); }
    void OnDrawBackground(wxDC &dc, const wxRect &rect, int item, int flags) const {
        DrawRectangle(dc, 0xFFFFFF, rect.x, rect.y, rect.width, rect.height);
    }

    void OnDrawItem(wxDC &dc, const wxRect &rect, int item, int flags) const {
        auto bm = bitmaps_display[item];
        sys->ImageDraw(bm, dc, rect.x + FromDIP(3), rect.y + FromDIP(3));
    }
};

static void ScaleBitmap(const wxBitmap &src, double sc, wxBitmap &dest) {
    dest = wxBitmap(src.ConvertToImage().Scale(src.GetWidth() * sc, src.GetHeight() * sc,
                    wxIMAGE_QUALITY_HIGH));
}
