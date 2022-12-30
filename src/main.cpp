
#include "stdafx.h"

#ifndef __WXMSW__
#define SIMPLERENDER
#endif

//#define SIMPLERENDER // for testing

int g_grid_margin = 1;
int g_cell_margin = 2;
int g_margin_extra = 2;  // TODO, could make this configurable: 0/2/4/6
int g_line_width = 1;
int g_selmargin = 2;
int g_deftextsize = 12;
int g_mintextsize() { return g_deftextsize - 8; }
int g_maxtextsize() { return g_deftextsize + 32; }
int g_grid_left_offset = 15;

int g_scrollratecursor = 240;  // FIXME: must be configurable
int g_scrollratewheel = 2;     // relative to 1 step on a fixed wheel usually being 120

const int MAX_LAUNCHES = 20;

static uint celltextcolors[] = {
    0xFFFFFF,  // CUSTOM COLOR!
    0xFFFFFF, 0x000000, 0x202020, 0x404040, 0x606060, 0x808080, 0xA0A0A0, 0xC0C0C0, 0xD0D0D0,
    0xE0E0E0, 0xE8E8E8, 0x000080, 0x0000FF, 0x8080FF, 0xC0C0FF, 0xC0C0E0, 0x008000, 0x00FF00,
    0x80FF80, 0xC0FFC0, 0xC0E0C0, 0x800000, 0xFF0000, 0xFF8080, 0xFFC0C0, 0xE0C0C0, 0x800080,
    0xFF00FF, 0xFF80FF, 0xFFC0FF, 0xE0C0E0, 0x008080, 0x00FFFF, 0x80FFFF, 0xC0FFFF, 0xC0E0E0,
    0x808000, 0xFFFF00, 0xFFFF80, 0xFFFFC0, 0xE0E0C0,
};
#define CUSTOMCOLORIDX 0

enum { TS_VERSION = 19, TS_TEXT = 0, TS_GRID, TS_BOTH, TS_NEITHER };

enum {
    A_NEW = 500,
    A_OPEN,
    A_CLOSE,
    A_SAVE,
    A_SAVEAS,
    A_CUT,
    A_COPY,
    A_PASTE,
    A_NEWGRID,
    A_UNDO,
    A_ABOUT,
    A_RUN,
    A_CLRVIEW,
    A_MARKDATA,
    A_MARKVIEWH,
    A_MARKVIEWV,
    A_MARKCODE,
    A_IMAGE,
    A_EXPIMAGE,
    A_EXPXML,
    A_EXPHTMLT,
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
    A_DEFFONT,
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
    A_PRINT,
    A_PREVIEW,
    A_PAGESETUP,
    A_PRINTSCALE,
    A_EXIT,
    A_NEXT,
    A_PREV,
    A_BOLD,
    A_ITALIC,
    A_TT,
    A_UNDERL,
    A_SEARCH,
    A_REPLACE,
    A_REPLACEONCE,
    A_REPLACEONCEJ,
    A_REPLACEALL,
    A_SELALL,
    A_CANCELEDIT,
    A_BROWSE,
    A_ENTERCELL,
    A_PROGRESSCELL, // see https://github.com/aardappel/treesheets/issues/139#issuecomment-544167524
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
    A_SEARCHNEXT,
    A_CUSTCOL,
    A_COLCELL,
    A_SORT,
    A_SEARCHF,
    A_MAKEBAKS,
    A_TOTRAY,
    A_AUTOSAVE,
    A_FULLSCREEN,
    A_ZEN_MODE,
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
    A_FASTRENDER,
    A_EXPCSV,
    A_PASTESTYLE,
    A_PREVFILE,
    A_NEXTFILE,
    A_IMAGER,
    A_INCWIDTHNH,
    A_DECWIDTHNH,
    A_ZOOMSCR,
    A_ICONSET,
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
    A_STRIKET,
    A_FOLD,
    A_FOLDALL,
    A_UNFOLDALL,
    A_IMAGESCP,
    A_IMAGESCF,
    A_IMAGESCN,
    A_IMAGECPY,
    A_HELPI,
    A_HELP_OP_REF,
    A_REDO,
    A_FSWATCH,
    A_DEFBGCOL,
    A_THINSELC,
    A_COPYCT,
    A_MINISIZE,
    A_CUSTKEY,
    A_AUTOEXPORT,
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
