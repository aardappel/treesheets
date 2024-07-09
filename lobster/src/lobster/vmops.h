#include "lobster/geom.h"
#include "lobster/vmdata.h"
#include "lobster/natreg.h"
#include "lobster/bytecode_generated.h"

extern string BreakPoint(lobster::VM &vm, string_view reason);

namespace lobster {

#define VM_OP_ARGS0
#define VM_OP_ARGS1 int _a
#define VM_OP_ARGS2 int _a, int _b
#define VM_OP_ARGS3 int _a, int _b, int _c
#define VM_OP_ARGS999999 const int *ip  // ILUNKNOWN
#define VM_OP_ARGSN(N) VM_OP_ARGS##N
#define VM_OP_PASS0
#define VM_OP_PASS1 _a
#define VM_OP_PASS2 _a, _b
#define VM_OP_PASS3 _a, _b, _c
#define VM_OP_PASS999999 ip  // ILUNKNOWN
#define VM_OP_PASSN(N) VM_OP_PASS##N
#define VM_COMMA_0
#define VM_COMMA_1 ,
#define VM_COMMA_2 ,
#define VM_COMMA_3 ,
#define VM_COMMA_999999 ,  // ILUNKNOWN
#define VM_COMMA_IF(N) VM_COMMA_##N

#if RTT_ENABLED
    #define VMTYPEEQ(val, vt) VMASSERT(vm, (val).type == (vt))
#else
    #define VMTYPEEQ(val, vt) { (void)(val); (void)(vt); (void)vm; }
#endif

VM_INLINE void PushDerefIdxVector1(VM &vm, StackPtr &sp, iint i) {
    Value r = Pop(sp);
    VMASSERT(vm, r.ref());
    auto v = r.vval();
    RANGECHECK(vm, i, v->len, v);
    Push(sp, v->At(i));
}

VM_INLINE void PushDerefIdxVector2V(VM &vm, StackPtr &sp, iint i) {
    Value r = Pop(sp);
    VMASSERT(vm, r.ref());
    auto v = r.vval();
    RANGECHECK(vm, i, v->len, v);
    v->AtVW(sp, i);
}

VM_INLINE void PushDerefIdxVectorSub1(VM &vm, StackPtr &sp, iint i, int offset) {
    Value r = Pop(sp);
    VMASSERT(vm, r.ref());
    auto v = r.vval();
    RANGECHECK(vm, i, v->len, v);
    Push(sp, v->AtSub(i, offset));
}

VM_INLINE void PushDerefIdxVectorSub2V(VM &vm, StackPtr &sp, iint i, int width, int offset) {
    Value r = Pop(sp);
    VMASSERT(vm, r.ref());
    auto v = r.vval();
    RANGECHECK(vm, i, v->len, v);
    v->AtVWSub(sp, i, width, offset);
}

VM_INLINE void PushDerefIdxStruct(StackPtr &sp, iint i, int l) {
    PopN(sp, l);
    auto val = *(TopPtr(sp) + i);
    Push(sp, val);
}

VM_INLINE void PushDerefIdxString(VM &vm, StackPtr &sp, iint i) {
    Value r = Pop(sp);
    VMASSERT(vm, r.ref());
    // Allow access of the terminating 0-byte.
    RANGECHECK(vm, i, r.sval()->len + 1, r.sval());
    Push(sp, Value(((uint8_t *)r.sval()->data())[i]));
}

VM_INLINE Value &GetFieldLVal(VM &vm, StackPtr &sp, iint i) {
    Value vec = Pop(sp);
    #ifndef NDEBUG
        RANGECHECK(vm, i, vec.oval()->Len(vm), vec.oval());
    #else
        (void)vm;
    #endif
    return vec.oval()->AtS(i);
}

VM_INLINE Value &GetFieldILVal(VM &vm, StackPtr &sp, iint i) {
    Value vec = Pop(sp);
    RANGECHECK(vm, i, vec.oval()->Len(vm), vec.oval());
    return vec.oval()->AtS(i);
}

VM_INLINE Value &GetVecLVal(VM &vm, StackPtr &sp, iint i) {
    Value vec = Pop(sp);
    auto v = vec.vval();
    RANGECHECK(vm, i, v->len, v);
    return *v->AtSt(i);
}

VM_INLINE void U_PUSHINT(VM &, StackPtr sp, int x) {
    Push(sp, Value(x));
}

VM_INLINE void U_PUSHFLT(VM &, StackPtr sp, int x) {
    Push(sp, Value(int2float(x).f));
}

VM_INLINE void U_PUSHNIL(VM &, StackPtr sp) {
    Push(sp, NilVal());
}

VM_INLINE void U_PUSHINT64(VM &, StackPtr sp, int a, int b) {
    auto v = Int64FromInts(a, b);
    Push(sp, Value(v));
}

VM_INLINE void U_PUSHFLT64(VM &, StackPtr sp, int a, int b) {
    Push(sp, Value(int2float64(Int64FromInts(a, b)).f));
}

VM_INLINE void U_PUSHFUN(VM &, StackPtr sp, int start, fun_base_t fcont) {
    (void)start;
    Push(sp, Value(FunPtr(fcont)));
}

VM_INLINE void U_PUSHSTR(VM &vm, StackPtr sp, int i) {
    // FIXME: have a way that constant strings can stay in the bytecode,
    // or at least preallocate them all
    auto &s = vm.constant_strings[i];
    if (!s) {
        auto fb_s = vm.bcf->stringtable()->Get(i);
        s = vm.NewString(fb_s->string_view());
    }
    #if STRING_CONSTANTS_KEEP
        s->Inc();
    #endif
    Push(sp, Value(s));
}

VM_INLINE void U_INCREF(VM &, StackPtr sp, int off) {
    TopM(sp, off).LTINCRTNIL();
}

VM_INLINE void U_KEEPREFLOOP(VM &, StackPtr, int, int) {
}

VM_INLINE void U_KEEPREF(VM &, StackPtr, int, int) {
}

VM_INLINE void U_CALL(VM &, StackPtr, int) {
}

VM_INLINE void U_CALLV(VM &vm, StackPtr sp) {
    Value fun = Pop(sp);
    VMTYPEEQ(fun, V_FUNCTION);
    vm.next_call_target = fun.ip();
}

VM_INLINE void U_DDCALL(VM &vm, StackPtr sp, int vtable_idx, int stack_idx) {
    auto self = TopM(sp, stack_idx);
    VMTYPEEQ(self, V_CLASS);
    auto start = self.oval()->ti(vm).vtable_start_or_bitmask;
    vm.next_call_target = vm.native_vtables[start + vtable_idx];
    assert(vm.next_call_target);
}

VM_INLINE void U_FUNSTART(VM &, StackPtr, const int *) {
}

VM_INLINE void U_RETURNLOCAL(VM &vm, StackPtr, int /*nrv*/) {
    #ifndef NDEBUG
        vm.ret_unwind_to = -9;
        vm.ret_slots = -9;
    #else
        (void)vm;
    #endif
}

VM_INLINE void U_RETURNNONLOCAL(VM &vm, StackPtr, int nrv, int df) {
    vm.ret_unwind_to = df;
    vm.ret_slots = nrv;
}

VM_INLINE void U_RETURNANY(VM &, StackPtr, int /*nretslots_norm*/) {
}

VM_INLINE void U_GOTOFUNEXIT(VM &, StackPtr) {
}

VM_INLINE void U_STATEMENT(VM &vm, StackPtr, int line, int fileidx) {
    vm.last_line = line;
    vm.last_fileidx = fileidx;
    #ifndef NDEBUG
        if (vm.trace != TraceMode::OFF) {
            auto &sd = vm.TraceStream();
            append(sd, vm.bcf->filenames()->Get(fileidx)->string_view(), "(", line, ")");
            if (vm.trace == TraceMode::TAIL) sd += "\n"; else LOG_PROGRAM(sd);
        }
    #endif
}

VM_INLINE void U_EXIT(VM &vm, StackPtr sp, int tidx) {
    if (tidx >= 0) vm.EndEval(sp, Pop(sp), vm.GetTypeInfo((type_elem_t)tidx));
    else vm.EndEval(sp, NilVal(), vm.GetTypeInfo(TYPE_ELEM_ANY));
}

VM_INLINE bool ForLoop(VM &, StackPtr sp, iint len) {
    auto &i = TopM(sp, 1);
    TYPE_ASSERT(i.type == V_INT);
    i.setival(i.ival() + 1);
    if (i.ival() >= len) {
        return false;
    } else {
        return true;
    }
}

#define FORELEM(L) \
    auto &iter = Top(sp); \
    auto i = TopM(sp, 1).ival(); \
    assert(i < L);

VM_INLINE bool U_IFOR(VM &vm, StackPtr sp) { return ForLoop(vm, sp, Top(sp).ival()); }
VM_INLINE bool U_VFOR(VM &vm, StackPtr sp) { return ForLoop(vm, sp, Top(sp).vval()->len); }
VM_INLINE bool U_SFOR(VM &vm, StackPtr sp) { return ForLoop(vm, sp, Top(sp).sval()->len); }

VM_INLINE void U_IFORELEM(VM &, StackPtr sp) {
    FORELEM(iter.ival()); (void)iter; Push(sp, i);
}
VM_INLINE void U_SFORELEM(VM &, StackPtr sp) {
    FORELEM(iter.sval()->len); Push(sp, Value(((uint8_t *)iter.sval()->data())[i]));
}
VM_INLINE void U_VFORELEM(VM &, StackPtr sp) {
    FORELEM(iter.vval()->len); Push(sp, iter.vval()->At(i));
}
VM_INLINE void U_VFORELEM2S(VM &, StackPtr sp) {
    FORELEM(iter.vval()->len); iter.vval()->AtVW(sp, i);
}
VM_INLINE void U_VFORELEMREF(VM &, StackPtr sp) {
    FORELEM(iter.vval()->len); auto el = iter.vval()->At(i); el.LTINCRTNIL(); Push(sp, el);
}
VM_INLINE void U_VFORELEMREF2S(VM &, StackPtr sp, int bitmask) {
    FORELEM(iter.vval()->len); iter.vval()->AtVWInc(sp, i, bitmask);
}

VM_INLINE void U_FORLOOPI(VM &, StackPtr sp) {
    auto &i = TopM(sp, 1);  // This relies on for being inlined, otherwise it would be 2.
    TYPE_ASSERT(i.type == V_INT);
    Push(sp, i);
}

#if LOBSTER_FRAME_PROFILER_BUILTINS
    #define BPROF(NFI) tracy::ScopedZone ___tracy_scoped_zone(&vm.nfr.pre_allocated_function_locations[NFI])
#else
    #define BPROF(NFI)
#endif

#if LOBSTER_FRAME_PROFILER_GLOBAL
    #define GPROF_START(NFI) g_builtin_locations.push_back(vm.nfr.pre_allocated_function_locations[NFI]);
    #define GPROF_END() g_builtin_locations.pop_back();
#else
    #define GPROF_START(NFI) 
    #define GPROF_END() 
#endif


VM_INLINE void U_BCALLRETV(VM &vm, StackPtr sp, int nfi, int /*has_ret*/) {
    auto nf = vm.nfr.nfuns[nfi];
    BPROF(nfi);
    GPROF_START(nfi);
    nf->fun.fV(sp, vm);
    GPROF_END();
}

#define BCALLOP(N,DECLS,ARGS) \
VM_INLINE void U_BCALLRET##N(VM &vm, StackPtr sp, int nfi, int has_ret) { \
    auto nf = vm.nfr.nfuns[nfi]; \
    BPROF(nfi); \
    GPROF_START(nfi); \
    DECLS; \
    Value v = nf->fun.f##N ARGS; \
    GPROF_END(); \
    if (has_ret) { Push(sp, v); vm.BCallRetCheck(sp, nf); } \
}

