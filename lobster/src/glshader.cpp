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

#include "lobster/vmdata.h"
#include "lobster/glinterface.h"
#include "lobster/glincludes.h"
#include "lobster/sdlinterface.h"

// We use a vector here because gl.get_shader may cause there to be references to
// old versions of a shader with a given name that has since been reloaded.
map<string, vector<unique_ptr<Shader>>, less<>> shadermap;

Shader *LookupShader(string_view name) {
    auto shi = shadermap.find(name);
    if (shi != shadermap.end()) return shi->second.back().get();
    return nullptr;
}

void ShaderShutDown() {
    shadermap.clear();
}

string GLSLError(int obj, bool isprogram, const char *source) {
    GLint length = 0;
    if (isprogram) GL_CALL(glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &length));
    else           GL_CALL(glGetShaderiv (obj, GL_INFO_LOG_LENGTH, &length));
    if (length > 1) {
        GLchar *log = new GLchar[length];
        if (isprogram) GL_CALL(glGetProgramInfoLog(obj, length, &length, log));
        else           GL_CALL(glGetShaderInfoLog (obj, length, &length, log));
        string err;
        err += log;
        int i = 0;
        if (source) {
            for (;;) {
                err += cat(++i, ": ");
                const char *next = strchr(source, '\n');
                if (next) {
                    err += string_view(source, next - source + 1);
                    source = next + 1;
                } else {
                    err += string_view(source) + "\n";
                    break;
                }
            }
            // repeat the error, since source often long, to avoid scrolling.
            err += log;
        }
        delete[] log;
        return err;
    }
    return "";
}

int CompileGLSLShader(GLenum type, int program, const GLchar *source, string &err)  {
    //WriteFile(cat("shaderdump_", program, "_", type, ".glsl"), false, source);
    int obj = glCreateShader(type);
    GL_CALL(glShaderSource(obj, 1, &source, nullptr));
    GL_CALL(glCompileShader(obj));
    GLint success;
    GL_CALL(glGetShaderiv(obj, GL_COMPILE_STATUS, &success));
    if (success) {
        GL_CALL(glAttachShader(program, obj));
        return obj;
    }
    err = GLSLError(obj, false, source);
    GL_CALL(glDeleteShader(obj));
    #if 0
        auto f = OpenForWriting(cat("errshaderdump_", program, "_", type, ".glsl"), false, false);
        if (f) {
            fwrite(source, strlen(source), 1, f);
            fclose(f);
        }
    #endif
    return 0;
}

