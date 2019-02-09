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
    vector<uchar> code_attr;
    vector<bytecode::LineInfo> lineinfo;
    vector<bytecode::SpecIdent> sids;
    Parser &parser;
    vector<const Node *> linenumbernodes;
    vector<pair<int, const SubFunction *>> call_fixups;
    SymbolTable &st;
    vector<type_elem_t> type_table, vint_typeoffsets, vfloat_typeoffsets;
    map<vector<type_elem_t>, type_elem_t> type_lookup;  // Wasteful, but simple.
    vector<TypeRef> rettypes, temptypestack;
    size_t nested_fors;
    vector<string_view> stringtable;  // sized strings.
    const Node *temp_parent = nullptr; // FIXME
    vector<int> speclogvars;  // Index into specidents.

    int Pos() { return (int)code.size(); }

    void GrowCodeAttr(size_t mins) {
        while (mins > code_attr.size()) code_attr.push_back(bytecode::Attr_NONE);
    }

    void Emit(int i) {
        auto &ln = linenumbernodes.back()->line;
        if (lineinfo.empty() || ln.line != lineinfo.back().line() ||
            ln.fileidx != lineinfo.back().fileidx())
            lineinfo.push_back(bytecode::LineInfo(ln.line, ln.fileidx, Pos()));
        code.push_back(i);
        GrowCodeAttr(code.size());
    }

    void SplitAttr(int at) {
        GrowCodeAttr(at + 1);
        code_attr[at] |= bytecode::Attr_SPLIT;
    }

    void Emit(int i, int j) { Emit(i); Emit(j); }
    void Emit(int i, int j, int k) { Emit(i); Emit(j); Emit(k); }
    void Emit(int i, int j, int k, int l) { Emit(i); Emit(j); Emit(k); Emit(l); }

    void SetLabel(int jumploc) {
        code[jumploc - 1] = Pos();
        SplitAttr(Pos());
    }

    // Make a table for use as VM runtime type.
    type_elem_t GetTypeTableOffset(TypeRef type) {
        vector<type_elem_t> tt;
        tt.push_back((type_elem_t)type->t);
        switch (type->t) {
            case V_NIL:
            case V_VECTOR:
                tt.push_back(GetTypeTableOffset(type->sub));
                break;
            case V_FUNCTION:
                tt.push_back((type_elem_t)type->sf->idx);
                break;
            case V_COROUTINE:
                if (type->sf) {
                    if (type->sf->cotypeinfo >= 0)
                        return type->sf->cotypeinfo;
                    type->sf->cotypeinfo = (type_elem_t)type_table.size();
                    // Reserve space, so other types can be added afterwards safely.
                    type_table.insert(type_table.end(), 3, (type_elem_t)0);
                    tt.push_back((type_elem_t)type->sf->idx);
                    tt.push_back(GetTypeTableOffset(type->sf->returntypes[0]));
                    std::copy(tt.begin(), tt.end(), type_table.begin() + type->sf->cotypeinfo);
                    return type->sf->cotypeinfo;
                } else {
                    tt.push_back((type_elem_t)-1);
                    tt.push_back(TYPE_ELEM_ANY);
                }
                break;
            case V_STRUCT:
                if (type->struc->typeinfo >= 0)
                    return type->struc->typeinfo;
                type->struc->typeinfo = (type_elem_t)type_table.size();
                // Reserve space, so other types can be added afterwards safely.
                type_table.insert(type_table.end(), type->struc->fields.size() + 3, (type_elem_t)0);
                tt.push_back((type_elem_t)type->struc->idx);
                tt.push_back((type_elem_t)type->struc->fields.size());
                for (auto &field : type->struc->fields.v) {
                    tt.push_back(GetTypeTableOffset(field.type));
                }
                std::copy(tt.begin(), tt.end(), type_table.begin() + type->struc->typeinfo);
                return type->struc->typeinfo;
            case V_VAR:
                // This can happen with an empty [] vector that was never bound to anything.
                // Should be benign to use any, since it is never accessed anywhere.
                // FIXME: would be even better to check this case before codegen, since this may
                // mask bugs.
                return GetTypeTableOffset(type_any);
            default:
                assert(IsRuntime(type->t));
                break;
        }
        // For everything that's not a struct / know coroutine:
        auto it = type_lookup.find(tt);
        if (it != type_lookup.end()) return it->second;
        auto offset = (type_elem_t)type_table.size();
        type_lookup[tt] = offset;
        type_table.insert(type_table.end(), tt.begin(), tt.end());
        return offset;
    }

    CodeGen(Parser &_p, SymbolTable &_st) : parser(_p), st(_st), nested_fors(0) {
        // Pre-load some types into the table, must correspond to order of type_elem_t enums.
                                                            GetTypeTableOffset(type_int);
                                                            GetTypeTableOffset(type_float);
        Type type_boxedint(V_BOXEDINT);                     GetTypeTableOffset(&type_boxedint);
        Type type_boxedfloat(V_BOXEDFLOAT);                 GetTypeTableOffset(&type_boxedfloat);
                                                            GetTypeTableOffset(type_string);
                                                            GetTypeTableOffset(type_resource);
                                                            GetTypeTableOffset(type_any);
        Type type_valuebuf(V_VALUEBUF);                     GetTypeTableOffset(&type_valuebuf);
        Type type_stackframebuf(V_STACKFRAMEBUF);           GetTypeTableOffset(&type_stackframebuf);
                                                            GetTypeTableOffset(type_vector_int);
                                                            GetTypeTableOffset(type_vector_float);
        Type type_vec_str(V_VECTOR, &*type_string);         GetTypeTableOffset(&type_vec_str);
        Type type_v_v_int(V_VECTOR, &*type_vector_int);     GetTypeTableOffset(&type_v_v_int);
        Type type_v_v_float(V_VECTOR, &*type_vector_float); GetTypeTableOffset(&type_v_v_float);
        assert(type_table.size() == TYPE_ELEM_FIXED_OFFSET_END);
        for (auto type : st.default_int_vector_types[0])
            vint_typeoffsets.push_back(!type.Null() ? GetTypeTableOffset(type) : (type_elem_t)-1);
        for (auto type : st.default_float_vector_types[0])
            vfloat_typeoffsets.push_back(!type.Null() ? GetTypeTableOffset(type) : (type_elem_t)-1);
        int sidx = 0;
        for (auto sid : st.specidents) {
            if (!sid->type.Null()) {  // Null ones are in unused functions.
                sid->sidx = sidx++;
                if (sid->id->logvar) {
                    sid->logvaridx = (int)speclogvars.size();
                    speclogvars.push_back(sid->Idx());
                }
                sids.push_back(bytecode::SpecIdent(sid->id->idx, GetTypeTableOffset(sid->type)));
            }
        }
        // Create list of subclasses, to help in creation of dispatch tables.
        for (auto struc : st.structtable) {
            if (struc->superclass) {
                struc->nextsubclass = struc->superclass->firstsubclass;
                struc->superclass->firstsubclass = struc;
            }
        }
        linenumbernodes.push_back(parser.root);
        SplitAttr(0);
        Emit(IL_JUMP, 0);
        auto fundefjump = Pos();
        SplitAttr(Pos());
        for (auto f : parser.st.functiontable)
            if (f->subf && f->subf->typechecked)
                GenFunction(*f);
        // Generate a dummmy function for function values that are never called.
        // Would be good if the optimizer guarantees these don't exist, but for now this is
        // more debuggable if it does happen to get called.
        auto dummyfun = Pos();
        Emit(IL_FUNSTART, 0, 0);
        Emit(IL_ABORT);
        Emit(IL_FUNEND, 0);
        // Emit the root function.
        SetLabel(fundefjump);
        SplitAttr(Pos());
        BodyGen(parser.root, true);
        Emit(IL_EXIT, GetTypeTableOffset(parser.root->children.back()->exptype));
        SplitAttr(Pos());  // Allow off by one indexing.
        linenumbernodes.pop_back();
        for (auto &[loc, sf] : call_fixups) {
            auto f = sf->parent;
            auto bytecodestart = f->multimethod ? f->bytecodestart : sf->subbytecodestart;
            if (!bytecodestart) bytecodestart = dummyfun;
            assert(!code[loc]);
            code[loc] = bytecodestart;
        }
    }

    ~CodeGen() {
    }

    void Dummy(int retval) { while (retval--) Emit(IL_PUSHNIL); }

    void BodyGen(const List *list, bool reqret) {
        for (auto c : list->children) Gen(c, reqret && c == list->children.back());
    }

    struct sfcompare {
        size_t nargs;
        CodeGen *cg;
        Function *f;
        bool operator() (SubFunction *a, SubFunction *b) {
            for (size_t i = 0; i < nargs; i++) {
                auto ta = a->args.v[i].type;
                auto tb = b->args.v[i].type;

                if (ta != tb) return !(ta < tb);  // V_ANY must be last.
            }
            cg->parser.Error("function signature overlap for " + f->name, nullptr);
            return false;
        }
    } sfcomparator;

    bool GenFunction(Function &f) {
        if (f.bytecodestart > 0 || f.istype) return false;
        if (!f.multimethod) {
            f.bytecodestart = Pos();
            for (auto sf = f.subf; sf; sf = sf->next) {
                GenScope(*sf);
            }
        } else {
            // do multi-dispatch
            vector<SubFunction *> sfs;
            for (auto sf = f.subf; sf; sf = sf->next) {
                sfs.push_back(sf);
                GenScope(*sf);
            }
            sfcomparator.nargs = f.nargs();
            sfcomparator.cg = this;
            sfcomparator.f = &f;
            sort(sfs.begin(), sfs.end(), sfcomparator);
            f.bytecodestart = Pos();
            int numentries = 0;
            auto multistart = Pos();
            SplitAttr(Pos());
            Emit(IL_FUNMULTI, 0, (int)f.nargs());
            // FIXME: invent a much faster, more robust multi-dispatch mechanic.
            for (auto sf : sfs) {
                auto gendispatch = [&] (size_t override_j, TypeRef override_type) {
                    Output(OUTPUT_DEBUG, "dispatch ", f.name);
                    for (size_t j = 0; j < f.nargs(); j++) {
                        auto type = j == override_j ? override_type : sf->args.v[j].type;
                        Output(OUTPUT_DEBUG, "arg ", j, ": ", TypeName(type));
                        Emit(GetTypeTableOffset(type));
                    }
                    Emit(sf->subbytecodestart);
                    numentries++;
                };
                // Generate regular dispatch entry.
                gendispatch(0xFFFFFFFF, nullptr);
                // See if this entry contains super-types and generate additional entries.
                for (size_t j = 0; j < f.nargs(); j++) {
                    auto arg = sf->args.v[j];
                    if (arg.type->t == V_STRUCT) {
                        auto struc = arg.type->struc;
                        for (auto subs = struc->firstsubclass; subs; subs = subs->nextsubclass) {
                            // See if this instance already exists:
                            for (auto osf : sfs) {
                                // Only check this arg, not all arg, which is reasonable.
                                if (*osf->args.v[j].type == subs->thistype) goto skip;
                            }
                            gendispatch(j, &subs->thistype);
                            // FIXME: We should also call it on subtypes of subs.
                            skip:;
                        }
                    }
                }
            }
            code[multistart + 1] = numentries;
        }
        return true;
    }

    void GenScope(SubFunction &sf) {
        if (sf.subbytecodestart > 0) return;
        sf.subbytecodestart = Pos();
        if (!sf.typechecked) {
            auto s = Dump(*sf.body, 0);
            Output(OUTPUT_DEBUG, "untypechecked: ", sf.parent->name, " : ", s);
            assert(0);
        }
        vector<SpecIdent *> defs;
        auto collect = [&](Arg &arg) {
            defs.push_back(arg.sid);
        };
        for (auto &arg : sf.locals.v) collect(arg);
        for (auto &arg : sf.dynscoperedefs.v) collect(arg);
        linenumbernodes.push_back(sf.body);
        SplitAttr(Pos());
        Emit(IL_FUNSTART);
        Emit((int)sf.args.v.size());
        for (auto &arg : sf.args.v) Emit(arg.sid->Idx());
        // FIXME: we now have sf.dynscoperedefs, so we could emit them seperately, and thus
        // optimize function calls
        Emit((int)defs.size());
        for (auto id : defs) Emit(id->Idx());
        if (sf.body) BodyGen(sf.body, sf.reqret);
        else Dummy(sf.reqret);
        if (sf.reqret) TakeTemp(1);
        assert(temptypestack.empty());
        Emit(IL_FUNEND, sf.reqret);
        linenumbernodes.pop_back();
    }

    void EmitTempInfo(const Node *callnode) {
        int i = 0;
        uint mask = 0;
        for (auto type : temptypestack) {
            if (IsRefNil(type->t)) {
                // FIXME: this is pretty lame, but hopefully rare. stopgap measure.
                if (i >= 32)
                    parser.Error("internal error: too many temporaries at function call site",
                                 callnode);
                mask |= 1 << i;
            }
            i++;
        }
        Emit((int)mask);
    }

    void TakeTemp(size_t n) { temptypestack.erase(temptypestack.end() - n, temptypestack.end()); }

    void GenFixup(const SubFunction *sf) {
        assert(sf->body);
        if (!sf->subbytecodestart) call_fixups.push_back({ Pos() - 1, sf });
    }

    const Node *GenArgs(const List *list, size_t &nargs, const Node *parent = nullptr) {
        // Skip unused args, this may happen for dynamic calls.
        const Node *lastarg = nullptr;
        for (auto c : list->children) {
            Gen(c, 1, false, parent);
            lastarg = c;
            nargs++;
        }
        return lastarg;
    };

    void GenCall(const SubFunction &sf, const List *args, const Node *errnode, size_t &nargs,
                 int retval) {
        auto &f = *sf.parent;
        GenArgs(args, nargs);
        if (f.nargs() != nargs)
            parser.Error(cat("call to function ", f.name, " needs ", f.nargs(),
                             " arguments, ", nargs, " given"), errnode);
        TakeTemp(nargs);
        Emit(f.multimethod ? IL_CALLMULTI : IL_CALL,
             f.idx,
             f.multimethod ? f.bytecodestart : sf.subbytecodestart);
        GenFixup(&sf);
        EmitTempInfo(args);
        if (f.multimethod) {
            for (auto c : args->children) Emit(GetTypeTableOffset(c->exptype));
        }
        SplitAttr(Pos());
        auto nretvals = max(f.nretvals, 1);
        assert(nretvals == (int)sf.returntypes.size());
        if (sf.reqret) {
            if (retval) {
                for (int i = 0; i < nretvals; i++) rettypes.push_back(sf.returntypes[i]);
            } else {
                // FIXME: better if this is impossible by making sure typechecker makes it !reqret.
                //assert(f.multimethod);
                for (int i = 0; i < nretvals; i++) GenPop(sf.returntypes[i]);
            }
        } else {
            assert(!retval);
            Dummy(retval);
        }
    };

    int JumpRef(int jumpop, TypeRef type) { return IsRefNil(type->t) ? jumpop + 1 : jumpop; }

    void GenFloat(float f) { Emit(IL_PUSHFLT); int2float i2f; i2f.f = f; Emit(i2f.i); }

    void GenPop(TypeRef type) {
        Emit(IsRefNil(type->t) ? IL_POPREF : IL_POP);
    }

    void GenDup(TypeRef type) {
        Emit(IsRefNil(type->t) ? IL_DUPREF : IL_DUP);
    }

    void Gen(const Node *n, int retval, bool taketemp = false, const Node *parent = nullptr) {
        // The cases below generate no retvals if retval==0, otherwise they generate however many
        // they can irrespective of retval, optionally record that in rettypes for the more complex
        // cases. Then at the end of this function the two get matched up.
        auto tempstartsize = temptypestack.size();
        linenumbernodes.push_back(n);

        temp_parent = parent; // FIXME
        n->Generate(*this, retval);

        assert(tempstartsize == temptypestack.size());
        (void)tempstartsize;
        // If 0, the above code already made sure to not generate value(s).
        if (retval) {
            // default case, 1 value
            if (rettypes.empty()) {
                rettypes.push_back(n->exptype);
            }
            // if we generate just 1 value, it can be copied into multiple vars if needed
            if (rettypes.size() == 1) {
                for (; retval > 1; retval--) {
                    rettypes.push_back(rettypes.back());
                    GenDup(rettypes.back());
                }
            // if the caller doesn't want all return values, just pop em
            } else if((int)rettypes.size() > retval) {
                while ((int)rettypes.size() > retval) {
                    GenPop(rettypes.back());
                    rettypes.pop_back();
                }
            // only happens if both are > 1
            } else if ((int)rettypes.size() < retval) {
                parser.Error("expression does not supply that many return values", n);
            }
            // Copy return types on temp stack, unless caller doesn't want them (taketemp = true).
            while (rettypes.size()) {
                if (!taketemp) temptypestack.push_back(rettypes.back());
                rettypes.pop_back();
            }
        }
        assert(rettypes.empty());
        linenumbernodes.pop_back();
    }

    void VarModified(SpecIdent *sid) {
        if (sid->id->logvar) Emit(IL_LOGWRITE, sid->Idx(), sid->logvaridx);
    }

    void GenAssign(const Node *lval, int lvalop, int retval, TypeRef type,
                   const Node *rhs = nullptr) {
        if (lvalop >= LVO_IADD && lvalop <= LVO_IMOD) {
            if (type->t == V_INT) {
            } else if (type->t == V_FLOAT)  {
                assert(lvalop != LVO_IMOD); lvalop += LVO_FADD - LVO_IADD;
            } else if (type->t == V_STRING) {
                assert(lvalop == LVO_IADD); lvalop = LVO_SADD;
            } else if (type->t == V_STRUCT) {
                auto sub = type->struc->sametype;
                bool withscalar = IsScalar(rhs->exptype->t);
                if (sub->t == V_INT) {
                    lvalop += (withscalar ? LVO_IVSADD : LVO_IVVADD) - LVO_IADD;
                } else if (sub->t == V_FLOAT) {
                    assert(lvalop != LVO_IMOD);
                    lvalop += (withscalar ? LVO_FVSADD : LVO_FVVADD) - LVO_IADD;
                } else assert(false);
            } else {
                assert(false);
            }
        } else if (lvalop >= LVO_IPP && lvalop <= LVO_IMMP) {
            if (type->t == V_FLOAT) lvalop += LVO_FPP - LVO_IPP;
            else assert(type->t == V_INT);
        }
        if (retval) lvalop++;
        int na = 0;
        if (rhs) { Gen(rhs, 1); na++; }
        if (auto idr = Is<IdentRef>(lval)) {
            TakeTemp(na);
            Emit(IL_LVALVAR, lvalop, idr->sid->Idx());
            VarModified(idr->sid);
        } else if (auto dot = Is<Dot>(lval)) {
            Gen(dot->child, 1);
            TakeTemp(na + 1);
            GenFieldAccess(*dot, lvalop, false);
        } else if (auto cod = Is<CoDot>(lval)) {
            Gen(cod->coroutine, 1);
            TakeTemp(na + 1);
            Emit(IL_LVALLOC, lvalop, AssertIs<IdentRef>(cod->variable)->sid->Idx());
        } else if (auto indexing = Is<Indexing>(lval)) {
            Gen(indexing->object, 1);
            Gen(indexing->index, 1);
            TakeTemp(na + 2);
            switch (indexing->object->exptype->t) {
                case V_VECTOR:
                    Emit(indexing->index->exptype->t == V_INT ? IL_VLVALIDXI : IL_LVALIDXV, lvalop);
                    break;
                case V_STRUCT:
                    assert(indexing->index->exptype->t == V_INT &&
                           indexing->object->exptype->struc->sametype->Numeric());
                    Emit(IL_NLVALIDXI, lvalop);
                    break;
                case V_STRING:
                    // FIXME: Would be better to catch this in typechecking, but typechecker does
                    // not currently distinquish lvalues.
                    parser.Error("cannot use string index as lvalue", lval);
                default:
                    assert(false);
            }
        } else {
            parser.Error("lvalue required", lval);
        }
    }

    void GenFieldAccess(const Dot &n, int lvalop, bool maybe) {
        auto smtype = n.child->exptype;
        auto stype = n.maybe && smtype->t == V_NIL ? smtype->Element() : smtype;
        auto f = n.fld;
        assert(stype->t == V_STRUCT);  // Ensured by typechecker.
        auto idx = stype->struc->Has(f);
        assert(idx >= 0);
        if (lvalop >= 0) Emit(IL_LVALFLD, lvalop);
        else Emit(IsRefNil(stype->struc->fields.v[idx].type->t) ? IL_PUSHFLDREF + (int)maybe
                                                                : IL_PUSHFLD);
        Emit(idx);
    }

    void GenMathOp(const BinOp *n, int retval, MathOp opc) {
        Gen(n->left, retval);
        Gen(n->right, retval);
        if (retval) GenMathOp(n->left->exptype, n->right->exptype, n->exptype, opc);
    }

    void GenMathOp(TypeRef ltype, TypeRef rtype, TypeRef ptype, MathOp opc) {
        TakeTemp(2);
        // Have to check right and left because comparison ops generate ints for node
        // overall.
        if (rtype->t == V_INT && ltype->t == V_INT) {
            Emit(IL_IADD + opc);
        } else if (rtype->t == V_FLOAT && ltype->t == V_FLOAT) {
            Emit(IL_FADD + opc);
        } else if (rtype->t == V_STRING && ltype->t == V_STRING) {
            Emit(IL_SADD + opc);
        } else {
            if (opc >= MOP_EQ) {  // EQ/NEQ
                assert(IsRefNil(ltype->t) &&
                        IsRefNil(rtype->t));
                Emit(IL_AEQ + opc - MOP_EQ);
            } else {
                // If this is a comparison op, be sure to use the child type.
                TypeRef vectype = opc >= MOP_LT ? ltype : ptype;
                assert(vectype->t == V_STRUCT);
                auto sub = vectype->struc->sametype;
                bool withscalar = IsScalar(rtype->t);
                if (sub->t == V_INT)
                    Emit((withscalar ? IL_IVSADD : IL_IVVADD) + opc);
                else if (sub->t == V_FLOAT)
                    Emit((withscalar ? IL_FVSADD : IL_FVVADD) + opc);
                else assert(false);
            }
        }
    }

    void GenBitOp(const BinOp *n, int retval, ILOP opc) {
        Gen(n->left, retval);
        Gen(n->right, retval);
        if (retval) {
            TakeTemp(2);
            Emit(opc);
        }
    }
};

