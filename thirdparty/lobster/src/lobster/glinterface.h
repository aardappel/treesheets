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

#ifndef LOBSTER_GL_INTERFACE
#define LOBSTER_GL_INTERFACE

// simple rendering interface for OpenGL (ES) (that doesn't depend on its headers)

#define TIME_QUERY_SAMPLE_COUNT 8u

enum BlendMode {
    BLEND_NONE = 0,
    BLEND_ALPHA,
    BLEND_ADD,
    BLEND_ADDALPHA,
    BLEND_MUL,
    BLEND_PREMULALPHA
};

enum Primitive { PRIM_TRIS, PRIM_FAN, PRIM_LOOP, PRIM_POINT };

// Meant to be passed by value.
struct Texture {
    int id;
    int3 size;
    int elemsize;
    unsigned type;
    unsigned internalformat;

    Texture(int _id, const int2 &_size, int es, unsigned t, unsigned f)
        : id(_id), size(int3(_size, 0)), elemsize(es), type(t), internalformat(f) {}
    Texture(int _id, const int3 &_size, int es, unsigned t, unsigned f)
        : id(_id), size(_size), elemsize(es), type(t), internalformat(f) {}

    size_t2 MemoryUsage() {
        return { sizeof(Texture), size_t(max(size, int3_1).volume() * elemsize) };
    }
};

extern Texture DummyTexture();

struct OwnedTexture : lobster::Resource {
    Texture t;

    OwnedTexture(Texture t) : t(t) {}
    ~OwnedTexture();
    size_t2 MemoryUsage() { return t.MemoryUsage(); }
    void Dump(string &sd) {
        append(sd, t.size.to_string());
    }
};

struct Shader : lobster::Resource {
    int vs = 0, ps = 0, cs = 0, program = 0;
    int mvp_i, mv_i, projection_i, col_i, camera_i, light1_i, lightparams1_i, framebuffer_size_i,
        bones_i, pointscale_i;
    int max_tex_defined = 0;

    enum { MAX_SAMPLERS = 32 };

    // Use this for reusing BO's for now:
    struct BOEntry {
        int bpi;
        uint32_t idx;
    };
    map<string, BOEntry, less<>> ubomap;
    int binding_point_index_alloc = 0;

    string shader_name;

    ~Shader();

    string Compile(string_view name, const char *vscode, const char *pscode);
    string Compile(string_view name, const char *comcode);
    string Link(string_view);
    void Activate();                            // Makes shader current;
    void Set();                                 // Activate + sets common uniforms.
    void SetAnim(float3x4 *bones, int num);     // Optionally, after Activate().
    void SetTextures(const vector<Texture> &textures);  // Optionally, after Activate().
    bool SetUniform(string_view_nt name,                // Optionally, after Activate().
                    const float *val,
                    int components, int elements = 1);
    bool SetUniform(string_view_nt name,                // Optionally, after Activate().
                    const int *val,
                    int components, int elements = 1);
    bool SetUniformMatrix(string_view_nt name, const float *val, int components, int elements, bool morerows);
    bool DumpBinary(string_view filename, bool stripnonascii);

    size_t2 MemoryUsage() {
        // FIXME: somehow find out sizes of all attached GPU blocks?
        return { sizeof(Shader), 0 };
    }
};

struct TimeQuery : lobster::Resource {
    bool active = false;
    uint32_t back_buffer_index = 0u;
    uint32_t front_buffer_index = 0u;
    uint32_t query_buffer_ids[2][2];
    float timing_average_buffer[TIME_QUERY_SAMPLE_COUNT];
    uint32_t timing_average_buffer_sample = 0u;
    float timing_average_result = 0.0;

    ~TimeQuery();

    void Start();
    void Stop();
    float GetResult();
};

struct Textured {
    vector<Texture> textures;

    Texture &Get(size_t i) {
        while (i >= textures.size())
            textures.emplace_back(DummyTexture());
        return textures[i];
    }
};

