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

// Compile time lifetime tracking between values and their recipients.
// If lifetimes correspond, no action is required.
// If recipient wants to keep, but value is borrowed, inc ref or copy or error.
// If recipient wants to borrow, but value is keep, dec ref or delete after recipient is done.
// NOTE: all positive values are an index of the SpecIdent being borrowed.
// If you're borrowing, you are "locking" the modification of the variable you borrow from.
enum Lifetime {
    // Value: you are receiving a value stored elsewhere, do not hold on.
    // Recipient: I do not want to be responsible for managing this value.
    LT_BORROW = -1,
    // Value: you are responsible for this value, you must delete or store.
    // Recipient: I want to hold on to this value (inc ref, or be sole owner).
    LT_KEEP = -2,
    // Value: lifetime shouldn't matter, because type is non-reference.
    // Recipient: I'm cool with any lifetime.
    LT_ANY = -3,
    // Value: there are multiple lifetimes, stored elsewhere.
    LT_MULTIPLE = -4,
    // Lifetime is not valid.
    LT_UNDEF = -5,
};

inline bool IsBorrow(Lifetime lt) { return lt >= LT_BORROW; }
inline Lifetime LifetimeType(Lifetime lt) { return IsBorrow(lt) ? LT_BORROW : lt; }

struct SubFunction;

struct Enum;

struct UDT;

struct TypeVariable;

struct Type;

struct SpecUDT {
    UDT *udt;
    vector<const Type *> specializers;
    bool is_generic = false;

    SpecUDT(UDT *udt) : udt(udt) {}

    bool Equal(const SpecUDT &o) const;
};

struct Type {
    const ValueType t = V_UNDEFINED;

    struct TupleElem { const Type *type; Lifetime lt; };

    union {
        const Type *sub;         // V_VECTOR | V_NIL | V_VAR | V_TYPEID
        SubFunction *sf;         // V_FUNCTION
        SpecUDT *spec_udt;       // V_UUDT
        UDT *udt;                // V_CLASS | V_STRUCT_*
        Enum *e;                 // V_INT
        vector<TupleElem> *tup;  // V_TUPLE
        TypeVariable *tv;        // V_TYPEVAR
    };

    Type()                               :               sub(nullptr) {}
    explicit Type(ValueType _t)          : t(_t),        sub(nullptr) {}
    Type(ValueType _t, const Type *_s)   : t(_t),        sub(_s)      {}
    Type(ValueType _t, SubFunction *_sf) : t(_t),        sf(_sf)      {}
    Type(SpecUDT *_su)                   : t(V_UUDT),    spec_udt(_su){}
    Type(ValueType _t, UDT *_udt)        : t(_t),        udt(_udt)    {}
    Type(Enum *_e)                       : t(V_INT),     e(_e)        {}
    Type(TypeVariable *_tv)              : t(V_TYPEVAR), tv(_tv)      {}


    bool Equal(const Type &o, bool allow_unresolved = false) const {
        if (this == &o) return true;
        if (t != o.t) {
            if (!allow_unresolved) return false;
            // Special case for V_UUDT, since sometime types are resolved in odd orders.
            // TODO: can the is_generic be removed?
            switch (t) {
                case V_UUDT:
                    return IsUDT(o.t) && spec_udt->udt == o.udt && !spec_udt->is_generic;
                case V_CLASS:
                case V_STRUCT_R:
                case V_STRUCT_S:
                    return o.t == V_UUDT && o.spec_udt->udt == udt && !o.spec_udt->is_generic;
                default:
                    return false;
            }
        }
        if (sub == o.sub) return true;  // Also compares sf/udt
        switch (t) {
            case V_VECTOR:
            case V_NIL:
                return sub->Equal(*o.sub, allow_unresolved);
            case V_UUDT:
                return spec_udt->Equal(*o.spec_udt);
            default:
                return false;
        }
    }

    Type &operator=(const Type &o) {
        // Hack: we want t to be const, but still have a working assignment operator.
        (ValueType &)t = o.t;
        sub = o.sub;
        return *this;
    }

    const Type *Element() const {
        assert(Wrapped());
        return sub;
    }

    const Type *ElementIfNil() const {
        return t == V_NIL ? sub : this;
    }

    Type *Wrap(Type *dest, ValueType with) const {
        assert(dest != this);
        *dest = Type(with, this);
        return dest;
    }

    bool Wrapped() const { return t == V_VECTOR || t == V_NIL; }

    const Type *UnWrapped() const { return Wrapped() ? sub : this; }
    const Type *UnWrapAll() const { return Wrapped() ? sub->UnWrapped() : this; }

    bool Numeric() const { return t == V_INT || t == V_FLOAT; }

    bool IsFunction() const { return t == V_FUNCTION && sf; }

    bool IsEnum() const { return t == V_INT && e; }

    bool IsBoundVar() const { return t == V_VAR && sub; }