string ParseMaterialFile(string_view mbuf, string_view prefix) {
    auto p = mbuf;
    string err;
    string_view last;
    string defines;
    string vfunctions, pfunctions, cfunctions, vertex, pixel, compute, vdecl, pdecl, csdecl, shader;
    string *accum = nullptr;
    set<string> shader_names;
    auto word = [&]() {
        p.remove_prefix(min(p.find_first_not_of(" \t\r"), p.size()));
        size_t len = min(p.find_first_of(" \t\r"), p.size());
        last = p.substr(0, len);
        p.remove_prefix(len);
    };
    auto finish = [&]() -> bool {
        if (!shader.empty()) {
            if (shader_names.count(shader) == 1) {
                err = cat("SHADER `", shader, "` is defined repeatedly!");
                return true;
            }
            shader_names.insert(shader);

            auto sh = make_unique<Shader>();
            if (compute.length()) {
                #ifdef PLATFORM_WINNIX
                    extern string glslversion;
                    auto header = "#version " + glslversion + "\n" + defines;
                #else
                    auto header = "#version 430\n" + defines;
                #endif
                err = sh->Compile(shader.c_str(), (header + csdecl + cfunctions +
                                                   "void main()\n{\n" + compute + "}\n").c_str());
            } else {
                string header;
                #ifdef PLATFORM_ES3
                    #ifdef __EMSCRIPTEN__
                        header += "#version 300 es\n";
                    #endif
                    header += "#ifdef GL_ES\nprecision highp float;\n#endif\n";
                #else
                    #ifdef __APPLE__
                    auto supported = glGetString(GL_SHADING_LANGUAGE_VERSION);
                    // Apple randomly changes what it supports, so just ask for that.
                    header += string_view("#version ") + string_view((const char *)supported, 1) +
                              string_view((const char *)supported + 2, 2) + "\n";
                    #else
                    extern string glslversion;
                    header += "#version " + glslversion + "\n";
                    header += "#extension GL_EXT_gpu_shader4 : enable\n";
                    #endif
                #endif
                header += defines;
                pdecl = "out vec4 frag_color;\n" + pdecl;
                err = sh->Compile(shader.c_str(),
                                  (header + vdecl + vfunctions + "void main()\n{\n" + vertex +
                                   "}\n").c_str(),
                                  (header + pdecl + pfunctions + "void main()\n{\n" + pixel +
                                   "}\n").c_str());
            }
            if (!err.empty()) {
                return true;
            }
            auto &v = shadermap[shader];
            // Put latest shader version at the end.
            v.emplace_back(std::move(sh));
            shader.clear();
            // Typically, all versions below the latest will not be in used and can be
            // deleted here, unless still referenced by gl.get_shader.
            for (size_t i = 0; i < v.size() - 1; ) {
                if (!v[i]->refc) {
                    v.erase(v.begin() + i);
                } else {
                    i++;
                }
            }
        }
        return false;
    };
    int line_number = 1;
    for (;;) {
        auto start = p;
        p.remove_suffix(p.size() - min(p.find_first_of("\r\n"), p.size()));
        auto line = p;
        word();
        if (!last.empty()) {
            if (last == "VERTEXFUNCTIONS")  {
                if (finish()) return err;
                vfunctions.clear();
                accum = &vfunctions;
            } else if (last == "PIXELFUNCTIONS") {
                if (finish()) return err;
                pfunctions.clear();
                accum = &pfunctions;
            } else if (last == "COMPUTEFUNCTIONS") {
                if (finish()) return err;
                cfunctions.clear();
                accum = &cfunctions;
            } else if (last == "VERTEX") {
                vertex.clear();
                accum = &vertex;
            } else if (last == "PIXEL") {
                pixel.clear();
                accum = &pixel;
            } else if (last == "COMPUTE") {
                compute.clear();
                accum = &compute;
            } else if (last == "SHADER") {
                if (finish()) return err;
                word();
                shader = cat(prefix, last);
                vdecl.clear();
                pdecl.clear();
                csdecl.clear();
                vertex.clear();
                pixel.clear();
                compute.clear();
                accum = nullptr;
            } else if (last == "UNIFORMS") {
                string &decl = accum == &compute ? csdecl : (accum == &vertex ? vdecl : pdecl);
                for (;;) {
                    word();
                    if (last.empty()) break;
                    else if (last == "mvp")              decl += "uniform mat4 mvp;\n";
                    else if (last == "mv")               decl += "uniform mat4 mv;\n";
                    else if (last == "projection")       decl += "uniform mat4 projection;\n";
                    else if (last == "col")              decl += "uniform vec4 col;\n";
                    else if (last == "camera")           decl += "uniform vec3 camera;\n";
                    else if (last == "light1")           decl += "uniform vec3 light1;\n";
                    else if (last == "lightparams1")     decl += "uniform vec2 lightparams1;\n";
                    else if (last == "framebuffer_size") decl += "uniform vec2 framebuffer_size;\n";
                    // FIXME: Make configurable.
                    else if (last == "bones")            decl += "uniform vec4 bones[230];\n";
                    else if (last == "pointscale")       decl += "uniform float pointscale;\n";
                    else if (last.substr(0, 3) == "tex") {
                        auto tp = last;
                        tp.remove_prefix(3);
                        bool cubemap = false;
                        bool floatingp = false;
                        bool halffloatingp = false;
                        bool singlechannel = false;
                        bool d3 = false;
                        bool uav = false;
                        bool write = false;
                        if (starts_with(tp, "cube")) {
                            tp.remove_prefix(4);
                            cubemap = true;
                        }
                        if (starts_with(tp, "3d")) {
                            tp.remove_prefix(2);
                            d3 = true;
                        }
                        if (starts_with(tp, "f")) {
                            tp.remove_prefix(1);
                            floatingp = true;
                        }
                        if (starts_with(tp, "hf")) {
                            tp.remove_prefix(2);
                            halffloatingp = true;
                        }
                        if (starts_with(tp, "sc")) {
                            tp.remove_prefix(2);
                            singlechannel = true;
                        }
                        if (starts_with(tp, "uav")) {
                            tp.remove_prefix(3);
                            uav = true;
                            if (starts_with(tp, "w")) {
                                tp.remove_prefix(1);
                                write = true;
                            }
                        }
                        auto unit = parse_int<int>(tp);
                        if (uav) {
                            decl += cat("layout(binding = ", unit);
                            auto format = (
                                floatingp ? (singlechannel ? "r32f" : "rgba32f") :
                                halffloatingp ? (singlechannel ? "r16f" : "rgba16f") :
                                (singlechannel ? "r8" : "rgba8")
                            );
                            if (!write) decl += cat(", ", format);
                            decl += ") ";
                        }
                        decl += "uniform ";
                        if (uav) {
                            // FIXME: make more general.
                            decl += write ? "writeonly " : "readonly ";
                            decl += cubemap ? "imageCube" : (d3 ? "image3D" : "image2D");
                        } else {
                            decl += cubemap ? "samplerCube" : (d3 ? "sampler3D" : "sampler2D");
                        }
                        decl += " " + last + ";\n";
                    } else return "unknown uniform: " + last;
                }
            } else if (last == "UNIFORM") {
                string &decl = accum == &compute ? csdecl : (accum == &vertex ? vdecl : pdecl);
                word();
                auto type = last;
                word();
                auto name = last;
                if (type.empty() || name.empty()) return "uniform decl must specify type and name";
                decl += "uniform " + type + " " + name + ";\n";
            } else if (last == "INPUTS") {
                string decl;
                for (;;) {
                    word();
                    if (last.empty()) break;
                    auto pos = last.find_first_of(":");
                    if (pos == string_view::npos) {
                        return "input " + last +
                               " doesn't specify number of components, e.g. anormal:3";
                    }
                    int comp = parse_int<int>(last.substr(pos + 1));
                    if (comp <= 0 || comp > 4) {
                        return "input " + last + " can only use 1..4 components";
                    }
                    last = last.substr(0, pos);
                    string d = cat(" vec", comp, " ", last, ";\n");
                    if (accum == &vertex) vdecl += "in" + d;
                    else { vdecl += "out" + d; pdecl += "in" + d; }
                }
            } else if (last == "LAYOUT") {
                word();
                auto xs = last;
                if (xs.empty()) xs = "1";
                word();
                auto ys = last;
                if (ys.empty()) ys = "1";
                word();
                auto zs = last;
                if (zs.empty()) zs = "1";
                csdecl += "layout(local_size_x = " + xs + ", local_size_y = " + ys + ", local_size_z = " + zs + ") in;\n";
            } else if (last == "DEFINE") {
                word();
                auto def = last;
                word();
                auto val = last;
                defines += "#define " + (val.empty() ? def : def + " " + val) + "\n";
            } else if (last == "INCLUDE") {
                if (!accum)
                    return "INCLUDE outside of FUNCTIONS/VERTEX/PIXEL/COMPUTE block: " + line;
                word();
                string ibuf;
                if (LoadFile(last, &ibuf) < 0)
                    return string_view("cannot load include file: ") + last;
                *accum += ibuf;
            } else {
                if (!accum)
                    return "GLSL code outside of FUNCTIONS/VERTEX/PIXEL/COMPUTE block: " + line;
                *accum += line;
                if (line.back() != '\\') *accum += cat("  // ", line_number);
                *accum += "\n";
            }
        }
        if (line.size() == start.size()) break;
        start.remove_prefix(line.size());
        if (!start.empty() && start[0] == '\r') start.remove_prefix(1);
        if (!start.empty() && start[0] == '\n') start.remove_prefix(1);
        p = start;
        line_number++;
    }
    finish();
    return err;
}

