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

// Mixing of signed and unsigned is an endless source of problems in C++.
// So, try to make as much as possible signed.
// Also, make types 64-bit as much as possible, to reduce on casts that are
// only necessary for (rapidly dwindling) 32-bit builds.
// So, code should use this type as much as possible for locals/args,
// and use the more verbose uint32_t etc for when unsigned and/or 32-bit is
// necessary.
typedef int64_t iint;

// Sadly, the STL on 32-bit platform has a 32-bit size_t for most of its
// args and indexing, so using the 64-bit type above would introduce a lot
// of casts. Best we can do is a signed version of that, until we can
// stop targetting 32-bit entirely.
// This may also be defined in Posix (sys/types.h), but in C++
// redefining typedefs is totally cool:
typedef ptrdiff_t ssize_t;

// Custom _L suffix, since neither L (different size on win/nix) or LL
// (does not convert to int64_t on nix!) is portable.
inline constexpr iint operator"" _L(unsigned long long int c) {
    return (iint)c;
}

// Size of any STL container.

// Version for always-64-bit:
template<typename T> iint isize(const T &c) { return (iint)c.size(); }
template<typename T> iint isizeof() { return (iint)sizeof(T); }
template<typename T> iint ialignof() { return (iint)alignof(T); }

// Signed variable size versions.
// Since ssize may or may not be already in the STL (C++20),
// simply provide overloads for the types we actually use for now.
inline ssize_t ssize(const string &c) { return (ssize_t)c.size(); }
inline ssize_t ssize(const string_view &c) { return (ssize_t)c.size(); }
template<typename T> ssize_t ssize(const vector<T> &c) { return (ssize_t)c.size(); }
template<typename T> ssize_t ssizeof() { return (ssize_t)sizeof(T); }
template<typename T> ssize_t salignof() { return (ssize_t)alignof(T); }

inline iint positive_bits(uint64_t i) { return (i << 1) >> 1; }

// Typed versions of memcpy.
template<typename T, typename S> void t_memcpy(T *dest, const T *src, S n) {
    memcpy(dest, src, (size_t)n * sizeof(T));
}

template<typename T, typename S> void t_memmove(T *dest, const T *src, S n) {
    memmove(dest, src, (size_t)n * sizeof(T));
}

// memcpy's that are intended to be faster if `n` is typically very small,
// since the tests for size can branch predicted better.
template<typename T, typename S> void ts_memcpy(T *dest, const T *src, S n) {
    if (n) {
        *dest++ = *src++;
        if (n > 1) {
            *dest++ = *src++;
            if (n > 2) {
                *dest++ = *src++;
                if (n > 3) {
                    *dest++ = *src++;
                    for (S i = 4; i < n; i++) *dest++ = *src++;
                }
            }
        }
    }
}

template<typename T, typename S> void tsnz_memcpy(T *dest, const T *src, S n) {
    assert(n);
    *dest++ = *src++;
    if (n > 1) {
        *dest++ = *src++;
        if (n > 2) {
            *dest++ = *src++;
            if (n > 3) {
                *dest++ = *src++;
                for (S i = 4; i < n; i++) *dest++ = *src++;
            }
        }
    }
}

// Doubly linked list.
// DLNodeRaw does not initialize nor assumes initialization, so can be used in
// situations where memory is already allocated DLNodeBase is meant to be a base
// class for objects that want to be held in a DLList. Unlike DLNodeRaw it is always
// initialized and checks that it's being used sensibly.
// These are better than std::list which doesn't have a node to inherit from,
// so causes 2 allocations rather than 1 for object hierarchies.

struct DLNodeRaw {
    DLNodeRaw *prev, *next;

    void Remove() {
        prev->next = next;
        next->prev = prev;
    }

    void InsertAfterThis(DLNodeRaw *o) {
        o->next = next;
        o->prev = this;
        next->prev = o;
        next = o;
    }

    void InsertBeforeThis(DLNodeRaw *o) {
        o->prev = prev;
        o->next = this;
        prev->next = o;
        prev = o;
    }
};

struct DLNodeBase : DLNodeRaw {
    DLNodeBase() { prev = next = nullptr; }

    bool Connected() { return next && prev; }

    ~DLNodeBase() {
        if (Connected()) Remove();
    }

    void Remove() {
        assert(Connected());
        DLNodeRaw::Remove();
        next = prev = nullptr;
    }

    void InsertAfterThis(DLNodeBase *o) {
        assert(Connected() && !o->Connected());
        DLNodeRaw::InsertAfterThis(o);
    }

