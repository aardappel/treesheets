#pragma once
#include "stdafx.h"
#include "myframe.fwd.h"
#include "document.fwd.h"

namespace treesheets {
struct TSCanvas : public wxScrolledWindow
{
    MyFrame *frame;
    Document *doc;
    
    int mousewheelaccum;
    bool lastrmbwaswithctrl;
    wxPoint lastmousepos;
    TSCanvas(MyFrame *fr, wxWindow *parent, const wxSize &size = wxDefaultSize);
    ~TSCanvas() ;
    void OnPaint( wxPaintEvent &event );
    void UpdateHover(int mx, int my, wxDC &dc);
    void OnMotion(wxMouseEvent &me);
    void SelectClick(int mx, int my, bool right, int isctrlshift);
    void OnLeftDown(wxMouseEvent &me);
    void OnLeftUp(wxMouseEvent &me);
    void OnRightDown(wxMouseEvent &me);
    void OnLeftDoubleClick(wxMouseEvent &me);
    void OnChar(wxKeyEvent &ce);
    void OnMouseWheel(wxMouseEvent &me);
    void OnSize(wxSizeEvent &se);
    void OnContextMenuClick(wxContextMenuEvent &cme);
    void CursorScroll(int dx, int dy);
    void Status(const char *msg = NULL);
    DECLARE_EVENT_TABLE()
};
}
