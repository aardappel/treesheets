struct MacClipboardResult {
    wxImage image;
    double scale_factor {1.0};
};

MacClipboardResult GetImageFromMacClipboard();