string LoadMaterialFile(string_view mfile, string_view prefix) {
    string mbuf;
    if (LoadFile(mfile, &mbuf) < 0) return string_view("cannot load material file: ") + mfile;
    auto err = ParseMaterialFile(mbuf, prefix);
    return err;
}

string Shader::Compile(string_view name, const char *vscode, const char *pscode) {
    program = glCreateProgram();
    GL_NAME(GL_PROGRAM, program, name);
    string err;
    vs = CompileGLSLShader(GL_VERTEX_SHADER, program, vscode, err);
    if (!vs) return string_view("couldn't compile vertex shader: ") + name + "\n" + err;
    GL_NAME(GL_SHADER, vs, name + "_vs");
    ps = CompileGLSLShader(GL_FRAGMENT_SHADER, program, pscode, err);
    if (!ps) return string_view("couldn't compile pixel shader: ") + name + "\n" + err;
    GL_NAME(GL_SHADER, ps, name + "_ps");
    GL_CALL(glBindAttribLocation(program, VATRR_POS, "apos"));
    GL_CALL(glBindAttribLocation(program, VATRR_NOR, "anormal"));
    GL_CALL(glBindAttribLocation(program, VATRR_TC1, "atc"));
    GL_CALL(glBindAttribLocation(program, VATRR_COL, "acolor"));
    GL_CALL(glBindAttribLocation(program, VATRR_WEI, "aweights"));
    GL_CALL(glBindAttribLocation(program, VATRR_IDX, "aindices"));
    GL_CALL(glBindAttribLocation(program, VATRR_TC2, "atc2"));
    return Link(name);
}

