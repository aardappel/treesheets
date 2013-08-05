#include "mycanvas.h"
#include "myframe.h"
#include "document.h"
#include "system.h"
#include "globals.h"

namespace treesheets {
TSCanvas::TSCanvas(MyFrame *fr, wxWindow *parent, const wxSize &size) : frame(fr), wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, size, wxScrolledWindowStyle|wxWANTS_CHARS), mousewheelaccum(0), doc(NULL), lastrmbwaswithctrl(false)
{
    SetBackgroundStyle(wxBG_STYLE_CUSTOM);
    SetBackgroundColour(*wxWHITE);
}

TSCanvas::~TSCanvas() { DELETEP(doc); frame = NULL; }

void TSCanvas::OnPaint( wxPaintEvent &event )
{
    wxAutoBufferedPaintDC dc(this);
    //DoPrepareDC(dc);
    doc->Draw(dc);
}

void TSCanvas::UpdateHover(int mx, int my, wxDC &dc)
{
    int x, y;
    CalcUnscrolledPosition(mx, my, &x, &y);
    DoPrepareDC(dc);
    doc->Hover(x/doc->currentviewscale, y/doc->currentviewscale, dc);
}

void TSCanvas::OnMotion(wxMouseEvent &me)
{
    wxClientDC dc(this);
    UpdateHover(me.GetX(), me.GetY(), dc);
    if(me.LeftIsDown() || me.RightIsDown()) Status(doc->Drag(dc));
    else if(me.MiddleIsDown())
    {
        wxPoint p = me.GetPosition()-lastmousepos;
        CursorScroll(-p.x, -p.y);
    }
    
    lastmousepos = me.GetPosition();
}

void TSCanvas::SelectClick(int mx, int my, bool right, int isctrlshift)
{
    wxClientDC dc(this);
    UpdateHover(mx, my, dc);
    Status(doc->Select(dc, right, isctrlshift));
}

void TSCanvas::OnLeftDown(wxMouseEvent &me)
{
    #ifndef __WXMSW__    // seems to not want to give the sw focus otherwise (thinks its already in focus when its not?)
            if(frame->filter) frame->filter->SetFocus();
    #endif
    SetFocus();
    if(me.ShiftDown()) OnMotion(me);
    else SelectClick(me.GetX(), me.GetY(), false, me.CmdDown()+me.AltDown()*2);
}

void TSCanvas::OnLeftUp(wxMouseEvent &me)
{
    if(me.CmdDown() || me.AltDown()) doc->SelectUp();
}

void TSCanvas::OnRightDown(wxMouseEvent &me)
{
    SetFocus();
    SelectClick(me.GetX(), me.GetY(), true, 0);
    lastrmbwaswithctrl = me.CmdDown();
    #ifndef __WXMSW__
            me.Skip();  // otherwise EVT_CONTEXT_MENU won't be triggered?
    #endif
}

void TSCanvas::OnLeftDoubleClick(wxMouseEvent &me)
{
	wxClientDC dc(this);
    UpdateHover(me.GetX(), me.GetY(), dc);
    Status(doc->DoubleClick(dc));
}

void TSCanvas::OnChar(wxKeyEvent &ce)
{
    /*
    if (sys->insidefiledialog)
    {
        ce.Skip();
        return;
    }
    */

    if(ce.AltDown() && wxIsalpha(ce.GetUnicodeKey())) { ce.Skip(); return; }    // only way to avoid keyboard menu nav to not work?

    wxClientDC dc(this);
    DoPrepareDC(dc);
    bool unprocessed = false;
    Status(doc->Key(dc, ce.GetUnicodeKey(), ce.GetKeyCode(), ce.AltDown(), ce.CmdDown(), ce.ShiftDown(), unprocessed));
    if(unprocessed) ce.Skip();
}

void TSCanvas::OnMouseWheel(wxMouseEvent &me)
{
    bool ctrl = me.CmdDown();
    if(sys->zoomscroll) ctrl = !ctrl;
    //wxLogError(L"%d %d %d\n", me.AltDown(), me.ShiftDown(), me.CmdDown());
    wxClientDC dc(this);
    if(me.AltDown() || ctrl || me.ShiftDown())
    {
        mousewheelaccum += me.GetWheelRotation();
        int steps = mousewheelaccum/me.GetWheelDelta();
        if(!steps) return;
        mousewheelaccum -= steps*me.GetWheelDelta();

        UpdateHover(me.GetX(), me.GetY(), dc);
        Status(doc->Wheel(dc, steps, me.AltDown(), ctrl, me.ShiftDown()));      
    }
    else if(me.GetWheelAxis())
    {
        CursorScroll(me.GetWheelRotation()*g_scrollratewheel, 0);
        UpdateHover(me.GetX(), me.GetY(), dc);
    }
    else
    {
        CursorScroll(0, -me.GetWheelRotation()*g_scrollratewheel);
        UpdateHover(me.GetX(), me.GetY(), dc);
    }
}

void TSCanvas::OnSize(wxSizeEvent &se)
{
    doc->Refresh();
}

void TSCanvas::OnContextMenuClick(wxContextMenuEvent &cme)
{
    if(lastrmbwaswithctrl)
    {
        wxMenu *tagmenu = new wxMenu();
        doc->RecreateTagMenu(*tagmenu);
        PopupMenu(tagmenu);
        delete tagmenu;
    }
    else
    {
        PopupMenu(frame->editmenupopup);        
    }
}

void TSCanvas::CursorScroll(int dx, int dy)
{
    int x, y;
    GetViewStart(&x, &y);
    x += dx;
    y += dy;
    Scroll(x, y);
}

void TSCanvas::Status(const char *msg)
{
    if(frame->GetStatusBar() && (!msg || *msg)) frame->SetStatusText(msg ? wxString::Format(L"%s", msg) : L"", 0);
    // using Format instead of FromAscii since the latter doesn't deal with >128 international ascii chars
}
}
BEGIN_EVENT_TABLE(treesheets::TSCanvas, wxScrolledWindow)
  EVT_MOUSEWHEEL(treesheets::TSCanvas::OnMouseWheel)
  EVT_PAINT(treesheets::TSCanvas::OnPaint)
  EVT_MOTION(treesheets::TSCanvas::OnMotion)
  EVT_LEFT_DOWN(treesheets::TSCanvas::OnLeftDown)
  EVT_LEFT_UP(treesheets::TSCanvas::OnLeftUp)
  EVT_RIGHT_DOWN(treesheets::TSCanvas::OnRightDown)
  EVT_LEFT_DCLICK(treesheets::TSCanvas::OnLeftDoubleClick)
  EVT_CHAR(treesheets::TSCanvas::OnChar)
  EVT_CONTEXT_MENU(treesheets::TSCanvas::OnContextMenuClick)
  EVT_SIZE(treesheets::TSCanvas::OnSize)
END_EVENT_TABLE()
