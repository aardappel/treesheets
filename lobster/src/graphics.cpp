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
#include "lobster/natreg.h"

#include "lobster/glinterface.h"
#include "lobster/sdlinterface.h"

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

Mesh &GetMesh(Value &res) {
    return *GetResourceDec<Mesh *>(res, &mesh_type);
}
Texture GetTexture(Value &res) {
    auto tex = GetResourceDec<Texture *>(res, &texture_type);
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
    g_vm->vml.LogFrame();
    return cb;
}

void TestGL() {
    if (!graphics_initialized)
        g_vm->BuiltinError("graphics system not initialized yet, call gl_window() first");
}

float2 localpos(const int2 &pos) {
    return (otransforms.view2object * float4(float3(float2(pos), 0), 1)).xyz().xy();
}
float2 localfingerpos(int i) {
    return localpos(GetFinger(i, false));
}

Value PushTransform(const float4x4 &forward, const float4x4 &backward, const Value &body) {
    if (body.True()) {
        g_vm->Push(Value(g_vm->NewString((char *)&otransforms, sizeof(objecttransforms))));
    }
    AppendTransform(forward, backward);
    return body;
}

void PopTransform() {
    auto s = g_vm->Pop();
    TYPE_ASSERT(s.type == V_STRING);
    assert(s.sval()->len == sizeof(objecttransforms));
    otransforms = *(objecttransforms *)s.sval()->str();
    s.DECRT();
}

int GetSampler(Value &i) {
    if (i.ival() < 0 || i.ival() >= Shader::MAX_SAMPLERS)
        g_vm->BuiltinError("graphics: illegal texture unit");
    return i.intval();
}

Mesh *CreatePolygon(Value &vl) {
    TestGL();
    auto len = vl.vval()->len;
    if (len < 3) g_vm->BuiltinError("polygon: must have at least 3 verts");
    auto vbuf = new BasicVert[len];
    for (int i = 0; i < len; i++) vbuf[i].pos = ValueToFLT<3>(vl.vval()->At(i));
    auto v1 = vbuf[1].pos - vbuf[0].pos;
    auto v2 = vbuf[2].pos - vbuf[0].pos;
    auto norm = normalize(cross(v2, v1));
    for (int i = 0; i < len; i++) {
        vbuf[i].norm = norm;
        vbuf[i].tc = vbuf[i].pos.xy();
        vbuf[i].col = byte4_255;
    }
    auto m = new Mesh(new Geometry(vbuf, len, sizeof(BasicVert), "PNTC"), polymode);
    delete[] vbuf;
    return m;
}

Value SetUniform(Value &name, const float *data, int len, bool ignore_errors) {
    TestGL();
    currentshader->Activate();
    auto ok = currentshader->SetUniform(name.sval()->str(), data, len);
    if (!ok && !ignore_errors)
        g_vm->Error("failed to set uniform: " + string(name.sval()->str()));
    name.DECRT();
    return Value(ok);
}

