template<typename DC>
static void DrawRectangle(DC &dc, uint color, int x, int y, int xs, int ys, bool outline = false) {
    const wxColour lightCol = LightColor(color);
    if (outline) {
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    } else {
        dc.SetBrush(wxBrush(lightCol));
    }
    dc.SetPen(wxPen(lightCol));
    dc.DrawRectangle(x, y, xs, ys);
}

#if defined(__WXGTK3__) && defined(TREESHEETS_USE_PANGO)
// wxGTK's graphics context creates, lays out (itemizes, breaks and shapes) and discards a new
// PangoLayout for every string it draws, which is most of the time spent drawing a screenful of
// cells. This keeps the layouts of the most recently drawn strings instead, and draws them the
// same way wxCairoContext::DoDrawText() would.
struct TextLayoutCache {
    struct Key {
        wxString text;
        size_t font;  // Hash of the font description, underline and strikethrough.
        bool operator==(const Key &o) const { return font == o.font && text == o.text; }
    };
    struct KeyHash {
        size_t operator()(const Key &k) const {
            return std::hash<wxString>()(k.text) ^ (k.font * 0x9E3779B97F4A7C15ULL);
        }
    };
    struct Entry {
        PangoLayout *layout;
        // To tell apart fonts with the same hash.
        PangoFontDescription *font;
        bool underlined;
        bool strikethrough;
        uint64_t lastuse;
    };
    enum { max_entries = 4096 };
    unordered_map<Key, Entry, KeyHash> entries;
    uint64_t uses {0};
    float fontscale {0};
    // The context of all layouts, and the parts of the cairo context it was last updated from
    // (see UpdateContext()).
    PangoContext *context {nullptr};
    cairo_font_options_t *surfaceoptions {nullptr};
    cairo_font_options_t *options {nullptr};
    double matrix[4] {};

    static TextLayoutCache &Get() {
        // Never destroyed: Pango may be gone by the time static destructors run.
        static auto *cache = new TextLayoutCache();
        return *cache;
    }

    static void Free(Entry &e) {
        g_object_unref(e.layout);
        pango_font_description_free(e.font);
    }

    void Clear() {
        for (auto &[k, e] : entries) { Free(e); }
        entries.clear();
    }

