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

namespace lobster {

#define FLATBUFFERS_DEBUG_VERIFICATION_FAILURE
#include "lobster/bytecode_generated.h"

struct CodeGen  {
    vector<int> code;
    vector<bytecode::LineInfo> lineinfo;
    vector<bytecode::SpecIdent> sids;
    Parser &parser;
    vector<const Node *> linenumbernodes;
    vector<tuple<int, const SubFunction *>> call_fixups;
    SymbolTable &st;
    vector<type_elem_t> type_table;
    map<vector<type_elem_t>, type_elem_t> type_lookup;  // Wasteful, but simple.
    vector<TypeLT> rettypes, temptypestack;
    vector<const Node *> loops;
    vector<int> breaks;
    vector<string_view> stringtable;  // sized strings.
    vector<const Node *> node_context;
    int keepvars = 0;
    int runtime_checks;
    vector<int> vtables;
    vector<ILOP> tstack;
    size_t tstack_max = 0;
    int dummyfun = -1;
    const SubFunction *cursf = nullptr;

    int Pos() { return (int)code.size(); }

    void Emit(int i) {
        auto &ln = linenumbernodes.back()->line;
        if (lineinfo.empty() || ln.line != lineinfo.back().line() ||
            ln.fileidx != lineinfo.back().fileidx())
            lineinfo.push_back(bytecode::LineInfo(ln.line, ln.fileidx, Pos()));
        code.push_back(i);
    }

    int TempStackSize() {
        return (int)tstack.size();
    }

    ILOP PopTemp() {
        assert(!tstack.empty());
        auto op = tstack.back();
        tstack.pop_back();
        return op;
    }

    void PushTemp(ILOP op) {
        tstack.push_back(op);
        tstack_max = std::max(tstack_max, tstack.size());
    }

    struct BlockStack {
        vector<ILOP> &tstack;
        size_t start;
        size_t max;
        BlockStack(vector<ILOP> &s) : tstack(s), start(s.size()), max(s.size()) {}
        void Start() { tstack.resize(start); }
        void End() { max = std::max(max, tstack.size()); }
        void Exit(CodeGen &cg) {
            assert(max >= tstack.size());
            while (tstack.size() < max) {
                // A value from something that doesn't return.
                cg.PushTemp(IL_EXIT);
            }
        }
    };

    void EmitOp(ILOP op, int useslots = ILUNKNOWN, int defslots = ILUNKNOWN) {
        Emit(op);
        Emit(TempStackSize());

        auto uses = ILUses()[op];
        if (uses == ILUNKNOWN) {
            assert(useslots != ILUNKNOWN);
            uses = useslots;
        }
        for (int i = 0; i < uses; i++) PopTemp();

        auto defs = ILDefs()[op];
        if (defs == ILUNKNOWN) {
            assert(defslots != ILUNKNOWN);
            defs = defslots;
        }
        for (int i = 0; i < defs; i++) { PushTemp(op); }

        //LOG_DEBUG("cg: ", ILNames()[op], " ", uses, "/", defs, " -> ", tstack.size());
    }

    void SetLabelNoBlockStart(int jumploc) {
        code[jumploc - 1] = Pos();
    }

    void SetLabel(int jumploc) {
        SetLabelNoBlockStart(jumploc);
        EmitOp(IL_BLOCK_START);
    }

    void SetLabels(vector<int> &jumplocs) {
        if (jumplocs.empty()) return;
        for (auto jl : jumplocs) SetLabelNoBlockStart(jl);
        jumplocs.clear();
        EmitOp(IL_BLOCK_START);
    }

    const int ti_num_udt_fields = 4;
    const int ti_num_udt_per_field = 2;

    void PushFields(UDT *udt, vector<type_elem_t> &tt, type_elem_t parent = (type_elem_t)-1) {
        for (auto &field : udt->fields) {
            auto ti = GetTypeTableOffset(field.resolvedtype());
            if (IsStruct(field.resolvedtype()->t)) {
                PushFields(field.resolved_udt(), tt, parent < 0 ? ti : parent);
            } else {
                tt.insert(tt.begin() + (ssize(tt) - ti_num_udt_fields) / ti_num_udt_per_field +
                          ti_num_udt_fields, ti);
                tt.push_back(parent);
            }
        }
    }

    // Make a table for use as VM runtime type.
    type_elem_t GetTypeTableOffset(TypeRef type) {
        vector<type_elem_t> tt;
        tt.push_back((type_elem_t)type->t);
        switch (type->t) {
            case V_INT:
                tt.push_back((type_elem_t)(type->e ? type->e->idx : -1));
                break;
            case V_NIL:
            case V_VECTOR:
                tt.push_back(GetTypeTableOffset(type->sub));
                break;
            case V_FUNCTION:
                tt.push_back((type_elem_t)type->sf->idx);
                break;
            case V_CLASS:
            case V_STRUCT_R:
            case V_STRUCT_S: {
                if (type->udt->typeinfo >= 0)
                    return type->udt->typeinfo;
                type->udt->typeinfo = (type_elem_t)type_table.size();
                // Reserve space, so other types can be added afterwards safely.
                assert(type->udt->numslots >= 0);
                auto ttsize = (type->udt->numslots * ti_num_udt_per_field) + ti_num_udt_fields;
                type_table.insert(type_table.end(), ttsize, (type_elem_t)0);
                tt.push_back((type_elem_t)type->udt->idx);
                tt.push_back((type_elem_t)type->udt->numslots);
                if (type->t == V_CLASS)
                    tt.push_back((type_elem_t)type->udt->vtable_start);
                else
                    tt.push_back((type_elem_t)ComputeBitMask(*type->udt, nullptr));
                PushFields(type->udt, tt);
                assert(ssize(tt) == ttsize);
                std::copy(tt.begin(), tt.end(), type_table.begin() + type->udt->typeinfo);
                return type->udt->typeinfo;
            }
            case V_VAR:
                // This should really not happen anymore, but if it does, it is probably an unused
                // value.
                assert(false);
                return GetTypeTableOffset(type_any);
            default:
                assert(IsRuntime(type->t));
                break;
        }
        // For everything that's not a struct:
        auto it = type_lookup.find(tt);
        if (it != type_lookup.end()) return it->second;
        auto offset = (type_elem_t)type_table.size();
        type_lookup[tt] = offset;
        type_table.insert(type_table.end(), tt.begin(), tt.end());
        return offset;
    }

