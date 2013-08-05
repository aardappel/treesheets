#pragma once
#include <wx/wx.h>
#include <wx/dir.h>

//#include <stdio.h>
//#include <stdlib.h>
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
#ifdef WIN32
#include <wx/msw/regconf.h>
#include <wx/msw/dc.h>
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

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/aui/auibook.h>

#include <wx/dc.h>
//#include <wx/uiaction.h>

//#include <wx/overlay.h>
//#include <wx/richtext/richtextctrl.h>

#ifdef WIN32
    #define FSWATCH
#endif
#ifdef FSWATCH
    #include <wx/fswatcher.h>
#endif

WX_DECLARE_STRING_HASH_MAP(bool, wxHashMapBool);

#include <new>

#include "tools.h"


#ifdef WIN32
    #include "..\treesheets\resource.h"
#endif
