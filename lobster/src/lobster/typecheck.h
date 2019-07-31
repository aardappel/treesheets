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
    vector<SharedField *> derefs;
    LValContext(const Node &n) {
        auto t = &n;
        while (auto dot = Is<Dot>(t)) {
            derefs.insert(derefs.begin(), dot->fld);
            t = dot->children[0];
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
    bool IsPrefix(const LValContext &o) {
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
};

struct Borrow : LValContext {
    int refc = 1;  // Number of outstanding borrowed values. While >0 can't assign.
    Borrow(const Node &n) : LValContext(n) {}
};

struct TypeChecker {
    Parser &parser;
    SymbolTable &st;
    struct Scope { SubFunction *sf; const Node *call_context; };
    vector<Scope> scopes, named_scopes;
    vector<FlowItem> flowstack;
    vector<Borrow> borrowstack;

    TypeChecker(Parser &_p, SymbolTable &_st, size_t retreq) : parser(_p), st(_st) {
        // FIXME: this is unfriendly.
        if (!st.RegisterDefaultTypes())
            TypeError("cannot find standard types (from stdtype.lobster)", *parser.root);
        for (auto &udt : st.udttable) {
            if (udt->generics.empty()) {
                // NOTE: all users of sametype will only act on it if it is numeric, since
                // otherwise it would a scalar field to become any without boxing.
                // Much of the implementation relies on these being 2-4 component vectors, so
                // deny this functionality to any other structs.
                if (udt->fields.size() >= 2 && udt->fields.size() <= 4) {
                    udt->sametype = udt->fields.v[0].type;
                    for (size_t i = 1; i < udt->fields.size(); i++) {
                        // Can't use Union here since it will bind variables, use simplified alternative:
                        if (!ExactType(udt->fields.v[i].type, udt->sametype)) {
                            udt->sametype = type_undefined;
                            break;
                        }
                    }
                }
                // Update the type to the correct struct type.
                if (udt->is_struct) {
                    for (auto &field : udt->fields.v) {
                        if (IsRefNil(field.type->t)) {
                            udt->hasref = true;
                            break;
                        }
                    }
                    const_cast<ValueType &>(udt->thistype.t) =
                        udt->hasref ? V_STRUCT_R : V_STRUCT_S;
                }
            }
            if (udt->superclass) {
                // If this type has fields inherited from the superclass that refer to the
                // superclass, make it refer to this type instead. There may be corner cases where
                // this is not what you want, but generally you do.
                for (auto &field : make_span(udt->fields.v.data(),
                                             udt->superclass->fields.v.size())) {
                    PromoteStructIdx(field.type, udt->superclass, udt);
                }
            }
            for (auto u = udt; u; u = u->superclass) u->subudts.push_back(udt);
        }
        AssertIs<Call>(parser.root)->sf->reqret = retreq;
        TT(parser.root, retreq, LT_KEEP);
        AssertIs<Call>(parser.root);
        CleanUpFlow(0);
        assert(borrowstack.empty());
        assert(scopes.empty());
        assert(named_scopes.empty());
        Stats();
    }

    // Needed for any sids in cloned code.
    void UpdateCurrentSid(SpecIdent *&sid) { sid = sid->Current(); }
    void RevertCurrentSid(SpecIdent *&sid) { sid->Current() = sid; }

    void PromoteStructIdx(TypeRef &type, const UDT *olds, const UDT *news) {
        auto u = type;
        while (u->Wrapped()) u = u->Element();
        if (IsUDT(u->t) && u->udt == olds) type = PromoteStructIdxRec(type, news);
    }

    TypeRef PromoteStructIdxRec(TypeRef type, const UDT *news) {
        return type->Wrapped()
            ? st.Wrap(PromoteStructIdxRec(type->sub, news), type->t)
            : &news->thistype;
    }

    string TypedArg(const GenericArgs &args, size_t i, bool withtype = true) {
        string s;
        s += args.GetName(i);
        if (args.GetType(i)->t != V_ANY && withtype)
            s += ":" + TypeName(args.GetType(i));
        return s;
    }

    string Signature(const GenericArgs &args, bool withtype = true) {
        string s = "(";
        for (size_t i = 0; i < args.size(); i++) {
            if (i) s += ", ";
            s += TypedArg(args, i, withtype);
        }
        return s + ")";
    }

    string Signature(const UDT &udt) {
        return udt.name + Signature(udt.fields);
    }
    string Signature(const SubFunction &sf, bool withtype = true) {
        return sf.parent->name + Signature(sf.args, withtype);
    }
    string Signature(const NativeFun &nf) {
        return nf.name + Signature(nf.args);
    }

    string SignatureWithFreeVars(const SubFunction &sf, set<Ident *> *already_seen,
                                 bool withtype = true) {
        string s = Signature(sf, withtype) + " { ";
        for (auto [i, freevar] : enumerate(sf.freevars.v)) {
            if (freevar.type->t != V_FUNCTION &&
                !freevar.sid->id->static_constant &&
                (!already_seen || already_seen->find(freevar.sid->id) == already_seen->end())) {
                s += TypedArg(sf.freevars, i) + " ";
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

    void TypeError(string_view required, TypeRef got, const Node &n, string_view argname = "",
                   string_view context = "") {
        TypeError(cat("\"", (context.size() ? context : NiceName(n)), "\" ",
                      (argname.size() ? "(" + argname + " argument) " : ""),
                      "requires type: ", required, ", got: ", TypeName(got)), n);
    }

    void TypeError(string err, const Node &n) {
        set<Ident *> already_seen;
        if (!scopes.empty())
        for (auto scope : reverse(scopes)) {
            if (scope.sf == st.toplevel) continue;
            err += "\n  in " + parser.lex.Location(scope.call_context->line) + ": ";
            err += SignatureWithFreeVars(*scope.sf, &already_seen);
            for (auto dl : scope.sf->body->children) {
                if (auto def = Is<Define>(dl)) {
                    for (auto p : def->sids) {
                        err += ", " + p.first->id->name + ":" + TypeName(p.first->type);
                    }
                }
            }
        }
        parser.Error(err, &n);
    }

    void NoStruct(const Node &n, string_view context) {
        if (IsStruct(n.exptype->t)) TypeError("struct value cannot be used in: " + context, n);
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
        TypeError(err, callnode);
    }

    TypeRef NewTypeVar() {
        auto var = st.NewType();
        *var = Type(V_VAR);
        // Vars store a cycle of all vars its been unified with, starting with itself.
        var->sub = var;
        return var;
    }

    TypeRef NewNilTypeVar() {
        auto nil = st.NewType();
        *nil = Type(V_NIL);
        nil->sub = &*NewTypeVar();
        return nil;
    }

    TypeRef NewTuple(size_t sz) {
        auto type = st.NewType();
        *type = Type(V_TUPLE);
        type->tup = new vector<Type::TupleElem>(sz);
        st.tuplelist.push_back(type->tup);
        return type;
    }

    void UnifyVar(TypeRef type, TypeRef hasvar) {
        // Typically Type is const, but this is the one place we overwrite them.
        // Type objects that are V_VAR are seperate heap instances, so overwriting them has no
        // side-effects on non-V_VAR Type instances.
        assert(hasvar->t == V_VAR);
        if (type->t == V_VAR) {
            // Combine two cyclic linked lists.. elegant!
            swap((Type *&)hasvar->sub, (Type *&)type->sub);
        } else {
            auto v = hasvar;
            do { // Loop thru all vars in unification cycle.
                auto next = v->sub;
                *(Type *)&*v = *type;  // Overwrite Type struct!
                v = next;
            } while (&*v != &*hasvar);  // Force TypeRef pointer comparison.
        }
    }

    bool ConvertsTo(TypeRef type, TypeRef sub, bool coercions, bool unifications = true) {
        if (sub == type) return true;
        if (type->t == V_VAR) {
            if (unifications) UnifyVar(sub, type);
            return true;
        }
        switch (sub->t) {
            case V_VOID:      return coercions;
            case V_VAR:       UnifyVar(type, sub); return true;
            case V_FLOAT:     return type->t == V_INT && coercions;
            case V_INT:       return (type->t == V_TYPEID && coercions) ||
                                     (type->t == V_INT && !sub->e);
            case V_STRING:    return coercions && IsRuntimePrintable(type->t);
            case V_FUNCTION:  return type->t == V_FUNCTION && !sub->sf;
            case V_NIL:       return (type->t == V_NIL &&
                                      ConvertsTo(type->Element(), sub->Element(), false,
                                                 unifications)) ||
                                     (!type->Numeric() && type->t != V_VOID && !IsStruct(type->t) &&
                                      ConvertsTo(type, sub->Element(), false, unifications)) ||
                                     (type->Numeric() &&  // For builtins.
                                      ConvertsTo(type, sub->Element(), false, unifications));
            case V_VECTOR:    return (type->t == V_VECTOR &&
                                      ConvertsTo(type->Element(), sub->Element(), false,
                                                 unifications));
            case V_CLASS:     return type->t == V_CLASS &&
                                     st.SuperDistance(sub->udt, type->udt) >= 0;
            case V_STRUCT_R:
            case V_STRUCT_S:  return type->t == sub->t && type->udt == sub->udt;
            case V_COROUTINE: return type->t == V_COROUTINE &&
                                     (sub->sf == type->sf ||
                                      (!sub->sf && type->sf && ConvertsTo(type->sf->coresumetype,
                                                              NewNilTypeVar(), false)));
            case V_TUPLE:     return type->t == V_TUPLE && ConvertsToTuple(*type->tup, *sub->tup);
            default:          return false;
        }
    }

    bool ConvertsToTuple(const vector<Type::TupleElem> &ttup, const vector<Type::TupleElem> &stup) {
        if (ttup.size() != stup.size()) return false;
        for (auto [i, te] : enumerate(ttup))
            if (!ConvertsTo(te.type, stup[i].type, false))
                return false;
        return true;
    }

    TypeRef Union(TypeRef at, TypeRef bt, bool coercions, const Node *err) {
        if (ConvertsTo(at, bt, coercions)) return bt;
        if (ConvertsTo(bt, at, coercions)) return at;
        if (at->t == V_VECTOR && bt->t == V_VECTOR) {
            auto et = Union(at->Element(), bt->Element(), false, err);
            return st.Wrap(et, V_VECTOR);
        }
        if (at->t == V_CLASS && bt->t == V_CLASS) {
            auto sstruc = st.CommonSuperType(at->udt, bt->udt);
            if (sstruc) return &sstruc->thistype;
        }
        if (err)
            TypeError(cat(TypeName(at), " and ", TypeName(bt), " have no common supertype"), *err);
        return type_undefined;
    }

    bool ExactType(TypeRef a, TypeRef b) {
        return a == b;  // Not inlined for documentation purposes.
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

    void StorageType(TypeRef type, const Node &context) {
        if (type->HasValueType(V_VOID)) TypeError("cannot store value of type void", context);
    }

    void SubTypeLR(TypeRef sub, BinOp &n) {
        SubType(n.left, sub, "left", n);
        SubType(n.right, sub, "right", n);
    }

    void SubType(Node *&a, TypeRef sub, string_view argname, const Node &context) {
        SubType(a, sub, argname, NiceName(context));
    }
    void SubType(Node *&a, TypeRef sub, string_view argname, string_view context) {
        if (ConvertsTo(a->exptype, sub, false)) return;
        switch (sub->t) {
            case V_FLOAT:
                if (a->exptype->t == V_INT) {
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
            case V_FUNCTION:
                if (a->exptype->IsFunction() && sub->sf) {
                    // See if these functions can be made compatible. Specialize and typecheck if
                    // needed.
                    auto sf = a->exptype->sf;
                    auto ss = sub->sf;
                    if (!ss->parent->istype)
                        TypeError("dynamic function value can only be passed to declared "
                                  "function type", *a);
                    if (sf->args.v.size() != ss->args.v.size()) break;
                    for (auto [i, arg] : enumerate(sf->args.v)) {
                        // Specialize to the function type, if requested.
                        if (!sf->typechecked && arg.flags & AF_GENERIC) {
                            arg.type = ss->args.v[i].type;
                        }
                        // Note this has the args in reverse: function args are contravariant.
                        if (!ConvertsTo(ss->args.v[i].type, arg.type, false))
                            goto error;
                        // This function must be compatible with all other function values that
                        // match this type, so we fix lifetimes to LT_BORROW.
                        // See typechecking of istype calls.
                        arg.sid->lt = LT_BORROW;
                    }
                    if (sf->typechecked) {
                        if (sf->reqret != ss->reqret) goto error;
                    } else {
                        sf->reqret = ss->reqret;
                    }
                    sf->isdynamicfunctionvalue = true;
                    TypeCheckFunctionDef(*sf, *sf->body);
                    // Covariant again.
                    if (sf->returntype->NumValues() != ss->returntype->NumValues() ||
                        !ConvertsTo(sf->returntype, ss->returntype, false))
                            break;
                    // Parser only parses one ret type for function types.
                    assert(ss->returntype->NumValues() <= 1);
                    return;
                }
                break;
            default:
                ;
        }
        error:
        TypeError(TypeName(sub), a->exptype, *a, argname, context);
    }

    void SubTypeT(TypeRef type, TypeRef sub, const Node &n, string_view argname,
                  string_view context = {}) {
        if (!ConvertsTo(type, sub, false))
            TypeError(TypeName(sub), type, n, argname, context);
    }

    bool MathCheckVector(TypeRef &type, Node *&left, Node *&right) {
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
        return false;
    }

    const char *MathCheck(TypeRef &type, BinOp &n, bool &unionchecked,
                          bool typechangeallowed) {
        if (Is<Mod>(&n) || Is<ModEq>(&n)) {
            if (type->t != V_INT) return "int";
        } else {
            if (!type->Numeric() && type->t != V_VECTOR && !IsUDT(type->t)) {
                if (MathCheckVector(type, n.left, n.right)) {
                    unionchecked = true;
                    return nullptr;
                }
                if (Is<Plus>(&n) || Is<PlusEq>(&n)) {
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
                    } else if (rtype->t == V_STRING && ltype->t != V_STRING && typechangeallowed) {
                        // Only if not in a +=
                        MakeString(n.left, LT_BORROW);
                        type = type_string;
                        unionchecked = true;
                    } else {
                        return "numeric/string/vector/struct";
                    }
                } else {
                    return "numeric/vector/struct";
                }
            }
        }
        return nullptr;
    }

    void MathError(TypeRef &type, BinOp &n, bool &unionchecked, bool typechangeallowed) {
        auto err = MathCheck(type, n, unionchecked, typechangeallowed);
        if (err) {
            if (MathCheck(n.left->exptype, n, unionchecked, typechangeallowed))
                TypeError(err, n.left->exptype, n, "left");
            if (MathCheck(n.right->exptype, n, unionchecked, typechangeallowed))
                TypeError(err, n.right->exptype, n, "right");
            TypeError("can\'t use \"" +
                      NiceName(n) +
                      "\" on " +
                      TypeName(n.left->exptype) +
                      " and " +
                      TypeName(n.right->exptype), n);
        }
    }

    void TypeCheckMathOp(BinOp &n) {
        TT(n.left, 1, LT_BORROW);
        TT(n.right, 1, LT_BORROW);
        n.exptype = Union(n.left->exptype, n.right->exptype, true, nullptr);
        bool unionchecked = false;
        MathError(n.exptype, n, unionchecked, true);
        if (!unionchecked) SubTypeLR(n.exptype, n);
        DecBorrowers(n.left->lt, n);
        DecBorrowers(n.right->lt, n);
        n.lt = LT_KEEP;
    }

    void TypeCheckMathOpEq(BinOp &n) {
        TT(n.left, 1, LT_BORROW);
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
    }

    void TypeCheckComp(BinOp &n) {
        TT(n.left, 1, LT_BORROW);
        TT(n.right, 1, LT_BORROW);
        n.exptype = &st.default_bool_type->thistype;
        auto u = Union(n.left->exptype, n.right->exptype, true, nullptr);
        if (!u->Numeric() && u->t != V_STRING) {
            if (Is<Equal>(&n) || Is<NotEqual>(&n)) {
                // Comparison with one result, but still by value for structs.
                if (u->t != V_VECTOR && !IsUDT(u->t) && u->t != V_NIL && u->t != V_FUNCTION)
                    TypeError(TypeName(n.left->exptype), n.right->exptype, n, "right-hand side");
            } else {
                // Comparison vector op: vector inputs, vector out.
                if (u->t == V_STRUCT_S && u->udt->sametype->Numeric()) {
                    n.exptype = st.default_int_vector_types[0][u->udt->fields.size()];
                } else if (MathCheckVector(n.exptype, n.left, n.right)) {
                    n.exptype = st.default_int_vector_types[0][n.exptype->udt->fields.size()];
                    // Don't do SubTypeLR since type already verified and `u` not
                    // appropriate anyway.
                    goto out;
                } else {
                    TypeError(n.Name() + " doesn\'t work on " + TypeName(n.left->exptype) +
                              " and " + TypeName(n.right->exptype), n);
                }
            }
        }
        SubTypeLR(u, n);
        out:
        DecBorrowers(n.left->lt, n);
        DecBorrowers(n.right->lt, n);
        n.lt = LT_KEEP;
    }

    void TypeCheckBitOp(BinOp &n) {
        TT(n.left, 1, LT_BORROW);
        TT(n.right, 1, LT_BORROW);
        auto u = Union(n.left->exptype, n.right->exptype, true, nullptr);
        if (u->t != V_INT) u = type_int;
        SubTypeLR(u, n);
        n.exptype = u;
        DecBorrowers(n.left->lt, n);
        DecBorrowers(n.right->lt, n);
        n.lt = LT_ANY;
    }

    void TypeCheckPlusPlus(Unary &n) {
        TT(n.child, 1, LT_BORROW);
        CheckLval(n.child);
        n.exptype = n.child->exptype;
        if (!n.exptype->Numeric())
            TypeError("numeric", n.exptype, n);
        n.lt = n.child->lt;
    }

    SubFunction *TopScope(vector<Scope> &_scopes) {
        return _scopes.empty() ? nullptr : _scopes.back().sf;
    }

    void RetVal(TypeRef type, SubFunction *sf, const Node &err, bool register_return = true) {
        if (register_return) {
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
                isc.sf->reuse_return_events.push_back({ sf, type });
            }
        }
        sf->num_returns++;
        if (sf->fixedreturntype.Null()) {
            if (sf->reqret) {
                // If this is a recursive call we must be conservative because there may already
                // be callers dependent on the return type so far, so any others must be subtypes.
                if (!sf->isrecursivelycalled) {
                    // We can safely generalize the type if needed, though not with coercions.
                    sf->returntype = Union(type, sf->returntype, false, &err);
                }
            } else {
                // The caller doesn't want return values.
                sf->returntype = type_void;
            }
        }
    }

    void TypeCheckFunctionDef(SubFunction &sf, const Node &call_context) {
        if (sf.typechecked) return;
        LOG_DEBUG("function start: ", SignatureWithFreeVars(sf, nullptr));
        Scope scope;
        scope.sf = &sf;
        scope.call_context = &call_context;
        scopes.push_back(scope);
        //for (auto &ns : named_scopes) LOG_DEBUG("named scope: ", ns.sf->parent->name);
        if (!sf.parent->anonymous) named_scopes.push_back(scope);
        sf.typechecked = true;
        for (auto &arg : sf.args.v) StorageType(arg.type, call_context);
        for (auto &fv : sf.freevars.v) UpdateCurrentSid(fv.sid);
        auto backup_vars = [&](ArgVector &in, ArgVector &backup) {
            for (auto [i, arg] : enumerate(in.v)) {
                // Need to not overwrite nested/recursive calls. e.g. map(): map(): ..
                backup.v[i].sid = arg.sid->Current();
                arg.sid->type = arg.type;
                RevertCurrentSid(arg.sid);
            }
        };
        auto backup_args = sf.args; backup_vars(sf.args, backup_args);
        auto backup_locals = sf.locals; backup_vars(sf.locals, backup_locals);
        auto enter_scope = [&](const Arg &var) {
            IncBorrowers(var.sid->lt, call_context);
        };
        for (auto &arg : sf.args.v) enter_scope(arg);
        for (auto &local : sf.locals.v) enter_scope(local);
        sf.coresumetype = sf.iscoroutine ? NewNilTypeVar() : type_undefined;
        sf.returntype = sf.reqret
            ? (!sf.fixedreturntype.Null() ? sf.fixedreturntype : NewTypeVar())
            : type_void;
        auto start_borrowed_vars = borrowstack.size();
        auto start_promoted_vars = flowstack.size();
        TypeCheckList(sf.body, true, 0, LT_ANY);
        CleanUpFlow(start_promoted_vars);
        if (!sf.num_returns) {
            if (!sf.fixedreturntype.Null() && sf.fixedreturntype->t != V_VOID)
                TypeError("missing return statement", *sf.body->children.back());
            sf.returntype = type_void;
        }
        // Let variables go out of scope in reverse order of declaration.
        auto exit_scope = [&](const Arg &var) {
            DecBorrowers(var.sid->lt, call_context);
        };
        for (auto &local : reverse(sf.locals.v)) exit_scope(local);
        for (auto &arg : sf.args.v) exit_scope(arg);  // No order.
        while (borrowstack.size() > start_borrowed_vars) {
            auto &b = borrowstack.back();
            if (b.refc) {
                TypeError(cat("variable ", b.Name(), " still has ", b.refc,
                              " borrowers"), *sf.body->children.back());
            }
            borrowstack.pop_back();
        }
        for (auto &back : backup_args.v)   RevertCurrentSid(back.sid);
        for (auto &back : backup_locals.v) RevertCurrentSid(back.sid);
        if (sf.returntype->HasValueType(V_VAR)) {
            // If this function return something with a variable in it, then it likely will get
            // bound by the caller. If the function then gets reused without specialization, it will
            // get the wrong return type, so we force specialization for subsequent calls of this
            // function. FIXME: check in which cases this is typically true, since its expensive
            // if done without reason.
            sf.mustspecialize = true;
        }
        if (!sf.parent->anonymous) named_scopes.pop_back();
        scopes.pop_back();
        LOG_DEBUG("function end ", Signature(sf), " returns ",
                             TypeName(sf.returntype));
    }

    UDT *FindStructSpecialization(UDT *given, const Constructor *cons) {
        // This code is somewhat similar to function specialization, but not similar enough to
        // share. If they're all typed, we bail out early:
        if (given->generics.empty()) return given;
        auto head = given->first;
        assert(cons->Arity() == head->fields.size());
        // Now find a match:
        UDT *best = nullptr;
        int bestmatch = 0;
        for (auto udt = head->next; udt; udt = udt->next) {
            int nmatches = 0;
            for (auto [i, arg] : enumerate(cons->children)) {
                auto &field = udt->fields.v[i];
                if (field.genericref >= 0) {
                    if (ExactType(arg->exptype, field.type)) nmatches++;
                    else break;
                }
            }
            if (nmatches > bestmatch) {
                bestmatch = nmatches;
                best = udt;
            }
        }
        if (best) return best;
        string s;
        for (auto &arg : cons->children) s += " " + TypeName(arg->exptype);
        TypeError("no specialization of " + given->first->name + " matches these types:" + s,
                  *cons);
        return nullptr;
    }

    void CheckIfSpecialization(UDT *spec_struc, TypeRef given, const Node &n,
                               string_view argname, string_view req = {},
                               bool subtypeok = false, string_view context = {}) {
        auto givenu = given->UnWrapped();
        if (!IsUDT(given->t) ||
            (!spec_struc->IsSpecialization(givenu->udt) &&
             (!subtypeok || st.SuperDistance(spec_struc, givenu->udt) < 0))) {
            TypeError(req.data() ? req : spec_struc->name, given, n, argname, context);
        }
    }

    void CheckGenericArg(TypeRef otype, TypeRef argtype, string_view argname, const Node &n,
                         string_view context) {
        // Check if argument is a generic struct type, or wrapped in vector/nilable.
        if (otype->t != V_ANY) {
            auto u = otype->UnWrapped();
            assert(IsUDT(u->t));
            if (otype->EqNoIndex(*argtype)) {
                CheckIfSpecialization(u->udt, argtype, n, argname, TypeName(otype), true,
                                      context);
            } else {
                // This likely generates either an error, or contains an unbound var that will get
                // bound.
                SubTypeT(argtype, otype, n, argname, context);
                //TypeError(TypeName(otype), argtype, n, argname, context);
            }
        }
    }

    bool FreeVarsSameAsCurrent(const SubFunction &sf, bool prespecialize) {
        for (auto &freevar : sf.freevars.v) {
            //auto atype = Promote(freevar.id->type);
            if (freevar.sid != freevar.sid->Current() ||
                !ExactType(freevar.type, freevar.sid->Current()->type)) {
                (void)prespecialize;
                assert(prespecialize ||
                       freevar.sid == freevar.sid->Current() ||
                       (freevar.sid && freevar.sid->Current()));
                return false;
            }
            //if (atype->t == V_FUNCTION) return false;
        }
        return true;
    }

    SubFunction *CloneFunction(SubFunction *csf, int i) {
        LOG_DEBUG("cloning: ", csf->parent->name);
        auto sf = st.CreateSubFunction();
        sf->SetParent(*csf->parent, csf->parent->overloads[i]);
        // Any changes here make sure this corresponds what happens in Inline() in the optimizer.
        st.CloneIds(*sf, *csf);
        sf->body = (List *)csf->body->Clone();
        sf->freevarchecked = true;
        sf->fixedreturntype = csf->fixedreturntype;
        sf->returntype = csf->returntype;
        sf->logvarcallgraph = csf->logvarcallgraph;
        sf->method_of = csf->method_of;
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

    void CheckReturnPast(const SubFunction *sf, const SubFunction *sf_to, const Node &context) {
        // Special case for returning out of top level, which is always allowed.
        if (sf_to == st.toplevel) return;
        if (sf->iscoroutine) {
            TypeError("cannot return out of coroutine", context);
        }
        if (sf->isdynamicfunctionvalue) {
            // This is because the function has been typechecked against one context, but
            // can be called again in a different context that does not have the same callers.
            TypeError("cannot return out of dynamic function value", context);
        }
    }

    TypeRef TypeCheckCall(SubFunction *csf, List *call_args, SubFunction *&chosen,
                          const Node &call_context, size_t reqret, int &vtable_idx) {
        Function &f = *csf->parent;
        UDT *dispatch_udt = nullptr;
        vtable_idx = -1;

        auto TypeCheckMatchingCall = [&](SubFunction *sf, bool static_dispatch, bool first_dynamic) {
            // Here we have a SubFunction witch matching specialized types.
            sf->numcallers++;
            Function &f = *sf->parent;
            if (!f.istype) TypeCheckFunctionDef(*sf, call_context);
            // Finally check all the manually typed args. We do this after checking the function
            // definition, since SubType below can cause specializations of the current function
            // to be typechecked with strongly typed function value arguments.
            for (auto [i, c] : enumerate(call_args->children)) {
                if (i < f.nargs()) /* see below */ {
                    auto &arg = sf->args.v[i];
                    if (static_dispatch || first_dynamic) {
                        // Check a dynamic dispatch only for the first case, and then skip
                        // checking the first arg.
                        if (!(arg.flags & AF_GENERIC) && (static_dispatch || i))
                            SubType(c, arg.type, ArgName(i), f.name);
                        AdjustLifetime(c, arg.sid->lt);
                    }
                }
                // This has to happen even to dead args:
                if (static_dispatch || first_dynamic) DecBorrowers(c->lt, call_context);
            }
            chosen = sf;
            for (auto &freevar : sf->freevars.v) {
                // New freevars may have been added during the function def typecheck above.
                // In case their types differ from the flow-sensitive value at the callsite (here),
                // we want to override them.
                freevar.type = freevar.sid->Current()->type;
            }
            // See if this call is recursive:
            for (auto &sc : scopes) if (sc.sf == sf) { sf->isrecursivelycalled = true; break; }
            return sf->returntype;
        };

        auto SpecializationIsCompatible = [&](const SubFunction &sf) {
            return reqret == sf.reqret &&
                FreeVarsSameAsCurrent(sf, false) &&
                CompatibleReturns(sf);
        };

        auto ReplayReturns = [&](const SubFunction *sf) {
            // Apply effects of return statements for functions being reused, see
            // RetVal above.
            for (auto [isf, type] : sf->reuse_return_events) {
                for (auto isc : reverse(scopes)) {
                    if (isc.sf->parent == isf->parent) {
                        // NOTE: will have to re-apply lifetimes as well if we change
                        // from default of LT_KEEP.
                        RetVal(type, isc.sf, call_context, false);
                        // This should in theory not cause an error, since the previous
                        // specialization was also ok with this set of return types.
                        // It could happen though if this specialization has an
                        // additional return statement that was optimized
                        // out in the previous one.
                        SubTypeT(type, isc.sf->returntype, call_context, "",
                            "reused return value");
                        break;
                    }
                    CheckReturnPast(isc.sf, isf, call_context);
                }
            }
        };

        auto TypeCheckCallStatic = [&](int overload_idx, bool static_dispatch, bool first_dynamic) {
            Function &f = *csf->parent;
            SubFunction *sf = f.overloads[overload_idx];
            if (sf->logvarcallgraph) {
                // Mark call-graph up to here as using logvars, if it hasn't been already.
                for (auto sc : reverse(scopes)) {
                    if (sc.sf->logvarcallgraph) break;
                    sc.sf->logvarcallgraph = true;
                }
            }
            // Check if we need to specialize: generic args, free vars and need of retval
            // must match previous calls.
            auto AllowAnyLifetime = [&](const Arg & arg) {
                return arg.sid->id->single_assignment && !sf->iscoroutine;
            };
            // Check if any existing specializations match.
            for (sf = f.overloads[overload_idx]; sf; sf = sf->next) {
                if (sf->typechecked && !sf->mustspecialize && !sf->logvarcallgraph) {
                    // We check against f.nargs because HOFs are allowed to call a function
                    // value with more arguments than it needs (if we're called from
                    // TypeCheckDynCall). Optimizer always removes these.
                    // Note: we compare only lt, since calling with other borrowed sid
                    // should be ok to reuse.
                    for (auto [i, c] : enumerate(call_args->children)) if (i < f.nargs()) {
                        auto &arg = sf->args.v[i];
                        if ((arg.flags & AF_GENERIC && !ExactType(c->exptype, arg.type)) ||
                            (IsBorrow(c->lt) != IsBorrow(arg.sid->lt) &&
                                AllowAnyLifetime(arg))) goto fail;
                    }
                    if (SpecializationIsCompatible(*sf)) {
                        // This function can be reused.
                        // Make sure to add any freevars this call caused to be
                        // added to its parents also to the current parents, just in case
                        // they're different.
                        LOG_DEBUG("re-using: ", Signature(*sf));
                        for (auto &fv : sf->freevars.v) CheckFreeVariable(*fv.sid);
                        ReplayReturns(sf);
                        return TypeCheckMatchingCall(sf, static_dispatch, first_dynamic);
                    }
                    fail:;
                }
            }
            // No match.
            sf = f.overloads[overload_idx];
            // Specialize existing function, or its clone.
            if (sf->typechecked) sf = CloneFunction(sf, overload_idx);
            // Now specialize.
            sf->reqret = reqret;
            // See if this is going to be a coroutine.
            for (auto [i, c] : enumerate(call_args->children)) if (i < f.nargs()) /* see above */ {
                if (Is<CoClosure>(c))
                    sf->iscoroutine = true;
            }
            for (auto [i, c] : enumerate(call_args->children)) if (i < f.nargs()) /* see above */ {
                auto &arg = sf->args.v[i];
                arg.sid->lt = AllowAnyLifetime(arg) ? c->lt : LT_KEEP;
                if (arg.flags & AF_GENERIC) {
                    arg.type = c->exptype;  // Specialized to arg.
                    CheckGenericArg(f.orig_args.v[i].type, arg.type, arg.sid->id->name,
                        *c, f.name);
                    LOG_DEBUG("arg: ", arg.sid->id->name, ":", TypeName(arg.type));
                }
            }
            // This must be the correct freevar specialization.
            assert(!f.anonymous || sf->freevarchecked);
            assert(!sf->freevars.v.size());
            LOG_DEBUG("specialization: ", Signature(*sf));
            return TypeCheckMatchingCall(sf, static_dispatch, first_dynamic);
        };

        auto TypeCheckCallDispatch = [&]() {
            // We must assume the instance may dynamically be different, so go thru vtable.
            // See if we already have a vtable entry for this type of call.
            for (auto [i, disp] : enumerate(dispatch_udt->dispatch)) {
                // FIXME: does this guarantee it find it in the recursive case?
                // TODO: we chould check for a superclass vtable entry also, but chances
                // two levels will be present are low.
                if (disp.sf && disp.sf->method_of == dispatch_udt && disp.is_dispatch_root &&
                    &f == disp.sf->parent && SpecializationIsCompatible(*disp.sf)) {
                    for (auto [i, c] : enumerate(call_args->children)) if (i < f.nargs()) {
                        auto &arg = disp.sf->args.v[i];
                        if (i && !ConvertsTo(c->exptype, arg.type, false, false))
                            goto fail;
                    }
                    for (auto udt : dispatch_udt->subudts) {
                        // Since all functions were specialized with the same args, they should
                        // all be compatible if the root is.
                        auto sf = udt->dispatch[i].sf;
                        LOG_DEBUG("re-using dyndispatch: ", Signature(*sf));
                        assert(SpecializationIsCompatible(*sf));
                        for (auto &fv : sf->freevars.v) CheckFreeVariable(*fv.sid);
                        ReplayReturns(sf);
                    }
                    // Type check this as if it is a static dispatch to just the root function.
                    TypeCheckMatchingCall(disp.sf, true, false);
                    vtable_idx = (int)i;
                    return dispatch_udt->dispatch[i].returntype;
                }
                fail:;
            }
            // Must create a new vtable entry.
            // TODO: would be good to search superclass if it has this method also.
            // Probably not super important since dispatching on the "middle" type in a
            // hierarchy will be rare.
            // Find subclasses and max vtable size.
            {
                vector<int> overload_idxs;
                for (auto sub : dispatch_udt->subudts) {
                    int best = -1;
                    int bestdist = 0;
                    for (auto [i, sf] : enumerate(csf->parent->overloads)) {
                        if (sf->method_of) {
                            auto sdist = st.SuperDistance(sf->method_of, sub);
                            if (sdist >= 0 && (best < 0 || bestdist > sdist)) {
                                best = (int)i;
                                bestdist = sdist;
                            }
                        }
                    }
                    if (best < 0) {
                        if (sub->constructed) {
                            TypeError("no implementation for " + sub->name + "." + csf->parent->name,
                                call_context);
                        } else {
                            // This UDT is unused, so we're ok there not being an implementation
                            // for it.. like e.g. an abstract base class.
                        }
                    }
                    overload_idxs.push_back(best);
                    vtable_idx = max(vtable_idx, (int)sub->dispatch.size());
                }
                // Add functions to all vtables.
                for (auto [i, udt] : enumerate(dispatch_udt->subudts)) {
                    auto &dt = udt->dispatch;
                    assert((int)dt.size() <= vtable_idx);  // Double entry.
                    // FIXME: this is not great, wasting space, but only way to do this
                    // on the fly without tracking lots of things.
                    while ((int)dt.size() < vtable_idx) dt.push_back({});
                    dt.push_back({ overload_idxs[i] < 0
                                    ? nullptr
                                    : csf->parent->overloads[overload_idxs[i]] });
                }
                // FIXME: if any of the overloads below contain recursive calls, it may run into
                // issues finding an existing dispatch above? would be good to guarantee..
                // The fact that in subudts the superclass comes first will help avoid problems
                // in many cases.
                auto de = &dispatch_udt->dispatch[vtable_idx];
                de->is_dispatch_root = true;
                de->returntype = NewTypeVar();
                // Typecheck all the individual functions.
                SubFunction *last_sf = nullptr;
                for (auto [i, udt] : enumerate(dispatch_udt->subudts)) {
                    auto sf = udt->dispatch[vtable_idx].sf;
                    if (!sf) continue;  // Missing implementation for unused UDT.
                    // FIXME: this possible runs the code below multiple times for the same sf,
                    // we rely on it finding the same specialization.
                    if (last_sf) {
                        // FIXME: good to have this check here so it only occurs for functions
                        // participating in the dispatch, but error now appears at the call site!
                        for (auto [j, arg] : enumerate(sf->args.v)) {
                            if (j && arg.type != last_sf->args.v[j].type &&
                                !(arg.flags & AF_GENERIC))
                                TypeError("argument " + to_string(j + 1) + " of " + f.name +
                                          " overload type mismatch", call_context);
                        }
                    }
                    call_args->children[0]->exptype = &udt->thistype;
                    // FIXME: return value?
                    /*auto rtype =*/ TypeCheckCallStatic(overload_idxs[i], false, !last_sf);
                    de = &dispatch_udt->dispatch[vtable_idx];  // May have realloced.
                    sf = chosen;
                    sf->method_of->dispatch[vtable_idx].sf = sf;
                    // FIXME: Lift these limits?
                    if (sf->returntype->NumValues() > 1)
                        TypeError("dynamic dispatch can currently return only 1 value.",
                            call_context);
                    auto u = sf->returntype;
                    if (de->returntype->IsBoundVar()) {
                        // Typically in recursive calls, but can happen otherwise also?
                        if (!ConvertsTo(u, de->returntype, false))
                            // FIXME: not a great error, but should be rare.
                            TypeError("dynamic dispatch for " + f.name +
                                " return value type " +
                                TypeName(sf->returntype) +
                                " doesn\'t match other case returning " +
                                TypeName(de->returntype), *sf->body);
                    } else {
                        if (i) {
                            // We have to be able to take the union of all retvals without
                            // coercion, since we're not fixing up any previously typechecked
                            // functions.
                            u = Union(u, de->returntype, false, &call_context);
                            // Ensure we didn't accidentally widen the type from a scalar.
                            assert(IsRef(de->returntype->t) || !IsRef(u->t));
                        }
                        de->returntype = u;
                    }
                    last_sf = sf;
                }
                call_args->children[0]->exptype = &dispatch_udt->thistype;
            }
            return dispatch_udt->dispatch[vtable_idx].returntype;
        };

        if (f.istype) {
            // Function types are always fully typed.
            // All calls thru this type must have same lifetimes, so we fix it to LT_BORROW.
            return TypeCheckMatchingCall(csf, true, false);
        }
        // Check if we need to do dynamic dispatch. We only do this for functions that have a
        // explicit first arg type of a class (not structs, since they can never dynamically be
        // different from their static type), and only when there is a sub-class that has a
        // method that can be called also.
        if (f.nargs()) {
            auto type = call_args->children[0]->exptype;
            if (type->t == V_CLASS) dispatch_udt = type->udt;
        }
        if (dispatch_udt) {
            size_t num_methods = 0;
            for (auto isf : csf->parent->overloads) if (isf->method_of) num_methods++;
            if (num_methods > 1) {
                // Go thru all other overloads, and see if any of them have this one as superclass.
                for (auto isf : csf->parent->overloads) {
                    if (isf->method_of && st.SuperDistance(dispatch_udt, isf->method_of) > 0) {
                        LOG_DEBUG("dynamic dispatch: ", Signature(*isf));
                        return TypeCheckCallDispatch();
                    }
                }
                // Yay there are no sub-class implementations, we can just statically dispatch.
            }
            // Yay only one method, we can statically dispatch.
        }
        // Do a static dispatch, if there are overloads, figure out from first arg which to pick,
        // much like dynamic dispatch. Unlike dynamic dispatch, we also include non-class types.
        // TODO: also involve the other arguments for more complex static overloads?
        int overload_idx = 0;
        if (f.nargs() && f.overloads.size() > 1) {
            overload_idx = -1;
            auto type0 = call_args->children[0]->exptype;
            // First see if there is an exact match.
            for (auto [i, isf] : enumerate(f.overloads)) {
                if (ExactType(type0, isf->args.v[0].type)) {
                    if (overload_idx >= 0)
                        TypeError("multiple overloads have the same type: " + f.name +
                                  ", first arg: " + TypeName(type0), call_context);
                    overload_idx = (int)i;
                }
            }
            // Then see if there's a match by subtyping.
            if (overload_idx < 0) {
                for (auto [i, isf] : enumerate(f.overloads)) {
                    auto arg0 = isf->args.v[0].type;
                    if (ConvertsTo(type0, arg0, false, false)) {
                        if (overload_idx >= 0) {
                            if (type0->t == V_CLASS) {
                                auto oarg0 = f.overloads[overload_idx]->args.v[0].type;
                                // Prefer "closest" supertype.
                                auto dist = st.SuperDistance(arg0->udt, type0->udt);
                                auto odist = st.SuperDistance(oarg0->udt, type0->udt);
                                if (dist < odist) overload_idx = (int)i;
                                else if (odist < dist) { /* keep old one */ }
                                else {
                                    TypeError("multiple overloads have the same class: " + f.name +
                                              ", first arg: " + TypeName(type0), call_context);
                                }
                            } else {
                                TypeError("multiple overloads apply: " + f.name + ", first arg: " +
                                    TypeName(type0), call_context);
                            }
                        } else {
                            overload_idx = (int)i;
                        }
                    }
                }
            }
            // Then finally try with coercion.
            if (overload_idx < 0) {
                for (auto [i, isf] : enumerate(f.overloads)) {
                    if (ConvertsTo(type0, isf->args.v[0].type, true, false)) {
                        if (overload_idx >= 0) {
                            TypeError("multiple overloads can coerce: " + f.name +
                                      ", first arg: " + TypeName(type0), call_context);
                        }
                        overload_idx = (int)i;
                    }
                }
            }
            if (overload_idx < 0)
                TypeError("no overloads apply: " + f.name + ", first arg: " + TypeName(type0),
                          call_context);
        }
        LOG_DEBUG("static dispatch: ", Signature(*f.overloads[overload_idx]));
        return TypeCheckCallStatic(overload_idx, true, false);
    }

    SubFunction *PreSpecializeFunction(SubFunction *hsf) {
        // Don't pre-specialize named functions, because this is not their call-site.
        if (!hsf->parent->anonymous) return hsf;
        assert(hsf->parent->overloads.size() == 1);
        hsf = hsf->parent->overloads[0];
        auto sf = hsf;
        if (sf->freevarchecked) {
            // See if there's an existing match.
            for (; sf; sf = sf->next) if (sf->freevarchecked) {
                if (FreeVarsSameAsCurrent(*sf, true)) return sf;
            }
            sf = CloneFunction(hsf, 0);
        } else {
            // First time this function has ever been touched.
            sf->freevarchecked = true;
        }
        assert(!sf->freevars.v.size());
        // Output without arg types, since those are yet to be overwritten.
        LOG_DEBUG("pre-specialization: ", SignatureWithFreeVars(*sf, nullptr, false));
        return sf;
    }

    pair<TypeRef, Lifetime> TypeCheckDynCall(SpecIdent *fval, List *args, SubFunction *&fspec,
                                             size_t reqret) {
        auto &ftype = fval->type;
        auto nargs = args->Arity();
        // FIXME: split this up in a Call, a Yield and a DynCall(istype = true) node, just like
        // GenericCall does.
        if (ftype->IsFunction()) {
            // We can statically typecheck this dynamic call. Happens for almost all non-escaping
            // closures.
            auto sf = ftype->sf;
            if (nargs < sf->parent->nargs())
                TypeError("function value called with too few arguments", *args);
            // In the case of too many args, TypeCheckCall will ignore them (and optimizer will
            // remove them).
            int vtable_idx = -1;
            auto type = TypeCheckCall(sf, args, fspec, *args, reqret, vtable_idx);
            assert(vtable_idx < 0);
            ftype = &fspec->thistype;
            return { type, fspec->ltret };
        } else if (ftype->t == V_YIELD) {
            // V_YIELD must have perculated up from a coroutine call.
            if (nargs != 1)
                TypeError("coroutine yield call must have exactly one argument", *args);
            NoStruct(*args->children[0], "yield");  // FIXME: implement.
            AdjustLifetime(args->children[0], LT_KEEP);
            for (auto scope : reverse(named_scopes)) {
                auto sf = scope.sf;
                if (!sf->iscoroutine) continue;
                // What yield returns to return_value(). If no arg, then it will return nil.
                auto type = args->children[0]->exptype;
                RetVal(type, sf, *args);
                SubTypeT(type, sf->returntype, *args, "", "yield value");
                // Now collect all ids between coroutine and yield, so that we can save these in the
                // VM.
                bool foundstart = false;
                for (auto savescope = scopes.begin(); savescope != scopes.end(); ++savescope) {
                    auto ssf = savescope->sf;
                    if (ssf == sf) foundstart = true;
                    if (!foundstart) continue;
                    for (auto &arg : ssf->args.v)
                        sf->coyieldsave.Add(arg);
                    for (auto &loc : ssf->locals.v)
                        sf->coyieldsave.Add(Arg(loc.sid, loc.sid->type, loc.flags & AF_WITHTYPE));
                }
                for (auto &cys : sf->coyieldsave.v) UpdateCurrentSid(cys.sid);
                return { sf->coresumetype, LT_KEEP };
            }
            TypeError("yield function called outside scope of coroutine", *args);
            return { type_void, LT_ANY };
        } else {
            TypeError("dynamic function call value doesn\'t have a function type: " +
                      TypeName(ftype), *args);
            return { type_void, LT_ANY };
        }
    }

    TypeRef TypeCheckBranch(bool iftrue, const Node *condition, Node *&bodycall,
                            bool reqret, Lifetime recip) {
        auto flowstart = CheckFlowTypeChanges(iftrue, condition);
        TT(bodycall, reqret, recip);
        CleanUpFlow(flowstart);
        return bodycall->exptype;
    }

    void CheckFlowTypeIdOrDot(const Node &n, TypeRef type) {
        FlowItem fi(n, type);
        if (fi.IsValid()) flowstack.push_back(fi);
    }

    void CheckFlowTypeChangesSub(bool iftrue, const Node *condition) {
        condition = SkipCoercions(condition);
        auto type = condition->exptype;
        if (auto c = Is<IsType>(condition)) {
            if (iftrue) CheckFlowTypeIdOrDot(*c->child, c->giventype);
        } else if (auto c = Is<Not>(condition)) {
            CheckFlowTypeChangesSub(!iftrue, c->child);
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
        if ((left.exptype->t == V_ANY && right->t != V_ANY) ||
            (left.exptype->t == V_NIL && right->t != V_NIL)) {
            CheckFlowTypeIdOrDot(left, right);
        }
    }

    // FIXME: this can in theory find the wrong node, if the same function nests, and the outer
    // one was specialized to a nilable and the inner one was not.
    // This would be very rare though, and benign.
    TypeRef AssignFlowDemote(FlowItem &left, TypeRef overwritetype, bool coercions) {
        // Early out, numeric types are not nillable, nor do they make any sense for "is"
        auto &type = left.now;
        if (type->Numeric()) return type;
        for (auto flow : reverse(flowstack)) {
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
            ao.exptype = Union(tleft, tright, false, nullptr);
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
        n = RemoveCoercions(n);
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

    void CheckLval(Node *n) {
        if (auto dot = Is<Dot>(n)) {
            auto type = dot->children[0]->exptype;
            if (IsStruct(type->t))
                TypeError("cannot write to field of value: " + type->udt->name, *n);
        }
        // This can happen due to late specialization of GenericCall.
        if (Is<Call>(n) || Is<NativeCall>(n))
            TypeError("function-call cannot be an l-value", *n);
        Borrow lv(*n);
        if (!lv.IsValid()) return;  // FIXME: force these to LT_KEEP?
        if (lv.derefs.empty() && LifetimeType(lv.sid->lt) == LT_BORROW) {
            // This should only happen for multimethods and anonymous functions used with istype
            // where we can't avoid arguments being LT_BORROW.
            // All others should have been specialized to LT_KEEP when a var is not
            // single_assignment.
            // This is not particularly elegant but should be rare.
            TypeError(cat("cannot assign to borrowed argument: ", lv.sid->id->name), *n);
        }
        // FIXME: make this faster.
        for (auto &b : reverse(borrowstack)) {
            if (!b.IsPrefix(lv)) continue;  // Not overwriting this one.
            if (!b.refc) continue;          // Lval is not borowed, writing is ok.
            TypeError(cat("cannot assign to ", lv.Name(), " while borrowed"), *n);
        }
    }

    Lifetime PushBorrow(Node *n) {
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
            if (sf->freevars.Add(Arg(&sid, sid.type, AF_GENERIC))) {
                //LOG_DEBUG("freevar added: ", id.name, " (", TypeName(id.type),
                //                     ") in ", sf->parent->name);
            }
        }
    }

    bool NeverReturns(const Node *n) {
        if (auto call = Is<Call>(n)) {
            // Have to be conservative for recursive calls since we're not done typechecking it.
            if (call->sf->isrecursivelycalled ||
                call->sf->method_of ||
                call->sf->iscoroutine ||
                call->sf->parent->istype) return false;
            if (!call->sf->num_returns) return true;
            if (call->sf->num_returns == 1) {
                auto ret = AssertIs<Return>(call->sf->body->children.back());
                assert(ret->sf == call->sf);
                return NeverReturns(ret->child);
            }
            // TODO: could also check num_returns > 1, but then have to scan all children.
        } else if (auto ifthen = Is<If>(n)) {
            auto tp = Is<Call>(ifthen->truepart);
            auto fp = Is<Call>(ifthen->falsepart);
            return tp && fp && NeverReturns(tp) && NeverReturns(fp);
        } else if (auto sw = Is<Switch>(n)) {
            auto have_default = false;
            for (auto c : sw->cases->children) {
                auto cas = AssertIs<Case>(c);
                if (cas->pattern->children.empty()) have_default = true;
                if (!NeverReturns(cas->body)) return false;
            }
            return have_default;
        } else if (auto nc = Is<NativeCall>(n)) {
            // A function may end in "assert false" and have only its previous return statements
            // taken into account.
            Value cval;
            if (nc->nf->IsAssert() && nc->children[0]->ConstVal(*this, cval) && !cval.True())
                return true;
        }
        // TODO: Other situations?
        return false;
    }

    void TypeCheckList(List *n, bool onlylast, size_t reqret, Lifetime lt) {
        for (auto &c : n->children) {
            auto tovoid = onlylast && c != n->children.back();
            TT(c, tovoid ? 0 : reqret,
                  tovoid ? LT_ANY : lt);
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

    Node *RemoveCoercions(Node *n) {
        auto c = IsCoercion(n);
        return c ? RemoveCoercions(DeleteCoercion((Coercion *)c)) : n;
    }

    Node *DeleteCoercion(Coercion *c) {
        auto ch = c->child;
        c->child = nullptr;
        delete c;
        return ch;
    }

    Lifetime LvalueLifetime(const Node &lval, bool deref) {
        if (auto idr = Is<IdentRef>(lval)) return idr->sid->lt;
        if (deref) {
            if (auto dot = Is<Dot>(lval)) return LvalueLifetime(*dot->children[0], deref);
            if (auto idx = Is<Indexing>(lval)) return LvalueLifetime(*idx->object, deref);
        }
        if (auto cod = Is<CoDot>(lval)) return AssertIs<IdentRef>(cod->variable)->sid->lt;
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
        assert(IsRefNilVar(b.sid->type->t));
        b.refc += change;
        LOG_DEBUG("borrow ", change, ": ", b.sid->id->name, " in ", NiceName(context),
               ", ", b.refc, " remain");
        // FIXME: this should really just not be possible, but hard to guarantee.
        if (b.refc < 0)
            TypeError(cat(b.sid->id->name, " used in ", NiceName(context),
                          " without being borrowed"), context);
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

    void AdjustLifetime(Node *&n, Lifetime recip, const vector<Node *> *idents = nullptr) {
        assert(n->lt != LT_UNDEF && recip != LT_UNDEF);
        if (recip == LT_ANY) return;
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
                    if (i >= sizeof(incref) * 8) TypeError("too many return values", *n);
                    if (given == LT_BORROW && recip == LT_KEEP) {
                        incref |= 1LL << i;
                        DecBorrowers(givenlt, *n);
                    } else if (given == LT_KEEP && recip == LT_BORROW) {
                        decref |= 1LL << i;
                    } else if (given == LT_ANY) {
                        // These are compatible with whatever recip wants.
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
            LOG_DEBUG("lifetime adjust for ", NiceName(*n), " to ", incref, "/",
                                 decref);
            MakeLifetime(n, idents ? LT_MULTIPLE: recip, incref, decref);
        }
    }

    // This is the central function thru which all typechecking flows, so we can conveniently
    // match up what the node produces and what the recipient expects.
    void TT(Node *&n, size_t reqret, Lifetime recip, const vector<Node *> *idents = nullptr) {
        // Central point from which each node is typechecked.
        n = n->TypeCheck(*this, reqret);
        // Check if we need to do any type adjustmenst.
        auto &rt = n->exptype;
        n->exptype = rt;
        auto nret = rt->NumValues();
        if (nret < reqret) {
            TypeError(cat(NiceName(*n), " returns ", nret, " values, ", reqret, " needed"), *n);
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
                    auto nt = NewTuple(reqret);
                    nt->tup->assign(rt->tup->begin(), rt->tup->begin() + reqret);
                    rt = nt;
                }
            }
        }
        // Check if we need to do any lifetime adjustments.
        AdjustLifetime(n, recip, idents);
    }

    // TODO: Can't do this transform ahead of time, since it often depends upon the input args.
    TypeRef ActualBuiltinType(int flen, TypeRef type, ArgFlags flags, Node *exp,
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
        auto etype = exp ? exp->exptype : nullptr;
        auto e = etype;
        size_t i = 0;
        for (auto vt = type; vt->t == V_VECTOR && i < SymbolTable::NUM_VECTOR_TYPE_WRAPPINGS;
            vt = vt->sub) {
            if (vt->sub->Numeric()) {
                // Check if we allow any vector length.
                if (!e.Null() && flen == -1 && e->t == V_STRUCT_S) {
                    flen = (int)e->udt->fields.size();
                }
                if (!etype.Null() && flen == -1 && etype->t == V_VAR) {
                    // Special case for "F}?" style types that can be matched against a
                    // DefaultArg, would be good to solve this more elegantly..
                    // FIXME: don't know arity, but it doesn't matter, so we pick 2..
                    return st.VectorType(vt, i, 2);
                }
                if (flen >= 2 && flen <= 4) {
                    if (!e.Null() && e->t == V_STRUCT_S && (int)e->udt->fields.size() == flen &&
                        e->udt->sametype == vt->sub) {
                        // Allow any similar vector type, like "color".
                        return etype;
                    }
                    else {
                        // Require xy/xyz/xyzw
                        return st.VectorType(vt, i, flen);
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
        if (!test_overloads)
            TypeError("cannot deduce struct type for " +
            (argn ? cat("argument ", argn) : "return value") +
                " of " + nf->name + (!etype.Null() ? ", got: " + TypeName(etype) : ""),
                errorn);
        return type;
    };

    void Stats() {
        if (min_output_level > OUTPUT_INFO) return;
        int origsf = 0, clonesf = 0;
        size_t orignodes = 0, clonenodes = 0;
        typedef pair<size_t, Function *> Pair;
        vector<Pair> funstats;
        for (auto f : st.functiontable) funstats.push_back({ 0, f });
        for (auto sf : st.subfunctiontable) {
            auto count = sf->body ? sf->body->Count() : 0;
            if (!sf->next)        {
                origsf++;
                orignodes += count;
            } else {
                clonesf++;
                clonenodes += count;
                funstats[sf->parent->idx].first += count;
            }
        }
        LOG_INFO("SF count: orig: ", origsf, ", cloned: ", clonesf);
        LOG_INFO("Node count: orig: ", orignodes, ", cloned: ", clonenodes);
        sort(funstats.begin(), funstats.end(),
            [](const Pair &a, const Pair &b) { return a.first > b.first; });
        for (auto &[fsize, f] : funstats) if (fsize > orignodes / 100) {
            auto &pos = f->overloads.back()->body->line;
            LOG_INFO("Most clones: ", f->name, " (", st.filenames[pos.fileidx], ":", pos.line,
                     ") -> ", fsize, " nodes accross ", f->NumSubf() - f->overloads.size(),
                     " clones (+", f->overloads.size(), " orig)");
        }
    }
};

Node *List::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    assert(false);  // Parent calls TypeCheckList.
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

Node *Inlined::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    assert(false);  // Generated after type-checker in optimizer.
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

Node *If::TypeCheck(TypeChecker &tc, size_t reqret) {
    tc.TT(condition, 1, LT_BORROW);
    tc.NoStruct(*condition, "if");
    tc.DecBorrowers(condition->lt, *this);
    Value cval;
    bool isconst = condition->ConstVal(tc, cval);
    if (!Is<DefaultVal>(falsepart)) {
        if (!isconst) {
            auto tleft = tc.TypeCheckBranch(true, condition, truepart, reqret, LT_ANY);
            auto tright = tc.TypeCheckBranch(false, condition, falsepart, reqret, LT_ANY);
            // FIXME: this is a bit of a hack. Much better if we had an actual type
            // to signify NORETURN, to be taken into account in more places.
            auto truec = AssertIs<Call>(truepart);
            auto falsec = AssertIs<Call>(falsepart);
            if (tc.NeverReturns(truec)) {
                exptype = tright;
                lt = falsepart->lt;
            } else if (tc.NeverReturns(falsec)) {
                exptype = tleft;
                lt = truepart->lt;
            } else {
                exptype = tc.Union(tleft, tright, true, this);
                // These will potentially make either body from T_CALL into some
                // coercion.
                tc.SubType(truepart, exptype, "then branch", *this);
                tc.SubType(falsepart, exptype, "else branch", *this);
                lt = tc.LifetimeUnion(truepart, falsepart, false);
            }
        } else if (cval.True()) {
            // Ignore the else part, optimizer guaranteed to cull it.
            exptype = tc.TypeCheckBranch(true, condition, truepart, reqret, LT_ANY);
            lt = truepart->lt;
        } else {
            // Ignore the then part, optimizer guaranteed to cull it.
            exptype = tc.TypeCheckBranch(false, condition, falsepart, reqret, LT_ANY);
            lt = falsepart->lt;
        }
    } else {
        // No else: this always returns void.
        if (!isconst || cval.True()) {
            tc.TypeCheckBranch(true, condition, truepart, 0, LT_ANY);
            truepart->exptype = type_void;
        } else {
            // constant == false: this if-then will get optimized out entirely, ignore it.
        }
        falsepart->exptype = type_void;
        exptype = type_void;
        lt = LT_ANY;
    }
    return this;
}

Node *While::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(condition, 1, LT_BORROW);
    tc.NoStruct(*condition, "while");
    tc.DecBorrowers(condition->lt, *this);
    // FIXME: this is caused by call forced to LT_KEEP.
    auto condc = AssertIs<Call>(Forward<ToLifetime>(condition));
    auto condexp = AssertIs<Return>(condc->sf->body->children.back());
    tc.TypeCheckBranch(true, condexp->child, body, 0, LT_ANY);
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
    else tc.TypeError("for can only iterate over int / string / vector, not: " +
        TypeName(itertype), *this);
    auto bodyc = AssertIs<Call>(body);
    auto &args = bodyc->children;
    if (args.size()) {
        args[0]->exptype = itertype;  // ForLoopElem
    }
    tc.TT(body, 0, LT_ANY);
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

Node *Switch::TypeCheck(TypeChecker &tc, size_t reqret) {
    // TODO: much like If, should only typecheck one case if the value is constant, and do
    // the corresponding work in the optimizer.
    tc.TT(value, 1, LT_BORROW);
    auto ptype = value->exptype;
    if (!ptype->Numeric() && ptype->t != V_STRING)
        tc.TypeError("switch value must be int / float / string", *this);
    exptype = nullptr;
    bool have_default = false;
    vector<bool> enum_cases;
    if (ptype->IsEnum()) enum_cases.resize(ptype->e->vals.size());
    for (auto &n : cases->children) {
        tc.TT(n, reqret, LT_KEEP);
        auto cas = AssertIs<Case>(n);
        if (cas->pattern->children.empty()) have_default = true;
        for (auto c : cas->pattern->children) {
            tc.SubTypeT(c->exptype, ptype, *c, "", "case");
            tc.DecBorrowers(c->lt, *cas);
            if (ptype->IsEnum()) {
                assert(c->exptype->IsEnum());
                Value v;
                if (c->ConstVal(tc, v)) {
                    for (auto [i, ev] : enumerate(ptype->e->vals)) if (ev->val == v.ival()) {
                        enum_cases[i] = true;
                        break;
                    }
                }
            }
        }
        auto body = AssertIs<Call>(cas->body);
        if (!tc.NeverReturns(body)) {
            exptype = exptype.Null() ? body->exptype
                                     : tc.Union(exptype, body->exptype, true, cas);
        }
    }
    for (auto n : cases->children) {
        auto cas = AssertIs<Case>(n);
        auto body = AssertIs<Call>(cas->body);
        if (!tc.NeverReturns(body)) {
            assert(!exptype.Null());
            tc.SubType(cas->body, exptype, "", "case block");
        }
    }
    if (exptype.Null()) exptype = type_void;  // Empty switch or all return statements.
    if (!have_default) {
        if (reqret) tc.TypeError("switch that returns a value must have a default case", *this);
        if (ptype->IsEnum()) {
            for (auto [i, ev] : enumerate(ptype->e->vals)) if (!enum_cases[i])
                tc.TypeError("enum value not tested in switch: " + ev->name, *value);
        }
    }
    tc.DecBorrowers(value->lt, *this);
    lt = LT_KEEP;
    return this;
}

Node *Case::TypeCheck(TypeChecker &tc, size_t reqret) {
    // FIXME: Since string constants are the real use case, LT_KEEP would be more
    // natural here, as this will introduce a lot of keeprefs. Alternatively make sure
    // string consts don't introduce keeprefs.
    tc.TypeCheckList(pattern, false, 1, LT_BORROW);
    tc.TT(body, reqret, LT_KEEP);
    exptype = body->exptype;
    lt = LT_KEEP;
    return this;
}

Node *Range::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(start, 1, LT_KEEP);
    tc.TT(end, 1, LT_KEEP);
    exptype = start->exptype;
    if (exptype->t != end->exptype->t || !exptype->Numeric())
        tc.TypeError("range can only be two equal numeric types", *this);
    lt = LT_ANY;
    return this;
}

Node *CoDot::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(coroutine, 1, LT_BORROW);
    // Leave right ident untypechecked.
    tc.SubType(coroutine, type_coroutine, "coroutine", *this);
    auto sf = coroutine->exptype->sf;
    Arg *uarg = nullptr;
    // This ident is not necessarily the right one.
    auto var = AssertIs<IdentRef>(variable);
    auto &name = var->sid->id->name;
    for (auto &arg : sf->coyieldsave.v) if (arg.sid->id->name == name) {
        if (uarg) tc.TypeError("multiple coroutine variables named: " + name, *this);
        uarg = &arg;
    }
    if (!uarg) tc.TypeError("no coroutine variables named: " + name, *this);
    var->sid = uarg->sid;
    var->exptype = exptype = uarg->type;
    // FIXME: this really also borrows from the actual variable, in case the coroutine is run
    // again?
    lt = coroutine->lt;
    return this;
}

Node *Define::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    for (auto &p : sids) {
        tc.UpdateCurrentSid(p.first);
        // We have to set these here just in case the init exp is a function/coroutine call that
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
    tc.TT(child, sids.size(), LT_KEEP);
    for (auto [i, p] : enumerate(sids)) {
        auto var = TypeLT(*child, i);
        if (!p.second.Null()) {
            var.type = p.second;
            // Have to subtype the initializer value, as that node may contain
            // unbound vars (a:[int] = []) or values that that need to be coerced
            // (a:float = 1)
            tc.SubType(child, var.type, "initializer", "definition");
        }
        auto sid = p.first;
        sid->type = var.type;
        tc.StorageType(var.type, *this);
        sid->type = var.type;
        sid->lt = var.lt;
        LOG_DEBUG("var: ", sid->id->name, ":", TypeName(var.type));
        if (sid->id->logvar) {
            for (auto &sc : tc.scopes)
                if (sc.sf->iscoroutine)
                    tc.TypeError("can\'t use log variable inside coroutine", *this);
        }
    }
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *AssignList::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    for (auto &c : children) {
        if (c != children.back()) {
            tc.TT(c, 1, LT_BORROW);
            tc.DecBorrowers(c->lt, *this);
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
        tc.AssignFlowDemote(fi, righttype, false);
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
    exptype = !giventype.Null() ? giventype : tc.st.Wrap(tc.NewTypeVar(), V_NIL);
    lt = LT_ANY;
    return this;
}

Node *Plus::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOp(*this);
    return this;
}

Node *Minus::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOp(*this);
    return this;
}

Node *Multiply::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOp(*this);
    return this;
}

Node *Divide::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOp(*this);
    return this;
}

Node *Mod::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOp(*this);
    return this;
}

Node *PlusEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOpEq(*this);
    return this;
}

Node *MultiplyEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOpEq(*this);
    return this;
}

Node *MinusEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOpEq(*this);
    return this;
}

Node *DivideEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOpEq(*this);
    return this;
}

Node *ModEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckMathOpEq(*this);
    return this;
}

Node *NotEqual::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckComp(*this);
    return this;
}

Node *Equal::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckComp(*this);
    return this;
}

Node *GreaterThanEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckComp(*this);
    return this;
}

