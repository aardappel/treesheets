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

#define STB_IMAGE_IMPLEMENTATION
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4244)
#endif
#include "stb/stb_image.h"
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#ifndef __EMSCRIPTEN__
const int nummultisamples = 4;
#endif

OwnedTexture::~OwnedTexture() {
    DeleteTexture(t);
}

Texture CreateTexture(string_view name, const uint8_t *buf, int3 dim, int tf) {
    LOBSTER_FRAME_PROFILE_THIS_FUNCTION;
    int id;
    GL_CALL(glGenTextures(1, (GLuint *)&id));
    assert(id);
    GLenum textype =
        #ifdef PLATFORM_WINNIX
        tf & TF_MULTISAMPLE ? GL_TEXTURE_2D_MULTISAMPLE :
        #endif
        (tf & TF_3D ? GL_TEXTURE_3D : GL_TEXTURE_2D);
    GLenum teximagetype = textype;
    textype         = tf & TF_CUBEMAP ? GL_TEXTURE_CUBE_MAP            : textype;
    teximagetype    = tf & TF_CUBEMAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : teximagetype;
    int texnumfaces = tf & TF_CUBEMAP ? 6                              : 1;
    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(textype, id));
    auto wrap = tf & TF_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    GL_CALL(glTexParameteri(textype, GL_TEXTURE_WRAP_S, wrap));
    GL_CALL(glTexParameteri(textype, GL_TEXTURE_WRAP_T, wrap));
    if(tf & TF_3D) GL_CALL(glTexParameteri(textype, GL_TEXTURE_WRAP_R, wrap));
    GL_CALL(glTexParameteri(textype, GL_TEXTURE_MAG_FILTER,
                            tf & TF_NEAREST_MAG ? GL_NEAREST : GL_LINEAR));
    GL_CALL(glTexParameteri(textype, GL_TEXTURE_MIN_FILTER,
        tf & TF_NOMIPMAP ? (tf & TF_NEAREST_MIN ? GL_NEAREST : GL_LINEAR)
                         : (tf & TF_NEAREST_MIN ? GL_NEAREST_MIPMAP_NEAREST
                                                : GL_LINEAR_MIPMAP_LINEAR)));
    //if (mipmap) glTexParameteri(textype, GL_GENERATE_MIPMAP, GL_TRUE);
    auto internalformat = tf & TF_SINGLE_CHANNEL
        ? GL_R8
        : (IsSRGBMode() ? GL_SRGB8_ALPHA8 : GL_RGBA8);
    auto bufferformat = tf & TF_SINGLE_CHANNEL ? GL_RED : GL_RGBA;
    auto elemsize = tf & TF_SINGLE_CHANNEL ? sizeof(uint8_t) : sizeof(byte4);
    auto buffercomponent = GL_UNSIGNED_BYTE;
    if ((tf & TF_SINGLE_CHANNEL) && (dim.x & 0x3)) {
        GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));  // Defaults to 4.
    }
    if (tf & TF_FLOAT) {
        #ifdef PLATFORM_WINNIX
            internalformat = tf & TF_SINGLE_CHANNEL ? GL_R32F : GL_RGBA32F;
            bufferformat = tf & TF_SINGLE_CHANNEL ? GL_RED : GL_RGBA;
            elemsize = tf & TF_SINGLE_CHANNEL ? sizeof(float) : sizeof(float4);
            buffercomponent = GL_FLOAT;
        #else
            assert(false);  // buf points to float data, which we don't support.
        #endif
    }
    if (tf & TF_DEPTH) {
        internalformat = GL_DEPTH_COMPONENT32F;
        bufferformat = GL_DEPTH_COMPONENT;
        elemsize = sizeof(float);
        buffercomponent = GL_FLOAT;
    }
    if (tf & TF_MULTISAMPLE) {
        #ifdef PLATFORM_WINNIX
            GL_CALL(glTexImage2DMultisample(teximagetype, nummultisamples, internalformat,
                                            dim.x, dim.y, true));
        #else
            assert(false);
        #endif
    } else if(tf & TF_3D) {
		#ifndef __EMSCRIPTEN__
			int mipl = 0;
			for (auto d = dim; tf & TF_BUFFER_HAS_MIPS ? d.volume() : !mipl; d /= 2) {
				GL_CALL(glTexImage3D(textype, mipl, internalformat, d.x, d.y, d.z, 0,
									 bufferformat, buffercomponent, buf));
				mipl++;
				buf += d.volume() * elemsize;
			}
		#else
			assert(false);
		#endif
    } else {
        int mipl = 0;
        for (auto d = dim.xy(); tf & TF_BUFFER_HAS_MIPS ? d.volume() : !mipl; d /= 2) {
            for (int i = 0; i < texnumfaces; i++) {
                GL_CALL(glTexImage2D(teximagetype + i, mipl, internalformat, d.x, d.y, 0,
                                     bufferformat, buffercomponent, buf));
                buf += d.volume() * elemsize;
            }
            mipl++;
        }
    }
    if (!(tf & TF_NOMIPMAP) && !(tf & TF_BUFFER_HAS_MIPS)) {
        #ifdef PLATFORM_WINNIX
        if (glGenerateMipmap)     // only exists in 3.0 contexts and up.
        #endif
            GL_CALL(glGenerateMipmap(textype));
    }
    GL_CALL(glBindTexture(textype, 0));
    GL_NAME(GL_TEXTURE, id, name);
    return Texture(id, dim, int(elemsize));
}