BCALLOP(0, {}, (sp, vm));
BCALLOP(1, auto a0 = Pop(sp), (sp, vm, a0));
BCALLOP(2, auto a1 = Pop(sp);auto a0 = Pop(sp), (sp, vm, a0, a1));
BCALLOP(3, auto a2 = Pop(sp);auto a1 = Pop(sp);auto a0 = Pop(sp), (sp, vm, a0, a1, a2));
BCALLOP(4, auto a3 = Pop(sp);auto a2 = Pop(sp);auto a1 = Pop(sp);auto a0 = Pop(sp), (sp, vm, a0, a1, a2, a3));
BCALLOP(5, auto a4 = Pop(sp);auto a3 = Pop(sp);auto a2 = Pop(sp);auto a1 = Pop(sp);auto a0 = Pop(sp), (sp, vm, a0, a1, a2, a3, a4));
BCALLOP(6, auto a5 = Pop(sp);auto a4 = Pop(sp);auto a3 = Pop(sp);auto a2 = Pop(sp);auto a1 = Pop(sp);auto a0 = Pop(sp), (sp, vm, a0, a1, a2, a3, a4, a5));
BCALLOP(7, auto a6 = Pop(sp);auto a5 = Pop(sp);auto a4 = Pop(sp);auto a3 = Pop(sp);auto a2 = Pop(sp);auto a1 = Pop(sp);auto a0 = Pop(sp), (sp, vm, a0, a1, a2, a3, a4, a5, a6));