Node *LessThanEq::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckComp(*this);
    return this;
}

Node *GreaterThan::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckComp(*this);
    return this;
}

Node *LessThan::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckComp(*this);
    return this;
}

Node *Not::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    tc.DecBorrowers(child->lt, *this);
    tc.NoStruct(*child, "not");
    exptype = &tc.st.default_bool_type->thistype;
    lt = LT_ANY;
    return this;
}

Node *BitAnd::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckBitOp(*this);
    return this;
}

Node *BitOr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckBitOp(*this);
    return this;
}

Node *Xor::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckBitOp(*this);
    return this;
}

Node *ShiftLeft::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckBitOp(*this);
    return this;
}

Node *ShiftRight::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckBitOp(*this);
    return this;
}

Node *Negate::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    tc.SubType(child, type_int, "negated value", *this);
    tc.DecBorrowers(child->lt, *this);
    exptype = child->exptype;
    lt = LT_ANY;
    return this;
}

Node *PostDecr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckPlusPlus(*this);
    return this;
}

Node *PostIncr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckPlusPlus(*this);
    return this;
}

Node *PreDecr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckPlusPlus(*this);
    return this;
}

Node *PreIncr::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckPlusPlus(*this);
    return this;
}

Node *UnaryMinus::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    exptype = child->exptype;
    if (!exptype->Numeric() &&
        (exptype->t != V_STRUCT_S || !exptype->udt->sametype->Numeric()))
        tc.TypeError("numeric / numeric struct", exptype, *this);
    tc.DecBorrowers(child->lt, *this);
    lt = LT_KEEP;
    return this;
}

