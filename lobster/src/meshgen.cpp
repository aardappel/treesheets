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

#include "lobster/meshgen.h"
#include "lobster/mctables.h"
#include "lobster/polyreduce.h"
#include "lobster/cubegen.h"

#include "lobster/simplex.h"

#include "lobster/graphics.h"

#include "ThreadPool/ThreadPool.h"

using namespace lobster;

/* TODO:

- a function that takes a list of cylinder widths to be able to do wineglasses etc
- replace pow by something faster?
- proper groups, so it can help in early culling
- but big grids in compressed columns, so it can possibly run on larger dimension
- turn some of the push_back vector stuff into single allocations
- could work on sharp crease boolean version (see treesheets)
- crease texturing: verts either get a tc of (0, rnd) when they are on a crease, or (1, rnd) when
  they are flat
*/

struct ImplicitFunction;

float3 rotated_size(const float3x3 &rot, const float3 &size) {
    float3x3 absm = float3x3(abs(rot[0]), abs(rot[1]), abs(rot[2]));
    return absm * size;
}

struct ImplicitFunction {
    float3 orig = float3_0;
    float3 size = float3_1;
    float3x3 rot = float3x3_1;
    float4 material = float4_1;
    float smoothmink = 1.0f;

    virtual ~ImplicitFunction() {}

    float3 Sized(const float3 &unit_size) const { return unit_size * size + smoothmink; }
    virtual float3 Size() { return Sized(float3_1); };
    virtual void FillGrid(const int3 &, const int3 &, DistGrid *,
                          const float3 &, const float3 &,
                          const float3x3 &, ThreadPool &) const {};
};

static int3 axesi[] = { int3(1, 0, 0), int3(0, 1, 0), int3(0, 0, 1) };

float max_smoothmink = 0;

static const float grid_epsilon = 0.01f;

// use curiously recurring template pattern to allow implicit function to be inlined in
// rasterization loop
template<typename T> struct ImplicitFunctionImpl : ImplicitFunction {
    void FillGrid(const int3 &start, const int3 &end, DistGrid *distgrid,
                  const float3 &gridscale, const float3 &gridtrans,
                  const float3x3 &gridrot, ThreadPool &threadpool) const override {
        assert(end <= distgrid->dim && int3(0) <= start);
        auto uniform_scale = average(size);
        max_smoothmink = max(max_smoothmink, uniform_scale * smoothmink);
        vector<future<void>> results(end.x - start.x);
        for (int x = start.x; x < end.x; x++) {
            results[x - start.x] = threadpool.enqueue([&, x]() {
                for (int y = start.y; y < end.y; y++) {
                    for (int z = start.z; z < end.z; z++) {
                        int3 ipos(x, y, z);
                        auto pos = float3(ipos);
                        pos -= gridtrans;
                        pos = pos * gridrot;
                        pos /= gridscale;
                        auto dist = static_cast<const T *>(this)->Eval(pos);
                        // dist was evaluated in the local coordinate system of the primitive.
                        // This is correct for trans/rot, but the scale makes it give the wrong
                        // distance globally.
                        // Most uses of mg_scale(vec) are uniform so this should be close enough:
                        dist *= uniform_scale;
                        // This is our only state access, but is thread-safe:
                        auto &dv = distgrid->Get(ipos);
                        // Could move this if outside loop, but should be branch predicted, so
                        // probably ok.
                        if (material.w >= 0.5f) {
                            auto h = smoothminh(dv.dist, dist, smoothmink);
                            dv.dist = smoothmix(dv.dist, dist, smoothmink, h);
                            dv.color = quantizec(dv.color.w
                                                 ? mix(material, color2vec(dv.color), h)
                                                 : material);
                        } else {
                            dv.dist = smoothmax(-dist, dv.dist, smoothmink);
                        }
                    }
                }
            });
        }
        for (auto &r : results) r.get();
    }
};

struct IFSphere : ImplicitFunctionImpl<IFSphere> {
    float rad;

    inline float Eval(const float3 &pos) const {
        return length(pos) - rad;
    }

    float3 Size() override { return Sized(float3(rad)); }
};

struct IFCube : ImplicitFunctionImpl<IFCube> {
    float3 extents;

    inline float Eval(const float3 &pos) const {
        auto d = abs(pos) - extents;
        //return max(d);
        return length(max(d, float3_0)) + max(min(d, float3_0));
    }

    float3 Size() override { return Sized(extents); }
};

