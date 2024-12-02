struct Image {
    vector<uint8_t> image_data;
    char image_type;
    wxBitmap bm_display;
    int trefc {0};
    int savedindex {-1};
    uint64_t hash {0};

    // This indicates a relative scale, where 1.0 means bitmap pixels match display pixels on
    // a low res 96 dpi display. On a high dpi screen it will look scaled up. Higher values
    // look better on most screens.
    // This is all relative to GetContentScalingFactor.
    double display_scale;
    int pixel_width {0};

    Image(uint64_t _hash, double _sc, vector<uint8_t> &&idv, char iti)
        : image_data(std::move(idv)), image_type(iti), hash(_hash), display_scale(_sc) {}

    void ImageRescale(double sc) {
        auto mapitem = imagetypes.find(image_type);
        if (mapitem == imagetypes.end()) return;
        wxBitmapType it = mapitem->second.first;
        wxImage im = ConvertBufferToWxImage(image_data, it);
        im.Rescale(im.GetWidth() * sc, im.GetHeight() * sc);
        image_data = ConvertWxImageToBuffer(im, it);
        hash = CalculateHash(image_data);
        bm_display = wxNullBitmap;
    }

    void DisplayScale(double sc) {
        display_scale /= sc;
        bm_display = wxNullBitmap;
    }

    void ResetScale(double sc) {
        display_scale = sc;
        bm_display = wxNullBitmap;
    }

    wxBitmap &Display() {
        // This might run in multiple threads in parallel
        // so this function must not touch any global resources
        // and callees must be thread-safe.
        if (!bm_display.IsOk()) {
            auto mapitem = imagetypes.find(image_type);
            if (mapitem == imagetypes.end()) return wxNullBitmap;
            wxBitmapType it = mapitem->second.first;
            wxBitmap bm = ConvertBufferToWxBitmap(image_data, it);
            pixel_width = bm.GetWidth();
            ScaleBitmap(bm, sys->frame->FromDIP(1.0) / display_scale, bm_display);
        }
        return bm_display;
    }
};

struct System {
    MyFrame *frame;
    wxString defaultfont {
    #ifdef WIN32
        L"Lucida Sans Unicode"
    #else
        L"Verdana"
    #endif
    };
    wxString searchstring;
    unique_ptr<wxConfigBase> cfg;
    Evaluator ev;
    wxString clipboardcopy;
    unique_ptr<Cell> cellclipboard;
    vector<unique_ptr<Image>> imagelist;
    vector<int> loadimageids;
    uchar versionlastloaded {0};
    wxLongLong fakelasteditonload;
    wxPen pen_tinytext {wxColour(0x808080ul)};
    wxPen pen_gridborder {wxColour(0xb5a6a4)};
    wxPen pen_tinygridlines {wxColour(0xf2dcd8)};
    wxPen pen_gridlines {wxColour(0xe5b7b0)};
    wxPen pen_thinselect {*wxLIGHT_GREY};
    int roundness {3};
    int defaultmaxcolwidth {80};
    bool makebaks {true};
    bool totray {false};
    bool autosave {true};
    bool zoomscroll {false};
    bool thinselc {true};
    bool minclose {false};
    bool singletray {false};
    bool centered {true};
    bool fswatch {true};
    bool autohtmlexport {false};
    bool casesensitivesearch {true};
    bool darkennonmatchingcells {false};
    bool fastrender {true};
    bool showtoolbar {true};
    bool showstatusbar {true};
    #ifndef SIMPLERENDER
        bool hovershadow {true};
    #endif
    int sortcolumn;
    int sortxs;
    int sortdescending;
    std::set<wxString> watchedpaths;
    bool insidefiledialog {false};
    struct TimerStruct : wxTimer {
        void Notify() {
            sys->SaveCheck();
            sys->cfg->Flush();
        }
    } every_second_timer;
    uint lastcellcolor {0xFFFFFF};
    uint lasttextcolor {0};
    uint lastbordcolor {0xA0A0A0};
    int customcolor {0xFFFFFF};
    #ifdef SIMPLERENDER
    int cursorcolor {0x00FF00};
    #endif