    void InsertBeforeThis(DLNodeBase *o) {
        assert(Connected() && !o->Connected());
        DLNodeRaw::InsertBeforeThis(o);
    }
};

template<typename T> struct DLList : DLNodeRaw {
    typedef T nodetype;

    DLList() { next = prev = this; }

    bool Empty() { return next==this; }

    T *Get() {
        assert(!Empty());
        DLNodeRaw *r = next;
        r->Remove();
        return (T *)r;
    }

    T *Next() { return (T *)next; }
    T *Prev() { return (T *)prev; }
};

template<typename T> T *Next(T *n) { return (T *)n->next; }
template<typename T> T *Prev(T *n) { return (T *)n->prev; }

// Safe Remove on not self.
#define loopdllistother(L, n) \
    for (auto n = (L).Next(); n != (void *)&(L); n = Next(n))
// Safe Remove on self.
#define loopdllist(L, n) \
    for (auto n = (L).Next(), p = Next(n); n != (void *)&(L); (n = p),(p = Next(n)))
// Safe Remove on self reverse.
#define loopdllistreverse(L, n) \
    for (auto n = (L).Prev(), p = Prev(n); n != (void *)&(L); (n = p),(p = Prev(n)))

class MersenneTwister {
    const static uint32_t N = 624;
    const static uint32_t M = 397;
    const static uint32_t K = 0x9908B0DFU;

    uint32_t hiBit(uint32_t u) { return u & 0x80000000U; }
    uint32_t loBit(uint32_t u) { return u & 0x00000001U; }
    uint32_t loBits(uint32_t u) { return u & 0x7FFFFFFFU; }

    uint32_t mixBits(uint32_t u, uint32_t v) { return hiBit(u) | loBits(v); }

    uint32_t state[N + 1];
    uint32_t *next;
    int left = -1;

    public:

    typedef uint32_t rnd_type;

    void Seed(uint32_t seed) {
        uint32_t x = (seed | 1U) & 0xFFFFFFFFU, *s = state;
        int j;
        for (left = 0, *s++ = x, j = N; --j; *s++ = (x *= 69069U) & 0xFFFFFFFFU)
            ;
    }

    uint32_t Reload() {
        uint32_t *p0 = state, *p2 = state + 2, *pM = state + M, s0, s1;
        int j;
        if (left < -1) Seed(4357U);
        left = N - 1;
        next = state + 1;
        for (s0 = state[0], s1 = state[1], j = N - M + 1; --j; s0 = s1, s1 = *p2++)
            *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        for (pM = state, j = M; --j; s0 = s1, s1 = *p2++)
            *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        s1 = state[0];
        *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        s1 ^= (s1 >> 11);
        s1 ^= (s1 << 7) & 0x9D2C5680U;
        s1 ^= (s1 << 15) & 0xEFC60000U;
        return (s1 ^ (s1 >> 18));
    }

    uint32_t Random() {
        if (--left < 0) return (Reload());
        uint32_t y = *next++;
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9D2C5680U;
        y ^= (y << 15) & 0xEFC60000U;
        return (y ^ (y >> 18));
    }

    void ReSeed(uint32_t seed) {
        Seed(seed);
        left = 0;
        Reload();
    }
};

class PCG32 {
    // This is apparently better than the Mersenne Twister, and its also smaller/faster!
    // Adapted from *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
    // Licensed under Apache License 2.0 (NO WARRANTY, etc. see website).
    uint64_t state = 0xABADCAFEDEADBEEF;
    uint64_t inc = 0xDEADBABEABADD00D;

    public:

    typedef uint32_t rnd_type;

    uint32_t Random() {
        uint64_t oldstate = state;
        // Advance internal state.
        state = oldstate * 6364136223846793005ULL + (inc | 1);
        // Calculate output function (XSH RR), uses old state for max ILP.
        uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = oldstate >> 59u;
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    void ReSeed(uint32_t s) {
        state = s;
        inc = 0xDEADBABEABADD00D;
    }
};

// https://thompsonsed.co.uk/random-number-generators-for-c-performance-tested
// See also SplitMix64Hash below.
class SplitMix64 {
    uint64_t x = 0; /* The state can be seeded with any value. */

    public:

    typedef uint64_t rnd_type;

    uint64_t Random() {
        uint64_t z = (x += UINT64_C(0x9E3779B97F4A7C15));
        z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
        return z ^ (z >> 31);
    }

