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

#ifndef LOBSTER_NATREG
#define LOBSTER_NATREG

#include "lobster/vmdata.h"

namespace lobster {

struct Named {
    string name;
    int idx;
    bool isprivate;

    Named() : idx(-1), isprivate(false) {}
    Named(string_view _name, int _idx = 0) : name(_name), idx(_idx), isprivate(false) {}
};

struct SubFunction;

struct Struct;

struct Type {
    const ValueType t;

    union {
        const Type *sub; // V_VECTOR | V_NIL | V_VAR
        Named *named;    // V_FUNCTION | V_COROUTINE | V_STRUCT
        SubFunction *sf; // V_FUNCTION | V_COROUTINE
        Struct *struc;   // V_STRUCT
    };

    Type()                               : t(V_ANY), sub(nullptr) {}
    explicit Type(ValueType _t)          : t(_t),    sub(nullptr) {}
    Type(ValueType _t, const Type *_s)   : t(_t),    sub(_s)      {}
    Type(ValueType _t, SubFunction *_sf) : t(_t),    sf(_sf)      {}
    Type(ValueType _t, Struct *_st)      : t(_t),    struc(_st)   {}

    bool operator==(const Type &o) const {
        return t == o.t &&
               (sub == o.sub ||  // Also compares sf/struc
                (Wrapped() && *sub == *o.sub));
    }

    bool operator!=(const Type &o) const { return !(*this == o); }

    bool EqNoIndex(const Type &o) const {
        return t == o.t && (!Wrapped() || sub->EqNoIndex(*o.sub));
    }

    Type &operator=(const Type &o) {
        // Hack: we want t to be const, but still have a working assignment operator.
        (ValueType &)t = o.t;
        sub = o.sub;
        return *this;
    }

    // This one is used to sort types for multi-dispatch.
    bool operator<(const Type &o) const {
        if (t != o.t) return t < o.t;
        switch (t) {
            case V_VECTOR:
            case V_NIL:
                return *sub < *o.sub;
            case V_FUNCTION:
            case V_STRUCT:
                return named->idx < o.named->idx;
            default:
                return false;
        }
    }

    const Type *Element() const {
        assert(Wrapped());
        return sub;
    }

    Type *Wrap(Type *dest, ValueType with = V_VECTOR) const {
        *dest = Type(with, this);
        return dest;
    }

    bool Wrapped() const { return t == V_VECTOR || t == V_NIL; }

    const Type *UnWrapped() const { return Wrapped() ? sub : this; }

    bool Numeric() const { return t == V_INT || t == V_FLOAT; }

    bool IsFunction() const { return t == V_FUNCTION && sf; }

    bool HasVariable() const {  // FIXME: does this cover all cases?
        return t == V_VAR ||
              (t == V_VECTOR && sub->t == V_VAR) ||
              (t == V_NIL    && sub->t == V_VAR);
    }
};

extern const Type g_type_any;

// This is essentially a smart-pointer, but behaves a little bit differently:
// - initialized to type_any instead of nullptr
// - pointer is const
// - comparisons are by value.
class TypeRef {
    const Type *type;

    public:
    TypeRef() : type(&g_type_any) {}
    TypeRef(const Type *_type) : type(_type) {}

    TypeRef &operator=(const TypeRef &o) {
        type = o.type;
        return *this;
    }

    const Type &operator*()  const { return *type; }
    const Type *operator->() const { return type; }

    // Must compare Type instances by value.
    bool operator==(const TypeRef &o) const { return *type == *o.type; };
    bool operator!=(const TypeRef &o) const { return *type != *o.type; };
    bool operator< (const TypeRef &o) const { return *type <  *o.type; };

