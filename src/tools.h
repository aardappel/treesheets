typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

//#define MAXINT 0x7FFFFFFF

#ifdef _DEBUG
#define ASSERT(c) assert(c)
#else
#define ASSERT(c) if(c) {}
#endif

#define loop(i, m)     for(int i = 0; i<int(m); i++)
#define loopv(i, v)    for(int i = 0; i<(v).size(); i++)
#define loopvrev(i, v) for(int i = (v).size()-1; i>=0; i--)

#define max(a, b) ((a)<(b) ? (b) : (a))
#define min(a, b) ((a)>(b) ? (b) : (a))
#define sign(x) ((x)<0 ? -1 : 1)

#define varargs(v, fmt, body) { va_list v; va_start(v, fmt); body; va_end(v); }

#define DELETEP(p) { if(p) { delete   p; p = NULL; } }
#define DELETEA(a) { if(a) { delete[] a; a = NULL; } }

#define bound(v, a, s, e)  { v += a; if(v>(e)) v = (e); if(v<(s)) v = (s); }

#ifdef WIN32
    #define PATH_SEPERATOR L"\\"
    #define LINE_SEPERATOR L"\r\n"
#else  
    #define PATH_SEPERATOR L"/"
    #ifdef __WXMAC__
        #define LINE_SEPERATOR L"\r"
    #else
        #define LINE_SEPERATOR L"\n"
    #endif
    #define __cdecl
    #define _vsnprintf vsnprintf
#endif

template<class T> inline void swap_(T &a, T &b) { T c = a; a = b; b = c; };

#ifdef WIN32
#pragma warning (3: 4189)       // local variable is initialized but not referenced
#pragma warning (disable: 4244) // conversion from 'int' to 'float', possible loss of data
#pragma warning (disable: 4355) // 'this' : used in base member initializer list
#pragma warning (disable: 4996) // 'strncpy' was declared deprecated
#endif

// from boost, stops a class from being accidental victim to default copy + destruct twice problem
// class that inherits from NonCopyable will work correctly with Vector, but give compile time error with std::vector

class NonCopyable
{
    NonCopyable(const NonCopyable &);
    const NonCopyable &operator=(const NonCopyable &);

protected:

    NonCopyable() {}
    virtual ~NonCopyable() {}
};

// helper function for containers below to delete members if they are of pointer type only

template <class X> void DelPtr(X &)   {}
template <class X> void DelPtr(X *&m) { DELETEP(m); }


// replacement for STL vector

// for any T that has a non-trivial destructor, STL requires a correct copy constructor / assignment op
// or otherwise it will free things twice on a reallocate/by value push_back.
// The vector below will behave correctly irrespective of whether a copy constructor is available
// or not and avoids the massive unnecessary (de/re)allocations the STL forces you to.
// The class itself never calls T's copy constructor, however the user of the class is still
// responsable for making sure of correct copying of elements obtained from the vector, or setting NonCopyable

// also automatically deletes pointer members and other neat things

template <class T> class Vector : public NonCopyable
{
    T *buf;
    uint alen, ulen;

    void reallocate(uint n)
    {
        ASSERT(n>ulen);
        T *obuf = buf;
        buf = (T *)malloc(sizeof(T)*(alen = n));        // use malloc instead of new to avoid constructor
        ASSERT(buf);
        if(ulen) memcpy(buf, obuf, ulen*sizeof(T));     // memcpy avoids copy constructor
        if(obuf) free(obuf);
    }

    T &push_nocons()
    {
        if(ulen==alen) reallocate(alen*2);
        return buf[ulen++];
    }

    void destruct(uint i) { buf[i].~T(); DelPtr(buf[i]); }

    public:

    T &operator[](uint i)       { ASSERT(i<ulen); return buf[i]; }
    T &operator[](uint i) const { ASSERT(i<ulen); return buf[i]; }

    Vector() { new (this) Vector(8); }
    Vector(uint n) : ulen(0), buf(NULL) { reallocate(n); }
    Vector(uint n, int c) { new (this) Vector(n); loop(i, c) push(); }
    ~Vector() { setsize(0); free(buf); }

    T &push() { return *new (&push_nocons()) T; }    // only way to create an element, to avoid copy constructor

    T *getbuf()  { return buf; }
    T &last()    { return (*this)[ulen-1]; }
    T &pop()     { return buf[--ulen]; }
    void drop()  { ASSERT(ulen); destruct(--ulen); }
    bool empty() { return ulen==0; }
    int size()   { return ulen; }

    void setsize   (uint i) { while(ulen>i) drop(); }    // explicitly destruct elements
    void setsize_nd(uint i) { while(ulen>i) pop(); }

    void sort(void *cf) { qsort(buf, ulen, sizeof(T), (int (__cdecl *)(const void *, const void *))cf); }

    void add_unique(T x)
    {
        loop(i, ulen) if(buf[i]==x) return;
        push() = x;
    }

    void remove(uint i)
    {
        ASSERT(i<ulen);
        destruct(i);
        memmove(buf+i, buf+i+1, sizeof(T)*(ulen---i-1));
    }

    void removeobj(T o)
    {
        loop(i, ulen) if(buf[i]==o) { remove(i); return; }
    }

    void append(Vector<T> &o)
    {
        loopv(i, o) push() = o[i];
    }
};

class MTRnd
{
    const static uint N = 624;
    const static uint M = 397;
    const static uint K = 0x9908B0DFU;

    uint hiBit(uint u)  { return u & 0x80000000U; }
    uint loBit(uint u)  { return u & 0x00000001U; }
    uint loBits(uint u) { return u & 0x7FFFFFFFU; }

    uint mixBits(uint u, uint v) { return hiBit(u)|loBits(v); }

    uint state[N+1];
    uint *next;
    int left;

public:

    MTRnd() : left(-1) {}

    void SeedMT(uint seed)
    {
        uint x = (seed | 1U) & 0xFFFFFFFFU, *s = state;
        int j;
        for(left=0, *s++=x, j=N; --j; *s++ = (x*=69069U) & 0xFFFFFFFFU);
    }

    uint ReloadMT()
    {
        uint *p0=state, *p2=state+2, *pM=state+M, s0, s1;
        int j;
        if(left < -1) SeedMT(4357U);
        left=N-1, next=state+1;
        for(s0=state[0], s1=state[1], j=N-M+1; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        for(pM=state, j=M; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        s1=state[0], *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        s1 ^= (s1 >> 11);
        s1 ^= (s1 <<  7) & 0x9D2C5680U;
        s1 ^= (s1 << 15) & 0xEFC60000U;
        return(s1 ^ (s1 >> 18));
    }

    uint RandomMT()
    {
        uint y;
        if(--left < 0) return(ReloadMT());
        y  = *next++;
        y ^= (y >> 11);
        y ^= (y <<  7) & 0x9D2C5680U;
        y ^= (y << 15) & 0xEFC60000U;
        return(y ^ (y >> 18));
    }

    int operator()(int max) { return RandomMT()%max; }
};


// for use with vc++ crtdbg



#ifdef _DEBUG
inline void *__cdecl operator new(size_t n, const char *fn, int l) { return ::operator new(n, 1, fn, l); }
inline void __cdecl operator delete(void *p, const char *fn, int l) { ::operator delete(p, 1, fn, l); }
#define new new(__FILE__,__LINE__)
#endif


