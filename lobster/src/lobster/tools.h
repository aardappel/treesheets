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

    virtual ~DLNodeBase() {
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

class MersenneTwister          {
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

    MersenneTwister() : left(-1) {}

    void Seed(uint seed) {
        uint x = (seed | 1U) & 0xFFFFFFFFU, *s = state;
        int j;
        for(left=0, *s++=x, j=N; --j; *s++ = (x*=69069U) & 0xFFFFFFFFU);
    }

    uint Reload() {
        uint *p0=state, *p2=state+2, *pM=state+M, s0, s1;
        int j;
        if(left < -1) Seed(4357U);
        left=N-1, next=state+1;
        for(s0=state[0], s1=state[1], j=N-M+1; --j; s0=s1, s1=*p2++)
            *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        for(pM=state, j=M; --j; s0=s1, s1=*p2++)
            *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        s1=state[0], *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
        s1 ^= (s1 >> 11);
        s1 ^= (s1 <<  7) & 0x9D2C5680U;
        s1 ^= (s1 << 15) & 0xEFC60000U;
        return(s1 ^ (s1 >> 18));
    }

    uint Random() {
        uint y;
        if(--left < 0) return(Reload());
        y  = *next++;
        y ^= (y >> 11);
        y ^= (y <<  7) & 0x9D2C5680U;
        y ^= (y << 15) & 0xEFC60000U;
        return(y ^ (y >> 18));
    }

    void ReSeed(uint seed) {
        Seed(seed);
        left = 0;
        Reload();
    }
};


class PCG32 {
    // This is apparently better than the Mersenne Twister, and its also smaller/faster!
    // Adapted from *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
    // Licensed under Apache License 2.0 (NO WARRANTY, etc. see website).

    uint64_t state;
    uint64_t inc;

    public:

    PCG32() : state(0xABADCAFEDEADBEEF), inc(0xDEADBABEABADD00D) {}

    uint32_t Random() {
        uint64_t oldstate = state;
        // Advance internal state.
        state = oldstate * 6364136223846793005ULL + (inc | 1);
        // Calculate output function (XSH RR), uses old state for max ILP.
        uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = oldstate >> 59u;
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    void ReSeed(uint32_t s) { state = s; inc = 0xDEADBABEABADD00D; }
};

template<typename T> struct RandomNumberGenerator {
    T rnd;

    void seed(uint s) { rnd.ReSeed(s); }

    int operator()(int max) { return rnd.Random() % max; }
    int operator()() { return rnd.Random(); }

    double rnddouble() { return rnd.Random() * (1.0 / 4294967296.0); }
    float rndfloat() { return (float)rnddouble(); } // FIXME: performance?
    float rndfloatsigned() { return (float)(rnddouble() * 2 - 1); }
};

// Special case for to_string to get exact float formatting we need.
template<typename T> string to_string_float(T x, int decimals = -1) {
    // stringstream gives more consistent cross-platform results than to_string() for floats, and
    // can at least be configured to turn scientific notation off.
    std::stringstream ss;
    // Suppress scientific notation.
    ss << std::fixed;
    // There's no way to tell it to just output however many decimals are actually significant,
    // sigh. Once you turn on fixed, it will default to 5 or 6, depending on platform, for both
    // float and double. So we set our own more useful defaults:
    size_t default_precision = sizeof(T) == sizeof(float) ? 6 : 12;
    ss << std::setprecision(decimals <= 0 ? default_precision : decimals);
    ss << x;
    auto s = ss.str();
    if (decimals <= 0) {
        // First trim whatever lies beyond the precision to avoid garbage digits.
        size_t max_significant = default_precision;
        max_significant += 2;  // "0."
        if (s[0] == '-') max_significant++;
        if (s.length() > max_significant) s.erase(max_significant);
        // Now strip unnecessary trailing zeroes.
        while (s.back() == '0') s.pop_back();
        // If there were only zeroes, keep at least 1.
        if (s.back() == '.') s.push_back('0');
    }
    return s;
}

inline string to_string_hex(size_t x) {
    stringstream ss;
    ss << "0x" << std::hex << x;
    return ss.str();
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

    Buf *first, *last, *iterator;
    size_t mingrowth;
    size_t totalsize;

    Buf *NewBuf(size_t _size, size_t numelems, T *elems, Buf *_next) {
        Buf *buf = (Buf *)malloc(sizeof(Buf) + sizeof(T) * _size);
        buf->size = _size;
        buf->unused = _size - numelems;
        buf->next = _next;
        memcpy(buf->Elems(), elems, sizeof(T) * numelems);
        return buf;
    }

    // Don't copy, create instances of this class with new preferably.
    Accumulator(const Accumulator &);
    Accumulator &operator=(const Accumulator &);

    public:
    Accumulator(size_t _mingrowth = 1024)
        : first(nullptr), last(nullptr), iterator(nullptr),
          mingrowth(_mingrowth), totalsize(0) {}

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
            memcpy(last->Elems() + (last->size - last->unused), newelems, sizeof(T) * fit);
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
            memcpy(dest, buf, size * sizeof(T));
            dest += size;
        }
    }
};

