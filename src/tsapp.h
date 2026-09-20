#if wxUSE_UNICODE == 0
    #error "must use unicode version of wx libs to ensure data integrity of .cts files"
#endif

struct IPCServer : wxServer {
    wxConnectionBase *OnAcceptConnection(const wxString &topic) override {
        sys->frame->DeIconize();
        if (!topic.IsEmpty() && topic != "*") { sys->Open(topic); }
        return new wxConnection();
    }
};

struct TSApp : wxApp {
    TSFrame *frame {nullptr};
    unique_ptr<IPCServer> serv {make_unique<IPCServer>()};
    wxString service {
        #ifdef __WXMSW__
                "4242"
        #else
                "/tmp/TreeSheets-socket"
        #endif
    };
    wxString filename;
    bool initiateventloop {false};
    wxString exename;
    wxString exepath;
    unique_ptr<wxSingleInstanceChecker> instance_checker {nullptr};

    struct CmdLine {
        bool portable {false};
        bool single_instance {true};
        bool dump_builtins {false};
        bool start_minimized {false};
    };

    CmdLine ParseCommandLine() {
        CmdLine cl;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] != '-') {
                filename = argv[i];
                continue;
            }
            switch (static_cast<int>(argv[i][1])) {
                case 'p': cl.portable = true; break;
                case 'i': cl.single_instance = false; break;
                case 'm': cl.start_minimized = true; break;
                case 'd':
                    cl.dump_builtins = true;
                    cl.single_instance = false;
                    break;
            }
        }
        return cl;
    }

    void InitPaths() {
        exename = wxStandardPaths::Get().GetExecutablePath();
        exepath = wxFileName(exename).GetPath();
        #ifdef __WXMAC__
            int cut = exepath.Find("/MacOS");
            if (cut > 0) { exepath = exepath.SubString(0, cut) + "/Resources"; }
        #endif
    }

    // Returns true if another instance is running and the request was forwarded to it.
    bool ForwardToRunningInstance() {
        instance_checker = make_unique<wxSingleInstanceChecker>(
            wxTheApp->GetAppName() + '-' + wxGetUserId(), wxStandardPaths::Get().GetTempDir());
        if (!instance_checker->IsAnotherRunning()) return false;
        wxClient client;
        client.MakeConnection("localhost", service,
                              !filename.IsEmpty() ? filename : wxString("*"));  // fire and forget
        return true;
    }

    bool OnInit() override {
        #ifdef __WXMAC__
            wxDisableAsserts();
        #endif
        InitPaths();
        const CmdLine cl = ParseCommandLine();
        if (cl.single_instance && ForwardToRunningInstance()) return false;

        wxStandardPaths::Get().SetFileLayout(wxStandardPathsBase::FileLayout_XDG);
        #ifdef __WXMSW__
            MSWEnableDarkMode();
        #endif
        sys = make_unique<System>(cl.portable);
        sys->UpdatePens();
        if (cl.start_minimized) { sys->startminimized = true; }
        SetupInternationalization();
        #ifdef __WXMSW__
            DeclareHiDpiAwareOnWindows();
        #endif
        frame = new TSFrame(this);

        #ifdef ENABLE_LOBSTER
            auto serr = ScriptInit(GetDataPath("scripts/"));
            if (!serr.empty()) {
                wxLogFatalError("Script system could not initialize: %s", serr);
                return false;
            }
            if (cl.dump_builtins) { TSDumpBuiltinDoc(); }
        #endif
        if (cl.dump_builtins) return false;

        SetTopWindow(frame);
        serv->Create(service);
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
        sys.reset();
        return 0;
    }

    void SetupInternationalization() const {
        wxUILocale::UseDefault();

        #ifdef __WXGTK__
            wxFileTranslationsLoader::AddCatalogLookupPathPrefix("/usr");
            wxFileTranslationsLoader::AddCatalogLookupPathPrefix("/usr/local");
            #ifdef LOCALEDIR
                wxFileTranslationsLoader::AddCatalogLookupPathPrefix(LOCALEDIR);
            #endif
            wxString prefix = wxStandardPaths::Get().GetInstallPrefix();
            wxFileTranslationsLoader::AddCatalogLookupPathPrefix(prefix);
        #endif
        wxFileTranslationsLoader::AddCatalogLookupPathPrefix(GetDataPath("translations"));

        auto *trans = new wxTranslations();
        if (sys->defaultlang.IsEmpty()) {
            trans->SetLanguage(wxEmptyString);
            trans->AddCatalog("ts");
        } else if (sys->defaultlang == "en") {
            trans->SetLanguage(wxLANGUAGE_UNKNOWN);
        } else {
            trans->SetLanguage(sys->defaultlang);
            trans->AddCatalog("ts");
        }

        wxTranslations::Set(trans);
    }

    wxString GetDataPath(const wxString &relpath) const {
        std::filesystem::path candidatePaths[] = {
            std::filesystem::path(!exepath.IsEmpty()
                                      ? exepath.ToStdString() + "/" + relpath.ToStdString()
                                      : relpath.ToStdString()),
            #ifdef TREESHEETS_DATADIR
                std::filesystem::path(TREESHEETS_DATADIR "/" + relpath.ToStdString()),
            #endif
        };
        std::filesystem::path relativePath;
        for (const auto &path : candidatePaths) {
            relativePath = path;
            if (std::filesystem::exists(relativePath)) { break; }
        }

        return {relativePath};
    }

    wxString GetDocPath(const wxString &relpath) const {
        std::filesystem::path candidatePaths[] = {
            std::filesystem::path(!exepath.IsEmpty()
                                      ? exepath.ToStdString() + "/" + relpath.ToStdString()
                                      : relpath.ToStdString()),
            #ifdef TREESHEETS_DOCDIR
                std::filesystem::path(TREESHEETS_DOCDIR "/" + relpath.ToStdString()),
            #endif
        };
        std::filesystem::path relativePath;
        for (const auto &path : candidatePaths) {
            relativePath = path;
            if (std::filesystem::exists(relativePath)) { break; }
        }

        return {relativePath};
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
};