    CodeGen(Parser &_p, SymbolTable &_st, bool return_value, int runtime_checks)
        : parser(_p), st(_st), runtime_checks(runtime_checks) {
        // Reserve space and index for all vtables.
        for (auto udt : st.udttable) {
            udt->vtable_start = (int)vtables.size();
            vtables.insert(vtables.end(), udt->dispatch.size(), -1);
        }
        // Pre-load some types into the table, must correspond to order of type_elem_t enums.
                                                            GetTypeTableOffset(type_int);
                                                            GetTypeTableOffset(type_float);
                                                            GetTypeTableOffset(type_string);
                                                            GetTypeTableOffset(type_resource);
                                                            GetTypeTableOffset(type_any);
        Type type_valuebuf(V_VALUEBUF);                     GetTypeTableOffset(&type_valuebuf);
                                                            GetTypeTableOffset(type_vector_int);
                                                            GetTypeTableOffset(type_vector_float);
        Type type_vec_str(V_VECTOR, &*type_string);         GetTypeTableOffset(&type_vec_str);
        Type type_v_v_int(V_VECTOR, &*type_vector_int);     GetTypeTableOffset(&type_v_v_int);
        Type type_v_v_float(V_VECTOR, &*type_vector_float); GetTypeTableOffset(&type_v_v_float);
                                                            GetTypeTableOffset(type_vector_resource);
        assert(type_table.size() == TYPE_ELEM_FIXED_OFFSET_END);
        for (auto f : parser.st.functiontable) {
            if (!f->istype) {
                for (auto &ov : f->overloads) for (auto sf = ov.sf; sf; sf = sf->next) {
                    if (sf->typechecked) {
                        // We only set this here, because any inlining of anonymous functions in
                        // the optimizers is likely to reduce the amount of vars for which this is
                        // true a great deal.
                        for (auto &fv : sf->freevars) fv.sid->used_as_freevar = true;
                    }
                }
            }
        }
        int sidx = 0;
        for (auto sid : st.specidents) {
            if (!sid->type.Null()) {  // Null ones are in unused functions.
                auto tti = GetTypeTableOffset(sid->type);
                assert(!IsStruct(sid->type->t) || sid->type->udt->numslots >= 0);
                sid->sidx = sidx;
                auto ns = ValWidth(sid->type);
                sidx += ns;
                for (int i = 0; i < ns; i++)
                    sids.push_back(bytecode::SpecIdent(sid->id->idx, tti, sid->used_as_freevar));
            }
        }

        // Start of the actual bytecode.
        linenumbernodes.push_back(parser.root);
        EmitOp(IL_JUMP);
        Emit(0);
        auto fundefjump = Pos();

        // Generate a dummmy function for function values that are never called.
        // Would be good if the optimizer guarantees these don't exist, but for now this is
        // more debuggable if it does happen to get called.
        dummyfun = Pos();
        EmitOp(IL_FUNSTART);
        Emit(-1);  // funid
        Emit(0);   // regs_max
        Emit(0);
        Emit(0);
        Emit(0);   // keepvars
        Emit(0);   // ownedvars
        EmitOp(IL_ABORT);

        // Generate all used functions.
        for (auto f : parser.st.functiontable) {
            if (f->bytecodestart <= 0 && !f->istype) {
                f->bytecodestart = Pos();
                for (auto &ov : f->overloads) for (auto sf = ov.sf; sf; sf = sf->next) {
                    if (sf->typechecked) GenScope(*sf);
                }
                if (f->bytecodestart == Pos()) f->bytecodestart = 0;
            }
        }

        // Emit the root function.
        SetLabelNoBlockStart(fundefjump);
        Gen(parser.root, return_value);
        auto type = parser.root->exptype;
        assert(type->NumValues() == (size_t)return_value);
        EmitOp(IL_EXIT, int(return_value));
        Emit(return_value ? GetTypeTableOffset(type) : -1);
        linenumbernodes.pop_back();

        // Fix up all calls.
        for (auto &[loc, sf] : call_fixups) {
            auto bytecodestart = sf->subbytecodestart;
            if (!bytecodestart)
                bytecodestart = dummyfun;
            assert(!code[loc]);
            code[loc] = bytecodestart;
        }

        // Now fill in the vtables.
        for (auto udt : st.udttable) {
            for (auto [i, de] : enumerate(udt->dispatch)) {
                if (de.sf) {
                    assert(de.sf->subbytecodestart);
                    vtables[udt->vtable_start + i] = de.sf->subbytecodestart;
                }
            }
        }
    }

    ~CodeGen() {
    }

    // FIXME: remove.
    void Dummy(size_t retval) {
        assert(!retval);
        while (retval--) EmitOp(IL_PUSHNIL);
    }

    void GenScope(SubFunction &sf) {
        if (sf.subbytecodestart > 0) return;
        cursf = &sf;
        keepvars = 0;
        tstack_max = 0;
        sf.subbytecodestart = Pos();
        if (!sf.typechecked) {
            auto s = DumpNode(*sf.sbody, 0, false);
            LOG_DEBUG("untypechecked: ", sf.parent->name, " : ", s);
            assert(0);
        }
        vector<int> ownedvars;
        linenumbernodes.push_back(sf.sbody);
        EmitOp(IL_FUNSTART);
        Emit(sf.parent->idx);
        auto regspos = Pos();
        Emit(0);
        auto ret = AssertIs<Return>(sf.sbody->children.back());
        auto ir = sf.consumes_vars_on_return ? AssertIs<IdentRef>(ret->child) : nullptr;
        auto emitvars = [&](const vector<Arg> &v) {
            auto nvarspos = Pos();
            Emit(0);
            auto nvars = 0;
            for (auto &arg : v) {
                auto n = ValWidth(arg.sid->type);
                for (int i = 0; i < n; i++) {
                    Emit(arg.sid->Idx() + i);
                    nvars++;
                    if (ShouldDec(IsStruct(arg.sid->type->t)
                                      ? TypeLT{ FindSlot(*arg.sid->type->udt, i)->resolvedtype(),
                                             arg.sid->lt }
                                      : TypeLT { *arg.sid }) && (!ir || arg.sid != ir->sid)) {
                        ownedvars.push_back(arg.sid->Idx() + i);
                    }
                }
            }
            code[nvarspos] = nvars;
        };
        emitvars(sf.args);
        emitvars(sf.locals);
        auto keepvarspos = Pos();
        Emit(0);
        // FIXME: don't really want to emit these.. instead should ensure someone takes
        // ownership of them.
        Emit((int)ownedvars.size());
        for (auto si : ownedvars) Emit(si);
        if (sf.sbody) for (auto c : sf.sbody->children) {
            Gen(c, 0);
            if (runtime_checks >= RUNTIME_ASSERT_PLUS) {
                EmitOp(IL_ENDSTATEMENT);
                Emit(c->line.line);
                Emit(c->line.fileidx);
                assert(tstack.empty());
            }
        }
        else Dummy(sf.reqret);
        assert(temptypestack.empty());
        assert(breaks.empty());
        assert(tstack.empty());
        code[regspos] = (int)tstack_max;
        code[keepvarspos] = keepvars;
        linenumbernodes.pop_back();
        cursf = nullptr;
    }

    // This must be called explicitly when any values are consumed.
    void TakeTemp(size_t n, bool can_handle_structs) {
        assert(node_context.size());
        for (; n; n--) {
            auto tlt = temptypestack.back();
            temptypestack.pop_back();
            assert(can_handle_structs || ValWidth(tlt.type) == 1); (void)tlt;
            (void)can_handle_structs;
        }
    }

    void GenFixup(const SubFunction *sf) {
        assert(sf->sbody);
        auto pos = Pos() - 1;
        if (!code[pos]) call_fixups.push_back({ pos, sf });
    }

    void GenUnwind(const SubFunction &sf, int nretslots_unwind_max, int nretslots_norm) {
        // We're in an odd position here, because what is on the stack can either be from
        // the function we're calling (if we're not falling thru) or from any function above it
        // with different number of return values (and there can be multiple such paths, with
        // different retvals, hence "max").
        // Then, below it, may be temps.
        // If we're falling thru, we actually want to 1) unwind, 2) copy rets, 3) pop temps
        // We manage the tstack as if we're not falling thru.
        // Need to ensure there's enough space for either path.
        for (int i = nretslots_norm; i < nretslots_unwind_max; i++)
            PushTemp(IL_CALL);
        EmitOp(IL_JUMPIFUNWOUND);
        Emit(sf.parent->idx);
        Emit(0);
        for (int i = nretslots_norm; i < nretslots_unwind_max; i++)
            PopTemp();
        auto loc = Pos();
        auto tstackbackup = tstack;
        EmitOp(IL_RETURNANY);
        Emit(nretslots_norm);
        for (auto &tse : reverse(temptypestack)) {
            GenPop(tse);
        }
        EmitOp(IL_SAVERETS);
        SetLabel(loc);
        tstack = tstackbackup;
    }

    void GenCall(const Call &call, size_t retval) {
        auto &sf = *call.sf;
        auto &f = *sf.parent;
        int inw = 0;
        int outw = ValWidthMulti(sf.returntype, sf.returntype->NumValues());
        for (auto c : call.children) {
            Gen(c, 1);
            inw += ValWidth(c->exptype);
        }
        size_t nargs = call.children.size();
        if (f.nargs() != nargs)
            parser.ErrorAt(node_context.back(),
                           "call to function ", Q(f.name), " needs ", f.nargs(),
                           " arguments, ", nargs, " given");
        TakeTemp(nargs, true);
        if (call.vtable_idx < 0) {
            EmitOp(IL_CALL, inw, outw);
            Emit(sf.subbytecodestart);
            GenFixup(&sf);
            if (sf.returned_thru_to_max >= 0) {
                GenUnwind(sf, sf.returned_thru_to_max, outw);
            }
        } else {
            EmitOp(IL_DDCALL, inw, outw);
            Emit(call.vtable_idx);
            Emit(inw - 1);
            // We get the dispatch from arg 0, since sf is an arbitrary overloads and
            // doesn't necessarily point to the dispatch root (which may not even have an sf).
            auto dispatch_type = call.children[0]->exptype;
            assert(IsUDT(dispatch_type->t));
            auto &de = dispatch_type->udt->dispatch[call.vtable_idx];
            assert(de.is_dispatch_root && !de.returntype.Null() && de.subudts_size);
            if (de.returned_thru_to_max >= 0) {
                // This works because all overloads of a DD sit under a single Function.
                GenUnwind(sf, de.returned_thru_to_max, outw);
            }
        }
        auto nretvals = sf.returntype->NumValues();
        for (size_t i = 0; i < nretvals; i++) {
            if (retval) {
                rettypes.push_back({ sf, i });
            } else {
                // FIXME: better if this is impossible by making sure typechecker makes it !reqret.
                GenPop({ sf, i });
            }
        }
        for (size_t i = nretvals; i < retval; i++) {
            // This can happen in a function that ends in a non-local return (thus nretvals==0)
            // but retval>0 because it is inside an if-then-else branch.
            // FIXME: take care of this in Gen() instead? Are there other nodes for which this
            // can happen?
            PushTemp(IL_EXIT);
        }
    };

