struct TreeSheetsScriptImpl : public ScriptInterface {
    Document *doc = nullptr;
    Cell *current = nullptr;
    bool docmodified = false;

    enum { max_new_grid_cells = 256 * 256 };  // Don't allow crazy sizes.

    void SwitchToCurrentDoc() {
        doc = sys->frame->GetCurrentTab()->doc;
        current = doc->root;
        docmodified = false;
    }

    void MarkDocAsModified() {
        if (docmodified) return;
        // The script can operate on multiple cells throughout the document
        doc->AddUndo(doc->root);
        docmodified = true;
    }

    std::string ScriptRun(const char *filename) {
        SwitchToCurrentDoc();

        bool dump_builtins = false;
        #ifdef _DEBUG
            //dump_builtins = true;
        #endif

        auto err = RunLobster(filename, {}, dump_builtins);

        doc->root->ResetChildren();
        doc->canvas->Refresh();

        doc = nullptr;
        current = nullptr;

        return err;
    }

    bool LoadDocument(const char *filename) {
        auto message = sys->LoadDB(filename);
        if (*message) return false;

        SwitchToCurrentDoc();
        return true;
    }

    void GoToRoot() { current = doc->root; }
    void GoToView() { current = doc->curdrawroot; }
    bool HasSelection() { return doc->selected.grid; }
    void GoToSelection() {
        auto c = doc->selected.GetFirst();
        if (c) current = c;
    }
    bool HasParent() { return current->parent; }
    void GoToParent() {
        if (current->parent) current = current->parent;
    }
    int NumChildren() { return current->grid ? current->grid->xs * current->grid->ys : 0; }

    icoord NumColumnsRows() {
        return current->grid ? icoord(current->grid->xs, current->grid->ys) : icoord(0, 0);
    }

    ibox SelectionBox() {
        auto &s = doc->selected;
        return s.grid ? ibox(icoord(s.x, s.y), icoord(s.xs, s.ys))
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
            MarkDocAsModified();
            current->text.t = wxString::FromUTF8(t.data(), t.size());
        }
    }

    void CreateGrid(int x, int y) {
        if (x > 0 && y > 0 && x * y < max_new_grid_cells) {
            MarkDocAsModified();
            current->AddGrid(x, y);
        }
    }

    void InsertColumn(int x) {
        if (current->grid && x >= 0 && x <= current->grid->xs) {
            MarkDocAsModified();
            current->grid->InsertCells(x, -1, 1, 0);
        }
    }

    void InsertRow(int y) {
        if (current->grid && y >= 0 && y <= current->grid->ys) {
            MarkDocAsModified();
            current->grid->InsertCells(-1, y, 0, 1);
        }
    }

    void Delete(int x, int y, int xs, int ys) {
        if (current->grid && x >= 0 && x + xs <= current->grid->xs && y >= 0 &&
            y + ys <= current->grid->ys) {
            MarkDocAsModified();
            Selection s(current->grid, x, y, xs, ys);
            current->grid->MultiCellDeleteSub(doc, s);
            doc->SetSelect(Selection());
            doc->Zoom(-100);
        }
    }

    void SetBackgroundColor(uint col) {
        MarkDocAsModified();
        current->cellcolor = col;
    }
    void SetTextColor(uint col) {
        MarkDocAsModified();
        current->textcolor = col;
    }
    void SetTextFiltered(bool filtered) {
        if (current->parent) {
            MarkDocAsModified();
            current->text.filtered = filtered;
        }
    }
    bool IsTextFiltered() { return current->text.filtered; }
    void SetBorderColor(uint col) {
        if (current->grid) {
            MarkDocAsModified();
            current->grid->bordercolor = col;
        }
    }
    void SetRelativeSize(int s) {
        MarkDocAsModified();
        current->text.relsize = s;
    }
    void SetStyle(int s) {
        MarkDocAsModified();
        current->text.stylebits = s;
    }

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

    std::string GetFileName() { return doc->filename.utf8_string(); }

    int64_t GetLastEdit() { return current->text.lastedit.GetValue().GetValue(); }
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
