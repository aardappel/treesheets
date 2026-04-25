struct TSFrame : wxFrame {
    TSApp *app;
    wxIcon icon;
    wxTaskBarIcon taskbaricon;
    wxMenu *editmenupopup;
    wxFileHistory filehistory;
    #ifdef ENABLE_LOBSTER
        wxFileHistory scripts {A_MAXACTION - A_SCRIPT, A_SCRIPT};
    #endif
    wxFileSystemWatcher *watcher;
    wxAuiNotebook *notebook {nullptr};
    wxAuiManager aui {this};
    wxBitmap line_nw;
    wxBitmap line_sw;
    wxBitmap foldicon;
    bool fromclosebox {true};
    bool watcherwaitingforuser {false};
    wxColour toolbarbackgroundcolor {0xD8C7BC};
    wxTextCtrl *filter {nullptr};
    wxTextCtrl *replaces {nullptr};
    ColorDropdown *cellcolordropdown {nullptr};
    ColorDropdown *textcolordropdown {nullptr};
    ColorDropdown *bordercolordropdown {nullptr};
    ImageDropdown *imagedropdown {nullptr};
    wxString imagepath;
    int refreshhack {0};
    int refreshhackinstances {0};
    std::map<wxString, wxString> menustrings;

    TSFrame(TSApp *_app)
        : wxFrame((wxFrame *)nullptr, wxID_ANY, "TreeSheets", wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_FRAME_STYLE),
          app(_app) {
        sys->frame = this;

        class MyLog : public wxLog {
            void DoLogString(const wxChar *message, time_t timestamp) { DoLogText(*message); }
            void DoLogText(const wxString &message) {
                #ifdef WIN32
                OutputDebugString(message.c_str());
                OutputDebugString(L"\n");
                #else
                fputws(message.c_str(), stderr);
                fputws(L"\n", stderr);
                #endif
            }
        };

        wxLog::SetActiveTarget(new MyLog());

        wxLogMessage("%s", wxVERSION_STRING);

        aui.SetManagedWindow(this);

        wxInitAllImageHandlers();

        wxIconBundle icons;
        wxIcon iconbig;
        #ifdef WIN32
            int iconsmall = ::GetSystemMetrics(SM_CXSMICON);
            int iconlarge = ::GetSystemMetrics(SM_CXICON);
        #endif
        icon.LoadFile(app->GetDataPath("images/icon16.png"), wxBITMAP_TYPE_PNG
            #ifdef WIN32
                , iconsmall, iconsmall
            #endif
        );
        iconbig.LoadFile(app->GetDataPath("images/icon32.png"), wxBITMAP_TYPE_PNG
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
        line_nw.LoadFile(app->GetDataPath("images/render/line_nw.png"), wxBITMAP_TYPE_PNG);
        line_sw.LoadFile(app->GetDataPath("images/render/line_sw.png"), wxBITMAP_TYPE_PNG);

        imagepath = app->GetDataPath("images/nuvola/dropdown/");

        if (sys->singletray)
            taskbaricon.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_UP,
                        wxTaskBarIconEventHandler(TSFrame::OnTBIDBLClick), nullptr, this);
        else
            taskbaricon.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_DCLICK,
                        wxTaskBarIconEventHandler(TSFrame::OnTBIDBLClick), nullptr, this);

        bool showtbar, showsbar, lefttabs;

        sys->cfg->Read("showtbar", &showtbar, true);
        sys->cfg->Read("showsbar", &showsbar, true);
        sys->cfg->Read("lefttabs", &lefttabs, true);

        filehistory.Load(*sys->cfg);
        #ifdef ENABLE_LOBSTER
            auto oldpath = sys->cfg->GetPath();
            sys->cfg->SetPath("/scripts");
            scripts.Load(*sys->cfg);
            sys->cfg->SetPath(oldpath);
        #endif

        #ifdef __WXMAC__
            #define CTRLORALT "CTRL"
        #else
            #define CTRLORALT "ALT"
        #endif

        #ifdef __WXMAC__
            #define ALTORCTRL "ALT"
        #else
            #define ALTORCTRL "CTRL"
        #endif

        auto expmenu = new wxMenu();
        MyAppend(expmenu, A_EXPXML, _("&XML..."),
                 _("Export the current view as XML (which can also be reimported without losing structure)"));
        expmenu->AppendSeparator();
        MyAppend(expmenu, A_EXPHTMLT, _("&HTML (Tables+Styling)..."),
                 _("Export the current view as HTML using nested tables, that will look somewhat like the TreeSheet"));
        MyAppend(expmenu, A_EXPHTMLTE, _("&HTML (Tables+Styling+Images)..."),
                 _("Export the curent view as HTML using nested tables and exported images"));
        MyAppend(expmenu, A_EXPHTMLB, _("HTML (&Bullet points)..."),
                 _("Export the current view as HTML as nested bullet points."));
        MyAppend(expmenu, A_EXPHTMLO, _("HTML (&Outline)..."),
                 _("Export the current view as HTML as nested headers, suitable for importing into Word's outline mode"));
        expmenu->AppendSeparator();
        MyAppend(
            expmenu, A_EXPTEXT, _("Indented &Text..."),
            _("Export the current view as tree structured text, using spaces for each indentation level. Suitable for importing into mindmanagers and general text programs"));
        MyAppend(
            expmenu, A_EXPCSV, _("&Comma delimited text (CSV)..."),
            _("Export the current view as CSV. Good for spreadsheets and databases. Only works on grids with no sub-grids (use the Flatten operation first if need be)"));
        expmenu->AppendSeparator();
        MyAppend(expmenu, A_EXPIMAGE, _("&Image..."),
                 _("Export the current view as an image. Useful for faithful renderings of the TreeSheet, and programs that don't accept any of the above options"));
        MyAppend(expmenu, A_EXPSVG, _("&Vector graphics..."),
                _("Export the current view to a SVG vector file."));

        auto impmenu = new wxMenu();
        MyAppend(impmenu, A_IMPXML, _("XML..."));
        MyAppend(impmenu, A_IMPXMLA, _("XML (attributes too, for OPML etc)..."));
        MyAppend(impmenu, A_IMPTXTI, _("Indented text..."));
        MyAppend(impmenu, A_IMPTXTC, _("Comma delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTS, _("Semi-Colon delimited text (CSV)..."));
        MyAppend(impmenu, A_IMPTXTT, _("Tab delimited text..."));

        auto recentmenu = new wxMenu();
        filehistory.UseMenu(recentmenu);
        filehistory.AddFilesToMenu();

        auto filemenu = new wxMenu();
        MyAppend(filemenu, wxID_NEW, _("&New") + "\tCTRL+N", _("Create a new document"));
        MyAppend(filemenu, wxID_OPEN, _("&Open...") + "\tCTRL+O", _("Open an existing document"));
        MyAppend(filemenu, wxID_CLOSE, _("&Close") + "\tCTRL+W", _("Close current document"));
        filemenu->AppendSubMenu(recentmenu, _("&Recent files"));
        MyAppend(filemenu, wxID_SAVE, _("&Save") + "\tCTRL+S", _("Save current document"));
        MyAppend(filemenu, wxID_SAVEAS, _("Save &As..."),
                 _("Save current document with a different filename"));
        MyAppend(filemenu, A_SAVEALL, _("Save All"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, A_PAGESETUP, _("Page Setup..."));
        MyAppend(filemenu, A_PRINTSCALE, _("Set Print Scale..."));
        MyAppend(filemenu, wxID_PREVIEW, _("Print preview..."));
        MyAppend(filemenu, wxID_PRINT, _("&Print...") + "\tCTRL+P");
        filemenu->AppendSeparator();
        filemenu->AppendSubMenu(expmenu, _("Export &view as"));
        filemenu->AppendSubMenu(impmenu, _("Import from"));
        filemenu->AppendSeparator();
        MyAppend(filemenu, wxID_EXIT, _("&Exit") + "\tCTRL+Q", _("Quit this program"));

        wxMenu *editmenu;
        loop(twoeditmenus, 2) {
            auto sizemenu = new wxMenu();
            MyAppend(sizemenu, A_INCSIZE,
                     _("&Increase text size (SHIFT+mousewheel)") + "\tSHIFT+PGUP");
            MyAppend(sizemenu, A_DECSIZE,
                     _("&Decrease text size (SHIFT+mousewheel)") + "\tSHIFT+PGDN");
            MyAppend(sizemenu, A_RESETSIZE, _("&Reset text sizes") + "\tCTRL+SHIFT+S");
            MyAppend(sizemenu, A_MINISIZE, _("&Shrink text of all sub-grids") + "\tCTRL+SHIFT+M");
            sizemenu->AppendSeparator();
            MyAppend(sizemenu, A_INCWIDTH,
                     _("Increase column width (ALT+mousewheel)") + "\tALT+PGUP");
            MyAppend(sizemenu, A_DECWIDTH,
                     _("Decrease column width (ALT+mousewheel)") + "\tALT+PGDN");
            MyAppend(sizemenu, A_INCWIDTHNH,
                     _("Increase column width (no sub grids)") + "\tCTRL+ALT+PGUP");
            MyAppend(sizemenu, A_DECWIDTHNH,
                     _("Decrease column width (no sub grids)") + "\tCTRL+ALT+PGDN");
            MyAppend(sizemenu, A_RESETWIDTH, _("Reset column widths") + "\tCTRL+R",
                     _("Reset the column widths in the selection to the default column width"));

            auto bordmenu = new wxMenu();
            MyAppend(bordmenu, A_BORD0, _("Border &0") + "\tCTRL+SHIFT+9");
            MyAppend(bordmenu, A_BORD1, _("Border &1") + "\tCTRL+SHIFT+1");
            MyAppend(bordmenu, A_BORD2, _("Border &2") + "\tCTRL+SHIFT+2");
            MyAppend(bordmenu, A_BORD3, _("Border &3") + "\tCTRL+SHIFT+3");
            MyAppend(bordmenu, A_BORD4, _("Border &4") + "\tCTRL+SHIFT+4");
            MyAppend(bordmenu, A_BORD5, _("Border &5") + "\tCTRL+SHIFT+5");

            auto selmenu = new wxMenu();
            MyAppend(selmenu, A_NEXT,
                #ifdef __WXGTK__
                    _("Move to next cell (TAB)")
                #else
                     _("Move to next cell") + "\tTAB"
                #endif
            );
            MyAppend(selmenu, A_PREV,
                #ifdef __WXGTK__
                    _("Move to previous cell (SHIFT+TAB)")
                #else
                     _("Move to previous cell") + "\tSHIFT+TAB"
                #endif
            );
            selmenu->AppendSeparator();
            MyAppend(selmenu, wxID_SELECTALL, _("Select &all in current grid/cell") + "\tCTRL+A");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LEFT,
                #ifdef __WXGTK__
                    _("Move Selection Left (LEFT)")
                #else
                     _("Move Selection Left") + "\tLEFT"
                #endif
            );
            MyAppend(selmenu, A_RIGHT,
                #ifdef __WXGTK__
                    _("Move Selection Right (RIGHT)")
                #else
                     _("Move Selection Right") + "\tRIGHT"
                #endif
            );
            MyAppend(selmenu, A_UP,
                #ifdef __WXGTK__
                    _("Move Selection Up (UP)")
                #else
                     _("Move Selection Up") + "\tUP"
                #endif
            );
            MyAppend(selmenu, A_DOWN,
                #ifdef __WXGTK__
                    _("Move Selection Down (DOWN)")
                #else
                     _("Move Selection Down") + "\tDOWN"
                #endif
            );
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_MLEFT, _("Move Cells Left") + "\tCTRL+LEFT");
            MyAppend(selmenu, A_MRIGHT, _("Move Cells Right") + "\tCTRL+RIGHT");
            MyAppend(selmenu, A_MUP, _("Move Cells Up") + "\tCTRL+UP");
            MyAppend(selmenu, A_MDOWN, _("Move Cells Down") + "\tCTRL+DOWN");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SLEFT, _("Extend Selection Left") + "\tSHIFT+LEFT");
            MyAppend(selmenu, A_SRIGHT, _("Extend Selection Right") + "\tSHIFT+RIGHT");
            MyAppend(selmenu, A_SUP, _("Extend Selection Up") + "\tSHIFT+UP");
            MyAppend(selmenu, A_SDOWN, _("Extend Selection Down") + "\tSHIFT+DOWN");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SROWS, _("Extend Selection Full Rows") + "\tCTRL+SHIFT+B");
            MyAppend(selmenu, A_SCLEFT, _("Extend Selection Rows Left") + "\tCTRL+SHIFT+LEFT");
            MyAppend(selmenu, A_SCRIGHT, _("Extend Selection Rows Right") + "\tCTRL+SHIFT+RIGHT");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_SCOLS, _("Extend Selection Full Columns") + "\tCTRL+SHIFT+A");
            MyAppend(selmenu, A_SCUP, _("Extend Selection Columns Up") + "\tCTRL+SHIFT+UP");
            MyAppend(selmenu, A_SCDOWN, _("Extend Selection Columns Down") + "\tCTRL+SHIFT+DOWN");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_CANCELEDIT, _("Select &Parent") + "\tESC");
            MyAppend(selmenu, A_ENTERGRID, _("Select First &Child") + "\tSHIFT+ENTER");
            selmenu->AppendSeparator();
            MyAppend(selmenu, A_LINK, _("Go To &Matching Cell (Text)") + "\tF6");
            MyAppend(selmenu, A_LINKREV, _("Go To Matching Cell (Text, Reverse)") + "\tSHIFT+F6");
            MyAppend(selmenu, A_LINKIMG, _("Go To Matching Cell (Image)") + "\tF7");
            MyAppend(selmenu, A_LINKIMGREV,
                     _("Go To Matching Cell (Image, Reverse)") + "\tSHIFT+F7");

            auto temenu = new wxMenu();
            MyAppend(temenu, A_LEFT, _("Cursor Left") + "\tLEFT");
            MyAppend(temenu, A_RIGHT, _("Cursor Right") + "\tRIGHT");
            MyAppend(temenu, A_MLEFT, _("Word Left") + "\tCTRL+LEFT");
            MyAppend(temenu, A_MRIGHT, _("Word Right") + "\tCTRL+RIGHT");
            temenu->AppendSeparator();
            MyAppend(temenu, A_SLEFT, _("Extend Selection Left") + "\tSHIFT+LEFT");
            MyAppend(temenu, A_SRIGHT, _("Extend Selection Right") + "\tSHIFT+RIGHT");
            MyAppend(temenu, A_SCLEFT, _("Extend Selection Word Left") + "\tCTRL+SHIFT+LEFT");
            MyAppend(temenu, A_SCRIGHT, _("Extend Selection Word Right") + "\tCTRL+SHIFT+RIGHT");
            MyAppend(temenu, A_SHOME, _("Extend Selection to Start") + "\tSHIFT+HOME");
            MyAppend(temenu, A_SEND, _("Extend Selection to End") + "\tSHIFT+END");
            temenu->AppendSeparator();
            MyAppend(temenu, A_HOME, _("Start of line of text") + "\tHOME");
            MyAppend(temenu, A_END, _("End of line of text") + "\tEND");
            MyAppend(temenu, A_CHOME, _("Start of text") + "\tCTRL+HOME");
            MyAppend(temenu, A_CEND, _("End of text") + "\tCTRL+END");
            temenu->AppendSeparator();
            MyAppend(temenu, A_ENTERCELL, _("Enter/exit text edit mode") + "\tENTER");
            MyAppend(temenu, A_ENTERCELL_JUMPTOEND,
                     _("...and jump to the end of the text") + "\tF2");
            MyAppend(
                temenu, A_ENTERCELL_JUMPTOSTART,
                _("...and progress to the first cell in the new row") + "\t" ALTORCTRL "+ENTER");
            MyAppend(temenu, A_PROGRESSCELL,
                     _("...and progress to the next cell on the right") + "\t" CTRLORALT "+ENTER");
            MyAppend(temenu, A_CANCELEDIT, _("Cancel text edits") + "\tESC");

            auto stmenu = new wxMenu();
            MyAppend(stmenu, wxID_BOLD, _("Toggle cell &BOLD") + "\tCTRL+B");
            MyAppend(stmenu, wxID_ITALIC, _("Toggle cell &ITALIC") + "\tCTRL+I");
            MyAppend(stmenu, A_TT, _("Toggle cell &typewriter") + "\tCTRL+ALT+T");
            MyAppend(stmenu, wxID_UNDERLINE, _("Toggle cell &underlined") + "\tCTRL+U");
            MyAppend(stmenu, wxID_STRIKETHROUGH, _("Toggle cell &strikethrough") + "\tCTRL+T");
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_RESETSTYLE, _("&Reset text styles") + "\tCTRL+SHIFT+R");
            MyAppend(stmenu, A_RESETCOLOR, _("Reset &colors") + "\tCTRL+SHIFT+C");
            stmenu->AppendSeparator();
            MyAppend(stmenu, A_LASTCELLCOLOR, _("Apply last cell color") + "\tSHIFT+ALT+C");
            MyAppend(stmenu, A_LASTTEXTCOLOR, _("Apply last text color") + "\tSHIFT+ALT+T");
            MyAppend(stmenu, A_LASTBORDCOLOR, _("Apply last border color") + "\tSHIFT+ALT+B");
            MyAppend(stmenu, A_OPENCELLCOLOR, _("Open cell colors") + "\tSHIFT+ALT+F9");
            MyAppend(stmenu, A_OPENTEXTCOLOR, _("Open text colors") + "\tSHIFT+ALT+F10");
            MyAppend(stmenu, A_OPENBORDCOLOR, _("Open border colors") + "\tSHIFT+ALT+F11");
            MyAppend(stmenu, A_OPENIMGDROPDOWN, _("Open image dropdown") + "\tSHIFT+ALT+F12");

            auto tagmenu = new wxMenu();
            MyAppend(tagmenu, A_TAGADD, _("&Add Cell Text as Tag"));
            MyAppend(tagmenu, A_TAGREMOVE, _("&Remove Cell Text from Tags"));
            MyAppend(tagmenu, A_NOP, _("&Set Cell Text to tag (use CTRL+RMB)"),
                     _("Hold CTRL while pressing right mouse button to quickly set a tag for the current cell using a popup menu"));

            auto orgmenu = new wxMenu();
            MyAppend(orgmenu, A_TRANSPOSE, _("&Transpose") + "\tCTRL+SHIFT+T",
                     _("changes the orientation of a grid"));
            MyAppend(orgmenu, A_SORT, _("Sort &Ascending"),
                     _("Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_SORTD, _("Sort &Descending"),
                     _("Make a 1xN selection to indicate which column to sort on, and which rows to affect"));
            MyAppend(orgmenu, A_HSWAP, _("Hierarchy &Swap") + "\tF8",
                     _("Swap all cells with this text at this level (or above) with the parent"));
            MyAppend(orgmenu, A_HIFY, _("&Hierarchify"),
                     _("Convert an NxN grid with repeating elements per column into an 1xN grid with hierarchy, useful to convert data from spreadsheets"));
            MyAppend(orgmenu, A_FLATTEN, _("&Flatten"),
                     _("Takes a hierarchy (nested 1xN or Nx1 grids) and converts it into a flat NxN grid, useful for export to spreadsheets"));

            auto imgmenu = new wxMenu();
            MyAppend(imgmenu, A_IMAGE, _("&Add..."), _("Add an image to the selected cell"));
            MyAppend(imgmenu, A_IMAGESVA, _("&Save as..."),
                     _("Save image(s) from selected cell(s) to disk. Multiple images will be saved with a counter appended to each file name."));
            imgmenu->AppendSeparator();
            MyAppend(
                imgmenu, A_IMAGESCP, _("Scale (re-sa&mple pixels, by %)"),
                _("Change the image(s) size if it is too big, by reducing the amount of pixels"));
            MyAppend(
                imgmenu, A_IMAGESCW, _("Scale (re-sample pixels, by &width)"),
                _("Change the image(s) size if it is too big, by reducing the amount of pixels"));
            MyAppend(imgmenu, A_IMAGESCF, _("Scale (&display only)"),
                     _("Change the image(s) size if it is too big or too small, by changing the size shown on screen. Applies to all uses of this image."));
            MyAppend(imgmenu, A_IMAGESCN, _("&Reset Scale (display only)"),
                     _("Change the image(s) scale to match DPI of the current display. Applies to all uses of this image."));
            imgmenu->AppendSeparator();
            MyAppend(
                imgmenu, A_SAVE_AS_JPEG, _("Embed as &JPEG"),
                _("Embed the image(s) in the selected cells in JPEG format (reduces data size)"));
            MyAppend(imgmenu, A_SAVE_AS_PNG, _("Embed as &PNG"),
                     _("Embed the image(s) in the selected cells in PNG format (default)"));
            imgmenu->AppendSeparator();
            MyAppend(imgmenu, A_LASTIMAGE, _("Insert last image") + "\tSHIFT+ALT+i",
                     _("Insert the last image that has been inserted before in TreeSheets."));
            MyAppend(imgmenu, A_IMAGER, _("Remo&ve"),
                     _("Remove image(s) from the selected cells"));

            auto navmenu = new wxMenu();
            MyAppend(navmenu, A_BROWSE, _("Open link in &browser") + "\tF5",
                     _("Opens up the text from the selected cell in browser (should start be a valid URL)"));
            MyAppend(navmenu, A_BROWSEF, _("Open &file") + "\tF4",
                     _("Opens up the text from the selected cell in default application for the file type"));

            auto laymenu = new wxMenu();
            MyAppend(laymenu, A_V_GS,
                     _("Vertical Layout with Grid Style Rendering") + "\t" CTRLORALT "+1");
            MyAppend(laymenu, A_V_BS,
                     _("Vertical Layout with Bubble Style Rendering") + "\t" CTRLORALT "+2");
            MyAppend(laymenu, A_V_LS,
                     _("Vertical Layout with Line Style Rendering") + "\t" CTRLORALT "+3");
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_H_GS,
                     _("Horizontal Layout with Grid Style Rendering") + "\t" CTRLORALT "+4");
            MyAppend(laymenu, A_H_BS,
                     _("Horizontal Layout with Bubble Style Rendering") + "\t" CTRLORALT "+5");
            MyAppend(laymenu, A_H_LS,
                     _("Horizontal Layout with Line Style Rendering") + "\t" CTRLORALT "+6");
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_GS, _("Grid Style Rendering") + "\t" CTRLORALT "+7");
            MyAppend(laymenu, A_BS, _("Bubble Style Rendering") + "\t" CTRLORALT "+8");
            MyAppend(laymenu, A_LS, _("Line Style Rendering") + "\t" CTRLORALT "+9");
            laymenu->AppendSeparator();
            MyAppend(laymenu, A_TEXTGRID, _("Toggle Vertical Layout") + "\t" CTRLORALT "+0",
                     _("Make a hierarchy layout more vertical (default) or more horizontal"));

            editmenu = new wxMenu();
            MyAppend(editmenu, wxID_CUT, _("Cu&t") + "\tCTRL+X", _("Cut selection"));
            MyAppend(editmenu, wxID_COPY, _("&Copy") + "\tCTRL+C", _("Copy selection"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_COPYWI, _("Copy with &Images") + "\tCTRL+ALT+C");
            MyAppend(editmenu, A_COPYBM, _("&Copy as Bitmap"));
            MyAppend(editmenu, A_COPYCT, _("Copy As Continuous Text"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, wxID_PASTE, _("&Paste") + "\tCTRL+V", _("Paste clipboard contents"));
            MyAppend(editmenu, A_PASTESTYLE, _("Paste Style Only") + "\tCTRL+SHIFT+V",
                     _("only sets the colors and style of the copied cell, and keeps the text"));
            MyAppend(editmenu, A_COLLAPSE, _("Collapse Ce&lls") + "\tCTRL+L");
            editmenu->AppendSeparator();

            MyAppend(editmenu, A_EDITNOTE, _("Edit &Note") + "\tCTRL+E",
                     _("Edit the note of the selected cell"));
            MyAppend(editmenu, wxID_UNDO, _("&Undo") + "\tCTRL+Z",
                     _("revert the changes, one step at a time"));
            MyAppend(editmenu, wxID_REDO, _("&Redo") + "\tCTRL+Y",
                     _("redo any undo steps, if you haven't made changes since"));
            editmenu->AppendSeparator();
            MyAppend(
                editmenu, A_DELETE, _("&Delete After") + "\tDEL",
                _("Deletes the column of cells after the selected grid line, or the row below"));
            MyAppend(
                editmenu, A_BACKSPACE, _("Delete Before") + "\tBACK",
                _("Deletes the column of cells before the selected grid line, or the row above"));
            MyAppend(editmenu, A_DELETE_WORD, _("Delete Word After") + "\tCTRL+DEL",
                     _("Deletes the entire word after the cursor"));
            MyAppend(editmenu, A_BACKSPACE_WORD, _("Delete Word Before") + "\tCTRL+BACK",
                     _("Deletes the entire word before the cursor"));
            editmenu->AppendSeparator();
            MyAppend(editmenu, A_NEWGRID,
                     #ifdef __WXMAC__
                     _("&Insert New Grid") + "\tCTRL+G",
                     #else
                     _("&Insert New Grid") + "\tINS",
                     #endif
                     _("Adds a grid to the selected cell"));
            MyAppend(editmenu, A_WRAP, _("&Wrap in new parent") + "\tF9",
                     _("Creates a new level of hierarchy around the current selection"));
            editmenu->AppendSeparator();
            // F10 is tied to the OS on both Ubuntu and OS X, and SHIFT+F10 is now right
            // click on all platforms?
            MyAppend(editmenu, A_FOLD,
                     #ifndef WIN32
                     _("Toggle Fold") + "\tCTRL+F10",
                     #else
                     _("Toggle Fold") + "\tF10",
                     #endif
                     _("Toggles showing the grid of the selected cell(s)"));
            MyAppend(editmenu, A_FOLDALL, _("Fold All") + "\tCTRL+SHIFT+F10",
                     _("Folds the grid of the selected cell(s) recursively"));
            MyAppend(editmenu, A_UNFOLDALL, _("Unfold All") + "\tCTRL+ALT+F10",
                     _("Unfolds the grid of the selected cell(s) recursively"));
            editmenu->AppendSeparator();
            editmenu->AppendSubMenu(selmenu, _("&Selection"));
            editmenu->AppendSubMenu(orgmenu, _("&Grid Reorganization"));
            editmenu->AppendSubMenu(laymenu, _("&Layout && Render Style"));
            editmenu->AppendSubMenu(imgmenu, _("&Images"));
            editmenu->AppendSubMenu(navmenu, _("&Browsing"));
            editmenu->AppendSubMenu(temenu, _("Text &Editing"));
            editmenu->AppendSubMenu(sizemenu, _("Text Sizing"));
            editmenu->AppendSubMenu(stmenu, _("Text Style"));
            editmenu->AppendSubMenu(bordmenu, _("Set Grid Border Width"));
            editmenu->AppendSubMenu(tagmenu, _("Tag"));

            if (!twoeditmenus) editmenupopup = editmenu;
        }

        auto semenu = new wxMenu();
        MyAppend(semenu, wxID_FIND, _("&Search") + "\tCTRL+F", _("Find in document"));
        semenu->AppendCheckItem(A_CASESENSITIVESEARCH, _("Case-sensitive search"));
        semenu->Check(A_CASESENSITIVESEARCH, sys->casesensitivesearch);
        semenu->AppendSeparator();
        MyAppend(semenu, A_SEARCHNEXT, _("&Next Match") + "\tF3", _("Go to next search match"));
        MyAppend(semenu, A_SEARCHPREV, _("&Previous Match") + "\tSHIFT+F3",
                 _("Go to previous search match"));
        semenu->AppendSeparator();
        MyAppend(semenu, wxID_REPLACE, _("&Replace") + "\tCTRL+H",
                 _("Find and replace in document"));
        MyAppend(semenu, A_REPLACEONCE, _("Replace in Current &Selection") + "\tCTRL+K");
        MyAppend(semenu, A_REPLACEONCEJ,
                 _("Replace in Current Selection && &Jump Next") + "\tCTRL+J");
        MyAppend(semenu, A_REPLACEALL, _("Replace &All"));

        auto scrollmenu = new wxMenu();
        MyAppend(scrollmenu, A_AUP, _("Scroll Up (mousewheel)") + "\tPGUP");
        MyAppend(scrollmenu, A_AUP, _("Scroll Up (mousewheel)") + "\tALT+UP");
        MyAppend(scrollmenu, A_ADOWN, _("Scroll Down (mousewheel)") + "\tPGDN");
        MyAppend(scrollmenu, A_ADOWN, _("Scroll Down (mousewheel)") + "\tALT+DOWN");
        MyAppend(scrollmenu, A_ALEFT, _("Scroll Left") + "\tALT+LEFT");
        MyAppend(scrollmenu, A_ARIGHT, _("Scroll Right") + "\tALT+RIGHT");

        auto filtermenu = new wxMenu();
        MyAppend(filtermenu, A_FILTEROFF, _("Turn filter &off") + "\tCTRL+SHIFT+F");
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
        MyAppend(filtermenu, A_FILTERNOTE, _("Show cells with notes"));
        MyAppend(filtermenu, A_FILTERBYCELLBG, _("Filter by the same cell color"));
        MyAppend(filtermenu, A_FILTERMATCHNEXT, _("Go to next filter match") + "\tCTRL+F3");

        auto viewmenu = new wxMenu();
        MyAppend(viewmenu, A_ZOOMIN, _("Zoom &In (CTRL+mousewheel)") + "\tCTRL+PGUP");
        MyAppend(viewmenu, A_ZOOMOUT, _("Zoom &Out (CTRL+mousewheel)") + "\tCTRL+PGDN");
        viewmenu->AppendSeparator();
        MyAppend(
            viewmenu, A_NEXTFILE,
            _("&Next tab")
                 #ifndef __WXGTK__
                    // On Linux, this conflicts with CTRL+I, see Document::Key()
                    // CTRL+SHIFT+TAB below still works, so that will have to be used to switch tabs.
                     + "\tCTRL+TAB"
                 #endif
            ,
            _("Go to the document in the next tab"));
        MyAppend(viewmenu, A_PREVFILE, _("Previous tab") + "\tCTRL+SHIFT+TAB",
                 _("Go to the document in the previous tab"));
        viewmenu->AppendSeparator();
        MyAppend(viewmenu, A_FULLSCREEN,
                 #ifdef __WXMAC__
                 _("Toggle &Fullscreen View") + "\tCTRL+F11");
                 #else
                 _("Toggle &Fullscreen View") + "\tF11");
                 #endif
        MyAppend(viewmenu, A_SCALED,
                 #ifdef __WXMAC__
                 _("Toggle &Scaled Presentation View") + "\tCTRL+F12");
                 #else
                 _("Toggle &Scaled Presentation View") + "\tF12");
                 #endif
        viewmenu->AppendSeparator();
        viewmenu->AppendSubMenu(scrollmenu, _("Scroll Sheet"));
        viewmenu->AppendSubMenu(filtermenu, _("Filter"));

        auto roundmenu = new wxMenu();
        roundmenu->AppendRadioItem(A_ROUND0, _("Radius &0"));
        roundmenu->AppendRadioItem(A_ROUND1, _("Radius &1"));
        roundmenu->AppendRadioItem(A_ROUND2, _("Radius &2"));
        roundmenu->AppendRadioItem(A_ROUND3, _("Radius &3"));
        roundmenu->AppendRadioItem(A_ROUND4, _("Radius &4"));
        roundmenu->AppendRadioItem(A_ROUND5, _("Radius &5"));
        roundmenu->AppendRadioItem(A_ROUND6, _("Radius &6"));
        roundmenu->Check(sys->roundness + A_ROUND0, true);

        auto autoexportmenu = new wxMenu();
        autoexportmenu->AppendRadioItem(A_AUTOEXPORT_HTML_NONE, _("No autoexport"));
        autoexportmenu->AppendRadioItem(A_AUTOEXPORT_HTML_WITH_IMAGES, _("Export with images"),
                                        _("Export to a HTML file with exported images alongside "
                                          "the original TreeSheets file when document is saved"));
        autoexportmenu->AppendRadioItem(A_AUTOEXPORT_HTML_WITHOUT_IMAGES,
                                        _("Export without images"),
                                        _("Export to a HTML file alongside the original "
                                          "TreeSheets file when document is saved"));
        autoexportmenu->Check(sys->autohtmlexport + A_AUTOEXPORT_HTML_NONE, true);

        auto optmenu = new wxMenu();
        MyAppend(optmenu, wxID_SELECT_FONT, _("Font..."),
                 _("Set the font the document text is displayed with"));
        MyAppend(optmenu, A_SET_FIXED_FONT, _("Typewriter font..."),
                 _("Set the font the typewriter text is displayed with."));
        MyAppend(optmenu, A_CUSTKEY, _("Key bindings..."),
                 _("Change the key binding of a menu item"));
        MyAppend(optmenu, A_SETLANG, _("Change language..."), _("Change interface language"));
        MyAppend(optmenu, A_DEFAULTMAXCOLWIDTH, _("Default column width..."),
                 _("Set the default column width for a new grid"));
        optmenu->AppendSeparator();
        MyAppend(optmenu, A_CUSTCOL, _("Custom &color..."),
                 _("Set a custom color for the color dropdowns"));
        MyAppend(
            optmenu, A_COLCELL, _("&Set custom color from cell background"),
            _("Set a custom color for the color dropdowns from the selected cell background"));
        MyAppend(optmenu, A_DEFBGCOL, _("Background color..."),
                 _("Set the color for the document background"));
        MyAppend(optmenu, A_DEFCURCOL, _("Cu&rsor color..."),
                 _("Set the color for the text cursor"));
        optmenu->AppendSeparator();
        MyAppend(optmenu, A_RESETPERSPECTIVE, _("Reset toolbar"),
                 _("Reset the toolbar appearance"));
        optmenu->AppendCheckItem(
            A_SHOWTBAR, _("Toolbar"),
            _("Toggle whether toolbar is shown between menu bar and documents"));
        optmenu->Check(A_SHOWTBAR, sys->showtoolbar);
        optmenu->AppendCheckItem(A_SHOWSBAR, _("Statusbar"),
                                 _("Toggle whether statusbar is shown below the documents"));
        optmenu->Check(A_SHOWSBAR, sys->showstatusbar);
        optmenu->AppendCheckItem(
            A_LEFTTABS, _("File Tabs on the bottom"),
            _("Toggle whether file tabs are shown on top or on bottom of the documents"));
        optmenu->Check(A_LEFTTABS, lefttabs);
        optmenu->AppendCheckItem(A_TOTRAY, _("Minimize to tray"),
                                 _("Toogle whether window is minimized to system tray"));
        optmenu->Check(A_TOTRAY, sys->totray);
        optmenu->AppendCheckItem(A_MINCLOSE, _("Minimize on close"),
                                 _("Toggle whether the window is minimized instead of closed"));
        optmenu->Check(A_MINCLOSE, sys->minclose);
        optmenu->AppendCheckItem(
            A_SINGLETRAY, _("Single click maximize from tray"),
            _("Toggle whether only one click is required to maximize from system tray"));
        optmenu->Check(A_SINGLETRAY, sys->singletray);
        optmenu->AppendCheckItem(A_STARTMINIMIZED, _("Start minimized"),
                                 _("Start the application minimized"));
        optmenu->Check(A_STARTMINIMIZED, sys->startminimized);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_ZOOMSCR, _("Swap mousewheel scrolling and zooming"));
        optmenu->Check(A_ZOOMSCR, sys->zoomscroll);
        optmenu->AppendCheckItem(A_THINSELC, _("Navigate in between cells with cursor keys"),
                                 _("Toggle whether the cursor keys are used for navigation in addition to text editing"));
        optmenu->Check(A_THINSELC, sys->thinselc);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_MAKEBAKS, _("Backup files"),
                                 _("Create backup file before document is saved to file"));
        optmenu->Check(A_MAKEBAKS, sys->makebaks);
        optmenu->AppendCheckItem(A_AUTOSAVE, _("Autosave"),
                                 _("Save open documents periodically to temporary files"));
        optmenu->Check(A_AUTOSAVE, sys->autosave);
        optmenu->AppendCheckItem(
            A_FSWATCH, _("Autoreload documents"),
            _("Reload when another computer has changed a file (if you have made changes, asks)"));
        optmenu->Check(A_FSWATCH, sys->fswatch);
        optmenu->AppendSubMenu(autoexportmenu, _("Autoexport to HTML"));
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(
            A_CENTERED, _("Render document centered"),
            _("Toggle whether documents are rendered centered or left aligned"));
        optmenu->Check(A_CENTERED, sys->centered);
        optmenu->AppendCheckItem(
            A_FASTRENDER, _("Faster line rendering"),
            _("Toggle whether lines are drawn solid (faster rendering) or dashed"));
        optmenu->Check(A_FASTRENDER, sys->fastrender);
        optmenu->AppendCheckItem(A_INVERTRENDER, _("Invert in dark mode"),
                                 _("Invert the document in dark mode"));
        optmenu->Check(A_INVERTRENDER, sys->followdarkmode);
        optmenu->AppendSubMenu(roundmenu, _("&Roundness of grid borders"));

        #ifdef ENABLE_LOBSTER
            auto scriptmenu = new wxMenu();
            MyAppend(scriptmenu, A_ADDSCRIPT, _("Add...") + "\tCTRL+ALT+L",
                     _("Add Lobster scripts to the menu"));
            MyAppend(scriptmenu, A_DETSCRIPT, _("Remove...") + "\tCTRL+SHIFT+ALT+L",
                     _("Remove script from list in the menu"));
            scripts.UseMenu(scriptmenu);
            scripts.SetMenuPathStyle(wxFH_PATH_SHOW_NEVER);
            scripts.AddFilesToMenu();

            auto scriptpath = app->GetDataPath("scripts/");
            auto sf = wxFindFirstFile(scriptpath + "*.lobster");
            int sidx = 0;
            while (!sf.empty()) {
                auto fn = wxFileName::FileName(sf).GetFullName();
                scripts.AddFileToHistory(fn);
                sf = wxFindNextFile();
            }
        #endif

        auto markmenu = new wxMenu();
        MyAppend(markmenu, A_MARKDATA, _("&Data") + "\tCTRL+ALT+D");
        MyAppend(markmenu, A_MARKCODE, _("&Operation") + "\tCTRL+ALT+O");
        MyAppend(markmenu, A_MARKVARD, _("Variable &Assign") + "\tCTRL+ALT+A");
        MyAppend(markmenu, A_MARKVARU, _("Variable &Read") + "\tCTRL+ALT+R");
        MyAppend(markmenu, A_MARKVIEWH, _("&Horizontal View") + "\tCTRL+ALT+.");
        MyAppend(markmenu, A_MARKVIEWV, _("&Vertical View") + "\tCTRL+ALT+,");

        auto langmenu = new wxMenu();
        MyAppend(langmenu, wxID_EXECUTE, _("&Run") + "\tCTRL+ALT+F5");
        langmenu->AppendSubMenu(markmenu, _("&Mark as"));
        MyAppend(langmenu, A_CLRVIEW, _("&Clear Views"));

        auto helpmenu = new wxMenu();
        MyAppend(helpmenu, wxID_ABOUT, _("&About..."), _("Show About dialog"));
        helpmenu->AppendSeparator();
        MyAppend(helpmenu, wxID_HELP, _("Interactive &tutorial") + "\tF1",
                 _("Load an interactive tutorial in TreeSheets"));
        MyAppend(helpmenu, A_HELP_OP_REF, _("Operation reference") + "\tCTRL+ALT+F1",
                 _("Load an interactive program operation reference in TreeSheets"));
        helpmenu->AppendSeparator();
        MyAppend(helpmenu, A_TUTORIALWEBPAGE, _("Tutorial &web page"),
                 _("Open the tutorial web page in browser"));
        #ifdef ENABLE_LOBSTER
            MyAppend(helpmenu, A_SCRIPTREFERENCE, _("&Script reference"),
                 _("Open the Lobster script reference in browser"));
        #endif

        wxAcceleratorEntry entries[3];
        entries[0].Set(wxACCEL_SHIFT, WXK_DELETE, wxID_CUT);
        entries[1].Set(wxACCEL_SHIFT, WXK_INSERT, wxID_PASTE);
        entries[2].Set(wxACCEL_CTRL, WXK_INSERT, wxID_COPY);
        wxAcceleratorTable accel(3, entries);
        SetAcceleratorTable(accel);

        auto menubar = new wxMenuBar();
        menubar->Append(filemenu, _("&File"));
        menubar->Append(editmenu, _("&Edit"));
        menubar->Append(semenu, _("&Search"));
        menubar->Append(viewmenu, _("&View"));
        menubar->Append(optmenu, _("&Options"));
        #ifdef ENABLE_LOBSTER
            menubar->Append(scriptmenu, _("S&cript"));
        #endif
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

        RefreshToolBar();

        auto sb = CreateStatusBar(5);
        SetStatusBarPane(0);
        SetDPIAwareStatusWidths();
        sb->Show(sys->showstatusbar);

        notebook =
            new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
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

        aui.AddPane(
            notebook,
            wxAuiPaneInfo().Name("notebook").Caption("Notebook").CenterPane().PaneBorder(false));
        aui.LoadPerspective(sys->cfg->Read("perspective", ""));
        aui.Update();

        Show(!IsIconized());

        // needs to be after Show() to avoid scrollbars rendered in the wrong place?
        if (ismax && !IsIconized()) Maximize(true);

        if (sys->startminimized)
            #ifdef __WXGTK__
                CallAfter([this]() { Iconize(true); });
            #else
                Iconize(true);
            #endif

        SetFileAssoc(app->exename);

        wxSafeYield();
    }

    wxArrayString GetToolbarPaneNames() {
        wxArrayString toolbarNames;
        wxAuiPaneInfoArray &all_panes = aui.GetAllPanes();
        for (size_t i = 0; i < all_panes.GetCount(); ++i) {
            wxAuiPaneInfo &pane = all_panes.Item(i);
            if (pane.IsToolbar()) { toolbarNames.Add(pane.name); }
        }
        return toolbarNames;
    }

    void DestroyToolbarPane(const wxString &name) {
        wxAuiPaneInfo &pane = aui.GetPane(name);
        if (pane.IsOk()) {
            wxWindow *wnd = pane.window;
            aui.DetachPane(wnd);
            if (wnd) { wnd->Destroy(); }
        }
    }

    void RefreshToolBar() {
        for (const auto &name : GetToolbarPaneNames()) { DestroyToolbarPane(name); }
        auto iconpath = app->GetDataPath("images/material/toolbar/");
        auto AddToolbarIcon = [&](wxAuiToolBar *tb, const wxChar *name, int action,
                                  wxString iconpath, wxString lighticon, wxString darkicon) {
            tb->AddTool(
                action, name,
                wxBitmapBundle::FromSVGFile(
                    iconpath + (wxSystemSettings::GetAppearance().IsDark() ? darkicon : lighticon),
                    wxSize(24, 24)),
                name, wxITEM_NORMAL);
        };

        auto filetb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        AddToolbarIcon(filetb, _("New (CTRL+n)"), wxID_NEW, iconpath, "filenew.svg",
                       "filenew_dark.svg");
        AddToolbarIcon(filetb, _("Open (CTRL+o)"), wxID_OPEN, iconpath, "fileopen.svg",
                       "fileopen_dark.svg");
        AddToolbarIcon(filetb, _("Save (CTRL+s)"), wxID_SAVE, iconpath, "filesave.svg",
                       "filesave_dark.svg");
        AddToolbarIcon(filetb, _("Save as..."), wxID_SAVEAS, iconpath, "filesaveas.svg",
                       "filesaveas_dark.svg");
        filetb->Realize();

        auto edittb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        AddToolbarIcon(edittb, _("Undo (CTRL+z)"), wxID_UNDO, iconpath, "undo.svg",
                       "undo_dark.svg");
        AddToolbarIcon(edittb, _("Copy (CTRL+c)"), wxID_COPY, iconpath, "editcopy.svg",
                       "editcopy_dark.svg");
        AddToolbarIcon(edittb, _("Paste (CTRL+v)"), wxID_PASTE, iconpath, "editpaste.svg",
                       "editpaste_dark.svg");
        edittb->Realize();

        auto zoomtb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        AddToolbarIcon(zoomtb, _("Zoom In (CTRL+mousewheel)"), A_ZOOMIN, iconpath, "zoomin.svg",
                       "zoomin_dark.svg");
        AddToolbarIcon(zoomtb, _("Zoom Out (CTRL+mousewheel)"), A_ZOOMOUT, iconpath, "zoomout.svg",
                       "zoomout_dark.svg");
        zoomtb->Realize();

        auto celltb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        AddToolbarIcon(celltb, _("New Grid (INS)"), A_NEWGRID, iconpath, "newgrid.svg",
                       "newgrid_dark.svg");
        AddToolbarIcon(celltb, _("Add Image"), A_IMAGE, iconpath, "image.svg", "image_dark.svg");
        AddToolbarIcon(celltb, _("Run"), wxID_EXECUTE, iconpath, "run.svg", "run_dark.svg");
        celltb->Realize();

        auto findtb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        findtb->AddControl(new wxStaticText(findtb, wxID_ANY, _("Search ")));
        findtb->AddControl(filter = new wxTextCtrl(findtb, A_SEARCH, "", wxDefaultPosition,
                                                   FromDIP(wxSize(80, 22)),
                                                   wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddToolbarIcon(findtb, _("Clear search"), A_CLEARSEARCH, iconpath, "cancel.svg",
                       "cancel_dark.svg");
        AddToolbarIcon(findtb, _("Go to Next Search Result"), A_SEARCHNEXT, iconpath, "search.svg",
                       "search_dark.svg");
        findtb->Realize();

        auto repltb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        repltb->AddControl(new wxStaticText(repltb, wxID_ANY, _("Replace ")));
        repltb->AddControl(replaces = new wxTextCtrl(repltb, A_REPLACE, "", wxDefaultPosition,
                                                     FromDIP(wxSize(80, 22)),
                                                     wxWANTS_CHARS | wxTE_PROCESS_ENTER));
        AddToolbarIcon(repltb, _("Clear replace"), A_CLEARREPLACE, iconpath, "cancel.svg",
                       "cancel_dark.svg");
        AddToolbarIcon(repltb, _("Replace in selection"), A_REPLACEONCE, iconpath, "replace.svg",
                       "replace_dark.svg");
        AddToolbarIcon(repltb, _("Replace All"), A_REPLACEALL, iconpath, "replaceall.svg",
                       "replaceall_dark.svg");
        repltb->Realize();

        auto GetColorIndex = [&](int targetcolor, int defaultindex) {
            for (auto i = 1; i < celltextcolors.size(); ++i) {
                if (celltextcolors[i] == targetcolor) return i;
            }
            if (sys->customcolor == targetcolor) return 0;
            return defaultindex;
        };

        auto cellcolortb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        cellcolortb->AddControl(new wxStaticText(cellcolortb, wxID_ANY, _("Cell ")));

        cellcolordropdown =
            new ColorDropdown(cellcolortb, A_CELLCOLOR, GetColorIndex(sys->lastcellcolor, 1));
        cellcolortb->AddControl(cellcolordropdown);
        cellcolortb->Realize();

        auto textcolortb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        textcolortb->AddControl(new wxStaticText(textcolortb, wxID_ANY, _("Text ")));
        textcolordropdown =
            new ColorDropdown(textcolortb, A_TEXTCOLOR, GetColorIndex(sys->lasttextcolor, 2));
        textcolortb->AddControl(textcolordropdown);
        textcolortb->Realize();

        auto bordercolortb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                              wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        bordercolortb->AddControl(new wxStaticText(bordercolortb, wxID_ANY, _("Border ")));
        bordercolordropdown =
            new ColorDropdown(bordercolortb, A_BORDCOLOR, GetColorIndex(sys->lastbordcolor, 7));
        bordercolortb->AddControl(bordercolordropdown);
        bordercolortb->Realize();

        auto imagetb = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                        wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
        imagetb->AddControl(new wxStaticText(imagetb, wxID_ANY, _("Image ")));
        imagedropdown = new ImageDropdown(imagetb, imagepath);
        imagetb->AddControl(imagedropdown);
        imagetb->Realize();

        aui.AddPane(filetb, wxAuiPaneInfo()
                                .Name("filetb")
                                .Caption("File operations")
                                .ToolbarPane()
                                .Top()
                                .Row(0)
                                .LeftDockable(false)
                                .RightDockable(false)
                                .Gripper(true));
        aui.AddPane(edittb, wxAuiPaneInfo()
                                .Name("edittb")
                                .Caption("Edit operations")
                                .ToolbarPane()
                                .Top()
                                .Row(0)
                                .LeftDockable(false)
                                .RightDockable(false)
                                .Gripper(true));
        aui.AddPane(zoomtb, wxAuiPaneInfo()
                                .Name("zoomtb")
                                .Caption("Zoom operations")
                                .ToolbarPane()
                                .Top()
                                .Row(0)
                                .LeftDockable(false)
                                .RightDockable(false)
                                .Gripper(true));
        aui.AddPane(celltb, wxAuiPaneInfo()
                                .Name("celltb")
                                .Caption("Cell operations")
                                .ToolbarPane()
                                .Top()
                                .Row(0)
                                .LeftDockable(false)
                                .RightDockable(false)
                                .Gripper(true));
        aui.AddPane(findtb, wxAuiPaneInfo()
                                .Name("findtb")
                                .Caption("Find operations")
                                .ToolbarPane()
                                .Top()
                                .Row(0)
                                .LeftDockable(false)
                                .RightDockable(false)
                                .Gripper(true));
        aui.AddPane(repltb, wxAuiPaneInfo()
                                .Name("repltb")
                                .Caption("Replace operations")
                                .ToolbarPane()
                                .Top()
                                .Row(0)
                                .LeftDockable(false)
                                .RightDockable(false)
                                .Gripper(true));
        aui.AddPane(cellcolortb, wxAuiPaneInfo()
                                     .Name("cellcolortb")
                                     .Caption("Cell color operations")
                                     .ToolbarPane()
                                     .Top()
                                     .Row(0)
                                     .LeftDockable(false)
                                     .RightDockable(false)
                                     .Gripper(true));
        aui.AddPane(textcolortb, wxAuiPaneInfo()
                                     .Name("textcolortb")
                                     .Caption("Text color operations")
                                     .ToolbarPane()
                                     .Top()
                                     .Row(0)
                                     .LeftDockable(false)
                                     .RightDockable(false)
                                     .Gripper(true));
        aui.AddPane(bordercolortb, wxAuiPaneInfo()
                                       .Name("bordercolortb")
                                       .Caption("Border color operations")
                                       .ToolbarPane()
                                       .Top()
                                       .Row(0)
                                       .LeftDockable(false)
                                       .RightDockable(false)
                                       .Gripper(true));
        aui.AddPane(imagetb, wxAuiPaneInfo()
                                 .Name("imagetb")
                                 .Caption("Image operations")
                                 .ToolbarPane()
                                 .Top()
                                 .Row(0)
                                 .LeftDockable(false)
                                 .RightDockable(false)
                                 .Gripper(true));
        auto artprovider = aui.GetArtProvider();
        artprovider->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
        artprovider->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);
    }

    void AppOnEventLoopEnter() {
        watcher = new wxFileSystemWatcher();
        watcher->SetOwner(this);
        Connect(wxEVT_FSWATCHER, wxFileSystemWatcherEventHandler(TSFrame::OnFileSystemEvent));
    }

    // event handling functions

    void OnMenu(wxCommandEvent &ce) {
        wxTextCtrl *tc;
        auto canvas = GetCurrentTab();
        if ((tc = filter) && filter == wxWindow::FindFocus() ||
            (tc = replaces) && replaces == wxWindow::FindFocus()) {
            long from, to;
            tc->GetSelection(&from, &to);
            switch (ce.GetId()) {
                #if defined(__WXMSW__) || defined(__WXMAC__)
                // FIXME: have to emulate this behavior on Windows and Mac because menu always captures these events (??)
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
                #endif
                #ifdef __WXMSW__
                case A_ENTERCELL: {
                    if (tc == filter) {
                        // OnSearchEnter equivalent implementation for MSW
                        // as EVT_TEXT_ENTER event is not generated.
                        if (sys->searchstring.IsEmpty()) {
                            canvas->SetFocus();
                        } else {
                            canvas->doc->Action(A_SEARCHNEXT);
                        }
                    } else if (tc == replaces) {
                        // OnReplaceEnter equivalent implementation for MSW
                        // as EVT_TEXT_ENTER event is not generated.
                        canvas->doc->Action(A_REPLACEONCEJ);
                    }
                    return;
                }
                #endif
                case A_CANCELEDIT:
                    tc->Clear();
                    canvas->SetFocus();
                    return;
            }
        }
        auto Check = [&](const wxString &cfg) {
            sys->cfg->Write(cfg, ce.IsChecked());
            SetStatus(_("change will take effect next run of TreeSheets"));
        };
        switch (ce.GetId()) {
            case A_NOP: break;

            case A_ALEFT: canvas->CursorScroll(-g_scrollratecursor, 0); break;
            case A_ARIGHT: canvas->CursorScroll(g_scrollratecursor, 0); break;
            case A_AUP: canvas->CursorScroll(0, -g_scrollratecursor); break;
            case A_ADOWN: canvas->CursorScroll(0, g_scrollratecursor); break;
            case A_RESETPERSPECTIVE:
                RefreshToolBar();
                sys->showtoolbar = true;
                aui.Update();
                break;
            case A_SHOWSBAR:
                if (!IsFullScreen()) {
                    sys->cfg->Write("showstatusbar", sys->showstatusbar = ce.IsChecked());
                    auto wsb = GetStatusBar();
                    wsb->Show(sys->showstatusbar);
                    SendSizeEvent();
                    Refresh();
                    wsb->Refresh();
                }
                break;
            case A_SHOWTBAR:
                if (!IsFullScreen()) {
                    sys->cfg->Write("showtoolbar", sys->showtoolbar = ce.IsChecked());
                    for (const auto &name : GetToolbarPaneNames()) {
                        if (sys->showtoolbar)
                            aui.GetPane(name).Show();
                        else
                            aui.GetPane(name).Hide();
                    }
                    aui.Update();
                }
                break;
            case A_CUSTCOL: {
                if (auto color = PickColor(sys->frame, sys->customcolor); color != (uint)-1)
                    sys->cfg->Write("customcolor", sys->customcolor = color);
                break;
            }

            #ifdef ENABLE_LOBSTER
                case A_ADDSCRIPT: {
                    wxArrayString filenames;
                    GetFilesFromUser(filenames, this, _("Please select Lobster script file(s):"),
                                     _("Lobster Files (*.lobster)|*.lobster|All Files (*.*)|*.*"));
                    for (auto &filename : filenames) scripts.AddFileToHistory(filename);
                    break;
                }

                case A_DETSCRIPT: {
                    wxArrayString filenames;
                    for (int i = 0, n = scripts.GetCount(); i < n; i++) {
                        filenames.Add(scripts.GetHistoryFile(i));
                    }
                    auto dialog = wxSingleChoiceDialog(
                        this, _("Please select the script you want to remove from the list:"),
                        _("Remove script from list..."), filenames);
                    if (dialog.ShowModal() == wxID_OK)
                        scripts.RemoveFileFromHistory(dialog.GetSelection());
                    break;
                }
            #endif

            case A_DEFAULTMAXCOLWIDTH: {
                int w = wxGetNumberFromUser(_("Please enter the default column width:"),
                                            _("Width"), _("Default column width"),
                                            sys->defaultmaxcolwidth, 1, 1000, sys->frame);
                if (w > 0) sys->cfg->Write("defaultmaxcolwidth", sys->defaultmaxcolwidth = w);
                break;
            }

            case A_LEFTTABS: Check("lefttabs"); break;
            case A_SINGLETRAY: Check("singletray"); break;
            case A_MAKEBAKS: sys->cfg->Write("makebaks", sys->makebaks = ce.IsChecked()); break;
            case A_TOTRAY: sys->cfg->Write("totray", sys->totray = ce.IsChecked()); break;
            case A_MINCLOSE: sys->cfg->Write("minclose", sys->minclose = ce.IsChecked()); break;
            case A_STARTMINIMIZED:
                sys->cfg->Write("startminimized", sys->startminimized = ce.IsChecked());
                break;
            case A_ZOOMSCR: sys->cfg->Write("zoomscroll", sys->zoomscroll = ce.IsChecked()); break;
            case A_THINSELC: sys->cfg->Write("thinselc", sys->thinselc = ce.IsChecked()); break;
            case A_AUTOSAVE: sys->cfg->Write("autosave", sys->autosave = ce.IsChecked()); break;
            case A_CENTERED:
                sys->cfg->Write("centered", sys->centered = ce.IsChecked());
                Refresh();
                break;
            case A_FSWATCH:
                Check("fswatch");
                sys->fswatch = ce.IsChecked();
                break;
            case A_AUTOEXPORT_HTML_NONE:
            case A_AUTOEXPORT_HTML_WITH_IMAGES:
            case A_AUTOEXPORT_HTML_WITHOUT_IMAGES:
                sys->cfg->Write(
                    "autohtmlexport",
                    static_cast<long>(sys->autohtmlexport = ce.GetId() - A_AUTOEXPORT_HTML_NONE));
                break;
            case A_FASTRENDER:
                sys->cfg->Write("fastrender", sys->fastrender = ce.IsChecked());
                Refresh();
                break;
            case A_INVERTRENDER:
                sys->cfg->Write("followdarkmode", sys->followdarkmode = ce.IsChecked());
                sys->colormask = (sys->followdarkmode && wxSystemSettings::GetAppearance().IsDark())
                                     ? 0x00FFFFFF
                                     : 0;
                Refresh();
                break;
            case A_FULLSCREEN:
                ShowFullScreen(!IsFullScreen());
                if (IsFullScreen()) SetStatus(_("Press F11 to exit fullscreen mode."));
                break;
            case wxID_FIND:
                if (filter) {
                    filter->SetFocus();
                    filter->SetSelection(0, 1000);
                } else {
                    SetStatus(_("Please enable (Options -> Show Toolbar) to use search."));
                }
                break;
            case wxID_REPLACE:
                if (replaces) {
                    replaces->SetFocus();
                    replaces->SetSelection(0, 1000);
                } else {
                    SetStatus(_("Please enable (Options -> Show Toolbar) to use replace."));
                }
                break;
            #ifdef __WXMAC__
                case wxID_OSX_HIDE: Iconize(true); break;
                case wxID_OSX_HIDEOTHERS: SetStatus("NOT IMPLEMENTED"); break;
                case wxID_OSX_SHOWALL: Iconize(false); break;
                case wxID_ABOUT: canvas->doc->Action(wxID_ABOUT); break;
                case wxID_PREFERENCES: canvas->doc->Action(wxID_SELECT_FONT); break;
            #endif
            case wxID_EXIT:
                fromclosebox = false;
                Close();
                break;
            case wxID_CLOSE:
                canvas->doc->Action(ce.GetId());
                break;  // canvas dangling pointer on return
            default:
                if (ce.GetId() >= wxID_FILE1 && ce.GetId() <= wxID_FILE9) {
                    wxString filename(filehistory.GetHistoryFile(ce.GetId() - wxID_FILE1));
                    SetStatus(sys->Open(filename));
                #ifdef ENABLE_LOBSTER
                    } else if (ce.GetId() >= A_TAGSET && ce.GetId() < A_SCRIPT) {
                        SetStatus(canvas->doc->TagSet(ce.GetId() - A_TAGSET));
                    } else if (ce.GetId() >= A_SCRIPT && ce.GetId() < A_MAXACTION) {
                        auto message =
                            tssi.ScriptRun(scripts.GetHistoryFile(ce.GetId() - A_SCRIPT).c_str());
                        message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
                        SetStatus(wxString(message));
                #else
                    } else if (ce.GetId() >= A_TAGSET && ce.GetId() < A_MAXACTION) {
                        SetStatus(canvas->doc->TagSet(ce.GetId() - A_TAGSET));
                #endif
                } else {
                    SetStatus(canvas->doc->Action(ce.GetId()));
                    break;
                }
        }
    }

    void OnTabChange(wxAuiNotebookEvent &nbe) {
        auto canvas = static_cast<TSCanvas *>(notebook->GetPage(nbe.GetSelection()));
        ClearStatus();
        sys->TabChange(canvas->doc.get());
        nbe.Skip();
    }

    void OnTabClose(wxAuiNotebookEvent &nbe) {
        auto canvas = static_cast<TSCanvas *>(notebook->GetPage(nbe.GetSelection()));
        if (notebook->GetPageCount() <= 1) {
            nbe.Veto();
            Close();
        } else if (canvas->doc->CloseDocument()) {
            nbe.Veto();
        } else {
            nbe.Skip();
        }
    }

    void OnSearch(wxCommandEvent &ce) {
        auto searchstring = ce.GetString();
        sys->darkennonmatchingcells = searchstring.Len() != 0;
        sys->searchstring = sys->casesensitivesearch ? searchstring : searchstring.Lower();
        TSCanvas *canvas = GetCurrentTab();
        Document *doc = canvas->doc.get();
        if (doc->searchfilter) {
            doc->SetSearchFilter(sys->searchstring.Len() != 0);
            doc->searchfilter = true;
        }
        canvas->Refresh();
    }

    void OnSearchReplaceEnter(wxCommandEvent &ce) {
        auto canvas = GetCurrentTab();
        if (ce.GetId() == A_SEARCH && ce.GetString().IsEmpty())
            canvas->SetFocus();
        else
            canvas->doc->Action(ce.GetId() == A_SEARCH ? A_SEARCHNEXT : A_REPLACEONCEJ);
    }

    void OnChangeColor(wxCommandEvent &ce) {
        GetCurrentTab()->doc->ColorChange(ce.GetId(), ce.GetInt());
        ReFocus();
    }

    void OnDDImage(wxCommandEvent &ce) {
        GetCurrentTab()->doc->ImageChange(imagedropdown->filenames[ce.GetInt()], dd_icon_res_scale);
        ReFocus();
    }

    void OnActivate(wxActivateEvent &ae) {
        // This causes warnings in the debug log, but without it keyboard entry upon window select
        // doesn't work.
        ReFocus();
    }

    void OnSizing(wxSizeEvent &se) { se.Skip(); }

    void OnMaximize(wxMaximizeEvent &me) {
        ReFocus();
        me.Skip();
    }

    void OnIconize(wxIconizeEvent &me) {
        if (me.IsIconized()) {
            #ifndef __WXMAC__
            if (sys->totray) {
                taskbaricon.SetIcon(icon, "TreeSheets");
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
            if (TSCanvas *canvas = GetCurrentTab()) canvas->SetFocus();
        }
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
        if (ce.CanVeto()) {
            // ask to save/discard all files before closing any
            loop(i, notebook->GetPageCount()) {
                auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
                if (canvas->doc->modified) {
                    notebook->SetSelection(i);
                    if (canvas->doc->CheckForChanges()) {
                        ce.Veto();
                        return;
                    }
                }
            }
            // all files have been saved/discarded
            while (notebook->GetPageCount()) {
                GetCurrentTab()->doc->RemoveTmpFile();
                notebook->DeletePage(notebook->GetSelection());
            }
        }
        sys->every_second_timer.Stop();
        filehistory.Save(*sys->cfg);
        #ifdef ENABLE_LOBSTER
            auto oldpath = sys->cfg->GetPath();
            sys->cfg->SetPath("/scripts");
            scripts.Save(*sys->cfg);
            sys->cfg->SetPath(oldpath);
        #endif
        if (!IsIconized()) {
            sys->cfg->Write("maximized", IsMaximized());
            if (!IsMaximized()) {
                sys->cfg->Write("resx", GetSize().x);
                sys->cfg->Write("resy", GetSize().y);
                sys->cfg->Write("posx", GetPosition().x);
                sys->cfg->Write("posy", GetPosition().y);
            }
        }
        sys->cfg->Write("notesizex", sys->notesizex);
        sys->cfg->Write("notesizey", sys->notesizey);
        sys->cfg->Write("perspective", aui.SavePerspective());
        sys->cfg->Write("lastcellcolor", sys->lastcellcolor);
        sys->cfg->Write("lasttextcolor", sys->lasttextcolor);
        sys->cfg->Write("lastbordcolor", sys->lastbordcolor);
        aui.ClearEventHashTable();
        aui.UnInit();
        DELETEP(editmenupopup);
        DELETEP(watcher);
        Destroy();
    }

    void OnFileSystemEvent(wxFileSystemWatcherEvent &event) {
        // 0xF == create/delete/rename/modify
        if ((event.GetChangeType() & 0xF) == 0 || watcherwaitingforuser || !notebook) return;
        const auto &modfile = event.GetPath().GetFullPath();
        loop(i, notebook->GetPageCount()) {
            Document *doc = static_cast<TSCanvas *>(notebook->GetPage(i))->doc.get();
            if (modfile == doc->filename) {
                auto modtime = wxFileName(modfile).GetModificationTime();
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
                    auto message = wxString::Format(
                        _("%s\nhas been modified on disk by another program / computer:\nWould you like to discard your changes and re-load from disk?"),
                        doc->filename);
                    watcherwaitingforuser = true;
                    int res = wxMessageBox(message, _("File modification conflict!"),
                                           wxYES_NO | wxICON_QUESTION, this);
                    watcherwaitingforuser = false;
                    if (res != wxYES) return;
                }
                auto message = sys->LoadDB(doc->filename, true, i);
                if (!message.IsEmpty()) {
                    SetStatus(message);
                } else {
                    notebook->DeletePage(i + 1);
                    ::wxRemoveFile(sys->TmpName(modfile));
                    SetStatus(
                        _("File has been re-loaded because of modifications of another program / computer"));
                }
                return;
            }
        }
    }

    void OnDPIChanged(wxDPIChangedEvent &dce) {
        // block all other events until we finished preparing
        wxEventBlocker blocker(this);
        wxBusyCursor wait;
        {
            ThreadPool pool(std::thread::hardware_concurrency());
            for (const auto &image : sys->imagelist) {
                pool.enqueue(
                    [](auto img) {
                        img->bm_display = wxNullBitmap;
                        img->Display();
                    },
                    image.get());
            }
        }  // wait until all tasks are finished
        RenderFolderIcon();
        dce.Skip();
    }

    void OnSysColourChanged(wxSysColourChangedEvent &se) {
        sys->colormask =
            (sys->followdarkmode && wxSystemSettings::GetAppearance().IsDark()) ? 0x00FFFFFF : 0;
        auto perspective = aui.SavePerspective();
        RefreshToolBar();
        aui.LoadPerspective(perspective);
        aui.Update();
        se.Skip();
    }

    // helper functions

    void CycleTabs(int offset = 1) {
        auto numtabs = static_cast<int>(notebook->GetPageCount());
        offset = offset >= 0 ? 1 : numtabs - 1;  // normalize to non-negative wrt modulo
        notebook->SetSelection((notebook->GetSelection() + offset) % numtabs);
    }

    void DeIconize() {
        if (!IsIconized()) {
            RequestUserAttention();
            return;
        }
        Show(true);
        Iconize(false);
        taskbaricon.RemoveIcon();
    }

    TSCanvas *GetCurrentTab() {
        return notebook ? static_cast<TSCanvas *>(notebook->GetCurrentPage()) : nullptr;
    }

    TSCanvas *GetTabByFileName(const wxString &filename) {
        if (notebook) loop(i, notebook->GetPageCount()) {
                auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
                if (canvas->doc->filename == filename) {
                    notebook->SetSelection(i);
                    return canvas;
                }
            }
        return nullptr;
    }

    void MyAppend(wxMenu *menu, int tag, const wxString &contents, const wxString &help = "") {
        auto item = contents;
        wxString key = "";
        if (int pos = contents.Find("\t"); pos >= 0) {
            item = contents.Mid(0, pos);
            key = contents.Mid(pos + 1);
        }
        key = sys->cfg->Read(item, key);
        auto newcontents = item;
        if (key.Length()) newcontents += "\t" + key;
        menu->Append(tag, newcontents, help);
        menustrings[item] = key;
    }

    TSCanvas *NewTab(unique_ptr<Document> doc, bool append = false, int insert_at = -1) {
        TSCanvas *canvas = new TSCanvas(this, notebook);
        doc->canvas = canvas;
        canvas->doc = std::move(doc);
        canvas->SetScrollRate(1, 1);
        if (insert_at >= 0)
            notebook->InsertPage(insert_at, canvas, _("<unnamed>"), true, wxNullBitmap);
        else if (append)
            notebook->AddPage(canvas, _("<unnamed>"), true, wxNullBitmap);
        else
            notebook->InsertPage(0, canvas, _("<unnamed>"), true, wxNullBitmap);
        canvas->SetDropTarget(new DropTarget(canvas->doc->dndobjc));
        canvas->SetFocus();
        return canvas;
    }

    void ReFocus() {
        if (TSCanvas *canvas = GetCurrentTab()) canvas->SetFocus();
    }

    void RenderFolderIcon() {
        wxImage foldiconi;
        foldiconi.LoadFile(app->GetDataPath("images/nuvola/fold.png"));
        foldicon = wxBitmap(foldiconi);
        ScaleBitmap(foldicon, FromDIP(1.0) / 3.0, foldicon);
    }

    void SetDPIAwareStatusWidths() {
        int statusbarfieldwidths[] = {-1, FromDIP(300), FromDIP(120), FromDIP(100), FromDIP(150)};
        SetStatusWidths(5, statusbarfieldwidths);
    }

    void SetFileAssoc(const wxString &exename) {
        #ifdef WIN32
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\.cts", "TreeSheets");
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets", "TreeSheets file");
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\Shell\\Open\\Command",
                       "\"" + exename + "\" \"%1\"");
        SetRegistryKey("HKEY_CURRENT_USER\\Software\\Classes\\TreeSheets\\DefaultIcon",
                       "\"" + exename + "\",0");
        #else
        // TODO: do something similar for mac/kde/gnome?
        #endif
    }

    void SetPageTitle(const wxString &filename, wxString mods, int page = -1) {
        if (page < 0) page = notebook->GetSelection();
        if (page < 0) return;
        if (page == notebook->GetSelection()) SetTitle("TreeSheets - " + filename + mods);
        notebook->SetPageText(
            page,
            (filename.empty() ? wxString(_("<unnamed>")) : wxFileName(filename).GetName()) + mods);
    }

    #ifdef WIN32
    void SetRegistryKey(const wxString &key, wxString value) {
        wxRegKey registrykey(key);
        registrykey.Create();
        registrykey.SetValue("", value);
    }
    #endif

    void SetStatus(const wxString &message) {
        if (GetStatusBar() && !message.IsEmpty()) SetStatusText(message, 0);
    }

    void ClearStatus() {
        if (GetStatusBar()) SetStatusText("", 0);
    }

    void TabsReset() {
        if (notebook) loop(i, notebook->GetPageCount()) {
                auto canvas = static_cast<TSCanvas *>(notebook->GetPage(i));
                canvas->doc->root->ResetChildren();
            }
    }

    void UpdateStatus(const Selection &s, bool updateamount) {
        if (GetStatusBar()) {
            if (Cell *c = s.GetCell(); c && s.xs) {
                SetStatusText(wxString::Format(_("Size %d"), -c->text.relsize), 3);
                SetStatusText(wxString::Format(_("Width %d"), s.grid->colwidths[s.x]), 2);
                SetStatusText(wxString::Format(_("Edited %s %s"), c->text.lastedit.FormatDate(),
                                               c->text.lastedit.FormatTime()),
                              1);
            } else
                for (int field : {1, 2, 3}) SetStatusText("", field);
            if (updateamount) SetStatusText(wxString::Format(_("%d cell(s)"), s.xs * s.ys), 4);
        }
    }

    DECLARE_EVENT_TABLE()
};
