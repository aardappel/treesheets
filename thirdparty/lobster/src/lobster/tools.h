// Copyright 2023 Wouter van Oortmerssen. All rights reserved.
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
// This may also be defined in Posix (sys/types.h) or in string.h
// on Darwin, where it breaks the build. Undefine ssize_t if defined.
#ifdef ssize_t
    #undef ssize_t
#endif
typedef ptrdiff_t ssize_t;

// Custom _L suffix, since neither L (different size on win/nix) or LL
// (does not convert to int64_t on nix!) is portable.
inline constexpr iint operator"" _L64(unsigned long long int c) {
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
    memcpy((void *)dest, (void *)src, (size_t)n * sizeof(T));
}

template<typename T, typename S> void t_memmove(T *dest, const T *src, S n) {
    memmove((void *)dest, (void *)src, (size_t)n * sizeof(T));
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
        extern bool hwpopcount;
        if (!hwpopcount) {
            int c = 0;
            for (; val; c++) val &= val - (uint64_t)1;
            return c;
        }
        #ifndef _M_ARM64
            #ifdef _WIN64
                return (int)__popcnt64(val);
            #else
                return (int)(__popcnt((uint32_t)val) + __popcnt((uint32_t)(val >> 32)));
            #endif
        #else
            assert(!hwpopcount);
            return 0;
        #endif
    #else
        return __builtin_popcountll(val);
    #endif
}

inline int HighZeroBits(uint64_t val) {
    #ifdef _MSC_VER
        #ifndef _M_ARM64
            return (int)__lzcnt64(val);
        #else
            unsigned long index;
            int d = (int)sizeof(uint64_t) * 8;
            if (_BitScanReverse64(&index, val)) {
                d = d - 1 - index;
            }
            return d;
        #endif
    #else
        return __builtin_clzll(val);
    #endif
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