VM_INLINE void U_ASSERTR(VM &vm, StackPtr sp, int line, int fileidx, int stringidx) {
    if (Top(sp).False()) {
        vm.last_line = line;
        vm.last_fileidx = fileidx;
        auto assert_exp = vm.bcf->stringtable()->Get(stringidx)->string_view();
        vm.Error(cat("assertion failed: ", assert_exp));
    }
}

VM_INLINE void U_ASSERT(VM &vm, StackPtr sp, int line, int fileidx, int stringidx) {
    U_ASSERTR(vm, sp, line, fileidx, stringidx);
    Pop(sp);
}

VM_INLINE void U_NEWVEC(VM &vm, StackPtr sp, int ty, int len) {
    auto type = (type_elem_t)ty;
    auto vec = vm.NewVec(len, len, type);
    if (len) vec->CopyElemsShallow(TopPtr(sp) - len * vec->width);
    PopN(sp, len * (int)vec->width);
    Push(sp, Value(vec));
}

VM_INLINE void U_NEWOBJECT(VM &vm, StackPtr sp, int ty) {
    auto type = (type_elem_t)ty;
    auto len = vm.GetTypeInfo(type).len;
    auto vec = vm.NewObject(len, type);
    if (len) vec->CopyElemsShallow(TopPtr(sp) - len, len);
    PopN(sp, len);
    Push(sp, Value(vec));
}

VM_INLINE void U_POP(VM &, StackPtr sp) { Pop(sp); }
VM_INLINE void U_POPREF(VM &vm, StackPtr sp) { auto x = Pop(sp); x.LTDECRTNIL(vm); }
VM_INLINE void U_POPV(VM &, StackPtr sp, int len) { PopN(sp, len); }

VM_INLINE void U_DUP(VM &, StackPtr sp) { auto x = Top(sp); Push(sp, x); }

VM_INLINE void U_SADDN(VM &vm, StackPtr sp, int len) {
    iint blen = 0;
    // Find total len.
    for (int i = 0; i < len; i++) blen += TopM(sp, i).sval()->len;
    // Just one alloc.
    auto ds = vm.NewString(blen);
    // Copy them all in, backwards.
    for (int i = 0; i < len; i++) {
        auto s = Pop(sp).sval();
        blen -= s->len;
        memcpy((char *)ds->data() + blen, s->data(), s->len);
    }
    Push(sp, Value(ds));
}

// While float div by zero is generally undefined in C++, if it promises to adhere to IEEE754
// we get the desirable result of Inf values instead, and we don't have to check for 0.
// This behavior is similar to what Java/C#/JS already do.
// https://en.cppreference.com/w/cpp/language/operator_arithmetic#Multiplicative_operators
// We do the same for https://en.cppreference.com/w/c/numeric/math/fmod
// Integer div by zero is still a language level runtime error, as is INT_MIN / -1.
static_assert(std::numeric_limits<double>::is_iec559, "IEEE754 floats required");

#define GETARGS() Value b = Pop(sp); Value a = Pop(sp)
#define TYPEOP(op, extras, av, bv) \
    if constexpr ((extras & 1) != 0) if (bv <= 0 && bv >= -1 && (!bv || av == LLONG_MIN)) vm.DivErr(bv); \
    Value res = av op bv; \
    if constexpr ((extras & 2) != 0) res = (decltype(res))fmod((double)av, (double)bv);

#define _IOP(op, extras) \
    TYPE_ASSERT(a.type == V_INT && b.type == V_INT); \
    TYPEOP(op, extras, a.ival(), b.ival())
#define _FOP(op, extras) \
    TYPE_ASSERT(a.type == V_FLOAT && b.type == V_FLOAT); \
    TYPEOP(op, extras, a.fval(), b.fval())

#define _VOPS(op, extras, V_T, field, geta) { \
    auto b = Pop(sp); \
    VMTYPEEQ(b, V_T) \
    auto veca = geta; \
    for (int j = 0; j < len; j++) { \
        auto &a = veca[j]; \
        VMTYPEEQ(a, V_T) \
        auto bv = b.field(); \
        TYPEOP(op, extras, a.field(), bv) \
        a = res; \
    } \
}
#define _SOPV(op, extras, V_T, field, geta) { \
    PopN(sp, len); \
    auto vecb = TopPtr(sp); \
    auto a = geta; \
    VMTYPEEQ(a, V_T) \
    for (int j = 0; j < len; j++) { \
        auto &b = vecb[j]; \
        VMTYPEEQ(b, V_T) \
        auto av = a.field(); \
        TYPEOP(op, extras, av, b.field()) \
        Push(sp, res); \
    } \
}
#define _VOPV(op, extras, V_T, field, geta) { \
    PopN(sp, len); \
    auto vecb = TopPtr(sp); \
    auto veca = geta; \
    for (int j = 0; j < len; j++) { \
        auto b = vecb[j]; \
        VMTYPEEQ(b, V_T) \
        auto &a = veca[j]; \
        VMTYPEEQ(a, V_T) \
        auto bv = b.field(); \
        TYPEOP(op, extras, a.field(), bv) \
        a = res; \
    } \
}
#define STCOMPEN(op, init, andor) { \
    PopN(sp, len); \
    auto vecb = TopPtr(sp); \
    PopN(sp, len); \
    auto veca = TopPtr(sp); \
    auto all = init; \
    for (int j = 0; j < len; j++) { \
        all = all andor veca[j].any() op vecb[j].any(); \
    } \
    Push(sp, all); \
}

