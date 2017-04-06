
static void DrawRectangle(wxDC &dc, uint c, int x, int y, int xs, int ys, bool outline = false) {
    if (outline)
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    else
        dc.SetBrush(wxBrush(wxColour(c)));
    dc.SetPen(wxPen(wxColour(c)));
    dc.DrawRectangle(x, y, xs, ys);
}

static void DrawLine(wxDC &dc, uint c, int x, int y, int xd, int yd) {
    dc.SetPen(wxPen(wxColour(c)));
    dc.DrawLine(x, y, x + xd, y + yd);
}

static void MyDrawText(wxDC &dc, const wxString &s, wxCoord x, wxCoord y, wxCoord w, wxCoord h) {
    #ifdef __WXMSW__  // this special purpose implementation is because the MSW implementation calls
                      // TextExtent, which costs
                      // 25% of all cpu time
    dc.CalcBoundingBox(x, y);
    dc.CalcBoundingBox(x + w, y + h);
    HDC hdc = (HDC)dc.GetHDC();
    ::SetTextColor(hdc, dc.GetTextForeground().GetPixel());
    ::SetBkColor(hdc, dc.GetTextBackground().GetPixel());
    ::ExtTextOut(hdc, x, y, 0, nullptr, s.c_str(), s.length(), nullptr);
    #else
    dc.DrawText(s, x, y);
    #endif
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
    double csf;

    ColorDropdown(wxWindow *parent, wxWindowID id, double _csf, int sel) {
        csf = _csf;
        wxArrayString as;
        as.Add(L"", sizeof(celltextcolors) / sizeof(uint));
        Create(parent, id, L"", wxDefaultPosition, wxSize(44, 22) * csf, as,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ColorPopup(this));
        SetSelection(sel);
        SetPopupMaxHeight(2000);
    }

    wxCoord OnMeasureItem(size_t item) const { return 22 * csf; }
    wxCoord OnMeasureItemWidth(size_t item) const { return 40 * csf; }
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
    double csf;
    const int image_space = 22;

    ImageDropdown(wxWindow *parent, wxString &path, double _csf) {
        csf = _csf;
        wxString f = wxFindFirstFile(path + L"*.*");
        while (!f.empty()) {
            wxBitmap bm;
            if (bm.LoadFile(f, wxBITMAP_TYPE_PNG)) {
                auto dbm = new wxBitmap();
                ScaleBitmap(bm, csf / dd_icon_res_scale, *dbm);
                bitmaps_display.push() = dbm;
                as.Add(f);
            }
            f = wxFindNextFile();
        }
        Create(parent, A_DDIMAGE, L"", wxDefaultPosition,
               wxSize(image_space * 2, image_space) * csf, as,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ImagePopup());
        SetSelection(0);
        SetPopupMaxHeight(2000);
    }

    wxCoord OnMeasureItem(size_t item) const { return image_space * csf; }
    wxCoord OnMeasureItemWidth(size_t item) const { return image_space * csf; }
    void OnDrawBackground(wxDC &dc, const wxRect &rect, int item, int flags) const {
        DrawRectangle(dc, 0xFFFFFF, rect.x, rect.y, rect.width, rect.height);
    }

    void OnDrawItem(wxDC &dc, const wxRect &rect, int item, int flags) const {
        auto bm = bitmaps_display[item];
        sys->ImageDraw(bm, dc, rect.x + 3 * csf, rect.y + 3 * csf);
    }
};

static void ScaleBitmap(const wxBitmap &src, double sc, wxBitmap &dest) {
    dest = wxBitmap(src.ConvertToImage().Scale(src.GetWidth() * sc, src.GetHeight() * sc,
                    wxIMAGE_QUALITY_HIGH));
}

