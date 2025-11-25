static void DrawRectangle(wxDC &dc, uint color, int x, int y, int xs, int ys,
                          bool outline = false) {
    if (outline)
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    else
        dc.SetBrush(wxBrush(wxColour(LightColor(color))));
    dc.SetPen(wxPen(wxColour(LightColor(color))));
    dc.DrawRectangle(x, y, xs, ys);
}

struct DropTarget : wxDropTarget {
    DropTarget(wxDataObject *data) : wxDropTarget(data) {};

    wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) {
        auto canvas = sys->frame->GetCurrentTab();
        canvas->RefreshHover(x, y);
        return canvas->doc->hover.grid ? wxDragCopy : wxDragNone;
    }

    bool OnDrop(wxCoord x, wxCoord y) {
        return sys->frame->GetCurrentTab()->doc->hover.grid != nullptr;
    }
    wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) {
        GetData();
        Document *doc = sys->frame->GetCurrentTab()->doc;
        doc->paintselectclick = true;
        doc->paintdrop = true;
        doc->canvas->RefreshHover(x, y);
        return wxDragCopy;
    }
};

struct ThreeChoiceDialog : public wxDialog {
    ThreeChoiceDialog(wxWindow *parent, const wxString &title, const wxString &msg,
                      const wxString &ch1, const wxString &ch2, const wxString &ch3)
        : wxDialog(parent, wxID_ANY, title) {
        auto bsv = new wxBoxSizer(wxVERTICAL);
        bsv->Add(new wxStaticText(this, -1, msg), 0, wxALL, 5);
        auto bsb = new wxBoxSizer(wxHORIZONTAL);
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
    wxStaticText introtext {this, wxID_ANY, _(L"Please select the datetime range.")};
    wxStaticText starttext {this, wxID_ANY, _(L"Start date and time")};
    wxDatePickerCtrl startdate {this, wxID_ANY};
    wxTimePickerCtrl starttime {this, wxID_ANY};
    wxStaticText endtext {this, wxID_ANY, _(L"End date and time")};
    wxDatePickerCtrl enddate {this, wxID_ANY};
    wxTimePickerCtrl endtime {this, wxID_ANY};
    wxButton okbtn {this, wxID_OK, _(L"Filter")};
    wxButton cancelbtn {this, wxID_CANCEL, _(L"Cancel")};
    wxDateTime begin;
    wxDateTime end;
    DateTimeRangeDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, _(L"Date and time range")) {
        wxSizerFlags sizerflags(1);
        auto startsizer = new wxFlexGridSizer(2, wxSize(5, 5));
        startsizer->Add(&startdate, 0, wxALL, 5);
        startsizer->Add(&starttime, 0, wxALL, 5);
        auto endsizer = new wxFlexGridSizer(2, wxSize(5, 5));
        endsizer->Add(&enddate, 0, wxALL, 5);
        endsizer->Add(&endtime, 0, wxALL, 5);
        auto btnsizer = new wxFlexGridSizer(2, wxSize(5, 5));
        btnsizer->Add(&okbtn, 0, wxALL, 5);
        btnsizer->Add(&cancelbtn, 0, wxALL, 5);
        auto topsizer = new wxFlexGridSizer(1);
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
        sys->frame->GetCurrentTab()->doc->ColorChange(m_combo->GetId(), GetSelection());
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
        DrawRectangle(dc, flags & wxODCB_PAINTING_SELECTED ? 0xA9A9A9 : 0xFFFFFF, rect.x, rect.y,
                      rect.width, rect.height);
    }

    void OnDrawItem(wxDC &dc, const wxRect &rect, int item, int flags) const {
        DrawRectangle(dc, item == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[item],
                      rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2);
        if (item == CUSTOMCOLORIDX) {
            dc.SetTextForeground(sys->invertindarkmode && wxSystemSettings::GetAppearance().IsDark()
                                     ? *wxWHITE
                                     : *wxBLACK);
            dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                              false, L""));
            dc.DrawText(L"Custom", rect.x + 1, rect.y + 1);
        }
    }
};

static uint PickColor(wxWindow *parent, uint defaultcolor) {
    auto color = wxGetColourFromUser(parent, wxColour(defaultcolor));
    if (color.IsOk()) return (color.Blue() << 16) + (color.Green() << 8) + color.Red();
    return -1;
}

static uint LightColor(uint color) {
    if (sys->invertindarkmode && wxSystemSettings::GetAppearance().IsDark()) color ^= 0x00FFFFFF;
    return color;
}

#define dd_icon_res_scale 3.0

struct ImagePopup : wxVListBoxComboPopup {
    void OnComboDoubleClick() {
        auto filename = GetString(GetSelection());
        sys->frame->GetCurrentTab()->doc->ImageChange(filename, dd_icon_res_scale);
    }
};

struct ImageDropdown : wxOwnerDrawnComboBox {
    vector<unique_ptr<wxBitmap>> bitmaps_display;
    wxArrayString filenames;
    const int image_space = 22;