string Shader::Compile(string_view name, const char *cscode) {
    #ifdef PLATFORM_WINNIX
        program = glCreateProgram();
        GL_NAME(GL_PROGRAM, program, name);
        string err;
        cs = CompileGLSLShader(GL_COMPUTE_SHADER, program, cscode, err);
        if (!cs) return string_view("couldn't compile compute shader: ") + name + "\n" + err;
        GL_NAME(GL_SHADER, cs, name + "_cs");
        return Link(name);
    #else
        return "compute shaders not supported";
    #endif
}

string Shader::Link(string_view name) {
    GL_CALL(glLinkProgram(program));
    GLint status;
    GL_CALL(glGetProgramiv(program, GL_LINK_STATUS, &status));
    if (status != GL_TRUE) {
        auto err = GLSLError(program, true, nullptr);
        return string_view("linking failed for shader: ") + name + "\n" + err;
    }
    assert(name.size());
    shader_name = name;
    mvp_i              = glGetUniformLocation(program, "mvp");
    mv_i               = glGetUniformLocation(program, "mv");
    projection_i       = glGetUniformLocation(program, "projection");
    col_i              = glGetUniformLocation(program, "col");
    camera_i           = glGetUniformLocation(program, "camera");
    light1_i           = glGetUniformLocation(program, "light1");
    lightparams1_i     = glGetUniformLocation(program, "lightparams1");
    framebuffer_size_i = glGetUniformLocation(program, "framebuffer_size");
    bones_i            = glGetUniformLocation(program, "bones");
    pointscale_i       = glGetUniformLocation(program, "pointscale");
    Activate();
    for (int i = 0; i < MAX_SAMPLERS; i++) {
        auto loc = glGetUniformLocation(program, cat("tex", i).c_str());
        if (loc < 0) {
            loc = glGetUniformLocation(program, cat("texcube", i).c_str());
            if (loc < 0) loc = glGetUniformLocation(program, cat("tex3d", i).c_str());
        }
        if (loc >= 0) {
            glUniform1i(loc, i);
            max_tex_defined = i + 1;
        }
    }
    return "";
}