    void ReSeed(uint64_t s) {
        x = s;
    }
};

// https://prng.di.unimi.it/
// https://nullprogram.com/blog/2017/09/21/
class Xoshiro256SS {
    uint64_t s[4];

    static inline uint64_t rotl(const uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    }

    public:

    typedef uint64_t rnd_type;

    Xoshiro256SS() {
        ReSeed(0);
    }

    uint64_t Random() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    void ReSeed(uint64_t x) {
        SplitMix64 sm;
        sm.ReSeed(x);
        for (int i = 0; i < 4; i++) {
            s[i] = sm.Random();
        }
    }

};

template<typename T> struct RandomNumberGenerator {
    T rnd;

    void seed(typename T::rnd_type s) {
        rnd.ReSeed(s);
    }

    int rnd_int(int max) {
        return (int)(rnd.Random() % max);
    }

    int rnd_int() {
        return (int)rnd.Random();
    }

    int64_t rnd_int64(int64_t max) {
        static_assert(sizeof(typename T::rnd_type) == 8);
        return (int64_t)(rnd.Random() % max);
    }

    template<typename U> U rnd_i(U max) {
        static_assert(sizeof(typename T::rnd_type) >= sizeof(U));
        return (U)(rnd.Random() % max);
    }

    double rnd_double() {
        static_assert(sizeof(typename T::rnd_type) == 8);
        return (rnd.Random() >> 11) * 0x1.0p-53;
    }

    float rnd_float() {
        return float(rnd_double());
    }

    float rnd_float_signed() {
        return float(rnd_double() * 2 - 1);
    }

    double n2 = 0.0;
    bool n2_cached = false;
    // Returns gaussian with stddev of 1 and mean of 0.
    // Box Muller method.
    double rnd_gaussian() {
        n2_cached = !n2_cached;
        if (n2_cached) {
            double x, y, r;
            do {
                x = 2.0 * rnd_double() - 1;
                y = 2.0 * rnd_double() - 1;
                r = x * x + y * y;
            } while (r == 0.0 || r > 1.0);
            double d = sqrt(-2.0 * log(r) / r);
            double n1 = x * d;
            n2 = y * d;
            return n1;
        } else {
            return n2;
        }
    }
};

// Special case for to_string to get exact float formatting we need.
template<typename T> string to_string_float(T x, int decimals = -1) {
    // FIXME: make this work for other compilers.
    // Looks like gcc/clang/xcode are behind on msvc on this one for once. May need to upgrade to
    // bleeding edge versions on CI somehow.
    #if _MSC_VER >= 1923
        string s;
        for (;;) {
            // Typical capacity of an empty string is 15 or 22 on 64-bit compilers since they
            // all use "small string optimization" nowadays. That would fit most floats, needing no
            // allocation in the base case.
            // Even on 32-bit, it would likely be 7 or 10 which fits some floats.
            s.resize(s.capacity());  // Sadly need this.
            auto res = decimals >= 0
                ? to_chars(s.data(), s.data() + s.size(), x, std::chars_format::fixed, decimals)
                : to_chars(s.data(), s.data() + s.size(), x, std::chars_format::fixed);
            if (res.ec == errc()) {
                s.resize(res.ptr - s.data());
                // We like floats to be recognizable as floats, not integers.
                if (s.find_last_of('.') == string::npos && isnormal(x)) s += ".0";
                return s;
            }
            // It didn't fit, we're going to have to allocate.
            // Simply double the capacity until it works.
            // We choose 15 as minimum bound, since together with the null-terminator, it is most
            // likely to fit in a memory allocator bucket.
             s.reserve(max(s.capacity(), (size_t)15) * 2);
        }
    #else
        // ostringstream gives more consistent cross-platform results than to_string() for floats,
        // and can at least be configured to turn scientific notation off.
        ostringstream ss;
        // Suppress scientific notation.
        ss << std::fixed;
        // There's no way to tell it to just output however many decimals are actually significant,
        // sigh. Once you turn on fixed, it will default to 5 or 6, depending on platform, for both
        // float and double. So we set our own more useful defaults:
        int default_precision = sizeof(T) == sizeof(float) ? 6 : 12;
        ss << std::setprecision(decimals <= 0 ? default_precision : decimals);
        ss << x;
        auto s = ss.str();
        if (decimals <= 0) {
            // First trim whatever lies beyond the precision to avoid garbage digits.
            size_t max_significant = default_precision;
            max_significant += 2;  // "0."
            if (s[0] == '-') max_significant++;
            while (s.length() > max_significant && s.back() != '.') s.pop_back();
            // Now strip unnecessary trailing zeroes.
            while (s.back() == '0') s.pop_back();
            // If there were only zeroes, keep at least 1.
            if (s.back() == '.') s.push_back('0');
        }
        return s;
    #endif
}

inline void to_string_hex(string &sd, size_t x) {
    // FIXME: replace with to_chars.
    ostringstream ss;
    ss << "0x" << std::hex << x << std::dec;
    sd += ss.str();
}

/* Accumulator: a container that is great for accumulating data like std::vector,
   but without the reallocation/copying and unused memory overhead.
   Instead stores elements as a 2-way growing list of blocks.
   Loses the O(1) random access time, but instead has fast block-wise iteration.
   Optimized to append/prepend many of T at once.
   Can specify a minimum growth (block size) such that small appends are also efficient
*/

template <typename T> class Accumulator {
    struct Buf {
        Buf *next;
        size_t size, unused;

        T *Elems() { return this + 1; }
    };

    Buf *first = nullptr, *last = nullptr, *iterator = nullptr;
    size_t mingrowth;
    size_t totalsize = 0;

    Buf *NewBuf(size_t _size, size_t numelems, T *elems, Buf *_next) {
        Buf *buf = (Buf *)malloc(sizeof(Buf) + sizeof(T) * _size);
        buf->size = _size;
        buf->unused = _size - numelems;
        buf->next = _next;
        t_memcpy(buf->Elems(), elems, numelems);
        return buf;
    }

    // Don't copy, create instances of this class with new preferably.
    Accumulator(const Accumulator &);
    Accumulator &operator=(const Accumulator &);

    public:
    Accumulator(size_t _mingrowth = 1024) : mingrowth(_mingrowth) {}

    ~Accumulator() {
        while (first) {
            Buf *buf = first->next;
            free(first);
            first = buf;
        }
    }

    size_t Size() { return totalsize; }

    void Append(const T &elem) { Append(&elem, 1); }

    void Append(const T *newelems, size_t amount) {
        totalsize += amount;
        // First fill up any unused space in the last block, this the common path for small appends.
        if (last && last->unused) {
            size_t fit = min(amount, last->unused);
            // Note: copy constructor skipped, if any.
            t_memcpy(last->Elems() + (last->size - last->unused), newelems, fit);
            last->unused -= fit;
            amount -= fit;
        }
        // If there are more elements left, create a new block of mingrowth or bigger size.
        if (amount) {
            size_t allocsize = max(mingrowth, amount);
            Buf *buf = NewBuf(allocsize, min(amount, allocsize), newelems, nullptr);
            if (last) last->next = buf;
            else last = first = buf;
        }
    }

    void Prepend(const T *newelems, size_t amount) {
        totalsize += amount;
        // Since Prepend is a less common operation, we don't respect mingrowth here and just
        // allocate a single block every time we could support mingrowth if needed, at the cost of
        // complicating tracking where the unused space lives.
        first = NewBuf(amount, amount, newelems, first);
        if (!last) last = first;
    }

    // Custom iterator, because it is more efficient to do this a block at a time for clients
    // wishing to process multiple elements at once. limitation of one iterator at a time seems
    // reasonable.
    void ResetIterator() { iterator = first; }

    size_t Iterate(T *&buf) {
        if (!iterator) return 0;
        size_t size = iterator->size - iterator->unused;
        buf = iterator->Elems();
        iterator = iterator->next;
        return size;
    }

    // Example of iterator usage: Copy into a single linear buffer. The size of dest must equal
    // what Size() returns.
    void CopyTo(T *dest) {
        T *buf;
        size_t size;
        ResetIterator();
        while((size = Iterate(buf))) {
            t_memcpy(dest, buf, size);
            dest += size;
        }
    }
};

typedef Accumulator<uint8_t> ByteAccumulator;

// Easy "dynamic scope" helper: replace any variable by a new value, and at the end of the scope
// put the old value back.
template <typename T> struct DS {
    T temp;
    T &dest;

    DS(T &_dest, const T &val) : dest(_dest) {
        temp = dest;
        dest = val;
    }

    ~DS() {
        dest = temp;
    }
};

// Container that turns pointers into integers, with O(1) add/delete/get.
// Robust: passing invalid integers will just return a nullptr pointer / ignore the delete
// Cannot store nullptr pointers (will assert on Add)
// conveniently, index 0 is never used, so can be used by the client to indicate invalid index.

// TODO: Can change IntResourceManager to takes T's instead of T* by making the next field have a
// special value for in-use (e.g. -2, or 0 if you skip the first field).

template <typename T> class IntResourceManager {
    struct Elem {
        T *t;
        size_t nextfree;

        Elem() : t(nullptr), nextfree(size_t(-1)) {}
    };

    vector<Elem> elems;
    size_t firstfree = size_t(-1);

    public:

    IntResourceManager() {
        // A nullptr item at index 0 that can never be allocated/deleted.
        elems.push_back(Elem());
    }

    ~IntResourceManager() {
        for (auto &e : elems)
            if (e.t)
                delete e.t;
    }

    size_t Add(T *t) {
        // We can't store nullptr pointers as elements, because we wouldn't be able to distinguish
        // them from unallocated slots.
        assert(t);
        size_t i = elems.size();
        if (firstfree < i) {
            i = firstfree;
            firstfree = elems[i].nextfree;
        } else {
            elems.push_back(Elem());
        }
        elems[i].t = t;
        return i;
    }

    T *Get(size_t i) {
        return i < elems.size() ? elems[i].t : nullptr;
    }

    void Delete(size_t i) {
        T *e = Get(i);
        if (e) {
            delete e;
            elems[i].t = nullptr;
            elems[i].nextfree = firstfree;
            firstfree = i;
        }
    }

    size_t Range() { return elems.size(); }     // If you wanted to iterate over all elements.
};

// Same as IntResourceManager, but now uses pointer tagging to store the free list in-place.
// Uses half the memory. Access is slightly slower, but if memory bound could still be faster
// overall. Can store nullptr pointers, but not pointers with the lowest bit set (e.g. char *
// pointing inside of another allocation, will assert on Add).
template <typename T> class IntResourceManagerCompact {
    vector<T *> elems;
    size_t firstfree = SIZE_MAX;
    const function<void(T *e)> deletefun;

    // Free slots have their lowest bit set, and represent an index (shifted by 1).
    bool IsFree(T *e) { return ((size_t)e) & 1; }
    size_t GetIndex(T *e) { return ((size_t)e) >> 1; }
    T *CreateIndex(size_t i) { return (T *)((i << 1) | 1); }
    bool ValidSlot(size_t i) { return i < elems.size() && !IsFree(elems[i]); }

    public:

    IntResourceManagerCompact(const function<void(T *e)> &_df)
        : deletefun(_df) {
        // Slot 0 is permanently blocked, so can be used to denote illegal index.
        elems.push_back(nullptr);
    }

    ~IntResourceManagerCompact() {
        ForEach(deletefun);
    }

    void ForEach(const function<void(T *e)> &f) {
        for (auto e : elems)
            if (!IsFree(e) && e)
                f(e);
    }

    size_t Add(T *e) {
        assert(!IsFree(e)); // Can't store pointers with their lowest bit set.

        size_t i = elems.size();
        if (firstfree < i) {
            i = firstfree;
            firstfree = GetIndex(elems[i]);
            elems[i] = e;
        } else {
            elems.push_back(e);
        }
        return i;
    }

    T *Get(size_t i) {
        return ValidSlot(i) ? elems[i] : nullptr;
    }

    void Delete(size_t i) {
        if (ValidSlot(i) && i) {
            T *&e = elems[i];
            if (e) deletefun(e);
            e = CreateIndex(firstfree);
            firstfree = i;
        }
    }

    size_t Range() { return elems.size(); }     // If you wanted to iterate over all elements.
};


/*

// From: http://average-coder.blogspot.com/2012/07/python-style-range-loops-in-c.html

template<class T>
class range_iterator : public std::iterator<std::input_iterator_tag, T>{
public:
    range_iterator(const T &item) : item(item) {}

    // Dereference, returns the current item.
    const T &operator*() {
        return item;
    }

    // Prefix.
    range_iterator<T> &operator++() {
        ++item;
        return *this;
    }

    // Postfix.
    range_iterator<T> &operator++(int) {
        range_iterator<T> range_copy(*this);
        ++item;
        return range_copy;
    }

    // Compare internal item
    bool operator==(const range_iterator<T> &rhs) {
        return item == rhs.item;
    }

    // Same as above
    bool operator!=(const range_iterator<T> &rhs) {
        return !(*this == rhs);
    }
private:
    T item;
};

template<class T>
class range_wrapper {
public:
    range_wrapper(const T &r_start, const T &r_end)
    : r_start(r_start), r_end(r_end) {}

    range_iterator<T> begin() {
        return range_iterator<T>(r_start);
    }

    range_iterator<T> end() {
        return range_iterator<T>(r_end);
    }
private:
    T r_start, r_end;
};

// Returns a range_wrapper<T> containing the range [start, end)
template<class T>
range_wrapper<T> range(const T &start, const T &end) {
    return range_wrapper<T>(start, end);
}

// Returns a range_wrapper<T> containing the range [T(), end)
template<class T>
range_wrapper<T> range(const T &end) {
    return range_wrapper<T>(T(), end);
}

*/

// From: http://reedbeta.com/blog/python-like-enumerate-in-cpp17/
// FIXME: doesn't work with T* types.

template <typename T,
          typename TIter = decltype(std::begin(std::declval<T>())),
          typename = decltype(std::end(std::declval<T>()))>
          constexpr auto enumerate(T && iterable) {
    struct iterator {
        size_t i;
        TIter iter;
        bool operator != (const iterator & other) const { return iter != other.iter; }
        void operator ++ () { ++i; ++iter; }
        auto operator * () const { return std::tie(i, *iter); }
    };
    struct iterable_wrapper {
        T iterable;
        auto begin() { return iterator{ 0, std::begin(iterable) }; }
        auto end() { return iterator{ 0, std::end(iterable) }; }
    };
    return iterable_wrapper{ std::forward<T>(iterable) };
}

// --- Reversed iterable

template<typename T> struct reversion_wrapper { T& iterable; };
template<typename T> auto begin(reversion_wrapper<T> w) { return rbegin(w.iterable); }
template<typename T> auto end(reversion_wrapper<T> w) { return rend(w.iterable); }
template<typename T> reversion_wrapper<T> reverse(T &&iterable) { return { iterable }; }

// Stops a class from being accidental victim to default copy + destruct twice problem.

class NonCopyable {
    NonCopyable(const NonCopyable&) = delete;
    const NonCopyable& operator=(const NonCopyable&) = delete;

protected:
    NonCopyable() {}
    //virtual ~NonCopyable() {}
};


inline uint32_t FNV1A32(string_view s) {
    uint32_t hash = 0x811C9DC5;
    for (auto c : s) {
        hash ^= (uint8_t)c;
        hash *= 0x01000193;
    }
    return hash;
}

inline uint64_t FNV1A64(string_view s) {
    uint64_t hash = 0xCBF29CE484222325;
    for (auto c : s) {
        hash ^= (uint8_t)c;
        hash *= 0x100000001B3;
    }
    return hash;
}

// https://nullprogram.com/blog/2018/07/31/
// See also SplitMix64 RNG above.
inline uint64_t SplitMix64Hash(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94D049BB133111EB);
    return x ^ (x >> 31);
}



