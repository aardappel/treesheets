// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#include "lobster/glinterface.h"

#include "lobster/3dgrid.h"
#include "lobster/meshgen.h"
#include "lobster/cubegen.h"
#include "lobster/simplex.h"

vector<Palette> palettes;

namespace lobster {

RandomNumberGenerator<Xoshiro256SS> cg_rnd;

ResourceType voxel_type = {
    "voxels",
    [](void *v) { delete (Voxels *)v; },
    nullptr,
    [](void *v) { return ((Voxels *)v)->MemoryUsage(); }
};

const unsigned int default_palette[256] = {
    0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff,
    0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
    0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff,
    0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
    0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc,
    0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
    0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc,
    0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
    0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc,
    0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
    0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999,
    0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
    0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099,
    0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
    0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66,
    0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
    0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366,
    0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
    0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33,
    0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
    0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633,
    0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
    0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00,
    0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
    0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600,
    0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
    0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000,
    0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
    0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700,
    0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
    0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd,
    0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
};

const uint8_t normal_table_size = 162;
const float3 default_normals[normal_table_size] = {
    { -0.525731f,  0.000000f,  0.850651f }, { -0.442863f,  0.238856f,  0.864188f }, 
    { -0.295242f,  0.000000f,  0.955423f }, { -0.309017f,  0.500000f,  0.809017f }, 
    { -0.162460f,  0.262866f,  0.951056f }, {  0.000000f,  0.000000f,  1.000000f }, 
    {  0.000000f,  0.850651f,  0.525731f }, { -0.147621f,  0.716567f,  0.681718f }, 
    {  0.147621f,  0.716567f,  0.681718f }, {  0.000000f,  0.525731f,  0.850651f }, 
    {  0.309017f,  0.500000f,  0.809017f }, {  0.525731f,  0.000000f,  0.850651f }, 
    {  0.295242f,  0.000000f,  0.955423f }, {  0.442863f,  0.238856f,  0.864188f }, 
    {  0.162460f,  0.262866f,  0.951056f }, { -0.681718f,  0.147621f,  0.716567f }, 
    { -0.809017f,  0.309017f,  0.500000f }, { -0.587785f,  0.425325f,  0.688191f }, 
    { -0.850651f,  0.525731f,  0.000000f }, { -0.864188f,  0.442863f,  0.238856f }, 
    { -0.716567f,  0.681718f,  0.147621f }, { -0.688191f,  0.587785f,  0.425325f }, 
    { -0.500000f,  0.809017f,  0.309017f }, { -0.238856f,  0.864188f,  0.442863f }, 
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.681718f, -0.147621f }, 
    { -0.500000f,  0.809017f, -0.309017f }, { -0.525731f,  0.850651f,  0.000000f }, 
    {  0.000000f,  0.850651f, -0.525731f }, { -0.238856f,  0.864188f, -0.442863f }, 
    {  0.000000f,  0.955423f, -0.295242f }, { -0.262866f,  0.951056f, -0.162460f }, 
    {  0.000000f,  1.000000f,  0.000000f }, {  0.000000f,  0.955423f,  0.295242f }, 
    { -0.262866f,  0.951056f,  0.162460f }, {  0.238856f,  0.864188f,  0.442863f }, 
    {  0.262866f,  0.951056f,  0.162460f }, {  0.500000f,  0.809017f,  0.309017f }, 
    {  0.238856f,  0.864188f, -0.442863f }, {  0.262866f,  0.951056f, -0.162460f }, 
    {  0.500000f,  0.809017f, -0.309017f }, {  0.850651f,  0.525731f,  0.000000f }, 
    {  0.716567f,  0.681718f,  0.147621f }, {  0.716567f,  0.681718f, -0.147621f }, 
    {  0.525731f,  0.850651f,  0.000000f }, {  0.425325f,  0.688191f,  0.587785f }, 
    {  0.864188f,  0.442863f,  0.238856f }, {  0.688191f,  0.587785f,  0.425325f }, 
    {  0.809017f,  0.309017f,  0.500000f }, {  0.681718f,  0.147621f,  0.716567f }, 
    {  0.587785f,  0.425325f,  0.688191f }, {  0.955423f,  0.295242f,  0.000000f }, 
    {  1.000000f,  0.000000f,  0.000000f }, {  0.951056f,  0.162460f,  0.262866f }, 
    {  0.850651f, -0.525731f,  0.000000f }, {  0.955423f, -0.295242f,  0.000000f }, 
    {  0.864188f, -0.442863f,  0.238856f }, {  0.951056f, -0.162460f,  0.262866f }, 
    {  0.809017f, -0.309017f,  0.500000f }, {  0.681718f, -0.147621f,  0.716567f }, 
    {  0.850651f,  0.000000f,  0.525731f }, {  0.864188f,  0.442863f, -0.238856f }, 
    {  0.809017f,  0.309017f, -0.500000f }, {  0.951056f,  0.162460f, -0.262866f }, 
    {  0.525731f,  0.000000f, -0.850651f }, {  0.681718f,  0.147621f, -0.716567f }, 
    {  0.681718f, -0.147621f, -0.716567f }, {  0.850651f,  0.000000f, -0.525731f }, 
    {  0.809017f, -0.309017f, -0.500000f }, {  0.864188f, -0.442863f, -0.238856f }, 
    {  0.951056f, -0.162460f, -0.262866f }, {  0.147621f,  0.716567f, -0.681718f }, 
    {  0.309017f,  0.500000f, -0.809017f }, {  0.425325f,  0.688191f, -0.587785f }, 
    {  0.442863f,  0.238856f, -0.864188f }, {  0.587785f,  0.425325f, -0.688191f }, 
    {  0.688191f,  0.587785f, -0.425325f }, { -0.147621f,  0.716567f, -0.681718f }, 
    { -0.309017f,  0.500000f, -0.809017f }, {  0.000000f,  0.525731f, -0.850651f }, 
    { -0.525731f,  0.000000f, -0.850651f }, { -0.442863f,  0.238856f, -0.864188f }, 
    { -0.295242f,  0.000000f, -0.955423f }, { -0.162460f,  0.262866f, -0.951056f }, 
    {  0.000000f,  0.000000f, -1.000000f }, {  0.295242f,  0.000000f, -0.955423f }, 
    {  0.162460f,  0.262866f, -0.951056f }, { -0.442863f, -0.238856f, -0.864188f }, 
    { -0.309017f, -0.500000f, -0.809017f }, { -0.162460f, -0.262866f, -0.951056f }, 
    {  0.000000f, -0.850651f, -0.525731f }, { -0.147621f, -0.716567f, -0.681718f }, 
    {  0.147621f, -0.716567f, -0.681718f }, {  0.000000f, -0.525731f, -0.850651f }, 
    {  0.309017f, -0.500000f, -0.809017f }, {  0.442863f, -0.238856f, -0.864188f }, 
    {  0.162460f, -0.262866f, -0.951056f }, {  0.238856f, -0.864188f, -0.442863f }, 
    {  0.500000f, -0.809017f, -0.309017f }, {  0.425325f, -0.688191f, -0.587785f }, 
    {  0.716567f, -0.681718f, -0.147621f }, {  0.688191f, -0.587785f, -0.425325f }, 
    {  0.587785f, -0.425325f, -0.688191f }, {  0.000000f, -0.955423f, -0.295242f }, 
    {  0.000000f, -1.000000f,  0.000000f }, {  0.262866f, -0.951056f, -0.162460f }, 
    {  0.000000f, -0.850651f,  0.525731f }, {  0.000000f, -0.955423f,  0.295242f }, 
    {  0.238856f, -0.864188f,  0.442863f }, {  0.262866f, -0.951056f,  0.162460f }, 
    {  0.500000f, -0.809017f,  0.309017f }, {  0.716567f, -0.681718f,  0.147621f }, 
    {  0.525731f, -0.850651f,  0.000000f }, { -0.238856f, -0.864188f, -0.442863f }, 
    { -0.500000f, -0.809017f, -0.309017f }, { -0.262866f, -0.951056f, -0.162460f }, 
    { -0.850651f, -0.525731f,  0.000000f }, { -0.716567f, -0.681718f, -0.147621f }, 
    { -0.716567f, -0.681718f,  0.147621f }, { -0.525731f, -0.850651f,  0.000000f }, 
    { -0.500000f, -0.809017f,  0.309017f }, { -0.238856f, -0.864188f,  0.442863f }, 
    { -0.262866f, -0.951056f,  0.162460f }, { -0.864188f, -0.442863f,  0.238856f }, 
    { -0.809017f, -0.309017f,  0.500000f }, { -0.688191f, -0.587785f,  0.425325f }, 
    { -0.681718f, -0.147621f,  0.716567f }, { -0.442863f, -0.238856f,  0.864188f }, 
    { -0.587785f, -0.425325f,  0.688191f }, { -0.309017f, -0.500000f,  0.809017f }, 
    { -0.147621f, -0.716567f,  0.681718f }, { -0.425325f, -0.688191f,  0.587785f }, 
    { -0.162460f, -0.262866f,  0.951056f }, {  0.442863f, -0.238856f,  0.864188f }, 
    {  0.162460f, -0.262866f,  0.951056f }, {  0.309017f, -0.500000f,  0.809017f }, 
    {  0.147621f, -0.716567f,  0.681718f }, {  0.000000f, -0.525731f,  0.850651f }, 
    {  0.425325f, -0.688191f,  0.587785f }, {  0.587785f, -0.425325f,  0.688191f }, 
    {  0.688191f, -0.587785f,  0.425325f }, { -0.955423f,  0.295242f,  0.000000f }, 
    { -0.951056f,  0.162460f,  0.262866f }, { -1.000000f,  0.000000f,  0.000000f }, 
    { -0.850651f,  0.000000f,  0.525731f }, { -0.955423f, -0.295242f,  0.000000f }, 
    { -0.951056f, -0.162460f,  0.262866f }, { -0.864188f,  0.442863f, -0.238856f }, 
    { -0.951056f,  0.162460f, -0.262866f }, { -0.809017f,  0.309017f, -0.500000f }, 
    { -0.864188f, -0.442863f, -0.238856f }, { -0.951056f, -0.162460f, -0.262866f }, 
    { -0.809017f, -0.309017f, -0.500000f }, { -0.681718f,  0.147621f, -0.716567f }, 
    { -0.681718f, -0.147621f, -0.716567f }, { -0.850651f,  0.000000f, -0.525731f }, 
    { -0.688191f,  0.587785f, -0.425325f }, { -0.587785f,  0.425325f, -0.688191f }, 
    { -0.425325f,  0.688191f, -0.587785f }, { -0.425325f, -0.688191f, -0.587785f }, 
    { -0.587785f, -0.425325f, -0.688191f }, { -0.688191f, -0.587785f, -0.425325f }
};

