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

namespace lobster {

static RandomNumberGenerator<MersenneTwister> rnd;

static int IntCompare(const Value &a, const Value &b) {
    return a.ival() < b.ival() ? -1 : a.ival() > b.ival();
}

static int FloatCompare(const Value &a, const Value &b) {
    return a.fval() < b.fval() ? -1 : a.fval() > b.fval();
}

static int StringCompare(const Value &a, const Value &b) {
    return strcmp(a.sval()->str(), b.sval()->str());
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

Value ReplaceStruct(VM &vm, Value &l, Value &i, Value &a) {
    auto len = l.stval()->Len(vm);
    if ((uintp)i.ival() >= (uintp)len) vm.BuiltinError("replace: index out of range");
    auto nv = vm.NewStruct(len, l.stval()->tti);
    if (len) nv->Init(vm, &l.stval()->AtS(0), len, true);
    l.DECRT(vm);
    nv->DecS(vm, i.ival());
    nv->AtS(i.ival()) = a;
    return Value(nv);
}

void AddBuiltins(NativeRegistry &natreg) {
    STARTDECL(print) (VM &vm, Value &a) {
        vm.ss_reuse.str(string());
        vm.ss_reuse.clear();
        RefToString(vm, vm.ss_reuse, a.refnil(), vm.programprintprefs);
        Output(OUTPUT_PROGRAM, vm.ss_reuse.str());
        return a;
    }
    ENDDECL1(print, "x", "A", "A1",
        "output any value to the console (with linefeed). returns its argument.");

    STARTDECL(string) (VM &vm, Value &a) {
        if (a.ref() && a.ref()->tti == TYPE_ELEM_STRING) return a;
        vm.ss_reuse.str(string());
        vm.ss_reuse.clear();
        RefToString(vm, vm.ss_reuse, a.refnil(), vm.programprintprefs);
        auto str = vm.NewString(vm.ss_reuse.str());
        a.DECRT(vm);
        return str;
    }
    ENDDECL1(string, "x", "A", "S",
        "convert any value to string");

    STARTDECL(set_print_depth) (VM &vm, Value &a) {
        vm.programprintprefs.depth = a.ival();
        return Value();
    }
    ENDDECL1(set_print_depth, "depth", "I", "",
        "for printing / string conversion: sets max vectors/objects recursion depth (default 10)");

    STARTDECL(set_print_length) (VM &vm, Value &a) {
        vm.programprintprefs.budget = a.ival();
        return Value();
    }
    ENDDECL1(set_print_length, "len", "I", "",
        "for printing / string conversion: sets max string length (default 10000)");

    STARTDECL(set_print_quoted) (VM &vm, Value &a) {
        vm.programprintprefs.quoted = a.ival() != 0;
        return Value();
    }
    ENDDECL1(set_print_quoted, "quoted", "I", "",
        "for printing / string conversion: if the top level value is a string, whether to convert"
        " it with escape codes and quotes (default false)");

    STARTDECL(set_print_decimals) (VM &vm, Value &a) {
        vm.programprintprefs.decimals = a.ival();
        return Value();
    }
    ENDDECL1(set_print_decimals, "decimals", "I", "",
        "for printing / string conversion: number of decimals for any floating point output"
        " (default -1, meaning all)");

    STARTDECL(getline) (VM &vm) {
        const int MAXSIZE = 1000;
        char buf[MAXSIZE];
        if (!fgets(buf, MAXSIZE, stdin)) buf[0] = 0;
        buf[MAXSIZE - 1] = 0;
        for (int i = 0; i < MAXSIZE; i++) if (buf[i] == '\n') { buf[i] = 0; break; }
        return Value(vm.NewString(buf));
    }
    ENDDECL0(getline, "", "", "S",
        "reads a string from the console if possible (followed by enter)");

    STARTDECL(if) (VM &, Value &c, Value &t, Value &e) {
        assert(0);  // Special case implementation in the VM
        (void)c;
        (void)t;
        (void)e;
        return Value();
    }
    ENDDECL3(if, "cond,then,else", "ACC?", "A",
        "evaluates then or else depending on cond, else is optional");

    STARTDECL(while) (VM &, Value &c, Value &b) {
        assert(0);  // Special case implementation in the VM
        (void)c;
        (void)b;
        return Value();
    }
    ENDDECL2(while, "cond,do", "C@C", "A",
        "evaluates body while cond (converted to a function) holds true, returns last body value");

    STARTDECL(for) (VM &, Value &iter, Value &body) {
        assert(0);  // Special case implementation in the VM
        (void)iter;
        (void)body;
        return Value();
    }
    ENDDECL2(for, "iter,do", "AC", "",
        "iterates over int/vector/string, body may take [ element [ , index ] ] arguments");

    STARTDECL(append) (VM &vm, Value &v1, Value &v2) {
        auto type = v1.vval()->tti;
        auto nv = (LVector *)vm.NewVec(0, v1.vval()->len + v2.vval()->len, type);
        nv->Append(vm, v1.vval(), 0, v1.vval()->len); v1.DECRT(vm);
        nv->Append(vm, v2.vval(), 0, v2.vval()->len); v2.DECRT(vm);
        return Value(nv);
    }
    ENDDECL2(append, "xs,ys", "V*V*1", "V1",
        "creates a new vector by appending all elements of 2 input vectors");

    STARTDECL(vector_reserve) (VM &vm, Value &type, Value &len) {
        return Value(vm.NewVec(0, len.ival(), (type_elem_t)type.ival()));
    }
    ENDDECL2(vector_reserve, "typeid,len", "TI", "V*",
        "creates a new empty vector much like [] would, except now ensures"
        " it will have space for len push() operations without having to reallocate."
        " pass \"typeof return\" as typeid.");

    STARTDECL(length) (VM &, Value &a) {
        return a;
    }
    ENDDECL1(length, "x", "I", "I",
        "length of int (identity function, useful in combination with string/vector version)");

    STARTDECL(length) (VM &vm, Value &a) {
        auto len = a.sval()->len;
        a.DECRT(vm);
        return Value(len);
    }
    ENDDECL1(length, "s", "S", "I",
        "length of string");

    STARTDECL(length) (VM &vm, Value &a) {
        auto len = a.stval()->Len(vm);
        a.DECRT(vm);
        return Value(len);
    }
    ENDDECL1(length, "s", "F}", "I",
        "number of fields in a numerical struct");

    STARTDECL(length) (VM &vm, Value &a) {
        auto len = a.stval()->Len(vm);
        a.DECRT(vm);
        return Value(len);
    }
    ENDDECL1(length, "s", "I}", "I",
        "number of fields in a numerical struct");

    STARTDECL(length) (VM &vm, Value &a) {
        auto len = a.vval()->len;
        a.DECRT(vm);
        return Value(len);
    }
    ENDDECL1(length, "xs", "V*", "I",
        "length of vector");

    STARTDECL(equal) (VM &vm, Value &a, Value &b) {
        bool eq = RefEqual(vm, a.refnil(), b.refnil(), true);
        a.DECRTNIL(vm);
        b.DECRTNIL(vm);
        return Value(eq);
    }
    ENDDECL2(equal, "a,b", "AA", "I",
        "structural equality between any two values (recurses into vectors/objects,"
        " unlike == which is only true for vectors/objects if they are the same object)");

    STARTDECL(push) (VM &vm, Value &l, Value &x) {
        l.vval()->Push(vm, x);
        return l;
    }
    ENDDECL2(push, "xs,x", "V*A1", "V1",
        "appends one element to a vector, returns existing vector");

    STARTDECL(pop) (VM &vm, Value &l) {
        if (!l.vval()->len) { l.DECRT(vm); vm.BuiltinError("pop: empty vector"); }
        auto v = l.vval()->Pop();
        l.DECRT(vm);
        return v;
    }
    ENDDECL1(pop, "xs", "V*", "A1",
        "removes last element from vector and returns it");

    STARTDECL(top) (VM &vm, Value &l) {
        if (!l.vval()->len) { l.DECRT(vm); vm.BuiltinError("top: empty vector"); }
        auto v = l.vval()->Top(vm);
        l.DECRT(vm);
        return v;
    }
    ENDDECL1(top, "xs", "V*", "A1",
        "returns last element from vector");

    STARTDECL(replace) (VM &vm, Value &l, Value &i, Value &a) {
        return ReplaceStruct(vm, l, i, a);
    }
    ENDDECL3(replace, "xs,i,x", "F}IF", "F}",
        "returns a copy of a numeric struct with the element at i replaced by x");

    STARTDECL(replace) (VM &vm, Value &l, Value &i, Value &a) {
        return ReplaceStruct(vm, l, i, a);
    }
    ENDDECL3(replace, "xs,i,x", "I}II", "I}",
        "returns a copy of a numeric struct with the element at i replaced by x");

    // FIXME: duplication with ReplaceStruct.
    STARTDECL(replace) (VM &vm, Value &l, Value &i, Value &a) {
        auto len = l.vval()->len;
        if ((uintp)i.ival() >= (uintp)len) vm.BuiltinError("replace: index out of range");
        auto nv = vm.NewVec(len, len, l.vval()->tti);
        if (len) nv->Init(vm, &l.vval()->At(0), true);
        l.DECRT(vm);
        nv->Dec(vm, i.ival(), nv->ElemType(vm));
        nv->At(i.ival()) = a;
        return Value(nv);
    }
    ENDDECL3(replace, "xs,i,x", "V*IA1", "V1",
        "returns a copy of a vector with the element at i replaced by x");

    STARTDECL(insert) (VM &vm, Value &l, Value &i, Value &a) {
        if (i.ival() < 0 || i.ival() > l.vval()->len)
            vm.BuiltinError("insert: index or n out of range");  // note: i==len is legal
        l.vval()->Insert(vm, a, i.ival());
        return l;
    }
    ENDDECL3(insert, "xs,i,x", "V*IA1", "V1",
        "inserts a value into a vector at index i, existing elements shift upward,"
        " returns original vector");

    STARTDECL(remove) (VM &vm, Value &l, Value &i, Value &n) {
        auto amount = max(n.ival(), (intp)1);
        if (n.ival() < 0 || amount > l.vval()->len || i.ival() < 0 ||
            i.ival() > l.vval()->len - amount)
            vm.BuiltinError(cat("remove: index (", i.ival(), ") or n (", amount,
                                   ") out of range (", l.vval()->len, ")"));
        auto v = l.vval()->Remove(vm, i.ival(), amount, 1);
        l.DECRT(vm);
        return v;
    }
    ENDDECL3(remove, "xs,i,n", "V*II?", "A1",
        "remove element(s) at index i, following elements shift down. pass the number of elements"
        " to remove as an optional argument, default 1. returns the first element removed.");

    STARTDECL(removeobj) (VM &vm, Value &l, Value &o) {
        intp removed = 0;
        auto vt = vm.GetTypeInfo(l.vval()->ti(vm).subt).t;
        for (intp i = 0; i < l.vval()->len; i++) {
            auto e = l.vval()->At(i);
            if (e.Equal(vm, vt, o, vt, false)) {
                l.vval()->Remove(vm, i--, 1, 0);
                removed++;
            }
        }
        l.DECRT(vm);
        return o;
    }
    ENDDECL2(removeobj, "xs,obj", "V*A1", "A2",
        "remove all elements equal to obj (==), returns obj.");

    STARTDECL(binarysearch) (VM &vm, Value &l, Value &key) {
        auto r = BinarySearch(vm, l, key, IntCompare);
        l.DECRT(vm);
        return r;
    }
    ENDDECL2(binarysearch, "xs,key", "I]I", "II",
        "does a binary search for key in a sorted vector, returns as first return value how many"
        " matches were found, and as second the index in the array where the matches start (so you"
        " can read them, overwrite them, or remove them), or if none found, where the key could be"
        " inserted such that the vector stays sorted. This overload is for int vectors and keys.");

    STARTDECL(binarysearch) (VM &vm, Value &l, Value &key) {
        auto r = BinarySearch(vm, l, key, FloatCompare);
        l.DECRT(vm);
        return r;
    }
    ENDDECL2(binarysearch, "xs,key", "F]F", "II",
        "float version.");

    STARTDECL(binarysearch) (VM &vm, Value &l, Value &key) {
        auto r = BinarySearch(vm, l, key, StringCompare);
        l.DECRT(vm);
        key.DECRT(vm);
        return r;
    }
    ENDDECL2(binarysearch, "xs,key", "S]S", "II",
        "string version.");

    STARTDECL(copy) (VM &vm, Value &v) {
        return v.Copy(vm);
    }
    ENDDECL1(copy, "xs", "A", "A1",
        "makes a shallow copy of any object.");

    STARTDECL(slice) (VM &vm, Value &l, Value &s, Value &e) {
        auto size = e.ival();
        if (size < 0) size = l.vval()->len + size;
        auto start = s.ival();
        if (start < 0) start = l.vval()->len + start;
        if (start < 0 || start + size > l.vval()->len)
            vm.BuiltinError("slice: values out of range");
        auto nv = (LVector *)vm.NewVec(0, size, l.vval()->tti);
        nv->Append(vm, l.vval(), start, size);
        l.DECRT(vm);
        return Value(nv);
    }
    ENDDECL3(slice,
        "xs,start,size", "V*II", "V1", "returns a sub-vector of size elements from index start."
        " start & size can be negative to indicate an offset from the vector length.");

    #define ANY_F(acc, at, len) \
        Value r(false); \
        auto l = v.acc()->len; \
        for (auto i = 0; i < l; i++) { \
            if (v.acc()->at.True()) { r = Value(true); break; } \
        } \
        v.DECRT(vm); \
        return r; \

    STARTDECL(any) (VM &vm, Value &v) {
        ANY_F(stval, AtS(i), Len(vm))
    }
    ENDDECL1(any, "xs", "I}", "I",
        "returns wether any elements of the numeric struct are true values");

    STARTDECL(any) (VM &vm, Value &v) {
        ANY_F(vval, At(i), len)
    }
    ENDDECL1(any, "xs", "V*", "I",
        "returns wether any elements of the vector are true values");

    STARTDECL(all) (VM &vm, Value &v) {
        Value r(true);
        auto l = v.stval()->Len(vm);
        for (intp i = 0; i < l; i++) {
            if (!v.stval()->AtS(i).True()) {
                r = Value(false);
                break;
            }
        }
        v.DECRT(vm);
        return r;
    }
    ENDDECL1(all, "xs", "I}", "I",
        "returns wether all elements of the numeric struct are true values");

    STARTDECL(all) (VM &vm, Value &v) {
        Value r(true);
        for (intp i = 0; i < v.vval()->len; i++) {
            if (!v.vval()->At(i).True()) {
                r = Value(false);
                break;
            }
        }
        v.DECRT(vm);
        return r;
    }
    ENDDECL1(all, "xs", "V*", "I",
        "returns wether all elements of the vector are true values");

    STARTDECL(substring) (VM &vm, Value &l, Value &s, Value &e) {
        intp size = e.ival();
        if (size < 0) size = l.sval()->len + size;
        intp start = s.ival();
        if (start < 0) start = l.sval()->len + start;
        if (start < 0 || start + size > l.sval()->len)
            vm.BuiltinError("substring: values out of range");

        auto ns = vm.NewString(string_view(l.sval()->str() + start, size));
        l.DECRT(vm);
        return Value(ns);
    }
    ENDDECL3(substring, "s,start,size", "SII", "S",
        "returns a substring of size characters from index start."
        " start & size can be negative to indicate an offset from the string length.");

    STARTDECL(string2int) (VM &vm, Value &s) {
        auto i = atoi(s.sval()->str());
        s.DECRT(vm);
        return Value(i);
    }
    ENDDECL1(string2int, "s", "S", "I",
        "converts a string to an int. returns 0 if no numeric data could be parsed");

    STARTDECL(string2float) (VM &vm, Value &s) {
        auto f = strtod(s.sval()->str(), nullptr);
        s.DECRT(vm);
        return Value(f);
    }
    ENDDECL1(string2float, "s", "S", "F",
        "converts a string to a float. returns 0.0 if no numeric data could be parsed");

    STARTDECL(tokenize) (VM &vm, Value &s, Value &delims, Value &whitespace) {
        auto v = (LVector *)vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);
        auto ws = whitespace.sval()->str();
        auto dl = delims.sval()->str();
        auto p = s.sval()->str();
        p += strspn(p, ws);
        auto strspn1 = [](char c, const char *set) {
            while (*set) if (*set == c) return 1;
            return 0;
        };
        while (*p) {
            auto delim = p + strcspn(p, dl);
            auto end = delim;
            while (end > p && strspn1(end[-1], ws)) end--;
            v->Push(vm, vm.NewString(string_view(p, end - p)));
            p = delim + strspn(delim, dl);
            p += strspn(p, ws);
        }
        s.DECRT(vm);
        delims.DECRT(vm);
        whitespace.DECRT(vm);
        return Value(v);
    }
    ENDDECL3(tokenize, "s,delimiters,whitespace", "SSS", "S]",
        "splits a string into a vector of strings, by splitting into segments upon each dividing or"
        " terminating delimiter. Segments are stripped of leading and trailing whitespace."
        " Example: \"; A ; B C; \" becomes [ \"\", \"A\", \"B C\" ] with \";\" as delimiter and"
        " \" \" as whitespace.");

    STARTDECL(unicode2string) (VM &vm, Value &v) {
        char buf[7];
        string s;
        for (intp i = 0; i < v.vval()->len; i++) {
            auto &c = v.vval()->At(i);
            ToUTF8((int)c.ival(), buf);
            s += buf;
        }
        v.DECRT(vm);
        return Value(vm.NewString(s));
    }
    ENDDECL1(unicode2string, "us", "I]", "S",
        "converts a vector of ints representing unicode values to a UTF-8 string.");

    STARTDECL(string2unicode) (VM &vm, Value &s) {
        auto v = (LVector *)vm.NewVec(0, s.sval()->len, TYPE_ELEM_VECTOR_OF_INT);
        const char *p = s.sval()->str();
        while (*p) {
            int u = FromUTF8(p);
            if (u < 0) { s.DECRT(vm); Value(v).DECRT(vm); return Value(); }
            v->Push(vm, u);
        }
        s.DECRT(vm);
        return Value(v);
    }
    ENDDECL1(string2unicode, "s", "S", "I]?",
        "converts a UTF-8 string into a vector of unicode values, or nil upon a decoding error");

    STARTDECL(number2string) (VM &vm, Value &n, Value &b, Value &mc) {
        if (b.ival() < 2 || b.ival() > 36 || mc.ival() > 32)
            vm.BuiltinError("number2string: values out of range");
        auto i = (uintp)n.ival();
        string s;
        const char *from = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        while (i || (intp)s.length() < mc.ival()) {
            s.insert(0, 1, from[i % b.ival()]);
            i /= b.ival();
        }
        return Value(vm.NewString(s));
    }
    ENDDECL3(number2string, "number,base,minchars", "III", "S",
        "converts the (unsigned version) of the input integer number to a string given the base"
        " (2..36, e.g. 16 for hex) and outputting a minimum of characters (padding with 0).");

    STARTDECL(lowercase) (VM &vm, Value &s) {
        auto ns = vm.NewString(s.sval()->strv());
        for (auto p = ns->str(); *p; p++) {
            // This is unicode-safe, since all unicode chars are in bytes >= 128
            if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
        }
        s.DECRT(vm);
        return Value(ns);
    }
    ENDDECL1(lowercase, "s", "S", "S",
             "converts a UTF-8 string from any case to lower case, affecting only A-Z");

    STARTDECL(uppercase) (VM &vm, Value &s) {
        auto ns = vm.NewString(s.sval()->strv());
        for (auto p = ns->str(); *p; p++) {
            // This is unicode-safe, since all unicode chars are in bytes >= 128
            if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
        }
        s.DECRT(vm);
        return Value(ns);
    }
    ENDDECL1(uppercase, "s", "S", "S",
             "converts a UTF-8 string from any case to upper case, affecting only a-z");

    STARTDECL(escapestring) (VM &vm, Value &s, Value &set, Value &prefix, Value &postfix) {
        string out;
        for (auto p = s.sval()->str();;) {
            auto loc = strpbrk(p, set.sval()->str());
            if (loc) {
                out.append(p, loc);
                auto presv = prefix.sval()->strv();
                out.append(presv.data(), presv.size());
                out += *loc++;
                auto postsv = postfix.sval()->strv();
                out.append(postsv.data(), postsv.size());
                p = loc;
            } else {
                out += p;
                break;
            }
        }
        s.DECRT(vm);
        set.DECRT(vm);
        prefix.DECRT(vm);
        postfix.DECRT(vm);
        return Value(vm.NewString(out));
    }
    ENDDECL4(escapestring, "s,set,prefix,postfix", "SSSS", "S",
             "prefixes & postfixes any occurrences or characters in set in string s");

    STARTDECL(concatstring) (VM &vm, Value &v, Value &sep) {
        string s;
        auto sepsv = sep.sval()->strv();
        for (intp i = 0; i < v.vval()->len; i++) {
            if (i) s.append(sepsv.data(), sepsv.size());
            auto esv = v.vval()->At(i).sval()->strv();
            s.append(esv.data(), esv.size());
        }
        v.DECRT(vm);
        sep.DECRT(vm);
        return Value(vm.NewString(s));
    }
    ENDDECL2(concatstring, "v,sep", "S]S", "S",
             "concatenates all elements of the string vector, separated with sep.");

    STARTDECL(pow) (VM &, Value &a, Value &b) { return Value(pow(a.fval(), b.fval())); }
    ENDDECL2(pow, "a,b", "FF", "F",
        "a raised to the power of b");

    STARTDECL(log) (VM &, Value &a) { return Value(log(a.fval())); } ENDDECL1(log, "a", "F", "F",
        "natural logaritm of a");

    STARTDECL(sqrt) (VM &, Value &a) { return Value(sqrt(a.fval())); } ENDDECL1(sqrt, "f", "F", "F",
        "square root");

    #define VECTOROPT(op, typeinfo) \
        auto len = a.stval()->Len(vm); \
        auto v = vm.NewStruct(len, typeinfo); \
        for (intp i = 0; i < len; i++) { \
            auto f = a.stval()->AtS(i); \
            v->AtS(i) = Value(op); \
        } \
        a.DECRT(vm); \
        return Value(v);
    #define VECTOROP(op) VECTOROPT(op, a.stval()->tti)

    STARTDECL(ceiling) (VM &, Value &a) { return Value(fceil(a.fval())); }
    ENDDECL1(ceiling, "f", "F", "I",
        "the nearest int >= f");
    STARTDECL(ceiling) (VM &vm, Value &a) { VECTOROPT(intp(fceil(f.fval())), vm.GetIntVectorType((int)len)); }
    ENDDECL1(ceiling, "v", "F}", "I}",
        "the nearest ints >= each component of v");

    STARTDECL(floor) (VM &, Value &a) { return Value(ffloor(a.fval())); }
    ENDDECL1(floor, "f", "F", "I",
        "the nearest int <= f");
    STARTDECL(floor) (VM &vm, Value &a) { VECTOROPT(ffloor(f.fval()), vm.GetIntVectorType((int)len)); }
    ENDDECL1(floor, "v", "F}", "I}",
        "the nearest ints <= each component of v");

    STARTDECL(int) (VM &, Value &a) { return Value(intp(a.fval())); }
    ENDDECL1(int, "f", "F", "I",
        "converts a float to an int by dropping the fraction");
    STARTDECL(int) (VM &vm, Value &a) { VECTOROPT(intp(f.fval()), vm.GetIntVectorType((int)len)); }
    ENDDECL1(int, "v", "F}", "I}",
        "converts a vector of floats to ints by dropping the fraction");

    STARTDECL(round) (VM &, Value &a) { return Value(intp(a.fval() + 0.5f)); }
    ENDDECL1(round, "f", "F", "I",
        "converts a float to the closest int. same as int(f + 0.5), so does not work well on"
        " negative numbers");
    STARTDECL(round) (VM &vm, Value &a) { VECTOROPT(intp(f.fval() + 0.5f), vm.GetIntVectorType((int)len)); }
    ENDDECL1(round, "v", "F}", "I}",
        "converts a vector of floats to the closest ints");

    STARTDECL(fraction) (VM &, Value &a) { return Value(a.fval() - int(a.fval())); }
    ENDDECL1(fraction, "f", "F", "F",
        "returns the fractional part of a float: short for f - int(f)");
    STARTDECL(fraction) (VM &vm, Value &a) { VECTOROP(f.fval() - int(f.fval())); }
    ENDDECL1(fraction, "v", "F}", "F}",
        "returns the fractional part of a vector of floats");

    STARTDECL(float) (VM &, Value &a) { return Value(floatp(a.ival())); }
    ENDDECL1(float, "i", "I", "F",
        "converts an int to float");
    STARTDECL(float) (VM &vm, Value &a) { VECTOROPT(floatp(f.ival()), vm.GetFloatVectorType((int)len)); }
    ENDDECL1(float, "v", "I}", "F}",
        "converts a vector of ints to floats");

    STARTDECL(sin) (VM &, Value &a) { return Value(sin(a.fval() * RAD)); }
    ENDDECL1(sin, "angle", "F", "F",
        "the y coordinate of the normalized vector indicated by angle (in degrees)");
    STARTDECL(cos) (VM &, Value &a) { return Value(cos(a.fval() * RAD)); }
    ENDDECL1(cos, "angle", "F", "F",
        "the x coordinate of the normalized vector indicated by angle (in degrees)");
    STARTDECL(tan) (VM &, Value &a) { return Value(tan(a.fval() * RAD)); }
    ENDDECL1(tan, "angle", "F", "F",
        "the tangent of an angle (in degrees)");

    STARTDECL(sincos) (VM &vm, Value &a) {
        return ToValueF(vm, floatp2(cos(a.fval() * RAD), sin(a.fval() * RAD)));
    }
    ENDDECL1(sincos, "angle", "F", "F}:2",
        "the normalized vector indicated by angle (in degrees), same as [ cos(angle), sin(angle) ]");

    STARTDECL(arcsin) (VM &, Value &y) { return Value(asin(y.fval()) / RAD); }
    ENDDECL1(arcsin, "y", "F", "F",
        "the angle (in degrees) indicated by the y coordinate projected to the unit circle");
    STARTDECL(arccos) (VM &, Value &x) { return Value(acos(x.fval()) / RAD); }
    ENDDECL1(arccos, "x", "F", "F",
        "the angle (in degrees) indicated by the x coordinate projected to the unit circle");

    STARTDECL(atan2) (VM &vm, Value &vec) {
        auto v = ValueDecToF<3>(vm, vec); return Value(atan2(v.y, v.x) / RAD);
    }
    ENDDECL1(atan2, "vec", "F}" , "F",
        "the angle (in degrees) corresponding to a normalized 2D vector");

    STARTDECL(radians) (VM &, Value &a) { return Value(a.fval() * RAD); }
    ENDDECL1(radians, "angle", "F", "F",
        "converts an angle in degrees to radians");
    STARTDECL(degrees) (VM &, Value &a) { return Value(a.fval() / RAD); }
    ENDDECL1(degrees, "angle", "F", "F",
        "converts an angle in radians to degrees");

    STARTDECL(normalize) (VM &vm, Value &vec) {
        switch (vec.stval()->Len(vm)) {
            case 2: { auto v = ValueDecToF<2>(vm, vec);
                      return ToValueF(vm, v == floatp2_0 ? v : normalize(v)); }
            case 3: { auto v = ValueDecToF<3>(vm, vec);
                      return ToValueF(vm, v == floatp3_0 ? v : normalize(v)); }
            case 4: { auto v = ValueDecToF<4>(vm, vec);
                      return ToValueF(vm, v == floatp4_0 ? v : normalize(v)); }
            default: return vm.BuiltinError("normalize() only works on vectors of length 2 to 4");
        }
    }
    ENDDECL1(normalize, "vec",  "F}" , "F}",
        "returns a vector of unit length");

    STARTDECL(dot) (VM &vm, Value &a, Value &b) { return Value(dot(ValueDecToF<4>(vm, a), ValueDecToF<4>(vm, b))); }
    ENDDECL2(dot,   "a,b", "F}F}", "F",
        "the length of vector a when projected onto b (or vice versa)");

    STARTDECL(magnitude) (VM &vm, Value &a)  { return Value(length(ValueDecToF<4>(vm, a))); }
    ENDDECL1(magnitude, "v", "F}", "F",
        "the geometric length of a vector");

    STARTDECL(manhattan) (VM &vm, Value &a) { return Value(manhattan(ValueDecToI<4>(vm, a))); }
    ENDDECL1(manhattan, "v", "I}", "I",
        "the manhattan distance of a vector");

    STARTDECL(cross) (VM &vm, Value &a, Value &b) {
        return ToValueF(vm, cross(ValueDecToF<3>(vm, a), ValueDecToF<3>(vm, b)));
    }
    ENDDECL2(cross, "a,b", "F}:3F}:3", "F}:3",
        "a perpendicular vector to the 2D plane defined by a and b (swap a and b for its inverse)");

    STARTDECL(rnd) (VM &, Value &a) { return Value(rnd(max(1, (int)a.ival()))); }
    ENDDECL1(rnd, "max", "I", "I",
        "a random value [0..max).");
    STARTDECL(rnd) (VM &vm, Value &a) { VECTOROP(rnd(max(1, (int)f.ival()))); }
    ENDDECL1(rnd, "max", "I}", "I}",
        "a random vector within the range of an input vector.");
    STARTDECL(rndfloat) (VM &) { return Value(rnd.rnddouble()); }
    ENDDECL0(rndfloat, "", "", "F",
             "a random float [0..1)");
    STARTDECL(rndgaussian) (VM &) { return Value(rnd.rndgaussian()); }
    ENDDECL0(rndgaussian, "", "", "F",
             "a random float in a gaussian distribution with mean 0 and stddev 1");
    STARTDECL(rndseed) (VM &, Value &seed) { rnd.seed((int)seed.ival()); return Value(); }
    ENDDECL1(rndseed, "seed", "I", "",
        "explicitly set a random seed for reproducable randomness");

    STARTDECL(div) (VM &, Value &a, Value &b) { return Value(floatp(a.ival()) / floatp(b.ival())); }
    ENDDECL2(div, "a,b", "II", "F",
        "forces two ints to be divided as floats");

    STARTDECL(clamp) (VM &, Value &a, Value &b, Value &c) {
        return Value(geom::clamp(a.ival(), b.ival(), c.ival()));
    }
    ENDDECL3(clamp, "x,min,max", "III", "I",
        "forces an integer to be in the range between min and max (inclusive)");

    STARTDECL(clamp) (VM &, Value &a, Value &b, Value &c) {
        return Value(geom::clamp(a.fval(), b.fval(), c.fval()));
    }
    ENDDECL3(clamp, "x,min,max", "FFF", "F",
             "forces a float to be in the range between min and max (inclusive)");

    STARTDECL(clamp) (VM &vm, Value &a, Value &b, Value &c) {
        return ToValueI(vm, geom::clamp(ValueDecToI<4>(vm, a), ValueDecToI<4>(vm, b), ValueDecToI<4>(vm, c)),
                        a.stval()->Len(vm));
    }
    ENDDECL3(clamp, "x,min,max", "I}I}I}", "I}",
             "forces an integer vector to be in the range between min and max (inclusive)");

    STARTDECL(clamp) (VM &vm, Value &a, Value &b, Value &c) {
        return ToValueF(vm, geom::clamp(ValueDecToF<4>(vm, a), ValueDecToF<4>(vm, b), ValueDecToF<4>(vm, c)),
                        a.stval()->Len(vm));
    }
    ENDDECL3(clamp, "x,min,max", "F}F}F}", "F}",
             "forces a float vector to be in the range between min and max (inclusive)");

    STARTDECL(inrange) (VM &, Value &x, Value &range, Value &bias) {
        return Value(x.ival() >= bias.ival() && x.ival() < bias.ival() + range.ival());
    }
    ENDDECL3(inrange, "x,range,bias", "III?", "I",
             "checks if an integer is >= bias and < bias + range. Bias defaults to 0.");

    STARTDECL(inrange) (VM &vm, Value &xv, Value &rangev, Value &biasv) {
        auto x     = ValueDecToI<3>(vm, xv);
        auto range = ValueDecToI<3>(vm, rangev, 1);
        auto bias  = biasv.True() ? ValueDecToI<3>(vm, biasv) : intp3_0;
        return Value(x >= bias && x < bias + range);
    }
    ENDDECL3(inrange, "x,range,bias", "I}I}I}?", "I",
             "checks if a 2d/3d integer vector is >= bias and < bias + range. Bias defaults to 0.");

    STARTDECL(inrange) (VM &vm, Value &xv, Value &rangev, Value &biasv) {
        auto x     = ValueDecToF<3>(vm, xv);
        auto range = ValueDecToF<3>(vm, rangev, 1);
        auto bias  = biasv.True() ? ValueDecToF<3>(vm, biasv) : floatp3_0;
        return Value(x >= bias && x < bias + range);
    }
    ENDDECL3(inrange, "x,range,bias", "F}F}F}?", "I",
        "checks if a 2d/3d float vector is >= bias and < bias + range. Bias defaults to 0.");

    STARTDECL(abs) (VM &, Value &a) { return Value(abs(a.ival())); } ENDDECL1(abs, "x", "I", "I",
        "absolute value of an integer");
    STARTDECL(abs) (VM &, Value &a) { return Value(fabs(a.fval())); } ENDDECL1(abs, "x", "F", "F",
        "absolute value of a float");
    STARTDECL(abs) (VM &vm, Value &a) { VECTOROP(abs(f.ival())); } ENDDECL1(abs, "x", "I}", "I}",
        "absolute value of an int vector");
    STARTDECL(abs) (VM &vm, Value &a) { VECTOROP(fabs(f.fval())); } ENDDECL1(abs, "x", "F}", "F}",
        "absolute value of a float vector");

    // FIXME: need to guarantee this assert in typechecking
    #define VECBINOP(name,access) \
        auto len = x.stval()->Len(vm); \
        if (len != y.stval()->Len(vm)) vm.BuiltinError(#name "() arguments must be equal length"); \
        auto type = x.stval()->tti; \
        assert(type == y.stval()->tti); \
        auto v = vm.NewStruct(len, type); \
        for (intp i = 0; i < len; i++) { \
            v->AtS(i) = Value(name(x.stval()->AtS(i).access(), y.stval()->AtS(i).access())); \
        } \
        x.DECRT(vm); y.DECRT(vm); \
        return Value(v);

    #define VECSCALAROP(type, init, fun, acc, len, at) \
        type v = init; \
        auto l = x.acc()->len; \
        for (intp i = 0; i < l; i++) { \
            auto f = x.acc()->at; \
            fun; \
        } \
        x.DECRT(vm); \
        return Value(v);

    STARTDECL(min) (VM &, Value &x, Value &y) { return Value(min(x.ival(), y.ival())); }
    ENDDECL2(min, "x,y", "II", "I",
        "smallest of 2 integers.");
    STARTDECL(min) (VM &, Value &x, Value &y) { return Value(min(x.fval(), y.fval())); }
    ENDDECL2(min, "x,y", "FF", "F",
        "smallest of 2 floats.");
    STARTDECL(min) (VM &vm, Value &x, Value &y) { VECBINOP(min,ival) }
    ENDDECL2(min, "x,y", "I}I}", "I}",
        "smallest components of 2 int vectors");
    STARTDECL(min) (VM &vm, Value &x, Value &y) { VECBINOP(min,fval) }
    ENDDECL2(min, "x,y", "F}F}", "F}",
        "smallest components of 2 float vectors");
    STARTDECL(min) (VM &vm, Value &x) { VECSCALAROP(intp, INT_MAX, v = min(v, f.ival()), stval, Len(vm), AtS(i)) }
    ENDDECL1(min, "v", "I}", "I",
        "smallest component of a int vector.");
    STARTDECL(min) (VM &vm, Value &x) { VECSCALAROP(floatp, FLT_MAX, v = min(v, f.fval()), stval, Len(vm), AtS(i)) }
    ENDDECL1(min, "v", "F}", "F",
        "smallest component of a float vector.");
    STARTDECL(min) (VM &vm, Value &x) { VECSCALAROP(intp, INT_MAX, v = min(v, f.ival()), vval, len, At(i)) }
    ENDDECL1(min, "v", "I]", "I",
        "smallest component of a int vector, or INT_MAX if length 0.");
    STARTDECL(min) (VM &vm, Value &x) { VECSCALAROP(floatp, FLT_MAX, v = min(v, f.fval()), vval, len, At(i)) }
    ENDDECL1(min, "v", "F]", "F",
        "smallest component of a float vector, or FLT_MAX if length 0.");

    STARTDECL(max) (VM &, Value &x, Value &y) { return Value(max(x.ival(), y.ival())); }
    ENDDECL2(max, "x,y", "II", "I",
        "largest of 2 integers.");
    STARTDECL(max) (VM &, Value &x, Value &y) { return Value(max(x.fval(), y.fval())); }
    ENDDECL2(max, "x,y", "FF", "F",
        "largest of 2 floats.");
    STARTDECL(max) (VM &vm, Value &x, Value &y) { VECBINOP(max,ival) }
    ENDDECL2(max, "x,y", "I}I}", "I}",
        "largest components of 2 int vectors");
    STARTDECL(max) (VM &vm, Value &x, Value &y) { VECBINOP(max,fval) }
    ENDDECL2(max, "x,y", "F}F}", "F}",
        "largest components of 2 float vectors");
    STARTDECL(max) (VM &vm, Value &x) { VECSCALAROP(intp, INT_MIN, v = max(v, f.ival()), stval, Len(vm), AtS(i)) }
    ENDDECL1(max, "v", "I}", "I",
        "largest component of a int vector.");
    STARTDECL(max) (VM &vm, Value &x) { VECSCALAROP(floatp, FLT_MIN, v = max(v, f.fval()), stval, Len(vm), AtS(i)) }
    ENDDECL1(max, "v", "F}", "F",
        "largest component of a float vector.");
    STARTDECL(max) (VM &vm, Value &x) { VECSCALAROP(intp, INT_MIN, v = max(v, f.ival()), vval, len, At(i)) }
    ENDDECL1(max, "v", "I]", "I",
        "largest component of a int vector, or INT_MIN if length 0.");
    STARTDECL(max) (VM &vm, Value &x) { VECSCALAROP(floatp, FLT_MIN, v = max(v, f.fval()), vval, len, At(i)) }
    ENDDECL1(max, "v", "F]", "F",
        "largest component of a float vector, or FLT_MIN if length 0.");

    STARTDECL(lerp) (VM &, Value &x, Value &y, Value &f) {
        return Value(mix(x.fval(), y.fval(), (float)f.fval()));
    }
    ENDDECL3(lerp, "x,y,f", "FFF", "F",
        "linearly interpolates between x and y with factor f [0..1]");

    STARTDECL(lerp) (VM &vm, Value &x, Value &y, Value &f) {
        auto numelems = x.stval()->Len(vm);
        return ToValueF(vm, mix(ValueDecToF<4>(vm, x), ValueDecToF<4>(vm, y), (float)f.fval()), numelems);
    }
    ENDDECL3(lerp, "a,b,f", "F}F}F", "F}",
        "linearly interpolates between a and b vectors with factor f [0..1]");

    STARTDECL(cardinalspline) (VM &vm, Value &z, Value &a, Value &b, Value &c, Value &f, Value &t) {
        return ToValueF(vm, cardinalspline(ValueDecToF<3>(vm, z),
                                           ValueDecToF<3>(vm, a),
                                           ValueDecToF<3>(vm, b),
                                           ValueDecToF<3>(vm, c), f.fval(), t.fval()));
    }
    ENDDECL6(cardinalspline, "z,a,b,c,f,tension", "F}F}F}F}FF", "F}:3",
        "computes the position between a and b with factor f [0..1], using z (before a) and c"
        " (after b) to form a cardinal spline (tension at 0.5 is a good default)");

    STARTDECL(line_intersect) (VM &vm, Value &l1a, Value &l1b, Value &l2a, Value &l2b) {
        floatp2 ipoint;
        auto r = line_intersect(ValueDecToF<2>(vm, l1a), ValueDecToF<2>(vm, l1b),
                                ValueDecToF<2>(vm, l2a), ValueDecToF<2>(vm, l2b), &ipoint);
        return r ? ToValueF(vm, ipoint) : Value();
    }
    ENDDECL4(line_intersect, "line1a,line1b,line2a,line2b", "F}:2F}:2F}:2F}:2", "F}:2?",
        "computes the intersection point between 2 line segments, or nil if no intersection");

    STARTDECL(circles_within_range) (VM &vm, Value &dist, Value &positions, Value &radiuses,
                                     Value &prefilter) {
        auto len = positions.vval()->len;
        if (radiuses.vval()->len != len || prefilter.vval()->len != len)
            return vm.BuiltinError(
                "circles_within_range: all input vectors must be the same size");
        struct Node { floatp2 pos; floatp rad; bool filter; intp idx; Node *next; };
        vector<Node> nodes(len, Node());
        floatp maxrad = 0;
        floatp2 minpos = floatp2(FLT_MAX), maxpos(FLT_MIN);
        for (intp i = 0; i < len; i++) {
            auto &n = nodes[i];
            auto p = ValueToF<2>(vm, positions.vval()->At(i));
            minpos = min(minpos, p);
            maxpos = max(maxpos, p);
            n.pos = p;
            auto r = radiuses.vval()->At(i).fval();
            maxrad = max(maxrad, r);
            n.rad = r;
            n.filter = prefilter.vval()->At(i).True();
            n.idx = i;
            n.next = nullptr;
        }
        positions.DECRT(vm);
        radiuses.DECRT(vm);
        prefilter.DECRT(vm);
        auto ncelld = (intp)sqrtf(float(len + 1) * 4);
        vector<Node *> cells(ncelld * ncelld, nullptr);
        auto wsize = maxpos - minpos;
        wsize *= 1.00001f;  // No objects may fall exactly on the far border.
        auto tocellspace = [&](const floatp2 &pos) {
            return intp2((pos - minpos) / wsize * float(ncelld));
        };
        for (intp i = 0; i < len; i++) {
            auto &n = nodes[i];
            auto cp = tocellspace(n.pos);
            auto &c = cells[cp.x + cp.y * ncelld];
            n.next = c;
            c = &n;
        }
        auto qdist = dist.fval();
        vector<intp> within_range;
        vector<LVector *> results(len, nullptr);
        for (intp i = 0; i < len; i++) {
            auto &n = nodes[i];
            auto scanrad = n.rad + maxrad + qdist;
            auto minc = max(intp2_0, min((ncelld - 1) * intp2_1, tocellspace(n.pos - scanrad)));
            auto maxc = max(intp2_0, min((ncelld - 1) * intp2_1, tocellspace(n.pos + scanrad)));
            for (intp y = minc.y; y <= maxc.y; y++) {
                for (intp x = minc.x; x <= maxc.x; x++) {
                    for (auto c = cells[x + y * ncelld]; c; c = c->next) {
                        if (c->filter && c != &n) {
                            auto d = length(c->pos - n.pos) - n.rad - c->rad;
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
        auto rvec = (LVector *)vm.NewVec(0, len, TYPE_ELEM_VECTOR_OF_VECTOR_OF_INT);
        for (auto vec : results) rvec->Push(vm, Value(vec));
        return Value(rvec);
    }
    ENDDECL4(circles_within_range, "dist,positions,radiuses,prefilter", "FF}:2]F]I]", "I]]",
        "given a vector of 2D positions (an same size vectors of radiuses and pre-filter), returns"
        " a vector of vectors of indices of the circles that are within dist of eachothers radius."
        " pre-filter indicates objects that should appear in the inner vectors.");

    STARTDECL(resume) (VM &vm, Value &co, Value &ret) {
        vm.CoResume(co.cval());
        // By the time CoResume returns, we're now back in the context of co, meaning that the
        // return value below is what is returned from yield.
        return ret;
        // The actual return value from this call to resume will be the argument to the next call
        // to yield, or the coroutine return value.
    }
    ENDDECL2(resume, "coroutine,returnvalue", "RA%?", "A",
        "resumes execution of a coroutine, passing a value back or nil");

    STARTDECL(returnvalue) (VM &vm, Value &co) {
        Value &rv = co.cval()->Current(vm);
        co.DECRT(vm);
        return rv;
    }
    ENDDECL1(returnvalue, "coroutine", "R", "A1",
        "gets the last return value of a coroutine");

    STARTDECL(active) (VM &vm, Value &co) {
        bool active = co.cval()->active;
        co.DECRT(vm);
        return Value(active);
    }
    ENDDECL1(active, "coroutine", "R", "I",
        "wether the given coroutine is still active");

    STARTDECL(hash) (VM &vm, Value &a) {
        auto h = a.Hash(vm, V_FUNCTION);
        return Value(h);
    }
    ENDDECL1(hash, "x", "C", "I",
        "hashes a function value into an int");
    STARTDECL(hash) (VM &vm, Value &a) {
        auto h = a.ref()->Hash(vm);
        a.DECRTNIL(vm);
        return Value(h);
    }
    ENDDECL1(hash, "x", "A", "I",
        "hashes any value into an int");

    STARTDECL(program_name) (VM &vm) {
        return Value(vm.NewString(vm.GetProgramName()));
    }
    ENDDECL0(program_name, "", "", "S",
        "returns the name of the main program (e.g. \"foo.lobster\".");

    STARTDECL(vm_compiled_mode) (VM &) {
        return Value(
            #ifdef VM_COMPILED_CODE_MODE
                true
            #else
                false
            #endif
        );
    }
    ENDDECL0(vm_compiled_mode, "", "", "I",
        "returns if the VM is running in compiled mode (Lobster -> C++).");

    STARTDECL(seconds_elapsed) (VM &vm) {
        return Value(vm.Time());
    }
    ENDDECL0(seconds_elapsed, "", "", "F",
        "seconds since program start as a float, unlike gl_time() it is calculated every time it is"
        " called");

    STARTDECL(assert) (VM &vm, Value &c) {
        if (!c.True()) vm.BuiltinError("assertion failed");
        return c;
    }
    ENDDECL1(assert, "condition", "A*", "A1",
        "halts the program with an assertion failure if passed false. returns its input");

    STARTDECL(trace_bytecode) (VM &vm, Value &i, Value &tail) {
        vm.Trace(i.ival() != 0, tail.ival() != 0);
        return Value();
    }
    ENDDECL2(trace_bytecode, "on,tail", "II", "",
        "tracing shows each bytecode instruction as it is being executed, not very useful unless"
        " you are trying to isolate a compiler bug");

    STARTDECL(collect_garbage) (VM &vm) {
        return Value(vm.GC());
    }
    ENDDECL0(collect_garbage, "", "", "I",
        "forces a garbage collection to re-claim cycles. slow and not recommended to be used."
        " instead, write code to clear any back pointers before abandoning data structures. Watch"
        " for a \"LEAKS FOUND\" message in the console upon program exit to know when you've"
        " created a cycle. returns number of objects collected.");

    STARTDECL(set_max_stack_size) (VM &vm, Value &max) {
        vm.SetMaxStack((int)max.ival() * 1024 * 1024 / sizeof(Value));
        return Value();
    }
    ENDDECL1(set_max_stack_size, "max",  "I", "",
        "size in megabytes the stack can grow to before an overflow error occurs. defaults to 1");

    STARTDECL(reference_count) (VM &vm, Value &x) {
        auto refc = x.refnil() ? x.refnil()->refc - 1 : -1;
        x.DECRTNIL(vm);
        return Value(refc);
    }
    ENDDECL1(reference_count, "val", "A", "I",
        "get the reference count of any value. for compiler debugging, mostly");

    STARTDECL(set_console) (VM &, Value &x) {
        SetConsole(x.True());
        return Value();
    }
    ENDDECL1(set_console, "on", "I", "",
             "lets you turn on/off the console window (on Windows)");

    STARTDECL(command_line_arguments) (VM &vm) {
        auto v = (LVector *)vm.NewVec(0, (int)vm.program_args.size(),
                                         TYPE_ELEM_VECTOR_OF_STRING);
        for (auto &a : vm.program_args) v->Push(vm, vm.NewString(a));
        return Value(v);
    }
    ENDDECL0(command_line_arguments, "", "", "S]",
             "");

}

}
