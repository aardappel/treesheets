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

#include "lobster/natreg.h"

#include "lobster/glinterface.h"
#include "lobster/sdlinterface.h"

#include "lobster/engine.h"

using namespace lobster;

Primitive polymode = PRIM_FAN;
Shader *currentshader = NULL;
Shader *colorshader = NULL;
float3 lasthitsize = float3_0;
float3 lastframehitsize = float3_0;
bool graphics_initialized = false;

ResourceType mesh_type = { "mesh", [](void *m) {
    delete (Mesh *)m;
} };

ResourceType texture_type = { "texture", [](void *t) {
    auto tex = (Texture *)t;
    DeleteTexture(*tex);
    delete tex;
} };

Mesh &GetMesh(VM &vm, Value &res) {
    return *GetResourceDec<Mesh *>(vm, res, &mesh_type);
}
Texture GetTexture(VM &vm, const Value &res) {
    auto tex = GetResourceDec<Texture *>(vm, res, &texture_type);
    return tex ? *tex : Texture();
}

// Should be safe to call even if it wasn't initialized partially or at all.
// FIXME: move this elsewhere.
void GraphicsShutDown() {
    extern void SteamShutDown(); SteamShutDown();
    VRShutDown();
    extern void CleanPhysics(); CleanPhysics();
    extern void MeshGenClear(); MeshGenClear();
    extern void CubeGenClear(); CubeGenClear();
    extern void FontCleanup(); FontCleanup();
    extern void IMGUICleanup(); IMGUICleanup();
    ShaderShutDown();
    currentshader = NULL;
    colorshader = NULL;
    OpenGLCleanup();
    SDLSoundClose();
    SDLShutdown();
    // We don't set this to false on most platforms, as currently SDL doesn't like being
    // reinitialized
    #ifdef __ANDROID__
        // FIXME: really only allow this if the app has been killed
        graphics_initialized = false;
    #endif
}

bool GraphicsFrameStart() {
    extern void CullFonts(); CullFonts();
    extern void SteamUpdate(); SteamUpdate();
    bool cb = SDLFrame();
    lastframehitsize = lasthitsize;
    lasthitsize = float3_0;
    OpenGLFrameStart(GetScreenSize());
    Set2DMode(GetScreenSize(), true);
    currentshader = colorshader;
    return cb;
}

void TestGL(VM &vm) {
    if (!graphics_initialized)
        vm.BuiltinError("graphics system not initialized yet, call gl_window() first");
}

float2 localpos(const int2 &pos) {
    return (otransforms.view2object * float4(float3(float2(pos), 0), 1)).xyz().xy();
}
float2 localfingerpos(int i) {
    return localpos(GetFinger(i, false));
}

Value PushTransform(VM &vm, const float4x4 &forward, const float4x4 &backward, const Value &body) {
    if (body.True()) vm.PushAnyAsString(otransforms);
    AppendTransform(forward, backward);
    return body;
}

void PopTransform(VM &vm) {
    vm.PopAnyFromString(otransforms);
}

int GetSampler(VM &vm, Value &i) {
    if (i.ival() < 0 || i.ival() >= Shader::MAX_SAMPLERS)
        vm.BuiltinError("graphics: illegal texture unit");
    return i.intval();
}

Mesh *CreatePolygon(VM &vm, Value &vl) {
    TestGL(vm);
    auto len = vl.vval()->len;
    if (len < 3) vm.BuiltinError("polygon: must have at least 3 verts");
    vector<BasicVert> vbuf(len);
    for (int i = 0; i < len; i++) vbuf[i].pos = ValueToFLT<3>(vl.vval()->AtSt(i), vl.vval()->width);
    auto v1 = vbuf[1].pos - vbuf[0].pos;
    auto v2 = vbuf[2].pos - vbuf[0].pos;
    auto norm = normalize(cross(v2, v1));
    for (int i = 0; i < len; i++) {
        vbuf[i].norm = norm;
        vbuf[i].tc = vbuf[i].pos.xy();
        vbuf[i].col = byte4_255;
    }
    auto m = new Mesh(new Geometry(make_span(vbuf), "PNTC"), polymode);
    return m;
}

Value SetUniform(VM &vm, const Value &name, const float *data, int len, bool ignore_errors) {
    TestGL(vm);
    currentshader->Activate();
    auto ok = currentshader->SetUniform(name.sval()->strv(), data, len);
    if (!ok && !ignore_errors)
        vm.Error("failed to set uniform: " + name.sval()->strv());
    return Value(ok);
}