    // Drops the least recently used half of the entries.
    void Evict() {
        vector<uint64_t> lastuses;
        lastuses.reserve(entries.size());
        for (auto &[k, e] : entries) { lastuses.push_back(e.lastuse); }
        auto mid = lastuses.begin() + lastuses.size() / 2;
        nth_element(lastuses.begin(), mid, lastuses.end());
        auto cutoff = *mid;
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->second.lastuse < cutoff) {
                Free(it->second);
                it = entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Makes the context match cr, like pango_cairo_create_layout() does for every layout it
    // creates. But unlike pango_cairo_update_context(), only touches the context when that
    // changes anything: it always marks the context as changed, so every layout would be laid
    // out again.
    void UpdateContext(cairo_t *cr) {
        auto *newsurfaceoptions = cairo_font_options_create();
        cairo_surface_get_font_options(cairo_get_target(cr), newsurfaceoptions);
        auto *newoptions = cairo_font_options_create();
        cairo_get_font_options(cr, newoptions);
        cairo_matrix_t m;
        cairo_get_matrix(cr, &m);
        // Pango ignores the translation.
        double newmatrix[4] {m.xx, m.yx, m.xy, m.yy};
        if (context == nullptr) {
            context = pango_cairo_create_context(cr);
        } else if (!cairo_font_options_equal(newsurfaceoptions, surfaceoptions) ||
                   !cairo_font_options_equal(newoptions, options) ||
                   memcmp(newmatrix, matrix, sizeof(matrix)) != 0) {
            pango_cairo_update_context(cr, context);
        } else {
            cairo_font_options_destroy(newsurfaceoptions);
            cairo_font_options_destroy(newoptions);
            return;
        }
        if (surfaceoptions != nullptr) { cairo_font_options_destroy(surfaceoptions); }
        if (options != nullptr) { cairo_font_options_destroy(options); }
        surfaceoptions = newsurfaceoptions;
        options = newoptions;
        memcpy(matrix, newmatrix, sizeof(matrix));
    }

    // Draws the text like gc->DrawText() does, with the DC's font and text color.
    void Draw(wxGraphicsContext *gc, const wxFont &font, const wxColour &color,
              const wxString &text, int x, int y) {
        auto *cr = static_cast<cairo_t *>(gc->GetNativeContext());
        UpdateContext(cr);
        // The system font scaling that wxCairoContext applies to every font.
        auto *screen = gdk_screen_get_default();
        auto scale = screen != nullptr ? float(gdk_screen_get_resolution(screen) / 96.0) : 1.0f;
        if (scale != fontscale) {
            Clear();
            fontscale = scale;
        }
        auto *desc = font.GetNativeFontInfo()->description;
        auto underlined = font.GetUnderlined();
        auto strikethrough = font.GetStrikethrough();
        Key key {text, pango_font_description_hash(desc) ^ (underlined ? 1U << 30 : 0U) ^
                           (strikethrough ? 1U << 31 : 0U)};
        auto it = entries.find(key);
        if (it != entries.end() && (!pango_font_description_equal(it->second.font, desc) ||
                                    it->second.underlined != underlined ||
                                    it->second.strikethrough != strikethrough)) {
            Free(it->second);
            entries.erase(it);
            it = entries.end();
        }
        if (it == entries.end()) {
            if (entries.size() >= max_entries) { Evict(); }
            auto *layout = pango_layout_new(context);
            pango_layout_set_font_description(
                layout, (scale == 1.0f ? font : font.Scaled(scale)).GetNativeFontInfo()->description);
            auto utf8 = text.utf8_str();
            pango_layout_set_text(layout, utf8, static_cast<int>(utf8.length()));
            font.GTKSetPangoAttrs(layout);
            it = entries
                     .emplace(std::move(key), Entry {layout, pango_font_description_copy(desc),
                                                     underlined, strikethrough, 0})
                     .first;
        }
        it->second.lastuse = ++uses;
        cairo_set_source_rgba(cr, color.Red() / 255.0, color.Green() / 255.0,
                              color.Blue() / 255.0, color.Alpha() / 255.0);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, it->second.layout);
    }
};
#endif

// Same as dc.DrawText(). But wxGTK's DrawText() first lays out and measures the text with Pango,
// only to update the DC's bounding box and to mirror the text in right-to-left layouts, and then
// lays it out a second time to draw it. Where neither applies, draw it with the DC's graphics
// context directly, which lays it out only once, and with wxGTK 3 reuses the layout of an
// earlier call with the same text and font (see TextLayoutCache).
template<typename DC> static void DrawText(DC &dc, const wxString &text, int x, int y) {
    auto *gc = dc.GetGraphicsContext();
    if (gc != nullptr && !dc.AreAutomaticBoundingBoxUpdatesEnabled() &&
        dc.GetLayoutDirection() != wxLayout_RightToLeft &&
        dc.GetBackgroundMode() == wxBRUSHSTYLE_TRANSPARENT) {
        #if defined(__WXGTK3__) && defined(TREESHEETS_USE_PANGO)
            if (!text.empty() && dc.GetFont().IsOk()) {
                TextLayoutCache::Get().Draw(gc, dc.GetFont(), dc.GetTextForeground(), text, x, y);
                return;
            }
        #endif
        gc->DrawText(text, x, y);
    } else {
        dc.DrawText(text, x, y);
    }
}

static uint SwapColor(uint c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c & 0xFF0000) >> 16); }

struct DropTarget : wxDropTarget {
    DropTarget(wxDataObject *data) : wxDropTarget(data) {};

    wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) override {
        auto *canvas = sys->frame->GetCurrentTab();
        wxInfoDC dc(canvas);
        canvas->doc->UpdateHover(dc, x, y);
        return canvas->doc->hover.grid ? wxDragCopy : wxDragNone;
    }

    bool OnDrop(wxCoord x, wxCoord y) override {
        return sys->frame->GetCurrentTab()->doc->hover.grid != nullptr;
    }
    wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override {
        GetData();
        auto *canvas = sys->frame->GetCurrentTab();
        wxInfoDC dc(canvas);
        canvas->doc->UpdateHover(dc, x, y);
        canvas->doc->SelectClick();
        canvas->doc->Drop();
        canvas->doc->UpdateLayout();
        canvas->Refresh();
        return wxDragCopy;
    }
};

struct ThreeChoiceDialog : public wxDialog {
    ThreeChoiceDialog(wxWindow *parent, const wxString &title, const wxString &msg,
                      const wxString &ch1, const wxString &ch2, const wxString &ch3)
        : wxDialog(parent, wxID_ANY, title) {
        auto *bsv = new wxBoxSizer(wxVERTICAL);
        bsv->Add(new wxStaticText(this, -1, msg), 0, wxALL, 5);
        auto *bsb = new wxBoxSizer(wxHORIZONTAL);
        bsb->Prepend(new wxButton(this, 2, ch3), 0, wxALL, 5);
        bsb->PrependStretchSpacer(1);
        bsb->Prepend(new wxButton(this, 1, ch2), 0, wxALL, 5);
        bsb->PrependStretchSpacer(1);
        bsb->Prepend(new wxButton(this, 0, ch1), 0, wxALL, 5);
        bsv->Add(bsb, 1, wxEXPAND);
        SetSizer(bsv);
        bsv->SetSizeHints(this);

        Bind(wxEVT_BUTTON, [this](wxCommandEvent &ce) { EndModal(ce.GetId()); }, wxID_ANY);
    }
    int Run() { return ShowModal(); }
};

// Asks for the number of rows and columns of a new grid. The columns follow the rows, so that
// a square grid only needs one number, until the user changes the columns.
struct GridSizeDialog : public wxDialog {
    wxSpinCtrl *rows {nullptr};
    wxSpinCtrl *columns {nullptr};
    bool columnschanged {false};
    bool syncing {false};

