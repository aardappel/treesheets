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

struct LValContext {
    // For now, only: ident ( . field )*.
    const SpecIdent *sid;
    small_vector<SharedField *, 3> derefs;
    LValContext(SpecIdent *sid) : sid(sid) {}
    LValContext(const Node &n) {
        auto t = &n;
        while (auto dot = Is<Dot>(t)) {
            derefs.insert(0, dot->fld);
            t = dot->child;
        }
        auto idr = Is<IdentRef>(t);
        sid = idr ? idr->sid : nullptr;
    }
    bool IsValid() { return sid; }
    bool DerefsEqual(const LValContext &o) {
        if (derefs.size() != o.derefs.size()) return false;
        for (auto &shf : derefs) if (shf != o.derefs[&shf - &derefs[0]]) return false;
        return true;
    }
    bool IsPrefix(const LValContext &o) {  // Is o a prefix of this?
        if (sid != o.sid || derefs.size() < o.derefs.size()) return false;
        for (auto &shf : o.derefs) if (shf != derefs[&shf - &o.derefs[0]]) return false;
        return true;
    }
    string Name() {
        auto s = sid ? sid->id->name : "<invalid>";
        for (auto &shf : derefs) {
            s += ".";
            s += shf->name;
        }
        return s;
    }
};

struct FlowItem : LValContext {
    TypeRef old, now;
    FlowItem(const Node &n, TypeRef type) : LValContext(n), old(n.exptype), now(type) {}
    FlowItem(SpecIdent *sid, TypeRef old, TypeRef now) : LValContext(sid), old(old), now(now) {}
};

struct Borrow : LValContext {
    int refc = 1;  // Number of outstanding borrowed values. While >0 can't assign.
    Borrow(const Node &n) : LValContext(n) {}
};

enum ConvertFlags {
    CF_NONE        = 0,

    CF_COERCIONS   = 1 << 0,
    CF_UNIFICATION = 1 << 1,
    CF_NUMERIC_NIL = 1 << 2,
    CF_EXACTTYPE   = 1 << 3,
    CF_COVARIANT   = 1 << 4,
};

struct TypeChecker {
    Parser &parser;
    SymbolTable &st;
    struct Scope {
        SubFunction *sf;
        const Node *call_context;
        int loop_count = 0;
        vector<Member *> scoped_fields;
    };
    vector<Scope> scopes, named_scopes;
    vector<FlowItem> flowstack;
    vector<Borrow> borrowstack;
    set<pair<Line, int64_t>> integer_literal_warnings;
    Query *query;
    bool full_error;
    Switch *switch_case_context = nullptr;
    set<pair<SubFunction *, SubFunction *>> freevar_check_preempt;

    TypeChecker(Parser &_p, SymbolTable &_st, size_t retreq, Query *query, bool full_error)
        : parser(_p), st(_st), query(query), full_error(full_error) {
        st.functions.clear();
        st.type_check_call_back = [&](UDT &udt) {
            // FIXME: this gives entirely wrong error context!
            TypeCheckUDT(udt, *scopes.back().call_context);
        };
        // FIXME: this is unfriendly.
        if (!st.RegisterDefaultTypes())
            Error(*parser.root, "cannot find standard types (from stdtype.lobster)");
        for (auto sf : st.subfunctiontable) {
            // TODO: This is not great, it mostly just substitutes SpecUDTs with no specialization for
            // the UDT such that function overload selection can work. We could also not do this
            // here, and instead improve that code.
            for (auto [i, arg] : enumerate(sf->args)) {
                auto type = sf->giventypes[i];
                if (type->t == V_UUDT &&
                    (type->spec_udt->specializers.empty() || !type->spec_udt->IsGeneric())) {
                    arg.type = SingleNonGenericSpecialization(*type->spec_udt->gudt);
                }
            }
        }
        AssertIs<Call>(parser.root)->sf->reqret = retreq;
        TT(parser.root, retreq, LT_KEEP);
        CleanUpFlow(0);
        assert(borrowstack.empty());
        assert(scopes.empty());
        assert(named_scopes.empty());
    }

    // Needed for any sids in cloned code.
    void UpdateCurrentSid(SpecIdent *&sid) { sid = sid->Current(); }
    void RevertCurrentSid(SpecIdent *&sid) { sid->Current() = sid; }

    string SignatureWithFreeVars(const SubFunction &sf, set<Ident *> *already_seen) {
        string s = Signature(sf) + " { ";
        size_t j = 0;
        for (auto [i, freevar] : enumerate(sf.freevars)) {
            if (freevar.type->t != V_FUNCTION &&
                !freevar.sid->id->static_constant &&
                (!already_seen || already_seen->find(freevar.sid->id) == already_seen->end())) {
                FormatArg(s, freevar.sid->id->name, j++, freevar.type);
                if (already_seen) already_seen->insert(freevar.sid->id);
            }
        }
        s += "}";
        return s;
    }

    string ArgName(size_t i) {
        switch (i) {
            case 0: return "1st";
            case 1: return "2nd";
            case 2: return "3rd";
            default: return cat(i + 1, "th");
        }
    }

    string_view NiceName(const Node &n) {
        if (auto call = Is<Call>(n))
            if (!call->sf->parent->anonymous)
                return call->sf->parent->name;
        if (auto idr = Is<IdentRef>(n))
            return idr->sid->id->name;
        return n.Name();
    }

    template<typename... Ts> void Error(const Node &n, const Ts &...args) {
        auto err = cat(args...);
        set<Ident *> already_seen;
        if (!scopes.empty()) {
            size_t scope_count = 0;
            for (auto scope : reverse(scopes)) {
                if (scope.sf == st.toplevel) continue;
                err += "\n  in " + parser.lex.Location(scope.call_context->line) + ": ";
                if (full_error) {
                    err += SignatureWithFreeVars(*scope.sf, &already_seen);
                    for (auto dl : scope.sf->sbody->children) {
                        if (auto def = Is<Define>(dl)) {
                            if (Is<DefaultVal>(def->child)) continue;  // A pre-decl.
                            for (auto p : def->sids) {
                                err += ", " + p.first->id->name + ":" + TypeName(p.first->type);
                            }
                        }
                    }
                } else {
                    err += Signature(*scope.sf);
                    scope_count++;
                    if (scope_count == 5 && scopes.size() > 7) {
                        err += cat("\n  (", scopes.size() - scope_count,
                                   " more functions omitted, --full- error to see more) ");
                        break;
                    }
                }
            }
        }
        parser.ErrorAt(&n, err);
    }

    void RequiresError(string_view required, TypeRef got, const Node &n, string_view argname = "",
                       string_view context = "") {
        Error(n, Q(context.size() ? context : NiceName(n)), " ",
                 (argname.size() ? "(" + argname + " argument) " : ""),
                 "requires type ", Q(required), ", got ", Q(TypeName(got)));
    }

    void NoStruct(const Node &n, string_view context) {
        if (IsStruct(n.exptype->t)) Error(n, "struct value cannot be used in ", Q(context));
    }

    void NatCallError(string_view errstr, const NativeFun *nf, const NativeCall &callnode) {
        auto err = errstr + nf->name;
        err += "\n  got:";
        for (auto c : callnode.children) {
            err += " " + TypeName(c->exptype);
        }
        for (auto cnf = nf->first; cnf; cnf = cnf->overloads) {
            err += "\n  overload: " + Signature(*cnf);
        }
        Error(callnode, err);
    }

    void AmbiguousOverloadError(const List &call_args, const Function &f, const TypeRef &type0,
                                const vector<Overload *> &from) {
        string err = "multiple overloads for `" + f.name +"` match the argument types `(";
        for (size_t a = 0; a < f.nargs(); a++) {
            if (a != 0) err += ", ";
            err += TypeName(a ? call_args.children[a]->exptype : type0);
        }
        err += ")`";
        for (auto ov : from)
               err += "\n  overload: " + Signature(*ov->sf);
        Error(call_args, err);
    }

    void UnifyVar(TypeRef type, TypeRef hasvar, ValueType var_parent) {
        // Typically Type is const, but this is the one place we overwrite them.
        // Type objects that are V_VAR are seperate heap instances, so overwriting them has no
        // side-effects on non-V_VAR Type instances.
        assert(hasvar->t == V_VAR);
        if (type->t == V_VAR) {
            // If these two are already part of the same cycle, don't do the swap, which
            // could disconnect the cycle!
            auto v = hasvar;
            do {  // Loop thru all vars in unification cycle.
                if (&*v == &*type) return;  // Same cycle.
                v = v->sub;
            } while (&*v != &*hasvar);  // Force TypeRef pointer comparison.
            // Combine two cyclic linked lists.. elegant!
            swap((Type *&)hasvar->sub, (Type *&)type->sub);
        } else {
            auto v = hasvar;
            do { // Loop thru all vars in unification cycle.
                auto next = v->sub;
                assert(var_parent != V_NIL || !type->Numeric());
                (void)var_parent;
                *(Type *)&*v = *type;  // Overwrite Type struct!
                v = next;
            } while (&*v != &*hasvar);  // Force TypeRef pointer comparison.
            // TODO: A fundamental problem with this overwriting is that we have to rely on
            // the caller to not allow to create non-sensical types, like a nil of nil,
            // but we can't assert that this isn't happening without knowing the parent of hasvar.
        }
    }

    bool ConvertsTo(TypeRef type, TypeRef bound, ConvertFlags cf,
                    ValueType type_parent = V_UNDEFINED, ValueType bound_parent = V_UNDEFINED) {
        if (bound->Equal(*type)) return true;
        if (type->t == V_VAR) {
            if (cf & CF_UNIFICATION) UnifyVar(bound, type, type_parent);
            return true;
        }
        switch (bound->t) {
            case V_VOID:
                return cf & CF_COERCIONS;
            case V_VAR:
                if (cf & CF_UNIFICATION) UnifyVar(type, bound, bound_parent);
                return cf & CF_UNIFICATION;
            case V_FLOAT:
                return type->t == V_INT && (cf & CF_COERCIONS);
            case V_INT:
                return (type->t == V_TYPEID && (cf & CF_COERCIONS)) ||
                       (type->t == V_INT && !bound->e);
            case V_FUNCTION:
                // This just here to accept overloads from "L" in natreg.h (hash).
                return type->t == V_FUNCTION &&
                       !bound->sf &&
                       type->sf->returntype->t == V_VOID &&
                       type->sf->args.empty();
            case V_NIL: {
                auto scf = ConvertFlags(cf & CF_UNIFICATION);
                return (type->t == V_NIL && ConvertsTo(type->Element(), bound->Element(), scf, V_NIL, V_NIL)) ||
                       (!type->Numeric() && type->t != V_VOID && !IsStruct(type->t) &&
                        ConvertsTo(type, bound->Element(), scf, type_parent, V_NIL)) ||
                       ((cf & CF_NUMERIC_NIL) && type->Numeric() &&  // For builtins.
                        ConvertsTo(type, bound->Element(), scf, type_parent, V_NIL));
            }
            case V_VECTOR: {
                // We don't generally allow covariance here unless const (to avoid supertype
                // elements added to subtype vectors) and no contravariance.
                auto cov = cf & CF_COVARIANT ? CF_NONE : CF_EXACTTYPE;
                return type->t == V_VECTOR &&
                       ConvertsTo(type->Element(), bound->Element(),
                                  ConvertFlags((cf & (CF_UNIFICATION | CF_NUMERIC_NIL)) | cov), V_VECTOR, V_VECTOR);
            }
            case V_CLASS: {
                if (type->t != V_CLASS) return false;
                auto sd = SuperDistance(bound->udt, type->udt);
                return cf & CF_EXACTTYPE ? sd == 0 : sd >= 0;
            }
            case V_STRUCT_R:
            case V_STRUCT_S: {
                if (type->t != bound->t) return false;
                if (SuperDistance(bound->udt, type->udt) < 0) return false;
                // If number of fields is same as bound then this trivially converts.
                if (type->udt->sfields.size() == bound->udt->sfields.size()) return true;
                // This can convert if we chop off extra fields.
                return (cf & CF_COERCIONS) != 0;
            }
            case V_TUPLE:
                return type->t == V_TUPLE && ConvertsToTuple(*type->tup, *bound->tup);
            case V_TYPEID:
                if (type->t != V_TYPEID) return false;
                bound = bound->sub;
                type = type->sub;
                // This is minimalistic, but suffices for the current uses of V_TYPEID.
                return bound->t == V_ANY;
            default:
                return false;
        }
    }

    bool ConvertsToTuple(const vector<Type::TupleElem> &ttup, const vector<Type::TupleElem> &stup) {
        if (ttup.size() != stup.size()) return false;
        for (auto [i, te] : enumerate(ttup))
            if (!ConvertsTo(te.type, stup[i].type, CF_UNIFICATION, V_TUPLE, V_TUPLE))
                return false;
        return true;
    }

    TypeRef Union(TypeRef at, TypeRef bt, string_view aname, string_view bname,
                  ConvertFlags coercions, const Node *err,
                  ValueType a_parent = V_UNDEFINED, ValueType b_parent = V_UNDEFINED) {
        if (ConvertsTo(at, bt, ConvertFlags(coercions | CF_UNIFICATION), a_parent, b_parent))
            return bt;
        if (ConvertsTo(bt, at, ConvertFlags(coercions | CF_UNIFICATION), a_parent, b_parent))
            return at;
        if (at->t == V_VECTOR && bt->t == V_VECTOR) {
            auto et = Union(at->Element(), bt->Element(), aname, bname, CF_NONE, nullptr, V_VECTOR, V_VECTOR);
            if (et->t == V_UNDEFINED) goto error;
            return st.Wrap(et, V_VECTOR, err ? &err->line : nullptr);
        }
        if (at->t == V_NIL || bt->t == V_NIL) {
            auto ate = at->ElementIfNil();
            auto bte = bt->ElementIfNil();
            if (IsUnBoxedOrStruct(ate->t) || IsUnBoxedOrStruct(bte->t)) goto error;
            auto et = Union(ate, bte, aname, bname, CF_NONE, nullptr,
                        at->t == V_NIL ? V_NIL : a_parent,
                        bt->t == V_NIL ? V_NIL : b_parent);
            if (et->t == V_UNDEFINED) goto error;
            return st.Wrap(et, V_NIL, err ? &err->line : nullptr);
        }
        if (at->t == V_CLASS && bt->t == V_CLASS) {
            auto sstruc = CommonSuperType(at->udt, bt->udt);
            if (sstruc) return &sstruc->thistype;
        }
        error:
        if (err) {
            Error(*err, Q(TypeName(at)), " (", aname, ") and ", Q(TypeName(bt)), " (", bname,
                        ") have no common supertype");
        }
        return type_undefined;
    }

    void MakeString(Node *&a, Lifetime orig_recip) {
        assert(a->exptype->t != V_STRING);
        DecBorrowers(a->lt, *a);
        a = new ToString(a->line, a);
        a->exptype = type_string;
        a->lt = LT_KEEP;
        // Make sure whatever lifetime a was typechecked at is preserved.
        AdjustLifetime(a, orig_recip);
    }

    void MakeBool(Node *&a) {
        DecBorrowers(a->lt, *a);
        if (a->exptype->t == V_INT) return;
        a = new ToBool(a->line, a);
        a->exptype = &st.default_bool_type->thistype;
        a->lt = LT_ANY;
    }

    void MakeInt(Node *&a) {
        auto ti = new ToInt(a->line, a);
        ti->exptype = type_int;
        ti->lt = a->lt;
        a = ti;
    }

    void MakeFloat(Node *&a) {
        auto tf = new ToFloat(a->line, a);
        tf->exptype = type_float;
        tf->lt = a->lt;
        a = tf;
    }

    void MakeLifetime(Node *&n, Lifetime lt, uint64_t incref, uint64_t decref) {
        auto tlt = new ToLifetime(n->line, n, incref, decref);
        tlt->exptype = n->exptype;
        tlt->lt = lt;
        n = tlt;
    }

    void MakeStructSuper(Node *&a, TypeRef structsuper) {
        auto ss = new ToStructSuper(a->line, a);
        ss->exptype = structsuper;
        ss->lt = a->lt;
        a = ss;
    }

    void StorageType(TypeRef type, const Node &context) {
        if (type->HasValueType(V_VOID))
            Error(context, "cannot store value of type ", Q(TypeName(type)));
    }

    void SubTypeLR(TypeRef bound, BinOp &n) {
        SubType(n.left, bound, "left", n);
        SubType(n.right, bound, "right", n);
    }

    void SubType(Node *&a, TypeRef bound, string_view argname, const Node &context,
                 ConvertFlags extra = CF_NONE) {
        SubType(a, bound, argname, NiceName(context), extra);
    }
    void SubType(Node *&a, TypeRef bound, string_view argname, string_view context,
                 ConvertFlags extra = CF_NONE) {
        // Special path for functions, since they may need type-checking.
        if (bound->t == V_FUNCTION && a->exptype->IsFunction()) {
            // See if these functions can be made compatible. Specialize and typecheck if
            // needed.
            auto sf = a->exptype->sf;
            if (sf == bound->sf) {
                // Early out: this happens when function types pass to function types.
                // Or sometimes even specific function value types passed on to another copy.
                return;
            }
            size_t reqret = 0;
            auto returntype = type_void;
            vector<Arg> no_args;
            auto args = &no_args;
            if (bound->sf) {
                reqret = bound->sf->reqret;
                returntype = bound->sf->returntype;
                args = &bound->sf->args;
                if (!bound->sf->parent->istype)
                    Error(*a,
                            "dynamic function value can only be passed to declared function "
                            "type");
            }
            if (sf->args.size() != args->size())
                goto error;
            for (auto [i, arg] : enumerate(sf->args)) {
                // Specialize to the function type, if requested.
                if (!sf->parent->istype) {
                    if (!sf->typechecked && st.IsGeneric(sf->giventypes[i])) {
                        arg.type = (*args)[i].type;
                    } else {
                        arg.type = st.ResolveTypeVars(sf->giventypes[i], a->line);
                    }
                }
                // Note this has the args in reverse: function args are contravariant.
                if (!ConvertsTo((*args)[i].type, arg.type, CF_UNIFICATION))
                    goto error;
                // This function must be compatible with all other function values that
                // match this type, so we fix lifetimes to LT_BORROW.
                // See typechecking of istype calls.
                if (!sf->parent->istype) arg.sid->lt = LT_BORROW;
            }
            if (sf->typechecked) {
                if (sf->reqret != reqret)
                    goto error;
            }
            if (!sf->parent->istype) {
                if (!sf->typechecked) {
                    sf->reqret = reqret;
                }
                sf->isdynamicfunctionvalue = true;
                assert(sf->freevarchecked);  // Must have been pre-specialized.
                // FIXME: shouldn't all functions arriving here already have
                // been cloned by PreSpecializeFunction? Move clone there?
                if (!sf->sbody) sf = CloneFunction(*sf->parent->overloads[0]);
                TypeCheckFunctionDef(*sf, *sf->sbody);
            }
            // Covariant again.
            if (sf->returntype->NumValues() != returntype->NumValues() ||
                !ConvertsTo(sf->returntype, returntype, CF_UNIFICATION))
                goto error;
            // Parser only parses one ret type for function types.
            assert(returntype->NumValues() <= 1);
            return;
        }
        // TODO: generalize this into check if `a` is un-aliased.
        if (Is<Constructor>(a)) {
            extra = ConvertFlags(CF_COVARIANT | extra);
        }
        if (ConvertsTo(a->exptype, bound, ConvertFlags(CF_UNIFICATION | extra))) {
            return;
        }
        // Here follow the cases that needs explicit coercion code to be made compatible.
        switch (bound->t) {
            case V_FLOAT:
                if (a->exptype->t == V_INT) {
                    if (auto ic = Is<IntConstant>(a)) {
                        auto this_warn = make_pair(ic->line, ic->integer);
                        if (integer_literal_warnings.insert(this_warn).second) {
                            parser.WarnAt(a, "integer literal (", ic->integer,
                                          ") where float expected");
                        }
                    }
                    MakeFloat(a);
                    return;
                }
                break;
            case V_INT:
                if (a->exptype->t == V_TYPEID) {
                    MakeInt(a);
                    return;
                }
                break;
            case V_STRUCT_R:
            case V_STRUCT_S: {
                if (a->exptype->t == bound->t &&
                    SuperDistance(bound->udt, a->exptype->udt) > 0 &&
                    a->exptype->udt->sfields.size() > bound->udt->sfields.size()) {
                    MakeStructSuper(a, bound);
                    return;
                }
                break;
            }
            default:
                ;
        }
        error:
        RequiresError(TypeName(bound), a->exptype, *a, argname, context);
    }

