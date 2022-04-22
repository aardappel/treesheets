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

#ifndef LOBSTER_VMDATA
#define LOBSTER_VMDATA

#include "il.h"

namespace bytecode { struct BytecodeFile; }  // FIXME

namespace lobster {

#ifndef NDEBUG
#define RTT_ENABLED 1
#define RTT_TYPE_ERRORS 1
#else
#define RTT_ENABLED 0
#define RTT_TYPE_ERRORS 0
#endif

#define STRING_CONSTANTS_KEEP 0

#define DELETE_DELAY 0

#ifdef NDEBUG
    // Inlining the base VM ops allows for a great deal of optimisation,
    // collapsing a lot of code.
    #ifdef _WIN32
        #define VM_INLINE __forceinline
    #else
        #define VM_INLINE inline __attribute__((always_inline))
    #endif
    #define VM_INLINEM VM_INLINE
#else
    // Inlining things causes a code explosion in debug, so use static instead.
    #define VM_INLINE static inline
    #define VM_INLINEM inline
#endif

enum ValueType : int {
    // refc types are negative
    V_MINVMTYPES = -9,
    V_ANY = -8,         // any other reference type.
    V_STACKFRAMEBUF = -7,
    V_VALUEBUF = -6,    // only used as memory type for vector/coro buffers, not used by Value.
    V_STRUCT_R = -5,
    V_RESOURCE = -4,
    V_STRING = -3,
    V_CLASS = -2,
    V_VECTOR = -1,
    V_NIL = 0,          // VM: null reference, Type checker: nillable.
    V_INT,
    V_FLOAT,
    V_FUNCTION,
    V_STRUCT_S,
    V_VAR,              // [typechecker only] like V_ANY, except idx refers to a type variable
    V_TYPEVAR,          // [typechecker only] refers to an explicit type variable in code, e.g. "T".
    V_TYPEID,           // [typechecker only] a typetable offset.
    V_VOID,             // [typechecker/codegen only] this exp does not produce a value.
    V_TUPLE,            // [typechecker/codegen only] this exp produces >1 value.
    V_UUDT,             // [parser/typechecker only] udt with unresolved generics.
    V_UNDEFINED,        // [typechecker only] this type should never be accessed.
    V_MAXVMTYPES
};

inline bool IsScalar(ValueType t) { return t == V_INT || t == V_FLOAT; }
inline bool IsUnBoxed(ValueType t) { return t == V_INT || t == V_FLOAT || t == V_FUNCTION; }
inline bool IsRef(ValueType t) { return t <  V_NIL; }
inline bool IsRefNil(ValueType t) { return t <= V_NIL; }
inline bool IsRefNilVar(ValueType t) { return t <= V_NIL || t == V_VAR; }
inline bool IsRefNilStruct(ValueType t) { return t <= V_NIL || t == V_STRUCT_S; }
inline bool IsRefNilNoStruct(ValueType t) { return t <= V_NIL && t != V_STRUCT_R; }
inline bool IsRuntime(ValueType t) { return t < V_VAR; }
inline bool IsStruct(ValueType t) { return t == V_STRUCT_R || t == V_STRUCT_S; }
inline bool IsUDT(ValueType t) { return t == V_CLASS || IsStruct(t); }

inline string_view BaseTypeName(ValueType t) {
    static const char *typenames[] = {
        "any", "<stackframe_buffer>", "<value_buffer>",
        "struct_ref",
        "resource", "string", "class", "vector",
        "nil", "int", "float", "function", "struct_scalar",
        "unknown", "type_variable", "typeid", "void",
        "tuple", "unresolved_udt", "undefined",
    };
    if (t <= V_MINVMTYPES || t >= V_MAXVMTYPES) {
        assert(false);
        return "<internal-error-type>";
    }
    return typenames[t - V_MINVMTYPES - 1];
}

enum type_elem_t : int {  // Strongly typed element of typetable.
    // These must correspond to typetable init in Codegen constructor.
    TYPE_ELEM_INT = 0,  // This has -1 for its enumidx.
    TYPE_ELEM_FLOAT = 2,
    TYPE_ELEM_STRING = 3,
    TYPE_ELEM_RESOURCE = 4,
    TYPE_ELEM_ANY = 5,
    TYPE_ELEM_VALUEBUF = 6,
    TYPE_ELEM_STACKFRAMEBUF = 7,
    TYPE_ELEM_VECTOR_OF_INT = 8,   // 2 each.
    TYPE_ELEM_VECTOR_OF_FLOAT = 10,
    TYPE_ELEM_VECTOR_OF_STRING = 12,
    TYPE_ELEM_VECTOR_OF_VECTOR_OF_INT = 14,
    TYPE_ELEM_VECTOR_OF_VECTOR_OF_FLOAT = 16,
    TYPE_ELEM_VECTOR_OF_RESOURCE = 18,

    TYPE_ELEM_FIXED_OFFSET_END = 20
};

struct VM;

struct TypeInfo {
    ValueType t;
    union {
        type_elem_t subt;  // V_VECTOR | V_NIL
        struct {           // V_CLASS, V_STRUCT_*
            int structidx;
            int len;
            int vtable_start_or_bitmask;
            type_elem_t elemtypes[1];  // len elems, followed by len parent types.
        };
        int enumidx;       // V_INT, -1 if not an enum.
        int sfidx;         // V_FUNCTION;
    };

    TypeInfo() = delete;
    TypeInfo(const TypeInfo &) = delete;
    TypeInfo &operator=(const TypeInfo &) = delete;

