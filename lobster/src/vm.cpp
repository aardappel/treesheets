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

#include "lobster/disasm.h"

namespace lobster {

#ifdef _DEBUG
    #define VM_PROFILER              // tiny VM slowdown and memory usage when enabled
#endif

#ifdef VM_COMPILED_CODE_MODE
    #define VM_OP_PASSTHRU ip, fcont
    #pragma warning (disable: 4458)  // ip hides class member, which we rely on
    #pragma warning (disable: 4100)  // ip may not be touched
#else
    #define VM_OP_PASSTHRU
#endif

enum {
    // *8 bytes each
    INITSTACKSIZE   =   4 * 1024,
    // *8 bytes each, modest on smallest handheld we support (iPhone 3GS has 256MB).
    DEFMAXSTACKSIZE = 128 * 1024,
    // *8 bytes each, max by which the stack could possibly grow in a single call.
    STACKMARGIN     =   1 * 1024
};

#define PUSH(v) (stack[++sp] = (v))
#define TOP() (stack[sp])
#define TOPM(n) (stack[sp - n])
#define POP() (stack[sp--]) // (sp < 0 ? 0/(sp + 1) : stack[sp--])
#define POPN(n) (sp -= (n))
#define TOPPTR() (stack + sp + 1)

VM::VM(NativeRegistry &natreg, string_view _pn, string &_bytecode_buffer, const void *entry_point,
       const void *static_bytecode, const vector<string> &args)
      : natreg(natreg), stack(nullptr), stacksize(0), maxstacksize(DEFMAXSTACKSIZE), sp(-1),
        #ifdef VM_COMPILED_CODE_MODE
            next_call_target(0), next_mm_table(nullptr), next_mm_call(nullptr),
        #else
            ip(nullptr),
        #endif
        curcoroutine(nullptr), vars(nullptr), codelen(0), codestart(nullptr),
        byteprofilecounts(nullptr), bytecode_buffer(std::move(_bytecode_buffer)),
        bcf(nullptr),
        programprintprefs(10, 10000, false, -1, false), typetable(nullptr),
        currentline(-1), maxsp(-1),
        debugpp(2, 50, true, -1, true), programname(_pn), vml(*this),
        trace(false), trace_tail(false), trace_ring_idx(0),
        vm_count_ins(0), vm_count_fcalls(0), vm_count_bcalls(0),
        compiled_code_ip(entry_point), program_args(args) {
    auto bcfb = (uchar *)(static_bytecode ? static_bytecode : bytecode_buffer.data());
    bcf = bytecode::GetBytecodeFile(bcfb);
    if (bcf->bytecode_version() != LOBSTER_BYTECODE_FORMAT_VERSION)
        THROW_OR_ABORT(string("bytecode is from a different version of Lobster"));
    codelen = bcf->bytecode()->Length();
    if (FLATBUFFERS_LITTLEENDIAN) {
        // We can use the buffer directly.
        codestart = (const int *)bcf->bytecode()->Data();
        typetable = (const type_elem_t *)bcf->typetable()->Data();
    } else {
        for (uint i = 0; i < codelen; i++)
            codebigendian.push_back(bcf->bytecode()->Get(i));
        codestart = codebigendian.data();

        for (uint i = 0; i < bcf->typetable()->Length(); i++)
            typetablebigendian.push_back((type_elem_t)bcf->typetable()->Get(i));
        typetable = typetablebigendian.data();
    }
    #ifndef VM_COMPILED_CODE_MODE
        ip = codestart;
    #endif
    vars = new Value[bcf->specidents()->size()];
    stack = new Value[stacksize = INITSTACKSIZE];
    #ifdef VM_PROFILER
        byteprofilecounts = new uint64_t[codelen];
        memset(byteprofilecounts, 0, sizeof(uint64_t) * codelen);
    #endif
    vml.LogInit(bcfb);
    #ifndef VM_INS_SWITCH
        #ifdef VM_COMPILED_CODE_MODE
            #define F(N, A) f_ins_pointers[IL_##N] = nullptr;
        #else
            #define F(N, A) f_ins_pointers[IL_##N] = &VM::F_##N;
        #endif
        ILNAMES
        #undef F
    #endif
}

VM::~VM() {
    if (stack) delete[] stack;
    if (vars)  delete[] vars;
    if (byteprofilecounts) delete[] byteprofilecounts;
}

void VM::OneMoreFrame() {
    // We just landed back into the VM after being suspended inside a gl_frame() call.
    // Emulate the return of gl_frame():
    PUSH(Value(1));  // We're not terminating yet.
    EvalProgram();   // Continue execution as if nothing happened.
}

const TypeInfo &VM::GetVarTypeInfo(int varidx) {
    return GetTypeInfo((type_elem_t)bcf->specidents()->Get(varidx)->typeidx());
}

type_elem_t VM::GetIntVectorType(int which) {
    auto i = bcf->default_int_vector_types()->Get(which);
    return type_elem_t(i < 0 ? -1 : i);
}
type_elem_t VM::GetFloatVectorType(int which) {
    auto i = bcf->default_float_vector_types()->Get(which);
    return type_elem_t(i < 0 ? -1 : i);
}

static bool _LeakSorter(void *va, void *vb) {
    auto a = (RefObj *)va;
    auto b = (RefObj *)vb;
    return a->refc != b->refc
    ? a->refc > b->refc
    : (a->tti != b->tti
        ? a->tti > b->tti
        : false);
}

void VM::DumpLeaks() {
    vector<void *> leaks;
    pool.findleaks([&](void *p) { leaks.push_back(p); });
    if (!leaks.empty()) {
        Output(OUTPUT_ERROR, "LEAKS FOUND (this indicates cycles in your object graph, or a bug in"
                             " Lobster, details in leaks.txt)");
        string s;
        //qsort(&leaks[0], leaks.size(), sizeof(void *), &LeakSorter);
        sort(leaks.begin(), leaks.end(), _LeakSorter);
        PrintPrefs leakpp = debugpp;
        leakpp.cycles = 0;
        for (auto p : leaks) {
            auto ro = (RefObj *)p;
            switch(ro->ti(*this).t) {
                case V_VALUEBUF:
                case V_STACKFRAMEBUF:
                    break;
                case V_STRING:
                case V_COROUTINE:
                case V_BOXEDINT:
                case V_BOXEDFLOAT:
                case V_VECTOR:
                case V_STRUCT: {
                    ostringstream ss;
                    ro->CycleStr(ss);
                    ss << " = ";
                    RefToString(*this, ss, ro, leakpp);
                    ss << "\n";
                    break;
                }
                default: assert(false);
            }
        }
        WriteFile("leaks.txt", false, s);
    }
    pool.printstats(false);
}

#undef new
LVector *VM::NewVec(intp initial, intp max, type_elem_t tti) {
    assert(GetTypeInfo(tti).t == V_VECTOR);
    return new (pool.alloc_small(sizeof(LVector))) LVector(*this, initial, max, tti);
}
LStruct *VM::NewStruct(intp max, type_elem_t tti) {
    assert(GetTypeInfo(tti).t == V_STRUCT);
    return new (pool.alloc(sizeof(LStruct) + sizeof(Value) * max)) LStruct(tti);
}
LString *VM::NewString(size_t l) {
    return new (pool.alloc(sizeof(LString) + l + 1)) LString((int)l);
}
LCoRoutine *VM::NewCoRoutine(InsPtr rip, const int *vip, LCoRoutine *p, type_elem_t cti) {
    assert(GetTypeInfo(cti).t == V_COROUTINE);
    return new (pool.alloc(sizeof(LCoRoutine)))
               LCoRoutine(sp + 2 /* top of sp + pushed coro */, (int)stackframes.size(), rip, vip, p,
                         cti);
}
BoxedInt *VM::NewInt(intp i) {
    return new (pool.alloc(sizeof(BoxedInt))) BoxedInt(i);
}
BoxedFloat *VM::NewFloat(floatp f) {
    return new (pool.alloc(sizeof(BoxedFloat))) BoxedFloat(f);
}
LResource *VM::NewResource(void *v, const ResourceType *t) {
    return new (pool.alloc(sizeof(LResource))) LResource(v, t);
}
#ifdef _WIN32
#ifdef _DEBUG
#define new DEBUG_NEW
#endif
#endif

LString *VM::NewString(string_view s) {
    auto r = NewString(s.size());
    memcpy(r->str(), s.data(), s.size());
    r->str()[s.size()] = 0;
    return r;
}

LString *VM::NewString(string_view s1, string_view s2) {
    auto s = NewString(s1.size() + s2.size());
    memcpy(s->str(), s1.data(), s1.size());
    memcpy(s->str() + s1.size(), s2.data(), s2.size());
    s->str()[s1.size() + s2.size()] = 0;
    return s;
}

// This function is now way less important than it was when the language was still dynamically
// typed. But ok to leave it as-is for "index out of range" and other errors that are still dynamic.
Value VM::Error(string err, const RefObj *a, const RefObj *b) {
    if (trace_tail && trace_output.size()) {
        string s;
        for (size_t i = trace_ring_idx; i < trace_output.size(); i++) s += trace_output[i].str();
        for (size_t i = 0; i < trace_ring_idx; i++) s += trace_output[i].str();
        s += err;
        THROW_OR_ABORT(s);
    }
    ostringstream ss;
    #ifndef VM_COMPILED_CODE_MODE
        // error is usually in the byte before the current ip.
        auto li = LookupLine(ip - 1, codestart, bcf);
        ss << flat_string_view(bcf->filenames()->Get(li->fileidx())) << '(' << li->line() << "): ";
    #endif
    ss << "VM error: " << err;
    if (a) { ss << "\n   arg: "; RefToString(*this, ss, a, debugpp); }
    if (b) { ss << "\n   arg: "; RefToString(*this, ss, b, debugpp); }
    while (sp >= 0 && (!stackframes.size() || sp != stackframes.back().spstart)) {
        // Sadly can't print this properly.
        ss << "\n   stack: ";
        to_string_hex(ss, (size_t)TOP().any());
        if (pool.pointer_is_in_allocator(TOP().any())) {
            ss << ", maybe: ";
            RefToString(*this, ss, TOP().ref(), debugpp);
        }
        POP();  // We don't DEC here, as we can't know what type it is.
                // This is ok, as we ignore leaks in case of an error anyway.
    }
    for (;;) {
        if (!stackframes.size()) break;
        int deffun = stackframes.back().definedfunction;
        if (deffun >= 0) {
            ss << "\nin function: " << flat_string_view(bcf->functions()->Get(deffun)->name());
        } else {
            ss << "\nin block";
        }
        #ifndef VM_COMPILED_CODE_MODE
        auto li = LookupLine(ip - 1, codestart, bcf);
        ss << " -> " << flat_string_view(bcf->filenames()->Get(li->fileidx())) << '('
           << li->line() << ')';
        #endif
        VarCleanup(ss.tellp() < 10000 ? &ss : nullptr, -2 /* clean up temps always */);
    }
    ss << "\nglobals:";
    for (size_t i = 0; i < bcf->specidents()->size(); i++) {
        DumpVar(ss, vars[i], i, true);
    }
    THROW_OR_ABORT(ss.str());
}

void VM::VMAssert(const char *what)  {
    Error(string("VM internal assertion failure: ") + what);
}
void VM::VMAssert(const char *what, const RefObj *a, const RefObj *b)  {
    Error(string("VM internal assertion failure: ") + what, a, b);
}

#if defined(_DEBUG) && RTT_ENABLED
    #define STRINGIFY(x) #x
    #define TOSTRING(x) STRINGIFY(x)
    #define VMASSERT(test) { if (!(test)) VMAssert(__FILE__ ": " TOSTRING(__LINE__) ": " #test); }
#else
    #define VMASSERT(test) {}
#endif
#if RTT_ENABLED
    #define VMTYPEEQ(val, vt) VMASSERT((val).type == (vt))
#else
    #define VMTYPEEQ(val, vt) { (void)(val); (void)(vt); }
#endif

void VM::DumpVar(ostringstream &ss, const Value &x, size_t idx, bool dumpglobals) {
    auto sid = bcf->specidents()->Get((uint)idx);
    auto id = bcf->idents()->Get(sid->ididx());
    if (id->readonly() || id->global() != dumpglobals) return;
    auto name = flat_string_view(id->name());
    auto static_type = GetVarTypeInfo((int)idx).t;
    #if RTT_ENABLED
        if (static_type != x.type) return;  // Likely uninitialized.
    #endif
    ss << "\n   " << name << " = ";
    x.ToString(*this, ss, static_type, debugpp);
}

void VM::EvalMulti(const int *mip, int definedfunction, const int *call_arg_types,
                   block_t comp_retip, int tempmask) {
    auto nsubf = *mip++;
    auto nargs = *mip++;
    for (int i = 0; i < nsubf; i++) {
        // TODO: rather than going thru all args, only go thru those that have types
        for (int j = 0; j < nargs; j++) {
            auto desiredi = (type_elem_t)*mip++;
            auto &desired = GetTypeInfo(desiredi);
            if (desired.t != V_ANY) {
                auto &given = GetTypeInfo((type_elem_t)call_arg_types[j]);
                // Have to check the actual value, since given may be a supertype.
                // FIXME: this is slow.
                if ((given.t != desired.t && given.t != V_ANY) ||
                    (IsRef(given.t) && stack[sp - nargs + j + 1].ref()->tti != desiredi)) {
                    mip += nargs - j;  // Includes the code starting point.
                    goto fail;
                }
            } else {
            }
        } {
            call_arg_types += nargs;
            #ifdef VM_COMPILED_CODE_MODE
                InsPtr retip(comp_retip);
                InsPtr fun(next_mm_table[i]);
            #else
                InsPtr retip(call_arg_types - codestart);
                InsPtr fun(*mip);
                (void)comp_retip;
            #endif
            StartStackFrame(definedfunction, retip, tempmask);
            return FunIntroPre(fun);
        }
        fail:;
    }
    string argtypes;
    for (int j = 0; j < nargs; j++) {
        auto &ti = GetTypeInfo((type_elem_t)call_arg_types[j]);
        Value &v = stack[sp - nargs + j + 1];
        argtypes += ProperTypeName(IsRef(ti.t) && v.ref() ? v.ref()->ti(*this) : ti);
        if (j < nargs - 1) argtypes += ", ";
    }
    Error("the call " + flat_string_view(bcf->functions()->Get(definedfunction)->name()) + "(" +
          argtypes + ") did not match any function variants");
}

void VM::FinalStackVarsCleanup() {
    VMASSERT(sp < 0 && !stackframes.size());
    for (size_t i = 0; i < bcf->specidents()->size(); i++) {
        auto sid = bcf->specidents()->Get((uint)i);
        //Output(OUTPUT_INFO, "destructing: ",
        //                    flat_string_view(bcf->idents()->Get(sid->ididx())->name()));
        vars[i].DECTYPE(*this, GetTypeInfo((type_elem_t)sid->typeidx()).t);
    }
    #ifdef _DEBUG
        Output(OUTPUT_INFO, "stack at its highest was: ", maxsp);
    #endif
}

void VM::JumpTo(InsPtr j) {
    #ifdef VM_COMPILED_CODE_MODE
        next_call_target = j.f;
    #else
        ip = j.f + codestart;
    #endif
}

InsPtr VM::GetIP() {
    #ifdef VM_COMPILED_CODE_MODE
        return InsPtr(next_call_target);
    #else
        return InsPtr(ip - codestart);
    #endif
}

int VM::VarCleanup(ostringstream *error, int towhere) {
    auto &stf = stackframes.back();
    VMASSERT(sp == stf.spstart);
    auto fip = stf.funstart;
    auto nargs = *fip++;
    auto freevars = fip + nargs;
    fip += nargs;
    auto ndef = *fip++;
    auto defvars = fip + ndef;
    while (ndef--) {
        auto i = *--defvars;
        if (error) DumpVar(*error, vars[i], i, false);
        else vars[i].DECTYPE(*this, GetVarTypeInfo(i).t);
        vars[i] = POP();
    }
    while (nargs--) {
        auto i = *--freevars;
        if (error) DumpVar(*error, vars[i], i, false);
        else vars[i].DECTYPE(*this, GetVarTypeInfo(i).t);
        vars[i] = POP();
    }
    JumpTo(stf.retip);
    bool lastunwind = towhere == -1 || towhere == stf.definedfunction;
    auto tempmask = stf.tempmask;
    stackframes.pop_back();
    if (!lastunwind) {
        auto untilsp = stackframes.size() ? stackframes.back().spstart : -1;
        if (tempmask && !error) {
            for (uint i = 0; i < (uint)min(32, sp - untilsp); i++)
                if (((uint)tempmask) & (1u << i))
                    stack[untilsp + 1 + i].DECRTNIL(*this);
        }
        sp = untilsp;
    }
    return lastunwind;
}

// Initializes only 3 fields of the stack frame, FunIntro must be called right after.
void VM::StartStackFrame(int definedfunction, InsPtr retip, int tempmask) {
    stackframes.push_back(StackFrame());
    auto &stf = stackframes.back();
    stf.retip = retip;
    stf.definedfunction = definedfunction;
    stf.tempmask = tempmask;
}

void VM::FunIntroPre(InsPtr fun) {
    JumpTo(fun);
    #ifdef VM_COMPILED_CODE_MODE
        // We don't call FunIntro() here, instead the compiled code for FUNSTART/FUNMULTI actually
        // does that.
    #else
        VMASSERT(*ip == IL_FUNSTART);
        ip++;
        FunIntro();
    #endif
}

// Only valid to be called right after StartStackFrame, with no bytecode in-between.
void VM::FunIntro(VM_OP_ARGS) {
    #ifdef VM_PROFILER
        vm_count_fcalls++;
    #endif
    auto funstart = ip;
    if (sp > stacksize - STACKMARGIN) {
        // per function call increment should be small
        // FIXME: not safe for untrusted scripts, could simply add lots of locals
        // could record max number of locals? not allow more than N locals?
        if (stacksize >= maxstacksize) Error("stack overflow! (use set_max_stack_size() if needed)");
        auto nstack = new Value[stacksize *= 2];
        memcpy(nstack, stack, sizeof(Value) * (sp + 1));
        delete[] stack;
        stack = nstack;

        Output(OUTPUT_DEBUG, "stack grew to: ", stacksize);
    }
    auto nargs_fun = *ip++;
    for (int i = 0; i < nargs_fun; i++) swap(vars[ip[i]], stack[sp - nargs_fun + i + 1]);
    ip += nargs_fun;
    auto ndef = *ip++;
    for (int i = 0; i < ndef; i++) {
        // for most locals, this just saves an nil, only in recursive cases it has an actual value.
        // The reason we don't clear the var after backing it up is that in the DS case,
        // you want to be able to use the old value until a new one gets defined, as in a <- a + 1.
        // clearing it would save the INC and a DEC when it eventually gets overwritten,
        // so maybe we can at some point distinguish between vars that are used with DS and those
        // that are not.
        auto varidx = *ip++;
        PUSH(vars[varidx].INCTYPE(GetVarTypeInfo(varidx).t));
    }
    auto &stf = stackframes.back();
    stf.funstart = funstart;
    stf.spstart = sp;
    #ifdef _DEBUG
        if (sp > maxsp) maxsp = sp;
    #endif
}

bool VM::FunOut(int towhere, int nrv) {
    bool bottom = false;
    sp -= nrv;
    // Have to store these off the stack, since VarCleanup() may cause stack activity if coroutines
    // are destructed.
    memcpy(retvalstemp, TOPPTR(), nrv * sizeof(Value));
    for(;;) {
        if (!stackframes.size()) {
            if (towhere >= 0)
                Error("\"return from " + flat_string_view(bcf->functions()->Get(towhere)->name()) +
                      "\" outside of function");
            bottom = true;
            break;
        }
        if(VarCleanup(nullptr, towhere)) break;
    }
    memcpy(TOPPTR(), retvalstemp, nrv * sizeof(Value));
    sp += nrv;
    return bottom;
}

void VM::CoVarCleanup(LCoRoutine *co) {
    // Convenient way to copy everything back onto the stack.
    InsPtr tip(0);
    auto copylen = co->Resume(sp + 1, stack, stackframes, tip, nullptr);
    auto startsp = sp;
    sp += copylen;
    for (int i = co->stackframecopylen - 1; i >= 0 ; i--) {
        auto &stf = stackframes.back();
        // FIXME: guarantee this statically.
        if (stf.spstart != sp) {
            // There are temps from an enclosing for loop on the stack.
            for (int i = 0; i < sp - stf.spstart; i++)
                if (co->tm & (1 << i))
                    stack[stf.spstart + 1 + i].DECRTNIL(*this);
            sp = stf.spstart;
        }
        // Save the ip, because VarCleanup will jump to it.
        auto bip = GetIP();
        VarCleanup(nullptr, !i ? stf.definedfunction : -2);
        JumpTo(bip);
    }
    assert(sp == startsp);
    (void)startsp;
}

void VM::CoNonRec(const int *varip) {
    // probably could be skipped in a "release" mode
    for (auto co = curcoroutine; co; co = co->parent) if (co->varip == varip) {
        // if allowed, inner coro would save vars of outer, and then possibly restore them outside
        // of scope of parent
        Error("cannot create coroutine recursively");
    }
    // TODO: this check guarantees all saved stack vars are undef, except for DS vars,
    // which could still cause problems
}

void VM::CoNew(VM_OP_ARGS_CALL) {
    #ifdef VM_COMPILED_CODE_MODE
        ip++;
        InsPtr returnip(fcont);
    #else
        InsPtr returnip(*ip++);
    #endif
    auto ctidx = (type_elem_t)*ip++;
    CoNonRec(ip);
    curcoroutine = NewCoRoutine(returnip, ip, curcoroutine, ctidx);
    curcoroutine->BackupParentVars(*this, vars);
    int nvars = *ip++;
    ip += nvars;
    // Always have the active coroutine at top of the stack, retaining 1 refcount. This is
    // because it is not guaranteed that there any other references, and we can't have this drop
    // to 0 while active.
    PUSH(Value(curcoroutine));
}

void VM::CoSuspend(InsPtr retip) {
    int newtop = curcoroutine->Suspend(*this, sp + 1, stack, stackframes, retip, curcoroutine);
    JumpTo(retip);
    sp = newtop - 1; // top of stack is now coro value from create or resume
}

void VM::CoClean() {
    // This function is like yield, except happens implicitly when the coroutine returns.
    // It will jump back to the resume (or create) that invoked it.
    for (int i = 1; i <= *curcoroutine->varip; i++) {
        auto &var = vars[curcoroutine->varip[i]];
        var = curcoroutine->stackcopy[i - 1];
    }
    auto co = curcoroutine;
    CoSuspend(InsPtr(0));
    VMASSERT(co->stackcopylen == 1);
    co->active = false;
}

void VM::CoYield(VM_OP_ARGS_CALL) {
    assert(curcoroutine);  // Should not be possible since yield calls are statically checked.
    curcoroutine->tm = *ip++;
    #ifdef VM_COMPILED_CODE_MODE
        (void)ip;
        InsPtr retip(fcont);
    #else
        InsPtr retip(ip - codestart);
    #endif
    auto ret = POP();
    for (int i = 1; i <= *curcoroutine->varip; i++) {
        auto &var = vars[curcoroutine->varip[i]];
        PUSH(var);
        //var.type = V_NIL;
        var = curcoroutine->stackcopy[i - 1];
    }
    PUSH(ret);  // current value always top of the stack
    CoSuspend(retip);
}

void VM::CoResume(LCoRoutine *co) {
    if (co->stackstart >= 0)
        Error("cannot resume running coroutine");
    if (!co->active)
        Error("cannot resume finished coroutine");
    // This will be the return value for the corresponding yield, and holds the ref for gc.
    PUSH(Value(co));
    CoNonRec(co->varip);
    auto rip = GetIP();
    sp += co->Resume(sp + 1, stack, stackframes, rip, curcoroutine);
    JumpTo(rip);
    curcoroutine = co;
    // must be, since those vars got backed up in it before
    VMASSERT(curcoroutine->stackcopymax >=  *curcoroutine->varip);
    curcoroutine->stackcopylen = *curcoroutine->varip;
    //curcoroutine->BackupParentVars(vars);
    POP().DECTYPE(*this, GetTypeInfo(curcoroutine->ti(*this).yieldtype).t);    // previous current value
    for (int i = *curcoroutine->varip; i > 0; i--) {
        auto &var = vars[curcoroutine->varip[i]];
        // No INC, since parent is still on the stack and hold ref for us.
        curcoroutine->stackcopy[i - 1] = var;
        var = POP();
    }
    // the builtin call takes care of the return value
}

void VM::EndEval(Value &ret, ValueType vt) {
    ostringstream ss;
    ret.ToString(*this, ss, vt, programprintprefs);
    evalret = ss.str();
    ret.DECTYPE(*this, vt);
    assert(sp == -1);
    FinalStackVarsCleanup();
    vml.LogCleanup();
    DumpLeaks();
    VMASSERT(!curcoroutine);
    #ifdef VM_PROFILER
        Output(OUTPUT_INFO, "Profiler statistics:");
        uint64_t total = 0;
        auto fraction = 200;  // Line needs at least 0.5% to be counted.
        vector<uint64_t> lineprofilecounts(bcf->lineinfo()->size());
        for (size_t i = 0; i < codelen; i++) {
            auto li = LookupLine(codestart + i, codestart, bcf); // FIXME: can do faster
            size_t j = li - bcf->lineinfo()->Get(0);
            lineprofilecounts[j] += byteprofilecounts[i];
            total += byteprofilecounts[i];
        }
        struct LineRange { int line, lastline, fileidx; uint64_t count; };
        vector<LineRange> uniques;
        for (uint i = 0; i < bcf->lineinfo()->size(); i++) {
            uint64_t c = lineprofilecounts[i];
            if (c > total / fraction) {
                auto li = bcf->lineinfo()->Get(i);
                uniques.push_back(LineRange{ li->line(), li->line(), li->fileidx(), c });
            }
        }
        std::sort(uniques.begin(), uniques.end(), [&] (const LineRange &a, const LineRange &b) {
            return a.fileidx != b.fileidx ? a.fileidx < b.fileidx : a.line < b.line;
        });
        for (auto it = uniques.begin(); it != uniques.end();)  {
            if (it != uniques.begin()) {
                auto pit = it - 1;
                if (it->fileidx == pit->fileidx &&
                    ((it->line == pit->lastline) ||
                        (it->line == pit->lastline + 1 && pit->lastline++))) {
                    pit->count += it->count;
                    it = uniques.erase(it);
                    continue;
                }
            }
            ++it;
        }
        for (auto &u : uniques) {
            Output(OUTPUT_INFO, flat_string_view(bcf->filenames()->Get(u.fileidx)), "(", u.line,
                   u.lastline != u.line ? "-" + to_string(u.lastline) : "",
                   "): ", u.count * 100.0f / total, " %");
        }
        if (vm_count_fcalls)  // remove trivial VM executions from output
            Output(OUTPUT_INFO, "ins ", vm_count_ins, ", fcall ", vm_count_fcalls, ", bcall ",
                                vm_count_bcalls);
    #endif
    #ifndef VM_ERROR_RET_EXPERIMENT
    THROW_OR_ABORT(string("end-eval"));
    #endif
}

void VM::EvalProgram() {
    // Keep exception handling code in seperate function from hot loop in EvalProgramInner()
    // just in case it affects the compiler.
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        EvalProgramInner();
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        if (s != "end-eval") THROW_OR_ABORT(s);
    }
    #endif
}