    void GenFloat(double f) {
        if ((float)f == f) {
            int2float i2f;
            i2f.f = (float)f;
            EmitOp(IL_PUSHFLT);
            Emit(i2f.i);
        } else {
            int2float64 i2f;
            i2f.f = f;
            EmitOp(IL_PUSHFLT64);
            Emit((int)i2f.i);
            Emit((int)(i2f.i >> 32));
        }
    }

    bool ShouldDec(TypeLT typelt) {
        return IsRefNil(typelt.type->t) && typelt.lt == LT_KEEP;
    }

    void GenPop(TypeLT typelt) {
        if (IsStruct(typelt.type->t)) {
            if (typelt.type->t == V_STRUCT_R) {
                // TODO: alternatively emit a single op with a list or bitmask? see EmitBitMaskForRefStuct
                for (int j = typelt.type->udt->numslots - 1; j >= 0; j--) {
                    EmitOp(IsRefNil(FindSlot(*typelt.type->udt, j)->resolvedtype()->t) ? IL_POPREF
                                                                                     : IL_POP);
                }
            } else {
                EmitOp(IL_POPV, typelt.type->udt->numslots, 0);
                Emit(typelt.type->udt->numslots);
            }
        } else {
            EmitOp(ShouldDec(typelt) ? IL_POPREF : IL_POP);
        }
    }

    void GenDup(TypeLT tlt) {
        EmitOp(IL_DUP);
        temptypestack.push_back(tlt);
    }

    void Gen(const Node *n, size_t retval) {
        // Generate() below generate no retvals if retval==0, otherwise they generate however many
        // they can irrespective of retval, optionally record that in rettypes for the more complex
        // cases. Then at the end of this function the two get matched up.
        auto tempstartsize = temptypestack.size();
        linenumbernodes.push_back(n);

        node_context.push_back(n);
        n->Generate(*this, retval);
        node_context.pop_back();

        assert(n->exptype->t != V_UNDEFINED);
        assert(!n->exptype->HasValueType(V_VAR));

        assert(tempstartsize == temptypestack.size());
        (void)tempstartsize;
        // If 0, the above code already made sure to not generate value(s).
        if (retval) {
            // default case, no rettypes specified.
            if (rettypes.empty()) {
                for (size_t i = 0; i < n->exptype->NumValues(); i++)
                    rettypes.push_back(TypeLT { *n, i });
            }
            // if the caller doesn't want all return values, just pop em
            if (rettypes.size() > retval) {
                while (rettypes.size() > retval) {
                    GenPop(rettypes.back());
                    rettypes.pop_back();
                }
            }
            assert(rettypes.size() == retval);
            // Copy return types on temp stack.
            while (rettypes.size()) {
                temptypestack.push_back(rettypes.front());
                rettypes.erase(rettypes.begin());
            }
        }
        assert(rettypes.empty());
        linenumbernodes.pop_back();
    }

    void EmitWidthIfStruct(TypeRef type) {
        if (IsStruct(type->t)) Emit(ValWidth(type));
    }

    int ComputeBitMask(const UDT &udt, const Node *errloc) {
        int bits = 0;
        for (int j = 0; j < udt.numslots; j++) {
            if (IsRefNil(FindSlot(udt, j)->resolvedtype()->t)) {
                if (j > 31)
                    parser.ErrorAt(errloc, "internal error: struct with too many reference fields");
                bits |= 1 << j;
            }
        }
        return bits;
    }

    void EmitBitMaskForRefStuct(TypeRef type, const Node *errloc) {
        assert(type->t == V_STRUCT_R);
        Emit(ComputeBitMask(*type->udt, errloc));
    }

    void GenValueWidth(TypeRef type) {
        // FIXME: struct variable size.
        EmitOp(IL_PUSHINT);
        Emit(ValWidth(type));
    }

    void GenAssign(const Node *lval, ILOP lvalop, size_t retval,
                   const Node *rhs, int take_temp, bool post) {
        assert(node_context.back()->exptype->NumValues() >= retval);
        auto type = lval->exptype;
        if (lvalop >= IL_LV_IADD && lvalop <= IL_LV_IMOD) {
            if (type->t == V_INT) {
            } else if (type->t == V_FLOAT) {
                assert(lvalop != IL_LV_IMOD); lvalop = GENOP(lvalop + (IL_LV_FADD - IL_LV_IADD));
            } else if (type->t == V_STRING) {
                assert(lvalop == IL_LV_IADD); lvalop = IL_LV_SADD;
            } else if (type->t == V_STRUCT_S) {
                auto sub = type->udt->sametype;
                bool withscalar = IsScalar(rhs->exptype->t);
                if (sub->t == V_INT) {
                    lvalop = GENOP(lvalop + ((withscalar ? IL_LV_IVSADD : IL_LV_IVVADD) - IL_LV_IADD));
                } else if (sub->t == V_FLOAT) {
                    assert(lvalop != IL_LV_IMOD);
                    lvalop = GENOP(lvalop + ((withscalar ? IL_LV_FVSADD : IL_LV_FVVADD) - IL_LV_IADD));
                } else assert(false);
            } else {
                assert(false);
            }
        } else if (lvalop >= IL_LV_IPP && lvalop <= IL_LV_IMM) {
            if (type->t == V_FLOAT) lvalop = GENOP(lvalop + (IL_LV_FPP - IL_LV_IPP));
            else assert(type->t == V_INT);
        }
        if (rhs) Gen(rhs, 1);
        auto GenLvalRet = [&](TypeRef lvt) {
            if (!post) {
                EmitOp(lvalop, ValWidth(lval->exptype));
                EmitWidthIfStruct(lvt);
                if (lvalop == IL_LV_WRITEREFV) EmitBitMaskForRefStuct(lvt, lval);
            }
            if (retval) {
                // FIXME: it seems these never need a refcount increase because they're always
                // borrowed? Be good to assert that somehow.
                auto outw = ValWidth(lvt);
                EmitOp(IsStruct(lvt->t) ? IL_LV_DUPV : IL_LV_DUP, 0, outw);
                EmitWidthIfStruct(lvt);
            }
            if (post) {
                EmitOp(lvalop, ValWidth(lval->exptype));
                EmitWidthIfStruct(lvt);
                if (lvalop == IL_LV_WRITEREFV) EmitBitMaskForRefStuct(lvt, lval);
            }
        };
        if (auto idr = Is<IdentRef>(lval)) {
            TakeTemp(take_temp, true);
            GenLvalVar(*idr->sid);
            GenLvalRet(idr->sid->type);
        } else if (auto dot = Is<Dot>(lval)) {
            auto stype = dot->child->exptype;
            assert(IsUDT(stype->t));  // Ensured by typechecker.
            auto idx = stype->udt->Has(dot->fld);
            assert(idx >= 0);
            auto &field = stype->udt->fields[idx];
            Gen(dot->child, 1);
            TakeTemp(take_temp + 1, true);
            EmitOp(IL_LVAL_FLD);
            Emit(field.slot);
            GenLvalRet(field.resolvedtype());
        } else if (auto indexing = Is<Indexing>(lval)) {
            Gen(indexing->object, 1);
            Gen(indexing->index, 1);
            TakeTemp(take_temp + 2, true);
            switch (indexing->object->exptype->t) {
                case V_VECTOR:
                    EmitOp(indexing->index->exptype->t == V_INT ? IL_LVAL_IDXVI : IL_LVAL_IDXVV,
                           ValWidth(indexing->index->exptype) + 1);
                    EmitWidthIfStruct(indexing->index->exptype);  // When index is struct.
                    GenLvalRet(type);
                    break;
                case V_CLASS:
                    assert(indexing->index->exptype->t == V_INT &&
                           indexing->object->exptype->udt->sametype->Numeric());
                    EmitOp(IL_LVAL_IDXNI);
                    assert(!IsStruct(type->t));
                    GenLvalRet(type);
                    break;
                case V_STRUCT_R:
                case V_STRUCT_S:
                case V_STRING:
                    // FIXME: Would be better to catch this in typechecking, but typechecker does
                    // not currently distinquish lvalues.
                    parser.ErrorAt(lval, "cannot use this type as lvalue");
                default:
                    assert(false);
            }
        } else {
            parser.ErrorAt(lval, "lvalue required");
        }
    }