    void SubTypeT(TypeRef type, TypeRef bound, const Node &n, string_view argname,
                  string_view context = {}) {
        if (!ConvertsTo(type, bound, CF_UNIFICATION))
            RequiresError(TypeName(bound), type, n, argname, context);
    }

    bool MathCheckVector(TypeRef &type, Node *&left, Node *&right, bool flipped = false) {
        TypeRef ltype = left->exptype;
        TypeRef rtype = right->exptype;
        // Special purpose check for vector * scalar etc.
        if (ltype->t == V_STRUCT_S && rtype->Numeric()) {
            auto etype = ltype->udt->sametype;
            if (etype->Numeric()) {
                if (etype->t == V_INT) {
                    // Don't implicitly convert int vectors to float.
                    if (rtype->t == V_FLOAT) return false;
                } else {
                    if (rtype->t == V_INT) SubType(right, type_float, "right", *right);
                }
                type = &ltype->udt->thistype;
                return true;
            }
        }
        if (!flipped) { return MathCheckVector(type, right, left, true); }
        return false;
    }

    const char *MathCheck(TypeRef &type, BinOp &n, bool &unionchecked,
                          bool typechangeallowed) {
        if (type->Numeric() || IsStruct(type->t))
            return nullptr;
        if (MathCheckVector(type, n.left, n.right)) {
            unionchecked = true;
            return nullptr;
        }
        if (!Is<Plus>(&n) && !Is<PlusEq>(&n))
            return "numeric/struct";
        // Special purpose checking for + on strings.
        auto ltype = n.left->exptype;
        auto rtype = n.right->exptype;
        if (ltype->t == V_STRING) {
            if (rtype->t != V_STRING) {
                // Anything can be added to a string on the right (because of +=).
                MakeString(n.right, LT_BORROW);
                // Make sure the overal type is string.
                type = type_string;
                unionchecked = true;
            }
            return nullptr;
        } else if (rtype->t == V_STRING && ltype->t != V_STRING && typechangeallowed) {
            // Only if not in a +=
            MakeString(n.left, LT_BORROW);
            type = type_string;
            unionchecked = true;
            return nullptr;
        } else {
            return "numeric/string/struct";
        }
    }

    void MathError(TypeRef &type, BinOp &n, bool &unionchecked, bool typechangeallowed) {
        auto err = MathCheck(type, n, unionchecked, typechangeallowed);
        if (err) {
            if (MathCheck(n.left->exptype, n, unionchecked, typechangeallowed))
                RequiresError(err, n.left->exptype, n, "left");
            if (MathCheck(n.right->exptype, n, unionchecked, typechangeallowed))
                RequiresError(err, n.right->exptype, n, "right");
            Error(n, "can\'t use ", Q(NiceName(n)), " on ", Q(TypeName(n.left->exptype)),
                     " and ", Q(TypeName(n.right->exptype)));
        }
    }

    Node *OperatorOverload(Node &n) {
        TT(n.Children()[0], 1, LT_ANY);
        // If this is not an overload, we return nullptr and the first child
        // will already have been typechecked with LT_BORROW.
        auto no_overload = [&]() {
            AdjustLifetime(n.Children()[0], LT_BORROW);
            return nullptr;
        };
        auto child1 = n.Children()[0];
        if (!IsUDT(child1->exptype->t)) return no_overload();
        auto opname = TName(T_OPERATOR) + (n.Name() == "indexing operation" ? "[]" : n.Name());
        auto it = st.operators.find(opname);
        if (it == st.operators.end()) return no_overload();
        auto f = it->second->first;
        while (f->nargs() != n.Arity()) {
            f = f->sibf;
            if (!f) return no_overload();
        }
        vector<Overload *> candidates;
        for (auto [i, ov] : enumerate(f->overloads)) {
            // FIXME: we are replicating some of the general overload selection that happens in
            // TypeCheckCall here.
            auto atype = ov->sf->args[0].type;
            if ((atype->t == V_UUDT && atype->spec_udt->gudt == &child1->exptype->udt->g) ||
                ConvertsTo(child1->exptype, atype, CF_NONE)) {
                if (n.SideEffect() && IsStruct(child1->exptype->t))
                    Error(n, "struct types can\'t model side effecting overloaded operators");
                candidates.push_back(ov);
            }
        }
        if (candidates.empty()) return no_overload();
        if (n.Arity() > 1) TT(n.Children()[1], 1, LT_ANY);
        if (candidates.size() > 1) {
            // FIXME: odd we support overloading on 2nd arg here, and not elsewhere..
            // generalize this!
            if (n.Arity() == 1)
                Error(n, "identical overloads for " + opname);
            auto child2 = n.Children()[1];
            candidates.erase(remove_if(candidates.begin(), candidates.end(), [&](Overload *ov) {
                    return !ConvertsTo(child2->exptype, ov->sf->args[1].type, CF_NONE);
                }),
                candidates.end());
            if (candidates.empty())
                Error(n, "no overloads apply based on 2nd arg to " + opname);
            if (candidates.size() > 1)
                Error(n, "multiple overloads apply based on 2nd arg to " + opname);
        }
        auto c = new Call(n.line, candidates[0]->sf);
        c->children.append(n.Children(), n.Arity());
        n.ClearChildren();
        c->exptype = TypeCheckCallStatic(c->sf, *c, 1, nullptr, *candidates[0],
                                         true, false, false);
        c->lt = c->sf->ltret;
        delete &n;
        return c;
    }

    Node *TypeCheckMathOp(BinOp &n) {
        if (auto nn = OperatorOverload(n)) return nn;
        TT(n.right, 1, LT_BORROW);
        n.exptype = Union(n.left->exptype, n.right->exptype, "lhs", "rhs", CF_COERCIONS, nullptr);
        bool unionchecked = false;
        MathError(n.exptype, n, unionchecked, true);
        if (!unionchecked) SubTypeLR(n.exptype, n);
        DecBorrowers(n.left->lt, n);
        DecBorrowers(n.right->lt, n);
        n.lt = LT_KEEP;
        return &n;
    }

    Node *TypeCheckMathOpEqBit(BinOp &n) {
        auto nn = TypeCheckBitOp(n);
        if (&n == nn) CheckLval(n.left);
        return nn;
    }

    Node *TypeCheckMathOpEq(BinOp &n) {
        if (auto nn = OperatorOverload(n)) return nn;
        DecBorrowers(n.left->lt, n);
        TT(n.right, 1, LT_BORROW);
        CheckLval(n.left);
        n.exptype = n.left->exptype;
        if (!MathCheckVector(n.exptype, n.left, n.right)) {
            bool unionchecked = false;
            MathError(n.exptype, n, unionchecked, false);
            if (!unionchecked) SubType(n.right, n.exptype, "right", n);
        }
        // This really does: "left = left op right" the result of op is LT_KEEP, which is
        // implicit, so the left var must be LT_KEEP as well. This is ensured elsewhere because
        // all !single_assignment vars are LT_KEEP.
        assert(!Is<IdentRef>(n.left) || LifetimeType(Is<IdentRef>(n.left)->sid->lt) != LT_BORROW);
        DecBorrowers(n.right->lt, n);
        n.lt = PushBorrow(n.left);
        return &n;
    }

    void StructCompResult(BinOp &n, TypeRef u) {
        auto nfields = u->udt->sfields.size();
        n.exptype = st.GetVectorType(type_vector_int, 0, (int)nfields);
        if (n.exptype.Null())
            Error(n, "no suitable struct of int type of size ", nfields, " known");
    }

    Node *TypeCheckComp(BinOp &n) {
        if (auto nn = OperatorOverload(n)) return nn;
        TT(n.right, 1, LT_BORROW);
        n.exptype = &st.default_bool_type->thistype;
        auto u = Union(n.left->exptype, n.right->exptype, "lhs", "rhs", CF_COERCIONS, nullptr);
        if (!u->Numeric() && u->t != V_STRING) {
            if (Is<Equal>(&n) || Is<NotEqual>(&n)) {
                // Comparison with one result, but still by value for structs.
                if (u->t != V_VECTOR && !IsUDT(u->t) && u->t != V_NIL &&
                    u->t != V_FUNCTION && u->t != V_RESOURCE && u->t != V_TYPEID)
                    RequiresError(TypeName(n.left->exptype), n.right->exptype, n,
                                  "right-hand side");
                if (u->t == V_STRUCT_S && !u->udt->sametype->Numeric())
                    RequiresError("numeric struct", u, n);
                // String comp by value, but nillables compared by ref, so only allow that
                // if its an explicit comparison against nil, because we don't want to compare
                // actual strings by ref.
                if (u->t == V_NIL && u->sub->t == V_STRING &&
                    !Is<Nil>(n.left) &&
                    !Is<Nil>(n.right))  
                    RequiresError("string", u, n);
            } else {
                // Comparison vector op: vector inputs, vector out.
                if (u->t == V_STRUCT_S && u->udt->sametype->Numeric()) {
                    StructCompResult(n, u);
                } else if (MathCheckVector(n.exptype, n.left, n.right)) {
                    StructCompResult(n, n.exptype);
                    // Don't do SubTypeLR since type already verified and `u` not
                    // appropriate anyway.
                    goto out;
                } else {
                    Error(n, Q(n.Name()), " doesn\'t work on ", Q(TypeName(n.left->exptype)),
                             " and ", Q(TypeName(n.right->exptype)));
                }
            }
        }
        SubTypeLR(u, n);
        out:
        DecBorrowers(n.left->lt, n);
        DecBorrowers(n.right->lt, n);
        n.lt = LT_KEEP;
        return &n;
    }

    Node *TypeCheckBitOp(BinOp &n) {
        if (auto nn = OperatorOverload(n)) return nn;
        TT(n.right, 1, LT_BORROW);
        auto u = Union(n.left->exptype, n.right->exptype, "lhs", "rhs", CF_COERCIONS, nullptr);
        if (u->t != V_INT) u = type_int;
        SubTypeLR(u, n);
        n.exptype = u;
        DecBorrowers(n.left->lt, n);
        DecBorrowers(n.right->lt, n);
        n.lt = LT_ANY;
        return &n;
    }

    Node *TypeCheckPlusPlus(Unary &n) {
        if (auto nn = OperatorOverload(n)) return nn;
        CheckLval(n.child);
        n.exptype = n.child->exptype;
        if (!n.exptype->Numeric())
            RequiresError("numeric", n.exptype, n);
        n.lt = n.child->lt;
        return &n;
    }

    void TypeCheckUDT(UDT &udt, const Node &errn) {
        // Give a type for fields that don't have one specified.
        for (auto [i, sfield] : enumerate(udt.sfields)) {
            auto &gudt = udt.g;
            auto &f = gudt.fields[i];
            if (f.defaultval && sfield.type->t == V_ANY) {
                auto cf_type = f.defaultval->CFType();
                if (cf_type.Null()) {
                    // FIXME: would be good to not call TT here generically but instead rely
                    // on CFType entirely, just in case TT has a side effect on type
                    // checking, especially function calls, whose "return from" may fail here.
                    // Sadly that is not easy given the amount of type-checking code this
                    // already relies on.
                    st.PushSuperGenerics(&udt);
                    TT(f.defaultval, 1, LT_ANY);
                    st.PopSuperGenerics(&udt);
                    DecBorrowers(f.defaultval->lt, errn);
                } else {
                    // TODO: expand the cases where CFType is sufficient.
                    f.defaultval->exptype = cf_type;
                }
                f.defaultval->lt = LT_UNDEF;
                sfield.type = f.defaultval->exptype;
                //  Here we force a check against types of this field in superclasses.
                //  This is necessary because each of these defaultvals have been typechecked
                //  independently, possibly containing V_VAR instances (for e.g. []) that if
                //  we don't unify them they could get bound to different types by code,
                //  causing incompatible fields.
                //  We skip generic supers because they can legitimately allow different types
                //  for fields.
                for (auto sudt = udt.ssuperclass; sudt; sudt = sudt->ssuperclass) {
                    if (i >= sudt->sfields.size()) break;
                    if (!ConvertsTo(f.defaultval->exptype, sudt->sfields[i].type,
                                    ConvertFlags(CF_EXACTTYPE | CF_UNIFICATION)))
                        Error(errn, "Field ", Q(f.id->name), " has type ",
                              Q(TypeName(f.defaultval->exptype)),
                              " which is incompatible with the superclass type of ",
                              Q(TypeName(sudt->sfields[i].type)));
                }
            }
        }
        for (auto u = &udt; u; u = u->ssuperclass) {
            if (u->subudts_dispatched) {
                // This is unfortunate, but code ordering has made it such that a
                // dispatch has already been typechecked before this gudt has been
                // fully processed. This should be solved by doing type-checking more
                // safely multipass, but until then, this error. Without this error,
                // the VM could run into empty vtable entries.
                // TODO: output the name of the method that caused this?
                Error(errn, "class ", Q(udt.name), " already used in dynamic dispatch on ",
                        Q(u->name), " before it has been declared");
            }
            u->subudts.push_back(&udt);
            // May have already been added by predeclaration, but rather than skipping it in
            // predeclaration (which would not put the dispatching gudt first), just remove this
            // one.
            for (size_t i = 0; i < u->subudts.size() - 1; i++) {
                if (u->subudts[i] == &udt) {
                    u->subudts.pop_back();
                    break;
                }
            }
            if (udt.g.is_abstract) {
                // Only necessary to add itself, but not to superclasses.
                // FIXME: if we could separate the concept of dispatch_root and subudts,
                // we wouldn't even need to add the root.
                break;
            }
        }
        if (!udt.ComputeSizes()) {
            Error(errn, cat("struct ", Q(udt.name), " cannot be self-referential"));
        }
    }

    void TypeCheckGUDT(GUDT &gudt, const Node &errn, bool predeclaration) {
        if (predeclaration) return;
        for (auto udt = gudt.first; udt; udt = udt->next) {
            TypeCheckUDT(*udt, errn);
        }
    }

    void RetVal(TypeRef type, SubFunction *sf, const Node &err) {
        for (auto isc : reverse(scopes)) {
            if (isc.sf->parent == sf->parent) break;
            // isc.sf is a function in the call chain between the return statement and the
            // function it is returning from. Since we're affecting the return type of the
            // function we're returning from, if it gets specialized but a function along the
            // call chain (isc.sf) does not, we must ensure that return type affects the second
            // specialization.
            // We do this by tracking return types, and replaying them when a function gets
            // reused.
            // A simple test case is in return_from unit test, and recursive_exception is also
            // affected.
            // Check if return event already exists, which may happen for multiple similar return
            // statement of when called from ReplayReturns in a similar call context. 
            for (auto &rre : isc.sf->reuse_return_events) {
                if (rre.first == sf && rre.second->Equal(*type)) goto found;
            }
            isc.sf->reuse_return_events.push_back({ sf, type });
            found:;
        }
        sf->num_returns++;
        if (sf != scopes.back().sf) sf->num_returns_non_local++;
        if (sf->returngiventype.Null()) {
            if (sf->reqret) {
                // We can safely generalize the type if needed, though not with coercions.
                sf->returntype = Union(type, sf->returntype, "return expression",
                                        "function return type", CF_NONE, &err);
            } else {
                // The caller doesn't want return values.
                sf->returntype = type_void;
            }
        }
    }