void VM::EvalProgramInner() {
    for (;;) {
        #ifdef VM_COMPILED_CODE_MODE
            #if VM_DISPATCH_METHOD == VM_DISPATCH_TRAMPOLINE
                compiled_code_ip = ((block_t)compiled_code_ip)(*this);
            #elif VM_DISPATCH_METHOD == VM_DISPATCH_SWITCH_GOTO
                ((block_base_t)compiled_code_ip)();
                assert(false);  // Should not return here.
            #endif
        #else
            #ifdef _DEBUG
                if (trace) {
                    size_t trace_size = trace_tail ? 50 : 1;
                    if (trace_output.size() < trace_size) trace_output.resize(trace_size);
                    if (trace_ring_idx == trace_size) trace_ring_idx = 0;
                    auto &ss = trace_output[trace_ring_idx++];
                    ss.str(string());
                    DisAsmIns(natreg, ss, ip, codestart, typetable, bcf);
                    ss << " [" << (sp + 1) << "] - ";
                    #if RTT_ENABLED
                    if (sp >= 0) {
                        auto x = TOP();
                        x.ToString(*this, ss, x.type, debugpp);
                    }
                    if (sp >= 1) {
                        auto x = TOPM(1);
                        ss << ' ';
                        x.ToString(*this, ss, x.type, debugpp);
                    }
                    #endif
                    if (trace_tail) {
                        ss << '\n';
                    } else {
                        Output(OUTPUT_INFO, ss.str());
                    }
                }
                //currentline = LookupLine(ip).line;
            #endif
            #ifdef VM_PROFILER
                auto code_idx = size_t(ip - codestart);
                assert(code_idx < codelen);
                byteprofilecounts[code_idx]++;
                vm_count_ins++;
            #endif
            auto op = *ip++;
            #ifdef _DEBUG
                if (op < 0 || op >= IL_MAX_OPS)
                    Error(cat("bytecode format problem: ", op));
            #endif
            #ifndef VM_INS_SWITCH
                #ifdef VM_ERROR_RET_EXPERIMENT
                    bool terminate =
                #endif
                ((*this).*(f_ins_pointers[op]))();
                #ifdef VM_ERROR_RET_EXPERIMENT
                    if (terminate) return;
                #endif
            #endif
        #endif

#ifndef VM_INS_SWITCH
    // For loop and function end here.
    } 
}
    #define VM_DEF_INS(N) VM_INS_RET VM::F_##N(VM_OP_ARGS)
    #define VM_DEF_CAL(N) VM_INS_RET VM::F_##N(VM_OP_ARGS_CALL)
    #define VM_DEF_JMP(N) VM_JMP_RET VM::F_##N()
