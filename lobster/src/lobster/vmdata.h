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

// These are used with VM_COMPILED_CODE_MODE
#define VM_DISPATCH_TRAMPOLINE 1
#define VM_DISPATCH_SWITCH_GOTO 2
#define VM_DISPATCH_METHOD VM_DISPATCH_TRAMPOLINE

#define STRING_CONSTANTS_KEEP 0

#define DELETE_DELAY 0

// Typedefs to make pointers and scalars the same size.
#if _WIN64 || __amd64__ || __x86_64__ || __ppc64__ || __LP64__
    #if !defined(VM_COMPILED_CODE_MODE) || VM_DISPATCH_METHOD != VM_DISPATCH_TRAMPOLINE
        //#define FORCE_32_BIT_MODEL
    #endif
    #ifndef FORCE_32_BIT_MODEL
        #define VALUE_MODEL_64 1
    #else
        #define VALUE_MODEL_64 0
    #endif
#else
    #define VALUE_MODEL_64 0
#endif

#if VALUE_MODEL_64
    typedef int64_t intp;
    typedef uint64_t uintp;
    typedef double floatp;
#else
    typedef int32_t intp;
    typedef uint32_t uintp;
    typedef float floatp;
#endif

typedef vec<floatp, 2> floatp2;
typedef vec<floatp, 3> floatp3;
typedef vec<floatp, 4> floatp4;

typedef vec<intp, 2> intp2;
typedef vec<intp, 3> intp3;
typedef vec<intp, 4> intp4;

const floatp4 floatp4_0 = floatp4(0.0f);
const floatp3 floatp3_0 = floatp3(0.0f);
const floatp2 floatp2_0 = floatp2(0.0f);

const intp2 intp2_0 = intp2((intp)0);
const intp2 intp2_1 = intp2(1);
const intp3 intp3_0 = intp3((intp)0);


