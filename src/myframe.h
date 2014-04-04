struct MyFrame : wxFrame
{
    wxMenu *editmenupopup;
    wxString exepath;
    wxFileHistory filehistory;
    wxTextCtrl *filter, *replaces;
    wxToolBar *tb;
    int refreshhack, refreshhackinstances;
    BlinkTimer bt;
    wxTaskBarIcon tbi;
    wxIcon icon;
    ImageDropdown *idd;
    
    wxAuiNotebook *nb;

    wxAuiManager *aui;

    wxBitmap line_nw, line_sw;
    wxImage foldicon;

    bool fromclosebox;

    wxApp *app;

    #ifdef FSWATCH
        wxFileSystemWatcher *watcher;
        bool watcherwaitingforuser;
    #endif

    MyFrame(wxString exename, wxApp *_app) : wxFrame((wxFrame *)NULL, wxID_ANY, L"TreeSheets", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE),
        filter(NULL), replaces(NULL), tb(NULL), nb(NULL), idd(NULL), refreshhack(0), refreshhackinstances(0), aui(NULL), fromclosebox(true), app(_app)
        #ifdef FSWATCH
        , watcherwaitingforuser(false)
        #endif
    {
        #ifdef FSWATCH
            watcher = NULL;
        #endif

        sys->frame = this;
        
        exepath = wxFileName(exename).GetPath();
        #ifdef __WXMAC__
            int cut = exepath.Find("/MacOS");
            if (cut > 0)
            {
                exepath = exepath.SubString(0, cut) + "/Resources";
            }
        #endif

        class MyLog : public wxLog
        {
            void DoLogString(const wxChar *msg, time_t timestamp) { DoLogText(*msg); }
            void DoLogText(const wxString &msg)
            {
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

        //SetBackgroundColour(*wxWHITE);

        wxInitAllImageHandlers();

        //wxFontEncoding fe = wxLocale::GetSystemEncoding();
        //printf("encoding = %d\n", fe);
        //wxFont::SetDefaultEncoding(fe);

        wxIconBundle icons;
        wxIcon iconbig;
        icon   .LoadFile(exepath+L"/images/icon16.png", wxBITMAP_TYPE_PNG);
        iconbig.LoadFile(exepath+L"/images/icon32.png", wxBITMAP_TYPE_PNG);
        #ifdef WIN32
            int iconsmall = ::GetSystemMetrics(SM_CXSMICON);
            int iconlarge = ::GetSystemMetrics(SM_CXICON);
            icon   .SetSize(iconsmall, iconsmall);  // this shouldn't be necessary...
            iconbig.SetSize(iconlarge, iconlarge);
        #endif
        icons.AddIcon(icon);
        icons.AddIcon(iconbig);
        SetIcons(icons);
        
        if(!line_nw.LoadFile(exepath+L"/images/render/line_nw.png", wxBITMAP_TYPE_PNG) ||
           !line_sw.LoadFile(exepath+L"/images/render/line_sw.png", wxBITMAP_TYPE_PNG) ||
           !foldicon.LoadFile(exepath+L"/images/nuvola/fold.png"))
        {
            wxMessageBox(L"Error loading core data file (TreeSheets not installed correctly?)", L"Initialization Error", wxOK, this);
            // FIXME: what is the correct way to exit?
        }
        
        if (sys->singletray) tbi.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_UP,     wxTaskBarIconEventHandler(MyFrame::OnTBIDBLClick), NULL, this);
        else                 tbi.Connect(wxID_ANY, wxEVT_TASKBAR_LEFT_DCLICK, wxTaskBarIconEventHandler(MyFrame::OnTBIDBLClick), NULL, this);

        aui = new wxAuiManager(this);

        bool mergetbar = false;

        bool showtbar, showsbar, lefttabs, iconset;

        sys->cfg.Read(L"showtbar",   &showtbar, true);
        //#ifdef __WXMAC__
        //sys->cfg.Read(L"showsbar",   &showsbar, false);     // debug asserts on redraw otherwise?
        //#else
        sys->cfg.Read(L"showsbar",   &showsbar, true);
        //#endif
        sys->cfg.Read(L"lefttabs",   &lefttabs, true);

        sys->cfg.Read(L"iconset",    &iconset,  false);

        filehistory.Load(sys->cfg);

        wxMenu *expmenu = new wxMenu();
        expmenu->Append(A_EXPXML,   L"&XML...",                        L"Export the current view as XML (which can also be reimported without losing structure)");
        expmenu->Append(A_EXPHTMLT, L"&HTML (Tables+Styling)...",      L"Export the current view as HTML using nested tables, that will look somewhat like the TreeSheet");
        expmenu->Append(A_EXPHTMLO, L"HTML (&Outline)...",             L"Export the current view as HTML as nested headers, suitable for importing into Word's outline mode");
        expmenu->Append(A_EXPTEXT,  L"Indented &Text...",              L"Export the current view as tree structured text, using spaces for each indentation level. Suitable for importing into mindmanagers and general text programs");
        expmenu->Append(A_EXPCSV,   L"&Comma delimited text (CSV)...", L"Export the current view as CSV. Good for spreadsheets and databases. Only works on grids with no sub-grids (use the Flatten operation first if need be)");
        expmenu->Append(A_EXPIMAGE, L"&Image...",                      L"Export the current view as an image. Useful for faithfull renderings of the TreeSheet, and programs that don't accept any of the above options");

        wxMenu *impmenu = new wxMenu();
        impmenu->Append(A_IMPXML,  L"XML...");
        impmenu->Append(A_IMPXMLA, L"XML (attributes too, for OPML etc)...");
        impmenu->Append(A_IMPTXTI, L"Indented text...");
        impmenu->Append(A_IMPTXTC, L"Comma delimited text (CSV)...");
        impmenu->Append(A_IMPTXTS, L"Semi-Colon delimited text (CSV)...");
        impmenu->Append(A_IMPTXTT, L"Tab delimited text...");

        wxMenu *recentmenu = new wxMenu();
        filehistory.UseMenu(recentmenu);
        filehistory.AddFilesToMenu();

        wxMenu *filemenu = new wxMenu();
        filemenu->Append(A_NEW,          L"&New\tCTRL+n"); //->SetBitmap(wxBitmap("images/nuvola/16x16/actions/filenew.png", wxBITMAP_TYPE_PNG));
        filemenu->Append(A_OPEN,         L"&Open...\tCTRL+o");
        filemenu->Append(A_CLOSE,        L"&Close\tCTRL+w");
        filemenu->AppendSubMenu(recentmenu, L"&Recent files");
        filemenu->Append(A_SAVE,         L"&Save\tCTRL+s");
        filemenu->Append(A_SAVEAS,       L"Save &As...");
        filemenu->AppendSeparator();
        filemenu->Append(A_PAGESETUP,    L"Page Setup...");
        filemenu->Append(A_PRINTSCALE,   L"Set Print Scale...");
        filemenu->Append(A_PREVIEW,      L"Print preview...");
        filemenu->Append(A_PRINT,        L"&Print...\tCTRL+p");
        filemenu->AppendSeparator();
        filemenu->AppendSubMenu(expmenu, L"Export &view as");
        filemenu->AppendSubMenu(impmenu, L"Import file from");
        filemenu->AppendSeparator();
        filemenu->Append(A_EXIT,         L"&Exit\tCTRL+q");

        wxMenu *editmenu;
        loop(twoeditmenus, 2)
        {
            
        wxMenu *sizemenu = new wxMenu();
        sizemenu->Append(A_INCSIZE,    L"&Increase text size (SHIFT+mousewheel)\tSHIFT+PGUP");
        sizemenu->Append(A_DECSIZE,    L"&Decrease text size (SHIFT+mousewheel)\tSHIFT+PGDN");
        sizemenu->Append(A_RESETSIZE,  L"&Reset text sizes\tSHIFT+CTRL+s");
        sizemenu->Append(A_MINISIZE,   L"&Shrink text of all sub-grids\tSHIFT+CTRL+m");
        sizemenu->AppendSeparator();
        sizemenu->Append(A_INCWIDTH,   L"Increase column width (ALT+mousewheel)\tALT+PGUP");
        sizemenu->Append(A_DECWIDTH,   L"Decrease column width (ALT+mousewheel)\tALT+PGDN");
        sizemenu->Append(A_INCWIDTHNH, L"Increase column width (no sub grids)\tCTRL+ALT+PGUP");
        sizemenu->Append(A_DECWIDTHNH, L"Decrease column width (no sub grids)\tCTRL+ALT+PGDN");
        sizemenu->Append(A_RESETWIDTH, L"Reset column widths\tSHIFT+CTRL+w");

        wxMenu *bordmenu = new wxMenu();
        bordmenu->Append(A_BORD1, L"&1");
        bordmenu->Append(A_BORD2, L"&2");
        bordmenu->Append(A_BORD3, L"&3");
        bordmenu->Append(A_BORD4, L"&4");
        bordmenu->Append(A_BORD5, L"&5");

        wxMenu *selmenu = new wxMenu();
        selmenu->Append(A_NEXT,   L"Move to next cell\tTAB");
        selmenu->Append(A_PREV,   L"Move to previous cell\tSHIFT+TAB");
        selmenu->AppendSeparator();
        selmenu->Append(A_SELALL, L"Select &all in current grid\tCTRL+a");
        selmenu->AppendSeparator();
        selmenu->Append(A_LEFT,   L"Move Selection Left\tLEFT");
        selmenu->Append(A_RIGHT,  L"Move Selection Right\tRIGHT");
        selmenu->Append(A_UP,     L"Move Selection Up\tUP");
        selmenu->Append(A_DOWN,   L"Move Selection Down\tDOWN");
        selmenu->AppendSeparator();
        selmenu->Append(A_MLEFT,  L"Move Cells Left\tCTRL+LEFT");
        selmenu->Append(A_MRIGHT, L"Move Cells Right\tCTRL+RIGHT");
        selmenu->Append(A_MUP,    L"Move Cells Up\tCTRL+UP");
        selmenu->Append(A_MDOWN,  L"Move Cells Down\tCTRL+DOWN");
        selmenu->AppendSeparator();
        selmenu->Append(A_SLEFT,  L"Extend Selection Left\tSHIFT+LEFT");
        selmenu->Append(A_SRIGHT, L"Extend Selection Right\tSHIFT+RIGHT");
        selmenu->Append(A_SUP,    L"Extend Selection Up\tSHIFT+UP");
        selmenu->Append(A_SDOWN,  L"Extend Selection Down\tSHIFT+DOWN");
        selmenu->Append(A_SCOLS,  L"Extend Selection Full Columns");
        selmenu->Append(A_SROWS,  L"Extend Selection Full Rows");
        selmenu->AppendSeparator();
        selmenu->Append(A_CANCELEDIT, L"Select &Parent\tESC");
        selmenu->Append(A_ENTERGRID,  L"Select First &Child\tSHIFT+ENTER");
        selmenu->AppendSeparator();
        selmenu->Append(A_LINK,       L"Go To &Matching Cell\tF6");

        wxMenu *temenu = new wxMenu();
        temenu->Append(A_LEFT,       L"Cursor Left\tLEFT");
        temenu->Append(A_RIGHT,      L"Cursor Right\tRIGHT");
        temenu->Append(A_MLEFT,      L"Word Left\tCTRL+LEFT");
        temenu->Append(A_MRIGHT,     L"Word Right\tCTRL+RIGHT");
        temenu->AppendSeparator();
        temenu->Append(A_SLEFT,      L"Extend Selection Left\tSHIFT+LEFT");
        temenu->Append(A_SRIGHT,     L"Extend Selection Right\tSHIFT+RIGHT");
        temenu->Append(A_SCLEFT,     L"Extend Selection Word Left\tSHIFT+CTRL+LEFT");
        temenu->Append(A_SCRIGHT,    L"Extend Selection Word Right\tSHIFT+CTRL+RIGHT");
        temenu->Append(A_SHOME,      L"Extend Selection to Start\tSHIFT+HOME");
        temenu->Append(A_SEND,       L"Extend Selection to End\tSHIFT+END");
        temenu->AppendSeparator();
        temenu->Append(A_HOME,       L"Start of line of text\tHOME");
        temenu->Append(A_END,        L"End of line of text\tEND");
        temenu->Append(A_CHOME,      L"Start of text\tCTRL+HOME");
        temenu->Append(A_CEND,       L"End of text\tCTRL+END");
        temenu->AppendSeparator();
        temenu->Append(A_ENTERCELL,  L"Enter/exit text edit mode\tENTER");
        temenu->Append(A_ENTERCELL,  L"Enter/exit text edit mode\tF2");
        temenu->Append(A_CANCELEDIT, L"Cancel text edits\tESC");

        wxMenu *stmenu = new wxMenu();
        stmenu->Append(A_BOLD,   L"Toggle cell &BOLD\tCTRL+b");
        stmenu->Append(A_ITALIC, L"Toggle cell &ITALIC\tCTRL+i");
        stmenu->Append(A_TT,     L"Toggle cell &typewriter");
        stmenu->Append(A_UNDERL, L"Toggle cell &underlined\tCTRL+u");
        stmenu->Append(A_STRIKET,L"Toggle cell &strikethrough\tCTRL+t");
        stmenu->AppendSeparator();
        stmenu->Append(A_RESETSTYLE, L"&Reset text styles\tSHIFT+CTRL+r");
        stmenu->Append(A_RESETCOLOR, L"Reset &colors\tSHIFT+CTRL+c");

        wxMenu *tagmenu = new wxMenu();
        tagmenu->Append(A_TAGADD,    L"&Add Cell Text as Tag");
        tagmenu->Append(A_TAGREMOVE, L"&Remove Cell Text from Tags");
        tagmenu->Append(A_NOP,       L"&Set Cell Text to tag (use CTRL+RMB)", L"Hold CTRL while pressing right mouse button to quickly set a tag for the current cell using a popup menu");

        wxMenu *orgmenu = new wxMenu();
        orgmenu->Append(A_TRANSPOSE,   L"&Transpose\tSHIFT+CTRL+t",            L"changes the orientation of a grid");
        orgmenu->Append(A_SORT,        L"Sort &Ascending",                     L"Make a 1xN selection to indicate which column to sort on, and which rows to affect");
        orgmenu->Append(A_SORTD,       L"Sort &Descending",                    L"Make a 1xN selection to indicate which column to sort on, and which rows to affect");
        orgmenu->Append(A_HSWAP,       L"Hierarchy &Swap\tF8",                 L"Swap all cells with this text at this level (or above) with the parent");        
        orgmenu->Append(A_HIFY,        L"&Hierarchify",                        L"Convert an NxN grid with repeating elements per column into an 1xN grid with hierarchy, useful to convert data from spreadsheets");        
        orgmenu->Append(A_FLATTEN,     L"&Flatten",                            L"Takes a hierarchy (nested 1xN or Nx1 grids) and converts it into a flat NxN grid, useful for export to spreadsheets");        

        wxMenu *imgmenu = new wxMenu();
        imgmenu->Append(A_IMAGE,       L"&Add Image",                          L"Adds an image to the selected cell");
        imgmenu->Append(A_IMAGESC,     L"&Scale Image",                        L"Change the image size if it is too big or too small");
        imgmenu->Append(A_IMAGER,      L"&Remove Image(s)",                    L"Remove image(s) from the selected cells");

        wxMenu *navmenu = new wxMenu();
        navmenu->Append(A_BROWSE,      L"Open link in &browser\tF5",           L"Opens up the text from the selected cell in browser (should start be a valid URL)");
        navmenu->Append(A_BROWSEF,     L"Open &file\tF4",                      L"Opens up the text from the selected cell in default application for the file type");

        wxMenu *laymenu = new wxMenu();
        laymenu->Append(A_V_GS,        L"Vertical Layout with Grid Style Rendering\tALT+1");
        laymenu->Append(A_V_BS,        L"Vertical Layout with Bubble Style Rendering\tALT+2");
        laymenu->Append(A_V_LS,        L"Vertical Layout with Line Style Rendering\tALT+3");
        laymenu->AppendSeparator();
        laymenu->Append(A_H_GS,        L"Horizontal Layout with Grid Style Rendering\tALT+4");
        laymenu->Append(A_H_BS,        L"Horizontal Layout with Bubble Style Rendering\tALT+5");
        laymenu->Append(A_H_LS,        L"Horizontal Layout with Line Style Rendering\tALT+6");
        laymenu->AppendSeparator();
        laymenu->Append(A_GS,          L"Grid Style Rendering\tALT+7");
        laymenu->Append(A_BS,          L"Bubble Style Rendering\tALT+8");
        laymenu->Append(A_LS,          L"Line Style Rendering\tALT+9");
        laymenu->AppendSeparator();
        laymenu->Append(A_TEXTGRID,    L"Toggle Vertical Layout\tF7",         L"Make a hierarchy layout more vertical (default) or more horizontal");

        editmenu = new wxMenu();
        editmenu->Append(A_CUT,         L"Cu&t\tCTRL+x");
        editmenu->Append(A_COPY,        L"&Copy\tCTRL+c");
        editmenu->Append(A_COPYCT,      L"Copy As Continuous Text");
        editmenu->Append(A_PASTE,       L"&Paste\tCTRL+v");
        editmenu->Append(A_PASTESTYLE,  L"Paste Style Only\tCTRL+SHIFT+v", L"only sets the colors and style of the copied cell, and keeps the text");
        editmenu->AppendSeparator();
        editmenu->Append(A_UNDO,        L"&Undo\tCTRL+z",                       L"revert the changes, one step at a time");
        editmenu->Append(A_REDO,        L"&Redo\tCTRL+y",                       L"redo any undo steps, if you haven't made changes since");
        editmenu->AppendSeparator();
        editmenu->Append(A_DELETE,      L"&Delete After\tDEL",         L"Deletes the column of cells after the selected grid line, or the row below");
        editmenu->Append(A_BACKSPACE,   L"Delete Before\tBACK",       L"Deletes the column of cells before the selected grid line, or the row above");
        editmenu->AppendSeparator();
        editmenu->Append(A_NEWGRID,     L"&Insert New Grid"
            #ifdef __WXMAC__
                L"\tCTRL+g"    
            #else
                L"\tINS"
            #endif
            , L"Adds a grid to the selected cell");
        editmenu->Append(A_WRAP,        L"&Wrap in new parent\tF9",             L"Creates a new level of hierarchy around the current selection");
        editmenu->AppendSeparator();
        editmenu->Append(A_FOLD,        L"Toggle Fold\t"
            #ifndef WIN32
                L"CTRL+F10",   // F10 is tied to the OS on both Ubuntu and OS X, and SHIFT+F10 is now right click on all platforms?
            #else
                L"F10", 
            #endif
            L"Toggles showing the grid of the selected cell(s)");
        editmenu->AppendSeparator();
        editmenu->AppendSubMenu(selmenu,  L"&Selection...");
        editmenu->AppendSubMenu(orgmenu,  L"&Grid Reorganization...");
        editmenu->AppendSubMenu(laymenu,  L"&Layout & Render Style...");
        editmenu->AppendSubMenu(imgmenu,  L"&Images...");
        editmenu->AppendSubMenu(navmenu,  L"&Browsing...");
        editmenu->AppendSubMenu(temenu,   L"Text &Editing...");
        editmenu->AppendSubMenu(sizemenu, L"Text Sizing...");
        editmenu->AppendSubMenu(stmenu,   L"Text Style...");
        editmenu->AppendSubMenu(bordmenu, L"Set Grid Border Width...");
        editmenu->AppendSubMenu(tagmenu,  L"Tag...");

        if(!twoeditmenus) editmenupopup = editmenu;

        }

        wxMenu *semenu = new wxMenu();
        semenu->Append(A_SEARCHF,     L"&Search\tCTRL+f");
        semenu->Append(A_SEARCHNEXT,  L"&Go To Next Search Result\tF3");
        semenu->Append(A_REPLACEONCE, L"&Replace in Current Selection\tCTRL+h");
        semenu->Append(A_REPLACEONCEJ,L"&Replace in Current Selection & Jump Next\tCTRL+j");
        semenu->Append(A_REPLACEALL,  L"Replace &All");        

        wxMenu *scrollmenu = new wxMenu();
        scrollmenu->Append(A_AUP,     L"Scroll Up (mousewheel)\tPGUP");
        scrollmenu->Append(A_AUP,     L"Scroll Up (mousewheel)\tALT+UP");
        scrollmenu->Append(A_ADOWN,   L"Scroll Down (mousewheel)\tPGDN");
        scrollmenu->Append(A_ADOWN,   L"Scroll Down (mousewheel)\tALT+DOWN");
        scrollmenu->Append(A_ALEFT,   L"Scroll Left\tALT+LEFT");
        scrollmenu->Append(A_ARIGHT,  L"Scroll Right\tALT+RIGHT");

        wxMenu *filtermenu = new wxMenu();
        filtermenu->Append(A_FILTEROFF,  L"Turn filter &off");
        filtermenu->Append(A_FILTERS,    L"Show only cells in current search");
        filtermenu->Append(A_FILTER5,    L"Show 5% of last edits");
        filtermenu->Append(A_FILTER10,   L"Show 10% of last edits");
        filtermenu->Append(A_FILTER20,   L"Show 20% of last edits");
        filtermenu->Append(A_FILTER50,   L"Show 50% of last edits");
        filtermenu->Append(A_FILTERM,    L"Show 1% more than the last filter");
        filtermenu->Append(A_FILTERL,    L"Show 1% less than the last filter");

        wxMenu *viewmenu = new wxMenu();
        viewmenu->Append(A_ZOOMIN,      L"Zoom &In (CTRL+mousewheel)\tCTRL+PGUP");
        viewmenu->Append(A_ZOOMOUT,     L"Zoom &Out (CTRL+mousewheel)\tCTRL+PGDN");
        viewmenu->Append(A_NEXTFILE,    L"Switch to &next file/tab\tCTRL+TAB");
        viewmenu->Append(A_PREVFILE,    L"Switch to &previous file/tab\tSHIFT+CTRL+TAB");
        #ifndef __WXMAC__
            viewmenu->Append(A_FULLSCREEN,  L"Toggle &Fullscreen View\tF11");
            viewmenu->Append(A_SCALED,      L"Toggle &Scaled Presentation View\tF12");
        #endif
        viewmenu->AppendSubMenu(scrollmenu, L"Scroll Sheet...");
        viewmenu->AppendSubMenu(filtermenu, L"Filter...");

        wxMenu *roundmenu = new wxMenu();
        roundmenu->AppendRadioItem(A_ROUND0,     L"Radius &0");
        roundmenu->AppendRadioItem(A_ROUND1,     L"Radius &1");
        roundmenu->AppendRadioItem(A_ROUND2,     L"Radius &2");
        roundmenu->AppendRadioItem(A_ROUND3,     L"Radius &3");
        roundmenu->AppendRadioItem(A_ROUND4,     L"Radius &4");
        roundmenu->AppendRadioItem(A_ROUND5,     L"Radius &5");
        roundmenu->AppendRadioItem(A_ROUND6,     L"Radius &6");
        roundmenu->Check(sys->roundness+A_ROUND0, true);

        wxMenu *optmenu = new wxMenu();
        optmenu->Append(A_DEFFONT,              L"Pick Default Font...");
        optmenu->Append(A_CUSTCOL,              L"Pick Custom &Color...");
        optmenu->Append(A_COLCELL,              L"&Set Custom Color From Cell BG");
        optmenu->Append(A_DEFBGCOL,             L"Pick Document Background...");
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_SHOWSBAR,    L"Show Statusbar");                  optmenu->Check(A_SHOWSBAR, showsbar);
        optmenu->AppendCheckItem(A_SHOWTBAR,    L"Show Toolbar");                    optmenu->Check(A_SHOWTBAR, showtbar);
        optmenu->AppendCheckItem(A_LEFTTABS,    L"File Tabs on the bottom");         optmenu->Check(A_LEFTTABS, lefttabs);
        optmenu->AppendCheckItem(A_TOTRAY,      L"Minimize to tray");                optmenu->Check(A_TOTRAY, sys->totray);
        optmenu->AppendCheckItem(A_MINCLOSE,    L"Minimize on close");               optmenu->Check(A_MINCLOSE, sys->minclose);
        optmenu->AppendCheckItem(A_SINGLETRAY,  L"Single click maximize from tray"); optmenu->Check(A_SINGLETRAY, sys->singletray);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_ZOOMSCR,     L"Swap mousewheel scrolling and zooming");      optmenu->Check(A_ZOOMSCR,  sys->zoomscroll);
        optmenu->AppendCheckItem(A_THINSELC,    L"Navigate in between cells with cursor keys"); optmenu->Check(A_THINSELC, sys->thinselc);
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_MAKEBAKS,    L"Create .bak files");                     optmenu->Check(A_MAKEBAKS, sys->makebaks);
        optmenu->AppendCheckItem(A_AUTOSAVE,    L"Autosave to .tmp");                      optmenu->Check(A_AUTOSAVE, sys->autosave);
        #ifdef FSWATCH
            optmenu->AppendCheckItem(A_FSWATCH,     L"Auto reload documents", L"Reloads when another computer has changed a file (if you have made changes, asks)"); optmenu->Check(A_FSWATCH,  sys->fswatch);
        #endif
        optmenu->AppendSeparator();
        optmenu->AppendCheckItem(A_CENTERED,    L"Render document centered");        optmenu->Check(A_CENTERED, sys->centered);
        optmenu->AppendCheckItem(A_FASTRENDER,  L"Faster line rendering");           optmenu->Check(A_FASTRENDER, sys->fastrender);
        optmenu->AppendCheckItem(A_ICONSET,     L"black and white toolbar icons");   optmenu->Check(A_ICONSET, iconset);
        optmenu->AppendSubMenu(roundmenu,       L"&Roundness of grid borders...");

        wxMenu *markmenu = new wxMenu();
        markmenu->Append(A_MARKDATA,  L"&Data");
        markmenu->Append(A_MARKCODE,  L"&Operation");
        markmenu->Append(A_MARKVARD,  L"Variable &Assign");
        markmenu->Append(A_MARKVARU,  L"Variable &Read");
        markmenu->Append(A_MARKVIEWH, L"&Horizontal View");
        markmenu->Append(A_MARKVIEWV, L"&Vertical View");

        wxMenu *langmenu = new wxMenu();
        langmenu->Append(A_RUN,           L"&Run");
        langmenu->AppendSubMenu(markmenu, L"&Mark as");
        langmenu->Append(A_CLRVIEW,       L"&Clear Views");

        wxMenu *helpmenu = new wxMenu();
        helpmenu->Append(A_ABOUT, L"&About...");
        helpmenu->Append(A_HELPI, L"Load interactive &tutorial...\tF1");
        helpmenu->Append(A_HELP,  L"View tutorial &web page...");

        wxAcceleratorEntry entries[3];
        entries[0].Set(wxACCEL_SHIFT, WXK_DELETE, A_CUT);
        entries[1].Set(wxACCEL_SHIFT, WXK_INSERT, A_PASTE);
        entries[2].Set(wxACCEL_CTRL,  WXK_INSERT, A_COPY);
        wxAcceleratorTable accel(3, entries);
        SetAcceleratorTable(accel);

        if(!mergetbar)
        {
            wxMenuBar *menubar = new wxMenuBar();
            menubar->Append(filemenu, L"&File");
            menubar->Append(editmenu, L"&Edit");
            menubar->Append(semenu,   L"&Search");
            menubar->Append(viewmenu, L"&View");
            menubar->Append(optmenu,  L"&Options");
            menubar->Append(langmenu, L"&Program");
            menubar->Append(helpmenu,
                #ifdef __WXMAC__
                    wxApp::s_macHelpMenuTitleName // so merges with osx provided help
                #else
                    L"&Help"
                #endif
                );
            #ifdef __WXMAC__
                // these don't seem to work anymore in the newer wxWidgets, handled in the menu event handler below instead
                wxApp::s_macAboutMenuItemId = A_ABOUT;
                wxApp::s_macExitMenuItemId = A_EXIT;
                wxApp::s_macPreferencesMenuItemId = A_DEFFONT;      // we have no prefs, so for now just select the font
            #endif 
            SetMenuBar(menubar);
        }

        wxColour toolbgcol(iconset ? 0xF0ECE8 : 0xD8C7BC /*0xEEDCCC word*/);

        if(showtbar || mergetbar)
        {
            tb = CreateToolBar(wxBORDER_NONE | wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
            tb->SetOwnBackgroundColour(toolbgcol);

            /*
            if(mergetbar)   // need a way to display text instead of bitmap to make this work
            {
                TBMenu(tb, filemenu, L"File", 6000);
                TBMenu(tb, editmenu, L"Edit", 6001);
                TBMenu(tb, semenu,   L"Search", 6002);
                TBMenu(tb, viewmenu, L"View", 6003);
                TBMenu(tb, optmenu,  L"Options", 6004);
                TBMenu(tb, langmenu, L"Program", 6005);
                TBMenu(tb, helpmenu, L"Help", 6006);
            }
            */
            
            #ifdef __WXMAC__
            #define SEPARATOR
            #else 
            #define SEPARATOR tb->AddSeparator()
            #endif

            wxString iconpath = exepath + (iconset ? L"/images/webalys/toolbar/" : L"/images/nuvola/toolbar/");
            tb->SetToolBitmapSize(iconset ? wxSize(18, 18) : wxSize(22, 22));

            AddTBIcon(tb, L"New (CTRL+n)",           A_NEW,     iconpath+L"filenew.png");
            AddTBIcon(tb, L"Open (CTRL+o)",          A_OPEN,    iconpath+L"fileopen.png");
            AddTBIcon(tb, L"Save (CTRL+s)",          A_SAVE,    iconpath+L"filesave.png");
            AddTBIcon(tb, L"Save As",                A_SAVEAS,  iconpath+L"filesaveas.png");
            SEPARATOR;
            AddTBIcon(tb, L"Undo (CTRL+z)",          A_UNDO,    iconpath+L"undo.png");
            AddTBIcon(tb, L"Copy (CTRL+c)",          A_COPY,    iconpath+L"editcopy.png");
            AddTBIcon(tb, L"Paste (CTRL+v)",         A_PASTE,   iconpath+L"editpaste.png");
            SEPARATOR;
            AddTBIcon(tb, L"Zoom In (CTRL+mousewheel)",  A_ZOOMIN,  iconpath+L"zoomin.png");
            AddTBIcon(tb, L"Zoom Out (CTRL+mousewheel)", A_ZOOMOUT, iconpath+L"zoomout.png");
            SEPARATOR;
            AddTBIcon(tb, L"New Grid (INS)",         A_NEWGRID, iconpath+L"newgrid.png");
            AddTBIcon(tb, L"Add Image",              A_IMAGE,   iconpath+L"image.png");
            SEPARATOR;
            AddTBIcon(tb, L"Run",                    A_RUN,     iconpath+L"run.png");
            tb->AddSeparator();
            tb->AddControl(new wxStaticText(tb, wxID_ANY, L"Search "));
            tb->AddControl(filter = new wxTextCtrl(tb, A_SEARCH, "", wxDefaultPosition, wxSize(80, 24)));
            SEPARATOR;
            tb->AddControl(new wxStaticText(tb, wxID_ANY, L"Replace "));
            tb->AddControl(replaces = new wxTextCtrl(tb, A_REPLACE, "", wxDefaultPosition, wxSize(60, 24)));
            tb->AddSeparator();
            tb->AddControl(new wxStaticText(tb, wxID_ANY, L"Cell "));
            tb->AddControl(new ColorDropdown(tb, A_CELLCOLOR, 0));
            SEPARATOR;
            tb->AddControl(new wxStaticText(tb, wxID_ANY, L"Text "));
            tb->AddControl(new ColorDropdown(tb, A_TEXTCOLOR, 1));
            SEPARATOR;
            tb->AddControl(new wxStaticText(tb, wxID_ANY, L"Border "));
            tb->AddControl(new ColorDropdown(tb, A_BORDCOLOR, 6));
            tb->AddSeparator();
            tb->AddControl(new wxStaticText(tb, wxID_ANY, L"Image "));
            wxString imagepath = exepath + L"/images/nuvola/dropdown/";
            idd = new ImageDropdown(tb, imagepath);
            tb->AddControl(idd);
            tb->Realize();
        }

        if(showsbar)
        {
            wxStatusBar *sb = CreateStatusBar(4);
            sb->SetOwnBackgroundColour(toolbgcol);
            SetStatusBarPane(0);
            int swidths[] = { -1, 200, 120, 100 };
            SetStatusWidths(4, swidths);
        }

        nb = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, /*lefttabs ? wxNB_LEFT : wxNB_TOP*/ wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_CLOSE_ON_ALL_TABS | (lefttabs ? wxAUI_NB_BOTTOM : wxAUI_NB_TOP));
        //nb->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
        nb->SetOwnBackgroundColour(toolbgcol);
        //nb->SetInternalBorder(0);
        //nb->SetControlMargin(0);
        //nb->SetBackgroundColour(*wxLIGHT_GREY);
        //nb->SetBackgroundStyle(wxBG_STYLE_COLOUR);
        
        //nb->SetPadding(wxSize(16, 2));
        //nb->SetOwnFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false));
    
        int resx, resy, posx, posy;
        sys->cfg.Read(L"resx", &resx, 1000);
        sys->cfg.Read(L"resy", &resy, 750);
        sys->cfg.Read(L"posx", &posx, 10);
        sys->cfg.Read(L"posy", &posy, 10);
        SetSize(resx, resy);
        SetPosition(wxPoint(posx, posy));

        bool ismax;
        sys->cfg.Read(L"maximized", &ismax, true);

        aui->AddPane(nb, wxCENTER);
        aui->Update();

        #ifdef FSWATCH
            watcher = new wxFileSystemWatcher();
            watcher->SetOwner(this);
            Connect(wxEVT_FSWATCHER, wxFileSystemWatcherEventHandler(MyFrame::OnFileSystemEvent));
        #endif

        Show(TRUE);
                
        if(ismax) Maximize(true);   // needs to be after Show() to avoid scrollbars rendered in the wrong place?

        SetFileAssoc(exename);

        wxSafeYield();
    }

    ~MyFrame()
    {
        filehistory.Save(sys->cfg);
        if(!IsIconized())
        {
            sys->cfg.Write(L"maximized", IsMaximized());
            if(!IsMaximized())
            {
                sys->cfg.Write(L"resx", GetSize().x);
                sys->cfg.Write(L"resy", GetSize().y);
                sys->cfg.Write(L"posx", GetPosition().x);
                sys->cfg.Write(L"posy", GetPosition().y);
            }
        }

        aui->ClearEventHashTable();
        aui->UnInit();
        DELETEP(aui);
        DELETEP(editmenupopup);
    }
    /*
    void OnSize(wxSizeEvent& event)
    {
        if(nb) nb->SetSize(GetClientSize());
    }
    */
    TSCanvas *NewTab(Document *doc, bool append = false)
    {        
        TSCanvas *sw = new TSCanvas(this, nb);
        sw->doc = doc;
        doc->sw = sw;
        sw->SetScrollRate(1, 1);
        if(append) nb->AddPage(sw, L"<unnamed>", true, wxNullBitmap); else nb->InsertPage(0, sw, L"<unnamed>", true, wxNullBitmap);
        sw->SetDropTarget(new DropTarget(doc->dataobjc));
        sw->SetFocus();
        return sw;
    }
    
    TSCanvas *GetCurTab()
    {
        return nb && nb->GetSelection()>=0 ? (TSCanvas *)nb->GetPage(nb->GetSelection()) : NULL;
    }
    
    TSCanvas *GetTabByFileName(const wxString &fn)
    {
        //if(singlesw && singlesw->doc->filename==fn) return singlesw;
        
        if(nb) loop(i, nb->GetPageCount())
        {
            TSCanvas *p = (TSCanvas *)nb->GetPage(i);
            if(p->doc->filename==fn)
            {
                nb->SetSelection(i);
                return p;
            }
        }
        
        return NULL;
    }
    
    void OnTabChange(wxAuiNotebookEvent &nbe)
    {
        TSCanvas *sw = (TSCanvas *)nb->GetPage(nbe.GetSelection());
        sw->Status();
        sys->TabChange(sw->doc);
    }

    void TabsReset()
    {
        if(nb) loop(i, nb->GetPageCount())
        {
            TSCanvas *p = (TSCanvas *)nb->GetPage(i);
            p->doc->rootgrid->ResetChildren();
        }
    }

    void OnTabClose(wxAuiNotebookEvent &nbe)
    {
        TSCanvas *sw = (TSCanvas *)nb->GetPage(nbe.GetSelection());
        sys->RememberOpenFiles();
        if(nb->GetPageCount()<=1)
        {
            nbe.Veto();
            Close();
        }
        else if(sw->doc->CloseDocument())
        {
            nbe.Veto();
        }
    }

    void CycleTabs(int offset = 1)
    {
        int numtabs = nb->GetPageCount();
        offset = ((offset>=0)? 1 : numtabs-1);		// normalize to non-negative wrt modulo
        nb->SetSelection((nb->GetSelection()+offset)%numtabs);
    }
    
    void SetPageTitle(const wxString &fn, wxString mods, int page = -1)
    {
        if(page<0) page = nb->GetSelection();
        if(page<0) return;
        if(page==nb->GetSelection()) SetTitle(L"TreeSheets - "+fn+mods);
        nb->SetPageText(page, (fn.empty() ? L"<unnamed>" : wxFileName(fn).GetName())+mods);
    }

    void AddTBIcon(wxToolBar *tb, const wxChar *name, int action, wxString file)
    {
        wxBitmap bm;
        if(bm.LoadFile(file, wxBITMAP_TYPE_PNG)) tb->AddTool(action, name, bm, bm, wxITEM_NORMAL, name);
    }

    void TBMenu(wxToolBar *tb, wxMenu *menu, const wxChar *name, int id = 0)
    {
        tb->AddTool(id, name, wxNullBitmap, wxEmptyString, wxITEM_DROPDOWN);
        tb->SetDropdownMenu(id, menu);
    }

    void OnMenu(wxCommandEvent &ce)
    {
        wxTextCtrl *tc;
        if(((tc = filter) && filter==wxWindow::FindFocus()) || ((tc = replaces) && replaces==wxWindow::FindFocus())) 
        {
            // FIXME: have to emulate this behavior because menu always captures these events (??)
            long from, to;
            tc->GetSelection(&from, &to);
            switch(ce.GetId())
            {
                case A_MLEFT:
                case A_LEFT:      if(from!=to) tc->SetInsertionPoint(from); else if(from)                    tc->SetInsertionPoint(from-1); return;
                case A_MRIGHT:
                case A_RIGHT:     if(from!=to) tc->SetInsertionPoint(to);   else if(to<tc->GetLineLength(0)) tc->SetInsertionPoint(to+1); return;

                case A_SHOME:     tc->SetSelection(0, to); return;
                case A_SEND:      tc->SetSelection(from, 1000); return;

                case A_SCLEFT:
                case A_SLEFT:     if(from) tc->SetSelection(from-1, to); return;
                case A_SCRIGHT:
                case A_SRIGHT:    if(to<tc->GetLineLength(0)) tc->SetSelection(from, to+1); return;

                case A_BACKSPACE: tc->Remove(from-(from==to), to); return;
                case A_DELETE:    tc->Remove(from, to+(from==to)); return;
                case A_HOME:      tc->SetSelection(0,    0); return;
                case A_END:       tc->SetSelection(1000, 1000); return;
                case A_SELALL:    tc->SetSelection(0,    1000); return;
            }
        }
        TSCanvas *sw = GetCurTab();
        wxClientDC dc(sw);
        sw->DoPrepareDC(dc);
        sw->doc->ShiftToCenter(dc);
        switch(ce.GetId())
        {
            case A_NOP:     break;
        
            case A_ALEFT:    sw->CursorScroll(-g_scrollratecursor,  0); break;
            case A_ARIGHT:   sw->CursorScroll( g_scrollratecursor,  0); break;
            case A_AUP:      sw->CursorScroll( 0, -g_scrollratecursor); break;
            case A_ADOWN:    sw->CursorScroll( 0,  g_scrollratecursor); break;
            
            case A_ICONSET:    sys->cfg.Write(L"iconset",    ce.IsChecked()); sw->Status("change will take effect next run of TreeSheets"); break;
            case A_SHOWSBAR:   sys->cfg.Write(L"showsbar",   ce.IsChecked()); sw->Status("change will take effect next run of TreeSheets"); break;
            case A_SHOWTBAR:   sys->cfg.Write(L"showtbar",   ce.IsChecked()); sw->Status("change will take effect next run of TreeSheets"); break;
            case A_LEFTTABS:   sys->cfg.Write(L"lefttabs",   ce.IsChecked()); sw->Status("change will take effect next run of TreeSheets"); break;
            case A_SINGLETRAY: sys->cfg.Write(L"singletray", ce.IsChecked()); sw->Status("change will take effect next run of TreeSheets"); break;            
            case A_MAKEBAKS:   sys->cfg.Write(L"makebaks",   sys->makebaks   = ce.IsChecked()); break;            
            case A_TOTRAY:     sys->cfg.Write(L"totray",     sys->totray     = ce.IsChecked()); break;            
            case A_MINCLOSE:   sys->cfg.Write(L"minclose",   sys->minclose   = ce.IsChecked()); break;            
            case A_ZOOMSCR:    sys->cfg.Write(L"zoomscroll", sys->zoomscroll = ce.IsChecked()); break;            
            case A_THINSELC:   sys->cfg.Write(L"thinselc",   sys->thinselc   = ce.IsChecked()); break;            
            case A_AUTOSAVE:   sys->cfg.Write(L"autosave",   sys->autosave   = ce.IsChecked()); break;            
            case A_CENTERED:   sys->cfg.Write(L"centered",   sys->centered   = ce.IsChecked()); Refresh(); break;            
            case A_FSWATCH:    sys->cfg.Write(L"fswatch",    sys->fswatch    = ce.IsChecked()); sw->Status("change will take effect next run of TreeSheets"); break;            
            case A_FASTRENDER: sys->cfg.Write(L"fastrender", sys->fastrender = ce.IsChecked()); Refresh(); break;            

            case A_FULLSCREEN: ShowFullScreen(!IsFullScreen()); if(IsFullScreen()) sw->Status("press F11 to exit fullscreen mode"); break;

            case A_SEARCHF: filter->SetFocus(); filter->SetSelection(0, 1000); break;
            
            #ifdef __WXMAC__
            case wxID_OSX_HIDE:       Iconize(true); break;
            case wxID_OSX_HIDEOTHERS: sw->Status("NOT IMPLEMENTED"); break;
            case wxID_OSX_SHOWALL:    Iconize(false); break;
            case wxID_ABOUT:          sw->doc->Action(dc, A_ABOUT); break;
            case wxID_PREFERENCES:    sw->doc->Action(dc, A_DEFFONT); break;
            case wxID_EXIT:           // FALL THRU:
            #endif
            case A_EXIT: fromclosebox = false; this->Close(); break;

            case A_CLOSE: sw->doc->Action(dc, ce.GetId()); break;   // sw dangling pointer on return
                

            default:
                if(ce.GetId()>=wxID_FILE1 && ce.GetId()<=wxID_FILE9)
                {
                    wxString f(filehistory.GetHistoryFile(ce.GetId() - wxID_FILE1));
                    sw->Status(sys->Open(f));
                }
                else if(ce.GetId()>=A_TAGSET)
                {
                    sw->Status(sw->doc->TagSet(ce.GetId()-A_TAGSET));
                }
                else
                {
                    sw->Status(sw->doc->Action(dc, ce.GetId())); break;
                }
        }
    }

    /*
    void OnMouseWheel(wxMouseEvent &me)
    {
        if(GetCurTab()) GetCurTab()->OnMouseWheel(me);
    }
    */

    void OnSearch(wxCommandEvent &ce)
    {
        sys->searchstring = ce.GetString().Lower();
        Document *doc = GetCurTab()->doc;
        doc->selected.g = NULL;
        if(doc->searchfilter) doc->SetSearchFilter(sys->searchstring.Len()!=0);
        else doc->Refresh();
        GetCurTab()->Status();
    }

    void ReFocus() { if(GetCurTab()) GetCurTab()->SetFocus(); } 

    void OnCellColor(wxCommandEvent &ce) { GetCurTab()->doc->ColorChange(A_CELLCOLOR, ce.GetInt()); ReFocus(); }
    void OnTextColor(wxCommandEvent &ce) { GetCurTab()->doc->ColorChange(A_TEXTCOLOR, ce.GetInt()); ReFocus(); }
    void OnBordColor(wxCommandEvent &ce) { GetCurTab()->doc->ColorChange(A_BORDCOLOR, ce.GetInt()); ReFocus(); }
    void OnDDImage  (wxCommandEvent &ce) { GetCurTab()->doc->ImageChange(idd->as[ce.GetInt()]);     ReFocus(); }
    
    void OnSizing(wxSizeEvent  &se) { se.Skip(); }

    void OnMaximize(wxMaximizeEvent &me) { ReFocus(); }
    void OnActivate(wxActivateEvent &ae) { ReFocus(); }    

    void OnIconize(wxIconizeEvent &me)
    {
        if(me.IsIconized())
        {
            #ifdef WIN32
                if(sys->totray)
                {
                    tbi.SetIcon(icon, L"TreeSheets");
                    Show(false);   
                    Iconize();
                }
            #endif
        }
        else
        {
            if(GetCurTab()) GetCurTab()->SetFocus();
        }
    }
    
    void DeIconize()
    {
        if(!IsIconized())
        {
            RequestUserAttention();
            return;
        }
        Show(true);
        Iconize(false);
        tbi.RemoveIcon();    
    }
    
    void OnTBIDBLClick(wxTaskBarIconEvent &e)
    {
        DeIconize();
    }

    void OnClosing(wxCloseEvent &ce)
    {
        bool fcb = fromclosebox;
        fromclosebox = true;

        if(fcb && sys->minclose)
        {
            ce.Veto();
            Iconize();
            return;
        }

        sys->RememberOpenFiles();

        if(ce.CanVeto()) while(nb->GetPageCount())
        {
            if(GetCurTab()->doc->CloseDocument())
            {
                ce.Veto();
                sys->RememberOpenFiles();   // may have closed some, but not all
                return;
            }
            else
            {
                nb->DeletePage(nb->GetSelection());
            }
        }
        
        bt.Stop();
        sys->savechecker.Stop();

        Destroy();
    }

    #ifdef WIN32
    void SetRegKey(wxChar *key, wxString val)
    {
        wxRegKey rk(key);
        rk.Create();
        rk.SetValue(L"", val);
    }
    #endif

    void SetFileAssoc(wxString &exename)
    {
        #ifdef WIN32
            SetRegKey(L"HKEY_CLASSES_ROOT\\.cts",                             L"TreeSheets");
            SetRegKey(L"HKEY_CLASSES_ROOT\\TreeSheets",                       L"TreeSheets file");
            SetRegKey(L"HKEY_CLASSES_ROOT\\TreeSheets\\Shell\\Open\\Command", wxString(L"\"")+exename+L"\" \"%1\"");
            SetRegKey(L"HKEY_CLASSES_ROOT\\TreeSheets\\DefaultIcon",          wxString(L"\"")+exename+L"\",0");
        #else
            //TODO: do something similar for mac/kde/gnome?
        #endif
    }

    #ifdef FSWATCH
    void OnFileSystemEvent(wxFileSystemWatcherEvent &event)
    {
        if(event.GetChangeType()!=wxFSW_EVENT_MODIFY || watcherwaitingforuser) return;

        wxString &modfile = event.GetPath().GetFullPath();

        if(nb) loop(i, nb->GetPageCount())
        {
            Document *doc = ((TSCanvas *)nb->GetPage(i))->doc;
            if(doc->filename==modfile)
            {
                wxDateTime modtime = wxFileName(modfile).GetModificationTime();
                if(modtime==doc->lastmodificationtime)
                {
                    return;
                }

                if(doc->modified)
                {
                    // TODO:
                    // this dialog is problematic since it may be on an unattended computer and more of these events may fire.
                    // since the occurrence of this situation is rare, it may be better to just take the most recently changed version (which is the one that has just been modified on disk)
                    // this potentially throws away local changes, but this can only happen if the user left changes unsaved, then decided to go edit an older version on another computer

                    // for now, we leave this code active, and guard it with watcherwaitingforuser

                    wxString msg = wxString::Format(L"%s\nhas been modified on disk by another program / computer:\nWould you like to discard your changes and re-load from disk?", doc->filename); 
                    watcherwaitingforuser = true;
                    int res = wxMessageBox(msg, L"File modification conflict!", wxYES_NO|wxICON_QUESTION, this);
                    watcherwaitingforuser = false;
                    if(res != wxYES) return;
                }
                
                const char *msg = sys->LoadDB(doc->filename, false, true);
                assert(msg);
                if(*msg)
                {
                    GetCurTab()->Status(msg);
                }
                else
                {
                    loop(j, nb->GetPageCount())
                        if(((TSCanvas *)nb->GetPage(j))->doc==doc)
                            nb->DeletePage(j);
                    ::wxRemoveFile(sys->TmpName(modfile));
                    GetCurTab()->Status("File has been re-loaded because of modifications of another program / computer");
                }
                return;
            }
        }

    }
    #endif

    DECLARE_EVENT_TABLE()
};

