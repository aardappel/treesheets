
BEGIN_EVENT_TABLE(treesheets::MyFrame, wxFrame)
  #ifndef __WXMAC__
    EVT_DPI_CHANGED(treesheets::MyFrame::OnDPIChanged)
  #endif
  EVT_SIZING(treesheets::MyFrame::OnSizing)
  EVT_MENU(wxID_ANY, treesheets::MyFrame::OnMenu)
  EVT_TEXT(A_SEARCH, treesheets::MyFrame::OnSearch)
  EVT_TEXT_ENTER(A_SEARCH, treesheets::MyFrame::OnSearchReplaceEnter)
  EVT_TEXT_ENTER(A_REPLACE, treesheets::MyFrame::OnSearchReplaceEnter)
  EVT_CLOSE(treesheets::MyFrame::OnClosing)
  EVT_MAXIMIZE(treesheets::MyFrame::OnMaximize)
  EVT_ACTIVATE_APP(treesheets::MyFrame::OnActivate)
  EVT_COMBOBOX(A_CELLCOLOR, treesheets::MyFrame::OnChangeColor)
  EVT_COMBOBOX(A_TEXTCOLOR, treesheets::MyFrame::OnChangeColor)
  EVT_COMBOBOX(A_BORDCOLOR, treesheets::MyFrame::OnChangeColor)
  EVT_COMBOBOX(A_DDIMAGE,   treesheets::MyFrame::OnDDImage)
  EVT_ICONIZE(treesheets::MyFrame::OnIconize)
  EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, treesheets::MyFrame::OnTabChange)
  EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, treesheets::MyFrame::OnTabClose)
  EVT_SYS_COLOUR_CHANGED(treesheets::MyFrame::OnSysColourChanged)
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
  EVT_KEY_DOWN(treesheets::TSCanvas::OnKeyDown)
  EVT_CONTEXT_MENU(treesheets::TSCanvas::OnContextMenuClick)
  EVT_SIZE(treesheets::TSCanvas::OnSize)
  EVT_SCROLLWIN(treesheets::TSCanvas::OnScrollWin)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(treesheets::ThreeChoiceDialog, wxDialog)
    EVT_BUTTON(wxID_ANY, treesheets::ThreeChoiceDialog::OnButton)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(treesheets::DateTimeRangeDialog, wxDialog)
    EVT_BUTTON(wxID_ANY, treesheets::DateTimeRangeDialog::OnButton)
END_EVENT_TABLE()