enum ValueType : int {
    // refc types are negative
    V_MINVMTYPES = -10,
    V_ANY = -9,         // any other reference type.
    V_STACKFRAMEBUF = -8,
    V_VALUEBUF = -7,    // only used as memory type for vector/coro buffers, not used by Value.
    V_STRUCT_R = -6,
    V_RESOURCE = -5,
    V_COROUTINE = -4,
    V_STRING = -3,
    V_CLASS = -2,
    V_VECTOR = -1,
    V_NIL = 0,          // VM: null reference, Type checker: nillable.
    V_INT,
    V_FLOAT,
    V_FUNCTION,
    V_YIELD,
    V_STRUCT_S,
    V_VAR,              // [typechecker only] like V_ANY, except idx refers to a type variable
    V_TYPEID,           // [typechecker only] a typetable offset.
    V_VOID,             // [typechecker/codegen only] this exp does not produce a value.
    V_TUPLE,            // [typechecker/codegen only] this exp produces >1 value.
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
inline bool IsRuntimePrintable(ValueType t) { return t <= V_FLOAT; }
inline bool IsStruct(ValueType t) { return t == V_STRUCT_R || t == V_STRUCT_S; }
inline bool IsUDT(ValueType t) { return t == V_CLASS || IsStruct(t); }
inline bool IsNillable(ValueType t) { return IsRef(t) && t != V_STRUCT_R; }

inline string_view BaseTypeName(ValueType t) {
    static const char *typenames[] = {
        "any", "<stackframe_buffer>", "<value_buffer>",
        "struct_ref",
        "resource", "coroutine", "string", "class", "vector",
        "nil", "int", "float", "function", "yield_function", "struct_scalar",
        "variable", "typeid", "void",
        "tuple", "undefined",
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

    TYPE_ELEM_FIXED_OFFSET_END = 18
};

struct VM;

struct TypeInfo {
    ValueType t;
    union {
        type_elem_t subt;  // V_VECTOR | V_NIL
        struct {           // V_CLASS, V_STRUCT_*
            int structidx;
            int len;
            int vtable_start;
            type_elem_t elemtypes[1];  // len elems, followed by len parent types.
        };
        int enumidx;       // V_INT, -1 if not an enum.
        int sfidx;         // V_FUNCTION;
        struct {           // V_COROUTINE
            int cofunidx;
            type_elem_t yieldtype;
        };
    };

    TypeInfo() = delete;
    TypeInfo(const TypeInfo &) = delete;
    TypeInfo &operator=(const TypeInfo &) = delete;

    string Debug(VM &vm, bool rec = true) const;

    type_elem_t GetElemOrParent(intp i) const {
        auto pti = elemtypes[len + i];
        return pti >= 0 ? pti : elemtypes[i];
    }
};

struct Value;
struct LString;
struct LVector;
struct LObject;
struct LCoRoutine;

struct PrintPrefs {
    intp depth;
    intp budget;
    bool quoted;
    intp decimals;
    int cycles = -1;
    int indent = 0;
    int cur_indent = 0;

    PrintPrefs(intp _depth, intp _budget, bool _quoted, intp _decimals)
        : depth(_depth), budget(_budget), quoted(_quoted), decimals(_decimals) {}
};

typedef void *(*block_base_t)(VM &);
#if VM_DISPATCH_METHOD == VM_DISPATCH_TRAMPOLINE
    typedef block_base_t block_t;
#elif VM_DISPATCH_METHOD == VM_DISPATCH_SWITCH_GOTO
    typedef intp block_t;
#endif

// ANY memory allocated by the VM must inherit from this, so we can identify leaked memory
struct DynAlloc {
    type_elem_t tti;  // offset into the VM's typetable
    const TypeInfo &ti(VM &vm) const;

    DynAlloc(type_elem_t _tti) : tti(_tti) {}
};

struct RefObj : DynAlloc {
    int refc = 1;

    #if DELETE_DELAY
    const int *alloc_ip;
    #endif

    RefObj(type_elem_t _tti) : DynAlloc(_tti)
        #if DELETE_DELAY
            , alloc_ip(nullptr)
        #endif
    {}

    void Inc() {
        #ifndef NDEBUG
            if (refc <= 0) {  // Should never be "re-vived".
                #if DELETE_DELAY
                    LOG_DEBUG("revive: ", (size_t)this);
                #endif
                assert(false);
            }
        #endif
        #if DELETE_DELAY
            LOG_DEBUG("inc: ", (size_t)this);
        #endif
        refc++;
    }

    void Dec(VM &vm) {
        refc--;
        #ifndef NDEBUG
            DECSTAT(vm);
        #endif
        #if DELETE_DELAY
            LOG_DEBUG("dec: ", (size_t)this);
        #endif
        if (refc <= 0) {
            DECDELETE(vm);
        }
    }

    void CycleStr(ostringstream &ss) const { ss << "_" << -refc << "_"; }

    bool CycleCheck(ostringstream &ss, PrintPrefs &pp) {
        if (pp.cycles >= 0) {
            if (refc < 0) { CycleStr(ss); return true; }
            refc = -(++pp.cycles);
        }
        return false;
    }

    void DECDELETE(VM &vm);
    void DECDELETENOW(VM &vm);
    void DECSTAT(VM &vm);

    intp Hash(VM &vm);
};

extern bool RefEqual(VM &vm, const RefObj *a, const RefObj *b, bool structural);
extern void RefToString(VM &vm, ostringstream &ss, const RefObj *ro, PrintPrefs &pp);

struct LString : RefObj {
    intp len;    // has to match the Value integer type, since we allow the length to be obtained
    LString(intp _l);

    const char *data() const { return (char *)(this + 1); }
    string_view strv() const { return string_view(data(), len); }

    void ToString(ostringstream &ss, PrintPrefs &pp);

    void DeleteSelf(VM &vm);

    bool operator==(LString &o) { return strv() == o.strv(); }
    bool operator!=(LString &o) { return strv() != o.strv(); }
    bool operator< (LString &o) { return strv() <  o.strv(); }
    bool operator<=(LString &o) { return strv() <= o.strv(); }
    bool operator> (LString &o) { return strv() >  o.strv(); }
    bool operator>=(LString &o) { return strv() >= o.strv(); }

    intp Hash();
};

// There must be a single of these per type, since they are compared by pointer.
struct ResourceType {
    const char *name;
    void (* deletefun)(void *);
};

struct LResource : RefObj {
    void *val;
    const ResourceType *type;

    LResource(void *v, const ResourceType *t);

    void DeleteSelf(VM &vm);

    void ToString(ostringstream &ss) {
        ss << "(resource:" << type->name << ")";
    }
};

struct InsPtr {
    #ifdef VM_COMPILED_CODE_MODE
        block_t f;
        explicit InsPtr(block_t _f) : f(_f) {}
        static_assert(sizeof(block_t) == sizeof(intp), "");
    #else
        intp f;
        explicit InsPtr(intp _f) : f(_f) {}
        #ifdef FORCE_32_BIT_MODEL
            explicit InsPtr(ptrdiff_t _f) : f((intp)_f) {}
        #endif
    #endif
    InsPtr() : f(0) {}
    bool operator==(const InsPtr o) const { return f == o.f; }
    bool operator!=(const InsPtr o) const { return f != o.f; }
};

#if RTT_ENABLED
    #define TYPE_INIT(t) type(t),
    #define TYPE_ASSERT(c) assert(c)
#else
    #define TYPE_INIT(t)
    #define TYPE_ASSERT(c)
#endif

// These pointer types are for use inside Value below. In most other parts of the code we
// use naked pointers.
#ifndef FORCE_32_BIT_MODEL
    // We use regular pointers of the current architecture.
    typedef LString *LStringPtr;
    typedef LVector *LVectorPtr;
    typedef LObject *LStructPtr;
    typedef LCoRoutine *LCoRoutinePtr;
    typedef LResource *LResourcePtr;
    typedef RefObj *RefObjPtr;
#else
    // We use a compressed pointer to fit in 32-bit on a 64-bit build.
    // These are shifted by COMPRESS_BITS, so for 3 we can address the bottom 32GB of the
    // address space. The memory allocator for these values must guarantee we only allocate
    // from that region, by using mmap or similar.
    template<typename T> class CompressedPtr {
        uint32_t c;
        enum { COMPRESS_BITS = 3, COMPRESS_MASK = (1 << COMPRESS_BITS) - 1 };
      public:
        CompressedPtr(const T *p) {
            auto bits = (size_t)p;
            assert(!(bits & COMPRESS_MASK));  // Must not have low bits set.
            bits >>= COMPRESS_BITS;
            assert(!(bits >> 32));  // Must not have high bits set.
            c = (uint32_t)bits;
        }
        T *get() const { return (T *)(((size_t)c) << COMPRESS_BITS); }
        operator T *() const { return get(); }
        T *operator->() const { return get(); }
    };
    typedef CompressedPtr<LString> LStringPtr;
    typedef CompressedPtr<LVector> LVectorPtr;
    typedef CompressedPtr<LObject> LStructPtr;
    typedef CompressedPtr<LCoRoutine> LCoRoutinePtr;
    typedef CompressedPtr<LResource> LResourcePtr;
    typedef CompressedPtr<BoxedInt> BoxedIntPtr;
    typedef CompressedPtr<BoxedFloat> BoxedFloatPtr;
    typedef CompressedPtr<RefObj> RefObjPtr;
#endif

static_assert(sizeof(intp) == sizeof(floatp) && sizeof(intp) == sizeof(RefObjPtr),
              "typedefs need fixing");

struct Value {
    #if RTT_ENABLED
    ValueType type;
    #endif

    private:
    union {
        // All these types can be defined to be either all 32 or 64-bit, depending on the
        // compilation mode.

        // Non-reference values.
        intp ival_;      // scalars stored as pointer-sized versions.
        floatp fval_;
        InsPtr ip_;

        // Reference values (includes NULL if nillable version).
        LStringPtr sval_;
        LVectorPtr vval_;
        LStructPtr oval_;
        LCoRoutinePtr cval_;
        LResourcePtr xval_;

        // Generic reference access.
        RefObjPtr ref_;

        // Temp: for inline structs.
        TypeInfo *ti_;
    };
    public:

    // These asserts help track down any invalid code generation issues.
    intp        ival  () const { TYPE_ASSERT(type == V_INT);        return ival_;        }
    floatp      fval  () const { TYPE_ASSERT(type == V_FLOAT);      return fval_;        }
    int         intval() const { TYPE_ASSERT(type == V_INT);        return (int)ival_;   }
    float       fltval() const { TYPE_ASSERT(type == V_FLOAT);      return (float)fval_; }
    LString    *sval  () const { TYPE_ASSERT(type == V_STRING);     return sval_;        }
    LVector    *vval  () const { TYPE_ASSERT(type == V_VECTOR);     return vval_;        }
    LObject    *oval  () const { TYPE_ASSERT(type == V_CLASS);      return oval_;        }
    LCoRoutine *cval  () const { TYPE_ASSERT(type == V_COROUTINE);  return cval_;        }
    LResource  *xval  () const { TYPE_ASSERT(type == V_RESOURCE);   return xval_;        }
    RefObj     *ref   () const { TYPE_ASSERT(IsRef(type));          return ref_;         }
    RefObj     *refnil() const { TYPE_ASSERT(IsRefNil(type));       return ref_;         }
    InsPtr      ip    () const { TYPE_ASSERT(type >= V_FUNCTION);   return ip_;          }
    void       *any   () const {                                    return ref_;         }
    TypeInfo   *tival () const { TYPE_ASSERT(type == V_STRUCT_S);   return ti_;          }

    template<typename T> T ifval() const {
        if constexpr (is_floating_point<T>()) { TYPE_ASSERT(type == V_FLOAT); return (T)fval_; }
        else                                  { TYPE_ASSERT(type == V_INT);   return (T)ival_; }
    }

    void setival(intp i)   { TYPE_ASSERT(type == V_INT);   ival_ = i; }
    void setfval(floatp f) { TYPE_ASSERT(type == V_FLOAT); fval_ = f; }

    inline Value()                   : TYPE_INIT(V_NIL)        ref_(nullptr)    {}
    inline Value(int i)              : TYPE_INIT(V_INT)        ival_(i)         {}
    inline Value(uint i)             : TYPE_INIT(V_INT)        ival_((intp)i)   {}
    inline Value(int64_t i)          : TYPE_INIT(V_INT)        ival_((intp)i)   {}
    inline Value(uint64_t i)         : TYPE_INIT(V_INT)        ival_((intp)i)   {}
    inline Value(int i, ValueType t) : TYPE_INIT(t)            ival_(i)         { (void)t; }
    inline Value(bool b)             : TYPE_INIT(V_INT)        ival_(b)         {}
    inline Value(float f)            : TYPE_INIT(V_FLOAT)      fval_(f)         {}
    inline Value(double f)           : TYPE_INIT(V_FLOAT)      fval_((floatp)f) {}
    inline Value(InsPtr i)           : TYPE_INIT(V_FUNCTION)   ip_(i)           {}

    inline Value(LString *s)         : TYPE_INIT(V_STRING)     sval_(s)         {}
    inline Value(LVector *v)         : TYPE_INIT(V_VECTOR)     vval_(v)         {}
    inline Value(LObject *s)         : TYPE_INIT(V_CLASS)      oval_(s)         {}
    inline Value(LCoRoutine *c)      : TYPE_INIT(V_COROUTINE)  cval_(c)         {}
    inline Value(LResource *r)       : TYPE_INIT(V_RESOURCE)   xval_(r)         {}
    inline Value(RefObj *r)          : TYPE_INIT(V_NIL)        ref_(r)          { assert(false); }

    inline Value(TypeInfo *ti)       : TYPE_INIT(V_STRUCT_S)   ti_(ti)          {}

    inline bool True() const { return ival_ != 0; }

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

    void ToString(VM &vm, ostringstream &ss, const TypeInfo &ti, PrintPrefs &pp) const;
    void ToStringBase(VM &vm, ostringstream &ss, ValueType t, PrintPrefs &pp) const;

    bool Equal(VM &vm, ValueType vtype, const Value &o, ValueType otype, bool structural) const;
    intp Hash(VM &vm, ValueType vtype);
    Value Copy(VM &vm);  // Shallow.
};

template<typename T> inline T *AllocSubBuf(VM &vm, size_t size, type_elem_t tti);
template<typename T> inline void DeallocSubBuf(VM &vm, T *v, size_t size);

struct LObject : RefObj {
    LObject(type_elem_t _tti) : RefObj(_tti) {}

    // FIXME: reduce the use of these.
    intp Len(VM &vm) const { return ti(vm).len; }

    Value *Elems() const { return (Value *)(this + 1); }

    // This may only be called from a context where i < len has already been ensured/asserted.
    Value &AtS(intp i) const {
        return Elems()[i];
    }

    void DeleteSelf(VM &vm);

    // This may only be called from a context where i < len has already been ensured/asserted.
    const TypeInfo &ElemTypeS(VM &vm, intp i) const;
    const TypeInfo &ElemTypeSP(VM &vm, intp i) const;

    void ToString(VM &vm, ostringstream &ss, PrintPrefs &pp);

    bool Equal(VM &vm, const LObject &o) {
        // RefObj::Equal has already guaranteed the typeoff's are the same.
        auto len = Len(vm);
        assert(len == o.Len(vm));
        for (intp i = 0; i < len; i++) {
            auto et = ElemTypeS(vm, i).t;
            if (!AtS(i).Equal(vm, et, o.AtS(i), et, true))
                return false;
        }
        return true;
    }

    intp Hash(VM &vm) {
        intp hash = 0;
        for (int i = 0; i < Len(vm); i++) hash ^= AtS(i).Hash(vm, ElemTypeS(vm, i).t);
        return hash;
    }

    void Init(VM &vm, Value *from, intp len, bool inc) {
        assert(len && len == Len(vm));
        t_memcpy(Elems(), from, len);
        if (inc) for (intp i = 0; i < len; i++) {
            AtS(i).LTINCTYPE(ElemTypeS(vm, i).t);
        }
    }
};

struct LVector : RefObj {
    intp len;    // has to match the Value integer type, since we allow the length to be obtained
    intp maxl;
    intp width;  // TODO: would be great to not have to store this.

    private:
    Value *v;   // use At()

    public:
    LVector(VM &vm, intp _initial, intp _max, type_elem_t _tti);

    ~LVector() { assert(0); }   // destructed by DECREF

    void DeallocBuf(VM &vm) {
        if (v) DeallocSubBuf(vm, v, maxl * width);
    }

    void DecSlot(VM &vm, intp i, ValueType et) const {
        AtSlot(i).LTDECTYPE(vm, et);
    }

    void DeleteSelf(VM &vm);

    const TypeInfo &ElemType(VM &vm) const;

    void Resize(VM &vm, intp newmax);

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

    void Insert(VM &vm, const Value *vals, intp i) {
        assert(i >= 0 && i <= len); // note: insertion right at the end is legal, hence <=
        if (len + 1 > maxl) Resize(vm, max(len + 1, maxl ? maxl * 2 : 4));
        t_memmove(v + (i + 1) * width, v + i * width, (len - i) * width);
        len++;
        tsnz_memcpy(v + i * width, vals, width);
    }

    void Remove(VM &vm, intp i, intp n, intp decfrom, bool stack_ret);

    Value *Elems() { return v; }

    Value &At(intp i) const {
        assert(i < len && width == 1);
        return v[i];
    }

    Value *AtSt(intp i) const {
        assert(i < len);
        return v + i * width;
    }

    Value &AtSlot(intp i) const {
        assert(i < len * width);
        return v[i];
    }

    void AtVW(VM &vm, intp i) const;
    void AtVWSub(VM &vm, intp i, int w, int off) const;

    void Append(VM &vm, LVector *from, intp start, intp amount);

    void ToString(VM &vm, ostringstream &ss, PrintPrefs &pp);

    bool Equal(VM &vm, const LVector &o) {
        // RefObj::Equal has already guaranteed the typeoff's are the same.
        assert(width == 1);
        if (len != o.len) return false;
        auto et = ElemType(vm).t;
        for (intp i = 0; i < len; i++) {
            if (!At(i).Equal(vm, et, o.At(i), et, true))
                return false;
        }
        return true;
    }

    intp Hash(VM &vm) {
        intp hash = 0;
        assert(width == 1);
        auto et = ElemType(vm).t;
        for (int i = 0; i < len; i++) hash ^= At(i).Hash(vm, et);
        return hash;
    }

    void Init(VM &vm, Value *from, bool inc) {
        assert(len);
        t_memcpy(v, from, len * width);
        auto et = ElemType(vm).t;
        if (inc && IsRefNil(et)) {
            for (intp i = 0; i < len; i++) {
                At(i).LTINCRTNIL();
            }
        }
    }
};

struct VMLog {
    struct LogVar {
        vector<Value> values;
        size_t read;
        const TypeInfo *type;
    };
    vector<LogVar> logvars;

    VM &vm;
    VMLog(VM &_vm);

    void LogInit(const uchar *bcf);
    void LogPurge();
    void LogFrame();
    Value LogGet(Value def, int idx);
    void LogWrite(Value newval, int idx);
    void LogCleanup();
};

struct StackFrame {
    InsPtr retip;
    const int *funstart;
    int spstart;
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
    string bytecode_buffer;
    const void *entry_point = nullptr;
    const void *static_bytecode = nullptr;
    size_t static_size = 0;
    vector<string> program_args;
    const lobster::block_t *native_vtables = nullptr;
    TraceMode trace = TraceMode::OFF;
};

struct VM : VMArgs {
    SlabAlloc pool;

    Value *stack = nullptr;
    int stacksize = 0;
    int maxstacksize;
    int sp = -1;

    Value retvalstemp[MAX_RETURN_VALUES];

    #ifdef VM_COMPILED_CODE_MODE
        block_t next_call_target = 0;
    #else
        const int *ip = nullptr;
    #endif

    vector<StackFrame> stackframes;

    LCoRoutine *curcoroutine = nullptr;

    Value *vars = nullptr;

    size_t codelen = 0;
    const int *codestart = nullptr;
    vector<int> codebigendian;
    vector<type_elem_t> typetablebigendian;
    uint64_t *byteprofilecounts = nullptr;

    const bytecode::BytecodeFile *bcf = nullptr;

    PrintPrefs programprintprefs { 10, 100000, false, -1 };
    const type_elem_t *typetable = nullptr;
    string evalret;

    int currentline = -1;
    int maxsp = -1;

    PrintPrefs debugpp { 2, 50, true, -1 };

    VMLog vml { *this };

    ostringstream ss_reuse;

    vector<ostringstream> trace_output;
    size_t trace_ring_idx = 0;

    vector<RefObj *> delete_delay;

    vector<LString *> constant_strings;

    vector<InsPtr> vtables;

    int64_t vm_count_ins = 0;
    int64_t vm_count_fcalls = 0;
    int64_t vm_count_bcalls = 0;
    int64_t vm_count_decref = 0;

    //#define VM_ERROR_RET_EXPERIMENT
    #if defined(VM_ERROR_RET_EXPERIMENT) && !defined(VM_COMPILED_CODE_MODE)
        #define VM_INS_RET bool
        #define VM_RET return false
        #define VM_TERMINATE return true
    #else
        #define VM_INS_RET void
        #define VM_RET
        #define VM_TERMINATE
    #endif

    typedef VM_INS_RET (VM::* f_ins_pointer)();
    f_ins_pointer f_ins_pointers[IL_MAX_OPS];

    const void *compiled_code_ip = nullptr;

    bool is_worker = false;
    vector<thread> workers;
    TupleSpace *tuple_space = nullptr;

    VM(VMArgs &&args);
    ~VM();

    void OneMoreFrame();

    const TypeInfo &GetTypeInfo(type_elem_t offset) {
        return *(TypeInfo *)(typetable + offset);
    }
    const TypeInfo &GetVarTypeInfo(int varidx);

    void SetMaxStack(int ms) { maxstacksize = ms; }
    string_view GetProgramName() { return programname; }

    type_elem_t GetIntVectorType(int which);
    type_elem_t GetFloatVectorType(int which);

    void DumpVal(RefObj *ro, const char *prefix);
    void DumpFileLine(const int *fip, ostringstream &ss);
    void DumpLeaks();

    ostringstream &TraceStream();

    void OnAlloc(RefObj *ro);
    LVector *NewVec(intp initial, intp max, type_elem_t tti);
    LObject *NewObject(intp max, type_elem_t tti);
    LCoRoutine *NewCoRoutine(InsPtr rip, const int *vip, LCoRoutine *p, type_elem_t tti);
    LResource *NewResource(void *v, const ResourceType *t);
    LString *NewString(size_t l);
    LString *NewString(string_view s);
    LString *NewString(string_view s1, string_view s2);
    LString *ResizeString(LString *s, intp size, int c, bool back);

    Value Error(string err, const RefObj *a = nullptr, const RefObj *b = nullptr);
    Value BuiltinError(string err) { return Error(err); }
    void VMAssert(const char *what);
    void VMAssert(const char *what, const RefObj *a, const RefObj *b);

    int DumpVar(ostringstream &ss, const Value &x, size_t idx);

    void FinalStackVarsCleanup();

    void StartWorkers(size_t numthreads);
    void TerminateWorkers();
    void WorkerWrite(RefObj *ref);
    LObject *WorkerRead(type_elem_t tti);

    #ifdef VM_COMPILED_CODE_MODE
        #define VM_COMMA ,
        #define VM_OP_ARGS const int *ip
        #define VM_OP_ARGS_CALL block_t fcont
        #define VM_IP_PASS_THRU ip
        #define VM_FC_PASS_THRU fcont
        #define VM_JMP_RET bool
    #else
        #define VM_COMMA
        #define VM_OP_ARGS
        #define VM_OP_ARGS_CALL
        #define VM_IP_PASS_THRU
        #define VM_FC_PASS_THRU
        #define VM_JMP_RET VM_INS_RET
    #endif

    void JumpTo(InsPtr j);
    InsPtr GetIP();
    template<int is_error> int VarCleanup(ostringstream *error, int towhere);
    void StartStackFrame(InsPtr retip);
    void FunIntroPre(InsPtr fun);
    void FunIntro(VM_OP_ARGS);
    void FunOut(int towhere, int nrv);

    void CoVarCleanup(LCoRoutine *co);
    void CoNonRec(const int *varip);
    void CoNew(VM_OP_ARGS VM_COMMA VM_OP_ARGS_CALL);
    void CoSuspend(InsPtr retip);
    void CoClean();
    void CoYield(VM_OP_ARGS_CALL);
    void CoResume(LCoRoutine *co);

    void EndEval(const Value &ret, const TypeInfo &ti);

    void InstructionPointerInit() {
        #ifdef VM_COMPILED_CODE_MODE
            #define F(N, A) f_ins_pointers[IL_##N] = nullptr;
        #else
            #define F(N, A) f_ins_pointers[IL_##N] = &VM::F_##N;
        #endif
        ILNAMES
        #undef F
    }

    #define VM_OP_ARGS0
    #define VM_OP_ARGS1 int _a
    #define VM_OP_ARGS2 int _a, int _b
    #define VM_OP_ARGS3 int _a, int _b, int _c
    #define VM_OP_ARGS9 VM_OP_ARGS  // ILUNKNOWNARITY
    #define VM_OP_ARGSN(N) VM_OP_ARGS##N
    #define VM_OP_DEFS0
    #define VM_OP_DEFS1 int _a = *ip++;
    #define VM_OP_DEFS2 int _a = *ip++; int _b = *ip++;
    #define VM_OP_DEFS3 int _a = *ip++; int _b = *ip++; int _c = *ip++;
    #define VM_OP_DEFS9  // ILUNKNOWNARITY
    #define VM_OP_DEFSN(N) VM_OP_DEFS##N (void)ip;
    #define VM_OP_PASS0
    #define VM_OP_PASS1 _a
    #define VM_OP_PASS2 _a, _b
    #define VM_OP_PASS3 _a, _b, _c
    #define VM_OP_PASS9 VM_IP_PASS_THRU  // ILUNKNOWNARITY
    #define VM_OP_PASSN(N) VM_OP_PASS##N
    #define VM_COMMA_0
    #define VM_COMMA_1 ,
    #define VM_COMMA_2 ,
    #define VM_COMMA_3 ,
    #define VM_COMMA_9 ,
    #define VM_COMMA_IF(N) VM_COMMA_##N
    #define VM_CCOMMA_0
    #define VM_CCOMMA_1 VM_COMMA
    #define VM_CCOMMA_2 VM_COMMA
    #define VM_CCOMMA_9 VM_COMMA
    #define VM_CCOMMA_IF(N) VM_CCOMMA_##N

    #define F(N, A) VM_INS_RET U_##N(VM_OP_ARGSN(A)); \
                    VM_INS_RET F_##N(VM_OP_ARGS) { \
                        VM_OP_DEFSN(A); \
                        return U_##N(VM_OP_PASSN(A)); \
                    }
        LVALOPNAMES
    #undef F
    #define F(N, A) VM_INS_RET U_##N(VM_OP_ARGSN(A)); \
                    VM_INS_RET F_##N(VM_OP_ARGS) { \
                        VM_OP_DEFSN(A); \
                        return U_##N(VM_OP_PASSN(A)); \
                    }
        ILBASENAMES
    #undef F
    #define F(N, A) VM_INS_RET U_##N(VM_OP_ARGSN(A) VM_CCOMMA_IF(A) VM_OP_ARGS_CALL); \
                    VM_INS_RET F_##N(VM_OP_ARGS VM_COMMA VM_OP_ARGS_CALL) { \
                        VM_OP_DEFSN(A); \
                        return U_##N(VM_OP_PASSN(A) VM_CCOMMA_IF(A) VM_FC_PASS_THRU); \
                    }
        ILCALLNAMES
    #undef F
    #define F(N, A) VM_JMP_RET U_##N(); VM_JMP_RET F_##N() { return U_##N(); }
        ILJUMPNAMES
    #undef F

    #pragma push_macro("LVAL")
    #undef LVAL
    #define LVAL(N, V) void LV_##N(Value &a VM_COMMA_IF(V) VM_OP_ARGSN(V));
        LVALOPNAMES
    #undef LVAL
    #pragma pop_macro("LVAL")

    void EvalProgram();
    void EvalProgramInner();

    VM_JMP_RET ForLoop(intp len);

    Value &GetFieldLVal(intp i);
    Value &GetFieldILVal(intp i);
    Value &GetLocLVal(int i);
    Value &GetVecLVal(intp i);

    void PushDerefIdxVector(intp i);
    void PushDerefIdxVectorSub(intp i, int width, int offset);
    void PushDerefIdxStruct(intp i, int l);
    void PushDerefIdxString(intp i);
    void LvalueIdxVector(int lvalop, intp i);
    void LvalueIdxStruct(int lvalop, intp i);
    void LvalueField(int lvalop, intp i);
    void LvalueOp(int op, Value &a);

    string ProperTypeName(const TypeInfo &ti);

    void Div0() { Error("division by zero"); }
    void IDXErr(intp i, intp n, const RefObj *v);
    void BCallProf();
    void BCallRetCheck(const NativeFun *nf);
    intp GrabIndex(int len);

    #define VM_PUSH(v) (stack[++sp] = (v))
    #define VM_TOP() (stack[sp])
    #define VM_TOPM(n) (stack[sp - (n)])
    #define VM_POP() (stack[sp--])
    #define VM_POPN(n) (sp -= (n))
    #define VM_PUSHN(n) (sp += (n))
    #define VM_TOPPTR() (stack + sp + 1)

    void Push(const Value &v) { VM_PUSH(v); }
    Value Pop() { return VM_POP(); }
    Value Top() { return VM_TOP(); }
    Value *TopPtr() { return VM_TOPPTR(); }
    void PushN(int n) { VM_PUSHN(n); }
    void PopN(int n) { VM_POPN(n); }
    pair<Value *, int> PopVecPtr() {
        auto width = VM_POP().intval();
        VM_POPN(width);
        return { VM_TOPPTR(), width };
    }
    template<typename T, int N> void PushVec(const vec<T, N> &v, int truncate = 4) {
        auto l = min(N, truncate);
        for (int i = 0; i < l; i++) VM_PUSH(v[i]);
    }
    template<typename T> T PopVec(typename T::CTYPE def = 0) {
        T v;
        auto l = VM_POP().intval();
        if (l > T::NUM_ELEMENTS) VM_POPN(l - T::NUM_ELEMENTS);
        for (int i = T::NUM_ELEMENTS - 1; i >= 0; i--) {
            v[i] = i < l ? VM_POP().ifval<typename T::CTYPE>() : def;
        }
        return v;
    }
    template<typename T> void PushAnyAsString(const T &t) {
        Push(NewString(string_view((char *)&t, sizeof(T))));
    }

    template<typename T> void PopAnyFromString(T &t) {
        auto s = Pop();
        assert(s.type == V_STRING);
        assert(s.sval()->len == sizeof(T));
        t = *(T *)s.sval()->strv().data();
        s.LTDECRT(*this);
    }

    string_view StructName(const TypeInfo &ti);
    string_view ReverseLookupType(uint v);
    void Trace(TraceMode m) { trace = m; }
    double Time() { return SecondsSinceStart(); }

    Value ToString(const Value &a, const TypeInfo &ti) {
        ss_reuse.str(string());
        ss_reuse.clear();
        a.ToString(*this, ss_reuse, ti, programprintprefs);
        return NewString(ss_reuse.str());
    }
    Value StructToString(const Value *elems, const TypeInfo &ti) {
        ss_reuse.str(string());
        ss_reuse.clear();
        StructToString(ss_reuse, programprintprefs, ti, elems);
        return NewString(ss_reuse.str());
    }
    void StructToString(ostringstream &ss, PrintPrefs &pp, const TypeInfo &ti, const Value *elems);

    string_view EnumName(intp val, int enumidx);
    string_view EnumName(int enumidx);
    optional<int64_t> LookupEnum(string_view name, int enumidx);
};

