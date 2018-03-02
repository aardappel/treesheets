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

#include "lobster/vmdata.h"
#include "lobster/natreg.h"

#include "lobster/disasm.h"

namespace lobster {

VM *g_vm = nullptr;                  // set during the lifetime of a VM object
SlabAlloc *vmpool = nullptr;         // set during the lifetime of a VM object

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

VM::VM(const char *_pn, string &_bytecode_buffer, const void *entry_point,
       const void *static_bytecode, const vector<string> &args)
      : stack(nullptr), stacksize(0), maxstacksize(DEFMAXSTACKSIZE), sp(-1),
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
        trace(false), trace_tail(false),
        vm_count_ins(0), vm_count_fcalls(0), vm_count_bcalls(0),
        compiled_code_ip(entry_point), program_args(args) {
    assert(vmpool == nullptr);
    vmpool = new SlabAlloc();
    bcf = bytecode::GetBytecodeFile(static_bytecode ? static_bytecode : bytecode_buffer.data());
    if (bcf->bytecode_version() != LOBSTER_BYTECODE_FORMAT_VERSION)
        throw string("bytecode is from a different version of Lobster");
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
    vml.LogInit(bcf);
    #ifdef VM_COMPILED_CODE_MODE
        #define F(N, A) f_ins_pointers[IL_##N] = nullptr;
    #else
        #define F(N, A) f_ins_pointers[IL_##N] = &VM::F_##N;
    #endif
        ILNAMES
    #undef F
    assert(g_vm == nullptr);
    g_vm = this;
}

VM::~VM() {
    assert(g_vm == this);
    g_vm = nullptr;
    if (stack) delete[] stack;
    if (vars)  delete[] vars;
    if (byteprofilecounts) delete[] byteprofilecounts;
    if (vmpool) {
        delete vmpool;
        vmpool = nullptr;
    }
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
    vmpool->findleaks([&](void *p) { leaks.push_back(p); });
    if (!leaks.empty()) {
        Output(OUTPUT_ERROR, "LEAKS FOUND (this indicates cycles in your object graph, or a bug in"
                             " Lobster, details in leaks.txt)");
        FILE *leakf = OpenForWriting("leaks.txt", false);
        if (leakf) {
            //qsort(&leaks[0], leaks.size(), sizeof(void *), &LeakSorter);
            sort(leaks.begin(), leaks.end(), _LeakSorter);
            PrintPrefs leakpp = debugpp;
            leakpp.cycles = 0;
            for (auto p : leaks) {
                auto ro = (RefObj *)p;
                switch(ro->ti().t) {
                    case V_VALUEBUF:
                    case V_STACKFRAMEBUF:
                        break;
                    case V_STRING:
                    case V_COROUTINE:
                    case V_BOXEDINT:
                    case V_BOXEDFLOAT:
                    case V_VECTOR:
                    case V_STRUCT: {
                        auto s = RefToString(ro, leakpp);
                        fputs((ro->CycleStr() + " = " + s + "\n").c_str(), leakf);
                        break;
                    }
                    default: assert(false);
                }
            }
            fclose(leakf);
        }
    }
    vmpool->printstats(false);
}

#undef new
LVector *VM::NewVec(intp initial, intp max, type_elem_t tti) {
    assert(GetTypeInfo(tti).t == V_VECTOR);
    return new (vmpool->alloc_small(sizeof(LVector))) LVector(initial, max, tti);
}
LStruct *VM::NewStruct(intp max, type_elem_t tti) {
    assert(GetTypeInfo(tti).t == V_STRUCT);
    return new (vmpool->alloc(sizeof(LStruct) + sizeof(Value) * max)) LStruct(tti);
}
LString *VM::NewString(size_t l) {
    return new (vmpool->alloc(sizeof(LString) + l + 1)) LString((int)l);
}
LCoRoutine *VM::NewCoRoutine(InsPtr rip, const int *vip, LCoRoutine *p, type_elem_t cti) {
    assert(GetTypeInfo(cti).t == V_COROUTINE);
    return new (vmpool->alloc(sizeof(LCoRoutine)))
               LCoRoutine(sp + 2 /* top of sp + pushed coro */, (int)stackframes.size(), rip, vip, p,
                         cti);
}
BoxedInt *VM::NewInt(intp i) {
    return new (vmpool->alloc(sizeof(BoxedInt))) BoxedInt(i);
}
BoxedFloat *VM::NewFloat(floatp f) {
    return new (vmpool->alloc(sizeof(BoxedFloat))) BoxedFloat(f);
}
LResource *VM::NewResource(void *v, const ResourceType *t) {
    return new (vmpool->alloc(sizeof(LResource))) LResource(v, t);
}
#ifdef _WIN32
#ifdef _DEBUG
#define new DEBUG_NEW
#endif
#endif

LString *VM::NewString(const char *c, size_t l) {
    auto s = NewString(l);
    memcpy(s->str(), c, l);
    s->str()[l] = 0;
    return s;
}

LString *VM::NewString(const string &s) {
    return NewString(s.c_str(), s.size());
}

LString *VM::NewString(const char *c1, size_t l1, const char *c2, size_t l2) {
    auto s = NewString(l1 + l2);
    memcpy(s->str(),      c1, l1);
    memcpy(s->str() + l1, c2, l2);
    s->str()[l1 + l2] = 0;
    return s;
}

// This function is now way less important than it was when the language was still dynamically
// typed. But ok to leave it as-is for "index out of range" and other errors that are still dynamic.
Value VM::Error(string err, const RefObj *a, const RefObj *b) {
    if (trace_tail && trace_output.length()) throw trace_output + err;
    string s;
    #ifndef VM_COMPILED_CODE_MODE
        // error is usually in the byte before the current ip.
        auto li = LookupLine(ip - 1, codestart, bcf);
        s += string(bcf->filenames()->Get(li->fileidx())->c_str()) + "(" + to_string(li->line()) +
             "): ";
    #endif
    s += "VM error: " + err;
    if (a) s += "\n   arg: " + ValueDBG(a);
    if (b) s += "\n   arg: " + ValueDBG(b);
    while (sp >= 0 && (!stackframes.size() || sp != stackframes.back().spstart)) {
        // Sadly can't print this properly.
        s += "\n   stack: " + to_string_hex((size_t)TOP().any());
        if (vmpool->pointer_is_in_allocator(TOP().any()))
            s += ", maybe: " + RefToString(TOP().ref(), debugpp);
        POP();  // We don't DEC here, as we can't know what type it is.
                // This is ok, as we ignore leaks in case of an error anyway.
    }
    for (;;) {
        if (!stackframes.size()) break;
        string locals;
        int deffun = stackframes.back().definedfunction;
        VarCleanup(s.length() < 10000 ? &locals : nullptr, -2 /* clean up temps always */);
        if (deffun >= 0) {
            s += string("\nin function: ") + bcf->functions()->Get(deffun)->name()->c_str();
        } else {
            s += "\nin block";
        }
        #ifndef VM_COMPILED_CODE_MODE
        auto li = LookupLine(ip - 1, codestart, bcf);
        s += string(" -> ") + bcf->filenames()->Get(li->fileidx())->c_str() + "(" +
             to_string(li->line()) + ")";
        #endif
        s += locals;
    }
    s += "\nglobals:";
    for (size_t i = 0; i < bcf->specidents()->size(); i++) {
        s += DumpVar(vars[i], i, true);
    }
    throw s;
}

void VM::VMAssert(bool ok, const char *what)  {
    if (!ok)
        Error(string("VM internal assertion failure: ") + what);
}
void VM::VMAssert(bool ok, const char *what, const RefObj *a, const RefObj *b)  {
    if (!ok)
        Error(string("VM internal assertion failure: ") + what, a, b);
}

#if defined(_DEBUG) && RTT_ENABLED
    #define STRINGIFY(x) #x
    #define TOSTRING(x) STRINGIFY(x)
    #define VMASSERT(test) { VMAssert(test, __FILE__ ": " TOSTRING(__LINE__) ": " #test); }
#else
    #define VMASSERT(test) {}
#endif
#if RTT_ENABLED
    #define VMTYPEEQ(val, vt) VMASSERT((val).type == (vt))
#else
    #define VMTYPEEQ(val, vt) { (void)(val); (void)(vt); }
#endif

string VM::ValueDBG(const RefObj *a) {
    return RefToString(a, debugpp);
}

string VM::DumpVar(const Value &x, size_t idx, bool dumpglobals) {
    auto sid = bcf->specidents()->Get((uint)idx);
    auto id = bcf->idents()->Get(sid->ididx());
    if (id->readonly() || id->global() != dumpglobals) return "";
    string name = id->name()->c_str();
    auto static_type = GetVarTypeInfo((int)idx).t;
    #if RTT_ENABLED
        if (static_type != x.type) return "";  // Likely uninitialized.
    #endif
    return "\n   " + name + " = " + x.ToString(static_type, debugpp);
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
                InsPtr retip(call_arg_types);
                InsPtr fun(codestart + *mip);
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
        argtypes += ProperTypeName(IsRef(ti.t) && v.ref() ? v.ref()->ti() : ti);
        if (j < nargs - 1) argtypes += ", ";
    }
    Error(string("the call ") + bcf->functions()->Get(definedfunction)->name()->c_str() + "(" +
          argtypes + ") did not match any function variants");
}