    System(bool portable)
        : cfg(portable ? (wxConfigBase *)new wxFileConfig(
                             L"", wxT(""), wxGetCwd() + wxT("/TreeSheets.ini"), wxT(""), 0)
                       : (wxConfigBase *)new wxConfig(L"TreeSheets")) {
        static const wxDash glpattern[] = {1, 3};
        pen_gridlines.SetDashes(2, glpattern);
        pen_gridlines.SetStyle(wxPENSTYLE_USER_DASH);
        static const wxDash tspattern[] = {2, 4};
        pen_thinselect.SetDashes(2, tspattern);
        pen_thinselect.SetStyle(wxPENSTYLE_USER_DASH);

        roundness = (int)cfg->Read(L"roundness", roundness);
        defaultfont = cfg->Read(L"defaultfont", defaultfont);
        cfg->Read(L"makebaks", &makebaks, makebaks);
        cfg->Read(L"totray", &totray, totray);
        cfg->Read(L"zoomscroll", &zoomscroll, zoomscroll);
        cfg->Read(L"thinselc", &thinselc, thinselc);
        cfg->Read(L"autosave", &autosave, autosave);
        cfg->Read(L"fastrender", &fastrender, fastrender);
        cfg->Read(L"minclose", &minclose, minclose);
        cfg->Read(L"singletray", &singletray, singletray);
        cfg->Read(L"centered", &centered, centered);
        cfg->Read(L"fswatch", &fswatch, fswatch);
        cfg->Read(L"autohtmlexport", &autohtmlexport, autohtmlexport);
        cfg->Read(L"casesensitivesearch", &casesensitivesearch, casesensitivesearch);
        cfg->Read(L"defaultfontsize", &g_deftextsize, g_deftextsize);
        cfg->Read(L"customcolor", &customcolor, customcolor);
        #ifdef SIMPLERENDER
            cfg->Read(L"cursorcolor", &cursorcolor, cursorcolor);
        #else
            cfg->Read(L"hovershadow", &hovershadow, hovershadow);
        #endif
        cfg->Read(L"showtoolbar", &showtoolbar, showtoolbar);
        cfg->Read(L"showstatusbar", &showstatusbar, showstatusbar);
        // fsw.Connect(wxID_ANY, wxID_ANY, wxEVT_FSWATCHER,
        // wxFileSystemWatcherEventHandler(System::OnFileChanged));
    }

    Document *NewTabDoc(bool append = false) {
        Document *doc = new Document();
        frame->NewTab(doc, append);
        return doc;
    }

    void TabChange(Document *newdoc) {
        // SetSelect(hover = Selection());
        newdoc->sw->SetFocus();
        newdoc->UpdateFileName();
        wxClientDC dc(newdoc->sw);
        newdoc->SearchNext(dc, false, false, false);
    }

    void Init(const wxString &filename) {
        ev.Init();

        if (filename.Len()) LoadDB(filename);

        if (!frame->nb->GetPageCount()) {
            int numfiles = (int)cfg->Read(L"numopenfiles", (long)0);
            loop(i, numfiles) {
                wxString fn;
                cfg->Read(wxString::Format(L"lastopenfile_%d", i), &fn);
                LoadDB(fn, true);
            }
        }

        if (!frame->nb->GetPageCount()) LoadTut();

        if (!frame->nb->GetPageCount()) InitDB(10);

        // Refresh();

        frame->bt.Start(BLINK_TIME);
        every_second_timer.Start(1000);
    }

    void LoadTut() {
        auto lang = frame->app->locale.GetCanonicalName();

        if (lang.Len() == 5 &&
            !LoadDB(frame->GetDocPath(L"examples/tutorial-" + lang + ".cts"))[0]) {
            return;
        }

        lang.Truncate(2);
        if (lang.Len() == 2 &&
            !LoadDB(frame->GetDocPath(L"examples/tutorial-" + lang + ".cts"))[0]) {
            return;
        }

        LoadDB(frame->GetDocPath(L"examples/tutorial.cts"));
    }