inline int64_t Int64FromInts(int a, int b) {
    int64_t v = (uint)a;
    v |= ((int64_t)b) << 32;
    return v;
}

inline const TypeInfo &DynAlloc::ti(VM &vm) const { return vm.GetTypeInfo(tti); }

template<typename T> inline T *AllocSubBuf(VM &vm, size_t size, type_elem_t tti) {
    auto header_sz = max(alignof(T), sizeof(DynAlloc));
    auto mem = (uchar *)vm.pool.alloc(size * sizeof(T) + header_sz);
    ((DynAlloc *)mem)->tti = tti;
    mem += header_sz;
    return (T *)mem;
}

template<typename T> inline void DeallocSubBuf(VM &vm, T *v, size_t size) {
    auto header_sz = max(alignof(T), sizeof(DynAlloc));
    auto mem = ((uchar *)v) - header_sz;
    vm.pool.dealloc(mem, size * sizeof(T) + header_sz);
}

template<bool back> LString *WriteMem(VM &vm, LString *s, intp i, const void *data, size_t size) {
    auto minsize = i + (intp)size;
    if (s->len < minsize) s = vm.ResizeString(s, minsize * 2, 0, back);
    memcpy((void *)(s->data() + (back ? s->len - i - size : i)), data, size);
    return s;
}