void VM::FinalStackVarsCleanup() {
    VMASSERT(sp < 0 && !stackframes.size());
    for (size_t i = 0; i < bcf->specidents()->size(); i++) {
        auto sid = bcf->specidents()->Get((uint)i);
        //Output(OUTPUT_INFO, "destructing: %s", bcf->idents()->Get(sid->ididx())->name()->c_str());
        vars[i].DECTYPE(GetTypeInfo((type_elem_t)sid->typeidx()).t);
    }
    #ifdef _DEBUG
        Output(OUTPUT_INFO, "stack at its highest was: %d", maxsp);
    #endif
}

void VM::JumpTo(InsPtr j) {
    #ifdef VM_COMPILED_CODE_MODE
        next_call_target = j.f;
    #else
        ip = j.f;
    #endif
}

InsPtr VM::GetIP() {
    #ifdef VM_COMPILED_CODE_MODE
        return InsPtr(next_call_target);
    #else
        return InsPtr(ip);
    #endif
}

int VM::VarCleanup(string *error, int towhere) {
    auto &stf = stackframes.back();
    assert(sp == stf.spstart);
    auto fip = stf.funstart;
    auto nargs = *fip++;
    auto freevars = fip + nargs;
    fip += nargs;
    auto ndef = *fip++;
    auto defvars = fip + ndef;
    while (ndef--) {
        auto i = *--defvars;
        if (error) (*error) += DumpVar(vars[i], i, false);
        else vars[i].DECTYPE(GetVarTypeInfo(i).t);
        vars[i] = POP();
    }
    while (nargs--) {
        auto i = *--freevars;
        if (error) (*error) += DumpVar(vars[i], i, false);
        else vars[i].DECTYPE(GetVarTypeInfo(i).t);
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
                    stack[untilsp + 1 + i].DECRTNIL();
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

        Output(OUTPUT_DEBUG, "stack grew to: %d", stacksize);
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
                Error(string("\"return from ") + bcf->functions()->Get(towhere)->name()->c_str() +
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
                    stack[stf.spstart + 1 + i].DECRTNIL();
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
        InsPtr returnip(codestart + *ip++);
    #endif
    auto ctidx = (type_elem_t)*ip++;
    CoNonRec(ip);
    curcoroutine = NewCoRoutine(returnip, ip, curcoroutine, ctidx);
    curcoroutine->BackupParentVars(vars);
    int nvars = *ip++;
    ip += nvars;
    // Always have the active coroutine at top of the stack, retaining 1 refcount. This is
    // because it is not guaranteed that there any other references, and we can't have this drop
    // to 0 while active.
    PUSH(Value(curcoroutine));
}

void VM::CoSuspend(InsPtr retip) {
    int newtop = curcoroutine->Suspend(sp + 1, stack, stackframes, retip, curcoroutine);
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
        InsPtr retip(ip);
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
    POP().DECTYPE(GetTypeInfo(curcoroutine->ti().yieldtype).t);    // previous current value
    for (int i = *curcoroutine->varip; i > 0; i--) {
        auto &var = vars[curcoroutine->varip[i]];
        // No INC, since parent is still on the stack and hold ref for us.
        curcoroutine->stackcopy[i - 1] = var;
        var = POP();
    }
    // the builtin call takes care of the return value
}

void VM::EndEval(Value &ret, ValueType vt) {
    evalret = ret.ToString(vt, programprintprefs);
    ret.DECTYPE(vt);
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
            Output(OUTPUT_INFO, "%s(%d%s): %.1f %%", bcf->filenames()->Get(u.fileidx)->c_str(),
                   u.line, u.lastline != u.line ? ("-" + to_string(u.lastline)).c_str() : "",
                   u.count * 100.0f / total);
        }
        if (vm_count_fcalls)  // remove trivial VM executions from output
            Output(OUTPUT_INFO, "ins %lld, fcall %lld, bcall %lld", vm_count_ins, vm_count_fcalls,
                   vm_count_bcalls);
    #endif
    throw string("end-eval");
}

void VM::F_PUSHINT(VM_OP_ARGS) { PUSH(Value(*ip++)); }
void VM::F_PUSHFLT(VM_OP_ARGS) { PUSH(Value(*(float *)ip)); ip++; }
void VM::F_PUSHNIL(VM_OP_ARGS) { PUSH(Value()); }

void VM::F_PUSHINT64(VM_OP_ARGS) {
    #if !VALUE_MODEL_64
        Error("Code containing 64-bit constants cannot run on a 32-bit build.");
    #endif
    int64_t v = (uint)*ip++;
    v |= ((int64_t)*ip++) << 32;
    PUSH(Value(v));
}


void VM::F_PUSHFUN(VM_OP_ARGS_CALL) {
    #ifdef VM_COMPILED_CODE_MODE
        ip++;
    #else
        int start = *ip++;
        auto fcont = codestart + start;
    #endif
    PUSH(Value(InsPtr(fcont)));
}

void VM::F_PUSHSTR(VM_OP_ARGS) {
    // FIXME: have a way that constant strings can stay in the bytecode,
    // or at least preallocate them all
    auto fb_s = bcf->stringtable()->Get(*ip++);
    auto s = NewString(fb_s->c_str(), fb_s->Length());
    PUSH(Value(s));
}

void VM::F_CALL(VM_OP_ARGS_CALL) {
    auto fvar = *ip++;
    #ifdef VM_COMPILED_CODE_MODE
        ip++;
        auto tm = *ip++;
        block_t fun = 0;  // Dynamic calls need this set, but for CALL it is ignored.
    #else
        auto fun = codestart + *ip++;
        auto tm = *ip++;
        auto fcont = ip;
    #endif
    StartStackFrame(fvar, InsPtr(fcont), tm);
    FunIntroPre(InsPtr(fun));
}

void VM::F_CALLMULTI(VM_OP_ARGS_CALL) {
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
}

void VM::F_FUNMULTI(VM_OP_ARGS) {
    #ifdef VM_COMPILED_CODE_MODE
        auto cip = next_mm_call;
        auto fvar = *cip++;
        cip++;
        auto tm = *cip++;
        EvalMulti(ip, fvar, cip, next_call_target, tm);
    #else
        VMASSERT(false);
    #endif
}

void VM::F_CALLVCOND(VM_OP_ARGS_CALL) {
    // FIXME: don't need to check for function value again below if false
    if (!TOP().True()) {
        ip++;
        #ifdef VM_COMPILED_CODE_MODE
            next_call_target = 0;
        #endif
    } else {
        F_CALLV(VM_OP_PASSTHRU);
    }
}

void VM::F_CALLV(VM_OP_ARGS_CALL) {
    Value fun = POP();
    VMTYPEEQ(fun, V_FUNCTION);
    auto tm = *ip++;
    #ifndef VM_COMPILED_CODE_MODE
        auto fcont = ip;
    #endif
    StartStackFrame(-1, InsPtr(fcont), tm);
    FunIntroPre(fun.ip());
}

void VM::F_FUNSTART(VM_OP_ARGS) {
    #ifdef VM_COMPILED_CODE_MODE
        FunIntro(ip);
    #else
        VMASSERT(false);
    #endif
}

void VM::F_FUNEND(VM_OP_ARGS) {
    int nrv = *ip++;
    FunOut(-1, nrv);
}

void VM::F_RETURN(VM_OP_ARGS) {
    int df = *ip++;
    int nrv = *ip++;
    int tidx = *ip++;
    if(FunOut(df, nrv)) {
        assert(nrv == 1);
        EndEval(POP(), GetTypeInfo((type_elem_t)tidx).t);
    }
}

void VM::F_EXIT(VM_OP_ARGS) {
    int tidx = *ip++;
    EndEval(POP(), GetTypeInfo((type_elem_t)tidx).t);
}

void VM::F_CONT1(VM_OP_ARGS) {
    auto nf = natreg.nfuns[*ip++];
    nf->cont1();
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
    TYPE_ASSERT(i.type == V_INT); \
    i.setival(i.ival() + 1); \
    intp len = 0; \
    if (i.ival() < (len = (L))) { \
        FOR_CONTINUE; \
    } else { \
        if (iterref) TOP().DECRT(); \
        (void)POP(); /* iter */ \
        (void)POP(); /* i */ \
        FOR_FINISHED; \
    } \
}
#define FORELEM(V) \
    auto &iter = TOP(); (void)iter; \
    auto &i = TOPM(1); \
    TYPE_ASSERT(i.type == V_INT); \
    PUSH(V);

VM_JUMP_RET VM::F_IFOR() { FORLOOP(iter.ival(), false); }
VM_JUMP_RET VM::F_VFOR() { FORLOOP(iter.vval()->len, true); }
VM_JUMP_RET VM::F_NFOR() { FORLOOP(iter.stval()->Len(), true); }
VM_JUMP_RET VM::F_SFOR() { FORLOOP(iter.sval()->len, true); }

void VM::F_IFORELEM(VM_OP_ARGS) { FORELEM(i); }
void VM::F_VFORELEM(VM_OP_ARGS) { FORELEM(iter.vval()->AtInc(i.ival())); }
void VM::F_NFORELEM(VM_OP_ARGS) { FORELEM(iter.stval()->At(i.ival())); }
void VM::F_SFORELEM(VM_OP_ARGS) { FORELEM(Value((int)((uchar *)iter.sval()->str())[i.ival()])); }

void VM::F_FORLOOPI(VM_OP_ARGS) {
    auto &i = TOPM(1);  // This relies on for being inlined, otherwise it would be 2.
    TYPE_ASSERT(i.type == V_INT);
    PUSH(i);
}

#define BCALLOPH(PRE,N,DECLS,ARGS,RETOP) void VM::F_BCALL##PRE##N(VM_OP_ARGS) { \
    BCallProf(); \
    auto nf = natreg.nfuns[*ip++]; \
    DECLS; \
    Value v = nf->fun.f##N ARGS; \
    RETOP; \
}

