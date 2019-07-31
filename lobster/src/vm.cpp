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

#ifndef NDEBUG
    #define VM_PROFILER              // tiny VM slowdown and memory usage when enabled
#endif

#ifdef VM_COMPILED_CODE_MODE
    #ifdef _WIN32
        #pragma warning (disable: 4458)  // ip hides class member, which we rely on
        #pragma warning (disable: 4100)  // ip may not be touched
    #endif
#endif

enum {
    // *8 bytes each
    INITSTACKSIZE   =   4 * 1024,
    // *8 bytes each, modest on smallest handheld we support (iPhone 3GS has 256MB).
    DEFMAXSTACKSIZE = 128 * 1024,
    // *8 bytes each, max by which the stack could possibly grow in a single call.
    STACKMARGIN     =   1 * 1024
};

#define MEASURE_INSTRUCTION_COMBINATIONS 0
#if MEASURE_INSTRUCTION_COMBINATIONS
map<pair<int, int>, size_t> instruction_combinations;
int last_instruction_opc = -1;
#endif

VM::VM(VMArgs &&vmargs) : VMArgs(std::move(vmargs)), maxstacksize(DEFMAXSTACKSIZE) {
    auto bcfb = (uchar *)(static_bytecode ? static_bytecode : bytecode_buffer.data());
    auto bcs = static_bytecode ? static_size : bytecode_buffer.size();
    flatbuffers::Verifier verifier(bcfb, bcs);
    auto ok = bytecode::VerifyBytecodeFileBuffer(verifier);
    if (!ok) THROW_OR_ABORT(string("bytecode file failed to verify"));
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
    #ifdef VM_COMPILED_CODE_MODE
        compiled_code_ip = entry_point;
    #else
        ip = codestart;
    #endif
    vars = new Value[bcf->specidents()->size()];
    stack = new Value[stacksize = INITSTACKSIZE];
    #ifdef VM_PROFILER
        byteprofilecounts = new uint64_t[codelen];
        memset(byteprofilecounts, 0, sizeof(uint64_t) * codelen);
    #endif
    vml.LogInit(bcfb);
    InstructionPointerInit();
    constant_strings.resize(bcf->stringtable()->size());
    #ifdef VM_COMPILED_CODE_MODE
        assert(native_vtables);
        for (size_t i = 0; i < bcf->vtables()->size(); i++) {
            vtables.push_back(InsPtr(native_vtables[i]));
        }
    #else
        assert(!native_vtables);
        for (auto bcs : *bcf->vtables()) {
            vtables.push_back(InsPtr(bcs));
        }
    #endif
}

VM::~VM() {
    TerminateWorkers();
    if (stack) delete[] stack;
    if (vars)  delete[] vars;
    if (byteprofilecounts) delete[] byteprofilecounts;
}

void VM::OneMoreFrame() {
    // We just landed back into the VM after being suspended inside a gl_frame() call.
    // Emulate the return of gl_frame():
    VM_PUSH(Value(1));  // We're not terminating yet.
    #ifdef VM_COMPILED_CODE_MODE
        // Native code generators ensure that next_call_target is set before
        // a native function call, and that it is returned to the trampoline
        // after, so do the same thing here.
        compiled_code_ip = (const void *)next_call_target;
    #endif
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

void VM::DumpVal(RefObj *ro, const char *prefix) {
    ostringstream ss;
    ss << prefix << ": ";
    RefToString(*this, ss, ro, debugpp);
    ss << " (" << ro->refc << "): " << (size_t)ro;
    LOG_DEBUG(ss.str());
}

void VM::DumpFileLine(const int *fip, ostringstream &ss) {
    // error is usually in the byte before the current ip.
    auto li = LookupLine(fip - 1, codestart, bcf);
    ss << bcf->filenames()->Get(li->fileidx())->string_view() << '(' << li->line() << ')';
}

void VM::DumpLeaks() {
    vector<void *> leaks = pool.findleaks();
    auto filename = "leaks.txt";
    if (leaks.empty()) {
        if (FileExists(filename)) FileDelete(filename);
    } else {
        LOG_ERROR("LEAKS FOUND (this indicates cycles in your object graph, or a bug in"
                             " Lobster)");
        ostringstream ss;
        #ifndef VM_COMPILED_CODE_MODE
            ss << "in: ";
            DumpFileLine(ip, ss);
            ss << "\n";
        #endif
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
                case V_RESOURCE:
                case V_VECTOR:
                case V_CLASS: {
                    ro->CycleStr(ss);
                    ss << " = ";
                    RefToString(*this, ss, ro, leakpp);
                    #if DELETE_DELAY
                    ss << " ";
                    DumpFileLine(ro->alloc_ip, ss);
                    ss << " " << (size_t)ro;
                    #endif
                    ss << "\n";
                    break;
                }
                default: assert(false);
            }
        }
        #ifndef NDEBUG
            LOG_ERROR(ss.str());
        #else
            if (leaks.size() < 50) {
                LOG_ERROR(ss.str());
            } else {
                LOG_ERROR(leaks.size(), " leaks, details in ", filename);
                WriteFile(filename, false, ss.str());
            }
        #endif
    }
    pool.printstats(false);
}

void VM::OnAlloc(RefObj *ro) {
    #if DELETE_DELAY
        LOG_DEBUG("alloc: ", (size_t)ro);
        ro->alloc_ip = ip;
    #else
        (void)ro;
    #endif
}

#undef new

LVector *VM::NewVec(intp initial, intp max, type_elem_t tti) {
    assert(GetTypeInfo(tti).t == V_VECTOR);
    auto v = new (pool.alloc_small(sizeof(LVector))) LVector(*this, initial, max, tti);
    OnAlloc(v);
    return v;
}

LObject *VM::NewObject(intp max, type_elem_t tti) {
    assert(IsUDT(GetTypeInfo(tti).t));
    auto s = new (pool.alloc(sizeof(LObject) + sizeof(Value) * max)) LObject(tti);
    OnAlloc(s);
    return s;
}
LString *VM::NewString(size_t l) {
    auto s = new (pool.alloc(sizeof(LString) + l + 1)) LString((int)l);
    OnAlloc(s);
    return s;\
}
LCoRoutine *VM::NewCoRoutine(InsPtr rip, const int *vip, LCoRoutine *p, type_elem_t cti) {
    assert(GetTypeInfo(cti).t == V_COROUTINE);
    auto c = new (pool.alloc(sizeof(LCoRoutine)))
       LCoRoutine(sp + 2 /* top of sp + pushed coro */, (int)stackframes.size(), rip, vip, p, cti);
    OnAlloc(c);
    return c;
}
LResource *VM::NewResource(void *v, const ResourceType *t) {
    auto r = new (pool.alloc(sizeof(LResource))) LResource(v, t);
    OnAlloc(r);
    return r;
}
#ifdef _WIN32
#ifndef NDEBUG
#define new DEBUG_NEW
#endif
#endif

LString *VM::NewString(string_view s) {
    auto r = NewString(s.size());
    auto dest = (char *)r->data();
    memcpy(dest, s.data(), s.size());
    #if DELETE_DELAY
        LOG_DEBUG("string: \"", s, "\" - ", (size_t)r);
    #endif
    return r;
}

LString *VM::NewString(string_view s1, string_view s2) {
    auto s = NewString(s1.size() + s2.size());
    auto dest = (char *)s->data();
    memcpy(dest, s1.data(), s1.size());
    memcpy(dest + s1.size(), s2.data(), s2.size());
    return s;
}

LString *VM::ResizeString(LString *s, intp size, int c, bool back) {
    auto ns = NewString(size);
    auto sdest = (char *)ns->data();
    auto cdest = sdest;
    auto remain = size - s->len;
    if (back) sdest += remain;
    else cdest += s->len;
    memcpy(sdest, s->data(), s->len);
    memset(cdest, c, remain);
    s->Dec(*this);
    return ns;
}