    void TypeCheckFunctionDef(SubFunction &sf, const Node &call_context) {
        if (sf.typechecked) return;
        STACK_PROFILE;
        LOG_DEBUG("function start: ", SignatureWithFreeVars(sf, nullptr));
        Scope scope;
        scope.sf = &sf;
        scope.call_context = &call_context;
        scopes.push_back(scope);
        //for (auto &ns : named_scopes) LOG_DEBUG("named scope: ", ns.sf->parent->name);
        if (!sf.parent->anonymous) named_scopes.push_back(scope);
        st.BlockScopeStart();
        sf.typechecked = true;
        for (auto &arg : sf.args) {
            StorageType(arg.type, call_context);
            if (arg.sid->withtype) st.AddWithStructTT(arg.type, arg.sid->id, &sf);
        }
        for (auto &fv : sf.freevars) UpdateCurrentSid(fv.sid);
        auto backup_vars = [&](vector<Arg> &in, vector<Arg> &backup) {
            for (auto [i, arg] : enumerate(in)) {
                // Need to not overwrite nested/recursive calls. e.g. map(): map(): ..
                backup[i].sid = arg.sid->Current();
                arg.sid->type = arg.type;
                RevertCurrentSid(arg.sid);
            }
        };
        auto backup_args = sf.args; backup_vars(sf.args, backup_args);
        auto backup_locals = sf.locals; backup_vars(sf.locals, backup_locals);
        auto enter_scope = [&](const Arg &var) {
            IncBorrowers(var.sid->lt, call_context);
        };
        for (auto &arg : sf.args) enter_scope(arg);
        for (auto &local : sf.locals) enter_scope(local);
        if (sf.reqret) {
            if (sf.returngiventype.Null()) {
                sf.returntype = st.NewTypeVar();
            } else {
                sf.returntype = st.ResolveTypeVars(sf.returngiventype, call_context.line);
                assert(sf.returntype->t != V_NIL || sf.returntype->sub->t != V_INT);
                auto len = sf.returntype->NumValues();
                if (len > sf.reqret) {
                    if (sf.reqret == 1) {
                        sf.returntype = sf.returntype->Get(0);
                    } else {
                        assert(sf.returntype->t == V_TUPLE);
                        auto nt = st.NewTuple(sf.reqret);
                        nt->tup->assign(sf.returntype->tup->begin(),
                                        sf.returntype->tup->begin() + sf.reqret);
                        sf.returntype = nt;
                    }
                }
            }
        } else {
            sf.returntype = type_void;
        }
        auto start_borrowed_vars = borrowstack.size();
        auto start_promoted_vars = flowstack.size();
        sf.sbody->TypeCheck(*this, 0);
        CleanUpFlow(start_promoted_vars);
        if (!sf.num_returns) {
            if (!sf.returngiventype.Null() && sf.returngiventype->t != V_VOID)
                Error(*sf.sbody->children.back(), "missing return statement");
            sf.returntype = type_void;
        }
        // Let variables go out of scope in reverse order of declaration.
        auto exit_scope = [&](const Arg &var) {
            DecBorrowers(var.sid->lt, call_context);
        };
        for (auto &local : reverse(sf.locals)) exit_scope(local);
        for (auto &arg : sf.args) exit_scope(arg);  // No order.
        while (borrowstack.size() > start_borrowed_vars) {
            auto &b = borrowstack.back();
            if (b.refc) {
                Error(*sf.sbody->children.back(),
                      "variable ", Q(b.Name()), " still has ", b.refc, " borrowers");
            }
            borrowstack.pop_back();
        }
        for (auto &back : backup_args)   RevertCurrentSid(back.sid);
        for (auto &back : backup_locals) RevertCurrentSid(back.sid);
        if (sf.returntype->HasValueType(V_VAR)) {
            // If this function return something with a variable in it, then it likely will get
            // bound by the caller. If the function then gets reused without specialization, it will
            // get the wrong return type, so we force specialization for subsequent calls of this
            // function. FIXME: check in which cases this is typically true, since its expensive
            // if done without reason.
            sf.mustspecialize = true;
        }
        if (sf.parent->is_constructor_of &&
            (!IsUDT(sf.returntype->t) || &sf.returntype->udt->g != sf.parent->is_constructor_of) &&
            (sf.returntype->t != V_VOID || !sf.returngiventype.Null())  //  Return value discarded.
            ) {
            Error(*sf.sbody->children.back(), "constructor must return value of its own type");
        }
        st.BlockScopeCleanup();
        for (auto member : scopes.back().scoped_fields) {
            auto f = member->field();
            f->in_scope = false;
        }
        if (!sf.parent->anonymous) named_scopes.pop_back();
        scopes.pop_back();
        LOG_DEBUG("function end ", Signature(sf), " returns ",
                             TypeName(sf.returntype));
    }

    bool FreeVarsSameAsCurrent(const SubFunction &sf, bool prespecialize) {
        for (auto &freevar : sf.freevars) {
            //auto atype = Promote(freevar.id->type);
            auto sid = freevar.sid;
            auto cur = sid->Current();
            if (sid != cur || !freevar.type->Equal(*cur->type)) {
                (void)prespecialize;
                assert(prespecialize || sid == cur || (sid && cur));
                return false;
            }
            //if (atype->t == V_FUNCTION) return false;
        }
        return true;
    }

    SubFunction *CloneFunction(Overload &ov) {
        auto esf = ov.sf;
        LOG_DEBUG("cloning: ", esf->parent->name);
        auto sbody = AssertIs<Block>(ov.gbody->Clone());
        if (!esf->sbody) {
            esf->sbody = sbody;
            return esf;
        }
        auto sf = st.CreateSubFunction();
        sf->SetParent(*esf->parent, ov);
        sf->sbody = sbody;
        // Any changes here make sure this corresponds what happens in Inline() in the
        // optimizer.
        st.CloneIds(*sf, *esf);
        sf->freevarchecked = true;
        sf->returngiventype = esf->returngiventype;
        sf->returntype = esf->returntype;
        sf->method_of = esf->method_of;
        sf->generics = esf->generics;
        sf->giventypes = esf->giventypes;
        sf->returned_thru_to_max = -1;
        sf->attributes = esf->attributes;
        sf->lexical_parent = esf->lexical_parent;
        sf->overload = esf->overload;
        sf->node_count = esf->node_count;
        return sf;
    }

    // See if returns produced by an existing specialization are compatible with our current
    // context of functions.
    bool CompatibleReturns(const SubFunction &ssf) {
        for (auto re : ssf.reuse_return_events) {
            auto sf = re.first;
            for (auto isc : reverse(scopes)) {
                if (isc.sf->parent == sf->parent) {
                    if (isc.sf->reqret != sf->reqret) return false;
                    goto found;
                }
            }
            return false;  // Function not in context.
            found:;
        }
        return true;
    }

    void CheckReturnPast(SubFunction *sf, const SubFunction *sf_to, const Node &context) {
        // Special case for returning out of top level, which is always allowed.
        if (sf_to != st.toplevel) {
            if (sf->isdynamicfunctionvalue) {
                // This is because the function has been typechecked against one context, but
                // can be called again in a different context that does not have the same callers.
                Error(context, "cannot return through dynamic function value (",
                      "return statement tries to return from ", Q(sf_to->parent->name), ")");
            }
        }
        auto nretslots = ValWidthMulti(sf_to->returntype, sf_to->returntype->NumValues());
        sf->returned_thru_to_max = std::max(sf->returned_thru_to_max, nretslots);
    }

    TypeRef TypeCheckMatchingCall(SubFunction *sf, List &call_args, bool static_dispatch,
                                  bool first_dynamic, bool may_have_lambda_args) {
        STACK_PROFILE;
        // Here we have a SubFunction witch matching specialized types.
        sf->numcallers++;
        Function &f = *sf->parent;
        if (may_have_lambda_args && (static_dispatch || first_dynamic)) {
            for (auto [i, c] : enumerate(call_args.children)) {
                auto &arg = sf->args[i];
                // We prefer doing this after the SubType call below (for better errors?)
                // But if we upgraded the args to LT_KEEP this can drop a borrow that could
                // cause lambdas typechecked inside TypeCheckFunctionDef to give unnecessary
                // borrow errors.
                if (arg.sid->lt == LT_KEEP && IsBorrow(c->lt)) {
                    AdjustLifetime(c, arg.sid->lt);
                }
            }
        }
        if (!f.istype) TypeCheckFunctionDef(*sf, call_args);
        // Finally check all args. We do this after checking the function
        // definition, since SubType below can cause specializations of the current function
        // to be typechecked with strongly typed function value arguments.
        if (static_dispatch || first_dynamic) {
            for (auto [i, c] : enumerate(call_args.children)) {
                auto &arg = sf->args[i];
                // Check a dynamic dispatch only for the first case, and then skip
                // checking the first arg.
                if (static_dispatch || i) SubType(c, arg.type, ArgName(i), f.name);
                AdjustLifetime(c, arg.sid->lt);  // Remaining cases.
                // We really don't want to specialize functions on variables, so we simply
                // disallow them. This should happen only infrequently.
                if (arg.type->HasValueType(V_VAR))
                    Error(call_args, "can\'t infer ", Q(ArgName(i)), " argument of call to ",
                          Q(f.name));
                // This has to happen even to dead args:
                DecBorrowers(c->lt, call_args);
            }
        }
        for (auto &freevar : sf->freevars) {
            // New freevars may have been added during the function def typecheck above.
            // In case their types differ from the flow-sensitive value at the callsite (here),
            // we want to override them.
            freevar.type = freevar.sid->Current()->type;
        }
        // See if this call is recursive:
        for (auto &sc : scopes) if (sc.sf == sf) {
            sf->isrecursivelycalled = true;
            if (sf->returngiventype.Null())
                Error(call_args, "recursive function ", Q(sf->parent->name),
                                 " must have explicit return type");
            break;
        }
        return sf->returntype;
    };

    bool SpecializationIsCompatible(const SubFunction &sf, size_t reqret) {
        return reqret == sf.reqret &&
            FreeVarsSameAsCurrent(sf, false) &&
            CompatibleReturns(sf);
    };

    void ReplayReturns(const SubFunction *sf, const Node &call_context) {
        // Apply effects of return statements for functions being reused, see
        // RetVal above.
        for (auto [isf, type] : sf->reuse_return_events) {
            for (auto isc : reverse(scopes)) {
                if (isc.sf->parent == isf->parent) {
                    // NOTE: will have to re-apply lifetimes as well if we change
                    // from default of LT_KEEP.
                    RetVal(type, isc.sf, call_context);
                    // This should in theory not cause an error, since the previous
                    // specialization was also ok with this set of return types.
                    // It could happen though if this specialization has an
                    // additional return statement that was optimized
                    // out in the previous one.
                    SubTypeT(type, isc.sf->returntype, call_context, "",
                        "reused return value");
                    goto destination_found;
                }
                CheckReturnPast(isc.sf, isf, call_context);
            }
            // This error should hopefully be rare, but still possible if this call is in
            // a very different context.
            Error(call_context, "return out of call to ", Q(sf->parent->name),
                                " can\'t find destination ", Q(isf->parent->name));
            destination_found:;
        }
    };

    void UnWrapBoth(TypeRef &otype, TypeRef &atype) {
        while (otype->Wrapped() && otype->t == atype->t) {
            otype = otype->Element();
            atype = atype->Element();
        }
    }

    void BindTypeVar(TypeRef giventype, TypeRef atype,
                     vector<GenericTypeVariable> &generics, GUDT *parent = nullptr) {
        auto otype = giventype;
        UnWrapBoth(otype, atype);
        if (otype->t == V_NIL) {
            otype = otype->Element();
        }
        if (otype->t == V_TYPEVAR) {
            for (auto &gtv : generics) {
                if (gtv.tv == otype->tv && gtv.type.Null()) {
                    gtv.type = atype;
                    break;
                }
            }
        } else if (otype->t == V_UUDT &&
                   IsUDT(atype->t) &&
                   otype->spec_udt->gudt != parent &&  // Avoid recursion!
                   otype->spec_udt->gudt == &atype->udt->g) {
            assert(otype->spec_udt->specializers.size() == atype->udt->bound_generics.size());
            for (auto [i, s] : enumerate(otype->spec_udt->specializers)) {
                BindTypeVar({ s }, atype->udt->bound_generics[i], generics,
                            otype->spec_udt->gudt);
            }
        }
    }

    TypeRef TypeCheckCallStatic(SubFunction *&sf, List &call_args, size_t reqret,
                                vector<TypeRef> *specializers, Overload &ov,
                                bool static_dispatch, bool first_dynamic, bool force_keep) {
        STACK_PROFILE;
        Function &f = *sf->parent;
        if (ov.isprivate && ov.declared_at.fileidx != call_args.line.fileidx)
            Error(call_args, "call to (partially) private function ", Q(f.name));
        sf = ov.sf;
        // Collect generic type values.
        vector<GenericTypeVariable> generics = sf->generics;
        for (auto &gtv : generics) gtv.type = nullptr;
        if (specializers) {
            if (specializers->size() > generics.size())
                Error(call_args, "too many specializers given");
            for (auto [i, type] : enumerate(*specializers))
                generics[i].type = st.ResolveTypeVars(type, call_args.line);
        }
        bool has_lambda_args = false;
        for (auto [i, c] : enumerate(call_args.children)) {
            BindTypeVar(sf->giventypes[i], c->exptype, generics);
            if (c->exptype->t == V_FUNCTION) has_lambda_args = true;
        }
        for (auto &gtv : generics)
            if (gtv.type.Null())
                Error(call_args, "cannot implicitly bind type variable ", Q(gtv.tv->name),
                                 " in call to ", Q(f.name), " (argument doesn't match?)");
        if (!force_keep) {
            // Having a lifetime per arg is mostly useful on smaller functions to not get
            // unnecessary refc overhead on the border, especially if they later get inlined.
            // But for really big functions it just risks unnecessary specializations for no gain.
            if (ov.sf->node_count > 25) force_keep = true;
        }
        // Check if we need to specialize: generic args, free vars and need of retval
        // must match previous calls.
        auto ArgLifetime = [&](const Node *c, const Arg &arg) {
            if (force_keep)
                return LT_KEEP;
            // We force !single_assignment to LT_KEEP, since any overwriting of the arg would be problematic
            // with incoming borrowed values at refc==1, and more generally if the pattern of overwriting is
            // complicated due to loops etc, this is the only way we can track the refc correctly.
            if (!arg.sid->id->single_assignment)
                return LT_KEEP; 
            // Similarly, a V_STRUCT_R is an exception in that is essentially multiple ref arguments, subject
            // to the same pitfalls, so must get the same treatment.
            // FIXME: this is conservative, since V_STRUCT_R args that never get assigned to should not get this
            // treatment. But where we track assignment in the parser we have no idea of types, and here we don't
            // know if it is assigned to, so that would require some new kind of tracking this info.
            if (c->exptype->t == V_STRUCT_R)
                return LT_KEEP;
            // This is a very special case that tends to happen if we pass a variable to a HOF, and then inside
            // the lambda to that HOF we assign to the same var. To avoid that, check !single_assignment for the
            // variable passed in, AND see if any function values are being passed in.
            // TODO: how does this apply to borrows that are not IdentRef, like Dot?
            if (has_lambda_args && Is<IdentRef>(c) && !Is<IdentRef>(c)->sid->id->single_assignment)
                return LT_KEEP;
            // No exceptions hold, it can be whatever lifetime it wants, including borrows.
            return c->lt;
        };
        // Check if any existing specializations match.
        for (sf = ov.sf; sf; sf = sf->next) {
            if (sf->typechecked && !sf->mustspecialize) {
                // Note: we compare only lt, since calling with other borrowed sid
                // should be ok to reuse.
                for (auto [i, c] : enumerate(call_args.children)) {
                    auto &arg = sf->args[i];
                    auto arg_lt = ArgLifetime(c, arg);
                    auto unequal_lifetimes = IsBorrow(arg_lt) != IsBorrow(arg.sid->lt);
                    // TODO: we need this check here because arg type may rely on parent
                    // struct (or function) generic, and thus isn't covered by the checking
                    // of sf->generics below. Can this be done more elegantly?
                    auto parent_generic =
                        st.IsGeneric(sf->giventypes[i]) && !c->exptype->Equal(*arg.type);
                    auto incompatible = unequal_lifetimes || parent_generic;
                    if (incompatible)
                        goto fail;
                }
                for (auto [i, gtv] : enumerate(sf->generics)) {
                    if (!gtv.type->Equal(*generics[i].type)) goto fail;
                }
                if (SpecializationIsCompatible(*sf, reqret)) {
                    // This function can be reused.
                    // Make sure to add any freevars this call caused to be
                    // added to its parents also to the current parents, just in case
                    // they're different.
                    LOG_DEBUG("re-using: ", Signature(*sf));
                    CheckFreeVariablesFromFunction(sf);
                    ReplayReturns(sf, call_args);
                    auto rtype = TypeCheckMatchingCall(sf, call_args, static_dispatch,
                                                       first_dynamic, has_lambda_args);
                    if (!sf->isrecursivelycalled) ReplayAssigns(sf);
                    return rtype;
                }
                fail:;
            }
        }
        // No match, make new specialization.
        sf = CloneFunction(ov);
        // Now specialize.
        sf->reqret = reqret;
        sf->generics = generics;
        UDT *udt = nullptr;
        if (sf->overload->method_of && IsUDT(call_args.children[0]->exptype->t)) {
            udt = call_args.children[0]->exptype->udt;
        }
        if (udt) {
            st.PushSuperGenerics(udt);
            sf->method_of = udt;
        }
        st.bound_typevars_stack.push_back(sf->generics);
        for (auto [i, c] : enumerate(call_args.children)) {
            auto &arg = sf->args[i];
            arg.sid->lt = ArgLifetime(c, arg);
            arg.type = st.ResolveTypeVars(sf->giventypes[i], call_args.line);
            LOG_DEBUG("arg: ", arg.sid->id->name, ":", TypeName(arg.type));
        }
        // This must be the correct freevar specialization.
        assert(!f.anonymous || sf->freevarchecked);
        assert(!sf->freevars.size());
        LOG_DEBUG("specialization: ", Signature(*sf));
        auto rtype =
            TypeCheckMatchingCall(sf, call_args, static_dispatch, first_dynamic, has_lambda_args);
        if (udt) {
            st.PopSuperGenerics(udt);
        }
        st.bound_typevars_stack.pop_back();
        return rtype;
    };