Node *IdentRef::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.UpdateCurrentSid(sid);
    for (auto &sc : reverse(tc.scopes)) if (sc.sf == sid->sf_def) goto in_scope;
    tc.TypeError("free variable not in scope: " + sid->id->name, *this);
    in_scope:
    exptype = tc.TypeCheckId(sid);
    FlowItem fi(*this, exptype);
    assert(fi.IsValid());
    exptype = tc.UseFlow(fi);
    lt = tc.PushBorrow(this);
    return this;
}

Node *Assign::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(left, 1, LT_BORROW);
    tc.DecBorrowers(left->lt, *this);
    tc.TT(right, 1, tc.LvalueLifetime(*left, false));
    tc.CheckLval(left);
    FlowItem fi(*left, left->exptype);
    if (fi.IsValid()) {
        left->exptype = tc.AssignFlowDemote(fi, right->exptype, true);
    }
    tc.SubType(right, left->exptype, "right", *this);
    if (fi.IsValid()) tc.AssignFlowPromote(*left, right->exptype);
    exptype = left->exptype;
    if (fi.IsValid()) exptype = tc.UseFlow(fi);
    lt = tc.PushBorrow(left);
    return this;
}

Node *DefaultVal::TypeCheck(TypeChecker &tc, size_t reqret) {
    // This is used as a default value for native call arguments. The variable
    // makes it equal to whatever the function expects, then codegen can use that type
    // to generate a correct value.
    // Also used as an empty else branch.
    exptype = reqret ? tc.NewTypeVar() : type_void;
    lt = LT_ANY;
    return this;
}