struct IFCylinder : ImplicitFunctionImpl<IFCylinder> {
    float radius, height;

    inline float Eval(const float3 &pos) const {
        return max(length(pos.xy()) - radius, abs(pos.z) - height);
    }

    float3 Size() override { return Sized(float3(radius, radius, height)); }
};

struct IFTaperedCylinder : ImplicitFunctionImpl<IFTaperedCylinder> {
    float bot, top, height;

    inline float Eval(const float3 &pos) const {
        auto xy = pos.xy();
        auto r = mix(bot, top, pos.z / (height * 2) + 0.5f);
        // FIXME: this is probably not the correct distance.
        return max(abs(pos.z) - height, dot(xy, xy) - r * r);
    }

    float3 Size() override {
        auto rad = max(top, bot);
        return Sized(float3(rad, rad, height));
    }
};

// TODO: pow is rather slow...
struct IFSuperQuadric : ImplicitFunctionImpl<IFSuperQuadric> {
    float3 exp;
    float3 scale;

    inline float Eval(const float3 &pos) const {
        return dot(pow(abs(pos) / scale, exp), float3_1) - 1;
    }

    float3 Size() override { return Sized(scale); }
};

struct IFSuperQuadricNonUniform : ImplicitFunctionImpl<IFSuperQuadricNonUniform> {
    float3 exppos, expneg;
    float3 scalepos, scaleneg;

    inline float Eval(const float3 &pos) const {
        auto d = pos.iflt(0, scaleneg, scalepos);
        auto e = pos.iflt(0, expneg, exppos);
        auto p = abs(pos) / d;
        // FIXME: why is max(p) even needed? not needed in IFSuperQuadric
        return max(max(p), dot(pow(p, e), float3_1)) - 1;
    }

    float3 Size() override { return Sized(max(scalepos, scaleneg)); }
};

struct IFSuperToroid : ImplicitFunctionImpl<IFSuperToroid> {
    float r;
    float3 exp;

    inline float Eval(const float3 &pos) const {
        auto p = pow(abs(pos), exp);
        auto xy = r - sqrtf(p.x + p.y);
        return powf(fabsf(xy), exp.z) + p.z - 1;
    }

    float3 Size() override { return Sized(float3(r * 2 + 1, r * 2 + 1, 1)); }
};

struct IFLandscape : ImplicitFunctionImpl<IFLandscape> {
    float zscale, xyscale;

    inline float Eval(const float3 &pos) const {
        if (!(abs(pos) <= 1)) return false;
        auto dpos = pos + float3(SimplexNoise(8, 0.5f, 1, float4(pos.xy() + 1, 0)),
                                 SimplexNoise(8, 0.5f, 1, float4(pos.xy() + 2, 0)),
                                 0) / 2;
        auto f = SimplexNoise(8, 0.5f, xyscale, float4(dpos.xy(), 0)) * zscale;
        // FIXME: this is obviously not the correct distance for anything but peaks.
        return dpos.z - f;
    }
};

struct Group : ImplicitFunctionImpl<Group> {
    vector<ImplicitFunction *> children;

    ~Group() {
        for (auto c : children) delete c;
    }

    static inline float Eval(const float3 & /*pos*/) { return 0.0f; }

    float3 Size() override {
        float3 p1, p2;
        for (auto c : children) {
            auto csz = c->Size();
            csz = rotated_size(c->rot, csz);
            p1 = c == children[0] ? c->orig - csz : min(p1, c->orig - csz);
            p2 = c == children[0] ? c->orig + csz : max(p2, c->orig + csz);
        }
        auto sz = (p2 - p1) / 2;
        orig = p1 + sz;
        return sz;
    }

    void FillGrid(const int3 & /*start*/, const int3 & /*end*/, DistGrid *distgrid,
                  const float3 &gridscale, const float3 &gridtrans,
                  const float3x3 & /*gridrot*/,
                  ThreadPool &threadpool) const override {
        for (auto c : children) {
            auto csize = c->Size();
            if (dot(csize, gridscale) > 3) {
                auto trans = gridtrans + c->orig * gridscale;
                auto scale = gridscale * c->size;
                auto rsize = rotated_size(c->rot, csize);
                auto start = int3(trans - rsize * gridscale - grid_epsilon);
                auto end   = int3(trans + rsize * gridscale + grid_epsilon + 2.0f);
                auto bs    = end - start;
                if (bs > 1) c->FillGrid(start, end, distgrid, scale, trans, c->rot, threadpool);
            }
        }
    }
};

