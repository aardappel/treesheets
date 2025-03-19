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

// stack_vector allows dynamically growing vectors of any size with (amortized) no
// dynamic allocation at all, with the limitation that only one of these can be
// growing at a time. Thus, used as local variables that may be used nested.
// Cost of individual ops may be slightly higher, so best used for small temp vectors
// where avoiding (re)allocation is key.
// Uses a single backing store, so don't use outside of main thread.
// Much like std::vector, adding and removing elements may invalidate existing
// pointer and iterators.

class stack_vector_storage {
    vector<uint8_t> storage;

    public:
    size_t size() {
        return storage.size();
    }

    uint8_t *data() {
        return storage.data();
    }

    void grow(size_t ns) {
        storage.resize(storage.size() + ns);
    }

    void shrink(size_t ns) {
        assert(ns <= storage.size());
        storage.resize(storage.size() - ns);
    }
};

// Cheap way to not have to deal with mixing alignment: a storage buffer for each kind of
// alignment, and a constexpr way to pick the right one.
extern stack_vector_storage g_svs1;
extern stack_vector_storage g_svs2;
extern stack_vector_storage g_svs4;
extern stack_vector_storage g_svs8;

constexpr stack_vector_storage &get_stack_vector_storage(size_t align) {
    switch (align) {
        case 1:
            return g_svs1;
        case 2:
            return g_svs2;
        case 4:
            return g_svs4;
        case 8:
            return g_svs8;
        default:
            static_assert("no stack_vector_storage alignment specialization");
            return g_svs8;
    }
}

template<typename T> class stack_vector {
    size_t didx = 0;  // Index into storage.
    size_t len = 0;   // In T elements.

    constexpr stack_vector_storage &svs() {
        return get_stack_vector_storage(alignof(T));
    }

    void check_we_are_on_top() {
        // If this assert hits, you're trying to add/remove elements to a stack_vector which is not
        // currently at the top of the stack storage.
        assert(svs().size() == didx + len * sizeof(T));
    }

    void grow(size_t nc) {
        check_we_are_on_top();
        svs().grow(nc * sizeof(T));
        len += nc;
    }

    void shrink(size_t rc) {
        check_we_are_on_top();
        svs().shrink(rc * sizeof(T));
        len -= rc;
    }

    public:
    stack_vector() : didx(svs().size()) {}

    stack_vector(size_t nelems, T init) : didx(svs().size()) {
        grow(nelems);
        auto d = data();
        for (size_t i = 0; i < len; i++) {
            d[i] = init;
        }
    }

    stack_vector(const stack_vector<T> &o) : didx(svs().size()) {
        grow(o.len);
        t_memcpy(data(), o.data(), len);
    }

    stack_vector(stack_vector<T> &&o) {
        // There's really no scenario where move construction can work.
        assert(false);
    }

    stack_vector(const vector<T> &o) {
        grow(o.size());
        t_memcpy(data(), o.data(), len);
    }

    ~stack_vector() {
        clear();
    }

    stack_vector &operator=(const stack_vector<T> &o) {
        clear();
        grow(o.size());
        t_memcpy(data(), o.data(), len);
        return *this;
    }

    stack_vector &operator=(const vector<T> &o) {
        clear();
        grow(o.size());
        t_memcpy(data(), o.data(), len);
        return *this;
    }

    size_t size() const {
        return len;
    }

    T *data() {
        return (T *)(svs().data() + didx);
    }

    const T *data() const {
        return (T *)(svs().data() + didx);
    }

    bool empty() {
        return len == 0;
    }

    void clear() {
        shrink(len);
    }

    T &operator[](size_t i) {
        assert(i < len);
        return data()[i];
    }
    const T &operator[](size_t i) const {
        assert(i < len);
        return data()[i];
    }

    T *begin() {
        return data();
    }
    T *end() {
        return data() + len;
    }

    const T *begin() const {
        return data();
    }
    const T *end() const {
        return data() + len;
    }

    const T *cbegin() const {
        return data();
    }
    const T *cend() const {
        return data() + len;
    }

    T &back() {
        assert(len);
        return data()[len - 1];
    }
    const T &back() const {
        assert(len);
        return data()[len - 1];
    }

    void pop_back() {
        assert(len);
        shrink(1);
    }

    void push_back(const T &e) {
        grow(1);
        data()[len - 1] = e;
    }

    void insert(size_t at, const T &e) {
        assert(at <= len);
        grow(1);
        auto d = data();
        if (at != len - 1) t_memmove(d + at + 1, d + at, size() - at);
        d[at] = e;
    }

    void append(const T *from, size_t n) {
        grow(n);
        t_memcpy(data() + len - n, from, n);
    }

    void erase(size_t at) {
        assert(at < len);
        if (at != len - 1) t_memmove(data() + at, data() + at + 1, size() - at - 1);
        shrink(1);
    }
};

template<typename T, int N>
ssize_t ssize(const stack_vector<T> &v) {
    return v.size();
}

template<typename T, int N>
void reset_from_stack_vector(vector<T> &d, const stack_vector<T> &v) {
    d.clear();
    for (auto e : v) d.push_back(e);
}

inline void unit_test_stack_vector() {
    stack_vector<int> sv1;
    sv1.push_back(1);
    // Once we start using sv2, sv1 can't change size.
    stack_vector<int> sv2;
    sv2.push_back(2);
    // We can use this one in parallel though, since its a different size.
    stack_vector<double> sv3;
    sv3.push_back(3.0);
    sv2.push_back(4);
    // This would assert.
    //sv1.push_back(5);
}