    void GenConcatOp(const BinOp *n, size_t retval) {
        // Exception to the code below, since we want to generate an efficient concatenation
        // of any number of strings.
        vector<Node *> strs;
        strs.push_back(n->left);
        strs.push_back(n->right);
        for (;;) {
            auto c = strs[0];
            if (auto lt = Is<ToLifetime>(c)) {
                assert(lt->decref == 1 && lt->incref == 0);
                c = lt->child;
            }
            auto p = Is<Plus>(c);
            if (p && p->left->exptype->t == V_STRING && p->right->exptype->t == V_STRING) {
                strs.erase(strs.begin());
                strs.insert(strs.begin(), p->right);
                strs.insert(strs.begin(), p->left);
            } else {
                break;
            }
        }
        // TODO: we can even detect any ToString nodes here and generate an even more efficient
        // call that does I2S etc inline with even fewer allocations.
        for (auto s : strs) {
            Gen(s, retval);
            TakeTemp(1, false);
        }
        if (!retval) return;
        if (strs.size() == 2) {
            // We still need this op for += and it's marginally more efficient, might as well use it.
            EmitOp(IL_SADD);
        } else {
            EmitOp(IL_SADDN, (int)strs.size(), 1);
            Emit((int)strs.size());
        }
    }

    void GenMathOp(const BinOp *n, size_t retval, int opc) {
        Gen(n->left, retval);
        Gen(n->right, retval);
        if (retval) GenMathOp(n->left->exptype, n->right->exptype, n->exptype, opc);
    }

    void GenMathOp(TypeRef ltype, TypeRef rtype, TypeRef ptype, int opc) {
        TakeTemp(2, true);
        // Have to check right and left because comparison ops generate ints for node
        // overall.
        if (rtype->t == V_INT && ltype->t == V_INT) {
            EmitOp(GENOP(IL_IADD + opc));
        } else if (rtype->t == V_FLOAT && ltype->t == V_FLOAT) {
            EmitOp(GENOP(IL_FADD + opc));
        } else if (rtype->t == V_STRING && ltype->t == V_STRING) {
            EmitOp(GENOP(IL_SADD + opc));
        } else if (rtype->t == V_FUNCTION && ltype->t == V_FUNCTION) {
            assert(opc == MOP_EQ || opc == MOP_NE);
            EmitOp(GENOP(IL_LEQ + (opc - MOP_EQ)));
        } else {
            if (opc >= MOP_EQ) {  // EQ/NEQ
                if (IsStruct(ltype->t)) {
                    EmitOp(GENOP(IL_STEQ + opc - MOP_EQ), ValWidth(ltype) * 2, 1);
                    EmitWidthIfStruct(ltype);
                } else {
                    assert(IsRefNil(ltype->t) &&
                           IsRefNil(rtype->t));
                    EmitOp(GENOP(IL_AEQ + opc - MOP_EQ));
                }
            } else {
                // If this is a comparison op, be sure to use the child type.
                TypeRef vectype = opc >= MOP_LT ? ltype : ptype;
                assert(vectype->t == V_STRUCT_S);
                auto sub = vectype->udt->sametype;
                bool withscalar = IsScalar(rtype->t);
                auto outw = ValWidth(ptype);
                auto inw = withscalar ? outw + 1 : outw * 2;
                if (sub->t == V_INT)
                    EmitOp(GENOP((withscalar ? IL_IVSADD : IL_IVVADD) + opc), inw, outw);
                else if (sub->t == V_FLOAT)
                    EmitOp(GENOP((withscalar ? IL_FVSADD : IL_FVVADD) + opc), inw, outw);
                else assert(false);
                EmitWidthIfStruct(vectype);
            }
        }
    }

    void GenBitOp(const BinOp *n, size_t retval, ILOP opc) {
        Gen(n->left, retval);
        Gen(n->right, retval);
        if (retval) {
            TakeTemp(2, false);
            EmitOp(opc);
        }
    }

    ILOP AssignBaseOp(TypeLT typelt) {
        auto dec = ShouldDec(typelt);
        return IsStruct(typelt.type->t)
            ? (dec ? IL_LV_WRITEREFV : IL_LV_WRITEV)
            : (dec ? IL_LV_WRITEREF : IL_LV_WRITE);
    }

    void EmitKeep(int stack_offset, int keep_index_add) {
        auto opc = !loops.empty() ? IL_KEEPREFLOOP : IL_KEEPREF;
        EmitOp(opc);
        Emit(stack_offset);
        Emit(keepvars++ + keep_index_add);
    }

    void GenLvalVar(SpecIdent &sid) {
        EmitOp(sid.used_as_freevar ? IL_LVAL_VARF : IL_LVAL_VARL);
        Emit(sid.Idx());
    }


    void GenPushVar(size_t retval, TypeRef type, int offset, bool used_as_freevar) {
        if (!retval) return;
        if (IsStruct(type->t)) {
            EmitOp(used_as_freevar ? IL_PUSHVARVF : IL_PUSHVARVL, 0, ValWidth(type));
            Emit(offset);
            EmitWidthIfStruct(type);
        } else {
            EmitOp(used_as_freevar ? IL_PUSHVARF : IL_PUSHVARL);
            Emit(offset);
        }
    }

    void GenPushField(size_t retval, Node *object, TypeRef stype, TypeRef ftype, int offset) {
        if (IsStruct(stype->t)) {
            // Attempt to not generate object at all, by reading the field inline.
            if (auto idr = Is<IdentRef>(object)) {
                GenPushVar(retval, ftype, idr->sid->Idx() + offset, idr->sid->used_as_freevar);
                return;
            } else if (auto dot = Is<Dot>(object)) {
                auto sstype = dot->child->exptype;
                assert(IsUDT(sstype->t));
                auto idx = sstype->udt->Has(dot->fld);
                assert(idx >= 0);
                auto &field = sstype->udt->fields[idx];
                assert(field.slot >= 0);
                GenPushField(retval, dot->child, sstype, ftype, field.slot + offset);
                return;
            } else if (auto indexing = Is<Indexing>(object)) {
                // For now only do this for vectors.
                if (indexing->object->exptype->t == V_VECTOR) {
                    GenPushIndex(retval, indexing->object, indexing->index, ValWidth(ftype), offset);
                    return;
                }
            }
        }
        Gen(object, retval);
        if (!retval) return;
        TakeTemp(1, true);
        if (IsStruct(stype->t)) {
            if (IsStruct(ftype->t)) {
                EmitOp(IL_PUSHFLDV2V, ValWidth(stype), ValWidth(ftype));
                Emit(offset);
                EmitWidthIfStruct(ftype);
            } else {
                EmitOp(IL_PUSHFLDV, ValWidth(stype), 1);
                Emit(offset);
            }
            EmitWidthIfStruct(stype);
        } else {
            if (IsStruct(ftype->t)) {
                EmitOp(IL_PUSHFLD2V, 1, ValWidth(ftype));
                Emit(offset);
                EmitWidthIfStruct(ftype);
            } else {
                EmitOp(IL_PUSHFLD);
                Emit(offset);
            }
        }
    }

    void GenPushIndex(size_t retval, Node *object, Node *index, int struct_elem_sub_width = -1,
                      int struct_elem_sub_offset = -1) {
        Gen(object, retval);
        Gen(index, retval);
        if (!retval) return;
        TakeTemp(2, true);
        switch (object->exptype->t) {
            case V_VECTOR: {
                auto etype = object->exptype;
                if (index->exptype->t == V_INT) {
                    etype = etype->Element();
                } else {
                    auto &udt = *index->exptype->udt;
                    for (auto &field : udt.fields) {
                        (void)field;
                        etype = etype->Element();
                    }
                }
                auto inw = ValWidth(index->exptype) + 1;
                auto elemwidth = ValWidth(etype);
                if (struct_elem_sub_width < 0) {
                    EmitOp(index->exptype->t == V_INT
                        ? (elemwidth == 1 ? IL_VPUSHIDXI : IL_VPUSHIDXI2V)
                        : IL_VPUSHIDXV, inw, elemwidth);
                    EmitWidthIfStruct(index->exptype);
                } else {
                    // We're indexing a sub-part of the element.
                    auto op = index->exptype->t == V_INT
                        ? (elemwidth == 1 ? IL_VPUSHIDXIS : IL_VPUSHIDXIS2V)
                        : IL_VPUSHIDXVS;
                    EmitOp(op, inw, struct_elem_sub_width);
                    EmitWidthIfStruct(index->exptype);
                    if (op != IL_VPUSHIDXIS) Emit(struct_elem_sub_width);
                    Emit(struct_elem_sub_offset);
                }
                break;
            }
            case V_STRUCT_S:
                assert(index->exptype->t == V_INT && object->exptype->udt->sametype->Numeric());
                EmitOp(IL_NPUSHIDXI, ValWidth(object->exptype) + 1);
                EmitWidthIfStruct(object->exptype);
                break;
            case V_STRING:
                assert(index->exptype->t == V_INT);
                EmitOp(IL_SPUSHIDXI);
                break;
            default:
                assert(false);
        }
    }