#else
    // We start a switch here that contains all instructions below.
        switch (op) {
    #define VM_DEF_INS(N) case IL_##N:
    #define VM_DEF_CAL(N) case IL_##N:
    #define VM_DEF_JMP(N) case IL_##N:
#endif

VM_DEF_INS(PUSHINT) { PUSH(Value(*ip++)); VM_RET; }
VM_DEF_INS(PUSHFLT) { PUSH(Value(*(float *)ip)); ip++; VM_RET; }
VM_DEF_INS(PUSHNIL) { PUSH(Value()); VM_RET; }

VM_DEF_INS(PUSHINT64) {
    #if !VALUE_MODEL_64
        Error("Code containing 64-bit constants cannot run on a 32-bit build.");
    #endif
    int64_t v = (uint)*ip++;
    v |= ((int64_t)*ip++) << 32;
    PUSH(Value(v));
    VM_RET;
}


VM_DEF_CAL(PUSHFUN) {
    #ifdef VM_COMPILED_CODE_MODE
        ip++;
    #else
        int start = *ip++;
        auto fcont = start;
    #endif
    PUSH(Value(InsPtr(fcont)));
    VM_RET;
}

VM_DEF_INS(PUSHSTR) {
    // FIXME: have a way that constant strings can stay in the bytecode,
    // or at least preallocate them all
    auto fb_s = bcf->stringtable()->Get(*ip++);
    auto s = NewString(flat_string_view(fb_s));
    PUSH(Value(s));
    VM_RET;
}

VM_DEF_CAL(CALL) {
    auto fvar = *ip++;
    #ifdef VM_COMPILED_CODE_MODE
        ip++;
        auto tm = *ip++;
        block_t fun = 0;  // Dynamic calls need this set, but for CALL it is ignored.
    #else
        auto fun = *ip++;
        auto tm = *ip++;
        auto fcont = ip - codestart;
    #endif
    StartStackFrame(fvar, InsPtr(fcont), tm);
    FunIntroPre(InsPtr(fun));
    VM_RET;
}

VM_DEF_CAL(CALLMULTI) {
    #ifdef VM_COMPILED_CODE_MODE
        next_mm_call = ip;
        next_call_target = fcont;  // Used just to transfer value here.
    #else
        auto fvar = *ip++;
        auto fun = *ip++;
        auto tm = *ip++;
        auto mip = codestart + fun;
        VMASSERT(*mip == IL_FUNMULTI);
        mip++;
        EvalMulti(mip, fvar, ip, 0, tm);
    #endif
    VM_RET;
}

VM_DEF_INS(FUNMULTI) {
    #ifdef VM_COMPILED_CODE_MODE
        auto cip = next_mm_call;
        auto fvar = *cip++;
        cip++;
        auto tm = *cip++;
        EvalMulti(ip, fvar, cip, next_call_target, tm);
    #else
        VMASSERT(false);
    #endif
    VM_RET;
}

VM_DEF_CAL(CALLVCOND) {
    // FIXME: don't need to check for function value again below if false
    if (!TOP().True()) {
        ip++;
        #ifdef VM_COMPILED_CODE_MODE
            next_call_target = 0;
        #endif
    } else {
        #ifdef VM_INS_SWITCH
            goto callv;
        #else
            F_CALLV(VM_OP_PASSTHRU);
        #endif
    }
    VM_RET;
}

VM_DEF_CAL(CALLV) {
    #ifdef VM_INS_SWITCH
    callv:
    #endif
    {
        Value fun = POP();
        VMTYPEEQ(fun, V_FUNCTION);
        auto tm = *ip++;
        #ifndef VM_COMPILED_CODE_MODE
            auto fcont = ip - codestart;
        #endif
        StartStackFrame(-1, InsPtr(fcont), tm);
        FunIntroPre(fun.ip());
        VM_RET;
    }
}

VM_DEF_INS(FUNSTART) {
    #ifdef VM_COMPILED_CODE_MODE
        FunIntro(ip);
    #else
        VMASSERT(false);
    #endif
    VM_RET;
}

VM_DEF_INS(FUNEND) {
    int nrv = *ip++;
    FunOut(-1, nrv);
    VM_RET;
}

VM_DEF_INS(RETURN) {
    int df = *ip++;
    int nrv = *ip++;
    int tidx = *ip++;
    if(FunOut(df, nrv)) {
        assert(nrv == 1);
        EndEval(POP(), GetTypeInfo((type_elem_t)tidx).t);
        VM_TERMINATE;
    }
    VM_RET;
}

VM_DEF_INS(EXIT) {
    int tidx = *ip++;
    EndEval(POP(), GetTypeInfo((type_elem_t)tidx).t);
    VM_TERMINATE;
}

VM_DEF_INS(CONT1) {
    auto nf = natreg.nfuns[*ip++];
    nf->cont1(*this);
    VM_RET;
}

#ifdef VM_COMPILED_CODE_MODE
    #define FOR_INIT
    #define FOR_CONTINUE return true
    #define FOR_FINISHED return false
#else
    #define FOR_INIT auto cont = *ip++
    #define FOR_CONTINUE ip = cont + codestart
    #define FOR_FINISHED
#endif
#define FORLOOP(L, iterref) { \
    FOR_INIT; \
    auto &iter = TOP(); \
    auto &i = TOPM(1); \
    assert(i.type == V_INT); \
    i.setival(i.ival() + 1); \
    intp len = 0; \
    if (i.ival() < (len = (L))) { \
        FOR_CONTINUE; \
    } else { \
        if (iterref) TOP().DECRT(*this); \
        (void)POP(); /* iter */ \
        (void)POP(); /* i */ \
        FOR_FINISHED; \
    } \
    VM_RET; \
}
#define FORELEM(L, V) \
    auto &iter = TOP(); (void)iter; \
    auto &i = TOPM(1); \
    assert(i.type == V_INT); \
    assert(i.ival() < L); \
    V; \
    VM_RET;