void Nil::Generate(CodeGen &cg, int retval) const {
    if (retval) { cg.Emit(IL_PUSHNIL); }
}

void IntConstant::Generate(CodeGen &cg, int retval) const {
    if (retval) {
        if (integer == (int)integer) cg.Emit(IL_PUSHINT, (int)integer); 
        else cg.Emit(IL_PUSHINT64, (int)integer, (int)(integer >> 32));
    };
}

void FloatConstant::Generate(CodeGen &cg, int retval) const {
    if (retval) { cg.GenFloat((float)flt); };
}

void StringConstant::Generate(CodeGen &cg, int retval) const {
    if (retval) {
        cg.Emit(IL_PUSHSTR, (int)cg.stringtable.size());
        cg.stringtable.push_back(str);
    };
}

void DefaultVal::Generate(CodeGen &cg, int retval) const {
    assert(exptype->t == V_NIL);  // Optional args are indicated by being nillable.
    if (retval) switch (exptype->sub->t) {
        case V_INT:   cg.Emit(IL_PUSHINT, 0); break;
        case V_FLOAT: cg.GenFloat(0); break;
        default:      cg.Emit(IL_PUSHNIL); break;
    }
}

void IdentRef::Generate(CodeGen &cg, int retval) const {
    if (retval) {
        cg.Emit(IsRefNil(sid->type->t) ? IL_PUSHVARREF : IL_PUSHVAR, sid->Idx());
    };
}