    bool HasValueType(ValueType vt) const {
        return t == vt || (Wrapped() && Element()->HasValueType(vt));
    }

    size_t NumValues() const {
        if (t == V_VOID) return 0;
        if (t == V_TUPLE) return tup->size();
        return 1;
    }

    const Type *Get(size_t i) const {
        return t == V_TUPLE ? (*tup)[i].type : this;
    }

    void Set(size_t i, const Type *type, Lifetime lt) const {
        assert(t == V_TUPLE);
        (*tup)[i] = { type, lt };
    }

    Lifetime GetLifetime(size_t i, Lifetime lt) const {
        return lt == LT_MULTIPLE && t == V_TUPLE ? (*tup)[i].lt : lt;
    }
};

extern const Type g_type_undefined;

// This is essentially a smart-pointer, but behaves a little bit differently:
// - initialized to type_undefined instead of nullptr
// - pointer is const
// - comparisons are by value.
class TypeRef {
    const Type *type;

    public:
    TypeRef()                  : type(&g_type_undefined) {}
    TypeRef(const Type *_type) : type(_type) {}

    TypeRef &operator=(const TypeRef &o) {
        type = o.type;
        return *this;
    }

    const Type &operator*()  const { return *type; }
    const Type *operator->() const { return type; }

    const Type *get() const { return type; }

    bool Null() const { return type == nullptr; }
};

struct UnresolvedTypeRef {
    TypeRef utr;
};


extern TypeRef type_int;
extern TypeRef type_float;
extern TypeRef type_string;
extern TypeRef type_any;
extern TypeRef type_vector_int;
extern TypeRef type_vector_float;
extern TypeRef type_function_null;
extern TypeRef type_function_cocl;
extern TypeRef type_resource;
extern TypeRef type_vector_resource;
extern TypeRef type_typeid;
extern TypeRef type_void;
extern TypeRef type_undefined;

TypeRef WrapKnown(TypeRef elem, ValueType with);

struct Named {
    string name;
    int idx = -1;
    bool isprivate = false;

    Named() = default;
    Named(string_view _name, int _idx = 0) : name(_name), idx(_idx) {}
};

enum NArgFlags {
    NF_NONE               = 0,
    NF_SUBARG1            = 1 << 0,
    NF_SUBARG2            = 1 << 1,
    NF_SUBARG3            = 1 << 2,
    NF_ANYVAR             = 1 << 3,
    NF_CONVERTANYTOSTRING = 1 << 4,
    NF_PUSHVALUEWIDTH     = 1 << 5,
    NF_BOOL               = 1 << 6,
    NF_UNION              = 1 << 7,
    NF_CONST              = 1 << 8,
};
DEFINE_BITWISE_OPERATORS_FOR_ENUM(NArgFlags)

struct Ident;
struct SpecIdent;

struct Narg {
    TypeRef type = type_undefined;
    NArgFlags flags = NF_NONE;
    string_view name;
    char fixed_len = 0;
    Lifetime lt = LT_UNDEF;

    void Set(const char *&tid, Lifetime def) {
        char t = *tid++;
        flags = NF_NONE;
        lt = def;
        switch (t) {
            case 'A': type = type_any; break;
            case 'I': type = type_int; break;
            case 'B': type = type_int; flags = flags | NF_BOOL; break;
            case 'F': type = type_float; break;
            case 'S': type = type_string; break;
            case 'L': type = type_function_null; break;  // FIXME: only used by hash(), in gui.lobster
            case 'R': type = type_resource; break;
            case 'T': type = type_typeid; break;
            default:  assert(0);
        }
        while (*tid && !isupper(*tid)) {
            switch (auto c = *tid++) {
                case 0: break;
                case '1': flags = flags | NF_SUBARG1; break;
                case '2': flags = flags | NF_SUBARG2; break;
                case '3': flags = flags | NF_SUBARG3; break;
                case 'u': flags = flags | NF_UNION; break;
                case '*': flags = flags | NF_ANYVAR; break;
                case 'c': flags = flags | NF_CONST; break;
                case 's': flags = flags | NF_CONVERTANYTOSTRING; break;
                case 'w': flags = flags | NF_PUSHVALUEWIDTH; break;
                case 'k': lt = LT_KEEP; break;
                case 'b': lt = LT_BORROW; break;
                case ']':
                case '}':
                    type = WrapKnown(type, V_VECTOR);
                    assert(!type.Null());
                    if (c == '}') fixed_len = -1;
                    break;
                case '?':
                    type = WrapKnown(type, V_NIL);
                    assert(!type.Null());
                    break;
                case ':':
                    assert(*tid >= '/' && *tid <= '9');
                    fixed_len = *tid++ - '0';
                    break;
                default:
                    assert(false);
            }
        }
    }
};