    TypeRef TypeCheckCallDispatch(UDT &dispatch_udt, SubFunction *&csf, List &call_args,
                                  size_t reqret, vector<TypeRef> *specializers,
                                  int &vtable_idx) {
        // FIXME: this is to lock the subudts, since adding to them later would invalidate
        // this dispatch.. would be better to solve ordering problems differently.
        dispatch_udt.subudts_dispatched = true;
        Function &f = *csf->parent;
        // We must assume the instance may dynamically be different, so go thru vtable.
        // See if we already have a vtable entry for this type of call.
        for (auto [i, disp] : enumerate(dispatch_udt.dispatch_table)) {
            // FIXME: does this guarantee it find it in the recursive case?
            // TODO: we chould check for a superclass vtable entry also, but chances
            // two levels will be present are low.
            if (disp.sf && disp.sf->method_of == &dispatch_udt && disp.is_dispatch_root &&
                !disp.is_switch_dispatch && &f == disp.sf->parent) {
                for (auto [i, c] : enumerate(call_args.children)) {
                    auto &arg = disp.sf->args[i];
                    if (i && !ConvertsTo(c->exptype, arg.type, CF_NONE))
                        goto fail;
                }
                // If this ever fails, that means new types got added during typechecking..
                // which means we'd just have to create a new vtable entry instead, or somehow
                // avoid the new type.
                assert(disp.subudts_size == dispatch_udt.subudts.size());
                // We must check that ALL functions involved are compatible, since some
                // may touch different freevars that the dispatch root doesn't have, such that
                // if they were different means we can't reuse this dispatch.
                for (auto udt : dispatch_udt.subudts) {
                    auto sf = udt->dispatch_table[i].sf;
                    if (!SpecializationIsCompatible(*sf, reqret))
                        goto fail;
                }
                // We can reuse!
                for (auto udt : dispatch_udt.subudts) {
                    auto sf = udt->dispatch_table[i].sf;
                    LOG_DEBUG("re-using dyndispatch: ", Signature(*sf));
                    if (sf->typechecked) {
                        // If sf is not typechecked here, it means a function before this in
                        // the list has a recursive call.
                        CheckFreeVariablesFromFunction(sf);
                        ReplayReturns(sf, call_args);
                        ReplayAssigns(sf);
                    }
                }
                // Type check this as if it is a static dispatch to just the root function.
                TypeCheckMatchingCall(csf = disp.sf, call_args, true, false, true);
                vtable_idx = (int)i;
                return dispatch_udt.dispatch_table[i].returntype;
            }
            fail:;
        }
        // Must create a new vtable entry.
        // TODO: would be good to search superclass if it has this method also.
        // Probably not super important since dispatching on the "middle" type in a
        // hierarchy will be rare.
        // Find subclasses and max vtable size.
        assert(!dispatch_udt.subudts.empty());  // We are dispatching because we found a subclass.
        {
            struct Pick { Overload *ov; bool supcall; };
            vector<Pick> overload_picks;
            // First, for the set of udts part of this dispatch, find the method that would apply.
            for (auto sub : dispatch_udt.subudts) {
                Overload *best = nullptr;
                int bestdist = -1;
                for (auto ov : csf->parent->overloads) {
                    if (ov->method_of) {
                        // We want the method "closest to" this udt in the inheritance chain.
                        // We must search specializations since method_of may be a generic UDT (the
                        // methods will get specialized and typechecked right below here, and
                        // method_of pointing to a specialized udt there.
                        auto sdist = DistanceToSpecializedSuper(ov->method_of, sub);
                        if (sdist >= 0 && (!best || bestdist >= sdist)) {
                            if (bestdist == sdist)
                                Error(call_args, "more than implementation of ", Q(f.name),
                                                 " applies to ", Q(sub->name));
                            best = ov;
                            bestdist = sdist;
                        }
                    }
                }
                if (!best) {
                    if (!sub->g.is_abstract) {
                        Error(call_args, "no implementation for ",
                                         Q(cat(sub->name, ".", csf->parent->name)));
                    } else {
                        // This UDT is unused, so we're ok there not being an implementation
                        // for it.. like e.g. an abstract base class.
                    }
                }
                overload_picks.push_back({ best, bestdist != 0 });
                vtable_idx = std::max(vtable_idx, (int)sub->dispatch_table.size());
            }
            assert(vtable_idx >= 0);
            // Add functions to all vtables.
            for (auto [i, udt] : enumerate(dispatch_udt.subudts)) {
                auto &dt = udt->dispatch_table;
                assert((int)dt.size() <= vtable_idx);  // Double entry.
                // FIXME: this is not great, wasting space, but only way to do this
                // on the fly without tracking lots of things.
                while ((int)dt.size() < vtable_idx) dt.push_back({});
                dt.push_back({ !overload_picks[i].ov
                                ? nullptr
                                : overload_picks[i].ov->sf });
            }
            // We are now going to type check all functions in the vtable for the given
            // call_args, which normally determines the lifetimes of the function args.
            // Problem is, function may change arg lifetimes based on things like internal
            // assignment, and lifetimes must be the same for all, so either we have to
            // guarantee that arg lifetimes never change, or for now,
            // standardize on a lifetime convention of always using LT_KEEP, by passing
            // force_keep = true below.          
            // FIXME: if any of the overloads below contain recursive calls, it may run into
            // issues finding an existing dispatch above? would be good to guarantee..
            // The fact that in subudts the superclass comes first will help avoid problems
            // in many cases.
            auto de = &dispatch_udt.dispatch_table[vtable_idx];
            de->is_dispatch_root = true;
            de->returntype = st.NewTypeVar();
            de->subudts_size = dispatch_udt.subudts.size();
            // Typecheck all the individual functions.
            SubFunction *last_sf = nullptr;
            bool any_recursive = false;
            int any_returned_thru_max = -1;
            for (auto [i, udt] : enumerate(dispatch_udt.subudts)) {
                auto sf = udt->dispatch_table[vtable_idx].sf;
                // Missing implementation for unused UDT.
                if (!sf)
                    continue;
                if (overload_picks[i].supcall) {
                    // We're using a superclass method. Skip if that superclass is in
                    // the dispatch table since we can then simply reuse that one.
                    // This might not be the case when the superclass is above the dispatch
                    // root and the dispatch root doesn't have its own implementation.
                    // Below we set the sf the superclass picked.
                    bool found_sup_impl = false;
                    for (auto &pick : overload_picks) {
                        if (!pick.supcall && pick.ov == overload_picks[i].ov) {
                            found_sup_impl = true;
                            break;
                        }
                    }
                    if (found_sup_impl)
                        continue;
                }
                call_args.children[0]->exptype = &udt->thistype;
                // FIXME: this has the side effect of giving call_args types relative to the last
                // overload type-checked, which is strictly speaking not correct, but may not
                // matter. Could call TypeCheckCallStatic once more at the end of this loop
                // to fix that?
                // FIXME: return value?
                /*auto rtype =*/
                TypeCheckCallStatic(csf, call_args, reqret, specializers, *overload_picks[i].ov,
                                    false, !last_sf, true);
                de = &dispatch_udt.dispatch_table[vtable_idx];  // May have realloced.
                sf = csf;
                udt->dispatch_table[vtable_idx].sf = sf;
                if (sf->isrecursivelycalled) any_recursive = true;
                any_returned_thru_max = std::max(any_returned_thru_max, sf->returned_thru_to_max);
                auto u = sf->returntype;
                if (de->returntype->IsBoundVar()) {
                    // FIXME: can this still happen now that recursive cases use explicit return
                    // types? If not change into assert?
                    if (!ConvertsTo(u, de->returntype, CF_UNIFICATION))
                        Error(*sf->sbody, "dynamic dispatch for ", Q(f.name),
                                          " return value type ", Q(TypeName(sf->returntype)),
                                          " doesn\'t match other case returning ",
                                          Q(TypeName(de->returntype)));
                } else {
                    if (i) {
                        // We have to be able to take the union of all retvals without
                        // coercion, since we're not fixing up any previously typechecked
                        // functions.
                        u = Union(u, de->returntype, "function return type", "other overloads",
                                  CF_NONE, &call_args);
                        // Ensure we didn't accidentally widen the type from a scalar.
                        assert(IsRef(de->returntype->t) || !IsRef(u->t));
                    }
                    de->returntype = u;
                }
                last_sf = sf;
            }
            // Pass 2.
            last_sf = nullptr;
            for (auto [i, udt] : enumerate(dispatch_udt.subudts)) {
                auto sf = udt->dispatch_table[vtable_idx].sf;
                if (!sf) continue;
                if (any_recursive && sf->returngiventype.Null())
                    Error(call_args, "recursive dynamic dispatch of ", Q(sf->parent->name),
                                     " must have explicit return type");
                if (overload_picks[i].supcall) {
                    // Now that everything has been typechecked, set the correct sf just
                    // in case the superclass picked a different one.
                    for (auto [j, pick] : enumerate(overload_picks)) {
                        if (!pick.supcall && pick.ov == overload_picks[i].ov) {
                            udt->dispatch_table[vtable_idx].sf =
                                dispatch_udt.subudts[j]->dispatch_table[vtable_idx].sf;
                            break;
                        }
                    }
                }
                if (last_sf) {
                    // We do this in pass 2 because otherwise arg types will be unresolved.
                    // FIXME: good to have this check here so it only occurs for functions
                    // participating in the dispatch, but error now appears at the call site!
                    for (auto [j, arg] : enumerate(sf->args)) {
                        if (j && !arg.type->Equal(*last_sf->args[j].type) &&
                            !st.IsGeneric(sf->giventypes[j]))
                            Error(call_args, "argument ", j + 1, " of declaration of ", Q(f.name),
                                          ", type ", Q(TypeName(arg.type)),
                                          " doesn\'t match type of previous declaration: ",
                                          Q(TypeName(last_sf->args[j].type)));
                    }
                }
                last_sf = sf;
                // Even if this sf is not returned thru, there may be an unwind check due to other
                // sfs in the dispatch.
                sf->returned_thru_to_max = std::max(sf->returned_thru_to_max, any_returned_thru_max);
            }
            dispatch_udt.dispatch_table[vtable_idx].returned_thru_to_max =
                std::max(dispatch_udt.dispatch_table[vtable_idx].returned_thru_to_max,
                         any_returned_thru_max);
            call_args.children[0]->exptype = &dispatch_udt.thistype;
        }
        return dispatch_udt.dispatch_table[vtable_idx].returntype;
    };

    // Reuse these, otherwise cause a LOT of allocations.
    vector<Overload *> pickfrom;
    vector<Overload *> matches;

    TypeRef TypeCheckCall(SubFunction *&csf, List &call_args, size_t reqret, int &vtable_idx,
                          vector<TypeRef> *specializers, bool super) {
        STACK_PROFILE;
        Function &f = *csf->parent;
        vtable_idx = -1;
        assert(!f.istype);
        // Check if we need to do dynamic dispatch. We only do this for functions that have a
        // explicit first arg type of a class (not structs, since they can never dynamically be
        // different from their static type), and only when there is a sub-class that has a
        // method that can be called also.
        UDT *dispatch_udt = nullptr;
        TypeRef type0;
        if (call_args.Arity()) {
            type0 = call_args.children[0]->exptype;
            if (type0->t == V_CLASS) dispatch_udt = type0->udt;
        }
        if (dispatch_udt) {
            if (super) {
                // We're forcing static dispatch to the superclass;
                type0 = &dispatch_udt->ssuperclass->thistype;
            } else {
                // Go thru all other overloads, and see if any of them have this one as superclass.
                for (auto ov : csf->parent->overloads) {
                    if (ov->method_of &&
                        DistanceFromSpecializedSub(dispatch_udt, ov->method_of) > 0) {
                        LOG_DEBUG("dynamic dispatch: ", Signature(*ov->sf));
                        return TypeCheckCallDispatch(*dispatch_udt, csf, call_args,
                            reqret, specializers, vtable_idx);
                    }
                }
            }
            // Yay there are no sub-class implementations, we can just statically dispatch.
        }
        // Do a static dispatch.
        // if there are overloads, figure out from first arg which to pick,
        // much like dynamic dispatch. Unlike dynamic dispatch, we also include non-class types.
        // TODO: also involve the other arguments for more complex static overloads?
        // FIXME: the use of args[].type here and further downstream only works because
        // we pre-resolve these in the TypeChecker constructor, instead we should use giventypes
        // properly here, and resolve them.
        assert(pickfrom.empty());
        assert(matches.empty());
        pickfrom = f.overloads;
        for (int argidx = 0; ; argidx++) {
            if (pickfrom.size() == 1) {
                // We're done, found unique match.
                auto pick = pickfrom[0];
                LOG_DEBUG("static dispatch: ", Signature(*pick->sf));
                pickfrom.clear();
                matches.clear();
                return TypeCheckCallStatic(csf, call_args, reqret, specializers, *pick, true,
                                           false, false);
            }
            if ((int)f.nargs() == argidx) {
                // Gotten to the end and we still have multiple matches!
                if (specializers) {
                    // Last ditch effort: remove overloads that don't match the generic params.
                    pickfrom.erase(remove_if(pickfrom.begin(), pickfrom.end(),
                                         [&](Overload *ov) {
                        return specializers->size() != ov->sf->generics.size();
                                             }),
                                   pickfrom.end());
                    if (pickfrom.size() == 1) {
                        argidx--;
                        continue;
                    }
                }
                AmbiguousOverloadError(call_args, f, type0, pickfrom);
            }
            // Now filter existing matches into a new set of matches based on current arg.
            matches.clear();
            auto type = argidx ? call_args.children[argidx]->exptype : type0;
            // First see if there is an exact match.
            for (auto ov : pickfrom) {
                if (type->Equal(*ov->sf->args[argidx].type)) matches.push_back(ov);
            }
            // Then see if there's a match if we'd instantiate a generic UDT arg.
            if (matches.empty() && IsUDT(type->t)) {
                for (auto ov : pickfrom) {
                    auto arg = ov->sf->giventypes[argidx];  // Want unresolved type.
                    if (arg->t == V_UUDT && arg->spec_udt->gudt == &type->udt->g) {
                        matches.push_back(ov);
                    }
                }
            }
            // Then see if there's a match by subtyping.
            if (matches.empty()) {
                for (auto ov : pickfrom) {
                    auto arg = ov->sf->args[argidx].type;
                    if (arg->t == V_UUDT && type->t == V_CLASS) {
                        auto dist = DistanceToSpecializedSuper(arg->spec_udt->gudt, type->udt);
                        if (dist >= 0) {
                            if (matches.size() == 1) {
                                auto oarg = matches[0]->sf->args[argidx].type;
                                assert(oarg->t == V_UUDT);
                                auto odist =
                                    DistanceToSpecializedSuper(oarg->spec_udt->gudt, type->udt);
                                if (dist < odist) {
                                    matches[0] = ov;  // Overwrite with better pick.
                                } else if (odist < dist) {
                                    // Keep old one.
                                } else {
                                    // Keep both, and hope the next arg disambiguates.
                                    matches.push_back(ov);
                                }                                
                            } else {
                                matches.push_back(ov);
                            }
                            if (matches.size() > 1) {
                                // FIXME: would it be ok to to defer to next arg like below?
                                Error(call_args, "multiple generic arguments apply");
                            }
                        }
                    } else if (ConvertsTo(type, arg, CF_NONE)) {
                        if (matches.size() == 1 && type->t == V_CLASS) {
                            auto oarg = matches[0]->sf->args[argidx].type;
                            // Prefer "closest" supertype.
                            auto dist = SuperDistance(arg->udt, type->udt);
                            auto odist = SuperDistance(oarg->udt, type->udt);
                            if (dist < odist) {
                                matches[0] = ov;  // Overwrite with better pick.
                            } else if (odist < dist) {
                                // Keep old one.
                            } else {
                                // Keep both, and hope the next arg disambiguates.
                                matches.push_back(ov);
                            }
                        } else {
                            matches.push_back(ov);
                        }
                    }
                }
            }
            // Then see if there's a match if we'd instantiate a fully generic arg.
            if (matches.empty()) {
                for (auto ov : pickfrom) {
                    auto arg = ov->sf->giventypes[argidx];  // Want unresolved type.
                    if (arg->t == V_TYPEVAR) { matches.push_back(ov); }
                }
            }
            // If the call has specializers, we should see bind those and see if they
            // uniquely identify an overload, e.g. for a [T] arg where T is now bound, or other
            // cases where the trivial case of T above doesn't apply.
            if (matches.empty() && specializers && !specializers->empty()) {
                // FIXME: This overlaps somewhat with resolving them during TypeCheckCallStatic
                vector<GenericTypeVariable> generics(specializers->size());
                for (auto [i, gtv] : enumerate(generics)) {
                    gtv.type = specializers->at(i);
                }
                for (auto ov : pickfrom) {
                    if (generics.size() != ov->sf->generics.size()) continue;
                    for (auto [i, gtv] : enumerate(generics)) {
                        gtv.tv = ov->sf->generics[i].tv;
                        gtv.type = st.ResolveTypeVars({ gtv.type }, call_args.line);
                    }
                    st.bound_typevars_stack.push_back(generics);
                    auto arg = st.ResolveTypeVars(ov->sf->giventypes[argidx], call_args.line);
                    st.bound_typevars_stack.pop_back();
                    // TODO: Should we instead do ConvertsTo here?
                    if (type->Equal(*arg)) {
                        matches.push_back(ov);
                    }
                }
            }
            // Then finally try with coercion.
            if (matches.empty()) {
                for (auto ov : pickfrom) {
                    if (ConvertsTo(type, ov->sf->args[argidx].type, CF_COERCIONS)) {
                        matches.push_back(ov);
                    }
                }
            }
            // Empty set: current arg can't select with any of the above methods.
            if (matches.empty()) {
                Error(call_args, "no overloads apply for ", Q(f.name), " with ", ArgName(argidx),
                      " argument type ", Q(TypeName(type)));
            }
            // We still have multiple matches that apply, so let the next arg(s) decide.
            pickfrom = matches;
        }
    }

    SubFunction *PreSpecializeFunction(SubFunction *hsf) {
        // Don't pre-specialize named functions, because this is not their call-site.
        if (!hsf->parent->anonymous) return hsf;
        assert(hsf->parent->overloads.size() == 1);
        hsf = hsf->parent->overloads[0]->sf;
        auto sf = hsf;
        if (sf->freevarchecked) {
            // See if there's an existing match.
            for (; sf; sf = sf->next) if (sf->freevarchecked) {
                if (FreeVarsSameAsCurrent(*sf, true)) return sf;
            }
            sf = CloneFunction(*hsf->parent->overloads[0]);
        } else {
            // First time this function has ever been touched.
            sf->freevarchecked = true;
        }
        assert(!sf->freevars.size());
        // Output without arg types, since those are yet to be overwritten.
        LOG_DEBUG("pre-specialization: ", SignatureWithFreeVars(*sf, nullptr));
        return sf;
    }

    Node *TypeCheckDynCall(DynCall *dc, size_t reqret) {
        UpdateCurrentSid(dc->sid);
        TypeCheckId(dc->sid);
        auto ftype = dc->sid->type;
        if (!ftype->IsFunction()) {
            Error(*dc, "dynamic function call value doesn\'t have a function type ",
                  Q(TypeName(ftype)));
        }
        // All dynamic calls can be statically typechecked.
        auto sf = ftype->sf;
        if (dc->Arity() < sf->parent->nargs())
            Error(*dc, "function value called with too few arguments");
        while (dc->Arity() > sf->parent->nargs()) {
            // HOFs are allowed to supply more args than the lambda needs.
            // TODO: This is somewhat odd, since it may throw away side effects. Then
            // again, this "feature" is quite similar to specifying default arguments,
            // so it being dead code should not be that surprising?
            delete dc->children.back();
            dc->children.pop_back();
        }
        TypeCheckList(dc, LT_ANY);
        if (sf->parent->istype) {
            // Function types are always fully typed.
            // All calls thru this type must have same lifetimes, so we fix it to LT_BORROW.
            dc->exptype = TypeCheckMatchingCall(sf, *dc, true, false, true);
            dc->lt = sf->ltret;
            dc->sf = sf;
            return dc;
        } else {
            auto c = new Call(dc->line, sf);
            c->children.append(dc->children.data(), dc->children.size());
            dc->children.clear();
            c->exptype =
                TypeCheckCallStatic(sf, *c, reqret, nullptr, *sf->parent->overloads[0], true, false, false);
            c->lt = sf->ltret;
            c->sf = sf;
            delete dc;
            return c;
        }
    }