template<typename T, bool back> LString *WriteValLE(VM &vm, LString *s, intp i, T val) {
    T t = flatbuffers::EndianScalar(val);
    return WriteMem<back>(vm, s, i, &t, sizeof(T));
}

template<typename T, bool back> T ReadValLE(const LString *s, intp i) {
    T val;
    memcpy(&val, (void *)(s->data() + (back ? s->len - i - sizeof(T) : i)), sizeof(T));
    return flatbuffers::EndianScalar(val);
}


// FIXME: turn check for len into an assert and make caller guarantee lengths match.
template<int N> inline vec<floatp, N> ValueToF(const Value *v, intp width, floatp def = 0) {
    vec<floatp, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->fval() : def;
    return t;
}
template<int N> inline vec<intp, N> ValueToI(const Value *v, intp width, intp def = 0) {
    vec<intp, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->ival() : def;
    return t;
}
template<int N> inline vec<float, N> ValueToFLT(const Value *v, intp width, float def = 0) {
    vec<float, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->fltval() : def;
    return t;
}
template<int N> inline vec<int, N> ValueToINT(const Value *v, intp width, int def = 0) {
    vec<int, N> t;
    for (int i = 0; i < N; i++) t[i] = width > i ? (v + i)->intval() : def;
    return t;
}