#define _IVOPS(op, extras, geta) _VOPS(op, extras, V_INT,   ival, geta)
#define _IVOPV(op, extras, geta) _VOPV(op, extras, V_INT,   ival, geta)
#define _SOPIV(op, extras, geta) _SOPV(op, extras, V_INT,   ival, geta)
#define _FVOPS(op, extras, geta) _VOPS(op, extras, V_FLOAT, fval, geta)
#define _FVOPV(op, extras, geta) _VOPV(op, extras, V_FLOAT, fval, geta)
#define _SOPFV(op, extras, geta) _SOPV(op, extras, V_FLOAT, fval, geta)

#define _SCAT() Value res = vm.NewString(a.sval()->strv(), b.sval()->strv())

#define ACOMPEN(op)     { GETARGS(); Value res = a.any() op b.any(); Push(sp, res); }
#define IOP(op, extras) { GETARGS(); _IOP(op, extras);               Push(sp, res); }
#define FOP(op, extras) { GETARGS(); _FOP(op, extras);               Push(sp, res); }
#define LOP(op)         { GETARGS(); auto res = a.ip() op b.ip();    Push(sp, res); }

#define IVVOP(op, extras) { _IVOPV(op, extras, TopPtr(sp) - len); }
#define FVVOP(op, extras) { _FVOPV(op, extras, TopPtr(sp) - len); }
#define IVSOP(op, extras) { _IVOPS(op, extras, TopPtr(sp) - len); }
#define FVSOP(op, extras) { _FVOPS(op, extras, TopPtr(sp) - len); }
#define SIVOP(op, extras) { _SOPIV(op, extras, Pop(sp)); }
#define SFVOP(op, extras) { _SOPFV(op, extras, Pop(sp)); }

#define SOP(op) { GETARGS(); Value res = *a.sval() op *b.sval(); Push(sp, res); }
#define SCAT()  { GETARGS(); _SCAT();                            Push(sp, res); }

// +  += I F Vif S
// -  -= I F Vif
// *  *= I F Vif
// /  /= I F Vif
// %  %= I F Vif

// <     I F Vif S
// >     I F Vif S
// <=    I F Vif S
// >=    I F Vif S
// ==    I F V   S   // FIXME differentiate struct / value / vector
// !=    I F V   S

// U-    I F Vif
// U!    A

VM_INLINE void U_IVVADD(VM &vm, StackPtr sp, int len) { IVVOP(+,  0); }
VM_INLINE void U_IVVSUB(VM &vm, StackPtr sp, int len) { IVVOP(-,  0); }
VM_INLINE void U_IVVMUL(VM &vm, StackPtr sp, int len) { IVVOP(*,  0); }
VM_INLINE void U_IVVDIV(VM &vm, StackPtr sp, int len) { IVVOP(/,  1); }
VM_INLINE void U_IVVMOD(VM &vm, StackPtr sp, int len) { IVVOP(%,  1); }
VM_INLINE void U_IVVLT(VM &vm, StackPtr sp, int len)  { IVVOP(<,  0); }
VM_INLINE void U_IVVGT(VM &vm, StackPtr sp, int len)  { IVVOP(>,  0); }
VM_INLINE void U_IVVLE(VM &vm, StackPtr sp, int len)  { IVVOP(<=, 0); }
VM_INLINE void U_IVVGE(VM &vm, StackPtr sp, int len)  { IVVOP(>=, 0); }
VM_INLINE void U_FVVADD(VM &vm, StackPtr sp, int len) { FVVOP(+,  0); }
VM_INLINE void U_FVVSUB(VM &vm, StackPtr sp, int len) { FVVOP(-,  0); }
VM_INLINE void U_FVVMUL(VM &vm, StackPtr sp, int len) { FVVOP(*,  0); }
VM_INLINE void U_FVVDIV(VM &vm, StackPtr sp, int len) { FVVOP(/,  0); }
VM_INLINE void U_FVVMOD(VM &vm, StackPtr sp, int len) { FVVOP(/,  2); }
VM_INLINE void U_FVVLT(VM &vm, StackPtr sp, int len)  { FVVOP(<,  0); }
VM_INLINE void U_FVVGT(VM &vm, StackPtr sp, int len)  { FVVOP(>,  0); }
VM_INLINE void U_FVVLE(VM &vm, StackPtr sp, int len)  { FVVOP(<=, 0); }
VM_INLINE void U_FVVGE(VM &vm, StackPtr sp, int len)  { FVVOP(>=, 0); }

