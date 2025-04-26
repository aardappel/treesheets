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

// IQM model loader, see: http://sauerbraten.org/iqm/ https://github.com/lsalzman/iqm

#include "lobster/stdafx.h"

#include "lobster/vmdata.h"
#include "lobster/glinterface.h"
#include "lobster/glincludes.h"

#define IQM_MAGIC "INTERQUAKEMODEL"
#define IQM_VERSION 2

struct iqmheader {
    char magic[16];
    uint32_t version;
    uint32_t filesize;
    uint32_t flags;
    uint32_t num_text, ofs_text;
    uint32_t num_meshes, ofs_meshes;
    uint32_t num_vertexarrays, num_vertexes, ofs_vertexarrays;
    uint32_t num_triangles, ofs_triangles, ofs_adjacency;
    uint32_t num_joints, ofs_joints;
    uint32_t num_poses, ofs_poses;
    uint32_t num_anims, ofs_anims;
    uint32_t num_frames, num_framechannels, ofs_frames, ofs_bounds;
    uint32_t num_comment, ofs_comment;
    uint32_t num_extensions, ofs_extensions;
};

struct iqmmesh {
    uint32_t name;
    uint32_t material;
    uint32_t first_vertex, num_vertexes;
    uint32_t first_triangle, num_triangles;
};

enum {
    IQM_POSITION     = 0,
    IQM_TEXCOORD     = 1,
    IQM_NORMAL       = 2,
    IQM_TANGENT      = 3,
    IQM_BLENDINDEXES = 4,
    IQM_BLENDWEIGHTS = 5,
    IQM_COLOR        = 6,
    IQM_CUSTOM       = 0x10
};

enum {
    IQM_BYTE   = 0,
    IQM_UBYTE  = 1,
    IQM_SHORT  = 2,
    IQM_USHORT = 3,
    IQM_INT    = 4,
    IQM_UINT   = 5,
    IQM_HALF   = 6,
    IQM_FLOAT  = 7,
    IQM_DOUBLE = 8,
};

struct iqmtriangle {
    uint32_t vertex[3];
};

struct iqmjoint {
    uint32_t name;
    int parent;
    float translate[3], rotate[4], scale[3];
};

struct iqmpose {
    int parent;
    uint32_t mask;
    float channeloffset[10];
    float channelscale[10];
};

struct iqmanim {
    uint32_t name;
    uint32_t first_frame, num_frames;
    float framerate;
    uint32_t flags;
};

enum {
    IQM_LOOP = 1<<0
};

struct iqmvertexarray {
    uint32_t type;
    uint32_t flags;
    uint32_t format;
    uint32_t size;
    uint32_t offset;
};

struct iqmbounds {
    float bbmin[3], bbmax[3];
    float xyradius, radius;
};

inline bool islittleendian() { static const int val = 1; return *(const uint8_t *)&val != 0; }

inline uint16_t endianswap16(uint16_t n) { return (n<<8) | (n>>8); }
inline uint32_t endianswap32(uint32_t n) { return (n<<24) | (n>>24) | ((n>>8)&0xFF00) | ((n<<8)&0xFF0000); }

template<class T> inline T endianswap(T n) {
    union { T t; uint32_t i; } conv; conv.t = n; conv.i = endianswap32(conv.i); return conv.t;
}
template<> inline uint16_t endianswap<uint16_t>(uint16_t n) { return endianswap16(n); }
template<> inline short endianswap<short>(short n) { return endianswap16(n); }
template<> inline uint32_t endianswap<uint32_t>(uint32_t n) { return endianswap32(n); }
template<> inline int endianswap<int>(int n) { return endianswap32(n); }

template<class T> inline void endianswap(T *buf, size_t len) {
    for(T *end = &buf[len]; buf < end; buf++) *buf = endianswap(*buf);
}
template<class T> inline T lilswap(T n) {
    return islittleendian() ? n : endianswap(n);
}
template<class T> inline void lilswap(T *buf, size_t len) {
    if(!islittleendian()) endianswap(buf, len);
}
template<class T> inline T bigswap(T n) {
    return islittleendian() ? endianswap(n) : n;
}
template<class T> inline void bigswap(T *buf, size_t len) {
    if(islittleendian()) endianswap(buf, len);
}