Texture CreateTextureFromFile(string_view name, int tf) {
    tf &= ~TF_FLOAT;  // Not supported yet.
    string fbuf;
    vector<uint8_t *> bufs;
    Texture tex;
    int3 adim = int3_0;
    static const char *cubesides[6] = { "_ft", "_bk", "_up", "_dn", "_rt", "_lf" };
    for (int i = 0; i < (tf & TF_CUBEMAP ? 6 : 1); i++) {
        auto fn = string(name);
        if (tf & TF_CUBEMAP) {
            auto pos = fn.find_last_of('.');
            if (pos != string::npos) {
                fn.insert(pos, cubesides[i]);
            }
        }
        if (LoadFile(fn, &fbuf) < 0)
            goto out;
        int3 dim(0);
        int comp;
        auto buf = stbi_load_from_memory((uint8_t *)fbuf.c_str(), (int)fbuf.length(), &dim.x,
                                         &dim.y, &comp, 4);
        if (!buf)
            goto out;
        bufs.push_back(buf);
        if (i && dim != adim)
            goto out;
        adim = dim;
    }
    if (tf & TF_CUBEMAP) {
        auto bsize = adim.x * adim.y * 4;
        auto buf = (uint8_t *)malloc(bsize * 6);
        for (int i = 0; i < 6; i++) {
            memcpy(buf + bsize * i, bufs[i], bsize);
        }
        tex = CreateTexture(name, buf, adim, tf);
        free(buf);
    } else {
        tex = CreateTexture(name, bufs[0], adim, tf);
    }
    out:
    for (auto b : bufs) stbi_image_free(b);
    return tex;
}

uint8_t *LoadImageFile(string_view fn, int2 &dim) {
    string fbuf;
    if (LoadFile(fn, &fbuf) < 0) return nullptr;
    int comp;
    return stbi_load_from_memory((uint8_t *)fbuf.c_str(), (int)fbuf.length(), &dim.x, &dim.y,
                                 &comp, 4);
}

void FreeImageFromFile(uint8_t *img) {
    stbi_image_free(img);
}

Texture CreateBlankTexture(string_view name, const int2 &size, const float4 &color, int tf) {
    if (tf & TF_MULTISAMPLE) {
        return CreateTexture(name, nullptr, int3(size, 0), tf);  // No buffer required.
    } else {
        auto sz = tf & TF_FLOAT ? sizeof(float4) : sizeof(byte4);
        if (tf & TF_CUBEMAP) sz *= 6;
        auto buf = new uint8_t[size.x * size.y * sz];
        for (int y = 0; y < size.y; y++) for (int x = 0; x < size.x; x++) {
            auto idx = y * size.x + x;
            if (tf & TF_FLOAT) ((float4 *)buf)[idx] = color;
            else               ((byte4  *)buf)[idx] = quantizec(color);
        }
        auto tex = CreateTexture(name, buf, int3(size, 0), tf);
        delete[] buf;
        return tex;
    }
}

void DeleteTexture(Texture &tex) {
    if (tex.id) GL_CALL(glDeleteTextures(1, (GLuint *)&tex.id));
    tex.id = 0;
}

bool SetTexture(int textureunit, const Texture &tex, int tf) {
    GL_CALL(glActiveTexture(GL_TEXTURE0 + textureunit));
    glBindTexture(tf & TF_CUBEMAP ? GL_TEXTURE_CUBE_MAP
                                          : (tf & TF_3D ? GL_TEXTURE_3D : GL_TEXTURE_2D), tex.id);
    // glBindTexture can fail if the wrong flags are passed for the texture.
    return glGetError() != 0;
}

