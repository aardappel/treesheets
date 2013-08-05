#pragma once
#include "stdafx.h"
#include "document.fwd.h"
#include "mycanvas.fwd.h"
#include "selection.h"
namespace treesheets {
struct UndoItem
{
    Vector<Selection> path, selpath;
    Selection sel;
    Cell *clone;

    UndoItem();
    ~UndoItem();
};

struct Document
{
    TSCanvas *sw;

    Cell *rootgrid;
    Selection hover, selected, begindrag;
    int isctrlshiftdrag;

    int originx, originy, maxx, maxy, centerx, centery;
    int layoutxs, layoutys, hierarchysize, fgutter;
    int lasttextsize, laststylebits;
    Cell *curdrawroot;  // for use during Render() calls

    Vector<UndoItem *> undolist, redolist;
    Vector<Selection> drawpath;
    int pathscalebias;

    wxString filename;

    long lastmodsinceautosave, undolistsizeatfullsave, lastsave;
    bool modified, tmpsavesuccess;

    wxDataObjectComposite *dataobjc;
    wxTextDataObject      *dataobjt;
    wxBitmapDataObject    *dataobji;
    wxFileDataObject      *dataobjf;

    struct MyPrintout : wxPrintout
    {
        Document *doc;
        MyPrintout(Document *d);

        bool OnPrintPage(int page);

        bool OnBeginDocument(int startPage, int endPage);

        void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo);

        bool HasPage(int pageNum);
    };

    bool while_printing;
    wxPrintData printData;
    wxPageSetupDialogData pageSetupData;
    uint printscale;

    bool blink;

    bool redrawpending;

    bool scaledviewingmode;
    double currentviewscale;
    
    bool searchfilter;
    
    wxHashMapBool tags;
    
    int editfilter;

    Vector<Cell *> itercells;

    wxDateTime lastmodificationtime;
    
    Document();
    ~Document();
    uint Background();
    void InitWith(Cell *r, wxString filename);
    void UpdateFileName(int page = -1);
    void ChangeFileName(const wxString &fn);
    const char *SaveDB(bool *success, bool istempfile = false, int page = -1);
    void DrawSelect(wxDC &dc, Selection &s, bool refreshinstead = false, bool cursoronly = false);
    void DrawSelectMove(wxDC &dc, Selection &s, bool refreshalways = false, bool refreshinstead = true);
    bool ScrollIfSelectionOutOfView(wxDC &dc, Selection &s, bool refreshalways = false);
    void ScrollOrZoom(wxDC &dc, bool zoomiftiny = false);
    void ZoomTiny(wxDC &dc);
    void Blink();
    void ResetCursor();
    void Hover(int x, int y, wxDC &dc);
    char *Select(wxDC &dc, bool right, int isctrlshift);
    void SelectUp();
    char *Drag(wxDC &dc);
    void Zoom(int dir, wxDC &dc, bool fromroot = false, bool selectionmaybedrawroot = true);
    const char *NoSel();
    const char *OneCell();
    const char *NoThin();
    const char *NoGrid();
    const char *Wheel(wxDC &dc, int dir, bool alt, bool ctrl, bool shift, bool hierarchical = true);
    void Layout(wxDC &dc);
    void ShiftToCenter(wxDC &dc);
    void Render(wxDC &dc);
    void Draw(wxDC &dc);
    void Print(wxDC &dc, wxPrintout &po);
    int TextSize(int depth, int relsize);
    bool FontIsMini(int textsize);
    bool PickFont(wxDC &dc, int depth, int relsize, int stylebits);
    void ResetFont();
    void RefreshReset();
    void Refresh();
    void RefreshHover();
    /*void RefreshCell()
    {
        GetSW()->RefreshRect(selected.g->GetRect(selected));
    }*/
    void ClearSelectionRefresh();
    bool CheckForChanges();
    bool CloseDocument();
    const char *DoubleClick(wxDC &dc);
    const char *Export(wxDC &dc, const wxChar *fmt, const wxChar *pat, const wxChar *msg, int k);
    const char *Save(bool saveas, bool *success = NULL);
    void AutoSave(bool minimized, int page);
    const char *Key(wxDC &dc, wxChar uk, int k, bool alt, bool ctrl, bool shift, bool &unprocessed);
    const char *Action(wxDC &dc, int k);
    char *SearchNext(wxDC &dc);
    uint PickColor(wxFrame *fr, uint defcol);
    const char *layrender(int ds, bool vert, bool toggle = false, bool noset = false);
    void ZoomOutIfNoGrid(wxDC &dc);
    void PasteSingleText(Cell *c, const wxString &t);
    void PasteOrDrop();
    const char *Sort(bool descending);
    void DelRowCol(int &v, int e, int gvs, int dec, int dx, int dy, int nxs, int nys);
    void CreatePath(Cell *c, Vector<Selection> &path);
    Cell *WalkPath(Vector<Selection> &path);
    bool LastUndoSameCell(Cell *c);
    void AddUndo(Cell *c);
    void Undo(wxDC &dc, Vector<UndoItem *> &fromlist, Vector<UndoItem *> &tolist, bool redo = false);
    void ColorChange(int which, int idx);
    void SetImageBM(Cell *c, const wxImage &im);
    bool LoadImageIntoCell(const wxString &fn, Cell *c);
    void ImageChange(wxString &fn);
    void RecreateTagMenu(wxMenu &menu);
    const char *TagSet(int tagno);
    void CollectCells();
    void CollectCellsSel(bool recurse = true);
    static int _timesort(const Cell **a, const Cell **b);
    void ApplyEditFilter();
    void SetSearchFilter(bool on);
};

}
