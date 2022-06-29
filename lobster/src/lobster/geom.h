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

#ifndef LOBSTER_GEOM
#define LOBSTER_GEOM

namespace geom {

#define PI 3.14159265f
#define RAD (PI / 180.0f)

// vec: supports 1..4 components of any numerical type.

// Compile time unrolled loops for 1..4 components,
#define DOVEC(F) {                     \
        const int i = 0;               \
        F;                             \
        if constexpr (N > 1) {         \
            const int i = 1;           \
            F;                         \
            if constexpr (N > 2) {     \
                const int i = 2;       \
                F;                     \
                if constexpr (N > 3) { \
                    const int i = 3;   \
                    F;                 \
                }                      \
            }                          \
        }                              \
    }

#define DOVECR(F) { vec<T, N> _t; DOVEC(_t.c[i] = F); return _t; }
#define DOVECRI(F) { vec<int, N> _t; DOVEC(_t.c[i] = F); return _t; }
#define DOVECF(I, F) { T _ = I; DOVEC(_ = F); return _; }
#define DOVECB(I, F) { bool _ = I; DOVEC(_ = F); return _; }


template<typename T> void default_debug_value(T &a) {
    a = (T)(0xABADCAFEDEADBEEF >> ((8 - sizeof(T)) * 8));
}
union int2float { int i; float f; };
template<> inline void default_debug_value<float>(float &a) {
    int2float nan;
    nan.i = 0x7Fc00000;
    a = nan.f;
}
union int2float64 { int64_t i; double f; };
template<> inline void default_debug_value<double>(double &a) {
    int2float64 nan;
    nan.i = 0x7ff8000000000000;
    a = nan.f;
}

template<typename T, int C, int R> class matrix;

template<typename T, int N> struct basevec {
};

template<typename T> struct basevec<T, 1> {
    union {
        T c[1];
        struct { T x; };
    };
};

template<typename T> struct basevec<T, 2> {
    union {
        T c[2];
        struct { T x; T y; };
    };
};

template<typename T> struct basevec<T, 3> {
    union {
        T c[3];
        struct { T x; T y; T z; };
    };
};

template<typename T> struct basevec<T, 4> {
    union {
        T c[4];
        struct { T x; T y; T z; T w; };
    };
};