// This function is now way less important than it was when the language was still dynamically
// typed. But ok to leave it as-is for "index out of range" and other errors that are still dynamic.
Value VM::Error(string err, const RefObj *a, const RefObj *b) {
    if (trace == TraceMode::TAIL && trace_output.size()) {
        string s;
        for (size_t i = trace_ring_idx; i < trace_output.size(); i++) s += trace_output[i].str();
        for (size_t i = 0; i < trace_ring_idx; i++) s += trace_output[i].str();
        s += err;
        THROW_OR_ABORT(s);
    }
    ostringstream ss;
    #ifndef VM_COMPILED_CODE_MODE
        DumpFileLine(ip, ss);
        ss << ": ";
    #endif
    ss << "VM error: " << err;
    if (a) { ss << "\n   arg: "; RefToString(*this, ss, a, debugpp); }
    if (b) { ss << "\n   arg: "; RefToString(*this, ss, b, debugpp); }
    while (sp >= 0 && (!stackframes.size() || sp != stackframes.back().spstart)) {
        // Sadly can't print this properly.
        ss << "\n   stack: ";
        to_string_hex(ss, (size_t)VM_TOP().any());
        if (pool.pointer_is_in_allocator(VM_TOP().any())) {
            ss << ", maybe: ";
            RefToString(*this, ss, VM_TOP().ref(), debugpp);
        }
        VM_POP();  // We don't DEC here, as we can't know what type it is.
                // This is ok, as we ignore leaks in case of an error anyway.
    }
    for (;;) {
        if (!stackframes.size()) break;
        int deffun = *(stackframes.back().funstart);
        if (deffun >= 0) {
            ss << "\nin function: " << bcf->functions()->Get(deffun)->name()->string_view();
        } else {
            ss << "\nin block";
        }
        #ifndef VM_COMPILED_CODE_MODE
        ss << " -> ";
        DumpFileLine(ip, ss);
        #endif
        VarCleanup<1>(ss.tellp() < 10000 ? &ss : nullptr, -2 /* clean up temps always */);
    }
    ss << "\nglobals:";
    for (size_t i = 0; i < bcf->specidents()->size(); ) {
        i += DumpVar(ss, vars[i], i, true);
    }
    THROW_OR_ABORT(ss.str());
}

void VM::VMAssert(const char *what)  {
    Error(string("VM internal assertion failure: ") + what);
}
void VM::VMAssert(const char *what, const RefObj *a, const RefObj *b)  {
    Error(string("VM internal assertion failure: ") + what, a, b);
}

#if !defined(NDEBUG) && RTT_ENABLED
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

int VM::DumpVar(ostringstream &ss, const Value &x, size_t idx, bool dumpglobals) {
    auto sid = bcf->specidents()->Get((uint)idx);
    auto id = bcf->idents()->Get(sid->ididx());
    if (id->readonly() || id->global() != dumpglobals) return 1;
    auto name = id->name()->string_view();
    auto &ti = GetVarTypeInfo((int)idx);
    #if RTT_ENABLED
        if (ti.t != x.type) return 1;  // Likely uninitialized.
    #endif
    ss << "\n   " << name << " = ";
    if (IsStruct(ti.t)) {
        StructToString(ss, debugpp, ti, &x);
        return ti.len;
    } else {
        x.ToString(*this, ss, ti, debugpp);
        return 1;
    }
}

