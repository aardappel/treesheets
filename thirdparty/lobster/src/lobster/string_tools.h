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

// string & string_view helpers.

// Use this instead of string_view where null-termination is statically known.
struct string_view_nt {
    string_view sv;

    string_view_nt(const string &s) : sv(s) {}
    explicit string_view_nt(const char *s) : sv(s) {}
    explicit string_view_nt(string_view osv) : sv(osv) {
        check_null_terminated();
    }

    void check_null_terminated() {
        assert(!sv.data()[sv.size()]);
    }

    size_t size() const { return sv.size(); }
    const char *data() const { return sv.data(); }

    const char *c_str() {
        check_null_terminated();  // Catch appends to parent buffer since construction.
        return sv.data();
    }
};

inline string operator+(string_view a, string_view b) {
    string r;
    r.reserve(a.size() + b.size());
    r += a;
    r += b;
    return r;
}

inline auto to_string_conv(string_view sv) {
    return[sv]() { return sv; };
}

inline auto to_string_conv(string_view_nt svnt) {
    return [svnt]() { return svnt.sv; };
}

inline auto to_string_conv(const string &s) {
    return[&s]() { return string_view(s); };
}

inline auto to_string_conv(const char *cs) {
    return [sv = string_view(cs)]() { return sv; };  // Caches strlen!
}

template<int I> auto to_string_conv(const char cs[I]) {
    return[sv = string_view(cs, I)]() { return sv; };  // Static strlen!
}

template<typename T> auto to_string_conv(const T *p) {
    return[s = to_string((size_t)p)]() { return string_view(s); };  // Caches to_string!
}

template<typename T> auto to_string_conv(T i) {
    static_assert(is_scalar<T>::value, "");
    return [s = to_string(i)]() { return string_view(s); };  // Caches to_string!
}

inline size_t size_helper(const char *, const char *, const char *suffix) {
    return suffix ? strlen(suffix) : 0;
}
template<typename T, typename ...Ts>
size_t size_helper(const char *prefix, const char *infix, const char *suffix,
                   const T &t, const Ts &... args) {
    return (prefix ? strlen(prefix) : 0) + t().size() + size_helper(infix, infix, suffix, args...);
}

inline void cat_helper(string &s, const char *, const char *, const char *suffix) {
    if (suffix) s += suffix;
}
template<typename T, typename ...Ts>
void cat_helper(string &s, const char *prefix, const char *infix, const char *suffix,
                const T &t, const Ts&... args) {
    if (prefix) s += prefix;
    s += t();
    cat_helper(s, infix, infix, suffix, args...);
}

template<typename ...Ts>
void cat_convs(string &s, const char *prefix, const char *infix, const char *suffix,
               const Ts &... args) {
    auto len = (prefix ? strlen(prefix) : 0) + size_helper(nullptr, infix, suffix, args...);
    s.reserve(len);  // Max 1 alloc ever.
    if (prefix) s += prefix;
    cat_helper(s, nullptr, infix, suffix, args...);
    assert(s.size() == len);
}

template<typename ...Ts>
string cat(const Ts&... args) {
    string s;
    cat_convs(s, nullptr, nullptr, nullptr, to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
string cat_spaced(const Ts &... args) {
    string s;
    cat_convs(s, nullptr, " ", nullptr, to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
string cat_parens(const Ts &... args) {
    string s;
    cat_convs(s, "(", ", ", ")", to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
string cat_quoted(const Ts &... args) {
    string s;
    cat_convs(s, "\"", "\" \"", "\"", to_string_conv(args)...);
    return s;
}

template<typename ...Ts>
void append(string &sd, const Ts &... args) {
    cat_helper(sd, nullptr, nullptr, nullptr, to_string_conv(args)...);
}

inline string Q(string_view s) { return cat("`", s, "`"); }

// This method is in C++20, but quite essential.
inline bool starts_with(string_view sv, string_view start) {
    return start.size() <= sv.size() && sv.substr(0, start.size()) == start;
}



template<typename T> T parse_int(string_view sv, int base = 10, const char **end = nullptr) {
    T val = 0;
    auto res = from_chars(sv.data(), sv.data() + sv.size(), val, base);
    if (end) *end = res.ptr;
    return val;
}

template<typename T> T parse_float(string_view sv, const char **end = nullptr) {
    // FIXME: Upgrade compilers for these platforms on CI.
    #if defined(__APPLE__) || defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(__FreeBSD__)
        auto &term = *(char *)(sv.data() + sv.size());
        auto orig = term;
        term = 0;
        auto v = (T)strtod(sv.data(), (char **)end);
        term = orig;
        return v;
    #else
        T val = 0;
        auto res = from_chars(sv.data(), sv.data() + sv.size(), val);
        if (end) *end = res.ptr;
        return val;
    #endif
}

// Special case for to_string to get exact float formatting we need.
template<typename T> string to_string_float(T x, int decimals = -1) {
    // FIXME: make this work for other compilers.
    // Looks like gcc/clang/xcode are behind on msvc on this one for once. May need to upgrade to
    // bleeding edge versions on CI somehow.
    #if _MSC_VER >= 1923
        string s;
        for (;;) {
            // Typical capacity of an empty string is 15 or 22 on 64-bit compilers since they
            // all use "small string optimization" nowadays. That would fit most floats, needing no
            // allocation in the base case.
            // Even on 32-bit, it would likely be 7 or 10 which fits some floats.
            s.resize(s.capacity());  // Sadly need this.
            auto res = decimals >= 0
                ? to_chars(s.data(), s.data() + s.size(), x, std::chars_format::fixed, decimals)
                : to_chars(s.data(), s.data() + s.size(), x, std::chars_format::fixed);
            if (res.ec == errc()) {
                s.resize(res.ptr - s.data());
                // We like floats to be recognizable as floats, not integers.
                auto no_dot = s.find_last_of('.') == string::npos;
                auto is_finite = isfinite(x);
                if (no_dot && is_finite) s += ".0";
                return s;
            }
            // It didn't fit, we're going to have to allocate.
            // Simply double the capacity until it works.
            // We choose 15 as minimum bound, since together with the null-terminator, it is most
            // likely to fit in a memory allocator bucket.
             s.reserve(max(s.capacity(), (size_t)15) * 2);
        }
    #else
        // ostringstream gives more consistent cross-platform results than to_string() for floats,
        // and can at least be configured to turn scientific notation off.
        ostringstream ss;
        // Suppress scientific notation.
        ss << std::fixed;
        // There's no way to tell it to just output however many decimals are actually significant,
        // sigh. Once you turn on fixed, it will default to 5 or 6, depending on platform, for both
        // float and double. So we set our own more useful defaults:
        int default_precision = sizeof(T) == sizeof(float) ? 6 : 12;
        ss << std::setprecision(decimals <= 0 ? default_precision : decimals);
        ss << x;
        auto s = ss.str();
        if (decimals <= 0) {
            // First trim whatever lies beyond the precision to avoid garbage digits.
            size_t max_significant = default_precision;
            max_significant += 2;  // "0."
            if (s[0] == '-') max_significant++;
            while (s.length() > max_significant && s.back() != '.') s.pop_back();
            // Now strip unnecessary trailing zeroes.
            while (s.back() == '0') s.pop_back();
            // If there were only zeroes, keep at least 1.
            if (s.back() == '.') s.push_back('0');
        }
        return s;
    #endif
}

inline void to_string_hex(string &sd, size_t x) {
    // FIXME: replace with to_chars.
    ostringstream ss;
    ss << "0x" << std::hex << x << std::dec;
    sd += ss.str();
}
