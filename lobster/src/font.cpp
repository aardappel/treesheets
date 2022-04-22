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
#include "lobster/fontrenderer.h"

using namespace lobster;

map<string, BitmapFont *> fontcache;
BitmapFont *curfont = nullptr;
int curfontsize = -1;
float curoutlinesize = 0;
int maxfontsize = 256;

map<string, OutlineFont *, less<>> loadedfaces;
OutlineFont *curface = nullptr;
string curfacename;

Shader *texturedshader = nullptr;

void CullFonts() {
    for(auto it = fontcache.begin(); it != fontcache.end(); ) {
        if (it->second->usedcount) {
            it->second->usedcount = 0;
            it++;
        } else {
            if (curfont == it->second) curfont = nullptr;
            delete it->second;
            fontcache.erase(it++);
        }
    }
}

void FontCleanup() {
    for (auto e : fontcache) delete e.second;
    fontcache.clear();
    curfont = nullptr;
    for (auto e : loadedfaces) delete e.second;
    loadedfaces.clear();
    curface = nullptr;
    FTClosedown();
}

void AddFont(NativeRegistry &nfr) {

nfr("gl_set_font_name", "filename", "S", "B",
    "sets a freetype/OTF/TTF font as current (and loads it from disk the first time). returns"
    " true if success.",
    [](StackPtr &, VM &vm, Value &fname) {
        extern void TestGL(VM &vm); TestGL(vm);
        auto piname = string(fname.sval()->strv());
        auto faceit = loadedfaces.find(piname);
        if (faceit != loadedfaces.end()) {
            curface = faceit->second;
            curfacename = piname;
            return Value(true);
        }
        texturedshader = LookupShader("textured");
        assert(texturedshader);
        curface = LoadFont(piname);
        if (curface)  {
            curfacename = piname;
            loadedfaces[piname] = curface;
            return Value(true);
        } else {
            return Value(false);
        }
    });

nfr("gl_set_font_size", "size,outlinesize", "IF?", "B",
    "sets the font for rendering into this fontsize (in pixels). caches into a texture first"
    " time this size is used, flushes from cache if this size is not used an entire frame. font"
    " rendering will look best if using 1:1 pixels (careful with gl_scale/gl_translate)."
    " an optional outlinesize will give the font a black outline."
    " make sure to call this every frame."
    " returns true if success",
    [](StackPtr &, VM &vm, Value &fontsize, Value &outlinesize) {
        if (!curface) vm.BuiltinError("gl_set_font_size: no current font set with gl_set_font_name");
        float osize = min(16.0f, max(0.0f, outlinesize.fltval()));
        int size = max(1, fontsize.intval());
        int csize = min(size, maxfontsize);
        if (osize > 0 && csize != size) osize = osize * csize / size;
        string fontname = curfacename;
        fontname += to_string(csize);
        fontname += "_";
        fontname += to_string_float(osize);
        curfontsize = size;
        curoutlinesize = osize;
        auto fontelem = fontcache.find(fontname);
        if (fontelem != fontcache.end()) {
            curfont = fontelem->second;
            return Value(true);
        }
        curfont = new BitmapFont(curface, csize, osize);
        fontcache.insert({ fontname, curfont });
        return Value(true);
    });

nfr("gl_set_max_font_size", "size", "I", "",
    "sets the max font size to render to bitmaps. any sizes specified over that by setfontsize"
    " will still work but cause scaled rendering. default 256",
    [](StackPtr &, VM &, Value &fontsize) {
        maxfontsize = fontsize.intval();
        return NilVal();
    });

nfr("gl_get_font_size", "", "", "I",
    "the current font size",
    [](StackPtr &, VM &) { return Value(curfontsize); });

nfr("gl_get_outline_size", "", "", "F",
    "the current font size",
    [](StackPtr &, VM &) { return Value(curoutlinesize); });

nfr("gl_text", "text", "S", "Sb",
    "renders a text with the current font (at the current coordinate origin)",
    [](StackPtr &, VM &vm, Value &s) {
        auto f = curfont;
        if (!f) return vm.BuiltinError("gl_text: no font / font size set");
        if (!s.sval()->len) return s;
        float4x4 oldobject2view;
        if (curfontsize > maxfontsize) {
            oldobject2view = otransforms.object2view();
            otransforms.set_object2view(otransforms.object2view() * scaling(curfontsize / float(maxfontsize)));
        }
        SetTexture(0, f->tex);
        texturedshader->Set();
        f->RenderText(s.sval()->strv());
        if (curfontsize > maxfontsize) otransforms.set_object2view(oldobject2view);
        return s;
    });

nfr("gl_text_size", "text", "S", "I}:2",
    "the x/y size in pixels the given text would need",
    [](StackPtr &sp, VM &vm) {
        auto f = curfont;
        if (!f) vm.BuiltinError("gl_text_size: no font / font size set");
        auto size = f->TextSize(Pop(sp).sval()->strv());
        if (curfontsize > maxfontsize) {
            size = fceil(float2(size) * float(curfontsize) / float(maxfontsize));
        }
        PushVec(sp, size);
    });

nfr("gl_get_glyph_name", "i", "I", "S",
    "the name of a glyph index, or empty string if the font doesn\'t have names",
    [](StackPtr &, VM &vm, Value &i) {
        return Value(vm.NewString(curface ? curface->GetName(i.intval()) : ""));
    });

nfr("gl_get_char_code", "name", "S", "I",
    "the char code of a glyph by specifying its name, or 0 if it can not be found"
    " (or if the font doesn\'t have names)",
    [](StackPtr &, VM &, Value &n) {
        return Value(curface ? curface->GetCharCode(n.sval()->strv()) : 0);
    });

}  // AddFont