template<typename T, int N> struct vec : basevec<T, N> {
    enum { NUM_ELEMENTS = N };
    typedef T CTYPE;

    using basevec<T, N>::c;

    vec() {
        #ifndef NDEBUG
            DOVEC(default_debug_value(c[i]));
        #endif
    }

    explicit vec(T e) {
        DOVEC(c[i] = e);
    }
    explicit vec(const T *v) {
        DOVEC(c[i] = v[i]);
    }

    template<typename U> explicit vec(const vec<U,N> &v) {
        DOVEC(c[i] = (T)v.c[i]);
    }

    vec(T _x, T _y, T _z, T _w) {
        static_assert(N == 4);
        c[0] = _x;
        c[1] = _y;
        c[2] = _z;
        c[3] = _w;
    }
    vec(T _x, T _y, T _z) {
        static_assert(N == 3);
        c[0] = _x;
        c[1] = _y;
        c[2] = _z;
    }
    vec(T _x, T _y) {
        static_assert(N == 2);
        c[0] = _x;
        c[1] = _y;
    }
    vec(const pair<T, T> &p) {
        static_assert(N == 2);
        c[0] = p.first;
        c[1] = p.second;
    }

    const T *data()  const { return c; }
    const T *begin() const { return c; }
    const T *end()   const { return c + N; }

    T operator[](int i) const {
        assert((i >= 0) && (i < N));
        return c[i];
    }
    T &operator[](int i) {
        assert((i >= 0) && (i < N));
        return c[i];
    }

    vec(const vec<T, 3> &v, T e) {
        DOVEC(if constexpr (i < 3) c[i] = v.c[i]; else c[i] = e);
    }
    vec(const vec<T, 2> &v, T e) {
        DOVEC(if constexpr (i < 2) c[i] = v.c[i]; else c[i] = e);
    }

    vec<T,3>   xyz()     const { static_assert(N == 4); return vec<T,3>(c); }
    vec<T,2>   xy()      const { static_assert(N >= 3); return vec<T,2>(c); }
    pair<T, T> to_pair() const { static_assert(N == 2); return { c[0], c[1] }; }

    vec operator+(const vec &v) const { DOVECR(c[i] + v.c[i]); }
    vec operator-(const vec &v) const { DOVECR(c[i] - v.c[i]); }
    vec operator*(const vec &v) const { DOVECR(c[i] * v.c[i]); }
    vec operator/(const vec &v) const { DOVECR(c[i] / v.c[i]); }
    vec operator%(const vec &v) const { DOVECR(c[i] % v.c[i]); }

    vec operator+(T e) const { DOVECR(c[i] + e); }
    vec operator-(T e) const { DOVECR(c[i] - e); }
    vec operator*(T e) const { DOVECR(c[i] * e); }
    vec operator/(T e) const { DOVECR(c[i] / e); }
    vec operator%(T e) const { DOVECR(c[i] % e); }
    vec operator&(T e) const { DOVECR(c[i] & e); }
    vec operator|(T e) const { DOVECR(c[i] | e); }
    vec operator<<(T e) const { DOVECR(c[i] << e); }
    vec operator>>(T e) const { DOVECR(c[i] >> e); }

    vec operator-() const { DOVECR(-c[i]); }

    vec &operator+=(const vec &v) { DOVEC(c[i] += v.c[i]); return *this; }
    vec &operator-=(const vec &v) { DOVEC(c[i] -= v.c[i]); return *this; }
    vec &operator*=(const vec &v) { DOVEC(c[i] *= v.c[i]); return *this; }
    vec &operator/=(const vec &v) { DOVEC(c[i] /= v.c[i]); return *this; }

    vec &operator+=(T e) { DOVEC(c[i] += e); return *this; }
    vec &operator-=(T e) { DOVEC(c[i] -= e); return *this; }
    vec &operator*=(T e) { DOVEC(c[i] *= e); return *this; }
    vec &operator/=(T e) { DOVEC(c[i] /= e); return *this; }
    vec &operator&=(T e) { DOVEC(c[i] &= e); return *this; }

    bool operator<=(const vec &v) const {
        DOVECB(true, _ && c[i] <= v.c[i]);
    }
    bool operator< (const vec &v) const {
        DOVECB(true, _ && c[i] < v.c[i]);
    }
    bool operator>=(const vec &v) const {
        DOVECB(true, _ && c[i] >= v.c[i]);
    }
    bool operator> (const vec &v) const {
        DOVECB(true, _ && c[i] > v.c[i]);
    }
    bool operator==(const vec &v) const {
        DOVECB(true, _ && c[i] == v.c[i]);
    }
    bool operator!=(const vec &v) const {
        DOVECB(false, _ || c[i] != v.c[i]);
    }

    bool operator<=(T e) const { DOVECB(true, _ && c[i] <= e); }
    bool operator< (T e) const { DOVECB(true, _ && c[i] <  e); }
    bool operator>=(T e) const { DOVECB(true, _ && c[i] >= e); }
    bool operator> (T e) const { DOVECB(true, _ && c[i] >  e); }
    bool operator==(T e) const { DOVECB(true, _ && c[i] == e); }
    bool operator!=(T e) const { DOVECB(false, _ || c[i] != e); }

    vec<int, N> lte(T e) const { DOVECRI(c[i] <= e); }
    vec<int, N> lt (T e) const { DOVECRI(c[i] <  e); }
    vec<int, N> gte(T e) const { DOVECRI(c[i] >= e); }
    vec<int, N> gt (T e) const { DOVECRI(c[i] >  e); }
    vec<int, N> eq (T e) const { DOVECRI(c[i] == e); }
    vec<int, N> ne (T e) const { DOVECRI(c[i] != e); }

    vec<int, N> lte(const vec &v) const { DOVECRI(c[i] <= v.c[i]); }
    vec<int, N> lt (const vec &v) const { DOVECRI(c[i] <  v.c[i]); }
    vec<int, N> gte(const vec &v) const { DOVECRI(c[i] >= v.c[i]); }
    vec<int, N> gt (const vec &v) const { DOVECRI(c[i] >  v.c[i]); }
    vec<int, N> eq (const vec &v) const { DOVECRI(c[i] == v.c[i]); }
    vec<int, N> ne (const vec &v) const { DOVECRI(c[i] != v.c[i]); }

    vec iflt(T e, const vec &a, const vec &b) const {
        DOVECR(c[i] < e ? a.c[i] : b.c[i]);
    }

    string to_string() const {
        string s = "(";
        DOVEC(if (i) s += ", "; s += std::to_string(c[i]));
        return s + ")";
    }

    T volume() const { DOVECF(1, _ * c[i]); }

    template<typename T2, int C, int R> friend class matrix;
};

template<typename T, int N> struct vec_iterator {
    vec<T, N> min;
    vec<T, N> max;
    vec<T, N> value;

    vec_iterator(vec<T, N> min, vec<T, N> max) : min(min), max(max), value(min) {}
    vec_iterator(vec<T, N> min, vec<T, N> max, vec<T, N> value)
        : min(min), max(max), value(value) {}

    vec<T, N> &operator*() {
        return value;
    }
    vec<T, N> *operator->() {
        return &value;
    }

    vec_iterator &operator++() {
        for (int i = 0; i < N; ++i) {
            auto &d = value[i];
            d++;
            if (d < max[i]) return *this;
            d = min[i];
        }
        return *this;
    }
    vec_iterator &operator--() {
        for (int i = 0; i < N; ++i) {
            auto &d = value[i];
            d--;
            if (d >= min[i]) return *this;
            d = max[i] - 1;
        }
        return *this;
    }
    vec_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }
    vec_iterator operator--(int) {
        auto tmp = *this;
        --(*this);
        return tmp;
    }

    vec_iterator begin() {
        return vec_iterator(min, max, min);
    }
    vec_iterator end() {
        return vec_iterator(min, max, max - vec<T, N>(1));
    }

    friend bool operator==(const vec_iterator &a, const vec_iterator &b) {
        return a.value == b.value;
    };
    friend bool operator!=(const vec_iterator &a, const vec_iterator &b) {
        return a.value != b.value;
    };
};

template<typename T> inline T mix(T a, T b, float f) { return (T)(a * (1 - f) + b * f); }

// Rational replacement for powf (when t = 0..1), due to
// "Ratioquadrics: An Alternative Model for Superquadrics"
//inline float rpowf(float t, float e)
//{
//    assert(t >= 0 && t <= 1); e = 1 / e + 1.85f/* wtf */; return t / (e + (1 - e) * t);
//}
inline float rpowf(float t, float e) { return expf(e * logf(t)); }

