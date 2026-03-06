// AI Chat Panel for TreeSheets — Docked right-side panel with chat history,
// model switching, copy/insert buttons, and session persistence.

struct AIChatPanel : wxPanel {
    // Chat message
    struct ChatMessage {
        bool isUser;
        wxString text;
    };

    // UI components
    wxChoice *modelChoice {nullptr};
    wxScrolledWindow *chatScroll {nullptr};
    wxBoxSizer *chatSizer {nullptr};
    wxTextCtrl *promptInput {nullptr};
    wxButton *sendBtn {nullptr};
    wxButton *cancelBtn {nullptr};
    wxButton *newSessionBtn {nullptr};
    wxButton *historyBtn {nullptr};
    wxStaticText *statusLabel {nullptr};

    // State
    vector<ChatMessage> messages;
    bool requestInProgress {false};
    wxString currentSessionFile;

    // Dark mode palette — used throughout the panel
    static constexpr unsigned long DK_BG       = 0x1E1E1E;  // main panel bg
    static constexpr unsigned long DK_SURFACE  = 0x252526;  // message area bg
    static constexpr unsigned long DK_USER     = 0x1E3A5F;  // user message bubble
    static constexpr unsigned long DK_AI       = 0x2D2D2D;  // AI message bubble
    static constexpr unsigned long DK_INPUT    = 0x3C3C3C;  // prompt input bg
    static constexpr unsigned long DK_BORDER   = 0x555555;  // separator / borders
    static constexpr unsigned long DK_TEXT     = 0xDDDDDD;  // primary text
    static constexpr unsigned long DK_MUTED    = 0x999999;  // muted / status text
    static constexpr unsigned long DK_ACCENT   = 0x4FC3F7;  // user header highlight
    static constexpr unsigned long DK_AI_HDR   = 0x81C784;  // AI header highlight
    static constexpr unsigned long DK_ERR_BG   = 0x3E1A1A;  // error panel bg
    static constexpr unsigned long DK_ERR_TEXT = 0xFF6B6B;  // error text

    AIChatPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY) {
        // Apply dark background to the panel itself
        SetBackgroundColour(wxColour(DK_BG));

        auto *mainSizer = new wxBoxSizer(wxVERTICAL);

        // Header: Model selector + session buttons
        auto *headerSizer = new wxBoxSizer(wxHORIZONTAL);
        auto *modelLabel = new wxStaticText(this, wxID_ANY, _(L"Model:"));
        modelLabel->SetForegroundColour(wxColour(DK_TEXT));
        headerSizer->Add(modelLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        modelChoice = new wxChoice(this, wxID_ANY);
        modelChoice->SetBackgroundColour(wxColour(DK_INPUT));
        modelChoice->SetForegroundColour(wxColour(DK_TEXT));
        headerSizer->Add(modelChoice, 1, wxEXPAND | wxRIGHT, 5);
        newSessionBtn = new wxButton(this, wxID_ANY, _(L"New"),
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        newSessionBtn->SetToolTip(_(L"Start a new chat session"));
        newSessionBtn->SetBackgroundColour(wxColour(DK_INPUT));
        newSessionBtn->SetForegroundColour(wxColour(DK_TEXT));
        historyBtn = new wxButton(this, wxID_ANY, _(L"History"),
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        historyBtn->SetToolTip(_(L"Load a previous chat session"));
        historyBtn->SetBackgroundColour(wxColour(DK_INPUT));
        historyBtn->SetForegroundColour(wxColour(DK_TEXT));
        headerSizer->Add(newSessionBtn, 0, wxRIGHT, 2);
        headerSizer->Add(historyBtn, 0);
        mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 5);

        // Chat display (scrollable)
        chatScroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxVSCROLL | wxBORDER_NONE);
        chatScroll->SetScrollRate(0, 10);
        chatScroll->SetBackgroundColour(wxColour(DK_SURFACE));
        chatSizer = new wxBoxSizer(wxVERTICAL);
        chatScroll->SetSizer(chatSizer);
        mainSizer->Add(chatScroll, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

        // Status
        statusLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
        statusLabel->SetForegroundColour(wxColour(DK_MUTED));
        statusLabel->SetBackgroundColour(wxColour(DK_BG));
        mainSizer->Add(statusLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

        // Input area
        promptInput = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxSize(-1, 60),
                                     wxTE_MULTILINE | wxTE_PROCESS_ENTER | wxWANTS_CHARS);
        promptInput->SetHint(_(L"Type your message... (Enter to send, Shift+Enter for newline)"));
        promptInput->SetBackgroundColour(wxColour(DK_INPUT));
        promptInput->SetForegroundColour(wxColour(DK_TEXT));
        mainSizer->Add(promptInput, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

        // Bottom bar: Cancel + Send buttons
        auto *bottomSizer = new wxBoxSizer(wxHORIZONTAL);
        bottomSizer->AddStretchSpacer();
        cancelBtn = new wxButton(this, wxID_ANY, _(L"Cancel"));
        cancelBtn->SetToolTip(_(L"Cancel the in-flight request"));
        cancelBtn->Enable(false);
        bottomSizer->Add(cancelBtn, 0, wxRIGHT, 6);
        sendBtn = new wxButton(this, wxID_ANY, _(L"Send"));
        bottomSizer->Add(sendBtn, 0);
        mainSizer->Add(bottomSizer, 0, wxEXPAND | wxALL, 5);

        SetSizer(mainSizer);

        // Event bindings
        sendBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnSend(); });
        cancelBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnCancel(); });
        newSessionBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnNewSession(); });
        historyBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnShowHistory(); });
        modelChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { OnModelChange(); });

        // Enter to send, Shift+Enter for newline
        promptInput->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &evt) {
            if (evt.GetKeyCode() == WXK_RETURN || evt.GetKeyCode() == WXK_NUMPAD_ENTER) {
                if (!evt.ShiftDown()) {
                    OnSend();
                } else {
                    promptInput->WriteText(L"\n"); // Explicit newline for Shift+Enter
                }
            } else {
                evt.Skip();
            }
        });

        // Give promptInput focus whenever the panel becomes visible.
        // CallAfter defers until after AUI layout settles (more reliable than
        // calling SetFocus() directly during the show event).
        Bind(wxEVT_SHOW, [this](wxShowEvent &evt) {
            if (evt.IsShown()) {
                CallAfter([this]() {
                    if (promptInput) promptInput->SetFocus();
                });
            }
            evt.Skip();
        });

        // Clicking anywhere on the chat scroll area (not a button or input)
        // redirects keyboard focus back to promptInput.
        chatScroll->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &evt) {
            promptInput->SetFocus();
            evt.Skip();
        });

        RefreshModelList();
    }

    // ---- Model Management ----

    void RefreshModelList() {
        modelChoice->Clear();
        auto count = sys->cfg->Read(L"ai_models_count", 0L);
        if (count == 0) {
            // Legacy: single model config
            if (!sys->ai_api_key.IsEmpty() || !sys->ai_model.IsEmpty()) {
                wxString name = sys->ai_model.IsEmpty() ? wxString(L"Default") : sys->ai_model;
                modelChoice->Append(name);
                modelChoice->SetSelection(0);
            } else {
                modelChoice->Append(_(L"(No models configured)"));
                modelChoice->SetSelection(0);
            }
        } else {
            for (long i = 0; i < count; i++) {
                auto name = sys->cfg->Read(
                    wxString::Format(L"ai_model_%ld_name", i),
                    wxString::Format(L"Model %ld", i + 1));
                modelChoice->Append(name);
            }
            auto active = sys->cfg->Read(L"ai_active_model", 0L);
            if (active >= 0 && active < count)
                modelChoice->SetSelection(static_cast<int>(active));
            else if (count > 0)
                modelChoice->SetSelection(0);
        }
    }

    struct AIModelConfig {
        wxString name;
        wxString api_key;
        wxString api_url;
        wxString model_id;
    };

    AIModelConfig GetActiveModelConfig() {
        AIModelConfig config;
        auto count = sys->cfg->Read(L"ai_models_count", 0L);
        if (count == 0) {
            // Legacy single model
            config.name = sys->ai_model;
            config.api_key = sys->ai_api_key;
            config.api_url = sys->ai_api_url;
            config.model_id = sys->ai_model;
        } else {
            auto idx = modelChoice->GetSelection();
            if (idx == wxNOT_FOUND) idx = 0;
            config.name = sys->cfg->Read(wxString::Format(L"ai_model_%ld_name", static_cast<long>(idx)), wxEmptyString);
            config.api_key = sys->cfg->Read(wxString::Format(L"ai_model_%ld_key", static_cast<long>(idx)), wxEmptyString);
            config.api_url = sys->cfg->Read(wxString::Format(L"ai_model_%ld_url", static_cast<long>(idx)), wxEmptyString);
            config.model_id = sys->cfg->Read(wxString::Format(L"ai_model_%ld_id", static_cast<long>(idx)), wxEmptyString);
        }
        return config;
    }

    void OnModelChange() {
        auto idx = modelChoice->GetSelection();
        if (idx != wxNOT_FOUND) {
            sys->cfg->Write(L"ai_active_model", static_cast<long>(idx));
        }
    }

    // ---- Chat Display ----

    void AddMessageToDisplay(bool isUser, const wxString &text) {
        auto *msgPanel = new wxPanel(chatScroll, wxID_ANY);
        // Dark mode bubble colors: blue-tinted for user, dark grey for AI
        msgPanel->SetBackgroundColour(wxColour(isUser ? DK_USER : DK_AI));

        auto *msgSizer = new wxBoxSizer(wxVERTICAL);

        // Header label
        auto *headerLabel = new wxStaticText(msgPanel, wxID_ANY, isUser ? _(L"You:") : _(L"AI:"));
        auto headerFont = headerLabel->GetFont();
        headerFont.SetWeight(wxFONTWEIGHT_BOLD);
        headerFont.SetPointSize(max(8, headerFont.GetPointSize() - 1));
        headerLabel->SetFont(headerFont);
        // Accent blue for user, green for AI
        headerLabel->SetForegroundColour(wxColour(isUser ? DK_ACCENT : DK_AI_HDR));
        headerLabel->SetBackgroundColour(wxColour(isUser ? DK_USER : DK_AI));
        msgSizer->Add(headerLabel, 0, wxLEFT | wxRIGHT | wxTOP, 5);

        // Message text (read-only, selectable).
        // Use wxTE_RICH2 on Windows; harmlessly ignored on macOS/Linux.
        auto *msgText = new wxTextCtrl(msgPanel, wxID_ANY, text,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_READONLY | wxTE_NO_VSCROLL |
                                       wxBORDER_SIMPLE | wxTE_RICH2);
        msgText->SetBackgroundColour(wxColour(isUser ? DK_USER : DK_AI));
        msgText->SetForegroundColour(wxColour(DK_TEXT));
        // Height estimate: count real newlines + estimate wraps from line length.
        // GetNumberOfLines() is unreliable before layout, so approximate instead.
        int explicitLines = text.Freq(L'\n') + 1;
        int wrapLines     = max(1, (int)(text.Length() / 60));  // ~60 chars/line estimate
        int estLines      = max(explicitLines, wrapLines);
        estLines          = min(estLines, 20);                   // cap at 20 to avoid giant panels
        int lineHeight    = max(16, msgText->GetCharHeight());
        msgText->SetMinSize(wxSize(-1, (estLines + 1) * lineHeight));
        msgSizer->Add(msgText, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

        // Action buttons for AI messages — visible, standard-bordered buttons
        if (!isUser) {
            // Thin separator (1px panel) above buttons — dark border colour
            auto *sep = new wxPanel(msgPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
            sep->SetBackgroundColour(wxColour(DK_BORDER));
            msgSizer->Add(sep, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

            auto *actionSizer = new wxBoxSizer(wxHORIZONTAL);
            actionSizer->AddStretchSpacer();

            // Copy button — dark-styled, clearly visible on dark background
            auto *copyBtn = new wxButton(msgPanel, wxID_ANY, _(L"Copy"),
                                         wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            copyBtn->SetToolTip(_(L"Copy AI response to clipboard"));
            copyBtn->SetBackgroundColour(wxColour(DK_INPUT));
            copyBtn->SetForegroundColour(wxColour(DK_TEXT));

            // Insert button
            auto *insertBtn = new wxButton(msgPanel, wxID_ANY, _(L"Insert into Cell"),
                                           wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            insertBtn->SetToolTip(_(L"Insert AI response into the selected TreeSheets cell"));
            insertBtn->SetBackgroundColour(wxColour(DK_INPUT));
            insertBtn->SetForegroundColour(wxColour(DK_TEXT));

            actionSizer->Add(copyBtn, 0, wxRIGHT, 5);
            actionSizer->Add(insertBtn, 0);
            msgSizer->Add(actionSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

            wxString responseText = text;
            copyBtn->Bind(wxEVT_BUTTON, [responseText](wxCommandEvent &) {
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxTextDataObject(responseText));
                    wxTheClipboard->Close();
                }
            });

            insertBtn->Bind(wxEVT_BUTTON, [this, responseText](wxCommandEvent &) {
                InsertIntoCell(responseText);
            });
        }

        msgPanel->SetSizer(msgSizer);
        chatSizer->Add(msgPanel, 0, wxEXPAND | wxALL, 3);

        // Update layout and scroll to bottom
        chatScroll->FitInside();
        chatScroll->Layout();
        CallAfter([this]() {
            int vx, vy;
            chatScroll->GetVirtualSize(&vx, &vy);
            int ppuX, ppuY;
            chatScroll->GetScrollPixelsPerUnit(&ppuX, &ppuY);
            if (ppuY > 0) chatScroll->Scroll(0, vy / ppuY);
        });
    }

    void ClearChatDisplay() {
        chatSizer->Clear(true);
        chatScroll->FitInside();
        chatScroll->Layout();
    }

    // ---- Cell Interaction ----

    // Strip leading/trailing Markdown code fences (``` ... ```) from AI responses.
    // If the text contains a fenced code block, extract and return only the content
    // inside it. If there are no fences, return the original text unchanged.
    static wxString StripCodeFences(const wxString &input) {
        wxString text = input;
        text.Trim(true).Trim(false);

        // Must contain at least one set of triple backticks
        if (!text.Contains(L"```")) return text;

        // Find opening ``` (may be followed by a language tag, e.g. ```tsv)
        int openPos = text.Find(L"```");
        if (openPos == wxNOT_FOUND) return text;

        // Skip to the end of the opening line (past any language identifier)
        wxString afterOpen = text.Mid(openPos + 3);
        int nlPos = afterOpen.Find(L'\n');
        if (nlPos == wxNOT_FOUND) return text;  // Malformed — no newline after fence

        // Content starts on the next line after the opening fence line
        wxString content = afterOpen.Mid(nlPos + 1);

        // Find the closing ``` and truncate there
        int closePos = content.Find(L"```");
        if (closePos != wxNOT_FOUND) {
            content = content.Left(closePos);
        }

        // Strip trailing newline/whitespace that precedes the closing fence
        content.Trim(true);
        return content.IsEmpty() ? text : content;
    }

    Document *GetCurrentDoc() {
        if (!sys->frame || !sys->frame->notebook || !sys->frame->notebook->GetPageCount())
            return nullptr;
        auto *canvas = sys->frame->GetCurrentTab();
        return canvas ? canvas->doc : nullptr;
    }

    void InsertIntoCell(const wxString &rawText) {
        auto *doc = GetCurrentDoc();
        if (!doc) {
            statusLabel->SetLabel(_(L"No document open."));
            return;
        }
        auto *cell = doc->selected.GetCell();
        if (!cell) {
            statusLabel->SetLabel(_(L"No cell selected."));
            return;
        }
        // Strip Markdown code fences (``` ... ```) so only the inner TSV/text is inserted
        wxString text = StripCodeFences(rawText);
        cell->AddUndo(doc);
        if (cell->text.t.IsEmpty()) {
            cell->text.t = text;
        } else {
            cell->text.t += "\n" + text;
        }
        cell->text.WasEdited();
        doc->canvas->Refresh();
        statusLabel->SetLabel(_(L"Inserted into cell."));
    }

    // ---- Sending ----

    // Cancel the in-flight request (invalidates generation so callback is ignored)
    void OnCancel() {
        if (!requestInProgress) return;
        ++AIAssistant::requestGeneration;   // stale-callback guard fires immediately
        requestInProgress = false;
        sendBtn->Enable(true);
        cancelBtn->Enable(false);
        statusLabel->SetLabel(_(L"Request cancelled."));
    }

    void OnSend() {
        wxString prompt = promptInput->GetValue();
        prompt.Trim().Trim(false);
        if (prompt.IsEmpty()) return;
        if (requestInProgress) return;

        auto config = GetActiveModelConfig();
        if (config.api_key.IsEmpty()) {
            statusLabel->SetLabel(_(L"No API key configured. Go to AI > AI Settings."));
            return;
        }
        if (config.api_url.IsEmpty()) {
            statusLabel->SetLabel(_(L"No API URL configured. Go to AI > AI Settings."));
            return;
        }
        if (config.model_id.IsEmpty()) {
            statusLabel->SetLabel(_(L"Model ID is empty. Check AI Settings."));
            return;
        }

        // Add user message
        messages.push_back({true, prompt});
        AddMessageToDisplay(true, prompt);
        promptInput->Clear();

        // Build context from all open tabs
        wxString docContext = L"";
        if (sys->frame && sys->frame->notebook) {
            for (size_t i = 0; i < sys->frame->notebook->GetPageCount(); i++) {
                auto *canvas = static_cast<TSCanvas *>(sys->frame->notebook->GetPage(i));
                if (canvas && canvas->doc && canvas->doc->root && canvas->doc->root->grid) {
                    auto docSel = canvas->doc->root->grid->SelectAll();
                    wxString tabText = canvas->doc->root->grid->ConvertToText(docSel, 0, A_EXPTEXT, canvas->doc, false, canvas->doc->root);
                    wxString tabName = sys->frame->notebook->GetPageText(i);
                    docContext += L"--- TAB: " + tabName + L" ---\n";
                    docContext += tabText + L"\n\n";
                }
            }
        }
        if (docContext.IsEmpty()) docContext = L"No open tabs.";

        wxString systemPrompt =
            L"You are an AI assistant embedded in TreeSheets, a hierarchical data organizer. "
            L"Below is the complete content of all open TreeSheets document tabs. "
            L"Use them as context when answering questions.\n\n"
            L"--- DOCUMENT CONTENT ---\n" + docContext + L"--- END DOCUMENT CONTENT ---\n\n"
            L"OUTPUT FORMAT RULE (strictly enforced):\n"
            L"Whenever you return structured or tabular data, output it as Tab-Separated Values (TSV). "
            L"Place the TSV data inside a single raw text code block (``` ... ```) so tab spacing is "
            L"preserved exactly. Do NOT use Markdown table formatting (no | vertical bars, no dashes). "
            L"Use real tab characters (\\t) to separate columns and standard line breaks for rows. "
            L"The first row must contain column headings. "
            L"For non-tabular conversational answers, plain text is fine.";

        // Build conversation messages for the API
        vector<pair<wxString, wxString>> apiMessages;
        apiMessages.push_back({L"system", systemPrompt});
        for (auto &msg : messages) {
            apiMessages.push_back({msg.isUser ? L"user" : L"assistant", msg.text});
        }

        statusLabel->SetLabel(_(L"Sending... (click Cancel to abort)"));
        sendBtn->Enable(false);
        cancelBtn->Enable(true);
        requestInProgress = true;

        AIAssistant::SendConversation(
            this,
            config.api_url,
            config.api_key,
            config.model_id,
            apiMessages,
            [this](bool success, const wxString &response) {
                requestInProgress = false;
                sendBtn->Enable(true);
                cancelBtn->Enable(false);
                if (success) {
                    messages.push_back({false, response});
                    AddMessageToDisplay(false, response);
                    statusLabel->SetLabel(_(L"Response received."));
                    SaveCurrentSession();
                } else {
                    // Build human-readable hint for common errors
                    wxString hint;
                    if (response.Contains("429"))
                        hint = L"\n\nHint: 429 = rate limit hit. Wait a moment and try again.";
                    else if (response.Contains("401") || response.Contains("403"))
                        hint = L"\n\nHint: Auth error. Check your API key in AI > AI Settings.";
                    else if (response.Contains("404"))
                        hint = L"\n\nHint: Endpoint not found. Check the API URL in AI > AI Settings.";
                    statusLabel->SetLabel(_(L"Request failed — see error above."));
                    // Show error panel (not added to conversation history) — dark styled
                    auto *errPanel = new wxPanel(chatScroll, wxID_ANY);
                    errPanel->SetBackgroundColour(wxColour(DK_ERR_BG));
                    auto *errSizer = new wxBoxSizer(wxVERTICAL);
                    auto *errText = new wxStaticText(errPanel, wxID_ANY, L"\u26A0 " + response + hint);
                    errText->SetForegroundColour(wxColour(DK_ERR_TEXT));
                    errText->SetBackgroundColour(wxColour(DK_ERR_BG));
                    errText->Wrap(320);
                    errSizer->Add(errText, 0, wxALL, 8);
                    errPanel->SetSizer(errSizer);
                    chatSizer->Add(errPanel, 0, wxEXPAND | wxALL, 3);
                    chatScroll->FitInside();
                    chatScroll->Layout();
                }
            });
    }

    // ---- Session Management ----

    wxString GetHistoryDir() {
        auto dir = wxStandardPaths::Get().GetUserDataDir() + wxFileName::GetPathSeparator() +
                   L"ai_chat_history";
        if (!wxDirExists(dir)) wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        return dir;
    }

    void OnNewSession() {
        if (!messages.empty()) SaveCurrentSession();
        messages.clear();
        ClearChatDisplay();
        currentSessionFile.Clear();
        statusLabel->SetLabel(_(L"New session started."));
    }

    void SaveCurrentSession() {
        if (messages.empty()) return;

        auto dir = GetHistoryDir();
        if (currentSessionFile.IsEmpty()) {
            auto now = wxDateTime::Now();
            currentSessionFile = dir + wxFileName::GetPathSeparator() +
                                 now.Format(L"session_%Y-%m-%d_%H-%M-%S.chat");
        }

        wxFFile file(currentSessionFile, L"w");
        if (!file.IsOpened()) return;

        // Header
        auto config = GetActiveModelConfig();
        file.Write(L"META:model=" + config.model_id + L"\n");

        auto *doc = GetCurrentDoc();
        if (doc && !doc->filename.IsEmpty()) {
            file.Write(L"META:document=" + wxFileName(doc->filename).GetFullName() + L"\n");
        }
        file.Write(L"META:timestamp=" + wxDateTime::Now().FormatISOCombined() + L"\n");

        // Messages - use Base64 encoding for content to handle any text
        for (auto &msg : messages) {
            wxString role = msg.isUser ? L"U" : L"A";
            auto utf8 = msg.text.ToUTF8();
            auto encoded = wxBase64Encode(utf8.data(), utf8.length());
            file.Write(L"MSG:" + role + L":" + encoded + L"\n");
        }
    }

    bool LoadSession(const wxString &filepath) {
        wxFFile file(filepath, L"r");
        if (!file.IsOpened()) return false;

        wxString content;
        if (!file.ReadAll(&content)) return false;

        messages.clear();
        ClearChatDisplay();

        auto lines = wxStringTokenize(content, L"\n");
        for (auto &line : lines) {
            if (line.StartsWith(L"MSG:")) {
                auto rest = line.Mid(4);
                bool isUser = rest.StartsWith(L"U:");
                auto encoded = rest.Mid(2);
                wxMemoryBuffer decoded = wxBase64Decode(encoded);
                wxString text = wxString::FromUTF8(
                    static_cast<const char *>(decoded.GetData()), decoded.GetDataLen());
                messages.push_back({isUser, text});
                AddMessageToDisplay(isUser, text);
            }
        }

        currentSessionFile = filepath;
        statusLabel->SetLabel(_(L"Session loaded."));
        return true;
    }

    void OnShowHistory() {
        auto dir = GetHistoryDir();

        // Collect session files
        wxArrayString sessionFiles;
        wxArrayString displayNames;

        auto sf = wxFindFirstFile(dir + wxFileName::GetPathSeparator() + L"*.chat");
        while (!sf.empty()) {
            sessionFiles.Add(sf);
            // Parse the file to get a display name
            wxString displayName = wxFileName(sf).GetName();
            // Try to extract model and timestamp from file
            wxFFile f(sf, L"r");
            if (f.IsOpened()) {
                wxString fullContent;
                f.ReadAll(&fullContent);
                auto lines = wxStringTokenize(fullContent, L"\n");
                wxString model, timestamp, document;
                for (auto &l : lines) {
                    if (l.StartsWith(L"META:model=")) model = l.Mid(11);
                    else if (l.StartsWith(L"META:timestamp=")) timestamp = l.Mid(15);
                    else if (l.StartsWith(L"META:document=")) document = l.Mid(14);
                    if (!model.IsEmpty() && !timestamp.IsEmpty()) break;
                }
                if (!timestamp.IsEmpty()) displayName = timestamp;
                if (!model.IsEmpty()) displayName += L" [" + model + L"]";
                if (!document.IsEmpty()) displayName += L" - " + document;
            }
            displayNames.Add(displayName);
            sf = wxFindNextFile();
        }

        if (sessionFiles.IsEmpty()) {
            wxMessageBox(_(L"No saved chat sessions found."), _(L"Chat History"), wxOK, this);
            return;
        }

        // Sort by name (which includes timestamp) in reverse order (newest first)
        // Simple bubble sort for small arrays
        for (size_t i = 0; i < displayNames.GetCount(); i++) {
            for (size_t j = i + 1; j < displayNames.GetCount(); j++) {
                if (displayNames[j] > displayNames[i]) {
                    displayNames[i].swap(displayNames[j]);
                    sessionFiles[i].swap(sessionFiles[j]);
                }
            }
        }

        wxSingleChoiceDialog dlg(this, _(L"Select a chat session to restore:"),
                                 _(L"Chat History"), displayNames);
        dlg.SetSize(wxSize(500, 400));
        if (dlg.ShowModal() == wxID_OK) {
            int sel = dlg.GetSelection();
            if (sel >= 0 && sel < (int)sessionFiles.GetCount()) {
                if (!messages.empty()) SaveCurrentSession();
                LoadSession(sessionFiles[sel]);
            }
        }
    }
};
