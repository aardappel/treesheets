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
#include "lobster/sdlinterface.h"

unordered_map<string, Shader *> shadermap;

Shader *LookupShader(const char *name) {
    auto shi = shadermap.find(name);
    if (shi != shadermap.end()) return shi->second;
    return nullptr;
}

void ShaderShutDown() {
    for (auto &it : shadermap)
        delete it.second;
}

string GLSLError(uint obj, bool isprogram, const char *source) {
    GLint length = 0;
    if (isprogram) GL_CALL(glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &length));
    else           GL_CALL(glGetShaderiv (obj, GL_INFO_LOG_LENGTH, &length));
    if (length > 1) {
        GLchar *log = new GLchar[length];
        if (isprogram) GL_CALL(glGetProgramInfoLog(obj, length, &length, log));
        else           GL_CALL(glGetShaderInfoLog (obj, length, &length, log));
        string err = "GLSL ERROR: ";
        err += log;
        int i = 0;
        if (source) for (;;) {
            err += to_string(++i);
            err += ": ";
            const char *next = strchr(source, '\n');
            if (next) { err += string(source, next - source + 1); source = next + 1; }
            else { err += string(source) + "\n"; break; }
        }
        delete[] log;
        return err;
    }
    return "";
}

uint CompileGLSLShader(GLenum type, uint program, const GLchar *source, string &err)  {
    uint obj = glCreateShader(type);
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
    return 0;
}

string ParseMaterialFile(char *mbuf) {
    auto p = mbuf;
    string err;
    string last;
    string defines;
    string vfunctions, pfunctions, cfunctions, vertex, pixel, compute, vdecl, pdecl, csdecl, shader;
    string *accum = nullptr;
    auto word = [&]() {
        p += strspn(p, " \t\r");
        size_t len = strcspn(p, " \t\r\0");
        last = string(p, len);
        p += len;
    };
    auto finish = [&]() -> bool {
        if (!shader.empty()) {
            auto sh = new Shader();
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
                    header += string("#version ") + char(supported[0]) + char(supported[2]) +
                              char(supported[3]) + "\n";
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
            if (!err.empty())
                return true;
            shadermap[shader] = sh;
            shader.clear();
        }
        return false;
    };
    for (;;) {
        auto start = p;
        auto end = p + strcspn(p, "\n\0");
        bool eof = !*end;
        *end = 0;
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
                shader = last;
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
                    else if (last == "mvp")          decl += "uniform mat4 mvp;\n";
                    else if (last == "col")          decl += "uniform vec4 col;\n";
                    else if (last == "camera")       decl += "uniform vec3 camera;\n";
                    else if (last == "light1")       decl += "uniform vec3 light1;\n";
                    else if (last == "lightparams1") decl += "uniform vec2 lightparams1;\n";
                    else if (last == "texturesize")  decl += "uniform vec2 texturesize;\n";
                    // FIXME: Make configurable.
                    else if (last == "bones")        decl += "uniform vec4 bones[230];\n";
                    else if (last == "pointscale")   decl += "uniform float pointscale;\n";
                    else if (!strncmp(last.c_str(), "tex", 3)) {
                        auto p = last.c_str() + 3;
                        bool cubemap = false;
                        bool floatingp = false;
                        bool d3 = false;
                        if (!strncmp(p, "cube", 4)) {
                            p += 4;
                            cubemap = true;
                        }
                        if (!strncmp(p, "3d", 2)) {
                            p += 2;
                            d3 = true;
                        }
                        if (*p == 'f') {
                            p++;
                            floatingp = true;
                        }
                        auto unit = atoi(p);
                        if (accum == &compute) {
                            decl += "layout(binding = " + to_string(unit) + ", " +
                                    (floatingp ? "rgba32f" : "rgba8") + ") ";
                        }
                        decl += "uniform ";
                        decl += accum == &compute ? (cubemap ? "imageCube" : "image2D")
                                                  : (cubemap ? "samplerCube" : (d3 ? "sampler3D" :
                                                                                     "sampler2D"));
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
                    auto pos = strstr(last.c_str(), ":");
                    if (!pos) {
                        return "input " + last +
                               " doesn't specify number of components, e.g. anormal:3";
                    }
                    int comp = atoi(pos + 1);
                    if (comp <= 0 || comp > 4) {
                        return "input " + last + " can only use 1..4 components";
                    }
                    last = last.substr(0, pos - last.c_str());
                    string d = " vec" + to_string(comp) + " " + last + ";\n";
                    if (accum == &vertex) vdecl += "in" + d;
                    else { vdecl += "out" + d; pdecl += "in" + d; }
                }
            } else if (last == "LAYOUT") {
                word();
                auto xs = last;
                word();
                auto ys = last;
                csdecl += "layout(local_size_x = " + xs + ", local_size_y = " + ys + ") in;\n";
            } else if (last == "DEFINE") {
                word();
                auto def = last;
                word();
                auto val = last;
                if (!val.empty()) def += " " + val;
                defines += "#define " + def + "\n";
            } else {
                if (!accum)
                    return "GLSL code outside of FUNCTIONS/VERTEX/PIXEL block: " + string(start);
                *accum += start;
                *accum += "\n";
            }
        }
        if (eof) break;
        *end = '\n';
        p = end + 1;
    }
    finish();
    return err;
}