Node *GenericCall::TypeCheck(TypeChecker &tc, size_t reqret) {
    // Here we decide which of Dot / Call / NativeCall this call should be transformed into.
    tc.TypeCheckList(this, false, 1, LT_ANY);
    auto nf = tc.parser.natreg.FindNative(name);
    auto fld = tc.st.FieldUse(name);
    TypeRef type;
    UDT *udt = nullptr;
    if (children.size()) {
        type = children[0]->exptype;
        if (IsUDT(type->t)) udt = type->udt;
    }
    Node *r = nullptr;
    if (fld && dotnoparens && udt && udt->Has(fld) >= 0) {
        auto dot = new Dot(fld, *this);
        dot->children = children;
        dot->TypeCheckSpecialized(tc, reqret);
        r = dot;
    } else {
        // See if any of sf's specializations matches type exactly, then it overrides nf.
        bool prefer_sf = false;
        if (sf && udt && sf->parent->nargs()) {
            for (auto sfi : sf->parent->overloads) {
                if (sfi->args.v[0].type->udt == udt) {
                    prefer_sf = true;
                    break;
                }
            }
        }
        if (nf && !prefer_sf) {
            auto nc = new NativeCall(nf, *this);
            nc->children = children;
            nc->TypeCheckSpecialized(tc, reqret);
            r = nc;
        } else if (sf) {
            auto fc = new Call(*this);
            fc->children = children;
            fc->TypeCheckSpecialized(tc, reqret);
            r = fc;
        } else {
            if (fld && dotnoparens)
                tc.TypeError("type " + TypeName(type) + " does not have field: " + fld->name, *this);
            tc.TypeError("unknown field/function reference: " + name, *this);
        }
    }
    children.clear();
    delete this;
    return r;
}

