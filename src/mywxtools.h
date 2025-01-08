
static void DrawRectangle(wxDC &dc, uint c, int x, int y, int xs, int ys, bool outline = false) {
    if (outline)
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    else
        dc.SetBrush(wxBrush(wxColour(c)));
    dc.SetPen(wxPen(wxColour(c)));
    dc.DrawRectangle(x, y, xs, ys);
}

struct DropTarget : wxDropTarget {
    DropTarget(wxDataObject *data) : wxDropTarget(data) {};

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
        Document *doc = sw->doc.get();
        switch (doc->dndobjc->GetReceivedFormat().GetType()) {
            case wxDF_BITMAP: doc->PasteOrDrop(*doc->dndobji); break;
            case wxDF_FILENAME: doc->PasteOrDrop(*doc->dndobjf); break;
            case wxDF_TEXT:
            case wxDF_UNICODETEXT: doc->PasteOrDrop(*doc->dndobjt);
            default:;
        }
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

struct DateTimeRangeDialog : public wxDialog {
    wxStaticText introtext {this, wxID_ANY, _("Please select the datetime range.")};
    wxStaticText starttext {this, wxID_ANY, _("Start date and time")};
    wxDatePickerCtrl startdate {this, wxID_ANY};
    wxTimePickerCtrl starttime {this, wxID_ANY};
    wxStaticText endtext {this, wxID_ANY, _("End date and time")};
    wxDatePickerCtrl enddate {this, wxID_ANY};
    wxTimePickerCtrl endtime {this, wxID_ANY};
    wxButton okbtn {this, wxID_OK, _("Filter")};
    wxButton cancelbtn {this, wxID_CANCEL, _("Cancel")};
    wxDateTime begin;
    wxDateTime end;
    DateTimeRangeDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, _("Date and time range")) {
        wxSizerFlags sizerflags(1);
        wxFlexGridSizer *startsizer = new wxFlexGridSizer(2, wxSize(5, 5));
        startsizer->Add(&startdate, 0, wxALL, 5);
        startsizer->Add(&starttime, 0, wxALL, 5);
        wxFlexGridSizer *endsizer = new wxFlexGridSizer(2, wxSize(5, 5));
        endsizer->Add(&enddate, 0, wxALL, 5);
        endsizer->Add(&endtime, 0, wxALL, 5);
        wxFlexGridSizer *btnsizer = new wxFlexGridSizer(2, wxSize(5, 5));
        btnsizer->Add(&okbtn, 0, wxALL, 5);
        btnsizer->Add(&cancelbtn, 0, wxALL, 5);
        wxFlexGridSizer *topsizer = new wxFlexGridSizer(1);
        topsizer->Add(&introtext, 0, wxALL, 5);
        topsizer->Add(&starttext, 0, wxALL, 5);
        topsizer->Add(startsizer, sizerflags);
        topsizer->Add(&endtext, 0, wxALL, 5);
        topsizer->Add(endsizer, sizerflags);
        topsizer->Add(btnsizer, sizerflags);
        SetSizerAndFit(topsizer);
        topsizer->SetSizeHints(this);
    }
    void OnButton(wxCommandEvent &ce) {
        if (ce.GetId() == wxID_OK) {
            int starthour, startmin, startsec;
            starttime.GetTime(&starthour, &startmin, &startsec);
            wxTimeSpan starttimespan(starthour, startmin, startsec);
            int endhour, endmin, endsec;
            endtime.GetTime(&endhour, &endmin, &endsec);
            wxTimeSpan endtimespan(endhour, endmin, endsec);
            begin = startdate.GetValue().Add(starttimespan);
            end = enddate.GetValue().Add(endtimespan);
        }
        EndModal(ce.GetId());
    }
    int Run() { return ShowModal(); }
    DECLARE_EVENT_TABLE()
};

struct ColorPopup : wxVListBoxComboPopup {
    ColorPopup(wxWindow *parent) {}

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

static uint PickColor(wxFrame *fr, uint defcol) {
    wxColour col = wxGetColourFromUser(fr, wxColour(defcol));
    if (col.IsOk()) return (col.Blue() << 16) + (col.Green() << 8) + col.Red();
    return -1;
}

#define dd_icon_res_scale 3.0

struct ImagePopup : wxVListBoxComboPopup {
    void OnComboDoubleClick() {
        wxString s = GetString(GetSelection());
        sys->frame->GetCurTab()->doc->ImageChange(s, dd_icon_res_scale);
    }
};

struct ImageDropdown : wxOwnerDrawnComboBox {
    vector<unique_ptr<wxBitmap>> bitmaps_display;
    wxArrayString as;
    const int image_space = 22;

    ImageDropdown(wxWindow *parent, const wxString &path) {
        FillBitmapVector(path);
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
        sys->ImageDraw(bitmaps_display[item].get(), dc, rect.x + FromDIP(3), rect.y + FromDIP(3));
    }

    void FillBitmapVector(const wxString &path) {
        if (!bitmaps_display.empty()) bitmaps_display.resize(0);
        wxString f = wxFindFirstFile(path + L"*.*");
        while (!f.empty()) {
            wxBitmap bm;
            if (bm.LoadFile(f, wxBITMAP_TYPE_PNG)) {
                unique_ptr<wxBitmap> dbm(new wxBitmap());
                ScaleBitmap(bm, FromDIP(1.0) / dd_icon_res_scale, *dbm);
                bitmaps_display.push_back(std::move(dbm));
                as.Add(f);
            }
            f = wxFindNextFile();
        }
    }
};

static void ScaleBitmap(const wxBitmap &src, double sc, wxBitmap &dest) {
    dest = wxBitmap(src.ConvertToImage().Scale(src.GetWidth() * sc, src.GetHeight() * sc,
                                               wxIMAGE_QUALITY_HIGH));
}

static vector<uint8_t> ConvertWxImageToBuffer(const wxImage &im, wxBitmapType bmt) {
    wxMemoryOutputStream mos(NULL, 0);
    im.SaveFile(mos, bmt);
    auto sz = mos.TellO();
    vector<uint8_t> buf(sz);
    mos.CopyTo(buf.data(), sz);
    return buf;
}

static wxImage ConvertBufferToWxImage(const vector<uint8_t> &buf, wxBitmapType bmt) {
    wxMemoryInputStream mis(buf.data(), buf.size());
    wxImage im(mis, bmt);
    if (!im.IsOk()) {
        int sz = 32;
        im.Create(sz, sz, false);
        im.SetRGB(wxRect(0, 0, sz, sz), 0xFF, 0, 0);
        // Set to red to indicate error.
    }
    return im;
}

static wxBitmap ConvertBufferToWxBitmap(const vector<uint8_t> &buf, wxBitmapType bmt) {
    wxImage im = ConvertBufferToWxImage(buf, bmt);
    wxBitmap bm(im, 32);
    return bm;
}

static uint64_t CalculateHash(vector<uint8_t> &idv) {
    int max = 4096;
    return FNV1A64(idv.data(), min(idv.size(), max));
}
