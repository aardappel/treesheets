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

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#include "lobster/wfc.h"

namespace lobster {

static RandomNumberGenerator<MersenneTwister> rndm;
static RandomNumberGenerator<Xoshiro256SS> rndx;

static int IntCompare(const Value &a, const Value &b) {
    return a.ival() < b.ival() ? -1 : a.ival() > b.ival();
}

static int FloatCompare(const Value &a, const Value &b) {
    return a.fval() < b.fval() ? -1 : a.fval() > b.fval();
}

static int StringCompare(const Value &a, const Value &b) {
    auto _a = a.sval()->strv();
    auto _b = b.sval()->strv();
    return (_a > _b) - (_b > _a);
}

static int ObjectCompare(const Value &a, const Value &b) {
    return a.any() < b.any() ? -1 : a.any() > b.any();
}

template<typename T> Value BinarySearch(StackPtr &sp, Value &l, Value &key, T comparefun) {
    iint size = l.vval()->len;
    iint i = 0;
    for (;;) {
        if (!size) break;
        iint mid = size / 2;
        iint comp = comparefun(key, l.vval()->At(i + mid));
        if (comp) {
            if (comp < 0) size = mid;
            else { mid++; i += mid; size -= mid; }
        } else {
            i += mid;
            size = 1;
            while (i && !comparefun(key, l.vval()->At(i - 1))) { i--; size++; }
            while (i + size < l.vval()->len && !comparefun(key, l.vval()->At(i + size))) {
                size++;
            }
            break;
        }
    }
    Push(sp, Value(size));
    return Value(i);
}

void AddBuiltins(NativeRegistry &nfr) {

nfr("print", "x", "Ss", "",
    "output any value to the console (with linefeed).",
    [](StackPtr &, VM &vm, Value &a) {
        vm.s_reuse.clear();
        RefToString(vm, vm.s_reuse, a.refnil(), vm.programprintprefs);
        LOG_PROGRAM(vm.s_reuse);
        return NilVal();
    });

// This is now the identity function, but still useful to force a coercion.
nfr("string", "x", "Ssk", "S",
    "convert any value to string",
    [](StackPtr &, VM &, Value &a) {
        return a;
    });

nfr("set_print_depth", "depth", "I", "I",
    "for printing / string conversion: sets max vectors/objects recursion depth (default 10), "
    "returns old value",
    [](StackPtr &, VM &vm, Value &a) {
        auto old = vm.programprintprefs.depth;
        vm.programprintprefs.depth = a.ival();
        return Value(old);
    });

nfr("set_print_length", "len", "I", "I",
    "for printing / string conversion: sets max string length (default 100000), "
    "returns old value",
    [](StackPtr &, VM &vm, Value &a) {
        auto old = vm.programprintprefs.budget;
        vm.programprintprefs.budget = a.ival();
        return Value(old);
    });

nfr("set_print_quoted", "quoted", "B", "B",
    "for printing / string conversion: if the top level value is a string, whether to convert"
    " it with escape codes and quotes (default false), returns old value",
    [](StackPtr &, VM &vm, Value &a) {
        auto old = vm.programprintprefs.quoted;
        vm.programprintprefs.quoted = a.ival() != 0;
        return Value(old);
    });

nfr("set_print_decimals", "decimals", "I", "I",
    "for printing / string conversion: number of decimals for any floating point output"
    " (default -1, meaning all), returns old value",
    [](StackPtr &, VM &vm, Value &a) {
        auto old = vm.programprintprefs.decimals;
        vm.programprintprefs.decimals = a.ival();
        return Value(old);
    });

nfr("set_print_indent", "spaces", "I", "I",
    "for printing / string conversion: number of spaces to indent with. default is 0:"
    " no indent / no multi-line, returns old value",
    [](StackPtr &, VM &vm, Value &a) {
        auto old = vm.programprintprefs.indent;
        vm.programprintprefs.indent = a.intval();
        return Value(old);
    });

nfr("get_line", "prefix", "S", "S",
    "reads a string from the console if possible (followed by enter). Prefix will be"
    " printed before the input",
    [](StackPtr &, VM &vm, Value &prefix) {
        fputs(prefix.sval()->data(), stdout);
        const int MAXSIZE = 1000;
        char buf[MAXSIZE];
        if (!fgets(buf, MAXSIZE, stdin)) buf[0] = 0;
        buf[MAXSIZE - 1] = 0;
        for (int i = 0; i < MAXSIZE; i++) if (buf[i] == '\n') { buf[i] = 0; break; }
        return Value(vm.NewString(buf));
    });

nfr("append", "xs,ys", "A]*cA]*u1c", "A]1",
    "creates a new vector by appending all elements of 2 input vectors",
    [](StackPtr &, VM &vm, Value &v1, Value &v2) {
        auto type = v1.vval()->tti;
        auto nv = (LVector *)vm.NewVec(0, v1.vval()->len + v2.vval()->len, type);
        nv->Append(vm, v1.vval(), 0, v1.vval()->len);
        nv->Append(vm, v2.vval(), 0, v2.vval()->len);
        return Value(nv);
    });

nfr("append_into", "dest,src", "A]*A]1c", "Ab]1",
    "appends all elements of the second vector into the first",
    [](StackPtr &, VM &vm, Value &v1, Value &v2) {
        v1.vval()->Append(vm, v2.vval(), 0, v2.vval()->len);
        return v1;
    });

nfr("vector_capacity", "xs,len", "A]*I", "Ab]1",
    "ensures the vector capacity (number of elements it can contain before re-allocating)"
    " is at least \"len\". Does not actually add (or remove) elements. This function is"
    " just for efficiency in the case the amount of \"push\" operations is known."
    " returns original vector.",
    [](StackPtr &, VM &vm, Value &vec, Value &len) {
        vec.vval()->MinCapacity(vm, len.ival());
        return vec;
    });

nfr("length", "x", "I", "I",
    "length of int (identity function, useful in combination with string/vector version)",
    [](StackPtr &, VM &, Value &a) {
        return a;
    });

nfr("length", "s", "S", "I",
    "length of string",
    [](StackPtr &, VM &, Value &a) {
        auto len = a.sval()->len;
        return Value(len);
    });

nfr("length", "xs", "A]*", "I",
    "length of vector",
    [](StackPtr &, VM &, Value &a) {
        auto len = a.vval()->len;
        return Value(len);
    });

nfr("equal", "a,b", "AA", "B",
    "structural equality between any two values (recurses into vectors/objects,"
    " unlike == which is only true for vectors/objects if they are the same object)",
    [](StackPtr &, VM &vm, Value &a, Value &b) {
        bool eq = RefEqual(vm, a.refnil(), b.refnil(), true);
        return Value(eq);
    });

nfr("push", "xs,x", "A]*Akw1", "Ab]1",
    "appends one element to a vector, returns existing vector",
    [](StackPtr &sp, VM &vm) {
        auto v = DangleVec<RefObjPtr>(sp);
        auto l = Pop(sp).vval();
        assert(v.len == l->width);
        l->PushVW(vm, v.vals);
        Push(sp, l);
    });

nfr("pop", "xs", "A]*", "A1",
    "removes last element from vector and returns it",
    [](StackPtr &sp, VM &vm) {
        auto l = Pop(sp).vval();
        if (!l->len) vm.BuiltinError("pop: empty vector");
        l->PopVW(TopPtr(sp));
        PushN(sp, (int)l->width);
    });

nfr("top", "xs", "A]*", "Ab1",
    "returns last element from vector",
    [](StackPtr &sp, VM &vm) {
        auto l = Pop(sp).vval();
        if (!l->len) vm.BuiltinError("top: empty vector");
        l->TopVW(TopPtr(sp));
        PushN(sp, (int)l->width);
    });

nfr("insert", "xs,i,x", "A]*IAkw1", "Ab]1",
    "inserts a value into a vector at index i, existing elements shift upward,"
    " returns original vector",
    [](StackPtr &sp, VM &vm) {
        auto v = DangleVec<RefObjPtr>(sp);
        auto i = Pop(sp).ival();
        auto l = Pop(sp).vval();
        if (i < 0 || i > l->len)
            vm.BuiltinError("insert: index or n out of range");  // note: i==len is legal
        assert(v.len == l->width);
        l->Insert(vm, v.vals, i);
        Push(sp, l);
    });

nfr("remove", "xs,i,n", "A]*II?", "A1",
    "remove element(s) at index i, following elements shift down. pass the number of elements"
    " to remove as an optional argument, default 1. returns the first element removed.",
    [](StackPtr &sp, VM &vm) {
        auto n = Pop(sp).ival();
        auto i = Pop(sp).ival();
        auto l = Pop(sp).vval();
        auto amount = std::max(n, 1_L);
        if (n < 0 || amount > l->len || i < 0 || i > l->len - amount)
            vm.BuiltinError(cat("remove: index (", i, ") or n (", amount,
                                    ") out of range (", l->len, ")"));
        l->Remove(sp, vm, i, amount, 1, true);
    });

nfr("remove_obj", "xs,obj", "A]*A1", "Ab2",
    "remove all elements equal to obj (==), returns obj.",
    [](StackPtr &sp, VM &vm, Value &l, Value &o) {
        iint removed = 0;
        auto vt = vm.GetTypeInfo(l.vval()->ti(vm).subt).t;
        for (iint i = 0; i < l.vval()->len; i++) {
            auto e = l.vval()->At(i);
            if (e.Equal(vm, vt, o, vt, false)) {
                l.vval()->Remove(sp, vm, i--, 1, 0, false);
                removed++;
            }
        }
        return o;
    });

nfr("binary_search", "xs,key", "I]I", "II",
    "does a binary search for key in a sorted vector, returns as first return value how many"
    " matches were found, and as second the index in the array where the matches start (so you"
    " can read them, overwrite them, or remove them), or if none found, where the key could be"
    " inserted such that the vector stays sorted. This overload is for int vectors and keys.",
    [](StackPtr &sp, VM &, Value &l, Value &key) {
        auto r = BinarySearch(sp, l, key, IntCompare);
        return r;
    });

nfr("binary_search", "xs,key", "F]F", "II",
    "float version.",
    [](StackPtr &sp, VM &, Value &l, Value &key) {
        auto r = BinarySearch(sp, l, key, FloatCompare);
        return r;
    });

nfr("binary_search", "xs,key", "S]S", "II",
    "string version.",
    [](StackPtr &sp, VM &, Value &l, Value &key) {
        auto r = BinarySearch(sp, l, key, StringCompare);
        return r;
    });

nfr("binary_search_object", "xs,key", "A]*A1", "II",
    "object version. compares by reference rather than contents.",
    [](StackPtr &sp, VM &, Value &l, Value &key) {
        auto r = BinarySearch(sp, l, key, ObjectCompare);
        return r;
    });

nfr("copy", "x", "A", "A1",
    "makes a shallow copy of any object/vector/string.",
    [](StackPtr &, VM &vm, Value &v) {
        return v.CopyRef(vm, false);
    });

nfr("deepcopy", "x", "A", "A1",
    "makes a deep copy of any object/vector/string. DAGs become trees, and cycles will make it run"
    " out of memory.",
    [](StackPtr &, VM &vm, Value &v) {
        return v.CopyRef(vm, true);
    });

nfr("slice", "xs,start,size", "A]*II", "A]1",
    "returns a sub-vector of size elements from index start."
    " size can be negative to indicate the rest of the vector.",
    [](StackPtr &, VM &vm, Value &l, Value &s, Value &e) {
        auto size = e.ival();
        auto start = s.ival();
        if (size < 0) size = l.vval()->len - start;
        if (start < 0 || start + size > l.vval()->len)
            vm.BuiltinError("slice: values out of range");
        auto nv = (LVector *)vm.NewVec(0, size, l.vval()->tti);
        nv->Append(vm, l.vval(), start, size);
        return Value(nv);
    });

nfr("any", "xs", "A]*", "B",
    "returns whether any elements of the vector are true values",
    [](StackPtr &, VM &, Value &v) {
        Value r(false);
        iint l = v.vval()->len;
        for (auto i = 0; i < l; i++) {
            if (v.vval()->At(i).True()) { r = Value(true); break; }
        }
        return r;
    });

nfr("any", "xs", "I}", "B",
    "returns whether any elements of the numeric struct are true values",
    [](StackPtr &sp, VM &) {
        auto r = false;
        auto l = Pop(sp).ival();
        for (iint i = 0; i < l; i++) {
            if (Pop(sp).True()) r = true;
        }
        Push(sp,  r);
    });

nfr("all", "xs", "A]*", "B",
    "returns whether all elements of the vector are true values",
    [](StackPtr &, VM &, Value &v) {
        Value r(true);
        for (iint i = 0; i < v.vval()->len; i++) {
            if (v.vval()->At(i).False()) { r = Value(false); break; }
        }
        return r;
    });

nfr("all", "xs", "I}", "B",
    "returns whether all elements of the numeric struct are true values",
    [](StackPtr &sp, VM &) {
        auto r = true;
        auto l = Pop(sp).ival();
        for (iint i = 0; i < l; i++) {
            if (!Pop(sp).True()) r = false;
        }
        Push(sp,  r);
    });

nfr("substring", "s,start,size", "SII", "S",
    "returns a substring of size characters from index start."
    " size can be negative to indicate the rest of the string.",
    [](StackPtr &, VM &vm, Value &l, Value &s, Value &e) {
        iint size = e.ival();
        iint start = s.ival();
        if (size < 0) size = l.sval()->len - start;
        if (start < 0 || start + size > l.sval()->len)
            vm.BuiltinError("substring: values out of range");

        auto ns = vm.NewString(string_view(l.sval()->data() + start, (size_t)size));
        return Value(ns);
    });

nfr("find_string", "s,substr,offset", "SSI?", "I",
    "finds the index at which substr first appears, or -1 if none."
    " optionally start at a position other than 0",
    [](StackPtr &, VM &, Value &s, Value &sub, Value &offset) {
        return Value((ssize_t)s.sval()->strv().find(sub.sval()->strv(), (size_t)offset.ival()));
    });

nfr("find_string_reverse", "s,substr,offset", "SSI?", "I",
    "finds the index at which substr first appears when searching from the end, or -1 if none."
    " optionally start at a position other than the end of the string",
    [](StackPtr &, VM &, Value &s, Value &sub, Value &offset) {
        auto sv = s.sval()->strv();
        return Value((ssize_t)sv.rfind(sub.sval()->strv(),
                                      offset.ival() ? (size_t)offset.ival() : sv.size()));
    });

nfr("replace_string", "s,a,b,count", "SSSI?", "S",
    "returns a copy of s where all occurrences of a have been replaced with b."
    " if a is empty, no replacements are made."
    " if count is specified, makes at most that many replacements",
    [](StackPtr &, VM &vm, Value &is, Value &ia, Value &ib, Value &count) {
        string s;
        auto sv = is.sval()->strv();
        auto a = ia.sval()->strv();
        auto b = ib.sval()->strv();
        auto c = count.ival() ? count.ival() : numeric_limits<iint>::max();
        if (a.empty()) {
            // We could error here, but more useful to just return the input.
            s = a;
        } else {
            for (size_t i = 0;;) {
                auto j = std::min(sv.find(a, i), sv.size());
                auto prefix = sv.substr(i, j - i);;
                s += prefix;
                i += prefix.size();
                if (j == sv.size()) break;
                s += b;
                i += a.size();
                if (!--c) {
                    s += sv.substr(i);
                    break;
                }
            }
        }
        auto ns = vm.NewString(s);
        return Value(ns);
    });

nfr("string_to_int", "s,base", "SI?", "IB",
    "converts a string to an int given the base (2..36, e.g. 16 for hex, default is 10)."
    "returns 0 if no numeric data could be parsed; second return value is true if all"
    "characters of the string were parsed.",
    [](StackPtr &sp, VM &vm, Value &s, Value &b) {
        int base = b.True() ? b.intval() : 10;
        if (base < 2 || base > 36)
            vm.BuiltinError("string_to_int: values out of range");
        char *end;
        auto sv = s.sval()->strv();
        auto i = parse_int<iint>(sv, base, &end);
        Push(sp,  i);
        return Value(end == sv.data() + sv.size());
    });

nfr("string_to_float", "s", "S", "F",
    "converts a string to a float. returns 0.0 if no numeric data could be parsed",
    [](StackPtr &, VM &, Value &s) {
        auto f = strtod(s.sval()->data(), nullptr);
        return Value(f);
    });

nfr("tokenize", "s,delimiters,whitespace", "SSS", "S]",
    "splits a string into a vector of strings, by splitting into segments upon each dividing or"
    " terminating delimiter. Segments are stripped of leading and trailing whitespace."
    " Example: \"; A ; B C; \" becomes [ \"\", \"A\", \"B C\" ] with \";\" as delimiter and"
    " \" \" as whitespace.",
    [](StackPtr &, VM &vm, Value &s, Value &delims, Value &whitespace) {
        auto v = (LVector *)vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);
        auto ws = whitespace.sval()->strv();
        auto dl = delims.sval()->strv();
        auto p = s.sval()->strv();
        p.remove_prefix(std::min(p.find_first_not_of(ws), p.size()));
        while (!p.empty()) {
            auto delim = std::min(p.find_first_of(dl), p.size());
            auto end = std::min(p.find_last_not_of(ws) + 1, delim);
            v->Push(vm, vm.NewString(string_view(p.data(), end)));
            p.remove_prefix(delim);
            p.remove_prefix(std::min(p.find_first_not_of(dl), p.size()));
            p.remove_prefix(std::min(p.find_first_not_of(ws), p.size()));
        }
        return Value(v);
    });

nfr("unicode_to_string", "us", "I]", "S",
    "converts a vector of ints representing unicode values to a UTF-8 string.",
    [](StackPtr &, VM &vm, Value &v) {
        char buf[7];
        string s;
        for (iint i = 0; i < v.vval()->len; i++) {
            auto &c = v.vval()->At(i);
            ToUTF8((int)c.ival(), buf);
            s += buf;
        }
        return Value(vm.NewString(s));
    });

nfr("string_to_unicode", "s", "S", "I]B",
    "converts a UTF-8 string into a vector of unicode values. second return value is false"
    " if there was a decoding error, and the vector will only contain the characters up to the"
    " error",
    [](StackPtr &sp, VM &vm, Value &s) {
        auto v = (LVector *)vm.NewVec(0, s.sval()->len, TYPE_ELEM_VECTOR_OF_INT);
        Push(sp, v);
        auto p = s.sval()->strv();
        while (!p.empty()) {
            int u = FromUTF8(p);
            if (u < 0) return Value(false);
            v->Push(vm, u);
        }
        return Value(true);
    });

nfr("number_to_string", "number,base,minchars", "III", "S",
    "converts the (unsigned version) of the input integer number to a string given the base"
    " (2..36, e.g. 16 for hex) and outputting a minimum of characters (padding with 0).",
    [](StackPtr &, VM &vm, Value &n, Value &b, Value &mc) {
        if (b.ival() < 2 || b.ival() > 36 || mc.ival() > 32)
            vm.BuiltinError("number_to_string: values out of range");
        auto i = (uint64_t)n.ival();
        string s;
        auto from = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        while (i || ssize(s) < mc.ival()) {
            s.insert(0, 1, from[i % b.ival()]);
            i /= b.ival();
        }
        return Value(vm.NewString(s));
    });

nfr("lowercase", "s", "S", "S",
    "converts a UTF-8 string from any case to lower case, affecting only A-Z",
    [](StackPtr &, VM &vm, Value &s) {
        auto ns = vm.NewString(s.sval()->strv());
        for (auto &c : ns->strv()) {
            // This is unicode-safe, since all unicode chars are in bytes >= 128
            if (c >= 'A' && c <= 'Z') (char &)c += 'a' - 'A';
        }
        return Value(ns);
    });

nfr("uppercase", "s", "S", "S",
    "converts a UTF-8 string from any case to upper case, affecting only a-z",
    [](StackPtr &, VM &vm, Value &s) {
        auto ns = vm.NewString(s.sval()->strv());
        for (auto &c : ns->strv()) {
            // This is unicode-safe, since all unicode chars are in bytes >= 128
            if (c >= 'a' && c <= 'z') (char &)c -= 'a' - 'A';
        }
        return Value(ns);
    });

nfr("escape_string", "s,set,prefix,postfix", "SSSS", "S",
    "prefixes & postfixes any occurrences or characters in set in string s",
    [](StackPtr &, VM &vm, Value &s, Value &set, Value &prefix, Value &postfix) {
        string out;
        for (auto p = s.sval()->strv();;) {
            auto loc = p.find_first_of(set.sval()->strv());
            if (loc != string_view::npos) {
                out.append(p.data(), loc);
                auto presv = prefix.sval()->strv();
                out.append(presv.data(), presv.size());
                out += p[loc++];
                auto postsv = postfix.sval()->strv();
                out.append(postsv.data(), postsv.size());
                p.remove_prefix(loc);
            } else {
                out += p;
                break;
            }
        }
        return Value(vm.NewString(out));
    });

nfr("concat_string", "v,sep", "S]S", "S",
    "concatenates all elements of the string vector, separated with sep.",
    [](StackPtr &, VM &vm, Value &v, Value &sep) {
        string s;
        auto sepsv = sep.sval()->strv();
        for (iint i = 0; i < v.vval()->len; i++) {
            if (i) s.append(sepsv);
            auto esv = v.vval()->At(i).sval()->strv();
            s.append(esv);
        }
        return Value(vm.NewString(s));
    });

nfr("repeat_string", "s,n", "SI", "S",
    "returns a string consisting of n copies of the input string.",
    [](StackPtr &, VM &vm, Value &s, Value &_n) {
        auto n = std::max(iint(0), _n.ival());
        auto len = s.sval()->len;
        auto ns = vm.NewString(len * n);
        for (iint i = 0; i < n; i++) {
            memcpy((char *)ns->data() + i * len, s.sval()->data(), (size_t)len);
        }
        return Value(ns);
    });


#define VECTORVARS \
    auto len = Pop(sp).ival(); \
    auto elems = TopPtr(sp) - len;

#define VECTOROPNR(op) \
    for (iint i = 0; i < len; i++) { \
        auto f = elems[i]; \
        op; \
    }

#define VECTOROP(op) VECTORVARS VECTOROPNR(elems[i] = Value(op))

nfr("pow", "a,b", "FF", "F",
    "a raised to the power of b",
    [](StackPtr &, VM &, Value &a, Value &b) { return Value(pow(a.fval(), b.fval())); });

nfr("pow", "a,b", "II", "I",
    "a raised to the power of b, for integers, using exponentiation by squaring",
    [](StackPtr &, VM &, Value &a, Value &b) {
        return Value(b.ival() >= 0 ? ipow<iint>(a.ival(), b.ival()) : 0);
    });

nfr("pow", "a,b", "F}F", "F}",
    "vector elements raised to the power of b",
    [](StackPtr &sp, VM &) {
        auto exp = Pop(sp).fval();
        VECTOROP(pow(f.fval(), exp));
    });

nfr("log", "a", "F", "F",
    "natural logaritm of a",
    [](StackPtr &, VM &, Value &a) { return Value(log(a.fval())); });

nfr("sqrt", "f", "F", "F",
    "square root",
    [](StackPtr &, VM &, Value &a) { return Value(sqrt(a.fval())); });

nfr("ceiling", "f", "F", "I",
    "the nearest int >= f",
    [](StackPtr &, VM &, Value &a) { return Value(fceil(a.fval())); });
nfr("ceiling", "v", "F}", "I}",
    "the nearest ints >= each component of v",
    [](StackPtr &sp, VM &) { VECTOROP(iint(fceil(f.fval()))); });

nfr("floor", "f", "F", "I",
    "the nearest int <= f",
    [](StackPtr &, VM &, Value &a) { return Value(ffloor(a.fval())); });
nfr("floor", "v", "F}", "I}",
    "the nearest ints <= each component of v",
    [](StackPtr &sp, VM &) { VECTOROP(ffloor(f.fval())); });

nfr("int", "f", "F", "I",
    "converts a float to an int by dropping the fraction",
    [](StackPtr &, VM &, Value &a) { return Value(iint(a.fval())); });
nfr("int", "v", "F}", "I}",
    "converts a vector of floats to ints by dropping the fraction",
    [](StackPtr &sp, VM &) { VECTOROP(iint(f.fval())); });

nfr("round", "f", "F", "I",
    "converts a float to the closest int. same as int(f + 0.5), so does not work well on"
    " negative numbers",
    [](StackPtr &, VM &, Value &a) { return Value(iint(a.fval() + 0.5f)); });
nfr("round", "v", "F}", "I}",
    "converts a vector of floats to the closest ints",
    [](StackPtr &sp, VM &) { VECTOROP(iint(f.fval() + 0.5f)); });

nfr("fraction", "f", "F", "F",
    "returns the fractional part of a float: short for f - floor(f)",
    [](StackPtr &, VM &, Value &a) { return Value(a.fval() - floor(a.fval())); });
nfr("fraction", "v", "F}", "F}",
    "returns the fractional part of a vector of floats",
    [](StackPtr &sp, VM &) { VECTOROP(f.fval() - int(f.fval())); });

nfr("float", "i", "I", "F",
    "converts an int to float",
    [](StackPtr &, VM &, Value &a) { return Value(double(a.ival())); });
nfr("float", "v", "I}", "F}",
    "converts a vector of ints to floats",
    [](StackPtr &sp, VM &) { VECTOROP(double(f.ival())); });

nfr("sin", "angle", "F", "F",
    "the y coordinate of the normalized vector indicated by angle (in degrees)",
    [](StackPtr &, VM &, Value &a) { return Value(sin(a.fval() * RAD)); });
nfr("cos", "angle", "F", "F",
    "the x coordinate of the normalized vector indicated by angle (in degrees)",
    [](StackPtr &, VM &, Value &a) { return Value(cos(a.fval() * RAD)); });
nfr("tan", "angle", "F", "F",
    "the tangent of an angle (in degrees)",
    [](StackPtr &, VM &, Value &a) { return Value(tan(a.fval() * RAD)); });

nfr("sincos", "angle", "F", "F}:2",
    "the normalized vector indicated by angle (in degrees), same as xy { cos(angle), sin(angle) }",
    [](StackPtr &sp, VM &) {
        auto a = Pop(sp).fval();
        PushVec(sp, double2(cos(a * RAD), sin(a * RAD)));
    });

nfr("asin", "y", "F", "F",
    "the angle (in degrees) indicated by the y coordinate projected to the unit circle",
    [](StackPtr &, VM &, Value &y) { return Value(asin(y.fval()) / RAD); });
nfr("acos", "x", "F", "F",
    "the angle (in degrees) indicated by the x coordinate projected to the unit circle",
    [](StackPtr &, VM &, Value &x) { return Value(acos(x.fval()) / RAD); });
nfr("atan", "x", "F", "F",
    "the angle (in degrees) indicated by the y coordinate of the tangent projected to the unit circle",
    [](StackPtr &, VM &, Value &x) { return Value(atan(x.fval()) / RAD); });

nfr("radians", "angle", "F", "F",
    "converts an angle in degrees to radians",
    [](StackPtr &, VM &, Value &a) { return Value(a.fval() * RAD); });
nfr("degrees", "angle", "F", "F",
    "converts an angle in radians to degrees",
    [](StackPtr &, VM &, Value &a) { return Value(a.fval() / RAD); });

nfr("atan2", "vec", "F}:2" , "F",
    "the angle (in degrees) corresponding to a normalized 2D vector",
    [](StackPtr &sp, VM &) {
        auto v = PopVec<double2>(sp);
        Push(sp, atan2(v.y, v.x) / RAD);
    });

nfr("radians", "angle", "F", "F",
    "converts an angle in degrees to radians",
    [](StackPtr &, VM &, Value &a) { return Value(a.fval() * RAD); });
nfr("degrees", "angle", "F", "F",
    "converts an angle in radians to degrees",
    [](StackPtr &, VM &, Value &a) { return Value(a.fval() / RAD); });

nfr("normalize", "vec",  "F}" , "F}",
    "returns a vector of unit length",
    [](StackPtr &sp, VM &) {
        double sql = 0.0;
        VECTORVARS;
        VECTOROPNR(sql += f.fval() * f.fval());
        double m = sqrt(sql);
        if (m == 0.0) {
            VECTOROPNR((void)f; elems[i] = 0.0);
        } else {
            VECTOROPNR(elems[i] = f.fval() / m);
        }
    });

nfr("dot", "a,b", "F}F}1", "F",
    "the length of vector a when projected onto b (or vice versa)",
    [](StackPtr &sp, VM &) {
        auto b = DangleVec<double>(sp);
        auto a = DangleVec<double>(sp);
        Push(sp, a.dot(b));
    });

nfr("magnitude", "v", "F}", "F",
    "the geometric length of a vector",
    [](StackPtr &sp, VM &) {
        auto a = DangleVec<double>(sp);
        Push(sp, a.length());
    });

nfr("magnitude_squared", "v", "F}", "F",
    "the geometric length of a vector squared",
    [](StackPtr &sp, VM &) {
        auto a = DangleVec<double>(sp);
        Push(sp, a.length_squared());
    });

nfr("magnitude_squared", "v", "I}", "I",
    "the geometric length of a vector squared",
    [](StackPtr &sp, VM &) {
        auto a = DangleVec<iint>(sp);
        Push(sp, a.length_squared());
    });

nfr("manhattan", "v", "I}", "I",
    "the manhattan distance of a vector",
    [](StackPtr &sp, VM &) {
        auto a = DangleVec<iint>(sp);
        Push(sp, a.manhattan());
    });

nfr("cross", "a,b", "F}:3F}:3", "F}:3",
    "a perpendicular vector to the 2D plane defined by a and b (swap a and b for its inverse)",
    [](StackPtr &sp, VM &) {
        auto b = PopVec<double3>(sp);
        auto a = PopVec<double3>(sp);
        PushVec(sp, cross(a, b));
    });

nfr("volume", "v", "F}", "F", "the volume of the area spanned by the vector",
    [](StackPtr &sp, VM &) {
        auto a = DangleVec<double>(sp);
        Push(sp, a.volume());
    });

nfr("volume", "v", "I}", "I", "the volume of the area spanned by the vector",
    [](StackPtr &sp, VM &) {
        auto a = DangleVec<iint>(sp);
        Push(sp, a.volume());
    });

nfr("rnd", "max", "I", "I",
    "a random value [0..max).",
    [](StackPtr &, VM &, Value &a) { return Value(rndx.rnd_int64(std::max((iint)1, a.ival()))); });
nfr("rnd", "max", "I}", "I}",
    "a random vector within the range of an input vector.",
    [](StackPtr &sp, VM &) { VECTOROP(rndx.rnd_int64(std::max((iint)1, f.ival()))); });
nfr("rnd_float", "", "", "F",
    "a random float [0..1)",
    [](StackPtr &, VM &) { return Value(rndx.rnd_double()); });
nfr("rnd_gaussian", "", "", "F",
    "a random float in a gaussian distribution with mean 0 and stddev 1",
    [](StackPtr &, VM &) { return Value(rndx.rnd_gaussian()); });
nfr("rnd_seed", "seed", "I", "",
    "explicitly set a random seed for reproducable randomness",
    [](StackPtr &, VM &, Value &seed) { rndx.seed(seed.ival()); return NilVal(); });


nfr("rndm", "max", "I", "I",
    "deprecated: old mersenne twister version of the above for backwards compat.",
    [](StackPtr &, VM &, Value &a) { return Value(rndm.rnd_int(std::max(1, (int)a.ival()))); });
nfr("rndm_seed", "seed", "I", "",
    "deprecated: old mersenne twister version of the above for backwards compat.",
    [](StackPtr &, VM &, Value &seed) { rndm.seed((int)seed.ival()); return NilVal(); });

nfr("div", "a,b", "II", "F",
    "forces two ints to be divided as floats",
    [](StackPtr &, VM &, Value &a, Value &b) { return Value(double(a.ival()) / double(b.ival())); });

nfr("clamp", "x,min,max", "III", "I",
    "forces an integer to be in the range between min and max (inclusive)",
    [](StackPtr &, VM &, Value &a, Value &b, Value &c) {
        return Value(geom::clamp(a.ival(), b.ival(), c.ival()));
    });

nfr("clamp", "x,min,max", "FFF", "F",
    "forces a float to be in the range between min and max (inclusive)",
    [](StackPtr &, VM &, Value &a, Value &b, Value &c) {
        return Value(geom::clamp(a.fval(), b.fval(), c.fval()));
    });

nfr("clamp", "x,min,max", "I}I}1I}1", "I}",
    "forces an integer vector to be in the range between min and max (inclusive)",
    [](StackPtr &sp, VM &) {
        auto c = DangleVec<iint>(sp);
        auto b = DangleVec<iint>(sp);
        auto a = ResultVec<iint>(sp);
        a.clamp(b, c);
    });

nfr("clamp", "x,min,max", "F}F}1F}1", "F}",
    "forces a float vector to be in the range between min and max (inclusive)",
    [](StackPtr &sp, VM &) {
        auto c = DangleVec<double>(sp);
        auto b = DangleVec<double>(sp);
        auto a = DangleVec<double>(sp);
        a.clamp(b, c);
    });

nfr("in_range", "x,range,bias", "III?", "B",
    "checks if an integer is >= bias and < bias + range. Bias defaults to 0.",
    [](StackPtr &, VM &, Value &x, Value &range, Value &bias) {
        return Value(x.ival() >= bias.ival() && x.ival() < bias.ival() + range.ival());
    });

nfr("in_range", "x,range,bias", "FFF?", "B",
    "checks if a float is >= bias and < bias + range. Bias defaults to 0.",
    [](StackPtr &, VM &, Value &x, Value &range, Value &bias) {
        return Value(x.fval() >= bias.fval() && x.fval() < bias.fval() + range.fval());
    });

nfr("in_range", "x,range,bias", "I}I}1I}1?", "B",
    "checks if a 2d/3d integer vector is >= bias and < bias + range. Bias defaults to 0.",
    [](StackPtr &sp, VM &) {
        auto bias = Top(sp).True() ? DangleVec<iint>(sp) : (Pop(sp), ValueVec<iint>());
        auto range = DangleVec<iint>(sp);
        auto x = DangleVec<iint>(sp);
        Push(sp, x.in_range(range, bias));
    });

nfr("in_range", "x,range,bias", "F}F}1F}1?", "B",
    "checks if a 2d/3d float vector is >= bias and < bias + range. Bias defaults to 0.",
    [](StackPtr &sp, VM &) {
        auto bias = Top(sp).True() ? DangleVec<double>(sp) : (Pop(sp), ValueVec<double>());
        auto range = DangleVec<double>(sp);
        auto x = DangleVec<double>(sp);
        Push(sp, x.in_range(range, bias));
    });

nfr("abs", "x", "I", "I",
    "absolute value of an integer",
    [](StackPtr &, VM &, Value &a) { return Value(std::abs(a.ival())); });
nfr("abs", "x", "F", "F",
    "absolute value of a float",
    [](StackPtr &, VM &, Value &a) { return Value(fabs(a.fval())); });
nfr("abs", "x", "I}", "I}",
    "absolute value of an int vector",
    [](StackPtr &sp, VM &) { VECTOROP(std::abs(f.ival())); });
nfr("abs", "x", "F}", "F}",
    "absolute value of a float vector",
    [](StackPtr &sp, VM &) { VECTOROP(fabs(f.fval())); });

nfr("sign", "x", "I", "I",
    "sign (-1, 0, 1) of an integer",
    [](StackPtr &, VM &, Value &a) { return Value(signum(a.ival())); });
nfr("sign", "x", "F", "I",
    "sign (-1, 0, 1) of a float",
    [](StackPtr &, VM &, Value &a) { return Value(signum(a.fval())); });
nfr("sign", "x", "I}", "I}",
    "signs of an int vector",
    [](StackPtr &sp, VM &) { VECTOROP(signum(f.ival())); });
nfr("sign", "x", "F}", "I}",
    "signs of a float vector",
    [](StackPtr &sp, VM &) { VECTOROP(signum(f.fval())); });

#define VECSCALAROP(type, init, fun, acc, len, at) \
    type v = init; \
    auto l = x.acc()->len; \
    for (iint i = 0; i < l; i++) { \
        auto f = x.acc()->at; \
        fun; \
    } \
    return Value(v);

#define STSCALAROP(type, init, fun) \
    type v = init; \
    auto l = Pop(sp).ival(); \
    for (iint i = 0; i < l; i++) { \
        auto f = Pop(sp); \
        fun; \
    } \
    Push(sp,  v);

nfr("min", "x,y", "II", "I",
    "smallest of 2 integers.",
    [](StackPtr &, VM &, Value &x, Value &y) {
        return Value(std::min(x.ival(), y.ival()));
    });
nfr("min", "x,y", "FF", "F",
    "smallest of 2 floats.",
    [](StackPtr &, VM &, Value &x, Value &y) {
        return Value(std::min(x.fval(), y.fval()));
    });
nfr("min", "x,y", "I}I}1", "I}",
    "smallest components of 2 int vectors",
    [](StackPtr &sp, VM &) {
        auto y = DangleVec<iint>(sp);
        auto x = ResultVec<iint>(sp);
        x.min_assign(y);
    });
nfr("min", "x,y", "F}F}1", "F}",
    "smallest components of 2 float vectors",
    [](StackPtr &sp, VM &) {
        auto y = DangleVec<double>(sp);
        auto x = ResultVec<double>(sp);
        x.min_assign(y);
    });
nfr("min", "v", "I}", "I",
    "smallest component of a int vector.",
    [](StackPtr &sp, VM &) {
        STSCALAROP(iint, INT_MAX, v = std::min(v, f.ival()))
    });
nfr("min", "v", "F}", "F",
    "smallest component of a float vector.",
    [](StackPtr &sp, VM &) {
        STSCALAROP(double, FLT_MAX, v = std::min(v, f.fval()))
    });
nfr("min", "v", "I]", "I",
    "smallest component of a int vector, or INT_MAX if length 0.",
    [](StackPtr &, VM &, Value &x) {
        VECSCALAROP(iint, INT_MAX, v = std::min(v, f.ival()), vval, len, At(i))
    });
nfr("min", "v", "F]", "F",
    "smallest component of a float vector, or FLT_MAX if length 0.",
    [](StackPtr &, VM &, Value &x) {
        VECSCALAROP(double, FLT_MAX, v = std::min(v, f.fval()), vval, len, At(i))
    });

nfr("max", "x,y", "II", "I",
    "largest of 2 integers.",
    [](StackPtr &, VM &, Value &x, Value &y) {
        return Value(std::max(x.ival(), y.ival()));
    });
nfr("max", "x,y", "FF", "F",
    "largest of 2 floats.",
    [](StackPtr &, VM &, Value &x, Value &y) {
        return Value(std::max(x.fval(), y.fval()));
    });
nfr("max", "x,y", "I}I}1", "I}",
    "largest components of 2 int vectors",
    [](StackPtr &sp, VM &) {
        auto y = DangleVec<iint>(sp);
        auto x = ResultVec<iint>(sp);
        x.max_assign(y);
    });
nfr("max", "x,y", "F}F}1", "F}",
    "largest components of 2 float vectors",
    [](StackPtr &sp, VM &) {
        auto y = DangleVec<double>(sp);
        auto x = ResultVec<double>(sp);
        x.max_assign(y);
    });
nfr("max", "v", "I}", "I",
    "largest component of a int vector.",
    [](StackPtr &sp, VM &) {
        STSCALAROP(iint, INT_MIN, v = std::max(v, f.ival()))
    });
nfr("max", "v", "F}", "F",
    "largest component of a float vector.",
    [](StackPtr &sp, VM &) {
        STSCALAROP(double, FLT_MIN, v = std::max(v, f.fval()))
    });
nfr("max", "v", "I]", "I",
    "largest component of a int vector, or INT_MIN if length 0.",
    [](StackPtr &, VM &, Value &x) {
        VECSCALAROP(iint, INT_MIN, v = std::max(v, f.ival()), vval, len, At(i))
    });
nfr("max", "v", "F]", "F",
    "largest component of a float vector, or FLT_MIN if length 0.",
    [](StackPtr &, VM &, Value &x) {
        VECSCALAROP(double, FLT_MIN, v = std::max(v, f.fval()), vval, len, At(i))
    });

nfr("lerp", "x,y,f", "FFF", "F",
    "linearly interpolates between x and y with factor f [0..1]",
    [](StackPtr &, VM &, Value &x, Value &y, Value &f) {
        return Value(mix(x.fval(), y.fval(), (float)f.fval()));
    });

nfr("lerp", "a,b,f", "F}F}1F", "F}",
    "linearly interpolates between a and b vectors with factor f [0..1]",
    [](StackPtr &sp, VM &) {
        auto f = Pop(sp).fltval();
        auto y = DangleVec<double>(sp);
        auto x = ResultVec<double>(sp);
        x.mix(y, f);
    });

nfr("smoothmin", "x,y,k", "FFF", "F",
    "k is the influence range",
    [](StackPtr &, VM &, Value &x, Value &y, Value &k) {
        return Value(smoothmin(x.fltval(), y.fltval(), k.fltval()));
    });

nfr("smoothstep", "x", "F", "F",
    "input must be in range 0..1, https://en.wikipedia.org/wiki/Smoothstep",
    [](StackPtr &, VM &, Value &x) {
        return Value(smoothstep(x.fltval()));
    });

nfr("smootherstep", "x", "F", "F",
    "input must be in range 0..1, https://en.wikipedia.org/wiki/Smoothstep",
    [](StackPtr &, VM &, Value &x) {
        return Value(smootherstep(x.fltval()));
    });

nfr("cardinal_spline", "z,a,b,c,f,tension", "F}F}1F}1F}1FF", "F}",
    "computes the position between a and b with factor f [0..1], using z (before a) and c"
    " (after b) to form a cardinal spline (tension at 0.5 is a good default)",
    [](StackPtr &sp, VM &) {
        auto t = Pop(sp).fval();
        auto f = Pop(sp).fval();
        auto numelems = Top(sp).intval();
        auto c = PopVec<double3>(sp);
        auto b = PopVec<double3>(sp);
        auto a = PopVec<double3>(sp);
        auto z = PopVec<double3>(sp);
        PushVec(sp, cardinal_spline(z, a, b, c, f, t), numelems);
    });

nfr("line_intersect", "line1a,line1b,line2a,line2b", "F}:2F}:2F}:2F}:2", "IF}:2",
    "computes if there is an intersection point between 2 line segments, with the point as"
    " second return value",
    [](StackPtr &sp, VM &) {
        auto l2b = PopVec<double2>(sp);
        auto l2a = PopVec<double2>(sp);
        auto l1b = PopVec<double2>(sp);
        auto l1a = PopVec<double2>(sp);
        double2 ipoint(0, 0);
        auto r = line_intersect(l1a, l1b, l2a, l2b, &ipoint);
        Push(sp,  r);
        PushVec(sp, ipoint);
    });

nfr("circles_within_range", "dist,positions,radiuses,positions2,radiuses2,gridsize", "FF}:2]F]F}:2]F]I}:2", "I]]",
    "Given a vector of 2D positions (and same size vectors of radiuses), returns a vector of"
    " vectors of indices (to the second set of positions and radiuses) of the circles that are"
    " within dist of eachothers radius. If the second set are [], the first set is used for"
    " both (and the self element is excluded)."
    " gridsize optionally specifies the size of the grid to use for accellerated lookup of nearby"
    " points. This is essential for the algorithm to be fast, too big or too small can cause slowdown."
    " Omit it, and a heuristic will be chosen for you, which is currently sqrt(num_circles) * 2 along"
    " each dimension, e.g. 100 elements would use a 20x20 grid."
    " Efficiency wise this algorithm is fastest if there is not too much variance in the radiuses of"
    " the second set and/or the second set has smaller radiuses than the first.",
    [](StackPtr &sp, VM &vm) {
        auto ncelld = PopVec<iint2>(sp);
        auto radiuses2 = Pop(sp).vval();
        auto positions2 = Pop(sp).vval();
        auto radiuses1 = Pop(sp).vval();
        auto positions1 = Pop(sp).vval();
        if (!radiuses2->len) radiuses2 = radiuses1;
        if (!positions2->len) positions2 = positions1;
        auto qdist = Pop(sp).fval();
        if (ncelld.x <= 0 || ncelld.y <= 0)
            ncelld = iint2((iint)sqrtf(float(positions2->len + 1) * 4));
        if (radiuses1->len != positions1->len || radiuses2->len != positions2->len)
            vm.BuiltinError("circles_within_range: input vectors size mismatch");
        struct Node { double2 pos; double rad; iint idx; Node *next; };
        vector<Node> nodes(positions2->SLen(), Node());
        double maxrad = 0;
        double2 minpos = double2(FLT_MAX), maxpos(FLT_MIN);
        for (ssize_t i = 0; i < positions2->SLen(); i++) {
            auto &n = nodes[i];
            auto p = ValueToF<2>(positions2->AtSt(i), positions2->width);
            minpos = min(minpos, p);
            maxpos = max(maxpos, p);
            n.pos = p;
            auto r = radiuses2->At(i).fval();
            maxrad = std::max(maxrad, r);
            n.rad = r;
            n.idx = i;
            n.next = nullptr;
        }
        vector<Node *> cells((ssize_t)(ncelld.x * ncelld.y), nullptr);
        auto wsize = max(maxpos - minpos, double2(0.0001f));  // Avoid either dim being 0.
        wsize *= 1.00001f;  // No objects may fall exactly on the far border.
        auto tocellspace = [&](const double2 &pos) {
            return iint2((pos - minpos) / wsize * double2(ncelld));
        };
        for (ssize_t i = 0; i < positions2->SLen(); i++) {
            auto &n = nodes[i];
            auto cp = tocellspace(n.pos);
            auto &c = cells[ssize_t(cp.x + cp.y * ncelld.x)];
            n.next = c;
            c = &n;
        }
        vector<iint> within_range;
        vector<LVector *> results(positions1->SLen(), nullptr);
        for (ssize_t i = 0; i < positions1->SLen(); i++) {
            auto pos = ValueToF<2>(positions1->AtSt(i), positions1->width);
            auto rad = radiuses1->At(i).fval();
            auto scanrad = rad + maxrad + qdist;
            auto minc = max(iint2_0, min(ncelld - 1, tocellspace(pos - scanrad)));
            auto maxc = max(iint2_0, min(ncelld - 1, tocellspace(pos + scanrad)));
            for (iint y = minc.y; y <= maxc.y; y++) {
                for (iint x = minc.x; x <= maxc.x; x++) {
                    for (auto c = cells[(ssize_t)(x + y * ncelld.x)]; c; c = c->next) {
                        if (c->idx != i || positions1 != positions2) {
                            auto d = length(c->pos - pos) - rad - c->rad;
                            if (d < qdist) {
                                within_range.push_back(c->idx);
                            }
                        }
                    }
                }
            }
            auto vec = (LVector *)vm.NewVec(0, (int)within_range.size(),
                                                    TYPE_ELEM_VECTOR_OF_INT);
            for (auto i : within_range) vec->Push(vm, Value(i));
            within_range.clear();
            results[i] = vec;
        }
        auto rvec = (LVector *)vm.NewVec(0, positions1->len, TYPE_ELEM_VECTOR_OF_VECTOR_OF_INT);
        for (auto vec : results) rvec->Push(vm, Value(vec));
        Push(sp,  rvec);
    });

nfr("wave_function_collapse", "tilemap,size", "S]I}:2", "S]I",
    "returns a tilemap of given size modelled after the possible shapes in the input"
    " tilemap. Tilemap should consist of chars in the 0..127 range. Second return value"
    " the number of failed neighbor matches, this should"
    " ideally be 0, but can be non-0 for larger maps. Simply call this function"
    " repeatedly until it is 0",
    [](StackPtr &sp, VM &vm) {
        auto sz = PopVec<int2>(sp);
        auto tilemap = Pop(sp);
        auto rows = tilemap.vval()->SLen();
        vector<const char *> inmap(rows);
        iint cols = 0;
        for (ssize_t i = 0; i < rows; i++) {
            auto sv = tilemap.vval()->At(i).sval()->strv();
            if (i) { if (ssize(sv) != cols) vm.BuiltinError("all columns must be equal length"); }
            else cols = sv.size();
            inmap[i] = sv.data();
        }
        auto outstrings = ToValueOfVectorOfStringsEmpty(vm, sz, 0);
        vector<char *> outmap(sz.y, nullptr);
        for (int i = 0; i < sz.y; i++) outmap[i] = (char *)outstrings.vval()->At(i).sval()->data();
        int num_contradictions = 0;
        auto ok = WaveFunctionCollapse(int2(iint2(cols, ssize(inmap))), inmap.data(), sz, outmap.data(),
                                       rndx, num_contradictions);
        if (!ok)
            vm.BuiltinError("tilemap contained too many tile ids");
        Push(sp,  outstrings);
        Push(sp,  num_contradictions);
    });

nfr("hash", "x", "I", "I",
    "hashes an int value into a positive int; may be the identity function",
    [](StackPtr &, VM &vm, Value &a) {
        auto h = positive_bits(a.Hash(vm, V_INT));
        return Value(h);
    });
nfr("hash", "x", "A", "I",
    "hashes any ref value into a positive int",
    [](StackPtr &, VM &vm, Value &a) {
        auto h = positive_bits(a.ref()->Hash(vm));
        return Value(h);
    });
nfr("hash", "x", "L", "I",
    "hashes a function value into a positive int",
    [](StackPtr &, VM &vm, Value &a) {
        auto h = positive_bits(a.Hash(vm, V_FUNCTION));
        return Value(h);
    });
nfr("hash", "x", "F", "I",
    "hashes a float value into a positive int",
    [](StackPtr &, VM &vm, Value &a) {
        auto h = positive_bits(a.Hash(vm, V_FLOAT));
        return Value(h);
    });
nfr("hash", "v", "I}", "I",
    "hashes a int vector into a positive int",
    [](StackPtr &sp, VM &vm) {
        auto a = DangleVec<iint>(sp);
        Push(sp, positive_bits(a.Hash(vm, V_INT)));
    });
nfr("hash", "v", "F}", "I",
    "hashes a float vector into a positive int",
    [](StackPtr &sp, VM &vm) {
        auto a = DangleVec<double>(sp);
        Push(sp, positive_bits(a.Hash(vm, V_FLOAT)));
    });

nfr("program_name", "", "", "S",
    "returns the name of the main program (e.g. \"foo.lobster\".",
    [](StackPtr &, VM &vm) {
        return Value(vm.NewString(vm.GetProgramName()));
    });

nfr("vm_compiled_mode", "", "", "B",
    "returns if the VM is running in compiled mode (Lobster -> C++), or false for JIT.",
    [](StackPtr &, VM &) {
        return Value(!VM_JIT_MODE);
    });

nfr("seconds_elapsed", "", "", "F",
    "seconds since program start as a float, unlike gl_time() it is calculated every time it is"
    " called",
    [](StackPtr &, VM &vm) {
        return Value(vm.Time());
    });

nfr("date_time", "utc", "B?", "I]",
    "a vector of integers representing date & time information (index with date_time.lobster)."
    " By default returns local time, pass true for UTC instead.",
    [](StackPtr &, VM &vm, Value &utc) {
        auto time = std::time(nullptr);
        const iint num_elems = 9;
        auto v = vm.NewVec(num_elems, num_elems, TYPE_ELEM_VECTOR_OF_INT);
        for (iint i = 0; i < num_elems; i++) v->At(i) = -1;
        if (!time) return Value(v);
        v->At(0) = (iint)time; // unix epoch in seconds
        auto tm = utc.True() ? std::gmtime(&time) : std::localtime(&time);
        if (!tm) return Value(v);
        v->At(1) = tm->tm_year;
        v->At(2) = tm->tm_mon;
        v->At(3) = tm->tm_mday;
        v->At(4) = tm->tm_yday;
        v->At(5) = tm->tm_wday;
        v->At(6) = tm->tm_hour;
        v->At(7) = tm->tm_min;
        v->At(8) = tm->tm_sec;
        return Value(v);
    });

nfr("date_time_string", "utc", "B?", "S",
    "a string representing date & time information in the format: \'Www Mmm dd hh:mm:ss yyyy\'."
    " By default returns local time, pass true for UTC instead.",
    [](StackPtr &, VM &vm, Value &utc) {
        auto time = std::time(nullptr);
        if (!time) return Value(vm.NewString(""));
        auto tm = utc.True() ? std::gmtime(&time) : std::localtime(&time);
        if (!tm) return Value(vm.NewString(""));
        auto ts = std::asctime(tm);
        auto s = vm.NewString(string_view(ts, 24));
        return Value(s);
    });

nfr("assert", "condition", "A*", "Ab1",
    "halts the program with an assertion failure if passed false. returns its input."
    " runtime errors like this will contain a stack trace if --runtime-verbose is on.",
    [](StackPtr &, VM &vm, Value &c) {
        if (c.False()) vm.BuiltinError("assertion failed");
        return c;
    });

nfr("get_stack_trace", "", "", "S",
    "gets a stack trace of the current location of the program (needs --runtime-verbose)"
    " without actually stopping the program.",
    [](StackPtr &, VM &vm) {
        string sd;
        vm.DumpStackTrace(sd);
        return Value(vm.NewString(sd));
    });

nfr("get_memory_usage", "n", "I", "S",
    "gets a text showing the top n object types that are using the most memory.",
    [](StackPtr &, VM &vm, Value &n) {
        return Value(vm.NewString(vm.MemoryUsage(n.intval())));
    });

nfr("pass", "", "", "",
    "does nothing. useful for empty bodies of control structures.",
    [](StackPtr &, VM &) {
        return NilVal();
    });

nfr("trace_bytecode", "mode", "I", "",
    "tracing shows each bytecode instruction as it is being executed, not very useful unless"
    " you are trying to isolate a compiler bug. Mode is off(0), on(1) or tail only (2)",
    [](StackPtr &, VM &vm, Value &i) {
        vm.Trace((TraceMode)i.ival());
        return NilVal();
    });

nfr("reference_count", "val", "A", "I",
    "get the reference count of any value. for compiler debugging, mostly",
    [](StackPtr &, VM &, Value &x) {
        auto refc = x.refnil() ? x.refnil()->refc - 1 : -1;
        return Value(refc);
    });

nfr("set_console", "on", "B", "",
    "lets you turn on/off the console window (on Windows)",
    [](StackPtr &, VM &, Value &x) {
        SetConsole(x.True());
        return NilVal();
    });

nfr("command_line_arguments", "", "", "S]",
    "",
    [](StackPtr &, VM &vm) {
        return ToValueOfVectorOfStrings(vm, vm.program_args);
    });

nfr("thread_information", "", "", "II",
    "returns the number of hardware threads, and the number of cores",
    [](StackPtr &sp, VM &) {
        Push(sp,  NumHWThreads());
        return Value(NumHWCores());
    });

nfr("is_worker_thread", "", "", "B",
    "whether the current thread is a worker thread",
    [](StackPtr &, VM &vm) {
        return Value(vm.is_worker);
    });

nfr("start_worker_threads", "numthreads", "I", "",
    "launch worker threads",
    [](StackPtr &, VM &vm, Value &n) {
        vm.StartWorkers(n.ival());
        return NilVal();
    });

nfr("stop_worker_threads", "", "", "",
    "only needs to be called if you want to stop the worker threads before the end of"
            " the program, or if you want to call start_worker_threads again. workers_alive"
            " will become false inside the workers, which should then exit.",
    [](StackPtr &, VM &vm) {
        vm.TerminateWorkers();
        return NilVal();
    });

nfr("workers_alive", "", "", "B",
    "whether workers should continue doing work. returns false after"
            " stop_worker_threads() has been called.",
    [](StackPtr &, VM &vm) {
        return Value(vm.tuple_space && vm.tuple_space->alive);
    });

nfr("thread_write", "struct", "A", "",
    "put this struct in the thread queue",
    [](StackPtr &, VM &vm, Value &s) {
        vm.WorkerWrite(s.refnil());
        return NilVal();
    });

nfr("thread_read", "type", "T", "A1?",
    "get a struct from the thread queue. pass the typeof struct. blocks if no such"
            "structs available. returns struct, or nil if stop_worker_threads() was called",
    [](StackPtr &, VM &vm, Value &t) {
        return Value(vm.WorkerRead((type_elem_t)t.ival()));
    });

}  // AddBuiltins

}