VM_INLINE void U_IVSADD(VM &vm, StackPtr sp, int len) { IVSOP(+,  0); }
VM_INLINE void U_IVSSUB(VM &vm, StackPtr sp, int len) { IVSOP(-,  0); }
VM_INLINE void U_IVSMUL(VM &vm, StackPtr sp, int len) { IVSOP(*,  0); }
VM_INLINE void U_IVSDIV(VM &vm, StackPtr sp, int len) { IVSOP(/,  1); }
VM_INLINE void U_IVSMOD(VM &vm, StackPtr sp, int len) { IVSOP(%,  1); }
VM_INLINE void U_IVSLT(VM &vm, StackPtr sp, int len)  { IVSOP(<,  0); }
VM_INLINE void U_IVSGT(VM &vm, StackPtr sp, int len)  { IVSOP(>,  0); }
VM_INLINE void U_IVSLE(VM &vm, StackPtr sp, int len)  { IVSOP(<=, 0); }
VM_INLINE void U_IVSGE(VM &vm, StackPtr sp, int len)  { IVSOP(>=, 0); }
VM_INLINE void U_FVSADD(VM &vm, StackPtr sp, int len) { FVSOP(+,  0); }
VM_INLINE void U_FVSSUB(VM &vm, StackPtr sp, int len) { FVSOP(-,  0); }
VM_INLINE void U_FVSMUL(VM &vm, StackPtr sp, int len) { FVSOP(*,  0); }
VM_INLINE void U_FVSDIV(VM &vm, StackPtr sp, int len) { FVSOP(/,  0); }
VM_INLINE void U_FVSMOD(VM &vm, StackPtr sp, int len) { FVSOP(/,  2); }
VM_INLINE void U_FVSLT(VM &vm, StackPtr sp, int len)  { FVSOP(<,  0); }
VM_INLINE void U_FVSGT(VM &vm, StackPtr sp, int len)  { FVSOP(>,  0); }
VM_INLINE void U_FVSLE(VM &vm, StackPtr sp, int len)  { FVSOP(<=, 0); }
VM_INLINE void U_FVSGE(VM &vm, StackPtr sp, int len)  { FVSOP(>=, 0); }

VM_INLINE void U_SIVADD(VM &vm, StackPtr sp, int len) { SIVOP(+,  0); }
VM_INLINE void U_SIVSUB(VM &vm, StackPtr sp, int len) { SIVOP(-,  0); }
VM_INLINE void U_SIVMUL(VM &vm, StackPtr sp, int len) { SIVOP(*,  0); }
VM_INLINE void U_SIVDIV(VM &vm, StackPtr sp, int len) { SIVOP(/,  1); }
VM_INLINE void U_SIVMOD(VM &vm, StackPtr sp, int len) { SIVOP(%,  1); }
VM_INLINE void U_SIVLT(VM &vm, StackPtr sp, int len)  { SIVOP(<,  0); }
VM_INLINE void U_SIVGT(VM &vm, StackPtr sp, int len)  { SIVOP(>,  0); }
VM_INLINE void U_SIVLE(VM &vm, StackPtr sp, int len)  { SIVOP(<=, 0); }
VM_INLINE void U_SIVGE(VM &vm, StackPtr sp, int len)  { SIVOP(>=, 0); }
VM_INLINE void U_SFVADD(VM &vm, StackPtr sp, int len) { SFVOP(+,  0); }
VM_INLINE void U_SFVSUB(VM &vm, StackPtr sp, int len) { SFVOP(-,  0); }
VM_INLINE void U_SFVMUL(VM &vm, StackPtr sp, int len) { SFVOP(*,  0); }
VM_INLINE void U_SFVDIV(VM &vm, StackPtr sp, int len) { SFVOP(/,  0); }
VM_INLINE void U_SFVMOD(VM &vm, StackPtr sp, int len) { SFVOP(/,  2); }
VM_INLINE void U_SFVLT(VM &vm, StackPtr sp, int len)  { SFVOP(<,  0); }
VM_INLINE void U_SFVGT(VM &vm, StackPtr sp, int len)  { SFVOP(>,  0); }
VM_INLINE void U_SFVLE(VM &vm, StackPtr sp, int len)  { SFVOP(<=, 0); }
VM_INLINE void U_SFVGE(VM &vm, StackPtr sp, int len)  { SFVOP(>=, 0); }

VM_INLINE void U_AEQ(VM &, StackPtr sp)  { ACOMPEN(==); }
VM_INLINE void U_ANE(VM &, StackPtr sp)  { ACOMPEN(!=); }

VM_INLINE void U_STEQ(VM &, StackPtr sp, int len) { STCOMPEN(==, true,  &&); }
VM_INLINE void U_STNE(VM &, StackPtr sp, int len) { STCOMPEN(!=, false, ||); }

VM_INLINE void U_LEQ(VM &, StackPtr sp) { LOP(==); }
VM_INLINE void U_LNE(VM &, StackPtr sp) { LOP(!=); }

VM_INLINE void U_IADD(VM &vm, StackPtr sp) { IOP(+,  0); }
VM_INLINE void U_ISUB(VM &vm, StackPtr sp) { IOP(-,  0); }
VM_INLINE void U_IMUL(VM &vm, StackPtr sp) { IOP(*,  0); }
VM_INLINE void U_IDIV(VM &vm, StackPtr sp) { IOP(/,  1); }
VM_INLINE void U_IMOD(VM &vm, StackPtr sp) { IOP(%,  1); }
VM_INLINE void U_ILT(VM &vm, StackPtr sp)  { IOP(<,  0); }
VM_INLINE void U_IGT(VM &vm, StackPtr sp)  { IOP(>,  0); }
VM_INLINE void U_ILE(VM &vm, StackPtr sp)  { IOP(<=, 0); }
VM_INLINE void U_IGE(VM &vm, StackPtr sp)  { IOP(>=, 0); }
VM_INLINE void U_IEQ(VM &vm, StackPtr sp)  { IOP(==, 0); }
VM_INLINE void U_INE(VM &vm, StackPtr sp)  { IOP(!=, 0); }

VM_INLINE void U_FADD(VM &vm, StackPtr sp) { FOP(+,  0); }
VM_INLINE void U_FSUB(VM &vm, StackPtr sp) { FOP(-,  0); }
VM_INLINE void U_FMUL(VM &vm, StackPtr sp) { FOP(*,  0); }
VM_INLINE void U_FDIV(VM &vm, StackPtr sp) { FOP(/,  0); }
VM_INLINE void U_FMOD(VM &vm, StackPtr sp) { FOP(/,  2); }
VM_INLINE void U_FLT(VM &vm, StackPtr sp)  { FOP(<,  0); }
VM_INLINE void U_FGT(VM &vm, StackPtr sp)  { FOP(>,  0); }
VM_INLINE void U_FLE(VM &vm, StackPtr sp)  { FOP(<=, 0); }
VM_INLINE void U_FGE(VM &vm, StackPtr sp)  { FOP(>=, 0); }
VM_INLINE void U_FEQ(VM &vm, StackPtr sp)  { FOP(==, 0); }
VM_INLINE void U_FNE(VM &vm, StackPtr sp)  { FOP(!=, 0); }

