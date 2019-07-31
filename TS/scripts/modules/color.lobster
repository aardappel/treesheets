// standard color constants

struct color:
    red:float
    green:float
    blue:float
    alpha:float

let color_black = color { 0, 0, 0, 1 }
let color_white = color { 1, 1, 1, 1 }

let color_red   = color { 1, 0, 0, 1 }
let color_green = color { 0, 1, 0, 1 }
let color_blue  = color { 0, 0, 1, 1 }

let color_yellow = color { 1, 1, 0, 1 }
let color_cyan   = color { 0, 1, 1, 1 }
let color_pink   = color { 1, 0, 1, 1 }

let color_grey          = color { 0.5,  0.5,  0.5,  1 }
let color_dark_grey     = color { 0.25, 0.25, 0.25, 1 }
let color_light_grey    = color { 0.75, 0.75, 0.75, 1 }
let color_lightest_grey = color { 0.9,  0.9,  0.9,  1 }
let color_darkest_grey  = color { 0.1,  0.1,  0.1,  1 }

let color_light_red    = color { 1,   0.5, 0.5, 1 }
let color_light_green  = color { 0.5, 1,   0.5, 1 }
let color_light_blue   = color { 0.5, 0.5, 1,   1 }
let color_light_yellow = color { 1,   1,   0.5, 1 }
let color_light_cyan   = color { 0.5, 1,   1,   1 }
let color_light_pink   = color { 1,   0.5, 1,   1 }

let color_dark_red   = color { 0.5,   0,   0, 1 }
let color_dark_green = color {   0, 0.5,   0, 1 }
let color_dark_blue  = color {   0,   0, 0.5, 1 }
let color_olive      = color { 0.5, 0.5, 0,   1 }
let color_teal       = color { 0,   0.5, 0.5, 1 }
let color_purple     = color { 0.5, 0,   0.5, 1 }

let color_orange      = color { 1,   0.5, 0,   1 }
let color_chartreuse  = color { 0.5, 1,   0,   1 }
let color_springgreen = color { 0,   1,   0.5, 1 }
let color_dodgerblue  = color { 0,   0.5, 1,   1 }
let color_pink_violet = color { 1,   0,   0.5, 1 }
let color_blue_violet = color { 0.5, 0,   1,   1 }