    string Debug(VM &vm, bool rec = true) const;

    type_elem_t GetElemOrParent(iint i) const {
        auto pti = elemtypes[len + i];
        return pti >= 0 ? pti : elemtypes[i];
    }

    type_elem_t SingleType() const {
        if (!len) return TYPE_ELEM_ANY;
        for (int i = 1; i < len; i++)
            if (elemtypes[i] != elemtypes[0])
                return TYPE_ELEM_ANY;
        return elemtypes[0];
    }
};

struct Value;
struct LString;
struct LVector;
struct LObject;

struct PrintPrefs {
    iint depth;
    iint budget;
    bool quoted;
    iint decimals;
    int cycles = -1;
    int indent = 0;
    int cur_indent = 0;

    PrintPrefs(iint _depth, iint _budget, bool _quoted, iint _decimals)
        : depth(_depth), budget(_budget), quoted(_quoted), decimals(_decimals) {}
};

// ANY memory allocated by the VM must inherit from this, so we can identify leaked memory
struct DynAlloc {
    type_elem_t tti;  // offset into the VM's typetable
    const TypeInfo &ti(VM &vm) const;

    DynAlloc(type_elem_t _tti) : tti(_tti) {}
};

struct RefObj : DynAlloc {
    int refc = 1;

    RefObj(type_elem_t _tti) : DynAlloc(_tti) {}

    void Inc() {
        #ifndef NDEBUG
            if (refc <= 0) {  // Should never be "re-vived".
                #if DELETE_DELAY
                    LOG_DEBUG("revive: ", (size_t)this, " - ", refc);
                #endif
                assert(false);
            }
        #endif
        refc++;
        #if DELETE_DELAY
            LOG_DEBUG("inc: ", (size_t)this, " - ", refc);
        #endif
    }

    void Dec(VM &vm) {
        refc--;
        #ifndef NDEBUG
            DECSTAT(vm);
        #endif
        #if DELETE_DELAY
            LOG_DEBUG("dec: ", (size_t)this, " - ", refc);
        #endif
        if (refc <= 0) {
            DECDELETE(vm);
        }
    }

    void CycleStr(string &sd) const { append(sd, "_", -refc, "_"); }

    bool CycleCheck(string &sd, PrintPrefs &pp) {
        if (pp.cycles >= 0) {
            if (refc < 0) { CycleStr(sd); return true; }
            refc = -(++pp.cycles);
        }
        return false;
    }

    void DECDELETE(VM &vm);
    void DECDELETENOW(VM &vm);
    void DECSTAT(VM &vm);

    iint Hash(VM &vm);
};

extern bool RefEqual(VM &vm, const RefObj *a, const RefObj *b, bool structural);
extern void RefToString(VM &vm, string &sd, const RefObj *ro, PrintPrefs &pp);

struct LString : RefObj {
    iint len;    // has to match the Value integer type, since we allow the length to be obtained
    LString(iint _l);

    const char *data() const { return (char *)(this + 1); }
    string_view strv() const { return string_view(data(), (size_t)len); }

    void ToString(string &sd, PrintPrefs &pp);

    void DeleteSelf(VM &vm);

    bool operator==(LString &o) { return strv() == o.strv(); }
    bool operator!=(LString &o) { return strv() != o.strv(); }
    bool operator< (LString &o) { return strv() <  o.strv(); }
    bool operator<=(LString &o) { return strv() <= o.strv(); }
    bool operator> (LString &o) { return strv() >  o.strv(); }
    bool operator>=(LString &o) { return strv() >= o.strv(); }

    iint Hash();
};

// There must be a single of these per type, since they are compared by pointer.
struct ResourceType {
    const char *name;
    void (* deletefun)(void *);
    void (* newfun)(void *);

    ResourceType(const char *n, void (*df)(void *), void (*nf)(void *) = nullptr)
        : name(n), deletefun(df), newfun(nf) {}
};

struct LResource : RefObj {
    void *val;
    const ResourceType *type;

    LResource(void *v, const ResourceType *t);

    void DeleteSelf(VM &vm);

    void ToString(string &sd) {
        append(sd, "(resource:", type->name, ")");
    }
};

#if RTT_ENABLED
    #define TYPE_INIT(t) ,type(t)
    #define TYPE_ASSERT(c) assert(c)
#else
    #define TYPE_INIT(t)
    #define TYPE_ASSERT(c)
#endif

typedef Value *StackPtr;

typedef void(*fun_base_t)(VM &, StackPtr);
#if VM_JIT_MODE
    extern "C" const void *vm_ops_jit_table[];
#else
    extern "C" void compiled_entry_point(VM & vm, StackPtr sp);
#endif

#if defined(VM_JIT_MODE) && !defined(_MSC_VER) && defined(USE_EXCEPTION_HANDLING)
    // Platforms like e.g. Linux cannot throw exceptions past jitted C code :(
    #define VM_USE_LONGJMP 1
#else
    #define VM_USE_LONGJMP 0
#endif

// These pointer types are for use inside Value below. In most other parts of the code we
// use naked pointers.
#if _WIN64 || __amd64__ || __x86_64__ || __ppc64__ || __LP64__
    // We use regular pointers.
    typedef LString *LStringPtr;
    typedef LVector *LVectorPtr;
    typedef LObject *LObjectPtr;
    typedef LResource *LResourcePtr;
    typedef RefObj *RefObjPtr;
    typedef TypeInfo *TypeInfoPtr;
    typedef fun_base_t FunPtr;
#else
    // We use this special pointer type to represent a 32-bit pointer inside a
    // 64-bit value.
    // This is necessary because we want all values to be exactly the same size,
    // to be able to test for 0, etc, so we can't have unused bits in the union.
    template<typename T> class ExpandedPtr {
        uint64_t c;
      public:
        ExpandedPtr(const T p) : c((uint64_t)p) {}
        T get() const { return (T)c; }
        operator T () const { return (T)c; }
        T operator->() const { return (T)c; }
    };
    typedef ExpandedPtr<LString *> LStringPtr;
    typedef ExpandedPtr<LVector *> LVectorPtr;
    typedef ExpandedPtr<LObject *> LObjectPtr;
    typedef ExpandedPtr<LResource *> LResourcePtr;
    typedef ExpandedPtr<RefObj *> RefObjPtr;
    typedef ExpandedPtr<TypeInfo *> TypeInfoPtr;
    typedef ExpandedPtr<fun_base_t> FunPtr;
#endif

static_assert(sizeof(iint) == sizeof(double) && sizeof(iint) == sizeof(RefObjPtr),
              "typedefs need fixing");

struct Value {
    private:
    union {
        // All these types must all be exactly 64-bits, even in 32-bit builds.

