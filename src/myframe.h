#pragma once
#include "document.fwd.h"
#include "mywxtools.h"
#include "mycanvas.fwd.h"
namespace treesheets {
struct MyFrame : wxFrame
{
    wxMenu *editmenupopup;
    wxString exepath;
    wxFileHistory filehistory;
    wxTextCtrl *filter, *replaces;
    wxToolBar *tb;
    int refreshhack, refreshhackinstances;
    BlinkTimer bt;
    wxTaskBarIcon tbi;
    wxIcon icon;
    ImageDropdown *idd;
    wxAuiNotebook *nb;
    wxAuiManager *aui;
    wxBitmap line_nw, line_sw;
    wxImage foldicon;
    bool fromclosebox;
    wxApp *app;
    #ifdef FSWATCH
        wxFileSystemWatcher *watcher;
        bool watcherwaitingforuser;
    #endif
    MyFrame(wxString exename, wxApp *_app);
    ~MyFrame();
    TSCanvas *NewTab(Document *doc);
    TSCanvas *GetCurTab();
    TSCanvas *GetTabByFileName(const wxString &fn);
    void OnTabChange(wxAuiNotebookEvent &nbe);
    void TabsReset();
    void OnTabClose(wxAuiNotebookEvent &nbe);
    void CycleTabs();
    void SetPageTitle(const wxString &fn, wxString mods, int page = -1);
    void AddTBIcon(wxToolBar *tb, const wxChar *name, int action, wxString file);
    void TBMenu(wxToolBar *tb, wxMenu *menu, const wxChar *name, int id = 0);
    void OnMenu(wxCommandEvent &ce);
    void OnSearch(wxCommandEvent &ce);
    void ReFocus();
    void OnCellColor(wxCommandEvent &ce);
    void OnTextColor(wxCommandEvent &ce);
    void OnBordColor(wxCommandEvent &ce);
    void OnDDImage  (wxCommandEvent &ce);
    void OnSizing(wxSizeEvent  &se);
    void OnMaximize(wxMaximizeEvent &me);
    void OnActivate(wxActivateEvent &ae);
    void OnIconize(wxIconizeEvent &me);
    void DeIconize();
    void OnTBIDBLClick(wxTaskBarIconEvent &e);
    void OnClosing(wxCloseEvent &ce);
    #ifdef WIN32
    void SetRegKey(wxChar *key, wxString val);
    #endif
    void SetFileAssoc(wxString &exename);
    #ifdef FSWATCH
    void OnFileSystemEvent(wxFileSystemWatcherEvent &event);
    #endif
    DECLARE_EVENT_TABLE();
};

}