void NativeCall::TypeCheckSpecialized(TypeChecker &tc, size_t /*reqret*/) {
    if (nf->first->overloads) {
        // Multiple overloads available, figure out which we want to call.
        auto cnf = nf->first;
        auto nargs = Arity();
        for (; cnf; cnf = cnf->overloads) {
            if (cnf->args.v.size() != nargs) continue;
            for (auto [i, arg] : enumerate(cnf->args.v)) {
                // Special purpose treatment of V_ANY to allow generic vectors in overloaded
                // length() etc.
                if (arg.type->t != V_ANY &&
                    (arg.type->t != V_VECTOR ||
                     children[i]->exptype->t != V_VECTOR ||
                     arg.type->sub->t != V_ANY) &&
                    !tc.ConvertsTo(children[i]->exptype,
                                   tc.ActualBuiltinType(arg.fixed_len, arg.type, arg.flags,
                                                        children[i], nf, true, i + 1, *this),
                                   arg.type->t != V_STRING, false)) goto nomatch;
            }
            nf = cnf;
            break;
            nomatch:;
        }
        if (!cnf)
            tc.NatCallError("arguments match no overloads of ", nf, *this);
    }
    vector<TypeRef> argtypes(children.size());
    for (auto [i, c] : enumerate(children)) {
        auto &arg = nf->args.v[i];
        auto argtype = tc.ActualBuiltinType(arg.fixed_len, arg.type, arg.flags, children[i], nf, false, i + 1, *this);
        // Filter out functions that are not struct aware.
        bool typed = false;
        if (argtype->t == V_NIL && argtype->sub->Numeric() && !Is<DefaultVal>(c)) {
            // This is somewhat of a hack, because we conflate V_NIL with being optional
            // for native functions, but we don't want numeric types to be nilable.
            // Codegen has a special case for T_DEFAULTVAL however.
            argtype = argtype->sub;
        }
        if (arg.flags & NF_CONVERTANYTOSTRING && c->exptype->t != V_STRING) {
            tc.AdjustLifetime(c, LT_BORROW);  // MakeString wants to borrow.
            tc.MakeString(c, arg.lt);
            argtype = type_string;
            typed = true;
        }
        int flag = NF_SUBARG1;
        for (int sa = 0; sa < 3; sa++) {
            if (arg.flags & flag) {
                tc.SubType(c,
                        nf->args.v[sa].type->t == V_VECTOR && argtype->t != V_VECTOR
                            ? argtypes[sa]->sub
                            : argtypes[sa],
                        tc.ArgName(i),
                        nf->name);
                // Stop these generic params being turned into any by SubType below.
                typed = true;
            }
            flag *= 2;
        }
        if (arg.flags & NF_ANYVAR) {
            if (argtype->t == V_VECTOR)
                argtype = tc.st.Wrap(tc.NewTypeVar(), V_VECTOR);
            else if (argtype->t == V_ANY) argtype = tc.NewTypeVar();
            else assert(0);
        }
        if (arg.flags & NF_CORESUME) {
            // Specialized typechecking for resume()
            assert(argtypes[0]->t == V_COROUTINE);
            auto csf = argtypes[0]->sf;
            if (csf) {
                tc.SubType(c, csf->coresumetype, "resume value", *c);
            } else {
                if (!Is<DefaultVal>(c))
                    tc.TypeError("cannot resume a generic coroutine type with an argument",
                                 *this);
            }
            if (c->exptype->t == V_VAR) {
                // No value supplied to resume, and none expected at yield either.
                // nil will be supplied, so make type reflect that.
                tc.UnifyVar(tc.NewNilTypeVar(), c->exptype);
            }
            typed = true;
        }
        if (argtype->t == V_ANY) {
            if (!arg.flags) {
                // Special purpose type checking to allow any reference type for functions like
                // copy/equal/hash etc. Note that this is the only place in the language where
                // we allow this!
                if (!IsRefNilNoStruct(c->exptype->t))
                    tc.TypeError("reference type", c->exptype, *c, nf->args.GetName(i), nf->name);
                typed = true;
            } else if (IsStruct(c->exptype->t) &&
                       !(arg.flags & NF_PUSHVALUEWIDTH) &&
                       c->exptype->udt->numslots > 1) {
                // Avoid unsuspecting generic functions taking values as args.
                // TODO: ideally this does not trigger for any functions.
                tc.TypeError("function does not support this struct type", *this);
            }
        }
        if (nf->fun.fnargs >= 0 && !arg.fixed_len && !(arg.flags & NF_PUSHVALUEWIDTH))
            tc.NoStruct(*c, nf->name);
        if (!typed)
            tc.SubType(c, argtype, tc.ArgName(i), nf->name);
        auto actualtype = c->exptype;
        if (actualtype->IsFunction()) {
            // We must assume this is going to get called and type-check it
            auto fsf = actualtype->sf;
            if (fsf->args.v.size()) {
                // we have no idea what args.
                tc.TypeError("function passed to " + nf->name +
                             " cannot take any arguments", *this);
            }
            auto chosen = fsf;
            List args(tc.parser.lex);
            int vtable_idx = -1;
            tc.TypeCheckCall(fsf, &args, chosen, *c, false, vtable_idx);
            assert(vtable_idx < 0);
            assert(fsf == chosen);
        }
        argtypes[i] = actualtype;
        tc.StorageType(actualtype, *this);
        tc.AdjustLifetime(c, arg.lt);
        tc.DecBorrowers(c->lt, *this);
    }

    exptype = type_void;  // no retvals
    lt = LT_ANY;
    if (nf->retvals.v.size() > 1) exptype = tc.NewTuple(nf->retvals.v.size());
    for (auto [i, ret] : enumerate(nf->retvals.v)) {
        int sa = 0;
        auto type = ret.type;
        auto rlt = ret.lt;
        switch (ret.flags) {
            case NF_SUBARG3: sa++;
            case NF_SUBARG2: sa++;
            case NF_SUBARG1: {
                type = argtypes[sa];
                auto nftype = nf->args.v[sa].type;

                if (nftype->t == V_TYPEID) {
                    assert(!sa);  // assumes always first.
                    auto tin = AssertIs<TypeOf>(children[0]);
                    if (!Is<DefaultVal>(tin->child)) type = tin->child->exptype;
                }

                if (ret.type->t == V_NIL) {
                    if (!IsNillable(type->t))
                        tc.TypeError(cat("argument ", sa + 1, " to ", nf->name,
                                    " has to be a reference type"), *this);
                    type = tc.st.Wrap(type, V_NIL);
                } else if (nftype->t == V_VECTOR && ret.type->t != V_VECTOR) {
                    if (type->t == V_VECTOR) type = type->sub;
                } else if (nftype->t == V_COROUTINE || nftype->t == V_FUNCTION) {
                    auto csf = type->sf;
                    if (csf) {
                        // In theory it is possible this hasn't been generated yet..
                        type = csf->returntype;
                    } else {
                        // This can happen when typechecking a multimethod with a coroutine arg.
                        tc.TypeError(cat("cannot call ", nf->name, " on generic coroutine type"),
                                         *this);
                    }
                }
                if (rlt == LT_BORROW) {
                    auto alt = nf->args.v[sa].lt;
                    assert(alt >= LT_BORROW);
                    rlt = alt;
                }
                break;
            }
            case NF_ANYVAR:
                type = ret.type->t == V_VECTOR ? tc.st.Wrap(tc.NewTypeVar(), V_VECTOR)
                                                  : tc.NewTypeVar();
                assert(rlt == LT_KEEP);
                break;
        }
        type = tc.ActualBuiltinType(ret.fixed_len, type, ret.flags,
                                    !i && Arity() ? children[0] : nullptr, nf, false,
                                    0, *this);
        if (!IsRefNilVar(type->t)) rlt = LT_ANY;
        if (nf->retvals.v.size() > 1) {
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
}

void Call::TypeCheckSpecialized(TypeChecker &tc, size_t reqret) {
    sf = tc.PreSpecializeFunction(sf);
    exptype = tc.TypeCheckCall(sf, this, sf, *this, reqret, vtable_idx);
    lt = sf->ltret;
}

Node *FunRef::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    sf = tc.PreSpecializeFunction(sf);
    exptype = &sf->thistype;
    lt = LT_ANY;
    return this;
}

Node *DynCall::TypeCheck(TypeChecker &tc, size_t reqret) {
    tc.UpdateCurrentSid(sid);
    tc.TypeCheckId(sid);
    //if (sid->type->IsFunction()) sid->type = &tc.PreSpecializeFunction(sid->type->sf)->thistype;
    tc.TypeCheckList(this, false, 1, LT_ANY);
    tie(exptype, lt) = tc.TypeCheckDynCall(sid, this, sf, reqret);
    return this;
}

Node *Return::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    exptype = type_void;
    lt = LT_ANY;
    for (auto isc : reverse(tc.scopes)) {
        if (isc.sf->parent == sf->parent) {
            sf = isc.sf;  // Take specialized version.
            break;
        }
        tc.CheckReturnPast(isc.sf, sf, *this);
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
            ir->sid->sf_def == sf &&
            sf->num_returns == 0 &&
            reqret &&
            sf->body->children.back() == this) {
            reqlt = LT_BORROW;  // Fake that we're cool borrowing this.
            ir->sid->consume_on_last_use = true;  // Don't decref this one when going out of scope.
        }
    }
    tc.TT(child, reqret, reqlt);
    tc.DecBorrowers(child->lt, *this);
    if (sf == tc.st.toplevel) {
        // return from program
        if (child->exptype->NumValues() > 1)
            tc.TypeError("cannot return multiple values from top level", *this);
    }
    auto nsf = tc.TopScope(tc.named_scopes);
    if (nsf != sf) {
        // This is a non-local "return from".
        if (!sf->typechecked)
            tc.parser.Error("return from " + sf->parent->name +
                            " called out of context", this);
    }
    auto never_returns = tc.NeverReturns(child);
    if (never_returns && make_void && sf->num_returns) {
        // A return with other returns inside of it that always bypass this return,
        // so should not contribute to return types.
        assert(child->exptype->t == V_VOID || child->exptype->t == V_VAR);
        return this;
    }
    if (never_returns && sf->reqret && sf->parent->anonymous) {
        // A return to the immediately enclosing anonymous function that needs to return a value
        // but is bypassed.
        tc.RetVal(child->exptype, sf, *this);  // If it's a variable, bind it.
        return this;
    }
    if (!Is<DefaultVal>(child)) {
        if (auto mrs = Is<MultipleReturn>(child)) {
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
    return this;
}

Node *TypeAnnotation::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    exptype = giventype;
    lt = LT_ANY;
    return this;
}

