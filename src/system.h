
struct Image {
    wxBitmap bm_orig;
    wxBitmap bm_display;

    int trefc;
    int savedindex;
    int checksum;

    // This indicates a relative scale, where 1.0 means bitmap pixels match display pixels on
    // a low res 96 dpi display. On a high dpi screen it will look scaled up. Higher values
    // look better on most screens.
    // This is all relative to GetContentScalingFactor.
    double display_scale;

    Image(wxBitmap _bm, int _cs, double _sc) : bm_orig(_bm), checksum(_cs), display_scale(_sc) {}

    void BitmapScale(double sc) {
        ScaleBitmap(bm_orig, sc, bm_orig);
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
        if (!bm_display.IsOk()) {
            ScaleBitmap(bm_orig, 1.0 / display_scale * sys->frame->csf, bm_display);
            // FIXME: this won't work because it will ignore the cell's bg color.
            //MakeInternallyScaled(bm_display, *wxWHITE, sys->frame->csf_orig);
        }
        return bm_display;
    }
};

struct System {
    MyFrame *frame;

    wxString defaultfont, searchstring;

    wxConfigBase *cfg;

    Evaluator ev;

    wxString clipboardcopy;
    Cell *cellclipboard;

    Vector<Image *> imagelist;
    Vector<int> loadimageids;

    uchar versionlastloaded;
    wxLongLong fakelasteditonload;

    wxPen pen_tinytext, pen_gridborder, pen_tinygridlines, pen_gridlines, pen_thinselect;

    uint customcolor;

    int roundness;
    int defaultmaxcolwidth;

    bool makebaks;
    bool totray;
    bool autosave;
    bool zoomscroll;
    bool thinselc;
    bool minclose;
    bool singletray;
    bool centered;
    bool fswatch;
    bool autohtmlexport;

    int sortcolumn, sortxs, sortdescending;

    bool fastrender;
    wxHashMapBool watchedpaths;

    bool insidefiledialog;

    struct SaveChecker : wxTimer {
        void Notify() {
            sys->SaveCheck();
            sys->cfg->Flush();
        }
    } savechecker;

    System(bool portable)
        : cfg(portable ? (wxConfigBase *)new wxFileConfig(
                             L"", wxT(""), wxGetCwd() + wxT("/TreeSheets.ini"), wxT(""), 0)
                       : (wxConfigBase *)new wxConfig(L"TreeSheets")),
          cellclipboard(nullptr),
          defaultfont(
            #ifdef WIN32
              L"Lucida Sans Unicode"
            #else
              L"Verdana"
            #endif
              ),
          pen_tinytext(wxColour(0x808080ul)),
          pen_gridborder(wxColour(0xb5a6a4)),
          pen_tinygridlines(wxColour(0xf2dcd8)),
          pen_gridlines(wxColour(0xe5b7b0)),
          pen_thinselect(*wxLIGHT_GREY),
          versionlastloaded(0),
          customcolor(0xFFFFFF),
          roundness(3),
          defaultmaxcolwidth(80),
          makebaks(true),
          totray(false),
          autosave(true),
          fastrender(true),
          zoomscroll(false),
          thinselc(true),
          minclose(false),
          singletray(false),
          centered(true),
          fswatch(true),
          autohtmlexport(false),
          insidefiledialog(false) {
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

        cfg->Read(L"defaultfontsize", &g_deftextsize, g_deftextsize);

        // fsw.Connect(wxID_ANY, wxID_ANY, wxEVT_FSWATCHER,
        // wxFileSystemWatcherEventHandler(System::OnFileChanged));
    }

    ~System() {
        DELETEP(cfg);
        DELETEP(cellclipboard);
    }

    Document *NewTabDoc(bool append = false) {
        Document *doc = new Document();
        frame->NewTab(doc, append);
        return doc;
    }

    void TabChange(Document *newdoc) {
        // hover = selected = begindrag = Selection();
        newdoc->sw->SetFocus();
        newdoc->UpdateFileName();
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

        frame->bt.Start(400);
        savechecker.Start(1000);

        ScriptInit(frame);
    }

    void LoadTut() {
        auto lang = frame->app->locale.GetCanonicalName();
        lang.Truncate(2);
        if (LoadDB(frame->GetPath(L"examples/tutorial-" + lang + ".cts"))[0]) {
            LoadDB(frame->GetPath(L"examples/tutorial.cts"));
        }
    }

