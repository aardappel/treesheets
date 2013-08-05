#pragma once
#include "myframe.fwd.h"
#include "cell.fwd.h"
#include "grid.fwd.h"
#include "selection.fwd.h"
#include "evaluator.h"
#include "document.fwd.h"
namespace treesheets {

struct Image
{
    wxBitmap bm;

    int trefc;
    int savedindex;
    int checksum;

    Image(wxBitmap _bm, int _cs);

    void Scale(float sc);
};

struct System
{
    MyFrame *frame;

    wxString defaultfont, searchstring;

    wxConfig cfg;

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
    
    int sortcolumn, sortxs, sortdescending;
    
    bool fastrender;
    wxHashMapBool watchedpaths;
    
    bool insidefiledialog;
    
    struct SaveChecker : wxTimer
    {   
        void Notify();
    } savechecker;
        
    System();
    ~System();
    Document *NewTabDoc();
    void TabChange(Document *newdoc);
    void Init(int argc, wxChar **argv);
    void LoadTut();
    Cell *&InitDB(int sizex, int sizey = 0);
    wxString BakName(const wxString &filename);
    wxString TmpName(const wxString &filename);
    wxString ExtName(const wxString &filename, wxString ext);
    const char *LoadDB(const wxString &filename, bool frominit = false, bool fromreload = false);
    void FileUsed(const wxString &filename, Document *doc);
    const char *Open(const wxString &fn);
    void RememberOpenFiles();
    void UpdateStatus(Selection &s);
    void SaveCheck();
    const char *Import(int k);
    int GetXMLNodes(wxXmlNode *n, Vector<wxXmlNode *> &ns, Vector<wxXmlAttribute *> *ps = NULL, bool attributestoo = false);
    void FillXML(Cell *c, wxXmlNode *n, bool attributestoo);
    int CountCol(const wxString &s);
    int FillRows(Grid *g, const wxArrayString &as, int column, int startrow, int starty);
    int AddImageToList(const wxImage &im);
    void ImageSize(Image *image, int &xs, int &ys);
    void ImageDraw(Image *image, wxDC &dc, int x, int y, int xs, int ys);
};
extern System *sys;
}