// dynamic_cast succeeds on both the given type and any derived types, which is frequently
// undesirable. "is" only succeeds on the exact type given, and is cheaper. It also defaults
// to pointer arguments.
template<typename T, typename U> T *Is(U *o) {
    return typeid(T) == typeid(*o) ? static_cast<T *>(o) : nullptr;
}

template<typename T, typename U> const T *Is(const U *o) {
    return typeid(T) == typeid(*o) ? static_cast<const T *>(o) : nullptr;
}

template<typename T, typename U> T *Is(U &o) {
    return typeid(T) == typeid(o) ? static_cast<T *>(&o) : nullptr;
}

template<typename T, typename U> const T *Is(const U &o) {
    return typeid(T) == typeid(o) ? static_cast<const T *>(&o) : nullptr;
}

template<typename T, typename U> T *AssertIs(U *o) {
    assert(typeid(T) == typeid(*o));
    return static_cast<T *>(o);
}

template<typename T, typename U> const T *AssertIs(const U *o) {
    assert(typeid(T) == typeid(*o));
    return static_cast<const T *>(o);
}


inline int PopCount(uint32_t val) {
    #ifdef _MSC_VER
        return (int)__popcnt(val);
    #else
        return __builtin_popcount(val);
    #endif
}

inline int PopCount(uint64_t val) {
    #ifdef _MSC_VER
        #ifdef _WIN64
            return (int)__popcnt64(val);
        #else
            return (int)(__popcnt((uint32_t)val) + __popcnt((uint32_t)(val >> 32)));
        #endif
    #else
        return __builtin_popcountll(val);
    #endif
}