        // Non-reference values.
        iint ival_;      // scalars stored as pointer-sized versions.
        double fval_;
        FunPtr ip_;

        // Reference values (includes NULL if nillable version).
        LStringPtr sval_;
        LVectorPtr vval_;
        LObjectPtr oval_;
        LResourcePtr xval_;

        // Generic reference access.
        RefObjPtr ref_;

        // Temp: for inline structs.
        TypeInfoPtr ti_;
    };
    public:
    #if RTT_ENABLED
        // This one comes second, since that allows e.g. the Wasm codegen to access the above
        // data without knowing if we're in debug mode.
        ValueType type;
    #endif

    // These asserts help track down any invalid code generation issues.
    VM_INLINEM iint        ival  () const { TYPE_ASSERT(type == V_INT);        return ival_;        }
    VM_INLINEM double      fval  () const { TYPE_ASSERT(type == V_FLOAT);      return fval_;        }
    VM_INLINEM int         intval() const { TYPE_ASSERT(type == V_INT);        return (int)ival_;   }
    VM_INLINEM float       fltval() const { TYPE_ASSERT(type == V_FLOAT);      return (float)fval_; }
    VM_INLINEM LString    *sval  () const { TYPE_ASSERT(type == V_STRING);     return sval_;        }
    VM_INLINEM LVector    *vval  () const { TYPE_ASSERT(type == V_VECTOR);     return vval_;        }
    VM_INLINEM LObject    *oval  () const { TYPE_ASSERT(type == V_CLASS);      return oval_;        }
    VM_INLINEM LResource  *xval  () const { TYPE_ASSERT(type == V_RESOURCE);   return xval_;        }
    VM_INLINEM RefObj     *ref   () const { TYPE_ASSERT(IsRef(type));          return ref_;         }
    VM_INLINEM RefObj     *refnil() const { TYPE_ASSERT(IsRefNil(type));       return ref_;         }
    VM_INLINEM FunPtr      ip    () const { TYPE_ASSERT(type >= V_FUNCTION);   return ip_;          }
    VM_INLINEM void       *any   () const {                                    return ref_;         }
    VM_INLINEM TypeInfo   *tival () const { TYPE_ASSERT(type == V_STRUCT_S);   return ti_;          }

    template<typename T> T ifval() const {
        if constexpr (is_floating_point<T>()) { TYPE_ASSERT(type == V_FLOAT); return (T)fval_; }
        else                                  { TYPE_ASSERT(type == V_INT);   return (T)ival_; }
    }

    VM_INLINEM void setival(iint i)   { TYPE_ASSERT(type == V_INT);   ival_ = i; }
    VM_INLINEM void setfval(double f) { TYPE_ASSERT(type == V_FLOAT); fval_ = f; }

    // Important for efficiency that these can be uninitialized.
    #if RTT_ENABLED
        VM_INLINEM Value() : ival_(0xABADCAFEDEADBEEF), type(V_UNDEFINED) {}
    #else
        VM_INLINEM Value() { /* UNINITIALIZED! */ }
    #endif

    // We underlying types here, because types like int64_t etc can be defined as different types
    // on different platforms, causing ambiguities between multiple types that are long or long long
    VM_INLINEM Value(int i)                : ival_((iint)i)   TYPE_INIT(V_INT)      {}
    VM_INLINEM Value(unsigned int i)       : ival_((iint)i)   TYPE_INIT(V_INT)      {}
    VM_INLINEM Value(long i)               : ival_((iint)i)   TYPE_INIT(V_INT)      {}
    VM_INLINEM Value(unsigned long i)      : ival_((iint)i)   TYPE_INIT(V_INT)      {}
    VM_INLINEM Value(long long i)          : ival_((iint)i)   TYPE_INIT(V_INT)      {}
    VM_INLINEM Value(unsigned long long i) : ival_((iint)i)   TYPE_INIT(V_INT)      {}
    VM_INLINEM Value(int i, ValueType t)   : ival_(i)         TYPE_INIT(t)          { (void)t; }
    VM_INLINEM Value(bool b)               : ival_(b)         TYPE_INIT(V_INT)      {}
    VM_INLINEM Value(float f)              : fval_(f)         TYPE_INIT(V_FLOAT)    {}
    VM_INLINEM Value(double f)             : fval_((double)f) TYPE_INIT(V_FLOAT)    {}
    VM_INLINEM Value(FunPtr i)             : ip_(i)           TYPE_INIT(V_FUNCTION) {}