template<class T> T getval(FILE *f) { T n; return fread(&n, 1, sizeof(n), f) == sizeof(n) ? n : 0; }
template<class T> T getlil(FILE *f) { return lilswap(getval<T>(f)); }
template<class T> T getbig(FILE *f) { return bigswap(getval<T>(f)); }

static string filebuffer;
static float *inposition = nullptr, *innormal = nullptr, *intangent = nullptr,
             *intexcoord = nullptr;
static uint8_t *inblendindex = nullptr, *inblendweight = nullptr, *incolor = nullptr;
static int nummeshes = 0, numtris = 0, numverts = 0, numjoints = 0, numframes = 0, numanims = 0;
static iqmtriangle *tris = nullptr, *adjacency = nullptr;
static iqmmesh *meshes = nullptr;
static const char **textures = nullptr;
static iqmjoint *joints = nullptr;
static iqmpose *poses = nullptr;
static iqmanim *anims = nullptr;
static const char **anim_names = nullptr;
static iqmbounds *bounds = nullptr;
static float3x4 *baseframe = nullptr, *inversebaseframe = nullptr, *frames = nullptr;

void cleanupiqm() {
    delete[] textures;         textures = nullptr;
    delete[] baseframe;        baseframe = nullptr;
    delete[] inversebaseframe; inversebaseframe = nullptr;
    delete[] frames;           frames = nullptr;
    delete[] anim_names;       anim_names = nullptr;
    string().swap(filebuffer);
    inposition = nullptr;
    innormal = nullptr;
    intangent = nullptr;
    intexcoord = nullptr;
    inblendindex = nullptr;
    inblendweight = nullptr;
    incolor = nullptr;
    nummeshes = 0;
    numtris = 0;
    numverts = 0;
    numjoints = 0;
    numframes = 0;
    numanims = 0;
    tris = nullptr;
    adjacency = nullptr;
    meshes = nullptr;
    joints = nullptr;
    poses = nullptr;
    anims = nullptr;
    bounds = nullptr;
}

bool loadiqmmeshes(const iqmheader &hdr, const char *buf) {
    lilswap((uint32_t *)&buf[hdr.ofs_vertexarrays],
            hdr.num_vertexarrays*sizeof(iqmvertexarray)/sizeof(uint32_t));
    lilswap((uint32_t *)&buf[hdr.ofs_triangles],
            hdr.num_triangles*sizeof(iqmtriangle)/sizeof(uint32_t));
    lilswap((uint32_t *)&buf[hdr.ofs_meshes],
            hdr.num_meshes*sizeof(iqmmesh)/sizeof(uint32_t));
    lilswap((uint32_t *)&buf[hdr.ofs_joints],
            hdr.num_joints*sizeof(iqmjoint)/sizeof(uint32_t));
    if(hdr.ofs_adjacency)
        lilswap((uint32_t *)&buf[hdr.ofs_adjacency],
                hdr.num_triangles*sizeof(iqmtriangle)/sizeof(uint32_t));
    nummeshes = hdr.num_meshes;
    numtris   = hdr.num_triangles;
    numverts  = hdr.num_vertexes;
    numjoints = hdr.num_joints;
    textures = new const char *[nummeshes];
    memset(textures, 0, nummeshes*sizeof(const char *));
    const char *str = hdr.ofs_text ? &buf[hdr.ofs_text] : "";
    iqmvertexarray *vas = (iqmvertexarray *)&buf[hdr.ofs_vertexarrays];
    for(int i = 0; i < (int)hdr.num_vertexarrays; i++) {
        iqmvertexarray &va = vas[i];
        switch(va.type) {
            case IQM_POSITION:
                if(va.format != IQM_FLOAT || va.size != 3) return false;
                inposition = (float *)&buf[va.offset]; lilswap(inposition, 3*hdr.num_vertexes);
                break;
            case IQM_NORMAL:
                if(va.format != IQM_FLOAT || va.size != 3) return false;
                innormal = (float *)&buf[va.offset]; lilswap(innormal,3*hdr.num_vertexes);
                break;
            case IQM_TANGENT:
                if(va.format != IQM_FLOAT || va.size != 4) return false;
                intangent = (float *)&buf[va.offset]; lilswap(intangent, 4*hdr.num_vertexes);
                break;
            case IQM_TEXCOORD:
                if(va.format != IQM_FLOAT || va.size != 2) return false;
                intexcoord = (float *)&buf[va.offset]; lilswap(intexcoord, 2*hdr.num_vertexes);
                break;
            case IQM_BLENDINDEXES:
                if(va.format != IQM_UBYTE || va.size != 4) return false;
                inblendindex = (uint8_t *)&buf[va.offset];
                break;
            case IQM_BLENDWEIGHTS:
                if(va.format != IQM_UBYTE || va.size != 4) return false;
                inblendweight = (uint8_t *)&buf[va.offset];
                break;
            case IQM_COLOR:
                if(va.format != IQM_UBYTE || va.size != 4) return false;
                incolor = (uint8_t *)&buf[va.offset];
                break;
        }
    }
    tris = (iqmtriangle *)&buf[hdr.ofs_triangles];
    meshes = (iqmmesh *)&buf[hdr.ofs_meshes];
    joints = (iqmjoint *)&buf[hdr.ofs_joints];
    if(hdr.ofs_adjacency) adjacency = (iqmtriangle *)&buf[hdr.ofs_adjacency];
    baseframe = new float3x4[hdr.num_joints];
    inversebaseframe = new float3x4[hdr.num_joints];
    for(int i = 0; i < (int)hdr.num_joints; i++) {
        iqmjoint &j = joints[i];
        baseframe[i] =
            rotationscaletrans(normalize(quat(j.rotate)), float3(j.scale), float3(j.translate));
        inversebaseframe[i] = invertortho(baseframe[i]);
        if(j.parent >= 0)  {
            baseframe[i] = baseframe[j.parent] * baseframe[i];
            inversebaseframe[i] *= inversebaseframe[j.parent];
        }
    }
    for(int i = 0; i < (int)hdr.num_meshes; i++) {
        iqmmesh &m = meshes[i];
        textures[i] = &str[m.name];
    }
    return true;
}