#define BCALLOP(N,DECLS,ARGS) \
    BCALLOPH(RET,N,DECLS,ARGS,PUSH(v);BCallRetCheck(nf)) \
    BCALLOPH(REF,N,DECLS,ARGS,v.DECRTNIL()) \
    BCALLOPH(UNB,N,DECLS,ARGS,(void)v)

BCALLOP(0, {}, ());
BCALLOP(1, auto a0 = POP(), (a0));
BCALLOP(2, auto a1 = POP();auto a0 = POP(), (a0, a1));
BCALLOP(3, auto a2 = POP();auto a1 = POP();auto a0 = POP(), (a0, a1, a2));
BCALLOP(4, auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (a0, a1, a2, a3));
BCALLOP(5, auto a4 = POP();auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (a0, a1, a2, a3, a4));
BCALLOP(6, auto a5 = POP();auto a4 = POP();auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (a0, a1, a2, a3, a4, a5));
BCALLOP(7, auto a6 = POP();auto a5 = POP();auto a4 = POP();auto a3 = POP();auto a2 = POP();auto a1 = POP();auto a0 = POP(), (a0, a1, a2, a3, a4, a5, a6));

void VM::F_NEWVEC(VM_OP_ARGS) {
    auto type = (type_elem_t)*ip++;
    auto len = *ip++;
    auto vec = NewVec(len, len, type);
    if (len) vec->Init(TOPPTR() - len, false);
    POPN(len);
    PUSH(Value(vec));
}