Node *IsType::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    tc.NoStruct(*child, "is");  // FIXME
    tc.DecBorrowers(child->lt, *this);
    exptype = &tc.st.default_bool_type->thistype;
    lt = LT_ANY;
    return this;
}

Node *Constructor::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TypeCheckList(this, false, 1, LT_KEEP);
    exptype = giventype;
    if (exptype.Null()) {
        if (Arity()) {
            // No type was specified.. first find union of all elements.
            TypeRef u(nullptr);
            for (auto c : children) {
                u = u.Null() ? c->exptype : tc.Union(u, c->exptype, true, c);
            }
            exptype = tc.st.Wrap(u, V_VECTOR);
            tc.StorageType(exptype, *this);
        } else {
            // special case for empty vectors
            exptype = tc.st.Wrap(tc.NewTypeVar(), V_VECTOR);
        }
    }
    if (IsUDT(exptype->t)) {
        // We have to check this here, since the parser couldn't check this yet.
        if (exptype->udt->fields.v.size() < children.size())
            tc.TypeError("too many initializers for: " + exptype->udt->name, *this);
        auto udt = tc.FindStructSpecialization(exptype->udt, this);
        exptype = &udt->thistype;
    }
    for (auto [i, c] : enumerate(children)) {
        TypeRef elemtype = IsUDT(exptype->t) ? exptype->udt->fields.v[i].type
                                             : exptype->Element();
        tc.SubType(c, elemtype, tc.ArgName(i), *this);
    }
    lt = LT_KEEP;
    return this;
}