struct Surface : Textured {
    size_t numidx;
    int ibo;
    string name;
    Primitive prim;

    Surface(string_view name, gsl::span<int> indices, Primitive _prim = PRIM_TRIS);
    ~Surface();

    void Render(Shader *sh);
    void WritePLY(string &s);

    size_t2 MemoryUsage() {
        return { sizeof(Surface) + textures.size() * sizeof(Texture), numidx * sizeof(int) };
    }
};

struct BasicVert {   // common generic format: "PNTC"
    float3 pos;
    float3 norm;
    float2 tc;
    byte4 col;
};

struct AnimVert : BasicVert { // "PNTCWI"
    byte4 weights;
    byte4 indices;
};

struct SpriteVert {   // "pT"
    float2 pos;
    float2 tc;
};

class Geometry  {
    const size_t vertsize1, vertsize2;

    public:
    string fmt;
    int vbo1 = 0, vbo2 = 0, vao = 0;
    const size_t nverts;

    template<typename T, typename U = float>
    Geometry(string_view name, gsl::span<T> verts1, string_view _fmt,
             gsl::span<U> verts2 = gsl::span<float>(),
             size_t elem_multiple = 1)
        : vertsize1(sizeof(T) * elem_multiple), vertsize2(sizeof(U) * elem_multiple), fmt(_fmt),
          nverts(verts1.size() / elem_multiple) {
        assert(verts2.empty() || verts2.size() == verts1.size());
        Init(name, verts1.data(), verts2.data());
    }

    ~Geometry();

    void Init(string_view name, const void *verts1, const void *verts2);

    void RenderSetup();
    bool WritePLY(string &s, size_t nindices);

    size_t2 MemoryUsage() {
        auto gpu = vertsize1 * nverts;
        if (vbo2) gpu += vertsize2 * nverts;
        return { sizeof(Geometry), gpu };
    }
};

// These must correspond to the constants in gl.lobster
enum ModelFormat { MF_PLY, MF_IQM };

struct Mesh : lobster::Resource {
    Geometry *geom;
    vector<Surface *> surfs;
    Primitive prim;  // If surfs is empty, this determines how to draw the verts.
    float pointsize = 1;  // if prim == PRIM_POINT
    int numframes = 0, numbones = 0;
    float3x4 *mats = nullptr;
    float anim_frame1 = 0.0;
    float anim_frame2 = -1.0;
    float anim_blending = 0.0;

    struct Animation {
        int first_frame, num_frames;
        float framerate;
    };
    map<string, Animation> animations;

    Mesh(Geometry *_g, Primitive _prim = PRIM_FAN)
        : geom(_g), prim(_prim) {}
    ~Mesh();

    void Render(Shader *sh);
    string Save(string_view filename, ModelFormat format);

    size_t2 MemoryUsage() {
        auto usage = size_t2(sizeof(Mesh) + numframes * numbones * sizeof(float3x4), 0);
        usage += geom->MemoryUsage();
        for (auto s : surfs) usage += s->MemoryUsage();
        return usage;
    }
};

struct Light {
    float4 pos;
    float2 params;
};

extern string OpenGLInit(int samples, bool srgb);
extern void OpenGLCleanup();
extern void OpenGLFrameStart(const int2 &ssize);
extern void OpenGLFrameEnd();
extern void OpenGLPostSwapBuffers();
extern void LogGLError(const char *file, int line, const char *call);
extern void SetScissorRect(int2 topleft, int2 size, pair<int2,int2>& prev);

extern void Set2DMode(const int2 &ssize, bool lh, bool depthtest = false);
extern void Set3DMode(float fovy, int2 fbo, int2 fbs, float znear, float zfar, bool nodepth);
extern void Set3DOrtho(const int2 &ssize, const float3 &center, const float3 &extends);
extern bool Is2DMode();
extern bool IsSRGBMode();
extern void CullFace(bool on);
extern void CullFront(bool on);
extern void ClearFrameBuffer(const float3 &c);
extern BlendMode SetBlendMode(BlendMode mode);
extern void SetPointSprite(float size);