void VM::F_NEWSTRUCT(VM_OP_ARGS) {
    auto type = (type_elem_t)*ip++;
    auto len = GetTypeInfo(type).len;
    auto vec = NewStruct(len, type);
    if (len) vec->Init(TOPPTR() - len, len, false);
    POPN(len);
    PUSH(Value(vec));
}

void VM::F_POP(VM_OP_ARGS)    { POP(); }
void VM::F_POPREF(VM_OP_ARGS) { POP().DECRTNIL(); }

void VM::F_DUP(VM_OP_ARGS)    { auto x = TOP();            PUSH(x); }
void VM::F_DUPREF(VM_OP_ARGS) { auto x = TOP().INCRTNIL(); PUSH(x); }

#define REFOP(exp) { res = exp; a.DECRTNIL(); b.DECRTNIL(); }
#define GETARGS() Value b = POP(); Value a = POP()
#define TYPEOP(op, extras, field, errstat) Value res; errstat; \
    if (extras & 1 && b.field == 0) Div0(); res = a.field op b.field;

#define _IOP(op, extras) \
    TYPEOP(op, extras, ival(), VMASSERT(a.type == V_INT && b.type == V_INT))
#define _FOP(op, extras) \
    TYPEOP(op, extras, fval(), VMASSERT(a.type == V_FLOAT && b.type == V_FLOAT))

#define _VELEM(a, i, isfloat, T) (isfloat ? (T)a.stval()->At(i).fval() : (T)a.stval()->At(i).ival())
#define _VOP(op, extras, T, isfloat, withscalar, comp) Value res; { \
    auto len = a.stval()->Len(); \
    assert(withscalar || b.stval()->Len() == len); \
    auto v = NewStruct(len, comp ? GetIntVectorType((int)a.stval()->Len()) : a.stval()->tti); \
    res = Value(v); \
    for (intp j = 0; j < len; j++) { \
        if (withscalar) VMTYPEEQ(b, isfloat ? V_FLOAT : V_INT) \
        else VMTYPEEQ(b.stval()->At(j), isfloat ? V_FLOAT : V_INT); \
        auto bv = withscalar ? (isfloat ? (T)b.fval() : (T)b.ival()) : _VELEM(b, j, isfloat, T); \
        if (extras&1 && bv == 0) Div0(); \
        VMTYPEEQ(a.stval()->At(j), isfloat ? V_FLOAT : V_INT); \
        v->At(j) = Value(_VELEM(a, j, isfloat, T) op bv); \
    } \
    a.DECRT(); \
    if (!withscalar) b.DECRT(); \
}
#define _IVOP(op, extras, withscalar, icomp) _VOP(op, extras, intp, false, withscalar, icomp)
#define _FVOP(op, extras, withscalar, fcomp) _VOP(op, extras, floatp, true, withscalar, fcomp)