float noisestretch = 1;
float noiseintensity = 0;
float randomizeverts = 0;

bool pointmode = false;

float3 id_grid_to_world(const int3 &pos) { return float3(pos); }

Mesh *polygonize_mc(const int3 &gridsize, float gridscale, const float3 &gridtrans,
                    const DistGrid *distgrid, float3 (* grid_to_world)(const int3 &pos)) {
    struct edge {
        int3 iclosest;
        float3 fmid;
        byte4 material;

        edge() : iclosest(int3_0), fmid(float3_0), material(byte4_0) {}
        edge(const int3 &_iclosest, const float3 &_fmid, const byte4 &_m)
            : iclosest(_iclosest), fmid(_fmid), material(_m) {}
    };
    vector<int> mctriangles;
    vector<edge> edges;
    bool mesh_displacent = true;
    bool flat_triangles_opt = true;
    bool simple_occlusion = false;
    bool marching_cubes = true;
    bool snap_to_mid = false;
    if (snap_to_mid) mesh_displacent = false;
    auto verts2edge = [&](const int3 &p1, const int3 &p2, DistVert &dv1, DistVert &dv2) {
        auto wp1 = grid_to_world(p1);
        auto wp2 = grid_to_world(p2);
        float3 mid;
        int3 iclosest;
        if (snap_to_mid) {
            // FIXME: this create null-area triangles that should be removed.
            mid = (wp1 + wp2) / 2;
            iclosest = p1;
        } else {
            if (abs(dv1.dist) < 0.00001f || abs(dv2.dist - dv1.dist) < 0.00001f) {
                mid = wp1;
                iclosest = p1;
            } else if (abs(dv2.dist) < 0.00001f) {
                mid = wp2;
                iclosest = p2;
            } else {
                auto mu = -dv1.dist / (dv2.dist - dv1.dist);
                mid = wp1 + mu * (wp2 - wp1);
                iclosest = abs(mu) < 0.5 ? p1 : p2;
            }
        }
        return edge(iclosest, mid, dv1.dist < dv2.dist ? dv1.color : dv2.color);
    };
    if (marching_cubes) {
        int3 gridpos[8];
        DistVert dv[8];
        int vertlist[12];
        const int3 corners[8] = {
            int3(0, 0, 0),
            int3(1, 0, 0),
            int3(1, 1, 0),
            int3(0, 1, 0),
            int3(0, 0, 1),
            int3(1, 0, 1),
            int3(1, 1, 1),
            int3(0, 1, 1),
        };
        // FIXME: this uses even more memory than the distgrid.
        EdgeGrid edgeidx(gridsize, int3(-1));
        for (int x = 0; x < gridsize.x - 1; x++)
            for (int y = 0; y < gridsize.y - 1; y++)
                for (int z = 0; z < gridsize.z - 1; z++) {
            int3 pos(x, y, z);
            int ci = 0;
            for (int i = 0; i < 8; i++) {
                gridpos[i] = pos + corners[i];
                dv[i] = distgrid->Get(gridpos[i]);
                ci |= (dv[i].dist < 0) << i;
            }
            if (mc_edge_table[ci] == 0) continue;
            for (int i = 0; i < 12; i++) if (mc_edge_table[ci] & (1 << i)) {
                int i1 = mc_edge_to_vert[i][0];
                int i2 = mc_edge_to_vert[i][1];
                auto &p1 = gridpos[i1];
                auto &p2 = gridpos[i2];
                int dir = p1.x < p2.x ? 0 : p1.y < p2.y ? 1 : 2;
                auto &ei = edgeidx.Get(p2);
                if (ei[dir]  < 0) {
                    ei[dir] = (int)edges.size();
                    auto &dv1 = dv[i1];
                    auto &dv2 = dv[i2];
                    #ifndef __EMSCRIPTEN__  // FIXME
                        assert(dv1.dist * dv2.dist <= 0);
                    #endif
                    edges.push_back(verts2edge(p1, p2, dv1, dv2));
                }
                vertlist[i] = ei[dir];
            }
            for (int i = 0; mc_tri_table[ci][i] != -1; ) {
                auto e1 = vertlist[mc_tri_table[ci][i++]];
                auto e2 = vertlist[mc_tri_table[ci][i++]];
                auto e3 = vertlist[mc_tri_table[ci][i++]];
                mctriangles.push_back(e1);
                mctriangles.push_back(e2);
                mctriangles.push_back(e3);
                auto area = triangle_area(edges[e1].fmid, edges[e2].fmid, edges[e3].fmid);
                //assert(area < 1);
                (void)area;
            }
        }
    } else {
        // Experimental marching squares slices mode, unfinished and unoptimized.
        mesh_displacent = false;
        flat_triangles_opt = false;
        polyreductionpasses = 0;
        int3 gridpos[3][4];
        DistVert dv[3][4];
        edge edgev[4];
        int3 corners[4] = {
            int3(0, 0, 0),
            int3(1, 0, 0),
            int3(1, 1, 0),
            int3(0, 1, 0),
        };
        int linelist[16][5] = {
            { -1 }, { 0, 3, -1 }, { 1, 0, -1 }, { 1, 3, -1 }, { 2, 1, -1 }, { 0, 3, 2, 1, -1 },
            { 2, 0, -1 }, { 2, 3, -1 }, { 3, 2, -1 }, { 0, 2, -1 }, { 1, 0, 3, 2, -1 },
            { 1, 2, -1 }, { 3, 1, -1 }, { 0, 1, -1 }, { 3, 0, -1 }, { -1 },
        };
        // Both ambiguous cases use the minimal version since that's less polygons overal.
        int trilist[16][10] = {
            { -1 }, { 0, 7, 1, -1 }, { 1, 3, 2, -1 }, { 0, 3, 2, 3, 0, 7, -1 }, { 3, 5, 4, -1 },
            { 3, 5, 4, 0, 7, 1, -1 }, { 1, 4, 2, 4, 1, 5, -1 }, { 0, 7, 2, 7, 5, 2, 5, 4, 2, -1 },
            { 5, 7, 6, -1 }, { 0, 5, 1, 5, 0, 6, -1 }, { 1, 3, 2, 5, 7, 6, -1 },
            { 0, 6, 5, 0, 5, 3, 0, 3, 2, -1 }, { 7, 4, 3, 4, 7, 6, -1 },
            { 6, 4, 3, 6, 3, 1, 6, 1, 0, -1 }, { 4, 2, 1, 4, 1, 7, 4, 7, 6, -1 },
            { 0, 4, 2, 4, 0, 6, -1 },
        };
        float3 zup(0, 0,  0.5f);
        float3 zdn(0, 0, -0.5f);
        for (int z = 1; z < gridsize.z - 1; z++)
            for (int x = 0; x < gridsize.x - 1; x++)
                for (int y = 0; y < gridsize.y - 1; y++) {
            int3 pos(x, y, z);
            int ci[3] = { 0, 0, 0 };
            for (int lz = 0; lz < 3; lz++) {
                for (int i = 0; i < 4; i++) {
                    gridpos[lz][i] = pos + corners[i] + int3(0, 0, lz - 1);
                    dv[lz][i] = distgrid->Get(gridpos[lz][i]);
                    ci[lz] |= (dv[lz][i].dist < 0) << i;
                }
            }
            if (linelist[ci[1]][0] < 0 && ci[1] == ci[0] && ci[1] == ci[2]) continue;
            for (int i = 0; i < 4; i++) {
                auto &p1 = gridpos[1][i];
                auto &p2 = gridpos[1][(i + 1) & 3];
                auto &dv1 = dv[1][i];
                auto &dv2 = dv[1][(i + 1) & 3];
                edgev[i] = verts2edge(p1, p2, dv1, dv2);
            }
            // Side polys.
            for (int i = 0; linelist[ci[1]][i] >= 0; i += 2) {
                // FIXME: disambiguate saddles?
                // FIXME: duplicate verts. reuse.
                for (int o = 0; o < 6; o++) {
                    auto a = linelist[ci[1]][i + (o > 0 && o < 4)];
                    mctriangles.push_back((int)edges.size());
                    auto e = edgev[a];
                    e.fmid += o > 1 && o < 5 ? zdn : zup;
                    edges.push_back(e);
                }
            }
            // Top polys.
            if (ci[1] != ci[2] || linelist[ci[1]][0] >= 0)
            {
                // FIXME: clip against adjacent level.
                for (int i = 0; trilist[ci[1]][i] >= 0; i += 3) {
                    for (int o = 0; o < 3; o++) {
                        auto a = trilist[ci[1]][i + o];
                        // FIXME: duplicate verts.
                        mctriangles.push_back((int)edges.size());
                        auto e = edgev[a / 2];
                        if (!(a & 1)) e.fmid = float3(gridpos[1][a / 2]);
                        else { assert(length(e.fmid - float3(gridpos[1][a / 2])) < 2); }
                        e.fmid += zup;
                        edges.push_back(e);
                    }
                }
            }
            // FIXME: bottom polys missing.
        }
    }
    delete distgrid;
    vector<int> triangles;
    vector<mgvert> verts;
    RandomNumberGenerator<Xoshiro256SS> r;
    /////////// MESH DISPLACEMENT
    if (mesh_displacent) {
        // "Mesh Displacement: An Improved Contouring Method for Trivariate Data"
        // http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.49.5214
        // reduces tris by half, and makes for more regular proportioned tris
        // from: http://chrishecker.com/My_liner_notes_for_spore
        struct dcell {
            float3 accum;
            float3 col;
            int n;
            dcell() : accum(float3_0), col(float3_0), n(0) {}
        };
        auto dcellindices = new IntGrid(gridsize, -1);
        vector<int3> iverts;
        vector<dcell> cells;
        for (edge &e : edges) {
            int &idx = dcellindices->Get(e.iclosest);
            if (idx < 0) {
                idx = (int)cells.size();
                cells.push_back(dcell());
            }
            dcell &c = cells[idx];
            c.accum += e.fmid;
            c.col += color2vec(e.material).xyz();
            c.n--;
            iverts.push_back(e.iclosest);
        }
        for (size_t t = 0; t < mctriangles.size(); t += 3) {
            int3 i[3];
            i[0] = iverts[mctriangles[t + 0]];
            i[1] = iverts[mctriangles[t + 1]];
            i[2] = iverts[mctriangles[t + 2]];
            if (i[0] != i[1] && i[0] != i[2] && i[1] != i[2]) {
                for (int j = 0; j < 3; j++) {
                    dcell &c = cells[dcellindices->Get(i[j])];
                    if (c.n < 0) {
                        c.accum /= (float)-c.n;
                        c.col /= (float)-c.n;
                        c.n = (int)verts.size();
                        verts.push_back(mgvert());
                        auto &v = verts.back();
                        v.pos = c.accum;
                        v.norm = float3_0;
                        v.col = quantizec(c.col, 1);
                    }
                    triangles.push_back(c.n);
                }
            }
        }
        delete dcellindices;
    } else {
        for (edge &e : edges) {
            verts.push_back(mgvert());
            auto &v = verts.back();
            assert(e.fmid >= 0 && e.fmid <= float3(gridsize));
            v.pos = e.fmid;
            v.col = e.material;
            v.norm = float3_0;
        }
        triangles.assign(mctriangles.begin(), mctriangles.end());
    }
    /////////// CULL FLAT TRIANGLES
    // TODO: can bad tris simply be culled based on bad normals?
    if (flat_triangles_opt) {
        bool *problemvert = new bool[verts.size()];
        memset(problemvert, 0, sizeof(bool) * verts.size());
        const float threshold = -0.98f;
        for (size_t t = 0; t < triangles.size(); t += 3) {
            auto &v1 = verts[triangles[t + 0]];
            auto &v2 = verts[triangles[t + 1]];
            auto &v3 = verts[triangles[t + 2]];
            assert(v1.pos != v2.pos && v1.pos != v3.pos && v2.pos != v3.pos);
            float3 d3  = normalize(cross(v3.pos - v1.pos, v2.pos - v1.pos));
            // if a plane normal points away near 180 from past normals, its likely part of triangle with no volume
            // behind it (special case in mesh displacement)
            if (v1.norm != float3_0 && dot(d3, normalize(v1.norm)) < threshold)
                problemvert[triangles[t + 0]] = true;
            if (v2.norm != float3_0 && dot(d3, normalize(v2.norm)) < threshold)
                problemvert[triangles[t + 1]] = true;
            if (v3.norm != float3_0 && dot(d3, normalize(v3.norm)) < threshold)
                problemvert[triangles[t + 2]] = true;
        }
        for (size_t t = 0; t < triangles.size(); t += 3) {
            // if all corners have screwey normals, the triangle should be culled
            // could also cull associated verts, but generally so few of them its not worth it
            if ((problemvert[triangles[t + 0]] &&
                problemvert[triangles[t + 1]] &&
                problemvert[triangles[t + 2]])) {
                triangles.erase(triangles.begin() + t, triangles.begin() + t + 3);
                t -= 3;
            }
        }
        delete[] problemvert;
    }
    /////////// RANDOMIZE
    if (randomizeverts > 0) {
        for (auto &v : verts) {
            v.pos += random_point_in_sphere(r) * randomizeverts;
        }
    }
    /////////// ORIGIN/SCALE
    for (auto &v : verts) {
        v.pos -= gridtrans;
        v.pos /= gridscale;
    }
    /////////// CALCULATE NORMALS
    RecomputeNormals(triangles, verts);
    /////////// POLYGON REDUCTION
    if (polyreductionpasses) {
        PolyReduce(triangles, verts);
    }
    /////////// APPLY NOISE TO COLOR
    if (noiseintensity > 0) {
        float scale = noisestretch;
        int octaves = 8;
        float persistence = 0.5f;

        for (auto &v : verts) {
            auto n = float3(SimplexNoise(octaves, persistence, scale, float4(v.pos, 0.0f / scale)),
                            SimplexNoise(octaves, persistence, scale, float4(v.pos, 0.3f / scale)),
                            SimplexNoise(octaves, persistence, scale, float4(v.pos, 0.6f / scale)));
            v.col = quantizec(color2vec(v.col).xyz() *
                              (float3_1 - (n + float3_1) / 2 * noiseintensity), 1);
        }
    }
    /////////// MODULATE LIGHTING BY CREASE FACTOR
    if (simple_occlusion) {
        float *cfactor = new float[verts.size()];
        memset(cfactor, 0, sizeof(float) * verts.size());
        for (size_t t = 0; t < triangles.size(); t += 3) {
            auto &v1 = verts[triangles[t + 0]];
            auto &v2 = verts[triangles[t + 1]];
            auto &v3 = verts[triangles[t + 2]];
            float3 v12 = normalize(v2.pos - v1.pos);
            float3 v13 = normalize(v3.pos - v1.pos);
            float3 v23 = normalize(v3.pos - v2.pos);
            auto centroid = (v1.pos + v2.pos + v3.pos) / 3;
            cfactor[triangles[t + 0]] +=
                dot(v1.norm, normalize(centroid - v1.pos)) * (1 - dot( v12, v13));
            cfactor[triangles[t + 1]] +=
                dot(v1.norm, normalize(centroid - v1.pos)) * (1 - dot(-v12, v23));
            cfactor[triangles[t + 2]] +=
                dot(v1.norm, normalize(centroid - v1.pos)) * (1 - dot(-v23,-v13));
        }
        for (auto &v : verts) {
            float f = cfactor[&v - &verts[0]] + 1;
            v.col = byte4(float4(min(float3(255),
                                     max(float3_0, float3(v.col.xyz()) - 64 * f)), 255));
        }
        delete[] cfactor;
    }
    LOG_DEBUG("meshgen verts = %lu, edgeverts = %lu, tris = %lu, mctris = %lu,"
           " scale = %f\n", verts.size(), edges.size(), triangles.size() / 3,
           mctriangles.size() / 3, gridscale);
    auto m =
        new Mesh(new Geometry("polygonize_mc_verts", gsl::make_span(verts), "PNC"),
                      pointmode ? PRIM_POINT : PRIM_TRIS);
    if (pointmode) {
        m->pointsize = 1000 / gridscale;
    } else {
        m->surfs.push_back(
            new Surface("polygonize_mc_idxs", gsl::make_span(triangles), PRIM_TRIS));
    }
    return m;
}