Shader::~Shader() {
    if (program) GL_CALL(glDeleteProgram(program));
    if (ps) GL_CALL(glDeleteShader(ps));
    if (vs) GL_CALL(glDeleteShader(vs));
    if (cs) GL_CALL(glDeleteShader(cs));
}

// FIXME: unlikely to cause ABA problem, but still better to reset once per frame just in case.
static int last_program = 0;
static Shader *last_shader = nullptr;  // Just for debugging purposes.

void ResetProgram() {
    last_program = 0;
}

void Shader::Activate() {
    if (program != last_program) {
        GL_CALL(glUseProgram(program));
        last_program = program;
        last_shader = this;
    }
}

void Shader::Set() {
    Activate();
    if (mvp_i >= 0) GL_CALL(glUniformMatrix4fv(mvp_i, 1, false,
                                               (view2clip * otransforms.object2view()).data()));
    if (mv_i >= 0) GL_CALL(glUniformMatrix4fv(mv_i, 1, false,
                                               otransforms.object2view().data()));
    if (projection_i >= 0) GL_CALL(glUniformMatrix4fv(projection_i, 1, false,
                                               view2clip.data()));
    if (col_i >= 0) GL_CALL(glUniform4fv(col_i, 1, curcolor.begin()));
    if (camera_i >= 0) GL_CALL(glUniform3fv(camera_i, 1, otransforms.camerapos().begin()));
    if (pointscale_i >= 0) GL_CALL(glUniform1f(pointscale_i, pointscale));
    if (lights.size() > 0) {
        if (light1_i >= 0)
            GL_CALL(glUniform3fv(light1_i, 1, (otransforms.view2object() * lights[0].pos).begin()));
        if (lightparams1_i >= 0)
            GL_CALL(glUniform2fv(lightparams1_i, 1, lights[0].params.begin()));
    }
    if (framebuffer_size_i >= 0)
        GL_CALL(glUniform2fv(framebuffer_size_i, 1,
                             float2(GetFrameBufferSize(GetScreenSize())).begin()));
}

void Shader::SetAnim(float3x4 *bones, int num) {
    // FIXME: Check if num fits with shader def.
    if (bones_i >= 0) GL_CALL(glUniform4fv(bones_i, num * 3, (float *)bones));
}

void Shader::SetTextures(const vector<Texture> &textures) {
    for (int i = 0; i < min(max_tex_defined, (int)textures.size()); i++) {
        SetTexture(i, textures[i]);
    }
}

bool Shader::SetUniform(string_view_nt name, const float *val, int components, int elements) {
    auto loc = glGetUniformLocation(program, name.c_str());
    if (loc < 0) return false;
    switch (components) {
        // glUniform fails on mismatched type, so this not an assert.
        case 1: glUniform1fv(loc, elements, val); return glGetError() == 0;
        case 2: glUniform2fv(loc, elements, val); return glGetError() == 0;
        case 3: glUniform3fv(loc, elements, val); return glGetError() == 0;
        case 4: glUniform4fv(loc, elements, val); return glGetError() == 0;
        default: return false;
    }
}

bool Shader::SetUniform(string_view_nt name, const int *val, int components, int elements) {
    auto loc = glGetUniformLocation(program, name.c_str());
    if (loc < 0) return false;
    switch (components) {
        // glUniform fails on mismatched type, so this not an assert.
        case 1: glUniform1iv(loc, elements, val); return glGetError() == 0;
        case 2: glUniform2iv(loc, elements, val); return glGetError() == 0;
        case 3: glUniform3iv(loc, elements, val); return glGetError() == 0;
        case 4: glUniform4iv(loc, elements, val); return glGetError() == 0;
        default: return false;
    }
}