uint8_t *ReadTexture(const Texture &tex) {
    #ifndef PLATFORM_ES3
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex.id));
        auto pixels = new uint8_t[tex.size.x * tex.size.y * 4];
        GL_CALL(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
        return pixels;
    #else
        return nullptr;
    #endif
}

void SetImageTexture(int textureunit, const Texture &tex, int tf) {
    #ifdef PLATFORM_WINNIX
        if (glBindImageTexture)
            GL_CALL(glBindImageTexture(textureunit, tex.id, 0, GL_TRUE, 0,
                               tf & TF_WRITEONLY
                                   ? GL_WRITE_ONLY
                                   : (tf & TF_READWRITE ? GL_READ_WRITE : GL_READ_ONLY),
                               tf & TF_FLOAT ? GL_RGBA32F : GL_RGBA8));
    #else
        assert(false);
    #endif
}

// from 2048 on older GLES2 devices and very old PCs to 16384 on the latest PC cards
int MaxTextureSize() { int mts = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts); return mts; }

int CreateFrameBuffer(const Texture &tex, int tf) {
	#ifdef PLATFORM_WINNIX
		if (!glGenFramebuffers)
			return 0;
	#endif
	int fb = 0;
    GL_CALL(glGenFramebuffers(1, (GLuint *)&fb));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fb));
    auto target =
        #ifdef PLATFORM_WINNIX
            tf & TF_MULTISAMPLE ? GL_TEXTURE_2D_MULTISAMPLE :
        #endif
        GL_TEXTURE_2D;
    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, tex.id, 0));
    return fb;
}

#ifndef __EMSCRIPTEN__
static int fb = 0;
static int rb = 0;
static Texture retex;  // Texture to resolve to at the end when fb refers to a multisample texture.
static int retf = 0;
static bool hasdepthtex = false;
static int2 framebuffersize(0);
#endif

int2 GetFrameBufferSize(const int2 &screensize) {
    #ifndef __EMSCRIPTEN__
    if (fb) return framebuffersize;
    #endif
    return screensize;
}

bool SwitchToFrameBuffer(const Texture &tex, int2 orig_screensize, bool depth, int tf,
                         const Texture &resolvetex, const Texture &depthtex) {
    LOBSTER_FRAME_PROFILE_THIS_FUNCTION;
	#ifdef PLATFORM_WINNIX
		if (!glGenRenderbuffers)
			return false;
	#endif
	#ifndef __EMSCRIPTEN__
        if (hasdepthtex) {
            GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                           0, 0));
            hasdepthtex = false;
        }
        if (rb) {
			GL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
			GL_CALL(glDeleteRenderbuffers(1, (GLuint *)&rb));
			rb = 0;
		}
		if (fb) {
			if (retex.id) {
				int refb = CreateFrameBuffer(retex, retf);
				GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, refb));
				GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb));
				GL_CALL(glBlitFramebuffer(0, 0, retex.size.x, retex.size.y,
								          0, 0, retex.size.x, retex.size.y,
                                          GL_COLOR_BUFFER_BIT, GL_NEAREST));
				GL_CALL(glDeleteFramebuffers(1, (GLuint *)&refb));
				retex = Texture();
				retf = 0;
			}
			GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
			GL_CALL(glDeleteFramebuffers(1, (GLuint *)&fb));
			fb = 0;
            framebuffersize = int2_0;
		}
		if (!tex.id) {
			OpenGLFrameStart(orig_screensize);
			//Set2DMode(orig_screensize, true, depth);
			return true;
		}
		fb = CreateFrameBuffer(tex, tf);
        framebuffersize = tex.size.xy();
		if (depth) {
            if (depthtex.id) {
                GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                               depthtex.id, 0));
                hasdepthtex = true;
            } else {
                GL_CALL(glGenRenderbuffers(1, (GLuint *)&rb));
                GL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, rb));
                if (tf & TF_MULTISAMPLE) {
                    GL_CALL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, nummultisamples,
                                                             GL_DEPTH_COMPONENT24, framebuffersize.x,
                                                             framebuffersize.y));
                } else {
                    GL_CALL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                                  framebuffersize.x, framebuffersize.y));
                }
                GL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                  GL_RENDERBUFFER, rb));
            }
		}
		retex = resolvetex;
		retf = tf & ~TF_MULTISAMPLE;
		OpenGLFrameStart(framebuffersize);
		//Set2DMode(framebuffersize, false, depth);  // Have to use rh mode here, otherwise texture is filled flipped.
        auto stat = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		auto ok = stat == GL_FRAMEBUFFER_COMPLETE;
		assert(ok);
		return ok;
	#else
		return false;
	#endif
}

