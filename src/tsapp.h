struct IPCServer : wxServer {
    wxConnectionBase *OnAcceptConnection(const wxString &topic) {
        sys->frame->DeIconize();
        if (topic.Len() && topic != L"*") sys->Open(topic);
        return new wxConnection();
    }
};

struct TSApp : wxApp {
    TSFrame *frame {nullptr};
    unique_ptr<IPCServer> serv {make_unique<IPCServer>()};
    wxString filename;
    bool initiateventloop {false};
    wxLocale locale;
    unique_ptr<wxSingleInstanceChecker> instance_checker {nullptr};

    bool OnInit() override {
        #if wxUSE_UNICODE == 0
            #error "must use unicode version of wx libs to ensure data integrity of .cts files"
        #endif
        ASSERT(wxUSE_UNICODE);

        #ifdef __WXMAC__
            wxDisableAsserts();
            // wxSystemOptions::SetOption("mac.toolbar.no-native", 1);
        #endif

        #ifdef __WXMSW__
            InitUnhandledExceptionFilter(argc, argv);
            DeclareHiDpiAwareOnWindows();
        #endif

        SetupLanguage();

        bool portable = false;
        bool single_instance = true;
        bool dump_builtins = false;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                switch (static_cast<int>(argv[i][1])) {
                    case 'p': portable = true; break;
                    case 'i': single_instance = false; break;
                    case 'd':
                        dump_builtins = true;
                        single_instance = false;
                        break;
                }
            } else {
                filename = argv[i];
            }
        }

        if (single_instance) {
            instance_checker.reset(new wxSingleInstanceChecker(
                wxTheApp->GetAppName() + '-' + wxGetUserId(), wxStandardPaths::Get().GetTempDir()));
            if (instance_checker->IsAnotherRunning()) {
                wxClient client;
                client.MakeConnection(
                    L"localhost", L"4242",
                    filename.Len() ? filename.wc_str() : L"*");  // fire and forget
                return false;
            }
        }

        wxStandardPaths::Get().SetFileLayout(wxStandardPathsBase::FileLayout_XDG);

        sys = new System(portable);
        frame = new TSFrame(GetExecutablePath(), this);

        auto serr = ScriptInit(frame->GetDataPath("scripts/"));
        if (!serr.empty()) {
            wxLogFatalError(L"Script system could not initialize: %s", serr);
            return false;
        }
        if (dump_builtins) {
            TSDumpBuiltinDoc();
            return false;
        }

        SetTopWindow(frame);

        serv->Create(L"4242");

        return true;
    }

    void OnEventLoopEnter(wxEventLoopBase *WXUNUSED(loop)) override {
        if (!initiateventloop) {
            initiateventloop = true;
            frame->AppOnEventLoopEnter();
            sys->Init(filename);
        }
    }

    #ifdef __WXMAC__
        void MacOpenFiles(const wxArrayString &filenames) override {
            if (!sys) return;
            // MacOpenFiles does not trigger OnEventLoopEnter so we need
            // to do this manually
            if (!initiateventloop) {
                initiateventloop = true;
                frame->AppOnEventLoopEnter();
            }
            for (auto &fn : filenames) { sys->Init(fn); }
        }
    #endif

    int OnExit() override {
        DELETEP(sys);
        return 0;
    }

    void SetupLanguage() {
        auto language = wxLocale::GetSystemLanguage();
        if (language == wxLANGUAGE_UNKNOWN || !wxLocale::IsAvailable(language)) {
            language = wxLANGUAGE_ENGLISH;
        }
        locale.Init(language);
    }

    void AddTranslation(const wxString &basepath) {
        #ifdef __WXGTK__
            locale.AddCatalogLookupPathPrefix(L"/usr");
            locale.AddCatalogLookupPathPrefix(L"/usr/local");
            #ifdef LOCALEDIR
                locale.AddCatalogLookupPathPrefix(LOCALEDIR);
            #endif
            wxString prefix = wxStandardPaths::Get().GetInstallPrefix();
            locale.AddCatalogLookupPathPrefix(prefix);
        #endif
        locale.AddCatalogLookupPathPrefix(basepath);
        locale.AddCatalog(L"ts", (wxLanguage)locale.GetLanguage());
    }

    wxString GetExecutablePath() {
        wxString executablepath = argv[0];
        #if defined(__WXMAC__)
            char path[PATH_MAX];
            uint32_t size = sizeof(path);
            if(_NSGetExecutablePath(path, &size) == 0) executablepath = path;
        #elif defined(__WXGTK__)
            // argv[0] could be relative, this is apparently a more robust way to get the
            // full path.
            char path[PATH_MAX];
            auto len = readlink("/proc/self/exe", path, PATH_MAX - 1);
            if (len >= 0) {
                path[len] = 0;
                executablepath = path;
            }
        #endif
        return executablepath;
    }

    #ifdef __WXMSW__
        void DeclareHiDpiAwareOnWindows() {
            // wxWidgets should really be doing this itself, but it does not (or expects you to
            // want to use a manifest), so we try to use the most recent Windows API to declare
            // ourselves as HiDPI compatible.

            #ifndef DPI_ENUMS_DECLARED
                typedef enum PROCESS_DPI_AWARENESS {
                    PROCESS_DPI_UNAWARE = 0,
                    PROCESS_SYSTEM_DPI_AWARE = 1,
                    PROCESS_PER_MONITOR_DPI_AWARE = 2
                } PROCESS_DPI_AWARENESS;
            #endif

            using SetProcessDPIAware_T = BOOL(WINAPI *)(void);
            using SetProcessDpiAwareness_T = HRESULT(WINAPI *)(PROCESS_DPI_AWARENESS);
            using SetProcessDpiAwarenessContext_T = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);

            SetProcessDPIAware_T SetProcessDPIAware = nullptr;
            SetProcessDpiAwareness_T SetProcessDpiAwareness = nullptr;
            SetProcessDpiAwarenessContext_T SetProcessDpiAwarenessContext = nullptr;

            HMODULE user32 = LoadLibraryA("User32.dll");
            HMODULE shcore = LoadLibraryA("Shcore.dll");

            if (user32) {
                SetProcessDPIAware = (SetProcessDPIAware_T)GetProcAddress(user32, "SetProcessDPIAware");
                SetProcessDpiAwarenessContext = (SetProcessDpiAwarenessContext_T)GetProcAddress(
                    user32, "SetProcessDpiAwarenessContext");
            }
            if (shcore) {
                SetProcessDpiAwareness =
                    (SetProcessDpiAwareness_T)GetProcAddress(shcore, "SetProcessDpiAwareness");
            }

            if (SetProcessDpiAwarenessContext) {
                SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            } else if (SetProcessDpiAwareness) {
                SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            } else if (SetProcessDPIAware) {
                SetProcessDPIAware();
            }

            if (user32) FreeLibrary(user32);
            if (shcore) FreeLibrary(shcore);
        }
    #endif

    DECLARE_EVENT_TABLE()
};