void AddGraphics(NativeRegistry &nfr) {

nfr("gl_window", "title,xs,ys,fullscreen,novsync,samples", "SIII?I?I?", "S?",
    "opens a window for OpenGL rendering. returns error string if any problems, nil"
    " otherwise.",
    [](VM &vm, Value &title, Value &xs, Value &ys, Value &fullscreen, Value &novsync,
                          Value &samples) {
        if (graphics_initialized)
            vm.BuiltinError("cannot call gl_window() twice");
        string err = SDLInit(title.sval()->strv(), int2(intp2(xs.ival(), ys.ival())),
                             fullscreen.ival() != 0, novsync.ival() == 0,
                             max(1, samples.intval()));
        if (err.empty()) {
            err = LoadMaterialFile("data/shaders/default.materials");
        }
        if (!err.empty()) {
            LOG_INFO(err);
            return Value(vm.NewString(err));
        }
        colorshader = LookupShader("color");
        assert(colorshader);
        LOG_INFO("graphics fully initialized...");
        graphics_initialized = true;
        return Value();
    });

nfr("gl_require_version", "major,minor", "II", "",
    "Call this before gl_window to request a certain version of OpenGL context."
            " Currently only works on win/nix, minimum is 3.2.",
    [](VM &, Value &major, Value &minor) {
        SDLRequireGLVersion(major.intval(), minor.intval());
        return Value();
    });

nfr("gl_load_materials", "materialdefs,inline", "SI?", "S?",
    "loads an additional materials file (data/shaders/default.materials is already loaded by default"
    " by gl_window()). if inline is true, materialdefs is not a filename, but the actual"
    " materials. returns error string if any problems, nil otherwise.",
    [](VM &vm, Value &fn, Value &isinline) {
        TestGL(vm);
        auto err = isinline.True() ? ParseMaterialFile(fn.sval()->strv())
                                   : LoadMaterialFile(fn.sval()->strv());
        return err[0] ? Value(vm.NewString(err)) : Value();
    });

nfr("gl_frame", "", "", "B",
    "advances rendering by one frame, swaps buffers, and collects new input events."
    " returns true if the closebutton on the window was pressed",
    [](VM &vm) {
        TestGL(vm);
        EngineSuspendIfNeeded();
        auto cb = GraphicsFrameStart();
        vm.vml.LogFrame();
        return Value(!cb);
    });

nfr("gl_log_frame", "delta", "F", "",
    "call this function instead of gl_frame() to simulate a frame based program from"
    " non-graphical code. does not require gl_window(). manages frame log state much like"
    " gl_frame(). allows gl_time and gl_delta_time to work. pass a desired delta time,"
    " e.g. 1.0/60.0",
    [](VM &vm, Value &delta) {
        SDLUpdateTime(delta.fval());
        vm.vml.LogFrame();
        return Value();
    });

nfr("gl_shutdown", "", "", "",
    "shuts down the OpenGL window. you only need to call this function if you wish to close it"
    " before the end of the program",
    [](VM &) {
        GraphicsShutDown();
        return Value();
    });

nfr("gl_window_title", "title", "S", "Sb",
    "changes the window title.",
    [](VM &vm, Value &s) {
        TestGL(vm);
        SDLTitle(s.sval()->strv());
        return s;
    });

nfr("gl_window_min_max", "dir", "I", "",
    ">0 to maximize, <0 to minimize or 0 to restore.",
    [](VM &vm, Value &dir) {
        TestGL(vm);
        SDLWindowMinMax(dir.intval());
        return Value();
    });

nfr("gl_visible", "", "", "B",
    "checks if the window is currently visible (not minimized, or on mobile devices, in the"
    " foreground). If false, you should not render anything, nor run the frame's code.",
    [](VM &) {
        return Value(!SDLIsMinimized());
    });

nfr("gl_cursor", "on", "B", "B",
    "default the cursor is visible, turn off for implementing FPS like control schemes. return"
    " wether it's on.",
    [](VM &vm, Value &on) {
        TestGL(vm);
        return Value(SDLCursor(on.ival() != 0));
    });

nfr("gl_grab", "on", "B", "B",
    "grabs the mouse when the window is active. return wether it's on.",
    [](VM &vm, Value &on) {
        TestGL(vm);
        return Value(SDLGrab(on.ival() != 0));
    });

nfr("gl_button", "name", "S", "B",
    "returns the state of a key/mousebutton/finger."
    " isdown: >= 1, wentdown: == 1, wentup: == 0, isup: <= 0."
    " (pass a string like mouse1/mouse2/mouse3/escape/space/up/down/a/b/f1 etc."
    " mouse11 and on are additional fingers)",
    [](VM &, Value &name) {
        auto ks = GetKS(name.sval()->strv());
        return Value(ks.Step());
    });

nfr("gl_touchscreen", "", "", "B",
    "wether a you\'re getting input from a touch screen (as opposed to mouse & keyboard)",
    [](VM &) {
        #ifdef PLATFORM_TOUCH
            return Value(true);
        #else
            return Value(false);
        #endif
    });

nfr("gl_dpi", "screen", "I", "I",
    "the DPI of the screen. always returns a value for screen 0, any other screens may return"
    " 0 to indicate the screen doesn\'t exist",
    [](VM &, Value &screen) {
        return Value(SDLScreenDPI(screen.intval()));
    });

nfr("gl_window_size", "", "", "I}:2",
    "a vector representing the size (in pixels) of the window, changes when the user resizes",
    [](VM &vm) {
        vm.PushVec(GetScreenSize());
    });

nfr("gl_mouse_pos", "i", "I", "I}:2",
    "the current mouse/finger position in pixels, pass a value other than 0 to read additional"
    " fingers (for touch screens only if the corresponding gl_isdown is true)",
    [](VM &vm) {
        vm.PushVec(GetFinger(vm.Pop().intval(), false));
    });

nfr("gl_mouse_delta", "i", "I", "I}:2",
    "number of pixels the mouse/finger has moved since the last frame. use this instead of"
    " substracting positions to correctly deal with lifted fingers and FPS mode"
    " (gl_cursor(0))",
    [](VM &vm) {
        vm.PushVec(GetFinger(vm.Pop().intval(), true));
    });

nfr("gl_local_mouse_pos", "i", "I", "F}:2",
    "the current mouse/finger position local to the current transform (gl_translate etc)"
    " (for touch screens only if the corresponding gl_isdown is true)",
    [](VM &vm) {
        vm.PushVec(localfingerpos(vm.Pop().intval()));
    });

nfr("gl_last_pos", "name,down", "SI", "I}:2",
    "position (in pixels) key/mousebutton/finger last went down (true) or up (false)",
    [](VM &vm) {
        auto on = vm.Pop().intval();
        auto name = vm.Pop().sval();
        auto p = GetKeyPos(name->strv(), on);
        vm.PushVec(p);
    });

nfr("gl_local_last_pos", "name,down", "SI", "F}:2",
    "position (local to the current transform) key/mousebutton/finger last went down (true) or"
    " up (false)",
    [](VM &vm) {
        auto on = vm.Pop().intval();
        auto name = vm.Pop().sval();
        auto p = localpos(GetKeyPos(name->strv(), on));
        vm.PushVec(p);
    });

nfr("gl_mousewheel_delta", "", "", "I",
    "amount the mousewheel scrolled this frame, in number of notches",
    [](VM &) {
        return Value(SDLWheelDelta());
    });

nfr("gl_joy_axis", "i", "I", "F",
    "the current joystick orientation for axis i, as -1 to 1 value",
    [](VM &, Value &i) {
        return Value(GetJoyAxis(i.intval()));
    });

nfr("gl_delta_time", "", "", "F",
    "seconds since the last frame, updated only once per frame",
    [](VM &) {
        return Value(SDLDeltaTime());
    });

nfr("gl_time", "", "", "F",
    "seconds since the start of the OpenGL subsystem, updated only once per frame (use"
    " seconds_elapsed() for continuous timing)",
    [](VM &) {
        return Value(SDLTime());
    });

nfr("gl_last_time", "name,down", "SI", "F",
    "time key/mousebutton/finger last went down (true) or up (false)",
    [](VM &, Value &name, Value &on) {
        auto t = GetKeyTime(name.sval()->strv(), on.intval());
        return Value(t);
    });

nfr("gl_clear", "col", "F}:4", "",
    "clears the framebuffer (and depth buffer) to the given color",
    [](VM &vm) {
        TestGL(vm);
        ClearFrameBuffer(vm.PopVec<float3>());
    });

nfr("gl_color", "col,body", "F}:4L?", "",
    "sets the current color. when a body is given, restores the previous color afterwards",
    [](VM &vm) {
        auto body = vm.Pop();
        auto col = vm.PopVec<float4>();
        auto cc = quantizec(curcolor);
        if (body.True()) vm.Push(*(int *)&cc);
        curcolor = col;
        vm.Push(body);
    }, [](VM &vm) {
        auto tmpcol = vm.Pop().intval();
        curcolor = color2vec(*(byte4 *)&tmpcol);
    });

nfr("gl_polygon", "vertlist", "F}]", "",
    "renders a polygon using the list of points given."
    " warning: gl_polygon creates a new mesh every time, gl_new_poly/gl_render_mesh is faster.",
    [](VM &vm, Value &vl) {
        auto m = CreatePolygon(vm, vl);
        m->Render(currentshader);
        delete m;
        return Value();
    });

nfr("gl_circle", "radius,segments", "FI", "",
    "renders a circle",
    [](VM &vm, Value &radius, Value &segments) {
        TestGL(vm);

        geomcache->RenderCircle(currentshader, polymode, max(segments.intval(), 3), radius.fltval());

        return Value();
    });

nfr("gl_open_circle", "radius,segments,thickness", "FIF", "",
    "renders a circle that is open on the inside. thickness is the fraction of the radius that"
    " is filled, try e.g. 0.2",
    [](VM &vm, Value &radius, Value &segments, Value &thickness) {
        TestGL(vm);

        geomcache->RenderOpenCircle(currentshader, max(segments.intval(), 3), radius.fltval(),
                                    thickness.fltval());

        return Value();
    });

nfr("gl_unit_cube", "insideout", "I?", "",
    "renders a unit cube (0,0,0) - (1,1,1). optionally pass true to have it rendered inside"
    " out",
    [](VM &, Value &inside) {
        geomcache->RenderUnitCube(currentshader, inside.True());
        return Value();
    });

nfr("gl_rotate_x", "vector,body", "F}:2L?", "",
    "rotates the yz plane around the x axis, using a 2D vector normalized vector as angle."
    " when a body is given, restores the previous transform afterwards",
    [](VM &vm) {
        auto body = vm.Pop();
        auto a = vm.PopVec<float2>();
        vm.Push(PushTransform(vm, rotationX(a), rotationX(a * float2(1, -1)), body));
    }, [](VM &vm) {
        PopTransform(vm);
    });

nfr("gl_rotate_y", "angle,body", "F}:2L?", "",
    "rotates the xz plane around the y axis, using a 2D vector normalized vector as angle."
    " when a body is given, restores the previous transform afterwards",
    [](VM &vm) {
        auto body = vm.Pop();
        auto a = vm.PopVec<float2>();
        vm.Push(PushTransform(vm, rotationY(a), rotationY(a * float2(1, -1)), body));
    }, [](VM &vm) {
        PopTransform(vm);
    });

nfr("gl_rotate_z", "angle,body", "F}:2L?", "",
    "rotates the xy plane around the z axis (used in 2D), using a 2D vector normalized vector"
    " as angle. when a body is given, restores the previous transform afterwards",
    [](VM &vm) {
        auto body = vm.Pop();
        auto a = vm.PopVec<float2>();
        vm.Push(PushTransform(vm, rotationZ(a), rotationZ(a * float2(1, -1)), body));
    }, [](VM &vm) {
        PopTransform(vm);
    });

nfr("gl_translate", "vec,body", "F}L?", "",
    "translates the current coordinate system along a vector. when a body is given,"
    " restores the previous transform afterwards",
    [](VM &vm) {
        auto body = vm.Pop();
        auto v = vm.PopVec<float3>();
        vm.Push(PushTransform(vm, translation(v), translation(-v), body));
    }, [](VM &vm) {
        PopTransform(vm);
    });

nfr("gl_scale", "factor,body", "FL?", "",
    "scales the current coordinate system using a numerical factor."
    " when a body is given, restores the previous transform afterwards",
    [](VM &vm, Value &f, Value &body) {
        auto v = f.fltval() * float3_1;
        return PushTransform(vm, float4x4(float4(v, 1)), float4x4(float4(float3_1 / v, 1)), body);
    }, [](VM &vm) {
        PopTransform(vm);
    });

nfr("gl_scale", "factor,body", "F}L?", "",
    "scales the current coordinate system using a vector."
    " when a body is given, restores the previous transform afterwards",
    [](VM &vm) {
        auto body = vm.Pop();
        auto v = vm.PopVec<float3>();
        vm.Push(PushTransform(vm, float4x4(float4(v, 1)), float4x4(float4(float3_1 / v, 1)), body));
    }, [](VM &vm) {
        PopTransform(vm);
    });

nfr("gl_origin", "", "", "F}:2",
    "returns a vector representing the current transform origin in pixels."
    " only makes sense in 2D mode (no gl_perspective called).",
    [](VM &vm) {
        auto pos = floatp2(otransforms.object2view[3].x, otransforms.object2view[3].y);
        vm.PushVec(pos);
    });

nfr("gl_scaling", "", "", "F}:2",
    "returns a vector representing the current transform scale in pixels."
    " only makes sense in 2D mode (no gl_perspective called).",
    [](VM &vm) {
        auto sc = floatp2(otransforms.object2view[0].x, otransforms.object2view[1].y);
        vm.PushVec(sc);
    });

nfr("gl_model_view_projection", "", "", "F]",
    "returns a vector representing the current model view projection matrix"
    " (16 elements)",
    [](VM &vm) {
        auto v = vm.NewVec(16, 16, TYPE_ELEM_VECTOR_OF_FLOAT);
        auto mvp = view2clip * otransforms.object2view;
        for (int i = 0; i < 16; i++) v->At(i) = mvp.data()[i];
        return Value(v);
    });

nfr("gl_point_scale", "factor", "F", "",
    "sets the current scaling factor for point sprites."
    " this can be what the current gl_scale is, or different, depending on the desired visuals."
    " the ideal size may also be FOV dependent.",
    [](VM &, Value &f) {
        custompointscale = f.fltval();
        return Value();
    });

nfr("gl_line_mode", "on,body", "IL", "",
    "set line mode (true == on). when a body is given,"
    " restores the previous mode afterwards",
    [](VM &vm, Value &on, Value &body) {
        if (body.True()) vm.Push(Value(polymode));
        polymode = on.ival() ? PRIM_LOOP : PRIM_FAN;
        return body;
    }, [](VM &vm) {
        polymode = (Primitive)vm.Pop().ival();
    });

nfr("gl_hit", "vec,i", "F}I", "B",
    "wether the mouse/finger is inside of the rectangle specified in terms of the current"
    " transform (for touch screens only if the corresponding gl_isdown is true). Only true if"
    " the last rectangle for which gl_hit was true last frame is of the same size as this one"
    " (allows you to safely test in most cases of overlapping rendering)",
    [](VM &vm) {
        auto i = vm.Pop().intval();
        auto size = vm.PopVec<float3>();
        auto localmousepos = localfingerpos(i);
        auto hit = localmousepos.x >= 0 &&
                   localmousepos.y >= 0 &&
                   localmousepos.x < size.x &&
                   localmousepos.y < size.y;
        if (hit) lasthitsize = size;
        /*
        #ifdef PLATFORM_TOUCH
        // Inefficient for fingers other than 0, which is going to be rare.
        auto ks = i ? GetKS((string_view("mouse1") + (char)('0' + i)).c_str()) : GetKS("mouse1");
        // On mobile, if the finger just went down, we wont have meaningfull lastframehitsize, so if
        // the programmer checks for the combination of gl_hit and gl_wentdown, that would fail.
        // Instead, we bypass that check.
        // PROBLEM: now we'll be returning true for overlapping elements.
        // if we can solve this, we can remove the frame delay from the input system.
        if (ks.wentdown && hit) return true;
        #endif
        */
        vm.Push(size == lastframehitsize && hit);
    });

nfr("gl_rect", "size,centered", "F}:2I?", "",
    "renders a rectangle (0,0)..(1,1) (or (-1,-1)..(1,1) when centered), scaled by the given"
    " size.",
    [](VM &vm) {
        auto centered = vm.Pop().True();
        auto vec = vm.PopVec<float2>();
        TestGL(vm);
        geomcache->RenderQuad(currentshader, polymode, centered,
                              float4x4(float4(vec, 1)));
    });

nfr("gl_rect_tc_col", "size,tc,tcsize,cols", "F}:2F}:2F}:2F}:4]", "",
    "Like gl_rect renders a sized quad, but allows you to specify texture coordinates and"
    " optionally colors (empty list for all white). Slow.",
    [](VM &vm) {
        TestGL(vm);
        auto cols = vm.Pop().vval();
        auto td = vm.PopVec<float2>();
        auto t = vm.PopVec<float2>();
        auto sz = vm.PopVec<float2>();
        auto te = t + td;
        struct Vert { float x, y, z, u, v; byte4 c; };
        Vert vb_square[4] = {
            #define _GETCOL(N) \
                cols->len > N ? quantizec(ValueToFLT<4>(cols->AtSt(N), cols->width)) : byte4_255
            { 0,    0,    0, t.x,  t.y,  _GETCOL(0) },
            { 0,    sz.y, 0, t.x,  te.y, _GETCOL(1) },
            { sz.x, sz.y, 0, te.x, te.y, _GETCOL(2) },
            { sz.x, 0,    0, te.x, t.y,  _GETCOL(3) }
        };
        currentshader->Set();
        RenderArraySlow(PRIM_FAN, make_span(vb_square, 4), "PTC");
    });

nfr("gl_unit_square", "centered", "I?", "",
    "renders a square (0,0)..(1,1) (or (-1,-1)..(1,1) when centered)",
    [](VM &vm, Value &centered) {
        TestGL(vm);
        geomcache->RenderUnitSquare(currentshader, polymode, centered.True());
        return Value();
    });

nfr("gl_line", "start,end,thickness", "F}F}F", "",
    "renders a line with the given thickness",
    [](VM &vm) {
        TestGL(vm);
        auto thickness = vm.Pop().fltval();
        auto v2 = vm.PopVec<float3>();
        auto v1 = vm.PopVec<float3>();
        if (Is2DMode()) geomcache->RenderLine2D(currentshader, polymode, v1, v2, thickness);
        else geomcache->RenderLine3D(currentshader, v1, v2, float3_0, thickness);
    });

nfr("gl_perspective", "fovy,znear,zfar", "FFF", "",
    "changes from 2D mode (default) to 3D right handed perspective mode with vertical fov (try"
    " 60), far plane (furthest you want to be able to render, try 1000) and near plane (try"
    " 1)",
    [](VM &, Value &fovy, Value &znear, Value &zfar) {
        Set3DMode(fovy.fltval() * RAD, GetScreenSize().x / (float)GetScreenSize().y, znear.fltval(),
                  zfar.fltval());
        return Value();
    });

nfr("gl_ortho", "rh,depth", "I?I?", "",
    "changes back to 2D mode rendering with a coordinate system from (0,0) top-left to the"
    " screen size in pixels bottom right. this is the default at the start of a frame, use this"
    " call to get back to that after gl_perspective."
    " Pass true to rh have (0,0) bottom-left instead."
    " Pass true to depth to have depth testing/writing on.",
    [](VM &, Value &rh, Value &depth) {
        Set2DMode(GetScreenSize(), !rh.True(), depth.True());
        return Value();
    });

nfr("gl_ortho3d", "center,extends", "F}F}", "",
    "sets a custom ortho projection as 3D projection.",
    [](VM &vm) {
        auto extends = vm.PopVec<float3>();
        auto center = vm.PopVec<float3>();
        Set3DOrtho(center, extends);
    });

nfr("gl_new_poly", "positions", "F}]", "R",
    "creates a mesh out of a loop of points, much like gl_polygon."
    " gl_line_mode determines how this gets drawn (fan or loop)."
    " returns mesh id",
    [](VM &vm, Value &positions) {
        auto m = CreatePolygon(vm, positions);
        return Value(vm.NewResource(m, &mesh_type));
    });

nfr("gl_new_mesh", "format,positions,colors,normals,texcoords1,texcoords2,indices", "SF}:3]F}:4]F}:3]F}:2]F}:2]I]?", "R",
    "creates a new vertex buffer and returns an integer id (1..) for it."
    " format must be made up of characters P (position), C (color), T (texcoord), N (normal)."
    " indices may be []. positions is obligatory."
    " you may specify [] for any of the other attributes if not required by format,"
    " or to get defaults for colors (white) / texcoords (position x & y) /"
    " normals (generated from adjacent triangles).",
    [](VM &vm, Value &format, Value &positions, Value &colors,
                           Value &normals, Value &texcoords1, Value &texcoords2, Value &indices) {
        TestGL(vm);
        auto nattr = format.sval()->len;
        if (nattr < 1 || nattr > 10)
            vm.BuiltinError("newmesh: illegal format/attributes size");
        auto fmt = format.sval()->strv();
        if (nattr > (int)min(fmt.find_first_not_of("PCTN"), fmt.size()) || fmt[0] != 'P')
            vm.BuiltinError("newmesh: illegal format characters (only PCTN allowed), P must be"
                               " first");
        intp nverts = positions.vval()->len;
        vector<int> idxs;
        if (indices.True()) {
            for (int i = 0; i < indices.vval()->len; i++) {
                auto &e = indices.vval()->At(i);
                if (e.ival() < 0 || e.ival() >= nverts)
                    vm.BuiltinError("newmesh: index out of range of vertex list");
                idxs.push_back(e.intval());
            }
        }
        size_t vsize = AttribsSize(fmt);
        size_t normal_offset = 0;
        auto verts = new uchar[nverts * vsize];
        for (intp i = 0; i < nverts; i++) {
            auto start = &verts[i * vsize];
            auto p = start;
            float3 pos;
            int texcoordn = 0;
            for (auto c : fmt) {
                switch (c) {
                    case 'P': {
                        pos = ValueToFLT<3>(positions.vval()->AtSt(i), positions.vval()->width);
                        WriteMemInc(p, pos);
                        break;
                    }
                    case 'C':
                        WriteMemInc(p,
                            i < colors.vval()->len
                                ? quantizec(ValueToFLT<4>(colors.vval()->AtSt(i),
                                                          colors.vval()->width, 1))
                                : byte4_255);
                        break;
                    case 'T': {
                        auto &texcoords = texcoordn ? texcoords2 : texcoords1;
                        WriteMemInc(p,
                            i < texcoords.vval()->len
                                ? ValueToFLT<2>(texcoords.vval()->AtSt(i),
                                                texcoords.vval()->width, 0)
                                : pos.xy());
                        texcoordn++;
                        break;
                    }
                    case 'N':
                        if (!normals.vval()->len) normal_offset = p - start;
                        WriteMemInc(p,
                            i < normals.vval()->len
                                ? ValueToFLT<3>(normals.vval()->AtSt(i), normals.vval()->width, 0)
                                : float3_0);
                        break;
                    default: assert(0);
                }
            }
        }
        if (normal_offset) {
            // if no normals were specified, generate them.
            normalize_mesh(make_span(idxs), verts, nverts, vsize, normal_offset);
        }
        // FIXME: make meshes into points in a more general way.
        auto m = new Mesh(new Geometry(make_span(verts, nverts * vsize), fmt, span<uchar>(), vsize),
                          indices.True() ? PRIM_TRIS : PRIM_POINT);
        if (idxs.size()) m->surfs.push_back(new Surface(make_span(idxs)));
        delete[] verts;
        return Value(vm.NewResource(m, &mesh_type));
    });

nfr("gl_new_mesh_iqm", "filename", "S", "R?",
    "load a .iqm file into a mesh, returns mesh or nil on failure to load.",
    [](VM &vm, Value &fn) {
        TestGL(vm);
        auto m = LoadIQM(fn.sval()->strv());
        return m ? Value(vm.NewResource(m, &mesh_type)) : Value();
    });

nfr("gl_mesh_parts", "m", "R", "S]",
    "returns an array of names of all parts of mesh m (names may be empty)",
    [](VM &vm, Value &i) {
        auto &m = GetMesh(vm, i);
        auto v = (LVector *)vm.NewVec(0, (int)m.surfs.size(), TYPE_ELEM_VECTOR_OF_STRING);
        for (auto s : m.surfs) v->Push(vm, Value(vm.NewString(s->name)));
        return Value(v);
    });

nfr("gl_mesh_size", "m", "R", "I",
    "returns the number of verts in this mesh",
    [](VM &vm, Value &i) {
        auto &m = GetMesh(vm, i);
        return Value((int)m.geom->nverts);
    });

nfr("gl_animate_mesh", "m,frame", "RF", "",
    "set the frame for animated mesh m",
    [](VM &vm, Value &i, Value &f) {
        GetMesh(vm, i).curanim = f.fltval();
        return Value();
    });

nfr("gl_render_mesh", "m", "R", "",
    "renders the specified mesh",
    [](VM &vm, Value &i) {
        TestGL(vm);
        GetMesh(vm, i).Render(currentshader);
        return Value();
    });

nfr("gl_save_mesh", "m,name", "RS", "B",
    "saves the specified mesh to a file in the PLY format. useful if the mesh was generated"
    " procedurally. returns false if the file could not be written",
    [](VM &vm, Value &i, Value &name) {
        TestGL(vm);
        bool ok = GetMesh(vm, i).SaveAsPLY(name.sval()->strv());
        return Value(ok);
    });

nfr("gl_set_shader", "shader", "S", "",
    "changes the current shader. shaders must reside in the shaders folder, builtin ones are:"
    " color / textured / phong",
    [](VM &vm, Value &shader) {
        TestGL(vm);
        auto sh = LookupShader(shader.sval()->strv());
        if (!sh) vm.BuiltinError("no such shader: " + shader.sval()->strv());
        currentshader = sh;
        return Value();
    });

nfr("gl_set_uniform", "name,value,ignore_errors", "SF}I?", "B",
    "set a uniform on the current shader. size of float vector must match size of uniform"
    " in the shader.",
    [](VM &vm) {
        auto ignore_errors = vm.Pop().True();
        auto len = vm.Top().intval();
        auto v = vm.PopVec<float4>();
        auto r = SetUniform(vm, vm.Pop(), v.begin(), len, ignore_errors);
        vm.Push(r);
    });

nfr("gl_set_uniform", "name,value,ignore_errors", "SFI?", "B",
    "set a uniform on the current shader. uniform"
    " in the shader must be a single float.",
    [](VM &vm, Value &name, Value &vec, Value &ignore_errors) {
        auto f = vec.fltval();
        return SetUniform(vm, name, &f, 1, ignore_errors.True());
    });

nfr("gl_set_uniform_array", "name,value", "SF}:4]", "B",
    "set a uniform on the current shader. uniform in the shader must be an array of vec4."
    " returns false on error.",
    [](VM &vm, Value &name, Value &vec) {
        TestGL(vm);
        vector<float4> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++)
            vals[i] = ValueToFLT<4>(vec.vval()->AtSt(i), vec.vval()->width);
        currentshader->Activate();
        auto ok = currentshader->SetUniform(name.sval()->strv(), vals.data()->data(), 4,
                                            (int)vals.size());
        return Value(ok);
    });

