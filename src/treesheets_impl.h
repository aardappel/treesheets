struct TreeSheetsScriptImpl : public ScriptInterface {
    Document *document = nullptr;
    Cell *current = nullptr;
    Cell *lowestcommonancestor = nullptr;

    enum { max_new_grid_cells = 256 * 256 };  // Don't allow crazy sizes.

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
        // Use parent as lowestcommonancestor so changes to siblings are already covered
        lowestcommonancestor = current->parent;
        document->AddUndo(lowestcommonancestor, newgeneration);
    }

    std::string ScriptRun(const char *filename) {
        SwitchToCurrentDocument();

        bool dump_builtins = false;
        #ifdef _DEBUG
            //dump_builtins = true;
        #endif

        auto errormessage = RunLobster(filename, {}, dump_builtins);

        document->root->ResetChildren();
        document->UpdateLayout();
        document->canvas->Refresh();

        document = nullptr;
        current = nullptr;

        return errormessage;
    }

    bool LoadDocument(const char *filename) override {
        auto message = sys->LoadDB(filename);
        if (message.IsEmpty()) { return false; }

        SwitchToCurrentDocument();
        return true;
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

    int GetColWidth() override {
        return current->parent != nullptr ? current->parent->grid->GetColWidth(current) : 0;
    }

    void SetColWidth(int w) override {
        if (current->parent != nullptr) { current->parent->grid->SetColWidth(current, w); }
    }

    ibox SelectionBox() override {
        auto &selection = document->selected;
        return selection.grid ? ibox(icoord(selection.x, selection.y), icoord(selection.xs, selection.ys))
                      : ibox(icoord(0, 0), icoord(0, 0));
    }

    void GoToChild(int n) override {
        if (current->grid && n < current->grid->xs * current->grid->ys) {
            current = current->grid->cells[n].get();
        }
    }

    void GoToColumnRow(int x, int y) override {
        if (current->grid && x < current->grid->xs && y < current->grid->ys) {
            current = current->grid->C(x, y).get();
        }
    }

    std::string GetText() override { return current->text.t.utf8_string(); }

    std::string GetNote() override { return current->note.utf8_string(); }

    void SetText(std::string_view t) override {
        if (current->parent != nullptr) {
            AddUndoIfNecessary();
            current->text.t = wxString::FromUTF8(t.data(), t.size());
        }
    }

    void SetNote(std::string_view t) override {
        if (current->parent != nullptr) {
            AddUndoIfNecessary();
            current->note = wxString::FromUTF8(t.data(), t.size());
        }
    }

    void CreateGrid(int x, int y) override {
        if (x > 0 && y > 0 && x * y < max_new_grid_cells) {
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

    void SetTextColor(uint color) override {
        AddUndoIfNecessary();
        current->textcolor = color;
    }

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

    int GetRelativeSize() override { return -current->text.relsize; }

    void SetRelativeSize(int relsize) override {
        AddUndoIfNecessary();
        current->text.relsize = -relsize;
    }

    void SetStyle(int stylebits) override {
        AddUndoIfNecessary();
        current->text.stylebits = stylebits;
    }

    int GetStyle() override { return current->text.stylebits; }

    void RemoveImage() override {
        AddUndoIfNecessary();
        current->text.image = nullptr;
    }

    void SetStatusMessage(std::string_view message) override {
        auto ws = wxString(message.data(), message.size());
        sys->frame->SetStatus(ws);
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