    bool Null() const { return type == nullptr; }
};

extern TypeRef type_int;
extern TypeRef type_float;
extern TypeRef type_string;
extern TypeRef type_any;
extern TypeRef type_vector_any;
extern TypeRef type_vector_int;
extern TypeRef type_vector_float;
extern TypeRef type_function_null;
extern TypeRef type_function_cocl;
extern TypeRef type_function_nil;
extern TypeRef type_coroutine;
extern TypeRef type_resource;
extern TypeRef type_typeid;

enum ArgFlags {
    AF_NONE = 0,
    AF_EXPFUNVAL = 1,
    AF_GENERIC = 2,
    NF_SUBARG1 = 4,
    NF_SUBARG2 = 8,
    NF_SUBARG3 = 16,
    NF_ANYVAR = 32,
    NF_CORESUME = 64,
    AF_WITHTYPE = 128
};
DEFINE_BITWISE_OPERATORS_FOR_ENUM(ArgFlags)

struct Ident;
struct SpecIdent;

struct Typed {
    TypeRef type;
    ArgFlags flags;

    Typed() : flags(AF_NONE) {}
    Typed(const Typed &o) : type(o.type), flags(o.flags) {}
    Typed(TypeRef _type, ArgFlags _flags) : type(_type), flags(_flags) {}
};

struct Narg : Typed {
    char fixed_len;

    Narg() : Typed(), fixed_len(0) {}
    Narg(const Narg &o) : Typed(o), fixed_len(o.fixed_len) {}

    void Set(const char *&tid, list<Type> &typestorage) {
        char t = *tid++;
        flags = AF_NONE;
        switch (t) {
            case 'I': type = type_int; break;
            case 'F': type = type_float; break;
            case 'S': type = type_string; break;
            case 'V': type = type_vector_any; break;  // Deprecated, use ']'
            case 'C': type = type_function_null; break;
            case 'R': type = type_coroutine; break;
            case 'X': type = type_resource; break;
            case 'A': type = type_any; break;
            case 'T': type = type_typeid; break;
            default:  assert(0);
        }
        while (*tid && !isalpha(*tid)) {
            switch (auto c = *tid++) {
                case 0: break;
                case '1': flags = flags | NF_SUBARG1; break;
                case '2': flags = flags | NF_SUBARG2; break;
                case '3': flags = flags | NF_SUBARG3; break;
                case '*': flags = flags | NF_ANYVAR; break;
                case '@': flags = flags | AF_EXPFUNVAL; break;
                case '%': flags = flags | NF_CORESUME; break; // FIXME: make a vm op.
                case ']':
                case '}':
                    typestorage.push_back(Type());
                    type = type->Wrap(&typestorage.back());
                    if (c == '}') fixed_len = -1;
                    break;
                case '?':
                    typestorage.push_back(Type());
                    type = type->Wrap(&typestorage.back(), V_NIL);
                    break;
                case ':': assert(*tid >= '/' && *tid <= '9'); fixed_len = *tid++ - '0'; break;
                default: assert(0);
            }
        }
    }
};

struct GenericArgs {
    virtual string_view GetName(size_t i) const = 0;
    virtual const Typed *GetType(size_t i) const = 0;
    virtual size_t size() const = 0;
};

struct NargVector : GenericArgs {
    vector<Narg> v;
    const char *idlist;

    NargVector(int nargs, const char *_idlist) : v(nargs), idlist(_idlist) {}

    size_t size() const { return v.size(); }
    const Typed *GetType(size_t i) const { return &v[i]; }
    string_view GetName(size_t i) const {
        auto ids = idlist;
        for (;;) {
            const char *idend = strchr(ids, ',');
            if (!idend) {
                // if this fails, you're not specifying enough arg names in the comma separated list
                assert(!i);
                idend = ids + strlen(ids);
            }
            if (!i--) return string_view(ids, idend - ids);
            ids = idend + 1;
        }
    }
};

struct BuiltinPtr {
    union  {
        Value (*f0)(VM &vm);
        Value (*f1)(VM &vm, Value &);
        Value (*f2)(VM &vm, Value &, Value &);
        Value (*f3)(VM &vm, Value &, Value &, Value &);
        Value (*f4)(VM &vm, Value &, Value &, Value &, Value &);
        Value (*f5)(VM &vm, Value &, Value &, Value &, Value &, Value &);
        Value (*f6)(VM &vm, Value &, Value &, Value &, Value &, Value &, Value &);
        Value (*f7)(VM &vm, Value &, Value &, Value &, Value &, Value &, Value &, Value &);
    };
};

enum NativeCallMode { NCM_NONE, NCM_CONT_EXIT };

struct NativeFun : Named {
    BuiltinPtr fun;