nfr("gl_set_uniform_matrix", "name,value", "SF]", "B",
    "set a uniform on the current shader. pass a vector of 4/9/12/16 floats to set a"
    " mat2/mat3/mat3x4/mat4 respectively. returns false on error.",
    [](VM &vm, Value &name, Value &vec) {
        TestGL(vm);
        vector<float> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++) vals[i] = vec.vval()->At(i).fltval();
        currentshader->Activate();
        auto ok = currentshader->SetUniformMatrix(name.sval()->strv(), vals.data(),
                                                  (int)vals.size(), 1);
        return Value(ok);
    });

nfr("gl_uniform_buffer_object", "name,value,ssbo", "SF}:4]I", "I",
    "creates a uniform buffer object, and attaches it to the current shader at the given"
    " uniform block name. uniforms in the shader must be all vec4s, or an array of them."
    " ssbo indicates if you want a shader storage block instead."
    " returns buffer id or 0 on error.",
    [](VM &vm, Value &name, Value &vec, Value &ssbo) {
        TestGL(vm);
        vector<float4> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++)
            vals[i] = ValueToFLT<4>(vec.vval()->AtSt(i), vec.vval()->width);
        auto id = UniformBufferObject(currentshader, vals.data()->data(),
                                      4 * sizeof(float) * vals.size(),
                                      name.sval()->strv(), ssbo.True(), 0);
        return Value((int)id);
    });