    ImageDropdown(wxWindow *parent, const wxString &directory) {
        FillBitmapVector(directory);
        Create(parent, A_DDIMAGE, L"", wxDefaultPosition,
               FromDIP(wxSize(image_space * 2, image_space)), filenames,
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

    void FillBitmapVector(const wxString &directory) {
        if (!bitmaps_display.empty()) bitmaps_display.resize(0);
        auto filename = wxFindFirstFile(directory + L"*.*");
        while (!filename.empty()) {
            wxBitmap bitmap;
            if (bitmap.LoadFile(filename, wxBITMAP_TYPE_PNG)) {
                auto scaledbitmap = make_unique<wxBitmap>();
                ScaleBitmap(bitmap, FromDIP(1.0) / dd_icon_res_scale, *scaledbitmap);
                bitmaps_display.push_back(std::move(scaledbitmap));
                filenames.Add(filename);
            }
            filename = wxFindNextFile();
        }
    }
};

static void ScaleBitmap(const wxBitmap &source, double scale, wxBitmap &destination) {
    destination = wxBitmap(source.ConvertToImage().Scale(
        source.GetWidth() * scale, source.GetHeight() * scale, wxIMAGE_QUALITY_HIGH));
}

static vector<uint8_t> ConvertWxImageToBuffer(const wxImage &image, wxBitmapType bitmaptype) {
    wxMemoryOutputStream imageoutputstream(NULL, 0);
    image.SaveFile(imageoutputstream, bitmaptype);
    auto size = imageoutputstream.TellO();
    vector<uint8_t> buffer(size);
    imageoutputstream.CopyTo(buffer.data(), size);
    return buffer;
}

static wxImage ConvertBufferToWxImage(const vector<uint8_t> &buffer, wxBitmapType bitmaptype) {
    wxMemoryInputStream imageinputstream(buffer.data(), buffer.size());
    wxImage image(imageinputstream, bitmaptype);
    if (!image.IsOk()) {
        int size = 32;
        image.Create(size, size, false);
        image.SetRGB(wxRect(0, 0, size, size), 0xFF, 0, 0);
        // Set to red to indicate error.
    }
    return image;
}

static wxBitmap ConvertBufferToWxBitmap(const vector<uint8_t> &buffer, wxBitmapType bmt) {
    auto image = ConvertBufferToWxImage(buffer, bmt);
    wxBitmap bitmap(image, 32);
    return bitmap;
}

static uint64_t CalculateHash(vector<uint8_t> &buffer) {
    int max = 4096;
    return FNV1A64(buffer.data(), min(buffer.size(), max));
}

static void GetFilesFromUser(wxArrayString &filenames, wxWindow *parent, const wxChar *title,
                             const wxChar *filter) {
    wxFileDialog filedialog(parent, title, L"", L"", filter,
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR | wxFD_MULTIPLE);
    if (filedialog.ShowModal() == wxID_OK) filedialog.GetPaths(filenames);
}

static void HintIMELocation(Document *doc, int bx, int by, int bh, int stylebits) {
    // TODO: implement on other platforms
    #ifdef __WXMSW__
        HWND hwnd = doc->canvas->GetHandle();
        if (hwnd == 0) return;
        int scrollx, scrolly;
        doc->canvas->GetViewStart(&scrollx, &scrolly);
        int imx = doc->centerx + (bx + doc->hierarchysize) * doc->currentviewscale - scrollx;
        int imy = doc->centery + (by + doc->hierarchysize) * doc->currentviewscale - scrolly;
        if (HIMC himc = ImmGetContext(hwnd)) {
            COMPOSITIONFORM cof = {.dwStyle = CFS_FORCE_POSITION,
                                   .ptCurrentPos = {.x = imx, .y = imy}};
            ImmSetCompositionWindow(himc, &cof);
            LOGFONT lf = {.lfHeight = static_cast<LONG>(-bh * doc->currentviewscale),
                          .lfWeight = stylebits & STYLE_BOLD ? FW_BOLD : FW_REGULAR,
                          .lfItalic = static_cast<BYTE>(stylebits & STYLE_ITALIC),
                          .lfUnderline = static_cast<BYTE>(stylebits & STYLE_UNDERLINE),
                          .lfStrikeOut = static_cast<BYTE>(stylebits & STYLE_STRIKETHRU),
                          .lfPitchAndFamily = static_cast<BYTE>(stylebits & STYLE_FIXED
                                                                    ? FIXED_PITCH | FF_MODERN
                                                                    : VARIABLE_PITCH | FF_SWISS)};
            ImmSetCompositionFont(himc, &lf);
            CANDIDATEFORM caf = {.dwStyle = CFS_CANDIDATEPOS, .ptCurrentPos = {.x = imx, .y = imy}};
            ImmSetCandidateWindow(himc, &caf);
            ImmReleaseContext(hwnd, himc);
        }
    #endif
}
