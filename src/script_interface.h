namespace script {

using icoord = std::pair<int, int>;
using ibox = std::pair<icoord, icoord>;

// The largest grid (in cells) a script may create in one go. Don't allow crazy sizes.
inline constexpr int64_t max_new_grid_cells = 256 * 256;

struct ScriptInterface {
    virtual bool LoadDocument(const char *filename) = 0;
    virtual void NewDocument(int cols, int rows) = 0;
    virtual bool SaveDocument(bool saveas) = 0;
    virtual bool SaveDocumentAs(const char *filename) = 0;
    virtual void GoToRoot() = 0;
    virtual void GoToView() = 0;
    virtual bool HasSelection() = 0;
    virtual void GoToSelection() = 0;
    virtual bool HasParent() = 0;
    virtual void GoToParent() = 0;
    virtual int NumChildren() = 0;
    virtual icoord NumColumnsRows() = 0;
    virtual ibox SelectionBox() = 0;
    virtual void GoToChild(int n) = 0;
    virtual void GoToColumnRow(int x, int y) = 0;
    virtual std::string GetText() = 0;
    virtual std::string GetNote() = 0;
    virtual void SetText(std::string_view t) = 0;
    virtual void SetNote(std::string_view t) = 0;
    virtual void CreateGrid(int x, int n) = 0;
    virtual void InsertColumn(int x) = 0;
    virtual void InsertRow(int y) = 0;
    virtual void Delete(int x, int y, int xs, int ys) = 0;
    virtual void SetBackgroundColor(uint32_t col) = 0;
    virtual void SetTextColor(uint32_t col) = 0;
    virtual void SetTextFiltered(bool filtered) = 0;
    virtual bool IsTextFiltered() = 0;
    virtual void SetBorderColor(uint32_t col) = 0;
    virtual int GetRelativeSize() = 0;
    virtual void SetRelativeSize(int s) = 0;
    virtual void SetStyle(int s) = 0;
    virtual int GetStyle() = 0;
    virtual int GetColWidth() = 0;
    virtual void SetColWidth(int w) = 0;
    virtual void SetStatusMessage(std::string_view message) = 0;
    virtual void SetAgentResult(std::string_view result) = 0;
    virtual void SetWindowSize(int width, int height) = 0;
    virtual std::string GetFileNameFromUser(bool is_save) = 0;
    virtual std::string GetFileName() = 0;
    virtual int64_t GetLastEdit() = 0;
    virtual bool IsTag() = 0;
    virtual bool HasImage() = 0;
    virtual void SetImageDisplayScale(int scale) = 0;
    virtual void RemoveImage() = 0;
    virtual bool SetImage(std::string_view filename) = 0;
    virtual bool Undo() = 0;
    virtual bool Redo() = 0;
    virtual bool IsGrid() = 0;
    virtual int GetCellType() = 0;
    virtual bool IsFolded() = 0;
    virtual void SetFolded(bool folded) = 0;
    virtual uint32_t GetBackgroundColor() = 0;
    virtual uint32_t GetTextColor() = 0;
    virtual uint32_t GetBorderColor() = 0;
    virtual std::string GetVersion() = 0;
    // Searches the subtree of the current cell (including itself) for a cell whose text
    // exactly equals `text`, and makes it current if found.
    virtual bool FindExact(std::string_view text) = 0;
    // Deep-clones the current cell (and its subtree) into an in-process scripting clipboard.
    virtual void CopyCurrent() = 0;
    // Pastes the scripting clipboard (see CopyCurrent()) into the current cell. False if there
    // is nothing copied yet, or the current cell has no parent (is the root).
    virtual bool PasteIntoCurrent() = 0;
    // Exports the subtree of the current cell to text in one call. format: 0 = plain indented
    // text, 1 = csv, 2 = xml.
    virtual std::string GetSubtreeText(int format) = 0;
    virtual ~ScriptInterface() = default;
};

extern std::string InitLobster(ScriptInterface *_si, const char *exefilepath,
                               const char *auxfilepath, bool from_bundle, FileLoader sl);
extern std::string RunLobster(std::string_view filename, std::string_view code, bool dump_builtins);
extern void TSDumpBuiltinDoc();

}  // namespace script
