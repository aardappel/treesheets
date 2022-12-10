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

#define DELETEP(p)    \
    {                 \
        if (p) {      \
            delete p; \
            p = nullptr; \
        }             \
    }
#define DELETEA(a)      \
    {                   \
        if (a) {        \
            delete[] a; \
            a = nullptr;   \
        }               \
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

template <class T>
inline void swap_(T &a, T &b) {
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
    uchar *buf = (uchar *)malloc(len+1);
    if (!buf) { fclose(f); return nullptr; }
    buf[len] = 0;
    size_t rlen = fread(buf, 1, len, f);
    fclose(f);
    if (len!=rlen || len<=0) { free(buf); return nullptr; }
    if (lenret) *lenret = len;
    return buf;
}

// from boost, stops a class from being accidental victim to default copy + destruct twice problem
// class that inherits from NonCopyable will work correctly with Vector, but give compile time error
// with std::vector

class NonCopyable {
    NonCopyable(const NonCopyable &);
    const NonCopyable &operator=(const NonCopyable &);

    protected:
    NonCopyable() {}
    virtual ~NonCopyable() {}
};

// helper function for containers below to delete members if they are of pointer type only

template <class X>
void DelPtr(X &) {}
template <class X>
void DelPtr(X *&m) {
    DELETEP(m);
}

// replacement for STL vector

// for any T that has a non-trivial destructor, STL requires a correct copy constructor / assignment
// op
// or otherwise it will free things twice on a reallocate/by value push_back.
// The vector below will behave correctly irrespective of whether a copy constructor is available
// or not and avoids the massive unnecessary (de/re)allocations the STL forces you to.
// The class itself never calls T's copy constructor, however the user of the class is still
// responsable for making sure of correct copying of elements obtained from the vector, or setting
// NonCopyable

// also automatically deletes pointer members and other neat things

template <class T>
class Vector : public NonCopyable {
    T *buf;
    uint alen, ulen;

    void reallocate(uint n) {
        ASSERT(n > ulen);
        T *obuf = buf;
        // use malloc instead of new to avoid constructor
        buf = (T *)malloc(sizeof(T) * (alen = n));
        ASSERT(buf);
        if (ulen) memcpy(buf, obuf, ulen * sizeof(T));  // memcpy avoids copy constructor
        if (obuf) free(obuf);
    }

    T &push_nocons() {
        if (ulen == alen) reallocate(alen * 2);
        return buf[ulen++];
    }

    void destruct(uint i) {
        buf[i].~T();
        DelPtr(buf[i]);
    }

    public:
    T &operator[](uint i) {
        ASSERT(i < ulen);
        return buf[i];
    }
    T &operator[](uint i) const {
        ASSERT(i < ulen);
        return buf[i];
    }

    Vector() { new (this) Vector(8); }
    Vector(uint n) : ulen(0), buf(nullptr) { reallocate(n); }
    Vector(uint n, int c) {
        new (this) Vector(n);
        loop(i, c) push();
    }
    ~Vector() {
        setsize(0);
        free(buf);
    }

    T &push() {
        return *new (&push_nocons()) T;
    }  // only way to create an element, to avoid copy constructor

    T *getbuf() { return buf; }
    T &last() { return (*this)[ulen - 1]; }
    T &pop() { return buf[--ulen]; }
    void drop() {
        ASSERT(ulen);
        destruct(--ulen);
    }
    bool empty() { return ulen == 0; }
    int size() { return ulen; }

    void setsize(uint i) {
        while (ulen > i) drop();
    }  // explicitly destruct elements
    void setsize_nd(uint i) {
        while (ulen > i) pop();
    }

    void sort(void *cf) {
        qsort(buf, ulen, sizeof(T), (int(__cdecl *)(const void *, const void *))cf);
    }

    void add_unique(T x) {
        loop(i, ulen) if (buf[i] == x) return;
        push() = x;
    }

    void remove(uint i) {
        ASSERT(i < ulen);
        destruct(i);
        ulen--;
        memmove(buf + i, buf + i + 1, sizeof(T) * (ulen - i));
    }

    void remove(uint i, uint n) {
        ASSERT(i + n <= ulen);
        for (uint d = i; d < i + n; d++) destruct(d);
        ulen -= n;
        memmove(buf + i, buf + i + n, sizeof(T) * (ulen - i));
    }

    void removeobj(T o) {
        loop(i, ulen) if (buf[i] == o) {
            remove(i);
            return;
        }
    }

    void append(Vector<T> &o) { loopv(i, o) push() = o[i]; }
};

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