    GridSizeDialog(wxWindow *parent, const wxString &title, const wxString &message, int size,
                   int maxsize)
        : wxDialog(parent, wxID_ANY, title) {
        rows = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxSP_ARROW_KEYS, 1, maxsize, size);
        columns = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxSP_ARROW_KEYS, 1, maxsize, size);
        auto *grid = new wxFlexGridSizer(2, wxSize(10, 5));
        grid->Add(new wxStaticText(this, wxID_ANY, _("Rows:")), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(rows);
        grid->Add(new wxStaticText(this, wxID_ANY, _("Columns:")), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(columns);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(this, wxID_ANY, message), 0, wxALL, 10);
        sizer->Add(grid, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
        SetSizerAndFit(sizer);

        // Typing only sends text events, whose value the control doesn't have yet.
        auto onrows = [this](const wxString &text) {
            long n = 0;
            if (columnschanged || !text.ToLong(&n) || n < 1 || n > rows->GetMax()) { return; }
            syncing = true;
            columns->SetValue(static_cast<int>(n));
            syncing = false;
        };
        rows->Bind(wxEVT_TEXT, [=](wxCommandEvent &ce) { onrows(ce.GetString()); });
        rows->Bind(wxEVT_SPINCTRL, [=](wxSpinEvent &se) {
            onrows(wxString::Format("%d", se.GetPosition()));
        });
        auto oncolumns = [this](wxCommandEvent &) {
            if (!syncing) { columnschanged = true; }
        };
        columns->Bind(wxEVT_TEXT, oncolumns);
        columns->Bind(wxEVT_SPINCTRL, oncolumns);

        rows->SetFocus();
        rows->SetSelection(-1, -1);
    }
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
    // Remembers the last chosen range across dialog instances so reopening starts
    // from where the user left off instead of resetting to "now".
    static inline wxDateTime lastbegin;
    static inline wxDateTime lastend;
    DateTimeRangeDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, _("Date and time range")) {
        if (lastbegin.IsValid() && lastend.IsValid()) {
            startdate.SetValue(lastbegin);
            starttime.SetValue(lastbegin);
            enddate.SetValue(lastend);
            endtime.SetValue(lastend);
        }
        // Lays out two controls side by side, e.g. a date picker next to its time picker.
        auto MakePairSizer = [](wxWindow *first, wxWindow *second) {
            auto *sizer = new wxFlexGridSizer(2, wxSize(5, 5));
            sizer->Add(first, 0, wxALL, 5);
            sizer->Add(second, 0, wxALL, 5);
            return sizer;
        };

        auto *topsizer = new wxFlexGridSizer(1);
        topsizer->Add(&introtext, 0, wxALL, 5);
        topsizer->Add(&starttext, 0, wxALL, 5);
        topsizer->Add(MakePairSizer(&startdate, &starttime), wxSizerFlags(1));
        topsizer->Add(&endtext, 0, wxALL, 5);
        topsizer->Add(MakePairSizer(&enddate, &endtime), wxSizerFlags(1));
        topsizer->Add(MakePairSizer(&okbtn, &cancelbtn), wxSizerFlags(1));
        SetSizerAndFit(topsizer);
        topsizer->SetSizeHints(this);

        Bind(wxEVT_BUTTON, &DateTimeRangeDialog::OnButton, this, wxID_ANY);
    }
    void OnButton(wxCommandEvent &ce) {
        if (ce.GetId() == wxID_OK) {
            auto CombineDateAndTime = [](wxDatePickerCtrl &date, wxTimePickerCtrl &time) {
                int hour = 0;
                int min = 0;
                int sec = 0;
                time.GetTime(&hour, &min, &sec);
                return date.GetValue().Add(wxTimeSpan(hour, min, sec));
            };
            begin = CombineDateAndTime(startdate, starttime);
            end = CombineDateAndTime(enddate, endtime);
            lastbegin = begin;
            lastend = end;
        }
        EndModal(ce.GetId());
    }
    int Run() { return ShowModal(); }
};

// The list of a color or image dropdown. A click on the current value applies it, the button
// opens the list.
struct DropdownPopup : wxVListBoxComboPopup {
    DropdownPopup(wxWindow *combo) {
        combo->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &me) {
            if (m_combo->IsPopupShown() || !m_combo->GetTextRect().Contains(me.GetPosition())) {
                me.Skip();
                return;
            }
            Apply();
            sys->frame->ReFocus();
        });
    }

    virtual void Apply() = 0;
    void OnComboDoubleClick() override {}

    #ifdef __WXMSW__
        // Keys go to the menu accelerators of the frame first on Windows, even with the list
        // focused. Navigating it with the cursor keys, Page Up/Down, Home/End, Return and Escape
        // would then act on the document instead, so keep all keys for the list.
        bool MSWShouldPreProcessMessage(WXMSG *msg) override {
            return msg->message != WM_KEYDOWN &&
                   wxVListBoxComboPopup::MSWShouldPreProcessMessage(msg);
        }
    #endif
};

struct ColorPopup : DropdownPopup {
    using DropdownPopup::DropdownPopup;

    void Apply() override {
        sys->frame->GetCurrentTab()->doc->ColorChange(m_combo->GetId(), GetSelection());
    }
};

struct ColorDropdown : wxOwnerDrawnComboBox {
    ColorDropdown(wxWindow *parent, wxWindowID id, int sel) {
        wxArrayString as;
        as.Add("", sizeof(celltextcolors) / sizeof(uint));
        Create(parent, id, "", wxDefaultPosition, FromDIP(wxSize(44, 22)), as,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ColorPopup(this));
        SetSelection(sel);
        SetPopupMaxHeight(wxDisplay().GetGeometry().GetHeight() * 3 / 4);
    }