typedef Accumulator<uchar> ByteAccumulator;

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
    size_t firstfree;

    public:

    IntResourceManager() : firstfree(size_t(-1)) {
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
    size_t firstfree;
    const function<void(T *e)> deletefun;

    // Free slots have their lowest bit set, and represent an index (shifted by 1).
    bool IsFree(T *e) { return ((size_t)e) & 1; }
    size_t GetIndex(T *e) { return ((size_t)e) >> 1; }
    T *CreateIndex(size_t i) { return (T *)((i << 1) | 1); }
    bool ValidSlot(size_t i) { return i < elems.size() && !IsFree(elems[i]); }

    public:

    IntResourceManagerCompact(const function<void(T *e)> &_df)
        : firstfree(SIZE_MAX), deletefun(_df) {
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

// Stops a class from being accidental victim to default copy + destruct twice problem.

class NonCopyable        {
    NonCopyable(const NonCopyable&);
    const NonCopyable& operator=(const NonCopyable&);

protected:
    NonCopyable() {}
    //virtual ~NonCopyable() {}
};

// This turns a sequence of booleans (the current value, and the values it had before) into a
// single number:
//   0: "became false": false, but it was true before.
//   1: "became true": true, but it was false before.
//  >1: "still true": true, and was already true. Number indicates how many times it has been true
//      in sequence.
//  <0: "still false": false, and false before it. Negative number indicates how many times it has
//      been false.
// >=1: "true": currently true, regardless of history.
// <=0: "false": currently false, regardless of history.

// This is useful for detecting state changes and acting on them, such as for input device buttons.

// T must be a signed integer type. Bigger types means more history before it clamps.

// This is nicer than a bitfield, because basic checks like "became true" are simpler, it encodes
// the history in a more human readable way, and it can encode a way longer history in the same
// bits.
template<typename T> class TimeBool {
    T step;

    enum {
        SIGN_BIT = 1 << ((sizeof(T) * 8) - 1),
        HIGHEST_VALUE_BIT = SIGN_BIT >> 1
    };

    // This encodes 2 booleans into 1 number.
    static T Step(bool current, bool before) {
        return T(current) * 2 - T(!before);
    }

    TimeBool(T _step) : step(_step) {}

    public:

    TimeBool() : step(-HIGHEST_VALUE_BIT) {}
    TimeBool(bool current, bool before) : step(Step(current, before)) {}

    bool True() { return step >= 1; }
    bool False() { return step <= 0; }
    bool BecameTrue() { return step == 1; }
    bool BecameFalse() { return step == 0; }
    bool StillTrue() { return step > 1; }
    bool StillFalse() { return step < 0; }

    T Step() { return step; }

    T Sign() { return True() * 2 - 1; }

    // This makes one time step, retaining the current value.
    // It increases the counter, i.e. 2 -> 3 or -1 -> -2, indicating the amount of steps it has
    // been true. Only allows this to increase to the largest power of 2 that fits inside T, e.g.
    // 64 and -64 for a char.
    void Advance() {
        // Increase away from 0 if highest 2 bits are equal.
        if ((step & HIGHEST_VALUE_BIT) == (step & SIGN_BIT))
            step += Sign();
    }

    // Sets new value, assumes its different from the one before (wipes history).
    void Set(bool newcurrent) { step = Step(newcurrent, True()); }
    void Update(bool newcurrent) { Advance(); Set(newcurrent); }

    // Previous state.
    // This gives accurate history for the "still true/false" values, but for "became true/false"
    // it assumes the history is "still false/true" as opposed to "became false/true" (which is
    // also possible). This is typically desirable, and usually the difference doesn't matter.
    TimeBool Back() { return TimeBool(step - Sign() * (step & ~1 ? 1 : HIGHEST_VALUE_BIT)); }
};

typedef TimeBool<char> TimeBool8;

inline uint FNV1A(const char *s) {
    uint hash = 0x811C9DC5;
    for (auto c = s; *c; ++c) {
        hash ^= (uchar)*c;
        hash *= 0x01000193;
    }
    return hash;
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

template<typename T, typename U> T *AssertIs(U *o) {
    assert(typeid(T) == typeid(*o));
    return static_cast<T *>(o);
}

template<typename T, typename U> const T *AssertIs(const U *o) {
    assert(typeid(T) == typeid(*o));
    return static_cast<const T *>(o);
}


inline int PopCount32(uint val) {
    #ifdef _WIN32
        return (int)__popcnt(val);
    #else
        return __builtin_popcount(val);
    #endif
}

inline int PopCount64(uint64_t val) {
    #ifdef _WIN32
        #ifdef _WIN64
            return (int)__popcnt64(val);
        #else
            return (int)(__popcnt((uint)val) + __popcnt((uint)(val >> 32)));
        #endif
    #else
        return __builtin_popcountll(val);
    #endif
}