void Dot::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) cg.GenFieldAccess(*this, -1, maybe);
}

void Indexing::Generate(CodeGen &cg, int retval) const {
    cg.Gen(object, retval);
    cg.Gen(index, retval);
    if (retval) {
        cg.TakeTemp(2);
        switch (object->exptype->t) {
            case V_VECTOR: {
                auto etype = object->exptype;
                if (index->exptype->t == V_INT) {
                    etype = etype->Element();
                } else {
                    auto &struc = *index->exptype->struc;
                    for (auto &field : struc.fields.v) {
                        (void)field;
                        etype = etype->Element();
                    }
                }
                cg.Emit(IsRefNil(etype->t)
                        ? (index->exptype->t == V_INT ? IL_VPUSHIDXIREF : IL_VPUSHIDXVREF)
                        : (index->exptype->t == V_INT ? IL_VPUSHIDXI : IL_VPUSHIDXV));
                break;
            }
            case V_STRUCT:
                assert(index->exptype->t == V_INT && object->exptype->struc->sametype->Numeric());
                cg.Emit(IL_NPUSHIDXI);
                break;
            case V_STRING:
                assert(index->exptype->t == V_INT);
                cg.Emit(IL_SPUSHIDXI);
                break;
            default:
                assert(false);
        }
    }
}