extern string LoadMaterialFile(string_view mfile, string_view prefix);
extern string ParseMaterialFile(string_view mfile, string_view prefix);
extern Shader *LookupShader(string_view name);
extern void ShaderShutDown();
extern void ResetProgram();

extern void DispatchCompute(const int3 &groups);
extern void SetImageTexture(int textureunit, const Texture &tex, int level, int tf);
extern void BindAsSSBO(Shader *sh, string_view_nt name, int id);

// These must correspond to the constants in texture.lobster
enum TextureFlag {
    TF_NONE            = 1 << 0,
    TF_CLAMP           = 1 << 1,
    TF_NOMIPMAP        = 1 << 2,
    TF_NEAREST_MAG     = 1 << 3,
    TF_NEAREST_MIN     = 1 << 4,
    TF_FLOAT           = 1 << 5, // rgba32f instead of rgba8
    TF_WRITEONLY       = 1 << 6,
    TF_READWRITE       = 1 << 7, // Default is readonly (compute).
    TF_CUBEMAP         = 1 << 8,
    TF_MULTISAMPLE     = 1 << 9,
    TF_SINGLE_CHANNEL  = 1 << 10, // Default is RGBA.
    TF_3D              = 1 << 11,
    TF_BUFFER_HAS_MIPS = 1 << 12,
    TF_DEPTH           = 1 << 13,
    TF_COMPUTE         = 1 << 14, // For use with compute: do not use SRGB.
    TF_HALF            = 1 << 15, // Use 16-bit representation if possible (only float atm.)
};

extern Texture CreateTexture(string_view name, const uint8_t *buf, int3 dim, int tf = TF_NONE);
extern Texture CreateTextureFromFile(string_view name, int tf = TF_NONE);
extern Texture CreateBlankTexture(string_view name, const int3 &size, int tf = TF_NONE);
extern Texture CreateColoredTexture(string_view name, const int3 &size, const float4 &color,
                                  int tf = TF_NONE);
extern void DeleteTexture(Texture &id);
extern void SetTexture(int textureunit, const Texture &tex);
extern void UnbindAllTextures();
extern void GenerateTextureMipMap(const Texture &tex);
extern uint8_t *ReadTexture(const Texture &tex);
extern bool SaveTexture(const Texture &tex, string_view_nt filename, bool flip);
extern int MaxTextureSize();
extern void SetTextureFlags(const Texture &tex, int tf);
extern bool SwitchToFrameBuffer(const Texture &tex, int2 orig_screensize,
                                bool depth = false, int tf = 0, const Texture &resolvetex = DummyTexture(),
                                const Texture &depthtex = DummyTexture());
extern int2 GetFrameBufferSize(const int2 &screensize);

extern uint8_t *LoadImageFile(string_view fn, int2 &dim);
extern void FreeImageFromFile(uint8_t *img);

extern uint8_t *ReadPixels(const int2 &pos, const int2 &size);

extern int GenBO_(string_view name, int type, size_t bytesize, const void *data, bool dyn);
template<typename T> int GenBO(string_view name, int type, gsl::span<T> d, bool dyn) {
    return GenBO_(name, type, sizeof(T) * d.size(), d.data(), dyn);
}
extern void DeleteBO(int id);
extern void RenderArray(Primitive prim, Geometry *geom, int ibo = 0, size_t tcount = 0);

struct BufferObject : lobster::Resource {
    int bo;
    int type;
    size_t size;
    bool dyn;

    BufferObject(int bo, int type, size_t size, bool dyn)
        : bo(bo), type(type), size(size), dyn(dyn) {}

    ~BufferObject() {
        DeleteBO(bo);
    }

    size_t2 MemoryUsage() {
        return { sizeof(BufferObject), size };
    }
};

extern BufferObject *UpdateBufferObject(BufferObject *buf, const void *data, size_t len,
                                        ptrdiff_t offset, bool ssbo, bool dyn);