    wxCoord OnMeasureItem(size_t item) const override { return FromDIP(22); }
    wxCoord OnMeasureItemWidth(size_t item) const override { return FromDIP(40); }

    void OnDrawBackground(wxDC &dc, const wxRect &rect, int item, int flags) const override {
        DrawRectangle(dc,
                      (flags & wxODCB_PAINTING_SELECTED) && !(flags & wxODCB_PAINTING_CONTROL)
                          ? 0xA9A9A9
                          : 0xFFFFFF,
                      rect.x, rect.y, rect.width, rect.height);
    }

    void OnDrawItem(wxDC &dc, const wxRect &rect, int item, int flags) const override {
        DrawRectangle(dc, item == CUSTOMCOLORIDX ? sys->customcolor : celltextcolors[item],
                      rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2);
        if (item == CUSTOMCOLORIDX) {
            dc.SetTextForeground(sys->rubberbandcolor);
            dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                              false, ""));
            dc.DrawText(_("Custom"), rect.x + 1, rect.y + 1);
        }
    }
};

static uint PickColor(wxWindow *parent, uint defaultcolor) {
    auto color = wxGetColourFromUser(parent, wxColour(defaultcolor));
    if (color.IsOk()) { return (color.Blue() << 16) + (color.Green() << 8) + color.Red(); }
    return -1;
}

static uint LightColor(uint color) { return color ^ sys->colormask; }

static const EmbeddedFile *FindEmbeddedFile(std::span<const EmbeddedFile> files,
                                             const wxString &name) {
    for (const auto &file : files) {
        if (name == file.name) { return &file; }
    }
    return nullptr;
}

// Images compiled into the executable from TS/images, by their path relative to it.
static const EmbeddedFile *GetEmbeddedImage(const wxString &name) {
    return FindEmbeddedFile(embedded_images, name);
}

static wxBitmap LoadEmbeddedBitmap(const wxString &name) {
    auto *image = GetEmbeddedImage(name);
    return image != nullptr ? wxBitmap::NewFromPNGData(image->data, image->size) : wxNullBitmap;
}

static wxBitmapBundle LoadEmbeddedSVG(const wxString &name, const wxSize &size) {
    auto *image = GetEmbeddedImage(name);
    return image != nullptr ? wxBitmapBundle::FromSVG(image->data, image->size, size)
                            : wxBitmapBundle();
}

// Loads the translations compiled into the executable from TS/translations/<language>/<domain>.mo.
struct EmbeddedTranslationsLoader : wxTranslationsLoader {
    wxMsgCatalog *LoadCatalog(const wxString &domain, const wxString &lang) override {
        auto *file = FindEmbeddedFile(embedded_translations, lang + "/" + domain + ".mo");
        if (file == nullptr) { return nullptr; }
        return wxMsgCatalog::CreateFromData(
            wxCharBuffer::CreateNonOwned(reinterpret_cast<const char *>(file->data), file->size),
            domain);
    }

    wxArrayString GetAvailableTranslations(const wxString &domain) const override {
        wxArrayString langs;
        for (const auto &file : embedded_translations) {
            wxString name = file.name;
            if (name.AfterFirst('/') == domain + ".mo") { langs.Add(name.BeforeFirst('/')); }
        }
        return langs;
    }
};

#define dd_icon_res_scale 3.0

struct ImagePopup : DropdownPopup {
    using DropdownPopup::DropdownPopup;

    void Apply() override {
        if (auto *image = GetEmbeddedImage(GetString(GetSelection()))) {
            sys->frame->GetCurrentTab()->doc->ImageChange(*image, dd_icon_res_scale);
        }
    }
};

struct ImageDropdown : wxOwnerDrawnComboBox {
    struct BitmapCacheKey {
        int item;
        int width;
        int height;

        bool operator==(const BitmapCacheKey &other) const {
            return item == other.item && width == other.width && height == other.height;
        }
    };

