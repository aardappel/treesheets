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

#include "lobster/unicode.h"

FT_Library library = nullptr;

BitmapFont::~BitmapFont() {
    DeleteTexture(tex);
}

BitmapFont::BitmapFont(OutlineFont *_font, int _size, float _osize)
    : size(_size), outlinesize(_osize), font(_font) {}

bool BitmapFont::CacheChars(string_view text) {
    usedcount++;
    font->EnsureCharsPresent(text);
    if (positions.size() == font->unicodetable.size())
        return true;
    DeleteTexture(tex);
    positions.clear();
    if (FT_Set_Pixel_Sizes((FT_Face)font->fthandle, 0, size))
        return false;
    auto face = (FT_Face)font->fthandle;
    const int outline_passes = outlinesize > 0 ? 2 : 1;
    FT_Stroker stroker = nullptr;
    if (outline_passes > 1) {
        FT_Stroker_New(library, &stroker);
        FT_Stroker_Set(stroker, FT_Fixed(outlinesize * 64), FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND, 0);
    }
    const int margin = 3;
    int texh = 0;
    int texw = MaxTextureSize();
    int max_descent = 0;
    int max_ascent = 0;
    int space_on_line = texw - margin, lines = 1;
    for (int i : font->unicodetable) {
        auto char_index = FT_Get_Char_Index(face, i);
        FT_Load_Glyph(face, char_index, FT_LOAD_DEFAULT);
        // FIXME: Can we avoid doing this twice?
        FT_Glyph glyph;
        FT_Get_Glyph(face->glyph, &glyph);
        if (stroker) FT_Glyph_StrokeBorder(&glyph, stroker, false, true);
        FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, true);
        FT_BitmapGlyph bglyph = (FT_BitmapGlyph)glyph;
        auto advance = (face->glyph->metrics.horiAdvance >> 6) + margin;
        if (advance > space_on_line) {
            space_on_line = texw - margin;
            ++lines;
        }
        space_on_line -= advance;
        max_ascent = max(bglyph->top, max_ascent);
        max_descent = max((int)bglyph->bitmap.rows - bglyph->top, max_descent);
        FT_Done_Glyph(glyph);
    }
    height = max_ascent + max_descent;
    auto needed_image_height = (max_ascent + max_descent + margin) * lines + margin;
    texh = 1;
    while (texh < needed_image_height)
        texh *= 2;
    uchar *image = new uchar[texh * texw * 4];
    memset(image, 0, texh * texw * 4);
    for (int pass = 0; pass < outline_passes; pass++) {
        auto x = margin, y = margin + max_ascent;
        for (int i : font->unicodetable) {
            auto char_index = FT_Get_Char_Index(face, i);
            FT_Load_Glyph(face, char_index, FT_LOAD_DEFAULT);
            FT_Glyph glyph;
            FT_Get_Glyph(face->glyph, &glyph);
            if (!pass && stroker) FT_Glyph_StrokeBorder(&glyph, stroker, false, true);
            FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, true);
            FT_BitmapGlyph bglyph = (FT_BitmapGlyph)glyph;
            auto advance = (face->glyph->metrics.horiAdvance >> 6) + margin;
            if (advance > texw - x) {
                x = margin;
                y += (max_ascent + max_descent + margin);
            }
            if (!pass) positions.push_back(int3(x, y - max_ascent, advance - margin));
            for (int row = 0; row < (int)bglyph->bitmap.rows; ++row) {
                for (int pixel = 0; pixel < (int)bglyph->bitmap.width; ++pixel) {
                    auto p = image + ((x + bglyph->left + pixel) +
                        (y - bglyph->top + row) * texw) * 4;
                    auto alpha = bglyph->bitmap.buffer[pixel + row * bglyph->bitmap.pitch];
                    if (outline_passes > 1) {
                        if (!pass) {
                            *p++ = 0x00;
                            *p++ = 0x00;
                            *p++ = 0x00;
                            *p++ = alpha;
                        } else {
                            auto cur_alpha = p[3];
                            auto max_alpha = max(cur_alpha, alpha);
                            auto col = (uchar)mix(0x00, 0xFF, alpha / 255.0f);
                            *p++ = col;
                            *p++ = col;
                            *p++ = col;
                            *p++ = max_alpha;
                        }
                    } else {
                        *p++ = 0xFF;    // FIXME: wastefull
                        *p++ = 0xFF;
                        *p++ = 0xFF;
                        *p++ = alpha;
                    }
                }
            }
            x += advance;
            FT_Done_Glyph(glyph);
        }
    }
    if (stroker) FT_Stroker_Done(stroker);
    tex = CreateTexture(image, int2(texw, texh).data(), TF_CLAMP | TF_NOMIPMAP);
    delete[] image;
    return true;
}

void BitmapFont::RenderText(string_view text) {
    if (!CacheChars(text))
        return;
    struct PT { float3 p; float2 t; };
    int len = StrLenUTF8(text);
    if (len <= 0)
        return;
    vector<PT> vbuf(len * 4);
    vector<int> ibuf(len * 6);
    auto x = 0.0f;
    auto y = 0.0f;
    float fontheighttex = height / float(tex.size.y);
    int idx = 0;
    for (int i = 0; i < len; i++) {
        int c = FromUTF8(text);
        int3 &pos = positions[font->unicodemap[c]];
        float x1 = pos.x / float(tex.size.x);
        float x2 = (pos.x + pos.z) / float(tex.size.x);
        float y1 = pos.y / float(tex.size.y);
        float advance = float(pos.z);
        int j = i * 4;
        auto &v0 = vbuf[j + 0]; v0.t = float2(x1, y1);
                                v0.p = float3(x, y, 0);
        auto &v1 = vbuf[j + 1]; v1.t = float2(x1, y1 + fontheighttex);
                                v1.p = float3(x, y + height, 0);
        auto &v2 = vbuf[j + 2]; v2.t = float2(x2, y1 + fontheighttex);
                                v2.p = float3(x + advance, y + height, 0);
        auto &v3 = vbuf[j + 3]; v3.t = float2(x2, y1);
                                v3.p = float3(x + advance, y, 0);
        ibuf[idx++] = j + 0;
        ibuf[idx++] = j + 1;
        ibuf[idx++] = j + 2;
        ibuf[idx++] = j + 2;
        ibuf[idx++] = j + 3;
        ibuf[idx++] = j + 0;
        x += advance;
    }
    SetTexture(0, tex);
    RenderArraySlow(PRIM_TRIS, make_span(vbuf), "PT", make_span(ibuf));
}

const int2 BitmapFont::TextSize(string_view text) {
    if (!CacheChars(text))
        return int2_0;
    auto x = 0;
    for (;;) {
        int c = FromUTF8(text);
        if (c <= 0) return int2(x, height);
        x += positions[font->unicodemap[c]].z;
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

string OutlineFont::GetName(uint i) {
    char buf[256];
    FT_Get_Glyph_Name((FT_Face)fthandle, i, buf, sizeof(buf));
    return buf;
}

uint OutlineFont::GetCharCode(string_view name) {
    auto glyphi = FT_Get_Name_Index((FT_Face)fthandle, (char *)null_terminated(name));
    if (!glyphi) return 0;
    if (glyph_to_char.empty()) {
        uint cgi = 0;
        auto c = FT_Get_First_Char((FT_Face)fthandle, &cgi);
        while (cgi) {
            glyph_to_char[cgi] = c;
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