    VM_INLINEM Value(LString *s)         : sval_(s)         TYPE_INIT(V_STRING)     {}
    VM_INLINEM Value(LVector *v)         : vval_(v)         TYPE_INIT(V_VECTOR)     {}
    VM_INLINEM Value(LObject *s)         : oval_(s)         TYPE_INIT(V_CLASS)      {}
    VM_INLINEM Value(LResource *r)       : xval_(r)         TYPE_INIT(V_RESOURCE)   {}
    VM_INLINEM Value(RefObj *r)          : ref_(r)          TYPE_INIT(V_NIL)        { assert(false); }

    VM_INLINEM Value(TypeInfo *ti) : ti_(ti) TYPE_INIT(V_STRUCT_S) {}

    VM_INLINEM bool True() const { return ival_ != 0; }
    VM_INLINEM bool False() const { return ival_ == 0; }

    inline Value &LTINCRT() {
        TYPE_ASSERT(IsRef(type) && ref_);
        ref_->Inc();
        return *this;
    }
    inline Value &LTINCRTNIL() {
        // Can't assert IsRefNil here, since scalar 0 are valid NIL values due to e.g. and/or.
        if (ref_) LTINCRT();
        return *this;
    }
    inline Value &LTINCTYPE(ValueType t) {
        return IsRefNil(t) ? LTINCRTNIL() : *this;
    }

    inline void LTDECRT(VM &vm) const {  // we already know its a ref type
        TYPE_ASSERT(IsRef(type) && ref_);
        ref_->Dec(vm);
    }
    inline void LTDECRTNIL(VM &vm) const {
        // Can't assert IsRefNil here, since scalar 0 are valid NIL values due to e.g. and/or.
        if (ref_) LTDECRT(vm);
    }
    inline void LTDECTYPE(VM &vm, ValueType t) const {
        if (IsRefNil(t)) LTDECRTNIL(vm);
    }

    void ToString(VM &vm, string &sd, const TypeInfo &ti, PrintPrefs &pp) const;
    void ToStringBase(VM &vm, string &sd, ValueType t, PrintPrefs &pp) const;

    void ToFlexBuffer(VM &vm, flexbuffers::Builder &builder, ValueType t) const;

    bool Equal(VM &vm, ValueType vtype, const Value &o, ValueType otype, bool structural) const;
    iint Hash(VM &vm, ValueType vtype);
    Value CopyRef(VM &vm, bool deep);
};

namespace vmath {

template<typename T> T add(Value, Value) {
    assert(false);
    return 0;
}
template<> inline iint add<iint>(Value a, Value b) {
    return a.ival() + b.ival();
}
template<> inline double add<double>(Value a, Value b) {
    return a.fval() + b.fval();
}

template<typename T> T mul(Value, Value) {
    assert(false);
    return 0;
}
template<> inline iint mul<iint>(Value a, Value b) {
    return a.ival() * b.ival();
}
template<> inline double mul<double>(Value a, Value b) {
    return a.fval() * b.fval();
}

template<typename T> T min(Value, Value) {
    assert(false);
    return 0;
}
template<> inline iint min<iint>(Value a, Value b) {
    return a.ival() < b.ival() ? a.ival() : b.ival();
}
template<> inline double min<double>(Value a, Value b) {
    return a.fval() < b.fval() ? a.fval() : b.fval();
}

template<typename T> T max(Value, Value) {
    assert(false);
    return 0;
}
template<> inline iint max<iint>(Value a, Value b) {
    return a.ival() < b.ival() ? b.ival() : a.ival();
}
template<> inline double max<double>(Value a, Value b) {
    return a.fval() < b.fval() ? b.fval() : a.fval();
}

template<typename T> T abs(Value) {
    assert(false);
    return 0;
}
template<> inline iint abs<iint>(Value a) {
    return std::abs(a.ival());
}
template<> inline double abs<double>(Value a) {
    return std::abs(a.fval());
}

template<typename T> T clamp(Value, Value, Value) {
    assert(false);
    return 0;
}
template<> inline iint clamp<iint>(Value a, Value b, Value c) {
    return geom::clamp(a.ival(), b.ival(), c.ival());
}
template<> inline double clamp<double>(Value a, Value b, Value c) {
    return geom::clamp(a.fval(), b.fval(), c.fval());
}

template<typename T> bool in_range(Value, Value, Value) {
    assert(false);
    return 0;
}
template<> inline bool in_range<iint>(Value x, Value range, Value bias) {
    return x.ival() >= bias.ival() && x.ival() < bias.ival() + range.ival();
}
template<> inline bool in_range<double>(Value x, Value range, Value bias) {
    return x.fval() >= bias.fval() && x.fval() < bias.fval() + range.fval();
}

}  // namespace vmath

template<typename T> struct ValueVec {
    Value *vals;
    iint len;

    ValueVec() : vals(nullptr), len(0) {}
    ValueVec(Value *_c, iint _l) : vals(_c), len(_l) {}

    T dot(ValueVec<T> o) {
        assert(o.len == len);
        T r = 0;
        for (iint i = 0; i < len; i++) {
            r += vmath::mul<T>(vals[i], o.vals[i]);
        }
        return r;
    }

    T length() {
        return sqrt(dot(*this));
    }

    T manhattan() {
        T r = 0;
        for (iint i = 0; i < len; i++) {
            r += vmath::abs<T>(vals[i]);
        }
        return r;
    }

    void min_assign(ValueVec<T> o) {
        assert(o.len == len);
        for (iint i = 0; i < len; i++) {
            vals[i] = vmath::min<T>(vals[i], o.vals[i]);
        }
    }

    void max_assign(ValueVec<T> o) {
        assert(o.len == len);
        for (iint i = 0; i < len; i++) {
            vals[i] = vmath::max<T>(vals[i], o.vals[i]);
        }
    }

    void mix(ValueVec<double> o, float f) {
        assert(o.len == len);
        for (iint i = 0; i < len; i++) {
            vals[i] = geom::mix(vals[i].fval(), o.vals[i].fval(), f);
        }
    }

    void clamp(ValueVec<T> mi, ValueVec<T> ma) {
        assert(mi.len == len && ma.len == len);
        for (iint i = 0; i < len; i++) {
            vals[i] = vmath::clamp<T>(vals[i], mi.vals[i], ma.vals[i]);
        }
    }

    bool in_range(ValueVec<T> range, ValueVec<T> bias) {
        assert(range.len == len);
        for (iint i = 0; i < len; i++) {
            if (!vmath::in_range<T>(vals[i], range.vals[i], bias.len > i ? bias.vals[i] : 0))
                return false;
        }
        return true;
    }

    uint64_t hash(VM &vm, ValueType vt) {
        uint64_t r = 0;
        for (iint i = 0; i < len; i++) {
            r ^= vals[i].Hash(vm, vt);
        }
        return r;
    }
};

template<typename T> inline T *AllocSubBuf(VM &vm, iint size, type_elem_t tti);
template<typename T> inline void DeallocSubBuf(VM &vm, T *v, iint size);

struct LObject : RefObj {
    LObject(type_elem_t _tti) : RefObj(_tti) {}

