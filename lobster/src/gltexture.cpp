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

#define STB_IMAGE_IMPLEMENTATION
#ifdef _WIN32
    #pragma warning(push)
    #pragma warning(disable: 4244)
#endif
#include "stb/stb_image.h"
#ifdef _WIN32
    #pragma warning(pop)
#endif

#ifndef __EMSCRIPTEN__
const int nummultisamples = 4;
#endif

Texture CreateTexture(const uchar *buf, const int *dim, int tf) {
    uint id;
    GL_CALL(glGenTextures(1, &id));
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
    auto internalformat = tf & TF_SINGLE_CHANNEL ? GL_R8 : GL_RGBA8;
    auto bufferformat = tf & TF_SINGLE_CHANNEL ? GL_RED : GL_RGBA;
    auto buffersize = tf & TF_SINGLE_CHANNEL ? sizeof(uchar) : sizeof(byte4);
    auto buffercomponent = GL_UNSIGNED_BYTE;
    if ((tf & TF_SINGLE_CHANNEL) && (dim[0] & 0x3)) {
        GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));  // Defaults to 4.
    }
    if (tf & TF_FLOAT) {
        #ifdef PLATFORM_WINNIX
            internalformat = tf & TF_SINGLE_CHANNEL ? GL_R32F : GL_RGBA32F;
            bufferformat = internalformat;
            buffersize = tf & TF_SINGLE_CHANNEL ? sizeof(float) : sizeof(float4);
            buffercomponent = GL_FLOAT;
        #else
            assert(false);  // buf points to float data, which we don't support.
        #endif
    }
    if (tf & TF_DEPTH) {
        internalformat = GL_DEPTH_COMPONENT32F;
        bufferformat = GL_DEPTH_COMPONENT;
        buffersize = sizeof(float);
        buffercomponent = GL_FLOAT;
    }
    if (tf & TF_MULTISAMPLE) {
        #ifdef PLATFORM_WINNIX
            GL_CALL(glTexImage2DMultisample(teximagetype, nummultisamples, internalformat,
                                            dim[0], dim[1], true));
        #else
            assert(false);
        #endif
    } else if(tf & TF_3D) {
		#ifndef __EMSCRIPTEN__
			int mipl = 0;
			for (auto d = int3(dim); tf & TF_BUFFER_HAS_MIPS ? d.volume() : !mipl; d /= 2) {
				GL_CALL(glTexImage3D(textype, mipl, internalformat, d.x, d.y, d.z, 0,
									 bufferformat, buffercomponent, buf));
				mipl++;
				buf += d.volume() * buffersize;
			}
		#else
			assert(false);
		#endif
    } else {
        int mipl = 0;
        for (auto d = int2(dim); tf & TF_BUFFER_HAS_MIPS ? d.volume() : !mipl; d /= 2) {
            for (int i = 0; i < texnumfaces; i++)
                GL_CALL(glTexImage2D(teximagetype + i, mipl, internalformat, d.x, d.y, 0,
                                     bufferformat, buffercomponent, buf));
            mipl++;
            buf += d.volume() * buffersize;
        }
    }
    if (!(tf & TF_NOMIPMAP) && !(tf & TF_BUFFER_HAS_MIPS)) {
        #ifdef PLATFORM_WINNIX
        if (glGenerateMipmap)     // only exists in 3.0 contexts and up.
        #endif
            GL_CALL(glGenerateMipmap(textype));
    }
    GL_CALL(glBindTexture(textype, 0));
    return Texture(id, int3(dim));
}

Texture CreateTextureFromFile(const char *name, int tf) {
    tf &= ~TF_FLOAT;  // Not supported yet.
    string fbuf;
    if (LoadFile(name, &fbuf) < 0)
        return Texture();
    int2 dim;
    int comp;
    auto buf = stbi_load_from_memory((uchar *)fbuf.c_str(), (int)fbuf.length(), &dim.x, &dim.y,
                                     &comp, 4);
    if (!buf)
        return Texture();
    auto tex = CreateTexture(buf, dim.data(), tf);
    stbi_image_free(buf);
    return tex;
}