void VM::FinalStackVarsCleanup() {
    VMASSERT(sp < 0 && !stackframes.size());
    #ifndef NDEBUG
        LOG_INFO("stack at its highest was: ", maxsp);
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

template<int is_error> int VM::VarCleanup(ostringstream *error, int towhere) {
    (void)error;
    auto &stf = stackframes.back();
    if constexpr (!is_error) VMASSERT(sp == stf.spstart);
    auto fip = stf.funstart;
    fip++;  // function id.
    auto nargs = *fip++;
    auto freevars = fip + nargs;
    fip += nargs;
    auto ndef = *fip++;
    fip += ndef;
    auto defvars = fip;
    auto nkeepvars = *fip++;
    if constexpr (is_error) {
        // Do this first, since values may get deleted below.
        for (int j = 0; j < ndef; ) {
            auto i = *(defvars - j - 1);
            j += DumpVar(*error, vars[i], i, false);
        }
        for (int j = 0; j < nargs; ) {
            auto i = *(freevars - j - 1);
            j += DumpVar(*error, vars[i], i, false);
        }
    }
    for (int i = 0; i < nkeepvars; i++) VM_POP().LTDECRTNIL(*this);
    auto ownedvars = *fip++;
    for (int i = 0; i < ownedvars; i++) vars[*fip++].LTDECRTNIL(*this);
    while (ndef--) {
        auto i = *--defvars;
        vars[i] = VM_POP();
    }
    while (nargs--) {
        auto i = *--freevars;
        vars[i] = VM_POP();
    }
    JumpTo(stf.retip);
    bool lastunwind = towhere == *stf.funstart;
    stackframes.pop_back();
    if (!lastunwind) {
        // This kills any temps on the stack. If these are refs these should not be
        // owners, since a var or keepvar owns them instead.
        sp = stackframes.size() ? stackframes.back().spstart : -1;
    }
    return lastunwind;
}

// Initializes only 3 fields of the stack frame, FunIntro must be called right after.
void VM::StartStackFrame(InsPtr retip) {
    stackframes.push_back(StackFrame());
    auto &stf = stackframes.back();
    stf.retip = retip;
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
    ip++;  // definedfunction
    if (sp > stacksize - STACKMARGIN) {
        // per function call increment should be small
        // FIXME: not safe for untrusted scripts, could simply add lots of locals
        // could record max number of locals? not allow more than N locals?
        if (stacksize >= maxstacksize) Error("stack overflow! (use set_max_stack_size() if needed)");
        auto nstack = new Value[stacksize *= 2];
        t_memcpy(nstack, stack, sp + 1);
        delete[] stack;
        stack = nstack;

        LOG_DEBUG("stack grew to: ", stacksize);
    }
    auto nargs_fun = *ip++;
    for (int i = 0; i < nargs_fun; i++) swap(vars[ip[i]], stack[sp - nargs_fun + i + 1]);
    ip += nargs_fun;
    auto ndef = *ip++;
    for (int i = 0; i < ndef; i++) {
        // for most locals, this just saves an nil, only in recursive cases it has an actual value.
        auto varidx = *ip++;
        VM_PUSH(vars[varidx]);
        vars[varidx] = Value();
    }
    auto nkeepvars = *ip++;
    for (int i = 0; i < nkeepvars; i++) VM_PUSH(Value());
    auto nownedvars = *ip++;
    ip += nownedvars;
    auto &stf = stackframes.back();
    stf.funstart = funstart;
    stf.spstart = sp;
    #ifndef NDEBUG
        if (sp > maxsp) maxsp = sp;
    #endif
}

void VM::FunOut(int towhere, int nrv) {
    sp -= nrv;
    // Have to store these off the stack, since VarCleanup() may cause stack activity if coroutines
    // are destructed.
    ts_memcpy(retvalstemp, VM_TOPPTR(), nrv);
    for(;;) {
        if (!stackframes.size()) {
            Error("\"return from " + bcf->functions()->Get(towhere)->name()->string_view() +
                    "\" outside of function");
        }
        if (VarCleanup<0>(nullptr, towhere)) break;
    }
    ts_memcpy(VM_TOPPTR(), retvalstemp, nrv);
    sp += nrv;
}

void VM::CoVarCleanup(LCoRoutine *co) {
    // Convenient way to copy everything back onto the stack.
    InsPtr tip(0);
    auto copylen = co->Resume(sp + 1, stack, stackframes, tip, nullptr);
    auto startsp = sp;
    sp += copylen;
    for (int i = co->stackframecopylen - 1; i >= 0 ; i--) {
        auto &stf = stackframes.back();
        sp = stf.spstart;  // Kill any temps on top of the stack.
        // Save the ip, because VarCleanup will jump to it.
        auto bip = GetIP();
        VarCleanup<0>(nullptr, !i ? *stf.funstart : -2);
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
    // this check guarantees all saved stack vars are undef.
}

void VM::CoNew(VM_OP_ARGS VM_COMMA VM_OP_ARGS_CALL) {
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
    VM_PUSH(Value(curcoroutine));
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
    #ifdef VM_COMPILED_CODE_MODE
        InsPtr retip(fcont);
    #else
        InsPtr retip(ip - codestart);
    #endif
    auto ret = VM_POP();
    for (int i = 1; i <= *curcoroutine->varip; i++) {
        auto &var = vars[curcoroutine->varip[i]];
        VM_PUSH(var);
        //var.type = V_NIL;
        var = curcoroutine->stackcopy[i - 1];
    }
    VM_PUSH(ret);  // current value always top of the stack, saved as part of suspended coroutine.
    CoSuspend(retip);
    // Actual top of stack here is coroutine itself, that we placed here with CoResume.
}

void VM::CoResume(LCoRoutine *co) {
    if (co->stackstart >= 0)
        Error("cannot resume running coroutine");
    if (!co->active)
        Error("cannot resume finished coroutine");
    // This will be the return value for the corresponding yield, and holds the ref for gc.
    VM_PUSH(Value(co));
    CoNonRec(co->varip);
    auto rip = GetIP();
    sp += co->Resume(sp + 1, stack, stackframes, rip, curcoroutine);
    JumpTo(rip);
    curcoroutine = co;
    // must be, since those vars got backed up in it before
    VMASSERT(curcoroutine->stackcopymax >=  *curcoroutine->varip);
    curcoroutine->stackcopylen = *curcoroutine->varip;
    //curcoroutine->BackupParentVars(vars);
    VM_POP().LTDECTYPE(*this, GetTypeInfo(curcoroutine->ti(*this).yieldtype).t);    // previous current value
    for (int i = *curcoroutine->varip; i > 0; i--) {
        auto &var = vars[curcoroutine->varip[i]];
        // No INC, since parent is still on the stack and hold ref for us.
        curcoroutine->stackcopy[i - 1] = var;
        var = VM_POP();
    }
    // the builtin call takes care of the return value
}

void VM::EndEval(const Value &ret, const TypeInfo &ti) {
    TerminateWorkers();
    ostringstream ss;
    ret.ToString(*this, ss, ti, programprintprefs);
    evalret = ss.str();
    ret.LTDECTYPE(*this, ti.t);
    assert(sp == -1);
    FinalStackVarsCleanup();
    vml.LogCleanup();
    for (auto s : constant_strings) {
        if (s) s->Dec(*this);
    }
    while (!delete_delay.empty()) {
        auto ro = delete_delay.back();
        delete_delay.pop_back();
        ro->DECDELETENOW(*this);
    }
    DumpLeaks();
    VMASSERT(!curcoroutine);
    #ifdef VM_PROFILER
        LOG_INFO("Profiler statistics:");
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
            LOG_INFO(bcf->filenames()->Get(u.fileidx)->string_view(), "(", u.line,
                   u.lastline != u.line ? "-" + to_string(u.lastline) : "",
                   "): ", u.count * 100.0f / total, " %");
        }
        if (vm_count_fcalls)  // remove trivial VM executions from output
            LOG_INFO("ins ", vm_count_ins, ", fcall ", vm_count_fcalls, ", bcall ",
                                vm_count_bcalls, ", decref ", vm_count_decref);
    #endif
    #if MEASURE_INSTRUCTION_COMBINATIONS
        struct trip { size_t freq; int opc1, opc2; };
        vector<trip> combinations;
        for (auto &p : instruction_combinations)
            combinations.push_back({ p.second, p.first.first, p.first.second });
        sort(combinations.begin(), combinations.end(), [](const trip &a, const trip &b) {
            return a.freq > b.freq;
        });
        combinations.resize(50);
        for (auto &c : combinations) {
            LOG_PROGRAM("instruction ", ILNames()[c.opc1], " -> ", ILNames()[c.opc2], " (",
                        c.freq, "x)");
        }
        instruction_combinations.clear();
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

ostringstream &VM::TraceStream() {
  size_t trace_size = trace == TraceMode::TAIL ? 50 : 1;
  if (trace_output.size() < trace_size) trace_output.resize(trace_size);
  if (trace_ring_idx == trace_size) trace_ring_idx = 0;
  auto &ss = trace_output[trace_ring_idx++];
  ss.str(string());
  return ss;
}

void VM::EvalProgramInner() {
    for (;;) {
        #ifdef VM_COMPILED_CODE_MODE
            #if VM_DISPATCH_METHOD == VM_DISPATCH_TRAMPOLINE
                compiled_code_ip = ((block_t)compiled_code_ip)(*this);
            #elif VM_DISPATCH_METHOD == VM_DISPATCH_SWITCH_GOTO
                ((block_base_t)compiled_code_ip)(*this);
                assert(false);  // Should not return here.
            #endif
        #else
            #ifndef NDEBUG
                if (trace != TraceMode::OFF) {
                    auto &ss = TraceStream();
                    DisAsmIns(nfr, ss, ip, codestart, typetable, bcf);
                    ss << " [" << (sp + 1) << "] -";
                    #if RTT_ENABLED
                    #if DELETE_DELAY
                        ss << ' ' << (size_t)VM_TOP().any();
                    #endif
                    for (int i = 0; i < 3 && sp - i >= 0; i++) {
                        auto x = VM_TOPM(i);
                        ss << ' ';
                        x.ToStringBase(*this, ss, x.type, debugpp);
                    }
                    #endif
                    if (trace == TraceMode::TAIL) ss << '\n'; else LOG_PROGRAM(ss.str());
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
            #if MEASURE_INSTRUCTION_COMBINATIONS
                instruction_combinations[{ last_instruction_opc, op }]++;
                last_instruction_opc = op;
            #endif
            #ifndef NDEBUG
                if (op < 0 || op >= IL_MAX_OPS)
                    Error(cat("bytecode format problem: ", op));
            #endif
            #ifdef VM_ERROR_RET_EXPERIMENT
                bool terminate =
            #endif
            ((*this).*(f_ins_pointers[op]))();
            #ifdef VM_ERROR_RET_EXPERIMENT
                if (terminate) return;
            #endif
        #endif
    }
}

VM_INS_RET VM::U_PUSHINT(int x) { VM_PUSH(Value(x)); VM_RET; }
VM_INS_RET VM::U_PUSHFLT(int x) { int2float i2f; i2f.i = x; VM_PUSH(Value(i2f.f)); VM_RET; }
VM_INS_RET VM::U_PUSHNIL() { VM_PUSH(Value()); VM_RET; }

VM_INS_RET VM::U_PUSHINT64(int a, int b) {
    #if !VALUE_MODEL_64
        Error("Code containing 64-bit constants cannot run on a 32-bit build.");
    #endif
    auto v = Int64FromInts(a, b);
    VM_PUSH(Value(v));
    VM_RET;
}

VM_INS_RET VM::U_PUSHFLT64(int a, int b) {
    int2float64 i2f;
    i2f.i = Int64FromInts(a, b);
    VM_PUSH(Value(i2f.f));
    VM_RET;
}

VM_INS_RET VM::U_PUSHFUN(int start VM_COMMA VM_OP_ARGS_CALL) {
    #ifdef VM_COMPILED_CODE_MODE
        (void)start;
    #else
        auto fcont = start;
    #endif
    VM_PUSH(Value(InsPtr(fcont)));
    VM_RET;
}

VM_INS_RET VM::U_PUSHSTR(int i) {
    // FIXME: have a way that constant strings can stay in the bytecode,
    // or at least preallocate them all
    auto &s = constant_strings[i];
    if (!s) {
        auto fb_s = bcf->stringtable()->Get(i);
        s = NewString(fb_s->string_view());
    }
    #if STRING_CONSTANTS_KEEP
        s->Inc();
    #endif
    VM_PUSH(Value(s));
    VM_RET;
}

VM_INS_RET VM::U_INCREF(int off) {
    VM_TOPM(off).LTINCRTNIL();
    VM_RET;
}

VM_INS_RET VM::U_KEEPREF(int off, int ki) {
    VM_TOPM(ki).LTDECRTNIL(*this);  // FIXME: this is only here for inlined for bodies!
    VM_TOPM(ki) = VM_TOPM(off);
    VM_RET;
}

VM_INS_RET VM::U_CALL(int f VM_COMMA VM_OP_ARGS_CALL) {
    #ifdef VM_COMPILED_CODE_MODE
        (void)f;
        block_t fun = 0;  // Dynamic calls need this set, but for CALL it is ignored.
    #else
        auto fun = f;
        auto fcont = ip - codestart;
    #endif
    StartStackFrame(InsPtr(fcont));
    FunIntroPre(InsPtr(fun));
    VM_RET;
}

VM_INS_RET VM::U_CALLVCOND(VM_OP_ARGS_CALL) {
    // FIXME: don't need to check for function value again below if false
    if (!VM_TOP().True()) {
        VM_POP();
        #ifdef VM_COMPILED_CODE_MODE
            next_call_target = 0;
        #endif
    } else {
        U_CALLV(VM_FC_PASS_THRU);
    }
    VM_RET;
}

VM_INS_RET VM::U_CALLV(VM_OP_ARGS_CALL) {
    Value fun = VM_POP();
    VMTYPEEQ(fun, V_FUNCTION);
    #ifndef VM_COMPILED_CODE_MODE
        auto fcont = ip - codestart;
    #endif
    StartStackFrame(InsPtr(fcont));
    FunIntroPre(fun.ip());
    VM_RET;
}

VM_INS_RET VM::U_DDCALL(int vtable_idx, int stack_idx VM_COMMA VM_OP_ARGS_CALL) {
    auto self = VM_TOPM(stack_idx);
    VMTYPEEQ(self, V_CLASS);
    auto start = self.oval()->ti(*this).vtable_start;
    auto fun = vtables[start + vtable_idx];
    #ifdef VM_COMPILED_CODE_MODE
    #else
        auto fcont = ip - codestart;
        assert(fun.f >= 0);
    #endif
    StartStackFrame(InsPtr(fcont));
    FunIntroPre(fun);
    VM_RET;
}

VM_INS_RET VM::U_FUNSTART(VM_OP_ARGS) {
    #ifdef VM_COMPILED_CODE_MODE
        FunIntro(ip);
    #else
        VMASSERT(false);
    #endif
    VM_RET;
}

VM_INS_RET VM::U_RETURN(int df, int nrv) {
    FunOut(df, nrv);
    VM_RET;
}

VM_INS_RET VM::U_ENDSTATEMENT(int line, int fileidx) {
    #ifdef NDEBUG
        (void)line;
        (void)fileidx;
    #else
        if (trace != TraceMode::OFF) {
            auto &ss = TraceStream();
            ss << bcf->filenames()->Get(fileidx)->string_view() << '(' << line << ')';
            if (trace == TraceMode::TAIL) ss << '\n'; else LOG_PROGRAM(ss.str());
        }
    #endif
    assert(sp == stackframes.back().spstart);
    VM_RET;
}

VM_INS_RET VM::U_EXIT(int tidx) {
    if (tidx >= 0) EndEval(VM_POP(), GetTypeInfo((type_elem_t)tidx));
    else EndEval(Value(), GetTypeInfo(TYPE_ELEM_ANY));
    VM_TERMINATE;
}

VM_INS_RET VM::U_CONT1(int nfi) {
    auto nf = nfr.nfuns[nfi];
    nf->cont1(*this);
    VM_RET;
}

VM_JMP_RET VM::ForLoop(intp len) {
    #ifndef VM_COMPILED_CODE_MODE
        auto cont = *ip++;
    #endif
    auto &i = VM_TOPM(1);
    assert(i.type == V_INT);
    i.setival(i.ival() + 1);
    if (i.ival() < len) {
        #ifdef VM_COMPILED_CODE_MODE
            return true;
        #else
            ip = cont + codestart;
        #endif
    } else {
        (void)VM_POP(); /* iter */
        (void)VM_POP(); /* i */
        #ifdef VM_COMPILED_CODE_MODE
            return false;
        #endif
    }
}

#define FORELEM(L) \
    auto &iter = VM_TOP(); \
    auto i = VM_TOPM(1).ival(); \
    assert(i < L); \

VM_JMP_RET VM::U_IFOR() { return ForLoop(VM_TOP().ival()); VM_RET; }
VM_JMP_RET VM::U_VFOR() { return ForLoop(VM_TOP().vval()->len); VM_RET; }
VM_JMP_RET VM::U_SFOR() { return ForLoop(VM_TOP().sval()->len); VM_RET; }

VM_INS_RET VM::U_IFORELEM()    { FORELEM(iter.ival()); (void)iter; VM_PUSH(i); VM_RET; }
VM_INS_RET VM::U_VFORELEM()    { FORELEM(iter.vval()->len); iter.vval()->AtVW(*this, i); VM_RET; }
VM_INS_RET VM::U_VFORELEMREF() { FORELEM(iter.vval()->len); auto el = iter.vval()->At(i); el.LTINCRTNIL(); VM_PUSH(el); VM_RET; }
VM_INS_RET VM::U_SFORELEM()    { FORELEM(iter.sval()->len); VM_PUSH(Value((int)((uchar *)iter.sval()->data())[i])); VM_RET; }

VM_INS_RET VM::U_FORLOOPI() {
    auto &i = VM_TOPM(1);  // This relies on for being inlined, otherwise it would be 2.
    assert(i.type == V_INT);
    VM_PUSH(i);
    VM_RET;
}

VM_INS_RET VM::U_BCALLRETV(int nfi) {
    BCallProf();
    auto nf = nfr.nfuns[nfi];
    nf->fun.fV(*this);
    VM_RET;
}
VM_INS_RET VM::U_BCALLREFV(int nfi) {
    BCallProf();
    auto nf = nfr.nfuns[nfi];
    nf->fun.fV(*this);
    // This can only pop a single value, not called for structs.
    VM_POP().LTDECRTNIL(*this);
    VM_RET;
}
VM_INS_RET VM::U_BCALLUNBV(int nfi) {
    BCallProf();
    auto nf = nfr.nfuns[nfi];
    nf->fun.fV(*this);
    // This can only pop a single value, not called for structs.
    VM_POP();
    VM_RET;
}

#define BCALLOPH(PRE,N,DECLS,ARGS,RETOP) VM_INS_RET VM::U_BCALL##PRE##N(int nfi) { \
    BCallProf(); \
    auto nf = nfr.nfuns[nfi]; \
    DECLS; \
    Value v = nf->fun.f##N ARGS; \
    RETOP; \
    VM_RET; \
}

#define BCALLOP(N,DECLS,ARGS) \
    BCALLOPH(RET,N,DECLS,ARGS,VM_PUSH(v);BCallRetCheck(nf)) \
    BCALLOPH(REF,N,DECLS,ARGS,v.LTDECRTNIL(*this)) \
    BCALLOPH(UNB,N,DECLS,ARGS,(void)v)

BCALLOP(0, {}, (*this));
BCALLOP(1, auto a0 = VM_POP(), (*this, a0));
BCALLOP(2, auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1));
BCALLOP(3, auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2));
BCALLOP(4, auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3));
BCALLOP(5, auto a4 = VM_POP();auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3, a4));
BCALLOP(6, auto a5 = VM_POP();auto a4 = VM_POP();auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3, a4, a5));
BCALLOP(7, auto a6 = VM_POP();auto a5 = VM_POP();auto a4 = VM_POP();auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3, a4, a5, a6));