uint8_t FindClosestNormal(float3 normal) {
    float bestdot = -1;
    uint8_t besti = 0;
    for (uint8_t i = 0; i < normal_table_size; i++) {
        auto d = dot(normal, default_normals[i]);
        if (d > bestdot) {
            bestdot = d;
            besti = i;
        }
    }
    return besti;
}

const size_t palette_size = 256 * sizeof(byte4);

size_t NewPalette(const byte4 *p) {
    auto hash = FNV1A64(string_view((const char *)&p, palette_size));
    // See if there's an existing matching palette.
    for (auto [pali, pal] : enumerate(palettes)) {
        if (pal.hash == hash &&  // Quick reject.
            memcmp(pal.colors.data(), p, palette_size) == 0) {
            return pali;  // Yay, found existing matching palette!
        }
    }
    // Need to create a new one.
    palettes.emplace_back(Palette{});
    auto &palette = palettes.back();
    palette.hash = hash;
    palette.colors.insert(palette.colors.end(), p, p + 256);
    return palettes.size() - 1;
}

Voxels *NewWorld(const int3 &size, size_t palette_idx) {
    auto v = new Voxels(size, palette_idx);
    if (palettes.empty()) {
        // Guarantees init of default_palette_idx (0) and normal_palette_idx (1).
        NewPalette((byte4 *)default_palette);
        vector<byte4> normal_palette;
        normal_palette.resize(256, quantizec(float3(0.5), 0));
        for (uint8_t i = 0; i < normal_table_size; i++) {
            normal_palette[i] = quantizec((default_normals[i] + 1) / 2, 0);
        }
        NewPalette(normal_palette.data());
    }
    return v;
}

