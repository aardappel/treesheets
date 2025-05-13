
struct TreeSheetsScriptImpl : public ScriptInterface {
    Document *doc = nullptr;
    Cell *cur = nullptr;
    wxDC *dc = nullptr;
    bool docmodified = false;

    enum { max_new_grid_cells = 256 * 256 };  // Don't allow crazy sizes.

    void SwitchToCurrentDoc() {
        doc = sys->frame->GetCurTab()->doc;
        cur = doc->rootgrid;
        docmodified = false;
    }

    void MarkDocAsModified() {
        if (docmodified) return;
        doc->AddUndo(cur);
        docmodified = true;
    }

    std::string ScriptRun(const char *filename, wxDC &_dc) {
        SwitchToCurrentDoc();

        bool dump_builtins = false;
        #ifdef _DEBUG
            //dump_builtins = true;
        #endif

        dc = &_dc;
        auto err = RunLobster(filename, {}, dump_builtins);
        dc = nullptr;

        doc->rootgrid->ResetChildren();
        doc->Refresh();

        doc = nullptr;
        cur = nullptr;

        return err;
    }

    bool LoadDocument(const char *filename) {
        auto msg = sys->LoadDB(filename);
        if (msg.Len()) return false;

        SwitchToCurrentDoc();
        return true;
    }

    void GoToRoot() { cur = doc->rootgrid; }
    void GoToView() { cur = doc->curdrawroot; }
    bool HasSelection() { return doc->selected.g; }
    void GoToSelection() {
        auto c = doc->selected.GetFirst();
        if (c) cur = c;
    }
    bool HasParent() { return cur->parent; }
    void GoToParent() {
        if (cur->parent) cur = cur->parent;
    }
    int NumChildren() { return cur->grid ? cur->grid->xs * cur->grid->ys : 0; }

    icoord NumColumnsRows() {
        return cur->grid ? icoord(cur->grid->xs, cur->grid->ys) : icoord(0, 0);
    }

    ibox SelectionBox() {
        auto &s = doc->selected;
        return s.g ? ibox(icoord(s.x, s.y), icoord(s.xs, s.ys)) : ibox(icoord(0, 0), icoord(0, 0));
    }

    void GoToChild(int n) {
        if (cur->grid && n < cur->grid->xs * cur->grid->ys) cur = cur->grid->cells[n];
    }

    void GoToColumnRow(int x, int y) {
        if (cur->grid && x < cur->grid->xs && y < cur->grid->ys) cur = cur->grid->C(x, y);
    }

    std::string GetText() {
        auto s = cur->text.t.utf8_str();
        return std::string(s.data(), s.length());
    }

    void SetText(std::string_view t) {
        MarkDocAsModified();
        if (cur->parent) cur->text.t = wxString::FromUTF8(t.data(), t.size());
    }

    void CreateGrid(int x, int y) {
        MarkDocAsModified();
        if (x > 0 && y > 0 && x * y < max_new_grid_cells) cur->AddGrid(x, y);
    }

    void InsertColumn(int x) {
        MarkDocAsModified();
        if (cur->grid && x >= 0 && x <= cur->grid->xs) cur->grid->InsertCells(x, -1, 1, 0);
    }

    void InsertRow(int y) {
        MarkDocAsModified();
        if (cur->grid && y >= 0 && y <= cur->grid->ys) cur->grid->InsertCells(-1, y, 0, 1);
    }

    void Delete(int x, int y, int xs, int ys) {
        MarkDocAsModified();
        if (cur->grid && x >= 0 && x + xs <= cur->grid->xs && y >= 0 && y + ys <= cur->grid->ys) {
            Selection s(cur->grid, x, y, xs, ys);
            cur->grid->MultiCellDeleteSub(doc, s);
            doc->SetSelect(Selection());
            doc->Zoom(-100, *dc);
        }
    }

    void SetBackgroundColor(uint col) {
        MarkDocAsModified();
        cur->cellcolor = col;
    }
    void SetTextColor(uint col) {
        MarkDocAsModified();
        cur->textcolor = col;
    }
    void SetTextFiltered(bool filtered) {
        MarkDocAsModified();
        if (cur->parent) cur->text.filtered = filtered;
    }
    bool IsTextFiltered() { return cur->text.filtered; }
    void SetBorderColor(uint col) {
        MarkDocAsModified();
        if (cur->grid) cur->grid->bordercolor = col;
    }
    void SetRelativeSize(int s) {
        MarkDocAsModified();
        cur->text.relsize = s;
    }
    void SetStyle(int s) {
        MarkDocAsModified();
        cur->text.stylebits = s;
    }

    void SetStatusMessage(std::string_view msg) {
        auto ws = wxString(msg.data(), msg.size());
        sys->frame->GetCurTab()->Status(ws);
    }

    void SetWindowSize(int width, int height) { sys->frame->SetSize(width, height); }

    std::string GetFileNameFromUser(bool is_save) {
        int flags = wxFD_CHANGE_DIR;
        if (is_save)
            flags |= wxFD_OVERWRITE_PROMPT | wxFD_SAVE;
        else
            flags |= wxFD_OPEN | wxFD_FILE_MUST_EXIST;
        wxString fn = ::wxFileSelector(_(L"Choose file:"), L"", L"", L"", L"*.*", flags);
        auto s = fn.utf8_str();
        return std::string(s.data(), s.length());
    }

    std::string GetFileName() {
        auto s = doc->filename.utf8_str();
        return std::string(s.data(), s.length());
    }

    int64_t LastEdit() { return cur->text.lastedit.GetValue().GetValue(); }
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
