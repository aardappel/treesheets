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

#ifndef LOBSTER_MESHGEN_H
#define LOBSTER_MESHGEN_H

#include "3dgrid.h"

struct DistVert {  // FIXME: can optimize this for memory usage making both 16bit
    float dist;
    byte4 color;

    DistVert() : dist(FLT_MAX), color(byte4_0) {}
};

// Turns out the way shapes overlap and are rasterized invidually below makes this not as efficient
// a data structure for this purpose as it at first seemed.
//typedef RLE3DGrid<DistVert> DistGrid;
//typedef RLE3DGrid<int> IntGrid;

// So use this instead:
typedef Chunk3DGrid<DistVert> DistGrid;
typedef Chunk3DGrid<int> IntGrid;
typedef Chunk3DGrid<int3> EdgeGrid;

struct mgvert {
    float3 pos;
    float3 norm;
    byte4 col;
};

inline void RecomputeNormals(vector<int> &triangles, vector<mgvert> &verts) {
    normalize_mesh(make_span(triangles), verts.data(), verts.size(), sizeof(mgvert),
                   (uchar *)&verts.data()->norm - (uchar *)&verts.data()->pos, false);
};

extern Mesh *polygonize_mc(const int3 &gridsize, float gridscale, const float3 &gridtrans,
                           const DistGrid *distgrid, float3 (* grid_to_world)(const int3 &pos));

#endif  // LOBSTER_MESHGEN_H