VM_INS_RET VM::U_ASSERTR(int line, int fileidx, int stringidx) {
    (void)line;
    (void)fileidx;
    if (!VM_TOP().True()) {
        Error(cat(
            #ifdef VM_COMPILED_CODE_MODE
                bcf->filenames()->Get(fileidx)->string_view(), "(", line, "): ",
            #endif
            "assertion failed: ", bcf->stringtable()->Get(stringidx)->string_view()));
    }
    VM_RET;
}

VM_INS_RET VM::U_ASSERT(int line, int fileidx, int stringidx) {
    U_ASSERTR(line, fileidx, stringidx);
    VM_POP();
    VM_RET;
}

VM_INS_RET VM::U_NEWVEC(int ty, int len) {
    auto type = (type_elem_t)ty;
    auto vec = NewVec(len, len, type);
    if (len) vec->Init(*this, VM_TOPPTR() - len * vec->width, false);
    VM_POPN(len * (int)vec->width);
    VM_PUSH(Value(vec));
    VM_RET;
}

VM_INS_RET VM::U_NEWOBJECT(int ty) {
    auto type = (type_elem_t)ty;
    auto len = GetTypeInfo(type).len;
    auto vec = NewObject(len, type);
    if (len) vec->Init(*this, VM_TOPPTR() - len, len, false);
    VM_POPN(len);
    VM_PUSH(Value(vec));
    VM_RET;
}

VM_INS_RET VM::U_POP()     { VM_POP(); VM_RET; }
VM_INS_RET VM::U_POPREF()  { auto x = VM_POP(); x.LTDECRTNIL(*this); VM_RET; }

VM_INS_RET VM::U_POPV(int len)    { VM_POPN(len); VM_RET; }
VM_INS_RET VM::U_POPVREF(int len) { while (len--) VM_POP().LTDECRTNIL(*this); VM_RET; }

VM_INS_RET VM::U_DUP()    { auto x = VM_TOP(); VM_PUSH(x); VM_RET; }

#define GETARGS() Value b = VM_POP(); Value a = VM_POP()
#define TYPEOP(op, extras, field, errstat) Value res; errstat; \
    if (extras & 1 && b.field == 0) Div0(); res = a.field op b.field;

#define _IOP(op, extras) \
    TYPEOP(op, extras, ival(), assert(a.type == V_INT && b.type == V_INT))
#define _FOP(op, extras) \
    TYPEOP(op, extras, fval(), assert(a.type == V_FLOAT && b.type == V_FLOAT))

