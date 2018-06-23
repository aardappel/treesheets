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

// OpenGL platform definitions

#ifdef __APPLE__
    #include "TargetConditionals.h"
    #ifdef __IOS__
        //#include <SDL_opengles2.h>
        #include <OpenGLES/ES3/gl.h>
        #include <OpenGLES/ES3/glext.h>
    #else
        #include <OpenGL/gl3.h>
    #endif
#elif defined(__ANDROID__)
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
	#include <GLES3/gl3.h>
    #include <GLES3/gl3ext.h>
#elif defined(__EMSCRIPTEN__)
	#define GL_GLEXT_PROTOTYPES
	#include <GLES/gl.h>
	#include <GLES/glext.h>
	#include <GLES3/gl3.h>
	//#include "SDL/SDL_opengl.h"
#else   // _WIN32 & Linux
    #ifdef _WIN32
        #define VC_EXTRALEAN
        #define WIN32_LEAN_AND_MEAN
        #define NOMINMAX
        #include <windows.h>
    #endif
    #include <GL/gl.h>
    #include <GL/glext.h>
    #ifdef _WIN32
        #define GLBASEEXTS \
            GLEXT(PFNGLACTIVETEXTUREARBPROC       , glActiveTexture            , 1) \
            GLEXT(PFNGLTEXIMAGE3DPROC             , glTexImage3D               , 1)
    #else
        #define GLBASEEXTS
    #endif
    #define GLEXTS \
        GLEXT(PFNGLGENBUFFERSARBPROC                 , glGenBuffers                    , 1) \
        GLEXT(PFNGLBINDBUFFERARBPROC                 , glBindBuffer                    , 1) \
        GLEXT(PFNGLMAPBUFFERARBPROC                  , glMapBuffer                     , 1) \
        GLEXT(PFNGLUNMAPBUFFERARBPROC                , glUnmapBuffer                   , 1) \
        GLEXT(PFNGLBUFFERDATAARBPROC                 , glBufferData                    , 1) \
        GLEXT(PFNGLBUFFERSUBDATAARBPROC              , glBufferSubData                 , 1) \
        GLEXT(PFNGLDELETEBUFFERSARBPROC              , glDeleteBuffers                 , 1) \
        GLEXT(PFNGLGETBUFFERSUBDATAARBPROC           , glGetBufferSubData              , 1) \
        GLEXT(PFNGLVERTEXATTRIBPOINTERARBPROC        , glVertexAttribPointer           , 1) \
        GLEXT(PFNGLENABLEVERTEXATTRIBARRAYARBPROC    , glEnableVertexAttribArray       , 1) \
        GLEXT(PFNGLDISABLEVERTEXATTRIBARRAYARBPROC   , glDisableVertexAttribArray      , 1) \
        GLEXT(PFNGLGENVERTEXARRAYSPROC               , glGenVertexArrays               , 1) \
        GLEXT(PFNGLBINDVERTEXARRAYPROC               , glBindVertexArray               , 1) \
        GLEXT(PFNGLDELETEVERTEXARRAYSPROC            , glDeleteVertexArrays            , 1) \
        GLEXT(PFNGLCREATEPROGRAMPROC                 , glCreateProgram                 , 1) \
        GLEXT(PFNGLDELETEPROGRAMPROC                 , glDeleteProgram                 , 1) \
        GLEXT(PFNGLDELETESHADERPROC                  , glDeleteShader                  , 1) \
        GLEXT(PFNGLUSEPROGRAMPROC                    , glUseProgram                    , 1) \
        GLEXT(PFNGLCREATESHADERPROC                  , glCreateShader                  , 1) \
        GLEXT(PFNGLSHADERSOURCEPROC                  , glShaderSource                  , 1) \
        GLEXT(PFNGLCOMPILESHADERPROC                 , glCompileShader                 , 1) \
        GLEXT(PFNGLGETPROGRAMIVARBPROC               , glGetProgramiv                  , 1) \
        GLEXT(PFNGLGETSHADERIVPROC                   , glGetShaderiv                   , 1) \
        GLEXT(PFNGLGETPROGRAMINFOLOGPROC             , glGetProgramInfoLog             , 1) \
        GLEXT(PFNGLGETSHADERINFOLOGPROC              , glGetShaderInfoLog              , 1) \
        GLEXT(PFNGLATTACHSHADERPROC                  , glAttachShader                  , 1) \
        GLEXT(PFNGLLINKPROGRAMARBPROC                , glLinkProgram                   , 1) \
        GLEXT(PFNGLGETUNIFORMLOCATIONARBPROC         , glGetUniformLocation            , 1) \
        GLEXT(PFNGLUNIFORM1FARBPROC                  , glUniform1f                     , 1) \
        GLEXT(PFNGLUNIFORM2FARBPROC                  , glUniform2f                     , 1) \
        GLEXT(PFNGLUNIFORM3FARBPROC                  , glUniform3f                     , 1) \
        GLEXT(PFNGLUNIFORM4FARBPROC                  , glUniform4f                     , 1) \
        GLEXT(PFNGLUNIFORM1FVARBPROC                 , glUniform1fv                    , 1) \
        GLEXT(PFNGLUNIFORM2FVARBPROC                 , glUniform2fv                    , 1) \
        GLEXT(PFNGLUNIFORM3FVARBPROC                 , glUniform3fv                    , 1) \
        GLEXT(PFNGLUNIFORM4FVARBPROC                 , glUniform4fv                    , 1) \
        GLEXT(PFNGLUNIFORM1IARBPROC                  , glUniform1i                     , 1) \
        GLEXT(PFNGLUNIFORMMATRIX2FVARBPROC           , glUniformMatrix2fv              , 1) \
        GLEXT(PFNGLUNIFORMMATRIX3FVARBPROC           , glUniformMatrix3fv              , 1) \
        GLEXT(PFNGLUNIFORMMATRIX4FVARBPROC           , glUniformMatrix4fv              , 1) \
        GLEXT(PFNGLUNIFORMMATRIX4FVARBPROC/*type*/   , glUniformMatrix3x4fv            , 1) \
        GLEXT(PFNGLBINDATTRIBLOCATIONARBPROC         , glBindAttribLocation            , 1) \
        GLEXT(PFNGLGETACTIVEUNIFORMARBPROC           , glGetActiveUniform              , 1) \
        GLEXT(PFNGLBINDRENDERBUFFERPROC              , glBindRenderbuffer              , 0) \
        GLEXT(PFNGLDELETERENDERBUFFERSPROC           , glDeleteRenderbuffers           , 0) \
        GLEXT(PFNGLBINDFRAMEBUFFERPROC               , glBindFramebuffer               , 0) \
        GLEXT(PFNGLDELETEFRAMEBUFFERSPROC            , glDeleteFramebuffers            , 0) \
        GLEXT(PFNGLGENFRAMEBUFFERSPROC               , glGenFramebuffers               , 0) \
        GLEXT(PFNGLFRAMEBUFFERTEXTURE2DPROC          , glFramebufferTexture2D          , 0) \
        GLEXT(PFNGLGENRENDERBUFFERSPROC              , glGenRenderbuffers              , 0) \
        GLEXT(PFNGLRENDERBUFFERSTORAGEPROC           , glRenderbufferStorage           , 0) \
        GLEXT(PFNGLFRAMEBUFFERRENDERBUFFERPROC       , glFramebufferRenderbuffer       , 0) \
        GLEXT(PFNGLCHECKFRAMEBUFFERSTATUSPROC        , glCheckFramebufferStatus        , 0) \
        GLEXT(PFNGLTEXIMAGE2DMULTISAMPLEPROC         , glTexImage2DMultisample         , 0) \
        GLEXT(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample, 0) \
        GLEXT(PFNGLBLITFRAMEBUFFERPROC               , glBlitFramebuffer               , 0) \
        GLEXT(PFNGLGENERATEMIPMAPEXTPROC             , glGenerateMipmap                , 0) \
        GLEXT(PFNGLDISPATCHCOMPUTEPROC               , glDispatchCompute               , 0) \
        GLEXT(PFNGLBINDIMAGETEXTUREPROC              , glBindImageTexture              , 0) \
        GLEXT(PFNGLGETPROGRAMRESOURCEINDEXPROC       , glGetProgramResourceIndex       , 0) \
        GLEXT(PFNGLSHADERSTORAGEBLOCKBINDINGPROC     , glShaderStorageBlockBinding     , 0) \
        GLEXT(PFNGLGETUNIFORMBLOCKINDEXPROC          , glGetUniformBlockIndex          , 0) \
        GLEXT(PFNGLUNIFORMBLOCKBINDINGPROC           , glUniformBlockBinding           , 0) \
        GLEXT(PFNGLGETPROGRAMBINARYPROC              , glGetProgramBinary              , 0) \
        GLEXT(PFNGLBINDBUFFERBASEPROC                , glBindBufferBase                , 0) \
        GLEXT(PFNGLMEMORYBARRIERPROC                 , glMemoryBarrier                 , 0) \
        GLEXT(PFNGLMAPBUFFERRANGEPROC                , glMapBufferRange                , 0)
    #define GLEXT(type, name, needed) extern type name;
        GLBASEEXTS
        GLEXTS
    #undef GLEXT
#endif
#if !defined(NDEBUG)
    #define LOG_GL_ERRORS
#endif
#ifdef LOG_GL_ERRORS
    #define GL_CHECK(what) LogGLError(__FILE__, __LINE__, what)
    #define GL_CALL(call) do { call; GL_CHECK(#call); } while (0)
#else
    #define GL_CHECK(what) (void)what
    #define GL_CALL(call) do { call; } while (0)
#endif