VM_DEF_JMP(IFOR) { FORLOOP(iter.ival(), false); }
VM_DEF_JMP(VFOR) { FORLOOP(iter.vval()->len, true); }
VM_DEF_JMP(NFOR) { FORLOOP(iter.stval()->Len(*this), true); }
VM_DEF_JMP(SFOR) { FORLOOP(iter.sval()->len, true); }

VM_DEF_INS(IFORELEM)    { FORELEM(iter.ival(), PUSH(i)); }
VM_DEF_INS(VFORELEM)    { FORELEM(iter.vval()->len, PUSH(iter.vval()->At(i.ival()))); }
VM_DEF_INS(VFORELEMREF) { FORELEM(iter.vval()->len, auto el = iter.vval()->At(i.ival()); el.INCRTNIL(); PUSH(el)); }
VM_DEF_INS(NFORELEM)    { FORELEM(iter.stval()->Len(*this), PUSH(iter.stval()->AtS(i.ival()))); }
VM_DEF_INS(SFORELEM)    { FORELEM(iter.sval()->len, PUSH(Value((int)((uchar *)iter.sval()->str())[i.ival()]))); }

VM_DEF_INS(FORLOOPI) {
    auto &i = TOPM(1);  // This relies on for being inlined, otherwise it would be 2.
    assert(i.type == V_INT);
    PUSH(i);
    VM_RET;
}