    TypeRef TypeCheckBranch(bool iftrue, const Node *condition, Block *block, size_t reqret) {
        auto flowstart = CheckFlowTypeChanges(iftrue, condition);
        block->TypeCheck(*this, reqret);
        CleanUpFlow(flowstart);
        return block->exptype;
    }

    void CheckFlowTypeIdOrDot(const Node &n, TypeRef type) {
        FlowItem fi(n, type);
        if (fi.IsValid()) flowstack.push_back(fi);
    }

    void CheckFlowTypeChangesSub(bool iftrue, const Node *condition) {
        condition = SkipCoercions(condition);
        auto type = condition->exptype;
        if (auto c = Is<IsType>(condition)) {
            if (iftrue) CheckFlowTypeIdOrDot(*c->child, c->resolvedtype);
        } else if (auto c = Is<Not>(condition)) {
            CheckFlowTypeChangesSub(!iftrue, c->child);
        } else if (auto eq = Is<Equal>(condition)) {
            if (Is<Nil>(eq->right)) CheckFlowTypeChangesSub(!iftrue, eq->left);
        } else if (auto neq = Is<NotEqual>(condition)) {
            if (Is<Nil>(neq->right)) CheckFlowTypeChangesSub(iftrue, neq->left);
        } else {
            if (iftrue && type->t == V_NIL) CheckFlowTypeIdOrDot(*condition, type->Element());
        }
    }

    void CheckFlowTypeChangesAndOr(bool iftrue, const BinOp *condition) {
        // AND only works for then, and OR only for else.
        if (iftrue == (Is<And>(condition) != nullptr)) {
            // This allows for a chain of and's without allowing mixed operators.
            auto cleft = SkipCoercions(condition->left);
            if (typeid(*cleft) == typeid(*condition)) {
                CheckFlowTypeChanges(iftrue, condition->left);
            } else {
                CheckFlowTypeChangesSub(iftrue, condition->left);
            }
            CheckFlowTypeChangesSub(iftrue, condition->right);
        }
    }

    size_t CheckFlowTypeChanges(bool iftrue, const Node *condition) {
        auto start = flowstack.size();
        condition = SkipCoercions(condition);
        if (auto c = Is<Or>(condition)) {
            CheckFlowTypeChangesAndOr(iftrue, c);
        } else if (auto c = Is<And>(condition)) {
            CheckFlowTypeChangesAndOr(iftrue, c);
        } else {
            CheckFlowTypeChangesSub(iftrue, condition);
        }
        return start;
    }

    void AssignFlowPromote(Node &left, TypeRef right) {
        if (left.exptype->t == V_NIL && right->t != V_NIL) {
            CheckFlowTypeIdOrDot(left, right);
        }
    }

    // FIXME: this can in theory find the wrong node, if the same function nests, and the outer
    // one was specialized to a nilable and the inner one was not.
    // This would be very rare though, and benign.
    TypeRef AssignFlowDemote(FlowItem &left, TypeRef overwritetype, ConvertFlags coercions) {
        // Early out, numeric types are not nillable, nor do they make any sense for "is"
        auto &type = left.now;
        if (type->Numeric()) return type;
        for (auto &flow : reverse(flowstack)) {
            if (flow.sid == left.sid) {
                if (left.derefs.empty()) {
                    if (flow.derefs.empty()) {
                        type = flow.old;
                        goto found;
                    } else {
                        // We're writing to var V and V.f is in the stack: invalidate regardless.
                        goto found;
                    }
                } else {
                    if (flow.DerefsEqual(left)) {
                        type = flow.old;
                        goto found;
                    }
                }
            }
            continue;
            found:
            if (!ConvertsTo(overwritetype, flow.now, coercions)) {
                // FLow based promotion is invalidated.
                flow.now = flow.old;
                // TODO: It be cool to instead overwrite with whatever type is currently being
                // assigned. That currently doesn't work, since our flow analysis is a
                // conservative approximation, so if this assignment happens conditionally it
                // wouldn't work.
            }
            // We continue with the loop here, since a single assignment may invalidate multiple
            // promotions
        }
        return type;
    }

    TypeRef UseFlow(const FlowItem &left) {
        if (left.now->Numeric()) return left.now;  // Early out, same as above.
        for (auto flow : reverse(flowstack)) {
            if (flow.sid == left.sid &&	flow.DerefsEqual(left)) {
                return flow.now;
            }
        }
        return left.now;
    }

    void CleanUpFlow(size_t start) {
        while (flowstack.size() > start) flowstack.pop_back();
    }

    void TypeCheckAndOr(BinOp &ao, bool only_true_type, bool reqret, TypeRef &promoted_type) {
        // only_true_type supports patterns like ((a & b) | c) where the type of a doesn't matter,
        // and the overal type should be the union of b and c.
        // Or a? | b, which should also be the union of a and b.
        TypeRef tleft, tright;
        TypeCheckAndOrSub(ao.left, Is<Or>(ao), true, tleft);
        auto flowstart = CheckFlowTypeChanges(Is<And>(ao), ao.left);
        TypeCheckAndOrSub(ao.right, only_true_type, reqret, tright);
        CleanUpFlow(flowstart);
        if (only_true_type && Is<And>(ao)) {
            ao.exptype = tright;
            ao.lt = ao.right->lt;
            DecBorrowers(ao.left->lt, ao);
        } else {
            ao.exptype = Union(tleft, tright, "lhs", "rhs", CF_NONE, nullptr);
            if (ao.exptype->t == V_UNDEFINED) {
                // Special case: unlike elsewhere, we allow merging scalar and reference types,
                // since they are just tested and thrown away. To make this work, we force all
                // values to bools.
                MakeBool(ao.left);
                MakeBool(ao.right);
                ao.exptype = &st.default_bool_type->thistype;
                ao.lt = LT_ANY;
            } else {
                ao.lt = LifetimeUnion(ao.left, ao.right, Is<And>(ao));
            }
        }
        promoted_type = ao.exptype;
    }

    void TypeCheckAndOrSub(Node *&n, bool only_true_type, bool reqret, TypeRef &promoted_type) {
        // only_true_type supports patterns like ((a & b) | c) where the type of a doesn't matter,
        // and the overal type should be the union of b and c.
        // Or a? | b, which should also be the union of a and b.
        if (!Is<And>(n) && !Is<Or>(n)) {
            TT(n, reqret, LT_ANY);
            NoStruct(*n, "and / or");
            promoted_type = n->exptype;
            if (promoted_type->t == V_NIL && only_true_type)
                promoted_type = promoted_type->Element();
        } else {
            auto ao = dynamic_cast<BinOp *>(n);
            assert(ao);
            TypeCheckAndOr(*ao, only_true_type, reqret, promoted_type);
        }
    }

    optional<Value> TypeCheckCondition(Node *&condition, Node *context, const char *name) {
        TT(condition, 1, LT_BORROW);
        NoStruct(*condition, name);
        DecBorrowers(condition->lt, *context);
        Value cval = NilVal();
        if (condition->ConstVal(this, cval) != V_VOID) return cval;
        return {};
    }

    void CheckLval(Node *n) {
        // This can happen due to late specialization of GenericCall.
        if (Is<Call>(n) || Is<NativeCall>(n))
            Error(*n, "function-call cannot be an l-value");
        Borrow lv(*n);
        if (!lv.IsValid()) return;  // FIXME: force these to LT_KEEP?
        if (IsRefNil(n->exptype->t)) {
            // If any of the functions this assign sits in is reused, we need to be able to replay
            // checking the errors in CheckLvalBorrowed, since the contents of the borrowstack
            // may be different.
            for (auto &sc : reverse(scopes)) {
                // we could uniqueify this vector, but that would entails comparing `n`
                // structurally (construct a Borrow for each?), which would probably be
                // slower than the redundant calls to CheckLvalBorrowed this causes later?
                // Especially since this uniqueifying cost is paid always, even when there
                // are no actual repeated assigns in a scope, which is not that common.
                sc.sf->reuse_assign_events.push_back(n);
                if (sc.sf == lv.sid->sf_def) break;  // Don't go further than where defined.
            }
        }
        CheckLvalBorrowed(n, lv);
    }

    void CheckLvalBorrowed(Node *n, Borrow &lv) {
        if (lv.derefs.empty() && LifetimeType(lv.sid->lt) == LT_BORROW) {
            // This should only happen for multimethods and anonymous functions used with istype
            // where we can't avoid arguments being LT_BORROW.
            // All others should have been specialized to LT_KEEP when a var is not
            // single_assignment.
            // This is not particularly elegant but should be rare.
            Error(*n, "cannot assign to borrowed argument ", Q(lv.sid->id->name));
        }
        // FIXME: make this faster.
        for (auto &b : reverse(borrowstack)) {
            //LOG_INFO("borrow check:", lv.Name(), " vs ", b.Name(), " ", b.refc);
            if (!b.IsPrefix(lv)) continue;  // Not overwriting this one.
            if (!b.refc) continue;          // Lval is not borowed, writing is ok.
            Error(*n, "cannot modify ", Q(lv.Name()), " while borrowed in ",
                      Q(lv.sid->sf_def->parent->name));
        }
    }

    void ReplayAssigns(SubFunction *sf) {
        for (auto n : sf->reuse_assign_events) {
            Borrow lv(*n);
            CheckLvalBorrowed(n, lv);
        }
    }

    Lifetime PushBorrow(Node *n) {
        // Because of the Var in the check below, we may end up with things on the borrowstack
        // that are not refs once the var binds! This should be benign, as all we're doing
        // is locking it from overwrites, so in the worst case it gives an undeserved error?
        if (!IsRefNilVar(n->exptype->t)) return LT_ANY;
        Borrow lv(*n);
        // FIXME: if this is an exp we don't know how to borrow from (like a[i].b) we
        // return a generic borrow, but this disables lock checks so is unsafe.
        if (!lv.IsValid()) return LT_BORROW;
        for (auto &b : reverse(borrowstack)) {
            if (b.sid == lv.sid && b.DerefsEqual(lv)) {
                b.refc++;
                return (Lifetime)(&b - &borrowstack[0]);
            }
        }
        // FIXME: this path is slow, should not have to scan all of borrowstack.
        auto lt = (Lifetime)borrowstack.size();
        borrowstack.push_back(lv);
        return lt;
    }

    void CheckFreeVariable(SpecIdent &sid) {
        // If this is a free variable, record it in all parents up to the definition point.
        // FIXME: this is technically not the same as a "free variable" in the literature,
        // since HOFs get marked with freevars of their functionvalue this way.
        // This is benign, since the HOF will be specialized to the function value anyway,
        // but would be good to clean up.
        // We currently don't have an easy way to test for lexically enclosing functions.
        for (int i = (int)scopes.size() - 1; i >= 0; i--) {
            auto sf = scopes[i].sf;
            // Check if we arrived at the definition point.
            if (sid.sf_def == sf)
                break;
            // We use the id's type, not the flow sensitive type, just in case there's multiple uses
            // of the var. This will get corrected after the call this is part of.
            if (sf->AddFreeVar(sid))
                // If the freevar was already there, a previous call must have added it all the way
                // to the definition point, so we can stop here too.
                break;
        }
    }

    void CheckFreeVariablesFromFunction(SubFunction *sf) {
        if (sf->freevars.empty()) return;
        auto par = scopes.back().sf;
        // Checking these freevars is very expensive, so we check for multiple calls in the
        // same parent, which removes about 30%.
        if (freevar_check_preempt.find({ sf, par }) == freevar_check_preempt.end()) {
            for (auto &fv : sf->freevars) CheckFreeVariable(*fv.sid);
            freevar_check_preempt.insert({ sf, par });
        }
    }

    void TypeCheckList(List *n, Lifetime lt) {
        for (auto &c : n->children) {
            TT(c, 1, lt);
        }
    }

    TypeRef TypeCheckId(SpecIdent *sid) {
        auto type = sid->type;
        CheckFreeVariable(*sid);
        return type;
    }

    const Coercion *IsCoercion(const Node *n) {
        return dynamic_cast<const Coercion *>(n);
    }

    const Node *SkipCoercions(const Node *n) {
        auto c = IsCoercion(n);
        return c ? SkipCoercions(c->child) : n;
    }

    Lifetime LvalueLifetime(const Node &lval, bool deref) {
        if (auto idr = Is<IdentRef>(lval)) return idr->sid->lt;
        if (deref) {
            if (auto dot = Is<Dot>(lval)) return LvalueLifetime(*dot->child, deref);
            if (auto idx = Is<Indexing>(lval)) return LvalueLifetime(*idx->object, deref);
        }
        return LT_KEEP;
    }

    Lifetime LifetimeUnion(Node *&a, Node *&b, bool is_and) {
        if (a->lt == b->lt) {
            DecBorrowers(b->lt, *b);
            return a->lt;
        } else if (a->lt == LT_ANY && b->lt >= LT_BORROW) {
            // This case may apply in an if-then between a var and nil, or an and/or between
            // a var and a scalar.
            return b->lt;
        } else if (b->lt == LT_ANY && a->lt >= LT_BORROW) {
            // Same.
            return a->lt;
        } else if (is_and && a->lt >= LT_BORROW && b->lt >= LT_BORROW) {
            // var_a and var_b never results in var_a.
            DecBorrowers(a->lt, *a);
            return b->lt;
        } else {
            // If it is an and we want to borrow the lhs since it will never be used.
            // Otherwise default to LT_KEEP for everything.
            // FIXME: for cases where both sides are >= LT_BORROW (in an if-then) we'd like to
            // combine both lifetimes into one, but we currently can't represent that.
            AdjustLifetime(a, is_and ? LT_BORROW : LT_KEEP);
            if (is_and) DecBorrowers(a->lt, *a);
            AdjustLifetime(b, LT_KEEP);
            return LT_KEEP;
        }
    }

    void Borrowers(Lifetime lt, int change, const Node &context) {
        if (lt < 0) return;
        auto &b = borrowstack[lt];
        // NOTE: b may refer to a type that is not a ref, so we can't assert on IsRefNilVar,
        // see PushBorrow.
        b.refc += change;
        LOG_DEBUG("borrow ", change, ": ", b.sid->id->name, " in ", NiceName(context),
               ", ", b.refc, " remain");
        // FIXME: this should really just not be possible, but hard to guarantee.
        if (b.refc < 0)
            Error(context, Q(b.sid->id->name), " used in ", Q(NiceName(context)),
                           " without being borrowed");
        assert(b.refc >= 0);
        (void)context;
    }

    void IncBorrowers(Lifetime lt, const Node &context) { Borrowers(lt, 1, context); }
    void DecBorrowers(Lifetime lt, const Node &context) { Borrowers(lt, -1, context); }

    void ModifyLifetime(Node *n, size_t i, Lifetime lt) {
        if (n->lt == LT_MULTIPLE) {
            n->exptype->Set(i, n->exptype->Get(i), lt);
        } else {
            n->lt = lt;
        }
    }

    void AdjustLifetime(Node *&n, Lifetime recip, const node_small_vector *idents = nullptr) {
        assert(n->lt != LT_UNDEF && recip != LT_UNDEF);
        uint64_t incref = 0, decref = 0;
        auto rt = n->exptype;
        for (size_t i = 0; i < rt->NumValues(); i++) {
            assert (n->lt != LT_MULTIPLE || rt->t == V_TUPLE);
            auto givenlt = rt->GetLifetime(i, n->lt);
            auto given = LifetimeType(givenlt);
            if (idents) recip = LvalueLifetime(*(*idents)[i], false);  // FIXME: overwrite var?
            recip = LifetimeType(recip);
            if (given != recip) {
                auto rtt = rt->Get(i)->t;
                // Sadly, if it a V_VAR we have to be conservate and assume it may become a ref.
                if (IsRefNilVar(rtt)) {
                    // Special action required.
                    if (i >= sizeof(incref) * 8) Error(*n, "too many return values");
                    if (given == LT_BORROW && recip == LT_KEEP) {
                        incref |= 1LL << i;
                        DecBorrowers(givenlt, *n);
                    } else if (given == LT_KEEP && recip == LT_BORROW) {
                        decref |= 1LL << i;
                    } else if (given == LT_ANY) {
                        // These are compatible with whatever recip wants.
                    } else if (recip == LT_ANY) {
                        // recipient doesn't care, e.g. void statement.
                    } else {
                        assert(false);
                    }
                } else {
                    if (given == LT_BORROW) {
                        // This is a scalar that depends on a borrowed value, but the recipient
                        // doesn't care.
                        ModifyLifetime(n, i, LT_ANY);  // Avoid it travelling any further.
                        DecBorrowers(givenlt, *n);
                    }
                }
                if (given == LT_ANY) {
                    // Fill in desired lifetime, for consistency.
                    ModifyLifetime(n, i, recip);
                }
            }
        }
        if (incref || decref) {
            LOG_DEBUG("lifetime adjust for ", NiceName(*n), " to ", incref, "/", decref);
            MakeLifetime(n, idents ? LT_MULTIPLE: recip, incref, decref);
        }
    }

    // This is the central function thru which all typechecking flows, so we can conveniently
    // match up what the node produces and what the recipient expects.
    void TT(Node *&n, size_t reqret, Lifetime recip, node_small_vector *idents = nullptr) {
        STACK_PROFILE;
        // Central point from which each node is typechecked.
        n = n->TypeCheck(*this, reqret);
        // Check if we need to do any type adjustmenst.
        auto &rt = n->exptype;
        n->exptype = rt;
        auto nret = rt->NumValues();
        if (nret < reqret) {
            if (!n->Terminal(*this)) {
                Error(*n, Q(NiceName(*n)), " returns ", nret, " values, ", reqret, " needed");
            } else {
                // FIXME: would be better to have a general NORETURN type than patching things up
                // this way.
                if (reqret == 1) {
                    rt = type_any;
                } else {
                    auto nt = st.NewTuple(reqret);
                    for (size_t i = 0; i < reqret; i++) {
                        if (i < nret) nt->Set(i, rt->Get(i), rt->GetLifetime(i, n->lt));
                        else nt->Set(i, &*type_any, LT_ANY);
                    }
                    rt = nt;
                }
            }
        } else if (nret > reqret) {
            for (size_t i = reqret; i < nret; i++) {
                // This value will be dropped.
                DecBorrowers(rt->GetLifetime(i, n->lt), *n);
                // If this is a LT_KEEP value, codegen will make sure to throw it away.
            }
            switch (reqret) {
                case 0:
                    n->lt = LT_ANY;
                    rt = type_void;
                    break;
                case 1: {
                    auto typelt = TypeLT { *n, 0 };  // Get from tuple.
                    n->lt = typelt.lt;
                    rt = typelt.type;
                    break;
                }
                default: {
                    auto nt = st.NewTuple(reqret);
                    nt->tup->assign(rt->tup->begin(), rt->tup->begin() + reqret);
                    rt = nt;
                }
            }
        }
        // Check if we need to do any lifetime adjustments.
        AdjustLifetime(n, recip, idents);
        // Check for queries.
        if (query && query->qloc == n->line) ProcessQuery();
    }

