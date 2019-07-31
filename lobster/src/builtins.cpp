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

#include "lobster/unicode.h"
#include "lobster/wfc.h"

namespace lobster {

static RandomNumberGenerator<MersenneTwister> rnd;

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

template<typename T> Value BinarySearch(VM &vm, Value &l, Value &key, T comparefun) {
    intp size = l.vval()->len;
    intp i = 0;
    for (;;) {
        if (!size) break;
        intp mid = size / 2;
        intp comp = comparefun(key, l.vval()->At(i + mid));
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
    vm.Push(Value(size));
    return Value(i);
}

void AddBuiltins(NativeRegistry &nfr) {

nfr("print", "x", "Ss", "",
    "output any value to the console (with linefeed).",
    [](VM &vm, Value &a) {
        vm.ss_reuse.str(string());
        vm.ss_reuse.clear();
        RefToString(vm, vm.ss_reuse, a.refnil(), vm.programprintprefs);
        LOG_PROGRAM(vm.ss_reuse.str());
        return Value();
    });

// This is now the identity function, but still useful to force a coercion.
nfr("string", "x", "Ssk", "S",
    "convert any value to string",
    [](VM &, Value &a) {
        return a;
    });

nfr("set_print_depth", "depth", "I", "",
    "for printing / string conversion: sets max vectors/objects recursion depth (default 10)",
    [](VM &vm, Value &a) {
        vm.programprintprefs.depth = a.ival();
        return Value();
    });

nfr("set_print_length", "len", "I", "",
    "for printing / string conversion: sets max string length (default 100000)",
    [](VM &vm, Value &a) {
        vm.programprintprefs.budget = a.ival();
        return Value();
    });

nfr("set_print_quoted", "quoted", "B", "",
    "for printing / string conversion: if the top level value is a string, whether to convert"
    " it with escape codes and quotes (default false)",
    [](VM &vm, Value &a) {
        vm.programprintprefs.quoted = a.ival() != 0;
        return Value();
    });

nfr("set_print_decimals", "decimals", "I", "",
    "for printing / string conversion: number of decimals for any floating point output"
    " (default -1, meaning all)",
    [](VM &vm, Value &a) {
        vm.programprintprefs.decimals = a.ival();
        return Value();
    });

nfr("set_print_indent", "spaces", "I", "",
    "for printing / string conversion: number of spaces to indent with. default is 0:"
    " no indent / no multi-line",
    [](VM &vm, Value &a) {
        vm.programprintprefs.indent = a.intval();
        return Value();
    });

nfr("get_line", "", "", "S",
    "reads a string from the console if possible (followed by enter)",
    [](VM &vm) {
        const int MAXSIZE = 1000;
        char buf[MAXSIZE];
        if (!fgets(buf, MAXSIZE, stdin)) buf[0] = 0;
        buf[MAXSIZE - 1] = 0;
        for (int i = 0; i < MAXSIZE; i++) if (buf[i] == '\n') { buf[i] = 0; break; }
        return Value(vm.NewString(buf));
    });

nfr("if", "cond,then,else", "ALL?", "A",
    "evaluates then or else depending on cond, else is optional",
    [](VM &, Value &c, Value &t, Value &e) {
        assert(0);  // Special case implementation in the VM
        (void)c;
        (void)t;
        (void)e;
        return Value();
    });

nfr("while", "cond,do", "L@L", "A",
    "evaluates body while cond (converted to a function) holds true, returns last body value",
    [](VM &, Value &c, Value &b) {
        assert(0);  // Special case implementation in the VM
        (void)c;
        (void)b;
        return Value();
    });

nfr("for", "iter,do", "AL", "",
    "iterates over int/vector/string, body may take [ element [ , index ] ] arguments",
    [](VM &, Value &iter, Value &body) {
        assert(0);  // Special case implementation in the VM
        (void)iter;
        (void)body;
        return Value();
    });

nfr("append", "xs,ys", "A]*A]*1", "A]1",
    "creates a new vector by appending all elements of 2 input vectors",
    [](VM &vm, Value &v1, Value &v2) {
        auto type = v1.vval()->tti;
        auto nv = (LVector *)vm.NewVec(0, v1.vval()->len + v2.vval()->len, type);
        nv->Append(vm, v1.vval(), 0, v1.vval()->len);
        nv->Append(vm, v2.vval(), 0, v2.vval()->len);
        return Value(nv);
    });

nfr("vector_reserve", "typeid,len", "TI", "A]*",
    "creates a new empty vector much like [] would, except now ensures"
    " it will have space for len push() operations without having to reallocate."
    " pass \"typeof return\" as typeid.",
    [](VM &vm, Value &type, Value &len) {
        return Value(vm.NewVec(0, len.ival(), (type_elem_t)type.ival()));
    });

nfr("length", "x", "I", "I",
    "length of int (identity function, useful in combination with string/vector version)",
    [](VM &, Value &a) {
        return a;
    });

nfr("length", "s", "S", "I",
    "length of string",
    [](VM &, Value &a) {
        auto len = a.sval()->len;
        return Value(len);
    });

nfr("length", "xs", "A]*", "I",
    "length of vector",
    [](VM &, Value &a) {
        auto len = a.vval()->len;
        return Value(len);
    });

nfr("equal", "a,b", "AA", "B",
    "structural equality between any two values (recurses into vectors/objects,"
    " unlike == which is only true for vectors/objects if they are the same object)",
    [](VM &vm, Value &a, Value &b) {
        bool eq = RefEqual(vm, a.refnil(), b.refnil(), true);
        return Value(eq);
    });

nfr("push", "xs,x", "A]*Akw1", "Ab]1",
    "appends one element to a vector, returns existing vector",
    [](VM &vm) {
        auto val = vm.PopVecPtr();
        auto l = vm.Pop().vval();
        assert(val.second == l->width);
        l->PushVW(vm, val.first);
        vm.Push(l);
    });

nfr("pop", "xs", "A]*", "A1",
    "removes last element from vector and returns it",
    [](VM &vm) {
        auto l = vm.Pop().vval();
        if (!l->len) vm.BuiltinError("pop: empty vector");
        l->PopVW(vm.TopPtr());
        vm.PushN((int)l->width);
    });

nfr("top", "xs", "A]*", "Ab1",
    "returns last element from vector",
    [](VM &vm) {
        auto l = vm.Pop().vval();
        if (!l->len) vm.BuiltinError("top: empty vector");
        l->TopVW(vm.TopPtr());
        vm.PushN((int)l->width);
    });

nfr("insert", "xs,i,x", "A]*IAkw1", "Ab]1",
    "inserts a value into a vector at index i, existing elements shift upward,"
    " returns original vector",
    [](VM &vm) {
        auto val = vm.PopVecPtr();
        auto i = vm.Pop().ival();
        auto l = vm.Pop().vval();
        if (i < 0 || i > l->len)
            vm.BuiltinError("insert: index or n out of range");  // note: i==len is legal
        assert(val.second == l->width);
        l->Insert(vm, val.first, i);
        vm.Push(l);
    });

nfr("remove", "xs,i,n", "A]*II?", "A1",
    "remove element(s) at index i, following elements shift down. pass the number of elements"
    " to remove as an optional argument, default 1. returns the first element removed.",
    [](VM &vm) {
        auto n = vm.Pop().ival();
        auto i = vm.Pop().ival();
        auto l = vm.Pop().vval();
        auto amount = max(n, (intp)1);
        if (n < 0 || amount > l->len || i < 0 || i > l->len - amount)
            vm.BuiltinError(cat("remove: index (", i, ") or n (", amount,
                                    ") out of range (", l->len, ")"));
        l->Remove(vm, i, amount, 1, true);
    });

nfr("remove_obj", "xs,obj", "A]*A1", "Ab2",
    "remove all elements equal to obj (==), returns obj.",
    [](VM &vm, Value &l, Value &o) {
        intp removed = 0;
        auto vt = vm.GetTypeInfo(l.vval()->ti(vm).subt).t;
        for (intp i = 0; i < l.vval()->len; i++) {
            auto e = l.vval()->At(i);
            if (e.Equal(vm, vt, o, vt, false)) {
                l.vval()->Remove(vm, i--, 1, 0, false);
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
    [](VM &vm, Value &l, Value &key) {
        auto r = BinarySearch(vm, l, key, IntCompare);
        return r;
    });

nfr("binary_search", "xs,key", "F]F", "II",
    "float version.",
    [](VM &vm, Value &l, Value &key) {
        auto r = BinarySearch(vm, l, key, FloatCompare);
        return r;
    });

nfr("binary_search", "xs,key", "S]S", "II",
    "string version.",
    [](VM &vm, Value &l, Value &key) {
        auto r = BinarySearch(vm, l, key, StringCompare);
        return r;
    });

nfr("copy", "xs", "A", "A1",
    "makes a shallow copy of any object.",
    [](VM &vm, Value &v) {
        return v.Copy(vm);
    });

nfr("slice", "xs,start,size", "A]*II", "A]1",
    "returns a sub-vector of size elements from index start."
    " size can be negative to indicate the rest of the vector.",
    [](VM &vm, Value &l, Value &s, Value &e) {
        auto size = e.ival();
        auto start = s.ival();
        if (size < 0) size = l.vval()->len - start;
        if (start < 0 || start + size > l.vval()->len)
            vm.BuiltinError("slice: values out of range");
        auto nv = (LVector *)vm.NewVec(0, size, l.vval()->tti);
        nv->Append(vm, l.vval(), start, size);
        return Value(nv);
    });

nfr("any", "xs", "I}", "B",
    "returns wether any elements of the numeric struct are true values",
    [](VM &vm) {
        auto r = false; 
        auto l = vm.Pop().ival(); 
        for (intp i = 0; i < l; i++) { 
            if (vm.Pop().True()) r = true;
        } 
        vm.Push(r);
    });

nfr("any", "xs", "A]*", "B",
    "returns wether any elements of the vector are true values",
    [](VM &, Value &v) {
        Value r(false); 
        intp l = v.vval()->len; 
        for (auto i = 0; i < l; i++) { 
            if (v.vval()->At(i).True()) { r = Value(true); break; } 
        } 
        return r; 
    });

nfr("all", "xs", "I}", "B",
    "returns wether all elements of the numeric struct are true values",
    [](VM &vm) {
        auto r = true;
        auto l = vm.Pop().ival();
        for (intp i = 0; i < l; i++) {
            if (!vm.Pop().True()) r = false;
        }
        vm.Push(r);
    });

nfr("all", "xs", "A]*", "B",
    "returns wether all elements of the vector are true values",
    [](VM &, Value &v) {
        Value r(true);
        for (intp i = 0; i < v.vval()->len; i++) {
            if (!v.vval()->At(i).True()) { r = Value(false); break; }
        }
        return r;
    });

nfr("substring", "s,start,size", "SII", "S",
    "returns a substring of size characters from index start."
    " size can be negative to indicate the rest of the string.",
    [](VM &vm, Value &l, Value &s, Value &e) {
        intp size = e.ival();
        intp start = s.ival();
        if (size < 0) size = l.sval()->len - start;
        if (start < 0 || start + size > l.sval()->len)
            vm.BuiltinError("substring: values out of range");

        auto ns = vm.NewString(string_view(l.sval()->data() + start, size));
        return Value(ns);
    });

nfr("string_to_int", "s", "S", "IB",
    "converts a string to an int. returns 0 if no numeric data could be parsed."
    "second return value is true if all characters of the string were parsed",
    [](VM &vm, Value &s) {
        char *end;
        auto sv = s.sval()->strv();
        auto i = parse_int<intp>(sv, 10, &end);
        vm.Push(i);
        return Value(end == sv.data() + sv.size());
    });

nfr("string_to_float", "s", "S", "F",
    "converts a string to a float. returns 0.0 if no numeric data could be parsed",
    [](VM &, Value &s) {
        auto f = strtod(s.sval()->data(), nullptr);
        return Value(f);
    });

nfr("tokenize", "s,delimiters,whitespace", "SSS", "S]",
    "splits a string into a vector of strings, by splitting into segments upon each dividing or"
    " terminating delimiter. Segments are stripped of leading and trailing whitespace."
    " Example: \"; A ; B C; \" becomes [ \"\", \"A\", \"B C\" ] with \";\" as delimiter and"
    " \" \" as whitespace.",
    [](VM &vm, Value &s, Value &delims, Value &whitespace) {
        auto v = (LVector *)vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);
        auto ws = whitespace.sval()->strv();
        auto dl = delims.sval()->strv();
        auto p = s.sval()->strv();
        p.remove_prefix(min(p.find_first_not_of(ws), p.size()));
        while (!p.empty()) {
            auto delim = min(p.find_first_of(dl), p.size());
            auto end = min(p.find_last_not_of(ws) + 1, delim);
            v->Push(vm, vm.NewString(string_view(p.data(), end)));
            p.remove_prefix(delim);
            p.remove_prefix(min(p.find_first_not_of(dl), p.size()));
            p.remove_prefix(min(p.find_first_not_of(ws), p.size()));
        }
        return Value(v);
    });

nfr("unicode_to_string", "us", "I]", "S",
    "converts a vector of ints representing unicode values to a UTF-8 string.",
    [](VM &vm, Value &v) {
        char buf[7];
        string s;
        for (intp i = 0; i < v.vval()->len; i++) {
            auto &c = v.vval()->At(i);
            ToUTF8((int)c.ival(), buf);
            s += buf;
        }
        return Value(vm.NewString(s));
    });

nfr("string_to_unicode", "s", "S", "I]?",
    "converts a UTF-8 string into a vector of unicode values, or nil upon a decoding error",
    [](VM &vm, Value &s) {
        auto v = (LVector *)vm.NewVec(0, s.sval()->len, TYPE_ELEM_VECTOR_OF_INT);
        auto p = s.sval()->strv();
        while (!p.empty()) {
            int u = FromUTF8(p);
            if (u < 0) return Value();
            v->Push(vm, u);
        }
        return Value(v);
    });

nfr("number_to_string", "number,base,minchars", "III", "S",
    "converts the (unsigned version) of the input integer number to a string given the base"
    " (2..36, e.g. 16 for hex) and outputting a minimum of characters (padding with 0).",
    [](VM &vm, Value &n, Value &b, Value &mc) {
        if (b.ival() < 2 || b.ival() > 36 || mc.ival() > 32)
            vm.BuiltinError("number_to_string: values out of range");
        auto i = (uintp)n.ival();
        string s;
        auto from = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        while (i || (intp)s.length() < mc.ival()) {
            s.insert(0, 1, from[i % b.ival()]);
            i /= b.ival();
        }
        return Value(vm.NewString(s));
    });

nfr("lowercase", "s", "S", "S",
    "converts a UTF-8 string from any case to lower case, affecting only A-Z",
    [](VM &vm, Value &s) {
        auto ns = vm.NewString(s.sval()->strv());
        for (auto &c : ns->strv()) {
            // This is unicode-safe, since all unicode chars are in bytes >= 128
            if (c >= 'A' && c <= 'Z') (char &)c += 'a' - 'A';
        }
        return Value(ns);
    });

nfr("uppercase", "s", "S", "S",
    "converts a UTF-8 string from any case to upper case, affecting only a-z",
    [](VM &vm, Value &s) {
        auto ns = vm.NewString(s.sval()->strv());
        for (auto &c : ns->strv()) {
            // This is unicode-safe, since all unicode chars are in bytes >= 128
            if (c >= 'a' && c <= 'z') (char &)c -= 'a' - 'A';
        }
        return Value(ns);
    });

nfr("escape_string", "s,set,prefix,postfix", "SSSS", "S",
    "prefixes & postfixes any occurrences or characters in set in string s",
    [](VM &vm, Value &s, Value &set, Value &prefix, Value &postfix) {
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
    [](VM &vm, Value &v, Value &sep) {
        string s;
        auto sepsv = sep.sval()->strv();
        for (intp i = 0; i < v.vval()->len; i++) {
            if (i) s.append(sepsv);
            auto esv = v.vval()->At(i).sval()->strv();
            s.append(esv);
        }
        return Value(vm.NewString(s));
    });

nfr("repeat_string", "s,n", "SI", "S",
    "returns a string consisting of n copies of the input string.",
    [](VM &vm, Value &s, Value &_n) {
        auto n = max(intp(0), _n.ival());
        auto len = s.sval()->len;
        auto ns = vm.NewString(len * n);
        for (intp i = 0; i < n; i++) memcpy((char *)ns->data() + i * len, s.sval()->data(), len);
        return Value(ns);
    });

#define VECTOROPT(op, typeinfo) \
    auto len = vm.Pop().ival(); \
    auto elems = vm.TopPtr() - len; \
    for (intp i = 0; i < len; i++) { \
        auto f = elems[i]; \
        elems[i] = Value(op); \
    }
#define VECTOROP(op) VECTOROPT(op, a.oval()->tti)
#define VECTOROPI(op) VECTOROPT(op, vm.GetIntVectorType((int)len))
#define VECTOROPF(op) VECTOROPT(op, vm.GetFloatVectorType((int)len))

nfr("pow", "a,b", "FF", "F",
    "a raised to the power of b",
    [](VM &, Value &a, Value &b) { return Value(pow(a.fval(), b.fval())); });

nfr("pow", "a,b", "II", "I",
    "a raised to the power of b, for integers, using exponentiation by squaring",
    [](VM &, Value &a, Value &b) {
        return Value(b.ival() >= 0 ? ipow<intp>(a.ival(), b.ival()) : 0);
    });

nfr("pow", "a,b", "F}F", "F}",
    "vector elements raised to the power of b",
    [](VM &vm) {
        auto exp = vm.Pop().fval();
        VECTOROPF(pow(f.fval(), exp));
    });

nfr("log", "a", "F", "F",
    "natural logaritm of a",
    [](VM &, Value &a) { return Value(log(a.fval())); });

nfr("sqrt", "f", "F", "F",
    "square root",
    [](VM &, Value &a) { return Value(sqrt(a.fval())); });

nfr("ceiling", "f", "F", "I",
    "the nearest int >= f",
    [](VM &, Value &a) { return Value(fceil(a.fval())); });
nfr("ceiling", "v", "F}", "I}",
    "the nearest ints >= each component of v",
    [](VM &vm) { VECTOROPI(intp(fceil(f.fval()))); });

nfr("floor", "f", "F", "I",
    "the nearest int <= f",
    [](VM &, Value &a) { return Value(ffloor(a.fval())); });
nfr("floor", "v", "F}", "I}",
    "the nearest ints <= each component of v",
    [](VM &vm) { VECTOROPI(ffloor(f.fval())); });

nfr("int", "f", "F", "I",
    "converts a float to an int by dropping the fraction",
    [](VM &, Value &a) { return Value(intp(a.fval())); });
nfr("int", "v", "F}", "I}",
    "converts a vector of floats to ints by dropping the fraction",
    [](VM &vm) { VECTOROPI(intp(f.fval())); });

nfr("round", "f", "F", "I",
    "converts a float to the closest int. same as int(f + 0.5), so does not work well on"
    " negative numbers",
    [](VM &, Value &a) { return Value(intp(a.fval() + 0.5f)); });
nfr("round", "v", "F}", "I}",
    "converts a vector of floats to the closest ints",
    [](VM &vm) { VECTOROPI(intp(f.fval() + 0.5f)); });

nfr("fraction", "f", "F", "F",
    "returns the fractional part of a float: short for f - int(f)",
    [](VM &, Value &a) { return Value(a.fval() - int(a.fval())); });
nfr("fraction", "v", "F}", "F}",
    "returns the fractional part of a vector of floats",
    [](VM &vm) { VECTOROP(f.fval() - int(f.fval())); });

nfr("float", "i", "I", "F",
    "converts an int to float",
    [](VM &, Value &a) { return Value(floatp(a.ival())); });
nfr("float", "v", "I}", "F}",
    "converts a vector of ints to floats",
    [](VM &vm) { VECTOROPF(floatp(f.ival())); });

nfr("sin", "angle", "F", "F",
    "the y coordinate of the normalized vector indicated by angle (in degrees)",
    [](VM &, Value &a) { return Value(sin(a.fval() * RAD)); });
nfr("cos", "angle", "F", "F",
    "the x coordinate of the normalized vector indicated by angle (in degrees)",
    [](VM &, Value &a) { return Value(cos(a.fval() * RAD)); });
nfr("tan", "angle", "F", "F",
    "the tangent of an angle (in degrees)",
    [](VM &, Value &a) { return Value(tan(a.fval() * RAD)); });

nfr("sincos", "angle", "F", "F}:2",
    "the normalized vector indicated by angle (in degrees), same as xy { cos(angle), sin(angle) }",
    [](VM &vm) {
        auto a = vm.Pop().fval();
        vm.PushVec(floatp2(cos(a * RAD), sin(a * RAD)));
    });

nfr("asin", "y", "F", "F",
    "the angle (in degrees) indicated by the y coordinate projected to the unit circle",
    [](VM &, Value &y) { return Value(asin(y.fval()) / RAD); });
nfr("acos", "x", "F", "F",
    "the angle (in degrees) indicated by the x coordinate projected to the unit circle",
    [](VM &, Value &x) { return Value(acos(x.fval()) / RAD); });
nfr("atan", "x", "F", "F",
    "the angle (in degrees) indicated by the y coordinate of the tangent projected to the unit circle",
    [](VM &, Value &x) { return Value(atan(x.fval()) / RAD); });

nfr("radians", "angle", "F", "F",
    "converts an angle in degrees to radians",
    [](VM &, Value &a) { return Value(a.fval() * RAD); });
nfr("degrees", "angle", "F", "F",
    "converts an angle in radians to degrees",
    [](VM &, Value &a) { return Value(a.fval() / RAD); });

nfr("atan2", "vec", "F}" , "F",
    "the angle (in degrees) corresponding to a normalized 2D vector",
    [](VM &vm) {
        auto v = vm.PopVec<floatp2>();
        vm.Push(atan2(v.y, v.x) / RAD);
    });

nfr("radians", "angle", "F", "F",
    "converts an angle in degrees to radians",
    [](VM &, Value &a) { return Value(a.fval() * RAD); });
nfr("degrees", "angle", "F", "F",
    "converts an angle in radians to degrees",
    [](VM &, Value &a) { return Value(a.fval() / RAD); });

nfr("normalize", "vec",  "F}" , "F}",
    "returns a vector of unit length",
    [](VM &vm) {
        switch (vm.Top().ival()) {
            case 2: {
                auto v = vm.PopVec<floatp2>();
                vm.PushVec(v == floatp2_0 ? v : normalize(v));
                break;
            }
            case 3: {
                auto v = vm.PopVec<floatp3>();
                vm.PushVec(v == floatp3_0 ? v : normalize(v));
                break;
            }
            case 4: {
                auto v = vm.PopVec<floatp4>();
                vm.PushVec(v == floatp4_0 ? v : normalize(v));
                break;
            }
            default:
                assert(false);
        }
    });

nfr("dot", "a,b", "F}F}", "F",
    "the length of vector a when projected onto b (or vice versa)",
    [](VM &vm) {
        auto b = vm.PopVec<floatp4>();
        auto a = vm.PopVec<floatp4>();
        vm.Push(dot(a, b));
    });

nfr("magnitude", "v", "F}", "F",
    "the geometric length of a vector",
    [](VM &vm) {
        auto a = vm.PopVec<floatp4>();
        vm.Push(length(a));
    });

nfr("manhattan", "v", "I}", "I",
    "the manhattan distance of a vector",
    [](VM &vm) {
        auto a = vm.PopVec<intp4>();
        vm.Push(manhattan(a));
    });

nfr("cross", "a,b", "F}:3F}:3", "F}:3",
    "a perpendicular vector to the 2D plane defined by a and b (swap a and b for its inverse)",
    [](VM &vm) {
        auto b = vm.PopVec<floatp3>();
        auto a = vm.PopVec<floatp3>();
        vm.PushVec(cross(a, b));
    });

nfr("rnd", "max", "I", "I",
    "a random value [0..max).",
    [](VM &, Value &a) { return Value(rnd(max(1, (int)a.ival()))); });
nfr("rnd", "max", "I}", "I}",
    "a random vector within the range of an input vector.",
    [](VM &vm) { VECTOROP(rnd(max(1, (int)f.ival()))); });
nfr("rnd_float", "", "", "F",
    "a random float [0..1)",
    [](VM &) { return Value(rnd.rnddouble()); });
nfr("rnd_gaussian", "", "", "F",
    "a random float in a gaussian distribution with mean 0 and stddev 1",
    [](VM &) { return Value(rnd.rnd_gaussian()); });
nfr("rnd_seed", "seed", "I", "",
    "explicitly set a random seed for reproducable randomness",
    [](VM &, Value &seed) { rnd.seed((int)seed.ival()); return Value(); });

nfr("div", "a,b", "II", "F",
    "forces two ints to be divided as floats",
    [](VM &, Value &a, Value &b) { return Value(floatp(a.ival()) / floatp(b.ival())); });

nfr("clamp", "x,min,max", "III", "I",
    "forces an integer to be in the range between min and max (inclusive)",
    [](VM &, Value &a, Value &b, Value &c) {
        return Value(geom::clamp(a.ival(), b.ival(), c.ival()));
    });

nfr("clamp", "x,min,max", "FFF", "F",
    "forces a float to be in the range between min and max (inclusive)",
    [](VM &, Value &a, Value &b, Value &c) {
        return Value(geom::clamp(a.fval(), b.fval(), c.fval()));
    });

nfr("clamp", "x,min,max", "I}I}I}", "I}",
    "forces an integer vector to be in the range between min and max (inclusive)",
    [](VM &vm) {
        auto l = vm.Top().intval();
        auto c = vm.PopVec<intp4>();
        auto b = vm.PopVec<intp4>();
        auto a = vm.PopVec<intp4>();
        vm.PushVec(geom::clamp(a, b, c), l);
    });

nfr("clamp", "x,min,max", "F}F}F}", "F}",
    "forces a float vector to be in the range between min and max (inclusive)",
    [](VM &vm) {
        auto l = vm.Top().intval();
        auto c = vm.PopVec<floatp4>();
        auto b = vm.PopVec<floatp4>();
        auto a = vm.PopVec<floatp4>();
        vm.PushVec(geom::clamp(a, b, c), l);
    });

nfr("in_range", "x,range,bias", "III?", "B",
    "checks if an integer is >= bias and < bias + range. Bias defaults to 0.",
    [](VM &, Value &x, Value &range, Value &bias) {
        return Value(x.ival() >= bias.ival() && x.ival() < bias.ival() + range.ival());
    });

nfr("in_range", "x,range,bias", "FFF?", "B",
    "checks if a float is >= bias and < bias + range. Bias defaults to 0.",
    [](VM &, Value &x, Value &range, Value &bias) {
        return Value(x.fval() >= bias.fval() && x.fval() < bias.fval() + range.fval());
    });

nfr("in_range", "x,range,bias", "I}I}I}?", "B",
    "checks if a 2d/3d integer vector is >= bias and < bias + range. Bias defaults to 0.",
    [](VM &vm) {
        auto bias  = vm.Top().True() ? vm.PopVec<intp3>() : (vm.Pop(), intp3_0);
        auto range = vm.PopVec<intp3>(1);
        auto x     = vm.PopVec<intp3>();
        vm.Push(x >= bias && x < bias + range);
    });

nfr("in_range", "x,range,bias", "F}F}F}?", "B",
    "checks if a 2d/3d float vector is >= bias and < bias + range. Bias defaults to 0.",
    [](VM &vm) {
        auto bias  = vm.Top().True() ? vm.PopVec<floatp3>() : (vm.Pop(), floatp3_0);
        auto range = vm.PopVec<floatp3>(1);
        auto x     = vm.PopVec<floatp3>();
        vm.Push(x >= bias && x < bias + range);
    });

nfr("abs", "x", "I", "I",
    "absolute value of an integer",
    [](VM &, Value &a) { return Value(abs(a.ival())); });
nfr("abs", "x", "F", "F",
    "absolute value of a float",
    [](VM &, Value &a) { return Value(fabs(a.fval())); });
nfr("abs", "x", "I}", "I}",
    "absolute value of an int vector",
    [](VM &vm) { VECTOROP(abs(f.ival())); });
nfr("abs", "x", "F}", "F}",
    "absolute value of a float vector",
    [](VM &vm) { VECTOROP(fabs(f.fval())); });

nfr("sign", "x", "I", "I",
    "sign (-1, 0, 1) of an integer",
    [](VM &, Value &a) { return Value(signum(a.ival())); });
nfr("sign", "x", "F", "I",
    "sign (-1, 0, 1) of a float",
    [](VM &, Value &a) { return Value(signum(a.fval())); });
nfr("sign", "x", "I}", "I}",
    "signs of an int vector",
    [](VM &vm) { VECTOROP(signum(f.ival())); });
nfr("sign", "x", "F}", "I}",
    "signs of a float vector",
    [](VM &vm) { VECTOROPI(signum(f.fval())); });

// FIXME: need to guarantee in typechecking that both vectors are the same len.
#define VECBINOP(name, T) \
    auto len = vm.Top().intval(); \
    auto y = vm.PopVec<T>(); \
    auto x = vm.PopVec<T>(); \
    vm.PushVec(name(x, y), len);

#define VECSCALAROP(type, init, fun, acc, len, at) \
    type v = init; \
    auto l = x.acc()->len; \
    for (intp i = 0; i < l; i++) { \
        auto f = x.acc()->at; \
        fun; \
    } \
    return Value(v);

#define STSCALAROP(type, init, fun) \
    type v = init; \
    auto l = vm.Pop().ival(); \
    for (intp i = 0; i < l; i++) { \
        auto f = vm.Pop(); \
        fun; \
    } \
    vm.Push(v);

nfr("min", "x,y", "II", "I",
    "smallest of 2 integers.",
    [](VM &, Value &x, Value &y) {
        return Value(min(x.ival(), y.ival()));
    });
nfr("min", "x,y", "FF", "F",
    "smallest of 2 floats.",
    [](VM &, Value &x, Value &y) {
        return Value(min(x.fval(), y.fval()));
    });
nfr("min", "x,y", "I}I}", "I}",
    "smallest components of 2 int vectors",
    [](VM &vm) {
        VECBINOP(min, intp4)
    });
nfr("min", "x,y", "F}F}", "F}",
    "smallest components of 2 float vectors",
    [](VM &vm) {
        VECBINOP(min, floatp4)
    });
nfr("min", "v", "I}", "I",
    "smallest component of a int vector.",
    [](VM &vm) {
        STSCALAROP(intp, INT_MAX, v = min(v, f.ival()))
    });
nfr("min", "v", "F}", "F",
    "smallest component of a float vector.",
    [](VM &vm) {
        STSCALAROP(floatp, FLT_MAX, v = min(v, f.fval()))
    });
nfr("min", "v", "I]", "I",
    "smallest component of a int vector, or INT_MAX if length 0.",
    [](VM &, Value &x) {
        VECSCALAROP(intp, INT_MAX, v = min(v, f.ival()), vval, len, At(i))
    });
nfr("min", "v", "F]", "F",
    "smallest component of a float vector, or FLT_MAX if length 0.",
    [](VM &, Value &x) {
        VECSCALAROP(floatp, FLT_MAX, v = min(v, f.fval()), vval, len, At(i))
    });

nfr("max", "x,y", "II", "I",
    "largest of 2 integers.",
    [](VM &, Value &x, Value &y) {
        return Value(max(x.ival(), y.ival()));
    });
nfr("max", "x,y", "FF", "F",
    "largest of 2 floats.",
    [](VM &, Value &x, Value &y) {
        return Value(max(x.fval(), y.fval()));
    });
nfr("max", "x,y", "I}I}", "I}",
    "largest components of 2 int vectors",
    [](VM &vm) {
        VECBINOP(max, intp4)
    });
nfr("max", "x,y", "F}F}", "F}",
    "largest components of 2 float vectors",
    [](VM &vm) {
        VECBINOP(max, floatp4)
    });
nfr("max", "v", "I}", "I",
    "largest component of a int vector.",
    [](VM &vm) {
        STSCALAROP(intp, INT_MIN, v = max(v, f.ival()))
    });
nfr("max", "v", "F}", "F",
    "largest component of a float vector.",
    [](VM &vm) {
        STSCALAROP(floatp, FLT_MIN, v = max(v, f.fval()))
    });
nfr("max", "v", "I]", "I",
    "largest component of a int vector, or INT_MIN if length 0.",
    [](VM &, Value &x) {
        VECSCALAROP(intp, INT_MIN, v = max(v, f.ival()), vval, len, At(i))
    });
nfr("max", "v", "F]", "F",
    "largest component of a float vector, or FLT_MIN if length 0.",
    [](VM &, Value &x) {
        VECSCALAROP(floatp, FLT_MIN, v = max(v, f.fval()), vval, len, At(i))
    });

nfr("lerp", "x,y,f", "FFF", "F",
    "linearly interpolates between x and y with factor f [0..1]",
    [](VM &, Value &x, Value &y, Value &f) {
        return Value(mix(x.fval(), y.fval(), (float)f.fval()));
    });

nfr("lerp", "a,b,f", "F}F}F", "F}",
    "linearly interpolates between a and b vectors with factor f [0..1]",
    [](VM &vm) {
        auto f = vm.Pop().fltval();
        auto numelems = vm.Top().intval();
        auto y = vm.PopVec<floatp4>();
        auto x = vm.PopVec<floatp4>();
        vm.PushVec(mix(x, y, f), numelems);
    });

nfr("cardinal_spline", "z,a,b,c,f,tension", "F}F}F}F}FF", "F}:3",
    "computes the position between a and b with factor f [0..1], using z (before a) and c"
    " (after b) to form a cardinal spline (tension at 0.5 is a good default)",
    [](VM &vm) {
        auto t = vm.Pop().fval();
        auto f = vm.Pop().fval();
        auto c = vm.PopVec<floatp3>();
        auto b = vm.PopVec<floatp3>();
        auto a = vm.PopVec<floatp3>();
        auto z = vm.PopVec<floatp3>();
        vm.PushVec(cardinal_spline(z, a, b, c, f, t));
    });

nfr("line_intersect", "line1a,line1b,line2a,line2b", "F}:2F}:2F}:2F}:2", "IF}:2",
    "computes if there is an intersection point between 2 line segments, with the point as"
    " second return value",
    [](VM &vm) {
        auto l2b = vm.PopVec<floatp2>();
        auto l2a = vm.PopVec<floatp2>();
        auto l1b = vm.PopVec<floatp2>();
        auto l1a = vm.PopVec<floatp2>();
        floatp2 ipoint(0, 0);
        auto r = line_intersect(l1a, l1b, l2a, l2b, &ipoint);
        vm.Push(r);
        vm.PushVec(ipoint);
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
    [](VM &vm) {
        auto ncelld = vm.PopVec<intp2>();
        auto radiuses2 = vm.Pop().vval();
        auto positions2 = vm.Pop().vval();
        auto radiuses1 = vm.Pop().vval();
        auto positions1 = vm.Pop().vval();
        if (!radiuses2->len) radiuses2 = radiuses1;
        if (!positions2->len) positions2 = positions1;
        auto qdist = vm.Pop().fval();
        if (ncelld.x <= 0 || ncelld.y <= 0)
            ncelld = intp2((intp)sqrtf(float(positions2->len + 1) * 4));
        if (radiuses1->len != positions1->len || radiuses2->len != positions2->len)
            vm.BuiltinError(
                "circles_within_range: input vectors size mismatch");
        struct Node { floatp2 pos; floatp rad; intp idx; Node *next; };
        vector<Node> nodes(positions2->len, Node());
        floatp maxrad = 0;
        floatp2 minpos = floatp2(FLT_MAX), maxpos(FLT_MIN);
        for (intp i = 0; i < positions2->len; i++) {
            auto &n = nodes[i];
            auto p = ValueToF<2>(positions2->AtSt(i), positions2->width);
            minpos = min(minpos, p);
            maxpos = max(maxpos, p);
            n.pos = p;
            auto r = radiuses2->At(i).fval();
            maxrad = max(maxrad, r);
            n.rad = r;
            n.idx = i;
            n.next = nullptr;
        }
        vector<Node *> cells(ncelld.x * ncelld.y, nullptr);
        auto wsize = maxpos - minpos;
        wsize *= 1.00001f;  // No objects may fall exactly on the far border.
        auto tocellspace = [&](const floatp2 &pos) {
            return intp2((pos - minpos) / wsize * floatp2(ncelld));
        };
        for (intp i = 0; i < positions2->len; i++) {
            auto &n = nodes[i];
            auto cp = tocellspace(n.pos);
            auto &c = cells[cp.x + cp.y * ncelld.x];
            n.next = c;
            c = &n;
        }
        vector<intp> within_range;
        vector<LVector *> results(positions1->len, nullptr);
        for (intp i = 0; i < positions1->len; i++) {
            auto pos = ValueToF<2>(positions1->AtSt(i), positions1->width);
            auto rad = radiuses1->At(i).fval();
            auto scanrad = rad + maxrad + qdist;
            auto minc = max(intp2_0, min(ncelld - 1, tocellspace(pos - scanrad)));
            auto maxc = max(intp2_0, min(ncelld - 1, tocellspace(pos + scanrad)));
            for (intp y = minc.y; y <= maxc.y; y++) {
                for (intp x = minc.x; x <= maxc.x; x++) {
                    for (auto c = cells[x + y * ncelld.x]; c; c = c->next) {
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
        vm.Push(rvec);
    });

nfr("wave_function_collapse", "tilemap,size", "S]I}:2", "S]I",
    "returns a tilemap of given size modelled after the possible shapes in the input"
    " tilemap. Tilemap should consist of chars in the 0..127 range. Second return value"
    " the number of failed neighbor matches, this should"
    " ideally be 0, but can be non-0 for larger maps. Simply call this function"
    " repeatedly until it is 0",
    [](VM &vm) {
        auto sz = vm.PopVec<int2>();
        auto tilemap = vm.Pop();
        auto rows = tilemap.vval()->len;
        vector<const char *> inmap(rows);
        intp cols = 0;
        for (intp i = 0; i < rows; i++) {
            auto sv = tilemap.vval()->At(i).sval()->strv();
            if (i) { if ((intp)sv.size() != cols) vm.Error("all columns must be equal length"); }
            else cols = sv.size();
            inmap[i] = sv.data();
        }
        auto outstrings = ToValueOfVectorOfStringsEmpty(vm, sz, 0);
        vector<char *> outmap(sz.y, nullptr);
        for (int i = 0; i < sz.y; i++) outmap[i] = (char *)outstrings.vval()->At(i).sval()->data();
        int num_contradictions = 0;
        auto ok = WaveFunctionCollapse(int2(intp2(cols, (intp)inmap.size())), inmap.data(), sz, outmap.data(),
                                        rnd, num_contradictions);
        if (!ok)
            vm.Error("tilemap contained too many tile ids");
        vm.Push(outstrings);
        vm.Push(num_contradictions);
    });

nfr("resume", "coroutine,return_value", "CkAk%?", "C?",
    "resumes execution of a coroutine, passing a value back or nil",
    [](VM &vm, Value &co, Value &ret) {
        vm.CoResume(co.cval());
        // By the time CoResume returns, we're now back in the context of co, meaning that the
        // return value below is what is returned from yield inside co.
        return ret;
        // The actual return value from this call to resume (in the caller) will be the coroutine
        // itself (which is holding the refcount while active, hence the "k").
        // The argument to the next call to yield (or the coroutine return value) will instead
        // be captured in the dormant coroutine stack and available over return_value() below.
    });

nfr("return_value", "coroutine", "C", "A1",
    "gets the last return value of a coroutine",
    [](VM &vm, Value &co) {
        Value &rv = co.cval()->Current(vm);
        return rv;
    });

nfr("active", "coroutine", "C", "B",
    "wether the given coroutine is still active",
    [](VM &, Value &co) {
        bool active = co.cval()->active;
        return Value(active);
    });

nfr("hash", "x", "L", "I",
    "hashes a function value into an int",
    [](VM &vm, Value &a) {
        auto h = a.Hash(vm, V_FUNCTION);
        return Value(h);
    });
nfr("hash", "x", "A", "I",
    "hashes any value into an int",
    [](VM &vm, Value &a) {
        auto h = a.ref()->Hash(vm);
        return Value(h);
    });

nfr("program_name", "", "", "S",
    "returns the name of the main program (e.g. \"foo.lobster\".",
    [](VM &vm) {
        return Value(vm.NewString(vm.GetProgramName()));
    });

nfr("vm_compiled_mode", "", "", "B",
    "returns if the VM is running in compiled mode (Lobster -> C++).",
    [](VM &) {
        return Value(
            #ifdef VM_COMPILED_CODE_MODE
                true
            #else
                false
            #endif
        );
    });

nfr("seconds_elapsed", "", "", "F",
    "seconds since program start as a float, unlike gl_time() it is calculated every time it is"
    " called",
    [](VM &vm) {
        return Value(vm.Time());
    });

nfr("assert", "condition", "A*", "Ab1",
    "halts the program with an assertion failure if passed false. returns its input",
    [](VM &vm, Value &c) {
        if (!c.True()) vm.BuiltinError("assertion failed");
        return c;
    });

nfr("trace_bytecode", "mode", "I", "",
    "tracing shows each bytecode instruction as it is being executed, not very useful unless"
    " you are trying to isolate a compiler bug. Mode is off(0), on(1) or tail only (2)",
    [](VM &vm, Value &i) {
        vm.Trace((TraceMode)i.ival());
        return Value();
    });

nfr("set_max_stack_size", "max", "I", "",
    "size in megabytes the stack can grow to before an overflow error occurs. defaults to 1",
    [](VM &vm, Value &max) {
        vm.SetMaxStack((int)max.ival() * 1024 * 1024 / sizeof(Value));
        return Value();
    });

nfr("reference_count", "val", "A", "I",
    "get the reference count of any value. for compiler debugging, mostly",
    [](VM &, Value &x) {
        auto refc = x.refnil() ? x.refnil()->refc - 1 : -1;
        return Value(refc);
    });

nfr("set_console", "on", "B", "",
    "lets you turn on/off the console window (on Windows)",
    [](VM &, Value &x) {
        SetConsole(x.True());
        return Value();
    });

nfr("command_line_arguments", "", "", "S]",
    "",
    [](VM &vm) {
        return ToValueOfVectorOfStrings(vm, vm.program_args);
    });

nfr("thread_information", "", "", "II",
    "returns the number of hardware threads, and the number of cores",
    [](VM &vm) {
        vm.Push(NumHWThreads());
        return Value(NumHWCores());
    });

nfr("is_worker_thread", "", "", "B",
    "wether the current thread is a worker thread",
    [](VM &vm) {
        return Value(vm.is_worker);
    });

nfr("start_worker_threads", "numthreads", "I", "",
    "launch worker threads",
    [](VM &vm, Value &n) {
        vm.StartWorkers(n.ival());
        return Value();
    });

nfr("stop_worker_threads", "", "", "",
    "only needs to be called if you want to stop the worker threads before the end of"
            " the program, or if you want to call start_worker_threads again. workers_alive"
            " will become false inside the workers, which should then exit.",
    [](VM &vm) {
        vm.TerminateWorkers();
        return Value();
    });

nfr("workers_alive", "", "", "B",
    "wether workers should continue doing work. returns false after"
            " stop_worker_threads() has been called.",
    [](VM &vm) {
        return Value(vm.tuple_space && vm.tuple_space->alive);
    });

nfr("thread_write", "struct", "A", "",
    "put this struct in the thread queue",
    [](VM &vm, Value &s) {
        vm.WorkerWrite(s.refnil());
        return Value();
    });

nfr("thread_read", "type", "T", "A1?",
    "get a struct from the thread queue. pass the typeof struct. blocks if no such"
            "structs available. returns struct, or nil if stop_worker_threads() was called",
    [](VM &vm, Value &t) {
        return Value(vm.WorkerRead((type_elem_t)t.ival()));
    });

nfr("log_frame", "", "", "",
    "call this function instead of gl_frame() or gl_log_frame() to simulate a frame based program"
    " from non-graphical code.",
    [](VM &vm) {
        vm.vml.LogFrame();
        return Value();
    });

}  // AddBuiltins

}
