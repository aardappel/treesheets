
int polyreductionpasses = 0;
float epsilon = 0.98f;
float maxtricornerdot = 0.95f;

inline void PolyReduce(vector<int> &triangles, vector<mgvert> &verts) {
    // FIXME: factor out holding triangle data (face normal etc) in arrays
    int *vertmap = new int[verts.size()];
    for (int prp = 0; prp < polyreductionpasses; prp++) {
        memset(vertmap, -1, verts.size() * sizeof(int));
        for (size_t t = 0; t < triangles.size(); t += 3) {
            auto &v1 = verts[triangles[t + 0]];
            auto &v2 = verts[triangles[t + 1]];
            auto &v3 = verts[triangles[t + 2]];
            float3 v12u = v2.pos - v1.pos;
            float3 v13u = v3.pos - v1.pos;
            float3 d3  = normalize(cross(v13u, v12u));
            for (int i = 0; i < 3; i++) {
                if (dot(verts[triangles[t + i]].norm, d3) < epsilon)
                    vertmap[triangles[t + i]] = -2;     // not available for reduction
            }
        }
        for (size_t t = 0; t < triangles.size(); t += 3) {
            auto &v1 = verts[triangles[t + 0]];
            auto &v2 = verts[triangles[t + 1]];
            auto &v3 = verts[triangles[t + 2]];
            float3 v12u = v2.pos - v1.pos;
            float3 v13u = v3.pos - v1.pos;
            int i1 = -1, i2 = -1, i3 = -1;
            for (int i = 0; i < 3; i++) {
                if (vertmap[triangles[t + i]] == -1) {
                    if (i2 < 0) {
                        if (i1 >= 0) i2 = i;
                        else i1 = i;
                    }
                } else i3 = i;
            }
            if (i3 < 0) {  // all 3 flat, pick the shortest edge
                auto l12 = length(v12u);
                auto l13 = length(v13u);
                auto l23 = length(v2.pos - v3.pos);
                if (l13 < l12 && l13 < l23) {  // pick 13
                    i2 = 2;
                    i3 = 1;
                } else if (l23 < l12 && l23 < l13) {  // pick 23
                    i1 = 1;
                    i2 = 2;
                    i3 = 0;
                } else {  // pick 12
                    i3 = 2;
                }
            }
            if (i1 >= 0 && i2 >= 0
                && vertmap[triangles[t + i3]] < 0  // why is this so important???
            ) {
                int vi = triangles[t + i1];
                int ovi = triangles[t + i2];
                vertmap[vi] = ovi;
                vertmap[ovi] = vi;
            }
        }
        int flipped = 0;
        for (size_t t = 0; t < triangles.size(); t += 3) {
            for (int i = 0; i < 3; i++) {
                int vi1 = triangles[t + i];
                int vi2 = triangles[t + (i + 1) % 3];
                int vi3 = triangles[t + (i + 2) % 3];
                if (vertmap[vi1] >= 0 && vertmap[vi1] != vi2 && vertmap[vi1] != vi3) {
                    auto &v1 = verts[vi1];
                    auto &v2 = verts[vi2];
                    auto &v3 = verts[vi3];
                    float3 d3  = normalize(cross(v3.pos - v1.pos, v2.pos - v1.pos));
                    float3 vm = (verts[vertmap[vi1]].pos + v1.pos) / 2;
                    float3 v12m = normalize(v2.pos - vm);
                    float3 v13m = normalize(v3.pos - vm);
                    float3 v23m = normalize(v3.pos - v2.pos);
                    float3 d3m  = normalize(cross(v13m, v12m));
                    if (dot(d3, d3m) < epsilon ||
                        dot(v12m, v13m) > maxtricornerdot ||
                        dot(-v12m, v23m) > maxtricornerdot ||
                        dot(-v23m, -v13m) > maxtricornerdot) {
                        vertmap[vertmap[vi1]] = -1;
                        vertmap[vi1] = -1;
                        flipped++;
                    }
                }
            }
        }
        //Output(OUTPUT_DEBUG, "flipped tris: ", flipped);
        for (size_t t = 0; t < triangles.size(); t += 3) {
            int keep = -1;
            for (int i = 0; i < 3; i++) {
                int vi = triangles[t + i];
                if (vertmap[vi] >= 0) {
                    if (keep >= 0) {
                        int kvi = triangles[t + keep];
                        if (vertmap[vi] != kvi) {
                            if (length(verts[ vi].pos - verts[vertmap[ vi]].pos) <
                                length(verts[kvi].pos - verts[vertmap[kvi]].pos)) {
                                vertmap[vertmap[kvi]] = -1;
                                vertmap[kvi] = -1;
                                keep = i;
                            } else {
                                vertmap[vertmap[vi]] = -1;
                                vertmap[vi] = -1;
                            }
                        }
                    } else keep = i;
                }
            }
        }
        size_t writep = 0;
        for (size_t t = 0; t < triangles.size(); t += 3) {
            for (int i = 0; i < 3; i++) {
                int target = vertmap[triangles[t + i]];
                if (target >= 0) {
                    if (triangles[t + (i + 1) % 3] == target ||
                        triangles[t + (i + 2) % 3] == target) {
                        writep -= i;
                        break;
                    }
                }
                triangles[writep++] = triangles[t + i];
            }
        }
        auto polysreduced = (triangles.size() - writep) / 3;
        //Output(OUTPUT_DEBUG, "reduced tris: ", polysreduced);
        triangles.erase(triangles.begin() + writep, triangles.end());
        for (size_t t = 0; t < triangles.size(); t++) {
            if (vertmap[triangles[t]] >= 0 && vertmap[triangles[t]] < triangles[t])
                triangles[t] = vertmap[triangles[t]];
        }
        for (size_t i = 0; i < verts.size(); i++) if (vertmap[i] >= 0 && vertmap[i] < (int)i) {
            auto &v1 = verts[i];
            auto &v2 = verts[vertmap[i]];

            v2.pos = (v1.pos + v2.pos) / 2;
            v2.col = byte4((int4(v1.col) + int4(v2.col)) / 2);
        }
        RecomputeNormals(triangles, verts);
        if (polysreduced < 100)
            break;
    }
    // TODO: this also deletes verts from the bad triangle finder, but only if tri reduction is on
    memset(vertmap, -1, verts.size() * sizeof(int));
    for (size_t t = 0; t < triangles.size(); t++) vertmap[triangles[t]]++;
    int ni = 0;
    for (size_t i = 0; i < verts.size(); i++) {
        if (vertmap[i] >= 0) {
            verts[ni] = verts[i];
            vertmap[i] = ni++;
        }
    }
    verts.erase(verts.begin() + ni, verts.end());
    for (size_t t = 0; t < triangles.size(); t++) triangles[t] = vertmap[triangles[t]];
    delete[] vertmap;
}