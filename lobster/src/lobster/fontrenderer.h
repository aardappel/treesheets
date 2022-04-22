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

// simple interface for FreeType (that doesn't depend on its headers)

struct BitmapFont;

struct OutlineFont {
    void *fthandle;
    string fbuf;
    unordered_map<int, int> unicodemap;
    vector<int> unicodetable;
    map<int, int> glyph_to_char;

    OutlineFont(void *fth, string &fb) : fthandle(fth) { fbuf.swap(fb); }
    ~OutlineFont();

    bool EnsureCharsPresent(string_view utf8str);
    string GetName(int i);
    int GetCharCode(string_view name);
};

struct BitmapFont {
    Texture tex;
    struct Glyph {
        int x;
        int y;
        int advance;
        int left;
        int top;  // From baseline.
        int width;
        int height;
    };
    vector<Glyph> positions;
    int usedcount = 1;
    int font_height = 0;
    int max_ascent = 0;
    float outlinesize = 0.0f;
    OutlineFont *font = nullptr;

    ~BitmapFont();
    BitmapFont(OutlineFont *_font, int _size, float _osize);

    void RenderText(string_view text);
    const int2 TextSize(string_view text);

    bool CacheChars(string_view text);
};

extern OutlineFont *LoadFont(string_view name);

extern void FTClosedown();
