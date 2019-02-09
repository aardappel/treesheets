// utility functions and constants for working with vectors

include "std.lobster"

// Convenient constants:

xy_0 :== xy { 0.0, 0.0 }
xy_1 :== xy { 1.0, 1.0 }
xy_h :== xy { 0.5, 0.5 }
xy_x :== xy { 1.0, 0.0 }
xy_y :== xy { 0.0, 1.0 }

xyz_0 :== xyz { 0.0, 0.0, 0.0 }
xyz_1 :== xyz { 1.0, 1.0, 1.0 }
xyz_x :== xyz { 1.0, 0.0, 0.0 }
xyz_y :== xyz { 0.0, 1.0, 0.0 }
xyz_z :== xyz { 0.0, 0.0, 1.0 }

xyzw_0 :== xyzw { 0.0, 0.0, 0.0, 0.0 }
xyzw_1 :== xyzw { 1.0, 1.0, 1.0, 1.0 }
xyzw_x :== xyzw { 1.0, 0.0, 0.0, 0.0 }
xyzw_y :== xyzw { 0.0, 1.0, 0.0, 0.0 }
xyzw_z :== xyzw { 0.0, 0.0, 1.0, 0.0 }
xyzw_w :== xyzw { 0.0, 0.0, 0.0, 1.0 }

// int versions

xy_0i :== xy { 0, 0 }
xy_1i :== xy { 1, 1 }
xy_xi :== xy { 1, 0 }
xy_yi :== xy { 0, 1 }

xyz_0i :== xyz { 0, 0, 0 }
xyz_1i :== xyz { 1, 1, 1 }
xyz_xi :== xyz { 1, 0, 0 }
xyz_yi :== xyz { 0, 1, 0 }
xyz_zi :== xyz { 0, 0, 1 }

xyzw_0i :== xyzw { 0, 0, 0, 0 }
xyzw_1i :== xyzw { 1, 1, 1, 1 }
xyzw_xi :== xyzw { 1, 0, 0, 0 }
xyzw_yi :== xyzw { 0, 1, 0, 0 }
xyzw_zi :== xyzw { 0, 0, 1, 0 }
xyzw_wi :== xyzw { 0, 0, 0, 1 }

cardinal_directions :== [ xy { 0, -1 }, xy { 1, 0 }, xy { 0, 1 }, xy { -1, 0 } ]
diagonal_directions :== [ xy { -1, -1 }, xy { 1, 1 }, xy { 1, -1 }, xy { -1, 1 } ]

positive_directions :== [ xy { 0, 0 }, xy { 1, 0 }, xy { 1, 1 }, xy { 0, 1 } ]

octant_directions :== [
    xyz {  1,  1,  1 },
    xyz {  1,  1, -1 },
    xyz {  1, -1,  1 },
    xyz {  1, -1, -1 },
    xyz { -1,  1,  1 },
    xyz { -1,  1, -1 },
    xyz { -1, -1,  1 },
    xyz { -1, -1, -1 }
]

// shorten vectors, e.g. xyz -> xy
// FIXME: subtyping should take care of this
def xy(v):  xy  { v.x, v.y }
def xyz(v): xyz { v.x, v.y, v.z }

// lengthen vectors
def xyz(v, z):  xyz  { v.x, v.y, z }
def xyzw(v, w): xyzw { v.x, v.y, v.z, w }

// create from vector types
def xy_v(v): xy { v[0], v[1] }
def xyz_v(v): xyz { v[0], v[1], v[2] }
def xyzw_v(v): xyzw { v[0], v[1], v[2], v[3] }

// flip vectors
def yx(v): xy { v.y, v.x }
def zyx(v): xyz { v.z, v.y, v.x }
def wzyx(v): xyzw { v.w, v.z, v.y, v.x }

def xy_rnd():   xy  { rndfloat(), rndfloat() }
def xyz_rnd():  xyz { rndfloat(), rndfloat(), rndfloat() }
def xyzw_rnd(): xyzw { rndfloat(), rndfloat(), rndfloat(), rndfloat() }

def xy_rnd_norm():  normalize(xy_rnd() - 0.5)
def xyz_rnd_norm(): normalize(xyz_rnd() - 0.5)

def xy_rndi(n):  xy  { rnd(n.x), rnd(n.y) }
def xyz_rndi(n): xyz { rnd(n.x), rnd(n.y), rnd(n.x) }

def forxy(v, fun): for(v.y) y: for(v.x) x: fun(xy { x, y })
def foryx(v, fun): for(v.x) x: for(v.y) y: fun(xy { x, y })
def mapxy(v, fun): map(v.y) y: map(v.x) x: fun(xy { x, y })

def forxyz(v, fun): for(v.z) z: for(v.y) y: for(v.x) x: fun(xyz { x, y, z })
def mapxyz(v, fun): map(v.z) z: map(v.y) y: map(v.x) x: fun(xyz { x, y, z })

def vecfromyawpitch(yaw, pitch, move, strafe):
    (xyz(sincos(yaw + 90/* start from Y rather than X axis */), 0) *
        cos(pitch) + xyz_z * sin(pitch)) * move + xyz(sincos(yaw), 0) * strafe

def rotate2D(v, angle):
    s := sin(-angle)
    c := cos(-angle)
    xy { v.x * c + v.y * s,
         v.y * c - v.x * s }

