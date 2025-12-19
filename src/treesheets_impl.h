struct TreeSheetsScriptImpl : public ScriptInterface {
    Document *document = nullptr;
    Cell *current = nullptr;
    Cell *lowestcommonancestor = nullptr;

    enum { max_new_grid_cells = 256 * 256 };  // Don't allow crazy sizes.

    void SwitchToCurrentDocument() {
        document = sys->frame->GetCurrentTab()->doc;
        current = document->root;
        lowestcommonancestor = nullptr;
    }

    void AddUndoIfNecessary() {
        if (!lowestcommonancestor) {
            UpdateLowestCommonAncestor(true);
        } else {
            for (auto p = current; p; p = p->parent) {
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
        document->canvas->Refresh();

        document = nullptr;
        current = nullptr;

        return errormessage;
    }

    bool LoadDocument(const char *filename) {
        auto message = sys->LoadDB(filename);
        if (*message) return false;

        SwitchToCurrentDocument();
        return true;
    }

    void GoToRoot() { current = document->root; }
    void GoToView() { current = document->currentdrawroot; }
    bool HasSelection() { return document->selected.grid; }
    void GoToSelection() {
        auto cell = document->selected.GetFirst();
        if (cell) current = cell;
    }
    bool HasParent() { return current->parent; }
    void GoToParent() {
        if (current->parent) current = current->parent;
    }
    int NumChildren() { return current->grid ? current->grid->xs * current->grid->ys : 0; }

    icoord NumColumnsRows() {
        return current->grid ? icoord(current->grid->xs, current->grid->ys) : icoord(0, 0);
    }

    int GetColWidth() { return current->parent ? current->parent->grid->GetColWidth(current) : 0; }

    void SetColWidth(int w) {
        if (current->parent) { current->parent->grid->SetColWidth(current, w); }
    }

    ibox SelectionBox() {
        auto &selection = document->selected;
        return selection.grid ? ibox(icoord(selection.x, selection.y), icoord(selection.xs, selection.ys))
                      : ibox(icoord(0, 0), icoord(0, 0));
    }

    void GoToChild(int n) {
        if (current->grid && n < current->grid->xs * current->grid->ys)
            current = current->grid->cells[n];
    }

    void GoToColumnRow(int x, int y) {
        if (current->grid && x < current->grid->xs && y < current->grid->ys)
            current = current->grid->C(x, y);
    }

    std::string GetText() { return current->text.t.utf8_string(); }

    void SetText(std::string_view t) {
        if (current->parent) {
            AddUndoIfNecessary();
            current->text.t = wxString::FromUTF8(t.data(), t.size());
        }
    }

    void CreateGrid(int x, int y) {
        if (x > 0 && y > 0 && x * y < max_new_grid_cells) {
            AddUndoIfNecessary();
            current->AddGrid(x, y);
        }
    }

    void InsertColumn(int x) {
        if (current->grid && x >= 0 && x <= current->grid->xs) {
            AddUndoIfNecessary();
            current->grid->InsertCells(x, -1, 1, 0);
        }
    }

    void InsertRow(int y) {
        if (current->grid && y >= 0 && y <= current->grid->ys) {
            AddUndoIfNecessary();
            current->grid->InsertCells(-1, y, 0, 1);
        }
    }

    void Delete(int x, int y, int xs, int ys) {
        if (current->grid && x >= 0 && x + xs <= current->grid->xs && y >= 0 &&
            y + ys <= current->grid->ys) {
            AddUndoIfNecessary();
            Selection s(current->grid, x, y, xs, ys);
            current->grid->MultiCellDeleteSub(document, s);
            document->SetSelect(Selection());
            document->Zoom(-100);
        }
    }

    void SetBackgroundColor(uint color) {
        AddUndoIfNecessary();
        current->cellcolor = color;
    }

    void SetTextColor(uint color) {
        AddUndoIfNecessary();
        current->textcolor = color;
    }

    void SetTextFiltered(bool filtered) {
        if (current->parent) {
            AddUndoIfNecessary();
            current->text.filtered = filtered;
        }
    }

    bool IsTextFiltered() { return current->text.filtered; }

    void SetBorderColor(uint color) {
        if (current->grid) {
            AddUndoIfNecessary();
            current->grid->bordercolor = color;
        }
    }

    void SetRelativeSize(int relsize) {
        AddUndoIfNecessary();
        current->text.relsize = relsize;
    }

    void SetStyle(int stylebits) {
        AddUndoIfNecessary();
        current->text.stylebits = stylebits;
    }

    int GetStyle() { return current->text.stylebits; }

    void SetStatusMessage(std::string_view message) {
        auto ws = wxString(message.data(), message.size());
        sys->frame->SetStatus(ws);
    }

    void SetWindowSize(int width, int height) { sys->frame->SetSize(width, height); }

    std::string GetFileNameFromUser(bool is_save) {
        int flags = wxFD_CHANGE_DIR;
        if (is_save)
            flags |= wxFD_OVERWRITE_PROMPT | wxFD_SAVE;
        else
            flags |= wxFD_OPEN | wxFD_FILE_MUST_EXIST;
        wxString fn = ::wxFileSelector(_(L"Choose file:"), L"", L"", L"", L"*.*", flags);
        return fn.utf8_string();
    }

    std::string GetFileName() { return document->filename.utf8_string(); }

    int64_t GetLastEdit() { return current->text.lastedit.GetValue().GetValue(); }

    bool IsTag() { return current->IsTag(document); }

    int GetRelSize() { return -current->text.relsize; }
};

static int64_t TreeSheetsLoader(string_view_nt absfilename, std::string *dest, int64_t start,
                                int64_t len) {
    size_t l = 0;
    auto buf = (char *)loadfile(absfilename.c_str(), &l);
    if (!buf) return -1;
    dest->assign(buf, l);
    free(buf);
    return l;
}

static TreeSheetsScriptImpl tssi;

static string ScriptInit(const wxString &datapath) {
    return InitLobster(&tssi, datapath, "", false, TreeSheetsLoader);
}