    void LoadOpRef() { LoadDB(frame->GetDocPath(L"examples/operation-reference.cts")); }

    unique_ptr<Cell> &InitDB(int sizex, int sizey = 0) {
        Cell *c = new Cell(nullptr, nullptr, CT_DATA, new Grid(sizex, sizey ? sizey : sizex));
        c->cellcolor = 0xCCDCE2;
        c->grid->InitCells();
        Document *doc = NewTabDoc();
        doc->InitWith(c, L"", nullptr, 1, 1);
        return doc->rootgrid;
    }

    wxString BakName(const wxString &filename) { return ExtName(filename, L".bak"); }
    wxString TmpName(const wxString &filename) { return ExtName(filename, L".tmp"); }
    wxString ExtName(const wxString &filename, wxString ext) {
        wxFileName fn(filename);
        return fn.GetPathWithSep() + fn.GetName() + ext;
    }

    const wxChar *LoadDB(const wxString &filename, bool frominit = false, bool fromreload = false) {
        wxString fn = filename;
        bool loadedfromtmp = false;

        if (!fromreload) {
            if (frame->GetTabByFileName(filename)) return L"";  //"this file is already loaded";

            if (::wxFileExists(TmpName(filename))) {
                if (::wxMessageBox(
                        _(L"A temporary autosave file exists, would you like to load it instead?"),
                        _(L"Autosave load"), wxYES_NO, frame) == wxYES) {
                    fn = TmpName(filename);
                    loadedfromtmp = true;
                }
            }
        }

        Document *doc = nullptr;
        bool anyimagesfailed = false;
        auto start_loading_time = wxGetLocalTimeMillis();

        {  // limit destructors
            wxBusyCursor wait;
            Cell *ics = nullptr;
            wxFFileInputStream fis(fn);
            wxDataInputStream dis(fis);
            if (!fis.IsOk()) return _(L"Cannot open file.");

            char buf[4];
            fis.Read(buf, 4);
            if (strncmp(buf, "TSFF", 4)) return _(L"Not a TreeSheets file.");
            fis.Read(&versionlastloaded, 1);
            if (versionlastloaded > TS_VERSION) return _(L"File of newer version.");
            int xs, ys;
            if (versionlastloaded >= 21) {
                xs = dis.Read8();
                ys = dis.Read8();
            } else {
                xs = ys = 1;
            }
            int zoomlevel = versionlastloaded >= 23 ? dis.Read8() : 0;
            fakelasteditonload = wxDateTime::Now().GetValue();

            loadimageids.clear();

            for (;;) {
                fis.Read(buf, 1);
                switch (*buf) {
                    case 'I':
                    case 'J': {
                        char iti = *buf;
                        auto mapitem = imagetypes.find(iti);
                        if (mapitem == imagetypes.end())
                            return _(L"Found an image type that is not defined in this program.");
                        if (versionlastloaded < 9) dis.ReadString();
                        double sc = versionlastloaded >= 19 ? dis.ReadDouble() : 1.0;
                        vector<uint8_t> image_data;
                        if (versionlastloaded >= 22) {
                            size_t imagelen = (size_t)dis.Read64();
                            image_data.resize(imagelen);
                            fis.Read(image_data.data(), imagelen);
                        } else {
                            off_t beforeimage = fis.TellI();

                            if (iti == 'I') {
                                uchar header[8];
                                fis.Read(header, 8);
                                uchar expected[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
                                if (memcmp(header, expected, 8)) return _(L"Corrupt PNG header.");
                                dis.BigEndianOrdered(true);
                                for (;;) {  // Skip all chunks.
                                    wxInt32 len = dis.Read32();
                                    char fourcc[4];
                                    fis.Read(fourcc, 4);
                                    fis.SeekI(len, wxFromCurrent);  // skip data
                                    dis.Read32();                   // skip CRC
                                    if (memcmp(fourcc, "IEND", 4) == 0) break;
                                }
                            } else if (iti == 'J') {
                                wxImage im;
                                im.LoadFile(fis);
                                if (!im.IsOk()) { return _(L"JPEG file is corrupted!"); }
                            }

                            off_t afterimage = fis.TellI();
                            fis.SeekI(beforeimage);
                            auto sz = afterimage - beforeimage;
                            image_data.resize(sz);
                            fis.Read(image_data.data(), sz);
                            fis.SeekI(afterimage);
                        }
                        if (!fis.IsOk()) image_data.clear();

                        loadimageids.push_back(AddImageToList(sc, std::move(image_data), iti));
                        break;
                    }

                    case 'D': {
                        wxZlibInputStream zis(fis);
                        if (!zis.IsOk()) return _(L"Cannot decompress file.");
                        wxDataInputStream dis(zis);
                        int numcells = 0, textbytes = 0;
                        Cell *root = Cell::LoadWhich(dis, nullptr, numcells, textbytes, ics);
                        if (!root) return _(L"File corrupted!");

                        doc = NewTabDoc(true);
                        if (loadedfromtmp) {
                            doc->undolistsizeatfullsave =
                                -1;  // if not, user will lose tmp without warning when he closes
                            doc->modified = true;
                        }
                        doc->InitWith(root, filename, ics, xs, ys);
                        doc->initialzoomlevel = zoomlevel;

                        if (versionlastloaded >= 11) {
                            for (;;) {
                                wxString s = dis.ReadString();
                                if (!s.Len()) break;
                                doc->tags.insert(s);
                            }
                        }

                        auto end_loading_time = wxGetLocalTimeMillis();

                        doc->sw->Status(
                            wxString::Format(
                                _(L"Loaded %s (%d cells, %d characters) in %d milliseconds."),
                                filename.c_str(), numcells, textbytes,
                                (int)((end_loading_time - start_loading_time).GetValue()))
                                .c_str());

                        goto done;
                    }

                    default: return _(L"Corrupt block header.");
                }
            }
        }

    done:

        doc->RefreshImageRefCount(false);
        {
            ThreadPool pool(std::thread::hardware_concurrency());
            for (const auto &image : sys->imagelist) {
                pool.enqueue(
                    [](auto *img) {
                        if (img->trefc) img->Display();
                    },
                    image.get());
            }
        }  // wait until all tasks are finished

        FileUsed(filename, doc);
        doc->Refresh();
        if (anyimagesfailed)
            wxMessageBox(_(L"PNG decode failed on some images in this document\nThey have been replaced by red squares."),
                         _(L"PNG decoder failure"), wxOK, frame);

        return L"";
    }

    void FileUsed(const wxString &filename, Document *doc) {
        frame->filehistory.AddFileToHistory(filename);
        RememberOpenFiles();
        if (fswatch) {
            doc->lastmodificationtime = wxFileName(filename).GetModificationTime();
            const wxString &d =
                wxFileName(filename).GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
            if (watchedpaths.insert(d).second) {
                frame->watcher->Add(wxFileName(d), wxFSW_EVENT_ALL);
            }
        }
    }

    const wxChar *Open(const wxString &fn) {
        if (!fn.empty()) {
            auto msg = LoadDB(fn);
            assert(msg);
            if (*msg) wxMessageBox(msg, fn.wx_str(), wxOK, frame);
            return msg;
        }
        return _(L"Open file cancelled.");
    }

    void RememberOpenFiles() {
        int n = (int)frame->nb->GetPageCount();
        int namedfiles = 0;

        loop(i, n) {
            TSCanvas *p = (TSCanvas *)frame->nb->GetPage(i);
            if (p->doc->filename.Len()) {
                cfg->Write(wxString::Format(L"lastopenfile_%d", namedfiles), p->doc->filename);
                namedfiles++;
            }
        }

        cfg->Write(L"numopenfiles", namedfiles);
        cfg->Flush();
    }

    void UpdateStatus(Selection &s) {
        if (frame->GetStatusBar()) {
            Cell *c = s.GetCell();
            if (c && s.xs) {
                frame->SetStatusText(wxString::Format(_(L"Size %d"), -c->text.relsize), 3);
                frame->SetStatusText(wxString::Format(_(L"Width %d"), s.g->colwidths[s.x]), 2);
                frame->SetStatusText(
                    wxString::Format(_(L"Edited %s %s"), c->text.lastedit.FormatDate().c_str(),
                                     c->text.lastedit.FormatTime().c_str()),
                    1);
            }
        }
    }

    void SaveCheck() {
        loop(i, frame->nb->GetPageCount()) {
            ((TSCanvas *)frame->nb->GetPage(i))->doc->AutoSave(!frame->IsActive(), i);
        }
    }

    void SaveAll() {
        loop(i, frame->nb->GetPageCount()) {
            frame->GetCurTab()->doc->Save(false);
            frame->CycleTabs(1);
        }
    }

    const wxChar *Import(int k) {
        wxString fn = ::wxFileSelector(_(L"Please select file to import:"), L"", L"", L"", L"*.*",
                                       wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
        if (!fn.empty()) {
            wxBusyCursor wait;
            switch (k) {
                case A_IMPXML:
                case A_IMPXMLA: {
                    wxXmlDocument doc;
                    if (!doc.Load(fn)) goto problem;
                    unique_ptr<Cell> &r = InitDB(1);
                    Cell *c = *r->grid->cells;
                    FillXML(c, doc.GetRoot(), k == A_IMPXMLA);
                    if (!c->HasText() && c->grid) {
                        *r->grid->cells = nullptr;
                        r.reset(c);
                        c->parent = nullptr;
                    }
                    break;
                }
                case A_IMPTXTI:
                case A_IMPTXTC:
                case A_IMPTXTS:
                case A_IMPTXTT: {
                    wxFFile f(fn);
                    if (!f.IsOpened()) goto problem;
                    wxString s;
                    if (!f.ReadAll(&s)) goto problem;
                    const wxArrayString &as = wxStringTokenize(s, LINE_SEPERATOR);

                    if (as.size()) switch (k) {
                            case A_IMPTXTI: {
                                Cell *r = InitDB(1).get();
                                FillRows(r->grid, as, CountCol(as[0]), 0, 0);
                            }; break;
                            case A_IMPTXTC:
                                InitDB(1, (int)as.size())->grid->CSVImport(as, L',');
                                break;
                            case A_IMPTXTS:
                                InitDB(1, (int)as.size())->grid->CSVImport(as, L';');
                                break;
                            case A_IMPTXTT:
                                InitDB(1, (int)as.size())->grid->CSVImport(as, L'\t');
                                break;
                        }
                    break;
                }
            }
            frame->GetCurTab()->doc->ChangeFileName(fn.Find(L'.') >= 0 ? fn.BeforeLast(L'.') : fn,
                                                    true);
            frame->GetCurTab()->doc->ClearSelectionRefresh();
        }
        return nullptr;
    problem:
        wxMessageBox(_(L"couldn't import file!"), fn, wxOK, frame);
        return _(L"File load error.");
    }

    int GetXMLNodes(wxXmlNode *n, vector<wxXmlNode *> &ns, vector<wxXmlAttribute *> *ps = nullptr,
                    bool attributestoo = false) {
        for (wxXmlNode *child = n->GetChildren(); child; child = child->GetNext()) {
            if (child->GetType() == wxXML_ELEMENT_NODE) ns.push_back(child);
        }
        if (attributestoo && ps)
            for (wxXmlAttribute *child = n->GetAttributes(); child; child = child->GetNext()) {
                ps->push_back(child);
            }
        return ns.size() + (ps ? ps->size() : 0);
    }

    void FillXML(Cell *c, wxXmlNode *n, bool attributestoo) {
        const wxArrayString &as = wxStringTokenize(
            n->GetType() == wxXML_ELEMENT_NODE ? n->GetNodeContent() : n->GetContent());
        loop(i, as.GetCount()) {
            if (c->text.t.Len()) c->text.t.Append(L' ');
            c->text.t.Append(as[i]);
        }

        if (n->GetName() == L"cell") {
            c->text.relsize = -wxAtoi(n->GetAttribute(L"relsize", L"0"));
            c->text.stylebits = wxAtoi(n->GetAttribute(L"stylebits", L"0"));
            c->cellcolor =
                std::stoi(n->GetAttribute(L"colorbg", L"0xFFFFFF").ToStdString(), nullptr, 0);
            c->textcolor =
                std::stoi(n->GetAttribute(L"colorfg", L"0x000000").ToStdString(), nullptr, 0);
            c->celltype = wxAtoi(n->GetAttribute(L"type", L"0"));
        }

        vector<wxXmlNode *> ns;
        vector<wxXmlAttribute *> ps;
        int numrows = GetXMLNodes(n, ns, &ps, attributestoo);
        if (!numrows) return;

        if (ns.size() == 1 && (!c->text.t.Len() || ns[0]->IsWhitespaceOnly()) &&
            ns[0]->GetName() != L"row") {
            FillXML(c, ns[0], attributestoo);
        } else {
            bool allrow = n->GetName() == L"grid";
            for (auto *n : ns)
                if (n->GetName() != L"row") {
                    allrow = false;
                    break;
                }
            if (allrow) {
                int desiredxs;
                loopv(i, ns) {
                    vector<wxXmlNode *> ins;
                    int xs = GetXMLNodes(ns[i], ins);
                    if (!i) {
                        desiredxs = xs ? xs : 1;
                        c->AddGrid(desiredxs, ns.size());
                        SetGridSettingsFromXML(c, n);
                    }
                    loop(j, desiredxs) if (ins.size() > j)
                        FillXML(c->grid->C(j, i), ins[j], attributestoo);
                }
            } else {
                c->AddGrid(1, numrows);
                SetGridSettingsFromXML(c, n);
                loopv(i, ps) c->grid->C(0, i)->text.t = ps[i]->GetValue();
                loopv(i, ns) FillXML(c->grid->C(0, i + ps.size()), ns[i], attributestoo);
            }
        }
    }

    void SetGridSettingsFromXML(Cell *c, wxXmlNode *n) {
        c->grid->folded = wxAtoi(n->GetAttribute(L"folded", L"0"));
        c->grid->bordercolor = std::stoi(
            n->GetAttribute(L"bordercolor", wxString() << g_bordercolor_default).ToStdString(),
            nullptr, 0);
        c->grid->user_grid_outer_spacing =
            wxAtoi(n->GetAttribute(L"outerspacing", wxString() << g_usergridouterspacing_default));
    }

    int CountCol(const wxString &s) {
        int col = 0;
        while (s[col] == ' ' || s[col] == '\t') col++;
        return col;
    }

    int FillRows(Grid *g, const wxArrayString &as, int column, int startrow, int starty) {
        int y = starty;
        for (int i = startrow; i < (int)as.size(); i++) {
            wxString s = as[i];
            int col = CountCol(s);
            if (col < column && startrow != 0) return i;
            if (col > column) {
                Cell *c = g->C(0, y - 1);
                Grid *sg = c->grid;
                i = FillRows(sg ? sg : c->AddGrid(), as, col, i, sg ? sg->ys : 0) - 1;
            } else {
                if (g->ys <= y) g->InsertCells(-1, y, 0, 1);
                Text &t = g->C(0, y)->text;
                t.t = s.Trim(false);
                y++;
            }
        }
        return (int)as.size();
    }

    int AddImageToList(double sc, vector<uint8_t> &&idv, char iti) {
        auto hash = CalculateHash(idv);
        loopv(i, imagelist) {
            if (imagelist[i]->hash == hash) return i;
        }
        imagelist.push_back(make_unique<Image>(hash, sc, std::move(idv), iti));
        return imagelist.size() - 1;
    }

    void ImageSize(wxBitmap *bm, int &xs, int &ys) {
        if (!bm) return;
        xs = bm->GetWidth();
        ys = bm->GetHeight();
    }

    void ImageDraw(wxBitmap *bm, wxDC &dc, int x, int y) { dc.DrawBitmap(*bm, x, y); }
};