Value CubesFromMeshGen(VM &vm, const DistGrid &grid, int targetgridsize, int zoffset) {
    auto &v = *NewWorld(int3_1 * targetgridsize, default_palette_idx);
    auto off = (grid.dim - v.grid.dim) / 2;
    off.z += zoffset;
    for (int x = 0; x < v.grid.dim.x; x++) {
        for (int y = 0; y < v.grid.dim.y; y++) {
            for (int z = 0; z < v.grid.dim.z; z++) {
                auto pos = int3(x, y, z);
                auto spos = pos + off;
                uint8_t np = transparant;
                if (spos >= 0 && spos < grid.dim) {
                    auto &dgc = grid.Get(spos);
                    np = v.Color2Palette(float4(color2vec(dgc.color).xyz(), dgc.dist <= 0));
                }
                v.grid.Get(pos) = np;
            }
        }
    }
    return vm.NewResource(&v, &voxel_type);
}

}

using namespace lobster;

void CubeGenClear() {
}

void AddCubeGen(NativeRegistry &nfr) {

nfr("cg_init", "size", "I}:3", "R:voxels",
    "initializes a new, empty 3D cube block. 1 byte per cell, careful with big sizes :)"
    " returns the block",
    [](StackPtr &sp, VM &vm) {
        auto v = NewWorld(PopVec<int3>(sp), default_palette_idx);
        Push(sp, vm.NewResource(v, &voxel_type));
    });

nfr("cg_size", "block", "R:voxels", "I}:3",
    "returns the current block size",
    [](StackPtr &sp, VM &) {
        PushVec(sp, GetVoxels(Pop(sp)).grid.dim);
    });

nfr("cg_set", "block,pos,size,paletteindex", "R:voxelsI}:3I}:3I", "",
    "sets a range of cubes to palette index. index 0 is considered empty space."
    "Coordinates automatically clipped to the size of the grid",
    [](StackPtr &sp, VM &) {
        auto color = Pop(sp).ival();
        auto size = PopVec<int3>(sp);
        auto pos = PopVec<int3>(sp);
        auto res = Pop(sp);
        GetVoxels(res).Set(pos, size, (uint8_t)color);
    });

nfr("cg_get", "block,pos", "R:voxelsI}:3", "I",
    "sets a range of cubes to palette index. index 0 is considered empty space."
    "Coordinates automatically clipped to the size of the grid",
    [](StackPtr &sp, VM &) {
        auto pos = PopVec<int3>(sp);
        auto res = Pop(sp);
        Push(sp, GetVoxels(res).grid.Get(pos));
    });

nfr("cg_copy", "block,pos,size,dest,flip", "R:voxelsI}:3I}:3I}:3I}:3", "",
    "copy a range of cubes from pos to dest. flip can be 1 (regular copy), or -1 (mirror)for"
    " each component, indicating the step from dest."
    " Coordinates automatically clipped to the size of the grid",
    [](StackPtr &sp, VM &) {
        auto fl = PopVec<int3>(sp);
        auto d = PopVec<int3>(sp);
        auto sz = PopVec<int3>(sp);
        auto p = PopVec<int3>(sp);
        auto res = Pop(sp);
        GetVoxels(res).Copy(p, sz, d, fl);
    });

nfr("cg_clone", "block,pos,size", "R:voxelsI}:3I}:3", "R:voxels",
    "clone a range of cubes from pos to a new block."
    " Coordinates automatically clipped to the size of the grid",
    [](StackPtr &sp, VM &vm) {
        auto sz = PopVec<int3>(sp);
        auto p = PopVec<int3>(sp);
        auto res = Pop(sp);
        auto &v = GetVoxels(res);
        auto nw = NewWorld(sz, v.palette_idx);
        v.Clone(p, sz, nw);
        Push(sp, vm.NewResource(nw, &voxel_type));
    });

nfr("cg_color_to_palette", "block,color", "R:voxelsF}:4", "I",
    "converts a color to a palette index. alpha < 0.5 is considered empty space."
    " note: this is fast for the default palette, slow otherwise.",
    [](StackPtr &sp, VM &) {
        auto color = PopVec<float4>(sp);
        auto res = Pop(sp);
        Push(sp, GetVoxels(res).Color2Palette(color));
    });

nfr("cg_palette_to_color", "block,paletteindex", "R:voxelsI", "F}:4",
    "converts a palette index to a color. empty space (index 0) will have 0 alpha",
    [](StackPtr &sp, VM &) {
        auto p = uint8_t(Pop(sp).ival());
        auto res = Pop(sp);
        PushVec(sp, color2vec(palettes[GetVoxels(res).palette_idx].colors[p]));
    });

nfr("cg_copy_palette", "fromworld,toworld", "R:voxelsR:voxels", "", "",
    [](StackPtr &, VM &, Value &fromworld, Value &toworld) {
        auto &w1 = GetVoxels(fromworld);
        auto &w2 = GetVoxels(toworld);
        w2.palette_idx = w1.palette_idx;
        return NilVal();
    });

nfr("cg_sample_down", "scale,world", "IR:voxels", "", "",
    [](StackPtr &, VM &vm, Value &scale, Value &world) {
        auto sc = scale.intval();
        if (sc < 2 || sc > 128)
            vm.Error("cg_sample_down: scale out of range");
        auto &v = GetVoxels(world);
        auto &palette = palettes[v.palette_idx].colors;
        for (int x = 0; x < v.grid.dim.x / sc; x++) {
            for (int y = 0; y < v.grid.dim.y / sc; y++) {
                for (int z = 0; z < v.grid.dim.z / sc; z++) {
                    auto pos = int3(x, y, z);
                    int4 acc(0);
                    for (int xd = 0; xd < sc; xd++) {
                        for (int yd = 0; yd < sc; yd++) {
                            for (int zd = 0; zd < sc; zd++) {
                                auto d = int3(xd, yd, zd);
                                auto c = v.grid.Get(pos * sc + d);
                                acc += int4(palette[c]);  // FIXME: not SRGB aware.
                            }
                        }
                    }
                    auto np = v.Color2Palette(float4(acc) / float(sc * sc * sc * 255));
                    v.grid.Get(pos) = np;
                }
            }
        }
        v.grid.Shrink(v.grid.dim / sc);
        return NilVal();
    });

nfr("cg_scale_up", "scale,world", "IR:voxels", "R:voxels", "",
    [](StackPtr &, VM &vm, Value &scale, Value &world) {
        auto sc = scale.intval();
        auto &v = GetVoxels(world);
        if (sc < 2 || sc > 256 || squaredlength(v.grid.dim) * sc > 2048)
            vm.Error("cg_scale_up: scale out of range");
        auto &d = *NewWorld(v.grid.dim * sc, v.palette_idx);
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    auto pos = int3(x, y, z);
                    auto p = v.grid.Get(pos);
                    for (int xd = 0; xd < sc; xd++) {
                        for (int yd = 0; yd < sc; yd++) {
                            for (int zd = 0; zd < sc; zd++) {
                                auto vd = int3(xd, yd, zd);
                                d.grid.Get(pos * sc + vd) = p;
                            }
                        }
                    }
                }
            }
        }
        return Value(vm.NewResource(&d, &voxel_type));
    });

