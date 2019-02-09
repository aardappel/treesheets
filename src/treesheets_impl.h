
struct TreeSheetsScriptImpl : public ScriptInterface {
    Document *doc = nullptr;
    Cell *cur = nullptr;

    enum { max_new_grid_dim = 256 };  // Don't allow crazy sizes.

    std::string ScriptRun(const char *filename) {
        doc = sys->frame->GetCurTab()->doc;
        cur = doc->rootgrid;

        doc->AddUndo(cur);

        bool dump_builtins = false;
        #ifdef _DEBUG
            //dump_builtins = true;
        #endif

        auto err = RunLobster(filename, nullptr, dump_builtins);

        doc = nullptr;
        cur = nullptr;

        return err;
    }

    void GoToRoot() { cur = doc->rootgrid; }
    void GoToView() { cur = doc->curdrawroot; }
    bool HasSelection() { return doc->selected.g; }
    void GoToSelection() { if (doc->selected.g) cur = doc->selected.g->cell; }
    bool HasParent() { return cur->parent; }
    void GoToParent() { if (cur->parent) cur = cur->parent; }
    int NumChildren() { return cur->grid ? cur->grid->xs * cur->grid->ys : 0; }

    icoord NumColumnsRows() {
        return cur->grid ? icoord(cur->grid->xs, cur->grid->ys)
                         : icoord(0, 0);
    }

    ibox SelectionBox() {
        auto &s = doc->selected;
        return s.g ? ibox(icoord(s.x, s.y), icoord(s.xs, s.ys))
                    : ibox(icoord(0, 0), icoord(0, 0));
    }

    void GoToChild(int n) {
        if (cur->grid && n < cur->grid->xs * cur->grid->ys)
            cur = cur->grid->cells[n];
    }

    void GoToColumnRow(int x, int y) {
        if (cur->grid && x < cur->grid->xs && y < cur->grid->ys)
            cur = cur->grid->C(x, y);
    }

    std::string GetText() {
        auto s = cur->text.t.utf8_str();
        return std::string(s.data(), s.length());
    }

    void SetText(std::string_view t) {
        if (cur->parent) cur->text.t = wxString(t.data(), t.size());
    }

    void CreateGrid(int x, int y) {
        if (x > 0 && y > 0 && x < max_new_grid_dim && y < max_new_grid_dim)
            cur->AddGrid(x, y);
    }

    void InsertColumns(int x, int n) {
        if (cur->grid && x >= 0 && x <= cur->grid->xs && n > 0 && n < max_new_grid_dim)
            cur->grid->InsertCells(x, -1, 1 /* FIXME n */, 0);
    }

    void InsertRows(int y, int n) {
        if (cur->grid && y >= 0 && y <= cur->grid->ys && n > 0 && n < max_new_grid_dim)
            cur->grid->InsertCells(-1, y, 0, 1 /* FIXME n */);
    }

    void Delete(int x, int y, int xs, int ys) {
        if (cur->grid && x >= 0 && x + xs <= cur->grid->xs && y >= 0 && y + ys <= cur->grid->ys) {
            Selection s(cur->grid, x, y, xs, ys);
            cur->grid->MultiCellDeleteSub(doc, s);
        }
    }

    void SetBackgroundColor(uint col) { cur->cellcolor = col; }
    void SetTextColor(uint col) { cur->textcolor = col; }
    void SetRelativeSize(int s) { cur->text.relsize = s; }
    void SetStyle(int s) { cur->text.stylebits = s; }

    void SetStatusMessage(std::string_view msg) {
        auto ws = wxString(msg.data(), msg.size());
        sys->frame->GetCurTab()->Status(ws);
    }

    std::string GetFileNameFromUser(bool is_save) {
        int flags = wxFD_CHANGE_DIR;
        if (is_save) flags |= wxFD_OVERWRITE_PROMPT | wxFD_SAVE;
        else flags |= wxFD_OPEN | wxFD_FILE_MUST_EXIST;
        wxString fn = ::wxFileSelector(_(L"Choose file:"), L"", L"", L"", L"*.*", flags);
        auto s = fn.utf8_str();
        return std::string(s.data(), s.length());
    }
};

static int64_t TreeSheetsLoader(std::string_view absfilename, std::string *dest, int64_t start,
                                int64_t len) {
    size_t l = 0;
    auto buf = (char *)loadfile(std::string(absfilename).c_str(), &l);
    if (!buf) return -1;
    dest->assign(buf, l);
    free(buf);
    return l;
}

static TreeSheetsScriptImpl tssi;

static void ScriptInit(MyFrame *frame) {
    auto serr = InitLobster(&tssi, frame->GetPath("scripts/"), "", false, TreeSheetsLoader);
    if (!serr.empty()) 
        frame->GetCurTab()->Status(wxString("Script system could not initialize: " + serr));
}