Group *root = nullptr;
Group *curgroup = nullptr;
ImplicitFunction cur;
vector<ImplicitFunction> fstack;

void MeshGenClear() {
    if (root) delete root;
    root = curgroup = nullptr;
    cur = ImplicitFunction();
    max_smoothmink = 0;
    fstack.clear();
}

Group *GetGroup() {
    if (!curgroup) {
        assert(!root);
        root = new Group();
        curgroup = root;
    }
    return curgroup;
}

Value AddShape(ImplicitFunction *f) {
    f->size = cur.size;
    f->orig = cur.orig;
    f->rot  = cur.rot;
    f->material = cur.material;
    f->smoothmink = cur.smoothmink;
    GetGroup()->children.push_back(f);
    return NilVal();
}

Value eval_and_polygonize(VM &vm, int targetgridsize, int zoffset, bool do_poly) {
    auto scenesize = root->Size() * 2;
    float biggestdim = max(scenesize.x, max(scenesize.y, scenesize.z));
    auto gridscale = targetgridsize / biggestdim;
    // Add a boundary of 1 cell in all directions, and additionally 10xepsilon to ensure
    // shapes always fit in the grid, even with some float imprecision.
    auto gridsize = int3(scenesize * gridscale + float3(grid_epsilon * 10.0f + 2.0f));
    auto gridtrans = (float3(gridsize) - 1) / 2 - root->orig * gridscale;
    auto distgrid = new DistGrid(gridsize, DistVert());
    ThreadPool threadpool((size_t)NumHWThreads());
    root->FillGrid(int3(0), gridsize, distgrid, float3(gridscale), gridtrans, float3x3_1,
        threadpool);
    if (do_poly) {
        auto mesh = polygonize_mc(gridsize, gridscale, gridtrans, distgrid, id_grid_to_world);
        MeshGenClear();
        extern ResourceType mesh_type;
        return Value(vm.NewResource(&mesh_type, mesh));
    } else {
        auto cg = CubesFromMeshGen(vm, *distgrid, targetgridsize, zoffset);
        MeshGenClear();
        delete distgrid;
        return cg;
    }
}