nfr("cg_stretch", "newsize,world", "I}:3R:voxels", "R:voxels", "",
    [](StackPtr &sp, VM &vm) {
        auto &v = GetVoxels(Pop(sp));
        auto ns = PopVec<int3>(sp);
        if (!(v.grid.dim <= ns) || !(ns < 256))
            vm.Error("cg_stretch: newsize out of range");
        auto &d = *NewWorld(ns, v.palette_idx);
        auto delta = ns - v.grid.dim;
        for (int xd = 0; xd < ns.x; xd++) {
            for (int yd = 0; yd < ns.y; yd++) {
                for (int zd = 0; zd < ns.z; zd++) {
                    auto vd = int3(xd, yd, zd);
                    auto od = vd;
                    for (int i = 0; i < 3; i++) {
                        int mid = v.grid.dim[i] / 2;
                        if (vd[i] >= mid) {
                            od[i] = vd[i] >= mid + delta[i] ? vd[i] - delta[i] : mid;
                        }
                    }
                    d.grid.Get(vd) = v.grid.Get(od);
                }
            }
        }
        Push(sp, Value(vm.NewResource(&d, &voxel_type)));
    });

nfr("cg_create_mesh", "block", "R:voxels", "R:mesh",
    "converts block to a mesh",
    [](StackPtr &, VM &vm, Value &wid) {
        auto &v = GetVoxels(wid);
        auto &palette = palettes[v.palette_idx].colors;
        static int3 neighbors[] = {
            int3(1, 0, 0), int3(-1,  0,  0),
            int3(0, 1, 0), int3( 0, -1,  0),
            int3(0, 0, 1), int3( 0,  0, -1),
        };
        // FIXME: normal can be byte4, pos can short4
        struct cvert { float3 pos; float3 normal; byte4 color; };
        vector<cvert> verts;
        vector<int> triangles;
        static const char *faces[6] = { "4576", "0231", "2673", "0154", "1375", "0462" };
        static int indices[6] = { 0, 1, 3, 1, 2, 3 };
        // off by default because slow, maybe try: https://github.com/greg7mdp/sparsepp
        bool optimize_verts = false;
        struct VKey {
            vec<short, 3> pos;
            uint8_t pal, dir;
            bool operator==(const VKey &o) const {
                return pos == o.pos && pal == o.pal && dir == o.dir;
            };
        };
        auto hasher = [](const VKey &k) {
            return k.pos.x ^ (k.pos.y << 3) ^ (k.pos.z << 6) ^ (k.pal << 3) ^ k.dir;
        };
        unordered_map<VKey, int, decltype(hasher)> vertlookup(optimize_verts ? 100000 : 10, hasher);
        vector<float> rnd_offset(1024);
        for (auto &f : rnd_offset) { f = cg_rnd.rnd_float_signed() * 0.075f; }
        // Woah nested loops!
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    auto pos = int3(x, y, z);
                    auto c = v.grid.Get(pos);
                    if (c != transparant) {
                        for (int n = 0; n < 6; n++) {
                            auto npos = pos + neighbors[n];
                            auto nc = npos >= 0 && npos < v.grid.dim ? v.grid.Get(npos)
                                                                     : transparant;
                            if (nc == transparant) {
                                auto face = faces[n];
                                int vindices[4];
                                for (int vn = 0; vn < 4; vn++) {
                                    int3 vpos;
                                    for (int d = 0; d < 3; d++) {
                                        vpos[d] = (face[vn] & (1 << (2 - d))) != 0;
                                    }
                                    vpos += pos;
                                    VKey vkey { vec<short, 3>(vpos), c, (uint8_t)n };
                                    if (optimize_verts) {
                                        auto it = vertlookup.find(vkey);
                                        if (it != vertlookup.end()) {
                                            vindices[vn] = it->second;
                                            continue;
                                        }
                                    }
                                    cvert vert;
                                    auto oi = ((vpos.z << 8) ^ (vpos.y << 4) ^ vpos.x) %
                                              (rnd_offset.size() - 2);
                                    auto offset = float3(&rnd_offset[oi]);
                                    vert.pos = float3(vpos) + offset;
                                    vert.normal = float3(neighbors[n]);
                                    vert.color = palette[c];
                                    vindices[vn] = (int)verts.size();
                                    verts.push_back(vert);
                                    if (optimize_verts) vertlookup[vkey] = vindices[vn];
                                }
                                for (int i = 0; i < 6; i++)
                                    triangles.push_back(vindices[indices[i]]);
                            }
                        }
                    }
                }
            }
        }
        normalize_mesh(gsl::make_span(triangles), verts.data(), verts.size(),
                       sizeof(cvert), (uint8_t *)&verts.data()->normal - (uint8_t *)&verts.data()->pos,
                       false);
        LOG_INFO("cubegen verts = ", verts.size(), ", tris = ", triangles.size() / 3);
        auto m = new Mesh(new Geometry(gsl::make_span(verts), "PNC"),
                          PRIM_TRIS);
        m->surfs.push_back(new Surface(gsl::make_span(triangles), PRIM_TRIS));
        extern ResourceType mesh_type;
        return Value(vm.NewResource(m, &mesh_type));
    });

