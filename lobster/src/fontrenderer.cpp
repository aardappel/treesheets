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
#include "lobster/fontrenderer.h"

#define USE_FREETYPE

#ifdef USE_FREETYPE
    #include <ft2build.h>
    #include FT_FREETYPE_H
    #include FT_STROKER_H
#else
    #define STB_TRUETYPE_IMPLEMENTATION
    #define STBTT_STATIC
    #include "stb/stb_truetype.h"
#endif

FT_Library library = nullptr;

BitmapFont::~BitmapFont() {
    DeleteTexture(tex);
}

BitmapFont::BitmapFont(OutlineFont *_font, int _size, float _osize)
    : font_height(_size), outlinesize(_osize), font(_font) {}

bool BitmapFont::CacheChars(string_view text) {
    usedcount++;
    font->EnsureCharsPresent(text);
    if (positions.size() == font->unicodetable.size())
        return true;
    DeleteTexture(tex);
    positions.clear();
    if (FT_Set_Pixel_Sizes((FT_Face)font->fthandle, 0, font_height))
        return false;
    auto face = (FT_Face)font->fthandle;
    const int outline_passes = outlinesize > 0 ? 2 : 1;
    FT_Stroker stroker = nullptr;
    if (outline_passes > 1) {
        FT_Stroker_New(library, &stroker);
        FT_Stroker_Set(stroker, FT_Fixed(outlinesize * 64), FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND, 0);
    }
    const int margin = 1;
    int texw = min(4096, MaxTextureSize());
    vector<byte4> image;
    auto x = margin, y = margin;
    int rowheight = 0;
    max_ascent = 0;
    int max_descent = 0;
    for (int i : font->unicodetable) {
        auto char_index = FT_Get_Char_Index(face, i);
        FT_Load_Glyph(face, char_index, FT_LOAD_DEFAULT);
        auto advance = (int)((face->glyph->metrics.horiAdvance + FT_Fixed(outlinesize * 64)) / 64);
        int maxwidth = 0;
        for (int pass = 0; pass < outline_passes; pass++) {
            FT_Glyph glyph;
            FT_Get_Glyph(face->glyph, &glyph);
            if (!pass && stroker) FT_Glyph_StrokeBorder(&glyph, stroker, false, true);
            FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, true);
            FT_BitmapGlyph bglyph = (FT_BitmapGlyph)glyph;
            auto width = (int)bglyph->bitmap.width;
            auto height = (int)bglyph->bitmap.rows;
            max_ascent = max(bglyph->top, max_ascent);
            max_descent = min(bglyph->top - height, max_descent);
            int offx = 0, offy = 0;
            if (!pass) {
                if (width > texw - x) {
                    x = margin;
                    y = rowheight + margin;
                }
                positions.push_back(Glyph {
                    x, y, advance,
                    bglyph->left, bglyph->top,
                    width, height
                });
                maxwidth = width;
                rowheight = max(rowheight, y + height);
            } else {
                offx = bglyph->left - positions.back().left;
                offy = positions.back().top - bglyph->top;
            }
            for (int row = 0; row < height; ++row) {
                for (int pixel = 0; pixel < width; ++pixel) {
                    auto off = (x + offx + pixel) + (y + offy + row) * texw;
                    if ((int)image.size() <= off) {
                        image.resize(off + 16, byte4_0);
                    }
                    auto alpha = bglyph->bitmap.buffer[pixel + row * bglyph->bitmap.pitch];
                    if (outline_passes > 1) {
                        if (!pass) {
                            image[off] = { 0x00, 0x00, 0x00, alpha };
                        } else {
                            auto cur_alpha = image[off].w;
                            auto max_alpha = max(cur_alpha, alpha);
                            auto col = (uint8_t)mix(0x00, 0xFF, alpha / 255.0f);
                            image[off] = { col, col, col, max_alpha };
                        }
                    } else {
                        // FIXME: wastefull
                        image[off] = { 0xFF, 0xFF, 0xFF, alpha };
                    }
                }
            }
            FT_Done_Glyph(glyph);
        }
        x += maxwidth + margin;
    }
    // FIXME: there appears to be no way to figure out the "baseline" in pixels, such
    // that we can place characters within a font_height bounding box correctly.
    // We have the max_ascent computed here, and also face->ascender etc, but even though
    // we can relate their scale, we can't know where realtive to the "em square"
    // (and thus font_height) the baseline is, since ascender + descender may extend
    // outside of it.
    // So we do something horrible, which actually works well for all fonts tested, and
    // thats to adjust by whatever amount we are bigger (or smaller) than the fontsize.
    max_ascent += (font_height - (max_ascent - max_descent)) / 2;
    if (stroker) FT_Stroker_Done(stroker);
    int texh = 1;
    while (texh <= (int)image.size() / texw)
        texh *= 2;
    image.resize(texh * texw);
    tex = CreateTexture("font_atlas", &image[0].x, int3(texw, texh, 0), TF_CLAMP | TF_NOMIPMAP);
    return true;
}