string LoadMaterialFile(const char *mfile) {
    string mbuf;
    if (LoadFile(mfile, &mbuf) < 0) return string("cannot load material file: ") + mfile;
    auto err = ParseMaterialFile((char *)mbuf.c_str());
    return err;
}

string Shader::Compile(const char *name, const char *vscode, const char *pscode) {
    program = glCreateProgram();
    string err;
    vs = CompileGLSLShader(GL_VERTEX_SHADER,   program, vscode, err);
    if (!vs) return string("couldn't compile vertex shader: ") + name + "\n" + err;
    ps = CompileGLSLShader(GL_FRAGMENT_SHADER, program, pscode, err);
    if (!ps) return string("couldn't compile pixel shader: ") + name + "\n" + err;
    GL_CALL(glBindAttribLocation(program, 0, "apos"));
    GL_CALL(glBindAttribLocation(program, 1, "anormal"));
    GL_CALL(glBindAttribLocation(program, 2, "atc"));
    GL_CALL(glBindAttribLocation(program, 3, "acolor"));
    GL_CALL(glBindAttribLocation(program, 4, "aweights"));
    GL_CALL(glBindAttribLocation(program, 5, "aindices"));
    Link(name);
    return "";
}

string Shader::Compile(const char *name, const char *cscode) {
    #ifdef PLATFORM_WINNIX
        program = glCreateProgram();
        string err;
        cs = CompileGLSLShader(GL_COMPUTE_SHADER, program, cscode, err);
        if (!cs) return string("couldn't compile compute shader: ") + name + "\n" + err;
        Link(name);
        return "";
    #else
        return "compute shaders not supported";
    #endif
}

void Shader::Link(const char *name) {
    GL_CALL(glLinkProgram(program));
    GLint status;
    GL_CALL(glGetProgramiv(program, GL_LINK_STATUS, &status));
    if (status != GL_TRUE) {
        GLSLError(program, true, nullptr);
        throw string("linking failed for shader: ") + name;
    }
    mvp_i          = glGetUniformLocation(program, "mvp");
    col_i          = glGetUniformLocation(program, "col");
    camera_i       = glGetUniformLocation(program, "camera");
    light1_i       = glGetUniformLocation(program, "light1");
    lightparams1_i = glGetUniformLocation(program, "lightparams1");
    texturesize_i  = glGetUniformLocation(program, "texturesize");
    bones_i        = glGetUniformLocation(program, "bones");
    pointscale_i   = glGetUniformLocation(program, "pointscale");
    Activate();
    for (int i = 0; i < MAX_SAMPLERS; i++) {
        auto is = to_string(i);
        auto loc = glGetUniformLocation(program, ("tex" + is).c_str());
        if (loc < 0) {
            loc = glGetUniformLocation(program, ("texcube" + is).c_str());
            if (loc < 0) loc = glGetUniformLocation(program, ("tex3d" + is).c_str());
        }
        if (loc >= 0) {
            glUniform1i(loc, i);
            max_tex_defined = i + 1;
        }
    }
}

Shader::~Shader() {
    if (program) GL_CALL(glDeleteProgram(program));
    if (ps) GL_CALL(glDeleteShader(ps));
    if (vs) GL_CALL(glDeleteShader(vs));
    if (cs) GL_CALL(glDeleteShader(cs));
}

// FIXME: unlikely to cause ABA problem, but still better to reset once per frame just in case.
static uint last_program = 0;

void Shader::Activate() {
    if (program != last_program) {
        GL_CALL(glUseProgram(program));
        last_program = program;
    }
}