#define BCALLOPH(PRE,N,DECLS,ARGS,RETOP) VM_DEF_INS(BCALL##PRE##N) { \
    BCallProf(); \
    auto nf = natreg.nfuns[*ip++]; \
    DECLS; \
    Value v = nf->fun.f##N ARGS; \
    RETOP; \
    VM_RET; \
}

#define BCALLOP(N,DECLS,ARGS) \
    BCALLOPH(RET,N,DECLS,ARGS,PUSH(v);BCallRetCheck(nf)) \
    BCALLOPH(REF,N,DECLS,ARGS,v.DECRTNIL(*this)) \
    BCALLOPH(UNB,N,DECLS,ARGS,(void)v)

BCALLOP(0, {}, (*this));
BCALLOP(1, auto a0 = POP(), (*this, a0));
BCALLOP(2, auto a1 = POP();auto a0 = POP(), (*this, a0, a1));
BCALLOP(3, auto a2 = POP();auto a1 = POP();auto a0 = POP(), (*this, a0, a1, a2));
BCALLOP(4, auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (*this, a0, a1, a2, a3));
BCALLOP(5, auto a4 = POP();auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (*this, a0, a1, a2, a3, a4));
BCALLOP(6, auto a5 = POP();auto a4 = POP();auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (*this, a0, a1, a2, a3, a4, a5));
BCALLOP(7, auto a6 = POP();auto a5 = POP();auto a4 = POP();auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (*this, a0, a1, a2, a3, a4, a5, a6));

VM_DEF_INS(NEWVEC) {
    auto type = (type_elem_t)*ip++;
    auto len = *ip++;
    auto vec = NewVec(len, len, type);
    if (len) vec->Init(*this, TOPPTR() - len, false);
    POPN(len);
    PUSH(Value(vec));
    VM_RET;
}

VM_DEF_INS(NEWSTRUCT) {
    auto type = (type_elem_t)*ip++;
    auto len = GetTypeInfo(type).len;
    auto vec = NewStruct(len, type);
    if (len) vec->Init(*this, TOPPTR() - len, len, false);
    POPN(len);
    PUSH(Value(vec));
    VM_RET;
}

VM_DEF_INS(POP)    { POP(); VM_RET; }
VM_DEF_INS(POPREF) { POP().DECRTNIL(*this); VM_RET; }

VM_DEF_INS(DUP)    { auto x = TOP();            PUSH(x); VM_RET; }
VM_DEF_INS(DUPREF) { auto x = TOP().INCRTNIL(); PUSH(x); VM_RET; }

#define REFOP(exp) { res = exp; a.DECRTNIL(*this); b.DECRTNIL(*this); }
#define GETARGS() Value b = POP(); Value a = POP()
#define TYPEOP(op, extras, field, errstat) Value res; errstat; \
    if (extras & 1 && b.field == 0) Div0(); res = a.field op b.field;

#define _IOP(op, extras) \
    TYPEOP(op, extras, ival(), assert(a.type == V_INT && b.type == V_INT))
#define _FOP(op, extras) \
    TYPEOP(op, extras, fval(), assert(a.type == V_FLOAT && b.type == V_FLOAT))

#define _VELEM(a, i, isfloat, T) (isfloat ? (T)a.stval()->AtS(i).fval() : (T)a.stval()->AtS(i).ival())
#define _VOP(op, extras, T, isfloat, withscalar, comp) Value res; { \
    auto len = a.stval()->Len(*this); \
    assert(withscalar || b.stval()->Len(*this) == len); \
    auto v = NewStruct(len, comp ? GetIntVectorType((int)len) : a.stval()->tti); \
    res = Value(v); \
    for (intp j = 0; j < len; j++) { \
        if (withscalar) VMTYPEEQ(b, isfloat ? V_FLOAT : V_INT) \
        else VMTYPEEQ(b.stval()->AtS(j), isfloat ? V_FLOAT : V_INT); \
        auto bv = withscalar ? (isfloat ? (T)b.fval() : (T)b.ival()) : _VELEM(b, j, isfloat, T); \
        if (extras&1 && bv == 0) Div0(); \
        VMTYPEEQ(a.stval()->AtS(j), isfloat ? V_FLOAT : V_INT); \
        v->AtS(j) = Value(_VELEM(a, j, isfloat, T) op bv); \
    } \
    a.DECRT(*this); \
    if (!withscalar) b.DECRT(*this); \
}
#define _IVOP(op, extras, withscalar, icomp) _VOP(op, extras, intp, false, withscalar, icomp)
#define _FVOP(op, extras, withscalar, fcomp) _VOP(op, extras, floatp, true, withscalar, fcomp)

#define _SOP(op) Value res; REFOP((*a.sval()) op (*b.sval()))
#define _SCAT() Value res; \
                REFOP(NewString(a.sval()->strv(), b.sval()->strv()))

#define ACOMPEN(op)        { GETARGS(); Value res; REFOP(a.any() op b.any()); PUSH(res); VM_RET; }
#define IOP(op, extras)    { GETARGS(); _IOP(op, extras);                PUSH(res); VM_RET; }
#define FOP(op, extras)    { GETARGS(); _FOP(op, extras);                PUSH(res); VM_RET; }
#define IVVOP(op, extras)  { GETARGS(); _IVOP(op, extras, false, false); PUSH(res); VM_RET; }
#define IVVOPC(op, extras) { GETARGS(); _IVOP(op, extras, false, true);  PUSH(res); VM_RET; }
#define FVVOP(op, extras)  { GETARGS(); _FVOP(op, extras, false, false); PUSH(res); VM_RET; }
#define FVVOPC(op, extras) { GETARGS(); _FVOP(op, extras, false, true);  PUSH(res); VM_RET; }
#define IVSOP(op, extras)  { GETARGS(); _IVOP(op, extras, true, false);  PUSH(res); VM_RET; }
#define IVSOPC(op, extras) { GETARGS(); _IVOP(op, extras, true, true);   PUSH(res); VM_RET; }
#define FVSOP(op, extras)  { GETARGS(); _FVOP(op, extras, true, false);  PUSH(res); VM_RET; }
#define FVSOPC(op, extras) { GETARGS(); _FVOP(op, extras, true, true);   PUSH(res); VM_RET; }
#define SOP(op)            { GETARGS(); _SOP(op);                        PUSH(res); VM_RET; }
#define SCAT()             { GETARGS(); _SCAT();                         PUSH(res); VM_RET; }

// +  += I F Vif S
// -  -= I F Vif
// *  *= I F Vif
// /  /= I F Vif
// %  %= I   Vi

// <     I F Vif S
// >     I F Vif S
// <=    I F Vif S
// >=    I F Vif S
// ==    I F V   S   // FIXME differentiate struct / value / vector
// !=    I F V   S

// U-    I F Vif
// U!    A

