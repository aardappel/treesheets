
#include "stdafx.h"

static_assert(wxCHECK_VERSION(3, 2, 6), "wxWidgets < 3.2.6 is not supported.");

#ifndef __WXMSW__
#define SIMPLERENDER
#endif

//#define SIMPLERENDER // for testing

static const int TS_VERSION = 23;
static const int g_grid_margin = 1;
static const int g_cell_margin = 2;
static const int g_margin_extra = 2;  // TODO, could make this configurable: 0/2/4/6
static const int g_usergridouterspacing_default = 3;
static const int g_bordercolor_default = 0xA0A0A0;
static const int g_line_width = 1;
static const int g_selmargin = 2;
static const int g_scrollratecursor = 240;  // FIXME: must be configurable
static const int g_scrollratewheel = 2;     // relative to 1 step on a fixed wheel usually being 120
static const int g_max_launches = 20;
static const int g_deftextsize_default = 12;
static const int g_mintextsize_delta = 8;
static const int g_maxtextsize_delta = 32;
static const int BLINK_TIME = 400;
static const int CUSTOMCOLORIDX = 0;
static const uint TS_SELECTION_MASK = 0x80;

static const std::array<uint, 42> celltextcolors = {
    0xFFFFFF,  // CUSTOM COLOR!
    0xFFFFFF, 0x000000, 0x202020, 0x404040, 0x606060, 0x808080, 0xA0A0A0, 0xC0C0C0, 0xD0D0D0,
    0xE0E0E0, 0xE8E8E8, 0x000080, 0x0000FF, 0x8080FF, 0xC0C0FF, 0xC0C0E0, 0x008000, 0x00FF00,
    0x80FF80, 0xC0FFC0, 0xC0E0C0, 0x800000, 0xFF0000, 0xFF8080, 0xFFC0C0, 0xE0C0C0, 0x800080,
    0xFF00FF, 0xFF80FF, 0xFFC0FF, 0xE0C0E0, 0x008080, 0x00FFFF, 0x80FFFF, 0xC0FFFF, 0xC0E0E0,
    0x808000, 0xFFFF00, 0xFFFF80, 0xFFFFC0, 0xE0E0C0,
};

static const std::map<char, pair<wxBitmapType, wxString>> imagetypes = {
    { 'I', { wxBITMAP_TYPE_PNG, "image/png" } }, { 'J', { wxBITMAP_TYPE_JPEG, "image/jpeg" } }
};

static int g_deftextsize = g_deftextsize_default;
static int g_mintextsize() { return g_deftextsize - g_mintextsize_delta; }
static int g_maxtextsize() { return g_deftextsize + g_maxtextsize_delta; }

enum {TS_TEXT = 0, TS_GRID = 1, TS_BOTH = 2, TS_NEITHER = 3};

