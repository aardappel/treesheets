#pragma once
#include "stdafx.h"
#include "document.fwd.h"
#include "grid.fwd.h"
#include "cell.fwd.h"
namespace treesheets {
class Selection
{
    bool textedit;

    public:
    Grid *g;
    int x, y, xs, ys;
    int cursor, cursorend;
    int firstdx, firstdy;

    Selection();
    Selection(Grid *_g, int _x, int _y, int _xs, int _ys);

    void SelAll();

    Cell *GetCell()  const;
    Cell *GetFirst() const;

    bool EqLoc(const Selection &s);
    bool operator==(const Selection &s);

    bool Thin() const;

    bool IsAll() const;

    void SetCursorEdit(Document *doc, bool edit);

    bool TextEdit();
    void EnterEditOnly(Document *doc);
    void EnterEdit(Document *doc, int c = 0, int ce = 0);
    void ExitEdit(Document *doc);

    bool IsInside(Selection &o);

    void Merge(const Selection &a, const Selection &b);

    int MaxCursor();

    char *Dir(Document *doc, bool ctrl, bool shift, wxDC &dc, int dx, int dy, int &v, int &vs, int &ovs, bool notboundaryperp, bool notboundarypar, bool exitedit);

    char *Cursor(Document *doc, int k, bool ctrl, bool shift, wxDC &dc, bool exitedit = false);

    void Next(Document *doc, wxDC &dc, bool backwards);
    
    const char *Wrap(Document *doc);

    Cell *ThinExpand(Document *doc);

    const char *HomeEnd(Document *doc, wxDC &dc, bool ishome);
};
}
