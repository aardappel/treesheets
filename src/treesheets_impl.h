struct TreeSheetsScriptImpl : public ScriptInterface {
    Document *document = nullptr;
    Cell *current = nullptr;
    Cell *lowestcommonancestor = nullptr;
    unique_ptr<Cell> script_clipboard;
    // Set while ScriptRun() is executing. A script that opens a modal dialog (e.g. Save As)
    // runs a nested event loop, which can deliver another agent request or menu action; a
    // nested ScriptRun() would reset document/current underneath the outer script.
    bool running = false;
    // The document whose selection the script set with select()/select_range(), which is
    // brought into view once the script is done.
    Document *selecteddoc = nullptr;

    void SwitchToCurrentDocument() {
        document = sys->frame->GetCurrentTab()->doc.get();
        current = document->root.get();
        lowestcommonancestor = nullptr;
    }

    void AddUndoIfNecessary() {
        if (lowestcommonancestor == nullptr) {
            UpdateLowestCommonAncestor(true);
        } else {
            for (auto *p = current; p != nullptr; p = p->parent) {
                if (p == lowestcommonancestor) {
                    // There is no need to add current to the undo stack as
                    // lowestcommonancestor including subordinated current
                    // is already in there.
                    return;
                }
            }
            UpdateLowestCommonAncestor(false);
        }
    }

    void UpdateLowestCommonAncestor(bool newgeneration) {
        // Use parent as lowestcommonancestor so changes to siblings are already covered.
        // The root has no parent, so there it is its own undo scope.
        lowestcommonancestor = current->parent != nullptr ? current->parent : current;
        document->AddUndo(lowestcommonancestor, newgeneration);
    }

    // If code is non-empty it is run directly (filename is then only used to label errors),
    // otherwise filename is loaded from disk.
    std::string ScriptRun(const char *filename, std::string_view code = {}) {
        if (running) return "a script is already running";
        running = true;
        selecteddoc = nullptr;
        SwitchToCurrentDocument();

        bool dump_builtins = false;
        #ifdef _DEBUG
            //dump_builtins = true;
        #endif

        // RunLobster() only catches (and converts to a returned error string) exceptions of
        // type `string` (Lobster compile errors / RUNTIME_ASSERT failures). Anything else that
        // escapes the VM/JIT layer must not be allowed to propagate further: the agent socket's
        // HandleLine() (agent_server.h) has no try/catch of its own around this call, so an
        // uncaught exception here would skip straight past writing a socket reply, silently
        // dropping the response to that request while the rest of the app (wx's event loop)
        // carries on as if nothing happened -- indistinguishable from the server having hung,
        // except it hasn't, which makes it far harder to diagnose than an outright hang.
        std::string errormessage;
        try {
            errormessage = RunLobster(filename, code, dump_builtins);
        } catch (const std::exception &e) {
            errormessage = std::string("internal error running script: ") + e.what();
        } catch (...) {
            errormessage = "internal error running script: unknown exception";
        }

        document->root->ResetChildren();
        document->UpdateLayout();
        document->canvas->Refresh();
        if (selecteddoc != nullptr) {
            // Later changes by the script may have removed the selected cells again.
            auto &s = selecteddoc->selected;
            if (!s.grid || !ContainsGrid(selecteddoc->root.get(), s.grid.get()) ||
                s.x + s.xs > s.grid->xs || s.y + s.ys > s.grid->ys) {
                selecteddoc->SetSelect();
            } else if (selecteddoc == document) {
                document->ScrollOrZoom(true);
                sys->frame->UpdateStatus(document->selected, true);
            }
            selecteddoc = nullptr;
        }

        document = nullptr;
        current = nullptr;
        running = false;

        return errormessage;
    }

    bool LoadDocument(const char *filename) override {
        auto message = sys->LoadDB(filename);
        if (!message.IsEmpty()) { return false; }

        SwitchToCurrentDocument();
        return true;
    }

    // Opens a new, unsaved tab (same as the "New" menu action / startup default) with a root
    // grid of the given size, and makes it current.
    void NewDocument(int cols, int rows) override {
        sys->InitDB(std::max(cols, 1), std::max(rows, 1));
        SwitchToCurrentDocument();
    }

    // Same as the "Save" (saveas=false) / "Save As" (saveas=true) menu actions. If saveas is
    // false and the document has no filename yet, this falls back to the save dialog same as
    // Save As. Returns false if the save failed or the dialog was cancelled.
    bool SaveDocument(bool saveas) override {
        bool success = false;
        document->Save(saveas, &success);
        return success;
    }

    // Saves to an explicit filename without ever showing the Save As dialog, unlike
    // SaveDocument(true). Mirrors LoadDocument() in taking a raw path.
    bool SaveDocumentAs(const char *filename) override {
        document->ChangeFileName(wxString::FromUTF8(filename), true);
        bool success = false;
        document->SaveDB(&success);
        return success;
    }

    void GoToRoot() override { current = document->root.get(); }
    void GoToView() override { current = document->currentdrawroot; }
    bool HasSelection() override { return document->selected.grid != nullptr; }
    void GoToSelection() override {
        auto *cell = document->selected.GetFirst();
        if (cell != nullptr) { current = cell; }
    }
    bool HasParent() override { return current->parent != nullptr; }
    void GoToParent() override {
        if (current->parent != nullptr) { current = current->parent; }
    }
    int NumChildren() override { return current->grid ? current->grid->xs * current->grid->ys : 0; }

    icoord NumColumnsRows() override {
        return current->grid ? icoord(current->grid->xs, current->grid->ys) : icoord(0, 0);
    }

    bool IsGrid() override { return current->grid != nullptr; }

    int GetCellType() override { return current->celltype; }

    bool IsFolded() override { return current->grid && current->grid->folded; }

    void SetFolded(bool folded) override {
        if (current->grid) {
            AddUndoIfNecessary();
            current->grid->folded = folded;
        }
    }

    // Searches the subtree of the current cell (including itself) for a cell whose text
    // exactly equals `text`, and makes it current if found.
    bool FindExact(std::string_view text) override {
        auto *f = current->FindExact(wxString::FromUTF8(text.data(), text.size()));
        if (f == nullptr) return false;
        current = f;
        return true;
    }

    int GetColWidth() override {
        return current->parent != nullptr ? current->parent->grid->GetColWidth(current) : 0;
    }

    void SetColWidth(int w) override {
        if (current->parent != nullptr) {
            current->parent->grid->SetColWidth(current, std::max(w, g_min_colwidth));
        }
    }

    ibox SelectionBox() override {
        auto &selection = document->selected;
        return selection.grid ? ibox(icoord(selection.x, selection.y), icoord(selection.xs, selection.ys))
                      : ibox(icoord(0, 0), icoord(0, 0));
    }

    void GoToChild(int n) override {
        if (current->grid && n >= 0 && n < current->grid->xs * current->grid->ys) {
            current = current->grid->cells[n].get();
        }
    }

    void GoToColumnRow(int x, int y) override {
        if (current->grid && x >= 0 && x < current->grid->xs && y >= 0 &&
            y < current->grid->ys) {
            current = current->grid->C(x, y).get();
        }
    }

    void SelectCurrent() override {
        document->SetSelect(current->parent->grid->FindCell(current));
        selecteddoc = document;
    }

    void SelectRange(int x, int y, int xs, int ys) override {
        document->SetSelect(Selection(current->grid, x, y, xs, ys));
        selecteddoc = document;
    }

    static bool ContainsGrid(Cell *c, const Grid *g) {
        if (!c->grid) { return false; }
        if (c->grid.get() == g) { return true; }
        for (auto &child : c->grid->cells) {
            if (ContainsGrid(child.get(), g)) { return true; }
        }
        return false;
    }

    std::string GetText() override { return current->text.t.utf8_string(); }

    std::string GetNote() override { return current->note.utf8_string(); }

    void SetText(std::string_view t) override {
        if (current->parent != nullptr) {
            AddUndoIfNecessary();
            current->text.SetText(wxString::FromUTF8(t.data(), t.size()));
        }
    }

    void SetNote(std::string_view t) override {
        if (current->parent != nullptr) {
            AddUndoIfNecessary();
            current->note = wxString::FromUTF8(t.data(), t.size());
        }
    }

    void CreateGrid(int x, int y) override {
        if (x > 0 && y > 0 && static_cast<int64_t>(x) * y <= max_new_grid_cells) {
            AddUndoIfNecessary();
            current->AddGrid(x, y);
        }
    }

    void InsertColumn(int x) override {
        if (current->grid && x >= 0 && x <= current->grid->xs) {
            AddUndoIfNecessary();
            current->grid->InsertCells(x, -1, 1, 0);
        }
    }

    void InsertRow(int y) override {
        if (current->grid && y >= 0 && y <= current->grid->ys) {
            AddUndoIfNecessary();
            current->grid->InsertCells(-1, y, 0, 1);
        }
    }

    void Delete(int x, int y, int xs, int ys) override {
        if (current->grid && x >= 0 && x + xs <= current->grid->xs && y >= 0 &&
            y + ys <= current->grid->ys) {
            AddUndoIfNecessary();
            Selection s(current->grid, x, y, xs, ys);
            current->grid->MultiCellDeleteSub(document, s);
            document->SetSelect(Selection());
            document->Zoom(-100);
        }
    }

    void SetBackgroundColor(uint color) override {
        AddUndoIfNecessary();
        current->cellcolor = color;
    }

    uint32_t GetBackgroundColor() override { return current->cellcolor; }

    void SetTextColor(uint color) override {
        AddUndoIfNecessary();
        current->textcolor = color;
    }

    uint32_t GetTextColor() override { return current->textcolor; }

    void SetTextFiltered(bool filtered) override {
        if (current->parent != nullptr) {
            AddUndoIfNecessary();
            current->text.filtered = filtered;
        }
    }

    bool IsTextFiltered() override { return current->text.filtered; }

    void SetBorderColor(uint color) override {
        if (current->grid) {
            AddUndoIfNecessary();
            current->grid->bordercolor = color;
        }
    }

    uint32_t GetBorderColor() override {
        return current->grid ? (uint32_t)current->grid->bordercolor : 0;
    }

    int GetRelativeSize() override { return -current->text.relsize; }

    void SetRelativeSize(int relsize) override {
        AddUndoIfNecessary();
        current->text.relsize = -relsize;
    }

    void SetStyle(int stylebits) override {
        AddUndoIfNecessary();
        current->text.stylebits = stylebits;
        current->text.runs.clear();
    }

    int GetStyle() override { return current->text.stylebits; }

    void SetTextAlignment(int align) override {
        AddUndoIfNecessary();
        current->textalign = align;
    }

    int GetTextAlignment() override { return current->textalign; }

    void SetVerticalAlignment(int align) override {
        AddUndoIfNecessary();
        current->vertalign = align;
    }

    int GetVerticalAlignment() override { return current->vertalign; }

    void RemoveImage() override {
        AddUndoIfNecessary();
        current->text.image = nullptr;
    }

    void SetStatusMessage(std::string_view message) override {
        auto ws = wxString(message.data(), message.size());
        sys->frame->SetStatus(ws);
    }

    // Set by the running script via ts.agent_result(), consumed by the agent server after
    // ScriptRun() returns. Empty if the script never called it.
    std::string agent_result;

    void SetAgentResult(std::string_view result) override { agent_result = result; }

    std::string TakeAgentResult() {
        std::string result = std::move(agent_result);
        agent_result.clear();
        return result;
    }

    void SetWindowSize(int width, int height) override { sys->frame->SetSize(width, height); }

    std::string GetFileNameFromUser(bool is_save) override {
        int flags = wxFD_CHANGE_DIR;
        if (is_save) {
            flags |= wxFD_OVERWRITE_PROMPT | wxFD_SAVE;
        } else {
            flags |= wxFD_OPEN | wxFD_FILE_MUST_EXIST;
        }
        wxString fn = ::wxFileSelector(_("Choose file:"), "", "", "", "*.*", flags);
        return fn.utf8_string();
    }

    std::string GetFileName() override { return document->filename.utf8_string(); }

    int64_t GetLastEdit() override { return current->text.lastedit.GetValue().GetValue(); }

    bool IsTag() override { return current->IsTag(document); }

    bool HasImage() override { return current->text.image != nullptr; }
    bool SetImage(std::string_view fn) override {
        AddUndoIfNecessary();
        return treesheets::Document::LoadImageIntoCell(wxString::FromUTF8(fn.data(), fn.size()),
                                                       current, sys->frame->FromDIP(1.0));
    }

    void SetImageDisplayScale(int scale) override {
        AddUndoIfNecessary();
        if (scale <= 0 || scale == 100) return;
        auto *image = current->text.image;
        if (image == nullptr) return;
        image = Document::NewImage(image->display_scale / (scale / 100.0),
                                   vector<uint8_t>(image->data), image->type);
        if (image != nullptr) { current->text.image = image; }
        document->currentdrawroot->ResetChildren();
        document->currentdrawroot->ResetLayout();
        document->UpdateLayout();
        document->canvas->Refresh();
    }

    // Undo/redo may replace the current cell tree wholesale, so `current` is reset to the root
    // afterwards rather than risk it dangling into a tree the undo/redo just discarded.
    bool Undo() override {
        if (document->undolist.empty()) return false;
        document->Undo(document->undolist, document->redolist);
        current = document->root.get();
        return true;
    }

    bool Redo() override {
        if (document->redolist.empty()) return false;
        document->Undo(document->redolist, document->undolist, true);
        current = document->root.get();
        return true;
    }

    std::string GetVersion() override { return PACKAGE_VERSION; }

    void CopyCurrent() override { script_clipboard = current->Clone(nullptr); }

    bool PasteIntoCurrent() override {
        if (!script_clipboard || current->parent == nullptr) return false;
        Selection s = current->parent->grid->FindCell(current);
        if (!s.grid) return false;
        AddUndoIfNecessary();
        // Pasting a grid into a cell without text merges it into the parent grid (see
        // Grid::MergeWithParent), which deletes current, so continue at the cell in its place.
        auto *parent = current->parent;
        int x = s.x, y = s.y;
        if (lowestcommonancestor == current) { lowestcommonancestor = parent; }
        current->Paste(document, script_clipboard.get(), s);
        current = parent->grid->C(x, y).get();
        return true;
    }

    std::string GetSubtreeText(int format) override { return CellText(current, format); }

    std::string CellText(Cell *c, int format) {
        if (!c->grid) return c->text.t.utf8_string();
        int exp_format;
        switch (format) {
            case 1: exp_format = A_EXPCSV; break;
            case 2: exp_format = A_EXPXML; break;
            default: exp_format = A_EXPTEXT; break;
        }
        // ToText() always exports its whole grid regardless of the selection passed in, hence
        // the empty Selection() here.
        return c->grid->ToText(0, Selection(), exp_format, document, false, c).utf8_string();
    }

    void SetCellType(int type) override {
        AddUndoIfNecessary();
        current->celltype = type == CT_CODE ? sys->evaluator.InferCellType(current->text) : type;
    }

    std::string Evaluate(int format) override {
        AddUndoIfNecessary();
        auto result = current->Eval(sys->evaluator);
        sys->evaluator.ClearVars();
        return result ? CellText(result.get(), format) : std::string();
    }

    std::vector<double> GridNumbers() override {
        return current->grid ? current->grid->Numbers() : std::vector<double>();
    }
};

static int64_t TreeSheetsLoader(string_view_nt absfilename, std::string *dest, int64_t start,
                                int64_t len) {
    size_t l = 0;
    auto *buf = reinterpret_cast<char *>(loadfile(absfilename.c_str(), &l));
    if (buf == nullptr) { return -1; }
    dest->assign(buf, l);
    free(buf);
    return l;
}

static TreeSheetsScriptImpl tssi;

static string ScriptInit(const wxString &datapath) {
    return InitLobster(&tssi, datapath, "", false, TreeSheetsLoader);
}