nfr("cg_create_3d_texture", "block,textureformat,monochrome", "R:voxelsII?", "R:texture",
    "returns the new texture, for format, pass flags you want in addition to"
    " 3d|single_channel|has_mips",
    [](StackPtr &, VM &vm, Value &wid, Value &textureflags, Value &monochrome) {
        auto &v = GetVoxels(wid);
        auto &palette = palettes[v.palette_idx].colors;
        auto mipsizes = 0;
        for (auto d = v.grid.dim; d.x; d /= 2) mipsizes += d.volume();
        auto buf = new uint8_t[mipsizes];
        v.grid.ToContinousGrid(buf);
        auto mipb = buf;
        for (auto db = v.grid.dim; db.x > 1; db /= 2) {
            auto ds = db / 2;
            auto mips = mipb + db.volume();
            for (int z = 0; z < ds.z; z++) {
                auto zb = z * 2;
                for (int y = 0; y < ds.y; y++) {
                    auto yb = y * 2;
                    for (int x = 0; x < ds.x; x++) {
                        auto xb = x * 2;
                        auto sum = float4_0;
                        int filled = 0;
                        for (int sz = 0; sz < 2; sz++) {
                            for (int sy = 0; sy < 2; sy++) {
                                for (int sx = 0; sx < 2; sx++) {
                                    auto i = mipb[(zb + sz) * db.x * db.y +
                                                    (yb + sy) * db.x + xb + sx];
                                    if (i != transparant) { sum += float4(palette[i]); filled++; }
                                }
                            }
                        }
                        auto pi = filled >= 4 ? v.Color2Palette(sum / (filled * 255.0f))
                                              : transparant;
                        mips[z * ds.x * ds.y + y * ds.x + x] = pi;
                    }
                }
            }
            mipb = mips;
        }
        if (monochrome.True()) {
            for (int i = 0; i < mipsizes; i++) buf[i] = buf[i] ? 255 : 0;
        }
        auto tex = CreateTexture(buf, v.grid.dim,
            TF_3D | /*TF_NEAREST_MAG | TF_NEAREST_MIN | TF_CLAMP |*/ TF_SINGLE_CHANNEL |
            TF_BUFFER_HAS_MIPS | textureflags.intval());
        delete[] buf;
        extern ResourceType texture_type;
        return Value(vm.NewResource(new Texture(tex), &texture_type));
    });

// https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox.txt
nfr("cg_load_vox", "name", "S", "R:voxels]S?",
    "loads a .vox file (supports both MagicaVoxel or VoxLap formats). "
    "returns vector of blocks or empty if file failed to load, and error string if any",
    [](StackPtr &sp, VM &vm, Value &name) {
        auto namep = name.sval()->strv();
        string buf;
        auto voxvec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_RESOURCE);
        auto errf = [&](string_view err) {
            // TODO: could clear voxvec elements if any?
            Push(sp, Value(voxvec));
            return Value(vm.NewString(err));
        };
        auto l = LoadFile(namep, &buf);
        if (l < 0)
            return errf(cat("could not load ", namep));
        if (strncmp(buf.c_str(), "VOX ", 4) == 0) {
            // This looks like a MagicaVoxel file.
            int3 size = int3_0;
            auto p = buf.c_str() + 8;
            bool chunks_skipped = false;
            Voxels *voxels = nullptr;
            while (p < buf.c_str() + buf.length()) {
                auto id = p;
                p += 4;
                auto contentlen = *((int *)p);
                p += 8;
                if (!strncmp(id, "SIZE", 4)) {
                    size = int3((int *)p);
                    voxels = NewWorld(size, default_palette_idx);
                    voxvec->Push(vm, Value(vm.NewResource(voxels, &voxel_type)));
                } else if (!strncmp(id, "RGBA", 4)) {
                    if (!voxels) return errf(".vox file RGBA chunk in wrong order");
                    vector<byte4> palette;
                    palette.push_back(byte4_0);
                    palette.insert(palette.end(), (byte4 *)p, ((byte4 *)p) + 255);
                    auto pi = NewPalette(palette.data());
                    for (iint i = 0; i < voxvec->len; i++) {
                        GetVoxels(voxvec->At(i)).palette_idx = pi;
                    }
                } else if (!strncmp(id, "XYZI", 4)) {
                    if (!voxels) return errf(".vox file XYZI chunk in wrong order");
                    auto numvoxels = *((int *)p);
                    for (int i = 0; i < numvoxels; i++) {
                        auto vox = byte4((uint8_t *)(p + i * 4 + 4));
                        auto pos = int3(vox.xyz());
                        if (pos < voxels->grid.dim) voxels->grid.Get(pos) = vox.w;
                    }
                } else if (!strncmp(id, "MAIN", 4)) {
                    // Ignore, wrapper around the above chunks.
                } else if (!strncmp(id, "PACK", 4)) {
                    // Ignore, tells us how many models, but we simply load em all.
                } else {
                    chunks_skipped = true;
                }
                p += contentlen;
            }
            if (!voxels) return errf(".vox file missing SIZE chunk");
            voxels->chunks_skipped = chunks_skipped;  // FIXME: only on last model.
        } else {
            // It may be a voxlap file which uses the same extension, exported e.g. from Qubicle.
            // Sadly these don't have a header, so rely on verifying the size.
            const int voxlap_palette_size = 256 * 3;
            if (buf.size() < sizeof(int3) + voxlap_palette_size + 1)
                return errf(".vox file too small");
            auto p = (const uint8_t *)buf.c_str();
            int3 size = int3((int *)p);
            p += sizeof(int3);
            if (!(size > 0) || !(size <= 1024)) return errf("voxlap XYZ size out of range");
            auto vol = size.volume();
            if (vol + voxlap_palette_size + sizeof(int3) != buf.size())
                return errf("voxlap XYZ size does not match file size");
            // Now should be save to read.
            auto voxels = NewWorld(size, default_palette_idx);
            voxvec->Push(vm, Value(vm.NewResource(voxels, &voxel_type)));
            for (int i = 0; i < vol; i++) {
                auto c = *p++;
                c = c == 255 ? 0 : c + 1;  // 255 is transparent;
                auto z = i % size.z;
                auto y = (i / size.z) % size.y;
                auto x = i / (size.z * size.y);
                auto pos = int3(x, y, z);
                pos = size - pos - 1;  // Coords are flipped?
                voxels->grid.Get(pos) = c;
            }
            vector<byte4> palette;
            palette.push_back(byte4_0);
            for (int i = 0; i < 255; i++) {
                byte4 c = byte4(p);
                p += 3;
                c *= 4;  // Values range from 0..63?
                c.w = 0xFF;
                palette.push_back(c);
            }
            voxels->palette_idx = NewPalette(palette.data());
        }
        Push(sp, Value(voxvec));
        return NilVal();
    });