#define _SOP(op) Value res; REFOP((*a.sval()) op (*b.sval()))
#define _SCAT() Value res; \
                REFOP(NewString(a.sval()->str(), a.sval()->len, b.sval()->str(), b.sval()->len))

#define ACOMPEN(op) { GETARGS(); Value res; REFOP(a.any() op b.any()); PUSH(res); }

#define IOP(op, extras)    { GETARGS(); _IOP(op, extras);                PUSH(res); }
#define FOP(op, extras)    { GETARGS(); _FOP(op, extras);                PUSH(res); }
#define IVVOP(op, extras)  { GETARGS(); _IVOP(op, extras, false, false); PUSH(res); }
#define IVVOPC(op, extras) { GETARGS(); _IVOP(op, extras, false, true);  PUSH(res); }
#define FVVOP(op, extras)  { GETARGS(); _FVOP(op, extras, false, false); PUSH(res); }
#define FVVOPC(op, extras) { GETARGS(); _FVOP(op, extras, false, true);  PUSH(res); }
#define IVSOP(op, extras)  { GETARGS(); _IVOP(op, extras, true, false);  PUSH(res); }
#define IVSOPC(op, extras) { GETARGS(); _IVOP(op, extras, true, true);   PUSH(res); }
#define FVSOP(op, extras)  { GETARGS(); _FVOP(op, extras, true, false);  PUSH(res); }
#define FVSOPC(op, extras) { GETARGS(); _FVOP(op, extras, true, true);   PUSH(res); }
#define SOP(op)            { GETARGS(); _SOP(op);                        PUSH(res); }
#define SCAT()             { GETARGS(); _SCAT();                         PUSH(res); }

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

void VM::F_IVVADD(VM_OP_ARGS) { IVVOP(+,  0);  }
void VM::F_IVVSUB(VM_OP_ARGS) { IVVOP(-,  0);  }
void VM::F_IVVMUL(VM_OP_ARGS) { IVVOP(*,  0);  }
void VM::F_IVVDIV(VM_OP_ARGS) { IVVOP(/,  1);  }
void VM::F_IVVMOD(VM_OP_ARGS) { VMASSERT(0);   }
void VM::F_IVVLT(VM_OP_ARGS)  { IVVOP(<,  0);  }
void VM::F_IVVGT(VM_OP_ARGS)  { IVVOP(>,  0);  }
void VM::F_IVVLE(VM_OP_ARGS)  { IVVOP(<=, 0);  }
void VM::F_IVVGE(VM_OP_ARGS)  { IVVOP(>=, 0);  }
void VM::F_FVVADD(VM_OP_ARGS) { FVVOP(+,  0);  }
void VM::F_FVVSUB(VM_OP_ARGS) { FVVOP(-,  0);  }
void VM::F_FVVMUL(VM_OP_ARGS) { FVVOP(*,  0);  }
void VM::F_FVVDIV(VM_OP_ARGS) { FVVOP(/,  1);  }
void VM::F_FVVMOD(VM_OP_ARGS) { VMASSERT(0);   }
void VM::F_FVVLT(VM_OP_ARGS)  { FVVOPC(<,  0); }
void VM::F_FVVGT(VM_OP_ARGS)  { FVVOPC(>,  0); }
void VM::F_FVVLE(VM_OP_ARGS)  { FVVOPC(<=, 0); }
void VM::F_FVVGE(VM_OP_ARGS)  { FVVOPC(>=, 0); }

void VM::F_IVSADD(VM_OP_ARGS) { IVSOP(+,  0);  }
void VM::F_IVSSUB(VM_OP_ARGS) { IVSOP(-,  0);  }
void VM::F_IVSMUL(VM_OP_ARGS) { IVSOP(*,  0);  }
void VM::F_IVSDIV(VM_OP_ARGS) { IVSOP(/,  1);  }
void VM::F_IVSMOD(VM_OP_ARGS) { VMASSERT(0);   }
void VM::F_IVSLT(VM_OP_ARGS)  { IVSOP(<,  0);  }
void VM::F_IVSGT(VM_OP_ARGS)  { IVSOP(>,  0);  }
void VM::F_IVSLE(VM_OP_ARGS)  { IVSOP(<=, 0);  }
void VM::F_IVSGE(VM_OP_ARGS)  { IVSOP(>=, 0);  }
void VM::F_FVSADD(VM_OP_ARGS) { FVSOP(+,  0);  }
void VM::F_FVSSUB(VM_OP_ARGS) { FVSOP(-,  0);  }
void VM::F_FVSMUL(VM_OP_ARGS) { FVSOP(*,  0);  }
void VM::F_FVSDIV(VM_OP_ARGS) { FVSOP(/,  1);  }
void VM::F_FVSMOD(VM_OP_ARGS) { VMASSERT(0);   }
void VM::F_FVSLT(VM_OP_ARGS)  { FVSOPC(<,  0); }
void VM::F_FVSGT(VM_OP_ARGS)  { FVSOPC(>,  0); }
void VM::F_FVSLE(VM_OP_ARGS)  { FVSOPC(<=, 0); }
void VM::F_FVSGE(VM_OP_ARGS)  { FVSOPC(>=, 0); }

void VM::F_AEQ(VM_OP_ARGS) { ACOMPEN(==); }
void VM::F_ANE(VM_OP_ARGS) { ACOMPEN(!=); }

