typedef unsigned char uchar;
typedef unsigned short ushort;
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

#define max(a, b) ((a) < (b) ? (b) : (a))
#define min(a, b) ((a) > (b) ? (b) : (a))
#define sign(x) ((x) < 0 ? -1 : 1)

#define varargs(v, fmt, body) \
    {                         \
        va_list v;            \
        va_start(v, fmt);     \
        body;                 \
        va_end(v);            \
    }

#define DELETEP(p)       \
    {                    \
        if (p) {         \
            delete p;    \
            p = nullptr; \
        }                \
    }
#define DELETEA(a)       \
    {                    \
        if (a) {         \
            delete[] a;  \
            a = nullptr; \
        }                \
    }

#define bound(v, a, s, e)     \
    {                         \
        v += a;               \
        if (v > (e)) v = (e); \
        if (v < (s)) v = (s); \
    }

// Use the same on all platforms, because:
// Win32: usually contains both.
// Macos: older versions use \r and newer \n in clipboard?
// Linux: should only ever be \n but if we encounter \r we want to strip it.
#define LINE_SEPERATOR L"\r\n"

#ifdef WIN32
#define PATH_SEPERATOR L"\\"
#else
#define PATH_SEPERATOR L"/"
#define __cdecl
#define _vsnprintf vsnprintf
#endif

template<class T> inline void swap_(T &a, T &b) {
    T c = a;
    a = b;
    b = c;
};

#ifdef WIN32
#pragma warning(3 : 4189)        // local variable is initialized but not referenced
#pragma warning(disable : 4244)  // conversion from 'int' to 'float', possible loss of data
#pragma warning(disable : 4355)  // 'this' : used in base member initializer list
#pragma warning(disable : 4996)  // 'strncpy' was declared deprecated
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

// for use with vc++ crtdbg

#if defined(_DEBUG) && defined(_WIN32)
inline void *__cdecl operator new(size_t n, const char *fn, int l) {
    return ::operator new(n, 1, fn, l);
}
inline void *__cdecl operator new[](size_t n, const char *fn, int l) {
    return ::operator new[](n, 1, fn, l);
}
inline void __cdecl operator delete(void *p, const char *fn, int l) {
    ::operator delete(p, 1, fn, l);
}
inline void __cdecl operator delete[](void *p, const char *fn, int l) {
    ::operator delete[](p, 1, fn, l);
}
#define new new (__FILE__, __LINE__)
#endif

inline uint32_t FNV1A32(std::vector<uint8_t> &vec) {
    uint32_t hash = 0x811C9DC5;
    for (uint8_t c : vec) {
        hash ^= c;
        hash *= 0x01000193;
    }
    return hash;
}

inline uint64_t FNV1A64(uint8_t *data, size_t size) {
    uint64_t hash = 0xCBF29CE484222325;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 0x100000001B3;
    }
    return hash;
}