void Dot::TypeCheckSpecialized(TypeChecker &tc, size_t /*reqret*/) {
    tc.AdjustLifetime(children[0], LT_BORROW);
    tc.DecBorrowers(children[0]->lt, *this);  // New borrow created below.
    auto stype = children[0]->exptype;
    if (!IsUDT(stype->t))
        tc.TypeError("class/struct", stype, *this, "object");
    auto udt = stype->udt;
    auto fieldidx = udt->Has(fld);
    if (fieldidx < 0)
        tc.TypeError("type " + udt->name + " has no field named " + fld->name, *this);
    exptype = udt->fields.v[fieldidx].type;
    FlowItem fi(*this, exptype);
    if (fi.IsValid()) exptype = tc.UseFlow(fi);
    lt = tc.PushBorrow(this);
    //lt = children[0]->lt;  // Also LT_BORROW, also depending on the same variable.
}

Node *Indexing::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(object, 1, LT_BORROW);
    tc.TT(index, 1, LT_BORROW);
    tc.DecBorrowers(index->lt, *this);
    auto vtype = object->exptype;
    if (vtype->t != V_VECTOR &&
        vtype->t != V_STRING &&
        (!IsStruct(vtype->t) || !vtype->udt->sametype->Numeric()))
        tc.TypeError("vector/string/numeric struct", vtype, *this, "container");
    auto itype = index->exptype;
    switch (itype->t) {
        case V_INT:
            exptype = vtype->t == V_VECTOR
                ? vtype->Element()
                : (IsUDT(vtype->t) ? vtype->udt->sametype : type_int);
            break;
        case V_STRUCT_S: {
            if (vtype->t != V_VECTOR)
                tc.TypeError("multi-dimensional indexing on non-vector", *this);
            auto &udt = *itype->udt;
            exptype = vtype;
            for (auto &field : udt.fields.v) {
                if (field.type->t != V_INT)
                    tc.TypeError("int field", field.type, *this, "index");
                if (exptype->t != V_VECTOR)
                    tc.TypeError("nested vector", exptype, *this, "container");
                exptype = exptype->Element();
            }
            break;
        }
        default: tc.TypeError("int/struct of int", itype, *this, "index");
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

Node *CoClosure::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    exptype = type_function_cocl;
    lt = LT_ANY;
    return this;
}

