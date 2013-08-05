#include "globals.h"
namespace treesheets
{
int g_grid_margin = 1;
int g_cell_margin = 2;
int g_margin_extra = 2; // TODO, could make this configurable: 0/2/4/6
int g_line_width = 1;
int g_selmargin = 2;
int g_deftextsize = 12;
int g_mintextsize() { return g_deftextsize - 8; }
int g_maxtextsize() { return g_deftextsize + 32; }
int g_grid_left_offset = 15;

int g_scrollratecursor = 160; // FIXME: must be configurable
int g_scrollratewheel = 1;  // relative to 1 step on a fixed wheel usually being 120

uint celltextcolors[] =
{
    0xFFFFFF, 0x000000, 0x202020, 0x404040, 0x606060, 
		0x808080, 0xA0A0A0, 0xC0C0C0, 0xD0D0D0, 0xE0E0E0, 0xE8E8E8,
    0x000080, 0x0000FF, 0x8080FF, 0xC0C0FF, 0xC0C0E0, 
    0x008000, 0x00FF00, 0x80FF80, 0xC0FFC0, 0xC0E0C0, 
    0x800000, 0xFF0000, 0xFF8080, 0xFFC0C0, 0xE0C0C0, 
    0x800080, 0xFF00FF, 0xFF80FF, 0xFFC0FF, 0xE0C0E0, 
    0x008080, 0x00FFFF, 0x80FFFF, 0xC0FFFF, 0xC0E0E0, 
    0x808000, 0xFFFF00, 0xFFFF80, 0xFFFFC0, 0xE0E0C0,
    0xFFFFFF,   // CUSTOM COLOR!
};
#define CUSTOMCOLORIDX 41
}