    size_t LoopTemps() {
        size_t t = 0;
        for (auto n : loops) if (Is<For>(n)) t += 2;
        return t;
    }

    void ApplyBreaks(size_t level) {
        while (breaks.size() > level) {
            SetLabel(breaks.back());
            breaks.pop_back();
        }
    }
};

void Nil::Generate(CodeGen &cg, size_t retval) const {
    if (retval) { cg.EmitOp(IL_PUSHNIL); }
}

void IntConstant::Generate(CodeGen &cg, size_t retval) const {
    if (!retval) return;
    if (integer == (int)integer) {
        cg.EmitOp(IL_PUSHINT);
        cg.Emit((int)integer);
    } else {
        cg.EmitOp(IL_PUSHINT64);
        cg.Emit((int)integer);
        cg.Emit((int)(integer >> 32));
    }
}

void FloatConstant::Generate(CodeGen &cg, size_t retval) const {
    if (retval) { cg.GenFloat(flt); };
}

void StringConstant::Generate(CodeGen &cg, size_t retval) const {
    if (!retval) return;
    cg.EmitOp(IL_PUSHSTR);
    cg.Emit((int)cg.stringtable.size());
    cg.stringtable.push_back(str);
}

void DefaultVal::Generate(CodeGen &cg, size_t retval) const {
    if (!retval) return;
    // Optional args are indicated by being nillable, but for structs passed to builtins the type
    // has already been made non-nil.
    switch (exptype->ElementIfNil()->t) {
        case V_INT:   cg.EmitOp(IL_PUSHINT); cg.Emit(0); break;
        case V_FLOAT: cg.GenFloat(0); break;
        default:      cg.EmitOp(IL_PUSHNIL); break;
    }
}

void IdentRef::Generate(CodeGen &cg, size_t retval) const {
    cg.GenPushVar(retval, sid->type, sid->Idx(), sid->used_as_freevar);
}

void Dot::Generate(CodeGen &cg, size_t retval) const {
    auto stype = child->exptype;
    assert(IsUDT(stype->t));
    auto idx = stype->udt->Has(fld);
    assert(idx >= 0);
    auto &field = stype->udt->fields[idx];
    assert(field.slot >= 0);
    cg.GenPushField(retval, child, stype, field.resolvedtype(), field.slot);
}

void Indexing::Generate(CodeGen &cg, size_t retval) const {
    cg.GenPushIndex(retval, object, index);
}

void GenericCall::Generate(CodeGen &, size_t /*retval*/) const {
    assert(false);
}

void AssignList::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(children.back(), children.size() - 1);
    for (size_t i = children.size() - 1; i-- > 0; ) {
        auto left = children[i];
        auto id = Is<IdentRef>(left);
        auto llt = id ? id->sid->lt : LT_KEEP /* Dot */;
        cg.GenAssign(left, cg.AssignBaseOp({ left->exptype, llt }), 0, nullptr, 1, false);
    }
    assert(!retval);  // Type checker guarantees this.
    (void)retval;
}

void Define::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, sids.size());
    for (size_t i = sids.size(); i-- > 0; ) {
        auto sid = sids[i].first;
        cg.TakeTemp(1, true);
        // FIXME: Sadly, even though FunIntro now guarantees that variables start as V_NIL,
        // we still can't replace this with a WRITE that doesn't have to decrement, since
        // loops with inlined bodies cause this def to be execute multiple times.
        // (also: multiple copies of the same inlined function in one parent).
        // We should emit a specialized opcode for these cases only.
        // NOTE: we already don't decref for borrowed vars generated by the optimizer here (!)
        cg.GenLvalVar(*sid);
        auto op = cg.AssignBaseOp({ *sid });
        cg.EmitOp(op, ValWidth(sid->type));
        cg.EmitWidthIfStruct(sid->type);
        if (op == IL_LV_WRITEREFV) cg.EmitBitMaskForRefStuct(sid->type, this);
    }
    assert(!retval);  // Parser guarantees this.
    (void)retval;
}

void Assign::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, cg.AssignBaseOp({ *right, 0 }), retval, right, 1, false);
}

void PlusEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_IADD, retval, right, 1, false);
}
void MinusEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_ISUB, retval, right, 1, false);
}
void MultiplyEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_IMUL, retval, right, 1, false);
}
void DivideEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_IDIV, retval, right, 1, false);
}
void ModEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_IMOD, retval, right, 1, false);
}
void AndEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_BINAND, retval, right, 1, false);
}
void OrEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_BINOR, retval, right, 1, false);
}
void XorEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_XOR, retval, right, 1, false);
}
void ShiftLeftEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_ASL, retval, right, 1, false);
}
void ShiftRightEq::Generate(CodeGen &cg, size_t retval) const {
    cg.GenAssign(left, IL_LV_ASR, retval, right, 1, false);
}

void PostDecr::Generate(CodeGen &cg, size_t retval) const { cg.GenAssign(child, IL_LV_IMM, retval, nullptr, 0, true); }
void PostIncr::Generate(CodeGen &cg, size_t retval) const { cg.GenAssign(child, IL_LV_IPP, retval, nullptr, 0, true); }
void PreDecr ::Generate(CodeGen &cg, size_t retval) const { cg.GenAssign(child, IL_LV_IMM,  retval, nullptr, 0, false); }
void PreIncr ::Generate(CodeGen &cg, size_t retval) const { cg.GenAssign(child, IL_LV_IPP,  retval, nullptr, 0, false); }

void NotEqual     ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_NE);  }
void Equal        ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_EQ);  }
void GreaterThanEq::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_GE);  }
void LessThanEq   ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_LE);  }
void GreaterThan  ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_GT);  }
void LessThan     ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_LT);  }
void Mod          ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_MOD); }
void Divide       ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_DIV); }
void Multiply     ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_MUL); }
void Minus        ::Generate(CodeGen &cg, size_t retval) const { cg.GenMathOp(this, retval, MOP_SUB); }
void Plus         ::Generate(CodeGen &cg, size_t retval) const {
    if (left->exptype->t == V_STRING && right->exptype->t == V_STRING) {
        cg.GenConcatOp(this, retval);
    } else {
        cg.GenMathOp(this, retval, MOP_ADD);
    }
}

void UnaryMinus::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    if (!retval) return;
    cg.TakeTemp(1, true);
    auto ctype = child->exptype;
    switch (ctype->t) {
        case V_INT: cg.EmitOp(IL_IUMINUS); break;
        case V_FLOAT: cg.EmitOp(IL_FUMINUS); break;
        case V_STRUCT_S: {
            auto elem = ctype->udt->sametype->t;
            auto inw = ValWidth(ctype);
            cg.EmitOp(elem == V_INT ? IL_IVUMINUS : IL_FVUMINUS, inw, inw);
            cg.EmitWidthIfStruct(ctype);
            break;
        }
        default: assert(false);
    }
}

void BitAnd    ::Generate(CodeGen &cg, size_t retval) const { cg.GenBitOp(this, retval, IL_BINAND); }
void BitOr     ::Generate(CodeGen &cg, size_t retval) const { cg.GenBitOp(this, retval, IL_BINOR); }
void Xor       ::Generate(CodeGen &cg, size_t retval) const { cg.GenBitOp(this, retval, IL_XOR); }
void ShiftLeft ::Generate(CodeGen &cg, size_t retval) const { cg.GenBitOp(this, retval, IL_ASL); }
void ShiftRight::Generate(CodeGen &cg, size_t retval) const { cg.GenBitOp(this, retval, IL_ASR); }

void Negate::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    if (!retval) return;
    cg.TakeTemp(1, false);
    cg.EmitOp(IL_NEG);
}