void CoDot::Generate(CodeGen &cg, int retval) const {
    cg.Gen(coroutine, retval, true);
    if (retval) cg.Emit(IL_PUSHLOC, AssertIs<IdentRef>(variable)->sid->Idx());
}

void AssignList::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, (int)sids.size());
    for (int i = (int)sids.size() - 1; i >= 0; i--) {
        if (Is<Define>(this)) {
            if (sids[i]->id->logvar)
                cg.Emit(IL_LOGREAD, sids[i]->logvaridx);
        }
        cg.TakeTemp(1);
        cg.Emit(IL_LVALVAR, IsRefNil(sids[i]->type->t) ? LVO_WRITEREF : LVO_WRITE, sids[i]->Idx());
        cg.VarModified(sids[i]);
    }
    // currently can only happen with def on last line of body, which is nonsensical
    cg.Dummy(retval);
}

void Define::Generate(CodeGen &cg, int retval) const {
    AssignList::Generate(cg, retval);
}

void Assign::Generate(CodeGen &cg, int retval) const {
    cg.GenAssign(left,
        IsRefNil(left->exptype->t) ? LVO_WRITEREF : LVO_WRITE, retval,
        nullptr,
        right);
}

void PlusEq    ::Generate(CodeGen &cg, int retval) const {
    cg.GenAssign(left, LVO_IADD, retval, exptype, right);
}
void MinusEq   ::Generate(CodeGen &cg, int retval) const {
    cg.GenAssign(left, LVO_ISUB, retval, exptype, right);
}
void MultiplyEq::Generate(CodeGen &cg, int retval) const {
    cg.GenAssign(left, LVO_IMUL, retval, exptype, right);
}
void DivideEq  ::Generate(CodeGen &cg, int retval) const {
    cg.GenAssign(left, LVO_IDIV, retval, exptype, right);
}
void ModEq     ::Generate(CodeGen &cg, int retval) const {
    cg.GenAssign(left, LVO_IMOD, retval, exptype, right);
}

