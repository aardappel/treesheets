typedef unsigned char uchar;
typedef unsigned int uint;

#ifdef _DEBUG
#define ASSERT(c) assert(c)
#else
#define ASSERT(c) \
    if (c) {}
#endif

#define loop(i, m) for (int i = 0; i < int(m); i++)
#define loopv(i, v) for (int i = 0; i < (v).size(); i++)
#define loopvrev(i, v) for (int i = (v).size() - 1; i >= 0; i--)

#define sign(x) ((x) < 0 ? -1 : 1)

#define DELETEP(p)       \
    {                    \
        if (p) {         \
            delete p;    \
            p = nullptr; \
        }                \
    }

// Any of these ends a line in text we take in, on all platforms, because:
// Win32: usually contains both.
// Macos: older versions use \r and newer \n in clipboard?
// Linux: should only ever be \n but if we encounter \r we want to strip it.
#define LINE_DELIMITERS "\r\n"

// What we write out ourselves.
#ifdef __WXGTK__
    #define LINE_SEPARATOR "\r\n"
#else
    #define LINE_SEPARATOR "\n"
#endif

inline uchar *loadfile(const char *fn, size_t *lenret = nullptr) {
    FILE *f = fopen(fn, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uchar *buf = (uchar *)malloc(len + 1);
    if (!buf) {
        fclose(f);
        return nullptr;
    }
    buf[len] = 0;
    size_t rlen = fread(buf, 1, len, f);
    fclose(f);
    if (len != rlen || len <= 0) {
        free(buf);
        return nullptr;
    }
    if (lenret) *lenret = len;
    return buf;
}

inline uint64_t FNV1A64(const uint8_t *data, size_t size) {
    uint64_t hash = 0xCBF29CE484222325;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 0x100000001B3;
    }
    return hash;
}