    // FIXME: reduce the use of these.
    iint Len(VM &vm) const { return ti(vm).len; }

    Value *Elems() const { return (Value *)(this + 1); }

    // This may only be called from a context where i < len has already been ensured/asserted.
    Value &AtS(iint i) const {
        return Elems()[i];
    }

    void DeleteSelf(VM &vm);

    // This may only be called from a context where i < len has already been ensured/asserted.
    const TypeInfo &ElemTypeS(VM &vm, iint i) const;
    const TypeInfo &ElemTypeSP(VM &vm, iint i) const;

    void ToString(VM &vm, string &sd, PrintPrefs &pp);
    void ToFlexBuffer(VM &vm, flexbuffers::Builder &builder);

    bool Equal(VM &vm, const LObject &o) {
        // RefObj::Equal has already guaranteed the typeoff's are the same.
        auto len = Len(vm);
        assert(len == o.Len(vm));
        for (iint i = 0; i < len; i++) {
            auto et = ElemTypeS(vm, i).t;
            if (!AtS(i).Equal(vm, et, o.AtS(i), et, true))
                return false;
        }
        return true;
    }

    iint Hash(VM &vm) {
        iint hash = 0;
        for (int i = 0; i < Len(vm); i++) hash ^= AtS(i).Hash(vm, ElemTypeS(vm, i).t);
        return hash;
    }

    void CopyElemsShallow(Value *from, iint len) {
        t_memcpy(Elems(), from, len);
    }

    void IncRefElems(VM &vm, iint len) {
        for (iint i = 0; i < len; i++) {
            AtS(i).LTINCTYPE(ElemTypeS(vm, i).t);
        }
    }

    void CopyRefElemsDeep(VM &vm, iint len) {
        for (iint i = 0; i < len; i++) {
            if (IsRefNil(ElemTypeS(vm, i).t)) AtS(i) = AtS(i).CopyRef(vm, true);
        }
    }
};

struct LVector : RefObj {
    iint len;    // has to match the Value integer type, since we allow the length to be obtained
    iint maxl;
    iint width;  // TODO: would be great to not have to store this.

    private:
    Value *v;   // use At()

    public:
    LVector(VM &vm, iint _initial, iint _max, type_elem_t _tti);

    ~LVector() { assert(0); }   // destructed by DECREF

    ssize_t SLen() { return (ssize_t)len; }

    void DeallocBuf(VM &vm) {
        if (v) DeallocSubBuf(vm, v, maxl * width);
    }

    void DestructElementRange(VM &vm, iint from, iint to);
    void IncElementRange(VM &vm, iint from, iint to);

    void DeleteSelf(VM &vm);

    const TypeInfo &ElemType(VM &vm) const;

    void Resize(VM &vm, iint newmax);

    void MinCapacity(VM& vm, iint newmax) {
        if (newmax > maxl) Resize(vm, newmax);
    }

    void Push(VM &vm, const Value &val) {
        assert(width == 1);
        if (len == maxl) Resize(vm, maxl ? maxl * 2 : 4);
        v[len++] = val;
    }

    void PushVW(VM &vm, const Value *vals) {
        if (len == maxl) Resize(vm, maxl ? maxl * 2 : 4);
        tsnz_memcpy(v + len * width, vals, width);
        len++;
    }

    Value Pop() {
        assert(width == 1);
        return v[--len];
    }

    void PopVW(Value *dest) {
        len--;
        tsnz_memcpy(dest, v + len * width, width);
    }

    Value &Top() const {
        assert(width == 1);
        return v[len - 1];
    }

    void TopVW(Value *dest) {
        tsnz_memcpy(dest, v + (len - 1) * width, width);
    }

