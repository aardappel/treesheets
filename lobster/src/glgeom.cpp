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
#include "lobster/glinterface.h"
#include "lobster/glincludes.h"

uint GenBO_(GLenum type, size_t bytesize, const void *data) {
    uint bo;
    GL_CALL(glGenBuffers(1, &bo));
    GL_CALL(glBindBuffer(type, bo));
    GL_CALL(glBufferData(type, bytesize, data, GL_STATIC_DRAW));
    return bo;
}

void DeleteBO(uint id) {
    GL_CALL(glDeleteBuffers(1, &id));
}

size_t AttribsSize(string_view fmt) {
    size_t size = 0;
    for (auto c : fmt) {
        switch (c) {
            case 'P': case 'N':           size += 12; break;
            case 'p': case 'n': case 'T': size +=  8; break;
            case 'C': case 'W': case 'I': size +=  4; break;
            default: assert(0);
        }
    }
    return size;
}

GLenum GetPrimitive(Primitive prim) {
    switch (prim) {
        default: assert(0);
        case PRIM_TRIS:  return GL_TRIANGLES;
        case PRIM_FAN:   return GL_TRIANGLE_FAN;
        case PRIM_LOOP:  return GL_LINE_LOOP;
        case PRIM_POINT: return GL_POINTS;
    }
}

Surface::Surface(span<int> indices, Primitive _prim) : numidx(indices.size()), prim(_prim) {
    ibo = GenBO(GL_ELEMENT_ARRAY_BUFFER, indices);
}

void Surface::Render(Shader *sh) {
    sh->SetTextures(textures);
    GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo));
    GL_CALL(glDrawElements(GetPrimitive(prim), (GLsizei)numidx, GL_UNSIGNED_INT, 0));
}

Surface::~Surface() {
    GL_CALL(glDeleteBuffers(1, &ibo));
}

void Geometry::Init(const void *verts1, const void *verts2) {
    vbo1 = GenBO_(GL_ARRAY_BUFFER, vertsize1 * nverts, verts1);
    if (verts2) vbo2 = GenBO_(GL_ARRAY_BUFFER, vertsize2 * nverts, verts2);
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo1));
    GL_CALL(glGenVertexArrays(1, &vao));
    GL_CALL(glBindVertexArray(vao));
    size_t offset = 0;
    size_t vs = vertsize1;
    for (auto attr : fmt) {
        switch (attr) {
            #define SETATTRIB(idx, comps, type, norm, size) \
                GL_CALL(glEnableVertexAttribArray(idx)); \
                GL_CALL(glVertexAttribPointer(idx, comps, type, norm, (GLsizei)vs, (void *)offset)); \
                offset += size; \
                break;
            case 'P': SETATTRIB(0, 3, GL_FLOAT,         false, 12)
            case 'p': SETATTRIB(0, 2, GL_FLOAT,         false,  8)
            case 'N': SETATTRIB(1, 3, GL_FLOAT,         false, 12)
            case 'n': SETATTRIB(1, 2, GL_FLOAT,         false,  8)
            case 'T': SETATTRIB(2, 2, GL_FLOAT,         false,  8)
            case 'C': SETATTRIB(3, 4, GL_UNSIGNED_BYTE, true,   4)
            case 'W': SETATTRIB(4, 4, GL_UNSIGNED_BYTE, true,   4)
            case 'I': SETATTRIB(5, 4, GL_UNSIGNED_BYTE, false,  4)
            default:
                LOG_ERROR("unknown attribute type: ", string() + attr);
                assert(false);
        }
        if (vbo2) {
            GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo2));
            vs = vertsize2;
            offset = 0;
        }
    }
    GL_CALL(glBindVertexArray(0));
}

void Geometry::RenderSetup() {
    GL_CALL(glBindVertexArray(vao));
}

Geometry::~Geometry() {
    GL_CALL(glDeleteBuffers(1, &vbo1));
    if (vbo2) GL_CALL(glDeleteBuffers(1, &vbo2));
    GL_CALL(glDeleteVertexArrays(1, &vao));
}