void BitmapFont::RenderText(string_view text) {
    if (!CacheChars(text))
        return;
    struct PT { float2 p; float2 t; };
    int len = StrLenUTF8(text);
    if (len <= 0)
        return;
    vector<PT> vbuf(len * 4);
    vector<int> ibuf(len * 6);
    auto x = 0.0f;
    auto y = 0.0f;
    int idx = 0;
    for (int j = 0; j < len * 4; j += 4) {
        int c = FromUTF8(text);
        auto &glyph = positions[font->unicodemap[c]];
        float x1 = glyph.x / float(tex.size.x);
        float x2 = (glyph.x + glyph.width) / float(tex.size.x);
        float y1 = glyph.y / float(tex.size.y);
        float y2 = (glyph.y + glyph.height) / float(tex.size.y);
        float ox = x + glyph.left;
        float oy = y + (max_ascent - glyph.top);
        vbuf[j + 0] = { float2(ox, oy), float2(x1, y1) };
        vbuf[j + 1] = { float2(ox, oy + glyph.height), float2(x1, y2) };
        vbuf[j + 2] = { float2(ox + glyph.width, oy + glyph.height), float2(x2, y2) };
        vbuf[j + 3] = { float2(ox + glyph.width, oy), float2(x2, y1) };
        ibuf[idx++] = j + 0;
        ibuf[idx++] = j + 1;
        ibuf[idx++] = j + 2;
        ibuf[idx++] = j + 2;
        ibuf[idx++] = j + 3;
        ibuf[idx++] = j + 0;
        x += glyph.advance;
    }
    SetTexture(0, tex);
    RenderArraySlow("RenderText", PRIM_TRIS, gsl::make_span(vbuf), "pT", gsl::make_span(ibuf));
}

const int2 BitmapFont::TextSize(string_view text) {
    if (!CacheChars(text))
        return int2_0;
    auto x = 0;
    for (;;) {
        int c = FromUTF8(text);
        if (c <= 0) return int2(x, font_height);
        x += positions[font->unicodemap[c]].advance;
    }
}

OutlineFont *LoadFont(string_view name) {
    FT_Error err = 0;
    if (!library) err = FT_Init_FreeType(&library);
    if (!err) {
        string fbuf;
        if (LoadFile(name, &fbuf) >= 0) {
            FT_Face face;
            err = FT_New_Memory_Face(library, (const FT_Byte *)fbuf.c_str(), (FT_Long)fbuf.length(),
                                     0, &face);
            if (!err) return new OutlineFont(face, fbuf);
        }
    }
    return nullptr;
}

OutlineFont::~OutlineFont() {
    FT_Done_Face((FT_Face)fthandle);
}

string OutlineFont::GetName(int i) {
    char buf[256];
    FT_Get_Glyph_Name((FT_Face)fthandle, (uint32_t)i, buf, sizeof(buf));
    return buf;
}

int OutlineFont::GetCharCode(string_view name) {
    auto glyphi = FT_Get_Name_Index((FT_Face)fthandle, (char *)null_terminated(name));
    if (!glyphi) return 0;
    if (glyph_to_char.empty()) {
        uint32_t cgi = 0;
        auto c = FT_Get_First_Char((FT_Face)fthandle, &cgi);
        while (cgi) {
            glyph_to_char[cgi] = (int)c;
            c = FT_Get_Next_Char((FT_Face)fthandle, c, &cgi);
        }
    }
    return glyph_to_char[glyphi];
}

bool OutlineFont::EnsureCharsPresent(string_view utf8str) {
    bool anynew = false;
    for (;;) {
        int uc = FromUTF8(utf8str);
        if (uc <= 0)
            break;
        auto it = unicodemap.find(uc);
        if (it == unicodemap.end()) {
            anynew = true;
            unicodemap[uc] = (int)unicodetable.size();
            unicodetable.push_back(uc);
        }
    }
    return anynew;
}

void FTClosedown() {
    if (library) FT_Done_FreeType(library);
    library = nullptr;
}

