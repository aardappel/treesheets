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

    Image(auto _hash, auto _sc, auto &&_data, auto _type)
        : hash(_hash), display_scale(_sc), data(std::move(_data)), type(_type) {}

    void ImageRescale(double sc) {
        if (!imagetypes.contains(type)) return;
        auto &[it, mime] = imagetypes.at(type);
        auto im = ConvertBufferToWxImage(data, it);
        im.Rescale(im.GetWidth() * sc, im.GetHeight() * sc);
        data = ConvertWxImageToBuffer(im, it);
        hash = CalculateHash(data);
        bm_display = wxNullBitmap;
    }

    void DisplayScale(double sc) {
        display_scale /= sc;
        bm_display = wxNullBitmap;
    }

    void ResetScale(double sc) {
        display_scale = sc;
        bm_display = wxNullBitmap;
    }

    wxBitmap &Display() {
        // This might run in multiple threads in parallel
        // so this function must not touch any global resources
        // and callees must be thread-safe.
        if (!bm_display.IsOk()) {
            if (!imagetypes.contains(type)) return wxNullBitmap;
            auto &[it, mime] = imagetypes.at(type);
            auto bm = ConvertBufferToWxBitmap(data, it);
            pixel_width = bm.GetWidth();
            ScaleBitmap(bm, sys->frame->FromDIP(1.0) / display_scale, bm_display);
        }
        return bm_display;
    }
};