void Geometry::BindAsSSBO(Shader *sh, string_view name) {
    UniformBufferObject(sh, nullptr, 0, name, true, vbo1);
    assert(!vbo2);
}

void Mesh::Render(Shader *sh) {
    if (prim == PRIM_POINT) SetPointSprite(pointsize);
    sh->Set();
    if (numbones && numframes) {
        int frame1 = ffloor(curanim);
        int frame2 = frame1 + 1;
        float frameoffset = curanim - frame1;
        float3x4 *mat1 = &mats[(frame1 % numframes) * numbones],
                 *mat2 = &mats[(frame2 % numframes) * numbones];
        auto outframe = new float3x4[numbones];
        for(int i = 0; i < numbones; i++) outframe[i] = mix(mat1[i], mat2[i], frameoffset);
        sh->SetAnim(outframe, numbones);
        delete[] outframe;
    }
    geom->RenderSetup();
    if (surfs.size()) {
        for (auto s : surfs) s->Render(sh);
    } else {
        GL_CALL(glDrawArrays(GetPrimitive(prim), 0, (GLsizei)geom->nverts));
    }
}

Mesh::~Mesh() {
    delete geom;
    for (auto s : surfs) delete s;
    if (mats) delete[] mats;
}

bool Geometry::WritePLY(string &s, size_t nindices) {
    #ifndef PLATFORM_ES3
    s += cat("ply\n"
             "format binary_little_endian 1.0\n"
             "element vertex ", nverts, "\n");
    for (auto fc : fmt) {
        switch (fc) {
            case 'P': s += "property float x\nproperty float y\nproperty float z\n"; break;
            case 'p': s += "property float x\nproperty float y\n"; break;
            case 'N': s += "property float nx\nproperty float ny\nproperty float nz\n"; break;
            case 'n': s += "property float nx\nproperty float ny\n"; break;
            case 'T': s += "property float u\nproperty float v\n"; break;
            case 'C': s += "property uchar red\nproperty uchar green\n"
                           "property uchar blue\nproperty uchar alpha\n"; break;
            case 'W': s += "property uchar wa\nproperty uchar wb\n"
                           "property uchar wc\nproperty uchar wd\n"; break;
            case 'I': s += "property uchar ia\nproperty uchar ib\n"
                           "property uchar ic\nproperty uchar id\n"; break;
            default: assert(0);
        }
    }
    s += cat("element face ", nindices / 3, "\n"
             "property list int int vertex_index\n"
             "end_header\n");
    vector<uchar> vdata(nverts * vertsize1);
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo1));
    GL_CALL(glGetBufferSubData(GL_ARRAY_BUFFER, 0, vdata.size(), vdata.data()));
    s.insert(s.end(), vdata.begin(), vdata.end());
    return true;
    #else
    (void)s;
    (void)nindices;
	(void)vertsize1;
    return false;
    #endif
}

void Surface::WritePLY(string &s) {
    #ifndef PLATFORM_ES3
    vector<int> idata(numidx / 3 * 4);
    GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo));
    GL_CALL(glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, numidx * sizeof(int), idata.data()));
    for (int i = (int)numidx - 3; i >= 0; i -= 3) {
        auto di = i / 3 * 4;
        idata[di + 3] = idata[i + 1];
        idata[di + 2] = idata[i + 2];  // we cull GL_FRONT
        idata[di + 1] = idata[i + 0];
        idata[di] = 3;
    }
    s.insert(s.end(), (char *)idata.data(), ((char *)idata.data()) + idata.size() * sizeof(int));
    #else
    (void)s;
    #endif
}

bool Mesh::SaveAsPLY(string_view filename) {
    size_t nindices = 0;
    for (auto &surf : surfs) nindices += surf->numidx;
    string s;
    if (!geom->WritePLY(s, nindices)) return false;
    for (auto &surf : surfs) surf->WritePLY(s);
    return WriteFile(filename, true, s);
}