inline int HighZeroBits(uint64_t val) {
    #ifdef _MSC_VER
        return (int)__lzcnt64(val);
    #else
        return __builtin_clzll(val);
    #endif
}


// string & string_view helpers.

inline string operator+(string_view a, string_view b) {
    string r;
    r.reserve(a.size() + b.size());
    r += a;
    r += b;
    return r;
}

inline auto to_string_conv(string_view sv) {
    return[sv]() { return sv; };
}

inline auto to_string_conv(const string &s) {
    return[&s]() { return string_view(s); };
}

inline auto to_string_conv(const char *cs) {
    return [sv = string_view(cs)]() { return sv; };  // Caches strlen!
}

template<int I> auto to_string_conv(const char cs[I]) {
    return[sv = string_view(cs, I)]() { return sv; };  // Static strlen!
}

template<typename T> auto to_string_conv(const T *p) {
    return[s = to_string((size_t)p)]() { return string_view(s); };  // Caches to_string!
}

template<typename T> auto to_string_conv(T i) {
    static_assert(is_scalar<T>::value, "");
    // FIXME: use to_chars.
    return [s = to_string(i)]() { return string_view(s); };  // Caches to_string!
}

inline size_t size_helper(const char *, const char *, const char *suffix) {
    return suffix ? strlen(suffix) : 0;
}
template<typename T, typename ...Ts>
size_t size_helper(const char *prefix, const char *infix, const char *suffix,
                   const T &t, const Ts &... args) {
    return (prefix ? strlen(prefix) : 0) + t().size() + size_helper(infix, infix, suffix, args...);
}

