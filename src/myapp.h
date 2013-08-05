#pragma once
#include "stdafx.h"
#include "myframe.h"
namespace treesheets {

struct IPCServer : wxServer
{
    wxConnectionBase *OnAcceptConnection(const wxString& topic);
};

class MyApp : public wxApp
{
  public:
    MyApp();
    bool OnInit();
    int OnExit();
#ifdef __WXMAC__
    void MacOpenFile(const wxString &fn);
#endif
  private:
    MyFrame *frame;
    wxSingleInstanceChecker *checker;
    IPCServer *serv;

    DECLARE_EVENT_TABLE()
};
}
