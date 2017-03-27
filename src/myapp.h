
struct IPCServer : wxServer {
    wxConnectionBase *OnAcceptConnection(const wxString &topic) {
        sys->frame->DeIconize();
        if (topic.Len() && topic != L"*") sys->Open(topic);
        return new wxConnection();
    }
};

struct MyApp : wxApp {
    MyFrame *frame;
    wxSingleInstanceChecker *checker;
    IPCServer *serv;
    wxString filename;
    bool initateventloop;
    wxLocale locale;

    MyApp() : checker(nullptr), frame(nullptr), serv(nullptr), initateventloop(false) {}

    void AddTranslation(const wxString &basepath) {
        #ifdef __WXGTK__
            locale.AddCatalogLookupPathPrefix(L"/usr");
            locale.AddCatalogLookupPathPrefix(L"/usr/local");
            wxString prefix = wxStandardPaths::Get().GetInstallPrefix();
            locale.AddCatalogLookupPathPrefix(prefix);
        #endif
        locale.AddCatalogLookupPathPrefix(basepath);
        locale.AddCatalog(L"ts", (wxLanguage)locale.GetLanguage());
    }

    bool OnInit() {
        #if wxUSE_UNICODE == 0
        #error "must use unicode version of wx libs to ensure data integrity of .cts files"
        #endif
        ASSERT(wxUSE_UNICODE);

        #ifdef __WXMAC__
        wxDisableAsserts();
        #endif

        locale.Init();

        bool portable = false;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                switch ((int)argv[i][1]) {
                    case 'p': portable = true; break;
                }
            } else {
                filename = argv[i];
            }
        }

        const wxString name =
            wxString::Format(L".treesheets-single-instance-check-%s", wxGetUserId().c_str());
        checker = new wxSingleInstanceChecker(name);
        if (checker->IsAnotherRunning()) {
            wxClient client;
            client.MakeConnection(L"localhost", L"4242",
                                  filename.Len() ? filename.wc_str() : L"*");  // fire and forget
            return false;
        }

        sys = new System(portable);
        frame = new MyFrame(argv[0], this);
        SetTopWindow(frame);

        serv = new IPCServer();
        serv->Create(L"4242");

        return true;
    }

    void OnEventLoopEnter(wxEventLoopBase* WXUNUSED(loop))
    {
        if (!initateventloop)
        {
            initateventloop = true;
            frame->AppOnEventLoopEnter();
            sys->Init(filename);
        }
    }

    int OnExit() {
        DELETEP(serv);
        DELETEP(sys);
        DELETEP(checker);
        return 0;
    }

    void MacOpenFile(const wxString &fn) {
        if (sys) sys->Open(fn);
    }

    DECLARE_EVENT_TABLE()
};
