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