nfr("cg_save_vox", "block,name", "R:voxelsS", "B",
    "saves a file in the .vox format (MagicaVoxel). returns false if file failed to save."
    " this format can only save blocks < 256^3, will fail if bigger",
    [](StackPtr &, VM &, Value &wid, Value &name) {
        auto &v = GetVoxels(wid);
        if (!(v.grid.dim < 256)) { return Value(false); }
        vector<byte4> voxels;
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    auto pos = int3(x, y, z);
                    auto i = v.grid.Get(pos);
                    if (i) voxels.push_back(byte4(int4(pos, i)));
                }
            }
        }
        FILE *f = OpenForWriting(name.sval()->strv(), true);
        if (!f) return Value(false);
        auto wint = [&](int i) { fwrite(&i, 4, 1, f); };
        auto wstr = [&](const char *s) { fwrite(s, 4, 1, f); };
        wstr("VOX ");
        wint(150);
        wstr("MAIN");
        wint(0);
        int bsize = 24;                                 // SIZE chunk.
        bsize += 16 + (int)voxels.size() * 4;           // XYZI chunk.
        if (v.palette_idx != default_palette_idx) bsize += 12 + 1024;  // RGBA chunk.
        wint(bsize);
        wstr("SIZE");
        wint(12);
        wint(0);
        wint(v.grid.dim.x);
        wint(v.grid.dim.y);
        wint(v.grid.dim.z);
        wstr("XYZI");
        wint((int)voxels.size() * 4 + 4);
        wint(0);
        wint((int)voxels.size());
        fwrite(voxels.data(), 4, voxels.size(), f);
        if (v.palette_idx != default_palette_idx) {
            wstr("RGBA");
            wint(256 * 4);
            wint(0);
            fwrite(palettes[v.palette_idx].colors.data() + 1, 4, 255, f);
            wint(0);
        }
        fclose(f);
        return Value(true);
    });

nfr("cg_chunks_skipped", "block", "R:voxels", "B", "",
    [](StackPtr &, VM &, Value &wid) {
        auto &v = GetVoxels(wid);
        return Value(v.chunks_skipped);
    });

nfr("cg_get_buf", "block", "R:voxels", "S",
    "returns the data as a string of all palette indices, in z-major order",
    [](StackPtr &, VM &vm, Value &wid) {
        auto &v = GetVoxels(wid);
        auto buf = vm.NewString(v.grid.dim.volume());
        v.grid.ToContinousGrid((uint8_t *)buf->strv().data());
        return Value(buf);
    });

nfr("cg_average_surface_color", "world", "R:voxels", "F}:3II", "",
	[](StackPtr &sp, VM &) {
		auto &v = GetVoxels(Pop(sp));
        auto &palette = palettes[v.palette_idx].colors;
		float3 col(0.0f);
		int nsurf = 0;
		int nvol = 0;
		int3 neighbors[] = {
			int3(0, 0, 1),  int3(0, 1, 0),  int3(1, 0, 0),
			int3(0, 0, -1), int3(0, -1, 0), int3(-1, 0, 0),
		};
		for (int x = 0; x < v.grid.dim.x; x++) {
			for (int y = 0; y < v.grid.dim.y; y++) {
				for (int z = 0; z < v.grid.dim.z; z++) {
					auto pos = int3(x, y, z);
					uint8_t c = v.grid.Get(pos);
					if (c != transparant) {
						nvol++;
						// Only count voxels that lay on the surface for color average.
						for (int i = 0; i < 6; i++) {
							auto p = pos + neighbors[i];
							if (!(p >= 0) || !(p < v.grid.dim) || !v.grid.Get(p)) {
								col += from_srgb(float3(palette[c].xyz()) / 255.0f);
								nsurf++;
								break;
							}
						}
					}
				}
			}
		}
		if (nsurf) col /= float(nsurf);
		PushVec(sp, col);
        Push(sp, nsurf);
        Push(sp, nvol);
	});

nfr("cg_num_solid", "world", "R:voxels", "I", "",
    [](StackPtr &sp, VM &) {
        auto &v = GetVoxels(Pop(sp));
        int nvol = 0;
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    auto pos = int3(x, y, z);
                    uint8_t c = v.grid.Get(pos);
                    if (c != transparant) nvol++;
                }
            }
        }
        Push(sp, nvol);
    });