Node *CoRoutine::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(call, 1, LT_KEEP);
    tc.NoStruct(*call, "coroutine call return value");  // FIXME
    if (auto fc = Is<Call>(call)) {
        auto sf = fc->sf;
        assert(sf->iscoroutine);
        auto ct = tc.st.NewType();
        *ct = Type(V_COROUTINE, sf);
        exptype = ct;
    } else {
        tc.TypeError("coroutine constructor must be regular function call", *call);
    }
    lt = LT_KEEP;
    return this;
}

Node *TypeOf::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    tc.TT(child, 1, LT_BORROW);
    tc.DecBorrowers(child->lt, *this);
    exptype = type_typeid;
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
    tc.TypeCheckList(this, false, 1, LT_ANY);
    exptype = tc.NewTuple(children.size());
    for (auto [i, mrc] : enumerate(children))
        exptype->Set(i, mrc->exptype.get(), mrc->lt);
    lt = LT_MULTIPLE;
    return this;
}

Node *EnumRef::TypeCheck(TypeChecker & /*tc*/, size_t /*reqret*/) {
    return this;
}

Node *UDTRef::TypeCheck(TypeChecker &tc, size_t /*reqret*/) {
    for (auto [i, f] : enumerate(udt->fields.v)) {
        if (f.defaultval && f.type->t == V_ANY) {
            // FIXME: would be good to not call TT here generically but instead have some
            // specialized checking, just in case TT has a side effect.
            tc.TT(f.defaultval, 1, LT_ANY);
            tc.DecBorrowers(f.defaultval->lt, *this);
            f.defaultval->lt = LT_UNDEF;
            f.type = f.defaultval->exptype;
        }
        // FIXME: this is a temp limitation, remove.
        if (udt->thistype.t == V_STRUCT_R && i &&
            IsRefNil(f.type->t) != IsRefNil(udt->fields.v[0].type->t))
            tc.TypeError("structs fields must be either all scalar or all references: " +
                         udt->name, *this);
    }
    if (!udt->ComputeSizes())
        tc.TypeError("structs cannot be self-referential: " + udt->name, *this);
    exptype = type_void;
    lt = LT_ANY;
    return this;
}

Node *Coercion::TypeCheck(TypeChecker &tc, size_t reqret) {
    // These have been added by another specialization. We could check if they still apply, but
    // even more robust is just to remove them, and let them be regenerated if need be.
    tc.TT(child, reqret, LT_ANY);
    return tc.DeleteCoercion(this);
}

bool And::ConstVal(TypeChecker &tc, Value &val) const {
    return left->ConstVal(tc, val) && (!val.True() || right->ConstVal(tc, val));
}

bool Or::ConstVal(TypeChecker &tc, Value &val) const {
    return left->ConstVal(tc, val) && (val.True() || right->ConstVal(tc, val));
}

bool Not::ConstVal(TypeChecker &tc, Value &val) const {
    auto isconst = child->ConstVal(tc, val);
    val = Value(!val.True());
    return isconst;
}

bool IsType::ConstVal(TypeChecker &tc, Value &val) const {
    if (child->exptype == giventype || giventype->t == V_ANY) {
        val = Value(true);
        return true;
    }
    if (!tc.ConvertsTo(giventype, child->exptype, false)) {
        val = Value(false);
        return true;
    }
    // This means it is always a reference type, since int/float/function don't convert
    // into anything without coercion.
    return false;
}

bool EnumCoercion::ConstVal(TypeChecker &tc, Value &val) const {
    return child->ConstVal(tc, val);
}

}  // namespace lobster