nfr("gl_uniform_buffer_object", "name,value,ssbo", "SSI", "I",
    "creates a uniform buffer object, and attaches it to the current shader at the given"
    " uniform block name. uniforms in the shader can be any type, as long as it matches the"
    " data layout in the string buffer."
    " ssbo indicates if you want a shader storage block instead."
    " returns buffer id or 0 on error.",
    [](VM &vm, Value &name, Value &vec, Value &ssbo) {
    TestGL(vm);
    auto id = UniformBufferObject(currentshader, vec.sval()->strv().data(),
                                  vec.sval()->strv().size(),
                                  name.sval()->strv(), ssbo.True(), 0);
    return Value((int)id);
});

nfr("gl_delete_buffer_object", "id", "I", "",
    "deletes a buffer objects, e.g. one allocated by gl_uniform_buffer_object().",
    [](VM &vm, Value &id) {
        TestGL(vm);
        // FIXME: should route this thru a IntResourceManagerCompact to be safe?
        // I guess GL doesn't care about illegal id's?
        DeleteBO(id.intval());
        return Value();
    });

nfr("gl_bind_mesh_to_compute", "mesh,name", "R?S", "",
    "Bind the vertex data of a mesh to a SSBO binding of a compute shader. Pass a nil mesh to"
    " unbind.",
    [](VM &vm, Value &mesh, Value &name) {
        TestGL(vm);
        if (mesh.True()) GetMesh(vm, mesh).geom->BindAsSSBO(currentshader, name.sval()->strv());
        else UniformBufferObject(currentshader, nullptr, 0, name.sval()->strv(), true, 0);
        return Value();
    });

