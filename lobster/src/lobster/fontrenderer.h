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
    map<uint, uint> glyph_to_char;

    OutlineFont(void *fth, string &fb) : fthandle(fth) { fbuf.swap(fb); }
    ~OutlineFont();

    bool EnsureCharsPresent(string_view utf8str);
    string GetName(uint i);
    uint GetCharCode(string_view name);
};

struct BitmapFont {
    Texture tex;
    vector<int3> positions;
    int height = 0;
    int usedcount = 1;
    int size;
    float outlinesize;
    OutlineFont *font;

    ~BitmapFont();
    BitmapFont(OutlineFont *_font, int _size, float _osize);

    void RenderText(string_view text);
    const int2 TextSize(string_view text);

    bool CacheChars(string_view text);
};

extern OutlineFont *LoadFont(string_view name);

extern void FTClosedown();