    Cell *&InitDB(int sizex, int sizey = 0) {
        Cell *c = new Cell(nullptr, nullptr, CT_DATA, new Grid(sizex, sizey ? sizey : sizex));
        c->cellcolor = 0xCCDCE2;
        c->grid->InitCells();
        Document *doc = NewTabDoc();
        doc->InitWith(c, L"");
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
            if (frame->GetTabByFileName(filename)) return nullptr;  //"this file is already loaded";

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

        {  // limit destructors
            wxBusyCursor wait;
            wxFFileInputStream fis(fn);
            if (!fis.IsOk()) return _(L"Cannot open file.");

            char buf[4];
            fis.Read(buf, 4);
            if (strncmp(buf, "TSFF", 4)) return _(L"Not a TreeSheets file.");
            fis.Read(&versionlastloaded, 1);
            if (versionlastloaded > TS_VERSION) return _(L"File of newer version.");

            fakelasteditonload = wxDateTime::Now().GetValue();

            loadimageids.setsize(0);

            for (;;) {
                fis.Read(buf, 1);
                switch (*buf) {
                    case 'I': {
                        wxDataInputStream dis(fis);
                        if (versionlastloaded < 9) dis.ReadString();
                        wxImage im;
                        double sc = versionlastloaded >= 19 ? dis.ReadDouble() : 1.0;
                        off_t beforepng = fis.TellI();
                        bool ok = im.LoadFile(fis);
                        // ok = false;
                        if (!ok) {
                            // Uhoh.. the decoder failed. Try to save the situation by skipping this
                            // PNG.
                            anyimagesfailed = true;
                            if (beforepng == wxInvalidOffset) return _(L"Cannot tell/seek document?");
                            fis.SeekI(beforepng);
                            // Now try to skip past this PNG
                            uchar header[8];
                            fis.Read(header, 8);
                            uchar expected[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
                            if (memcmp(header, expected, 8)) return _(L"Corrupt PNG header.");
                            dis.BigEndianOrdered(true);
                            for (;;)  // Skip all chunks.
                            {
                                wxInt32 len = dis.Read32();
                                char fourcc[4];
                                fis.Read(fourcc, 4);
                                fis.SeekI(len, wxFromCurrent);  // skip data
                                dis.Read32();                   // skip CRC
                                if (memcmp(fourcc, "IEND", 4) == 0) break;
                            }
                            // Set empty image here, since document expect there to be one here.
                            int sz = 32;
                            im.Create(sz, sz, false);
                            im.SetRGB(wxRect(0, 0, sz, sz), 0xFF, 0,
                                      0);  // Set to red to indicate error.
                        }
                        loadimageids.push() = AddImageToList(im, sc);
                        break;
                    }

                    case 'D': {
                        wxZlibInputStream zis(fis);
                        if (!zis.IsOk()) return _(L"Cannot decompress file.");
                        wxDataInputStream dis(zis);
                        int numcells = 0, textbytes = 0;
                        Cell *root = Cell::LoadWhich(dis, nullptr, numcells, textbytes);
                        if (!root) return _(L"File corrupted!");

                        doc = NewTabDoc(true);
                        if (loadedfromtmp) {
                            doc->undolistsizeatfullsave =
                                -1;  // if not, user will lose tmp without warning when he closes
                            doc->modified = true;
                        }
                        doc->InitWith(root, filename);

                        if (versionlastloaded >= 11) {
                            for (;;) {
                                wxString s = dis.ReadString();
                                if (!s.Len()) break;
                                doc->tags[s] = true;
                            }
                        }

                        doc->sw->Status(wxString::Format(_(L"Loaded %s (%d cells, %d characters)."),
                                                         filename.c_str(), numcells, textbytes)
                                            .c_str());

                        goto done;
                    }

                    default: return _(L"Corrupt block header.");
                }
            }
        }

    done:

        FileUsed(filename, doc);

        doc->ClearSelectionRefresh();

        if (anyimagesfailed)
            wxMessageBox(
                _(L"PNG decode failed on some images in this document\nThey have been replaced by "
                  L"red squares."),
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
            if (watchedpaths.find(d) == watchedpaths.end()) {
                watchedpaths[d] = true;
                frame->watcher->Add(wxFileName(d), wxFSW_EVENT_ALL);
            }
        }
    }

    const wxChar *Open(const wxString &fn) {
        if (!fn.empty()) {
            auto msg = LoadDB(fn);
            if (msg && *msg) wxMessageBox(msg, fn.wx_str(), wxOK, frame);
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
                    wxString::Format(_(L"Edited %s"), c->text.lastedit.FormatDate().c_str()), 1);
            }
        }
    }

    void SaveCheck() {
        loop(i, frame->nb->GetPageCount()) {
            ((TSCanvas *)frame->nb->GetPage(i))->doc->AutoSave(!frame->IsActive(), i);
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
                    Cell *&r = InitDB(1);
                    Cell *c = *r->grid->cells;
                    FillXML(c, doc.GetRoot(), k == A_IMPXMLA);
                    if (!c->HasText() && c->grid) {
                        *r->grid->cells = nullptr;
                        delete r;
                        r = c;
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
                                Cell *r = InitDB(1);
                                FillRows(r->grid, as, CountCol(as[0]), 0, 0);
                            }; break;
                            case A_IMPTXTC: InitDB(1, (int)as.size())->grid->CSVImport(as, L','); break;
                            case A_IMPTXTS: InitDB(1, (int)as.size())->grid->CSVImport(as, L';'); break;
                            case A_IMPTXTT: InitDB(1, (int)as.size())->grid->CSVImport(as, L'\t'); break;
                        }
                    break;
                }
            }
            frame->GetCurTab()->doc->ChangeFileName(fn.BeforeLast(L'.').Append(L".cts"));
            frame->GetCurTab()->doc->ClearSelectionRefresh();
        }
        return nullptr;
    problem:
        wxMessageBox(_(L"couldn't import file!"), fn, wxOK, frame);
        return _(L"File load error.");
    }

    int GetXMLNodes(wxXmlNode *n, Vector<wxXmlNode *> &ns, Vector<wxXmlAttribute *> *ps = nullptr,
                    bool attributestoo = false) {
        for (wxXmlNode *child = n->GetChildren(); child; child = child->GetNext()) {
            if (child->GetType() == wxXML_ELEMENT_NODE) ns.push() = child;
        }
        if (attributestoo && ps)
            for (wxXmlAttribute *child = n->GetAttributes(); child; child = child->GetNext()) {
                ps->push() = child;
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
            c->cellcolor = wxAtoi(n->GetAttribute(L"colorbg", L"16777215"));
            c->textcolor = wxAtoi(n->GetAttribute(L"colorfg", L"0"));
        }

        Vector<wxXmlNode *> ns;
        Vector<wxXmlAttribute *> ps;
        int numrows = GetXMLNodes(n, ns, &ps, attributestoo);
        if (!numrows) return;

        if (ns.size() == 1 && (!c->text.t.Len() || ns[0]->IsWhitespaceOnly()) &&
            ns[0]->GetName() != L"row") {
            FillXML(c, ns[0], attributestoo);
        } else {
            bool allrow = n->GetName() == L"grid";
            loopv(i, ns) if (ns[i]->GetName() != L"row") allrow = false;
            if (allrow) {
                int desiredxs;
                loopv(i, ns) {
                    Vector<wxXmlNode *> ins;
                    int xs = GetXMLNodes(ns[i], ins);
                    if (!i) {
                        desiredxs = xs ? xs : 1;
                        c->AddGrid(desiredxs, ns.size());
                    }
                    loop(j, desiredxs) if (ins.size() > j)
                        FillXML(c->grid->C(j, i), ins[j], attributestoo);
                    ins.setsize_nd(0);
                }
            } else {
                c->AddGrid(1, numrows);
                loopv(i, ps) c->grid->C(0, i)->text.t = ps[i]->GetValue();
                loopv(i, ns) FillXML(c->grid->C(0, i + ps.size()), ns[i], attributestoo);
            }
        }

        ns.setsize_nd(0);
        ps.setsize_nd(0);
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

    int AddImageToList(const wxImage &im, double sc) {
        uint *p = (uint *)im.GetData();
        uint checksum = im.GetWidth() | (im.GetHeight() << 16);
        loop(i, im.GetWidth() * im.GetHeight() * 3 / 4) checksum ^= *p++;

        loopv(i, imagelist) {
            if (imagelist[i]->checksum == checksum) return i;
        }

        imagelist.push() = new Image(wxBitmap(im), checksum, sc);
        return imagelist.size() - 1;
    }

    void ImageSize(wxBitmap *bm, int &xs, int &ys) {
        if (!bm) return;
        xs = bm->GetWidth();
        ys = bm->GetHeight();
    }

    void ImageDraw(wxBitmap *bm, wxDC &dc, int x, int y) {
        dc.DrawBitmap(*bm, x, y);
    }
};
