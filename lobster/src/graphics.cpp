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

#include "lobster/graphics.h"

using namespace lobster;

Primitive polymode = PRIM_FAN;
Shader *currentshader = NULL;
Shader *colorshader = NULL;
float3 lasthitsize = float3_0;
float3 lastframehitsize = float3_0;
bool graphics_initialized = false;

ResourceType mesh_type = { "mesh" };
ResourceType texture_type = { "texture" };
ResourceType shader_type = { "shader" };

Mesh &GetMesh(Value &res) {
    return GetResourceDec<Mesh>(res, &mesh_type);
}
Texture GetTexture(const Value &res) {
    if (res.False()) return Texture();
    return GetResourceDec<OwnedTexture>(res, &texture_type).t;
}
Shader &GetShader(Value &res) {
    return GetResourceDec<Shader>(res, &shader_type);
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
    OpenGLFrameEnd();
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

// This function can also be called directly from the native backends, to avoid
// an indirect call (which is important in Wasm).
extern "C" void GLFrame(StackPtr sp, VM &vm) {
    TestGL(vm);
    auto cb = GraphicsFrameStart();
    Push(sp, Value(!cb));
}

float2 localpos(const int2 &pos) {
    return (otransforms.view2object() * float4(float3(float2(pos), 0), 1)).xyz().xy();
}
float2 localfingerpos(int i) {
    return localpos(GetFinger(i, false));
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
    auto m = new Mesh(new Geometry("CreatePolygon", gsl::make_span(vbuf), "PNTC"), polymode);
    return m;
}

Value SetUniform(VM &vm, const Value &name, const float *data, int len) {
    TestGL(vm);
    currentshader->Activate();
    auto ok = currentshader->SetUniform(name.sval()->strv(), data, len);
    return Value(ok);
}

Value SetUniform(VM &vm, const Value &name, const int *data, int len) {
    TestGL(vm);
    currentshader->Activate();
    auto ok = currentshader->SetUniform(name.sval()->strv(), data, len);
    return Value(ok);
}

void AddGraphics(NativeRegistry &nfr) {

nfr("gl_window", "title,xs,ys,flags,samples", "SIII?I?:1", "S?",
    "opens a window for OpenGL rendering. returns error string if any problems, nil"
    " otherwise. For flags, see modules/gl.lobster",
    [](StackPtr &, VM &vm, Value &title, Value &xs, Value &ys, Value &flags, Value &samples) {
        if (graphics_initialized)
            vm.BuiltinError("cannot call gl_window() twice");
        string err = SDLInit(title.sval()->strv(), int2(iint2(xs.ival(), ys.ival())),
                             (InitFlags)flags.intval(), max(1, samples.intval()));
        if (err.empty()) {
            err = LoadMaterialFile("data/shaders/default.materials");
        }
        if (!err.empty()) {
            LOG_INFO(err);
            return Value(vm.NewString(err));
        }
        colorshader = LookupShader("color");
        assert(colorshader);
        currentshader = colorshader;
        LOG_INFO("graphics fully initialized...");
        graphics_initialized = true;
        atexit(GraphicsShutDown);
        return NilVal();
    });

nfr("gl_require_version", "major,minor", "II", "",
    "Call this before gl_window to request a certain version of OpenGL context."
            " Currently only works on win/nix, minimum is 3.2.",
    [](StackPtr &, VM &, Value &major, Value &minor) {
        SDLRequireGLVersion(major.intval(), minor.intval());
        return NilVal();
    });

nfr("gl_load_materials", "materialdefs,inline", "SI?", "S?",
    "loads an additional materials file (data/shaders/default.materials is already loaded by default"
    " by gl_window()). if inline is true, materialdefs is not a filename, but the actual"
    " materials. returns error string if any problems, nil otherwise.",
    [](StackPtr &, VM &vm, Value &fn, Value &isinline) {
        TestGL(vm);
        auto err = isinline.True() ? ParseMaterialFile(fn.sval()->strv())
                                   : LoadMaterialFile(fn.sval()->strv());
        return err[0] ? Value(vm.NewString(err)) : NilVal();
    });

nfr("gl_scissor", "top_left,size", "I}:2I}:2", "I}:2I}:2",
    "Sets the scissor testing, so only the pixels in the given rectangle can"
    "be written.  Returns the previous value of the scissor rectangle.",
    [](StackPtr &sp, VM &vm) {
        auto size = PopVec<int2>(sp);
        auto topleft = PopVec<int2>(sp);
        TestGL(vm);
        pair<int2, int2> prev;

        SetScissorRect(topleft, size, prev);
        PushVec(sp, prev.first);
        PushVec(sp, prev.second);
    });

nfr("gl_frame", "", "", "B",
    "advances rendering by one frame, swaps buffers, and collects new input events."
    " returns false if the closebutton on the window was pressed",
    [](StackPtr &sp, VM &vm) {
        // Native backends call this directly rather than going thru the function pointer.
        GLFrame(sp, vm);
    });

nfr("gl_shutdown", "", "", "",
    "shuts down the OpenGL window. you only need to call this function if you wish to close it"
    " before the end of the program",
    [](StackPtr &, VM &) {
        GraphicsShutDown();
        return NilVal();
    });

nfr("gl_window_title", "title", "S", "Sb",
    "changes the window title.",
    [](StackPtr &, VM &vm, Value &s) {
        TestGL(vm);
        SDLTitle(s.sval()->strv());
        return s;
    });

nfr("gl_window_min_max", "dir", "I", "",
    ">0 to maximize, <0 to minimize or 0 to restore.",
    [](StackPtr &, VM &vm, Value &dir) {
        TestGL(vm);
        SDLWindowMinMax(dir.intval());
        return NilVal();
    });

nfr("gl_visible", "", "", "B",
    "checks if the window is currently visible (not minimized, or on mobile devices, in the"
    " foreground). If false, you should not render anything, nor run the frame's code.",
    [](StackPtr &, VM &) {
        return Value(!SDLIsMinimized());
    });

nfr("gl_cursor", "on", "B", "B",
    "default the cursor is visible, turn off for implementing FPS like control schemes. return"
    " whether it's on.",
    [](StackPtr &, VM &vm, Value &on) {
        TestGL(vm);
        return Value(SDLCursor(on.ival() != 0));
    });

nfr("gl_grab", "on", "B", "B",
    "grabs the mouse when the window is active. return whether it's on.",
    [](StackPtr &, VM &vm, Value &on) {
        TestGL(vm);
        return Value(SDLGrab(on.ival() != 0));
    });

nfr("gl_button", "name", "S", "II",
    "returns the number of frames a key/mousebutton/finger has been down."
    " went down this frame: == 1, is already down: >= 1, not down: == 0."
    " the second return value is the same, but for the up direction:"
    " went up this frame: == 1, is already up: >= 1, not up: == 0."
    " it is possible both happen inside one frame, i.e. down==1 and up==1! "
    " for name, pass a string like mouse1/mouse2/mouse3/escape/space/up/down/a/b/f1/joy1 etc."
    " mouse11 and on are additional fingers",
    [](StackPtr &sp, VM &, Value &name) {
        auto ks = GetKS(name.sval()->strv());
        Push(sp, ks.first);
        return Value(ks.second);
    });

nfr("gl_key_repeat", "name", "S", "B",
    "returns if a key was a key repeat (went down, or is down with a key repeat)",
    [](StackPtr &, VM &, Value &name) {
        auto ks = GetKS(name.sval()->strv());
        return Value(ks.first == 1 || (ks.first > 1 && KeyRepeat(name.sval()->strv())));
    });

nfr("gl_start_text_input", "pos,size", "I}:2I}:2", "",
    "starts text input. unlike gl_button which gets you keyboard keys, this is for input of"
    " strings, that can deal with unicode IME etc. pos & size are a hint where the string"
    " being edited is being displayed, such that an IME can popup a box next to it, if needed.",
    [](StackPtr &sp, VM &) {
        auto pos = PopVec<int2>(sp);
        auto size = PopVec<int2>(sp);
        SDLStartTextInput(pos, size);
    });

nfr("gl_text_input_state", "", "", "SSII",
    "returns the string that has been input since text input started, followed by any candinate"
    " text (partial characters in case of IME editing), and the cursor & selection size for it",
    [](StackPtr &sp, VM &vm) {
        auto &ti = SDLTextInputState();
        Push(sp, vm.NewString(ti.text));
        Push(sp, vm.NewString(ti.editing));
        Push(sp, ti.cursor);
        Push(sp, ti.len);
    });

nfr("gl_set_text_input", "text", "S", "",
    "overwrites the current text string being accumulated",
    [](StackPtr &, VM &, Value &text) {
        SDLTextInputSet(text.sval()->strv());
        return NilVal();
    });

nfr("gl_end_text_input", "", "", "",
    "stops accumulating text input",
    [](StackPtr &, VM &) {
        SDLEndTextInput();
        return NilVal();
    });

nfr("gl_touchscreen", "", "", "B",
    "whether a you\'re getting input from a touch screen (as opposed to mouse & keyboard)",
    [](StackPtr &, VM &) {
        #ifdef PLATFORM_TOUCH
            return Value(true);
        #else
            return Value(false);
        #endif
    });

nfr("gl_dpi", "screen", "I", "I",
    "the DPI of the screen. always returns a value for screen 0, any other screens may return"
    " 0 to indicate the screen doesn\'t exist",
    [](StackPtr &, VM &, Value &screen) {
        return Value(SDLScreenDPI(screen.intval()));
    });

nfr("gl_window_size", "", "", "I}:2",
    "a vector representing the size (in pixels) of the window, changes when the user resizes",
    [](StackPtr &sp, VM &) {
        PushVec(sp, GetScreenSize());
    });

nfr("gl_mouse_pos", "i", "I", "I}:2",
    "the current mouse/finger position in pixels, pass a value other than 0 to read additional"
    " fingers (for touch screens only if the corresponding gl_isdown is true)",
    [](StackPtr &sp, VM &) {
        PushVec(sp, GetFinger(Pop(sp).intval(), false));
    });

nfr("gl_mouse_delta", "i", "I", "I}:2",
    "number of pixels the mouse/finger has moved since the last frame. use this instead of"
    " substracting positions to correctly deal with lifted fingers and FPS mode"
    " (gl_cursor(0))",
    [](StackPtr &sp, VM &) {
        PushVec(sp, GetFinger(Pop(sp).intval(), true));
    });

nfr("gl_local_mouse_pos", "i", "I", "F}:2",
    "the current mouse/finger position local to the current transform (gl_translate etc)"
    " (for touch screens only if the corresponding gl_isdown is true)",
    [](StackPtr &sp, VM &) {
        PushVec(sp, localfingerpos(Pop(sp).intval()));
    });

nfr("gl_last_pos", "name,down", "SI", "I}:2",
    "position (in pixels) key/mousebutton/finger last went down (true) or up (false)",
    [](StackPtr &sp, VM &) {
        auto on = Pop(sp).intval();
        auto name = Pop(sp).sval();
        auto p = GetKeyPos(name->strv(), on);
        PushVec(sp, p);
    });

nfr("gl_local_last_pos", "name,down", "SI", "F}:2",
    "position (local to the current transform) key/mousebutton/finger last went down (true) or"
    " up (false)",
    [](StackPtr &sp, VM &) {
        auto on = Pop(sp).intval();
        auto name = Pop(sp).sval();
        auto p = localpos(GetKeyPos(name->strv(), on));
        PushVec(sp, p);
    });

nfr("gl_mousewheel_delta", "", "", "I",
    "amount the mousewheel scrolled this frame, in number of notches",
    [](StackPtr &, VM &) {
        return Value(SDLWheelDelta());
    });

nfr("gl_joy_axis", "i", "I", "F",
    "the current joystick orientation for axis i, as -1 to 1 value",
    [](StackPtr &, VM &, Value &i) {
        return Value(GetJoyAxis(i.intval()));
    });

nfr("gl_delta_time", "", "", "F",
    "seconds since the last frame, updated only once per frame",
    [](StackPtr &, VM &) {
        return Value(SDLDeltaTime());
    });

nfr("gl_time", "", "", "F",
    "seconds since the start of the OpenGL subsystem, updated only once per frame (use"
    " seconds_elapsed() for continuous timing)",
    [](StackPtr &, VM &) {
        return Value(SDLTime());
    });

nfr("gl_last_time", "name,down", "SI", "F",
    "time key/mousebutton/finger last went down (true) or up (false)",
    [](StackPtr &, VM &, Value &name, Value &on) {
        auto t = GetKeyTime(name.sval()->strv(), on.intval());
        return Value(t);
    });

nfr("gl_clear", "col", "F}:4", "",
    "clears the framebuffer (and depth buffer) to the given color",
    [](StackPtr &sp, VM &vm) {
        TestGL(vm);
        ClearFrameBuffer(PopVec<float3>(sp));
    });

nfr("gl_color", "col", "F}:4", "F}:4",
    "sets the current color, returns previous one",
    [](StackPtr &sp, VM &) {
        auto oldcolor = curcolor;
        curcolor = PopVec<float4>(sp);
        PushVec(sp, oldcolor);
    });

nfr("gl_polygon", "vertlist", "F}]", "",
    "renders a polygon using the list of points given."
    " warning: gl_polygon creates a new mesh every time, gl_new_poly/gl_render_mesh is faster.",
    [](StackPtr &, VM &vm, Value &vl) {
        auto m = CreatePolygon(vm, vl);
        m->Render(currentshader);
        delete m;
        return NilVal();
    });

nfr("gl_circle", "radius,segments", "FI", "",
    "renders a circle",
    [](StackPtr &, VM &vm, Value &radius, Value &segments) {
        TestGL(vm);
        geomcache->RenderCircle(currentshader, polymode, max(segments.intval(), 3), radius.fltval());
        return NilVal();
    });

nfr("gl_open_circle", "radius,segments,thickness", "FIF", "",
    "renders a circle that is open on the inside. thickness is the fraction of the radius that"
    " is filled, try e.g. 0.2",
    [](StackPtr &, VM &vm, Value &radius, Value &segments, Value &thickness) {
        TestGL(vm);

        geomcache->RenderOpenCircle(currentshader, max(segments.intval(), 3), radius.fltval(),
                                    thickness.fltval());

        return NilVal();
    });

nfr("gl_unit_cube", "insideout", "I?", "",
    "renders a unit cube (0,0,0) - (1,1,1). optionally pass true to have it rendered inside"
    " out",
    [](StackPtr &, VM &, Value &inside) {
        geomcache->RenderUnitCube(currentshader, inside.True());
        return NilVal();
    });

nfr("gl_rotate_x", "vec", "F}:2", "",
    "rotates the yz plane around the x axis, using a 2D vector normalized vector as angle",
    [](StackPtr &sp, VM &) {
        auto a = PopVec<float2>(sp);
        otransforms.append_object2view(rotationX(a));
    });

nfr("gl_rotate_y", "angle", "F}:2", "",
    "rotates the xz plane around the y axis, using a 2D vector normalized vector as angle",
    [](StackPtr &sp, VM &) {
        auto a = PopVec<float2>(sp);
        otransforms.append_object2view(rotationY(a));
    });

nfr("gl_rotate_z", "angle", "F}:2", "",
    "rotates the xy plane around the z axis (used in 2D), using a 2D vector normalized vector"
    " as angle",
    [](StackPtr &sp, VM &) {
        auto a = PopVec<float2>(sp);
        otransforms.append_object2view(rotationZ(a));
    });

nfr("gl_translate", "vec", "F}", "",
    "translates the current coordinate system along a vector",
    [](StackPtr &sp, VM &) {
        auto v = PopVec<float3>(sp);
        otransforms.append_object2view(translation(v));
    });

nfr("gl_scale", "factor", "F", "",
    "scales the current coordinate system using a numerical factor",
    [](StackPtr &sp, VM &) {
        auto f = Pop(sp);
        auto v = f.fltval() * float3_1;
        otransforms.append_object2view(float4x4(float4(v, 1)));
    });

nfr("gl_scale", "factor", "F}", "",
    "scales the current coordinate system using a vector",
    [](StackPtr &sp, VM &) {
        auto v = PopVec<float3>(sp, 1);
        otransforms.append_object2view(float4x4(float4(v, 1)));
    });

nfr("gl_origin", "", "", "F}:2",
    "returns a vector representing the current transform origin in pixels."
    " only makes sense in 2D mode (no gl_perspective called).",
    [](StackPtr &sp, VM &) {
        auto pos = double2(otransforms.object2view()[3].x, otransforms.object2view()[3].y);
        PushVec(sp, pos);
    });

nfr("gl_scaling", "", "", "F}:2",
    "returns a vector representing the current transform scale in pixels."
    " only makes sense in 2D mode (no gl_perspective called).",
    [](StackPtr &sp, VM &) {
        auto sc = double2(otransforms.object2view()[0].x, otransforms.object2view()[1].y);
        PushVec(sp, sc);
    });

nfr("gl_model_view_projection", "", "", "F]",
    "returns a vector representing the current model view projection matrix"
    " (16 elements)",
    [](StackPtr &, VM &vm) {
        auto v = vm.NewVec(16, 16, TYPE_ELEM_VECTOR_OF_FLOAT);
        auto mvp = view2clip * otransforms.object2view();
        for (int i = 0; i < 16; i++) v->At(i) = mvp.data()[i];
        return Value(v);
    });

nfr("gl_push_model_view", "", "", "",
    "save the current state of the model view matrix (gl_translate, gl_rotate etc)",
    [](StackPtr &, VM &) {
        otransforms.push();
    });

nfr("gl_pop_model_view", "", "", "B",
    "restore a previous state of the model view matrix. returns false if none",
    [](StackPtr &, VM &) {
        return Value(otransforms.pop());
    });

nfr("gl_point_scale", "factor", "F", "",
    "sets the current scaling factor for point sprites."
    " this can be what the current gl_scale is, or different, depending on the desired visuals."
    " the ideal size may also be FOV dependent.",
    [](StackPtr &, VM &, Value &f) {
        custompointscale = f.fltval();
        return NilVal();
    });

nfr("gl_line_mode", "on", "I", "I",
    "set line mode (true == on), returns previous mode",
    [](StackPtr &sp, VM &) {
        auto oldmode = polymode;
        auto on = Pop(sp);
        polymode = on.ival() ? PRIM_LOOP : PRIM_FAN;
        Push(sp, oldmode == PRIM_LOOP);
    });

nfr("gl_hit", "vec,i", "F}I", "B",
    "whether the mouse/finger is inside of the rectangle specified in terms of the current"
    " transform (for touch screens only if the corresponding gl_isdown is true). Only true if"
    " the last rectangle for which gl_hit was true last frame is of the same size as this one"
    " (allows you to safely test in most cases of overlapping rendering)",
    [](StackPtr &sp, VM &) {
        auto i = Pop(sp).intval();
        auto size = PopVec<float3>(sp);
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
        Push(sp,  size == lastframehitsize && hit);
    });

nfr("gl_rect", "size,centered", "F}:2I?", "",
    "renders a rectangle (0,0)..(1,1) (or (-1,-1)..(1,1) when centered), scaled by the given"
    " size.",
    [](StackPtr &sp, VM &vm) {
        auto centered = Pop(sp).True();
        auto vec = PopVec<float2>(sp);
        TestGL(vm);
        geomcache->RenderQuad(currentshader, polymode, centered,
                              float4x4(float4(vec, 1)));
    });

nfr("gl_rect_tc_col", "size,tc,tcsize,cols", "F}:2F}:2F}:2F}:4]", "",
    "Like gl_rect renders a sized quad, but allows you to specify texture coordinates and"
    " optionally colors (empty list for all white). Slow.",
    [](StackPtr &sp, VM &vm) {
        TestGL(vm);
        auto cols = Pop(sp).vval();
        auto td = PopVec<float2>(sp);
        auto t = PopVec<float2>(sp);
        auto sz = PopVec<float2>(sp);
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
        RenderArraySlow("gl_rect_tc_col", PRIM_FAN, gsl::make_span(vb_square, 4), "PTC");
    });

nfr("gl_unit_square", "centered", "I?", "",
    "renders a square (0,0)..(1,1) (or (-1,-1)..(1,1) when centered)",
    [](StackPtr &, VM &vm, Value &centered) {
        TestGL(vm);
        geomcache->RenderUnitSquare(currentshader, polymode, centered.True());
        return NilVal();
    });

nfr("gl_line", "start,end,thickness", "F}F}1F", "",
    "renders a line with the given thickness",
    [](StackPtr &sp, VM &vm) {
        TestGL(vm);
        auto thickness = Pop(sp).fltval();
        auto v2 = PopVec<float3>(sp);
        auto v1 = PopVec<float3>(sp);
        if (Is2DMode()) geomcache->RenderLine2D(currentshader, polymode, v1, v2, thickness);
        else geomcache->RenderLine3D(currentshader, v1, v2, float3_0, thickness);
    });

nfr("gl_perspective", "fovy,znear,zfar,frame_buffer_size,frame_buffer_offset", "FFFI}:2?I}:2?", "",
    "changes from 2D mode (default) to 3D right handed perspective mode with vertical fov (try"
    " 60), far plane (furthest you want to be able to render, try 1000) and near plane (try"
    " 1). Optionally specify a framebuffer size to override the current gl_framebuffer_size",
    [](StackPtr &sp, VM &) {
        int2 fbo = PopVec<int2>(sp);
        int2 fbs = PopVec<int2>(sp);
        if (fbs.x + fbs.y == 0)
            fbs = GetFrameBufferSize(GetScreenSize()) - fbo;
        auto zfar = Pop(sp).fltval();
        auto znear = Pop(sp).fltval();
        auto fovy = Pop(sp).fltval();
        Set3DMode(fovy * RAD, fbo, fbs, znear, zfar);
    });

nfr("gl_ortho", "rh,depth", "I?I?", "",
    "changes back to 2D mode rendering with a coordinate system from (0,0) top-left to the"
    " screen size in pixels bottom right. this is the default at the start of a frame, use this"
    " call to get back to that after gl_perspective."
    " Pass true to rh have (0,0) bottom-left instead."
    " Pass true to depth to have depth testing/writing on.",
    [](StackPtr &, VM &, Value &rh, Value &depth) {
        Set2DMode(GetFrameBufferSize(GetScreenSize()), rh.False(), depth.True());
        return NilVal();
    });

nfr("gl_ortho3d", "center,extends", "F}:3F}:3", "",
    "sets a custom ortho projection as 3D projection.",
    [](StackPtr &sp, VM &) {
        auto extends = PopVec<float3>(sp);
        auto center = PopVec<float3>(sp);
        Set3DOrtho(GetFrameBufferSize(GetScreenSize()), center, extends);
    });

nfr("gl_new_poly", "positions", "F}]", "R:mesh",
    "creates a mesh out of a loop of points, much like gl_polygon."
    " gl_line_mode determines how this gets drawn (fan or loop)."
    " automatically generates texcoords and normals."
    " returns mesh id",
    [](StackPtr &, VM &vm, Value &positions) {
        auto m = CreatePolygon(vm, positions);
        return Value(vm.NewResource(&mesh_type, m));
    });

nfr("gl_new_mesh", "format,positions,colors,normals,texcoords1,texcoords2,indices", "SF}:3]F}:4]F}:3]F}:2]F}:2]I]?", "R:mesh",
    "creates a new vertex buffer and returns an integer id (1..) for it."
    " format must be made up of characters P (position), C (color), T (texcoord), N (normal)."
    " indices may be []. positions is obligatory."
    " you may specify [] for any of the other attributes if not required by format,"
    " or to get defaults for colors (white) / texcoords (position x & y) /"
    " normals (generated from adjacent triangles).",
    [](StackPtr &, VM &vm, Value &format, Value &positions, Value &colors,
                           Value &normals, Value &texcoords1, Value &texcoords2, Value &indices) {
        TestGL(vm);
        auto nattr = format.sval()->len;
        if (nattr < 1 || nattr > 10)
            vm.BuiltinError("newmesh: illegal format/attributes size");
        auto fmt = format.sval()->strv();
        if (nattr > (int)min(fmt.find_first_not_of("PCTN"), fmt.size()) || fmt[0] != 'P')
            vm.BuiltinError("newmesh: illegal format characters (only PCTN allowed), P must be"
                               " first");
        iint nverts = positions.vval()->len;
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
        auto verts = new uint8_t[nverts * vsize];
        for (iint i = 0; i < nverts; i++) {
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
            normalize_mesh(gsl::make_span(idxs), verts, nverts, vsize, normal_offset);
        }
        auto m = new Mesh(new Geometry("gl_new_mesh_verts", gsl::make_span(verts, nverts * vsize), fmt,
                                       gsl::span<uint8_t>(), vsize),
                          indices.True() ? PRIM_TRIS : PRIM_POINT);
        if (idxs.size()) m->surfs.push_back(new Surface("gl_new_mesh_idxs", gsl::make_span(idxs)));
        delete[] verts;
        return Value(vm.NewResource(&mesh_type, m));
    });

nfr("gl_new_mesh_iqm", "filename", "S", "R:mesh?",
    "load a .iqm file into a mesh, returns mesh or nil on failure to load.",
    [](StackPtr &, VM &vm, Value &fn) {
        TestGL(vm);
        auto m = LoadIQM(fn.sval()->strv());
        return m ? Value(vm.NewResource(&mesh_type, m)) : NilVal();
    });

nfr("gl_mesh_parts", "m", "R:mesh", "S]",
    "returns an array of names of all parts of mesh m (names may be empty)",
    [](StackPtr &, VM &vm, Value &i) {
        auto &m = GetMesh(i);
        auto v = (LVector *)vm.NewVec(0, (int)m.surfs.size(), TYPE_ELEM_VECTOR_OF_STRING);
        for (auto s : m.surfs) v->Push(vm, Value(vm.NewString(s->name)));
        return Value(v);
    });

nfr("gl_mesh_size", "m", "R:mesh", "I",
    "returns the number of verts in this mesh",
    [](StackPtr &, VM &, Value &i) {
        auto &m = GetMesh(i);
        return Value((int)m.geom->nverts);
    });

nfr("gl_animate_mesh", "m,frame", "R:meshF", "",
    "set the frame for animated mesh m",
    [](StackPtr &, VM &, Value &i, Value &f) {
        GetMesh(i).curanim = f.fltval();
        return NilVal();
    });

nfr("gl_render_mesh", "m", "R:mesh", "",
    "renders the specified mesh",
    [](StackPtr &, VM &vm, Value &i) {
        TestGL(vm);
        GetMesh(i).Render(currentshader);
        return NilVal();
    });

nfr("gl_save_mesh", "m,name", "R:meshS", "B",
    "saves the specified mesh to a file in the PLY format. useful if the mesh was generated"
    " procedurally. returns false if the file could not be written",
    [](StackPtr &, VM &vm, Value &i, Value &name) {
        TestGL(vm);
        bool ok = GetMesh(i).SaveAsPLY(name.sval()->strv());
        return Value(ok);
    });

nfr("gl_mesh_pointsize", "m,pointsize", "R:meshF", "",
    "sets the pointsize for this mesh. "
    "the mesh must have been created with indices = nil for point rendering to be used. "
    "you also want to use a shader that works with points, such as color_attr_particle.",
    [](StackPtr &, VM &, Value &i, Value &ps) {
        auto &m = GetMesh(i);
        m.pointsize = ps.fltval();
        return NilVal();
    });

nfr("gl_set_shader", "shader", "S", "",
    "changes the current shader. shaders must reside in the shaders folder, builtin ones are:"
    " color / textured / phong",
    [](StackPtr &, VM &vm, Value &shader) {
        TestGL(vm);
        auto sh = LookupShader(shader.sval()->strv());
        if (!sh) vm.BuiltinError("no such shader: " + shader.sval()->strv());
        currentshader = sh;
        return NilVal();
    });

nfr("gl_set_shader", "shader", "R:shader", "",
    "changes the current shader from a value received from gl_get_shader",
    [](StackPtr &, VM &vm, Value &shader) {
        TestGL(vm);
        currentshader = &GetShader(shader);
        return NilVal();
    });

nfr("gl_get_shader", "shader", "S", "R:shader",
    "gets a shader by name, for use with gl_set_shader",
    [](StackPtr &, VM &vm, Value &shader) {
        TestGL(vm);
        auto sh = LookupShader(shader.sval()->strv());
        if (!sh) vm.BuiltinError("no such shader: " + shader.sval()->strv());
        return Value(vm.NewResource(&shader_type, sh)->NotOwned());
    });

nfr("gl_set_uniform", "name,value", "SF}", "B",
    "set a uniform on the current shader. size of float vector must match size of uniform"
    " in the shader. returns false on error.",
    [](StackPtr &sp, VM &vm) {
        auto len = Top(sp).intval();
        auto v = PopVec<float4>(sp);
        auto name = Pop(sp);
        auto r = SetUniform(vm, name, v.begin(), len);
        Push(sp,  r);
    });

nfr("gl_set_uniform", "name,value", "SF", "B",
    "set a uniform on the current shader. uniform"
    " in the shader must be a single float. returns false on error.",
    [](StackPtr &, VM &vm, Value &name, Value &vec) {
        auto f = vec.fltval();
        return SetUniform(vm, name, &f, 1);
    });

nfr("gl_set_uniform", "name,value", "SI}", "B",
    "set a uniform on the current shader. size of int vector must match size of uniform"
    " in the shader. returns false on error.",
    [](StackPtr &sp, VM &vm) {
        auto len = Top(sp).intval();
        auto v = PopVec<int4>(sp);
        auto name = Pop(sp);
        auto r = SetUniform(vm, name, v.begin(), len);
        Push(sp, r);
    });

nfr("gl_set_uniform", "name,value", "SI", "B",
    "set a uniform on the current shader. uniform"
    " in the shader must be a single int. returns false on error.",
    [](StackPtr &, VM &vm, Value &name, Value &vec) {
        auto i = vec.intval();
        return SetUniform(vm, name, &i, 1);
    });

nfr("gl_set_uniform_array", "name,value", "SF}:4]", "B",
    "set a uniform on the current shader. uniform in the shader must be an array of vec4."
    " returns false on error.",
    [](StackPtr &, VM &vm, Value &name, Value &vec) {
        TestGL(vm);
        vector<float4> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++)
            vals[i] = ValueToFLT<4>(vec.vval()->AtSt(i), vec.vval()->width);
        currentshader->Activate();
        auto ok = currentshader->SetUniform(name.sval()->strv(), vals.data()->data(), 4,
                                            (int)vals.size());
        return Value(ok);
    });

nfr("gl_set_uniform_matrix", "name,value,morerows", "SF]B?", "B",
    "set a uniform on the current shader. pass a vector of 4/6/9/12/16 floats to set a"
    " mat2/mat3x2/mat3/mat4x3/mat4 respectively. pass true for morerows to get"
    " mat2x3/mat3x4. returns false on error.",
    [](StackPtr &, VM &vm, Value &name, Value &vec, Value &morerows) {
        TestGL(vm);
        vector<float> vals(vec.vval()->len);
        for (int i = 0; i < vec.vval()->len; i++) vals[i] = vec.vval()->At(i).fltval();
        currentshader->Activate();
        auto ok = currentshader->SetUniformMatrix(name.sval()->strv(), vals.data(),
                                                  (int)vals.size(), 1, morerows.True());
        return Value(ok);
    });

nfr("gl_uniform_buffer_object", "name,value,ssbo", "SSI", "I",
    "creates a uniform buffer object, and attaches it to the current shader at the given"
    " uniform block name. uniforms in the shader can be any type, as long as it matches the"
    " data layout in the string buffer."
    " ssbo indicates if you want a shader storage block instead."
    " returns buffer id or 0 on error.",
    [](StackPtr &, VM &vm, Value &name, Value &vec, Value &ssbo) {
        TestGL(vm);
        auto id = UniformBufferObject(currentshader, vec.sval()->strv().data(),
                                      vec.sval()->strv().size(), -1,
                                      name.sval()->strv(), ssbo.True(), 0);
        return Value((int)id);
    });

nfr("gl_delete_buffer_object", "id", "I", "",
    "deletes a buffer objects, e.g. one allocated by gl_uniform_buffer_object().",
    [](StackPtr &, VM &vm, Value &id) {
        TestGL(vm);
        // FIXME: should route this thru a IntResourceManagerCompact to be safe?
        // I guess GL doesn't care about illegal id's?
        DeleteBO(id.intval());
        return NilVal();
    });

nfr("gl_bind_mesh_to_compute", "mesh,name", "R:mesh?S", "",
    "Bind the vertex data of a mesh to a SSBO binding of a compute shader. Pass a nil mesh to"
    " unbind.",
    [](StackPtr &, VM &vm, Value &mesh, Value &name) {
        TestGL(vm);
        if (mesh.True()) GetMesh(mesh).geom->BindAsSSBO(currentshader, name.sval()->strv());
        else UniformBufferObject(currentshader, nullptr, 0, -1, name.sval()->strv(), true, 0);
        return NilVal();
    });

nfr("gl_dispatch_compute", "groups", "I}:3", "",
    "dispatches the currently set compute shader in groups of sizes of the specified x/y/z"
    " values.",
    [](StackPtr &sp, VM &vm) {
        auto groups = PopVec<int3>(sp);
        TestGL(vm);
        DispatchCompute(groups);
    });

nfr("gl_dump_shader", "filename,stripnonascii", "SB", "B",
    "Dumps the compiled (binary) version of the current shader to a file. Contents are driver"
    " dependent. On Nvidia hardware it contains the assembly version of the shader as text,"
    " pass true for stripnonascii if you're only interested in that part.",
    [](StackPtr &, VM &vm, Value &filename, Value &stripnonascii) {
        TestGL(vm);
        currentshader->Activate();
        auto ok = currentshader->Dump(filename.sval()->strv(), stripnonascii.True());
        return Value(ok);
    });

nfr("gl_blend", "on", "I", "I",
    "changes the blending mode (use blending constants from color.lobster), returns old mode",
    [](StackPtr &sp, VM &vm) {
        auto mode = Pop(sp);
        TestGL(vm);
        BlendMode old = SetBlendMode((BlendMode)mode.ival());
        Push(sp, old);
    });

nfr("gl_load_texture", "name,textureformat", "SI?", "R:texture?",
    "returns texture if succesfully loaded from file name, otherwise nil."
    " see texture.lobster for texture format. If textureformat includes cubemap,"
    " will load 6 images with \"_ft\" etc inserted before the \".\" in the filename."
    " Uses stb_image internally (see http://nothings.org/), loads JPEG Baseline,"
    " subsets of PNG, TGA, BMP, PSD, GIF, HDR, PIC.",
    [](StackPtr &, VM &vm, Value &name, Value &tf) {
        TestGL(vm);
        auto tex = CreateTextureFromFile(name.sval()->strv(), tf.intval());
        return tex.id ? vm.NewResource(&texture_type, new OwnedTexture(tex)) : NilVal();
    });

nfr("gl_set_primitive_texture", "i,tex,textureformat", "IR:textureI?", "I",
    "sets texture unit i to texture (for use with rect/circle/polygon/line)",
    [](StackPtr &, VM &vm, Value &i, Value &id, Value &tf) {
        TestGL(vm);
        return Value(SetTexture(GetSampler(vm, i), GetTexture(id), tf.intval()));
    });

nfr("gl_set_mesh_texture", "mesh,part,i,texture", "R:meshIIR:texture", "",
    "sets texture unit i to texture for a mesh and part (0 if not a multi-part mesh)",
    [](StackPtr &, VM &vm, Value &mid, Value &part, Value &i, Value &id) {
        auto &m = GetMesh(mid);
        if (part.ival() < 0 || part.ival() >= (int)m.surfs.size())
            vm.BuiltinError("setmeshtexture: illegal part index");
        m.surfs[part.ival()]->Get(GetSampler(vm, i)) = GetTexture(id);
        return NilVal();
    });

nfr("gl_set_image_texture", "i,tex,textureformat", "IR:textureI", "",
    "sets image unit i to texture (for use with compute). texture format must be the same"
    " as what you specified in gl_load_texture / gl_create_texture,"
    " with optionally writeonly/readwrite flags.",
    [](StackPtr &, VM &vm, Value &i, Value &id, Value &tf) {
        TestGL(vm);
        SetImageTexture(GetSampler(vm, i), GetTexture(id), tf.intval());
        return NilVal();
    });

nfr("gl_create_texture", "matrix,textureformat", "F}:4]]I?", "R:texture",
    "creates a texture from a 2d array of color vectors."
    " see texture.lobster for texture format",
    [](StackPtr &, VM &vm, Value &matv, Value &tf) {
        TestGL(vm);
        auto mat = matv.vval();
        auto ys = mat->len;
        auto xs = mat->At(0).vval()->len;
        auto sz = tf.ival() & TF_FLOAT ? sizeof(float4) : sizeof(byte4);
        auto buf = new uint8_t[xs * ys * sz];
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
        auto tex = CreateTexture("gl_create_texture", buf, int3((int)xs, (int)ys, 0), tf.intval());
        delete[] buf;
        return Value(vm.NewResource(&texture_type, new OwnedTexture(tex)));
    });

nfr("gl_create_blank_texture", "size,color,textureformat", "I}:2F}:4I?", "R:texture",
    "creates a blank texture (for use as frame buffer or with compute shaders)."
    " see texture.lobster for texture format",
    [](StackPtr &sp, VM &vm) {
        TestGL(vm);
        auto tf = Pop(sp).intval();
        auto col = PopVec<float4>(sp);
        auto size = PopVec<int2>(sp);
        auto tex = CreateBlankTexture("gl_create_blank_texture", size, col, tf);
        Push(sp, vm.NewResource(&texture_type, new OwnedTexture(tex)));
    });

nfr("gl_texture_size", "tex", "R:texture", "I}:2",
    "returns the size of a texture",
    [](StackPtr &sp, VM &vm) {
        TestGL(vm);
        auto v = Pop(sp);
        PushVec(sp, GetTexture(v).size.xy());
    });

nfr("gl_read_texture", "tex", "R:texture", "S?",
    "read back RGBA texture data into a string or nil on failure",
    [](StackPtr &, VM &vm, Value &t) {
        TestGL(vm);
        auto tex = GetTexture(t);
        auto numpixels = tex.size.x * tex.size.y;
        if (!numpixels) return NilVal();
        auto buf = ReadTexture(tex);
        if (!buf) return NilVal();
        auto s = vm.NewString(string_view((char *)buf, numpixels * 4));
        delete[] buf;
        return Value(s);
    });

nfr("gl_switch_to_framebuffer", "tex,hasdepth,textureformat,resolvetex,depthtex",
    "R:texture?I?I?R:texture?R:texture?", "B",
    "switches to a new framebuffer, that renders into the given texture."
    " also allocates a depth buffer for it if depth is true."
    " pass the textureformat that was used for this texture."
    " pass a resolve texture if the base texture is multisample."
    " pass your own depth texture if desired."
    " pass a nil texture to switch back to the original framebuffer."
    " performance note: do not recreate texture passed in unless necessary.",
    [](StackPtr &, VM &vm, Value &t, Value &depth, Value &tf, Value &retex,
                                       Value &depthtex) {
        TestGL(vm);
        auto tex = GetTexture(t);
        return Value(SwitchToFrameBuffer(tex, GetScreenSize(),
                                         depth.True(), tf.intval(), GetTexture(retex),
                                         GetTexture(depthtex)));
    });

nfr("gl_framebuffer_size", "", "", "I}:2",
    "a vector representing the size (in pixels) of the framebuffer, according to the last call"
    " to gl_switch_to_framebuffer, or same as gl_window_size otherwise",
    [](StackPtr &sp, VM &) {
        PushVec(sp, GetFrameBufferSize(GetScreenSize()));
    });

nfr("gl_light", "pos,params", "F}:3F}:2", "",
    "sets up a light at the given position for this frame. make sure to call this after your"
    " camera transforms but before any object transforms (i.e. defined in \"worldspace\")."
    " params contains specular exponent in x (try 32/64/128 for different material looks) and"
    " the specular scale in y (try 1 for full intensity)",
    [](StackPtr &sp, VM &) {
        Light l;
        l.params = PopVec<float2>(sp);
        l.pos = otransforms.object2view() * float4(PopVec<float3>(sp), 1);
        lights.push_back(l);
    });

nfr("gl_render_tiles", "positions,tilecoords,mapsize", "F}:2]I}:2]I}:2", "",
    "Renders a list of tiles from a tilemap. Each tile rendered is 1x1 in size."
    " Positions may be anywhere. tilecoords are indices into the map (0..mapsize-1), mapsize is"
    " the amount of tiles in the texture. Tiles may overlap, they are drawn in order."
    " Before calling this, make sure to have the texture set and a textured shader",
    [](StackPtr &sp, VM &vm) {
        TestGL(vm);
        auto msize = float2(PopVec<int2>(sp));
        auto tile = Pop(sp).vval();
        auto pos = Pop(sp).vval();
        auto len = pos->len;
        if (len != tile->len)
            vm.BuiltinError("rendertiles: vectors of different size");
        vector<SpriteVert> vbuf(len * 6);
        for (iint i = 0; i < len; i++) {
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
        RenderArraySlow("gl_render_tiles", PRIM_TRIS, gsl::make_span(vbuf), "pT");
    });

nfr("gl_debug_grid", "num,dist,thickness", "I}:3F}:3F", "",
    "renders a grid in space for debugging purposes. num is the number of lines in all 3"
    " directions, and dist their spacing. thickness of the lines in the same units",
    [](StackPtr &sp, VM &vm) {
        TestGL(vm);
        auto thickness = Pop(sp).fltval();
        auto dist = PopVec<float3>(sp);
        auto num = PopVec<iint3>(sp);
        float3 cp = otransforms.camerapos();
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
    [](StackPtr &, VM &, Value &fn) {
        bool ok = ScreenShot(fn.sval()->strv());
        return Value(ok);
    });

nfr("gl_dropped_file", "", "", "S",
    "if a file was dropped on the window this frame, the filename, otherwise empty",
    [](StackPtr &, VM &vm) {
        return Value(vm.NewString(GetDroppedFile()));
    });

}  // AddGraphics

