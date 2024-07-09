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

