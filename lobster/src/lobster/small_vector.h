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

// small_vector, similar to llvm::SmallVector, stores N elements in-line, only dynamically
// allocated if more than that.
// Uses memcpy on growth, so not for elements with non-trivial copy constructors.
// It stores a pointer overlapping with the fixed elements, so there is no point to
// make N smaller than sizeof(T *) / sizeof(T).

template<typename T, int N> class small_vector {
    // These could be 16-bit, but there's no easy portable way to stop the T* below from
    // padding the struct in that case.
    uint32_t len = 0;
    uint32_t cap = N;

    union {
        T elems[N];
        T *buf;
    };

    void grow(uint32_t nc) {
        auto b = new T[nc];
        t_memcpy(b, data(), len);
        if (cap > N) delete[] buf;
        cap = nc;
        buf = b;
    }

    public:
    small_vector() {}

    small_vector(size_t nelems, T init) {
        if ((uint32_t)nelems > cap) grow((uint32_t)nelems);
        len = (uint32_t)nelems;
        auto d = data();
        for (uint32_t i = 0; i < len; i++) {
            d[i] = init;
        }
    }

    small_vector(const small_vector<T, N> &o) {
        len = o.len;
        cap = o.cap;
        if (o.cap == N) {
            for (uint32_t i = 0; i < len; i++) {
                elems[i] = o.elems[i];
            }
        } else {
            buf = new T[len];
            t_memcpy(buf, o.buf, len);
        }
    }

    small_vector(small_vector<T, N> &&o) {
        len = o.len;
        cap = o.cap;
        buf = o.buf;
        o.buf = nullptr;
        o.len = 0;
        o.cap = N;
    }

    small_vector(const vector<T> &o) {
        if ((uint32_t)o.size() > cap) grow((uint32_t)o.size());
        for (auto e : o) {
            push_back(e);
        }
    }

    ~small_vector() {
        if (cap > N) delete[] buf;
    }

    small_vector &operator=(const small_vector<T, N> &o) {
        len = 0;
        if (o.len > cap) grow(o.len);
        for (auto e : o) {
            push_back(e);
        }
        return *this;
    }

    small_vector &operator=(const vector<T> &o) {
        len = 0;
        if ((uint32_t)o.size() > cap) grow((uint32_t)o.size());
        for (auto e : o) {
            push_back(e);
        }
        return *this;
    }

    size_t size() const {
        return len;
    }

    T *data() {
        return cap == N ? elems : buf;
    }

    const T *data() const {
        return cap == N ? elems : buf;
    }

    bool empty() {
        return len == 0;
    }

    void clear() {
        len = 0;
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
        len--;
    }

    void push_back(const T &e) {
        if (len == cap) grow(len * 2);
        data()[len++] = e;
    }

    void insert(size_t at, const T &e) {
        assert(at <= len);
        if (len == cap) grow(len * 2);
        if (at != len) t_memmove(data() + at + 1, data() + at, size() - at);
        data()[at] = e;
        len++;
    }

    void append(const T *from, size_t n) {
        if (len + n > cap) grow(len + (uint32_t)n);
        t_memcpy(data() + len, from, n);
        len += (uint32_t)n;
    }

    void erase(size_t at) {
        assert(at < len);
        if (at != len - 1) t_memmove(data() + at, data() + at + 1, size() - at - 1);
        len--;
    }

    // For use with map.
    bool operator<(const small_vector<T, N> &o) const {
        const auto a = data();
        const auto b = o.data();
        for (uint32_t i = 0; i < len; i++) {
            if (i >= o.len) return false;
            if (a[i] < b[i]) return true;
            if (a[i] > b[i]) return false;
        }
        return len < o.len;
    }
};

template<typename T, int N>
ssize_t ssize(const small_vector<T, N> &v) {
    return (ssize_t)v.size();
}

template<typename T, int N>
void reset_from_small_vector(vector<T> &d, const small_vector<T, N> &v) {
    d.clear();
    for (auto e : v) d.push_back(e);
}