VM_INLINE void U_SADD(VM &vm, StackPtr sp) { SCAT(); }
VM_INLINE void U_SSUB(VM &vm, StackPtr)    { VMASSERT(vm, 0); }
VM_INLINE void U_SMUL(VM &vm, StackPtr)    { VMASSERT(vm, 0); }
VM_INLINE void U_SDIV(VM &vm, StackPtr)    { VMASSERT(vm, 0); }
VM_INLINE void U_SMOD(VM &vm, StackPtr)    { VMASSERT(vm, 0); }
VM_INLINE void U_SLT(VM &, StackPtr sp)    { SOP(<);  }
VM_INLINE void U_SGT(VM &, StackPtr sp)    { SOP(>);  }
VM_INLINE void U_SLE(VM &, StackPtr sp)    { SOP(<=); }
VM_INLINE void U_SGE(VM &, StackPtr sp)    { SOP(>=); }
VM_INLINE void U_SEQ(VM &, StackPtr sp)    { SOP(==); }
VM_INLINE void U_SNE(VM &, StackPtr sp)    { SOP(!=); }

VM_INLINE void U_IUMINUS(VM &, StackPtr sp) { Value a = Pop(sp); Push(sp, Value(-a.ival())); }
VM_INLINE void U_FUMINUS(VM &, StackPtr sp) { Value a = Pop(sp); Push(sp, Value(-a.fval())); }

VM_INLINE void U_IVUMINUS(VM &vm, StackPtr sp, int len) {
    auto vec = TopPtr(sp) - len;
    for (int i = 0; i < len; i++) {
        auto &a = vec[i];
        VMTYPEEQ(a, V_INT);
        a = -a.ival();
    }
}

VM_INLINE void U_FVUMINUS(VM &vm, StackPtr sp, int len) {
    auto vec = TopPtr(sp) - len;
    for (int i = 0; i < len; i++) {
        auto &a = vec[i];
        VMTYPEEQ(a, V_FLOAT);
        a = -a.fval();
    }
}

VM_INLINE void U_LOGNOT(VM &, StackPtr sp) {
    Value a = Pop(sp);
    Push(sp, a.False());
}

VM_INLINE void U_LOGNOTREF(VM &, StackPtr sp) {
    Value a = Pop(sp);
    bool b = a.True();
    Push(sp, !b);
}

#define BITOP(op) { GETARGS(); Push(sp, a.ival() op b.ival()); }
VM_INLINE void U_BINAND(VM &, StackPtr sp) { BITOP(&);  }
VM_INLINE void U_BINOR(VM &, StackPtr sp)  { BITOP(|);  }
VM_INLINE void U_XOR(VM &, StackPtr sp)    { BITOP(^);  }
VM_INLINE void U_ASL(VM &, StackPtr sp)    { BITOP(<<); }
VM_INLINE void U_ASR(VM &, StackPtr sp)    { BITOP(>>); }
VM_INLINE void U_NEG(VM &, StackPtr sp)    { auto a = Pop(sp); Push(sp, ~a.ival()); }

VM_INLINE void U_I2F(VM &vm, StackPtr sp) {
    Value a = Pop(sp);
    VMTYPEEQ(a, V_INT);
    Push(sp, (double)a.ival());
}

VM_INLINE void U_A2S(VM &vm, StackPtr sp, int ty) {
    Value a = Pop(sp);
    Push(sp, vm.ToString(a, vm.GetTypeInfo((type_elem_t)ty)));
}

VM_INLINE void U_ST2S(VM &vm, StackPtr sp, int ty) {
    auto &ti = vm.GetTypeInfo((type_elem_t)ty);
    PopN(sp, ti.len);
    auto top = TopPtr(sp);
    Push(sp, vm.StructToString(top, ti));
}

VM_INLINE void U_E2B(VM &, StackPtr sp) {
    Value a = Pop(sp);
    Push(sp, a.True());
}

VM_INLINE void U_E2BREF(VM &vm, StackPtr sp) {
    Value a = Pop(sp);
    a.LTDECRTNIL(vm);
    Push(sp, a.True());
}

VM_INLINE void U_PUSHVARL(VM &, StackPtr, int) {
    assert(false);
}

VM_INLINE void U_PUSHVARF(VM &vm, StackPtr sp, int vidx) {
    Push(sp, vm.fvars[vidx]);
}

VM_INLINE void U_PUSHVARVL(VM &, StackPtr, int, int) {
    assert(false);
}

VM_INLINE void U_PUSHVARVF(VM &vm, StackPtr sp, int vidx, int l) {
    tsnz_memcpy(TopPtr(sp), &vm.fvars[vidx], l);
    PushN(sp, l);
}

VM_INLINE void U_PUSHFLD(VM &vm, StackPtr sp, int i) {
    Value r = Pop(sp);
    VMASSERT(vm, r.ref());
    assert(i < r.oval()->Len(vm));
    Push(sp, r.oval()->AtS(i));
}
VM_INLINE void U_PUSHFLDMREF(VM &vm, StackPtr sp, int i) {
    Value r = Pop(sp);
    if (!r.ref()) {
        Push(sp, r);
    } else {
        assert(i < r.oval()->Len(vm));
        (void)vm;
        Push(sp, r.oval()->AtS(i));
    }
}
VM_INLINE void U_PUSHFLD2V(VM &vm, StackPtr sp, int i, int l) {
    Value r = Pop(sp);
    VMASSERT(vm, r.ref());
    assert(i + l <= r.oval()->Len(vm));
    tsnz_memcpy(TopPtr(sp), &r.oval()->AtS(i), l);
    PushN(sp, l);
}
VM_INLINE void U_PUSHFLDV(VM &, StackPtr sp, int i, int l) {
    PopN(sp, l);
    auto val = *(TopPtr(sp) + i);
    Push(sp, val);
}
VM_INLINE void U_PUSHFLDV2V(VM &, StackPtr sp, int i, int rl, int l) {
    PopN(sp, l);
    t_memmove(TopPtr(sp), TopPtr(sp) + i, rl);
    PushN(sp, rl);
}