nfr("cg_rotate", "block,n", "R:voxelsI", "R:voxels",
    "returns a new block rotated by n 90 degree steps from the input",
    [](StackPtr &, VM &vm, Value &wid, Value &rots) {
        auto &v = GetVoxels(wid);
        auto n = rots.ival();
        auto &d = n == 1 || n == 3
			? *NewWorld(int3(v.grid.dim.y, v.grid.dim.x, v.grid.dim.z), v.palette_idx)
            : *NewWorld(v.grid.dim, v.palette_idx);
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    uint8_t c = v.grid.Get(int3(x, y, z));
					switch (n) {
                        case 1:
							d.grid.Get(int3(v.grid.dim.y - y - 1, x, z)) = c;
							break;
                        case 2:
                            d.grid.Get(int3(v.grid.dim.x - x - 1, v.grid.dim.y - y - 1, z)) = c;
                            break;
                        case 3:
							d.grid.Get(int3(y, v.grid.dim.x - x - 1, z)) = c;
							break;
                        default:
							d.grid.Get(int3(x, y, z)) = c;
							break;
					}
                }
            }
        }
        return Value(vm.NewResource(&d, &voxel_type));
    });

nfr("cg_simplex", "block,pos,size,spos,ssize,octaves,scale,persistence,solidcol,zscale,zbias", "R:voxelsI}:3I}:3F}:3F}:3IFFIFF", "",
    "",
    [](StackPtr &sp, VM &) {
        auto zbias = Pop(sp).fltval();
        auto zscale = Pop(sp).fltval();
        auto solidcol = Pop(sp).intval();
        auto persistence = Pop(sp).fltval();
        auto scale = Pop(sp).fltval();
        auto octaves = Pop(sp).intval();
        auto ssize = PopVec<float3>(sp);
        auto spos = PopVec<float3>(sp);
        auto sz = PopVec<int3>(sp);
        auto p = PopVec<int3>(sp);
        auto res = Pop(sp);
        auto &v = GetVoxels(res);
        v.Do(p, sz, [&](const int3 &pos, uint8_t &vox) {
            auto sp = (float3(pos - p) + 0.5) / float3(sz) * ssize + spos;
            auto fun = SimplexNoise(octaves, persistence, scale, sp) + sp.z * zscale - zbias;
            vox = uint8_t((fun < 0) * solidcol);
        });
    });

nfr("cg_bounding_box", "world,minsolids", "R:voxelsF", "I}:3I}:3",
    "",
	[](StackPtr &sp, VM &) {
        auto minsolids = Pop(sp).fltval();
        auto res = Pop(sp);
		auto &v = GetVoxels(res);
        auto bmin = int3_0;
        auto bmax = v.grid.dim;
        auto bestsolids = 0.0f;
        while ((bmax - bmin).volume()) {
            bestsolids = 1.0f;
            auto smin = bmin;
            auto smax = bmax;
            for (int c = 0; c < 3; c++) {
                for (int mm = 0; mm < 2; mm++) {
                    auto tmin = bmin;
                    auto tmax = bmax;
                    if (mm) tmin[c] = tmax[c] - 1;
                    else tmax[c] = tmin[c] + 1;
                    int solid = 0;
                    v.Do(tmin, tmax - tmin, [&](const int3 &, uint8_t &vox) {
                        if (vox) solid++;
                    });
                    auto total = (tmax - tmin).volume();
                    auto ratio = solid / float(total);
                    if (ratio < bestsolids) {
                        bestsolids = ratio;
                        smin = bmin;
                        smax = bmax;
                        if (mm) smax[c]--;
                        else smin[c]++;
                    }
                }
            }
            if (bestsolids <= minsolids) {
                bmin = smin;
                bmax = smax;
            } else {
                break;
            }
        }
        PushVec(sp, bmin);
        PushVec(sp, bmax);
	});

nfr("cg_randomize", "world,rnd_range,cutoff,paletteindex,filter", "R:voxelsIIII", "", "",
    [](StackPtr &, VM &, Value &world, Value &rnd_range, Value &cutoff, Value &paletteindex, Value &filter) {
        auto &v = GetVoxels(world);
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    auto pos = int3(x, y, z);
                    auto &p = v.grid.Get(pos);
                    if (p != filter.ival() && cg_rnd.rnd_int(rnd_range.intval()) < cutoff.intval())
                        p = (uint8_t)paletteindex.ival();
                }
            }
        }
        return NilVal();
    });

nfr("cg_erode", "world,minsolid,maxsolid", "R:voxelsII", "R:voxels", "",
    [](StackPtr &, VM &vm, Value &world, Value &minsolid, Value &maxsolid) {
        auto &v = GetVoxels(world);
        auto &d = *NewWorld(v.grid.dim, v.palette_idx);
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    auto pos = int3(x, y, z);
                    auto p = v.grid.Get(pos);
                    int nsolid = 0;
                    uint8_t last_solid = 0;
                    for (int xd = -1; xd <= 1; xd++) {
                        for (int yd = -1; yd <= 1; yd++) {
                            for (int zd = -1; zd <= 1; zd++) {
                                auto vd = int3(xd, yd, zd);
                                auto pp = pos + vd;
                                if (pp >= 0 && pp < v.grid.dim) {
                                    auto n = v.grid.Get(pp);
                                    if (n) {
                                        nsolid++;
                                        last_solid = n;
                                    }
                                }
                            }
                        }
                    }
                    if (p && nsolid <= minsolid.intval()) p = 0;
                    else if (!p && nsolid >= maxsolid.intval()) p = last_solid;
                    d.grid.Get(pos) = p;
                }
            }
        }
        return Value(vm.NewResource(&d, &voxel_type));
    });