void PostDecr::Generate(CodeGen &cg, int retval) const { cg.GenAssign(child, LVO_IMMP, retval, exptype); }
void PostIncr::Generate(CodeGen &cg, int retval) const { cg.GenAssign(child, LVO_IPPP, retval, exptype); }
void PreDecr ::Generate(CodeGen &cg, int retval) const { cg.GenAssign(child, LVO_IMM,  retval, exptype); }
void PreIncr ::Generate(CodeGen &cg, int retval) const { cg.GenAssign(child, LVO_IPP,  retval, exptype); }

void NotEqual     ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_NE);  }
void Equal        ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_EQ);  }
void GreaterThanEq::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_GE);  }
void LessThanEq   ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_LE);  }
void GreaterThan  ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_GT);  }
void LessThan     ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_LT);  }
void Mod          ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_MOD); }
void Divide       ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_DIV); }
void Multiply     ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_MUL); }
void Minus        ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_SUB); }
void Plus         ::Generate(CodeGen &cg, int retval) const { cg.GenMathOp(this, retval, MOP_ADD); }

void UnaryMinus::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) {
        auto ctype = child->exptype;
        switch (ctype->t) {
            case V_INT: cg.Emit(IL_IUMINUS); break;
            case V_FLOAT: cg.Emit(IL_FUMINUS); break;
            case V_STRUCT:
            case V_VECTOR: {
                auto elem = ctype->struc->sametype->t;
                cg.Emit(elem == V_INT ? IL_IVUMINUS : IL_FVUMINUS);
                break;
            }
            default: assert(false);
        }
    }
}

