
BEGIN_EVENT_TABLE(treesheets::MyFrame, wxFrame)
  EVT_SIZING(treesheets::MyFrame::OnSizing)
  //EVT_SIZE(treesheets::MyFrame::OnSize)
  EVT_MENU(wxID_ANY, treesheets::MyFrame::OnMenu)
  EVT_TEXT(A_SEARCH, treesheets::MyFrame::OnSearch)
  EVT_CLOSE(treesheets::MyFrame::OnClosing)
  EVT_MAXIMIZE(treesheets::MyFrame::OnMaximize)
  EVT_ACTIVATE(treesheets::MyFrame::OnActivate)
  EVT_COMBOBOX(A_CELLCOLOR, treesheets::MyFrame::OnCellColor)
  EVT_COMBOBOX(A_TEXTCOLOR, treesheets::MyFrame::OnTextColor)
  EVT_COMBOBOX(A_BORDCOLOR, treesheets::MyFrame::OnBordColor)
  EVT_COMBOBOX(A_DDIMAGE,   treesheets::MyFrame::OnDDImage)
  EVT_ICONIZE(treesheets::MyFrame::OnIconize)
  EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, treesheets::MyFrame::OnTabChange)
  EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, treesheets::MyFrame::OnTabClose)
  //EVT_MOUSEWHEEL(treesheets::MyFrame::OnMouseWheel)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(treesheets::MyApp, wxApp)
END_EVENT_TABLE()

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

BEGIN_EVENT_TABLE(treesheets::ThreeChoiceDialog, wxDialog)
    EVT_BUTTON(wxID_ANY, treesheets::ThreeChoiceDialog::OnButton)
END_EVENT_TABLE()

/*
BEGIN_EVENT_TABLE(treesheets::MyFSWatch, wxFileSystemWatcher)
    EVT_FSWATCHER(wxID_ANY, treesheets::MyFSWatch::OnFileChanged)
END_EVENT_TABLE()
*/

/*
IMPLEMENT_DYNAMIC_CLASS (treesheets::CTransparentStaticText, wxStaticText)

BEGIN_EVENT_TABLE(treesheets::CTransparentStaticText, wxStaticText)
    EVT_PAINT(treesheets::CTransparentStaticText::OnPaint)
END_EVENT_TABLE()
*/