// Exponentiation by squaring for integer types.
template<typename T> T ipow(T base, T exp) {
    assert(exp >= 0);
    T result = 1;
    for (;;) {
        if (exp & 1) result *= base;
        exp >>= 1;
        if (!exp) return result;
        base *= base;
    }
}

template<typename T> int ffloor(T f) { int i = (int)f; return i - (f < i); }
template<typename T> int fceil(T f) { int i = (int)f; return i + (f > i); }

template<typename T> bool in_range(T x, T range, T bias = 0) {
    return x >= bias && x < bias + range;
}


template<typename T> int signum(T val) {
    return (T(0) < val) - (val < T(0));
}

template<typename T, int N> inline vec<T,N> operator+(T f, const vec<T,N> &v) { DOVECR(f + v.c[i]); }
template<typename T, int N> inline vec<T,N> operator-(T f, const vec<T,N> &v) { DOVECR(f - v.c[i]); }
template<typename T, int N> inline vec<T,N> operator*(T f, const vec<T,N> &v) { DOVECR(f * v.c[i]); }
template<typename T, int N> inline vec<T,N> operator/(T f, const vec<T,N> &v) { DOVECR(f / v.c[i]); }

template<typename T, int N> inline T dot(const vec<T,N> &a, const vec<T,N> &b) {
    DOVECF(0, _ + a.c[i] * b.c[i]);
}
template<typename T, int N> inline T squaredlength(const vec<T,N> &v) { return dot(v, v); }
template<typename T, int N> inline T length(const vec<T,N> &v) { return sqrt(squaredlength(v)); }
template<typename T, int N> inline vec<T,N> normalize(const vec<T,N> &v) { return v / length(v); }
template<typename T, int N> inline vec<T,N> abs(const vec<T,N> &v) { DOVECR(fabsf(v.c[i])); }
template<typename T, int N> inline vec<T,N> sign(const vec<T,N> &v) {
    DOVECR((T)(v.c[i] >= 0 ? 1 : -1));
}
template<typename T, int N> inline vec<int,N> signum(const vec<T,N> &v) {
    DOVECR(signum(v.c[i]));
}
template<typename T, int N> inline vec<T,N> min(const vec<T,N> &a, const vec<T,N> &b) {
    DOVECR(std::min(a.c[i], b.c[i]));
}
template<typename T, int N> inline vec<T,N> max(const vec<T,N> &a, const vec<T,N> &b) {
    DOVECR(std::max(a.c[i], b.c[i]));
}
template<typename T, int N> inline vec<T,N> pow(const vec<T,N> &a, const vec<T,N> &b) {
    DOVECR(powf(a.c[i], b.c[i]));
}
template<typename T, int N> inline vec<T,N> rpow(const vec<T,N> &a, const vec<T,N> &b) {
    DOVECR(rpowf(a.c[i], b.c[i]));
}

template<typename T, int N> inline vec<T, N> from_srgb(const vec<T, N> &a) {
    DOVECR(powf(a.c[i], 2.2f));
}
template<typename T, int N> inline vec<T, N> to_srgb(const vec<T, N> &a) {
    DOVECR(powf(a.c[i], 1.0f / 2.2f));
}

template<typename T, int N> inline T min(const vec<T,N> &a) {
    DOVECF(std::numeric_limits<T>::max(), std::min(a.c[i], _));
}
template<typename T, int N> inline T max(const vec<T,N> &a) {
    DOVECF(-std::numeric_limits<T>::max(), std::max(a.c[i], _));
}

template<typename T, int N> inline T sum(const vec<T,N> &a) {
    DOVECF(0, _ + a.c[i]);
}
template<typename T, int N> inline T average(const vec<T,N> &a) {
    return sum(a) / N;
}
template<typename T, int N> inline T manhattan(const vec<T, N> &a) {
    DOVECF(0, _ + std::abs(a.c[i]));
}

template<typename T, int N> inline vec<int, N> fceil(const vec<T, N> &v) {
    DOVECRI(fceil(v.c[i]));
}
template<typename T, int N> inline vec<int, N> ffloor(const vec<T, N> &v) {
    DOVECRI(ffloor(v.c[i]));
}
template<typename T, int N> inline vec<T, N> round(const vec<T, N> &v) {
    DOVECR(roundf(v.c[i]));
}

template<typename T> inline T clamp(T v, T lo, T hi) {
    static_assert(is_scalar<T>(), "");
    return std::min(hi, std::max(lo, v));
}
template<typename T, int N> inline vec<T, N> clamp(const vec<T, N> &v, const vec<T, N> &lo,
                                                   const vec<T, N> &hi) {
    DOVECR(clamp(v.c[i], lo.c[i], hi.c[i]));
}
template<typename T, int N> inline vec<T, N> clamp(const vec<T, N> &v, T lo, T hi) {
    DOVECR(clamp(v.c[i], lo, hi));
}

template<typename T, int N> inline bool in_range(const vec<T, N> &v, const vec<T, N> &range,
                                                   const vec<T, N> &bias = vec<T, N>(0)) {
    DOVECB(true, _ && v[i] >= bias.c[i] && v[i] < (bias.c[i] + range.c[i]));
}

template<typename T, int N, typename R> inline vec<float, N> rndunitvec(RandomNumberGenerator<R> &r) {
    DOVECR(r.rnd_float());
}
template<typename T, int N, typename R> inline vec<float, N> rndsignedvec(RandomNumberGenerator<R> &r) {
    DOVECR(r.rnd_float_signed());
}
template<typename T, int N, typename R> inline vec<int, N> rndivec(RandomNumberGenerator<R> &r,
                                                                   const vec<int, N> &max) {
    DOVECR(r.rnd_int(max[i]));
}

