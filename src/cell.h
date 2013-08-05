#pragma once
#include "evaluator.fwd.h"
#include "grid.fwd.h"
#include "cell.fwd.h"
#include "document.fwd.h"
#include "selection.fwd.h"
#include "text.h"
namespace treesheets {

enum { CT_DATA = 0, CT_CODE, CT_VARD, CT_VIEWH, CT_VARU, CT_VIEWV };
enum { DS_GRID, DS_BLOBSHIER, DS_BLOBLINE };

struct Cell
{
    Cell *parent;
    int sx, sy, ox, oy, minx, miny, ycenteroff, txs, tys;
    int celltype;

    Text text;
    Grid *grid;
    
    uint cellcolor, textcolor, actualcellcolor;

    bool tiny;
    bool verticaltextandgrid;

    wxUint8 drawstyle;
    
    Cell(Cell *_p = NULL, Cell *_clonefrom = NULL, int _ct = CT_DATA, Grid *_g = NULL);
    ~Cell();
    void Clear();
    bool HasText();
    bool HasTextSize();
    bool HasTextState();
    bool HasHeader();
    bool HasContent();
    bool GridShown(Document *doc);
    int MinRelsize();   // the smallest relsize is actually the biggest text
    void Layout(Document *doc, wxDC &dc, int depth, int maxcolwidth, bool forcetiny);
    void Render(Document *doc, int bx, int by, wxDC &dc, int depth, int ml, int mr, int mt, int mb, int maxcolwidth, int cell_margin);
    void CloneStyleFrom(Cell *o);
    Cell *Clone(Cell *_p);
    bool IsInside(int x, int y);
    int GetX(Document *doc);
    int GetY(Document *doc);
    int Depth();
    Cell *Parent(int i);
    Cell *SetParent(Cell *g);
    uint SwapColor(uint c);
    wxString ToText(int indent, const Selection &s, int format, Document *doc);
    void RelSize(int dir, int zoomdepth);
    void Reset();
    void ResetChildren();
    void ResetLayout();
    void LazyLayout(Document *doc, wxDC &dc, int depth, int maxcolwidth, bool forcetiny);
    void AddUndo(Document *doc);
    void Save(wxDataOutputStream &dos);
    Grid *AddGrid(int x = 1, int y = 1);
    Cell *LoadGrid(wxDataInputStream &dis, int &numcells, int &textbytes);
    static Cell *LoadWhich(wxDataInputStream &dis, Cell *_p, int &numcells, int &textbytes);
    Cell *Eval(Evaluator &ev);
    void Paste(Document *doc, Cell *c, Selection &s);
    Cell *FindNextSearchMatch(wxString &search, Cell *best, Cell *selected, bool &lastwasselected);
    Cell *FindLink(Selection &s, Cell *link, Cell *best, bool &lastthis, bool &stylematch);
    void FindReplaceAll(const wxString &str);
    Cell *FindExact(wxString &s);
    void ImageRefCount();
    void SetBorder(int width);
    void ColorChange(int which, uint color);
    void SetGridTextLayout(int ds, bool vert, bool noset);
    bool IsTag(Document *doc);
    void MaxDepthLeaves(int curdepth, int &maxdepth, int &leaves);
    int ColWidth();
    void CollectCells(Vector<Cell *> &itercells, bool recurse = true);
};

}