bool loadiqmanims(const iqmheader &hdr, const char *buf) {
    if((int)hdr.num_poses != numjoints) return false;
    lilswap((uint32_t *)&buf[hdr.ofs_poses], hdr.num_poses*sizeof(iqmpose)/sizeof(uint32_t));
    lilswap((uint32_t *)&buf[hdr.ofs_anims], hdr.num_anims*sizeof(iqmanim)/sizeof(uint32_t));
    lilswap((uint16_t *)&buf[hdr.ofs_frames], hdr.num_frames*hdr.num_framechannels);
    if(hdr.ofs_bounds)
        lilswap((uint32_t *)&buf[hdr.ofs_bounds], hdr.num_frames*sizeof(iqmbounds)/sizeof(uint32_t));
    numanims = hdr.num_anims;
    numframes = hdr.num_frames;
    anims = (iqmanim *)&buf[hdr.ofs_anims];
    poses = (iqmpose *)&buf[hdr.ofs_poses];
    frames = new float3x4[hdr.num_frames * hdr.num_poses];
    uint16_t *framedata = (uint16_t *)&buf[hdr.ofs_frames];
    if(hdr.ofs_bounds) bounds = (iqmbounds *)&buf[hdr.ofs_bounds];
    for(int i = 0; i < (int)hdr.num_frames; i++) {
        for(int j = 0; j < (int)hdr.num_poses; j++) {
            float trs[10];
            iqmpose &p = poses[j];
            for (int k = 0; k < 10; k++) {
                trs[k] = p.channeloffset[k];
                if(p.mask&(1<<k)) trs[k] += *framedata++ * p.channelscale[k];
            }
            // Concatenate each pose with the inverse base pose to avoid doing this at animation
            // time. If the joint has a parent, then it needs to be pre-concatenated with its
            // parent's base pose. Thus it all negates at animation time like so:
            //   (parentPose * parentInverseBasePose) *
            //   (parentBasePose * childPose * childInverseBasePose) =>
            //   parentPose * (parentInverseBasePose * parentBasePose) *
            //   childPose * childInverseBasePose =>
            //   parentPose * childPose * childInverseBasePose
            auto m = rotationscaletrans(normalize(quat(&trs[3])), *(float3 *)&trs[7],
                                        *(float3 *)&trs[0]);
            if(p.parent >= 0) m = baseframe[p.parent] * m * inversebaseframe[j];
            else m = m * inversebaseframe[j];
            // This parent multiplication may have to be moved to blend time for more complicated
            // anim features.
            if(joints[j].parent >= 0)
                m = frames[i*hdr.num_poses + joints[j].parent] * (float3x4 &)m;
            frames[i*hdr.num_poses + j] = m;
        }
    }
    const char *str = hdr.ofs_text ? &buf[hdr.ofs_text] : "";
    anim_names = new const char *[numanims];
    memset(anim_names, 0, numanims * sizeof(const char *));
    for (int i = 0; i < numanims; i++) {
        anim_names[i] = &str[anims[i].name];
    }
    return true;
}