#define _GETA() VM_TOPPTR() - len
#define _VOP(op, extras, V_T, field, withscalar, geta) { \
    if (withscalar) { \
        auto b = VM_POP(); \
        VMTYPEEQ(b, V_T) \
        auto veca = geta; \
        for (int j = 0; j < len; j++) { \
            auto &a = veca[j]; \
            VMTYPEEQ(a, V_T) \
            auto bv = b.field(); \
            if (extras&1 && bv == 0) Div0(); \
            a = Value(a.field() op bv); \
        } \
    } else { \
        VM_POPN(len); \
        auto vecb = VM_TOPPTR(); \
        auto veca = geta; \
        for (int j = 0; j < len; j++) { \
            auto b = vecb[j]; \
            VMTYPEEQ(b, V_T) \
            auto &a = veca[j]; \
            VMTYPEEQ(a, V_T) \
            auto bv = b.field(); \
            if (extras & 1 && bv == 0) Div0(); \
            a = Value(a.field() op bv); \
        } \
    } \
}
#define STCOMPEN(op, init, andor) { \
    VM_POPN(len); \
    auto vecb = VM_TOPPTR(); \
    VM_POPN(len); \
    auto veca = VM_TOPPTR(); \
    auto all = init; \
    for (int j = 0; j < len; j++) { \
        all = all andor veca[j].any() op vecb[j].any(); \
    } \
    VM_PUSH(all); \
    VM_RET; \
}

#define _IVOP(op, extras, withscalar, geta) _VOP(op, extras, V_INT, ival, withscalar, geta)
#define _FVOP(op, extras, withscalar, geta) _VOP(op, extras, V_FLOAT, fval, withscalar, geta)

#define _SOP(op) Value res = *a.sval() op *b.sval()
#define _SCAT() Value res = NewString(a.sval()->strv(), b.sval()->strv())

#define ACOMPEN(op)        { GETARGS(); Value res = a.any() op b.any();  VM_PUSH(res); VM_RET; }
#define IOP(op, extras)    { GETARGS(); _IOP(op, extras);                VM_PUSH(res); VM_RET; }
#define FOP(op, extras)    { GETARGS(); _FOP(op, extras);                VM_PUSH(res); VM_RET; }

#define LOP(op)            { GETARGS(); auto res = a.ip() op b.ip();     VM_PUSH(res); VM_RET; }

#define IVVOP(op, extras)  { _IVOP(op, extras, false, _GETA()); VM_RET; }
#define FVVOP(op, extras)  { _FVOP(op, extras, false, _GETA()); VM_RET; }
#define IVSOP(op, extras)  { _IVOP(op, extras, true, _GETA());  VM_RET; }
#define FVSOP(op, extras)  { _FVOP(op, extras, true, _GETA());  VM_RET; }

#define SOP(op)            { GETARGS(); _SOP(op);                        VM_PUSH(res); VM_RET; }
#define SCAT()             { GETARGS(); _SCAT();                         VM_PUSH(res); VM_RET; }

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

