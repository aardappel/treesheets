
#include <wx/wx.h>
#include <wx/dir.h>

#include <ctype.h>

#include <wx/zstream.h>
#include <wx/wfstream.h>
#include <wx/datstrm.h>
#include <wx/txtstrm.h>
#include <wx/dcbuffer.h>
#include <wx/clipbrd.h>
#include <wx/dnd.h>
#include <wx/tokenzr.h>

#include <wx/numdlg.h>
#include <wx/aboutdlg.h>

#include <wx/config.h>
#include <wx/confbase.h>
#include <wx/fileconf.h>

#ifdef _WIN32
    #include <wx/msw/regconf.h>
    #include <wx/msw/dc.h>
    #include <WinUser.h>
#endif

#include <wx/fontdlg.h>
#include <wx/colordlg.h>

#include <wx/filename.h>

#include <wx/xml/xml.h>

#include <wx/docview.h>

#include <wx/print.h>
#include <wx/printdlg.h>

#include <wx/odcombo.h>

#include <wx/sysopt.h>

#include <wx/taskbar.h>

#include <wx/notebook.h>

#include <wx/snglinst.h>

#include <wx/ipc.h>

#include <wx/srchctrl.h>
#include <wx/datectrl.h>
#include <wx/sizer.h>

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/aui/auibook.h>

#include <wx/display.h>

#include <clocale>

#include <wx/fswatcher.h>

#include <wx/stdpaths.h>
#include <wx/mstream.h>

WX_DECLARE_STRING_HASH_MAP(bool, wxHashMapBool);

#include <new>
#include <vector>
#include <array>
#include <string>
#include <string_view>
#include <map>
#include <algorithm>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <filesystem>
#include <utility>
#include <locale>
#include <sstream>

#include "threadpool.h"
#include "tools.h"


#ifdef _WIN32
    #include "..\treesheets\resource.h"
    #include "StackWalker\StackWalkerHelpers.h"
#endif

using namespace std;