VM_INLINE void U_VPUSHIDXI(VM &vm, StackPtr sp) {
    auto x = Pop(sp).ival();
    PushDerefIdxVector1(vm, sp, x);
}

VM_INLINE void U_VPUSHIDXI2V(VM &vm, StackPtr sp) {
    auto x = Pop(sp).ival();
    PushDerefIdxVector2V(vm, sp, x);
}

VM_INLINE void U_VPUSHIDXV(VM &vm, StackPtr sp, int l) {
    auto x = vm.GrabIndex(sp, l);
    PushDerefIdxVector2V(vm, sp, x);
}

VM_INLINE void U_VPUSHIDXIS(VM &vm, StackPtr sp, int o) {
    auto x = Pop(sp).ival();
    PushDerefIdxVectorSub1(vm, sp, x, o);
}

VM_INLINE void U_VPUSHIDXIS2V(VM &vm, StackPtr sp, int w, int o) {
    auto x = Pop(sp).ival();
    PushDerefIdxVectorSub2V(vm, sp, x, w, o);
}

VM_INLINE void U_VPUSHIDXVS(VM &vm, StackPtr sp, int l, int w, int o) {
    auto x = vm.GrabIndex(sp, l);
    PushDerefIdxVectorSub2V(vm, sp, x, w, o);
}

VM_INLINE void U_NPUSHIDXI(VM &, StackPtr sp, int l) {
    auto x = Pop(sp).ival();
    PushDerefIdxStruct(sp, x, l);
}

VM_INLINE void U_SPUSHIDXI(VM &vm, StackPtr sp) {
    auto x = Pop(sp).ival();
    PushDerefIdxString(vm, sp, x);
}

VM_INLINE void U_BLOCK_START(VM &, StackPtr) {
    assert(false);
}

VM_INLINE void U_JUMP_TABLE_END(VM &, StackPtr) {
    assert(false);
}

VM_INLINE void U_JUMP_TABLE_CASE_START(VM &, StackPtr) {
    assert(false);
}

VM_INLINE bool U_JUMP(VM &, StackPtr) {
    assert(false);
    return false;
}

VM_INLINE bool U_JUMPFAIL(VM &, StackPtr sp) {
    return Top(sp).True();
}

VM_INLINE bool U_JUMPFAILR(VM &, StackPtr sp) {
    return Top(sp).True();
}

VM_INLINE bool U_JUMPNOFAIL(VM &, StackPtr sp) {
    auto x = Pop(sp);
    return x.False();
}

VM_INLINE bool U_JUMPNOFAILR(VM &, StackPtr sp) {
    auto x = Top(sp);
    return x.False();
}

VM_INLINE bool U_JUMPIFUNWOUND(VM &vm, StackPtr, int df) {
    assert(vm.ret_unwind_to >= 0);
    return vm.ret_unwind_to != df;
}

VM_INLINE bool U_JUMPIFSTATICLF(VM &vm, StackPtr, int vidx) {
    auto &v = vm.fvars[vidx];
    auto jump = v.ival() < vm.frame_count;
    v = vm.frame_count + 1;
    return jump;
}

VM_INLINE bool U_JUMPIFMEMBERLF(VM &vm, StackPtr sp, int slot) {
    auto self = Pop(sp).oval();
    auto &v = self->AtS(slot);
    auto jump = v.ival() < vm.frame_count;
    v = vm.frame_count + 1;
    return jump;
}

VM_INLINE void U_JUMP_TABLE(VM &, StackPtr, const int *) {
    assert(false);
}

VM_INLINE void U_JUMP_TABLE_DISPATCH(VM &, StackPtr, const int *) {
    assert(false);
}

VM_INLINE void U_ISTYPE(VM &vm, StackPtr sp, int ty) {
    auto to = (type_elem_t)ty;
    auto v = Pop(sp);
    // Optimizer guarantees we don't have to deal with scalars.
    if (v.refnil()) Push(sp, v.ref()->tti == to);
    else Push(sp, vm.GetTypeInfo(to).t == V_NIL);  // FIXME: can replace by fixed type_elem_t ?
}

VM_INLINE void U_ABORT(VM &vm, StackPtr) {
    vm.SeriousError("VM internal error: abort");
}

VM_INLINE void U_LVAL_VARL(VM &, StackPtr, int) {
    assert(false);
}

VM_INLINE void U_LVAL_VARF(VM &vm, StackPtr, int vidx) {
    vm.temp_lval = &vm.fvars[vidx];
}

VM_INLINE void U_LVAL_FLD(VM &vm, StackPtr sp, int i) {
    vm.temp_lval = &GetFieldLVal(vm, sp, i);
}

VM_INLINE void U_LVAL_IDXVI(VM &vm, StackPtr sp, int offset) {
    auto x = Pop(sp).ival();
    vm.temp_lval = &GetVecLVal(vm, sp, x) + offset;
}

VM_INLINE void U_LVAL_IDXVV(VM &vm, StackPtr sp, int offset, int l) {
    auto x = vm.GrabIndex(sp, l);
    vm.temp_lval = &GetVecLVal(vm, sp, x) + offset;
}

// Class accessed by index.
VM_INLINE void U_LVAL_IDXNI(VM &vm, StackPtr sp, int offset) {
    auto x = Pop(sp).ival();
    vm.temp_lval = &GetFieldILVal(vm, sp, x) + offset;
}

VM_INLINE void U_LV_DUP(VM &vm, StackPtr sp) {
    Push(sp, *vm.temp_lval);
}

VM_INLINE void U_LV_DUPV(VM &vm, StackPtr sp, int l) {
    tsnz_memcpy(TopPtr(sp), vm.temp_lval, l);
    PushN(sp, l);
}