nfr("cg_normal_indices", "block,radius", "R:voxelsI", "R:voxels",
    "creates a new block with normal indices based on voxel surface shape."
    "the indices refer to the associated pallette."
    "empty voxels will have a 0 length normal."
    "2 is a good radius that balances speed/quality, use 1 for speed, 3 for max quality",
    [](StackPtr &sp, VM &vm) {
        auto radius = Pop(sp).intval();
        auto res = Pop(sp);
        auto &v = GetVoxels(res);

        auto nw = NewWorld(v.grid.dim, normal_palette_idx);
        auto ComputeNormal = [&](int3 c, int rad) {
            int3 normal = int3_0;
            for (int x = -rad; x <= rad; x++) {
                for (int y = -rad; y <= rad; y++) {
                    for (int z = -rad; z <= rad; z++) {
                        int3 s = int3(x, y, z) + c;
                        if (s != c) {
                            if (s < v.grid.dim && s >= 0 && v.grid.Get(s) != 0) {
                                // normal -= s - c;
                            } else {
                                normal += s - c;
                            }
                        }
                    }
                }
            }
            return normal;
        };
        int3 neighbors[6] = {
            int3(0, 0, 1),  int3(0, 0, -1), int3(0, 1, 0),
            int3(0, -1, 0), int3(1, 0, 0),  int3(-1, 0, 0),
        };
        int num_surface_voxels = 0;
        for (int x = 0; x < v.grid.dim.x; x++) {
            for (int y = 0; y < v.grid.dim.y; y++) {
                for (int z = 0; z < v.grid.dim.z; z++) {
                    auto pos = int3(x, y, z);
                    auto p = v.grid.Get(pos);
                    if (!p) {
                        // Empty cells don't need a normal.
                        nw->grid.Get(pos) = 0xFF;
                        continue;
                    }
                    // If the voxel is on the edge of the model it is visible.
                    if (max(pos.eq(0)) != 1 && max(pos.eq(v.grid.dim - 1)) != 1) {
                        // Check the 6 neighbors, if any are empty, it is visble.
                        for (int i = 0; i < 6; i++) {
                            if (v.grid.Get(pos + neighbors[i]) == 0) goto visible;
                        }
                        // Not visible, early out.
                        nw->grid.Get(pos) = 0xFF;
                        continue;
                        visible:;
                    }
                    // For surface voxels we compute a normal.
                    num_surface_voxels++;
                    auto cn = ComputeNormal(pos, radius);
                    if (manhattan(cn) <= 1) {
                        // Most normals cancelled eachother out, let's try once more
                        // with a wider radius.
                        cn = ComputeNormal(pos, radius * 2);
                    }
                    if (manhattan(cn) == 0) {
                        // This should be very rare in actual models, just use Z-up.
                        cn = int3(0, 0, 1);
                    }
                    nw->grid.Get(pos) = FindClosestNormal(normalize(float3(cn)));
                }
            }
        }
        Push(sp, vm.NewResource(nw, &voxel_type));
    });


nfr("cg_load_image", "name,depth,edge,numtiles", "SIII}:2", "R:voxels]",
    "loads an image file (same formats as gl_load_texture) and turns it into blocks."
    " returns blocks or [] if file failed to load",
    [](StackPtr &sp, VM &vm) {
        auto numtiles = PopVec<int2>(sp);
        auto edge = Pop(sp).intval();
        auto depth = Pop(sp).intval();
        auto name = Pop(sp).sval()->strv();
        auto idim = int2_0;
        auto buf = LoadImageFile(name, idim);
        auto dim = idim / numtiles;
        auto vec = vm.NewVec(0, numtiles.x * numtiles.y, TYPE_ELEM_VECTOR_OF_RESOURCE);
        if (buf) {
            for (int ty = 0; ty < numtiles.y; ty++) {
                for (int tx = 0; tx < numtiles.x; tx++) {
                    // FIXME: make orientation configurable.
                    auto size = int3(dim.x, depth, dim.y);
                    Voxels *voxels = NewWorld(size, default_palette_idx);
                    int2 neighbors[] = { int2(0, 1), int2(0, -1), int2(1, 0), int2(-1, 0) };
                    auto Get = [&](int2 p) {
                        return ((byte4 *)buf)[(p.x + tx * dim.x) + (dim.y - p.y - 1 + ty * dim.y) * idim.x];
                    };
                    for (int y = 0; y < dim.y; y++) {
                        for (int x = 0; x < dim.x; x++) {
                            int ndist = 0;
                            for (int e = 1; e <= edge; e++) {
                                for (int c = 0; c < 4; c++) {
                                    auto p = int2(x, y) + neighbors[c] * e;
                                    if (!(p >= 0 && p < dim) || Get(p).w < 128) {
                                        ndist = edge - e + 1;
                                        goto done;
                                    }
                                }
                            }
                            done:
                            auto col = color2vec(Get(int2(x, y)));
                            auto pi = voxels->Color2Palette(col);
                            for (int d = 0; d < depth; d++) {
                                auto p = int3(x, d, y);
                                auto dd = d < depth / 2 ? d : depth - 1 - d;
                                voxels->grid.Get(p) = dd < ndist ? transparant : pi;
                            }
                        }
                    }
                    vec->Push(vm, vm.NewResource(voxels, &voxel_type));
                }
            }
            FreeImageFromFile(buf);
        }
        Push(sp, vec);
    });

nfr("cg_palette_storage_index", "block", "R:voxels", "I", "",
    [](StackPtr &sp, VM &) {
        auto res = Pop(sp);
        Push(sp, GetVoxels(res).palette_idx);
    });

nfr("cg_get_palette_storage_len", "", "", "I", "",
    [](StackPtr &, VM &) {
        return Value(palettes.size());
    });

nfr("cg_get_palette_storage_buf", "", "", "S", "",
    [](StackPtr &, VM &vm) {
        auto buf = vm.NewString(palettes.size() * palette_size);
        for (auto [i, pal] : enumerate(palettes)) {
            memcpy((uint8_t *)buf->strv().data() + i * palette_size, pal.colors.data(),
                   palette_size);
        }
        return Value(buf);
    });

}  // AddCubeGen