template <typename T, int N> inline void ToValue(Value *dest, intp width, const vec<T, N> &v) {
    for (intp i = 0; i < width; i++) dest[i] = i < N ? v[i] : 0;
}

inline intp RangeCheck(VM &vm, const Value &idx, intp range, intp bias = 0) {
    auto i = idx.ival();
    if (i < bias || i >= bias + range)
        vm.BuiltinError(cat("index out of range [", bias, "..", bias + range, "): ", i));
    return i;
}

template<typename T> inline T GetResourceDec(VM &vm, const Value &val, const ResourceType *type) {
    if (!val.True())
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
    auto v = vm.NewVec(0, (intp)in.size(), TYPE_ELEM_VECTOR_OF_STRING);
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

void EscapeAndQuote(string_view s, ostringstream &ss);

struct LCoRoutine : RefObj {
    bool active = true;  // Goes to false when it has hit the end of the coroutine instead of a yield.

    int stackstart;    // When currently running, otherwise -1
    Value *stackcopy = nullptr;
    int stackcopylen = 0;
    int stackcopymax = 0;

    int stackframestart;  // When currently running, otherwise -1
    StackFrame *stackframescopy = nullptr;
    int stackframecopylen = 0;
    int stackframecopymax = 0;
    int top_at_suspend = -1;

    InsPtr returnip;
    const int *varip;
    LCoRoutine *parent;

    LCoRoutine(int _ss, int _sfs, InsPtr _rip, const int *_vip, LCoRoutine *_p, type_elem_t cti)
        : RefObj(cti), stackstart(_ss), stackframestart(_sfs), returnip(_rip), varip(_vip),
          parent(_p) {}

    Value &Current(VM &vm) {
        if (stackstart >= 0) vm.BuiltinError("cannot get value of active coroutine");
        return stackcopy[stackcopylen - 1].LTINCTYPE(vm.GetTypeInfo(ti(vm).yieldtype).t);
    }

    void Resize(VM &vm, int newlen) {
        if (newlen > stackcopymax) {
            if (stackcopy) DeallocSubBuf(vm, stackcopy, stackcopymax);
            stackcopy = AllocSubBuf<Value>(vm, stackcopymax = newlen, TYPE_ELEM_VALUEBUF);
        }
        stackcopylen = newlen;
    }

    void ResizeFrames(VM &vm, int newlen) {
        if (newlen > stackframecopymax) {
            if (stackframescopy) DeallocSubBuf(vm, stackframescopy, stackframecopymax);
            stackframescopy = AllocSubBuf<StackFrame>(vm, stackframecopymax = newlen,
                                                      TYPE_ELEM_STACKFRAMEBUF);
        }
        stackframecopylen = newlen;
    }

    int Suspend(VM &vm, int top, Value *stack, vector<StackFrame> &stackframes, InsPtr &rip,
                LCoRoutine *&curco) {
        assert(stackstart >= 0);
        swap(rip, returnip);
        assert(curco == this);
        curco = parent;
        parent = nullptr;
        ResizeFrames(vm, (int)stackframes.size() - stackframestart);
        t_memcpy(stackframescopy, stackframes.data() + stackframestart, stackframecopylen);
        stackframes.erase(stackframes.begin() + stackframestart, stackframes.end());
        stackframestart = -1;
        top_at_suspend = top;
        Resize(vm, top - stackstart);
        t_memcpy(stackcopy, stack + stackstart, stackcopylen);
        int ss = stackstart;
        stackstart = -1;
        return ss;
    }

    void AdjustStackFrames(int top) {
        int topdelta = (top + stackcopylen) - top_at_suspend;
        if (topdelta) {
            for (int i = 0; i < stackframecopylen; i++) {
                stackframescopy[i].spstart += topdelta;
            }
        }
    }

    int Resume(int top, Value *stack, vector<StackFrame> &stackframes, InsPtr &rip, LCoRoutine *p) {
        assert(stackstart < 0);
        swap(rip, returnip);
        assert(!parent);
        parent = p;
        stackframestart = (int)stackframes.size();
        AdjustStackFrames(top);
        stackframes.insert(stackframes.end(), stackframescopy, stackframescopy + stackframecopylen);
        stackstart = top;
        // FIXME: assume that it fits, which is not guaranteed with recursive coros
        t_memcpy(stack + top, stackcopy, stackcopylen);
        return stackcopylen;
    }

    void BackupParentVars(VM &vm, Value *vars) {
        // stored here while coro is active
        Resize(vm, *varip);
        for (int i = 1; i <= *varip; i++) {
            auto &var = vars[varip[i]];
            // we don't INC, since parent var is still on the stack and will hold ref
            stackcopy[i - 1] = var;
        }
    }

    Value &AccessVar(int savedvaridx) {
        assert(stackstart < 0);
        // Variables are always saved on top of the stack before the stackcopy gets made, so they
        // are last, followed by the retval (thus -1).
        return stackcopy[stackcopylen - *varip + savedvaridx - 1];
    }

    Value &GetVar(VM &vm, int ididx) {
        if (stackstart >= 0)
            vm.BuiltinError("cannot access locals of running coroutine");
        // FIXME: we can probably make it work without this search, but for now no big deal
        for (int i = 1; i <= *varip; i++) {
            if (varip[i] == ididx) {
                return AccessVar(i - 1);
            }
        }
        // This one should be really rare, since parser already only allows lexically contained vars
        // for that function, could happen when accessing var that's not in the callchain of yields.
        vm.BuiltinError("local variable being accessed is not part of coroutine state");
        return *stackcopy;
    }

    void DeleteSelf(VM &vm) {
        assert(stackstart < 0);
        if (stackcopy) {
            auto curvaltype = vm.GetTypeInfo(ti(vm).yieldtype).t;
            auto &ts = stackcopy[--stackcopylen];
            ts.LTDECTYPE(vm, curvaltype);
            if (active) {
                for (int i = *varip; i > 0; i--) {
                    auto &vti = vm.GetVarTypeInfo(varip[i]);
                    stackcopy[--stackcopylen].LTDECTYPE(vm, vti.t);
                }
                top_at_suspend -= *varip + 1;
                // This calls Resume() to get the rest back onto the stack, then unwinds it.
                vm.CoVarCleanup(this);
            } else {
               assert(!stackcopylen);
            }
            DeallocSubBuf(vm, stackcopy, stackcopymax);
        }
        if (stackframescopy) DeallocSubBuf(vm, stackframescopy, stackframecopymax);
        vm.pool.dealloc(this, sizeof(LCoRoutine));
    }

    ValueType ElemType(VM &vm, int i) {
        assert(i < *varip);
        auto varidx = varip[i + 1];
        auto &vti = vm.GetVarTypeInfo(varidx);
        auto vt = vti.t;
        if (vt == V_NIL) vt = vm.GetTypeInfo(vti.subt).t;
        #if RTT_ENABLED
        auto &var = AccessVar(i);
        // FIXME: For testing.
        if(vt != var.type && var.type != V_NIL && !(vt == V_VECTOR && IsUDT(var.type))) {
            LOG_INFO("coro elem ", vti.Debug(vm), " != ", BaseTypeName(var.type));
            assert(false);
        }
        #endif
        return vt;
    }
};

}  // namespace lobster

#endif  // LOBSTER_VMDATA