inline void cat_helper(string &s, const char *, const char *, const char *suffix) {
    if (suffix) s += suffix;
}
template<typename T, typename ...Ts>
void cat_helper(string &s, const char *prefix, const char *infix, const char *suffix,
                const T &t, const Ts&... args) {
    if (prefix) s += prefix;
    s += t();
    cat_helper(s, infix, infix, suffix, args...);
}

template<typename ...Ts>
void cat_convs(string &s, const char *prefix, const char *infix, const char *suffix,
               const Ts &... args) {
    auto len = (prefix ? strlen(prefix) : 0) + size_helper(nullptr, infix, suffix, args...);
    s.reserve(len);  // Max 1 alloc ever.
    if (prefix) s += prefix;
    cat_helper(s, nullptr, infix, suffix, args...);
    assert(s.size() == len);
}

template<typename ...Ts>
string cat(const Ts&... args) {
    string s;
    cat_convs(s, nullptr, nullptr, nullptr, to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
string cat_spaced(const Ts &... args) {
    string s;
    cat_convs(s, nullptr, " ", nullptr, to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
string cat_parens(const Ts &... args) {
    string s;
    cat_convs(s, "(", ", ", ")", to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
string cat_quoted(const Ts &... args) {
    string s;
    cat_convs(s, "\"", "\" \"", "\"", to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
void append(string &sd, const Ts &... args) {
    cat_helper(sd, nullptr, nullptr, nullptr, to_string_conv(args)...);
}

inline string Q(string_view s) { return cat("`", s, "`"); }

// This method is in C++20, but quite essential.
inline bool starts_with(string_view sv, string_view start) {
    return start.size() <= sv.size() && sv.substr(0, start.size()) == start;
}

// Efficient passing of string_view to old APIs wanting a null-terminated
// const char *: only go thru a string if not null-terminated already, which is
// often the case.
// NOTE: uses static string, so to call twice inside the same statement supply
// template args <0>, <1> etc.
template<int I = 0> const char *null_terminated(string_view sv) {
  if (!sv.data()[sv.size()]) return sv.data();
  static string temp;
  temp = sv;
  return temp.data();
}

template<typename T> T parse_int(string_view sv, int base = 10, char **end = nullptr) {
  // This should be using from_chars(), which apparently is not supported by
  // gcc/clang yet :(
  return (T)strtoll(null_terminated(sv), end, base);
}


// Strict aliasing safe memory reading and writing.
// memcpy with a constant size is replaced by a single instruction in VS release mode, and for
// clang/gcc always.

template<typename T> T ReadMem(const void *p) {
    T dest;
    memcpy(&dest, p, sizeof(T));
    return dest;
}

template<typename T> void WriteMemInc(uint8_t *&dest, const T &src) {
    memcpy(dest, &src, sizeof(T));
    dest += sizeof(T);
}

template<typename T> bool ReadSpan(const gsl::span<const uint8_t> p, T &v) {
    if (p.size_bytes() < sizeof(T))
        return false;
    memcpy(&v, p.data(), sizeof(T));
    return true;
}

template<typename T> bool ReadSpanInc(gsl::span<const uint8_t> &p, T &v) {
    if (p.size_bytes() < sizeof(T))
        return false;
    memcpy(&v, p.data(), sizeof(T));
    p = p.subspan(sizeof(T));
    return true;
}

template<typename T, typename K = uint64_t> bool ReadSpanVec(gsl::span<const uint8_t> &p, T &v) {
    K len;
    if (!ReadSpanInc<K>(p, len))
        return false;
    auto blen = len * sizeof(typename T::value_type);
    if (p.size_bytes() < blen)
        return false;
    v.resize((size_t)len);
    memcpy(v.data(), p.data(), blen);
    p = p.subspan(blen);
    return true;
}

template<typename T, typename K = uint64_t> bool SkipSpanVec(gsl::span<const uint8_t> &p) {
    K len;
    if (!ReadSpanInc(p, len))
        return false;
    auto blen = len * sizeof(typename T::value_type);
    if (p.size_bytes() < blen)
        return false;
    p = p.subspan(blen);
    return true;
}


// Enum operators.

#define DEFINE_BITWISE_OPERATORS_FOR_ENUM(T) \
    inline T operator~ (T a) { return (T)~(int)a; } \
    inline T operator| (T a, T b) { return (T)((int)a | (int)b); } \
    inline T operator& (T a, T b) { return (T)((int)a & (int)b); } \
    inline T &operator|= (T &a, T b) { return (T &)((int &)a |= (int)b); } \
    inline T &operator&= (T &a, T b) { return (T &)((int &)a &= (int)b); }

#ifndef DISABLE_EXCEPTION_HANDLING
    #define USE_EXCEPTION_HANDLING
#endif

inline void THROW_OR_ABORT(const string &s) {
    #ifdef USE_EXCEPTION_HANDLING
        throw s;
    #else
        printf("%s\n", s.c_str());
        abort();
    #endif
}

inline void unit_test_tools() {
    assert(strcmp(null_terminated<0>(string_view("aa", 1)),
                  null_terminated<1>(string_view("bb", 1))) != 0);
    assert(cat_parens(1, 2) == "(1, 2)");
}


// Stack profiling.


struct StackProfile {
    const char *name;
    void *stack_level;
    ptrdiff_t diff;
};
extern vector<StackProfile> stack_profiles;
struct StackHelper {
    StackHelper(const char *name) {
        stack_profiles.push_back({
            name,
            this,
            stack_profiles.empty() ? 0 : (char *)stack_profiles.back().stack_level - (char *)this
        });
    }
    ~StackHelper() {
        stack_profiles.pop_back();
    }
};

#define STACK_PROFILING_ON 0

#if STACK_PROFILING_ON
    #define STACK_PROFILE StackHelper __stack_helper(__FUNCTION__);
#else
    #define STACK_PROFILE
#endif




// small_vector, similar to llvm::SmallVector, stores N elements in-line, only dynamically
// allocated if more than that.
// Since this is usually used for single-digit lengths, we use a max 16-bit length for
// compactness (a small_vector<int, 2> is only 12 bytes!).
// Uses memcpy on growth, so not for elements with non-trivial copy constructors.
// It stores a pointer overlapping with the fixed elements, so there is no point to
// make N smaller than sizeof(T *) / sizeof(T).

template<typename T, int N> class small_vector {
    uint16_t len = 0;
    uint16_t cap = N;
    union {
        T elems[N];
        T *buf;
    };

    void grow() {
        assert((cap & 0x8000) == 0);  // Can't grow beyond 16-bit.
        uint16_t nc = len * 2;
        auto b = new T[nc];
        t_memcpy(b, data(), len);
        if (cap > N) delete[] buf;
        cap = nc;
        buf = b;
    }

    public:

    small_vector() {}

    ~small_vector() {
        if (cap > N) delete[] buf;
    }

    size_t size() {
        return len;
    }

    T *data() {
        return cap == N ? elems : buf;
    }

    bool empty() {
        return len == 0;
    }

    T &operator[](size_t i) {
        assert(i < len);
        return data()[i];
    }

    T *begin() { return data(); }
    T *end() { return data() + len; }

    T &back() {
        assert(len);
        return data()[len - 1];
    }

    void push_back(const T &e) {
        if (len == cap) grow();
        data()[len++] = e;
    }

    void insert(size_t at, const T &e) {
        assert(at <= len);
        if (len == cap) grow();
        if (at != len) t_memmove(data() + at + 1, data() + at, size() - at);
        data()[at] = e;
        len++;
    }

    void erase(size_t at) {
        assert(at < len);
        if (at != len - 1) t_memmove(data() + at, data() + at + 1, size() - at - 1);
        len--;
    }
};