void VM::F_IADD(VM_OP_ARGS) { IOP(+,  0); }
void VM::F_ISUB(VM_OP_ARGS) { IOP(-,  0); }
void VM::F_IMUL(VM_OP_ARGS) { IOP(*,  0); }
void VM::F_IDIV(VM_OP_ARGS) { IOP(/ , 1); }
void VM::F_IMOD(VM_OP_ARGS) { IOP(%,  1); }
void VM::F_ILT(VM_OP_ARGS)  { IOP(<,  0); }
void VM::F_IGT(VM_OP_ARGS)  { IOP(>,  0); }
void VM::F_ILE(VM_OP_ARGS)  { IOP(<=, 0); }
void VM::F_IGE(VM_OP_ARGS)  { IOP(>=, 0); }
void VM::F_IEQ(VM_OP_ARGS)  { IOP(==, 0); }
void VM::F_INE(VM_OP_ARGS)  { IOP(!=, 0); }

void VM::F_FADD(VM_OP_ARGS) { FOP(+,  0); }
void VM::F_FSUB(VM_OP_ARGS) { FOP(-,  0); }
void VM::F_FMUL(VM_OP_ARGS) { FOP(*,  0); }
void VM::F_FDIV(VM_OP_ARGS) { FOP(/,  1); }
void VM::F_FMOD(VM_OP_ARGS) { VMASSERT(0); }
void VM::F_FLT(VM_OP_ARGS)  { FOP(<,  0); }
void VM::F_FGT(VM_OP_ARGS)  { FOP(>,  0); }
void VM::F_FLE(VM_OP_ARGS)  { FOP(<=, 0); }
void VM::F_FGE(VM_OP_ARGS)  { FOP(>=, 0); }
void VM::F_FEQ(VM_OP_ARGS)  { FOP(==, 0); }
void VM::F_FNE(VM_OP_ARGS)  { FOP(!=, 0); }

void VM::F_SADD(VM_OP_ARGS) { SCAT();  }
void VM::F_SSUB(VM_OP_ARGS) { VMASSERT(0); }
void VM::F_SMUL(VM_OP_ARGS) { VMASSERT(0); }
void VM::F_SDIV(VM_OP_ARGS) { VMASSERT(0); }
void VM::F_SMOD(VM_OP_ARGS) { VMASSERT(0); }
void VM::F_SLT(VM_OP_ARGS)  { SOP(<);  }
void VM::F_SGT(VM_OP_ARGS)  { SOP(>);  }
void VM::F_SLE(VM_OP_ARGS)  { SOP(<=); }
void VM::F_SGE(VM_OP_ARGS)  { SOP(>=); }
void VM::F_SEQ(VM_OP_ARGS)  { SOP(==); }
void VM::F_SNE(VM_OP_ARGS)  { SOP(!=); }

void VM::F_IUMINUS(VM_OP_ARGS) { Value a = POP(); PUSH(Value(-a.ival())); }
void VM::F_FUMINUS(VM_OP_ARGS) { Value a = POP(); PUSH(Value(-a.fval())); }

#define VUMINUS(isfloat, type) { \
    Value a = POP(); \
    Value res; \
    auto len = a.stval()->Len(); \
    res = Value(NewStruct(len, a.stval()->tti)); \
    for (intp i = 0; i < len; i++) { \
        VMTYPEEQ(a.stval()->At(i), isfloat ? V_FLOAT : V_INT); \
        res.stval()->At(i) = Value(-_VELEM(a, i, isfloat, type)); \
    } \
    a.DECRT(); \
    PUSH(res); \
    }
void VM::F_IVUMINUS(VM_OP_ARGS) { VUMINUS(false, intp) }
void VM::F_FVUMINUS(VM_OP_ARGS) { VUMINUS(true, floatp) }

void VM::F_LOGNOT(VM_OP_ARGS) {
    Value a = POP();
    PUSH(!a.True());
}
void VM::F_LOGNOTREF(VM_OP_ARGS) {
    Value a = POP();
    bool b = a.True();
    PUSH(!b);
    if (b) a.DECRT();
}

#define BITOP(op) { GETARGS(); PUSH(a.ival() op b.ival()); }
void VM::F_BINAND(VM_OP_ARGS) { BITOP(&);  }
void VM::F_BINOR(VM_OP_ARGS)  { BITOP(|);  }
void VM::F_XOR(VM_OP_ARGS)    { BITOP(^);  }
void VM::F_ASL(VM_OP_ARGS)    { BITOP(<<); }
void VM::F_ASR(VM_OP_ARGS)    { BITOP(>>); }
void VM::F_NEG(VM_OP_ARGS)    { auto a = POP(); PUSH(~a.ival()); }

void VM::F_I2F(VM_OP_ARGS) {
    Value a = POP();
    VMTYPEEQ(a, V_INT);
    PUSH((float)a.ival());
}

void VM::F_A2S(VM_OP_ARGS) {
    Value a = POP();
    TYPE_ASSERT(IsRefNil(a.type));
    PUSH(NewString(a.ToString(a.ref() ? a.ref()->ti().t : V_NIL, programprintprefs)));
    a.DECRTNIL();
}

void VM::F_I2A(VM_OP_ARGS) {
    Value i = POP();
    VMTYPEEQ(i, V_INT);
    PUSH(NewInt(i.ival()));
}

void VM::F_F2A(VM_OP_ARGS) {
    Value f = POP();
    VMTYPEEQ(f, V_FLOAT);
    PUSH(NewFloat(f.fval()));
}

void VM::F_E2B(VM_OP_ARGS) {
    Value a = POP();
    PUSH(a.True());
}

void VM::F_E2BREF(VM_OP_ARGS) {
    Value a = POP();
    PUSH(a.True());
    a.DECRTNIL();
}

void VM::F_PUSHVAR(VM_OP_ARGS)    { PUSH(vars[*ip++]); }
void VM::F_PUSHVARREF(VM_OP_ARGS) { PUSH(vars[*ip++].INCRTNIL()); }