nfr("gl_dispatch_compute", "groups", "I}:3", "",
    "dispatches the currently set compute shader in groups of sizes of the specified x/y/z"
    " values.",
    [](VM &vm) {
        auto groups = vm.PopVec<int3>();
        TestGL(vm);
        DispatchCompute(groups);
    });

nfr("gl_dump_shader", "filename,stripnonascii", "SB", "B",
    "Dumps the compiled (binary) version of the current shader to a file. Contents are driver"
    " dependent. On Nvidia hardware it contains the assembly version of the shader as text,"
    " pass true for stripnonascii if you're only interested in that part.",
    [](VM &vm, Value &filename, Value &stripnonascii) {
        TestGL(vm);
        currentshader->Activate();
        auto ok = currentshader->Dump(filename.sval()->strv(), stripnonascii.True());
        return Value(ok);
    });

nfr("gl_blend", "on,body", "IL?", "",
    "changes the blending mode (use blending constants from color.lobster). when a body is"
    " given, restores the previous mode afterwards",
    [](VM &vm, Value &mode, Value &body) {
        TestGL(vm);
        int old = SetBlendMode((BlendMode)mode.ival());
        if (body.True()) vm.Push(Value(old));
        return body;
    }, [](VM &vm) {
        auto m = vm.Pop();
        assert(m.type == V_INT);
        SetBlendMode((BlendMode)m.ival());
    });

