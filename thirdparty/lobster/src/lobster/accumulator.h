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
