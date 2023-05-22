
namespace script {

typedef std::pair<int, int> icoord;
typedef std::pair<icoord, icoord> ibox;

struct ScriptInterface {
    virtual bool LoadDocument(const char *filename) = 0;
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
    virtual void SetText(std::string_view t) = 0;
    virtual void CreateGrid(int x, int n) = 0;
    virtual void InsertColumn(int x) = 0;
    virtual void InsertRow(int y) = 0;
    virtual void Delete(int x, int y, int xs, int ys) = 0;
    virtual void SetBackgroundColor(uint32_t col) = 0;
    virtual void SetTextColor(uint32_t col) = 0;
    virtual void SetBorderColor(uint32_t col) = 0;
    virtual void SetRelativeSize(int s) = 0;
    virtual void SetStyle(int s) = 0;
    virtual void SetStatusMessage(std::string_view msg) = 0;
    virtual void SetWindowSize(int width, int height) = 0;
    virtual std::string GetFileNameFromUser(bool is_save) = 0;
    virtual std::string GetFileName() = 0;
    virtual ~ScriptInterface() {};
};

typedef int64_t(*ScriptLoader)(std::string_view absfilename, std::string *dest, int64_t start,
                               int64_t len);

extern std::string InitLobster(ScriptInterface *_si, const char *exefilepath, const char *auxfilepath,
                               bool from_bundle, ScriptLoader sl);
extern std::string RunLobster(std::string_view filename, std::string_view code, bool dump_builtins);
extern void TSDumpBuiltinDoc();

}
