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

// simple rendering interface for OpenGL (ES) (that doesn't depend on its headers)

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
    int id = 0;
    int3 size { 0 };

    Texture() = default;
    Texture(int _id, const int2 &_size) : id(_id), size(int3(_size, 0)) {}
    Texture(int _id, const int3 &_size) : id(_id), size(_size) {}
};

struct Shader {
    int vs = 0, ps = 0, cs = 0, program = 0;
    int mvp_i, col_i, camera_i, light1_i, lightparams1_i, framebuffer_size_i,
        bones_i, pointscale_i;
    int max_tex_defined = 0;

    enum { MAX_SAMPLERS = 32 };

    ~Shader();

    string Compile(const char *name, const char *vscode, const char *pscode);
    string Compile(const char *name, const char *comcode);
    string Link(const char *name);
    void Activate();                            // Makes shader current;
    void Set();                                 // Activate + sets common uniforms.
    void SetAnim(float3x4 *bones, int num);     // Optionally, after Activate().
    void SetTextures(const vector<Texture> &textures);  // Optionally, after Activate().
    bool SetUniform(string_view name,           // Optionally, after Activate().
                    const float *val,
                    int components, int elements = 1);
    bool SetUniform(string_view name,           // Optionally, after Activate().
                    const int *val,
                    int components, int elements = 1);
    bool SetUniformMatrix(string_view name, const float *val, int components, int elements, bool morerows);
    bool Dump(string_view filename, bool stripnonascii);
};

struct Textured {
    vector<Texture> textures;

    Texture &Get(size_t i) {
        textures.resize(max(i + 1, textures.size()));
        return textures[i];
    }
};

struct Surface : Textured {
    size_t numidx;
    int ibo;
    string name;
    Primitive prim;

    Surface(gsl::span<int> indices, Primitive _prim = PRIM_TRIS);
    ~Surface();

    void Render(Shader *sh);
    void WritePLY(string &s);
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
    string fmt;
    int vbo1 = 0, vbo2 = 0, vao = 0;

    public:
    const size_t nverts;

    template<typename T, typename U = float>
    Geometry(gsl::span<T> verts1, string_view _fmt, gsl::span<U> verts2 = gsl::span<float>(),
             size_t elem_multiple = 1)
        : vertsize1(sizeof(T) * elem_multiple), vertsize2(sizeof(U) * elem_multiple), fmt(_fmt),
          nverts(verts1.size() / elem_multiple) {
        assert(verts2.empty() || verts2.size() == verts1.size());
        Init(verts1.data(), verts2.data());
    }

    ~Geometry();

    void Init(const void *verts1, const void *verts2);

    void RenderSetup();
    void BindAsSSBO(Shader *sh, string_view name);
    bool WritePLY(string &s, size_t nindices);
};

struct Mesh {
    Geometry *geom;
    vector<Surface *> surfs;
    Primitive prim;  // If surfs is empty, this determines how to draw the verts.
    float pointsize = 1;  // if prim == PRIM_POINT
    int numframes = 0, numbones = 0;
    float3x4 *mats = nullptr;
    float curanim = 0;

    Mesh(Geometry *_g, Primitive _prim = PRIM_FAN)
        : geom(_g), prim(_prim) {}
    ~Mesh();

    void Render(Shader *sh);
    bool SaveAsPLY(string_view filename);
};

struct Light {
    float4 pos;
    float2 params;
};


extern string OpenGLInit(int samples, bool srgb);
extern void OpenGLCleanup();
extern void OpenGLFrameStart(const int2 &ssize);
extern void OpenGLFrameEnd();
extern void LogGLError(const char *file, int line, const char *call);
extern void SetScissorRect(int2 topleft, int2 size, pair<int2,int2>& prev);

extern void Set2DMode(const int2 &ssize, bool lh, bool depthtest = false);
extern void Set3DMode(float fovy, float ratio, float znear, float zfar);
extern void Set3DOrtho(const float3 &center, const float3 &extends);
extern bool Is2DMode();
extern bool IsSRGBMode();
extern void ClearFrameBuffer(const float3 &c);
extern BlendMode SetBlendMode(BlendMode mode);
extern void SetPointSprite(float size);

extern string LoadMaterialFile(string_view mfile);
extern string ParseMaterialFile(string_view mfile);
extern Shader *LookupShader(string_view name);
extern void ShaderShutDown();