bool loadiqm(string_view filename) {
    if(LoadFile(filename, &filebuffer) < 0) return false;
    iqmheader hdr = *(iqmheader *)filebuffer.c_str();
    if(memcmp(hdr.magic, IQM_MAGIC, sizeof(hdr.magic)))
        return false;
    lilswap(&hdr.version, (sizeof(hdr) - sizeof(hdr.magic))/sizeof(uint32_t));
    if(hdr.version != IQM_VERSION)
        return false;
    if(filebuffer.length() != hdr.filesize || hdr.filesize > (16<<20))
        return false; // sanity check... don't load files bigger than 16 MB
    if(hdr.num_meshes > 0 && !loadiqmmeshes(hdr, filebuffer.c_str())) return false;
    if(hdr.num_anims  > 0 && !loadiqmanims (hdr, filebuffer.c_str())) return false;
    return true;
}

Mesh *LoadIQM(string_view filename) {
    if (!loadiqm(filename)) {
        cleanupiqm();
        return nullptr;
    }
    // FIXME: Can save 8 bytes on non-animated verts by using BasicVert.
    vector<AnimVert> verts(numverts);
    for (int i = 0; i < numverts; i++) {
        auto &v = verts[i];
        v.pos     = inposition    ? *(float3 *)&inposition   [i * 3] : float3_0;
        v.norm    = innormal      ? *(float3 *)&innormal     [i * 3] : float3_0;
        v.tc      = intexcoord    ? *(float2 *)&intexcoord   [i * 2] : float2_0;
        v.col     = incolor       ? *(byte4  *)&incolor      [i * 4] : byte4_255;
        v.weights = inblendweight ? *(byte4  *)&inblendweight[i * 4] : byte4_0;
        v.indices = inblendindex  ? *(byte4  *)&inblendindex [i * 4] : byte4_0;
    }
    if (!innormal)
        normalize_mesh(gsl::make_span((int *)tris, numtris * 3), verts.data(), numverts, sizeof(AnimVert),
                       (uint8_t *)&verts[0].norm - (uint8_t *)&verts[0].pos);
    auto geom = new Geometry(filename, gsl::make_span(verts), "PNTCWI");
    auto mesh = new Mesh(geom);
    for (int i = 0; i < nummeshes; i++) {
        auto surf =
            new Surface(filename, gsl::make_span((int *)(tris + meshes[i].first_triangle), meshes[i].num_triangles * 3));
        surf->name = textures[i];
        mesh->surfs.push_back(surf);
    }
    if (numjoints) {
        mesh->numbones = numjoints;
        mesh->numframes = numframes;
        auto mats = new float3x4[numjoints * numframes];
        t_memcpy(mats, frames, numjoints * numframes);
        mesh->mats = mats;
    }
    for (int i = 0; i < numanims; i++) {
        auto anim =
            Mesh::Animation{(int)anims[i].first_frame, (int)anims[i].num_frames, anims[i].framerate};
        mesh->animations[anim_names[i]] = anim;
    }
    cleanupiqm();
    return mesh;
}