enum {
    A_SAVEALL = 500,
    A_COLLAPSE,
    A_NEWGRID,
    A_CLRVIEW,
    A_MARKDATA,
    A_MARKVIEWH,
    A_MARKVIEWV,
    A_MARKCODE,
    A_IMAGE,
    A_EXPIMAGE,
    A_EXPXML,
    A_EXPHTMLT,
    A_EXPHTMLTI,
    A_EXPHTMLO,
    A_EXPHTMLB,
    A_EXPTEXT,
    A_ZOOMIN,
    A_ZOOMOUT,
    A_TRANSPOSE,
    A_DELETE,
    A_BACKSPACE,
    A_DELETE_WORD,
    A_BACKSPACE_WORD,
    A_LEFT,
    A_RIGHT,
    A_UP,
    A_DOWN,
    A_MLEFT,
    A_MRIGHT,
    A_MUP,
    A_MDOWN,
    A_SLEFT,
    A_SRIGHT,
    A_SUP,
    A_SDOWN,
    A_ALEFT,
    A_ARIGHT,
    A_AUP,
    A_ADOWN,
    A_SCLEFT,
    A_SCRIGHT,
    A_SCUP,
    A_SCDOWN,
    A_IMPXML,
    A_IMPXMLA,
    A_IMPTXTI,
    A_IMPTXTC,
    A_IMPTXTS,
    A_IMPTXTT,
    A_HELP,
    A_MARKVARD,
    A_MARKVARU,
    A_SHOWSBAR,
    A_SHOWTBAR,
    A_LEFTTABS,
    A_TRADSCROLL,
    A_HOME,
    A_END,
    A_CHOME,
    A_CEND,
    A_PAGESETUP,
    A_PRINTSCALE,
    A_NEXT,
    A_PREV,
    A_TT,
    A_SEARCH,
    A_CASESENSITIVESEARCH,
    A_CLEARSEARCH,
    A_CLEARREPLACE,
    A_REPLACE,
    A_REPLACEONCE,
    A_REPLACEONCEJ,
    A_REPLACEALL,
    A_CANCELEDIT,
    A_BROWSE,
    A_ENTERCELL,
    A_ENTERCELL_JUMPTOEND,
    A_PROGRESSCELL,  // see
                     // https://github.com/aardappel/treesheets/issues/139#issuecomment-544167524
    A_CELLCOLOR,
    A_TEXTCOLOR,
    A_BORDCOLOR,
    A_INCSIZE,
    A_DECSIZE,
    A_INCWIDTH,
    A_DECWIDTH,
    A_ENTERGRID,
    A_LINK,
    A_LINKREV,
    A_LINKIMG,
    A_LINKIMGREV,
    A_SEARCHNEXT,
    A_SEARCHPREV,
    A_CUSTCOL,
    A_COLCELL,
    A_SORT,
    A_MAKEBAKS,
    A_TOTRAY,
    A_AUTOSAVE,
    A_FULLSCREEN,
    A_SCALED,
    A_SCOLS,
    A_SROWS,
    A_SHOME,
    A_SEND,
    A_BORD0,
    A_BORD1,
    A_BORD2,
    A_BORD3,
    A_BORD4,
    A_BORD5,
    A_HSWAP,
    A_TEXTGRID,
    A_TAGADD,
    A_TAGREMOVE,
    A_WRAP,
    A_HIFY,
    A_FLATTEN,
    A_BROWSEF,
    A_ROUND0,
    A_ROUND1,
    A_ROUND2,
    A_ROUND3,
    A_ROUND4,
    A_ROUND5,
    A_ROUND6,
    A_FILTER5,
    A_FILTER10,
    A_FILTER20,
    A_FILTER50,
    A_FILTERM,
    A_FILTERL,
    A_FILTERS,
    A_FILTEROFF,
    A_FILTERBYCELLBG,
    A_FILTERMATCHNEXT,
    A_FILTERRANGE,
    A_FILTERDIALOG,
    A_FASTRENDER,
    A_EXPCSV,
    A_PASTESTYLE,
    A_PREVFILE,
    A_NEXTFILE,
    A_IMAGER,
    A_INCWIDTHNH,
    A_DECWIDTHNH,
    A_ZOOMSCR,
    A_V_GS,
    A_V_BS,
    A_V_LS,
    A_H_GS,
    A_H_BS,
    A_H_LS,
    A_GS,
    A_BS,
    A_LS,
    A_RESETSIZE,
    A_RESETWIDTH,
    A_RESETSTYLE,
    A_RESETCOLOR,
    A_LASTCELLCOLOR,
    A_LASTTEXTCOLOR,
    A_LASTBORDCOLOR,
    A_OPENCELLCOLOR,
    A_OPENTEXTCOLOR,
    A_OPENBORDCOLOR,
    A_DDIMAGE,
    A_MINCLOSE,
    A_SINGLETRAY,
    A_CENTERED,
    A_SORTD,
    A_FOLD,
    A_FOLDALL,
    A_UNFOLDALL,
    A_IMAGESCP,
    A_IMAGESCW,
    A_IMAGESCF,
    A_IMAGESCN,
    A_IMAGESVA,
    A_SAVE_AS_JPEG,
    A_SAVE_AS_PNG,
    A_HELP_OP_REF,
    A_FSWATCH,
    A_DEFBGCOL,
    #ifdef SIMPLERENDER
        A_DEFCURCOL,
    #else
        A_HOVERSHADOW,
    #endif
    A_THINSELC,
    A_COPYCT,
    A_COPYBM,
    A_COPYWI,
    A_MINISIZE,
    A_CUSTKEY,
    A_AUTOEXPORT,
    A_DRAGANDDROP,
    A_SHOWSTATS,
    A_DEFAULTMAXCOLWIDTH,
    A_ADDSCRIPT,
    A_DETSCRIPT,
    A_NOP,
    A_TAGSET = 1000,  // and all values from here on
    A_SCRIPT = 2000,  // and all values from here on
    A_MAXACTION = 3000
};

enum {
    STYLE_BOLD = 1,
    STYLE_ITALIC = 2,
    STYLE_FIXED = 4,
    STYLE_UNDERLINE = 8,
    STYLE_STRIKETHRU = 16
};

enum { TEXT_SPACE = 3, TEXT_SEP = 2, TEXT_CHAR = 1 };

// script_interface.h is both used by TreeSheets and lobster-impl
// and uses data types that are already defined by lobster.

// Define these data types separately on the TreeSheets side here
// to avoid redefinitions.

struct string_view_nt {
    string_view sv;
    string_view_nt(const string &s) : sv(s) {}
    explicit string_view_nt(const char *s) : sv(s) {}
    explicit string_view_nt(string_view osv) : sv(osv) { check_null_terminated(); }
    void check_null_terminated() { assert(!sv.data()[sv.size()]); }
    size_t size() const { return sv.size(); }
    const char *data() const { return sv.data(); }
    const char *c_str() {
        check_null_terminated();  // Catch appends to parent buffer since construction.
        return sv.data();
    }
};
typedef int64_t (*FileLoader)(string_view_nt absfilename, std::string *dest, int64_t start,
                              int64_t len);

#include "script_interface.h"

using namespace script;

struct treesheets {
    struct TreeSheetsScriptImpl;

    struct Cell;
    struct Grid;
    struct Text;
    struct Evaluator;
    struct Image;
    struct Document;
    class Selection;

    struct System;

    struct MyApp;
    struct MyFrame;
    struct TSCanvas;

    static System *sys;

    #include "treesheets_impl.h"

    #include "selection.h"
    #include "text.h"
    #include "cell.h"
    #include "grid.h"
    #include "evaluator.h"

    #include "document.h"
    #include "system.h"

    #include "mywxtools.h"
    #include "mycanvas.h"
    #include "myframe.h"
    #include "myapp.h"
};

treesheets::System *treesheets::sys = nullptr;
treesheets::TreeSheetsScriptImpl treesheets::tssi;

IMPLEMENT_APP(treesheets::MyApp)

#include "myevents.h"