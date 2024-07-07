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

// -------------------------------------------------------------------

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


