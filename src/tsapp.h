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
    wxString service {
        #ifdef __WXMSW__
                L"4242"
        #else
                L"/tmp/TreeSheets-socket"
        #endif
    };
    wxString filename;
    bool initiateventloop {false};
    wxString exename;
    wxString exepath;
    unique_ptr<wxSingleInstanceChecker> instance_checker {nullptr};

    bool OnInit() override {
        #if wxUSE_UNICODE == 0
            #error "must use unicode version of wx libs to ensure data integrity of .cts files"
        #endif
        ASSERT(wxUSE_UNICODE);

        exename = GetExecutablePath();
        exepath = wxFileName(exename).GetPath();

        #if defined(__WXMAC__)
            int cut = exepath.Find("/MacOS");
            if (cut > 0) { exepath = exepath.SubString(0, cut) + "/Resources"; }
            wxDisableAsserts();
            // wxSystemOptions::SetOption("mac.toolbar.no-native", 1);
        #elif defined(__WXMSW__)
            DeclareHiDpiAwareOnWindows();
        #endif

        bool portable = false;
        bool single_instance = true;
        bool dump_builtins = false;
        bool start_minimized = false;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                switch (static_cast<int>(argv[i][1])) {
                    case 'p': portable = true; break;
                    case 'i': single_instance = false; break;
                    case 'm': start_minimized = true; break;
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
                    L"localhost", service,
                    filename.Len() ? filename.wc_str() : L"*");  // fire and forget
                return false;
            }
        }

        wxStandardPaths::Get().SetFileLayout(wxStandardPathsBase::FileLayout_XDG);
        sys = new System(portable);
        if (start_minimized) sys->startminimized = true;
        SetupInternationalization();
        frame = new TSFrame(this);

        #ifdef ENABLE_LOBSTER
            auto serr = ScriptInit(GetDataPath("scripts/"));
            if (!serr.empty()) {
                wxLogFatalError(L"Script system could not initialize: %s", serr);
                return false;
            }
        #endif

        if (dump_builtins) {
            #ifdef ENABLE_LOBSTER
                TSDumpBuiltinDoc();
            #endif
            return false;
        }

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
        DELETEP(sys);
        return 0;
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

    void SetupInternationalization() {
        wxUILocale::UseDefault();

        #ifdef __WXGTK__
            wxFileTranslationsLoader::AddCatalogLookupPathPrefix(L"/usr");
            wxFileTranslationsLoader::AddCatalogLookupPathPrefix(L"/usr/local");
            #ifdef LOCALEDIR
                wxFileTranslationsLoader::AddCatalogLookupPathPrefix(LOCALEDIR);
            #endif
            wxString prefix = wxStandardPaths::Get().GetInstallPrefix();
            wxFileTranslationsLoader::AddCatalogLookupPathPrefix(prefix);
        #endif
        wxFileTranslationsLoader::AddCatalogLookupPathPrefix(GetDataPath("translations"));

        auto trans = new wxTranslations();
        trans->SetLanguage(sys->defaultlang);
        trans->AddCatalog(L"ts");

        wxTranslations::Set(trans);
    }

    wxString GetDataPath(const wxString &relpath) {
        std::filesystem::path candidatePaths[] = {
            std::filesystem::path(exepath.Length()
                                      ? exepath.ToStdString() + "/" + relpath.ToStdString()
                                      : relpath.ToStdString()),
            #ifdef TREESHEETS_DATADIR
                std::filesystem::path(TREESHEETS_DATADIR "/" + relpath.ToStdString()),
            #endif
        };
        std::filesystem::path relativePath;
        for (auto path : candidatePaths) {
            relativePath = path;
            if (std::filesystem::exists(relativePath)) { break; }
        }

        return wxString(relativePath.c_str());
    }

    wxString GetDocPath(const wxString &relpath) {
        std::filesystem::path candidatePaths[] = {
            std::filesystem::path(exepath.Length()
                                      ? exepath.ToStdString() + "/" + relpath.ToStdString()
                                      : relpath.ToStdString()),
            #ifdef TREESHEETS_DOCDIR
                std::filesystem::path(TREESHEETS_DOCDIR "/" + relpath.ToStdString()),
            #endif
        };
        std::filesystem::path relativePath;
        for (auto path : candidatePaths) {
            relativePath = path;
            if (std::filesystem::exists(relativePath)) { break; }
        }

        return wxString(relativePath.c_str());
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
