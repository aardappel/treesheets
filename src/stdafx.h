
#ifdef _WIN32
    #define _CRT_SECURE_NO_WARNINGS
    #define _SCL_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <wx/aboutdlg.h>
#include <wx/clipbrd.h>
#include <wx/confbase.h>
#include <wx/config.h>
#include <wx/datstrm.h>
#include <wx/dcbuffer.h>
#include <wx/dir.h>
#include <wx/dnd.h>
#include <wx/fileconf.h>
#include <wx/numdlg.h>
#include <wx/tokenzr.h>
#include <wx/txtstrm.h>
#include <wx/wfstream.h>
#include <wx/wx.h>
#include <wx/zstream.h>

#ifdef _WIN32
    #include <WinUser.h>
    #include <wx/msw/dc.h>
    #include <wx/msw/regconf.h>
#endif

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/aui/auibook.h>
#include <wx/base64.h>
#include <wx/bmpbndl.h>
#include <wx/colordlg.h>
#include <wx/datectrl.h>
#include <wx/display.h>
#include <wx/docview.h>
#include <wx/filename.h>
#include <wx/fontdlg.h>
#include <wx/fswatcher.h>
#include <wx/graphics.h>
#include <wx/ipc.h>
#include <wx/mstream.h>
#include <wx/notebook.h>
#include <wx/odcombo.h>
#include <wx/print.h>
#include <wx/printdlg.h>
#include <wx/sizer.h>
#include <wx/snglinst.h>
#include <wx/srchctrl.h>
#include <wx/stdpaths.h>
#include <wx/sysopt.h>
#include <wx/taskbar.h>
#include <wx/timectrl.h>
#include <wx/xml/xml.h>

#include <algorithm>
#include <array>
#include <clocale>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <future>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "threadpool.h"
#include "tools.h"

#ifdef _WIN32
    #include "..\treesheets\resource.h"
    #include "StackWalker\StackWalkerHelpers.h"
#endif

using namespace std;