    // TODO: Can't do this transform ahead of time, since it often depends upon the input args.
    TypeRef ActualBuiltinType(int flen, TypeRef type, NArgFlags flags, TypeRef etype,
                              const NativeFun *nf, bool test_overloads, size_t argn,
                              const Node &errorn) {
        if (flags & NF_BOOL) {
            type = type->ElementIfNil();
            assert(type->t == V_INT);
            return &st.default_bool_type->thistype;
        }
        // See if we can promote the type to one of the standard vector types
        // (xy/xyz/xyzw).
        if (!flen) return type;
        type = type->ElementIfNil();
        auto e = etype;
        size_t i = 0;
        for (auto vt = type; vt->t == V_VECTOR && i < SymbolTable::NUM_VECTOR_TYPE_WRAPPINGS;
            vt = vt->sub) {
            if (vt->sub->Numeric()) {
                // Check if we allow any vector length.
                if (!e.Null() && flen == -1 && e->t == V_STRUCT_S) {
                    flen = (int)e->udt->sfields.size();
                }
                if (flen >= 1) {
                    if (!e.Null() && e->t == V_STRUCT_S && (int)e->udt->sfields.size() == flen &&
                        e->udt->sametype->Equal(*vt->sub)) {
                        // Allow any similar vector type, like "color".
                        return etype;
                    } else {
                        // Require xy/xyz/xyzw
                        auto nvt = st.GetVectorType(vt, i, flen);
                        if (nvt.Null())
                            break;
                        return nvt;
                    }
                }
            }
            e = !e.Null() && e->t == V_VECTOR ? e->sub : nullptr;
            i++;
        }
        // We arrive here typically if flen == -1 but we weren't able to derive a length.
        // Sadly, we can't allow to return a vector type instead of a struct, so we error out,
        // and rely on the user to specify more precise types.
        // Not sure if there is a better solution.
        if (!test_overloads) {
            Error(errorn, "cannot deduce struct type for ",
                          (argn ? cat("argument ", argn) : "return value"),
                          " of ", Q(nf->name),
                          (!etype.Null() ? ", got " + Q(TypeName(etype)) : ""));
        }
        return type;
    };

    void LocationQuery(Line &line, string_view type) {
        THROW_OR_ABORT(
            cat("query_location: ", (*query->filenames)[line.fileidx].second, " ", line.line,
                " ", type));
    }

    void FindVar(vector<Arg> &vars) {
        for (auto &var : vars) {
            if (var.sid->id->name == query->iden) {
                LocationQuery(var.sid->id->line, TypeName(var.sid->type));
            }
        }
    }

    void ProcessQuery() {
        if (query->kind == "definition") {
            // The top scope includes a list of free vars so should be able to resolve any var
            // at the given location.. if no scopes, use top fun.
            auto sf = scopes.empty() ? st.toplevel : scopes.back().sf;
            FindVar(sf->args);
            FindVar(sf->locals);
            FindVar(sf->freevars);
            auto gudt = st.LookupStruct(query->iden);
            if (gudt) {
                LocationQuery(gudt->line, Signature(*gudt));
            }
            // FIXME: may not work when namespaces are involved.
            auto f = st.FindFunction(query->iden);
            if (f) {
                auto ov = f->overloads[0];
                if (ov->gbody) {  // FIXME: ignores function types.
                    LocationQuery(ov->gbody->line, ov->sf ? Signature(*ov->sf) : "");  
                }
            }
            auto nf = parser.natreg.FindNative(query->iden);
            if (nf) {
                // This doesn't have a source code location, so output a signature the IDE can display.
                THROW_OR_ABORT("query_signature: " + Signature(*nf));
            }
            auto fld = st.FieldUse(query->iden);
            if (fld) {
                // To know what this belongs to, would need to find the object it belongs to.
                // For now, simply see if we can find any class that has this field.
                for (auto gudt : st.gudttable) {
                    int fi = gudt->Has(fld);
                    if (fi >= 0) {
                        // FIXME: this is really basic, lets at least find the field line.
                        LocationQuery(gudt->line, TypeName(gudt->fields[fi].giventype));
                    }
                }
            }
            THROW_OR_ABORT("query_unknown_ident: " + query->iden);
        } else {
            THROW_OR_ABORT("query_unknown_kind: " + query->kind);
        }
    }

    void Stats(vector<pair<string, string>> &filenames) {
        if (min_output_level > OUTPUT_INFO) return;
        int origsf = 0, clonesf = 0;
        size_t orignodes = 0, clonenodes = 0;
        map<Overload *, size_t> funstats;
        for (auto sf : st.subfunctiontable) {
            auto count = sf->node_count;
            if (!sf->next)        {
                origsf++;
                orignodes += count;
            } else {
                clonesf++;
                clonenodes += count;
                funstats[sf->overload] += count;
            }
        }
        vector<pair<Overload *, size_t>> funstatsv;
        for (auto &p : funstats)
            if (p.second > orignodes / 200)
                funstatsv.push_back(p);
        sort(funstatsv.begin(), funstatsv.end(),
             [](const pair<Overload *, size_t> &a, const pair<Overload *, size_t> &b) {
                 return a.second > b.second;
             });
        LOG_INFO("SF count: orig: ", origsf, ", cloned: ", clonesf);
        LOG_INFO("Node count: orig: ", orignodes, ", cloned: ", clonenodes);
        for (auto [ov, fsize] : funstatsv) {
            auto f = ov->sf->parent;
            auto s = cat("Most clones: ", f->name, "/", f->nargs());
            if (auto body = ov->sf->sbody) {
                s += cat(" (", filenames[body->line.fileidx].first, ":", body->line.line, ")");
            }
            LOG_INFO(s, " -> ", fsize, " nodes accross ", ov->NumSubf() - 1, " extra clones");
        }
    }
};

Node *Block::TypeCheck(TypeChecker &tc, size_t reqret) {
    // Bring functions into scope.
    for (auto def : children) {
        if (auto fr = Is<FunRef>(def)) {
            auto f = fr->sf->parent;
            if (!f->anonymous) tc.st.FunctionDeclTT(*f);
        }
    };
    // Now typecheck the body.
    for (auto &c : children) {
        tc.TT(c, c != children.back() ? 0 : reqret, LT_ANY);
    }
    lt = children.back()->lt;
    exptype = children.back()->exptype;
    // Remove functions from scope again. See also CleanupStatements.
    for (auto def : children) {
        if (auto fr = Is<FunRef>(def)) {
            auto f = fr->sf->parent;
            if (!f->anonymous) tc.st.Unregister(f);
        }
    };
    return this;
}

Node *List::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    assert(false);  // Parents call TypeCheckList
    return this;
}

Node *Unary::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    assert(false);
    return this;
}

Node *BinOp::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    assert(false);
    return this;
}

Node *Or::TypeCheck(TypeChecker &tc, size_t reqret) {
    TypeRef dummy;
    tc.TypeCheckAndOr(*this, false, reqret, dummy);
    return this;
}

Node *And::TypeCheck(TypeChecker &tc, size_t reqret) {
    TypeRef dummy;
    tc.TypeCheckAndOr(*this, false, reqret, dummy);
    return this;
}

Node *IfThen::TypeCheck(TypeChecker &tc, size_t) {
    auto constant = tc.TypeCheckCondition(condition, this, "if");
    if (!constant || constant->True()) {
        tc.TypeCheckBranch(true, condition, truepart, 0);
        if (truepart->Terminal(tc)) {
            // This is an if ..: return, we should leave promotions for code after the if.
            tc.CheckFlowTypeChanges(false, condition);
        }
        if (constant && constant->True()) {
            // Replace if-then by just the branch.
            auto r = truepart;
            truepart = nullptr;
            delete this;
            return r;
        }
    } else {
        // constant == false: this if-then is entirely redundant, replace.
        auto r = new DefaultVal(line);
        r->exptype = type_void;
        r->lt = LT_ANY;
        delete this;
        return r;
    }
    // No else: this always returns void.
    truepart->exptype = type_void;
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *IfElse::TypeCheck(TypeChecker &tc, size_t reqret) {
    auto constant = tc.TypeCheckCondition(condition, this, "if");
    if (!constant) {
        auto tleft = tc.TypeCheckBranch(true, condition, truepart, reqret);
        auto tright = tc.TypeCheckBranch(false, condition, falsepart, reqret);
        // FIXME: this is a bit of a hack. Much better if we had an actual type
        // to signify NORETURN, to be taken into account in more places.
        if (truepart->Terminal(tc)) {
            exptype = tright;
            lt = falsepart->lt;
        } else if (falsepart->Terminal(tc)) {
            exptype = tleft;
            lt = truepart->lt;
        } else {
            exptype = tc.Union(tleft, tright, "then branch", "else branch",
                               CF_COERCIONS, this);
            tc.SubType(truepart->children.back(), exptype, "then branch", *this);
            tc.SubType(falsepart->children.back(), exptype, "else branch", *this);
            lt = tc.LifetimeUnion(truepart->children.back(), falsepart->children.back(), false);
        }
        return this;
    } else if (constant->True()) {
        // Ignore the else part, and delete it, since we don't want to TT it.
        tc.TypeCheckBranch(true, condition, truepart, reqret);
        auto r = truepart;
        truepart = nullptr;
        delete this;
        return r;
    } else {
        // Ignore the then part, and delete it, since we don't want to TT it.
        tc.TypeCheckBranch(false, condition, falsepart, reqret);
        auto r = falsepart;
        falsepart = nullptr;
        delete this;
        return r;
    }
}

Node *While::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckCondition(condition, this, "while");
    tc.scopes.back().loop_count++;
    tc.TypeCheckBranch(true, condition, wbody, 0);
    tc.scopes.back().loop_count--;
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *For::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    // FIXME: would be good to detect when iter is not written to, so ForLoopElem can be LT_BORROW.
    // Alternatively we could IncBorrowers on iter, but that would be very restrictive.
    tc.TT(iter, 1, LT_BORROW);
    auto itertype = iter->exptype;
    if (itertype->t == V_INT) {}
    else if (itertype->t == V_STRING)
        itertype = type_int;
    else if (itertype->t == V_VECTOR)
        itertype = itertype->Element();
    else tc.Error(*this, Q("for"), " can only iterate over int / string / vector, not ",
                         Q(TypeName(itertype)));
    tc.st.BlockScopeStart();
    auto def = Is<Define>(fbody->children[0]);
    if (def) {
        auto fle = Is<ForLoopElem>(def->child);
        if (fle) {
            fle->exptype = itertype;
            if (def->sids[0].first->withtype)
                tc.st.AddWithStructTT(itertype, def->sids[0].first->id, tc.scopes.back().sf);
        }
    }
    tc.scopes.back().loop_count++;
    fbody->TypeCheck(tc, 0);
    tc.scopes.back().loop_count--;
    tc.st.BlockScopeCleanup();
    tc.DecBorrowers(iter->lt, *this);
    // Currently always return V_NIL
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *ForLoopElem::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    // Already been assigned a type in For.
    lt = LT_KEEP;
    return this;
}

Node *ForLoopCounter::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    exptype = type_int;
    lt = LT_ANY;
    return this;
}

Node *Break::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (!tc.scopes.back().loop_count)
        tc.Error(*this, Q("break"), " must occur inside a ", Q("while"), " or ", Q("for"));
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *Switch::TypeCheck(TypeChecker &tc, size_t reqret) {
    // TODO: much like If, should only typecheck one case if the value is constant, and do
    // the corresponding work in the optimizer.
    tc.TT(value, 1, LT_BORROW);
    tc.DecBorrowers(value->lt, *this);
    auto ptype = value->exptype;
    if (!ptype->Numeric() && ptype->t != V_STRING && ptype->t != V_CLASS)
        tc.Error(*this, "switch value must be int / float / string / class");
    exptype = nullptr;
    ssize_t default_loc = -1;
    vector<bool> enum_cases;
    if (ptype->IsEnum()) enum_cases.resize(ptype->e->vals.size());
    cases->exptype = type_void;
    cases->lt = LT_ANY;
    for (auto [i, n] : enumerate(cases->children)) {
        tc.switch_case_context = this;
        tc.TT(n, reqret, LT_KEEP);
        auto cas = AssertIs<Case>(n);
        if (!cas->pattern->Arity()) default_loc = i;
        cas->pattern->exptype = type_void;
        cas->pattern->lt = LT_ANY;
        for (auto c : cas->pattern->children) {
            if (ptype->t == V_CLASS) {
                if (!Is<UDTRef>(c)) tc.Error(*c, "non-type value in switch on type");
            } else {
                tc.SubTypeT(c->exptype, ptype, *c, "", "case");
                tc.DecBorrowers(c->lt, *cas);
                if (ptype->IsEnum()) {
                    assert(c->exptype->IsEnum());
                    Value v = NilVal();
                    if (c->ConstVal(&tc, v) != V_VOID) {
                        for (auto [i, ev] : enumerate(ptype->e->vals)) if (ev->val == v.ival()) {
                            enum_cases[i] = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!cas->cbody->Terminal(tc)) {
            exptype = exptype.Null() ? cas->cbody->exptype
                                     : tc.Union(exptype, cas->cbody->exptype, "switch type", "case type",
                                                CF_COERCIONS, cas);
        }
    }
    if (default_loc >= 0) {
        // Stick the default at the end, simplifies codegen.
        auto d = cases->children[default_loc];
        cases->children.erase(default_loc);
        cases->children.push_back(d);
    }
    for (auto n : cases->children) {
        auto cas = AssertIs<Case>(n);
        if (!cas->cbody->Terminal(tc)) {
            assert(!exptype.Null());
            tc.SubType(cas->cbody, exptype, "", "case block");
        }
    }
    if (exptype.Null()) exptype = type_void;  // Empty switch or all return statements.
    if (ptype->t == V_CLASS) {
        auto &dispatch_udt = *ptype->udt;
        dispatch_udt.subudts_dispatched = true;
        vtable_idx = -1;
        vector<int> case_picks;
        for (auto udt : dispatch_udt.subudts) {
            int pick = -1;
            if (!udt->g.is_abstract) {
                int best_dist = -1;
                int default_case = -1;
                for (auto [i, n] : enumerate(cases->children)) {
                    auto cas = AssertIs<Case>(n);
                    if (cas->pattern->Arity()) {
                        auto udtref = Is<UDTRef>(cas->pattern->children[0]);
                        auto sdist = SuperDistance(udtref->udt, udt);
                        if (sdist >= 0 && (pick < 0 || best_dist >= sdist)) {
                            if (best_dist == sdist)
                                tc.Error(*udtref, "more than one case applies to ", Q(udt->name));
                            pick = (int)i;
                            best_dist = sdist;
                        }
                    } else {
                        default_case = (int)i;
                    }
                }
                if (pick < 0) pick = default_case;
                if (pick < 0) tc.Error(*this, "no case applies to ", Q(udt->name));
            }
            case_picks.push_back(pick);
            vtable_idx = std::max(vtable_idx, (int)udt->dispatch_table.size());
        }
        // FIXME: check here if any vtable entries are equal so we don't need to store
        // a new one.
        assert(vtable_idx >= 0);
        // Add cases to all vtables.
        for (auto [i, udt] : enumerate(dispatch_udt.subudts)) {
            auto &dt = udt->dispatch_table;
            assert((int)dt.size() <= vtable_idx);  // Double entry.
            // FIXME: this is not great, wasting space, but only way to do this
            // on the fly without tracking lots of things.
            while ((int)dt.size() < vtable_idx)
                dt.push_back({});
            dt.push_back({ nullptr, case_picks[i], true });
        }
        auto de = &dispatch_udt.dispatch_table[vtable_idx];
        de->is_switch_dispatch = true;
        de->is_dispatch_root = true;
        de->subudts_size = dispatch_udt.subudts.size();
    } else {
        if (default_loc < 0) {
            if (reqret) tc.Error(*this, "switch that returns a value must have a default case");
            if (ptype->IsEnum()) {
                for (auto [i, ev] : enumerate(ptype->e->vals)) {
                    if (!enum_cases[i])
                        tc.Error(*value, "enum value ", Q(ev->name), " not tested in switch");
                }
            }
        }
    }
    lt = LT_KEEP;
    return this;
}

Node *Case::TypeCheck(TypeChecker &tc, size_t reqret) {
    // FIXME: Since string constants are the real use case, LT_KEEP would be more
    // natural here, as this will introduce a lot of keeprefs. Alternatively make sure
    // string consts don't introduce keeprefs.
    auto sw = tc.switch_case_context;
    tc.switch_case_context = nullptr;
    auto flowstart = tc.flowstack.size();
    if (pattern->Arity()) {
        if (auto udtref = Is<UDTRef>(pattern->children[0])) {
            tc.CheckFlowTypeIdOrDot(*sw->value, &udtref->udt->thistype);
            udtref->TypeCheck(tc, 0);
        } else {
            tc.TypeCheckList(pattern, LT_BORROW);
        }
    }
    tc.TT(cbody, reqret, LT_KEEP);
    tc.CleanUpFlow(flowstart);
    exptype = cbody->exptype;
    lt = LT_KEEP;
    return this;
}

Node *Range::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(start, 1, LT_KEEP);
    tc.TT(end, 1, LT_KEEP);
    exptype = start->exptype;
    if (exptype->t != end->exptype->t || !exptype->Numeric())
        tc.Error(*this, "range can only be two equal numeric types");
    lt = LT_ANY;
    return this;
}

Node *Define::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    for (auto &p : sids) {
        tc.UpdateCurrentSid(p.first);
        // We have to set these here just in case the init exp is a function call that
        // tries use/assign this variable, type_undefined will force that to be an error.
        // TODO: could make this a specialized error, but probably not worth it because it is rare.
        p.first->type = type_undefined;
        p.first->lt = LT_UNDEF;
    }
    // We default to LT_KEEP here.
    // There are case where we could allow borrow, but in practise this runs into trouble easily:
    // - Variables that later get assigned (!sid->id->single_assignment) where taking ownership
    //   was really what was intended (since the lval being assigned from may go away).
    // - old := cur cases, where old is meant to hang on to the previous value as cur gets updated,
    //   which then runs into borrowing errors.
    tc.TT(child, Is<DefaultVal>(child) ? 0 : sids.size(), LT_KEEP);
    for (auto [i, p] : enumerate(sids)) {
        auto var = TypeLT(*child, i);
        if (!p.second.Null()) {
            var.type = tc.st.ResolveTypeVars(p.second, this->line);
            if (Is<DefaultVal>(child)) {  // A pre-decl.
                p.first->id->predeclaration = true;
            } else {
                p.first->id->predeclaration = false;
                // Have to subtype the initializer value, as that node may contain
                // unbound vars (a:[int] = []) or values that that need to be coerced
                // (a:float = 1)
                if (sids.size() == 1) {
                    tc.SubType(child, var.type, "initializer", "definition");
                } else {
                    // FIXME: no coercion when mult-return?
                    tc.SubTypeT(child->exptype->Get(i), var.type, *this, p.first->id->name);
                }
                // In addition, the initializer may already cause the type to be promoted.
                // a:string? = ""
                FlowItem fi(p.first, var.type, child->exptype);
                // Similar to AssignFlowPromote (TODO: refactor):
                if (fi.now->t != V_NIL && fi.old->t == V_NIL) tc.flowstack.push_back(fi);
            }
        }
        auto sid = p.first;
        sid->type = var.type;
        tc.StorageType(var.type, *this);
        sid->type = var.type;
        sid->lt = var.lt;
        LOG_DEBUG("var: ", sid->id->name, ":", TypeName(var.type));
    }
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *Member::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_KEEP);
    auto &f = *field();
    f.in_scope = true;
    tc.scopes.back().scoped_fields.push_back(this);
    if (this_sid) tc.UpdateCurrentSid(this_sid);
    exptype = type_void;
    lt = LT_ANY; 
    return this;
}

Node *Static::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, sid->lt);
    tc.SubType(child, sid->type, "static initializer", *this);
    // FIXME: not doing any of the flow stuff Assign / Define do, needed?
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *AssignList::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    for (auto &c : children) {
        if (c != children.back()) {
            tc.TT(c, 1, LT_BORROW);
            tc.DecBorrowers(c->lt, *this);
            if (!Is<IdentRef>(c) && !Is<Dot>(c)) {
                tc.Error(*this, "assignment list elements must be variables or class members");
            }
        } else {
            tc.TT(c, children.size() - 1, LT_MULTIPLE /*unused*/, &children);
        }
    }
    for (size_t i = 0; i < children.size() - 1; i++) {
        auto left = children[i];
        tc.CheckLval(left);
        TypeRef righttype = children.back()->exptype->Get(i);
        FlowItem fi(*left, left->exptype);
        assert(fi.IsValid());
        tc.AssignFlowDemote(fi, righttype, CF_NONE);
        tc.SubTypeT(righttype, left->exptype, *this, "right");
        tc.StorageType(left->exptype, *left);
        // TODO: should call tc.AssignFlowPromote(*left, vartype) here?
    }
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *IntConstant::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    exptype = from ? &from->e->thistype : type_int;
    lt = LT_ANY;
    return this;
}