extern bool BindBufferObject(Shader *sh, BufferObject *buf, string_view_nt uniformblockname);
extern bool CopyBufferObjects(BufferObject *src, BufferObject *dst, ptrdiff_t srcoffset,
                                        ptrdiff_t dstoffset, size_t len);

template<typename T, typename U = float>
void RenderArraySlow(string_view name, Primitive prim, gsl::span<T> vbuf1, string_view fmt,
                     gsl::span<int> ibuf = gsl::span<int>(),
                     gsl::span<U> vbuf2 = gsl::span<float>()) {
    Geometry geom(name, vbuf1, fmt, vbuf2);
    if (ibuf.empty()) {
        RenderArray(prim, &geom);
    } else {
        Surface surf(name, ibuf, prim);
        RenderArray(prim, &geom, surf.ibo, ibuf.size());
    }
}

struct GeometryCache {
    Geometry *quadgeom[2] = { nullptr, nullptr };
    Geometry *cube_geom[2] = { nullptr, nullptr };
    int cube_ibo[2] = { 0, 0 };
    map<int, Geometry *> circlevbos;
    map<tuple<int, float, float, float>, Geometry *> roundedboxvbos;
    map<pair<int, float>, pair<Geometry *, int>> opencirclevbos;

    ~GeometryCache();

    void RenderUnitSquare(Shader *sh, Primitive prim, bool centered);
    void RenderQuad(Shader *sh, Primitive prim, bool centered, const float4x4 &trans);
    void RenderLine2D(Shader *sh, Primitive prim, const float3 &v1, const float3 &v2,
                      float thickness);
    void RenderLine3D(Shader *sh, const float3 &v1, const float3 &v2, const float3 &campos,
                      float thickness);
    void RenderUnitCube(Shader *sh, int inside);
    void RenderRoundedRectangle(Shader *sh, Primitive prim, int segments, float2 size,
                                float corner_ratio);
    void RenderCircle(Shader *sh, Primitive prim, int segments, float radius);
    void RenderOpenCircle(Shader *sh, int segments, float radius, float thickness);
};

extern size_t AttribsSize(string_view fmt);

extern Mesh *LoadIQM(string_view filename);
extern string SaveAsIQM(const Mesh *mesh, string_view filename);

extern float4x4 view2clip;

class objecttransforms {
    float4x4 o2v;
    vector<float4x4> o2v_stack;
    float4x4 v2o;
    bool v2o_valid = true;

  public:
    objecttransforms() : o2v(1), v2o(1) {}

    const float4x4 &object2view() const {
        return o2v;
    }

    void set_object2view(const float4x4 &n) {
        o2v = n;
        v2o_valid = false;
    }

    void append_object2view(const float4x4 &n) {
        o2v *= n;
        v2o_valid = false;
    }

    // This is needed infrequently, so we cache the inverse.
    // FIXME: somehow track if object2view is only affected by translate/rotate
    // so we can use transpose instead?
    const float4x4 &view2object() {
        if (!v2o_valid) {
            v2o = invert(o2v);
            v2o_valid = true;
        }
        return v2o;
    }

    float3 camerapos() { return view2object()[3].xyz(); }

    void push() {
        o2v_stack.push_back(o2v);
    }

    bool pop() {
        if (o2v_stack.empty()) return false;
        set_object2view(o2v_stack.back());
        o2v_stack.pop_back();
        return true;
    }
};

extern objecttransforms otransforms;

extern vector<Light> lights;

extern float4 curcolor;

extern float pointscale, custompointscale;

extern GeometryCache *geomcache;

extern int max_ssbo, max_ubo;

template<typename F> void Transform(const float4x4 &mat, F body) {
    otransforms.push();
    otransforms.append_object2view(mat);
    body();
    otransforms.pop();
}

extern bool VRInit();
extern void VRShutDown();

extern void RenderDocStartFrameCapture();
extern void RenderDocStopFrameCapture();

#endif  // LOBSTER_GL_INTERFACE