void AddGraphics() {
    STARTDECL(gl_window) (Value &title, Value &xs, Value &ys, Value &fullscreen, Value &novsync,
                          Value &samples) {
        if (graphics_initialized)
            g_vm->BuiltinError("cannot call gl_window() twice");
        string err = SDLInit(title.sval()->str(), int2(intp2(xs.ival(), ys.ival())),
                             fullscreen.ival() != 0, novsync.ival() == 0,
                             max(1, samples.intval()));
        title.DECRT();
        if (err.empty()) {
            err = LoadMaterialFile("shaders/default.materials");
        }
        if (!err.empty()) {
            Output(OUTPUT_INFO, err.c_str());
            return Value(g_vm->NewString(err));
        }
        colorshader = LookupShader("color");
        assert(colorshader);
        Output(OUTPUT_INFO, "graphics fully initialized...");
        graphics_initialized = true;
        return Value();
    }
    ENDDECL6(gl_window, "title,xs,ys,fullscreen,novsync,samples", "SIII?I?I?", "S?",
        "opens a window for OpenGL rendering. returns error string if any problems, nil"
        " otherwise.");

    STARTDECL(gl_require_version) (Value &major, Value &minor) {
        SDLRequireGLVersion(major.intval(), minor.intval());
        return Value();
    }
    ENDDECL2(gl_require_version, "major,minor", "II", "",
             "Call this before gl_window to request a certain version of OpenGL context."
             " Currently only works on win/nix, minimum is 3.2.");

    STARTDECL(gl_loadmaterials) (Value &fn, Value &isinline) {
        TestGL();
        auto err = isinline.True() ? ParseMaterialFile(fn.sval()->str())
                                   : LoadMaterialFile(fn.sval()->str());
        fn.DECRT();
        return err[0] ? Value(g_vm->NewString(err)) : Value();
    }
    ENDDECL2(gl_loadmaterials, "materialdefs,inline", "SI?", "S?",
        "loads an additional materials file (shader/default.materials is already loaded by default"
        " by gl_window()). if inline is true, materialdefs is not a filename, but the actual"
        " materials. returns error string if any problems, nil otherwise.");

    STARTDECL(gl_frame) () {
        TestGL();
        #ifdef USE_MAIN_LOOP_CALLBACK
            // Here we have to something hacky: emscripten requires us to not take over the main
            // loop. So we use this exception to suspend the VM right inside the gl_frame() call.
            // FIXME: do this at the start of the frame instead?
            throw string("SUSPEND-VM-MAINLOOP");
        #endif
        auto cb = GraphicsFrameStart();
        return Value(!cb);
    }
    ENDDECL0(gl_frame, "", "", "I",
        "advances rendering by one frame, swaps buffers, and collects new input events."
        " returns true if the closebutton on the window was pressed");

    STARTDECL(gl_logframe) (Value &delta) {
        SDLFakeFrame(delta.fval());
        g_vm->vml.LogFrame();
        return Value();
    }
    ENDDECL1(gl_logframe, "delta", "F", "",
        "call this function instead of gl_frame() to simulate a frame based program from"
        " non-graphical code. does not require gl_window(). manages frame log state much like"
        " gl_frame(). allows gl_time and gl_deltatime to work. pass a desired delta time,"
        " e.g. 1.0/60.0");

    STARTDECL(gl_shutdown) () {
        GraphicsShutDown();
        return Value();
    }
    ENDDECL0(gl_shutdown, "", "", "",
        "shuts down the OpenGL window. you only need to call this function if you wish to close it"
        " before the end of the program");

    STARTDECL(gl_windowtitle) (Value &s) {
        TestGL();
        SDLTitle(s.sval()->str());
        return s;
    }
    ENDDECL1(gl_windowtitle, "title", "S", "S",
        "changes the window title.");

    STARTDECL(gl_windowminmax) (Value &dir) {
        TestGL();
        SDLWindowMinMax(dir.intval());
        return Value();
    }
    ENDDECL1(gl_windowminmax, "dir", "I", "",
             ">0 to maximize, <0 to minimize or 0 to restore.");

    STARTDECL(gl_visible) () {
        return Value(!SDLIsMinimized());
    }
    ENDDECL0(gl_visible, "", "", "I",
        "checks if the window is currently visible (not minimized, or on mobile devices, in the"
        " foreground). If false, you should not render anything, nor run the frame's code.");

    STARTDECL(gl_cursor) (Value &on) {
        TestGL();
        return Value(SDLCursor(on.ival() != 0));
    }
    ENDDECL1(gl_cursor, "on", "I", "I",
        "default the cursor is visible, turn off for implementing FPS like control schemes. return"
        " wether it's on.");

    STARTDECL(gl_grab) (Value &on) {
        TestGL();
        return Value(SDLGrab(on.ival() != 0));
    }
    ENDDECL1(gl_grab, "on", "I", "I",
        "grabs the mouse when the window is active. return wether it's on.");

    STARTDECL(gl_button) (Value &name) {
        auto ks = GetKS(name.sval()->str());
        name.DECRT();
        return Value(ks.Step());
    }
    ENDDECL1(gl_button, "name", "S", "I",
        "returns the state of a key/mousebutton/finger."
        " isdown: >= 1, wentdown: == 1, wentup: == 0, isup: <= 0."
        " (pass a string like mouse1/mouse2/mouse3/escape/space/up/down/a/b/f1 etc."
        " mouse11 and on are additional fingers)");

    STARTDECL(gl_touchscreen) () {
        #ifdef PLATFORM_TOUCH
            return Value(true);
        #else
            return Value(false);
        #endif
    }
    ENDDECL0(gl_touchscreen, "", "", "I",
        "wether a you\'re getting input from a touch screen (as opposed to mouse & keyboard)");

    STARTDECL(gl_dpi) (Value &screen) {
        return Value(SDLScreenDPI(screen.intval()));
    }
    ENDDECL1(gl_dpi, "screen", "I", "I",
        "the DPI of the screen. always returns a value for screen 0, any other screens may return"
        " 0 to indicate the screen doesn\'t exist");

    STARTDECL(gl_windowsize) () {
        return ToValueINT(GetScreenSize());
    }
    ENDDECL0(gl_windowsize, "", "", "I}:2",
        "a vector representing the size (in pixels) of the window, changes when the user resizes");

    STARTDECL(gl_mousepos) (Value &i) {
        return ToValueINT(GetFinger(i.intval(), false));
    }
    ENDDECL1(gl_mousepos, "i", "I", "I}:2",
        "the current mouse/finger position in pixels, pass a value other than 0 to read additional"
        " fingers (for touch screens only if the corresponding gl_isdown is true)");

    STARTDECL(gl_mousedelta) (Value &i) {
        return ToValueINT(GetFinger(i.intval(), true));
    }
    ENDDECL1(gl_mousedelta, "i", "I", "I}:2",
        "number of pixels the mouse/finger has moved since the last frame. use this instead of"
        " substracting positions to correctly deal with lifted fingers and FPS mode"
        " (gl_cursor(0))");

    STARTDECL(gl_localmousepos) (Value &i) {
        return ToValueFLT(localfingerpos(i.intval()));
    }
    ENDDECL1(gl_localmousepos, "i", "I", "F}:2",
        "the current mouse/finger position local to the current transform (gl_translate etc)"
        " (for touch screens only if the corresponding gl_isdown is true)");

    STARTDECL(gl_lastpos) (Value &name, Value &on) {
        auto p = GetKeyPos(name.sval()->str(), on.intval());
        name.DECRT();
        return ToValueINT(p);
    }
    ENDDECL2(gl_lastpos, "name,down", "SI", "I}:2",
        "position (in pixels) key/mousebutton/finger last went down (true) or up (false)");

    STARTDECL(gl_locallastpos) (Value &name, Value &on) {
        auto p = localpos(GetKeyPos(name.sval()->str(), on.intval()));
        name.DECRT();
        return ToValueFLT(p);
    }
    ENDDECL2(gl_locallastpos, "name,down", "SI", "F}:2",
        "position (local to the current transform) key/mousebutton/finger last went down (true) or"
        " up (false)");

    STARTDECL(gl_mousewheeldelta) () {
        return Value(SDLWheelDelta());
    }
    ENDDECL0(gl_mousewheeldelta, "", "", "I",
        "amount the mousewheel scrolled this frame, in number of notches");

    STARTDECL(gl_joyaxis) (Value &i) {
        return Value(GetJoyAxis(i.intval()));
    }
    ENDDECL1(gl_joyaxis, "i", "I", "F",
        "the current joystick orientation for axis i, as -1 to 1 value");

    STARTDECL(gl_deltatime) () {
        return Value(SDLDeltaTime());
    }
    ENDDECL0(gl_deltatime, "", "", "F",
        "seconds since the last frame, updated only once per frame");

    STARTDECL(gl_time) () {
        return Value(SDLTime());
    }
    ENDDECL0(gl_time, "", "", "F",
        "seconds since the start of the OpenGL subsystem, updated only once per frame (use"
        " seconds_elapsed() for continuous timing)");

    STARTDECL(gl_lasttime) (Value &name, Value &on) {
        auto t = GetKeyTime(name.sval()->str(), on.intval());
        name.DECRT();
        return Value(t);
    }
    ENDDECL2(gl_lasttime, "name,down", "SI", "F",
        "time key/mousebutton/finger last went down (true) or up (false)");

    STARTDECL(gl_clear) (Value &col) {
        TestGL();
        ClearFrameBuffer(ValueDecToFLT<3>(col));
        return Value();
    }
    ENDDECL1(gl_clear, "col", "F}:4", "",
        "clears the framebuffer (and depth buffer) to the given color");

    STARTDECL(gl_color) (Value &col, Value &body) {
        // FIXME: maybe more efficient as an int
        if (body.True()) g_vm->Push(ToValueFLT(curcolor));
        curcolor = ValueDecToFLT<4>(col);
        return body;
    }
    MIDDECL(gl_color) () {
        curcolor = ValueDecToFLT<4>(g_vm->Pop());
    }
    ENDDECL2CONTEXIT(gl_color, "col,body", "F}:4C?", "",
        "sets the current color. when a body is given, restores the previous color afterwards");

    STARTDECL(gl_polygon) (Value &vl) {
        auto m = CreatePolygon(vl);
        m->Render(currentshader);
        delete m;
        return vl;
    }
    ENDDECL1(gl_polygon, "vertlist", "F}]", "A1",
        "renders a polygon using the list of points given. returns the argument."
        " warning: gl_polygon creates a new mesh every time, gl_newpoly/gl_rendermesh is faster.");

    STARTDECL(gl_circle) (Value &radius, Value &segments) {
        TestGL();

        geomcache->RenderCircle(currentshader, polymode, max(segments.intval(), 3), radius.fltval());

        return Value();
    }
    ENDDECL2(gl_circle, "radius,segments", "FI", "",
        "renders a circle");

    STARTDECL(gl_opencircle) (Value &radius, Value &segments, Value &thickness) {
        TestGL();

        geomcache->RenderOpenCircle(currentshader, max(segments.intval(), 3), radius.fltval(),
                                    thickness.fltval());

        return Value();
    }
    ENDDECL3(gl_opencircle, "radius,segments,thickness", "FIF", "",
        "renders a circle that is open on the inside. thickness is the fraction of the radius that"
        " is filled, try e.g. 0.2");

    STARTDECL(gl_unitcube) (Value &inside) {
        geomcache->RenderUnitCube(currentshader, inside.True());
        return Value();
    }
    ENDDECL1(gl_unitcube, "insideout", "I?", "",
        "renders a unit cube (0,0,0) - (1,1,1). optionally pass true to have it rendered inside"
        " out");

    STARTDECL(gl_rotate_x) (Value &angle, Value &body) {
        auto a = ValueDecToFLT<2>(angle);
        return PushTransform(rotationX(a), rotationX(a * float2(1, -1)), body);
    }
    MIDDECL(gl_rotate_x) () {
        PopTransform();
    }
    ENDDECL2CONTEXIT(gl_rotate_x, "vector,body", "F}:2C?", "",
        "rotates the yz plane around the x axis, using a 2D vector normalized vector as angle."
        " when a body is given, restores the previous transform afterwards");

    STARTDECL(gl_rotate_y) (Value &angle, Value &body) {
        auto a = ValueDecToFLT<2>(angle);
        return PushTransform(rotationY(a), rotationY(a * float2(1, -1)), body);
    }
    MIDDECL(gl_rotate_y) () {
        PopTransform();
    }
    ENDDECL2CONTEXIT(gl_rotate_y, "angle,body", "F}:2C?", "",
        "rotates the xz plane around the y axis, using a 2D vector normalized vector as angle."
        " when a body is given, restores the previous transform afterwards");

    STARTDECL(gl_rotate_z) (Value &angle, Value &body) {
        auto a = ValueDecToFLT<2>(angle);
        return PushTransform(rotationZ(a), rotationZ(a * float2(1, -1)), body);
    }
    MIDDECL(gl_rotate_z) () {
        PopTransform();
    }
    ENDDECL2CONTEXIT(gl_rotate_z, "angle,body", "F}:2C?", "",
        "rotates the xy plane around the z axis (used in 2D), using a 2D vector normalized vector"
        " as angle. when a body is given, restores the previous transform afterwards");

    STARTDECL(gl_translate) (Value &vec, Value &body) {
        auto v = ValueDecToFLT<3>(vec);
        return PushTransform(translation(v), translation(-v), body);
    }
    MIDDECL(gl_translate) () {
        PopTransform();
    }
    ENDDECL2CONTEXIT(gl_translate, "vec,body", "F}C?", "",
        "translates the current coordinate system along a vector. when a body is given,"
        " restores the previous transform afterwards");

    STARTDECL(gl_scale) (Value &f, Value &body) {
        auto v = f.fltval() * float3_1;
        return PushTransform(float4x4(float4(v, 1)), float4x4(float4(float3_1 / v, 1)), body);
    }
    MIDDECL(gl_scale) () {
        PopTransform();
    }
    ENDDECL2CONTEXIT(gl_scale, "factor,body", "FC?", "",
        "scales the current coordinate system using a numerical factor."
        " when a body is given, restores the previous transform afterwards");

    STARTDECL(gl_scale) (Value &vec, Value &body) {
        auto v = ValueDecToFLT<3>(vec, 1);
        return PushTransform(float4x4(float4(v, 1)), float4x4(float4(float3_1 / v, 1)), body);
    }
    MIDDECL(gl_scale) () {
        PopTransform();
    }
    ENDDECL2CONTEXIT(gl_scale, "factor,body", "F}C?", "",
        "scales the current coordinate system using a vector."
        " when a body is given, restores the previous transform afterwards");

    STARTDECL(gl_origin) () {
        auto pos = floatp2(otransforms.object2view[3].x, otransforms.object2view[3].y);
        return ToValueF(pos);
    }
    ENDDECL0(gl_origin, "", "", "F}:2",
        "returns a vector representing the current transform origin in pixels."
        " only makes sense in 2D mode (no gl_perspective called).");

    STARTDECL(gl_scaling) () {
        auto sc = floatp2(otransforms.object2view[0].x, otransforms.object2view[1].y);
        return ToValueF(sc);
    }
    ENDDECL0(gl_scaling, "", "", "F}:2",
        "returns a vector representing the current transform scale in pixels."
        " only makes sense in 2D mode (no gl_perspective called).");

    STARTDECL(gl_modelviewprojection) () {
        auto v = g_vm->NewVec(16, 16, TYPE_ELEM_VECTOR_OF_FLOAT);
        auto mvp = view2clip * otransforms.object2view;
        for (int i = 0; i < 16; i++) v->At(i) = mvp.data()[i];
        return Value(v);
    }
    ENDDECL0(gl_modelviewprojection, "", "", "F]",
             "returns a vector representing the current model view projection matrix"
             " (16 elements)");

    STARTDECL(gl_pointscale) (Value &f) {
        custompointscale = f.fltval();
        return Value();
    }
    ENDDECL1(gl_pointscale, "factor", "F", "",
        "sets the current scaling factor for point sprites."
        " this can be what the current gl_scale is, or different, depending on the desired visuals."
        " the ideal size may also be FOV dependent.");

    STARTDECL(gl_linemode) (Value &on, Value &body) {
        if (body.True()) g_vm->Push(Value(polymode));
        polymode = on.ival() ? PRIM_LOOP : PRIM_FAN;
        return body;
    }
    MIDDECL(gl_linemode) () {
        polymode = (Primitive)g_vm->Pop().ival();
    }
    ENDDECL2CONTEXIT(gl_linemode, "on,body", "IC", "",
        "set line mode (true == on). when a body is given,"
        " restores the previous mode afterwards");

    STARTDECL(gl_hit) (Value &vec, Value &i) {
        auto size = ValueDecToFLT<3>(vec);
        auto localmousepos = localfingerpos(i.intval());
        auto hit = localmousepos.x >= 0 &&
                   localmousepos.y >= 0 &&
                   localmousepos.x < size.x &&
                   localmousepos.y < size.y;
        if (hit) lasthitsize = size;
        /*
        #ifdef PLATFORM_TOUCH
        // Inefficient for fingers other than 0, which is going to be rare.
        auto ks = i ? GetKS((string("mouse1") + (char)('0' + i)).c_str()) : GetKS("mouse1");
        // On mobile, if the finger just went down, we wont have meaningfull lastframehitsize, so if
        // the programmer checks for the combination of gl_hit and gl_wentdown, that would fail.
        // Instead, we bypass that check.
        // PROBLEM: now we'll be returning true for overlapping elements.
        // if we can solve this, we can remove the frame delay from the input system.
        if (ks.wentdown && hit) return true;
        #endif
        */
        return Value(size == lastframehitsize && hit);
    }
    ENDDECL2(gl_hit, "vec,i", "F}I", "I",
        "wether the mouse/finger is inside of the rectangle specified in terms of the current"
        " transform (for touch screens only if the corresponding gl_isdown is true). Only true if"
        " the last rectangle for which gl_hit was true last frame is of the same size as this one"
        " (allows you to safely test in most cases of overlapping rendering)");

    STARTDECL(gl_rect) (Value &vec, Value &centered) {
        TestGL();
        geomcache->RenderQuad(currentshader, polymode, centered.True(),
                              float4x4(float4(ValueToFLT<2>(vec), 1)));
        return vec;
    }
    ENDDECL2(gl_rect, "size,centered", "F}I?", "F}",
        "renders a rectangle (0,0)..(1,1) (or (-1,-1)..(1,1) when centered), scaled by the given"
        " size. returns the argument.");

    STARTDECL(gl_unit_square) (Value &centered) {
        TestGL();
        geomcache->RenderUnitSquare(currentshader, polymode, centered.True());
        return Value();
    }
    ENDDECL1(gl_unit_square, "centered", "I?", "",
        "renders a square (0,0)..(1,1) (or (-1,-1)..(1,1) when centered)");

    STARTDECL(gl_line) (Value &start, Value &end, Value &thickness) {
        TestGL();
        auto v1 = ValueDecToFLT<3>(start);
        auto v2 = ValueDecToFLT<3>(end);
        if (Is2DMode()) geomcache->RenderLine2D(currentshader, polymode, v1, v2, thickness.fltval());
        else geomcache->RenderLine3D(currentshader, v1, v2, float3_0, thickness.fltval());
        return Value();
    }
    ENDDECL3(gl_line, "start,end,thickness", "F}F}F", "",
        "renders a line with the given thickness");

    STARTDECL(gl_perspective) (Value &fovy, Value &znear, Value &zfar) {
        Set3DMode(fovy.fltval() * RAD, GetScreenSize().x / (float)GetScreenSize().y, znear.fltval(),
                  zfar.fltval());
        return Value();
    }
    ENDDECL3(gl_perspective, "fovy,znear,zfar", "FFF", "",
        "changes from 2D mode (default) to 3D right handed perspective mode with vertical fov (try"
        " 60), far plane (furthest you want to be able to render, try 1000) and near plane (try"
        " 1)");

    STARTDECL(gl_ortho) (Value &rh) {
        Set2DMode(GetScreenSize(), !rh.True());
        return Value();
    }
    ENDDECL1(gl_ortho, "rh", "I?", "",
        "changes back to 2D mode rendering with a coordinate system from (0,0) top-left to the"
        " screen size in pixels bottom right. this is the default at the start of a frame, use this"
        " call to get back to that after gl_perspective."
        " Pass true to have (0,0) bottom-left instead");

    STARTDECL(gl_ortho3d) (Value &center, Value &extends) {
        Set3DOrtho(ValueDecToFLT<3>(center), ValueDecToFLT<3>(extends));
        return Value();
    }
    ENDDECL2(gl_ortho3d, "center,extends", "F}F}", "",
        "sets a custom ortho projection as 3D projection.");

    STARTDECL(gl_newmesh) (Value &format, Value &positions, Value &colors,
                           Value &normals, Value &texcoords1, Value &texcoords2, Value &indices) {
        TestGL();
        auto nattr = format.sval()->len;
        if (nattr < 1 || nattr > 10)
            g_vm->BuiltinError("newmesh: illegal format/attributes size");
        auto fmt = format.sval()->str();
        if (nattr != (int)strspn(fmt, "PCTN") || fmt[0] != 'P')
            g_vm->BuiltinError("newmesh: illegal format characters (only PCTN allowed), P must be"
                               " first");
        intp nverts = positions.vval()->len;
        vector<int> idxs;
        if (indices.True()) {
            for (int i = 0; i < indices.vval()->len; i++) {
                auto &e = indices.vval()->At(i);
                if (e.ival() < 0 || e.ival() >= nverts)
                    g_vm->BuiltinError("newmesh: index out of range of vertex list");
                idxs.push_back(e.intval());
            }
            indices.DECRTNIL();
        }
        size_t vsize = AttribsSize(fmt);
        size_t normal_offset = 0;
        auto verts = new uchar[nverts * vsize];
        for (intp i = 0; i < nverts; i++) {
            auto start = &verts[i * vsize];
            auto p = start;
            auto fmt_it = fmt;
            float3 pos;
            int texcoordn = 0;
            while (*fmt_it) {
                switch (*fmt_it++) {
                    case 'P':
                        *((float3 *&)p)++ = pos = ValueToFLT<3>(positions.vval()->At(i));
                        break;
                    case 'C':
                        *((byte4  *&)p)++ =
                            i < colors.vval()->len
                                ? quantizec(ValueToFLT<4>(colors.vval()->At(i), 1))
                                : byte4_255;
                        break;
                    case 'T': {
                        auto &texcoords = texcoordn ? texcoords2 : texcoords1;
                        *((float2 *&)p)++ =
                            i < texcoords.vval()->len
                                ? ValueToFLT<2>(texcoords.vval()->At(i), 0)
                                : pos.xy();
                        texcoordn++;
                        break;
                    }
                    case 'N':
                        if (!normals.vval()->len) normal_offset = p - start;
                        *((float3 *&)p)++ =
                            i < normals.vval()->len
                                ? ValueToFLT<3>(normals.vval()->At(i), 0)
                                : float3_0;
                        break;
                    default: assert(0);
                }
            }
        }
        if (normal_offset) {
            // if no normals were specified, generate them.
            normalize_mesh(&idxs[0], idxs.size(), verts, nverts, vsize, normal_offset);
        }
        // FIXME: make meshes into points in a more general way.
        auto m = new Mesh(new Geometry(verts, nverts, vsize, fmt),
                          indices.True() ? PRIM_TRIS : PRIM_POINT);
        if (idxs.size()) m->surfs.push_back(new Surface(&idxs[0], idxs.size()));
        delete[] verts;
        format.DECRT();
        positions.DECRT();
        colors.DECRT();
        normals.DECRT();
        texcoords1.DECRT();
        texcoords2.DECRT();
        return Value(g_vm->NewResource(m, &mesh_type));
    }
    ENDDECL7(gl_newmesh, "format,positions,colors,normals,texcoords1,texcoords2,indices",
             "SF}:3]F}:4]F}:3]F}:2]F}:2]I]?", "X",
        "creates a new vertex buffer and returns an integer id (1..) for it."
        " format must be made up of characters P (position), C (color), T (texcoord), N (normal)."
        " indices may be []. positions is obligatory."
        " you may specify [] for any of the other attributes if not required by format,"
        " or to get defaults for colors (white) / texcoords (position x & y) /"
        " normals (generated from adjacent triangles).")

    STARTDECL(gl_newpoly) (Value &positions) {
        auto m = CreatePolygon(positions);
        positions.DECRT();
        return Value(g_vm->NewResource(m, &mesh_type));
    }
    ENDDECL1(gl_newpoly, "positions", "F}]", "X",
        "creates a mesh out of a loop of points, much like gl_polygon."
        " gl_linemode determines how this gets drawn (fan or loop)."
        " returns mesh id");

    STARTDECL(gl_newmesh_iqm) (Value &fn) {
        TestGL();
        auto m = LoadIQM(fn.sval()->str());
        fn.DECRT();
        return m ? Value(g_vm->NewResource(m, &mesh_type)) : Value();
    }
    ENDDECL1(gl_newmesh_iqm, "filename", "S", "X?",
        "load a .iqm file into a mesh, returns mesh or nil on failure to load.");

    STARTDECL(gl_meshparts) (Value &i) {
        auto &m = GetMesh(i);
        auto v = (LVector *)g_vm->NewVec(0, (int)m.surfs.size(), TYPE_ELEM_VECTOR_OF_STRING);
        for (auto s : m.surfs) v->Push(Value(g_vm->NewString(s->name)));
        return Value(v);
    }
    ENDDECL1(gl_meshparts, "m", "X", "S]",
        "returns an array of names of all parts of mesh m (names may be empty)");

    STARTDECL(gl_meshsize) (Value &i) {
        auto &m = GetMesh(i);
        return Value((int)m.geom->nverts);
    }
    ENDDECL1(gl_meshsize, "m", "X", "I",
        "returns the number of verts in this mesh");

    STARTDECL(gl_animatemesh) (Value &i, Value &f) {
        GetMesh(i).curanim = f.fltval();
        return Value();
    }
    ENDDECL2(gl_animatemesh, "m,frame", "XF", "",
        "set the frame for animated mesh m");

    STARTDECL(gl_rendermesh) (Value &i) {
        TestGL();
        GetMesh(i).Render(currentshader);
        return Value();
    }
    ENDDECL1(gl_rendermesh, "m", "X", "",
        "renders the specified mesh");

    STARTDECL(gl_savemesh) (Value &i, Value &name) {
        TestGL();
        bool ok = GetMesh(i).SaveAsPLY(name.sval()->str());
        name.DECRT();
        return Value(ok);
    }
    ENDDECL2(gl_savemesh, "m,name", "XS", "I",
        "saves the specified mesh to a file in the PLY format. useful if the mesh was generated"
        " procedurally. returns false if the file could not be written");

    STARTDECL(gl_setshader) (Value &shader) {
        TestGL();
        auto sh = LookupShader(shader.sval()->str());
        if (!sh) g_vm->BuiltinError(string("no such shader: ") + shader.sval()->str());
        shader.DECRT();
        currentshader = sh;
        return Value();
    }
    ENDDECL1(gl_setshader, "shader", "S", "",
        "changes the current shader. shaders must reside in the shaders folder, builtin ones are:"
        " color / textured / phong");

    STARTDECL(gl_setuniform) (Value &name, Value &vec, Value &ignore_errors) {
        auto v = ValueToFLT<4>(vec);
        auto r = SetUniform(name, v.begin(), (int)vec.stval()->Len(), ignore_errors.True());
        vec.DECRT();
        return r;
    }
    ENDDECL3(gl_setuniform, "name,value,ignore_errors", "SF}I?", "I",
        "set a uniform on the current shader. size of float vector must match size of uniform"
        " in the shader.");

    STARTDECL(gl_setuniform) (Value &name, Value &vec, Value &ignore_errors) {
        auto f = vec.fltval();
        return SetUniform(name, &f, 1, ignore_errors.True());
    }
    ENDDECL3(gl_setuniform, "name,value,ignore_errors", "SFI?", "I",
        "set a uniform on the current shader. uniform"
        " in the shader must be a single float.");

    STARTDECL(gl_setuniformarray) (Value &name, Value &vec) {
        TestGL();
        vector<float4> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++) vals[i] = ValueToFLT<4>(vec.vval()->At(i));
        vec.DECRT();
        currentshader->Activate();
        auto ok = currentshader->SetUniform(name.sval()->str(), vals.data()->data(), 4,
                                            (int)vals.size());
        name.DECRT();
        return Value(ok);
    }
    ENDDECL2(gl_setuniformarray, "name,value", "SF}:4]", "I",
             "set a uniform on the current shader. uniform in the shader must be an array of vec4."
             " returns false on error.");

    STARTDECL(gl_setuniformmatrix) (Value &name, Value &vec) {
        TestGL();
        vector<float> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++) vals[i] = vec.vval()->At(i).fltval();
        vec.DECRT();
        currentshader->Activate();
        auto ok = currentshader->SetUniformMatrix(name.sval()->str(), vals.data(),
                                                  (int)vals.size(), 1);
        name.DECRT();
        return Value(ok);
    }
    ENDDECL2(gl_setuniformmatrix, "name,value", "SF]", "I",
             "set a uniform on the current shader. pass a vector of 4/9/12/16 floats to set a"
             " mat2/mat3/mat3x4/mat4 respectively. returns false on error.");

    STARTDECL(gl_uniformbufferobject) (Value &name, Value &vec, Value &ssbo) {
        TestGL();
        vector<float4> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++) vals[i] = ValueToFLT<4>(vec.vval()->At(i));
        vec.DECRT();
        auto id = UniformBufferObject(currentshader, vals.data()->data(),
                                      4 * sizeof(float) * vals.size(),
                                      name.sval()->str(), ssbo.True());
        name.DECRT();
        return Value((int)id);
    }
    ENDDECL3(gl_uniformbufferobject, "name,value,ssbo", "SF}:4]I?", "I",
        "creates a uniform buffer object, and attaches it to the current shader at the given"
        " uniform block name. uniforms in the shader must be all vec4s, or an array of them."
        " ssbo indicates if you want a shader storage block instead."
        " returns buffer id or 0 on error.");

    STARTDECL(gl_deletebufferobject) (Value &id) {
        TestGL();
        // FIXME: should route this thru a IntResourceManagerCompact to be safe?
        // I guess GL doesn't care about illegal id's?
        DeleteBO(id.intval());
        return Value();
    }
    ENDDECL1(gl_deletebufferobject, "id", "I", "",
        "deletes a buffer objects, e.g. one allocated by gl_uniformbufferobject().");

    STARTDECL(gl_bindmeshtocompute) (Value &mesh, Value &bpi) {
        TestGL();
        if (mesh.True()) GetMesh(mesh).geom->BindAsSSBO(bpi.intval());
        else BindVBOAsSSBO(bpi.intval(), 0);
        return Value();
    }
    ENDDECL2(gl_bindmeshtocompute, "mesh,binding", "X?I", "",
        "Bind the vertex data of a mesh to a SSBO binding of a compute shader. Pass a nil mesh to"
        " unbind.");

    STARTDECL(gl_dispatchcompute) (Value &groups) {
        TestGL();
        DispatchCompute(ValueDecToINT<3>(groups));
        return Value();
    }
    ENDDECL1(gl_dispatchcompute, "groups", "I}:3", "",
        "dispatches the currently set compute shader in groups of sizes of the specified x/y/z"
        " values.");

    STARTDECL(gl_dumpshader) (Value &filename, Value &stripnonascii) {
        TestGL();
        currentshader->Activate();
        auto ok = currentshader->Dump(filename.sval()->str(), stripnonascii.True());
        filename.DECRT();
        return Value(ok);
    }
    ENDDECL2(gl_dumpshader, "filename,stripnonascii", "SI", "I",
        "Dumps the compiled (binary) version of the current shader to a file. Contents are driver"
        " dependent. On Nvidia hardware it contains the assembly version of the shader as text,"
        " pass true for stripnonascii if you're only interested in that part.");

    STARTDECL(gl_blend) (Value &mode, Value &body) {
        TestGL();
        int old = SetBlendMode((BlendMode)mode.ival());
        if (body.True()) g_vm->Push(Value(old));
        return body;
    }
    MIDDECL(gl_blend) () {
        auto m = g_vm->Pop();
        TYPE_ASSERT(m.type == V_INT);
        SetBlendMode((BlendMode)m.ival());
    }
    ENDDECL2CONTEXIT(gl_blend, "on,body", "IC?", "",
        "changes the blending mode (use blending constants from color.lobster). when a body is"
        " given, restores the previous mode afterwards");

    STARTDECL(gl_loadtexture) (Value &name, Value &tf) {
        TestGL();
        auto tex = CreateTextureFromFile(name.sval()->str(), tf.intval());
        name.DECRT();
        return tex.id ? g_vm->NewResource(new Texture(tex), &texture_type) : Value();
    }
    ENDDECL2(gl_loadtexture, "name,textureformat", "SI?", "X?",
        "returns texture if succesfully loaded from file name, otherwise nil."
        " see color.lobster for texture format. Uses stb_image internally"
        " (see http://nothings.org/), loads JPEG Baseline, subsets of PNG, TGA, BMP, PSD, GIF, HDR,"
        " PIC.");

    STARTDECL(gl_setprimitivetexture) (Value &i, Value &id, Value &tf) {
        TestGL();
        SetTexture(GetSampler(i), GetTexture(id), tf.intval());
        return Value();
    }
    ENDDECL3(gl_setprimitivetexture, "i,tex,textureformat", "IXI?", "",
        "sets texture unit i to texture (for use with rect/circle/polygon/line)");

    STARTDECL(gl_setmeshtexture) (Value &mid, Value &part, Value &i, Value &id) {
        auto &m = GetMesh(mid);
        if (part.ival() < 0 || part.ival() >= (int)m.surfs.size())
            g_vm->BuiltinError("setmeshtexture: illegal part index");
        m.surfs[part.ival()]->Get(GetSampler(i)) = GetTexture(id);
        return Value();
    }
    ENDDECL4(gl_setmeshtexture, "mesh,part,i,texture", "XIIX", "",
        "sets texture unit i to texture for a mesh and part (0 if not a multi-part mesh)");

    STARTDECL(gl_setimagetexture) (Value &i, Value &id, Value &tf) {
        TestGL();
        SetImageTexture(GetSampler(i), GetTexture(id), tf.intval());
        return Value();
    }
    ENDDECL3(gl_setimagetexture, "i,tex,textureformat", "IXI", "",
        "sets image unit i to texture (for use with compute). texture format must be the same"
        " as what you specified in gl_loadtexture / gl_createtexture,"
        " with optionally writeonly/readwrite flags.");

    STARTDECL(gl_createtexture) (Value &matv, Value &tf) {
        TestGL();
        auto mat = matv.vval();
        auto ys = mat->len;
        auto xs = mat->At(0).vval()->len;
        auto sz = tf.ival() & TF_FLOAT ? sizeof(float4) : sizeof(byte4);
        auto buf = new uchar[xs * ys * sz];
        memset(buf, 0, xs * ys * sz);
        for (int i = 0; i < ys; i++) {
            auto row = mat->At(i).vval();
            for (int j = 0; j < min(xs, row->len); j++) {
                float4 col = ValueToFLT<4>(row->At(j));
                auto idx = i * xs + j;
                if (tf.ival() & TF_FLOAT) ((float4 *)buf)[idx] = col;
                else                      ((byte4  *)buf)[idx] = quantizec(col);
            }
        }
        matv.DECRT();
        auto tex = CreateTexture(buf, int2(intp2(xs, ys)).data(), tf.intval());
        delete[] buf;
        return Value(g_vm->NewResource(new Texture(tex), &texture_type));
    }
    ENDDECL2(gl_createtexture, "matrix,textureformat", "F}:4]]I?", "X",
        "creates a texture from a 2d array of color vectors."
        " see texture.lobster for texture format");

    STARTDECL(gl_createblanktexture) (Value &size_, Value &col, Value &tf) {
        TestGL();
        auto tex = CreateBlankTexture(ValueDecToINT<2>(size_), ValueDecToFLT<4>(col), tf.intval());
        return Value(g_vm->NewResource(new Texture(tex), &texture_type));
    }
    ENDDECL3(gl_createblanktexture, "size,color,textureformat", "I}:2F}:4I?", "X",
        "creates a blank texture (for use as frame buffer or with compute shaders)."
        " see texture.lobster for texture format");

    STARTDECL(gl_texturesize) (Value &tex) {
        TestGL();
        return ToValueINT(GetTexture(tex).size.xy());
    }
    ENDDECL1(gl_texturesize, "tex", "X", "I}:2",
        "returns the size of a texture");

    STARTDECL(gl_readtexture) (Value &t) {
        TestGL();
        auto tex = GetTexture(t);
        auto numpixels = tex.size.x * tex.size.y;
        if (!numpixels) return Value();
        auto buf = ReadTexture(tex);
        if (!buf) return Value();
        auto s = g_vm->NewString((char *)buf, numpixels * 4);
        delete[] buf;
        return Value(s);
    }
    ENDDECL1(gl_readtexture, "tex", "X", "S?",
        "read back RGBA texture data into a string or nil on failure");

    STARTDECL(gl_switchtoframebuffer) (Value &t, Value &depth, Value &tf, Value &retex,
                                       Value &depthtex) {
        TestGL();
        auto tex = GetTexture(t);
        return Value(SwitchToFrameBuffer(tex.id ? tex : Texture(0, GetScreenSize()),
                                         depth.True(), tf.intval(), GetTexture(retex),
                                         GetTexture(depthtex)));
    }
    ENDDECL5(gl_switchtoframebuffer, "tex,hasdepth,textureformat,resolvetex,depthtex", "X?I?I?X?X?",
        "I",
        "switches to a new framebuffer, that renders into the given texture."
        " also allocates a depth buffer for it if depth is true."
        " pass the textureformat that was used for this texture."
        " pass a resolve texture if the base texture is multisample."
        " pass your own depth texture if desired."
        " pass a nil texture to switch back to the original framebuffer."
        " performance note: do not recreate texture passed in unless necessary.");

    STARTDECL(gl_light) (Value &pos, Value &params) {
        Light l;
        l.pos = otransforms.object2view * float4(ValueDecToFLT<3>(pos), 1);
        l.params = ValueDecToFLT<2>(params);
        lights.push_back(l);
        return Value();
    }
    ENDDECL2(gl_light, "pos,params", "F}:3F}:2", "",
        "sets up a light at the given position for this frame. make sure to call this after your"
        " camera transforms but before any object transforms (i.e. defined in \"worldspace\")."
        " params contains specular exponent in x (try 32/64/128 for different material looks) and"
        " the specular scale in y (try 1 for full intensity)");

    STARTDECL(gl_rendertiles) (Value &pos, Value &tile, Value &mapsize) {
        TestGL();
        auto msize = float2(ValueDecToI<2>(mapsize));
        auto len = pos.vval()->len;
        if (len != tile.vval()->len)
            g_vm->BuiltinError("rendertiles: vectors of different size");
        auto vbuf = new SpriteVert[len * 6];
        for (intp i = 0; i < len; i++) {
            auto p = ValueToFLT<2>(pos.vval()->At(i));
            auto t = float2(ValueToI<2>(tile.vval()->At(i))) / msize;
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
        pos.DECRT();
        tile.DECRT();
        currentshader->Set();
        RenderArraySlow(PRIM_TRIS, (int)len * 6, "pT", sizeof(SpriteVert), vbuf);
        delete[] vbuf;
        return Value();
    }
    ENDDECL3(gl_rendertiles, "positions,tilecoords,mapsize", "F}:2]I}:2]I}:2", "",
        "Renders a list of tiles from a tilemap. Each tile rendered is 1x1 in size."
        " Positions may be anywhere. Tile coordinates are inside the texture map, map size is"
        " the amount of tiles in the texture. Tiles may overlap, they are drawn in order."
        " Before calling this, make sure to have the texture set and a textured shader");

    STARTDECL(gl_recttc) (Value &size, Value &tc, Value &tcdim) {
        TestGL();
        auto sz = ValueDecToFLT<2>(size);
        auto t = ValueDecToFLT<2>(tc);
        auto td = ValueDecToFLT<2>(tcdim);
        auto te = t + td;
        float vb_square[20] = {
            0,      0,      0, t.x,  t.y,
            0,      sz.y, 0, t.x,  te.y,
            sz.x, sz.y, 0, te.x, te.y,
            sz.x, 0,      0, te.x, t.y,
        };
        currentshader->Set();
        RenderArraySlow(PRIM_FAN, 4, "PT", sizeof(float) * 5, vb_square);
        return Value();
    }
    ENDDECL3(gl_recttc, "size,tc,tcsize", "F}:2F}:2F}:2", "",
        "Like gl_rect renders a sized quad, but allows you to specify texture coordinates. Slow.");

    STARTDECL(gl_debug_grid) (Value &num, Value &dist, Value &thickness) {
        TestGL();
        float3 cp = otransforms.view2object[3].xyz();
        auto m = float3(ValueDecToI<3>(num));
        auto step = ValueDecToFLT<3>(dist);
        auto oldcolor = curcolor;
        curcolor = float4(0, 1, 0, 1);
        for (float z = 0; z <= m.z; z += step.x) {
            for (float x = 0; x <= m.x; x += step.x) {
                geomcache->RenderLine3D(currentshader, float3(x, 0, z), float3(x, m.y, z), cp,
                             thickness.fltval());
            }
        }
        curcolor = float4(1, 0, 0, 1);
        for (float z = 0; z <= m.z; z += step.y) {
            for (float y = 0; y <= m.y; y += step.y) {
                geomcache->RenderLine3D(currentshader, float3(0, y, z), float3(m.x, y, z), cp,
                    thickness.fltval());
            }
        }
        curcolor = float4(0, 0, 1, 1);
        for (float y = 0; y <= m.y; y += step.z) {
            for (float x = 0; x <= m.x; x += step.z) {
                geomcache->RenderLine3D(currentshader, float3(x, y, 0), float3(x, y, m.z), cp,
                    thickness.fltval());
            }
        }
        curcolor = oldcolor;
        return Value();
    }
    ENDDECL3(gl_debug_grid, "num,dist,thickness", "I}:3F}:3F", "",
        "renders a grid in space for debugging purposes. num is the number of lines in all 3"
        " directions, and dist their spacing. thickness of the lines in the same units");

    STARTDECL(gl_screenshot) (Value &fn) {
        bool ok = ScreenShot(fn.sval()->str());
        fn.DECRT();
        return Value(ok);
    }
    ENDDECL1(gl_screenshot, "filename", "S", "I",
             "saves a screenshot in .png format, returns true if succesful");

}