void SetPointSprite(float scale) {
    pointscale = scale * custompointscale;
    #ifdef PLATFORM_ES3
        // glEnable(GL_POINT_SPRITE_OES);
        // glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE);
    #else
        #ifndef __APPLE__
            // GL_CALL(glEnable(GL_POINT_SPRITE));
            // GL_CALL(glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE));
        #endif
        GL_CALL(glEnable(GL_VERTEX_PROGRAM_POINT_SIZE));
    #endif
}

void RenderArray(Primitive prim, Geometry *geom, uint ibo, size_t tcount) {
    GLenum glprim = GetPrimitive(prim);
    geom->RenderSetup();
    if (ibo) {
        GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo));
        GL_CALL(glDrawElements(glprim, (GLsizei)tcount, GL_UNSIGNED_INT, 0));
    } else {
        GL_CALL(glDrawArrays(glprim, 0, (GLsizei)geom->nverts));
    }
}

void GeometryCache::RenderUnitSquare(Shader *sh, Primitive prim, bool centered) {
    if (!quadgeom[centered]) {
        static SpriteVert vb_square[4] = {
            SpriteVert{ float2(0, 0), float2(0, 0) },
            SpriteVert{ float2(0, 1), float2(0, 1) },
            SpriteVert{ float2(1, 1), float2(1, 1) },
            SpriteVert{ float2(1, 0), float2(1, 0) },
        };
        static SpriteVert vb_square_centered[4] = {
            SpriteVert{ float2(-1, -1), float2(0, 0) },
            SpriteVert{ float2(-1,  1), float2(0, 1) },
            SpriteVert{ float2( 1,  1), float2(1, 1) },
            SpriteVert{ float2( 1, -1), float2(1, 0) },
        };
        quadgeom[centered] =
            new Geometry(make_span(centered ? vb_square_centered : vb_square, 4), "pT");
    }
    sh->Set();
    RenderArray(prim, quadgeom[centered]);
}

void GeometryCache::RenderQuad(Shader *sh, Primitive prim, bool centered, const float4x4 &trans) {
    Transform2D(trans, [&]() { RenderUnitSquare(sh, prim, centered); });
}

void GeometryCache::RenderLine2D(Shader *sh, Primitive prim, const float3 &v1, const float3 &v2,
                                 float thickness) {
    auto v = (v2 - v1) / 2;
    auto len = length(v);
    auto vnorm = v / len;
    auto trans = translation(v1 + v) *
                 rotationZ(vnorm.xy()) *
                 float4x4(float4(len, thickness / 2, 1, 1));
    RenderQuad(sh, prim, true, trans);
}

void GeometryCache::RenderLine3D(Shader *sh, const float3 &v1, const float3 &v2,
                                 const float3 &/*campos*/, float thickness) {
    GL_CALL(glDisable(GL_CULL_FACE));  // An exception in 3d mode.
    // FIXME: need to rotate the line also to make it face the camera.
    //auto camvec = normalize(campos - (v1 + v2) / 2);
    auto v = v2 - v1;
    auto vq = quatfromtwovectors(normalize(v), float3_x);
    //auto sq = quatfromtwovectors(camvec, float3_z);
    auto trans = translation((v1 + v2) / 2) *
                 float3x3to4x4(rotation(vq)) *  // FIXME: cheaper?
                 float4x4(float4(length(v) / 2, thickness, 1, 1));
    RenderQuad(sh, PRIM_FAN, true, trans);
    GL_CALL(glEnable(GL_CULL_FACE));
}