void ToFloat::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    if (!retval) return;
    cg.TakeTemp(1, false);
    cg.EmitOp(IL_I2F);
}

void ToString::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    if (!retval) return;
    cg.TakeTemp(1, true);
    switch (child->exptype->t) {
        case V_STRUCT_R:
        case V_STRUCT_S: {
            // TODO: can also roll these into A2S?
            cg.EmitOp(IL_ST2S, ValWidth(child->exptype), 1);
            cg.Emit(cg.GetTypeTableOffset(child->exptype));
            break;
        }
        default: {
            cg.EmitOp(IL_A2S);
            cg.Emit(cg.GetTypeTableOffset(child->exptype->ElementIfNil()));
            break;
        }
    }
}

void ToBool::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    if (!retval) return;
    cg.TakeTemp(1, false);
    cg.EmitOp(cg.ShouldDec(TypeLT(*child, 0)) ? IL_E2BREF : IL_E2B);
}

void ToInt::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    // No actual opcode needed, this node is purely to store correct types.
    if (retval) cg.TakeTemp(1, false);
}

void ToLifetime::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    int stack_offset = 0;
    for (int fi = 0; fi < (int)retval; fi++) {
        // We have to check for reftype again, since typechecker allowed V_VAR values that may
        // have become scalars by now.
        auto i = (int)(retval - fi - 1);
        assert(i < ssize(cg.temptypestack));
        auto type = cg.temptypestack[cg.temptypestack.size() - 1 - fi].type;
        if (IsRefNil(child->exptype->Get(i)->t)) {
            if (incref & (1LL << i)) {
                assert(IsRefNil(type->t));
                if (type->t == V_STRUCT_R) {
                    // TODO: alternatively emit a single op with a list or bitmask? see EmitBitMaskForRefStuct
                    for (int j = 0; j < type->udt->numslots; j++) {
                        if (IsRefNil(FindSlot(*type->udt, j)->resolvedtype()->t)) {
                            cg.EmitOp(IL_INCREF);
                            cg.Emit(stack_offset + type->udt->numslots - 1 - j);
                        }
                    }
                } else {
                    cg.EmitOp(IL_INCREF);
                    cg.Emit(stack_offset);
                }
            }
            if (decref & (1LL << i)) {
                assert(IsRefNil(type->t));
                if (type->t == V_STRUCT_R) {
                    // TODO: alternatively emit a single op with a list or bitmask? see EmitBitMaskForRefStuct
                    for (int j = 0; j < type->udt->numslots; j++) {
                        if (IsRefNil(FindSlot(*type->udt, j)->resolvedtype()->t))
                            cg.EmitKeep(stack_offset + j, 0);
                    }
                } else {
                    cg.EmitKeep(stack_offset, 0);
                }
            }
        }
        stack_offset += ValWidth(type);
    }
    // We did not consume these, so we have to pass them on.
    for (size_t i = 0; i < retval; i++) {
        // Note: take LT from this node, not existing one on temptypestack, which we just changed!
        cg.rettypes.push_back(TypeLT(*this, i));
        cg.temptypestack.pop_back();
    }
}

void FunRef::Generate(CodeGen &cg, size_t retval) const {
    if (!retval) return;
    // If no body, then the function has been optimized away, meaning this
    // function value will never be used.
    // FIXME: instead, ensure such values are removed by the optimizer.
    if (sf->parent->anonymous && sf->sbody) {
        cg.EmitOp(IL_PUSHFUN);
        cg.Emit(sf->subbytecodestart);
        cg.GenFixup(sf);
    } else {
        cg.EmitOp(IL_PUSHFUN);
        cg.Emit(cg.dummyfun);
    }
}

void EnumRef::Generate(CodeGen &cg, size_t retval) const {
    cg.Dummy(retval);
}

void UDTRef::Generate(CodeGen &cg, size_t retval) const {
    cg.Dummy(retval);
}

void NativeCall::Generate(CodeGen &cg, size_t retval) const {
    if (nf->IsAssert()) {
        // FIXME: lift this into a language feature.
        auto c = children[0];
        if (retval || cg.runtime_checks >= RUNTIME_ASSERT) {
            cg.Gen(c, 1);
            cg.TakeTemp(1, false);
            if (cg.runtime_checks >= RUNTIME_ASSERT) {
                cg.EmitOp(GENOP(IL_ASSERT + (!!retval)));
                cg.Emit(c->line.line);
                cg.Emit(c->line.fileidx);
                cg.Emit((int)cg.stringtable.size());
                // FIXME: would be better to use the original source code here.
                cg.stringtable.push_back(cg.st.StoreName(DumpNode(*c, 0, true)));
            }
        } else {
            cg.Gen(c, 0);
        }
        return;
    }
    if (nf->name == "string") {
        // A frequently used function that doesn't actually do anything by itself, so ensure it
        // doesn't get emitted.
        cg.Gen(children[0], retval);
        cg.TakeTemp(1, false);
        return;
    }
    // TODO: could pass arg types in here if most exps have types, cheaper than
    // doing it all in call instruction?
    size_t numstructs = 0;
    auto start = cg.tstack.size();
    for (auto [i, c] : enumerate(children)) {
        cg.Gen(c, 1);
        if ((IsStruct(c->exptype->t) ||
             nf->args[i].flags & NF_PUSHVALUEWIDTH) &&
            !Is<DefaultVal>(c)) {
            cg.GenValueWidth(c->exptype);
            cg.temptypestack.push_back({ type_int, LT_ANY });
            numstructs++;
        }
    }
    auto inw = int(cg.tstack.size() - start);
    size_t nargs = children.size();
    cg.TakeTemp(nargs + numstructs, true);
    assert(nargs == nf->args.size() && (nf->fun.fnargs < 0 || nargs <= 7));
    auto vmop = nf->fun.fnargs >= 0 ? GENOP(IL_BCALLRET0 + (int)nargs) : IL_BCALLRETV;
    cg.EmitOp(vmop, inw, ValWidthMulti(nattype, nattype->NumValues()));
    cg.Emit(nf->idx);
    cg.Emit(!nf->retvals.empty());
    if (nf->retvals.size() > 0) {
        assert(nf->retvals.size() == nattype->NumValues());
        for (size_t i = 0; i < nattype->NumValues(); i++) {
            cg.rettypes.push_back({ nattype->Get(i), nattype->GetLifetime(i, natlt) });
        }
    } else {
        assert(nf->retvals.size() >= retval);
    }
    if (!retval) {
        while (cg.rettypes.size()) {
            cg.GenPop(cg.rettypes.back());
            cg.rettypes.pop_back();
        }
    }
}

void Call::Generate(CodeGen &cg, size_t retval) const {
    cg.GenCall(*this, retval);
}

void DynCall::Generate(CodeGen &cg, size_t retval) const {
    assert(sf && sf == sid->type->sf && sf->parent->istype);
    int arg_width = 0;
    for (auto c : children) {
        cg.Gen(c, 1);
        arg_width += ValWidth(c->exptype);
    }
    size_t nargs = children.size();
    assert(nargs == sf->args.size());
    cg.GenPushVar(1, type_function_null, sid->Idx(), sid->used_as_freevar);
    cg.TakeTemp(nargs, true);
    cg.EmitOp(IL_CALLV, arg_width + 1, ValWidthMulti(exptype, sf->returntype->NumValues()));
    if (sf->reqret) {
        if (!retval) cg.GenPop({ exptype, lt });
    } else {
        cg.Dummy(retval);
    }
}

void Block::Generate(CodeGen &cg, size_t retval) const {
    assert(retval <= 1);
    auto tstack_start = cg.tstack.size();
    (void)tstack_start;
    for (auto c : children) {
        if (c != children.back()) {
            // Not the last element.
            cg.Gen(c, 0);
            assert(tstack_start == cg.tstack.size());
        } else {
            if (false && c->exptype->t == V_VOID) {
                // This may happen because it is an inlined function whose result is never used,
                // because returns escape out of it, e.g. check in std.lobster.
                cg.Gen(c, 0);
            } else {
                cg.Gen(c, retval);
                cg.TakeTemp(retval, true);
            }
        }
    }
}

