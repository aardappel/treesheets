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
    double csf {(double)FromDIP(1.0)};  // TODO: functions using this attribute should be modified
                                        // to handle device-independent pixels
    std::vector<std::string> scripts_in_menu;
    wxToolBar *tb {nullptr};
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

        return wxString(relativePath.c_str());
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

        return wxString(relativePath.c_str());
    }

    std::map<wxString, wxString> menustrings;

    void MyAppend(wxMenu *menu, int tag, const wxString &contents, const wchar_t *help = L"") {
        wxString item = contents;
        wxString key = L"";
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
        : wxFrame((wxFrame *)nullptr, wxID_ANY, L"TreeSheets", wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_FRAME_STYLE),
          app(_app) {
        sys->frame = this;
        exepath_ = wxFileName(exename).GetPath();
        #ifdef __WXMAC__
        int cut = exepath_.Find("/MacOS");
        if (cut > 0) { exepath_ = exepath_.SubString(0, cut) + "/Resources"; }
        #endif

        class MyLog : public wxLog {
            void DoLogString(const wxChar *msg, time_t timestamp) { DoLogText(*msg); }
            void DoLogText(const wxString &msg) {
                #ifdef WIN32
                OutputDebugString(msg.c_str());
                OutputDebugString(L"\n");
                #else
                fputws(msg.c_str(), stderr);
                fputws(L"\n", stderr);
                #endif
            }
        };

        wxLog::SetActiveTarget(new MyLog());

        wxLogMessage(L"%s", wxVERSION_STRING);

        wxLogMessage(L"locale: %s", std::setlocale(LC_CTYPE, nullptr));

        app->AddTranslation(GetDataPath("translations"));

        wxInitAllImageHandlers();

        wxIconBundle icons;
        wxIcon iconbig;
        #ifdef WIN32
            int iconsmall = ::GetSystemMetrics(SM_CXSMICON);
            int iconlarge = ::GetSystemMetrics(SM_CXICON);
        #endif
        icon.LoadFile(GetDataPath(L"images/icon16.png"), wxBITMAP_TYPE_PNG
            #ifdef WIN32
                , iconsmall, iconsmall
            #endif
        );
        iconbig.LoadFile(GetDataPath(L"images/icon32.png"), wxBITMAP_TYPE_PNG
            #ifdef WIN32
                , iconlarge, iconlarge
            #endif
        );
        if (!icon.IsOk() || !iconbig.IsOk()) {
            wxMessageBox(_(L"Error loading core data file (TreeSheets not installed correctly?)"),
                         _(L"Initialization Error"), wxOK, this);
            exit(1);
        }
        icons.AddIcon(icon);
        icons.AddIcon(iconbig);
        SetIcons(icons);

        RenderFolderIcon();
        line_nw.LoadFile(GetDataPath(L"images/render/line_nw.png"), wxBITMAP_TYPE_PNG);
        line_sw.LoadFile(GetDataPath(L"images/render/line_sw.png"), wxBITMAP_TYPE_PNG);

        imagepath = GetDataPath("images/nuvola/dropdown/");

        if (sys->singletray)
            tbi.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_UP,
                        wxTaskBarIconEventHandler(MyFrame::OnTBIDBLClick), nullptr, this);
        else
            tbi.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_DCLICK,
                        wxTaskBarIconEventHandler(MyFrame::OnTBIDBLClick), nullptr, this);

        bool mergetbar = false;

        bool showtbar, showsbar, lefttabs;

        sys->cfg->Read(L"showtbar", &showtbar, true);
        sys->cfg->Read(L"showsbar", &showsbar, true);
        sys->cfg->Read(L"lefttabs", &lefttabs, true);

        filehistory.Load(*sys->cfg);

        wxMenu *expmenu = new wxMenu();
        MyAppend(expmenu, A_EXPXML, _(L"&XML..."),
                 _(L"Export the current view as XML (which can also be reimported without losing structure)"));
        MyAppend(expmenu, A_EXPHTMLT, _(L"&HTML (Tables+Styling)..."),
                 _(L"Export the current view as HTML using nested tables, that will look somewhat like the TreeSheet"));
        MyAppend(expmenu, A_EXPHTMLB, _(L"HTML (&Bullet points)..."),
                 _(L"Export the current view as HTML as nested bullet points."));
        MyAppend(expmenu, A_EXPHTMLO, _(L"HTML (&Outline)..."),
                 _(L"Export the current view as HTML as nested headers, suitable for importing into Word's outline mode"));
        MyAppend(
            expmenu, A_EXPTEXT, _(L"Indented &Text..."),
            _(L"Export the current view as tree structured text, using spaces for each indentation level. Suitable for importing into mindmanagers and general text programs"));
        MyAppend(
            expmenu, A_EXPCSV, _(L"&Comma delimited text (CSV)..."),
            _(L"Export the current view as CSV. Good for spreadsheets and databases. Only works on grids with no sub-grids (use the Flatten operation first if need be)"));
        MyAppend(expmenu, A_EXPIMAGE, _(L"&Image..."),
                 _(L"Export the current view as an image. Useful for faithful renderings of the TreeSheet, and programs that don't accept any of the above options"));

        wxMenu *impmenu = new wxMenu();
        MyAppend(impmenu, A_IMPXML, _(L"XML..."));
        MyAppend(impmenu, A_IMPXMLA, _(L"XML (attributes too, for OPML etc)..."));
        MyAppend(impmenu, A_IMPTXTI, _(L"Indented text..."));
        MyAppend(impmenu, A_IMPTXTC, _(L"Comma delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTS, _(L"Semi-Colon delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTT, _(L"Tab delimited text..."));

        wxMenu *recentmenu = new wxMenu();
        filehistory.UseMenu(recentmenu);
        filehistory.AddFilesToMenu();

        wxMenu *filemenu = new wxMenu();
        MyAppend(filemenu, A_NEW, _(L"&New\tCTRL+n"));
        MyAppend(filemenu, A_OPEN, _(L"&Open...\tCTRL+o"));
        MyAppend(filemenu, A_CLOSE, _(L"&Close\tCTRL+w"));
        filemenu->AppendSubMenu(recentmenu, _(L"&Recent files"));
        MyAppend(filemenu, A_SAVE, _(L"&Save\tCTRL+s"));
        MyAppend(filemenu, A_SAVEAS, _(L"Save &As..."));
        MyAppend(filemenu, A_SAVEALL, _(L"Save All"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, A_PAGESETUP, _(L"Page Setup..."));
        MyAppend(filemenu, A_PRINTSCALE, _(L"Set Print Scale..."));
        MyAppend(filemenu, A_PREVIEW, _(L"Print preview..."));
        MyAppend(filemenu, A_PRINT, _(L"&Print...\tCTRL+p"));
        filemenu->AppendSeparator();
        filemenu->AppendSubMenu(expmenu, _(L"Export &view as"));
        filemenu->AppendSubMenu(impmenu, _(L"Import file from"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, wxID_EXIT, _(L"&Exit\tCTRL+q"));

        wxMenu *editmenu;
        loop(twoeditmenus, 2) {
            wxMenu *sizemenu = new wxMenu();
            MyAppend(sizemenu, A_INCSIZE, _(L"&Increase text size (SHIFT+mousewheel)\tSHIFT+PGUP"));
            MyAppend(sizemenu, A_DECSIZE, _(L"&Decrease text size (SHIFT+mousewheel)\tSHIFT+PGDN"));
            MyAppend(sizemenu, A_RESETSIZE, _(L"&Reset text sizes\tCTRL+SHIFT+s"));
            MyAppend(sizemenu, A_MINISIZE, _(L"&Shrink text of all sub-grids\tCTRL+SHIFT+m"));
            sizemenu->AppendSeparator();
            MyAppend(sizemenu, A_INCWIDTH, _(L"Increase column width (ALT+mousewheel)\tALT+PGUP"));
            MyAppend(sizemenu, A_DECWIDTH, _(L"Decrease column width (ALT+mousewheel)\tALT+PGDN"));
            MyAppend(sizemenu, A_INCWIDTHNH,
                     _(L"Increase column width (no sub grids)\tCTRL+ALT+PGUP"));
            MyAppend(sizemenu, A_DECWIDTHNH,
                     _(L"Decrease column width (no sub grids)\tCTRL+ALT+PGDN"));
            MyAppend(sizemenu, A_RESETWIDTH, _(L"Reset column widths\tCTRL+SHIFT+w"));

            wxMenu *bordmenu = new wxMenu();
            MyAppend(bordmenu, A_BORD0, _(L"Border &0\tCTRL+SHIFT+9"));
            MyAppend(bordmenu, A_BORD1, _(L"Border &1\tCTRL+SHIFT+1"));
            MyAppend(bordmenu, A_BORD2, _(L"Border &2\tCTRL+SHIFT+2"));
            MyAppend(bordmenu, A_BORD3, _(L"Border &3\tCTRL+SHIFT+3"));
            MyAppend(bordmenu, A_BORD4, _(L"Border &4\tCTRL+SHIFT+4"));
            MyAppend(bordmenu, A_BORD5, _(L"Border &5\tCTRL+SHIFT+5"));

            wxMenu *selmenu = new wxMenu();
            MyAppend(selmenu, A_NEXT, _(L"Move to next cell\tTAB"));
            MyAppend(selmenu, A_PREV, _(L"Move to previous cell\tSHIFT+TAB"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SELALL, _(L"Select &all in current grid/cell\tCTRL+a"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LEFT, _(L"Move Selection Left\tLEFT"));
            MyAppend(selmenu, A_RIGHT, _(L"Move Selection Right\tRIGHT"));
            MyAppend(selmenu, A_UP, _(L"Move Selection Up\tUP"));
            MyAppend(selmenu, A_DOWN, _(L"Move Selection Down\tDOWN"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_MLEFT, _(L"Move Cells Left\tCTRL+LEFT"));
            MyAppend(selmenu, A_MRIGHT, _(L"Move Cells Right\tCTRL+RIGHT"));
            MyAppend(selmenu, A_MUP, _(L"Move Cells Up\tCTRL+UP"));
            MyAppend(selmenu, A_MDOWN, _(L"Move Cells Down\tCTRL+DOWN"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SLEFT, _(L"Extend Selection Left\tSHIFT+LEFT"));
            MyAppend(selmenu, A_SRIGHT, _(L"Extend Selection Right\tSHIFT+RIGHT"));
            MyAppend(selmenu, A_SUP, _(L"Extend Selection Up\tSHIFT+UP"));
            MyAppend(selmenu, A_SDOWN, _(L"Extend Selection Down\tSHIFT+DOWN"));
            MyAppend(selmenu, A_SCOLS, _(L"Extend Selection Full Columns"));
            MyAppend(selmenu, A_SROWS, _(L"Extend Selection Full Rows"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_CANCELEDIT, _(L"Select &Parent\tESC"));
            MyAppend(selmenu, A_ENTERGRID, _(L"Select First &Child\tSHIFT+ENTER"));
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LINK, _(L"Go To &Matching Cell (Text)\tF6"));
            MyAppend(selmenu, A_LINKREV, _(L"Go To Matching Cell (Text, Reverse)\tSHIFT+F6"));
            MyAppend(selmenu, A_LINKIMG, _(L"Go To Matching Cell (Image)\tF7"));
            MyAppend(selmenu, A_LINKIMGREV, _(L"Go To Matching Cell (Image, Reverse)\tSHIFT+F7"));

            wxMenu *temenu = new wxMenu();
            MyAppend(temenu, A_LEFT, _(L"Cursor Left\tLEFT"));
            MyAppend(temenu, A_RIGHT, _(L"Cursor Right\tRIGHT"));
            MyAppend(temenu, A_MLEFT, _(L"Word Left\tCTRL+LEFT"));
            MyAppend(temenu, A_MRIGHT, _(L"Word Right\tCTRL+RIGHT"));
            temenu->AppendSeparator();
            MyAppend(temenu, A_SLEFT, _(L"Extend Selection Left\tSHIFT+LEFT"));
            MyAppend(temenu, A_SRIGHT, _(L"Extend Selection Right\tSHIFT+RIGHT"));
            MyAppend(temenu, A_SCLEFT, _(L"Extend Selection Word Left\tCTRL+SHIFT+LEFT"));
            MyAppend(temenu, A_SCRIGHT, _(L"Extend Selection Word Right\tCTRL+SHIFT+RIGHT"));
            MyAppend(temenu, A_SHOME, _(L"Extend Selection to Start\tSHIFT+HOME"));
            MyAppend(temenu, A_SEND, _(L"Extend Selection to End\tSHIFT+END"));
            temenu->AppendSeparator();
            MyAppend(temenu, A_HOME, _(L"Start of line of text\tHOME"));
            MyAppend(temenu, A_END, _(L"End of line of text\tEND"));
            MyAppend(temenu, A_CHOME, _(L"Start of text\tCTRL+HOME"));
            MyAppend(temenu, A_CEND, _(L"End of text\tCTRL+END"));
            temenu->AppendSeparator();
            MyAppend(temenu, A_ENTERCELL, _(L"Enter/exit text edit mode\tENTER"));
            MyAppend(temenu, A_ENTERCELL_JUMPTOEND, _(L"Enter/exit text edit mode\tF2"));
            MyAppend(temenu, A_PROGRESSCELL, _(L"Enter/exit text edit to the right\tALT+ENTER"));
            MyAppend(temenu, A_CANCELEDIT, _(L"Cancel text edits\tESC"));

            wxMenu *stmenu = new wxMenu();
            MyAppend(stmenu, A_BOLD, _(L"Toggle cell &BOLD\tCTRL+b"));
            MyAppend(stmenu, A_ITALIC, _(L"Toggle cell &ITALIC\tCTRL+i"));
            MyAppend(stmenu, A_TT, _(L"Toggle cell &typewriter\tCTRL+ALT+t"));
            MyAppend(stmenu, A_UNDERL, _(L"Toggle cell &underlined\tCTRL+u"));
            MyAppend(stmenu, A_STRIKET, _(L"Toggle cell &strikethrough\tCTRL+t"));
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_RESETSTYLE, _(L"&Reset text styles\tCTRL+SHIFT+r"));
            MyAppend(stmenu, A_RESETCOLOR, _(L"Reset &colors\tCTRL+SHIFT+c"));
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_LASTCELLCOLOR, _(L"Apply last cell color\tSHIFT+ALT+c"));
            MyAppend(stmenu, A_LASTTEXTCOLOR, _(L"Apply last text color\tSHIFT+ALT+t"));
            MyAppend(stmenu, A_LASTBORDCOLOR, _(L"Apply last border color\tSHIFT+ALT+b"));
            MyAppend(stmenu, A_OPENCELLCOLOR, _(L"Open cell colors\tSHIFT+ALT+F9"));
            MyAppend(stmenu, A_OPENTEXTCOLOR, _(L"Open text colors\tSHIFT+ALT+F10"));
            MyAppend(stmenu, A_OPENBORDCOLOR, _(L"Open border colors\tSHIFT+ALT+F11"));

            wxMenu *tagmenu = new wxMenu();
            MyAppend(tagmenu, A_TAGADD, _(L"&Add Cell Text as Tag"));
            MyAppend(tagmenu, A_TAGREMOVE, _(L"&Remove Cell Text from Tags"));
            MyAppend(tagmenu, A_NOP, _(L"&Set Cell Text to tag (use CTRL+RMB)"),
                     _(L"Hold CTRL while pressing right mouse button to quickly set a tag for the current cell using a popup menu"));

            wxMenu *orgmenu = new wxMenu();
            MyAppend(orgmenu, A_TRANSPOSE, _(L"&Transpose\tCTRL+SHIFT+t"),
                     _(L"changes the orientation of a grid"));
            MyAppend(orgmenu, A_SORT, _(L"Sort &Ascending"),
                     _(L"Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_SORTD, _(L"Sort &Descending"),
                     _(L"Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_HSWAP, _(L"Hierarchy &Swap\tF8"),
                     _(L"Swap all cells with this text at this level (or above) with the parent"));
            MyAppend(orgmenu, A_HIFY, _(L"&Hierarchify"),
                     _(L"Convert an NxN grid with repeating elements per column into an 1xN grid with hierarchy, useful to convert data from spreadsheets"));
            MyAppend(orgmenu, A_FLATTEN, _(L"&Flatten"),
                     _(L"Takes a hierarchy (nested 1xN or Nx1 grids) and converts it into a flat NxN grid, useful for export to spreadsheets"));

            wxMenu *imgmenu = new wxMenu();
            MyAppend(imgmenu, A_IMAGE, _(L"&Add Image"), _(L"Adds an image to the selected cell"));
            MyAppend(imgmenu, A_IMAGESVA, _(L"Save Image(s) to disk"),
                     _(L"Save image(s) to disk. Multiple images will be saved with a counter appended to each file name."));
            imgmenu->AppendSeparator();
            MyAppend(
                imgmenu, A_IMAGESCP, _(L"&Scale Image (re-sample pixels, by %)"),
                _(L"Change the image size if it is too big, by reducing the amount of pixels"));
            MyAppend(
                imgmenu, A_IMAGESCW, _(L"&Scale Image (re-sample pixels, by width)"),
                _(L"Change the image size if it is too big, by reducing the amount of pixels"));
            MyAppend(imgmenu, A_IMAGESCF, _(L"&Scale Image (display only)"),
                     _(L"Change the image size if it is too big or too small, by changing the size shown on screen. Applies to all uses of this image."));
            MyAppend(imgmenu, A_IMAGESCN, _(L"&Reset Scale (display only)"),
                     _(L"Change the scale to match DPI of the current display. Applies to all uses of this image."));
            imgmenu->AppendSeparator();
            MyAppend(imgmenu, A_SAVE_AS_JPEG, _(L"Save image as JPEG"),
                     _(L"Save the image in the TreeSheets file in JPEG format"));
            MyAppend(imgmenu, A_SAVE_AS_PNG, _(L"Save image as PNG (default)"),
                     _(L"Save the image in the TreeSheets file in PNG format"));
            imgmenu->AppendSeparator();
            MyAppend(imgmenu, A_IMAGER, _(L"&Remove Image(s)"),
                     _(L"Remove image(s) from the selected cells"));

            wxMenu *navmenu = new wxMenu();
            MyAppend(navmenu, A_BROWSE, _(L"Open link in &browser\tF5"),
                     _(L"Opens up the text from the selected cell in browser (should start be a valid URL)"));
            MyAppend(navmenu, A_BROWSEF, _(L"Open &file\tF4"),
                     _(L"Opens up the text from the selected cell in default application for the file type"));

            wxMenu *laymenu = new wxMenu();
            MyAppend(laymenu, A_V_GS, _(L"Vertical Layout with Grid Style Rendering\tALT+1"));
            MyAppend(laymenu, A_V_BS, _(L"Vertical Layout with Bubble Style Rendering\tALT+2"));
            MyAppend(laymenu, A_V_LS, _(L"Vertical Layout with Line Style Rendering\tALT+3"));
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_H_GS, _(L"Horizontal Layout with Grid Style Rendering\tALT+4"));
            MyAppend(laymenu, A_H_BS, _(L"Horizontal Layout with Bubble Style Rendering\tALT+5"));
            MyAppend(laymenu, A_H_LS, _(L"Horizontal Layout with Line Style Rendering\tALT+6"));
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_GS, _(L"Grid Style Rendering\tALT+7"));
            MyAppend(laymenu, A_BS, _(L"Bubble Style Rendering\tALT+8"));
            MyAppend(laymenu, A_LS, _(L"Line Style Rendering\tALT+9"));
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_TEXTGRID, _(L"Toggle Vertical Layout\tALT+0"),
                     _(L"Make a hierarchy layout more vertical (default) or more horizontal"));

            editmenu = new wxMenu();
            MyAppend(editmenu, A_CUT, _(L"Cu&t\tCTRL+x"));
            MyAppend(editmenu, A_COPY, _(L"&Copy\tCTRL+c"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_COPYWI, _(L"Copy with &Images\tCTRL+ALT+c"));
            MyAppend(editmenu, A_COPYBM, _(L"&Copy as Bitmap"));
            MyAppend(editmenu, A_COPYCT, _(L"Copy As Continuous Text"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_PASTE, _(L"&Paste\tCTRL+v"));
            MyAppend(editmenu, A_PASTESTYLE, _(L"Paste Style Only\tCTRL+SHIFT+v"),
                     _(L"only sets the colors and style of the copied cell, and keeps the text"));
            MyAppend(editmenu, A_COLLAPSE, _(L"Collapse Ce&lls\tCTRL+l"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_UNDO, _(L"&Undo\tCTRL+z"),
                     _(L"revert the changes, one step at a time"));
            MyAppend(editmenu, A_REDO, _(L"&Redo\tCTRL+y"),
                     _(L"redo any undo steps, if you haven't made changes since"));
            editmenu->AppendSeparator();
            MyAppend(
                editmenu, A_DELETE, _(L"&Delete After\tDEL"),
                _(L"Deletes the column of cells after the selected grid line, or the row below"));
            MyAppend(
                editmenu, A_BACKSPACE, _(L"Delete Before\tBACK"),
                _(L"Deletes the column of cells before the selected grid line, or the row above"));
            MyAppend(editmenu, A_DELETE_WORD, _(L"Delete Word After\tCTRL+DEL"),
                     _(L"Deletes the entire word after the cursor"));
            MyAppend(editmenu, A_BACKSPACE_WORD, _(L"Delete Word Before\tCTRL+BACK"),
                     _(L"Deletes the entire word before the cursor"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_NEWGRID,
                     #ifdef __WXMAC__
                     _(L"&Insert New Grid\tCTRL+g"),
                     #else
                     _(L"&Insert New Grid\tINS"),
                     #endif
                     _(L"Adds a grid to the selected cell"));
            MyAppend(editmenu, A_WRAP, _(L"&Wrap in new parent\tF9"),
                     _(L"Creates a new level of hierarchy around the current selection"));
            editmenu->AppendSeparator();
            // F10 is tied to the OS on both Ubuntu and OS X, and SHIFT+F10 is now right
            // click on all platforms?
            MyAppend(editmenu, A_FOLD,
                     #ifndef WIN32
                     _(L"Toggle Fold\tCTRL+F10"),
                     #else
                     _(L"Toggle Fold\tF10"),
                     #endif
                    _("Toggles showing the grid of the selected cell(s)"));
            MyAppend(editmenu, A_FOLDALL, _(L"Fold All\tCTRL+SHIFT+F10"),
                _(L"Folds the grid of the selected cell(s) recursively"));
            MyAppend(editmenu, A_UNFOLDALL, _(L"Unfold All\tCTRL+ALT+F10"),
                _(L"Unfolds the grid of the selected cell(s) recursively"));
            editmenu->AppendSeparator();
            editmenu->AppendSubMenu(selmenu, _(L"&Selection..."));
            editmenu->AppendSubMenu(orgmenu, _(L"&Grid Reorganization..."));
            editmenu->AppendSubMenu(laymenu, _(L"&Layout && Render Style..."));
            editmenu->AppendSubMenu(imgmenu, _(L"&Images..."));
            editmenu->AppendSubMenu(navmenu, _(L"&Browsing..."));
            editmenu->AppendSubMenu(temenu, _(L"Text &Editing..."));
            editmenu->AppendSubMenu(sizemenu, _(L"Text Sizing..."));
            editmenu->AppendSubMenu(stmenu, _(L"Text Style..."));
            editmenu->AppendSubMenu(bordmenu, _(L"Set Grid Border Width..."));
            editmenu->AppendSubMenu(tagmenu, _(L"Tag..."));

            if (!twoeditmenus) editmenupopup = editmenu;
        }

        wxMenu *semenu = new wxMenu();
        MyAppend(semenu, A_SEARCHF, _(L"&Search\tCTRL+f"));
        semenu->AppendCheckItem(A_CASESENSITIVESEARCH, _(L"Case-sensitive search"));
        semenu->Check(A_CASESENSITIVESEARCH, sys->casesensitivesearch);
        MyAppend(semenu, A_SEARCHNEXT, _(L"&Go To Next Search Result\tF3"));
        MyAppend(semenu, A_SEARCHPREV, _(L"Go To &Previous Search Result\tSHIFT+F3"));
        MyAppend(semenu, A_REPLACEF, _(L"&Replace\tCTRL+h"));
        MyAppend(semenu, A_REPLACEONCE, _(L"Replace in Current &Selection\tCTRL+k"));
        MyAppend(semenu, A_REPLACEONCEJ, _(L"Replace in Current Selection && &Jump Next\tCTRL+j"));
        MyAppend(semenu, A_REPLACEALL, _(L"Replace &All"));

        wxMenu *scrollmenu = new wxMenu();
        MyAppend(scrollmenu, A_AUP, _(L"Scroll Up (mousewheel)\tPGUP"));
        MyAppend(scrollmenu, A_AUP, _(L"Scroll Up (mousewheel)\tALT+UP"));
        MyAppend(scrollmenu, A_ADOWN, _(L"Scroll Down (mousewheel)\tPGDN"));
        MyAppend(scrollmenu, A_ADOWN, _(L"Scroll Down (mousewheel)\tALT+DOWN"));
        MyAppend(scrollmenu, A_ALEFT, _(L"Scroll Left\tALT+LEFT"));
        MyAppend(scrollmenu, A_ARIGHT, _(L"Scroll Right\tALT+RIGHT"));

        wxMenu *filtermenu = new wxMenu();
        MyAppend(filtermenu, A_FILTEROFF, _(L"Turn filter &off"));
        MyAppend(filtermenu, A_FILTERS, _(L"Show only cells in current search"));
        MyAppend(filtermenu, A_FILTERRANGE, _(L"Show last edits in specific date range"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER5, _(L"Show 5% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER10, _(L"Show 10% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER20, _(L"Show 20% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTER50, _(L"Show 50% of last edits"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTERM, _(L"Show 1% more than the last filter"));
        // xgettext:no-c-format
        MyAppend(filtermenu, A_FILTERL, _(L"Show 1% less than the last filter"));
        MyAppend(filtermenu, A_FILTERBYCELLBG, _(L"Filter by the same cell color"));
        MyAppend(filtermenu, A_FILTERMATCHNEXT, _(L"Go to next filter match\tCTRL+F3"));

        wxMenu *viewmenu = new wxMenu();
        MyAppend(viewmenu, A_ZOOMIN, _(L"Zoom &In (CTRL+mousewheel)\tCTRL+PGUP"));
        MyAppend(viewmenu, A_ZOOMOUT, _(L"Zoom &Out (CTRL+mousewheel)\tCTRL+PGDN"));
        MyAppend(viewmenu, A_NEXTFILE,
                 #ifndef __WXGTK__
                 _(L"Switch to &next file/tab\tCTRL+TAB"));
                 #else
                 // On Linux, this conflicts with CTRL+I, see Document::Key()
                 // CTRL+SHIFT+TAB below still works, so that will have to be used to switch tabs.
                 _(L"Switch to &next file/tab"));
                 #endif
        MyAppend(viewmenu, A_PREVFILE, _(L"Switch to &previous file/tab\tCTRL+SHIFT+TAB"));
        MyAppend(viewmenu, A_FULLSCREEN,
                 #ifdef __WXMAC__
                 _(L"Toggle &Fullscreen View\tCTRL+F11"));
                 #else
                 _(L"Toggle &Fullscreen View\tF11"));
                 #endif
        MyAppend(viewmenu, A_SCALED,
                 #ifdef __WXMAC__
                 _(L"Toggle &Scaled Presentation View\tCTRL+F12"));
                 #else
                 _(L"Toggle &Scaled Presentation View\tF12"));
                 #endif
        viewmenu->AppendSubMenu(scrollmenu, _(L"Scroll Sheet..."));
        viewmenu->AppendSubMenu(filtermenu, _(L"Filter..."));
        MyAppend(viewmenu, A_SHOWSTATS, _(L"Show statistics\tCTRL+d"));

        wxMenu *roundmenu = new wxMenu();
        roundmenu->AppendRadioItem(A_ROUND0, _(L"Radius &0"));
        roundmenu->AppendRadioItem(A_ROUND1, _(L"Radius &1"));
        roundmenu->AppendRadioItem(A_ROUND2, _(L"Radius &2"));
        roundmenu->AppendRadioItem(A_ROUND3, _(L"Radius &3"));
        roundmenu->AppendRadioItem(A_ROUND4, _(L"Radius &4"));
        roundmenu->AppendRadioItem(A_ROUND5, _(L"Radius &5"));
        roundmenu->AppendRadioItem(A_ROUND6, _(L"Radius &6"));
        roundmenu->Check(sys->roundness + A_ROUND0, true);

        wxMenu *optmenu = new wxMenu();
        MyAppend(optmenu, A_DEFFONT, _(L"Pick Default Font..."));
        MyAppend(optmenu, A_CUSTKEY, _(L"Change a key binding..."));
        MyAppend(optmenu, A_CUSTCOL, _(L"Pick Custom &Color..."));
        MyAppend(optmenu, A_COLCELL, _(L"&Set Custom Color From Cell BG"));
        MyAppend(optmenu, A_DEFBGCOL, _(L"Pick Document Background..."));
        #ifdef SIMPLERENDER
        MyAppend(optmenu, A_DEFCURCOL, _(L"Pick Cu&rsor Color..."));
        #endif
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_SHOWTBAR, _(L"Show Toolbar"));
        optmenu->Check(A_SHOWTBAR, sys->showtoolbar);
        optmenu->AppendCheckItem(A_SHOWSBAR, _(L"Show Statusbar"));
        optmenu->Check(A_SHOWSBAR, sys->showstatusbar);
        optmenu->AppendCheckItem(A_LEFTTABS, _(L"File Tabs on the bottom"));
        optmenu->Check(A_LEFTTABS, lefttabs);
        optmenu->AppendCheckItem(A_TOTRAY, _(L"Minimize to tray"));
        optmenu->Check(A_TOTRAY, sys->totray);
        optmenu->AppendCheckItem(A_MINCLOSE, _(L"Minimize on close"));
        optmenu->Check(A_MINCLOSE, sys->minclose);
        optmenu->AppendCheckItem(A_SINGLETRAY, _(L"Single click maximize from tray"));
        optmenu->Check(A_SINGLETRAY, sys->singletray);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_ZOOMSCR, _(L"Swap mousewheel scrolling and zooming"));
        optmenu->Check(A_ZOOMSCR, sys->zoomscroll);
        optmenu->AppendCheckItem(A_THINSELC, _(L"Navigate in between cells with cursor keys"));
        optmenu->Check(A_THINSELC, sys->thinselc);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_MAKEBAKS, _(L"Create .bak files"));
        optmenu->Check(A_MAKEBAKS, sys->makebaks);
        optmenu->AppendCheckItem(A_AUTOSAVE, _(L"Autosave to .tmp"));
        optmenu->Check(A_AUTOSAVE, sys->autosave);
        optmenu->AppendCheckItem(A_FSWATCH, _(L"Auto reload documents"),
                                 _(L"Reloads when another computer has changed a file (if you have made changes, asks)"));
        optmenu->Check(A_FSWATCH, sys->fswatch);
        optmenu->AppendCheckItem(A_AUTOEXPORT, _(L"Automatically export a .html on every save"));
        optmenu->Check(A_AUTOEXPORT, sys->autohtmlexport);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_CENTERED, _(L"Render document centered"));
        optmenu->Check(A_CENTERED, sys->centered);
        optmenu->AppendCheckItem(A_FASTRENDER, _(L"Faster line rendering"));
        optmenu->Check(A_FASTRENDER, sys->fastrender);
        optmenu->AppendSubMenu(roundmenu, _(L"&Roundness of grid borders..."));

        wxMenu *scriptmenu = new wxMenu();
        auto scriptpath = GetDataPath("scripts/");
        wxString sf = wxFindFirstFile(scriptpath + L"*.lobster");
        int sidx = 0;
        while (!sf.empty()) {
            auto fn = wxFileName::FileName(sf).GetFullName();
            auto ms = fn.BeforeFirst('.');
            if (sidx < 26) {
                ms += L"\tCTRL+SHIFT+ALT+";
                ms += wxChar('A' + sidx);
            }
            MyAppend(scriptmenu, A_SCRIPT + sidx, ms);
            auto ss = fn.utf8_str();
            scripts_in_menu.push_back(std::string(ss.data(), ss.length()));
            sf = wxFindNextFile();
            sidx++;
        }

        wxMenu *markmenu = new wxMenu();
        MyAppend(markmenu, A_MARKDATA, _(L"&Data\tCTRL+ALT+d"));
        MyAppend(markmenu, A_MARKCODE, _(L"&Operation\tCTRL+ALT+o"));
        MyAppend(markmenu, A_MARKVARD, _(L"Variable &Assign\tCTRL+ALT+a"));
        MyAppend(markmenu, A_MARKVARU, _(L"Variable &Read\tCTRL+ALT+r"));
        MyAppend(markmenu, A_MARKVIEWH, _(L"&Horizontal View\tCTRL+ALT+."));
        MyAppend(markmenu, A_MARKVIEWV, _(L"&Vertical View\tCTRL+ALT+,"));

        wxMenu *langmenu = new wxMenu();
        MyAppend(langmenu, A_RUN, _(L"&Run\tCTRL+ALT+F5"));
        langmenu->AppendSubMenu(markmenu, _(L"&Mark as"));
        MyAppend(langmenu, A_CLRVIEW, _(L"&Clear Views"));

        wxMenu *helpmenu = new wxMenu();
        MyAppend(helpmenu, A_ABOUT, _(L"&About..."));
        MyAppend(helpmenu, A_HELPI, _(L"Load interactive &tutorial...\tF1"));
        MyAppend(helpmenu, A_HELP_OP_REF, _(L"Load operation reference...\tCTRL+ALT+F1"));
        MyAppend(helpmenu, A_HELP, _(L"View tutorial &web page..."));

        wxAcceleratorEntry entries[3];
        entries[0].Set(wxACCEL_SHIFT, WXK_DELETE, A_CUT);
        entries[1].Set(wxACCEL_SHIFT, WXK_INSERT, A_PASTE);
        entries[2].Set(wxACCEL_CTRL, WXK_INSERT, A_COPY);
        wxAcceleratorTable accel(3, entries);
        SetAcceleratorTable(accel);

        if (!mergetbar) {
            wxMenuBar *menubar = new wxMenuBar();
            menubar->Append(filemenu, _(L"&File"));
            menubar->Append(editmenu, _(L"&Edit"));
            menubar->Append(semenu, _(L"&Search"));
            menubar->Append(viewmenu, _(L"&View"));
            menubar->Append(optmenu, _(L"&Options"));
            menubar->Append(scriptmenu, _(L"Script"));
            menubar->Append(langmenu, _(L"&Program"));
            menubar->Append(helpmenu,
                            #ifdef __WXMAC__
                            wxApp::s_macHelpMenuTitleName  // so merges with osx provided help
                            #else
                            _(L"&Help")
                            #endif
                            );
            #ifdef __WXMAC__
            // these don't seem to work anymore in the newer wxWidgets, handled in the menu event
            // handler below instead
            wxApp::s_macAboutMenuItemId = A_ABOUT;
            wxApp::s_macExitMenuItemId = wxID_EXIT;
            wxApp::s_macPreferencesMenuItemId =
                A_DEFFONT;  // we have no prefs, so for now just select the font
            #endif
            SetMenuBar(menubar);
        }

        wxColour toolbgcol(0xD8C7BC);

        tb = CreateToolBar(wxBORDER_NONE | wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
        tb->SetOwnBackgroundColour(toolbgcol);

        #ifdef __WXMAC__
        #define SEPARATOR
        #else
        #define SEPARATOR tb->AddSeparator()
        #endif

        wxString iconpath = GetDataPath(L"images/material/toolbar/");

        auto AddTBIcon = [&](const wxChar *name, int action, wxString file) {
            tb->AddTool(action, name, wxBitmapBundle::FromSVGFile(file, wxSize(24, 24)), name,
                        wxITEM_NORMAL);
        };

        AddTBIcon(_(L"New (CTRL+n)"), A_NEW, iconpath + L"filenew.svg");
        AddTBIcon(_(L"Open (CTRL+o)"), A_OPEN, iconpath + L"fileopen.svg");
        AddTBIcon(_(L"Save (CTRL+s)"), A_SAVE, iconpath + L"filesave.svg");
        AddTBIcon(_(L"Save As"), A_SAVEAS, iconpath + L"filesaveas.svg");
        SEPARATOR;
        AddTBIcon(_(L"Undo (CTRL+z)"), A_UNDO, iconpath + L"undo.svg");
        AddTBIcon(_(L"Copy (CTRL+c)"), A_COPY, iconpath + L"editcopy.svg");
        AddTBIcon(_(L"Paste (CTRL+v)"), A_PASTE, iconpath + L"editpaste.svg");
        SEPARATOR;
        AddTBIcon(_(L"Zoom In (CTRL+mousewheel)"), A_ZOOMIN, iconpath + L"zoomin.svg");
        AddTBIcon(_(L"Zoom Out (CTRL+mousewheel)"), A_ZOOMOUT, iconpath + L"zoomout.svg");
        SEPARATOR;
        AddTBIcon(_(L"New Grid (INS)"), A_NEWGRID, iconpath + L"newgrid.svg");
        AddTBIcon(_(L"Add Image"), A_IMAGE, iconpath + L"image.svg");
        SEPARATOR;
        AddTBIcon(_(L"Run"), A_RUN, iconpath + L"run.svg");
        tb->AddSeparator();
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _(L"Search ")));
        tb->AddControl(filter = new wxTextCtrl(tb, A_SEARCH, "", wxDefaultPosition,
                                               FromDIP(wxSize(80, 22)),
                                               wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddTBIcon(_(L"Clear search"), A_CLEARSEARCH, iconpath + L"cancel.svg");
        AddTBIcon(_(L"Go to Next Search Result"), A_SEARCHNEXT, iconpath + L"search.svg");
        SEPARATOR;
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _(L"Replace ")));
        tb->AddControl(replaces = new wxTextCtrl(tb, A_REPLACE, "", wxDefaultPosition,
                                                 FromDIP(wxSize(80, 22)),
                                                 wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddTBIcon(_(L"Clear replace"), A_CLEARREPLACE, iconpath + L"cancel.svg");
        AddTBIcon(_(L"Replace in selection"), A_REPLACEONCE, iconpath + L"replace.svg");
        AddTBIcon(_(L"Replace All"), A_REPLACEALL, iconpath + L"replaceall.svg");
        tb->AddSeparator();
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _(L"Cell ")));
        celldd = new ColorDropdown(tb, A_CELLCOLOR, 1);
        tb->AddControl(celldd);
        SEPARATOR;
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _(L"Text ")));
        textdd = new ColorDropdown(tb, A_TEXTCOLOR, 2);
        tb->AddControl(textdd);
        SEPARATOR;
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _(L"Border ")));
        borddd = new ColorDropdown(tb, A_BORDCOLOR, 7);
        tb->AddControl(borddd);
        tb->AddSeparator();
        tb->AddControl(new wxStaticText(tb, wxID_ANY, _(L"Image ")));
        idd = new ImageDropdown(tb, imagepath);
        tb->AddControl(idd);
        tb->Realize();
        tb->Show(sys->showtoolbar);

        wxStatusBar *sb = CreateStatusBar(4);
        sb->SetOwnBackgroundColour(toolbgcol);
        SetStatusBarPane(0);
        SetDPIAwareStatusWidths();
        sb->Show(sys->showstatusbar);

        nb = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxAUI_NB_TAB_MOVE | wxAUI_NB_TAB_SPLIT | wxAUI_NB_SCROLL_BUTTONS |
                                   wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_CLOSE_ON_ALL_TABS |
                                   (lefttabs ? wxAUI_NB_BOTTOM : wxAUI_NB_TOP));
        nb->SetOwnBackgroundColour(toolbgcol);

        int display_id = wxDisplay::GetFromWindow(this);
        wxRect disprect = wxDisplay(display_id == wxNOT_FOUND ? 0 : display_id).GetClientArea();
        const int screenx = disprect.width - disprect.x;
        const int screeny = disprect.height - disprect.y;

        const int boundary = 64;
        const int defx = screenx - 2 * boundary;
        const int defy = screeny - 2 * boundary;
        int resx, resy, posx, posy;
        sys->cfg->Read(L"resx", &resx, defx);
        sys->cfg->Read(L"resy", &resy, defy);
        sys->cfg->Read(L"posx", &posx, boundary + disprect.x);
        sys->cfg->Read(L"posy", &posy, boundary + disprect.y);
        if (resx > screenx || resy > screeny || posx < disprect.x || posy < disprect.y ||
            posx + resx > disprect.width + disprect.x ||
            posy + resy > disprect.height + disprect.y) {
            // Screen res has been resized since we last ran, set sizes to default to avoid being
            // off-screen.
            resx = defx;
            resy = defy;
            posx = posy = boundary;
            posx += disprect.x;
            posy += disprect.y;
        }
        SetSize(resx, resy);
        SetPosition(wxPoint(posx, posy));

        bool ismax;
        sys->cfg->Read(L"maximized", &ismax, true);

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
        sys->cfg->Write(L"customcolor", sys->customcolor);
        #ifdef SIMPLERENDER
        sys->cfg->Write(L"cursorcolor", sys->cursorcolor);
        #endif
        sys->cfg->Write(L"showtoolbar", sys->showtoolbar);
        sys->cfg->Write(L"showstatusbar", sys->showstatusbar);
        if (!IsIconized()) {
            sys->cfg->Write(L"maximized", IsMaximized());
            if (!IsMaximized()) {
                sys->cfg->Write(L"resx", GetSize().x);
                sys->cfg->Write(L"resy", GetSize().y);
                sys->cfg->Write(L"posx", GetPosition().x);
                sys->cfg->Write(L"posy", GetPosition().y);
            }
        }
        aui->ClearEventHashTable();
        aui->UnInit();
        DELETEP(editmenupopup);
    }

    TSCanvas *NewTab(Document *doc, bool append = false) {
        TSCanvas *sw = new TSCanvas(this, nb);
        sw->doc = doc;
        doc->sw = sw;
        sw->SetScrollRate(1, 1);
        if (append)
            nb->AddPage(sw, _(L"<unnamed>"), true, wxNullBitmap);
        else
            nb->InsertPage(0, sw, _(L"<unnamed>"), true, wxNullBitmap);
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
        sys->TabChange(sw->doc);
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
        if (page == nb->GetSelection()) SetTitle(L"TreeSheets - " + fn + mods);
        nb->SetPageText(page,
                        (fn.empty() ? wxString(_(L"<unnamed>")) : wxFileName(fn).GetName()) + mods);
    }

    void TBMenu(wxToolBar *tb, wxMenu *menu, const wxChar *name, int id = 0) {
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
                case A_SELALL: tc->SetSelection(0, 1000); return;
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
        auto Check = [&](const wxChar *cfg) {
            sys->cfg->Write(cfg, ce.IsChecked());
            sw->Status(_(L"change will take effect next run of TreeSheets"));
        };
        switch (ce.GetId()) {
            case A_NOP: break;

            case A_ALEFT: sw->CursorScroll(-g_scrollratecursor, 0); break;
            case A_ARIGHT: sw->CursorScroll(g_scrollratecursor, 0); break;
            case A_AUP: sw->CursorScroll(0, -g_scrollratecursor); break;
            case A_ADOWN: sw->CursorScroll(0, g_scrollratecursor); break;

            case A_SHOWSBAR:
                if (!IsFullScreen()) {
                    sys->showstatusbar = !sys->showstatusbar;
                    wxStatusBar *wsb = this->GetStatusBar();
                    wsb->Show(sys->showstatusbar);
                    this->SendSizeEvent();
                    this->Refresh();
                    wsb->Refresh();
                }
                break;
            case A_SHOWTBAR:
                if (!IsFullScreen()) {
                    sys->showtoolbar = !sys->showtoolbar;
                    wxToolBar *wtb = this->GetToolBar();
                    wtb->Show(sys->showtoolbar);
                    this->SendSizeEvent();
                    this->Refresh();
                    wtb->Refresh();
                }
                break;
            case A_LEFTTABS: Check(L"lefttabs"); break;
            case A_SINGLETRAY: Check(L"singletray"); break;
            case A_MAKEBAKS: sys->cfg->Write(L"makebaks", sys->makebaks = ce.IsChecked()); break;
            case A_TOTRAY: sys->cfg->Write(L"totray", sys->totray = ce.IsChecked()); break;
            case A_MINCLOSE: sys->cfg->Write(L"minclose", sys->minclose = ce.IsChecked()); break;
            case A_ZOOMSCR: sys->cfg->Write(L"zoomscroll", sys->zoomscroll = ce.IsChecked()); break;
            case A_THINSELC: sys->cfg->Write(L"thinselc", sys->thinselc = ce.IsChecked()); break;
            case A_AUTOSAVE: sys->cfg->Write(L"autosave", sys->autosave = ce.IsChecked()); break;
            case A_CENTERED:
                sys->cfg->Write(L"centered", sys->centered = ce.IsChecked());
                Refresh();
                break;
            case A_FSWATCH:
                Check(L"fswatch");
                sys->fswatch = ce.IsChecked();
                break;
            case A_AUTOEXPORT:
                sys->cfg->Write(L"autohtmlexport", sys->autohtmlexport = ce.IsChecked());
                break;
            case A_FASTRENDER:
                sys->cfg->Write(L"fastrender", sys->fastrender = ce.IsChecked());
                Refresh();
                break;
            case A_FULLSCREEN:
                ShowFullScreen(!IsFullScreen());
                if (IsFullScreen()) sw->Status(_(L"Press F11 to exit fullscreen mode."));
                break;
            case A_SEARCHF:
                if (filter) {
                    filter->SetFocus();
                    filter->SetSelection(0, 1000);
                } else {
                    sw->Status(_(L"Please enable (Options -> Show Toolbar) to use search."));
                }
                break;
            case A_REPLACEF:
                if (replaces) {
                    replaces->SetFocus();
                    replaces->SetSelection(0, 1000);
                } else {
                    sw->Status(_(L"Please enable (Options -> Show Toolbar) to use replace."));
                }
                break;
            #ifdef __WXMAC__
            case wxID_OSX_HIDE: Iconize(true); break;
            case wxID_OSX_HIDEOTHERS: sw->Status(L"NOT IMPLEMENTED"); break;
            case wxID_OSX_SHOWALL: Iconize(false); break;
            case wxID_ABOUT: sw->doc->Action(dc, A_ABOUT); break;
            case wxID_PREFERENCES: sw->doc->Action(dc, A_DEFFONT); break;
            #endif
            case wxID_EXIT:
                fromclosebox = false;
                this->Close();
                break;
            case A_CLOSE: sw->doc->Action(dc, ce.GetId()); break;  // sw dangling pointer on return
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
        filter->SetBackgroundColour(found ? wxColour("AQUAMARINE")
                                          : wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
        filter->SetForegroundColour(found ? *wxBLACK
                                          : wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT));
        filter->Refresh();
    }

    void OnSearch(wxCommandEvent &ce) {
        wxString searchstring = ce.GetString();
        sys->darkennonmatchingcells = searchstring.Len() != 0;
        sys->searchstring = (sys->casesensitivesearch) ? searchstring : searchstring.Lower();
        SetSearchTextBoxBackgroundColour(false);
        Document *doc = GetCurTab()->doc;
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
        foldiconi.LoadFile(GetDataPath(L"images/nuvola/fold.png"));
        foldicon = wxBitmap(foldiconi);
        ScaleBitmap(foldicon, FromDIP(1.0) / 3.0, foldicon);
    }

    void SetDPIAwareStatusWidths() {
        int swidths[] = {-1, FromDIP(300), FromDIP(120), FromDIP(100)};
        SetStatusWidths(4, swidths);
    }

    void OnDPIChanged(wxDPIChangedEvent &dce) {
        {  // block all other events until we finished preparing
            wxEventBlocker blocker(this);
            wxBusyCursor wait;
            csf = FromDIP(1.0);
            {
                ThreadPool pool(std::thread::hardware_concurrency());
                loopv(i, sys->imagelist) {
                    pool.enqueue(
                        [](Image *img) {
                            img->bm_display = wxNullBitmap;
                            img->Display();
                        },
                        sys->imagelist[i]);
                }
            }  // wait until all tasks are finished
            RenderFolderIcon();
            if (nb) {
                loop(i, nb->GetPageCount()) {
                    TSCanvas *p = (TSCanvas *)nb->GetPage(i);
                    p->doc->dpichanged = true;
                    p->doc->scrolltoselection = true;
                }
                nb->SetTabCtrlHeight(-1);
            }
            idd->FillBitmapVector(imagepath);
            if (GetStatusBar()) SetDPIAwareStatusWidths();
        }
    }

    void OnIconize(wxIconizeEvent &me) {
        if (me.IsIconized()) {
            #ifndef __WXMAC__
            if (sys->totray) {
                tbi.SetIcon(icon, L"TreeSheets");
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
    void SetRegKey(const wxChar *key, wxString val) {
        wxRegKey rk(key);
        rk.Create();
        rk.SetValue(L"", val);
    }
    #endif

    void SetFileAssoc(wxString &exename) {
        #ifdef WIN32
        SetRegKey(L"HKEY_CURRENT_USER\\Software\\Classes\\.cts", L"TreeSheets");
        SetRegKey(L"HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets", L"TreeSheets file");
        SetRegKey(L"HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\Shell\\Open\\Command",
                  wxString(L"\"") + exename + L"\" \"%1\"");
        SetRegKey(L"HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\DefaultIcon",
                  wxString(L"\"") + exename + L"\",0");
        #else
        // TODO: do something similar for mac/kde/gnome?
        #endif
    }

    void OnFileSystemEvent(wxFileSystemWatcherEvent &event) {
        // 0xF == create/delete/rename/modify
        if ((event.GetChangeType() & 0xF) == 0 || watcherwaitingforuser || !nb) return;
        const wxString &modfile = event.GetPath().GetFullPath();
        loop(i, nb->GetPageCount()) {
            Document *doc = ((TSCanvas *)nb->GetPage(i))->doc;
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
                        _(L"%s\nhas been modified on disk by another program / computer:\nWould you like to discard your changes and re-load from disk?"),
                        doc->filename);
                    watcherwaitingforuser = true;
                    int res = wxMessageBox(msg, _(L"File modification conflict!"),
                                           wxYES_NO | wxICON_QUESTION, this);
                    watcherwaitingforuser = false;
                    if (res != wxYES) return;
                }
                auto msg = sys->LoadDB(doc->filename, false, true);
                assert(msg);
                if (*msg) {
                    GetCurTab()->Status(msg);
                } else {
                    loop(j, nb->GetPageCount()) if (((TSCanvas *)nb->GetPage(j))->doc == doc)
                        nb->DeletePage(j);
                    ::wxRemoveFile(sys->TmpName(modfile));
                    GetCurTab()->Status(
                        _(L"File has been re-loaded because of modifications of another program / computer"));
                }
                return;
            }
        }
    }

    DECLARE_EVENT_TABLE()
};