    void Insert(VM &vm, const Value *vals, iint i) {
        assert(i >= 0 && i <= len); // note: insertion right at the end is legal, hence <=
        if (len + 1 > maxl) Resize(vm, std::max(len + 1, maxl ? maxl * 2 : 4));
        t_memmove(v + (i + 1) * width, v + i * width, (len - i) * width);
        len++;
        tsnz_memcpy(v + i * width, vals, width);
    }

    void Remove(StackPtr &sp, VM &vm, iint i, iint n, iint decfrom, bool stack_ret);

    Value *Elems() { return v; }
    const Value *Elems() const { return v; }

    Value &At(iint i) const {
        assert(i < len && width == 1);
        return v[i];
    }

    Value &AtSub(iint i, int off) const {
        assert(i < len);
        return v[i * width + off];
    }

    Value *AtSt(iint i) const {
        assert(i < len);
        return v + i * width;
    }

    Value &AtSlot(iint i) const {
        assert(i < len * width);
        return v[i];
    }

    void AtVW(StackPtr &sp, iint i) const;
    void AtVWInc(StackPtr &sp, iint i, int bitmask) const;
    void AtVWSub(StackPtr &sp, iint i, int w, int off) const;

    void Append(VM &vm, LVector *from, iint start, iint amount);

    void ToString(VM &vm, string &sd, PrintPrefs &pp);
    void ToFlexBuffer(VM &vm, flexbuffers::Builder &builder);

    bool Equal(VM &vm, const LVector &o) {
        // RefObj::Equal has already guaranteed the typeoff's are the same.
        assert(width == 1);
        if (len != o.len) return false;
        auto et = ElemType(vm).t;
        for (iint i = 0; i < len; i++) {
            if (!At(i).Equal(vm, et, o.At(i), et, true))
                return false;
        }
        return true;
    }

    iint Hash(VM &vm) {
        iint hash = 0;
        assert(width == 1);
        auto et = ElemType(vm).t;
        for (int i = 0; i < len; i++) hash ^= At(i).Hash(vm, et);
        return hash;
    }

    void CopyElemsShallow(Value *from) {
        t_memcpy(v, from, len * width);
    }

    void IncRefElems(VM &vm) {
        IncElementRange(vm, 0, len);
    }

    void CopyRefElemsDeep(VM &vm) {
        auto &eti = ElemType(vm);
        if (!IsRefNil(eti.t)) return;
        for (int j = 0; j < width; j++) {
            if (eti.t != V_STRUCT_R || (1 << j) & eti.vtable_start_or_bitmask) {
                for (iint i = 0; i < len; i++) {
                    auto l = i * width + j;
                    AtSlot(l) = AtSlot(l).CopyRef(vm, true);
                }
            }
        }
    }

    type_elem_t SingleType(VM &vm);
};

struct StackFrame {
    const int *funstart;
    iint spstart;
};

struct NativeFun;
struct NativeRegistry;

// This contains all data shared between threads.
struct TupleSpace {
    struct TupleType {
        // We have an independent list of tuples and synchronization per type, for minimum
        // contention.
        list<Value *> tuples;
        mutex mtx;
        condition_variable condition;
    };
    vector<TupleType> tupletypes;

    atomic<bool> alive;

    TupleSpace(size_t numstructs) : tupletypes(numstructs), alive(true) {}

    ~TupleSpace() {
        for (auto &tt : tupletypes) for (auto p : tt.tuples) delete[] p;
    }
};

enum class TraceMode { OFF, ON, TAIL };

struct VMArgs {
    NativeRegistry &nfr;
    string_view programname;
    const uint8_t *static_bytecode = nullptr;
    size_t static_size = 0;
    vector<string> program_args;
    const fun_base_t *native_vtables = nullptr;
    fun_base_t jit_entry = nullptr;
    TraceMode trace = TraceMode::OFF;
};

struct VM : VMArgs {
    SlabAlloc pool;

    Value *temp_lval = nullptr;

    fun_base_t next_call_target = 0;

    int ret_unwind_to = -1;

    vector<type_elem_t> typetablebigendian;
    uint64_t *byteprofilecounts = nullptr;

    const bytecode::BytecodeFile *bcf;

    PrintPrefs programprintprefs { 10, 100000, false, -1 };
    const type_elem_t *typetable = nullptr;
    string evalret;

    int currentline = -1;
    iint maxsp = -1;

    PrintPrefs debugpp { 2, 50, true, -1 };

    string s_reuse;

    vector<string> trace_output;
    size_t trace_ring_idx = 0;

    int last_line = -1;
    int last_fileidx = -1;

    vector<RefObj *> delete_delay;

    vector<LString *> constant_strings;

    int64_t vm_count_ins = 0;
    int64_t vm_count_fcalls = 0;
    int64_t vm_count_bcalls = 0;
    int64_t vm_count_decref = 0;

    typedef StackPtr (* f_ins_pointer)(VM &, StackPtr);

    bool is_worker = false;
    vector<thread> workers;
    TupleSpace *tuple_space = nullptr;

    // A runtime error triggers code that does extensive stack trace & variable dumping, which
    // for certain errors could trigger yet more errors. This vars ensures that we don't.
    bool error_has_occured = false;  // Don't error again.
    string errmsg;
    #if VM_USE_LONGJMP
        jmp_buf jump_buffer;
    #endif

    vector<Value> fvar_def_backup;

    // We stick this in here directly, since the constant offsets into this array in
    // compiled mode a big win.
    Value fvars[1] = { -1 };

    // NOTE: NO MORE VAR DECLS AFTER "fvars"

    VM(VMArgs &&args, const bytecode::BytecodeFile *bcf);
    ~VM();