#undef DOVEC
#undef DOVECR
#undef DOVECRI
#undef DOVECF
#undef DOVECB

typedef vec<float, 2> float2;
typedef vec<float, 3> float3;
typedef vec<float, 4> float4;

typedef vec<int, 2> int2;
typedef vec<int, 3> int3;
typedef vec<int, 4> int4;

typedef vec<double, 2> double2;
typedef vec<double, 3> double3;
typedef vec<double, 4> double4;

typedef vec<iint, 2> iint2;
typedef vec<iint, 3> iint3;
typedef vec<iint, 4> iint4;

typedef vec<uint8_t, 4> byte4;

typedef vec<size_t, 2> size_t2;

const float4 float4_0 = float4(0.0f);
const float4 float4_1 = float4(1.0f);

const float3 float3_0 = float3(0.0f);
const float3 float3_1 = float3(1.0f);
const float3 float3_x = float3(1, 0, 0);
const float3 float3_y = float3(0, 1, 0);
const float3 float3_z = float3(0, 0, 1);

const float2 float2_0 = float2(0.0f);
const float2 float2_1 = float2(1.0f);
const float2 float2_x = float2(1, 0);
const float2 float2_y = float2(0, 1);

const int2 int2_0 = int2(0);
const int2 int2_1 = int2(1);

const int3 int3_0 = int3(0);
const int3 int3_1 = int3(1);

const double4 double4_0 = double4(0.0);
const double3 double3_0 = double3(0.0);
const double2 double2_0 = double2(0.0);

const iint2 iint2_0 = iint2(0_L);
const iint2 iint2_1 = iint2(1_L);
const iint3 iint3_0 = iint3(0_L);

const byte4 byte4_0   = byte4((uint8_t)0);
const byte4 byte4_255 = byte4((uint8_t)255);


