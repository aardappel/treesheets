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

    OutlineFont(void *fth, string &fb) : fthandle(fth) { fbuf.swap(fb); }
    ~OutlineFont();

    bool EnsureCharsPresent(const char *utf8str);
};

struct BitmapFont {
    Texture tex;
    vector<int3> positions;
    int height;
    int usedcount;
    int size;
    OutlineFont *font;

    ~BitmapFont();
    BitmapFont(OutlineFont *_font, int _size);

    void RenderText(const char *text);
    const int2 TextSize(const char *text);

    bool CacheChars(const char *text);
};

extern OutlineFont *LoadFont(const char *name);

extern void FTClosedown();