VM_INS_RET VM::U_IVVADD(int len) { IVVOP(+,  0);  }
VM_INS_RET VM::U_IVVSUB(int len) { IVVOP(-,  0);  }
VM_INS_RET VM::U_IVVMUL(int len) { IVVOP(*,  0);  }
VM_INS_RET VM::U_IVVDIV(int len) { IVVOP(/,  1);  }
VM_INS_RET VM::U_IVVMOD(int)     { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_IVVLT(int len)  { IVVOP(<,  0);  }
VM_INS_RET VM::U_IVVGT(int len)  { IVVOP(>,  0);  }
VM_INS_RET VM::U_IVVLE(int len)  { IVVOP(<=, 0);  }
VM_INS_RET VM::U_IVVGE(int len)  { IVVOP(>=, 0);  }
VM_INS_RET VM::U_FVVADD(int len) { FVVOP(+,  0);  }
VM_INS_RET VM::U_FVVSUB(int len) { FVVOP(-,  0);  }
VM_INS_RET VM::U_FVVMUL(int len) { FVVOP(*,  0);  }
VM_INS_RET VM::U_FVVDIV(int len) { FVVOP(/,  1);  }
VM_INS_RET VM::U_FVVMOD(int)     { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_FVVLT(int len)  { FVVOP(<,  0); }
VM_INS_RET VM::U_FVVGT(int len)  { FVVOP(>,  0); }
VM_INS_RET VM::U_FVVLE(int len)  { FVVOP(<=, 0); }
VM_INS_RET VM::U_FVVGE(int len)  { FVVOP(>=, 0); }

VM_INS_RET VM::U_IVSADD(int len) { IVSOP(+,  0);  }
VM_INS_RET VM::U_IVSSUB(int len) { IVSOP(-,  0);  }
VM_INS_RET VM::U_IVSMUL(int len) { IVSOP(*,  0);  }
VM_INS_RET VM::U_IVSDIV(int len) { IVSOP(/,  1);  }
VM_INS_RET VM::U_IVSMOD(int)     { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_IVSLT(int len)  { IVSOP(<,  0);  }
VM_INS_RET VM::U_IVSGT(int len)  { IVSOP(>,  0);  }
VM_INS_RET VM::U_IVSLE(int len)  { IVSOP(<=, 0);  }
VM_INS_RET VM::U_IVSGE(int len)  { IVSOP(>=, 0);  }
VM_INS_RET VM::U_FVSADD(int len) { FVSOP(+,  0);  }
VM_INS_RET VM::U_FVSSUB(int len) { FVSOP(-,  0);  }
VM_INS_RET VM::U_FVSMUL(int len) { FVSOP(*,  0);  }
VM_INS_RET VM::U_FVSDIV(int len) { FVSOP(/,  1);  }
VM_INS_RET VM::U_FVSMOD(int)     { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_FVSLT(int len)  { FVSOP(<,  0); }
VM_INS_RET VM::U_FVSGT(int len)  { FVSOP(>,  0); }
VM_INS_RET VM::U_FVSLE(int len)  { FVSOP(<=, 0); }
VM_INS_RET VM::U_FVSGE(int len)  { FVSOP(>=, 0); }

VM_INS_RET VM::U_AEQ()  { ACOMPEN(==); }
VM_INS_RET VM::U_ANE()  { ACOMPEN(!=); }
VM_INS_RET VM::U_STEQ(int len) { STCOMPEN(==, true, &&); }
VM_INS_RET VM::U_STNE(int len) { STCOMPEN(!=, false, ||); }
VM_INS_RET VM::U_LEQ() { LOP(==); }
VM_INS_RET VM::U_LNE() { LOP(!=); }

VM_INS_RET VM::U_IADD() { IOP(+,  0); }
VM_INS_RET VM::U_ISUB() { IOP(-,  0); }
VM_INS_RET VM::U_IMUL() { IOP(*,  0); }
VM_INS_RET VM::U_IDIV() { IOP(/ , 1); }
VM_INS_RET VM::U_IMOD() { IOP(%,  1); }
VM_INS_RET VM::U_ILT()  { IOP(<,  0); }
VM_INS_RET VM::U_IGT()  { IOP(>,  0); }
VM_INS_RET VM::U_ILE()  { IOP(<=, 0); }
VM_INS_RET VM::U_IGE()  { IOP(>=, 0); }
VM_INS_RET VM::U_IEQ()  { IOP(==, 0); }
VM_INS_RET VM::U_INE()  { IOP(!=, 0); }

VM_INS_RET VM::U_FADD() { FOP(+,  0); }
VM_INS_RET VM::U_FSUB() { FOP(-,  0); }
VM_INS_RET VM::U_FMUL() { FOP(*,  0); }
VM_INS_RET VM::U_FDIV() { FOP(/,  1); }
VM_INS_RET VM::U_FMOD() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_FLT()  { FOP(<,  0); }
VM_INS_RET VM::U_FGT()  { FOP(>,  0); }
VM_INS_RET VM::U_FLE()  { FOP(<=, 0); }
VM_INS_RET VM::U_FGE()  { FOP(>=, 0); }
VM_INS_RET VM::U_FEQ()  { FOP(==, 0); }
VM_INS_RET VM::U_FNE()  { FOP(!=, 0); }

VM_INS_RET VM::U_SADD() { SCAT();  }
VM_INS_RET VM::U_SSUB() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SMUL() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SDIV() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SMOD() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SLT()  { SOP(<);  }
VM_INS_RET VM::U_SGT()  { SOP(>);  }
VM_INS_RET VM::U_SLE()  { SOP(<=); }
VM_INS_RET VM::U_SGE()  { SOP(>=); }
VM_INS_RET VM::U_SEQ()  { SOP(==); }
VM_INS_RET VM::U_SNE()  { SOP(!=); }

VM_INS_RET VM::U_IUMINUS() { Value a = VM_POP(); VM_PUSH(Value(-a.ival())); VM_RET; }
VM_INS_RET VM::U_FUMINUS() { Value a = VM_POP(); VM_PUSH(Value(-a.fval())); VM_RET; }

VM_INS_RET VM::U_IVUMINUS(int len) {
    auto vec = VM_TOPPTR() - len;
    for (int i = 0; i < len; i++) {
        auto &a = vec[i];
        VMTYPEEQ(a, V_INT);
        a = -a.ival();
    }
    VM_RET;
}
VM_INS_RET VM::U_FVUMINUS(int len) {
    auto vec = VM_TOPPTR() - len;
    for (int i = 0; i < len; i++) {
        auto &a = vec[i];
        VMTYPEEQ(a, V_FLOAT);
        a = -a.fval();
    }
    VM_RET;
}

VM_INS_RET VM::U_LOGNOT() {
    Value a = VM_POP();
    VM_PUSH(!a.True());
    VM_RET;
}
VM_INS_RET VM::U_LOGNOTREF() {
    Value a = VM_POP();
    bool b = a.True();
    VM_PUSH(!b);
    VM_RET;
}

#define BITOP(op) { GETARGS(); VM_PUSH(a.ival() op b.ival()); VM_RET; }
VM_INS_RET VM::U_BINAND() { BITOP(&);  }
VM_INS_RET VM::U_BINOR()  { BITOP(|);  }
VM_INS_RET VM::U_XOR()    { BITOP(^);  }
VM_INS_RET VM::U_ASL()    { BITOP(<<); }
VM_INS_RET VM::U_ASR()    { BITOP(>>); }
VM_INS_RET VM::U_NEG()    { auto a = VM_POP(); VM_PUSH(~a.ival()); VM_RET; }

VM_INS_RET VM::U_I2F() {
    Value a = VM_POP();
    VMTYPEEQ(a, V_INT);
    VM_PUSH((float)a.ival());
    VM_RET;
}

VM_INS_RET VM::U_A2S(int ty) {
    Value a = VM_POP();
    VM_PUSH(ToString(a, GetTypeInfo((type_elem_t)ty)));
    VM_RET;
}

VM_INS_RET VM::U_ST2S(int ty) {
    auto &ti = GetTypeInfo((type_elem_t)ty);
    VM_POPN(ti.len);
    auto top = VM_TOPPTR();
    VM_PUSH(StructToString(top, ti));
    VM_RET;
}

VM_INS_RET VM::U_E2B() {
    Value a = VM_POP();
    VM_PUSH(a.True());
    VM_RET;
}

VM_INS_RET VM::U_E2BREF() {
    Value a = VM_POP();
    VM_PUSH(a.True());
    VM_RET;
}

VM_INS_RET VM::U_PUSHVAR(int vidx) {
    VM_PUSH(vars[vidx]);
    VM_RET;
}

VM_INS_RET VM::U_PUSHVARV(int vidx, int l) {
    tsnz_memcpy(VM_TOPPTR(), &vars[vidx], l);
    VM_PUSHN(l);
    VM_RET;
}

VM_INS_RET VM::U_PUSHFLD(int i) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    assert(i < r.oval()->Len(*this));
    VM_PUSH(r.oval()->AtS(i));
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLDMREF(int i) {
    Value r = VM_POP();
    if (!r.ref()) {
        VM_PUSH(r);
    } else {
        assert(i < r.oval()->Len(*this));
        VM_PUSH(r.oval()->AtS(i));
    }
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLD2V(int i, int l) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    assert(i + l <= r.oval()->Len(*this));
    tsnz_memcpy(VM_TOPPTR(), &r.oval()->AtS(i), l);
    VM_PUSHN(l);
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLDV(int i, int l) {
    VM_POPN(l);
    auto val = *(VM_TOPPTR() + i);
    VM_PUSH(val);
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLDV2V(int i, int rl, int l) {
    VM_POPN(l);
    t_memmove(VM_TOPPTR(), VM_TOPPTR() + i, rl);
    VM_PUSHN(rl);
    VM_RET;
}

VM_INS_RET VM::U_VPUSHIDXI()  { PushDerefIdxVector(VM_POP().ival()); VM_RET; }
VM_INS_RET VM::U_VPUSHIDXV(int l)  { PushDerefIdxVector(GrabIndex(l)); VM_RET; }
VM_INS_RET VM::U_VPUSHIDXIS(int w, int o) { PushDerefIdxVectorSub(VM_POP().ival(), w, o); VM_RET; }
VM_INS_RET VM::U_VPUSHIDXVS(int l, int w, int o) { PushDerefIdxVectorSub(GrabIndex(l), w, o); VM_RET; }
VM_INS_RET VM::U_NPUSHIDXI(int l)  { PushDerefIdxStruct(VM_POP().ival(), l); VM_RET; }
VM_INS_RET VM::U_SPUSHIDXI()  { PushDerefIdxString(VM_POP().ival()); VM_RET; }

VM_INS_RET VM::U_PUSHLOC(int i) {
    auto coro = VM_POP().cval();
    VM_PUSH(coro->GetVar(*this, i));
    VM_RET;
}

VM_INS_RET VM::U_PUSHLOCV(int i, int l) {
    auto coro = VM_POP().cval();
    tsnz_memcpy(VM_TOPPTR(), &coro->GetVar(*this, i), l);
    VM_PUSHN(l);
    VM_RET;
}

#ifdef VM_COMPILED_CODE_MODE
    #define GJUMP(N, V, C, P) VM_JMP_RET VM::U_##N() \
        { V; if (C) { P; return true; } else { return false; } }
#else
    #define GJUMP(N, V, C, P) VM_JMP_RET VM::U_##N() \
        { V; auto nip = *ip++; if (C) { ip = codestart + nip; P; } VM_RET; }
#endif

GJUMP(JUMP       ,                  , true     ,                 )
GJUMP(JUMPFAIL   , auto x = VM_POP(), !x.True(),                 )
GJUMP(JUMPFAILR  , auto x = VM_POP(), !x.True(), VM_PUSH(x)      )
GJUMP(JUMPFAILN  , auto x = VM_POP(), !x.True(), VM_PUSH(Value()))
GJUMP(JUMPNOFAIL , auto x = VM_POP(),  x.True(),                 )
GJUMP(JUMPNOFAILR, auto x = VM_POP(),  x.True(), VM_PUSH(x)      )

VM_INS_RET VM::U_ISTYPE(int ty) {
    auto to = (type_elem_t)ty;
    auto v = VM_POP();
    // Optimizer guarantees we don't have to deal with scalars.
    if (v.refnil()) VM_PUSH(v.ref()->tti == to);
    else VM_PUSH(GetTypeInfo(to).t == V_NIL);  // FIXME: can replace by fixed type_elem_t ?
    VM_RET;
}

VM_INS_RET VM::U_YIELD(VM_OP_ARGS_CALL) { CoYield(VM_FC_PASS_THRU); VM_RET; }

// This value never gets used anywhere, just a placeholder.
VM_INS_RET VM::U_COCL() { VM_PUSH(Value(0, V_YIELD)); VM_RET; }

VM_INS_RET VM::U_CORO(VM_OP_ARGS VM_COMMA VM_OP_ARGS_CALL) { CoNew(VM_IP_PASS_THRU VM_COMMA VM_FC_PASS_THRU); VM_RET; }

VM_INS_RET VM::U_COEND() { CoClean(); VM_RET; }

VM_INS_RET VM::U_LOGREAD(int vidx) {
    auto val = VM_POP();
    VM_PUSH(vml.LogGet(val, vidx));
    VM_RET;
}

VM_INS_RET VM::U_LOGWRITE(int vidx, int lidx) {
    vml.LogWrite(vars[vidx], lidx);
    VM_RET;
}

VM_INS_RET VM::U_ABORT() {
    Error("VM internal error: abort");
    VM_RET;
}

void VM::IDXErr(intp i, intp n, const RefObj *v) {
    Error(cat("index ", i, " out of range ", n), v);
}
#define RANGECHECK(I, BOUND, VEC) if ((uintp)I >= (uintp)BOUND) IDXErr(I, BOUND, VEC);

void VM::PushDerefIdxVector(intp i) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    auto v = r.vval();
    RANGECHECK(i, v->len, v);
    v->AtVW(*this, i);
}

void VM::PushDerefIdxVectorSub(intp i, int width, int offset) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    auto v = r.vval();
    RANGECHECK(i, v->len, v);
    v->AtVWSub(*this, i, width, offset);
}

void VM::PushDerefIdxStruct(intp i, int l) {
    VM_POPN(l);
    auto val = *(VM_TOPPTR() + i);
    VM_PUSH(val);
}

void VM::PushDerefIdxString(intp i) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    // Allow access of the terminating 0-byte.
    RANGECHECK(i, r.sval()->len + 1, r.sval());
    VM_PUSH(Value((int)((uchar *)r.sval()->data())[i]));
}

Value &VM::GetFieldLVal(intp i) {
    Value vec = VM_POP();
    #ifndef NDEBUG
        RANGECHECK(i, vec.oval()->Len(*this), vec.oval());
    #endif
    return vec.oval()->AtS(i);
}

Value &VM::GetFieldILVal(intp i) {
    Value vec = VM_POP();
    RANGECHECK(i, vec.oval()->Len(*this), vec.oval());
    return vec.oval()->AtS(i);
}

Value &VM::GetVecLVal(intp i) {
    Value vec = VM_POP();
    auto v = vec.vval();
    RANGECHECK(i, v->len, v);
    return *v->AtSt(i);
}

Value &VM::GetLocLVal(int i) {
    Value coro = VM_POP();
    VMTYPEEQ(coro, V_COROUTINE);
    return coro.cval()->GetVar(*this, i);
}

#pragma push_macro("LVAL")
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_VAR_##N(int vidx VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(vars[vidx] VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_FLD_##N(int i VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(GetFieldLVal(i) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_LOC_##N(int i VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(GetLocLVal(i) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_IDXVI_##N(VM_OP_ARGSN(V)) \
    { LV_##N(GetVecLVal(VM_POP().ival()) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_IDXVV_##N(int l VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(GetVecLVal(GrabIndex(l)) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_IDXNI_##N(VM_OP_ARGSN(V)) \
    { LV_##N(GetFieldILVal(VM_POP().ival()) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#pragma pop_macro("LVAL")

#define LVALCASES(N, B) void VM::LV_##N(Value &a) { Value b = VM_POP(); B; }
#define LVALCASER(N, B) void VM::LV_##N(Value &fa, int len) { B; }
#define LVALCASESTR(N, B, B2) void VM::LV_##N(Value &a) { Value b = VM_POP(); B; a.LTDECRTNIL(*this); B2; }

LVALCASER(IVVADD , _IVOP(+, 0, false, &fa))
LVALCASER(IVVADDR, _IVOP(+, 0, false, &fa))
LVALCASER(IVVSUB , _IVOP(-, 0, false, &fa))
LVALCASER(IVVSUBR, _IVOP(-, 0, false, &fa))
LVALCASER(IVVMUL , _IVOP(*, 0, false, &fa))
LVALCASER(IVVMULR, _IVOP(*, 0, false, &fa))
LVALCASER(IVVDIV , _IVOP(/, 1, false, &fa))
LVALCASER(IVVDIVR, _IVOP(/, 1, false, &fa))
LVALCASER(IVVMOD , VMASSERT(0); (void)fa; (void)len)
LVALCASER(IVVMODR, VMASSERT(0); (void)fa; (void)len)

LVALCASER(FVVADD , _FVOP(+, 0, false, &fa))
LVALCASER(FVVADDR, _FVOP(+, 0, false, &fa))
LVALCASER(FVVSUB , _FVOP(-, 0, false, &fa))
LVALCASER(FVVSUBR, _FVOP(-, 0, false, &fa))
LVALCASER(FVVMUL , _FVOP(*, 0, false, &fa))
LVALCASER(FVVMULR, _FVOP(*, 0, false, &fa))
LVALCASER(FVVDIV , _FVOP(/, 1, false, &fa))
LVALCASER(FVVDIVR, _FVOP(/, 1, false, &fa))

LVALCASER(IVSADD , _IVOP(+, 0, true,  &fa))
LVALCASER(IVSADDR, _IVOP(+, 0, true,  &fa))
LVALCASER(IVSSUB , _IVOP(-, 0, true,  &fa))
LVALCASER(IVSSUBR, _IVOP(-, 0, true,  &fa))
LVALCASER(IVSMUL , _IVOP(*, 0, true,  &fa))
LVALCASER(IVSMULR, _IVOP(*, 0, true,  &fa))
LVALCASER(IVSDIV , _IVOP(/, 1, true,  &fa))
LVALCASER(IVSDIVR, _IVOP(/, 1, true,  &fa))
LVALCASER(IVSMOD , VMASSERT(0); (void)fa; (void)len)
LVALCASER(IVSMODR, VMASSERT(0); (void)fa; (void)len)

LVALCASER(FVSADD , _FVOP(+, 0, true,  &fa))
LVALCASER(FVSADDR, _FVOP(+, 0, true,  &fa))
LVALCASER(FVSSUB , _FVOP(-, 0, true,  &fa))
LVALCASER(FVSSUBR, _FVOP(-, 0, true,  &fa))
LVALCASER(FVSMUL , _FVOP(*, 0, true,  &fa))
LVALCASER(FVSMULR, _FVOP(*, 0, true,  &fa))
LVALCASER(FVSDIV , _FVOP(/, 1, true,  &fa))
LVALCASER(FVSDIVR, _FVOP(/, 1, true,  &fa))

LVALCASES(IADD   , _IOP(+, 0); a = res;          )
LVALCASES(IADDR  , _IOP(+, 0); a = res; VM_PUSH(res))
LVALCASES(ISUB   , _IOP(-, 0); a = res;          )
LVALCASES(ISUBR  , _IOP(-, 0); a = res; VM_PUSH(res))
LVALCASES(IMUL   , _IOP(*, 0); a = res;          )
LVALCASES(IMULR  , _IOP(*, 0); a = res; VM_PUSH(res))
LVALCASES(IDIV   , _IOP(/, 1); a = res;          )
LVALCASES(IDIVR  , _IOP(/, 1); a = res; VM_PUSH(res))
LVALCASES(IMOD   , _IOP(%, 1); a = res;          )
LVALCASES(IMODR  , _IOP(%, 1); a = res; VM_PUSH(res))

LVALCASES(FADD   , _FOP(+, 0); a = res;          )
LVALCASES(FADDR  , _FOP(+, 0); a = res; VM_PUSH(res))
LVALCASES(FSUB   , _FOP(-, 0); a = res;          )
LVALCASES(FSUBR  , _FOP(-, 0); a = res; VM_PUSH(res))
LVALCASES(FMUL   , _FOP(*, 0); a = res;          )
LVALCASES(FMULR  , _FOP(*, 0); a = res; VM_PUSH(res))
LVALCASES(FDIV   , _FOP(/, 1); a = res;          )
LVALCASES(FDIVR  , _FOP(/, 1); a = res; VM_PUSH(res))

LVALCASESTR(SADD , _SCAT(),    a = res;          )
LVALCASESTR(SADDR, _SCAT(),    a = res; VM_PUSH(res))

#define OVERWRITE_VAR(a, b) { assert(a.type == b.type || a.type == V_NIL || b.type == V_NIL); a = b; }

void VM::LV_WRITE    (Value &a) { auto  b = VM_POP();                      OVERWRITE_VAR(a, b); }
void VM::LV_WRITER   (Value &a) { auto &b = VM_TOP();                      OVERWRITE_VAR(a, b); }
void VM::LV_WRITEREF (Value &a) { auto  b = VM_POP(); a.LTDECRTNIL(*this); OVERWRITE_VAR(a, b); }
void VM::LV_WRITERREF(Value &a) { auto &b = VM_TOP(); a.LTDECRTNIL(*this); OVERWRITE_VAR(a, b); }

#define WRITESTRUCT(DECS) \
    auto b = VM_TOPPTR() - l; \
    if (DECS) for (int i = 0; i < l; i++) (&a)[i].LTDECRTNIL(*this); \
    tsnz_memcpy(&a, b, l);

void VM::LV_WRITEV    (Value &a, int l) { WRITESTRUCT(false); VM_POPN(l); }
void VM::LV_WRITERV   (Value &a, int l) { WRITESTRUCT(false); }
void VM::LV_WRITEREFV (Value &a, int l) { WRITESTRUCT(true); VM_POPN(l); }
void VM::LV_WRITERREFV(Value &a, int l) { WRITESTRUCT(true); }

#define PPOP(name, ret, op, pre, accessor) void VM::LV_##name(Value &a) { \
    if (ret && !pre) VM_PUSH(a); \
    a.set##accessor(a.accessor() op 1); \
    if (ret && pre) VM_PUSH(a); \
}

PPOP(IPP  , false, +, true , ival)
PPOP(IPPR , true , +, true , ival)
PPOP(IMM  , false, -, true , ival)
PPOP(IMMR , true , -, true , ival)
PPOP(IPPP , false, +, false, ival)
PPOP(IPPPR, true , +, false, ival)
PPOP(IMMP , false, -, false, ival)
PPOP(IMMPR, true , -, false, ival)
PPOP(FPP  , false, +, true , fval)
PPOP(FPPR , true , +, true , fval)
PPOP(FMM  , false, -, true , fval)
PPOP(FMMR , true , -, true , fval)
PPOP(FPPP , false, +, false, fval)
PPOP(FPPPR, true , +, false, fval)
PPOP(FMMP , false, -, false, fval)
PPOP(FMMPR, true , -, false, fval)

string VM::ProperTypeName(const TypeInfo &ti) {
    switch (ti.t) {
        case V_STRUCT_R:
        case V_STRUCT_S:
        case V_CLASS: return string(ReverseLookupType(ti.structidx));
        case V_NIL: return ProperTypeName(GetTypeInfo(ti.subt)) + "?";
        case V_VECTOR: return "[" + ProperTypeName(GetTypeInfo(ti.subt)) + "]";
        case V_INT: return ti.enumidx >= 0 ? string(EnumName(ti.enumidx)) : "int";
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
        if (!nf->cont1) {
            for (size_t i = 0; i < nf->retvals.v.size(); i++) {
                auto t = (VM_TOPPTR() - nf->retvals.v.size() + i)->type;
                auto u = nf->retvals.v[i].type->t;
                assert(t == u || u == V_ANY || u == V_NIL || (u == V_VECTOR && IsUDT(t)));
            }
            assert(nf->retvals.v.size() || VM_TOP().type == V_NIL);
        }
    #else
        (void)nf;
    #endif
}

intp VM::GrabIndex(int len) {
    auto &v = VM_TOPM(len);
    for (len--; ; len--) {
        auto sidx = VM_POP().ival();
        if (!len) return sidx;
        RANGECHECK(sidx, v.vval()->len, v.vval());
        v = v.vval()->At(sidx);
    }
}

string_view VM::StructName(const TypeInfo &ti) {
    return bcf->udts()->Get(ti.structidx)->name()->string_view();
}

string_view VM::ReverseLookupType(uint v) {
    return bcf->udts()->Get(v)->name()->string_view();
}

string_view VM::EnumName(intp val, int enumidx) {
    auto &vals = *bcf->enums()->Get(enumidx)->vals();
    // FIXME: can store a bool that says wether this enum is contiguous, so we just index instead.
    for (auto v : vals)
        if (v->val() == val)
            return v->name()->string_view();
    return {};
}

string_view VM::EnumName(int enumidx) {
    return bcf->enums()->Get(enumidx)->name()->string_view();
}

optional<int64_t> VM::LookupEnum(string_view name, int enumidx) {
    auto &vals = *bcf->enums()->Get(enumidx)->vals();
    for (auto v : vals)
        if (v->name()->string_view() == name)
            return v->val();
    return {};
}

void VM::StartWorkers(size_t numthreads) {
    if (is_worker) Error("workers can\'t start more worker threads");
    if (tuple_space) Error("workers already running");
    // Stop bad values from locking up the machine :)
    numthreads = min(numthreads, (size_t)256);
    tuple_space = new TupleSpace(bcf->udts()->size());
    for (size_t i = 0; i < numthreads; i++) {
        // Create a new VM that should own all its own memory and be completely independent
        // from this one.
        // We share nfr and programname for now since they're fully read-only.
        // FIXME: have to copy bytecode buffer even though it is read-only.
        auto vmargs = *(VMArgs *)this;
        vmargs.program_args.resize(0);
        vmargs.trace = TraceMode::OFF;
        auto wvm = new VM(std::move(vmargs));
        wvm->is_worker = true;
        wvm->tuple_space = tuple_space;
        workers.emplace_back([wvm] {
            string err;
            #ifdef USE_EXCEPTION_HANDLING
            try
            #endif
            {
                wvm->EvalProgram();
            }
            #ifdef USE_EXCEPTION_HANDLING
            catch (string &s) {
                if (s != "end-eval") err = s;
            }
            #endif
            delete wvm;
            // FIXME: instead return err to main thread?
            if (!err.empty()) LOG_ERROR("worker error: ", err);
        });
    }
}

void VM::TerminateWorkers() {
    if (is_worker || !tuple_space) return;
    tuple_space->alive = false;
    for (auto &tt : tuple_space->tupletypes) tt.condition.notify_all();
    for (auto &worker : workers) worker.join();
    workers.clear();
    delete tuple_space;
    tuple_space = nullptr;
}

void VM::WorkerWrite(RefObj *ref) {
    if (!tuple_space) return;
    if (!ref) Error("thread write: nil reference");
    auto &ti = ref->ti(*this);
    if (ti.t != V_CLASS) Error("thread write: must be a class");
    auto st = (LObject *)ref;
    auto buf = new Value[ti.len];
    for (int i = 0; i < ti.len; i++) {
        // FIXME: lift this restriction.
        if (IsRefNil(GetTypeInfo(ti.elemtypes[i]).t))
            Error("thread write: only scalar class members supported for now");
        buf[i] = st->AtS(i);
    }
    auto &tt = tuple_space->tupletypes[ti.structidx];
    {
        unique_lock<mutex> lock(tt.mtx);
        tt.tuples.push_back(buf);
    }
    tt.condition.notify_one();
}

LObject *VM::WorkerRead(type_elem_t tti) {
    auto &ti = GetTypeInfo(tti);
    if (ti.t != V_CLASS) Error("thread read: must be a class type");
    Value *buf = nullptr;
    auto &tt = tuple_space->tupletypes[ti.structidx];
    {
        unique_lock<mutex> lock(tt.mtx);
        tt.condition.wait(lock, [&] { return !tuple_space->alive || !tt.tuples.empty(); });
        if (!tt.tuples.empty()) {
            buf = tt.tuples.front();
            tt.tuples.pop_front();
        }
    }
    if (!buf) return nullptr;
    auto ns = NewObject(ti.len, tti);
    ns->Init(*this, buf, ti.len, false);
    delete[] buf;
    return ns;
}

}  // namespace lobster


// Make VM ops available as C functions for linking purposes:

#ifdef VM_COMPILED_CODE_MODE

extern "C" {

using namespace lobster;

#ifndef NDEBUG
    #define CHECKI(B) \
        if (vm->trace != TraceMode::OFF) { \
            auto &ss = vm->TraceStream(); \
            ss << B; \
            if (vm->trace == TraceMode::TAIL) ss << '\n'; else LOG_PROGRAM(ss.str()); \
        }
    // FIXME: add spaces.
    #define CHECK(N, A) CHECKI(#N << cat A)
    #define CHECKJ(N) CHECKI(#N)
#else
    #define CHECK(N, A)
    #define CHECKJ(N)
#endif


void CVM_SetNextCallTarget(VM *vm, block_t fcont) {
    vm->next_call_target = fcont;
}

block_t CVM_GetNextCallTarget(VM *vm) {
    return vm->next_call_target;
}

#define F(N, A) \
    void CVM_##N(VM *vm VM_COMMA_IF(A) VM_OP_ARGSN(A)) { \
        CHECK(N, (VM_OP_PASSN(A))); vm->U_##N(VM_OP_PASSN(A)); }
    LVALOPNAMES
#undef F
#define F(N, A) \
    void CVM_##N(VM *vm VM_COMMA_IF(A) VM_OP_ARGSN(A)) { \
        CHECK(N, (VM_OP_PASSN(A))); vm->U_##N(VM_OP_PASSN(A)); }
    ILBASENAMES
#undef F
#define F(N, A) \
    void CVM_##N(VM *vm VM_COMMA_IF(A) VM_OP_ARGSN(A), block_t fcont) { \
        CHECK(N, (VM_OP_PASSN(A))); vm->U_##N(VM_OP_PASSN(A) VM_COMMA_IF(A) fcont); }
    ILCALLNAMES
#undef F
#define F(N, A) \
    bool CVM_##N(VM *vm) { \
        CHECKJ(N); return vm->U_##N(); }
    ILJUMPNAMES
#undef F

}  // extern "C"

#endif  // VM_COMPILED_CODE_MODE
