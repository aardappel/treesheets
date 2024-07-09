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

// packed_vector stores multiple variable sized items in contiguous memory
// while still allowing O(1) indexing, by keeping offsets to all items.
// 
// This is intended to allow it to be used in similar cases as std::vector<std::string>>
// with reduced memory usage and reduced allocations, with slower inserts/deletes
// as the main downside.
// 
// Besides strings, varints and variable sized tagged unions are fun uses cases.
// You can specify offset types on a per use case basis for maximum compactness.

// The type T must be the "view" version of a variable size container,
// e.g. string_view, since the actual storage is managed by this vector,
// not the element type T.
// The Offset type must be able to hold offsets into the total amount of memory
// managed and also element indices, you are encouraged to use smaller types
// here whenever possible.

// packed_vector was designed to work with string_view, but can work with
// any other type if it has a data() that returns a char *, a size() that returns a byte size,
// and a constructor that takes a byte start and size.

// Memory layout of the buffer:
// - Offset offsets[elen]
// - Offset total         // Allows computing size by offsets[i + 1] - offsets[i]
// - Offset unused[..]    // "ecap" points at the end of this.
// - char elements[..]    // "blen" points at the end of this.
// - char unused[..]      // "bcap" points at the end of this.
// (we use char instead of uint8_t/int8_t to be trivially compatible with string_view).

template<typename T = string_view, typename Offset = uint32_t>
class packed_vector {
    char *buf = nullptr;
    Offset elen = 0;
    Offset ecap = 1;
    Offset blen = (Offset)sizeof(Offset);  // Space for the "total" offset.
    Offset bcap = (Offset)sizeof(Offset);

    public:
    packed_vector() {
    }

    ~packed_vector() {
        if (buf) delete[] buf;
    }

    size_t size() {
        return elen;
    }

    bool empty() {
        return !elen;
    }

    void reserve(size_t min) {
        if (!buf) {
            // If this first call is from push_back we have to make ecap == 2,
            // so if this was called for whatever reason directly after construction,
            // we need minimally allow 2 elements.. we could assert, but better to
            // not require the user to understand internals :)
            min = std::max(min, sizeof(Offset) * 2);
            buf = new char[min];
            Offset zero = 0;
            memcpy(buf, &zero, sizeof(Offset));  // Total.
            ecap = 2;
            blen = (Offset)(ecap * sizeof(Offset));
            bcap = (Offset)min;
            return;
        }
        // Now we're expanding an existing buffer.
        if (min < bcap) {
            return;
        }
        // Our resizing strategy is very conservative (1.5x), given that the point of this
        // container is to use less memory.
        // If the user wants to avoid many reallocations they should pre-reserve.
        size_t ncap = std::max((size_t)(blen + blen / 2), min);
        if ((Offset)ncap != ncap) {
            // This realloc is going to overflow the chosen Offset type.
            assert(false);
            // If you realloc std::vector such that it runs out of memory
            // it will throw bad_alloc, so appropriate we do the same, if
            // asserts are off. Allowing it to be ignored and would
            // probably be worse.
            #ifdef USE_EXCEPTION_HANDLING
                throw std::bad_alloc();
            #else
                abort();
            #endif
        }
        auto nbuf = new char[ncap];
        // Now have to figure out of all the extra space how much to give
        // to the data portion of the buffer, and how much to the offsets.
        // If this was a minimum realloc just enough to place the new element,
        // we only get 1 extra offset and the rest goes to the data.
        size_t extra_offsets = 1;
        if (ncap > min) {
            // Otherwise.. we have to apply a heuristic according to their
            // current ratios.
            auto offset_space = (elen + 1 + extra_offsets) * sizeof(Offset);
            auto extra_data = min - blen - sizeof(Offset);
            auto data_space = blen - ecap * sizeof(Offset) + extra_data;
            auto total_space = offset_space + data_space;
            assert(total_space <= ncap);
            auto surplus = ncap - total_space;
            if (surplus) {
                auto ratio = (double)offset_space / (double) total_space;
                auto more_offsets = (size_t)(surplus * ratio) / sizeof(Offset);
                extra_offsets += more_offsets;
            }
        }
        memcpy(nbuf, buf, (elen + 1) * sizeof(Offset));
        auto necap = std::max((size_t)ecap, elen + 1 + extra_offsets);
        memcpy(nbuf + necap * sizeof(Offset),
               buf + ecap * sizeof(Offset),
               blen - ecap * sizeof(Offset));
        delete[] buf;
        buf = nbuf;
        auto shift = (necap - ecap) * sizeof(Offset);
        ecap = (Offset)necap;
        bcap = (Offset)ncap;
        blen += (Offset)shift;
        for (size_t i = 0; i <= elen; i++) {
            Offset off;
            memcpy(&off, buf + i * sizeof(Offset), sizeof(Offset));
            off += (Offset)shift;
            memcpy(buf + i * sizeof(Offset), &off, sizeof(Offset));
        }
    }

    void push_back(const T &e) {
        auto esz = e.size();
        if (blen + esz > bcap || elen + 1 == ecap) {
            reserve(blen + esz + sizeof(Offset));
        }
        memcpy(buf + blen, e.data(), esz);
        memcpy(buf + elen * sizeof(Offset), &blen, sizeof(Offset));
        elen++;
        blen += (Offset)esz;
        memcpy(buf + elen * sizeof(Offset), &blen, sizeof(Offset));
    }

    T operator[](size_t i) {
        assert(i < elen);
        Offset off1, off2;
        memcpy(&off1, buf + i * sizeof(Offset), sizeof(Offset));
        memcpy(&off2, buf + (i + 1) * sizeof(Offset), sizeof(Offset));
        return T(buf + off1, (size_t)(off2 - off1));
    }

    string dump() {
        string s = "[";
        for (size_t i = 0; i < elen; i++) {
            if (i) s += ", ";
            auto sv = (*this)[i];
            s += "\"";
            for (size_t j = 0; j < sv.size(); j++) {
                if (sv[j] >= ' ' && sv[j] <= '~')
                    s += sv[j];
                else
                    s += cat("\\", sv[j]);
            }
            s += "\"";
        }
        s += "]";
        return s;
    }
};



inline void unit_test_packed_vector() {
    packed_vector pvsv;
    pvsv.push_back("hello");
    pvsv.push_back("world");
    assert(pvsv[1] == "world");
    auto test = "abcdefghijklmnopqrstuvwxyz";
    for (size_t i = 0; i <= 26; i++) {
        pvsv.push_back(string_view(test, i));
        auto r = pvsv[2 + i];
        assert(r == string_view(test, i));
        (void)r;
    }
}
