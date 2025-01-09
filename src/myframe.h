struct MyFrame : wxFrame {
    wxString exepath_;
    MyApp *app;
    wxIcon icon;
    wxTaskBarIcon tbi;
    wxMenu *editmenupopup;
    wxFileHistory filehistory;
    unique_ptr<wxFileSystemWatcher> watcher {nullptr};
    wxAuiNotebook *nb {nullptr};
    unique_ptr<wxAuiManager> aui {make_unique<wxAuiManager>(this)};
    wxBitmap line_nw;
    wxBitmap line_sw;
    wxBitmap foldicon;
    bool fromclosebox {true};
    bool watcherwaitingforuser {false};
    bool darkmode {wxSystemSettings::GetAppearance().IsDark()};
    vector<string> scripts_in_menu;
    wxToolBar *tb {nullptr};
    wxColour toolbgcol {0xD8C7BC};
    wxTextCtrl *filter {nullptr};
    wxTextCtrl *replaces {nullptr};
    ColorDropdown *celldd {nullptr};
    ColorDropdown *textdd {nullptr};
    ColorDropdown *borddd {nullptr};
    ImageDropdown *idd {nullptr};
    wxString imagepath;
    BlinkTimer bt;
    int refreshhack {0};
    int refreshhackinstances {0};

    wxString GetDocPath(const wxString &relpath) {
        std::filesystem::path candidatePaths[] = {
            std::filesystem::path(exepath_.Length() ? exepath_.ToStdString() + "/" + relpath.ToStdString() : relpath.ToStdString()),
            #ifdef TREESHEETS_DOCDIR
                std::filesystem::path(TREESHEETS_DOCDIR "/" + relpath.ToStdString()),
            #endif
        };
        std::filesystem::path relativePath;
        for (auto path : candidatePaths) {
            relativePath = path;
            if (std::filesystem::exists(relativePath)) { break; }
        }

        return wxString(relativePath);
    }
    wxString GetDataPath(const wxString &relpath) {
        std::filesystem::path candidatePaths[] = {
            std::filesystem::path(exepath_.Length() ? exepath_.ToStdString() + "/" + relpath.ToStdString() : relpath.ToStdString()),
            #ifdef TREESHEETS_DATADIR
                std::filesystem::path(TREESHEETS_DATADIR "/" + relpath.ToStdString()),
            #endif
        };
        std::filesystem::path relativePath;
        for (auto path : candidatePaths) {
            relativePath = path;
            if (std::filesystem::exists(relativePath)) { break; }
        }

        return wxString(relativePath);
    }

    std::map<wxString, wxString> menustrings;

    void MyAppend(wxMenu *menu, int tag, const wxString &contents, const char *help = "") {
        wxString item = contents;
        wxString key = "";
        int pos = contents.Find("\t");
        if (pos >= 0) {
            item = contents.Mid(0, pos);
            key = contents.Mid(pos + 1);
        }
        key = sys->cfg->Read(item, key);
        wxString newcontents = item;
        if (key.Length()) newcontents += "\t" + key;
        menu->Append(tag, newcontents, help);
        menustrings[item] = key;
    }

    MyFrame(wxString exename, MyApp *_app)
        : wxFrame((wxFrame *)nullptr, wxID_ANY, "TreeSheets", wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_FRAME_STYLE),
          app(_app) {
        sys->frame = this;
        exepath_ = wxFileName(exename).GetPath();
        #ifdef __WXMAC__
        int cut = exepath_.Find("/MacOS");
        if (cut > 0) { exepath_ = exepath_.SubString(0, cut) + "/Resources"; }
        #endif

        class MyLog : public wxLog {
            void DoLogString(const char *msg, time_t timestamp) { DoLogText(*msg); }
            void DoLogText(const wxString &msg) {
                #ifdef WIN32
                OutputDebugString(msg);
                OutputDebugString(L"\n");
                #else
                fputws(msg, stderr);
                fputws(L"\n", stderr);
                #endif
            }
        };

        wxLog::SetActiveTarget(new MyLog());

        wxLogMessage("%s", wxVERSION_STRING);

        wxLogMessage("locale: %s", std::setlocale(LC_CTYPE, nullptr));

        app->AddTranslation(GetDataPath("translations"));

        wxInitAllImageHandlers();

        wxIconBundle icons;
        wxIcon iconbig;
        #ifdef WIN32
            int iconsmall = ::GetSystemMetrics(SM_CXSMICON);
            int iconlarge = ::GetSystemMetrics(SM_CXICON);
        #endif
        icon.LoadFile(GetDataPath("images/icon16.png"), wxBITMAP_TYPE_PNG
            #ifdef WIN32
                , iconsmall, iconsmall
            #endif
        );
        iconbig.LoadFile(GetDataPath("images/icon32.png"), wxBITMAP_TYPE_PNG
            #ifdef WIN32
                , iconlarge, iconlarge
            #endif
        );
        if (!icon.IsOk() || !iconbig.IsOk()) {
            wxMessageBox(_("Error loading core data file (TreeSheets not installed correctly?)"),
                         _("Initialization Error"), wxOK, this);
            exit(1);
        }
        icons.AddIcon(icon);
        icons.AddIcon(iconbig);
        SetIcons(icons);

        RenderFolderIcon();
        line_nw.LoadFile(GetDataPath("images/render/line_nw.png"), wxBITMAP_TYPE_PNG);
        line_sw.LoadFile(GetDataPath("images/render/line_sw.png"), wxBITMAP_TYPE_PNG);

        imagepath = GetDataPath("images/nuvola/dropdown/");

        if (sys->singletray)
            tbi.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_UP,
                        wxTaskBarIconEventHandler(MyFrame::OnTBIDBLClick), nullptr, this);
        else
            tbi.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_DCLICK,
                        wxTaskBarIconEventHandler(MyFrame::OnTBIDBLClick), nullptr, this);

        bool showtbar, showsbar, lefttabs;

        sys->cfg->Read("showtbar", &showtbar, true);
        sys->cfg->Read("showsbar", &showsbar, true);
        sys->cfg->Read("lefttabs", &lefttabs, true);

        filehistory.Load(*sys->cfg);

        wxMenu *expmenu = new wxMenu();
        MyAppend(expmenu, A_EXPXML, _("&XML..."),
                 _("Export the current view as XML (which can also be reimported without losing structure)"));
        MyAppend(expmenu, A_EXPHTMLT, _("&HTML (Tables+Styling)..."),
                 _("Export the current view as HTML using nested tables, that will look somewhat like the TreeSheet"));
        MyAppend(expmenu, A_EXPHTMLB, _("HTML (&Bullet points)..."),
                 _("Export the current view as HTML as nested bullet points."));
        MyAppend(expmenu, A_EXPHTMLO, _("HTML (&Outline)..."),
                 _("Export the current view as HTML as nested headers, suitable for importing into Word's outline mode"));
        MyAppend(
            expmenu, A_EXPTEXT, _("Indented &Text..."),
            _("Export the current view as tree structured text, using spaces for each indentation level. Suitable for importing into mindmanagers and general text programs"));
        MyAppend(
            expmenu, A_EXPCSV, _("&Comma delimited text (CSV)..."),
            _("Export the current view as CSV. Good for spreadsheets and databases. Only works on grids with no sub-grids (use the Flatten operation first if need be)"));
        MyAppend(expmenu, A_EXPIMAGE, _("&Image..."),
                 _("Export the current view as an image. Useful for faithful renderings of the TreeSheet, and programs that don't accept any of the above options"));

        wxMenu *impmenu = new wxMenu();
        MyAppend(impmenu, A_IMPXML, _("XML..."));
        MyAppend(impmenu, A_IMPXMLA, _("XML (attributes too, for OPML etc)..."));
        MyAppend(impmenu, A_IMPTXTI, _("Indented text..."));
        MyAppend(impmenu, A_IMPTXTC, _("Comma delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTS, _("Semi-Colon delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTT, _("Tab delimited text..."));

        wxMenu *recentmenu = new wxMenu();
        filehistory.UseMenu(recentmenu);
        filehistory.AddFilesToMenu();

        wxMenu *filemenu = new wxMenu();
        MyAppend(filemenu, wxID_NEW, _("&New\tCTRL+n"));
        MyAppend(filemenu, wxID_OPEN, _("&Open...\tCTRL+o"));
        MyAppend(filemenu, wxID_CLOSE, _("&Close\tCTRL+w"));
        filemenu->AppendSubMenu(recentmenu, _("&Recent files"));
        MyAppend(filemenu, wxID_SAVE, _("&Save\tCTRL+s"));
        MyAppend(filemenu, wxID_SAVEAS, _("Save &As..."));
        MyAppend(filemenu, A_SAVEALL, _("Save All"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, A_PAGESETUP, _("Page Setup..."));
        MyAppend(filemenu, A_PRINTSCALE, _("Set Print Scale..."));
        MyAppend(filemenu, wxID_PREVIEW, _("Print preview..."));
        MyAppend(filemenu, wxID_PRINT, _("&Print...\tCTRL+p"));
        filemenu->AppendSeparator();
        filemenu->AppendSubMenu(expmenu, _("Export &view as"));
        filemenu->AppendSubMenu(impmenu, _("Import file from"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, wxID_EXIT, _("&Exit\tCTRL+q"));

        wxMenu *editmenu;
        loop(twoeditmenus, 2) {
            wxMenu *sizemenu = new wxMenu();
            MyAppend(sizemenu, A_INCSIZE, _("&Increase text size (SHIFT+mousewheel)\tSHIFT+PGUP"));
            MyAppend(sizemenu, A_DECSIZE, _("&Decrease text size (SHIFT+mousewheel)\tSHIFT+PGDN"));
            MyAppend(sizemenu, A_RESETSIZE, _("&Reset text sizes\tCTRL+SHIFT+s"));
            MyAppend(sizemenu, A_MINISIZE, _("&Shrink text of all sub-grids\tCTRL+SHIFT+m"));
            sizemenu->AppendSeparator();
            MyAppend(sizemenu, A_INCWIDTH, _("Increase column width (ALT+mousewheel)\tALT+PGUP"));
            MyAppend(sizemenu, A_DECWIDTH, _("Decrease column width (ALT+mousewheel)\tALT+PGDN"));
            MyAppend(sizemenu, A_INCWIDTHNH,
                     _("Increase column width (no sub grids)\tCTRL+ALT+PGUP"));
            MyAppend(sizemenu, A_DECWIDTHNH,
                     _("Decrease column width (no sub grids)\tCTRL+ALT+PGDN"));
            MyAppend(sizemenu, A_RESETWIDTH, _("Reset column widths\tCTRL+SHIFT+w"));

            wxMenu *bordmenu = new wxMenu();
            MyAppend(bordmenu, A_BORD0, _("Border &0\tCTRL+SHIFT+9"));
            MyAppend(bordmenu, A_BORD1, _("Border &1\tCTRL+SHIFT+1"));
            MyAppend(bordmenu, A_BORD2, _("Border &2\tCTRL+SHIFT+2"));
            MyAppend(bordmenu, A_BORD3, _("Border &3\tCTRL+SHIFT+3"));
            MyAppend(bordmenu, A_BORD4, _("Border &4\tCTRL+SHIFT+4"));
            MyAppend(bordmenu, A_BORD5, _("Border &5\tCTRL+SHIFT+5"));

            wxMenu *selmenu = new wxMenu();
            MyAppend(selmenu, A_NEXT,
                #ifdef __WXGTK__
                    _("Move to next cell (TAB)")
                #else
                    _("Move to next cell\tTAB")
                #endif
            );
            MyAppend(selmenu, A_PREV, 
                #ifdef __WXGTK__
                    _("Move to previous cell (SHIFT+TAB)")
                #else
                    _("Move to previous cell\tSHIFT+TAB")
                #endif
            );
            selmenu->AppendSeparator();
            MyAppend(selmenu, wxID_SELECTALL, _("Select &all in current grid/cell\tCTRL+a"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LEFT, 
                #ifdef __WXGTK__
                    _("Move Selection Left (LEFT)")
                #else
                    _("Move Selection Left\tLEFT")
                #endif
            );
            MyAppend(selmenu, A_RIGHT, 
                #ifdef __WXGTK__
                    _("Move Selection Right (RIGHT)")
                #else 
                    _("Move Selection Right\tRIGHT")
                #endif
            );
            MyAppend(selmenu, A_UP, 
                #ifdef __WXGTK__
                    _("Move Selection Up (UP)")
                #else
                    _("Move Selection Up\tUP")
                #endif
            );
            MyAppend(selmenu, A_DOWN, 
                #ifdef __WXGTK__
                    _("Move Selection Down (DOWN)")
                #else
                    _("Move Selection Down\tDOWN")
                #endif
            );
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_MLEFT, _("Move Cells Left\tCTRL+LEFT"));
            MyAppend(selmenu, A_MRIGHT, _("Move Cells Right\tCTRL+RIGHT"));
            MyAppend(selmenu, A_MUP, _("Move Cells Up\tCTRL+UP"));
            MyAppend(selmenu, A_MDOWN, _("Move Cells Down\tCTRL+DOWN"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SLEFT, _("Extend Selection Left\tSHIFT+LEFT"));
            MyAppend(selmenu, A_SRIGHT, _("Extend Selection Right\tSHIFT+RIGHT"));
            MyAppend(selmenu, A_SUP, _("Extend Selection Up\tSHIFT+UP"));
            MyAppend(selmenu, A_SDOWN, _("Extend Selection Down\tSHIFT+DOWN"));
            MyAppend(selmenu, A_SCOLS, _("Extend Selection Full Columns"));
            MyAppend(selmenu, A_SROWS, _("Extend Selection Full Rows"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_CANCELEDIT, _("Select &Parent\tESC"));
            MyAppend(selmenu, A_ENTERGRID, _("Select First &Child\tSHIFT+ENTER"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LINK, _("Go To &Matching Cell (Text)\tF6"));
            MyAppend(selmenu, A_LINKREV, _("Go To Matching Cell (Text, Reverse)\tSHIFT+F6"));
            MyAppend(selmenu, A_LINKIMG, _("Go To Matching Cell (Image)\tF7"));
            MyAppend(selmenu, A_LINKIMGREV, _("Go To Matching Cell (Image, Reverse)\tSHIFT+F7"));

            wxMenu *temenu = new wxMenu();
            MyAppend(temenu, A_LEFT, _("Cursor Left\tLEFT"));
            MyAppend(temenu, A_RIGHT, _("Cursor Right\tRIGHT"));
            MyAppend(temenu, A_MLEFT, _("Word Left\tCTRL+LEFT"));
            MyAppend(temenu, A_MRIGHT, _("Word Right\tCTRL+RIGHT"));
            temenu->AppendSeparator();
            MyAppend(temenu, A_SLEFT, _("Extend Selection Left\tSHIFT+LEFT"));
            MyAppend(temenu, A_SRIGHT, _("Extend Selection Right\tSHIFT+RIGHT"));
            MyAppend(temenu, A_SCLEFT, _("Extend Selection Word Left\tCTRL+SHIFT+LEFT"));
            MyAppend(temenu, A_SCRIGHT, _("Extend Selection Word Right\tCTRL+SHIFT+RIGHT"));
            MyAppend(temenu, A_SHOME, _("Extend Selection to Start\tSHIFT+HOME"));
            MyAppend(temenu, A_SEND, _("Extend Selection to End\tSHIFT+END"));
            temenu->AppendSeparator();
            MyAppend(temenu, A_HOME, _("Start of line of text\tHOME"));
            MyAppend(temenu, A_END, _("End of line of text\tEND"));
            MyAppend(temenu, A_CHOME, _("Start of text\tCTRL+HOME"));
            MyAppend(temenu, A_CEND, _("End of text\tCTRL+END"));
            temenu->AppendSeparator();
            MyAppend(temenu, A_ENTERCELL, _("Enter/exit text edit mode\tENTER"));
            MyAppend(temenu, A_ENTERCELL_JUMPTOEND, _("Enter/exit text edit mode\tF2"));
            MyAppend(temenu, A_PROGRESSCELL, _("Enter/exit text edit to the right\tALT+ENTER"));
            MyAppend(temenu, A_CANCELEDIT, _("Cancel text edits\tESC"));

            wxMenu *stmenu = new wxMenu();
            MyAppend(stmenu, wxID_BOLD, _("Toggle cell &BOLD\tCTRL+b"));
            MyAppend(stmenu, wxID_ITALIC, _("Toggle cell &ITALIC\tCTRL+i"));
            MyAppend(stmenu, A_TT, _("Toggle cell &typewriter\tCTRL+ALT+t"));
            MyAppend(stmenu, wxID_UNDERLINE, _("Toggle cell &underlined\tCTRL+u"));
            MyAppend(stmenu, wxID_STRIKETHROUGH, _("Toggle cell &strikethrough\tCTRL+t"));
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_RESETSTYLE, _("&Reset text styles\tCTRL+SHIFT+r"));
            MyAppend(stmenu, A_RESETCOLOR, _("Reset &colors\tCTRL+SHIFT+c"));
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_LASTCELLCOLOR, _("Apply last cell color\tSHIFT+ALT+c"));
            MyAppend(stmenu, A_LASTTEXTCOLOR, _("Apply last text color\tSHIFT+ALT+t"));
            MyAppend(stmenu, A_LASTBORDCOLOR, _("Apply last border color\tSHIFT+ALT+b"));
            MyAppend(stmenu, A_OPENCELLCOLOR, _("Open cell colors\tSHIFT+ALT+F9"));
            MyAppend(stmenu, A_OPENTEXTCOLOR, _("Open text colors\tSHIFT+ALT+F10"));
            MyAppend(stmenu, A_OPENBORDCOLOR, _("Open border colors\tSHIFT+ALT+F11"));

            wxMenu *tagmenu = new wxMenu();
            MyAppend(tagmenu, A_TAGADD, _("&Add Cell Text as Tag"));
            MyAppend(tagmenu, A_TAGREMOVE, _("&Remove Cell Text from Tags"));
            MyAppend(tagmenu, A_NOP, _("&Set Cell Text to tag (use CTRL+RMB)"),
                     _("Hold CTRL while pressing right mouse button to quickly set a tag for the current cell using a popup menu"));

            wxMenu *orgmenu = new wxMenu();
            MyAppend(orgmenu, A_TRANSPOSE, _("&Transpose\tCTRL+SHIFT+t"),
                     _("changes the orientation of a grid"));
            MyAppend(orgmenu, A_SORT, _("Sort &Ascending"),
                     _("Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_SORTD, _("Sort &Descending"),
                     _("Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_HSWAP, _("Hierarchy &Swap\tF8"),
                     _("Swap all cells with this text at this level (or above) with the parent"));
            MyAppend(orgmenu, A_HIFY, _("&Hierarchify"),
                     _("Convert an NxN grid with repeating elements per column into an 1xN grid with hierarchy, useful to convert data from spreadsheets"));
            MyAppend(orgmenu, A_FLATTEN, _("&Flatten"),
                     _("Takes a hierarchy (nested 1xN or Nx1 grids) and converts it into a flat NxN grid, useful for export to spreadsheets"));

            wxMenu *imgmenu = new wxMenu();
            MyAppend(imgmenu, A_IMAGE, _("&Add Image"), _("Adds an image to the selected cell"));
            MyAppend(imgmenu, A_IMAGESVA, _("Save Image(s) to disk"),
                     _("Save image(s) to disk. Multiple images will be saved with a counter appended to each file name."));
            imgmenu->AppendSeparator();
            MyAppend(
                imgmenu, A_IMAGESCP, _("&Scale Image (re-sample pixels, by %)"),
                _("Change the image size if it is too big, by reducing the amount of pixels"));
            MyAppend(
                imgmenu, A_IMAGESCW, _("&Scale Image (re-sample pixels, by width)"),
                _("Change the image size if it is too big, by reducing the amount of pixels"));
            MyAppend(imgmenu, A_IMAGESCF, _("&Scale Image (display only)"),
                     _("Change the image size if it is too big or too small, by changing the size shown on screen. Applies to all uses of this image."));
            MyAppend(imgmenu, A_IMAGESCN, _("&Reset Scale (display only)"),
                     _("Change the scale to match DPI of the current display. Applies to all uses of this image."));
            imgmenu->AppendSeparator();
            MyAppend(imgmenu, A_SAVE_AS_JPEG, _("Save image as JPEG"),
                     _("Save the image in the TreeSheets file in JPEG format"));
            MyAppend(imgmenu, A_SAVE_AS_PNG, _("Save image as PNG (default)"),
                     _("Save the image in the TreeSheets file in PNG format"));
            imgmenu->AppendSeparator();
            MyAppend(imgmenu, A_IMAGER, _("&Remove Image(s)"),
                     _("Remove image(s) from the selected cells"));

            wxMenu *navmenu = new wxMenu();
            MyAppend(navmenu, A_BROWSE, _("Open link in &browser\tF5"),
                     _("Opens up the text from the selected cell in browser (should start be a valid URL)"));
            MyAppend(navmenu, A_BROWSEF, _("Open &file\tF4"),
                     _("Opens up the text from the selected cell in default application for the file type"));

            #ifdef __WXMAC__
                #define CTRLORALT "CTRL"
            #else
                #define CTRLORALT "ALT"
            #endif

            wxMenu *laymenu = new wxMenu();
            MyAppend(laymenu, A_V_GS,
                     _("Vertical Layout with Grid Style Rendering\t" CTRLORALT "+1"));
            MyAppend(laymenu, A_V_BS,
                     _("Vertical Layout with Bubble Style Rendering\t" CTRLORALT "+2"));
            MyAppend(laymenu, A_V_LS,
                     _("Vertical Layout with Line Style Rendering\t" CTRLORALT "+3"));
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_H_GS,
                     _("Horizontal Layout with Grid Style Rendering\t" CTRLORALT "+4"));
            MyAppend(laymenu, A_H_BS,
                     _("Horizontal Layout with Bubble Style Rendering\t" CTRLORALT "+5"));
            MyAppend(laymenu, A_H_LS,
                     _("Horizontal Layout with Line Style Rendering\t" CTRLORALT "+6"));
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_GS, _("Grid Style Rendering\t" CTRLORALT "+7"));
            MyAppend(laymenu, A_BS, _("Bubble Style Rendering\t" CTRLORALT "+8"));
            MyAppend(laymenu, A_LS, _("Line Style Rendering\t" CTRLORALT "+9"));
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_TEXTGRID, _("Toggle Vertical Layout\t" CTRLORALT "+0"),
                     _("Make a hierarchy layout more vertical (default) or more horizontal"));

            editmenu = new wxMenu();
            MyAppend(editmenu, wxID_CUT, _("Cu&t\tCTRL+x"));
            MyAppend(editmenu, wxID_COPY, _("&Copy\tCTRL+c"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_COPYWI, _("Copy with &Images\tCTRL+ALT+c"));
            MyAppend(editmenu, A_COPYBM, _("&Copy as Bitmap"));
            MyAppend(editmenu, A_COPYCT, _("Copy As Continuous Text"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, wxID_PASTE, _("&Paste\tCTRL+v"));
            MyAppend(editmenu, A_PASTESTYLE, _("Paste Style Only\tCTRL+SHIFT+v"),
                     _("only sets the colors and style of the copied cell, and keeps the text"));
            MyAppend(editmenu, A_COLLAPSE, _("Collapse Ce&lls\tCTRL+l"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, wxID_UNDO, _("&Undo\tCTRL+z"),
                     _("revert the changes, one step at a time"));
            MyAppend(editmenu, wxID_REDO, _("&Redo\tCTRL+y"),
                     _("redo any undo steps, if you haven't made changes since"));
            editmenu->AppendSeparator();
            MyAppend(
                editmenu, A_DELETE, _("&Delete After\tDEL"),
                _("Deletes the column of cells after the selected grid line, or the row below"));
            MyAppend(
                editmenu, A_BACKSPACE, _("Delete Before\tBACK"),
                _("Deletes the column of cells before the selected grid line, or the row above"));
            MyAppend(editmenu, A_DELETE_WORD, _("Delete Word After\tCTRL+DEL"),
                     _("Deletes the entire word after the cursor"));
            MyAppend(editmenu, A_BACKSPACE_WORD, _("Delete Word Before\tCTRL+BACK"),
                     _("Deletes the entire word before the cursor"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_NEWGRID,
                     #ifdef __WXMAC__
                     _("&Insert New Grid\tCTRL+g"),
                     #else
                     _("&Insert New Grid\tINS"),
                     #endif
                     _("Adds a grid to the selected cell"));
            MyAppend(editmenu, A_WRAP, _("&Wrap in new parent\tF9"),
                     _("Creates a new level of hierarchy around the current selection"));
            editmenu->AppendSeparator();
            // F10 is tied to the OS on both Ubuntu and OS X, and SHIFT+F10 is now right
            // click on all platforms?
            MyAppend(editmenu, A_FOLD,
                     #ifndef WIN32
                     _("Toggle Fold\tCTRL+F10"),
                     #else
                     _("Toggle Fold\tF10"),
                     #endif
                    _("Toggles showing the grid of the selected cell(s)"));
            MyAppend(editmenu, A_FOLDALL, _("Fold All\tCTRL+SHIFT+F10"),
                _("Folds the grid of the selected cell(s) recursively"));
            MyAppend(editmenu, A_UNFOLDALL, _("Unfold All\tCTRL+ALT+F10"),
                _("Unfolds the grid of the selected cell(s) recursively"));
            editmenu->AppendSeparator();
            editmenu->AppendSubMenu(selmenu, _("&Selection..."));
            editmenu->AppendSubMenu(orgmenu, _("&Grid Reorganization..."));
            editmenu->AppendSubMenu(laymenu, _("&Layout && Render Style..."));
            editmenu->AppendSubMenu(imgmenu, _("&Images..."));
            editmenu->AppendSubMenu(navmenu, _("&Browsing..."));
            editmenu->AppendSubMenu(temenu, _("Text &Editing..."));
            editmenu->AppendSubMenu(sizemenu, _("Text Sizing..."));
            editmenu->AppendSubMenu(stmenu, _("Text Style..."));
            editmenu->AppendSubMenu(bordmenu, _("Set Grid Border Width..."));
            editmenu->AppendSubMenu(tagmenu, _("Tag..."));

            if (!twoeditmenus) editmenupopup = editmenu;
        }

        wxMenu *semenu = new wxMenu();
        MyAppend(semenu, wxID_FIND, _("&Search\tCTRL+f"));
        semenu->AppendCheckItem(A_CASESENSITIVESEARCH, _("Case-sensitive search"));
        semenu->Check(A_CASESENSITIVESEARCH, sys->casesensitivesearch);
        MyAppend(semenu, A_SEARCHNEXT, _("&Go To Next Search Result\tF3"));
        MyAppend(semenu, A_SEARCHPREV, _("Go To &Previous Search Result\tSHIFT+F3"));
        MyAppend(semenu, wxID_REPLACE, _("&Replace\tCTRL+h"));
        MyAppend(semenu, A_REPLACEONCE, _("Replace in Current &Selection\tCTRL+k"));
        MyAppend(semenu, A_REPLACEONCEJ, _("Replace in Current Selection && &Jump Next\tCTRL+j"));
        MyAppend(semenu, A_REPLACEALL, _("Replace &All"));

        wxMenu *scrollmenu = new wxMenu();
        MyAppend(scrollmenu, A_AUP, _("Scroll Up (mousewheel)\tPGUP"));
        MyAppend(scrollmenu, A_AUP, _("Scroll Up (mousewheel)\tALT+UP"));
        MyAppend(scrollmenu, A_ADOWN, _("Scroll Down (mousewheel)\tPGDN"));
        MyAppend(scrollmenu, A_ADOWN, _("Scroll Down (mousewheel)\tALT+DOWN"));
        MyAppend(scrollmenu, A_ALEFT, _("Scroll Left\tALT+LEFT"));
        MyAppend(scrollmenu, A_ARIGHT, _("Scroll Right\tALT+RIGHT"));

        wxMenu *filtermenu = new wxMenu();
        MyAppend(filtermenu, A_FILTEROFF, _("Turn filter &off"));
        MyAppend(filtermenu, A_FILTERS, _("Show only cells in current search"));
        MyAppend(filtermenu, A_FILTERRANGE, _("Show last edits in specific date range"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER5, _("Show 5% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER10, _("Show 10% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER20, _("Show 20% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER50, _("Show 50% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTERM, _("Show 1% more than the last filter"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTERL, _("Show 1% less than the last filter"));
        MyAppend(filtermenu, A_FILTERBYCELLBG, _("Filter by the same cell color"));
        MyAppend(filtermenu, A_FILTERMATCHNEXT, _("Go to next filter match\tCTRL+F3"));

        wxMenu *viewmenu = new wxMenu();
        MyAppend(viewmenu, A_ZOOMIN, _("Zoom &In (CTRL+mousewheel)\tCTRL+PGUP"));
        MyAppend(viewmenu, A_ZOOMOUT, _("Zoom &Out (CTRL+mousewheel)\tCTRL+PGDN"));
        MyAppend(viewmenu, A_NEXTFILE,
                 #ifndef __WXGTK__
                 _("Switch to &next file/tab\tCTRL+TAB"));
                 #else
                 // On Linux, this conflicts with CTRL+I, see Document::Key()
                 // CTRL+SHIFT+TAB below still works, so that will have to be used to switch tabs.
                 _("Switch to &next file/tab"));
                 #endif
        MyAppend(viewmenu, A_PREVFILE, _("Switch to &previous file/tab\tCTRL+SHIFT+TAB"));
        MyAppend(viewmenu, A_FULLSCREEN,
                 #ifdef __WXMAC__
                 _("Toggle &Fullscreen View\tCTRL+F11"));
                 #else
                 _("Toggle &Fullscreen View\tF11"));
                 #endif
        MyAppend(viewmenu, A_SCALED,
                 #ifdef __WXMAC__
                 _("Toggle &Scaled Presentation View\tCTRL+F12"));
                 #else
                 _("Toggle &Scaled Presentation View\tF12"));
                 #endif
        viewmenu->AppendSubMenu(scrollmenu, _("Scroll Sheet..."));
        viewmenu->AppendSubMenu(filtermenu, _("Filter..."));
        MyAppend(viewmenu, A_SHOWSTATS, _("Show statistics\tCTRL+d"));

        wxMenu *roundmenu = new wxMenu();
        roundmenu->AppendRadioItem(A_ROUND0, _("Radius &0"));
        roundmenu->AppendRadioItem(A_ROUND1, _("Radius &1"));
        roundmenu->AppendRadioItem(A_ROUND2, _("Radius &2"));
        roundmenu->AppendRadioItem(A_ROUND3, _("Radius &3"));
        roundmenu->AppendRadioItem(A_ROUND4, _("Radius &4"));
        roundmenu->AppendRadioItem(A_ROUND5, _("Radius &5"));
        roundmenu->AppendRadioItem(A_ROUND6, _("Radius &6"));
        roundmenu->Check(sys->roundness + A_ROUND0, true);

        wxMenu *optmenu = new wxMenu();
        MyAppend(optmenu, wxID_SELECT_FONT, _("Pick Default Font..."));
        MyAppend(optmenu, A_CUSTKEY, _("Change a key binding..."));
        MyAppend(optmenu, A_CUSTCOL, _("Pick Custom &Color..."));
        MyAppend(optmenu, A_COLCELL, _("&Set Custom Color From Cell BG"));
        MyAppend(optmenu, A_DEFBGCOL, _("Pick Document Background..."));
        #ifdef SIMPLERENDER
            MyAppend(optmenu, A_DEFCURCOL, _("Pick Cu&rsor Color..."));
        #else
            optmenu->AppendCheckItem(A_HOVERSHADOW, _("Hover shadow"));
            optmenu->Check(A_HOVERSHADOW, sys->hovershadow);
        #endif
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_SHOWTBAR, _("Show Toolbar"));
        optmenu->Check(A_SHOWTBAR, sys->showtoolbar);
        optmenu->AppendCheckItem(A_SHOWSBAR, _("Show Statusbar"));
        optmenu->Check(A_SHOWSBAR, sys->showstatusbar);
        optmenu->AppendCheckItem(A_LEFTTABS, _("File Tabs on the bottom"));
        optmenu->Check(A_LEFTTABS, lefttabs);
        optmenu->AppendCheckItem(A_TOTRAY, _("Minimize to tray"));
        optmenu->Check(A_TOTRAY, sys->totray);
        optmenu->AppendCheckItem(A_MINCLOSE, _("Minimize on close"));
        optmenu->Check(A_MINCLOSE, sys->minclose);
        optmenu->AppendCheckItem(A_SINGLETRAY, _("Single click maximize from tray"));
        optmenu->Check(A_SINGLETRAY, sys->singletray);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_ZOOMSCR, _("Swap mousewheel scrolling and zooming"));
        optmenu->Check(A_ZOOMSCR, sys->zoomscroll);
        optmenu->AppendCheckItem(A_THINSELC, _("Navigate in between cells with cursor keys"));
        optmenu->Check(A_THINSELC, sys->thinselc);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_MAKEBAKS, _("Create .bak files"));
        optmenu->Check(A_MAKEBAKS, sys->makebaks);
        optmenu->AppendCheckItem(A_AUTOSAVE, _("Autosave to .tmp"));
        optmenu->Check(A_AUTOSAVE, sys->autosave);
        optmenu->AppendCheckItem(A_FSWATCH, _("Auto reload documents"),
                                 _("Reloads when another computer has changed a file (if you have made changes, asks)"));
        optmenu->Check(A_FSWATCH, sys->fswatch);
        optmenu->AppendCheckItem(A_AUTOEXPORT, _("Automatically export a .html on every save"));
        optmenu->Check(A_AUTOEXPORT, sys->autohtmlexport);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_CENTERED, _("Render document centered"));
        optmenu->Check(A_CENTERED, sys->centered);
        optmenu->AppendCheckItem(A_FASTRENDER, _("Faster line rendering"));
        optmenu->Check(A_FASTRENDER, sys->fastrender);
        optmenu->AppendSubMenu(roundmenu, _("&Roundness of grid borders..."));

        wxMenu *scriptmenu = new wxMenu();
        auto scriptpath = GetDataPath("scripts/");
        wxString sf = wxFindFirstFile(scriptpath + "*.lobster");
        int sidx = 0;
        while (!sf.empty()) {
            auto fn = wxFileName::FileName(sf).GetFullName();
            auto ms = fn.BeforeFirst('.');
            if (sidx < 26) {
                ms += "\tCTRL+SHIFT+ALT+";
                ms += wxChar('A' + sidx);
            }
            MyAppend(scriptmenu, A_SCRIPT + sidx, ms);
            auto ss = fn.utf8_str();
            scripts_in_menu.push_back(string(ss.data(), ss.length()));
            sf = wxFindNextFile();
            sidx++;
        }

        wxMenu *markmenu = new wxMenu();
        MyAppend(markmenu, A_MARKDATA, _("&Data\tCTRL+ALT+d"));
        MyAppend(markmenu, A_MARKCODE, _("&Operation\tCTRL+ALT+o"));
        MyAppend(markmenu, A_MARKVARD, _("Variable &Assign\tCTRL+ALT+a"));
        MyAppend(markmenu, A_MARKVARU, _("Variable &Read\tCTRL+ALT+r"));
        MyAppend(markmenu, A_MARKVIEWH, _("&Horizontal View\tCTRL+ALT+."));
        MyAppend(markmenu, A_MARKVIEWV, _("&Vertical View\tCTRL+ALT+,"));

        wxMenu *langmenu = new wxMenu();
        MyAppend(langmenu, wxID_EXECUTE, _("&Run\tCTRL+ALT+F5"));
        langmenu->AppendSubMenu(markmenu, _("&Mark as"));
        MyAppend(langmenu, A_CLRVIEW, _("&Clear Views"));

        wxMenu *helpmenu = new wxMenu();
        MyAppend(helpmenu, wxID_ABOUT, _("&About..."));
        MyAppend(helpmenu, wxID_HELP, _("Load interactive &tutorial...\tF1"));
        MyAppend(helpmenu, A_HELP_OP_REF, _("Load operation reference...\tCTRL+ALT+F1"));
        MyAppend(helpmenu, A_HELP, _("View tutorial &web page..."));

        wxAcceleratorEntry entries[3];
        entries[0].Set(wxACCEL_SHIFT, WXK_DELETE, wxID_CUT);
        entries[1].Set(wxACCEL_SHIFT, WXK_INSERT, wxID_PASTE);
        entries[2].Set(wxACCEL_CTRL, WXK_INSERT, wxID_COPY);
        wxAcceleratorTable accel(3, entries);
        SetAcceleratorTable(accel);

        wxMenuBar *menubar = new wxMenuBar();
        menubar->Append(filemenu, _("&File"));
        menubar->Append(editmenu, _("&Edit"));
        menubar->Append(semenu, _("&Search"));
        menubar->Append(viewmenu, _("&View"));
        menubar->Append(optmenu, _("&Options"));
        menubar->Append(scriptmenu, _("Script"));
        menubar->Append(langmenu, _("&Program"));
        menubar->Append(helpmenu,
                        #ifdef __WXMAC__
                        wxApp::s_macHelpMenuTitleName  // so merges with osx provided help
                        #else
                        _("&Help")
                        #endif
                        );
        #ifdef __WXMAC__
        // these don't seem to work anymore in the newer wxWidgets, handled in the menu event
        // handler below instead
        wxApp::s_macAboutMenuItemId = wxID_ABOUT;
        wxApp::s_macExitMenuItemId = wxID_EXIT;
        wxApp::s_macPreferencesMenuItemId =
            wxID_SELECT_FONT;  // we have no prefs, so for now just select the font
        #endif
        SetMenuBar(menubar);

        ConstructToolBar();

        wxStatusBar *sb = CreateStatusBar(5);
        SetStatusBarPane(0);
        SetDPIAwareStatusWidths();
        sb->Show(sys->showstatusbar);

        nb = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxAUI_NB_TAB_MOVE | wxAUI_NB_TAB_SPLIT | wxAUI_NB_SCROLL_BUTTONS |
                                   wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_CLOSE_ON_ALL_TABS |
                                   (lefttabs ? wxAUI_NB_BOTTOM : wxAUI_NB_TOP));

        int display_id = wxDisplay::GetFromWindow(this);
        wxRect disprect = wxDisplay(display_id == wxNOT_FOUND ? 0 : display_id).GetClientArea();
        const int boundary = 64;
        const int defx = disprect.width - 2 * boundary;
        const int defy = disprect.height - 2 * boundary;
        int resx, resy, posx, posy;
        sys->cfg->Read("resx", &resx, defx);
        sys->cfg->Read("resy", &resy, defy);
        sys->cfg->Read("posx", &posx, boundary + disprect.x);
        sys->cfg->Read("posy", &posy, boundary + disprect.y);
        #ifndef __WXGTK__
        // On X11, disprect only refers to the primary screen. Thus, for a multi-head display,
        // the conditions below might be fulfilled (e.g. large window spanning multiple screens
        // or being on the secondary screen), so just ignore them.
        if (resx > disprect.width || resy > disprect.height || posx < disprect.x ||
            posy < disprect.y || posx + resx > disprect.width + disprect.x ||
            posy + resy > disprect.height + disprect.y) {
            // Screen res has been resized since we last ran, set sizes to default to avoid being
            // off-screen.
            resx = defx;
            resy = defy;
            posx = posy = boundary;
            posx += disprect.x;
            posy += disprect.y;
        }
        #endif
        SetSize(resx, resy);
        Move(posx, posy);

        bool ismax;
        sys->cfg->Read("maximized", &ismax, true);

        aui->AddPane(nb, wxCENTER);
        aui->Update();

        Show(!IsIconized());

        // needs to be after Show() to avoid scrollbars rendered in the wrong place?
        if (ismax && !IsIconized()) Maximize(true);

        SetFileAssoc(exename);

        wxSafeYield();
    }

    void AppOnEventLoopEnter() {
        // Have to do this here, if we do it in the Frame constructor above, it crashes on OS X.
        watcher.reset(new wxFileSystemWatcher());
        watcher->SetOwner(this);
        Connect(wxEVT_FSWATCHER, wxFileSystemWatcherEventHandler(MyFrame::OnFileSystemEvent));
    }

    ~MyFrame() {
        filehistory.Save(*sys->cfg);
        if (!IsIconized()) {
            sys->cfg->Write("maximized", IsMaximized());
            if (!IsMaximized()) {
                sys->cfg->Write("resx", GetSize().x);
                sys->cfg->Write("resy", GetSize().y);
                sys->cfg->Write("posx", GetPosition().x);
                sys->cfg->Write("posy", GetPosition().y);
            }
        }
        aui->ClearEventHashTable();
        aui->UnInit();
        DELETEP(editmenupopup);
    }

    TSCanvas *NewTab(Document *doc, bool append = false) {
        TSCanvas *sw = new TSCanvas(this, nb);
        sw->doc.reset(doc);
        doc->sw = sw;
        sw->SetScrollRate(1, 1);
        if (append)
            nb->AddPage(sw, _("<unnamed>"), true, wxNullBitmap);
        else
            nb->InsertPage(0, sw, _("<unnamed>"), true, wxNullBitmap);
        sw->SetDropTarget(new DropTarget(doc->dndobjc));
        sw->SetFocus();
        return sw;
    }

    TSCanvas *GetCurTab() {
        return nb && nb->GetSelection() >= 0 ? (TSCanvas *)nb->GetPage(nb->GetSelection())
                                             : nullptr;
    }
    TSCanvas *GetTabByFileName(const wxString &fn) {
        if (nb) loop(i, nb->GetPageCount()) {
                TSCanvas *p = (TSCanvas *)nb->GetPage(i);
                if (p->doc->filename == fn) {
                    nb->SetSelection(i);
                    return p;
                }
            }
        return nullptr;
    }

    void OnTabChange(wxAuiNotebookEvent &nbe) {
        TSCanvas *sw = (TSCanvas *)nb->GetPage(nbe.GetSelection());
        sw->Status();
        SetSearchTextBoxBackgroundColour(false);
        sys->TabChange(sw->doc.get());
    }

    void TabsReset() {
        if (nb) loop(i, nb->GetPageCount()) {
                TSCanvas *p = (TSCanvas *)nb->GetPage(i);
                p->doc->rootgrid->ResetChildren();
            }
    }

    void OnTabClose(wxAuiNotebookEvent &nbe) {
        TSCanvas *sw = (TSCanvas *)nb->GetPage(nbe.GetSelection());
        sys->RememberOpenFiles();
        if (nb->GetPageCount() <= 1) {
            nbe.Veto();
            Close();
        } else if (sw->doc->CloseDocument()) {
            nbe.Veto();
        }
    }

    void CycleTabs(int offset = 1) {
        auto numtabs = (int)nb->GetPageCount();
        offset = ((offset >= 0) ? 1 : numtabs - 1);  // normalize to non-negative wrt modulo
        nb->SetSelection((nb->GetSelection() + offset) % numtabs);
    }

    void SetPageTitle(const wxString &fn, wxString mods, int page = -1) {
        if (page < 0) page = nb->GetSelection();
        if (page < 0) return;
        if (page == nb->GetSelection()) SetTitle("TreeSheets - " + fn + mods);
        nb->SetPageText(page,
                        (fn.empty() ? wxString(_("<unnamed>")) : wxFileName(fn).GetName()) + mods);
    }

    void ConstructToolBar() {
        tb = CreateToolBar(wxBORDER_NONE | wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
        tb->SetOwnBackgroundColour(toolbgcol);

        #ifdef __WXMAC__
        #define SEPARATOR
        #else
        #define SEPARATOR tb->AddSeparator()
        #endif

        wxString iconpath = GetDataPath("images/material/toolbar/");

        auto AddTBIcon = [&](const char *name, int action, wxString iconpath, wxString lighticon,
                             wxString darkicon) {
            tb->AddTool(action, name,
                        wxBitmapBundle::FromSVGFile(iconpath + (darkmode ? darkicon : lighticon),
                                                    wxSize(24, 24)),
                        name, wxITEM_NORMAL);
        };

        AddTBIcon(_("New (CTRL+n)"), wxID_NEW, iconpath, "filenew.svg", "filenew_dark.svg");
        AddTBIcon(_("Open (CTRL+o)"), wxID_OPEN, iconpath, "fileopen.svg", "fileopen_dark.svg");
        AddTBIcon(_("Save (CTRL+s)"), wxID_SAVE, iconpath, "filesave.svg", "filesave_dark.svg");
        AddTBIcon(_("Save As"), wxID_SAVEAS, iconpath, "filesaveas.svg", "filesaveas_dark.svg");
        SEPARATOR;
        AddTBIcon(_("Undo (CTRL+z)"), wxID_UNDO, iconpath, "undo.svg", "undo_dark.svg");
        AddTBIcon(_("Copy (CTRL+c)"), wxID_COPY, iconpath, "editcopy.svg", "editcopy_dark.svg");
        AddTBIcon(_("Paste (CTRL+v)"), wxID_PASTE, iconpath, "editpaste.svg", "editpaste_dark.svg");
        SEPARATOR;
        AddTBIcon(_("Zoom In (CTRL+mousewheel)"), A_ZOOMIN, iconpath, "zoomin.svg",
                  "zoomin_dark.svg");
        AddTBIcon(_("Zoom Out (CTRL+mousewheel)"), A_ZOOMOUT, iconpath, "zoomout.svg",
                  "zoomout_dark.svg");
        SEPARATOR;
        AddTBIcon(_("New Grid (INS)"), A_NEWGRID, iconpath, "newgrid.svg", "newgrid_dark.svg");
        AddTBIcon(_("Add Image"), A_IMAGE, iconpath, "image.svg", "image_dark.svg");
        SEPARATOR;
        AddTBIcon(_("Run"), wxID_EXECUTE, iconpath, "run.svg", "run_dark.svg");
        tb->AddSeparator();
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _("Search ")));
        tb->AddControl(filter = new wxTextCtrl(tb, A_SEARCH, "", wxDefaultPosition,
                                               FromDIP(wxSize(80, 22)),
                                               wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddTBIcon(_("Clear search"), A_CLEARSEARCH, iconpath, "cancel.svg", "cancel_dark.svg");
        AddTBIcon(_("Go to Next Search Result"), A_SEARCHNEXT, iconpath, "search.svg",
                  "search_dark.svg");
        SEPARATOR;
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _("Replace ")));
        tb->AddControl(replaces = new wxTextCtrl(tb, A_REPLACE, "", wxDefaultPosition,
                                                 FromDIP(wxSize(80, 22)),
                                                 wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddTBIcon(_("Clear replace"), A_CLEARREPLACE, iconpath, "cancel.svg", "cancel_dark.svg");
        AddTBIcon(_("Replace in selection"), A_REPLACEONCE, iconpath, "replace.svg",
                  "replace_dark.svg");
        AddTBIcon(_("Replace All"), A_REPLACEALL, iconpath, "replaceall.svg",
                  "replaceall_dark.svg");
        tb->AddSeparator();
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _("Cell ")));
        celldd = new ColorDropdown(tb, A_CELLCOLOR, 1);
        tb->AddControl(celldd);
        SEPARATOR;
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _("Text ")));
        textdd = new ColorDropdown(tb, A_TEXTCOLOR, 2);
        tb->AddControl(textdd);
        SEPARATOR;
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _("Border ")));
        borddd = new ColorDropdown(tb, A_BORDCOLOR, 7);
        tb->AddControl(borddd);
        tb->AddSeparator();
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _("Image ")));
        idd = new ImageDropdown(tb, imagepath);
        tb->AddControl(idd);
        tb->Realize();
        tb->Show(sys->showtoolbar);
    }

    void TBMenu(wxToolBar *tb, wxMenu *menu, const char *name, int id = 0) {
        tb->AddTool(id, name, wxNullBitmap, wxEmptyString, wxITEM_DROPDOWN);
        tb->SetDropdownMenu(id, menu);
    }

    void OnMenu(wxCommandEvent &ce) {
        wxTextCtrl *tc;
        TSCanvas *sw = GetCurTab();
        if (((tc = filter) && filter == wxWindow::FindFocus()) ||
            ((tc = replaces) && replaces == wxWindow::FindFocus())) {
            long from, to;
            tc->GetSelection(&from, &to);
            switch (ce.GetId()) {
                #ifdef __WXMSW__
                // FIXME: have to emulate this behavior on Windows because menu always captures these events (??)
                case A_MLEFT:
                case A_LEFT:
                    if (from != to)
                        tc->SetInsertionPoint(from);
                    else if (from)
                        tc->SetInsertionPoint(from - 1);
                    return;
                case A_MRIGHT:
                case A_RIGHT:
                    if (from != to)
                        tc->SetInsertionPoint(to);
                    else if (to < tc->GetLineLength(0))
                        tc->SetInsertionPoint(to + 1);
                    return;

                case A_SHOME: tc->SetSelection(0, to); return;
                case A_SEND: tc->SetSelection(from, 1000); return;

                case A_SCLEFT:
                case A_SLEFT:
                    if (from) tc->SetSelection(from - 1, to);
                    return;
                case A_SCRIGHT:
                case A_SRIGHT:
                    if (to < tc->GetLineLength(0)) tc->SetSelection(from, to + 1);
                    return;

                case A_BACKSPACE: tc->Remove(from - (from == to), to); return;
                case A_DELETE: tc->Remove(from, to + (from == to)); return;
                case A_HOME: tc->SetSelection(0, 0); return;
                case A_END: tc->SetSelection(1000, 1000); return;
                case wxID_SELECTALL: tc->SetSelection(0, 1000); return;
                case A_ENTERCELL: {
                    wxClientDC dc(sw);
                    if (tc == filter) {
                        // OnSearchEnter equivalent implementation for MSW
                        // as EVT_TEXT_ENTER event is not generated.
                        if (sys->searchstring.Len() == 0) {
                            sw->SetFocus();
                        } else {
                            sw->doc->Action(dc, A_SEARCHNEXT);
                        }
                    } else if (tc == replaces) {
                        // OnReplaceEnter equivalent implementation for MSW
                        // as EVT_TEXT_ENTER event is not generated.
                        sw->doc->Action(dc, A_REPLACEONCEJ);
                    }
                    return;
                }
                #endif
                case A_CANCELEDIT: tc->Clear(); sw->SetFocus(); return;
            }
        }
        wxClientDC dc(sw);
        sw->DoPrepareDC(dc);
        sw->doc->ShiftToCenter(dc);
        auto Check = [&](const char *cfg) {
            sys->cfg->Write(cfg, ce.IsChecked());
            sw->Status(_("change will take effect next run of TreeSheets"));
        };
        switch (ce.GetId()) {
            case A_NOP: break;

            case A_ALEFT: sw->CursorScroll(-g_scrollratecursor, 0); break;
            case A_ARIGHT: sw->CursorScroll(g_scrollratecursor, 0); break;
            case A_AUP: sw->CursorScroll(0, -g_scrollratecursor); break;
            case A_ADOWN: sw->CursorScroll(0, g_scrollratecursor); break;

            case A_SHOWSBAR:
                if (!IsFullScreen()) {
                    sys->cfg->Write("showstatusbar", sys->showstatusbar = ce.IsChecked());
                    wxStatusBar *wsb = this->GetStatusBar();
                    wsb->Show(sys->showstatusbar);
                    this->SendSizeEvent();
                    this->Refresh();
                    wsb->Refresh();
                }
                break;
            case A_SHOWTBAR:
                if (!IsFullScreen()) {
                    sys->cfg->Write("showtoolbar", sys->showtoolbar = ce.IsChecked());
                    wxToolBar *wtb = this->GetToolBar();
                    wtb->Show(sys->showtoolbar);
                    this->SendSizeEvent();
                    this->Refresh();
                    wtb->Refresh();
                }
                break;
            case A_CUSTCOL: {
                uint c = PickColor(sys->frame, sys->customcolor);
                if (c != (uint)-1) sys->cfg->Write("customcolor", sys->customcolor = c);
                break;
            }
            case A_LEFTTABS: Check("lefttabs"); break;
            case A_SINGLETRAY: Check("singletray"); break;
            case A_MAKEBAKS: sys->cfg->Write("makebaks", sys->makebaks = ce.IsChecked()); break;
            case A_TOTRAY: sys->cfg->Write("totray", sys->totray = ce.IsChecked()); break;
            case A_MINCLOSE: sys->cfg->Write("minclose", sys->minclose = ce.IsChecked()); break;
            case A_ZOOMSCR: sys->cfg->Write("zoomscroll", sys->zoomscroll = ce.IsChecked()); break;
            case A_THINSELC: sys->cfg->Write("thinselc", sys->thinselc = ce.IsChecked()); break;
            case A_AUTOSAVE: sys->cfg->Write("autosave", sys->autosave = ce.IsChecked()); break;
            #ifndef SIMPLERENDER
                case A_HOVERSHADOW:
                    sys->cfg->Write("hovershadow", sys->hovershadow = ce.IsChecked());
                    break;
            #endif
            case A_CENTERED:
                sys->cfg->Write("centered", sys->centered = ce.IsChecked());
                Refresh();
                break;
            case A_FSWATCH:
                Check("fswatch");
                sys->fswatch = ce.IsChecked();
                break;
            case A_AUTOEXPORT:
                sys->cfg->Write("autohtmlexport", sys->autohtmlexport = ce.IsChecked());
                break;
            case A_FASTRENDER:
                sys->cfg->Write("fastrender", sys->fastrender = ce.IsChecked());
                Refresh();
                break;
            case A_FULLSCREEN:
                ShowFullScreen(!IsFullScreen());
                if (IsFullScreen()) sw->Status(_("Press F11 to exit fullscreen mode."));
                break;
            case wxID_FIND:
                if (filter) {
                    filter->SetFocus();
                    filter->SetSelection(0, 1000);
                } else {
                    sw->Status(_("Please enable (Options -> Show Toolbar) to use search."));
                }
                break;
            case wxID_REPLACE:
                if (replaces) {
                    replaces->SetFocus();
                    replaces->SetSelection(0, 1000);
                } else {
                    sw->Status(_("Please enable (Options -> Show Toolbar) to use replace."));
                }
                break;
            #ifdef __WXMAC__
            case wxID_OSX_HIDE: Iconize(true); break;
            case wxID_OSX_HIDEOTHERS: sw->Status("NOT IMPLEMENTED"); break;
            case wxID_OSX_SHOWALL: Iconize(false); break;
            case wxID_ABOUT: sw->doc->Action(dc, wxID_ABOUT); break;
            case wxID_PREFERENCES: sw->doc->Action(dc, wxID_SELECT_FONT); break;
            #endif
            case wxID_EXIT:
                fromclosebox = false;
                this->Close();
                break;
            case wxID_CLOSE: sw->doc->Action(dc, ce.GetId()); break;  // sw dangling pointer on return
            default:
                if (ce.GetId() >= wxID_FILE1 && ce.GetId() <= wxID_FILE9) {
                    wxString f(filehistory.GetHistoryFile(ce.GetId() - wxID_FILE1));
                    sw->Status(sys->Open(f));
                } else if (ce.GetId() >= A_TAGSET && ce.GetId() < A_SCRIPT) {
                    sw->Status(sw->doc->TagSet(ce.GetId() - A_TAGSET));
                } else if (ce.GetId() >= A_SCRIPT && ce.GetId() < A_MAXACTION) {
                    auto msg = tssi.ScriptRun(scripts_in_menu[ce.GetId() - A_SCRIPT].c_str(), dc);
                    msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
                    sw->Status(wxString(msg));
                } else {
                    sw->Status(sw->doc->Action(dc, ce.GetId()));
                    break;
                }
        }
    }

    void SetSearchTextBoxBackgroundColour(bool found) {
        if (!filter) return;
        filter->SetForegroundColour((found && darkmode) ? wxColour("AQUAMARINE") : wxNullColour);
        filter->SetBackgroundColour((found && !darkmode) ? wxColour("AQUAMARINE") : wxNullColour);
        filter->Refresh();
    }

    void OnSearch(wxCommandEvent &ce) {
        wxString searchstring = ce.GetString();
        sys->darkennonmatchingcells = searchstring.Len() != 0;
        sys->searchstring = (sys->casesensitivesearch) ? searchstring : searchstring.Lower();
        SetSearchTextBoxBackgroundColour(false);
        Document *doc = GetCurTab()->doc.get();
        TSCanvas *sw = GetCurTab();
        wxClientDC dc(sw);
        doc->SearchNext(dc, false, false, false);
        if (doc->searchfilter) {
            doc->SetSearchFilter(sys->searchstring.Len() != 0);
        } else
            doc->Refresh();
        GetCurTab()->Status();
    }

    void OnSearchReplaceEnter(wxCommandEvent &ce) {
        TSCanvas *sw = GetCurTab();
        if (ce.GetId() == A_SEARCH && ce.GetString().Len() == 0) {
            sw->SetFocus();
        } else {
            wxClientDC dc(sw);
            sw->doc->Action(dc, ce.GetId() == A_SEARCH ? A_SEARCHNEXT : A_REPLACEONCEJ);
        }
    }

    void ReFocus() {
        if (GetCurTab()) GetCurTab()->SetFocus();
    }

    void OnChangeColor(wxCommandEvent &ce) {
        GetCurTab()->doc->ColorChange(ce.GetId(), ce.GetInt());
        ReFocus();
    }

    void OnDDImage(wxCommandEvent &ce) {
        GetCurTab()->doc->ImageChange(idd->as[ce.GetInt()], dd_icon_res_scale);
        ReFocus();
    }

    void OnSizing(wxSizeEvent &se) { se.Skip(); }
    void OnMaximize(wxMaximizeEvent &me) {
        ReFocus();
        me.Skip();
    }
    void OnActivate(wxActivateEvent &ae) {
        // This causes warnings in the debug log, but without it keyboard entry upon window select
        // doesn't work.
        ReFocus();
    }

    void RenderFolderIcon() {
        wxImage foldiconi;
        foldiconi.LoadFile(GetDataPath("images/nuvola/fold.png"));
        foldicon = wxBitmap(foldiconi);
        ScaleBitmap(foldicon, FromDIP(1.0) / 3.0, foldicon);
    }

    void SetDPIAwareStatusWidths() {
        int swidths[] = {-1, FromDIP(300), FromDIP(120), FromDIP(100), FromDIP(150)};
        SetStatusWidths(5, swidths);
    }

    void OnDPIChanged(wxDPIChangedEvent &dce) {
        // block all other events until we finished preparing
        wxEventBlocker blocker(this);
        wxBusyCursor wait;
        {
            ThreadPool pool(std::thread::hardware_concurrency());
            for (const auto &image : sys->imagelist) {
                pool.enqueue(
                    [](auto *img) {
                        img->bm_display = wxNullBitmap;
                        img->Display();
                    },
                    image.get());
            }
        }  // wait until all tasks are finished
        RenderFolderIcon();
        if (nb) {
            loop(i, nb->GetPageCount()) {
                TSCanvas *p = (TSCanvas *)nb->GetPage(i);
                p->doc->curdrawroot->ResetChildren();
                p->doc->curdrawroot->ResetLayout();
                p->doc->scrolltoselection = true;
            }
            nb->SetTabCtrlHeight(-1);
        }
        idd->FillBitmapVector(imagepath);
        if (GetStatusBar()) SetDPIAwareStatusWidths();
    }

    void OnSysColourChanged(wxSysColourChangedEvent &se) {
        if (bool newmode = wxSystemSettings::GetAppearance().IsDark(); newmode != darkmode)
            OnDarkModeChanged(newmode);
        se.Skip();
    }

    void OnDarkModeChanged(bool newmode) {
        darkmode = newmode;
        wxString s_filter =  filter->GetValue();
        wxString s_replaces = replaces->GetValue();
        delete (tb);
        ConstructToolBar();
        filter->SetValue(s_filter);
        replaces->SetValue(s_replaces);
    }

    void OnIconize(wxIconizeEvent &me) {
        if (me.IsIconized()) {
            #ifndef __WXMAC__
            if (sys->totray) {
                tbi.SetIcon(icon, "TreeSheets");
                Show(false);
                Iconize();
            }
            #endif
        } else {
            #ifdef __WXGTK__
            if (sys->totray) {
                Show(true);
            }
            #endif
            if (GetCurTab()) GetCurTab()->SetFocus();
        }
    }

    void DeIconize() {
        if (!IsIconized()) {
            RequestUserAttention();
            return;
        }
        Show(true);
        Iconize(false);
        tbi.RemoveIcon();
    }

    void OnTBIDBLClick(wxTaskBarIconEvent &e) { DeIconize(); }

    void OnClosing(wxCloseEvent &ce) {
        bool fcb = fromclosebox;
        fromclosebox = true;
        if (fcb && sys->minclose) {
            ce.Veto();
            Iconize();
            return;
        }
        sys->RememberOpenFiles();
        int n = (int)nb->GetPageCount();
        if (ce.CanVeto()) {
            // ask to save/discard all files before closing any
            for (int i = 0; i < n; i++) {
                TSCanvas *p = (TSCanvas *)nb->GetPage(i);
                if (p->doc->modified) {
                    nb->SetSelection(i);
                    if (p->doc->CheckForChanges()) {
                        ce.Veto();
                        return;
                    }
                }
            }
            // all files have been saved/discarded
            while (nb->GetPageCount()) {
                GetCurTab()->doc->RemoveTmpFile();
                nb->DeletePage(nb->GetSelection());
            }
        }
        bt.Stop();
        sys->every_second_timer.Stop();
        Destroy();
    }

    #ifdef WIN32
    void SetRegKey(const char *key, wxString val) {
        wxRegKey rk(key);
        rk.Create();
        rk.SetValue(L"", val);
    }
    #endif

    void SetFileAssoc(wxString &exename) {
        #ifdef WIN32
        SetRegKey("HKEY_CURRENT_USER\\Software\\Classes\\.cts", L"TreeSheets");
        SetRegKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets", L"TreeSheets file");
        SetRegKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\Shell\\Open\\Command",
                  wxString("\"") + exename + "\" \"%1\"");
        SetRegKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\DefaultIcon",
                  wxString("\"") + exename + "\",0");
        #else
        // TODO: do something similar for mac/kde/gnome?
        #endif
    }

    void OnFileSystemEvent(wxFileSystemWatcherEvent &event) {
        // 0xF == create/delete/rename/modify
        if ((event.GetChangeType() & 0xF) == 0 || watcherwaitingforuser || !nb) return;
        const wxString &modfile = event.GetPath().GetFullPath();
        loop(i, nb->GetPageCount()) {
            Document *doc = ((TSCanvas *)nb->GetPage(i))->doc.get();
            if (modfile == doc->filename) {
                wxDateTime modtime = wxFileName(modfile).GetModificationTime();
                // Compare with last modified to trigger multiple times.
                if (!modtime.IsValid() || !doc->lastmodificationtime.IsValid() ||
                    modtime == doc->lastmodificationtime) {
                    return;
                }
                if (doc->modified) {
                    // TODO: this dialog is problematic since it may be on an unattended
                    // computer and more of these events may fire. since the occurrence of this
                    // situation is rare, it may be better to just take the most
                    // recently changed version (which is the one that has just been modified
                    // on disk) this potentially throws away local changes, but this can only
                    // happen if the user left changes unsaved, then decided to go edit an older
                    // version on another computer.
                    // for now, we leave this code active, and guard it with
                    // watcherwaitingforuser
                    wxString msg = wxString::Format(
                        _("%s\nhas been modified on disk by another program / computer:\nWould you like to discard your changes and re-load from disk?"),
                        doc->filename);
                    watcherwaitingforuser = true;
                    int res = wxMessageBox(msg, _("File modification conflict!"),
                                           wxYES_NO | wxICON_QUESTION, this);
                    watcherwaitingforuser = false;
                    if (res != wxYES) return;
                }
                auto msg = sys->LoadDB(doc->filename, false, true);
                assert(msg);
                if (*msg) {
                    GetCurTab()->Status(msg);
                } else {
                    loop(j, nb->GetPageCount()) if (((TSCanvas *)nb->GetPage(j))->doc.get() == doc)
                        nb->DeletePage(j);
                    ::wxRemoveFile(sys->TmpName(modfile));
                    GetCurTab()->Status(
                        _("File has been re-loaded because of modifications of another program / computer"));
                }
                return;
            }
        }
    }

    DECLARE_EVENT_TABLE()
};