void Shader::Set() {
    Activate();
    if (mvp_i >= 0) GL_CALL(glUniformMatrix4fv(mvp_i, 1, false,
                                               (view2clip * otransforms.object2view).data()));
    if (col_i >= 0) GL_CALL(glUniform4fv(col_i, 1, curcolor.begin()));
    if (camera_i >= 0) GL_CALL(glUniform3fv(camera_i, 1, otransforms.view2object[3].begin()));
    if (pointscale_i >= 0) GL_CALL(glUniform1f(pointscale_i, pointscale));
    if (lights.size() > 0) {
        if (light1_i >= 0)
            GL_CALL(glUniform3fv(light1_i, 1, (otransforms.view2object * lights[0].pos).begin()));
        if (lightparams1_i >= 0)
            GL_CALL(glUniform2fv(lightparams1_i, 1, lights[0].params.begin()));
    }
    if (texturesize_i >= 0)
        GL_CALL(glUniform2fv(texturesize_i, 1, float2(GetScreenSize()).begin()));
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

bool Shader::SetUniform(const char *name, const float *val, int components, int elements) {
    auto loc = glGetUniformLocation(program, name);
    if (loc < 0) return false;
    switch (components) {
        case 1: GL_CALL(glUniform1fv(loc, elements, val)); return true;
        case 2: GL_CALL(glUniform2fv(loc, elements, val)); return true;
        case 3: GL_CALL(glUniform3fv(loc, elements, val)); return true;
        case 4: GL_CALL(glUniform4fv(loc, elements, val)); return true;
        default: return false;
    }
}

bool Shader::SetUniformMatrix(const char *name, const float *val, int components, int elements) {
    auto loc = glGetUniformLocation(program, name);
    if (loc < 0) return false;
    switch (components) {
        case 4:  GL_CALL(glUniformMatrix2fv(loc, elements, false, val)); return true;
        case 9:  GL_CALL(glUniformMatrix3fv(loc, elements, false, val)); return true;
        case 12: GL_CALL(glUniformMatrix3x4fv(loc, elements, false, val)); return true;
        case 16: GL_CALL(glUniformMatrix4fv(loc, elements, false, val)); return true;
        default: return false;
    }
}

void DispatchCompute(const int3 &groups) {
    #ifdef PLATFORM_WINNIX
        if (glDispatchCompute) GL_CALL(glDispatchCompute(groups.x, groups.y, groups.z));
        // Make sure any imageStore/VBOasSSBO operations have completed.
        // Would be better to decouple this from DispatchCompute.
        if (glMemoryBarrier)
            GL_CALL(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT |
                                    GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT));
    #else
        assert(false);
    #endif
}

// Simple function for getting some uniform / shader storage attached to a shader. Should ideally
// be split up for more flexibility.
uint UniformBufferObject(Shader *sh, const void *data, size_t len, const char *uniformblockname,
                         bool ssbo) {
    GLuint bo = 0;
    #ifdef PLATFORM_WINNIX
        if (sh && glGetProgramResourceIndex && glShaderStorageBlockBinding && glBindBufferBase &&
                  glUniformBlockBinding && glGetUniformBlockIndex) {
            sh->Activate();
            auto idx = ssbo
                ? glGetProgramResourceIndex(sh->program, GL_SHADER_STORAGE_BLOCK, uniformblockname)
                : glGetUniformBlockIndex(sh->program, uniformblockname);

            GLint maxsize;
            // FIXME: call glGetInteger64v if we ever want buffers >2GB.
            if (ssbo) glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxsize);
            else glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxsize);
            if (idx != GL_INVALID_INDEX && len <= maxsize) {
                auto type = ssbo ? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER;
                bo = GenBO(type, 1, len, data);
                GL_CALL(glBindBuffer(type, 0));
                static GLuint bo_binding_point_index = 0;
                bo_binding_point_index++;  // FIXME: how do we allocate these properly?
                GL_CALL(glBindBufferBase(type, bo_binding_point_index, bo));
                if (ssbo) GL_CALL(glShaderStorageBlockBinding(sh->program, idx,
                                                              bo_binding_point_index));
                else GL_CALL(glUniformBlockBinding(sh->program, idx, bo_binding_point_index));
            }
        }
    #else
        // UBO's are in ES 3.0, not sure why OS X doesn't have them
    #endif
    return bo;
}

void BindVBOAsSSBO(uint bind_point_index, uint vbo) {
    #ifdef PLATFORM_WINNIX
        if (glBindBufferBase) {
            GL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bind_point_index, vbo));
        }
    #endif
}

bool Shader::Dump(const char *filename, bool stripnonascii) {
	#ifdef PLATFORM_WINNIX
		if (!glGetProgramBinary) return false;
	#endif
	#ifndef __EMSCRIPTEN__
		int len = 0;
		GL_CALL(glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &len));
		vector<char> buf;
		buf.resize(len);
		GLenum format = 0;
		GL_CALL(glGetProgramBinary(program, len, nullptr, &format, buf.data()));
		if (stripnonascii) {
			buf.erase(remove_if(buf.begin(), buf.end(), [](char c) {
				return (c < ' ' || c > '~') && c != '\n' && c != '\t';
			}), buf.end());
		}
		return WriteFile(filename, true, buf.data(), buf.size());
	#else
		return false;
	#endif
}
