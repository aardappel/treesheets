// utility functions and constants for working with vectors

import std

// Convenient constants:

let float2_0 = float2 { 0.0, 0.0 }
let float2_1 = float2 { 1.0, 1.0 }
let float2_h = float2 { 0.5, 0.5 }
let float2_x = float2 { 1.0, 0.0 }
let float2_y = float2 { 0.0, 1.0 }

let float3_0 = float3 { 0.0, 0.0, 0.0 }
let float3_1 = float3 { 1.0, 1.0, 1.0 }
let float3_x = float3 { 1.0, 0.0, 0.0 }
let float3_y = float3 { 0.0, 1.0, 0.0 }
let float3_z = float3 { 0.0, 0.0, 1.0 }

let float4_0 = float4 { 0.0, 0.0, 0.0, 0.0 }
let float4_1 = float4 { 1.0, 1.0, 1.0, 1.0 }
let float4_x = float4 { 1.0, 0.0, 0.0, 0.0 }
let float4_y = float4 { 0.0, 1.0, 0.0, 0.0 }
let float4_z = float4 { 0.0, 0.0, 1.0, 0.0 }
let float4_w = float4 { 0.0, 0.0, 0.0, 1.0 }

// int versions

let int2_0 = int2 { 0, 0 }
let int2_1 = int2 { 1, 1 }
let int2_x = int2 { 1, 0 }
let int2_y = int2 { 0, 1 }

let int3_0 = int3 { 0, 0, 0 }
let int3_1 = int3 { 1, 1, 1 }
let int3_x = int3 { 1, 0, 0 }
let int3_y = int3 { 0, 1, 0 }
let int3_z = int3 { 0, 0, 1 }

let int4_0 = int4 { 0, 0, 0, 0 }
let int4_1 = int4 { 1, 1, 1, 1 }
let int4_x = int4 { 1, 0, 0, 0 }
let int4_y = int4 { 0, 1, 0, 0 }
let int4_z = int4 { 0, 0, 1, 0 }
let int4_w = int4 { 0, 0, 0, 1 }

let cardinal_directions = [ int2 {  0, -1 }, int2 { 1, 0 }, int2 { 0,  1 }, int2 { -1, 0 } ]
let diagonal_directions = [ int2 { -1, -1 }, int2 { 1, 1 }, int2 { 1, -1 }, int2 { -1, 1 } ]
let positive_directions = [ int2 {  0,  0 }, int2 { 1, 0 }, int2 { 1,  1 }, int2 {  0, 1 } ]

let octant_directions = [
    int3 {  1,  1,  1 },
    int3 {  1,  1, -1 },
    int3 {  1, -1,  1 },
    int3 {  1, -1, -1 },
    int3 { -1,  1,  1 },
    int3 { -1,  1, -1 },
    int3 { -1, -1,  1 },
    int3 { -1, -1, -1 },
]

// shorten vectors, e.g. vec3 -> vec2
def xy<T>(v:vec3<T>) -> vec2<T>: return v  // Shorten by subtyping.
def xy<T>(v:vec4<T>) -> vec2<T>: return v  // Shorten by subtyping.
def xz(v): return vec2 { v.x, v.z }
def yw(v): return vec2 { v.y, v.w }
def zw(v): return vec2 { v.z, v.w }

def xyz(v): return vec3 { v.x, v.y, v.z }

// lengthen vectors
def int3  (v:int2,   z:int):   return int3    { v.x, v.y, z }
def float3(v:float2, z:float): return float3  { v.x, v.y, z }
def vec3<T>(v:vec2<T>, z:T):   return vec3<T> { v.x, v.y, z }
def int4  (v:int3,   w:int):   return int4    { v.x, v.y, v.z, w }
def float4(v:float3, w:float): return float4  { v.x, v.y, v.z, w }
def vec4<T>(v:vec3<T>, w:T):   return vec4<T> { v.x, v.y, v.z, w }

// create from vector types
def vec2_v<T>(v:[T]): return vec2<T> { v[0], v[1] }
def vec3_v<T>(v:[T]): return vec3<T> { v[0], v[1], v[2] }
def vec4_v<T>(v:[T]): return vec4<T> { v[0], v[1], v[2], v[3] }
// or to vector
def v_vec2<T>(v:vec2<T>): return [ v.x, v.y ]
def v_vec3<T>(v:vec3<T>): return [ v.x, v.y, v.z ]
def v_vec4<T>(v:vec4<T>): return [ v.x, v.y, v.z, v.w ]

// flip vectors
def yx<T>(v:vec2<T>):   return vec2<T> { v.y, v.x }
def zyx<T>(v:vec3<T>):  return vec3<T> { v.z, v.y, v.x }
def wzyx<T>(v:vec4<T>): return vec4<T> { v.w, v.z, v.y, v.x }

def rnd_float2(): return float2 { rnd_float(), rnd_float() }
def rnd_float3(): return float3 { rnd_float(), rnd_float(), rnd_float() }
def rnd_float4(): return float4 { rnd_float(), rnd_float(), rnd_float(), rnd_float() }

def rnd_float2_norm(): return normalize(rnd_float2() - 0.5)
def rnd_float3_norm(): return normalize(rnd_float3() - 0.5)

def rnd_int2(n:int):  return int2  { rnd(n), rnd(n) }
def rnd_int2(n:int2): return int2  { rnd(n.x), rnd(n.y) }
def rnd_int3(n:int):  return int3 { rnd(n), rnd(n), rnd(n) }
def rnd_int3(n:int3): return int3 { rnd(n.x), rnd(n.y), rnd(n.z) }

def forxy(v:int2, fun): for(v.y) y: for(v.x) x: fun(int2 { x, y })
def foryx(v:int2, fun): for(v.x) x: for(v.y) y: fun(int2 { x, y })
def mapxy(v:int2, fun): return map(v.y) y: map(v.x) x: fun(int2 { x, y })

def forxyz(v:int3, fun): for(v.z) z: for(v.y) y: for(v.x) x: fun(int3 { x, y, z })
def mapxyz(v:int3, fun): return map(v.z) z: map(v.y) y: map(v.x) x: fun(int3 { x, y, z })

def vecfromyawpitch(yaw:float, pitch:float, move:float, strafe:float):
    return (float3(sincos(yaw + 90.0/* start from Y rather than X axis */), 0.0) *
        cos(pitch) + float3_z * sin(pitch)) * move + float3(sincos(yaw), 0.0) * strafe

def rotate2D(v:float2, angle:float):
    let s = sin(-angle)
    let c = cos(-angle)
    return float2 { v.x * c + v.y * s,
                  v.y * c - v.x * s }