    NargVector args, retvals;

    bool has_body;
    void (*cont1)(VM &);

    const char *idlist;
    const char *help;

    int subsystemid;

    NativeFun *overloads, *first;

    NativeFun(const char *_name, BuiltinPtr f, const char *_ids, const char *typeids,
              const char *rets, int nargs, const char *_help, bool _has_body, void (*_cont1)(VM &),
              list<Type> &typestorage)
        : Named(_name, 0), fun(f), args(nargs, _ids), retvals(0, nullptr),
          has_body(_has_body), cont1(_cont1), help(_help), subsystemid(-1), overloads(nullptr),
          first(this) {
        auto TypeLen = [](const char *s) { int i = 0; while (*s) if(isalpha(*s++)) i++; return i; };
        auto nretvalues = TypeLen(rets);
        assert(TypeLen(typeids) == nargs);
        for (int i = 0; i < nargs; i++) {
            args.GetName(i);  // Call this just to trigger the assert.
            args.v[i].Set(typeids, typestorage);
        }
        for (int i = 0; i < nretvalues; i++) {
            retvals.v.push_back(Narg());
            retvals.v[i].Set(rets, typestorage);
        }
    }
};

struct NativeRegistry {
    vector<NativeFun *> nfuns;
    unordered_map<string_view, NativeFun *> nfunlookup;  // Key points to value!
    vector<string> subsystems;
    list<Type> typestorage;  // For any native functions with types that rely on Wrap().

    ~NativeRegistry() {
        for (auto f : nfuns) delete f;
    }

    void NativeSubSystemStart(const char *name) { subsystems.push_back(name); }

    void Register(NativeFun *nf) {
        nf->idx = (int)nfuns.size();
        nf->subsystemid = (int)subsystems.size() - 1;
        auto existing = FindNative(nf->name);
        if (existing) {
            if (/*nf->args.v.size() != existing->args.v.size() ||
                nf->retvals.v.size() != existing->retvals.v.size() || */
                nf->subsystemid != existing->subsystemid ||
                nf->has_body != existing->has_body) {
                // Must have similar signatures.
                assert(0);
                THROW_OR_ABORT("native library name clash: " + nf->name);
            }
            nf->overloads = existing->overloads;
            existing->overloads = nf;
            nf->first = existing->first;
        } else {
            nfunlookup[nf->name /* must be in value */] = nf;
        }
        nfuns.push_back(nf);
    }

    NativeFun *FindNative(string_view name) {
        auto it = nfunlookup.find(name);
        return it != nfunlookup.end() ? it->second : nullptr;
    }
};

#define STARTDECL(name) { struct ___##name { static Value s_##name

#define MIDDECL(name) static void mid_##name

#define ENDDECL_(name, ids, types, rets, help, field, ncm, cont1) }; { \
    BuiltinPtr bp; bp.f##field = &___##name::s_##name; \
    natreg.Register(new NativeFun(#name, bp, ids, types, rets, field, help, ncm, cont1, \
                                  natreg.typestorage)); } }

#define ENDDECL0(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 0, NCM_NONE, nullptr)
#define ENDDECL1(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 1, NCM_NONE, nullptr)
#define ENDDECL2(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 2, NCM_NONE, nullptr)
#define ENDDECL3(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 3, NCM_NONE, nullptr)
#define ENDDECL4(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 4, NCM_NONE, nullptr)
#define ENDDECL5(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 5, NCM_NONE, nullptr)
#define ENDDECL6(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 6, NCM_NONE, nullptr)
#define ENDDECL7(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 7, NCM_NONE, nullptr)
#define ENDDECL2CONTEXIT(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 2, NCM_CONT_EXIT, &___##name::mid_##name)
#define ENDDECL3CONTEXIT(name, ids, types, rets, help) \
    ENDDECL_(name, ids, types, rets, help, 3, NCM_CONT_EXIT, &___##name::mid_##name)

}  // namespace lobster

#endif  // LOBSTER_NATREG