void BitAnd    ::Generate(CodeGen &cg, int retval) const { cg.GenBitOp(this, retval, IL_BINAND); }
void BitOr     ::Generate(CodeGen &cg, int retval) const { cg.GenBitOp(this, retval, IL_BINOR); }
void Xor       ::Generate(CodeGen &cg, int retval) const { cg.GenBitOp(this, retval, IL_XOR); }
void ShiftLeft ::Generate(CodeGen &cg, int retval) const { cg.GenBitOp(this, retval, IL_ASL); }
void ShiftRight::Generate(CodeGen &cg, int retval) const { cg.GenBitOp(this, retval, IL_ASR); }

void Negate::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) cg.Emit(IL_NEG);
}

void ToFloat::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) cg.Emit(IL_I2F);
}

void ToString::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) cg.Emit(IL_A2S);
}

void ToAny::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) {
        switch (child->exptype->t) {
            case V_INT:   cg.Emit(IL_I2A); break;
            case V_FLOAT: cg.Emit(IL_F2A); break;
            default: break;  // Everything else is already compatible.
        }
    }
}

void ToNil::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, 0);
    cg.Dummy(retval);
}

void ToBool::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) cg.Emit(IsRefNil(child->exptype->t) ? IL_E2BREF : IL_E2B);
}

void ToInt::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    // No actual opcode needed, this node is purely to store correct types.
}

void FunRef::Generate(CodeGen &cg, int retval) const {
    if (retval)  {
        // If no body, then the function has been optimized away, meaning this
        // function value will never be used.
        // FIXME: instead, ensure such values are removed by the optimizer.
        if (sf->parent->anonymous && sf->body) {
            cg.Emit(IL_PUSHFUN, sf->subbytecodestart);
            cg.GenFixup(sf);
        } else {
            cg.Dummy(retval);
        }
    }
}

void StructRef::Generate(CodeGen &cg, int retval) const {
    cg.Dummy(retval);
}

void NativeCall::Generate(CodeGen &cg, int retval) const {
    // TODO: could pass arg types in here if most exps have types, cheaper than
    // doing it all in call instruction?
    size_t nargs = 0;
    auto lastarg = cg.GenArgs(this, nargs, this);
    cg.TakeTemp(nargs);
    assert(nargs == nf->args.size() && nargs <= 7);
    int vmop = IL_BCALLRET0 + (int)(nargs * 3);
    if (nf->has_body) { // graphics.h
        if (!Is<DefaultVal>(lastarg)) {
            cg.Emit(vmop, nf->idx);
            cg.Emit(IL_CALLVCOND);  // FIXME: doesn't need to be COND anymore?
            cg.EmitTempInfo(this);
            cg.SplitAttr(cg.Pos());
            assert(lastarg->exptype->t == V_FUNCTION);
            assert(!lastarg->exptype->sf->reqret);  // We never use the retval.
            cg.Emit(IL_CONT1, nf->idx);  // Never returns a value.
            cg.Dummy(retval);
        } else {
            if (!retval) vmop += 2;  // These always return nil.
            cg.Emit(vmop, nf->idx);
        }
    } else if (nf->name == "resume") {  // FIXME: make a vm op.
        cg.Emit(vmop, nf->idx);
        cg.SplitAttr(cg.Pos());
        if (!retval) cg.GenPop(exptype);
    } else {
        if (!retval) {
            // Generate version that never produces top of stack (but still may have
            // additional return values)
            vmop++;
            if (!IsRefNil(exptype->t)) vmop++;
        }
        cg.Emit(vmop, nf->idx);
    }
    if (nf->retvals.v.size() > 1) {
        for (auto &rv : nf->retvals.v) cg.rettypes.push_back(rv.type);
    } else if (!nf->retvals.v.size() && retval) {
        // FIXME: can't make this an error since these functions are often called as
        // the last thing in a function, sometimes still requiring a return value.
        // Check what still causes this to happen.
        // parser.Error(nf->name + " returns no value", n);
    }
    if (!retval) {
        // Top of stack has already been removed by op, but still need to pop any
        // additional values.
        while (cg.rettypes.size() > 1) {
            cg.GenPop(cg.rettypes.back());
            cg.rettypes.pop_back();
        }
        cg.rettypes.clear();
    }
}

void Call::Generate(CodeGen &cg, int retval) const {
    size_t nargs = 0;
    cg.GenCall(*sf, this, this, nargs, retval);
}

void DynCall::Generate(CodeGen &cg, int retval) const {
    size_t nargs = 0;
    if (sid->type->t == V_YIELD) {
        if (Arity()) {
            cg.GenArgs(this, nargs);
            cg.TakeTemp(nargs);
            assert(nargs == 1);
        } else {
            cg.Emit(IL_PUSHNIL);
        }
        cg.Emit(IL_YIELD);
        // We may have temps on the stack from an enclosing for.
        // Check that these temps are actually from for loops, to not mask bugs.
        assert(cg.temptypestack.size() == cg.nested_fors * 2);
        cg.EmitTempInfo(this);
        cg.SplitAttr(cg.Pos());
        if (!retval) cg.GenPop(exptype);
    } else {
        assert(sf && sf == sid->type->sf);
        // FIXME: in the future, we can make a special case for istype calls.
        if (!sf->parent->istype) {
            // We statically know which function this is calling.
            // We can now turn this into a normal call.
            cg.GenCall(*sf, this, this, nargs, retval);
        } else {
            cg.GenArgs(this, nargs);
            assert(nargs == sf->args.size());
            cg.Emit(IL_PUSHVAR, sid->Idx());
            cg.TakeTemp(nargs);
            cg.Emit(IL_CALLV);
            cg.EmitTempInfo(this);
            cg.SplitAttr(cg.Pos());
            if (sf->reqret) {
                if (!retval) cg.GenPop(exptype);
            } else {
                cg.Dummy(retval);
            }
        }
    }
}