VM_DEF_INS(IVVADD) { IVVOP(+,  0);  }
VM_DEF_INS(IVVSUB) { IVVOP(-,  0);  }
VM_DEF_INS(IVVMUL) { IVVOP(*,  0);  }
VM_DEF_INS(IVVDIV) { IVVOP(/,  1);  }
VM_DEF_INS(IVVMOD) { VMASSERT(0); VM_RET; }
VM_DEF_INS(IVVLT)  { IVVOP(<,  0);  }
VM_DEF_INS(IVVGT)  { IVVOP(>,  0);  }
VM_DEF_INS(IVVLE)  { IVVOP(<=, 0);  }
VM_DEF_INS(IVVGE)  { IVVOP(>=, 0);  }
VM_DEF_INS(FVVADD) { FVVOP(+,  0);  }
VM_DEF_INS(FVVSUB) { FVVOP(-,  0);  }
VM_DEF_INS(FVVMUL) { FVVOP(*,  0);  }
VM_DEF_INS(FVVDIV) { FVVOP(/,  1);  }
VM_DEF_INS(FVVMOD) { VMASSERT(0); VM_RET; }
VM_DEF_INS(FVVLT)  { FVVOPC(<,  0); }
VM_DEF_INS(FVVGT)  { FVVOPC(>,  0); }
VM_DEF_INS(FVVLE)  { FVVOPC(<=, 0); }
VM_DEF_INS(FVVGE)  { FVVOPC(>=, 0); }

VM_DEF_INS(IVSADD) { IVSOP(+,  0);  }
VM_DEF_INS(IVSSUB) { IVSOP(-,  0);  }
VM_DEF_INS(IVSMUL) { IVSOP(*,  0);  }
VM_DEF_INS(IVSDIV) { IVSOP(/,  1);  }
VM_DEF_INS(IVSMOD) { VMASSERT(0); VM_RET; }
VM_DEF_INS(IVSLT)  { IVSOP(<,  0);  }
VM_DEF_INS(IVSGT)  { IVSOP(>,  0);  }
VM_DEF_INS(IVSLE)  { IVSOP(<=, 0);  }
VM_DEF_INS(IVSGE)  { IVSOP(>=, 0);  }
VM_DEF_INS(FVSADD) { FVSOP(+,  0);  }
VM_DEF_INS(FVSSUB) { FVSOP(-,  0);  }
VM_DEF_INS(FVSMUL) { FVSOP(*,  0);  }
VM_DEF_INS(FVSDIV) { FVSOP(/,  1);  }
VM_DEF_INS(FVSMOD) { VMASSERT(0); VM_RET; }
VM_DEF_INS(FVSLT)  { FVSOPC(<,  0); }
VM_DEF_INS(FVSGT)  { FVSOPC(>,  0); }
VM_DEF_INS(FVSLE)  { FVSOPC(<=, 0); }
VM_DEF_INS(FVSGE)  { FVSOPC(>=, 0); }

VM_DEF_INS(AEQ) { ACOMPEN(==); }
VM_DEF_INS(ANE) { ACOMPEN(!=); }

VM_DEF_INS(IADD) { IOP(+,  0); }
VM_DEF_INS(ISUB) { IOP(-,  0); }
VM_DEF_INS(IMUL) { IOP(*,  0); }
VM_DEF_INS(IDIV) { IOP(/ , 1); }
VM_DEF_INS(IMOD) { IOP(%,  1); }
VM_DEF_INS(ILT)  { IOP(<,  0); }
VM_DEF_INS(IGT)  { IOP(>,  0); }
VM_DEF_INS(ILE)  { IOP(<=, 0); }
VM_DEF_INS(IGE)  { IOP(>=, 0); }
VM_DEF_INS(IEQ)  { IOP(==, 0); }
VM_DEF_INS(INE)  { IOP(!=, 0); }

VM_DEF_INS(FADD) { FOP(+,  0); }
VM_DEF_INS(FSUB) { FOP(-,  0); }
VM_DEF_INS(FMUL) { FOP(*,  0); }
VM_DEF_INS(FDIV) { FOP(/,  1); }
VM_DEF_INS(FMOD) { VMASSERT(0); VM_RET; }
VM_DEF_INS(FLT)  { FOP(<,  0); }
VM_DEF_INS(FGT)  { FOP(>,  0); }
VM_DEF_INS(FLE)  { FOP(<=, 0); }
VM_DEF_INS(FGE)  { FOP(>=, 0); }
VM_DEF_INS(FEQ)  { FOP(==, 0); }
VM_DEF_INS(FNE)  { FOP(!=, 0); }

VM_DEF_INS(SADD) { SCAT();  }
VM_DEF_INS(SSUB) { VMASSERT(0); VM_RET; }
VM_DEF_INS(SMUL) { VMASSERT(0); VM_RET; }
VM_DEF_INS(SDIV) { VMASSERT(0); VM_RET; }
VM_DEF_INS(SMOD) { VMASSERT(0); VM_RET; }
VM_DEF_INS(SLT)  { SOP(<);  }
VM_DEF_INS(SGT)  { SOP(>);  }
VM_DEF_INS(SLE)  { SOP(<=); }
VM_DEF_INS(SGE)  { SOP(>=); }
VM_DEF_INS(SEQ)  { SOP(==); }
VM_DEF_INS(SNE)  { SOP(!=); }

VM_DEF_INS(IUMINUS) { Value a = POP(); PUSH(Value(-a.ival())); VM_RET; }
VM_DEF_INS(FUMINUS) { Value a = POP(); PUSH(Value(-a.fval())); VM_RET; }

#define VUMINUS(isfloat, type) { \
    Value a = POP(); \
    Value res; \
    auto len = a.stval()->Len(*this); \
    res = Value(NewStruct(len, a.stval()->tti)); \
    for (intp i = 0; i < len; i++) { \
        VMTYPEEQ(a.stval()->AtS(i), isfloat ? V_FLOAT : V_INT); \
        res.stval()->AtS(i) = Value(-_VELEM(a, i, isfloat, type)); \
    } \
    a.DECRT(*this); \
    PUSH(res); \
    VM_RET; \
    }
VM_DEF_INS(IVUMINUS) { VUMINUS(false, intp) }
VM_DEF_INS(FVUMINUS) { VUMINUS(true, floatp) }

VM_DEF_INS(LOGNOT) {
    Value a = POP();
    PUSH(!a.True());
    VM_RET;
}
VM_DEF_INS(LOGNOTREF) {
    Value a = POP();
    bool b = a.True();
    PUSH(!b);
    if (b) a.DECRT(*this);
    VM_RET; 
}

#define BITOP(op) { GETARGS(); PUSH(a.ival() op b.ival()); VM_RET; }
VM_DEF_INS(BINAND) { BITOP(&);  }
VM_DEF_INS(BINOR)  { BITOP(|);  }
VM_DEF_INS(XOR)    { BITOP(^);  }
VM_DEF_INS(ASL)    { BITOP(<<); }
VM_DEF_INS(ASR)    { BITOP(>>); }
VM_DEF_INS(NEG)    { auto a = POP(); PUSH(~a.ival()); VM_RET; }

VM_DEF_INS(I2F) {
    Value a = POP();
    VMTYPEEQ(a, V_INT);
    PUSH((float)a.ival());
    VM_RET;
}

VM_DEF_INS(A2S) {
    Value a = POP();
    assert(IsRefNil(a.type));
    ss_reuse.str(string());
    ss_reuse.clear();
    a.ToString(*this, ss_reuse, a.ref() ? a.ref()->ti(*this).t : V_NIL, programprintprefs);
    PUSH(NewString(ss_reuse.str()));
    a.DECRTNIL(*this);
    VM_RET;
}

VM_DEF_INS(I2A) {
    Value i = POP();
    VMTYPEEQ(i, V_INT);
    PUSH(NewInt(i.ival()));
    VM_RET;
}

VM_DEF_INS(F2A) {
    Value f = POP();
    VMTYPEEQ(f, V_FLOAT);
    PUSH(NewFloat(f.fval()));
    VM_RET;
}

VM_DEF_INS(E2B) {
    Value a = POP();
    PUSH(a.True());
    VM_RET;
}

VM_DEF_INS(E2BREF) {
    Value a = POP();
    PUSH(a.True());
    a.DECRTNIL(*this);
    VM_RET;
}

VM_DEF_INS(PUSHVAR)    { PUSH(vars[*ip++]); VM_RET; }
VM_DEF_INS(PUSHVARREF) { PUSH(vars[*ip++].INCRTNIL()); VM_RET; }

VM_DEF_INS(PUSHFLD) {
    auto i = *ip++;
    Value r = POP();
    VMASSERT(r.ref());
    assert(i < r.stval()->Len(*this));
    PUSH(r.stval()->AtS(i));
    r.DECRT(*this);
    VM_RET;
}
VM_DEF_INS(PUSHFLDREF) {
    auto i = *ip++;
    Value r = POP();
    VMASSERT(r.ref());
    assert(i < r.stval()->Len(*this));
    auto el = r.stval()->AtS(i);
    el.INCRTNIL();
    PUSH(el);
    r.DECRT(*this);
    VM_RET;
}
VM_DEF_INS(PUSHFLDMREF) {
    auto i = *ip++;
    Value r = POP();
    if (!r.ref()) {
        PUSH(r);
    } else {
        assert(i < r.stval()->Len(*this));
        auto el = r.stval()->AtS(i);
        el.INCRTNIL();
        PUSH(el);
        r.DECRT(*this);
    }
    VM_RET;
}

