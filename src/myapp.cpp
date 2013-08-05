#include "myapp.h"
#include "system.h"

namespace treesheets {
wxConnectionBase *IPCServer::OnAcceptConnection(const wxString& topic)
{
    sys->frame->DeIconize();
    if(topic.Len() && topic!=L"*") sys->Open(topic);
    return new wxConnection();
}

MyApp::MyApp() : checker(NULL), frame(NULL), serv(NULL) {}

bool MyApp::OnInit()
{
    #if wxUSE_UNICODE==0
        #error "must use unicode version of wx libs to ensure data integrity of .cts files"
    #endif
    ASSERT(wxUSE_UNICODE);
    
    #ifdef __WXMAC__
        wxDisableAsserts();
    #endif
    
    const wxString name = wxString::Format(L".treesheets-single-instance-check-%s", wxGetUserId().c_str());
    checker = new wxSingleInstanceChecker(name);
    if(checker->IsAnotherRunning())
    {
        wxClient client;
        client.MakeConnection(L"localhost", L"4242", argc==2 ? argv[1] : L"*");  // fire and forget            
        return false;
    }
    
    sys = new System();
    frame = new MyFrame(argv[0], this);
    SetTopWindow(frame);
    sys->Init(argc, argv);
    
    serv = new IPCServer();
    serv->Create(L"4242");
    
    return true;
}

int MyApp::OnExit()
{
    DELETEP(serv);
    DELETEP(sys);
    DELETEP(checker);
    return 0;
}

#ifdef __WXMAC__
void MyApp::MacOpenFile(const wxString &fn)
{
    if(sys) sys->Open(fn);
}
#endif
}
BEGIN_EVENT_TABLE(treesheets::MyApp, wxApp)
END_EVENT_TABLE()