bool Shader::SetUniformMatrix(string_view_nt name, const float *val, int components, int elements,
                              bool mr) {
    auto loc = glGetUniformLocation(program, name.c_str());
    if (loc < 0) return false;
    switch (components) {
        case 4:  GL_CALL(glUniformMatrix2fv(loc, elements, false, val)); return true;
        case 6:  if (mr) GL_CALL(glUniformMatrix2x3fv(loc, elements, false, val));
                 else GL_CALL(glUniformMatrix3x2fv(loc, elements, false, val));
                 return true;
        case 9:  GL_CALL(glUniformMatrix3fv(loc, elements, false, val)); return true;
        case 12: if (mr) GL_CALL(glUniformMatrix3x4fv(loc, elements, false, val));
                 else GL_CALL(glUniformMatrix4x3fv(loc, elements, false, val));
                 return true;
        case 16: GL_CALL(glUniformMatrix4fv(loc, elements, false, val)); return true;
        default: return false;
    }
}

void DispatchCompute(const int3 &groups) {
    LOBSTER_FRAME_PROFILE_GPU;  
    #ifdef PLATFORM_WINNIX
        if (glDispatchCompute) GL_CALL(glDispatchCompute(groups.x, groups.y, groups.z));
        // Make sure any imageStore/VBOasSSBO operations have completed.
        // Would be better to decouple this from DispatchCompute.
        if (glMemoryBarrier)
            GL_CALL(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT |
                                    GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT));
    #else
        assert(false);
    #endif
}

// Simple functions for getting some uniform / shader storage attached to a shader.

// If offset < 0 then its a buffer replacement/creation.
// If buf == nullptr then it is always creation. 
BufferObject *UpdateBufferObject(BufferObject *buf, const void *data, size_t len, ptrdiff_t offset,
                                 bool ssbo, bool dyn) {
    #ifndef PLATFORM_WINNIX
        // UBO's are in ES 3.0, not sure why OS X doesn't have them
        return nullptr;
    #else
        LOBSTER_FRAME_PROFILE_THIS_SCOPE;
        LOBSTER_FRAME_PROFILE_GPU;
        if (len > size_t(ssbo ? max_ssbo : max_ubo)) {
            return 0;
        }
        auto type = ssbo ? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER;
        if (!buf) {
            assert(offset < 0);
            auto bo = GenBO_("UpdateBufferObject", type, len, data, dyn);
            return new BufferObject(bo, type, len, dyn);
		} else {
            GL_CALL(glBindBuffer(type, buf->bo));
            // We're going to re-upload the buffer.
            // See this for what is fast:
            // https://xeolabs.com/pdfs/OpenGLInsights.pdf chapter 28: Asynchronous Buffer Transfers
            // https://thothonegan.tumblr.com/post/135193767243/glbuffersubdata-vs-glbufferdata
            if (offset < 0) {
                LOBSTER_FRAME_PROFILE_THIS_SCOPE;
                // Whole buffer.
                if (false && len == buf->size) {
                    // Is this faster than glBufferData if same size?
                    // Actually, this might cause *more* sync issues than glBufferData.
                    GL_CALL(glBufferSubData(type, 0, len, data));
                } else {
                    auto drawt = buf->dyn ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
                    // We can "orphan" the buffer before uploading, that way if a draw
                    // call is still using it, we won't have to sync.
                    // This doesn't actually seem faster in testing sofar:
                    // supposedly drivers do this for you nowadays.
                    //glBufferData(type, len, nullptr, drawt);
                    GL_CALL(glBufferData(type, len, data, drawt));
                    buf->size = len;
                }
            } else {
                LOBSTER_FRAME_PROFILE_THIS_SCOPE;
                // Partial buffer.
                GL_CALL(glBufferSubData(type, offset, len, data));
            }
            return buf;
        }
    #endif
}