VM_DEF_INS(VPUSHIDXI)    { PushDerefIdxVectorSc(POP().ival()); VM_RET; }
VM_DEF_INS(VPUSHIDXV)    { PushDerefIdxVectorSc(GrabIndex(POP())); VM_RET; }
VM_DEF_INS(VPUSHIDXIREF) { PushDerefIdxVectorRef(POP().ival()); VM_RET; }
VM_DEF_INS(VPUSHIDXVREF) { PushDerefIdxVectorRef(GrabIndex(POP())); VM_RET; }
VM_DEF_INS(NPUSHIDXI)    { PushDerefIdxStruct(POP().ival()); VM_RET; }
VM_DEF_INS(SPUSHIDXI)    { PushDerefIdxString(POP().ival()); VM_RET; }

VM_DEF_INS(PUSHLOC) {
    int i = *ip++;
    Value coro = POP();
    VMTYPEEQ(coro, V_COROUTINE);
    PUSH(coro.cval()->GetVar(*this, i));
    TOP().INCTYPE(GetVarTypeInfo(i).t);
    coro.DECRT(*this);
    VM_RET;
}

VM_DEF_INS(LVALLOC) {
    int lvalop = *ip++;
    int i = *ip++;
    Value coro = POP();
    VMTYPEEQ(coro, V_COROUTINE);
    Value &a = coro.cval()->GetVar(*this, i);
    LvalueOp(lvalop, a);
    coro.DECRT(*this);
    VM_RET;
}

VM_DEF_INS(LVALVAR)    {
    int lvalop = *ip++;
    LvalueOp(lvalop, vars[*ip++]);
    VM_RET;
}

VM_DEF_INS(VLVALIDXI) { int lvalop = *ip++; LvalueIdxVector(lvalop, POP().ival()); VM_RET; }
VM_DEF_INS(NLVALIDXI) { int lvalop = *ip++; LvalueIdxStruct(lvalop, POP().ival()); VM_RET; }
VM_DEF_INS(LVALIDXV)  { int lvalop = *ip++; LvalueIdxVector(lvalop, GrabIndex(POP())); VM_RET; }
VM_DEF_INS(LVALFLD)   { int lvalop = *ip++; LvalueField(lvalop, *ip++); VM_RET; }

#ifdef VM_COMPILED_CODE_MODE
    #define GJUMP(N, V, D1, C, P, D2) VM_JMP_RET VM::F_##N() \
        { V; D1; if (C) { P; return true; } else { D2; return false; } }
#else
    #define GJUMP(N, V, D1, C, P, D2) VM_DEF_JMP(N) \
        { V; auto nip = *ip++; D1; if (C) { ip = codestart + nip; P; } else { D2; } VM_RET; }
#endif

GJUMP(JUMP          ,               ,             , true     ,              ,             )
GJUMP(JUMPFAIL      , auto x = POP(),             , !x.True(),              ,             )
GJUMP(JUMPFAILR     , auto x = POP(),             , !x.True(), PUSH(x)      ,             )
GJUMP(JUMPFAILN     , auto x = POP(),             , !x.True(), PUSH(Value()),             )
GJUMP(JUMPNOFAIL    , auto x = POP(),             ,  x.True(),              ,             )
GJUMP(JUMPNOFAILR   , auto x = POP(),             ,  x.True(), PUSH(x)      ,             )
GJUMP(JUMPFAILREF   , auto x = POP(), x.DECRTNIL(*this), !x.True(),              ,             )
GJUMP(JUMPFAILRREF  , auto x = POP(),             , !x.True(), PUSH(x)      , x.DECRTNIL(*this))
GJUMP(JUMPFAILNREF  , auto x = POP(), x.DECRTNIL(*this), !x.True(), PUSH(Value()),             )
GJUMP(JUMPNOFAILREF , auto x = POP(), x.DECRTNIL(*this),  x.True(),              ,             )
GJUMP(JUMPNOFAILRREF, auto x = POP(),             ,  x.True(), PUSH(x)      , x.DECRTNIL(*this))

VM_DEF_INS(ISTYPE) {
    auto to = (type_elem_t)*ip++;
    auto v = POP();
    // Optimizer guarantees we don't have to deal with scalars.
    if (v.refnil()) PUSH(v.ref()->tti == to);
    else PUSH(GetTypeInfo(to).t == V_NIL);  // FIXME: can replace by fixed type_elem_t ?
    v.DECRTNIL(*this);
    VM_RET;
}

VM_DEF_CAL(YIELD) { CoYield(VM_OP_PASSTHRU); VM_RET; }

// This value never gets used anywhere, just a placeholder.
VM_DEF_INS(COCL) { PUSH(Value(0, V_YIELD)); VM_RET; }

VM_DEF_CAL(CORO) { CoNew(VM_OP_PASSTHRU); VM_RET; }

VM_DEF_INS(COEND) { CoClean(); VM_RET; }

VM_DEF_INS(LOGREAD) {
    auto val = POP();
    PUSH(vml.LogGet(val, *ip++));
    VM_RET;
}

VM_DEF_INS(LOGWRITE) {
    auto vidx = *ip++;
    auto lidx = *ip++;
    vml.LogWrite(vars[vidx], lidx);
    VM_RET;
}

VM_DEF_INS(ABORT) {
    Error("VM internal error: abort");
    VM_RET;
}

#ifdef VM_INS_SWITCH
        }  // switch
    }  // for
}  // EvalProgramInner()
#endif

void VM::IDXErr(intp i, intp n, const RefObj *v) {
    Error(cat("index ", i, " out of range ", n), v);
}
#define RANGECHECK(I, BOUND, VEC) if ((uintp)I >= (uintp)BOUND) IDXErr(I, BOUND, VEC);

void VM::PushDerefIdxVectorSc(intp i) {
    Value r = POP();
    VMASSERT(r.ref());
    RANGECHECK(i, r.vval()->len, r.vval());
    PUSH(r.vval()->At(i));
    r.DECRT(*this);
}

void VM::PushDerefIdxVectorRef(intp i) {
    Value r = POP();
    VMASSERT(r.ref());
    RANGECHECK(i, r.vval()->len, r.vval());
    auto el = r.vval()->At(i);
    el.INCRTNIL();
    PUSH(el);
    r.DECRT(*this);
}

void VM::PushDerefIdxStruct(intp i) {
    Value r = POP();
    VMASSERT(r.ref());
    RANGECHECK(i, r.stval()->Len(*this), r.stval());
    PUSH(r.stval()->AtS(i));
    r.DECRT(*this);
}

void VM::PushDerefIdxString(intp i) {
    Value r = POP();
    VMASSERT(r.ref());
    RANGECHECK(i, r.sval()->len, r.sval());
    PUSH(Value((int)((uchar *)r.sval()->str())[i]));
    r.DECRT(*this);
}

void VM::LvalueIdxVector(int lvalop, intp i) {
    Value vec = POP();
    RANGECHECK(i, vec.vval()->len, vec.vval());
    Value &a = vec.vval()->At(i);
    LvalueOp(lvalop, a);
    vec.DECRT(*this);
}

void VM::LvalueIdxStruct(int lvalop, intp i) {
    Value vec = POP();
    RANGECHECK(i, vec.stval()->Len(*this), vec.stval());
    Value &a = vec.stval()->AtS(i);
    LvalueOp(lvalop, a);
    vec.DECRT(*this);
}

void VM::LvalueField(int lvalop, intp i) {
    Value vec = POP();
    RANGECHECK(i, vec.stval()->Len(*this), vec.stval());
    Value &a = vec.stval()->AtS(i);
    LvalueOp(lvalop, a);
    vec.DECRT(*this);
}