Texture CreateBlankTexture(const int2 &size, const float4 &color, int tf) {
    if (tf & TF_MULTISAMPLE) {
        return CreateTexture(nullptr, size.data(), tf);  // No buffer required.
    } else {
        auto sz = tf & TF_FLOAT ? sizeof(float4) : sizeof(byte4);
        auto buf = new uchar[size.x * size.y * sz];
        for (int y = 0; y < size.y; y++) for (int x = 0; x < size.x; x++) {
            auto idx = y * size.x + x;
            if (tf & TF_FLOAT) ((float4 *)buf)[idx] = color;
            else               ((byte4  *)buf)[idx] = quantizec(color);
        }
        auto tex = CreateTexture(buf, size.data(), tf);
        delete[] buf;
        return tex;
    }
}

void DeleteTexture(Texture &tex) {
    if (tex.id) GL_CALL(glDeleteTextures(1, &tex.id));
    tex.id = 0;
}

void SetTexture(int textureunit, const Texture &tex, int tf) {
    GL_CALL(glActiveTexture(GL_TEXTURE0 + textureunit));
    GL_CALL(glBindTexture(tf & TF_CUBEMAP ? GL_TEXTURE_CUBE_MAP
                                          : (tf & TF_3D ? GL_TEXTURE_3D : GL_TEXTURE_2D), tex.id));
}

uchar *ReadTexture(const Texture &tex) {
    #ifndef PLATFORM_ES3
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex.id));
        auto pixels = new uchar[tex.size.x * tex.size.y * 4];
        GL_CALL(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
        return pixels;
    #else
        return nullptr;
    #endif
}

void SetImageTexture(uint textureunit, const Texture &tex, int tf) {
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

uint CreateFrameBuffer(const Texture &tex, int tf) {
	#ifdef PLATFORM_WINNIX
		if (!glGenFramebuffers)
			return 0;
	#endif
	uint fb = 0;
    GL_CALL(glGenFramebuffers(1, &fb));
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
static uint fb = 0;
static uint rb = 0;
static Texture retex;  // Texture to resolve to at the end when fb refers to a multisample texture.
static int retf = 0;
static bool hasdepthtex = false;
#endif

bool SwitchToFrameBuffer(const Texture &tex, bool depth, int tf, const Texture &resolvetex,
                         const Texture &depthtex) {
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
			GL_CALL(glDeleteRenderbuffers(1, &rb));
			rb = 0;
		}
		if (fb) {
			if (retex.id) {
				uint refb = CreateFrameBuffer(retex, retf);
				GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, refb));
				GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb));
				GL_CALL(glBlitFramebuffer(0, 0, retex.size.x, retex.size.y,
								          0, 0, retex.size.x, retex.size.y,
                                          GL_COLOR_BUFFER_BIT, GL_NEAREST));
				GL_CALL(glDeleteFramebuffers(1, &refb));
				retex = Texture();
				retf = 0;
			}
			GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
			GL_CALL(glDeleteFramebuffers(1, &fb));
			fb = 0;
		}
		if (!tex.id) {
			OpenGLFrameStart(tex.size.xy());
			Set2DMode(tex.size.xy(), true, depth);
			return true;
		}
		fb = CreateFrameBuffer(tex, tf);
		if (depth) {
            if (depthtex.id) {
                GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                               depthtex.id, 0));
                hasdepthtex = true;
            } else {
                GL_CALL(glGenRenderbuffers(1, &rb));
                GL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, rb));
                if (tf & TF_MULTISAMPLE) {
                    GL_CALL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, nummultisamples,
                                                             GL_DEPTH_COMPONENT24, tex.size.x,
                                                             tex.size.y));
                } else {
                    GL_CALL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                                  tex.size.x, tex.size.y));
                }
                GL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                  GL_RENDERBUFFER, rb));
            }
		}
		retex = resolvetex;
		retf = tf & ~TF_MULTISAMPLE;
		OpenGLFrameStart(tex.size.xy());
		Set2DMode(tex.size.xy(), false, depth);  // Have to use rh mode here, otherwise texture is filled flipped.
        auto stat = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		auto ok = stat == GL_FRAMEBUFFER_COMPLETE;
		assert(ok);
		return ok;
	#else
		return false;
	#endif
}