void AddMeshGen(NativeRegistry &nfr) {

nfr("mg_sphere", "radius", "F", "",
    "a sphere",
    [](StackPtr &, VM &, Value &rad) {
        auto s = new IFSphere();
        s->rad = rad.fltval();
        return AddShape(s);
    });

nfr("mg_cube", "extents", "F}:3", "",
    "a cube (extents are size from center)",
    [](StackPtr &sp, VM &) {
        auto c = new IFCube();
        c->extents = PopVec<float3>(sp);
        Push(sp,  AddShape(c));
    });

nfr("mg_cylinder", "radius,height", "FF", "",
    "a unit cylinder (height is from center)",
    [](StackPtr &, VM &, Value &radius, Value &height) {
        auto c = new IFCylinder();
        c->radius = radius.fltval();
        c->height = height.fltval();
        return AddShape(c);
    });

nfr("mg_tapered_cylinder", "bot,top,height", "FFF", "",
    "a cyclinder where you specify the top and bottom radius (height is from center)",
    [](StackPtr &, VM &, Value &bot, Value &top, Value &height) {
        auto tc = new IFTaperedCylinder();
        tc->bot = bot.fltval();
        tc->top = top.fltval();
        tc->height = height.fltval();
        return AddShape(tc);
    });

nfr("mg_superquadric", "exponents,scale", "F}:3F}:3", "",
    "a super quadric. specify an exponent of 2 for spherical, higher values for rounded"
    " squares",
    [](StackPtr &sp, VM &) {
        auto sq = new IFSuperQuadric();
        sq->scale = PopVec<float3>(sp);
        sq->exp = PopVec<float3>(sp);
        AddShape(sq);
    });

nfr("mg_superquadric_non_uniform", "posexponents,negexponents,posscale,negscale", "F}:3F}:3F}:3F}:3", "",
    "a superquadric that allows you to specify exponents and sizes in all 6 directions"
    " independently for maximum modelling possibilities",
    [](StackPtr &sp, VM &) {
        auto sq = new IFSuperQuadricNonUniform();
        sq->scaleneg = max(float3(0.01f), PopVec<float3>(sp));
        sq->scalepos = max(float3(0.01f), PopVec<float3>(sp));
        sq->expneg   = PopVec<float3>(sp);
        sq->exppos   = PopVec<float3>(sp);
        AddShape(sq);
    });

nfr("mg_supertoroid", "R,exponents", "FF}:3", "",
    "a super toroid. R is the distance from the origin to the center of the ring.",
    [](StackPtr &sp, VM &) {
        auto t = new IFSuperToroid();
        t->exp = PopVec<float3>(sp);
        t->r = Pop(sp).fltval();
        AddShape(t);
    });

nfr("mg_landscape", "zscale,xyscale", "FF", "",
    "a simplex landscape of unit size",
    [](StackPtr &, VM &, Value &zscale, Value &xyscale) {
        auto ls = new IFLandscape();
        ls->zscale = zscale.fltval();
        ls->xyscale = xyscale.fltval();
        return AddShape(ls);
    });

nfr("mg_set_polygon_reduction", "polyreductionpasses,epsilon,maxtricornerdot", "IFF", "",
    "controls the polygon reduction algorithm. set polyreductionpasses to 0 for off, 100 for"
    " max compression, or low values for generation speed or to keep the mesh uniform. epsilon"
    " determines how flat adjacent triangles must be to be reduced, use 0.98 as a good"
    " tradeoff, lower to get more compression. maxtricornerdot avoid very thin triangles, use"
    " 0.95 as a good tradeoff, up to 0.99 to get more compression",
    [](StackPtr &, VM &, Value &_polyreductionpasses, Value &_epsilon,
                                        Value &_maxtricornerdot) {
        polyreductionpasses = _polyreductionpasses.intval();
        epsilon = _epsilon.fltval();
        maxtricornerdot = _maxtricornerdot.fltval();
        return NilVal();
    });

nfr("mg_set_color_noise", "noiseintensity,noisestretch", "FF", "",
    "applies simplex noise to the colors of the model. try 0.3 for intensity."
    " stretch scales the pattern over the model",
    [](StackPtr &, VM &, Value &_noiseintensity, Value &_noisestretch) {
        noisestretch = _noisestretch.fltval();
        noiseintensity = _noiseintensity.fltval();
        return NilVal();
    });

nfr("mg_set_vertex_randomize", "factor", "F", "",
    "randomizes all verts produced to give a more organic look and to hide the inherent messy"
    " polygons produced by the algorithm. try 0.15. note that any setting other than 0 will"
    " likely counteract the polygon reduction algorithm",
    [](StackPtr &, VM &, Value &factor) {
        randomizeverts = factor.fltval();
        return NilVal();
    });

nfr("mg_set_point_mode", "on", "B", "",
    "generates a point mesh instead of polygons",
    [](StackPtr &, VM &, Value &aspoints) {
        pointmode = aspoints.True();
        return NilVal();
    });

nfr("mg_polygonize", "subdiv", "I", "R:mesh",
    "returns a generated mesh from past mg_ commands."
    " subdiv determines detail and number of polygons (relative to the largest dimension of the"
    " model), try 30.. 300 depending on the subject."
    " values much higher than that will likely make you run out of memory (or take very long).",
    [](StackPtr &, VM &vm, Value &subdiv) {
        return eval_and_polygonize(vm, subdiv.intval(), 0, true);
    });

nfr("mg_convert_to_cubes", "subdiv,zoffset", "II", "R:voxels",
    "returns a cubegen block (see cg_ functions) from past mg_ commands."
    " subdiv determines detail and number of cubes (relative to the largest dimension of the"
    " model).",
    [](StackPtr &, VM &vm, Value &subdiv, Value &zoffset) {
        return eval_and_polygonize(vm, subdiv.intval(), zoffset.intval(), false);
    });

nfr("mg_translate", "vec", "F}:3", "",
    "translates the current coordinate system along a vector",
    [](StackPtr &sp, VM &) {
        auto v = PopVec<float3>(sp);
        // FIXME: not good enough if non-uniform scale, might as well forbid that before any trans
        cur.orig += cur.rot * (v * cur.size);
    });

nfr("mg_scale", "f", "F", "",
    "scales the current coordinate system by the given factor",
    [](StackPtr &sp, VM &) {
        auto f = Pop(sp).fltval();
        cur.size *= f;
    });

nfr("mg_scale_vec", "vec", "F}:3", "",
    "non-unimformly scales the current coordinate system using individual factors per axis",
    [](StackPtr &sp, VM &) {
        auto v = PopVec<float3>(sp);
        cur.size *= v;
    });

nfr("mg_rotate", "axis,angle", "F}:3F", "",
    "rotates using axis/angle",
    [](StackPtr &sp, VM &) {
        auto angle = Pop(sp).fltval();
        auto axis = PopVec<float3>(sp);
        cur.rot *= float3x3(angle * RAD, axis);
    });

nfr("mg_color", "color", "F}:4", "",
    "sets the color, where an alpha of 1 means to add shapes to the scene (union), and 0"
    " substracts them (carves)",
    [](StackPtr &sp, VM &) {
        auto v = PopVec<float4>(sp);
        cur.material = v;
    });

nfr("mg_smooth", "smooth", "F", "",
    "sets the smoothness in terms of the range of distance from the shape smoothing happens,"
    " defaults to 1.0",
    [](StackPtr &sp, VM &) {
        auto smooth = Pop(sp).fltval();
        cur.smoothmink = smooth;
    });

nfr("mg_push_transform", "", "", "",
    "save the current state of the transform",
    [](StackPtr &, VM &) {
        fstack.push_back(cur);
    });

nfr("mg_pop_transform", "", "", "",
    "restore a previous state of the transform",
    [](StackPtr &, VM &) {
        if (!fstack.empty()) {
            cur = fstack.back();
            fstack.pop_back();
        }
    });

}  // AddMeshGen
