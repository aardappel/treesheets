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

#include "lobster/vmops.h"

namespace lobster {


VM::VM(VMArgs &&vmargs, const bytecode::BytecodeFile *bcf)
    : VMArgs(std::move(vmargs)), bcf(bcf) {

    if (FLATBUFFERS_LITTLEENDIAN) {
        // We can use the buffer directly.
        typetable = (const type_elem_t *)bcf->typetable()->Data();
    } else {
        for (uint32_t i = 0; i < bcf->typetable()->size(); i++)
            typetablebigendian.push_back((type_elem_t)bcf->typetable()->Get(i));
        typetable = typetablebigendian.data();
    }
    constant_strings.resize(bcf->stringtable()->size());
    assert(native_vtables);
}

VM::~VM() {
    TerminateWorkers();
    if (byteprofilecounts) delete[] byteprofilecounts;
}

VMAllocator::VMAllocator(VMArgs &&args) {
    // Verify the bytecode.
    flatbuffers::Verifier verifier(args.static_bytecode, args.static_size);
    auto ok = bytecode::VerifyBytecodeFileBuffer(verifier);
    if (!ok) THROW_OR_ABORT("bytecode file failed to verify");
    auto bcf = bytecode::GetBytecodeFile(args.static_bytecode);
    if (bcf->bytecode_version() != LOBSTER_BYTECODE_FORMAT_VERSION)
        THROW_OR_ABORT("bytecode is from a different version of Lobster");

    // Allocate enough memory to fit the "fvars" array inline.
    auto size = sizeof(VM) + sizeof(Value) * bcf->specidents()->size();
    auto mem = malloc(size);
    assert(mem);
    memset(mem, 0, size);  // FIXME: this shouldn't be necessary.

    #undef new

    vm = new (mem) VM(std::move(args), bcf);

    #ifdef _MSC_VER
    #ifndef NDEBUG
    #define new DEBUG_NEW
    #endif
    #endif
}

VMAllocator::~VMAllocator() {
    if (!vm) return;
    vm->~VM();
    free(vm);
}

const TypeInfo &VM::GetVarTypeInfo(int varidx) {
    return GetTypeInfo((type_elem_t)bcf->specidents()->Get(varidx)->typeidx());
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
    string sd;
    append(sd, prefix, ": ");
    RefToString(*this, sd, ro, debugpp);
    append(sd, " (", ro->refc, "): ", (size_t)ro);
    LOG_DEBUG(sd);
}

void VM::DumpLeaks() {
    if (!dump_leaks) return;
    vector<void *> leaks = pool.findleaks();
    auto filename = "leaks.txt";
    if (leaks.empty()) {
        if (FileExists(filename)) FileDelete(filename);
    } else {
        LOG_ERROR("LEAKS FOUND (this indicates cycles in your object graph, or a bug in"
                             " Lobster)");
        sort(leaks.begin(), leaks.end(), _LeakSorter);
        PrintPrefs leakpp = debugpp;
        leakpp.cycles = 0;
        string sd;
        for (auto p : leaks) {
            auto ro = (RefObj *)p;
            switch(ro->ti(*this).t) {
                case V_VALUEBUF:
                    break;
                case V_STRING:
                case V_RESOURCE:
                case V_VECTOR:
                case V_CLASS: {
                    ro->CycleStr(sd);
                    sd += " = ";
                    RefToString(*this, sd, ro, leakpp);
                    #if DELETE_DELAY
                        append(sd, " ", (size_t)ro);
                    #endif
                    sd += "\n";
                    break;
                }
                default: assert(false);
            }
        }
        #ifndef NDEBUG
            LOG_ERROR(sd);
        #else
            if (leaks.size() < 50) {
                LOG_ERROR(sd);
            } else {
                LOG_ERROR(leaks.size(), " leaks, details in ", filename);
                WriteFile(filename, false, sd);
            }
        #endif
    }
    pool.printstats(false);
}


struct Stat {
    size_t num = 0;
    size_t bytes = 0;
    size_t max = 0;
    size_t gpu = 0;
    const ResourceType *rt = nullptr;

    void Add(size_t2 size, const ResourceType *_rt = nullptr) {
        num++;
        bytes += size.x + size.y;
        max = std::max(max, size.x + size.y);
        gpu += size.y;
        rt = _rt;
    }