typedef void  (*builtinfV)(StackPtr &sp, VM &vm);
typedef Value (*builtinf0)(StackPtr &sp, VM &vm);
typedef Value (*builtinf1)(StackPtr &sp, VM &vm, Value &);
typedef Value (*builtinf2)(StackPtr &sp, VM &vm, Value &, Value &);
typedef Value (*builtinf3)(StackPtr &sp, VM &vm, Value &, Value &, Value &);
typedef Value (*builtinf4)(StackPtr &sp, VM &vm, Value &, Value &, Value &, Value &);
typedef Value (*builtinf5)(StackPtr &sp, VM &vm, Value &, Value &, Value &, Value &, Value &);
typedef Value (*builtinf6)(StackPtr &sp, VM &vm, Value &, Value &, Value &, Value &, Value &, Value &);
typedef Value (*builtinf7)(StackPtr &sp, VM &vm, Value &, Value &, Value &, Value &, Value &, Value &, Value &);

struct BuiltinPtr {
    union  {
        builtinfV fV;
        builtinf0 f0;
        builtinf1 f1;
        builtinf2 f2;
        builtinf3 f3;
        builtinf4 f4;
        builtinf5 f5;
        builtinf6 f6;
        builtinf7 f7;
    };
    int fnargs;

    BuiltinPtr()      : f0(nullptr), fnargs(0) {}
    BuiltinPtr(builtinfV f) : fV(f), fnargs(-1) {}
    BuiltinPtr(builtinf0 f) : f0(f), fnargs(0) {}
    BuiltinPtr(builtinf1 f) : f1(f), fnargs(1) {}
    BuiltinPtr(builtinf2 f) : f2(f), fnargs(2) {}
    BuiltinPtr(builtinf3 f) : f3(f), fnargs(3) {}
    BuiltinPtr(builtinf4 f) : f4(f), fnargs(4) {}
    BuiltinPtr(builtinf5 f) : f5(f), fnargs(5) {}
    BuiltinPtr(builtinf6 f) : f6(f), fnargs(6) {}
    BuiltinPtr(builtinf7 f) : f7(f), fnargs(7) {}
};

struct NativeFun : Named {
    BuiltinPtr fun;

    vector<Narg> args, retvals;

    const char *help;

    int subsystemid = -1;

    NativeFun *overloads = nullptr, *first = this;

    int TypeLen(const char *s) {
        int i = 0;
        while (*s) if(isupper(*s++)) i++;
        return i;
    };

    NativeFun(const char *name, BuiltinPtr f, const char *ids, const char *typeids,
              const char *rets, const char *help)
        : Named(name, 0), fun(f), args(TypeLen(typeids)), retvals(TypeLen(rets)),
          help(help) {
        assert((int)args.size() == f.fnargs || f.fnargs < 0);
        auto StructArgsVararg = [&](const Narg &arg) {
            assert(!arg.fixed_len || IsRef(arg.type->sub->t) || f.fnargs < 0);
            (void)arg;
        };
        for (auto [i, arg] : enumerate(args)) {
            const char *idend = strchr(ids, ',');
            if (!idend) {
                // if this fails, you're not specifying enough arg names in the comma separated list
                assert(i == args.size() - 1);
                idend = ids + strlen(ids);
            }
            arg.name = string_view(ids, idend - ids);
            ids = idend + 1;
            arg.Set(typeids, LT_BORROW);
            StructArgsVararg(arg);
        }
        for (auto &ret : retvals) {
            ret.Set(rets, LT_KEEP);
            StructArgsVararg(ret);
        }
    }

    bool IsGLFrame() {
        return name == "gl_frame";
    }

    bool IsAssert() {
        // FIXME: make into a language feature.
        return name == "assert";
    }
};

struct NativeRegistry {
    vector<NativeFun *> nfuns;
    unordered_map<string_view, NativeFun *> nfunlookup;  // Key points to value!
    vector<string> subsystems;

    ~NativeRegistry() {
        for (auto f : nfuns) delete f;
    }

    void NativeSubSystemStart(const char *name) { subsystems.push_back(name); }

    #define REGISTER(N) \
    void operator()(const char *name, const char *ids, const char *typeids, \
                    const char *rets, const char *help, builtinf##N f) { \
        Reg(new NativeFun(name, BuiltinPtr(f), ids, typeids, rets, help)); \
    }
    REGISTER(V)
    REGISTER(0)
    REGISTER(1)
    REGISTER(2)
    REGISTER(3)
    REGISTER(4)
    REGISTER(5)
    REGISTER(6)
    REGISTER(7)
    #undef REGISTER

    void Reg(NativeFun *nf) {
        nf->idx = (int)nfuns.size();
        nf->subsystemid = (int)subsystems.size() - 1;
        auto existing = FindNative(nf->name);
        if (existing) {
            if (/*nf->args.v.size() != existing->args.v.size() ||
                nf->retvals.v.size() != existing->retvals.v.size() || */
                nf->subsystemid != existing->subsystemid ) {
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

}  // namespace lobster

#endif  // LOBSTER_NATREG