Node *FloatConstant::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    exptype = type_float;
    lt = LT_ANY;
    return this;
}

Node *StringConstant::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    exptype = type_string;
    // The VM keeps all the constant strings for the length of the program,
    // so these can be borrow, avoiding a ton of keepvars when used in + and
    // builtin functions etc (at the cost of some increfs when stored in vars
    // and data structures).
    lt = STRING_CONSTANTS_KEEP ? LT_KEEP : LT_BORROW;
    return this;
}

Node *Nil::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (giventype.Null()) {
        exptype = tc.st.Wrap(tc.st.NewTypeVar(), V_NIL, &line);
    } else {
        exptype = tc.st.ResolveTypeVars(giventype, this->line);
        if (!tc.st.IsNillable(exptype->sub)) tc.Error(*this, "illegal nil type");
    }
    lt = LT_ANY;
    return this;
}

Node *Plus::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Minus::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Multiply::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Divide::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Mod::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *PlusEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *MultiplyEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *MinusEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *DivideEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *ModEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *AndEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEqBit(*this);
}

Node *OrEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEqBit(*this);
}

Node *XorEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEqBit(*this);
}

Node *ShiftLeftEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEqBit(*this);
}

Node *ShiftRightEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckMathOpEqBit(*this);
}

Node *NotEqual::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *Equal::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *GreaterThanEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *LessThanEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *GreaterThan::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *LessThan::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *Not::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (auto nn = tc.OperatorOverload(*this)) return nn;
    tc.DecBorrowers(child->lt, *this);
    tc.NoStruct(*child, "not");
    exptype = &tc.st.default_bool_type->thistype;
    lt = LT_ANY;
    return this;
}

Node *BitAnd::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *BitOr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *Xor::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *ShiftLeft::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *ShiftRight::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *Negate::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (auto nn = tc.OperatorOverload(*this)) return nn;
    tc.SubType(child, type_int, "negated value", *this);
    tc.DecBorrowers(child->lt, *this);
    exptype = child->exptype;
    lt = LT_ANY;
    return this;
}

Node *PostDecr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *PostIncr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *PreDecr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *PreIncr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *UnaryMinus::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (auto nn = tc.OperatorOverload(*this)) return nn;
    exptype = child->exptype;
    if (!exptype->Numeric() &&
        (exptype->t != V_STRUCT_S || !exptype->udt->sametype->Numeric()))
        tc.RequiresError("numeric / numeric struct", exptype, *this);
    tc.DecBorrowers(child->lt, *this);
    lt = LT_KEEP;
    return this;
}

Node *IdentRef::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.UpdateCurrentSid(sid);
    for (auto &sc : reverse(tc.scopes)) if (sc.sf == sid->sf_def) goto in_scope;
    tc.Error(*this, "free variable ", Q(sid->id->name), " not in scope");
    in_scope:
    if (sid->id->predeclaration)
        tc.Error(*this, "access of ", Q(sid->id->name), " before being initialized");
    exptype = tc.TypeCheckId(sid);
    FlowItem fi(*this, exptype);
    assert(fi.IsValid());
    exptype = tc.UseFlow(fi);
    lt = tc.PushBorrow(this);
    return this;
}

Node *Assign::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (auto nn = tc.OperatorOverload(*this)) return nn;
    tc.DecBorrowers(left->lt, *this);
    tc.TT(right, 1, tc.LvalueLifetime(*left, false));
    tc.CheckLval(left);
    FlowItem fi(*left, left->exptype);
    if (fi.IsValid()) {
        left->exptype = tc.AssignFlowDemote(fi, right->exptype, CF_COERCIONS);
    }
    tc.SubType(right, left->exptype, "right", *this);
    if (fi.IsValid()) tc.AssignFlowPromote(*left, right->exptype);
    exptype = left->exptype;
    if (fi.IsValid()) exptype = tc.UseFlow(fi);
    lt = tc.PushBorrow(left);
    return this;
}

Node *DefaultVal::TypeCheck(TypeChecker &, size_t) {
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *GenericCall::TypeCheck(TypeChecker &tc, size_t reqret) {
    STACK_PROFILE;
    // Here we decide which of Dot / Call / NativeCall this call should be transformed into.
    tc.TypeCheckList(this, LT_ANY);
    tc.st.current_namespace = ns;
    auto nf = tc.parser.FindNative(name);
    auto fld = tc.st.FieldUse(name);
    TypeRef type;
    UDT *udt = nullptr;
    if (children.size()) {
        type = children[0]->exptype;
        if (IsUDT(type->t)) udt = type->udt;
    }
    Node *r = nullptr;
    auto sup_err = [&]() {
        if (super) {
            tc.Error(*this, "super must be used on a method that has a superclass implementation");
        }
    };
    if (fld && fromdot && noparens && udt && udt->g.Has(fld) >= 0) {
        auto dot = new Dot(fld, *this);
        sup_err();
        r = dot->TypeCheck(tc, reqret);
    } else {
        bool prefer_ff = false;
        // Pick better match if any..
        auto nargs = children.size();
        // FIXME: this doesn't always find lexically enclosing functions, since if the typechecker
        // goes funlevel1a -> funlevel2b -> funlevel1c, it will still find functions from level2!
        // Not the same as the parser, and not sure how to best fix that.
        auto ff = tc.st.FindFunction(name);
        auto f = ff;
        // Get best one sofar.
        for (; f; f = f->sibf) {
            if (nargs == f->nargs()) {
                break;
            }
        }
        if (!f) f = ff;
        SubFunction *usf = nullptr;
        int udist = 999;
        if (udt && f && f->nargs()) {
            for (auto ov : f->overloads) {
                auto ti = ov->sf->args[0].type;
                if (IsUDT(ti->t)) {
                    auto dist = SuperDistance(ti->udt, udt);
                    if (dist >= 0 && dist < udist) {
                        usf = ov->sf;
                        udist = dist;
                        if (dist == 0) break;
                    }
                }
            }
        }
        if (f && nf && f->nargs() == nargs) {
            // If we have an f with more & matching args than the nf, pick that.
            // Or if any of f's specializations matches type, then it overrides nf.
            if (f->nargs() > nf->args.size() || usf) {
                prefer_ff = true;
            }
        }
        if (nf && !prefer_ff) {
            auto nc = new NativeCall(nf, line);
            nc->children = children;
            sup_err();
            r = nc->TypeCheck(tc, reqret);
        } else if (f) {
            // Now that we're sure it's going to be a call, pick the right function, fill in default/self args.
            // sibf is ordered by most args first, which is the right order for us to check them in,
            // since if we possibly can insert a self-arg with matching type that should take priority.
            // And the parser already guaranteed there is no overlap between functions w.r.t. default
            // args so no risk we match a higher args version unnecessarily.
            for (f = ff; f; f = f->sibf) {
                if (nargs > f->nargs()) {
                    f = nullptr;
                    break;
                }
                // If we have less args, try insert self arg.
                if (nargs < f->nargs() && !fromdot && (int)nargs + 1 >= f->first_default_arg) {
                    // We go down the withstack but skip items that don't correspond to lexical order
                    // for cases where withcontext1 -> withcontext2 -> lambdaincontext1
                    // or to simply not use withstack items of unrelated callers.
                    Overload *lex_ov = tc.scopes.back().sf->overload;
                    for (auto wse : reverse(tc.st.withstack)) {
                        bool in_lex_scope = false;
                        for (auto lov = lex_ov; lov; lov = lov->sf->lexical_parent) {
                            if (lov == wse.sf->overload) {
                                in_lex_scope = true;
                                break;
                            }
                        }
                        if (!in_lex_scope) continue;
                        int best_superdist = INT_MAX;
                        for (auto ov : f->overloads) {
                            auto &arg0 = ov->sf->args[0];
                            // If we're in the context of a withtype, calling a function that starts
                            // with an arg of the same type we pass it in automatically. This is
                            // maybe a bit very liberal, should maybe restrict it?
                            auto gudt0 = GetGUDTAny(arg0.type);
                            if (gudt0 && arg0.sid->withtype && wse.id) {
                                auto superdist = DistanceToSpecializedSuper(gudt0, wse.udt_tc);
                                if (superdist >= 0 && superdist < best_superdist) {
                                    best_superdist = superdist;
                                    usf = ov->sf;
                                }
                            }
                        }
                        if (best_superdist < INT_MAX) {
                            auto self = new IdentRef(line, wse.id->cursid);
                            children.insert(0, self);
                            tc.TT(children[0], 1, LT_ANY);
                            nargs++;
                            ff = f;
                            break;
                        }
                    }
                }
                // If we have still have less args, try insert default args.
                if (nargs < f->nargs() && (int)nargs >= f->first_default_arg) {
                    for (size_t i = nargs; i < f->nargs(); i++) {
                        children.push_back(f->default_args[i - f->first_default_arg]->Clone());
                        tc.TT(children.back(), 1, LT_ANY);
                        nargs++;
                    }
                }
                if (nargs == f->nargs()) {
                    ff = f;
                    break;
                }
            }
            if (!f) {
                tc.Error(*this, "no version of function ", Q(name), " takes ", nargs, " arguments");
            }
            if (!usf || !usf->overload->method_of || usf->overload->method_of->gsuperclass.Null())
                sup_err();
            auto fc = new Call(*this, usf && usf->parent == ff ? usf : ff->overloads[0]->sf);
            fc->children = children;
            r = fc->TypeCheck(tc, reqret);
        } else {
            if (fld && fromdot && noparens) {
                tc.Error(*this, "type ", Q(TypeName(type)), " does not have field ", Q(fld->name));
            }
            tc.Error(*this, "unknown field/function reference ", Q(name));
        }
    }



    children.clear();
    delete this;
    return r;
}

Node *NativeCall::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (!children.empty() && children[0]->exptype->t == V_UNDEFINED) {
        // Not from GenericCall.
        tc.TypeCheckList(this, LT_ANY);
    }
    if (nf->first->overloads) {
        // Multiple overloads available, figure out which we want to call.
        auto cnf = nf->first;
        for (; cnf; cnf = cnf->overloads) {
            if (cnf->args.size() < Arity()) continue;
            for (auto [i, arg] : enumerate(cnf->args)) {
                if (i >= children.size()) {
                    // Default args always good for overload match.
                    if (arg.type->t == V_NIL) continue;
                    goto nomatch;
                }
                // Special purpose treatment of V_ANY to allow generic vectors in overloaded
                // length() etc.
                auto etype = children[i]->exptype;
                auto cf = CF_NUMERIC_NIL;
                if (arg.type->t != V_STRING) cf = ConvertFlags(cf | CF_COERCIONS);
                if (arg.type->t != V_ANY &&
                    (arg.type->t != V_VECTOR ||
                     etype->t != V_VECTOR ||
                     arg.type->sub->t != V_ANY) &&
                    !tc.ConvertsTo(etype,
                                   tc.ActualBuiltinType(arg.fixed_len, arg.type, arg.flags,
                                                        etype, nf, true, i + 1, *this),
                                   cf)) goto nomatch;
            }
            nf = cnf;
            break;
            nomatch:;
        }
        if (!cnf)
            tc.NatCallError("arguments match no overloads of ", nf, *this);
    }
    for (auto [i, arg] : enumerate(nf->args)) {
        if (i >= Arity()) {
            if (arg.type->t == V_NIL) {
                assert(arg.fixed_len >= 0);
                auto type = tc.ActualBuiltinType(arg.fixed_len, arg.type->sub, arg.flags,
                                                 type_undefined,
                                                 nf, true, i + 1, *this);
                switch (type->t) {
                    case V_INT: {
                        auto ic = new IntConstant(line, arg.default_val);
                        Add(ic);
                        if ((arg.flags & NF_BOOL) && tc.st.default_bool_type) {
                            auto ev = tc.st.default_bool_type->Lookup(arg.default_val);
                            if (ev) ic->from = ev;
                        }
                        break;
                    }
                    case V_FLOAT:
                        Add(new FloatConstant(line, arg.default_val));
                        break;
                    case V_STRUCT_S: {
                        auto cons = new Constructor(line, { type });
                        for (auto &f : type->udt->sfields) {
                            if (f.type->t == V_FLOAT) {
                                cons->Add(new FloatConstant(line, 0.0));
                            } else {
                                // FIXME: This may be unresolved/ununified, for now assume int,
                                // since that will convert to float if needed.
                                cons->Add(new IntConstant(line, 0));
                            }
                        }
                        Add(cons);
                        break;
                    }
                    default:
                        Add(new Nil(line, { tc.st.Wrap(type, V_NIL, &line) }));
                        break;
                }
                tc.TT(children.back(), 1, LT_ANY);
            } else {
                tc.NatCallError("wrong number of many arguments for ", nf, *this);
            }
        }
    }
    if (children.size() != nf->args.size()) {
        tc.NatCallError("too many arguments for ", nf, *this);
    }
    vector<TypeRef> argtypes(children.size());
    for (auto [i, c] : enumerate(children)) {
        auto &arg = nf->args[i];
        auto argtype = tc.ActualBuiltinType(arg.fixed_len, arg.type, arg.flags, children[i]->exptype, nf, false, i + 1, *this);
        // Filter out functions that are not struct aware.
        bool typed = false;
        if (argtype->t == V_NIL && argtype->sub->Numeric()) {
            // This is somewhat of a hack, because we conflate V_NIL with being optional
            // for native functions, but we don't want numeric types to be nilable.
            argtype = argtype->sub;
        }
        if (arg.flags & NF_CONVERTANYTOSTRING && c->exptype->t != V_STRING) {
            tc.AdjustLifetime(c, LT_BORROW);  // MakeString wants to borrow.
            tc.MakeString(c, arg.lt);
            argtype = type_string;
            typed = true;
        }
        auto cf_const = arg.flags & NF_CONST ? CF_COVARIANT : CF_NONE;
        int flag = NF_SUBARG1;
        for (int sa = 0; sa < 3; sa++) {
            if (arg.flags & flag) {
                if (arg.flags & NF_UNION) {
                    argtypes[sa] = tc.Union(argtypes[sa], c->exptype, "left", "right",
                                            CF_NONE, this);
                }
                tc.SubType(c,
                           nf->args[sa].type->t == V_VECTOR && argtypes[sa]->t == V_VECTOR && argtype->t != V_VECTOR
                            ? argtypes[sa]->sub
                            : argtypes[sa],
                        tc.ArgName(i),
                        nf->name,
                        cf_const);
                // Stop these generic params being turned into any by SubType below.
                typed = true;
            }
            flag *= 2;
        }
        if (arg.flags & NF_ANYVAR) {
            if (argtype->t == V_VECTOR)
                argtype = tc.st.Wrap(tc.st.NewTypeVar(), V_VECTOR, &line);
            else if (argtype->t == V_ANY)
                argtype = tc.st.NewTypeVar();
            else assert(0);
        }
        if (argtype->t == V_ANY) {
            if (!arg.flags) {
                // Special purpose type checking to allow any reference type for functions like
                // copy/equal/hash etc. Note that this is the only place in the language where
                // we allow this!
                if (!IsRefNilNoStruct(c->exptype->t))
                    tc.RequiresError("reference type", c->exptype, *c, nf->args[i].name, nf->name);
                typed = true;
            } else if (IsStruct(c->exptype->t) &&
                       !(arg.flags & NF_PUSHVALUEWIDTH) &&
                       c->exptype->udt->numslots > 1) {
                // Avoid unsuspecting generic functions taking values as args.
                // TODO: ideally this does not trigger for any functions.
                tc.Error(*this, "function does not support this struct type");
            }
        }
        if (nf->fun.fnargs >= 0 && !arg.fixed_len && !(arg.flags & NF_PUSHVALUEWIDTH))
            tc.NoStruct(*c, nf->name);
        if (!typed)
            tc.SubType(c, argtype, tc.ArgName(i), nf->name, cf_const);
        argtypes[i] = c->exptype;
        tc.StorageType(c->exptype, *this);
        tc.AdjustLifetime(c, arg.lt);
        tc.DecBorrowers(c->lt, *this);
    }

    exptype = type_void;  // no retvals
    lt = LT_ANY;
    if (nf->retvals.size() > 1) exptype = tc.st.NewTuple(nf->retvals.size());
    for (auto [i, ret] : enumerate(nf->retvals)) {
        int sa = 0;
        auto type = ret.type;
        auto rlt = ret.lt;
        switch (ret.flags) {
            case NF_SUBARG3: sa++;
            case NF_SUBARG2: sa++;
            case NF_SUBARG1: {
                type = argtypes[sa];
                auto nftype = nf->args[sa].type;

                if (nftype->t == V_TYPEID) {
                    assert(!sa);  // assumes always first.
                    auto tin = AssertIs<TypeOf>(children[0]);
                    type = tin->child->exptype;
                }

                if (ret.type->t == V_NIL) {
                    if (!tc.st.IsNillable(type))
                        tc.Error(*this, "argument ", sa + 1, " to ", Q(nf->name),
                                        " has to be a reference type");
                    type = tc.st.Wrap(type, V_NIL, &line);
                } else if (nftype->t == V_VECTOR && ret.type->t != V_VECTOR) {
                    if (type->t == V_VECTOR) type = type->sub;
                } else if (nftype->t == V_FUNCTION) {
                    auto csf = type->sf;
                    assert(csf);
                    // In theory it is possible this hasn't been generated yet..
                    type = csf->returntype;
                }
                if (rlt == LT_BORROW) {
                    auto alt = nf->args[sa].lt;
                    assert(alt >= LT_BORROW);
                    rlt = alt;
                }
                break;
            }
            case NF_ANYVAR:
                type = ret.type->t == V_VECTOR ? tc.st.Wrap(tc.st.NewTypeVar(), V_VECTOR, &line)
                                               : tc.st.NewTypeVar();
                assert(rlt == LT_KEEP);
                break;
            default:
                break;
        }
        // This allows the 0th retval to inherit the type of the 0th arg, and is
        // a bit special purpose..
        type = tc.ActualBuiltinType(ret.fixed_len, type, ret.flags,
                                    !i && Arity() ? children[0]->exptype : nullptr, nf, false,
                                    0, *this);
        if (!IsRefNilVar(type->t)) rlt = LT_ANY;
        if (nf->retvals.size() > 1) {
            exptype->Set(i, type.get(), rlt);
            lt = LT_MULTIPLE;
        } else {
            exptype = type;
            lt = rlt;
        }
    }

    if (nf->IsAssert()) {
        // Special case, add to flow:
        tc.CheckFlowTypeChanges(true, children[0]);
        // Also make result non-nil, if it was.
        if (exptype->t == V_NIL) exptype = exptype->Element();
    }

    nattype = exptype;
    natlt = lt;
    return this;
}

