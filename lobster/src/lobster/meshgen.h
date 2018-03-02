
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
    normalize_mesh(triangles.data(), triangles.size(), verts.data(), verts.size(), sizeof(mgvert),
                   (uchar *)&verts.data()->norm - (uchar *)&verts.data()->pos, false);
};

extern Mesh *polygonize_mc(const int3 &gridsize, float gridscale, const float3 &gridtrans,
                           const DistGrid *distgrid, float3 (* grid_to_world)(const int3 &pos));