void VM::LvalueOp(int op, Value &a) {
    switch(op) {
        #define LVALCASE(N, B) case N: { Value b = POP(); B;  break; }
        LVALCASE(LVO_IVVADD , _IVOP(+, 0, false, false); a = res;                  )
        LVALCASE(LVO_IVVADDR, _IVOP(+, 0, false, false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_IVVSUB , _IVOP(-, 0, false, false); a = res;                  )
        LVALCASE(LVO_IVVSUBR, _IVOP(-, 0, false, false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_IVVMUL , _IVOP(*, 0, false, false); a = res;                  )
        LVALCASE(LVO_IVVMULR, _IVOP(*, 0, false, false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_IVVDIV , _IVOP(/, 1, false, false); a = res;                  )
        LVALCASE(LVO_IVVDIVR, _IVOP(/, 1, false, false); a = res; PUSH(res.INCRT()))

        LVALCASE(LVO_FVVADD , _FVOP(+, 0, false, false); a = res;                  )
        LVALCASE(LVO_FVVADDR, _FVOP(+, 0, false, false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_FVVSUB , _FVOP(-, 0, false, false); a = res;                  )
        LVALCASE(LVO_FVVSUBR, _FVOP(-, 0, false, false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_FVVMUL , _FVOP(*, 0, false, false); a = res;                  )
        LVALCASE(LVO_FVVMULR, _FVOP(*, 0, false, false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_FVVDIV , _FVOP(/, 1, false, false); a = res;                  )
        LVALCASE(LVO_FVVDIVR, _FVOP(/, 1, false, false); a = res; PUSH(res.INCRT()))

        LVALCASE(LVO_IVSADD , _IVOP(+, 0, true,  false); a = res;                  )
        LVALCASE(LVO_IVSADDR, _IVOP(+, 0, true,  false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_IVSSUB , _IVOP(-, 0, true,  false); a = res;                  )
        LVALCASE(LVO_IVSSUBR, _IVOP(-, 0, true,  false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_IVSMUL , _IVOP(*, 0, true,  false); a = res;                  )
        LVALCASE(LVO_IVSMULR, _IVOP(*, 0, true,  false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_IVSDIV , _IVOP(/, 1, true,  false); a = res;                  )
        LVALCASE(LVO_IVSDIVR, _IVOP(/, 1, true,  false); a = res; PUSH(res.INCRT()))

        LVALCASE(LVO_FVSADD , _FVOP(+, 0, true,  false); a = res;                  )
        LVALCASE(LVO_FVSADDR, _FVOP(+, 0, true,  false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_FVSSUB , _FVOP(-, 0, true,  false); a = res;                  )
        LVALCASE(LVO_FVSSUBR, _FVOP(-, 0, true,  false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_FVSMUL , _FVOP(*, 0, true,  false); a = res;                  )
        LVALCASE(LVO_FVSMULR, _FVOP(*, 0, true,  false); a = res; PUSH(res.INCRT()))
        LVALCASE(LVO_FVSDIV , _FVOP(/, 1, true,  false); a = res;                  )
        LVALCASE(LVO_FVSDIVR, _FVOP(/, 1, true,  false); a = res; PUSH(res.INCRT()))

        LVALCASE(LVO_IADD   , _IOP(+, 0);                a = res;                  )
        LVALCASE(LVO_IADDR  , _IOP(+, 0);                a = res; PUSH(res)        )
        LVALCASE(LVO_ISUB   , _IOP(-, 0);                a = res;                  )
        LVALCASE(LVO_ISUBR  , _IOP(-, 0);                a = res; PUSH(res)        )
        LVALCASE(LVO_IMUL   , _IOP(*, 0);                a = res;                  )
        LVALCASE(LVO_IMULR  , _IOP(*, 0);                a = res; PUSH(res)        )
        LVALCASE(LVO_IDIV   , _IOP(/, 1);                a = res;                  )
        LVALCASE(LVO_IDIVR  , _IOP(/, 1);                a = res; PUSH(res)        )
        LVALCASE(LVO_IMOD   , _IOP(%, 1);                a = res;                  )
        LVALCASE(LVO_IMODR  , _IOP(%, 1);                a = res; PUSH(res)        )

        LVALCASE(LVO_FADD   , _FOP(+, 0);                a = res;                  )
        LVALCASE(LVO_FADDR  , _FOP(+, 0);                a = res; PUSH(res)        )
        LVALCASE(LVO_FSUB   , _FOP(-, 0);                a = res;                  )
        LVALCASE(LVO_FSUBR  , _FOP(-, 0);                a = res; PUSH(res)        )
        LVALCASE(LVO_FMUL   , _FOP(*, 0);                a = res;                  )
        LVALCASE(LVO_FMULR  , _FOP(*, 0);                a = res; PUSH(res)        )
        LVALCASE(LVO_FDIV   , _FOP(/, 1);                a = res;                  )
        LVALCASE(LVO_FDIVR  , _FOP(/, 1);                a = res; PUSH(res)        )

        LVALCASE(LVO_SADD   , _SCAT();                   a = res;                  )
        LVALCASE(LVO_SADDR  , _SCAT();                   a = res; PUSH(res.INCRT()))

        case LVO_WRITE:     { Value  b = POP();                          a = b; break; }
        case LVO_WRITER:    { Value &b = TOP();                          a = b; break; }
        case LVO_WRITEREF:  { Value  b = POP();            a.DECRTNIL(*this); a = b; break; }
        case LVO_WRITERREF: { Value &b = TOP().INCRTNIL(); a.DECRTNIL(*this); a = b; break; }

        #define PPOP(ret, op, pre, accessor) { \
            if (ret && !pre) PUSH(a); \
            a.set##accessor(a.accessor() op 1); \
            if (ret && pre) PUSH(a); \
        }
        case LVO_IPP:
        case LVO_IPPR:  { PPOP(op == LVO_IPPR,  +, true,  ival);  break; }
        case LVO_IMM:
        case LVO_IMMR:  { PPOP(op == LVO_IMMR,  -, true,  ival);  break; }
        case LVO_IPPP:
        case LVO_IPPPR: { PPOP(op == LVO_IPPPR, +, false, ival); break; }
        case LVO_IMMP:
        case LVO_IMMPR: { PPOP(op == LVO_IMMPR, -, false, ival); break; }
        case LVO_FPP:
        case LVO_FPPR:  { PPOP(op == LVO_FPPR,  +, true,  fval);  break; }
        case LVO_FMM:
        case LVO_FMMR:  { PPOP(op == LVO_FMMR,  -, true,  fval);  break; }
        case LVO_FPPP:
        case LVO_FPPPR: { PPOP(op == LVO_FPPPR, +, false, fval); break; }
        case LVO_FMMP:
        case LVO_FMMPR: { PPOP(op == LVO_FMMPR, -, false, fval); break; }

        default:
            Error(cat("bytecode format problem (lvalue): ", op));
    }
}

string VM::ProperTypeName(const TypeInfo &ti) {
    switch (ti.t) {
        case V_STRUCT: return string(ReverseLookupType(ti.structidx));
        case V_NIL: return ProperTypeName(GetTypeInfo(ti.subt)) + "?";
        case V_VECTOR: return "[" + ProperTypeName(GetTypeInfo(ti.subt)) + "]";
        default: return string(BaseTypeName(ti.t));
    }
}

void VM::BCallProf() {
    #ifdef VM_PROFILER
        vm_count_bcalls++;
    #endif
}

void VM::BCallRetCheck(const NativeFun *nf) {
    #if RTT_ENABLED
        // See if any builtin function is lying about what type it returns
        // other function types return intermediary values that don't correspond to final return
        // values.
        if (!nf->has_body) {
            for (size_t i = 0; i < nf->retvals.v.size(); i++) {
                auto t = (TOPPTR() - nf->retvals.v.size() + i)->type;
                auto u = nf->retvals.v[i].type->t;
                assert(t == u || u == V_ANY || u == V_NIL || (u == V_VECTOR && t == V_STRUCT));
            }
            assert(nf->retvals.v.size() || TOP().type == V_NIL);
        }
    #else
        (void)nf;
    #endif
}

intp VM::GrabIndex(const Value &idx) {
    auto &v = TOP();
    for (auto i = idx.stval()->Len(*this) - 1; ; i--) {
        auto sidx = idx.stval()->AtS(i);
        VMTYPEEQ(sidx, V_INT);
        if (!i) {
            idx.DECRT(*this);
            return sidx.ival();
        }
        RANGECHECK(sidx.ival(), v.vval()->len, v.vval());
        auto nv = v.vval()->At(sidx.ival()).INCRT();
        v.DECRT(*this);
        v = nv;
    }
}

void VM::Push(const Value &v) { PUSH(v); }

Value VM::Pop() { return POP(); }

string_view VM::StructName(const TypeInfo &ti) {
    return flat_string_view(bcf->structs()->Get(ti.structidx)->name());
}

string_view VM::ReverseLookupType(uint v) {
    auto s = bcf->structs()->Get(v)->name();
    return flat_string_view(s);
}

int VM::GC() {  // shouldn't really be used, but just in case
    for (int i = 0; i <= sp; i++) {
        //stack[i].Mark(?);
        // TODO: we could actually walk the stack here and recover correct types, but it is so easy
        // to avoid this error that that may not be worth it.
        if (stack[i].True())  // Typically all nil
            Error("collect_garbage() must be called from a top level function");
    }
    for (uint i = 0; i < bcf->specidents()->size(); i++) vars[i].Mark(*this, GetVarTypeInfo(i).t);
    vml.LogMark();
    vector<RefObj *> leaks;
    int total = 0;
    pool.findleaks([&](void *p) {
        total++;
        auto r = (RefObj *)p;
        if (r->tti == TYPE_ELEM_VALUEBUF ||
            r->tti == TYPE_ELEM_STACKFRAMEBUF) return;
        if (r->refc > 0) leaks.push_back(r);
        r->refc = -r->refc;
    });
    for (auto p : leaks) {
        auto ro = (RefObj *)p;
        ro->refc = 0;
        ro->DECDELETE(*this, false);
    }
    return (int)leaks.size();
}

}  // namespace lobster