nfr("gl_load_texture", "name,textureformat", "SI?", "R?",
    "returns texture if succesfully loaded from file name, otherwise nil."
    " see color.lobster for texture format. Uses stb_image internally"
    " (see http://nothings.org/), loads JPEG Baseline, subsets of PNG, TGA, BMP, PSD, GIF, HDR,"
    " PIC.",
    [](VM &vm, Value &name, Value &tf) {
        TestGL(vm);
        auto tex = CreateTextureFromFile(name.sval()->strv(), tf.intval());
        return tex.id ? vm.NewResource(new Texture(tex), &texture_type) : Value();
    });

nfr("gl_set_primitive_texture", "i,tex,textureformat", "IRI?", "",
    "sets texture unit i to texture (for use with rect/circle/polygon/line)",
    [](VM &vm, Value &i, Value &id, Value &tf) {
        TestGL(vm);
        SetTexture(GetSampler(vm, i), GetTexture(vm, id), tf.intval());
        return Value();
    });

nfr("gl_set_mesh_texture", "mesh,part,i,texture", "RIIR", "",
    "sets texture unit i to texture for a mesh and part (0 if not a multi-part mesh)",
    [](VM &vm, Value &mid, Value &part, Value &i, Value &id) {
        auto &m = GetMesh(vm, mid);
        if (part.ival() < 0 || part.ival() >= (int)m.surfs.size())
            vm.BuiltinError("setmeshtexture: illegal part index");
        m.surfs[part.ival()]->Get(GetSampler(vm, i)) = GetTexture(vm, id);
        return Value();
    });