    struct BitmapCacheHash {
        std::size_t operator()(const BitmapCacheKey &key) const {
            return static_cast<std::size_t>(
                FNV1A64(reinterpret_cast<const uint8_t *>(&key), sizeof(BitmapCacheKey)));
        }
    };

    vector<unique_ptr<wxBitmap>> bitmaps_display;
    wxArrayString filenames;
    const int image_space = 22;
    mutable std::unordered_map<BitmapCacheKey, wxBitmap, BitmapCacheHash> scaled_bitmap_cache;

    ImageDropdown(wxWindow *parent, const wxString &directory) {
        FillBitmapVector(directory);
        Create(parent, A_DDIMAGE, "", wxDefaultPosition,
               FromDIP(wxSize(image_space * 2, image_space)), filenames,
               wxCB_READONLY | wxCC_SPECIAL_DCLICK);
        SetPopupControl(new ImagePopup(this));
        SetSelection(0);
        SetPopupMaxHeight(wxDisplay().GetGeometry().GetHeight() * 3 / 4);
    }

    wxCoord OnMeasureItem(size_t item) const override { return FromDIP(image_space); }
    wxCoord OnMeasureItemWidth(size_t item) const override { return FromDIP(image_space); }

    void OnDrawBackground(wxDC &dc, const wxRect &rect, int item, int flags) const override {
        DrawRectangle(dc,
                      (flags & wxODCB_PAINTING_SELECTED) && !(flags & wxODCB_PAINTING_CONTROL)
                          ? 0xA9A9A9
                          : 0xFFFFFF,
                      rect.x, rect.y, rect.width, rect.height, true);
    }

    void OnDrawItem(wxDC &dc, const wxRect &rect, int item, int flags) const override {
        BitmapCacheKey key {item, rect.width, rect.height};
        auto it = scaled_bitmap_cache.find(key);
        if (it == scaled_bitmap_cache.end()) {
            auto *bitmap = bitmaps_display[item].get();
            auto scale = min((static_cast<double>(rect.height) - FromDIP(6)) / bitmap->GetHeight(),
                             (static_cast<double>(rect.width) - FromDIP(6)) / bitmap->GetWidth());
            wxBitmap scaled_bitmap;
            ScaleBitmap(*bitmap, scale, scaled_bitmap);
            it = scaled_bitmap_cache.emplace(key, scaled_bitmap).first;
        }
        treesheets::System::ImageDraw(&it->second, dc, rect.x + FromDIP(3), rect.y + FromDIP(3));
    }

    void FillBitmapVector(const wxString &directory) {
        bitmaps_display.clear();
        filenames.clear();
        scaled_bitmap_cache.clear();
        for (const auto &image : embedded_images) {
            wxString name = image.name;
            if (!name.StartsWith(directory) || !name.EndsWith(".png")) { continue; }
            auto bitmap = make_unique<wxBitmap>(wxBitmap::NewFromPNGData(image.data, image.size));
            if (bitmap->IsOk()) {
                bitmaps_display.push_back(std::move(bitmap));
                filenames.Add(name);
            }
        }
    }
};

static void ScaleBitmap(const wxBitmap &source, double scale, wxBitmap &destination) {
    destination = wxBitmap(source.ConvertToImage().Scale(
        source.GetWidth() * scale, source.GetHeight() * scale, wxIMAGE_QUALITY_HIGH));
}

static vector<uint8_t> ConvertWxImageToBuffer(const wxImage &image, wxBitmapType bitmaptype) {
    wxMemoryOutputStream imageoutputstream(nullptr, 0);
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
    return FNV1A64(buffer.data(), buffer.size());
}

static void GetFilesFromUser(wxArrayString &filenames, wxWindow *parent, const wxString &title,
                             const wxString &filter, const wxString &defaultdir = "") {
    wxFileDialog filedialog(parent, title, defaultdir, "", filter,
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR | wxFD_MULTIPLE);
    if (filedialog.ShowModal() == wxID_OK) { filedialog.GetPaths(filenames); }
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
