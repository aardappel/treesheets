struct MacClipboardResult {
    wxImage image;
    double scale_factor {1.0};
};

MacClipboardResult GetImageFromMacClipboard();

// Function keys like the arrows go to the key window directly when bypass() is true, see TSCanvas.
void BypassMacMenuForFunctionKeys(bool (*bypass)());