void List::Generate(CodeGen & /*cg*/, size_t /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void TypeAnnotation::Generate(CodeGen & /*cg*/, size_t /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void Unary::Generate(CodeGen & /*cg*/, size_t /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void Coercion::Generate(CodeGen & /*cg*/, size_t /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void BinOp::Generate(CodeGen & /*cg*/, size_t /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void Seq::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(head, 0);
    cg.Gen(tail, retval);
    if (retval) cg.TakeTemp(1, true);
}

void MultipleReturn::Generate(CodeGen &cg, size_t retval) const {
    for (auto [i, c] : enumerate(children))
        cg.Gen(c, i < retval);
    cg.TakeTemp(retval, true);
    for (auto[i, c] : enumerate(children))
        if (i < retval)
            cg.rettypes.push_back({ c->exptype, c->lt });
}

void And::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(left, 1);
    cg.TakeTemp(1, false);
    cg.EmitOp(retval ? IL_JUMPFAILR : IL_JUMPFAIL);
    cg.Emit(0);
    auto loc = cg.Pos();
    if (retval) cg.EmitOp(IL_POP);
    cg.Gen(right, retval);
    if (retval) cg.TakeTemp(1, false);
    cg.SetLabel(loc);
}

void Or::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(left, 1);
    cg.TakeTemp(1, false);
    cg.EmitOp(retval ? IL_JUMPNOFAILR : IL_JUMPNOFAIL);
    cg.Emit(0);
    auto loc = cg.Pos();
    if (retval) cg.EmitOp(IL_POP);
    cg.Gen(right, retval);
    if (retval) cg.TakeTemp(1, false);
    cg.SetLabel(loc);
}

void Not::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    if (retval) {
        cg.TakeTemp(1, false);
        cg.EmitOp(IsRefNil(child->exptype->t) ? IL_LOGNOTREF : IL_LOGNOT);
    }
}

void IfThen::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(condition, 1);
    cg.TakeTemp(1, false);
    cg.EmitOp(IL_JUMPFAIL);
    cg.Emit(0);
    auto loc = cg.Pos();
    assert(!retval); (void)retval;
    cg.Gen(truepart, 0);
    cg.SetLabel(loc);
}

void IfElse::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(condition, 1);
    cg.TakeTemp(1, false);
    cg.EmitOp(IL_JUMPFAIL);
    cg.Emit(0);
    auto loc = cg.Pos();
    CodeGen::BlockStack bs(cg.tstack);
    bs.Start();
    cg.Gen(truepart, retval);
    bs.End();
    if (retval) cg.TakeTemp(1, true);
    cg.EmitOp(IL_JUMP);
    cg.Emit(0);
    auto loc2 = cg.Pos();
    cg.SetLabel(loc);
    bs.Start();
    cg.Gen(falsepart, retval);
    bs.End();
    if (retval) cg.TakeTemp(1, true);
    cg.SetLabel(loc2);
    bs.Exit(cg);
}

void While::Generate(CodeGen &cg, size_t retval) const {
    auto loopback = cg.Pos();
    cg.EmitOp(IL_BLOCK_START);
    cg.loops.push_back(this);
    cg.Gen(condition, 1);
    cg.TakeTemp(1, false);
    cg.EmitOp(IL_JUMPFAIL);
    cg.Emit(0);
    auto jumpout = cg.Pos();
    auto break_level = cg.breaks.size();
    cg.Gen(wbody, 0);
    cg.loops.pop_back();
    cg.EmitOp(IL_JUMP);
    cg.Emit(loopback);
    cg.SetLabel(jumpout);
    cg.ApplyBreaks(break_level);
    cg.Dummy(retval);
}

void For::Generate(CodeGen &cg, size_t retval) const {
    cg.EmitOp(IL_PUSHINT);
    cg.Emit(-1);   // i
    cg.temptypestack.push_back({ type_int, LT_ANY });
    cg.Gen(iter, 1);
    cg.loops.push_back(this);
    auto startloop = cg.Pos();
    cg.EmitOp(IL_BLOCK_START);
    auto break_level = cg.breaks.size();
    auto tstack_level = cg.tstack.size();
    switch (iter->exptype->t) {
        case V_INT:      cg.EmitOp(IL_IFOR); cg.Emit(0); break;
        case V_STRING:   cg.EmitOp(IL_SFOR); cg.Emit(0); break;
        case V_VECTOR:   cg.EmitOp(IL_VFOR); cg.Emit(0); break;
        default:         assert(false);
    }
    auto exitloop = cg.Pos();
    cg.Gen(fbody, 0);
    cg.EmitOp(IL_JUMP);
    cg.Emit(startloop);
    cg.SetLabel(exitloop);
    cg.loops.pop_back();
    cg.TakeTemp(2, false);
    assert(tstack_level == cg.tstack.size()); (void)tstack_level;
    cg.PopTemp();
    cg.PopTemp();
    cg.ApplyBreaks(break_level);
    cg.Dummy(retval);
}

void ForLoopElem::Generate(CodeGen &cg, size_t /*retval*/) const {
    auto typelt = cg.temptypestack.back();
    switch (typelt.type->t) {
        case V_INT:
            cg.EmitOp(IL_IFORELEM);
            break;
        case V_STRING:
            cg.EmitOp(IL_SFORELEM);
            break;
        case V_VECTOR: {
            auto op = IsRefNil(typelt.type->sub->t)
                          ? (IsStruct(typelt.type->sub->t) ? IL_VFORELEMREF2S : IL_VFORELEMREF)
                          : (IsStruct(typelt.type->sub->t) ? IL_VFORELEM2S : IL_VFORELEM);
            cg.EmitOp(op, 2, ValWidth(typelt.type->sub) + 2);
            if (op == IL_VFORELEMREF2S) cg.EmitBitMaskForRefStuct(typelt.type->sub, this);
            break;
        }
        default:
            assert(false);
    }
}

void ForLoopCounter::Generate(CodeGen &cg, size_t /*retval*/) const {
    cg.EmitOp(IL_FORLOOPI);
}

void Break::Generate(CodeGen &cg, size_t retval) const {
    assert(!retval);
    (void)retval;
    assert(!cg.rettypes.size());
    assert(!cg.loops.empty());
    // FIXME: this code below likely doesn't work with inlined blocks
    // whose parents have temps on the stack above the top for loop.
    assert(cg.temptypestack.size() == cg.LoopTemps());
    if (Is<For>(cg.loops.back())) {
        auto fort1 = cg.PopTemp();
        auto fort2 = cg.PopTemp();
        cg.PushTemp(fort2);
        cg.PushTemp(fort1);
        cg.GenPop(cg.temptypestack[cg.temptypestack.size() - 1]);
        cg.GenPop(cg.temptypestack[cg.temptypestack.size() - 2]);
        cg.EmitOp(IL_JUMP);
        cg.PushTemp(fort2);
        cg.PushTemp(fort1);
    } else {
        cg.EmitOp(IL_JUMP);
    }
    cg.Emit(0);
    cg.breaks.push_back(cg.Pos());
}

void Switch::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(value, 1);
    cg.TakeTemp(1, false);
    // See if we should do a jump table version.
    if (GenerateJumpTable(cg, retval))
        return;
    // Do slow default implementation for sparse integers, expressions and strings.
    auto valtlt = TypeLT{ *value, 0 };
    vector<int> nextcase, thiscase, exitswitch;
    bool have_default = false;
    auto valop = cg.PopTemp();
    CodeGen::BlockStack bs(cg.tstack);
    for (auto n : cases->children) {
        bs.Start();
        cg.PushTemp(valop);
        cg.SetLabels(nextcase);
        cg.temptypestack.push_back(valtlt);
        auto cas = AssertIs<Case>(n);
        if (cas->pattern->children.empty()) have_default = true;
        for (auto c : cas->pattern->children) {
            auto is_last = c == cas->pattern->children.back();
            cg.GenDup(valtlt);
            int loc = -1;
            auto switchtype = value->exptype;
            if (auto r = Is<Range>(c)) {
                cg.Gen(r->start, 1);
                cg.GenMathOp(switchtype, c->exptype, switchtype, MOP_GE);
                cg.EmitOp(IL_JUMPFAIL);
                cg.Emit(0);
                loc = cg.Pos();
                if (is_last) nextcase.push_back(loc);
                cg.GenDup(valtlt);
                cg.Gen(r->end, 1);
                cg.GenMathOp(switchtype, c->exptype, switchtype, MOP_LE);
            } else {
                // FIXME: if this is a string, will alloc a temp string object just for the sake of
                // comparison. Better to create special purpose opcode to compare with const string.
                cg.Gen(c, 1);
                cg.GenMathOp(switchtype, c->exptype, switchtype, MOP_EQ);
            }
            if (is_last) {
                cg.EmitOp(IL_JUMPFAIL);
                cg.Emit(0);
                nextcase.push_back(cg.Pos());
            } else {
                cg.EmitOp(IL_JUMPNOFAIL);
                cg.Emit(0);
                thiscase.push_back(cg.Pos());
            }
            if (Is<Range>(c)) {
                if (!is_last) cg.SetLabel(loc);
            }
        }
        cg.SetLabels(thiscase);
        cg.GenPop(valtlt);
        cg.TakeTemp(1, false);
        cg.Gen(cas->cbody, retval);
        if (retval) cg.TakeTemp(1, true);
        bs.End();
        if (n != cases->children.back() || !have_default) {
            cg.EmitOp(IL_JUMP);
            cg.Emit(0);
            exitswitch.push_back(cg.Pos());
        }
    }
    cg.SetLabels(nextcase);
    if (!have_default) {
        bs.Start();
        cg.PushTemp(valop);
        cg.GenPop(valtlt);
        bs.End();
    }
    cg.SetLabels(exitswitch);
    bs.Exit(cg);
}

