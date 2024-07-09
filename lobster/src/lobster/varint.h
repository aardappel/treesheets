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

// Varints optimized for speed, typical cases with no loops.
// Bottom 2 bits indicate if this is encoded as 6, 14, 22, or byte sized.
// This assumes values are heavily skewed towards smaller values.
// Use "zig-zag" encoding for signed numbers.
// This should be equal to LEB in the 1 byte case, significantly faster in
// the 2 or 3 byte case, and similar otherwise.

// TODO: replace push_back with something more efficient that only checks
// for end of buffer once.
inline void EncodeVarintU(uint64_t z, vector<uint8_t> &v) {
    if (z < 0x40) {
        v.push_back(uint8_t(z << 2));
    } else if (z < 0x4000) {
        v.push_back(uint8_t(z << 2 | 1));
        v.push_back(uint8_t(z >> 6));
    } else if (z < 0x400000) {
        v.push_back(uint8_t(z << 2 | 2));
        v.push_back(uint8_t(z >> 6));
        v.push_back(uint8_t(z >> 14));
    } else {
        // Use remaining bits in starter byte to store bytes needed.
        auto start = v.size();
        v.push_back(3);
        while (z) {
            v.push_back(uint8_t(z));
            z >>= 8;
            v[start] += 1 << 2;
        }
    }
}

inline void EncodeVarintS(int64_t n, vector<uint8_t> &v) {
    uint64_t z = (uint64_t(n) << 1) ^ (n >> 63);
    EncodeVarintU(z, v);
}

inline void EncodeVarintZero(vector<uint8_t> &v) {  // For readability.
    v.push_back(0);
}

// Returns zero upon reaching the end prematurely, which is not great.
// Then again, this is meant for internal use cases where we know the
// sender, and some speed/simplicity/robustness is more useful here than
// error checking for something that is unlikely.
inline uint64_t DecodeVarintU(const uint8_t *&p, const uint8_t *end) {
    if (p == end) return 0;
    uint8_t f = *p++;
    uint8_t l = f & 0x03;
    f >>= 2;
    if (!l) {
        return f;
    } else if (l == 1) {
        if (p == end) return 0;
        return f | (uint64_t(*p++) << 6);
    } else if (l == 2) {
        if (p + 1 >= end) return 0;
        auto t = uint64_t(*p++) << 6;
        return f | t | (uint64_t(*p++) << 14);
    } else {
        uint64_t zm = 0;
        int shift = 0;
        // f contains number of bytes that follow.
        while (f--) {
            if (p == end) return 0;
            zm |= uint64_t(*p++) << shift;
            shift += 8;
        }
        return zm;
    }
}

inline int64_t DecodeVarintS(const uint8_t *&p, const uint8_t *end) {
    auto z = DecodeVarintU(p, end);
    return z & 1 ? (z >> 1) ^ -1 : z >> 1;
}

inline void unit_test_varint() {
    vector<uint8_t> buf;
    for (size_t b = 0; b < 63; b++) {
        auto un = uint64_t(1) << b;
        buf.clear();
        EncodeVarintU(un, buf);
        const uint8_t *p2 = buf.data();
        auto und = DecodeVarintU(p2, p2 + buf.size());
        assert(buf.data() + buf.size() == p2 && un == und);
        (void)und;
        auto sn = int64_t(un);
        buf.clear();
        EncodeVarintS(sn, buf);
        p2 = buf.data();
        auto snd = DecodeVarintS(p2, p2 + buf.size());
        assert(buf.data() + buf.size() == p2 && sn == snd);
        sn = -sn;
        buf.clear();
        EncodeVarintS(sn, buf);
        p2 = buf.data();
        snd = DecodeVarintS(p2, p2 + buf.size());
        assert(buf.data() + buf.size() == p2 && sn == snd);
        (void)snd;
    }
}