void VM::F_PUSHFLD(VM_OP_ARGS)  { PushDerefField(*ip++); }
void VM::F_PUSHFLDM(VM_OP_ARGS) { PushDerefField(*ip++); }

void VM::F_VPUSHIDXI(VM_OP_ARGS) { PushDerefIdxVector(POP().ival()); }
void VM::F_VPUSHIDXV(VM_OP_ARGS) { PushDerefIdxVector(GrabIndex(POP())); }
void VM::F_NPUSHIDXI(VM_OP_ARGS) { PushDerefIdxStruct(POP().ival()); }
void VM::F_SPUSHIDXI(VM_OP_ARGS) { PushDerefIdxString(POP().ival()); }

void VM::F_PUSHLOC(VM_OP_ARGS) {
    int i = *ip++;
    Value coro = POP();
    VMTYPEEQ(coro, V_COROUTINE);
    PUSH(coro.cval()->GetVar(i));
    TOP().INCTYPE(GetVarTypeInfo(i).t);
    coro.DECRT();
}

void VM::F_LVALLOC(VM_OP_ARGS) {
    int lvalop = *ip++;
    int i = *ip++;
    Value coro = POP();
    VMTYPEEQ(coro, V_COROUTINE);
    Value &a = coro.cval()->GetVar(i);
    LvalueOp(lvalop, a);
    coro.DECRT();
}

void VM::F_LVALVAR(VM_OP_ARGS)    {
    int lvalop = *ip++;
    LvalueOp(lvalop, vars[*ip++]);
}

void VM::F_VLVALIDXI(VM_OP_ARGS) { int lvalop = *ip++; LvalueIdxVector(lvalop, POP().ival()); }
void VM::F_NLVALIDXI(VM_OP_ARGS) { int lvalop = *ip++; LvalueIdxStruct(lvalop, POP().ival()); }
void VM::F_LVALIDXV(VM_OP_ARGS)  { int lvalop = *ip++; LvalueIdxVector(lvalop, GrabIndex(POP())); }
void VM::F_LVALFLD(VM_OP_ARGS)   { int lvalop = *ip++; LvalueField(lvalop, *ip++); }

#ifdef VM_COMPILED_CODE_MODE
    #define GJUMP(N, V, D1, C, P, D2) VM_JUMP_RET VM::N() \
        { V; D1; if (C) { P; return true; } else { D2; return false; } }
#else
    #define GJUMP(N, V, D1, C, P, D2) VM_JUMP_RET VM::N() \
        { V; auto nip = *ip++; D1; if (C) { ip = codestart + nip; P; } else { D2; } }
#endif

GJUMP(F_JUMP          ,               ,             , true     ,              ,             )
GJUMP(F_JUMPFAIL      , auto x = POP(),             , !x.True(),              ,             )
GJUMP(F_JUMPFAILR     , auto x = POP(),             , !x.True(), PUSH(x)      ,             )
GJUMP(F_JUMPFAILN     , auto x = POP(),             , !x.True(), PUSH(Value()),             )
GJUMP(F_JUMPNOFAIL    , auto x = POP(),             ,  x.True(),              ,             )
GJUMP(F_JUMPNOFAILR   , auto x = POP(),             ,  x.True(), PUSH(x)      ,             )
GJUMP(F_JUMPFAILREF   , auto x = POP(), x.DECRTNIL(), !x.True(),              ,             )
GJUMP(F_JUMPFAILRREF  , auto x = POP(),             , !x.True(), PUSH(x)      , x.DECRTNIL())
GJUMP(F_JUMPFAILNREF  , auto x = POP(), x.DECRTNIL(), !x.True(), PUSH(Value()),             )
GJUMP(F_JUMPNOFAILREF , auto x = POP(), x.DECRTNIL(),  x.True(),              ,             )
GJUMP(F_JUMPNOFAILRREF, auto x = POP(),             ,  x.True(), PUSH(x)      , x.DECRTNIL())

void VM::F_ISTYPE(VM_OP_ARGS) {
    auto to = (type_elem_t)*ip++;
    auto v = POP();
    // Optimizer guarantees we don't have to deal with scalars.
    if (v.refnil()) PUSH(v.ref()->tti == to);
    else PUSH(GetTypeInfo(to).t == V_NIL);  // FIXME: can replace by fixed type_elem_t ?
    v.DECRTNIL();
}

void VM::F_YIELD(VM_OP_ARGS_CALL) { CoYield(VM_OP_PASSTHRU); }

// This value never gets used anywhere, just a placeholder.
void VM::F_COCL(VM_OP_ARGS) { PUSH(Value(0, V_YIELD)); }

void VM::F_CORO(VM_OP_ARGS_CALL) { CoNew(VM_OP_PASSTHRU); }

void VM::F_COEND(VM_OP_ARGS) { CoClean(); }

void VM::F_LOGREAD(VM_OP_ARGS) {
    auto val = POP();
    PUSH(vml.LogGet(val, *ip++));
}

void VM::F_LOGWRITE(VM_OP_ARGS) {
    auto vidx = *ip++;
    auto lidx = *ip++;
    vml.LogWrite(vars[vidx], lidx);
}