nfr("gl_set_image_texture", "i,tex,textureformat", "IRI", "",
    "sets image unit i to texture (for use with compute). texture format must be the same"
    " as what you specified in gl_load_texture / gl_create_texture,"
    " with optionally writeonly/readwrite flags.",
    [](VM &vm, Value &i, Value &id, Value &tf) {
        TestGL(vm);
        SetImageTexture(GetSampler(vm, i), GetTexture(vm, id), tf.intval());
        return Value();
    });

nfr("gl_create_texture", "matrix,textureformat", "F}:4]]I?", "R",
    "creates a texture from a 2d array of color vectors."
    " see texture.lobster for texture format",
    [](VM &vm, Value &matv, Value &tf) {
        TestGL(vm);
        auto mat = matv.vval();
        auto ys = mat->len;
        auto xs = mat->At(0).vval()->len;
        auto sz = tf.ival() & TF_FLOAT ? sizeof(float4) : sizeof(byte4);
        auto buf = new uchar[xs * ys * sz];
        memset(buf, 0, xs * ys * sz);
        for (int i = 0; i < ys; i++) {
            auto row = mat->At(i).vval();
            for (int j = 0; j < min(xs, row->len); j++) {
                float4 col = ValueToFLT<4>(row->AtSt(j), row->width);
                auto idx = i * xs + j;
                if (tf.ival() & TF_FLOAT) ((float4 *)buf)[idx] = col;
                else                      ((byte4  *)buf)[idx] = quantizec(col);
            }
        }
        auto tex = CreateTexture(buf, int2(intp2(xs, ys)).data(), tf.intval());
        delete[] buf;
        return Value(vm.NewResource(new Texture(tex), &texture_type));
    });

nfr("gl_create_blank_texture", "size,color,textureformat", "I}:2F}:4I?", "R",
    "creates a blank texture (for use as frame buffer or with compute shaders)."
    " see texture.lobster for texture format",
    [](VM &vm) {
        TestGL(vm);
        auto tf = vm.Pop().intval();
        auto col = vm.PopVec<float4>();
        auto size = vm.PopVec<int2>();
        auto tex = CreateBlankTexture(size, col, tf);
        vm.Push(vm.NewResource(new Texture(tex), &texture_type));
    });

