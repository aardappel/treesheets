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

// To and from UTF-8 unicode conversion functions.

inline int ToUTF8(int u, char *out /* must have space for 7 chars */) {
    assert(u >= 0);                   // top bit can't be set
    for (int i = 0; i < 6; i++) {     // 6 possible encodings: http://en.wikipedia.org/wiki/UTF-8
        int maxbits = 6 + i * 5 + !i; // max bits this encoding can represent
        if (u < (1 << maxbits)) {     // does it fit?
            int remainbits = i * 6;   // remaining bits not encoded in the first byte, store 6 each
            *out++ = char((0xFE << (maxbits - remainbits)) + (u >> remainbits));      // first byte
            for (int j = i - 1; j >= 0; j--) *out++ = ((u >> (j * 6)) & 0x3F) | 0x80; // other bytes
            *out++ = 0;     // terminate it
            return i + 1;   // strlen
        }
    }
    assert(0);  // impossible to arrive here
    return -1;
}

inline int FromUTF8(const char *&in) {  // returns -1 upon corrupt UTF-8 encoding
    int len = 0;
    for (int mask = 0x80; mask >= 0x04; mask >>= 1) { // count leading 1 bits
        if (*in & mask) len++;
        else break;
    }
    if ((*in << len) & 0x80) return -1;      // bit after leading 1's must be 0
    if (!len) return *in++;
    int r = *in++ & ((1 << (7 - len)) - 1);  // grab initial bits of the code
    for (int i = 0; i < len - 1; i++) {
        if ((*in & 0xC0) != 0x80) return -1; // upper bits must 1 0
        r <<= 6;
        r |= *in++ & 0x3F;                   // grab 6 more bits of the code
    }
    return r;
}

// convenience functions

inline string ToUTF8(const wchar_t *in) {
    string r;
    char buf[7];
    while (*in) {
        ToUTF8(*in++, buf);
        r += buf;
    }
    return r;
}

// Appends into dest, returns false if encoding error encountered.
inline bool FromUTF8(const char *in, wstring &dest) {
    for (;;) {
        int u = FromUTF8(in);
        if (!u) return true;
        if (u < 0) return false;
        dest += (wchar_t)u; // should we check it fits inside a wchar_t ?
    }
}

// Returns number of code points, returns -1 if encoding error encountered.
inline int StrLenUTF8(const char *in) {
    int num = 0;
    for (;;) {
        int u = FromUTF8(in);
        if (!u) return num;
        if (u < 0) return -1;
        num++;
    }
}

inline void utf8test() {
    char buf[7];
    ToUTF8(0x24, buf);    assert(!strcmp(buf, "\x24"));
    ToUTF8(0xA2, buf);    assert(!strcmp(buf, "\xC2\xA2"));
    ToUTF8(0x20AC, buf);  assert(!strcmp(buf, "\xE2\x82\xAC"));
    ToUTF8(0x24B62, buf); assert(!strcmp(buf, "\xF0\xA4\xAD\xA2"));

    assert(ToUTF8(L"\u30E6\u30FC\u30B6\u30FC\u5225\u30B5\u30A4\u30C8") ==
           string("\xe3\x83\xa6\xe3\x83\xbc\xe3\x82\xb6\xe3\x83\xbc\xe5\x88\xa5\xe3\x82\xb5\xe3\x82"
                  "\xa4\xe3\x83\x88"));

    const char *p;
    p = "\x24";             assert(FromUTF8(p) == 0x24    && !*p);
    p = "\xC2\xA2";         assert(FromUTF8(p) == 0xA2    && !*p);
    p = "\xE2\x82\xAC";     assert(FromUTF8(p) == 0x20AC  && !*p);
    p = "\xF0\xA4\xAD\xA2"; assert(FromUTF8(p) == 0x24B62 && !*p);
    (void)p;

    wstring dest;
    assert(FromUTF8("\xe3\x83\xa6\xe3\x83\xbc\xe3\x82\xb6\xe3\x83\xbc\xe5\x88"
                    "\xa5\xe3\x82\xb5\xe3\x82\xa4\xe3\x83\x88\x00", dest));
    assert(dest == wstring(L"\u30E6\u30FC\u30B6\u30FC\u5225\u30B5\u30A4\u30C8"));
}
