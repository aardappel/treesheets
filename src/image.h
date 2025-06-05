struct Image {
    vector<uint8_t> data;
    char type;
    wxBitmap bm_display;
    int trefc {0};
    int savedindex {-1};
    uint64_t hash {0};

    // This indicates a relative scale, where 1.0 means bitmap pixels match display pixels on
    // a low res 96 dpi display. On a high dpi screen it will look scaled up. Higher values
    // look better on most screens.
    // This is all relative to GetContentScalingFactor.
    double display_scale;
    int pixel_width {0};

    Image(auto _hash, auto _sc, auto &&idv, auto iti)
        : data(std::move(idv)), type(iti), hash(_hash), display_scale(_sc) {}

    auto ImageRescale(auto sc) {
        auto mapitem = imagetypes.find(type);
        if (mapitem == imagetypes.end()) return;
        auto it = mapitem->second.first;
        auto im = ConvertBufferToWxImage(data, it);
        im.Rescale(im.GetWidth() * sc, im.GetHeight() * sc);
        data = ConvertWxImageToBuffer(im, it);
        hash = CalculateHash(data);
        bm_display = wxNullBitmap;
    }

    auto DisplayScale(auto sc) {
        display_scale /= sc;
        bm_display = wxNullBitmap;
    }

    auto ResetScale(auto sc) {
        display_scale = sc;
        bm_display = wxNullBitmap;
    }

    auto &Display() {
        // This might run in multiple threads in parallel
        // so this function must not touch any global resources
        // and callees must be thread-safe.
        if (!bm_display.IsOk()) {
            auto mapitem = imagetypes.find(type);
            if (mapitem == imagetypes.end()) return wxNullBitmap;
            auto it = mapitem->second.first;
            auto bm = ConvertBufferToWxBitmap(data, it);
            pixel_width = bm.GetWidth();
            ScaleBitmap(bm, sys->frame->FromDIP(1.0) / display_scale, bm_display);
        }
        return bm_display;
    }
};
