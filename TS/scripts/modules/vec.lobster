// utility functions and constants for working with vectors

import std

// Convenient constants:

let xy_0 = xy { 0.0, 0.0 }
let xy_1 = xy { 1.0, 1.0 }
let xy_h = xy { 0.5, 0.5 }
let xy_x = xy { 1.0, 0.0 }
let xy_y = xy { 0.0, 1.0 }

let xyz_0 = xyz { 0.0, 0.0, 0.0 }
let xyz_1 = xyz { 1.0, 1.0, 1.0 }
let xyz_x = xyz { 1.0, 0.0, 0.0 }
let xyz_y = xyz { 0.0, 1.0, 0.0 }
let xyz_z = xyz { 0.0, 0.0, 1.0 }

let xyzw_0 = xyzw { 0.0, 0.0, 0.0, 0.0 }
let xyzw_1 = xyzw { 1.0, 1.0, 1.0, 1.0 }
let xyzw_x = xyzw { 1.0, 0.0, 0.0, 0.0 }
let xyzw_y = xyzw { 0.0, 1.0, 0.0, 0.0 }
let xyzw_z = xyzw { 0.0, 0.0, 1.0, 0.0 }
let xyzw_w = xyzw { 0.0, 0.0, 0.0, 1.0 }

// int versions

let xy_0i = xy { 0, 0 }
let xy_1i = xy { 1, 1 }
let xy_xi = xy { 1, 0 }
let xy_yi = xy { 0, 1 }

let xyz_0i = xyz { 0, 0, 0 }
let xyz_1i = xyz { 1, 1, 1 }
let xyz_xi = xyz { 1, 0, 0 }
let xyz_yi = xyz { 0, 1, 0 }
let xyz_zi = xyz { 0, 0, 1 }

let xyzw_0i = xyzw { 0, 0, 0, 0 }
let xyzw_1i = xyzw { 1, 1, 1, 1 }
let xyzw_xi = xyzw { 1, 0, 0, 0 }
let xyzw_yi = xyzw { 0, 1, 0, 0 }
let xyzw_zi = xyzw { 0, 0, 1, 0 }
let xyzw_wi = xyzw { 0, 0, 0, 1 }

let cardinal_directions = [ xy { 0, -1 }, xy { 1, 0 }, xy { 0, 1 }, xy { -1, 0 } ]
let diagonal_directions = [ xy { -1, -1 }, xy { 1, 1 }, xy { 1, -1 }, xy { -1, 1 } ]

let positive_directions = [ xy { 0, 0 }, xy { 1, 0 }, xy { 1, 1 }, xy { 0, 1 } ]

let octant_directions = [
    xyz {  1,  1,  1 },
    xyz {  1,  1, -1 },
    xyz {  1, -1,  1 },
    xyz {  1, -1, -1 },
    xyz { -1,  1,  1 },
    xyz { -1,  1, -1 },
    xyz { -1, -1,  1 },
    xyz { -1, -1, -1 },
]

// shorten vectors, e.g. xyz -> xy
// FIXME: subtyping should take care of this
def xy(v):  return xy  { v.x, v.y }
def xyz(v): return xyz { v.x, v.y, v.z }

// lengthen vectors
def xyz(v, z):  return xyz  { v.x, v.y, z }
def xyzw(v, w): return xyzw { v.x, v.y, v.z, w }

// create from vector types
def xy_v(v):   return xy { v[0], v[1] }
def xyz_v(v):  return xyz { v[0], v[1], v[2] }
def xyzw_v(v): return xyzw { v[0], v[1], v[2], v[3] }
// or to vector
def v_xy(v):   return [ v.x, v.y ]
def v_xyz(v):  return [ v.x, v.y, v.z ]
def v_xyzw(v): return [ v.x, v.y, v.z, v.w ]

// flip vectors
def yx(v):   return xy { v.y, v.x }
def zyx(v):  return xyz { v.z, v.y, v.x }
def wzyx(v): return xyzw { v.w, v.z, v.y, v.x }

def xy_rnd():   return xy  { rnd_float(), rnd_float() }
def xyz_rnd():  return xyz { rnd_float(), rnd_float(), rnd_float() }
def xyzw_rnd(): return xyzw { rnd_float(), rnd_float(), rnd_float(), rnd_float() }

def xy_rnd_norm():  return normalize(xy_rnd() - 0.5)
def xyz_rnd_norm(): return normalize(xyz_rnd() - 0.5)

def xy_rndi(n:int):    return xy  { rnd(n), rnd(n) }
def xy_rndi(n:xy_i):   return xy  { rnd(n.x), rnd(n.y) }
def xyz_rndi(n:int):   return xyz { rnd(n), rnd(n), rnd(n) }
def xyz_rndi(n:xyz_i): return xyz { rnd(n.x), rnd(n.y), rnd(n.z) }

def forxy(v, fun): for(v.y) y: for(v.x) x: fun(xy { x, y })
def foryx(v, fun): for(v.x) x: for(v.y) y: fun(xy { x, y })
def mapxy(v, fun): return map(v.y) y: map(v.x) x: fun(xy { x, y })

def forxyz(v, fun): for(v.z) z: for(v.y) y: for(v.x) x: fun(xyz { x, y, z })
def mapxyz(v, fun): return map(v.z) z: map(v.y) y: map(v.x) x: fun(xyz { x, y, z })

def vecfromyawpitch(yaw, pitch, move, strafe):
    return (xyz(sincos(yaw + 90.0/* start from Y rather than X axis */), 0.0) *
        cos(pitch) + xyz_z * sin(pitch)) * move + xyz(sincos(yaw), 0.0) * strafe

def rotate2D(v, angle):
    let s = sin(-angle)
    let c = cos(-angle)
    return xy { v.x * c + v.y * s,
                v.y * c - v.x * s }