nfr("gl_texture_size", "tex", "R", "I}:2",
    "returns the size of a texture",
    [](VM &vm) {
        TestGL(vm);
        vm.PushVec(GetTexture(vm, vm.Pop()).size.xy());
    });

nfr("gl_read_texture", "tex", "R", "S?",
    "read back RGBA texture data into a string or nil on failure",
    [](VM &vm, Value &t) {
        TestGL(vm);
        auto tex = GetTexture(vm, t);
        auto numpixels = tex.size.x * tex.size.y;
        if (!numpixels) return Value();
        auto buf = ReadTexture(tex);
        if (!buf) return Value();
        auto s = vm.NewString(string_view((char *)buf, numpixels * 4));
        delete[] buf;
        return Value(s);
    });

nfr("gl_switch_to_framebuffer", "tex,hasdepth,textureformat,resolvetex,depthtex", "R?I?I?R?R?", "B",
    "switches to a new framebuffer, that renders into the given texture."
    " also allocates a depth buffer for it if depth is true."
    " pass the textureformat that was used for this texture."
    " pass a resolve texture if the base texture is multisample."
    " pass your own depth texture if desired."
    " pass a nil texture to switch back to the original framebuffer."
    " performance note: do not recreate texture passed in unless necessary.",
    [](VM &vm, Value &t, Value &depth, Value &tf, Value &retex,
                                       Value &depthtex) {
        TestGL(vm);
        auto tex = GetTexture(vm, t);
        return Value(SwitchToFrameBuffer(tex.id ? tex : Texture(0, GetScreenSize()),
                                         depth.True(), tf.intval(), GetTexture(vm, retex),
                                         GetTexture(vm, depthtex)));
    });

nfr("gl_light", "pos,params", "F}:3F}:2", "",
    "sets up a light at the given position for this frame. make sure to call this after your"
    " camera transforms but before any object transforms (i.e. defined in \"worldspace\")."
    " params contains specular exponent in x (try 32/64/128 for different material looks) and"
    " the specular scale in y (try 1 for full intensity)",
    [](VM &vm) {
        Light l;
        l.params = vm.PopVec<float2>();
        l.pos = otransforms.object2view * float4(vm.PopVec<float3>(), 1);
        lights.push_back(l);
    });

nfr("gl_render_tiles", "positions,tilecoords,mapsize", "F}:2]I}:2]I}:2", "",
    "Renders a list of tiles from a tilemap. Each tile rendered is 1x1 in size."
    " Positions may be anywhere. Tile coordinates are inside the texture map, map size is"
    " the amount of tiles in the texture. Tiles may overlap, they are drawn in order."
    " Before calling this, make sure to have the texture set and a textured shader",
    [](VM &vm) {
        TestGL(vm);
        auto msize = float2(vm.PopVec<int2>());
        auto tile = vm.Pop().vval();
        auto pos = vm.Pop().vval();
        auto len = pos->len;
        if (len != tile->len)
            vm.BuiltinError("rendertiles: vectors of different size");
        vector<SpriteVert> vbuf(len * 6);
        for (intp i = 0; i < len; i++) {
            auto p = ValueToFLT<2>(pos->AtSt(i), pos->width);
            auto t = float2(ValueToI<2>(tile->AtSt(i), tile->width)) / msize;
            vbuf[i * 6 + 0].pos = p;
            vbuf[i * 6 + 1].pos = p + float2_y;
            vbuf[i * 6 + 2].pos = p + float2_1;
            vbuf[i * 6 + 3].pos = p;
            vbuf[i * 6 + 4].pos = p + float2_1;
            vbuf[i * 6 + 5].pos = p + float2_x;
            vbuf[i * 6 + 0].tc = t;
            vbuf[i * 6 + 1].tc = t + float2_y / msize;
            vbuf[i * 6 + 2].tc = t + float2_1 / msize;
            vbuf[i * 6 + 3].tc = t;
            vbuf[i * 6 + 4].tc = t + float2_1 / msize;
            vbuf[i * 6 + 5].tc = t + float2_x / msize;
        }
        currentshader->Set();
        RenderArraySlow(PRIM_TRIS, make_span(vbuf), "pT");
    });

nfr("gl_debug_grid", "num,dist,thickness", "I}:3F}:3F", "",
    "renders a grid in space for debugging purposes. num is the number of lines in all 3"
    " directions, and dist their spacing. thickness of the lines in the same units",
    [](VM &vm) {
        TestGL(vm);
        auto thickness = vm.Pop().fltval();
        auto dist = vm.PopVec<float3>();
        auto num = vm.PopVec<intp3>();
        float3 cp = otransforms.view2object[3].xyz();
        auto m = float3(num);
        auto step = dist;
        auto oldcolor = curcolor;
        curcolor = float4(0, 1, 0, 1);
        for (float z = 0; z <= m.z; z += step.x) {
            for (float x = 0; x <= m.x; x += step.x) {
                geomcache->RenderLine3D(currentshader, float3(x, 0, z), float3(x, m.y, z), cp,
                             thickness);
            }
        }
        curcolor = float4(1, 0, 0, 1);
        for (float z = 0; z <= m.z; z += step.y) {
            for (float y = 0; y <= m.y; y += step.y) {
                geomcache->RenderLine3D(currentshader, float3(0, y, z), float3(m.x, y, z), cp,
                    thickness);
            }
        }
        curcolor = float4(0, 0, 1, 1);
        for (float y = 0; y <= m.y; y += step.z) {
            for (float x = 0; x <= m.x; x += step.z) {
                geomcache->RenderLine3D(currentshader, float3(x, y, 0), float3(x, y, m.z), cp,
                    thickness);
            }
        }
        curcolor = oldcolor;
    });

nfr("gl_screenshot", "filename", "S", "B",
    "saves a screenshot in .png format, returns true if succesful",
    [](VM &, Value &fn) {
        bool ok = ScreenShot(fn.sval()->strv());
        return Value(ok);
    });

}  // AddFont