    void Add(size_t size) {
        Add(size_t2(size, 0));
    }
};

static bool _UsageSorter(const pair<const void *, Stat> &a, const pair<const void *, Stat> &b) {
    return a.second.bytes != b.second.bytes ? a.second.bytes > b.second.bytes : false;
}

string VM::MemoryUsage(size_t show_max) {
    vector<void *> leaks = pool.findleaks();
    string sd;
    map<const void *, Stat> stats;
    for (auto p : leaks) {
        auto ro = (RefObj *)p;
        auto &ti = ro->ti(*this);
        switch(ti.t) {
            case V_VALUEBUF:
                break;
            case V_STRING:
                stats[&ti].Add(((LString *)ro)->MemoryUsage());
                break;
            case V_RESOURCE:
                stats[((LResource *)ro)->type].Add(((LResource *)ro)->MemoryUsage(),
                                                   ((LResource *)ro)->type);
                break;
            case V_VECTOR:
                stats[&ti].Add(((LVector *)ro)->MemoryUsage());
                break;
            case V_CLASS:
                stats[&ti].Add(((LObject *)ro)->MemoryUsage(*this));
                break;
            default:
                assert(false);
        }
    }
    vector<pair<const void *, Stat>> sorted;
    size_t total = 0;
    size_t totalgpu = 0;
    for (auto &p : stats) {
        sorted.push_back(p);
        total += p.second.bytes;
        totalgpu += p.second.gpu;
    }
    sort(sorted.begin(), sorted.end(), _UsageSorter);
    append(sd, "TOTAL: ", total / 1024, " K (", totalgpu * 100 / total, "% on GPU)\n");
    for (auto [i, p] : enumerate(sorted)) {
        if (i >= show_max || p.second.bytes < 1024) break;
        if (p.second.rt) append(sd, "resource<", p.second.rt->name, ">");
        else append(sd, ((const TypeInfo *)p.first)->Debug(*this, false));
        append(sd, ": ", p.second.bytes / 1024, " K in ", p.second.num, " objects");
        if (p.second.max >= 1024 && p.second.max != p.second.bytes / p.second.num) {
            append(sd, " (biggest: ", p.second.max / 1024, " K)");
        }
        if (p.second.gpu) {
            append(sd, " (", p.second.gpu * 100 / p.second.bytes, "% on GPU)");
        }
        append(sd, "\n");
    }
    return sd;
}

void VM::OnAlloc(RefObj *ro) {
    #if DELETE_DELAY
        LOG_DEBUG("alloc: ", (size_t)ro, " - ", ro->refc);
    #else
        (void)ro;
    #endif
}

#undef new

LVector *VM::NewVec(iint initial, iint max, type_elem_t tti) {
    assert(GetTypeInfo(tti).t == V_VECTOR);
    auto v = new (pool.alloc_small(sizeof(LVector))) LVector(*this, initial, max, tti);
    OnAlloc(v);
    return v;
}

LObject *VM::NewObject(iint max, type_elem_t tti) {
    assert(IsUDT(GetTypeInfo(tti).t));
    auto s = new (pool.alloc(ssizeof<LObject>() + ssizeof<Value>() * max)) LObject(tti);
    OnAlloc(s);
    return s;
}

LString *VM::NewString(iint l) {
    auto s = new (pool.alloc(ssizeof<LString>() + l + 1)) LString(l);
    OnAlloc(s);
    return s;
}

LResource *VM::NewResource(void *v, const ResourceType *t) {
    auto r = new (pool.alloc(sizeof(LResource))) LResource(v, t);
    if (t->newfun) t->newfun(r->val);
    OnAlloc(r);
    return r;
}

#ifdef _MSC_VER
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

LString *VM::ResizeString(LString *s, iint size, int c, bool back) {
    auto ns = NewString(size);
    auto sdest = (char *)ns->data();
    auto cdest = sdest;
    auto remain = size - s->len;
    if (back) sdest += remain;
    else cdest += s->len;
    memcpy(sdest, s->data(), (size_t)s->len);
    memset(cdest, c, (size_t)remain);
    s->Dec(*this);
    return ns;
}

void VM::ErrorBase(const string &err) {
    if (error_has_occured) {
        // We're calling this function recursively, not good. Try to get back to a reasonable
        // state by throwing an exception to be caught by the original error.
        errmsg = err;
        UnwindOnError();
    }
    error_has_occured = true;
    if (trace == TraceMode::TAIL && trace_output.size()) {
        for (size_t i = trace_ring_idx; i < trace_output.size(); i++) errmsg += trace_output[i];
        for (size_t i = 0; i < trace_ring_idx; i++) errmsg += trace_output[i];
        errmsg += err;
        UnwindOnError();
    }
    append(errmsg, "VM error (");
    if (last_line >= 0 && last_fileidx >= 0) {
        append(errmsg, bcf->filenames()->Get(last_fileidx)->string_view(), ":", last_line);
    } else {
        append(errmsg, programname);
    }
    append(errmsg, "): ", err);
}

int VM::DumpVar(string &sd, Value *x, int idx) {
    auto sid = bcf->specidents()->Get((uint32_t)idx);
    auto id = bcf->idents()->Get(sid->ididx());
    // FIXME: this is not ideal, it filters global "let" declared vars.
    // It should probably instead filter global let vars whose values are entirely
    // constructors, and which are never written to.
    auto name = id->name()->string_view();
    auto &ti = GetVarTypeInfo(idx);
    auto size = IsStruct(ti.t) ? ti.len : 1;
    //if (id->readonly() && id->global()) return size;
    append(sd, "        ", name);
    if (fvars[idx].True() && x->False()) {
        // Free vars live in fvars, but we can't tell which.
        // fvars are NIL when not in use, so swapping when not-nil is safe?
        x = &fvars[idx];
    }
    #if RTT_ENABLED
        if (ti.t != x->type && !IsStruct(ti.t)) {
            append(sd, ":");
            ti.Print(*this, sd);
            append(sd, " != ", BaseTypeName(x->type));
            return size;  // Likely uninitialized.
        }
    #endif
    append(sd, " = ");
    PrintPrefs minipp { 1, 20, true, -1 };
    if (IsStruct(ti.t)) {
        StructToString(sd, minipp, ti, x);
    } else {
        x->ToString(*this, sd, ti, minipp);
    }
    return size;
}

void VM::DumpStackTrace(string &sd) {
    if (fun_id_stack.empty()) return;

    #ifdef USE_EXCEPTION_HANDLING
    try {
    #endif

    if (!sd.empty()) append(sd, "\n");
    for (auto [fip, locals] : reverse(fun_id_stack)) {
        auto deffun = *fip++;
        append(sd, "in function: ", bcf->functions()->Get(deffun)->name()->string_view(), "(");
        fip++;  // regs_max
        auto nargs = *fip++;
        auto args = fip;
        fip += nargs;
        auto ndef = *fip++;
        fip += ndef;
        // auto defvars = fip;
        *fip++;  // nkeepvars
        if (nargs) append(sd, "\n");
        locals -= nargs;
        for (int j = 0; j < nargs;) {
            auto i = *(args + j);
            j += DumpVar(sd, locals + j, i);
            if (j < nargs) append(sd, ",\n");
        }
        append(sd, ")\n");
        // for (int j = 0; j < ndef;) {
        //    auto i = *(defvars - j - 1);
        //    j += DumpVar(sd, nullptr, i);
        //}
        fip++;  // Owned vars.
    }

    #ifdef USE_EXCEPTION_HANDLING
    } catch (string &s) {
        // Error happened while we were building this stack trace.
        // That may happen if the reason we're dumping the stack trace is because something got in an
        // inconsistent state in the first place.
        append(sd, "\nRECURSIVE ERROR:\n", s);
    }
    #endif
}

Value VM::Error(string err) {
    ErrorBase(err);
    DumpStackTrace(errmsg);
    UnwindOnError();
    return NilVal();
}

// Unlike Error above, this one does not attempt any variable dumping since the VM may already be
// in an inconsistent state.
Value VM::SeriousError(string err) {
    ErrorBase(err);
    UnwindOnError();
    return NilVal();
}

void VM::VMAssert(const char *what)  {
    SeriousError(string("VM internal assertion failure: ") + what);
}

void VM::EndEval(StackPtr &, const Value &ret, const TypeInfo &ti) {
    TerminateWorkers();
    ret.ToString(*this, evalret, ti, programprintprefs);
    ret.LTDECTYPE(*this, ti.t);
    for (auto s : constant_strings) {
        if (s) s->Dec(*this);
    }
    while (!delete_delay.empty()) {
        auto ro = delete_delay.back();
        delete_delay.pop_back();
        ro->DECDELETENOW(*this);
    }
    DumpLeaks();
}

void VM::UnwindOnError() {
    // This is the single location from which we unwind the execution stack from within the VM.
    // This requires special care, because there may be jitted code on the stack, and depending
    // on the platform we can use exception handling, or not.
    // This code is only needed upon error, the regular execution path uses normal returns.
    #if VM_USE_LONGJMP
        // We are in JIT mode, and on a platform that cannot throw exceptions "thru" C code,
        // e.g. Linux.
        // To retain modularity (allow the VM to be used in an environment where a VM error
        // shouldn't terminate the whole app) we try to work around this with setjmp/longjmp.
        // This does NOT call destructors on the way, so code calling into here should make sure
        // to not require these.
        // Though even if there are some, a small memory leak upon a VM error is probably
        // preferable to aborting when modularity is needed.
        // FIXME: audit calling code for destructors. Can we automatically enforce this?
        longjmp(jump_buffer, 1);
        // The corresponding setjmp is right below here.
    #else
        // Use the standard error mechanism, which uses exceptions (on Windows, or other platforms
        // when not JIT-ing) or aborts (Wasm).
        THROW_OR_ABORT(errmsg);
    #endif
}

void VM::EvalProgram() {
    #if VM_USE_LONGJMP
        // See longjmp above for why this is needed.
        if (setjmp(jump_buffer)) {
            // Resume normal error now that we've jumped past the C/JIT-ted code.
            THROW_OR_ABORT(errmsg);
        }
    #endif
    #if VM_JIT_MODE
        jit_entry(*this, nullptr);
    #else
        compiled_entry_point(*this, nullptr);
    #endif
}

string &VM::TraceStream() {
  size_t trace_size = trace == TraceMode::TAIL ? 50 : 1;
  if (trace_output.size() < trace_size) trace_output.resize(trace_size);
  if (trace_ring_idx == trace_size) trace_ring_idx = 0;
  auto &sd = trace_output[trace_ring_idx++];
  sd.clear();
  return sd;
}

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

void VM::BCallRetCheck(StackPtr sp, const NativeFun *nf) {
    #if RTT_ENABLED
        // See if any builtin function is lying about what type it returns
        // other function types return intermediary values that don't correspond to final return
        // values.
        for (size_t i = 0; i < nf->retvals.size(); i++) {
            #ifndef NDEBUG
            auto t = (TopPtr(sp) - nf->retvals.size() + i)->type;
            auto u = nf->retvals[i].type->t;
            assert(t == u || u == V_ANY || u == V_NIL || (u == V_VECTOR && IsUDT(t)));
            #endif
        }
    #else
        (void)nf;
        (void)sp;
    #endif
}

iint VM::GrabIndex(StackPtr &sp, int len) {
    auto &v = TopM(sp, len);
    for (len--; ; len--) {
        auto sidx = Pop(sp).ival();
        if (!len) return sidx;
        RANGECHECK((*this), sidx, v.vval()->len, v.vval());
        v = v.vval()->At(sidx);
    }
}

void VM::IDXErr(iint i, iint n, const RefObj *v) {
    string sd;
    append(sd, "index ", i, " out of range ", n, " of: ");
    RefToString(*this, sd, v, debugpp);
    Error(sd);
}

string_view VM::StructName(const TypeInfo &ti) {
    return bcf->udts()->Get(ti.structidx)->name()->string_view();
}

string_view VM::ReverseLookupType(int v) {
    return bcf->udts()->Get((flatbuffers::uoffset_t)v)->name()->string_view();
}

string_view VM::LookupField(int stidx, iint fieldn) const {
    auto st = bcf->udts()->Get((flatbuffers::uoffset_t)stidx);
    return st->fields()->Get((flatbuffers::uoffset_t)fieldn)->name()->string_view();
}

bool VM::EnumName(string &sd, iint enum_val, int enumidx) {
    auto enum_def = bcf->enums()->Get(enumidx);
    auto &vals = *enum_def->vals();
    auto lookup = [&](iint val) -> bool {
        // FIXME: can store a bool that says whether this enum is contiguous, so we just index instead.
        for (auto v : vals)
            if (v->val() == val) {
                sd += v->name()->string_view();
                return true;
            }
        return false;
    };
    if (!enum_def->flags() || !enum_val) return lookup(enum_val);
    auto start = sd.size();
    auto upto = 64 - HighZeroBits(enum_val);
    for (int i = 0; i < upto; i++) {
        auto bit = enum_val & (1LL << i);
        if (bit) {
            if (sd.size() != start) sd += "|";
            if (!lookup(bit)) {
                // enum contains unknown bits, so can't display this properly.
                sd.resize(start);
                return false;
            }
        }
    }
    return true;
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

void VM::StartWorkers(iint numthreads) {
    if (is_worker) Error("workers can\'t start more worker threads");
    if (tuple_space) Error("workers already running");
    // Stop bad values from locking up the machine :)
    numthreads = std::min(numthreads, 256_L);
    tuple_space = new TupleSpace(bcf->udts()->size());
    for (iint i = 0; i < numthreads; i++) {
        // Create a new VM that should own all its own memory and be completely independent
        // from this one.
        // We share nfr and programname for now since they're fully read-only.
        // FIXME: have to copy bytecode buffer even though it is read-only.
        auto vmargs = *(VMArgs *)this;
        vmargs.program_args.resize(0);
        vmargs.trace = TraceMode::OFF;
        auto vma = new VMAllocator(std::move(vmargs));
        vma->vm->is_worker = true;
        vma->vm->tuple_space = tuple_space;
        workers.emplace_back([vma] {
            string err;
            #ifdef USE_EXCEPTION_HANDLING
            try
            #endif
            {
                vma->vm->EvalProgram();
            }
            #ifdef USE_EXCEPTION_HANDLING
            catch (string &s) {
                err = s;
            }
            #endif
            delete vma;
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
    // Use malloc instead of pool, since this is being sent to another thread.
    auto buf = (Value *)malloc(sizeof(Value) * ti.len);
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
    ns->CopyElemsShallow(buf, ti.len);
    free(buf);
    return ns;
}

}  // namespace lobster


// Make VM ops available as C functions for linking purposes:

extern "C" {

using namespace lobster;

void TraceIL(VM *vm, StackPtr sp, initializer_list<int> _ip) {
    auto ip = _ip.begin();
    auto &sd = vm->TraceStream();
    DisAsmIns(vm->nfr, sd, ip, nullptr, (type_elem_t *)vm->bcf->typetable()->data(), vm->bcf,
              vm->last_line);
    #if RTT_ENABLED
        (void)sp;
        /*
        if (sp >= vm->stack) {
            sd += " - ";
            Top(sp).ToStringBase(*vm, sd, Top(sp).type, vm->debugpp);
            if (sp > vm->stack) {
                sd += " - ";
                TopM(sp, 1).ToStringBase(*vm, sd, TopM(sp, 1).type, vm->debugpp);
            }
        }
        */
    #else
        (void)sp;
    #endif
    // append(sd, " / ", (size_t)Top(sp).any());
    // for (int _i = 0; _i < 7; _i++) { append(sd, " #", (size_t)vm->vars[_i].any()); }
    if (vm->trace == TraceMode::TAIL) sd += "\n"; else LOG_PROGRAM(sd);
}

void TraceVA(VM *vm, StackPtr, int opc, int fid) {
    auto &sd = vm->TraceStream();
    sd += "\t";
    sd += ILNames()[opc];
    if (opc == IL_FUNSTART && fid >= 0) {
        sd += " ";
        sd += vm->bcf->functions()->Get(fid)->name()->string_view();
    }
    if (vm->trace == TraceMode::TAIL) sd += "\n"; else LOG_PROGRAM(sd);
}

#ifndef NDEBUG
    #define CHECK(B) if (vm->trace != TraceMode::OFF) TraceIL(vm, sp, {B});
    #define CHECKVA(OPC, FID) if (vm->trace != TraceMode::OFF) TraceVA(vm, sp, OPC, FID);
#else
    #define CHECK(B)
    #define CHECKVA(OPC, FID)
#endif

fun_base_t CVM_GetNextCallTarget(VM *vm) {
    return vm->next_call_target;
}

void CVM_Entry(int value_size) {
    if (value_size != sizeof(Value)) {
        THROW_OR_ABORT("INTERNAL ERROR: C <-> C++ Value size mismatch!");
    }
}

void CVM_SwapVars(VM *vm, int i, StackPtr psp, int off) { SwapVars(*vm, i, psp, off); }
void CVM_BackupVar(VM *vm, int i) { BackupVar(*vm, i); }
void CVM_NilVal(Value *d) { *d = NilVal(); }
void CVM_DecOwned(VM *vm, int i) { DecOwned(*vm, i); }
void CVM_DecVal(VM *vm, Value v) { DecVal(*vm, v); }
void CVM_RestoreBackup(VM *vm, int i) { RestoreBackup(*vm, i); }
StackPtr CVM_PopArg(VM *vm, int i, StackPtr psp) { return PopArg(*vm, i, psp); }
void CVM_SetLVal(VM *vm, Value *v) { SetLVal(*vm, v); }
int CVM_RetSlots(VM *vm) { return RetSlots(*vm); }
void CVM_PushFunId(VM *vm, const int *id, StackPtr locals) { PushFunId(*vm, id, locals); }
void CVM_PopFunId(VM *vm) { PopFunId(*vm); }

#define F(N, A, USE, DEF) \
    void CVM_##N(VM *vm, StackPtr sp VM_COMMA_IF(A) VM_OP_ARGSN(A)) { \
        CHECK(IL_##N VM_COMMA_1 0 VM_COMMA_IF(A) VM_OP_PASSN(A));         \
        return U_##N(*vm, sp VM_COMMA_IF(A) VM_OP_PASSN(A));              \
    }
ILBASENAMES
#undef F
#define F(N, A, USE, DEF) \
    void CVM_##N(VM *vm, StackPtr sp VM_COMMA_IF(A) VM_OP_ARGSN(A), fun_base_t fcont) { \
        CHECK(IL_##N VM_COMMA_1 0 VM_COMMA_IF(A) VM_OP_PASSN(A));                           \
        return U_##N(*vm, sp, VM_OP_PASSN(A) VM_COMMA_IF(A) fcont);                         \
    }
ILCALLNAMES
#undef F
#define F(N, A, USE, DEF) \
    void CVM_##N(VM *vm, StackPtr sp VM_COMMA_IF(A) VM_OP_ARGSN(A)) { \
        CHECKVA(IL_##N, *ip);                                             \
        return U_##N(*vm, sp VM_COMMA_IF(A) VM_OP_PASSN(A));              \
    }
ILVARARGNAMES
#undef F
#define F(N, A, USE, DEF) \
    int CVM_##N(VM *vm, StackPtr sp) {                     \
        CHECK(IL_##N VM_COMMA_1 0 VM_COMMA_1 0 /*FIXME*/); \
        return U_##N(*vm, sp);                             \
    }
ILJUMPNAMES1
#undef F
#define F(N, A, USE, DEF) \
    int CVM_##N(VM *vm, StackPtr sp, int df) {                           \
        CHECK(IL_##N VM_COMMA_1 0 VM_COMMA_1 df VM_COMMA_1 0 /*FIXME*/); \
        return U_##N(*vm, sp, df);                                       \
    }
ILJUMPNAMES2
#undef F

#if VM_JIT_MODE

#if LOBSTER_ENGINE
extern "C" void GLFrame(StackPtr sp, VM & vm);
#endif

const void *vm_ops_jit_table[] = {
    #define F(N, A, USE, DEF) "U_" #N, (void *)&CVM_##N,
        ILNAMES
    #undef F
    "GetNextCallTarget", (void *)CVM_GetNextCallTarget,
    "Entry", (void *)CVM_Entry,
    "SwapVars", (void *)CVM_SwapVars,
    "BackupVar", (void *)CVM_BackupVar,
    "NilVal", (void *)CVM_NilVal,
    "DecOwned", (void *)CVM_DecOwned,
    "DecVal", (void *)CVM_DecVal,
    "RestoreBackup", (void *)CVM_RestoreBackup,
    "PopArg", (void *)CVM_PopArg,
    "SetLVal", (void *)CVM_SetLVal,
    "RetSlots", (void *)CVM_RetSlots,
    "PushFunId", (void *)CVM_PushFunId,
    "PopFunId", (void *)CVM_PopFunId,
    #if LOBSTER_ENGINE
    "GLFrame", (void *)GLFrame,
    #endif
    0, 0
};
#endif

}  // extern "C"