// Note that bo_binding_point_index is assigned automatically based on unique block names.
// You can also specify these in the shader using `binding=`, but GL doesn't seem to have a way
// to retrieve these programmatically.
bool BindBufferObject(Shader *sh, BufferObject *buf, string_view_nt uniformblockname) {
    #ifndef PLATFORM_WINNIX
        // UBO's are in ES 3.0, not sure why OS X doesn't have them
        return false;
    #else
        LOBSTER_FRAME_PROFILE_THIS_SCOPE;
        LOBSTER_FRAME_PROFILE_GPU;
        if (!sh || !glGetProgramResourceIndex || !glShaderStorageBlockBinding || !glBindBufferBase ||
            !glUniformBlockBinding || !glGetUniformBlockIndex) {
            return false;
        }
        sh->Activate();
        int bo_binding_point_index = 0;
        uint32_t idx = GL_INVALID_INDEX;
        auto it = sh->ubomap.find(uniformblockname.sv);
        if (it == sh->ubomap.end()) {
            LOBSTER_FRAME_PROFILE_THIS_SCOPE;
            bo_binding_point_index = sh->binding_point_index_alloc++;
            idx = buf->type == GL_SHADER_STORAGE_BUFFER
                      ? glGetProgramResourceIndex(sh->program, GL_SHADER_STORAGE_BLOCK,
                                                  uniformblockname.c_str())
                      : glGetUniformBlockIndex(sh->program, uniformblockname.c_str());
            if (idx == GL_INVALID_INDEX) {
                return false;
            }
            // FIXME: if the BufferObject gets deleted before the shader, then bo is dangling.
            // This is probably benign-ish since OpenGL is probably tolerant of deleted buffers
            // still being bound?
            // If not, must allow Shader to inc refc of BufferObject.
            sh->ubomap[string(uniformblockname.sv)] = { bo_binding_point_index, idx };
		} else {
            LOBSTER_FRAME_PROFILE_THIS_SCOPE;
            bo_binding_point_index = it->second.bpi;
            idx = it->second.idx;
        }
        // Bind to shader.
        {
            LOBSTER_FRAME_PROFILE_THIS_SCOPE;
            // Support for unbinding this way removed in GL 3.1?
            GL_CALL(glBindBuffer(buf->type, 0));
            // FIXME: this causes:
            // GLDEBUG: GL_INVALID_OPERATION error generated. Buffer name does not refer to an buffer object generated by OpenGL.
            // when this is called from BindAsSSBO a second time with the same bo id on a different shader.
            // See raytrace_compute_sceneubo_thinstack_points_min.lobster
            GL_CALL(glBindBufferBase(buf->type, bo_binding_point_index, buf->bo));
            if (buf->type == GL_SHADER_STORAGE_BUFFER) {
                GL_CALL(glShaderStorageBlockBinding(sh->program, idx, bo_binding_point_index));
            } else {
                GL_CALL(glUniformBlockBinding(sh->program, idx, bo_binding_point_index));
            }
        }
        return true;
    #endif
}

bool CopyBufferObjects(BufferObject *src, BufferObject *dst, ptrdiff_t srcoffset,
                       ptrdiff_t dstoffset, size_t len) {
    #ifdef PLATFORM_WINNIX
        LOBSTER_FRAME_PROFILE_THIS_SCOPE;
        LOBSTER_FRAME_PROFILE_GPU;
        GL_CALL(glBindBuffer(GL_COPY_READ_BUFFER, src->bo));
        GL_CALL(glBindBuffer(GL_COPY_WRITE_BUFFER, dst->bo));
        GL_CALL(glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcoffset, dstoffset, len));
        // Unbind
        GL_CALL(glBindBuffer(GL_COPY_READ_BUFFER, 0));
        GL_CALL(glBindBuffer(GL_COPY_WRITE_BUFFER, 0));
        return true;
    #endif
    return false;
}

bool Shader::DumpBinary(string_view filename, bool stripnonascii) {
  #ifdef PLATFORM_WINNIX
    if (!glGetProgramBinary) return false;
  #endif
  #ifndef __EMSCRIPTEN__
    int len = 0;
    GL_CALL(glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &len));
    string buf;
    buf.resize(len);
    GLenum format = 0;
    GL_CALL(glGetProgramBinary(program, len, nullptr, &format, buf.data()));
    if (stripnonascii) {
      buf.erase(remove_if(buf.begin(), buf.end(), [](char c) {
        return (c < ' ' || c > '~') && c != '\n' && c != '\t';
      }), buf.end());
    }
    return WriteFile(filename, true, buf, false);
  #else
    return false;
  #endif
}
