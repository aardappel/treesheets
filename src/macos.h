struct MacClipboardResult {
    wxImage image;
    double scale_factor {1.0};
};

MacClipboardResult GetImageFromMacClipboard();

// Function keys like the arrows go to the key window directly when bypass() is true, see TSCanvas.
void BypassMacMenuForFunctionKeys(bool (*bypass)());

// Shows the suggestions of the wxTextCompleter of the text control with native handle `field`.
void ShowMacCompletions(WXWidget field);

// Brings TreeSheets in front of the active application, which raising a window alone doesn't.
void ActivateMacApp();