void List::Generate(CodeGen & /*cg*/, int /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void TypeAnnotation::Generate(CodeGen & /*cg*/, int /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void Unary::Generate(CodeGen & /*cg*/, int /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void BinOp::Generate(CodeGen & /*cg*/, int /*retval*/) const {
    assert(false);  // Handled by individual parents.
}

void Inlined::Generate(CodeGen &cg, int retval) const {
    for (auto c : children) {
        cg.Gen(c, c != children.back() ? 0 : retval, retval != 0);
    }
}

void Seq::Generate(CodeGen &cg, int retval) const {
    cg.Gen(head, 0);
    cg.Gen(tail, retval, true);
}

void MultipleReturn::Generate(CodeGen &cg, int retval) const {
    for (auto c : children) cg.Gen(c, retval != 0);
    if (retval) {
        assert((int)Arity() == retval);
        cg.TakeTemp(Arity());
        for (auto c : children) cg.rettypes.push_back(c->exptype);
    }
}

void NativeRef::Generate(CodeGen & /*cg*/, int /*retval*/) const {
    assert(false);
}

void And::Generate(CodeGen &cg, int retval) const {
    cg.Gen(left, 1, true);
    cg.Emit(cg.JumpRef(retval ? IL_JUMPFAILR : IL_JUMPFAIL, left->exptype), 0);
    auto loc = cg.Pos();
    cg.Gen(right, retval, true);
    cg.SetLabel(loc);
}

void Or::Generate(CodeGen &cg, int retval) const {
    cg.Gen(left, 1, true);
    cg.Emit(cg.JumpRef(retval ? IL_JUMPNOFAILR : IL_JUMPNOFAIL, left->exptype), 0);
    auto loc = cg.Pos();
    cg.Gen(right, retval, true);
    cg.SetLabel(loc);
}

void Not::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    if (retval) cg.Emit(IsRefNil(child->exptype->t) ? IL_LOGNOTREF : IL_LOGNOT);
}

void If::Generate(CodeGen &cg, int retval) const {
    cg.Gen(condition, 1, true);
    bool has_else = !Is<DefaultVal>(falsepart);
    cg.Emit(cg.JumpRef(!has_else && retval ? IL_JUMPFAILN : IL_JUMPFAIL, condition->exptype), 0);
    auto loc = cg.Pos();
    if (has_else) {
        cg.Gen(truepart, retval, true);
        cg.Emit(IL_JUMP, 0);
        auto loc2 = cg.Pos();
        cg.SetLabel(loc);
        cg.Gen(falsepart, retval, true);
        cg.SetLabel(loc2);
    } else {
        // If retval, then this will generate nil thru T_E2N
        cg.Gen(truepart, retval, true);
        cg.SetLabel(loc);
    }
}

void While::Generate(CodeGen &cg, int retval) const {
    cg.SplitAttr(cg.Pos());
    auto loopback = cg.Pos();
    cg.Gen(condition, 1, true);
    cg.Emit(cg.JumpRef(IL_JUMPFAIL, condition->exptype), 0);
    auto jumpout = cg.Pos();
    cg.Gen(body, 0);
    cg.Emit(IL_JUMP, loopback);
    cg.SetLabel(jumpout);
    cg.Dummy(retval);
}

void For::Generate(CodeGen &cg, int retval) const {
    cg.Emit(IL_PUSHINT, -1);   // i
    cg.temptypestack.push_back(type_int);
    cg.Gen(iter, 1);
    cg.nested_fors++;
    cg.Emit(IL_JUMP, 0);
    auto startloop = cg.Pos();
    cg.SplitAttr(cg.Pos());
    cg.Gen(body, 0);
    cg.SetLabel(startloop);
    switch (iter->exptype->t) {
        case V_INT:    cg.Emit(IL_IFOR); break;
        case V_STRING: cg.Emit(IL_SFOR); break;
        case V_VECTOR: cg.Emit(IL_VFOR); break;
        case V_STRUCT: cg.Emit(IL_NFOR); break;
        default:       assert(false);
    }
    cg.Emit(startloop);
    cg.nested_fors--;
    cg.TakeTemp(2);
    cg.Dummy(retval);
}

void ForLoopElem::Generate(CodeGen &cg, int /*retval*/) const {
    auto type = cg.temptypestack.back();
    switch (type->t) {
        case V_INT:    cg.Emit(IL_IFORELEM); break;
        case V_STRING: cg.Emit(IL_SFORELEM); break;
        case V_VECTOR: cg.Emit(IsRefNil(type->sub->t) ? IL_VFORELEMREF : IL_VFORELEM); break;
        case V_STRUCT: cg.Emit(IL_NFORELEM); break;
        default:       assert(false);
    }
}

void ForLoopCounter::Generate(CodeGen &cg, int /*retval*/) const {
    cg.Emit(IL_FORLOOPI);
}

void Switch::Generate(CodeGen &cg, int retval) const {
    // TODO: create specialized version for dense range of ints with jump table.
    cg.Gen(value, 1, true);
    vector<int> nextcase, thiscase, exitswitch;
    for (auto n : cases->children) {
        for (auto loc : nextcase) cg.SetLabel(loc);
        nextcase.clear();
        auto cas = AssertIs<Case>(n);
        for (auto c : cas->pattern->children) {
            auto is_last = c == cas->pattern->children.back();
            cg.GenDup(value->exptype);
            cg.temptypestack.push_back(value->exptype);
            auto compare_one = [&](MathOp op, Node *cn) {
                cg.Gen(cn, 1);
                cg.GenMathOp(value->exptype, c->exptype, value->exptype, op);
            };
            auto compare_one_jump = [&](MathOp op, Node *cn) {
                compare_one(op, cn);
                cg.Emit(is_last ? IL_JUMPFAIL : IL_JUMPNOFAIL, 0);
                (is_last ? nextcase : thiscase).push_back(cg.Pos());
            };
            if (auto r = Is<Range>(c)) {
                compare_one(MOP_GE, r->start);
                cg.Emit(IL_JUMPFAIL, 0);
                auto loc = cg.Pos();
                if (is_last) nextcase.push_back(loc);
                cg.GenDup(value->exptype);
                cg.temptypestack.push_back(value->exptype);
                compare_one_jump(MOP_LE, r->end);
                if (!is_last) cg.SetLabel(loc);
            } else {
                // FIXME: if this is a string, will alloc a temp string object just for the sake of
                // comparison. Better to create special purpose opcode to compare with const string.
                compare_one_jump(MOP_EQ, c);
            }
        }
        for (auto loc : thiscase) cg.SetLabel(loc);
        thiscase.clear();
        cg.GenPop(value->exptype);
        cg.Gen(cas->body, retval, true);
        if (n != cases->children.back()) {
            cg.Emit(IL_JUMP, 0);
            exitswitch.push_back(cg.Pos());
        }
    }
    for (auto loc : exitswitch) cg.SetLabel(loc);
}

void Case::Generate(CodeGen &/*cg*/, int /*retval*/) const {
    assert(false);
}

void Range::Generate(CodeGen &/*cg*/, int /*retval*/) const {
    assert(false);
}

void Constructor::Generate(CodeGen &cg, int retval) const {
    // FIXME: a malicious script can exploit this for a stack overflow.
    for (auto c : children) cg.Gen(c, 1);
    cg.TakeTemp(Arity());
    auto vtype = exptype;
    auto offset = cg.GetTypeTableOffset(vtype);
    if (vtype->t == V_STRUCT) {
        assert(vtype->struc->fields.size() == Arity());
        cg.Emit(IL_NEWSTRUCT, offset);
    } else {
        assert(vtype->t == V_VECTOR);
        cg.Emit(IL_NEWVEC, offset, (int)Arity());
    }
    if (!retval) cg.Emit(IL_POPREF);
}

void IsType::Generate(CodeGen &cg, int retval) const {
    cg.Gen(child, retval, true);
    // If the value was a scalar, then it always results in a compile time type check,
    // which means this T_IS would have been optimized out. Which means from here on we
    // can assume its a ref.
    assert(!IsUnBoxed(child->exptype->t));
    if (retval) {
        cg.Emit(IL_ISTYPE, cg.GetTypeTableOffset(giventype));
    }
}

void Return::Generate(CodeGen &cg, int /*retval*/) const {
    assert(!cg.rettypes.size());
    if (cg.temptypestack.size()) {
        // We have temps on the stack from an enclosing for.
        // We can't actually remove these from the stack as the parent nodes still
        // expect them to be there.
        // Check that these temps are actually from for loops, to not mask bugs.
        assert(cg.temptypestack.size() == cg.nested_fors * 2);
        for (int i = (int)cg.temptypestack.size() - 1; i >= 0; i--) {
            cg.GenPop(cg.temptypestack[i]);
        }
    }
    auto sf = subfunction_idx >= 0 ? cg.st.subfunctiontable[subfunction_idx] : nullptr;
    int fid = subfunction_idx >= 0 ? sf->parent->idx : subfunction_idx;
    int nretvals = sf ? sf->parent->nretvals : 1;
    if (nretvals > MAX_RETURN_VALUES) cg.parser.Error("too many return values");
    if (!sf || sf->reqret) {
        if (!Is<DefaultVal>(child)) cg.Gen(child, nretvals, true);
        else { cg.Emit(IL_PUSHNIL); assert(nretvals == 1); }
    } else {
        if (!Is<DefaultVal>(child)) cg.Gen(child, 0, true);
        nretvals = 0;
    }
    // FIXME: we could change the VM to instead work with SubFunction ids.
    cg.Emit(IL_RETURN, fid, nretvals, cg.GetTypeTableOffset(exptype));
    // retval==true is nonsensical here, but can't enforce
}

void CoClosure::Generate(CodeGen &cg, int retval) const {
    if (retval) cg.Emit(IL_COCL);
}

void CoRoutine::Generate(CodeGen &cg, int retval) const {
    cg.Emit(IL_CORO, 0);
    auto loc = cg.Pos();
    auto sf = exptype->sf;
    assert(exptype->t == V_COROUTINE && sf);
    cg.Emit(cg.GetTypeTableOffset(exptype));
    // TODO: We shouldn't need to store this table for each call, instead do it once for
    // each function.
    cg.Emit((int)sf->coyieldsave.v.size());
    for (auto &arg : sf->coyieldsave.v) cg.Emit(arg.sid->Idx());
    cg.Gen(call, 1, true);
    cg.Emit(IL_COEND);
    cg.SetLabel(loc);
    if (!retval) cg.Emit(IL_POPREF);
}

void TypeOf::Generate(CodeGen &cg, int /*retval*/) const {
    if (auto dv = Is<DefaultVal>(child)) {
        // FIXME
        if (!cg.temp_parent || !Is<NativeCall>(cg.temp_parent))
            cg.parser.Error("typeof return out of call context", dv);
        cg.Emit(IL_PUSHINT, cg.GetTypeTableOffset(cg.temp_parent->exptype));
    } else  if (auto idr = Is<IdentRef>(child)) {
        cg.Emit(IL_PUSHINT, cg.GetTypeTableOffset(idr->exptype));
    } else {
        auto ta = AssertIs<TypeAnnotation>(child);
        cg.Emit(IL_PUSHINT, cg.GetTypeTableOffset(ta->giventype));
    }
}

}  // namespace lobster