template<typename T> vec<T, 3> cross(const vec<T, 3> &a, const vec<T, 3> &b) {
    return vec<T, 3>(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline float smoothminh(float a, float b, float k) {
    return std::min(std::max(0.5f + 0.5f * (b - a) / k, 0.0f), 1.0f);
}

template<typename T> float smoothmix(T a, T b, T k, T h) {
    return mix(b, a, h) - k * h * (1.0f - h);
}

inline float smoothmin(float a, float b, float k) {
    return smoothmix(a, b, k, smoothminh(a, b, k));
}

inline float smoothmax(float a, float b, float r) {
    auto u = max(float2(r + a, r + b), float2_0);
    return std::min(-r, std::max(a, b)) + length(u);
}

inline float smoothstep(float x) {
    return x * x * (3 - 2 * x);
}

inline float smootherstep(float x) {
    return x * x * x * (x * (x * 6 - 15) + 10);
}

template<typename T> inline float3 random_point_in_sphere(RandomNumberGenerator<T> &r) {
    for (;;) {
        auto p = rndsignedvec<float, 3, T>(r);
        if (dot(p, p) < 1.f)
            return p;
    }
}

inline float3 rotateX(const float3 &v, const float2 &a) {
    return float3(v.x, v.y * a.x - v.z * a.y, v.y * a.y + v.z * a.x);
}
inline float3 rotateY(const float3 &v, const float2 &a) {
    return float3(v.x * a.x + v.z * a.y, v.y, v.z * a.x - v.x * a.y);
}
inline float3 rotateZ(const float3 &v, const float2 &a) {
    return float3(v.x * a.x - v.y * a.y, v.x * a.y + v.y * a.x, v.z);
}

inline float3 rotateX(const float3 &v, float a) { return rotateX(v, float2(cosf(a), sinf(a))); }
inline float3 rotateY(const float3 &v, float a) { return rotateY(v, float2(cosf(a), sinf(a))); }
inline float3 rotateZ(const float3 &v, float a) { return rotateZ(v, float2(cosf(a), sinf(a))); }

struct quat : float4 {
    quat() {}
    quat(float x, float y, float z, float w) : float4(x, y, z, w) {}
    quat(const float3 &v, float w)           : float4(v, w) {}
    quat(const float4 &v)                    : float4(v) {}
    quat(float angle, const float3 &axis) {
        float s = sinf(0.5f*angle);
        *this = quat(s * axis, cosf(0.5f * angle));
    }
    explicit quat(const float3 &v) : float4(v, -sqrtf(std::max(1.0f - squaredlength(v), 0.0f))) {}
    explicit quat(const float *v) : float4(v[0], v[1], v[2], v[3]) {}

    quat operator*(const quat &o) const {
        return quat(w * o.x + x * o.w + y * o.z - z * o.y,
                    w * o.y - x * o.z + y * o.w + z * o.x,
                    w * o.z + x * o.y - y * o.x + z * o.w,
                    w * o.w - x * o.x - y * o.y - z * o.z);
    }

    quat operator-() const { return quat(-xyz(), w); }

    void flip() { *this = quat(-(float4)*this); }

    float3 transform(const float3 &p) const {
        return p + cross(xyz(), cross(xyz(), p) + p * w) * 2.0f;
    }
};

template<typename T, int C, int R> class matrix {
    typedef vec<T,R> V;
    V m[C];

    public:
    matrix() {}

    explicit matrix(T e) {
        for (int x = 0; x < C; x++)
            for (int y = 0; y < R; y++)
                m[x].c[y] = e * (T)(x == y);
    }

    explicit matrix(const V &v) {
        for (int x = 0; x < C; x++)
            for (int y = 0; y < R; y++)
                m[x].c[y] = x == y ? v[x] : 0;
    }

    explicit matrix(const T *mat_data) {
        for (int x = 0; x < C; x++)
            for (int y = 0; y < R; y++)
                m[x].c[y] = *mat_data++;
    }

    matrix(V x, V y, V z, V w) { static_assert(C == 4); m[0] = x; m[1] = y; m[2] = z; m[3] = w; }
    matrix(V x, V y, V z)      { static_assert(C == 3); m[0] = x; m[1] = y; m[2] = z; }
    matrix(V x, V y)           { static_assert(C == 2); m[0] = x; m[1] = y; }

    matrix(float a, const float3 &v) {
        static_assert(C >= 3);
        static_assert(R >= 3);

        *this = matrix(1);

        float s = sinf(a);
        float c = cosf(a);

        const float	t = 1.f - c;
        const float3 n = normalize(v);
        const float	x = n.x;
        const float	y = n.y;
        const float	z = n.z;

        m[0].x = t*x*x + c;
        m[0].y = t*x*y + z*s;
        m[0].z = t*x*z - y*s;
        m[1].x = t*x*y - z*s;
        m[1].y = t*y*y + c;
        m[1].z = t*y*z + x*s;
        m[2].x = t*x*z + y*s;
        m[2].y = t*y*z - x*s;
        m[2].z = t*z*z + c;
    }

    const T *data()  const { return m[0].c; }
          T *data_mut()    { return m[0].c; }
    const T *begin() const { return m[0].c; }
    const T *end()   const { return m[C].c; }

    const V &operator[](size_t i) const { return m[i]; }

    // not an operator on purpose, don't use outside this header
    void set(int i, const V &v) { m[i] = v; }

    vec<T, C> row(size_t i) const {
        if constexpr (C == 2) return vec<T,C>(m[0].c[i], m[1].c[i]);
        if constexpr (C == 3) return vec<T,C>(m[0].c[i], m[1].c[i], m[2].c[i]);
        if constexpr (C == 4) return vec<T,C>(m[0].c[i], m[1].c[i], m[2].c[i], m[3].c[i]);
    }

    matrix<T,R,C> transpose() const {
        matrix<T,R,C> res;
        for (int y = 0; y < R; y++) res.set(y, row(y));
        return res;
    }

    V operator*(const vec<T,C> &v) const {
        V res(0.0f);
        for (int i = 0; i < C; i++)
            res += m[i] * v.c[i];
        return res;
    }

    matrix &operator*=(const matrix &o) { return *this = *this * o; }

    matrix operator*(const matrix<T,R,C> &o) const {
        matrix<T,R,C> t = transpose();
        matrix<T,R,R> res;
        for (int x = 0; x < R; x++)
            for (int y = 0; y < R; y++)
                res.m[x].c[y] = dot(t[y], o[x]);
        return res;
    }

    matrix operator*(T f) const {
        matrix res;
        for (int x = 0; x < C; x++)
            res.m[x] = m[x] * f;
        return res;
    }

    matrix operator+(const matrix &o) const {
        matrix res;
        for (int x = 0; x < C; x++)
            res.m[x] = m[x] + o.m[x];
        return res;
    }

    string to_string() const {
        string s = "(";
        for (int x = 0; x < C; x++) {
            if (x) s += ", ";
            s += m[x].to_string();
        }
        return s + ")";
    }
};

template<typename T, int C, int R> inline vec<T,R> operator*(const vec<T,R> &v,
                                                             const matrix<T,C,R> &m) {
    vec<T,R> t;
    for (int i = 0; i < R; i++) t[i] = dot(v, m[i]);
    return t;
}

typedef matrix<float,4,4> float4x4;
typedef matrix<float,3,3> float3x3;
typedef matrix<float,3,4> float3x4;
typedef matrix<float,4,3> float4x3;

const float4x4 float4x4_1 = float4x4(1);
const float3x3 float3x3_1 = float3x3(1);

inline float3x4 operator*(const float3x4 &m, const float3x4 &o) {  // FIXME: clean this up
    return float3x4(
        (o[0]*m[0].x + o[1]*m[0].y + o[2]*m[0].z + float4(0, 0, 0, m[0].w)),
        (o[0]*m[1].x + o[1]*m[1].y + o[2]*m[1].z + float4(0, 0, 0, m[1].w)),
        (o[0]*m[2].x + o[1]*m[2].y + o[2]*m[2].z + float4(0, 0, 0, m[2].w)));
}

inline float4x4 translation(const float3 &t) {
    return float4x4(
        float4(1, 0, 0, 0),
        float4(0, 1, 0, 0),
        float4(0, 0, 1, 0),
        float4(t, 1));
}

inline float4x4 scaling(float s) {
    return float4x4(
        float4(s, 0, 0, 0),
        float4(0, s, 0, 0),
        float4(0, 0, s, 0),
        float4(0, 0, 0, 1));
}

inline float4x4 scaling(const float3 &s) {
    return float4x4(
        float4(s.x, 0,   0,   0),
        float4(0,   s.y, 0,   0),
        float4(0,   0,   s.z, 0),
        float4(0,   0,   0,   1));
}

inline float4x4 rotationX(const float2 &v) {
    return float4x4(
        float4(1,  0,   0,   0),
        float4(0,  v.x, v.y, 0),
        float4(0, -v.y, v.x, 0),
        float4(0,  0,   0,   1));
}

inline float4x4 rotationY(const float2 &v) {
    return float4x4(
        float4(v.x, 0, -v.y, 0),
        float4(0,   1,  0,   0),
        float4(v.y, 0,  v.x, 0),
        float4(0,   0,  0,   1));
}

inline float4x4 rotationZ(const float2 &v) {
    return float4x4(
        float4( v.x, v.y, 0, 0),
        float4(-v.y, v.x, 0, 0),
        float4( 0,   0,   1, 0),
        float4( 0,   0,   0, 1));
}

inline float4x4 rotation3D(const float3 &v) {
    return float4x4(
        float4( 0,   -v.z,  v.y, 0),
        float4( v.z,  0,   -v.x, 0),
        float4(-v.y,  v.x,  0,   0),
        float4( 0,    0,    0,   1));
}

inline float4x4 rotationX(float a) { return rotationX(float2(cosf(a), sinf(a))); }
inline float4x4 rotationY(float a) { return rotationY(float2(cosf(a), sinf(a))); }
inline float4x4 rotationZ(float a) { return rotationZ(float2(cosf(a), sinf(a))); }

inline quat quatfromtwovectors(const float3 &u, const float3 &v) {
    float norm_u_norm_v = sqrt(dot(u, u) * dot(v, v));
    float real_part = norm_u_norm_v + dot(u, v);
    float3 w;
    if (real_part < 1.e-6f * norm_u_norm_v) {
        // If u and v are exactly opposite, rotate 180 degrees
        // around an arbitrary orthogonal axis. Axis normalisation
        // can happen later, when we normalise the quaternion.
        real_part = 0.0f;
        w = fabsf(u.x) > fabsf(u.z) ? float3(-u.y, u.x, 0.f)
                                        : float3(0.f,   -u.z, u.y);
    } else {
        // Otherwise, build quaternion the standard way.
        w = cross(u, v);
    }
    return normalize(quat(w, real_part));
}

inline float3x3 rotation(const quat &q) {
    float x = q.x, y = q.y, z = q.z, w = q.w,
              tx = 2*x, ty = 2*y, tz = 2*z,
              txx = tx*x, tyy = ty*y, tzz = tz*z,
              txy = tx*y, txz = tx*z, tyz = ty*z,
              twx = w*tx, twy = w*ty, twz = w*tz;
    return float3x3(float3(1 - (tyy + tzz), txy - twz, txz + twy),
                    float3(txy + twz, 1 - (txx + tzz), tyz - twx),
                    float3(txz - twy, tyz + twx, 1 - (txx + tyy)));
}

// FIXME: This is not generic, here because of IQM.
inline float3x4 rotationscaletrans(const quat &q, const float3 &s, const float3 &t) {
    float3x3 m = rotation(q);
    for (int i = 0; i < 3; i++) m.set(i, m[i] * s);
    return float3x4(float4(m[0], t.x),
                    float4(m[1], t.y),
                    float4(m[2], t.z));
}

inline float4x4 float3x3to4x4(const float3x3 &m) {
    return float4x4(float4(m[0], 0),
                    float4(m[1], 0),
                    float4(m[2], 0),
                    float4(0, 0, 0, 1));
}

// FIXME: This is not generic, here because of IQM.
inline float3x4 invertortho(const float3x4 &o) {
    float4x3 inv = o.transpose();
    for (int i = 0; i < 3; i++) inv.set(i, inv[i] / squaredlength(inv[i]));
    return float3x4(float4(inv[0], -dot(inv[0], inv[3])),
                    float4(inv[1], -dot(inv[1], inv[3])),
                    float4(inv[2], -dot(inv[2], inv[3])));
}

inline float4x4 invert(const float4x4 &mat) {
    auto m = mat.data();
    float4x4 dest;
    auto inv = dest.data_mut();

    inv[0] =   m[5]  * m[10] * m[15] -
               m[5]  * m[11] * m[14] -
               m[9]  * m[6]  * m[15] +
               m[9]  * m[7]  * m[14] +
               m[13] * m[6]  * m[11] -
               m[13] * m[7]  * m[10];

    inv[4] =  -m[4]  * m[10] * m[15] +
               m[4]  * m[11] * m[14] +
               m[8]  * m[6]  * m[15] -
               m[8]  * m[7]  * m[14] -
               m[12] * m[6]  * m[11] +
               m[12] * m[7]  * m[10];

    inv[8] =   m[4]  * m[9]  * m[15] -
               m[4]  * m[11] * m[13] -
               m[8]  * m[5]  * m[15] +
               m[8]  * m[7]  * m[13] +
               m[12] * m[5]  * m[11] -
               m[12] * m[7]  * m[9];

    inv[12] = -m[4]  * m[9]  * m[14] +
               m[4]  * m[10] * m[13] +
               m[8]  * m[5]  * m[14] -
               m[8]  * m[6]  * m[13] -
               m[12] * m[5]  * m[10] +
               m[12] * m[6]  * m[9];

    inv[1] =  -m[1]  * m[10] * m[15] +
               m[1]  * m[11] * m[14] +
               m[9]  * m[2]  * m[15] -
               m[9]  * m[3]  * m[14] -
               m[13] * m[2]  * m[11] +
               m[13] * m[3]  * m[10];

    inv[5] =   m[0]  * m[10] * m[15] -
               m[0]  * m[11] * m[14] -
               m[8]  * m[2]  * m[15] +
               m[8]  * m[3]  * m[14] +
               m[12] * m[2]  * m[11] -
               m[12] * m[3]  * m[10];

    inv[9] =  -m[0]  * m[9]  * m[15] +
               m[0]  * m[11] * m[13] +
               m[8]  * m[1]  * m[15] -
               m[8]  * m[3]  * m[13] -
               m[12] * m[1]  * m[11] +
               m[12] * m[3]  * m[9];

    inv[13] =  m[0]  * m[9]  * m[14] -
               m[0]  * m[10] * m[13] -
               m[8]  * m[1]  * m[14] +
               m[8]  * m[2]  * m[13] +
               m[12] * m[1]  * m[10] -
               m[12] * m[2]  * m[9];

    inv[2] =   m[1]  * m[6]  * m[15] -
               m[1]  * m[7]  * m[14] -
               m[5]  * m[2]  * m[15] +
               m[5]  * m[3]  * m[14] +
               m[13] * m[2]  * m[7]  -
               m[13] * m[3]  * m[6];

    inv[6] =  -m[0]  * m[6]  * m[15] +
               m[0]  * m[7]  * m[14] +
               m[4]  * m[2]  * m[15] -
               m[4]  * m[3]  * m[14] -
               m[12] * m[2]  * m[7]  +
               m[12] * m[3]  * m[6];

    inv[10] =  m[0]  * m[5]  * m[15] -
               m[0]  * m[7]  * m[13] -
               m[4]  * m[1]  * m[15] +
               m[4]  * m[3]  * m[13] +
               m[12] * m[1]  * m[7]  -
               m[12] * m[3]  * m[5];

    inv[14] = -m[0]  * m[5]  * m[14] +
               m[0]  * m[6]  * m[13] +
               m[4]  * m[1]  * m[14] -
               m[4]  * m[2]  * m[13] -
               m[12] * m[1]  * m[6]  +
               m[12] * m[2]  * m[5];

    inv[3] =  -m[1]  * m[6]  * m[11] +
               m[1]  * m[7]  * m[10] +
               m[5]  * m[2]  * m[11] -
               m[5]  * m[3]  * m[10] -
               m[9]  * m[2]  * m[7]  +
               m[9]  * m[3]  * m[6];

    inv[7] =   m[0]  * m[6]  * m[11] -
               m[0]  * m[7]  * m[10] -
               m[4]  * m[2]  * m[11] +
               m[4]  * m[3]  * m[10] +
               m[8]  * m[2]  * m[7]  -
               m[8]  * m[3]  * m[6];

    inv[11] = -m[0]  * m[5]  * m[11] +
               m[0]  * m[7]  * m[9]  +
               m[4]  * m[1]  * m[11] -
               m[4]  * m[3]  * m[9] -
               m[8]  * m[1]  * m[7]  +
               m[8]  * m[3]  * m[5];

    inv[15] =  m[0]  * m[5]  * m[10] -
               m[0]  * m[6]  * m[9]  -
               m[4]  * m[1]  * m[10] +
               m[4]  * m[2]  * m[9]  +
               m[8]  * m[1]  * m[6]  -
               m[8]  * m[2]  * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0) {
        assert(false);
        return float4x4_1;
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        inv[i] = inv[i] * det;
    return dest;
}

// Handedness: 1.f for RH, -1.f for LH.
inline float4x4 perspective( float fovy, float aspect, float znear, float zfar, float handedness) {
    const float y = 1 / tanf(fovy * .5f);
    const float x = y / aspect;
    const float zdist = (znear - zfar) * handedness;
    const float zfar_per_zdist = zfar / zdist;
    return float4x4(
        float4(x, 0, 0,					   		             0),
        float4(0, y, 0,					   		             0),
        float4(0, 0, zfar_per_zdist,    	   	             -1.f * handedness),
        float4(0, 0, 2 * znear * zfar_per_zdist * handedness, 0));
}

inline float4x4 ortho(float left, float right, float bottom, float top, float znear, float zfar) {
    return float4x4(
        float4(2.0f / (right - left), 0, 0, 0),
        float4(0, 2.0f / (top - bottom), 0, 0),
        float4(0, 0, -2.0f / (zfar - znear), 0),
        float4(-(right + left) / (right - left),
               -(top + bottom) / (top - bottom),
               -(zfar + znear) / (zfar - znear),
               1.0f)
    );
}

inline byte4 quantizec(const float3 &v, float w) { return byte4(float4(v, w) * 255); }
inline byte4 quantizec(const float4 &v) { return byte4(v * 255); }

inline float4 color2vec(const byte4 &col) { return float4(col) / 255; }

// Spline interpolation.
template<typename T>
inline vec<T, 3> cardinal_spline(const vec<T, 3> &z, const vec<T, 3> &a, const vec<T, 3> &b,
                                 const vec<T, 3> &c, T s, T tension = 0.5) {
    T s2 = s * s;
    T s3 = s * s2;
    return a * (2 * s3 - 3 * s2 + 1) + b * (-2 * s3 + 3 * s2) +
           (b - z) * tension * (s3 - 2 * s2 + s) + (c - a) * tension * (s3 - s2);
}

inline float triangle_area(const float3 &a, const float3 &b, const float3 &c) {
    return length(cross(b - a, c - a)) / 2;
}

template<typename T>
bool line_intersect(const vec<T, 2> &l1a, const vec<T, 2> &l1b, const vec<T, 2> &l2a,
                    const vec<T, 2> &l2b, vec<T, 2> *out = nullptr) {
    vec<T, 2> a(l1b - l1a);
    vec<T, 2> b(l2b - l2a);
    vec<T, 2> aperp(-a.y, a.x);
    auto f = dot(aperp, b);
    if (!f) return false;  // Parallel.
    vec<T, 2> c(l2b - l1b);
    vec<T, 2> bperp(-b.y, b.x);
    auto aa = dot(aperp, c);
    auto bb = dot(bperp, c);
    if (f < 0) {
        if (aa > 0 || bb > 0 || aa < f || bb < f) return false;
    } else {
        if (aa < 0 || bb < 0 || aa > f || bb > f) return false;
    }
    if (out) {
        auto lerp = 1.0f - (aa / f);
        *out = ((l2b - l2a) * lerp) + l2a;
    }
    return true;
}

// Vector from a point to the closest point on a box.
template<typename T, int N>
const vec<T, N> point_to_box(const vec<T, N> &p, const vec<T, N> &bmin, const vec<T, N> &bmax) {
    return std::max(bmin - p, std::max(vec<T, N>((T)0), p - bmax));
}

template<typename T, int N>
bool boxes_intersect(const vec<T, N> &b1min, const vec<T, N> &b1max,
    const vec<T, N> &b2min, const vec<T, N> &b2max) {
    return b1min <= b2max && b2min <= b1max;
}

// Return the enter and exit t value.
inline float2 ray_bb_intersect(const float3 &bbmin, const float3 &bbmax,
                               const float3 &rayo, const float3 &reciprocal_raydir) {
    auto v1 = (bbmin - rayo) * reciprocal_raydir;
    auto v2 = (bbmax - rayo) * reciprocal_raydir;
    auto tmin = std::min(v1.x, v2.x);
    auto tmax = std::max(v1.x, v2.x);
    tmin = std::max(tmin, std::min(std::min(v1.y, v2.y), tmax));
    tmax = std::min(tmax, std::max(std::max(v1.y, v2.y), tmin));
    tmin = std::max(tmin, std::min(std::min(v1.z, v2.z), tmax));
    tmax = std::min(tmax, std::max(std::max(v1.z, v2.z), tmin));
    return float2(tmin, tmax);
}

// Call this on the result of ray_bb_intersect() if it isn't known whether there is an intersection.
inline bool does_intersect(const float2 &minmax) {
    return minmax.y >= std::max(minmax.x, 0.0f);
}

// Moves start of ray to first intersection point if one exists, otherwise leaves it in place.
// returns false for no intersection.
inline bool clamp_bb(const float3 &bbmin, const float3 &bbmax, const float3 &rayo,
                     const float3 &raydir, const float3 &reciprocal_raydir, float3 &dest) {
    auto minmax = ray_bb_intersect(bbmin, bbmax, rayo, reciprocal_raydir);
    auto ok = does_intersect(minmax);
    dest = rayo;
    if (ok && minmax.x >= 0) dest += raydir * minmax.x;
    return ok;
}

struct sphere {
    float3 center;
    float rad;
};

inline sphere bounding_sphere(sphere a, sphere b) {
    float dist = length(a.center - b.center);
    // <= accounts for equal rad & pos.
    if (a.rad + dist <= b.rad) {
        return b;
    } else if (b.rad + dist <= a.rad) {
        return a;
    } else {
        assert(dist);
        auto rad = (a.rad + b.rad + dist) / 2;
        auto center = a.center + (b.center - a.center) * ((rad - a.rad) / dist);
        return sphere { center, rad };
    }
}

inline void normalize_mesh(gsl::span<int> idxs, void *verts, size_t vertlen, size_t vsize,
                           size_t normaloffset, bool ignore_bad_tris = true) {
    for (size_t i = 0; i < vertlen; i++) {
        *(float3 *)((uint8_t *)verts + i * vsize + normaloffset) = float3_0;
    }
    for (size_t t = 0; t < idxs.size(); t += 3) {
        int v1i = idxs[t + 0];
        int v2i = idxs[t + 1];
        int v3i = idxs[t + 2];
        float3 &v1p = *(float3 *)((uint8_t *)verts + v1i * vsize);
        float3 &v2p = *(float3 *)((uint8_t *)verts + v2i * vsize);
        float3 &v3p = *(float3 *)((uint8_t *)verts + v3i * vsize);
        float3 &v1n = *(float3 *)((uint8_t *)verts + v1i * vsize + normaloffset);
        float3 &v2n = *(float3 *)((uint8_t *)verts + v2i * vsize + normaloffset);
        float3 &v3n = *(float3 *)((uint8_t *)verts + v3i * vsize + normaloffset);
        if (v1p != v2p && v1p != v3p && v2p != v3p) {
            float3 v12 = normalize(v2p - v1p);
            float3 v13 = normalize(v3p - v1p);
            float3 v23 = normalize(v3p - v2p);
            float3 d3  = normalize(cross(v13, v12));
            v1n += d3 * (1 - dot( v12, v13));
            v2n += d3 * (1 - dot(-v12, v23));
            v3n += d3 * (1 - dot(-v23,-v13));
        } else {
            assert(ignore_bad_tris);
            (void)ignore_bad_tris;
        }
    }
    for (size_t i = 0; i < vertlen; i++) {
        float3 &norm = *(float3 *)((uint8_t *)verts + i * vsize + normaloffset);
        if (norm != float3_0)
            norm = normalize(norm);
    }
}

}  // namespace geom

#endif  // LOBSTER_GEOM