string SaveAsIQM(const Mesh *mesh, string_view filename) {
    #ifndef PLATFORM_ES3
    if (mesh->geom->fmt != "PNC") {
        return "only support 'PNC' vertex format";
    }
    if (mesh->surfs.size() != 1) {
        return "only support one mesh part";
    }
    if (mesh->surfs[0]->prim != PRIM_TRIS) {
        return "only support triangles primitive";
    }

    struct vert {
        float pos[3], normal[3];
        uint8_t color[4];
    };

    Geometry *geom = mesh->geom;
    Surface *surf = mesh->surfs[0];
    iqmheader hdr;
    iqmmesh mesh2;
    iqmvertexarray varray[3];
    vector<vert> vattribs;
    vector<float3> vpositions;
    vector<float3> vnormals;
    vector<byte4> vcolors;
    vector<iqmtriangle> triangles;

    // Prepare vertex data
    vattribs.resize(geom->nverts);
    vpositions.reserve(geom->nverts);
    vnormals.reserve(geom->nverts);
    vcolors.reserve(geom->nverts);
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, geom->vbo1));
    GL_CALL(glGetBufferSubData(GL_ARRAY_BUFFER, 0, geom->nverts*sizeof(vert), vattribs.data()));
    for (const auto &v : vattribs) {
        vpositions.push_back(float3(v.pos[0], v.pos[1], v.pos[2]));
        vnormals.push_back(float3(v.normal[0], v.normal[1], v.color[2]));
        vcolors.push_back(byte4(v.color[0], v.color[1], v.color[2], v.color[3]));
    }

    // Prepare triangle data
    memset(&mesh2, 0, sizeof(mesh2));
    mesh2.num_vertexes = (uint32_t)geom->nverts;
    mesh2.num_triangles = (uint32_t)surf->numidx / 3;
    triangles.resize(mesh2.num_triangles);
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, surf->ibo));
    GL_CALL(glGetBufferSubData(GL_ARRAY_BUFFER, 0, mesh2.num_triangles*sizeof(iqmtriangle), triangles.data()));

    // Init header
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, IQM_MAGIC, sizeof(hdr.magic)); 
    hdr.filesize = sizeof(hdr);
    hdr.version = IQM_VERSION;
    hdr.num_meshes = 1;
    hdr.ofs_meshes = hdr.filesize;
    hdr.filesize += sizeof(iqmmesh);
    uint32_t voffset = hdr.filesize + sizeof(varray);
    hdr.num_vertexarrays = 3;
    hdr.ofs_vertexarrays = hdr.filesize;
    hdr.filesize += sizeof(varray);
    uint32_t valign = (8 - (hdr.filesize%8))%8;
    voffset += valign;
    hdr.filesize += uint32_t(valign + vattribs.size() * sizeof(vert));
    hdr.num_vertexes = (uint32_t)vattribs.size();
    hdr.num_triangles = (uint32_t)triangles.size();
    hdr.ofs_triangles = hdr.filesize;
    hdr.filesize += uint32_t(triangles.size() * sizeof(iqmtriangle));

    // Init vertexarray(s)
    memset(varray, 0, sizeof(varray));
    varray[0].type = IQM_POSITION;
    varray[0].format = IQM_FLOAT;
    varray[0].size = 3;
    varray[0].offset = voffset;
    varray[1].type = IQM_NORMAL;
    varray[1].format = IQM_FLOAT;
    varray[1].size = 3;
    varray[1].offset = uint32_t(voffset + vpositions.size() * sizeof(float3));
    varray[2].type = IQM_COLOR;
    varray[2].format = IQM_UBYTE;
    varray[2].size = 4;
    varray[2].offset = uint32_t(voffset + (vpositions.size() + vnormals.size()) * sizeof(float3));

    // Change to little-endian
    if (!islittleendian()) {
        endianswap(&hdr.version, (sizeof(hdr) - sizeof(hdr.magic))/sizeof(uint32_t));
        endianswap((uint32_t*)&mesh2, sizeof(mesh2)/sizeof(uint32_t));
        endianswap((uint32_t*)&varray[0], sizeof(varray)/(sizeof(uint32_t)));
        endianswap((float*)vpositions.data(), vpositions.size() * 3);
        endianswap((float*)vnormals.data(), vnormals.size() * 3);
        endianswap((uint32_t*)triangles.data(), triangles.size() * 3);
    }

    // Accumulate data to be written
    string s;
    s.append((char*)&hdr, sizeof(hdr));
    s.append((char*)&mesh2, sizeof(mesh2));
    s.append((char*)&varray[0], sizeof(varray));
    for (uint32_t i = 0; i < valign; i++) s.append("\x00");
    s.append((char*)vpositions.data(), sizeof(float3)*vpositions.size());
    s.append((char*)vnormals.data(), sizeof(float3)*vnormals.size());
    s.append((char*)vcolors.data(), sizeof(byte4)*vcolors.size());
    s.append((char*)triangles.data(), sizeof(iqmtriangle)*triangles.size());

    if (!WriteFile(filename, true, s, false)) {
        return "unable to write file to filesystem";
    }
    return "";
    #else
    (void)mesh;
    (void)filename;
    return "unsupported when using OpenGL ES";
    #endif
}