extern void DispatchCompute(const int3 &groups);
extern void SetImageTexture(int textureunit, const Texture &tex, int tf);
extern int UniformBufferObject(Shader *sh, const void *data, size_t len, ptrdiff_t offset,
                               string_view uniformblockname, bool ssbo, int bo);

// These must correspond to the constants in color.lobster
enum TextureFlag {
    TF_NONE = 0,
    TF_CLAMP = 1,
    TF_NOMIPMAP = 2,
    TF_NEAREST_MAG = 4,
    TF_NEAREST_MIN = 8,
    TF_FLOAT = 16,                           // rgba32f instead of rgba8
    TF_WRITEONLY = 32, TF_READWRITE = 64,   // Default is readonly.
    TF_CUBEMAP = 128,
    TF_MULTISAMPLE = 256,
    TF_SINGLE_CHANNEL = 512,                // Default is RGBA.
    TF_3D = 1024,
    TF_BUFFER_HAS_MIPS = 2048,
    TF_DEPTH = 4096
};

extern Texture CreateTexture(const uint8_t *buf, int3 dim, int tf = TF_NONE);
extern Texture CreateTextureFromFile(string_view name, int tf = TF_NONE);
extern Texture CreateBlankTexture(const int2 &size, const float4 &color, int tf = TF_NONE);
extern void DeleteTexture(Texture &id);
extern bool SetTexture(int textureunit, const Texture &tex, int tf = TF_NONE);
extern uint8_t *ReadTexture(const Texture &tex);
extern int MaxTextureSize();
extern bool SwitchToFrameBuffer(const Texture &tex, int2 orig_screensize,
                                bool depth = false, int tf = 0,
                                const Texture &resolvetex = Texture(),
                                const Texture &depthtex = Texture());
extern int2 GetFrameBufferSize(const int2 &screensize);

extern uint8_t *LoadImageFile(string_view fn, int2 &dim);
extern void FreeImageFromFile(uint8_t *img);

extern uint8_t *ReadPixels(const int2 &pos, const int2 &size);

extern int GenBO_(int type, size_t bytesize, const void *data);
template<typename T> int GenBO(int type, gsl::span<T> d) {
    return GenBO_(type, sizeof(T) * d.size(), d.data());
}
extern void DeleteBO(int id);
extern void RenderArray(Primitive prim, Geometry *geom, int ibo = 0, size_t tcount = 0);

template<typename T, typename U = float>
void RenderArraySlow(Primitive prim, gsl::span<T> vbuf1, string_view fmt,
                     gsl::span<int> ibuf = gsl::span<int>(),
                     gsl::span<U> vbuf2 = gsl::span<float>()) {
    Geometry geom(vbuf1, fmt, vbuf2);
    if (ibuf.empty()) {
        RenderArray(prim, &geom);
    } else {
        Surface surf(ibuf, prim);
        RenderArray(prim, &geom, surf.ibo, ibuf.size());
    }
}

struct GeometryCache {
    Geometry *quadgeom[2] = { nullptr, nullptr };
    Geometry *cube_geom[2] = { nullptr, nullptr };
    int cube_ibo[2] = { 0, 0 };
    map<int, Geometry *> circlevbos;
    map<pair<int, float>, pair<Geometry *, int>> opencirclevbos;

    ~GeometryCache();

    void RenderUnitSquare(Shader *sh, Primitive prim, bool centered);
    void RenderQuad(Shader *sh, Primitive prim, bool centered, const float4x4 &trans);
    void RenderLine2D(Shader *sh, Primitive prim, const float3 &v1, const float3 &v2,
                      float thickness);
    void RenderLine3D(Shader *sh, const float3 &v1, const float3 &v2, const float3 &campos,
                      float thickness);
    void RenderUnitCube(Shader *sh, int inside);
    void RenderCircle(Shader *sh, Primitive prim, int segments, float radius);
    void RenderOpenCircle(Shader *sh, int segments, float radius, float thickness);
};

extern size_t AttribsSize(string_view fmt);

extern Mesh *LoadIQM(string_view filename);

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

template<typename F> void Transform(const float4x4 &mat, F body) {
    auto oldobject2view = otransforms.object2view();
    otransforms.set_object2view(oldobject2view * mat);
    body();
    otransforms.set_object2view(oldobject2view);
}

extern bool VRInit();
extern void VRShutDown();