/*
VM_INLINE void U_LV_DUPREF(VM &vm, StackPtr sp) {
    vm.temp_lval->LTINCRTNIL();
    Push(sp, *vm.temp_lval);
}

VM_INLINE void U_LV_DUPREFV(VM &vm, StackPtr sp, int l) {
    tsnz_memcpy(TopPtr(sp), vm.temp_lval, l);
    for (int i = 0; i < l; i++) {
        sp++;
        Top(sp).LTINCRTNIL();
    }
}
*/

#define LVALCASES(N, B) VM_INLINE void U_LV_##N(VM &vm, StackPtr sp) { \
    auto &a = *vm.temp_lval; Value b = Pop(sp); B; }

#define LVALCASER(N, B) VM_INLINE void U_LV_##N(VM &vm, StackPtr sp, int len) { \
    auto &fa = *vm.temp_lval; B; }

LVALCASER(IVVADD, _IVOPV(+, 0, &fa))
LVALCASER(IVVSUB, _IVOPV(-, 0, &fa))
LVALCASER(IVVMUL, _IVOPV(*, 0, &fa))
LVALCASER(IVVDIV, _IVOPV(/, 1, &fa))
LVALCASER(IVVMOD, _IVOPV(/, 3, &fa))

LVALCASER(FVVADD, _FVOPV(+, 0, &fa))
LVALCASER(FVVSUB, _FVOPV(-, 0, &fa))
LVALCASER(FVVMUL, _FVOPV(*, 0, &fa))
LVALCASER(FVVDIV, _FVOPV(/, 0, &fa))
LVALCASER(FVVMOD, _FVOPV(/, 2, &fa))

LVALCASER(IVSADD, _IVOPS(+, 0, &fa))
LVALCASER(IVSSUB, _IVOPS(-, 0, &fa))
LVALCASER(IVSMUL, _IVOPS(*, 0, &fa))
LVALCASER(IVSDIV, _IVOPS(/, 1, &fa))
LVALCASER(IVSMOD, _IVOPS(/, 3, &fa))

LVALCASER(FVSADD, _FVOPS(+, 0, &fa))
LVALCASER(FVSSUB, _FVOPS(-, 0, &fa))
LVALCASER(FVSMUL, _FVOPS(*, 0, &fa))
LVALCASER(FVSDIV, _FVOPS(/, 0, &fa))
LVALCASER(FVSMOD, _FVOPS(/, 2, &fa))

LVALCASES(IADD  , _IOP(+, 0); a = res;)
LVALCASES(ISUB  , _IOP(-, 0); a = res;)
LVALCASES(IMUL  , _IOP(*, 0); a = res;)
LVALCASES(IDIV  , _IOP(/, 1); a = res;)
LVALCASES(IMOD  , _IOP(%, 1); a = res;)

LVALCASES(BINAND, _IOP(&,  0); a = res;)
LVALCASES(BINOR , _IOP(|,  0); a = res;)
LVALCASES(XOR   , _IOP(^,  0); a = res;)
LVALCASES(ASL   , _IOP(<<, 0); a = res;)
LVALCASES(ASR   , _IOP(>>, 0); a = res;)

LVALCASES(FADD  , _FOP(+, 0); a = res;)
LVALCASES(FSUB  , _FOP(-, 0); a = res;)
LVALCASES(FMUL  , _FOP(*, 0); a = res;)
LVALCASES(FDIV  , _FOP(/, 0); a = res;)
LVALCASES(FMOD  , _FOP(/, 2); a = res;)

VM_INLINE void U_LV_SADD(VM &vm, StackPtr sp) {
    auto &a = *vm.temp_lval;
    Value b = Pop(sp);
    _SCAT();
    a.LTDECRTNIL(vm);
    a = res;
}

VM_INLINE void U_LV_WRITE(VM &vm, StackPtr sp) {
    auto &a = *vm.temp_lval;
    auto  b = Pop(sp);
    TYPE_ASSERT(a.type == b.type || a.type == V_NIL || b.type == V_NIL);
    a = b;
}

VM_INLINE void U_LV_WRITEREF(VM &vm, StackPtr sp) {
    auto &a = *vm.temp_lval;
    auto  b = Pop(sp);
    a.LTDECRTNIL(vm);
    TYPE_ASSERT(a.type == b.type || a.type == V_NIL || b.type == V_NIL);
    a = b;
}

VM_INLINE void U_LV_WRITEV(VM &vm, StackPtr sp, int l) {
    auto &a = *vm.temp_lval;
    auto b = TopPtr(sp) - l;
    tsnz_memcpy(&a, b, l);
    PopN(sp, l);
}

VM_INLINE void U_LV_WRITEREFV(VM &vm, StackPtr sp, int l, int bitmask) {
    // TODO: if this bitmask checking is expensive, either make a version of
    // this op for structs with all ref elems, or better yet, special case for
    // structs with a single elem.
    auto &a = *vm.temp_lval;
    for (int i = 0; i < l; i++) if ((1 << i) & bitmask) (&a)[i].LTDECRTNIL(vm);
    auto b = TopPtr(sp) - l;
    tsnz_memcpy(&a, b, l);
    PopN(sp, l);
}

VM_INLINE void U_LV_IPP(VM &vm, StackPtr) {
    auto &a = *vm.temp_lval;
    a.setival(a.ival() + 1);
}

VM_INLINE void U_LV_IMM(VM & vm, StackPtr) {
    auto &a = *vm.temp_lval;
    a.setival(a.ival() - 1);
}

VM_INLINE void U_LV_FPP(VM & vm, StackPtr) {
    auto &a = *vm.temp_lval;
    a.setfval(a.fval() + 1);
}

VM_INLINE void U_LV_FMM(VM & vm, StackPtr) {
    auto &a = *vm.temp_lval;
    a.setfval(a.fval() - 1);
}

VM_INLINE void U_PROFILE(VM &, StackPtr, int) {
    assert(false);
}

}  // namespace lobster
