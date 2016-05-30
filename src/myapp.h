
struct IPCServer : wxServer
{
    wxConnectionBase *OnAcceptConnection(const wxString &topic)
    {
        sys->frame->DeIconize();
        if (topic.Len() && topic != L"*") sys->Open(topic);
        return new wxConnection();
    }
};

struct MyApp : wxApp
{
    MyFrame *frame;
    wxSingleInstanceChecker *checker;
    IPCServer *serv;
    //wxLocale locale;

    MyApp() : checker(NULL), frame(NULL), serv(NULL) {}
    bool OnInit()
    {
        #if wxUSE_UNICODE == 0
        #error "must use unicode version of wx libs to ensure data integrity of .cts files"
        #endif
        ASSERT(wxUSE_UNICODE);

        //#ifdef __WXMAC__
        // Now needed on WIN32 as well, because of all the locale related asserts. sigh.
        wxDisableAsserts();
        //#endif

        //locale.Init();
        //// wxWidgets forces the use of LC_ALL, and doesn't allow use of setlocale without a wxLocale
        //// so to get what we want, we reset back to C locale first.
        //std::setlocale(LC_ALL, "C");
        std::setlocale(LC_CTYPE, "");  // correct handling of non-latin symbols

        bool portable = false;
        wxString filename;
        for (int i = 1; i < argc; i++)
        {
            if (argv[i][0] == '-')
            {
                switch ((int)argv[i][1])
                {
                    case 'p': portable = true; break;
                }
            }
            else
            {
                filename = argv[i];
            }
        }

        const wxString name = wxString::Format(L".treesheets-single-instance-check-%s", wxGetUserId().c_str());
        checker = new wxSingleInstanceChecker(name);
        if (checker->IsAnotherRunning())
        {
            wxClient client;
            client.MakeConnection(L"localhost", L"4242", filename.Len() ? filename.wc_str() : L"*");  // fire and forget
            return false;
        }

        sys = new System(portable);
        frame = new MyFrame(argv[0], this);
        SetTopWindow(frame);
        sys->Init(filename);

        serv = new IPCServer();
        serv->Create(L"4242");

        return true;
    }

    int OnExit()
    {
        DELETEP(serv);
        DELETEP(sys);
        DELETEP(checker);
        return 0;
    }

    #ifdef __WXMAC__
    void MacOpenFile(const wxString &fn)
    {
        if (sys) sys->Open(fn);
    }
    #endif

    DECLARE_EVENT_TABLE()
};