Node *Call::TypeCheck(TypeChecker &tc, size_t reqret) {
    STACK_PROFILE;
    if (!children.empty() && children[0]->exptype->t == V_UNDEFINED) {
        // Not from GenericCall.
        tc.TypeCheckList(this, LT_ANY);
    }
    sf = tc.PreSpecializeFunction(sf);
    exptype = tc.TypeCheckCall(sf, *this, reqret, vtable_idx, &specializers, super);
    lt = sf->ltret;
    return this;
}

Node *FunRef::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    sf = tc.PreSpecializeFunction(sf);
    if (sf->parent->istype) {
        for (auto [i, arg] : enumerate(sf->args)) {
            arg.type = tc.st.ResolveTypeVars(sf->giventypes[i], this->line);
        }
        sf->returntype = tc.st.ResolveTypeVars(sf->returngiventype, this->line);
    }
    exptype = &sf->thistype;
    lt = LT_ANY;
    return this;
}

Node *DynCall::TypeCheck(TypeChecker &tc, size_t reqret) {
    return tc.TypeCheckDynCall(this, reqret);
}

Node *Return::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    exptype = type_void;
    lt = LT_ANY;
    // Ensure what we're returning from is going to be on the stack at runtime.
    // First fund correct specialization for sf.
    for (auto isc : reverse(tc.scopes)) {
        if (isc.sf->parent == sf->parent) {
            sf = isc.sf;
            break;
        }
    }
    // TODO: LT_KEEP here is to keep it simple for now, since ideally we want to also allow
    // LT_BORROW, but then we have to prove that we don't outlive the owner.
    // Additionally, we have to do this for reused specializations on new SpecIdents.
    auto reqlt = LT_KEEP;
    auto reqret = make_void ? 0 : sf->reqret;
    // Special (but very common) case: optimize lifetime for "return var" case, where var owns
    // and this is the only return statement. Without this we'd get an inc on the var that's
    // immediately undone as the scope ends.
    auto ir = Is<IdentRef>(child);
    if (ir) {
        tc.UpdateCurrentSid(ir->sid);  // Ahead of time, because ir not typechecked yet.
        if (ir->sid->lt == LT_KEEP &&
            IsRefNil(ir->sid->type->t) &&
            ir->sid->sf_def == sf &&
            sf->num_returns == 0 &&
            sf->returned_thru_to_max < 0 &&  // Since unwind path still needs this var in ownedvars.
            reqret &&
            sf->sbody->children.back() == this) {
            // NOTE: see also Call::Optimize where we potentially have to undo this when inlined.
            reqlt = LT_BORROW;  // Fake that we're cool borrowing this.
            sf->consumes_vars_on_return = true;  // Don't decref this one when going out of scope.
        }
    }
    tc.TT(child, reqret, reqlt);
    tc.DecBorrowers(child->lt, *this);
    if (sf == tc.st.toplevel) {
        // return from program
        if (child->exptype->NumValues() > 1)
            tc.Error(*this, "cannot return multiple values from top level");
    }
    auto never_returns = child->Terminal(tc);
    if (never_returns && make_void && sf->num_returns) {
        // A return with other returns inside of it that always bypass this return,
        // so should not contribute to return types.
        if (child->exptype->t == V_VAR) tc.UnifyVar(type_void, child->exptype, V_UNDEFINED);
        assert(child->exptype->t == V_VOID);
        // Call this for correct counting of number of returns, with existing type, should
        // have no effect on the type.
        tc.RetVal(sf->returntype, sf, *this);
        return this;
    }
    if (never_returns && sf->reqret && sf->parent->anonymous) {
        // A return to the immediately enclosing anonymous function that needs to return a value
        // but is bypassed.
        tc.RetVal(child->exptype, sf, *this);  // If it's a variable, bind it.
        return this;
    }
    if (make_void && sf->num_returns && sf->returntype->t != V_VOID) {
        // Specialized error to not have the code below complain that there is a non-existing
        // return statement returning void.
        // FIXME: need V_VOID check because num_returns can be 1 in functions with exp-less "return"??
        tc.Error(*this, "control reaches end of non-void function");
    }
    if (!Is<DefaultVal>(child)) {
        auto scchild = (Node *)tc.SkipCoercions(child);
        if (auto mrs = Is<MultipleReturn>(scchild)) {
            tc.RetVal(mrs->exptype, sf, *this);
            for (auto [i, mr] : enumerate(mrs->children)) {
                if (i < sf->reqret)
                    tc.SubType(mr, sf->returntype->Get(i), tc.ArgName(i), *this);
            }
        } else {
            tc.RetVal(child->exptype, sf, *this);
            tc.SubType(child, sf->returntype, "", *this);
        }
    } else {
        tc.RetVal(type_void, sf, *this);
        tc.SubType(child, sf->returntype, "", *this);
    }
    // Now we can check what we're returning past as well.
    // Do this last, since we want RetVal to have been called on sf.
    for (auto isc : reverse(tc.scopes)) {
        if (isc.sf->parent == sf->parent) { goto destination_found; }
        tc.CheckReturnPast(isc.sf, sf, *this);
    }
    tc.Error(*this, "return from ", Q(sf->parent->name), " called out of context");
    destination_found:
    return this;
}

Node *TypeAnnotation::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    exptype = tc.st.ResolveTypeVars(giventype, this->line);
    lt = LT_ANY;
    return this;
}

Node *IsType::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    tc.DecBorrowers(child->lt, *this);
    resolvedtype = tc.st.ResolveTypeVars(giventype, this->line);
    exptype = &tc.st.default_bool_type->thistype;
    lt = LT_ANY;
    // Check for constness early, to be able to lift out side effects, which
    // makes downstream if-then optimisations easier.
    Value cval = NilVal();
    auto t = ConstVal(&tc, cval);
    if (t == V_INT) {
        auto intc = (new IntConstant(line, cval.ival()))->TypeCheck(tc, 1);
        if (child->SideEffectRec()) {
            // must retain side effects.
            auto seq = new Seq(child->line, child, intc);
            seq->exptype = type_int;
            seq->lt = LT_ANY;
            child = nullptr;
            delete this;
            return seq;
        } else {
            delete this;
            return intc;
        }
    } else {
        // ConstVal should always be const for structs.
        assert(!IsStruct(resolvedtype->t));
    }
    return this;
}

Node *Constructor::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (giventype.Null()) {
        if (Arity()) {
            tc.TypeCheckList(this, LT_KEEP);
            // No type was specified.. first find union of all elements.
            TypeRef u(nullptr);
            for (auto c : children) {
                u = u.Null() ? c->exptype
                             : tc.Union(u, c->exptype, "constructor", "constructor element",
                                        CF_COERCIONS, c);
            }
            exptype = tc.st.Wrap(u, V_VECTOR, &line);
            tc.StorageType(exptype, *this);
        } else {
            // special case for empty vectors
            exptype = tc.st.Wrap(tc.st.NewTypeVar(), V_VECTOR, &line);
        }
    } else if (giventype->t == V_UUDT && giventype->spec_udt->specializers.empty()) {
        // Special case for generic type constructor with no specializers.
        // Versions WITH specializers are instead resolved below.
        tc.TypeCheckList(this, LT_KEEP);
        auto gudt = giventype->spec_udt->gudt;
        // Try and find a matching named specialization.
        if (Arity() != gudt->fields.size())
            tc.Error(*this, "incorrect argument count for generic constructor");
        // Now find a match:
        int bestmatch = 0;
        UDT *udt = nullptr;
        for (auto udti = gudt->first; udti; udti = udti->next) {
            if (udti->unnamed_specialization) continue;
            int nmatches = 0;
            for (auto [i, arg] : enumerate(children)) {
                auto &sfield = udti->sfields[i];
                if (tc.ConvertsTo(arg->exptype, sfield.type, CF_NONE)) nmatches++;
                else break;
            }
            if (nmatches > bestmatch) {
                bestmatch = nmatches;
                udt = udti;
            }
        }
        if (!udt) {
            string s;
            for (auto &arg : children) s += " " + TypeName(arg->exptype);
            auto err = "generic constructor matches no named explicit specialization of " +
                        Q(gudt->name) + " with types:" + s;
            for (auto udti = gudt->first; udti; udti = udti->next) {
                err += "\n  specialization: ";
                err += Signature(*udti);
            }
            tc.Error(*this, err);
        }
        exptype = &udt->thistype;
    } else {
        exptype = tc.st.ResolveTypeVars(giventype, this->line);
        auto udt = IsUDT(exptype->t) ? exptype->udt : nullptr;
        if (udt) {
            // Sadly, this causes more problems than it solves, since these UDTs may depend
            // on global vars not typechecked, etc.
            //tc.TypeCheckUDT(*udt, *this, true);
            tc.st.PushSuperGenerics(udt);
            // Fill in default args.. already done in the parser normally, but can happen if
            // this is a T {} constructor.
            for (size_t i = children.size(); i < udt->g.fields.size(); i++) {
                if (udt->g.fields[i].defaultval)
                    Add(udt->g.fields[i].defaultval->Clone());
                else
                    tc.Error(*this, "field ", Q(udt->g.fields[i].id->name), " not initialized");
            }
        }
        // These may include field initializers copied from the definition, which may include
        // type variables that are now bound.
        tc.TypeCheckList(this, LT_KEEP);
        if (udt) tc.st.PopSuperGenerics(udt);
    }
    if (IsUDT(exptype->t)) {
        auto udt = exptype->udt;
        // We have to check this here, since the parser couldn't check this yet.
        if (udt->sfields.size() < children.size())
            tc.Error(*this, "too many initializers for ", Q(udt->name));
        exptype = &udt->thistype;
        if (udt->g.has_constructor_function &&
            (tc.named_scopes.empty() ||
                tc.named_scopes.back().sf->parent->is_constructor_of != &udt->g)) {
            tc.Error(*this, Q(udt->name), " may only be constructed thru its constructor function");
        }
    }
    for (auto [i, c] : enumerate(children)) {
        TypeRef elemtype = IsUDT(exptype->t) ? exptype->udt->sfields[i].type
                                             : exptype->Element();
        tc.SubType(c, elemtype, tc.ArgName(i), *this);
    }
    lt = LT_KEEP;
    return this;
}

Node *Dot::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (child->exptype->t == V_UNDEFINED) {
        // Not from GenericCall.
        tc.TT(child, 1, LT_ANY);
    }
    tc.AdjustLifetime(child, LT_BORROW);
    tc.DecBorrowers(child->lt, *this);  // New borrow created below.
    auto stype = child->exptype;
    if (!IsUDT(stype->t))
        tc.RequiresError("class/struct", stype, *this, "object");
    auto udt = stype->udt;
    auto fieldidx = udt->g.Has(fld);
    if (fieldidx < 0)
        tc.Error(*this, "type ", Q(udt->name), " has no field ", Q(fld->name));
    auto &field = udt->g.fields[fieldidx];
    if (field.isprivate && line.fileidx != field.defined_in.fileidx)
        tc.Error(*this, "field ", Q(field.id->name), " is private");
    if (!field.in_scope) tc.Error(*this, "field ", Q(field.id->name), " is not in scope");
    exptype = udt->sfields[fieldidx].type;
    FlowItem fi(*this, exptype);
    if (fi.IsValid()) exptype = tc.UseFlow(fi);
    lt = tc.PushBorrow(this);
    //lt = children[0]->lt;  // Also LT_BORROW, also depending on the same variable.
    return this;
}

Node *Indexing::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    if (auto nn = tc.OperatorOverload(*this)) return nn;
    tc.TT(index, 1, LT_BORROW);
    tc.DecBorrowers(index->lt, *this);
    auto vtype = object->exptype;
    if (vtype->t != V_VECTOR &&
        vtype->t != V_STRING &&
        (!IsStruct(vtype->t) || !vtype->udt->sametype->Numeric()))
        tc.RequiresError("vector/string/numeric struct", vtype, *this, "container");
    auto itype = index->exptype;
    switch (itype->t) {
        case V_INT:
            exptype = vtype->t == V_VECTOR
                ? vtype->Element()
                : (IsUDT(vtype->t) ? vtype->udt->sametype : type_int);
            break;
        case V_STRUCT_S: {
            if (vtype->t != V_VECTOR)
                tc.Error(*this, "multi-dimensional indexing on non-vector");
            auto &udt = *itype->udt;
            exptype = vtype;
            for (auto &field : udt.sfields) {
                if (field.type->t != V_INT)
                    tc.RequiresError("int field", field.type, *this, "index");
                if (exptype->t != V_VECTOR)
                    tc.RequiresError("nested vector", exptype, *this, "container");
                exptype = exptype->Element();
            }
            break;
        }
        default: tc.RequiresError("int/struct of int", itype, *this, "index");
    }
    lt = object->lt;  // Also LT_BORROW, also depending on the same variable.
    return this;
}

Node *Seq::TypeCheck(TypeChecker &tc, size_t reqret) {
    tc.TT(head, 0, LT_ANY);
    tc.TT(tail, reqret, LT_ANY);
    exptype = tail->exptype;
    lt = tail->lt;
    return this;
}

Node *TypeOf::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    tc.DecBorrowers(child->lt, *this);
    auto ti = tc.st.NewType();
    exptype = child->exptype->Wrap(ti, V_TYPEID);
    lt = LT_ANY;
    return this;
}

Node *EnumCoercion::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    tc.SubType(child, type_int, "coerced value", *this);
    tc.DecBorrowers(child->lt, *this);
    exptype = &e->thistype;
    lt = LT_ANY;
    return this;
}

Node *MultipleReturn::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckList(this, LT_ANY);
    exptype = tc.st.NewTuple(children.size());
    for (auto [i, mrc] : enumerate(children))
        exptype->Set(i, mrc->exptype.get(), mrc->lt);
    lt = LT_MULTIPLE;
    return this;
}

Node *EnumRef::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    return this;
}

Node *GUDTRef::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckGUDT(*gudt, *this, predeclaration);
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *UDTRef::TypeCheck(TypeChecker &, size_t /*reqret*/) {
    // For now, being type-checked from TypeCheckGUDT.
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *Coercion::TypeCheck(TypeChecker &tc, size_t reqret) {
    assert(false);  // Should not be called, since only inserted by TT.
    tc.TT(child, reqret, LT_ANY);
    return this;
}

bool Return::Terminal(TypeChecker &) const {
    return true;
}

bool Block::Terminal(TypeChecker &tc) const {
    return children.back()->Terminal(tc);
}

bool IfElse::Terminal(TypeChecker &tc) const {
    return truepart->Terminal(tc) && falsepart->Terminal(tc);
}

bool While::Terminal(TypeChecker &tc) const {
    // NOTE: if wbody is terminal, that does not entail the loop is, since
    // condition may be false on first iteration.
    // Instead, it is only terminal if this is an infinite loop.
    Value val = NilVal();
    return condition->ConstVal(&tc, val) != V_VOID && val.True();
}

bool Break::Terminal(TypeChecker &) const {
    return true;
}

bool Switch::Terminal(TypeChecker &tc) const {
    auto have_default = false;
    for (auto c : cases->children) {
        auto cas = AssertIs<Case>(c);
        if (cas->pattern->children.empty()) have_default = true;
        if (!cas->cbody->Terminal(tc)) return false;
    }
    // FIXME: this should return true if the switch is proven exhaustive for types, sadly cannot guarantee that with enums.
    return have_default;
}

bool NativeCall::Terminal(TypeChecker &) const {
    // A function may end in "assert false" and have only its previous return statements
    // taken into account.
    if (nf->IsAssert()) {
        auto i = Is<IntConstant>(children[0]);
        return i && !i->integer;
    }
    return false;
}

bool Call::Terminal(TypeChecker &tc) const {
    // Have to be conservative for recursive calls since we're not done typechecking it.
    if (sf->isrecursivelycalled ||
        sf->method_of ||
        sf->parent->istype) return false;
    if (!sf->num_returns) return true;  // The minimum 1 return is apparently returning out of it.
    if (sf->num_returns == 1) {
        auto ret = AssertIs<Return>(sf->sbody->children.back());
        if (ret->sf == sf) {
            return ret->child->Terminal(tc);
        } else {
            // This is a "return from", which means the real return is elsewhere, and thus
            // this call may not be terminal.
        }
    }
    // TODO: could also check num_returns > 1, but then have to scan all children.
    return false;
}

}  // namespace lobster