void VM::EvalProgram() {
    try {
        for (;;) {
            #ifdef VM_COMPILED_CODE_MODE
                #if VM_DISPATCH_METHOD == VM_DISPATCH_TRAMPOLINE
                    compiled_code_ip = ((block_t)compiled_code_ip)();
                #elif VM_DISPATCH_METHOD == VM_DISPATCH_SWITCH_GOTO
                    ((block_base_t)compiled_code_ip)();
                    assert(false);  // Should not return here.
                #endif
            #else
                #ifdef _DEBUG
                    if (trace) {
                        if (!trace_tail) trace_output.clear();
                        DisAsmIns(trace_output, ip, codestart, typetable, bcf);
                        trace_output += " [";
                        trace_output += to_string(sp + 1);
                        trace_output += "] - ";
                        #if RTT_ENABLED
                        if (sp >= 0) {
                            auto x = TOP();
                            trace_output += x.ToString(x.type, debugpp);
                        }
                        if (sp >= 1) {
                            auto x = TOPM(1);
                            trace_output += " ";
                            trace_output += x.ToString(x.type, debugpp);
                        }
                        #endif
                        if (trace_tail) {
                            trace_output += "\n";
                            const int trace_max = 10000;
                            if (trace_output.length() > trace_max)
                                trace_output.erase(0, trace_max / 2);
                        } else {
                            Output(OUTPUT_INFO, "%s", trace_output.c_str());
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
                        Error("bytecode format problem: " + to_string(op));
                #endif
                ((*this).*(f_ins_pointers[op]))();
            #endif
        }
    }
    catch (string &s) {
        if (s != "end-eval") throw s;
    }
}

void VM::PushDerefField(int i) {
    Value r = POP();
    if (!r.ref()) { PUSH(r); return; }  // nil.
    PUSH(r.stval()->AtInc(i));
    r.DECRT();
}

void VM::PushDerefIdxVector(intp i) {
    Value r = POP();
    if (!r.ref()) { PUSH(r); return; }  // nil.
    IDXErr(i, r.vval()->len, r.vval());
    PUSH(r.vval()->AtInc(i));
    r.DECRT();
}

void VM::PushDerefIdxStruct(intp i) {
    Value r = POP();
    if (!r.ref()) { PUSH(r); return; }  // nil.
    IDXErr(i, r.stval()->Len(), r.stval());
    PUSH(r.stval()->AtInc(i));
    r.DECRT();
}

void VM::PushDerefIdxString(intp i) {
    Value r = POP();
    if (!r.ref()) { PUSH(r); return; }  // nil.
    IDXErr(i, r.sval()->len, r.sval());
    PUSH(Value((int)((uchar *)r.sval()->str())[i]));
    r.DECRT();
}

void VM::LvalueIdxVector(int lvalop, intp i) {
    Value vec = POP();
    IDXErr(i, (int)vec.vval()->len, vec.vval());
    Value &a = vec.vval()->At(i);
    LvalueOp(lvalop, a);
    vec.DECRT();
}

void VM::LvalueIdxStruct(int lvalop, intp i) {
    Value vec = POP();
    IDXErr(i, (int)vec.stval()->Len(), vec.stval());
    Value &a = vec.stval()->At(i);
    LvalueOp(lvalop, a);
    vec.DECRT();
}

void VM::LvalueField(int lvalop, intp i) {
    Value vec = POP();
    IDXErr(i, (int)vec.stval()->Len(), vec.stval());
    Value &a = vec.stval()->At(i);
    LvalueOp(lvalop, a);
    vec.DECRT();
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
        case LVO_WRITEREF:  { Value  b = POP();            a.DECRTNIL(); a = b; break; }
        case LVO_WRITERREF: { Value &b = TOP().INCRTNIL(); a.DECRTNIL(); a = b; break; }

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
            Error("bytecode format problem (lvalue): " + to_string(op));
    }
}

string VM::ProperTypeName(const TypeInfo &ti) {
    switch (ti.t) {
        case V_STRUCT: return ReverseLookupType(ti.structidx);
        case V_NIL: return ProperTypeName(GetTypeInfo(ti.subt)) + "?";
        case V_VECTOR: return "[" + ProperTypeName(GetTypeInfo(ti.subt)) + "]";
    }
    return BaseTypeName(ti.t);
}

void VM::IDXErr(intp i, intp n, const RefObj *v) {
    if (i < 0 || i >= n) Error("index " + to_string(i) + " out of range " + to_string(n), v);
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
                TYPE_ASSERT(t == u || u == V_ANY || u == V_NIL || (u == V_VECTOR && t == V_STRUCT));
            }
            TYPE_ASSERT(nf->retvals.v.size() || TOP().type == V_NIL);
        }
    #else
        (void)nf;
    #endif
}

intp VM::GrabIndex(const Value &idx) {
    auto &v = TOP();
    for (auto i = idx.stval()->Len() - 1; ; i--) {
        auto sidx = idx.stval()->At(i);
        VMTYPEEQ(sidx, V_INT);
        if (!i) {
            idx.DECRT();
            return sidx.ival();
        }
        IDXErr(sidx.ival(), v.vval()->len, v.vval());
        auto nv = v.vval()->At(sidx.ival()).INCRT();
        v.DECRT();
        v = nv;
    }
}

void VM::Push(const Value &v) { PUSH(v); }

Value VM::Pop() { return POP(); }

string VM::StructName(const TypeInfo &ti) {
    return bcf->structs()->Get(ti.structidx)->name()->c_str();
}

const char *VM::ReverseLookupType(uint v) {
    return bcf->structs()->Get(v)->name()->c_str();
}

int VM::GC() {  // shouldn't really be used, but just in case
    for (int i = 0; i <= sp; i++) {
        //stack[i].Mark(?);
        // TODO: we could actually walk the stack here and recover correct types, but it is so easy
        // to avoid this error that that may not be worth it.
        if (stack[i].True())  // Typically all nil
            Error("collect_garbage() must be called from a top level function");
    }
    for (uint i = 0; i < bcf->specidents()->size(); i++) vars[i].Mark(GetVarTypeInfo(i).t);
    vml.LogMark();
    vector<RefObj *> leaks;
    int total = 0;
    vmpool->findleaks([&](void *p) {
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
        ro->DECDELETE(false);
    }
    return (int)leaks.size();
}

void RunBytecode(const char *programname, string &bytecode, const void *entry_point,
                 const void *static_bytecode, const vector<string> &program_args) {
    // Sets up g_vm
    new VM(programname, bytecode, entry_point, static_bytecode, program_args);
    g_vm->EvalProgram();
}

}  // namespace lobster
