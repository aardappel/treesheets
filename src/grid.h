#pragma once
#include "stdafx.h"
#include "grid.fwd.h"
#include "cell.fwd.h"
#include "evaluator.fwd.h"
#include "selection.fwd.h"
#include "document.fwd.h"
namespace treesheets {

struct Grid
{
#define foreachcell(c)            for(int y = 0;          y<ys;       y++) for(int x = 0;          x<xs;       x++) for(bool _f = true; _f; ) for(Cell *&c =    C(x, y); _f; _f = false)
#define foreachcelly(c)           for(int y = 0;          y<ys;       y++)                                          for(bool _f = true; _f; ) for(Cell *&c =    C(0, y); _f; _f = false)
#define foreachcellcolumn(c)      for(int x = 0;          x<xs;       x++) for(int y = 0;          y<ys;       y++) for(bool _f = true; _f; ) for(Cell *&c =    C(x, y); _f; _f = false)
#define foreachcellinsel(c, s)    for(int y = s.y;        y<s.y+s.ys; y++) for(int x = s.x;        x<s.x+s.xs; x++) for(bool _f = true; _f; ) for(Cell *&c =    C(x, y); _f; _f = false)
#define foreachcellinselrev(c, s) for(int y = s.y+s.ys-1; y>=s.y;     y--) for(int x = s.x+s.xs-1; x>=s.x;     x--) for(bool _f = true; _f; ) for(Cell *&c =    C(x, y); _f; _f = false)
#define foreachcellingrid(c, g)   for(int y = 0;          y<g->ys;    y++) for(int x = 0;          x<g->xs;    x++) for(bool _f = true; _f; ) for(Cell *&c = g->C(x, y); _f; _f = false)

    Cell *cell;

    Cell **cells;
    int *colwidths;
    
    int xs, ys;
    int view_margin, view_grid_outer_spacing, user_grid_outer_spacing, cell_margin;
    int bordercolor;
    
    bool horiz;
    bool tinyborder;
    bool folded;

    Cell *&C(int x, int y);
    Grid(int _xs, int _ys, Cell *_c = NULL);
    ~Grid();
    void InitCells(Cell *clonestylefrom = NULL);
    void InitColWidths();
    void Clone(Grid *g);
    Cell *CloneSel(const Selection &s);
    void SetOrient();
    bool Layout(Document *doc, wxDC &dc, int depth, int &sx, int &sy, int startx, int starty, bool forcetiny);
    void Render(Document *doc, int bx, int by, wxDC &dc, int depth, int sx, int sy, int xoff, int yoff);
    void FindXY(Document *doc, int px, int py, wxDC &dc);
    Cell *FindLink(Selection &s, Cell *link, Cell *best, bool &lastthis, bool &stylematch);
    Cell *FindNextSearchMatch(wxString &search, Cell *best, Cell *selected, bool &lastwasselected);
    void FindReplaceAll(const wxString &str);
    void ReplaceCell(Cell *o, Cell *n);
    Selection FindCell(Cell *o);
    Selection SelectAll();
    void ImageRefCount();
    void DrawHover(Document *doc, wxDC &dc, Selection &s);
    void DrawCursor(Document *doc, wxDC &dc, Selection &s, bool full, uint color, bool cursoronly);
    void DrawInsert(Document *doc, wxDC &dc, Selection &s, uint colour);
    wxRect GetRect(Document *doc, Selection &s, bool minimal = false);
    void DrawSelect(Document *doc, wxDC &dc, Selection &s, bool cursoronly);
    void DeleteCells(int dx, int dy, int nxs, int nys);
    void MultiCellDelete(Document *doc, Selection &s);
    void MultiCellDeleteSub(Document *doc, Selection &s);
    void DelSelf(Document *doc, Selection &s);
    void IdealRelSize(int &rs, bool wantsize = false);
    void InsertCells(int dx, int dy, int nxs, int nys, Cell *nc = NULL);
    void Save(wxDataOutputStream &dos);
    bool LoadContents(wxDataInputStream &dis, int &numcells, int &textbytes);
    void Formatter(wxString &r, int format, int indent, const wxChar *xml, const wxChar *html);
    wxString ToText(int indent, const Selection &s, int format, Document *doc);
    wxString ConvertToText(const Selection &s, int indent, int format, Document *doc);
    void RelSize(int dir, int zoomdepth);
    void RelSize(int dir, Selection &s, int zoomdepth);
    void SetBorder(int width, Selection &s);
    int MinRelsize(int rs);
    void ResetChildren();
    void Move(int dx, int dy, Selection &s);
    void Add(Cell *c);
    void MergeWithParent(Grid *p, Selection &s);
    char *SetStyle(Document *doc, Selection &s, int sb);
    void ColorChange(Document *doc, int which, uint color, Selection &s);
    void ReplaceStr(Document *doc, const wxString &str, Selection &s);
    void CSVImport(const wxArrayString &as, wxChar sep);
    Cell *EvalGridCell(Evaluator &ev, Cell *&c, Cell *acc, int &x, int &y, bool &alldata, bool vert);
    Cell *Eval(Evaluator &ev);
    void Split(Vector<Grid *> &gs, bool vert);
    Cell *Sum();
    Cell *Transpose();
    static int sortfunc(const Cell **a, const Cell **b);
    void Sort(Selection &s, bool descending);
    Cell *FindExact(wxString &s);
    Selection HierarchySwap(wxString tag);
    void ReParent(Cell *p);
    Cell *DeleteTagParent(Cell *tag, Cell *basecell, Cell *found);
    void MergeTagCell(Cell *f, Cell *&selcell);
    void MergeTagAll(Cell *into);
    void SetGridTextLayout(int ds, bool vert, bool noset, const Selection &s);
    bool IsTable();
    void Hierarchify(Document *doc);
    void MergeRow(Grid *tm);
    void MaxDepthLeaves(int curdepth, int &maxdepth, int &leaves);
    int Flatten(int curdepth, int cury, Grid *g);
    void ResizeColWidths(int dir, const Selection &s, bool hierarchical);
    void CollectCells(Vector<Cell *> &itercells);
    void CollectCellsSel(Vector<Cell *> &itercells, Selection &s, bool recurse = true);
    void SetStyles(Selection &s, Cell *o);
    void ClearImages(Selection &s);
};
}