    const TypeInfo &GetTypeInfo(type_elem_t offset) {
        return *(TypeInfo *)(typetable + offset);
    }
    const TypeInfo &GetVarTypeInfo(int varidx);

    string_view GetProgramName() { return programname; }

    void DumpVal(RefObj *ro, const char *prefix);
    void DumpLeaks();

    string &TraceStream();

    void OnAlloc(RefObj *ro);
    LVector *NewVec(iint initial, iint max, type_elem_t tti);
    LObject *NewObject(iint max, type_elem_t tti);
    LResource *NewResource(void *v, const ResourceType *t);
    LString *NewString(iint l);
    LString *NewString(string_view s);
    LString *NewString(string_view s1, string_view s2);
    LString *ResizeString(LString *s, iint size, int c, bool back);

    Value Error(string err);
    Value BuiltinError(string err) { return Error(err); }
    Value SeriousError(string err);
    void ErrorBase(const string &err);
    void VMAssert(const char *what);
    void UnwindOnError();

    int DumpVar(string &sd, const Value &x, int idx, bool invalid);

    void StartWorkers(iint numthreads);
    void TerminateWorkers();
    void WorkerWrite(RefObj *ref);
    LObject *WorkerRead(type_elem_t tti);

    void EndEval(StackPtr &sp, const Value &ret, const TypeInfo &ti);

    void EvalProgram();

    void LvalueIdxVector(int lvalop, iint i);
    void LvalueIdxStruct(int lvalop, iint i);
    void LvalueField(int lvalop, iint i);
    void LvalueOp(int op, Value &a);

    string ProperTypeName(const TypeInfo &ti);

    void Div0() { Error("division by zero"); }
    void IDXErr(iint i, iint n, const RefObj *v);
    void BCallRetCheck(StackPtr sp, const NativeFun *nf);
    iint GrabIndex(StackPtr &sp, int len);

    string_view StructName(const TypeInfo &ti);
    string_view ReverseLookupType(int v);
    string_view LookupField(int stidx, iint fieldn) const;
    void Trace(TraceMode m) { trace = m; }
    double Time() { return SecondsSinceStart(); }