void GeometryCache::RenderUnitCube(Shader *sh, int inside) {
    struct cvert { float3 pos; float3 normal; float2 tc; };
    if (!cube_geom[inside]) {
        static float3 normals[] = {
            float3(1, 0, 0), float3(-1,  0,  0),
            float3(0, 1, 0), float3( 0, -1,  0),
            float3(0, 0, 1), float3( 0,  0, -1),
        };
        static float2 tcs[] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 1) };
        static const char *faces[6] = { "4576", "0231", "2673", "0154", "1375", "0462" };
        static int indices[2][6] = { { 0, 1, 3, 1, 2, 3 }, { 0, 3, 1, 1, 3, 2 } };
        vector<cvert> verts;
        vector<int> triangles;
        for (int n = 0; n < 6; n++) {
            auto face = faces[n];
            for (int i = 0; i < 6; i++) triangles.push_back(indices[inside][i] + (int)verts.size());
            for (int vn = 0; vn < 4; vn++) {
                cvert vert;
                for (int d = 0; d < 3; d++) {
                    vert.pos[d] = float((face[vn] & (1 << (2 - d))) != 0);
                }
                vert.normal = normals[n];
                vert.tc = tcs[vn];
                verts.push_back(vert);
            }
        }
        cube_geom[inside] = new Geometry(make_span(verts), "PNT");
        cube_ibo[inside] = GenBO(GL_ELEMENT_ARRAY_BUFFER, make_span(triangles));
    }
    sh->Set();
    RenderArray(PRIM_TRIS, cube_geom[inside], cube_ibo[inside], 36);
}

void GeometryCache::RenderCircle(Shader *sh, Primitive prim, int segments, float radius) {
    assert(segments >= 3);
    auto &geom = circlevbos[segments];
    if (!geom) {
        vector<float3> vbuf(segments);
        float step = PI * 2 / segments;
        for (int i = 0; i < segments; i++) {
            // + 1 to reduce "aliasing" from exact 0 / 90 degrees points
            vbuf[i] = float3(sinf(i * step + 1),
                             cosf(i * step + 1), 0);
        }
        geom = new Geometry(make_span(vbuf), "P");
    }
    Transform2D(float4x4(float4(float2_1 * radius, 1)), [&]() {
        sh->Set();
        RenderArray(prim, geom);
    });
}

void GeometryCache::RenderOpenCircle(Shader *sh, int segments, float radius, float thickness) {
    assert(segments >= 3);
    auto &vibo = opencirclevbos[{ segments, thickness }];
    auto nverts = segments * 2;
    auto nindices = segments * 6;
    if (!vibo.first) {
        vector<float3> vbuf(nverts);
        vector<int> ibuf(nindices);
        float step = PI * 2 / segments;
        float inner = 1 - thickness;
        for (int i = 0; i < segments; i++) {
            // + 1 to reduce "aliasing" from exact 0 / 90 degrees points
            float x = sinf(i * step + 1);
            float y = cosf(i * step + 1);
            vbuf[i * 2 + 0] = float3(x, y, 0);
            vbuf[i * 2 + 1] = float3(x * inner, y * inner, 0);
            ibuf[i * 6 + 0] = i * 2 + 0;
            ibuf[i * 6 + 1] = ((i + 1) * 2 + 0) % nverts;
            ibuf[i * 6 + 2] = i * 2 + 1;
            ibuf[i * 6 + 3] = i * 2 + 1;
            ibuf[i * 6 + 4] = ((i + 1) * 2 + 1) % nverts;
            ibuf[i * 6 + 5] = ((i + 1) * 2 + 0) % nverts;
        }
        vibo.first = new Geometry(make_span(vbuf), "P");
        vibo.second = GenBO(GL_ELEMENT_ARRAY_BUFFER, make_span(ibuf));
    }
    Transform2D(float4x4(float4(float2_1 * radius, 1)), [&]() {
        sh->Set();
        RenderArray(PRIM_TRIS, vibo.first, vibo.second, nindices);
    });
}

GeometryCache::~GeometryCache() {
    for (int i = 0; i < 2; i++) {
        delete quadgeom[i];
        delete cube_geom[i];
        if (cube_ibo[i]) DeleteBO(cube_ibo[i]);
    }
    for (auto &p : circlevbos) delete p.second;
    for (auto &p : opencirclevbos) delete p.second.first;
}