bool Switch::GenerateJumpTable(CodeGen &cg, size_t retval) const {
    if (value->exptype->t != V_INT)
        return false;
    int64_t mini = INT64_MAX / 2, maxi = INT64_MIN / 2;
    int64_t num = 0;
    auto get_range = [&](Node *c) -> pair<IntConstant *, IntConstant *> {
        auto start = c;
        auto end = c;
        if (auto r = Is<Range>(c)) {
            start = r->start;
            end = r->end;
        }
        return { Is<IntConstant>(start), Is<IntConstant>(end) };
    };
    for (auto n : cases->children) {
        auto cas = AssertIs<Case>(n);
        for (auto c : cas->pattern->children) {
            auto [istart, iend] = get_range(c);
            if (!istart || !iend || istart->integer > iend->integer)
                return false;
            num += iend->integer - istart->integer + 1;
            mini = std::min(mini, istart->integer);
            maxi = std::max(maxi, iend->integer);
        }
    }
    // Decide if jump table is economic.
    const int64_t min_vals = 3;  // Minimum to do jump table.
    // TODO: This should be slightly non-linear? More values means you really want the
    // jump table, typically.
    const int64_t min_load_factor = 5;
    int64_t range = maxi - mini + 1;
    if (num < min_vals ||
        range / num > min_load_factor ||
        mini < INT32_MIN ||
        maxi >= INT32_MAX)
        return false;
    // Emit jump table version.
    cg.EmitOp(IL_JUMP_TABLE);
    cg.Emit((int)mini);
    cg.Emit((int)maxi);
    auto table_start = cg.Pos();
    for (int i = 0; i < (int)range + 1; i++) cg.Emit(-1);
    vector<int> exitswitch;
    int default_pos = -1;
    CodeGen::BlockStack bs(cg.tstack);
    for (auto n : cases->children) {
        bs.Start();
        auto cas = AssertIs<Case>(n);
        for (auto c : cas->pattern->children) {
            auto [istart, iend] = get_range(c);
            assert(istart && iend);
            for (auto i = istart->integer; i <= iend->integer; i++) {
                cg.code[table_start + (int)i - (int)mini] = cg.Pos();
            }
        }
        if (cas->pattern->children.empty()) default_pos = cg.Pos();
        cg.EmitOp(IL_JUMP_TABLE_CASE_START);
        cg.Gen(cas->cbody, retval);
        if (retval) cg.TakeTemp(1, true);
        bs.End();
        if (n != cases->children.back()) {
            cg.EmitOp(IL_JUMP);
            cg.Emit(0);
            exitswitch.push_back(cg.Pos());
        }
    }
    cg.EmitOp(IL_JUMP_TABLE_END);
    cg.SetLabels(exitswitch);
    if (default_pos < 0) default_pos = cg.Pos();
    for (int i = 0; i < (int)range + 1; i++) {
        if (cg.code[table_start + i] == -1)
            cg.code[table_start + i] = default_pos;
    }
    bs.Exit(cg);
    return true;
}

void Case::Generate(CodeGen &/*cg*/, size_t /*retval*/) const {
    assert(false);
}

void Range::Generate(CodeGen &/*cg*/, size_t /*retval*/) const {
    assert(false);
}

void Constructor::Generate(CodeGen &cg, size_t retval) const {
    // FIXME: a malicious script can exploit this for a stack overflow.
    int arg_width = 0;
    for (auto c : children) {
        cg.Gen(c, retval);
        arg_width += ValWidth(c->exptype);
    }
    if (!retval) return;
    cg.TakeTemp(Arity(), true);
    auto offset = cg.GetTypeTableOffset(exptype);
    if (IsUDT(exptype->t)) {
        assert(exptype->udt->fields.size() == Arity());
        if (IsStruct(exptype->t)) {
            // This is now a no-op! Struct elements sit inline on the stack.
        } else {
            cg.EmitOp(IL_NEWOBJECT, arg_width);
            cg.Emit(offset);
        }
    } else {
        assert(exptype->t == V_VECTOR);
        cg.EmitOp(IL_NEWVEC, arg_width);
        cg.Emit(offset);
        cg.Emit((int)Arity());
    }
}

void IsType::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    // If the value was a scalar, then it always results in a compile time type check,
    // which means this T_IS would have been optimized out. Which means from here on we
    // can assume its a ref.
    assert(!IsUnBoxed(child->exptype->t));
    if (retval) {
        cg.TakeTemp(1, false);
        cg.EmitOp(IL_ISTYPE);
        cg.Emit(cg.GetTypeTableOffset(gr.resolvedtype()));
    }
}

void EnumCoercion::Generate(CodeGen &cg, size_t retval) const {
    cg.Gen(child, retval);
    if (retval) cg.TakeTemp(1, false);
}

void Return::Generate(CodeGen &cg, size_t retval) const {
    assert(!cg.rettypes.size());
    auto typestackbackup = cg.temptypestack;
    auto tstackbackup = cg.tstack;
    if (cg.temptypestack.size()) {
        // We have temps on the stack, these can be from:
        // * an enclosing for.
        // * an (inlined) block, whose caller already had temps on the stack.
        // We can't actually remove these from the stack permanently as the parent nodes still
        // expect them to be there.
        while (!cg.temptypestack.empty()) {
            cg.GenPop(cg.temptypestack.back());
            cg.temptypestack.pop_back();
        }
    }
    int nretslots = 0;
    if (sf->reqret) {
        auto nretvals = make_void ? 0 : sf->returntype->NumValues();
        if (!Is<DefaultVal>(child)) {
            cg.Gen(child, nretvals);
            cg.TakeTemp(nretvals, true);
        } else {
            cg.EmitOp(IL_PUSHNIL);
            assert(nretvals == 1);
        }
        nretslots = ValWidthMulti(sf->returntype, nretvals);
    } else {
        if (!Is<DefaultVal>(child)) cg.Gen(child, 0);
    }
    // FIXME: we could change the VM to instead work with SubFunction ids.
    // Note: this can only work as long as the type checker forces specialization
    // of the functions in between here and the function returned to.
    // Actually, doesn't work with DDCALL and RETURN_THRU.
    // FIXME: shouldn't need any type here if V_VOID, but nretvals is at least 1 ?
    if (sf == cg.cursf && sf->returned_thru_to_max < 0) {
        cg.EmitOp(IL_RETURNLOCAL, nretslots);
        cg.Emit(nretslots);
    } else {
        // This is for both if the return itself is non-local, or if the destination has
        // an unwind check, could potentially split those up further.
        cg.EmitOp(IL_RETURNNONLOCAL, nretslots);
        cg.Emit(nretslots);
        cg.Emit(sf->parent->idx);
    }

    cg.temptypestack = typestackbackup;
    cg.tstack = tstackbackup;
    // We can promise to be providing whatever retvals the caller wants.
    for (size_t i = 0; i < retval; i++) {
        cg.rettypes.push_back({ type_undefined, LT_ANY });
        cg.PushTemp(IL_RETURNLOCAL);  // FIXME: is this necessary? do more generally?
    }
}

void TypeOf::Generate(CodeGen &cg, size_t /*retval*/) const {
    if (auto idr = Is<IdentRef>(child)) {
        cg.EmitOp(IL_PUSHINT);
        cg.Emit(cg.GetTypeTableOffset(idr->exptype));
    } else {
        auto ta = AssertIs<TypeAnnotation>(child);
        cg.EmitOp(IL_PUSHINT);
        cg.Emit(cg.GetTypeTableOffset(ta->exptype));
    }
}

}  // namespace lobster