    Value ToString(const Value &a, const TypeInfo &ti) {
        s_reuse.clear();
        a.ToString(*this, s_reuse, ti, programprintprefs);
        return NewString(s_reuse);
    }
    Value StructToString(const Value *elems, const TypeInfo &ti) {
        s_reuse.clear();
        StructToString(s_reuse, programprintprefs, ti, elems);
        return NewString(s_reuse);
    }
    void StructToString(string &sd, PrintPrefs &pp, const TypeInfo &ti, const Value *elems);
    void StructToFlexBuffer(VM &vm, flexbuffers::Builder &builder, const TypeInfo &ti,
                            const Value *elems);
    bool EnumName(string &sd, iint val, int enumidx);
    string_view EnumName(int enumidx);
    optional<int64_t> LookupEnum(string_view name, int enumidx);
};

// This is like a smart-pointer for VM above that dynamically allocates the size of "vars".
struct VMAllocator {
    VM *vm = nullptr;
    VMAllocator(VMArgs &&args);
    ~VMAllocator();
};

VM_INLINE void Push(StackPtr &sp, Value v) { *sp++ = v; }
VM_INLINE Value Pop(StackPtr &sp) { return *--sp; }
VM_INLINE Value &Top(StackPtr sp) { return *(sp - 1); }
VM_INLINE Value &TopM(StackPtr sp, iint n) { return *(sp - (n + 1)); }
VM_INLINE Value *TopPtr(StackPtr sp) { return sp; }
VM_INLINE void PushN(StackPtr &sp, iint n) { sp += n; }
VM_INLINE void PopN(StackPtr &sp, iint n) { sp -= n; }

// Codegen helpers.

VM_INLINE void SwapVars(VM &vm, int i, StackPtr psp, int off) {
    swap(vm.fvars[i], *(psp - off));
}

VM_INLINE Value NilVal() {
    return Value(0, V_NIL);
}

VM_INLINE void BackupVar(VM &vm, int i) {
    vm.fvar_def_backup.push_back(vm.fvars[i]);
    vm.fvars[i] = NilVal();
}

VM_INLINE void DecOwned(VM &vm, int i) {
    vm.fvars[i].LTDECRTNIL(vm);
}

VM_INLINE void DecVal(VM &vm, Value v) {
    v.LTDECRTNIL(vm);
}

VM_INLINE void RestoreBackup(VM &vm, int i) {
    vm.fvars[i] = vm.fvar_def_backup.back();
    vm.fvar_def_backup.pop_back();
}

VM_INLINE StackPtr PopArg(VM &vm, int i, StackPtr psp) {
    vm.fvars[i] = Pop(psp);
    return psp;
}

VM_INLINE void SetLVal(VM &vm, Value *v) {
    vm.temp_lval = v;
}

template<typename T, int N> void PushVec(StackPtr &sp, const vec<T, N> &v, int truncate = 4) {
    auto l = std::min(N, truncate);
    for (int i = 0; i < l; i++) Push(sp, v[i]);
}

// Returns a reference to a struct on the stack that can only be
// referred to before the next push.
template<typename T> ValueVec<T> DangleVec(StackPtr &sp) {
    auto l = Pop(sp).ival();
    PopN(sp, l);
    return ValueVec<T>(TopPtr(sp), l);
}

// Returns a reference to a struct on the stack that that can be
// overwritten to become the return value (which doesn't need the
// length field on the stack.
template<typename T> ValueVec<T> ResultVec(StackPtr &sp) {
    auto l = Pop(sp).ival();
    return ValueVec<T>(TopPtr(sp) - l, l);
}

template<typename T> T PopVec(StackPtr &sp, typename T::CTYPE def = 0) {
    T v;
    auto l = Pop(sp).intval();
    if (l > T::NUM_ELEMENTS) PopN(sp, l - T::NUM_ELEMENTS);
    for (int i = T::NUM_ELEMENTS - 1; i >= 0; i--) {
        v[i] = i < l ? Pop(sp).ifval<typename T::CTYPE>() : def;
    }
    return v;
}

inline int64_t Int64FromInts(int a, int b) {
    int64_t v = (uint32_t)a;
    v |= ((int64_t)b) << 32;
    return v;
}

inline const TypeInfo &DynAlloc::ti(VM &vm) const { return vm.GetTypeInfo(tti); }

template<typename T> inline T *AllocSubBuf(VM &vm, iint size, type_elem_t tti) {
    auto header_sz = std::max(salignof<T>(), ssizeof<DynAlloc>());
    auto mem = (uint8_t *)vm.pool.alloc(size * ssizeof<T>() + header_sz);
    ((DynAlloc *)mem)->tti = tti;
    mem += header_sz;
    return (T *)mem;
}

template<typename T> inline void DeallocSubBuf(VM &vm, T *v, iint size) {
    auto header_sz = std::max(salignof<T>(), ssizeof<DynAlloc>());
    auto mem = ((uint8_t *)v) - header_sz;
    vm.pool.dealloc(mem, size * ssizeof<T>() + header_sz);
}

template<bool back> LString *WriteMem(VM &vm, LString *s, iint i, const void *data, iint size) {
    auto minsize = i + size;
    if (s->len < minsize) s = vm.ResizeString(s, minsize * 2, 0, back);
    memcpy((void *)(s->data() + (back ? s->len - i - size : i)), data, (size_t)size);
    return s;
}

template<typename T, bool back> LString *WriteValLE(VM &vm, LString *s, iint i, T val) {
    T t = flatbuffers::EndianScalar(val);
    return WriteMem<back>(vm, s, i, &t, ssizeof<T>());
}

template<typename T, bool back> T ReadValLE(const LString *s, iint i) {
    T val;
    memcpy(&val, (void *)(s->data() + (back ? s->len - i - sizeof(T) : i)), sizeof(T));
    return flatbuffers::EndianScalar(val);
}


// FIXME: turn check for len into an assert and make caller guarantee lengths match.
template<int N> inline vec<double, N> ValueToF(const Value *v, iint width, double def = 0) {
    vec<double, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->fval() : def;
    return t;
}
template<int N> inline vec<iint, N> ValueToI(const Value *v, iint width, iint def = 0) {
    vec<iint, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->ival() : def;
    return t;
}
template<int N> inline vec<float, N> ValueToFLT(const Value *v, iint width, float def = 0) {
    vec<float, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->fltval() : def;
    return t;
}
template<int N> inline vec<int, N> ValueToINT(const Value *v, iint width, int def = 0) {
    vec<int, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->intval() : def;
    return t;
}

template <typename T, int N> inline void ToValue(Value *dest, iint width, const vec<T, N> &v) {
    for (iint i = 0; i < width; i++) dest[i] = i < N ? v.c[i] : 0;
}

inline iint RangeCheck(VM &vm, const Value &idx, iint range, iint bias = 0) {
    auto i = idx.ival();
    if (i < bias || i >= bias + range)
        vm.BuiltinError(cat("index out of range [", bias, "..", bias + range, "): ", i));
    return i;
}

template<typename T> inline T GetResourceDec(VM &vm, const Value &val, const ResourceType *type) {
    if (val.False())
        return nullptr;
    auto x = val.xval();
    if (x->type != type)
        vm.BuiltinError(string_view("needed resource type: ") + type->name + ", got: " +
            x->type->name);
    return (T)x->val;
}

inline vector<string> ValueToVectorOfStrings(Value &v) {
    vector<string> r;
    for (int i = 0; i < v.vval()->len; i++) r.push_back(string(v.vval()->At(i).sval()->strv()));
    return r;
}

inline Value ToValueOfVectorOfStrings(VM &vm, const vector<string> &in) {
    auto v = vm.NewVec(0, ssize(in), TYPE_ELEM_VECTOR_OF_STRING);
    for (auto &a : in) v->Push(vm, vm.NewString(a));
    return Value(v);
}

inline Value ToValueOfVectorOfStringsEmpty(VM &vm, const int2 &size, char init) {
    auto v = vm.NewVec(0, size.y, TYPE_ELEM_VECTOR_OF_STRING);
    for (int i = 0; i < size.y; i++) {
        auto s = vm.NewString(size.x);
        memset((char *)s->data(), init, size.x);
        v->Push(vm, s);
    }
    return Value(v);
}

void EscapeAndQuote(string_view s, string &sd);

#if !defined(NDEBUG) && RTT_ENABLED
    #define STRINGIFY(x) #x
    #define TOSTRING(x) STRINGIFY(x)
    #define VMASSERT(vm, test) { if (!(test)) vm.VMAssert(__FILE__ ": " TOSTRING(__LINE__) ": " #test); }
#else
    #define VMASSERT(vm, test) { (void)vm; }
#endif

#define RANGECHECK(vm, I, BOUND, VEC) \
    if ((uint64_t)I >= (uint64_t)BOUND) vm.IDXErr(I, BOUND, VEC);

}  // namespace lobster

#endif  // LOBSTER_VMDATA
