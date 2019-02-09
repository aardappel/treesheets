
namespace lobster {

struct FlowItem {
    // For now, only: ident ( . field )*.
    const SpecIdent *sid;
    vector<SharedField *> derefs;
    TypeRef old, now;
    FlowItem(SpecIdent *_sid) : sid(_sid), old(sid->type), now(sid->type) {}
    FlowItem(const Node &n, TypeRef type) {
        auto t = &n;
        while (auto dot = Is<Dot>(t)) {
            derefs.insert(derefs.begin(), dot->fld);
            t = dot->child;
        }
        auto idr = Is<IdentRef>(t);
        if (idr) {
            sid = idr->sid;
            old = n.exptype; //idr->sid->type;
            now = type;
        } else {
            sid = nullptr;
        }
    }
    bool IsValid() { return sid; }
	bool DerefsEqual(const FlowItem &o) {
		if (derefs.size() != o.derefs.size()) return false;
		for (auto &shf : derefs) if (shf != o.derefs[&shf - &derefs[0]]) return false;
		return true;
	}
};

struct TypeChecker {
    Parser &parser;
    SymbolTable &st;
    struct Scope { SubFunction *sf; const Node *call_context; };
    vector<Scope> scopes, named_scopes;
    vector<FlowItem> flowstack;

    TypeChecker(Parser &_p, SymbolTable &_st) : parser(_p), st(_st) {
        // FIXME: this is unfriendly.
        if (!st.RegisterDefaultVectorTypes())
            TypeError("cannot find standard vector types (include stdtype.lobster)", *parser.root);
        for (auto &struc : st.structtable) {
            if (!struc->generic) ComputeStructSameType(struc);
            if (struc->superclass) for (auto &field : struc->fields.v) {
                // If this type refers to the super struct type, make it refer to this type instead.
                // There may be corner cases where this is not what you want, but generally you do.
                PromoteStructIdx(field.type, struc->superclass, struc);
            }
        }
        TypeCheckList(parser.root, true);
        CleanUpFlow(0);
        assert(!scopes.size());
        assert(!named_scopes.size());
        Stats();
    }

    // Needed for any sids in cloned code.
    void UpdateCurrentSid(SpecIdent *&sid) { sid = sid->Current(); }
    void RevertCurrentSid(SpecIdent *&sid) { sid->Current() = sid; }

    void ComputeStructSameType(Struct *struc) {
        // NOTE: all users of sametype will only act on it if it is numeric, since 
        // otherwise it would a scalar field to become any without boxing.
        // Much of the implementation relies on these being 2-4 component vectors, so
        // deny this functionality to any other structs.
        if (struc->fields.size() >= 2 && struc->fields.size() <= 4) {
            TypeRef sametype = struc->fields.v[0].type;
            for (size_t i = 1; i < struc->fields.size(); i++) {
                // FIXME: Can't use Union here since it will bind variables
                //sametype = Union(sametype, struc->fields[i].type, false);
                // use simplified alternative:
                if (!ExactType(struc->fields.v[i].type, sametype)) sametype = type_any;
            }
            struc->sametype = sametype;
        }
    }

    void PromoteStructIdx(TypeRef &type, const Struct *olds, const Struct *news) {
        auto u = type;
        while (u->Wrapped()) u = u->Element();
        if (u->t == V_STRUCT && u->struc == olds) type = PromoteStructIdxRec(type, news);
    }

    TypeRef PromoteStructIdxRec(TypeRef type, const Struct *news) {
        return type->Wrapped()
            ? PromoteStructIdxRec(type->sub, news)->Wrap(st.NewType(), type->t)
            : &news->thistype;
    }

    string TypedArg(const GenericArgs &args, size_t i, bool withtype = true) {
        string s;
        s += args.GetName(i);
        if (args.GetType(i)->type->t != V_ANY && withtype)
            s += ":" + TypeName(args.GetType(i)->type);
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

    string Signature(const Struct &struc) {
        return struc.name + Signature(struc.fields);
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
        size_t i = 0;
        for (auto &freevar : sf.freevars.v) {
            if (freevar.type->t != V_FUNCTION &&
                !freevar.sid->id->static_constant &&
                (!already_seen || already_seen->find(freevar.sid->id) == already_seen->end())) {
                s += TypedArg(sf.freevars, i) + " ";
                if (already_seen) already_seen->insert(freevar.sid->id);
            }
            i++;
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

    void TypeError(string_view required, TypeRef got, const Node &n, string_view argname = "",
                   string_view context = "") {
        TypeError(cat("\"", (context.size() ? context : n.Name()), "\" ",
                      (argname.size() ? "(" + argname + " argument) " : ""),
                      "requires type: ", required, ", got: ", TypeName(got)), n);
    }

    void TypeError(string err, const Node &n) {
        set<Ident *> already_seen;
        for (auto it = scopes.rbegin(); it != scopes.rend() - 1 /* -1 == __top_level_expression */;
             ++it) {
            auto &scope = *it;
            err += "\n  in " + parser.lex.Location(scope.call_context->line) + ": ";
            err += SignatureWithFreeVars(*scope.sf, &already_seen);
            for (auto dl : scope.sf->body->children) {
                if (auto def = Is<Define>(dl)) {
                    for (auto sid : def->sids) {
                        err += ", " + sid->id->name + ":" + TypeName(sid->type);
                    }
                }
            }
        }
        parser.Error(err, &n);
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

    bool ConvertsTo(TypeRef type, TypeRef sub, bool coercions, bool unifications = true,
                    bool genericany = false) {
        if (sub == type) return true;
        if (type->t == V_VAR) {
            if (unifications) UnifyVar(sub, type);
            return true;
        }
        switch (sub->t) {
            case V_ANY:       return genericany || coercions || !IsUnBoxed(type->t);
            case V_VAR:       UnifyVar(type, sub); return true;
            case V_FLOAT:     return type->t == V_INT && coercions;
            case V_INT:       return type->t == V_TYPEID && coercions;
            case V_STRING:    return false; //coercions;
            case V_FUNCTION:  return type->t == V_FUNCTION && !sub->sf;
            case V_NIL:       return (type->t == V_NIL &&
                                      ConvertsTo(type->Element(), sub->Element(), false,
                                                 unifications, genericany)) ||
                                     (!type->Numeric() &&
                                      ConvertsTo(type, sub->Element(), false,
                                                 unifications, genericany)) ||
                                     (type->Numeric() && type == sub->Element());  // For builtins.
            case V_VECTOR:    return (type->t == V_VECTOR &&
                                      ConvertsTo(type->Element(), sub->Element(), false,
                                                 unifications, genericany));
            case V_STRUCT:    return type->t == V_STRUCT &&
                                     st.IsSuperTypeOrSame(sub->struc, type->struc);
            case V_COROUTINE: return type->t == V_COROUTINE &&
                                     (sub->sf == type->sf ||
                                      (!sub->sf && ConvertsTo(type->sf->coresumetype,
                                                              NewNilTypeVar(), false)));
            default:          return false;
        }
    }

    TypeRef Union(const Node *a, const Node *b, bool coercions) {
         return Union(a->exptype, b->exptype, coercions);
    }
    TypeRef Union(TypeRef at, TypeRef bt, bool coercions) {
        if (ConvertsTo(at, bt, coercions)) return bt;
        if (ConvertsTo(bt, at, coercions)) return at;
        if (at->t == V_VECTOR && bt->t == V_VECTOR) {
            auto et = Union(at->Element(), bt->Element(), false);
            return et->Wrap(st.NewType());
        }
        if (at->t == V_STRUCT && bt->t == V_STRUCT) {
            auto sstruc = st.CommonSuperType(at->struc, bt->struc);
            if (sstruc) return &sstruc->thistype;
        }
        return type_any;
    }

    bool ExactType(TypeRef a, TypeRef b) {
        return a == b;  // Not inlined for documentation purposes.
    }

    void MakeString(Node *&a) {
        assert(a->exptype->t != V_STRING);
        MakeAny(a);  // Could instead make a T_I2S etc, but string() goes thru any also.
        a = new ToString(a->line, a);
        a->exptype = type_string;
    }

    void MakeAny(Node *&a) {
        if (a->exptype->t == V_FUNCTION) TypeError("cannot convert a function value to any", *a);
        if (a->exptype->t == V_TYPEID) TypeError("cannot convert a typeid to any", *a);
        a = new ToAny(a->line, a);
        a->exptype = type_any;
    }

    void MakeNil(Node *&a) {
        a = new ToNil(a->line, a);
        a->exptype = type_any;
    }

    void MakeBool(Node *&a) {
        if (a->exptype->t != V_INT) a = new ToBool(a->line, a);
        a->exptype = type_int;
    }

    void MakeInt(Node *&a) {
        a = new ToInt(a->line, a);
        a->exptype = type_int;
    }

    void MakeFloat(Node *&a) {
        a = new ToFloat(a->line, a);
        a->exptype = type_float;
    }

    void SubTypeLR(TypeRef sub, BinOp &n) {
        SubType(n.left, sub, "left", n);
        SubType(n.right, sub, "right", n);
    }

    void SubType(Node *&a, TypeRef sub, string_view argname, const Node &context) {
        SubType(a, sub, argname, context.Name());
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
            case V_ANY:
                MakeAny(a);
                return;
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
                    if (sf->args.v.size() != ss->args.v.size()) break;
                    int i = 0;
                    for (auto &arg : sf->args.v) {
                        // Specialize to the function type, if requested.
                        if (!sf->typechecked && arg.flags & AF_GENERIC) {
                            arg.type = ss->args.v[i].type;
                        }
                        // Note this has the args in reverse: function args are contravariant.
                        if (!ConvertsTo(ss->args.v[i].type, arg.type, false))
                            goto error;
                        i++;
                    }
                    if (sf->typechecked) {
                        if (sf->reqret != ss->reqret) goto error;
                    } else {
                        sf->reqret = ss->reqret;
                    }
                    TypeCheckFunctionDef(*sf, sf->body);
                    // Covariant again.
                    if (sf->returntypes.size() != ss->returntypes.size() ||
                        !ConvertsTo(sf->returntypes[0], ss->returntypes[0], false))
                            break;
                    // Parser only parses one ret type for function types.
                    assert(ss->returntypes.size() == 1);
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
                  string_view context = string_view()) {
        if (!ConvertsTo(type, sub, false))
            TypeError(TypeName(sub), type, n, argname, context);
    }

    bool MathCheckVector(TypeRef &type, Node *&left, Node *&right) {
        TypeRef ltype = left->exptype;
        TypeRef rtype = right->exptype;
        // Special purpose check for vector * scalar etc.
        if (ltype->t == V_STRUCT && rtype->Numeric()) {
            auto etype = ltype->struc->sametype;
            if (etype->Numeric()) {
                if (etype->t == V_INT) {
                    // Don't implicitly convert int vectors to float.
                    if (rtype->t == V_FLOAT) return false;
                } else {
                    if (rtype->t == V_INT) SubType(right, type_float, "right", *right);
                }
                type = &ltype->struc->thistype;
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
            if (!type->Numeric() && type->t != V_VECTOR && type->t != V_STRUCT) {
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
                            MakeString(n.right);
                            // Make sure the overal type is string.
                            type = type_string;
                            unionchecked = true;
                        }
                    } else if (rtype->t == V_STRING && ltype->t != V_STRING && typechangeallowed) {
                        // Only if not in a +=
                        MakeString(n.left);
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
                      n.Name() +
                      "\" on " +
                      TypeName(n.left->exptype) +
                      " and " +
                      TypeName(n.right->exptype), n);
        }
    }

    Node *TypeCheckMathOp(BinOp &n) {
        n.left = n.left->TypeCheck(*this, true);
        n.right = n.right->TypeCheck(*this, true);
        n.exptype = Union(n.left, n.right, true);
        bool unionchecked = false;
        MathError(n.exptype, n, unionchecked, true);
        if (!unionchecked) SubTypeLR(n.exptype, n);
        return &n;
    }

    Node *TypeCheckMathOpEq(BinOp &n) {
        n.left = n.left->TypeCheck(*this, true);
        n.right = n.right->TypeCheck(*this, true);
        CheckReadOnly(*n.left);
        n.exptype = n.left->exptype;
        if (!MathCheckVector(n.exptype, n.left, n.right)) {
            bool unionchecked = false;
            MathError(n.exptype, n, unionchecked, false);
            if (!unionchecked) SubType(n.right, n.exptype, "right", n);
        }
        return &n;
    }

    Node *TypeCheckComp(BinOp &n) {
        n.left = n.left->TypeCheck(*this, true);
        n.right = n.right->TypeCheck(*this, true);
        n.exptype = type_int;
        auto u = Union(n.left, n.right, true);
        if (!u->Numeric() && u->t != V_STRING) {
            // FIXME: rather than nullptr, these TypeError need to figure out which side
            // caused the error much like MathError
            if (Is<Equal>(&n) || Is<NotEqual>(&n)) {
                // pointer comparison
                if (u->t != V_VECTOR && u->t != V_STRUCT && u->t != V_NIL)
                    TypeError("numeric / string / vector / struct", u, n);
            } else {
                // comparison vector op
                if (u->t == V_STRUCT && u->struc->sametype->Numeric()) {
                    n.exptype = st.default_int_vector_types[0][u->struc->fields.size()];
                } else if (MathCheckVector(n.exptype, n.left, n.right)) {
                    n.exptype = st.default_int_vector_types[0][n.exptype->struc->fields.size()];
                    // Don't do SubTypeLR since type already verified and `u` not
                    // appropriate anyway.
                    return &n;
                } else {
                    TypeError("numeric / string / numeric struct", u, n);
                }
            }
        }
        SubTypeLR(u, n);
        return &n;
    }

    Node *TypeCheckBitOp(BinOp &n) {
        n.left = n.left->TypeCheck(*this, true);
        n.right = n.right->TypeCheck(*this, true);
        SubTypeLR(type_int, n);
        n.exptype = type_int;
        return &n;
    }

    Node *TypeCheckPlusPlus(Unary &n) {
        n.child = n.child->TypeCheck(*this, true);
        CheckReadOnly(*n.child);
        n.exptype = n.child->exptype;
        if (!n.exptype->Numeric())
            TypeError("numeric", n.exptype, n);
        return &n;
    }

    SubFunction *TopScope(vector<Scope> &_scopes) {
        return _scopes.empty() ? nullptr : _scopes.back().sf;
    }

    // Only one of n or type is specified. Returns new n if specified.
    Node *RetVal(Node *n, TypeRef type, SubFunction *sf, size_t i, Node &context) {
        if (!sf) return n;
        if (i >= sf->returntypes.size()) {
            if (sf->fixedreturntype)
                TypeError("number of returned value(s) must correspond to declared return type",
                          context);
            assert(i == sf->returntypes.size());
            sf->returntypes.push_back(!type.Null() ? type : n->exptype);
        } else if (sf->reqret) {
            auto argname = "return value";
            if (!type.Null()) SubTypeT(type, sf->returntypes[i], context, argname);
            else if (n) SubType(n, sf->returntypes[i], argname, context);
            // FIXME: this allows "return" followed by "return 1" ?
            else SubTypeT(type_any, sf->returntypes[i], context, argname);
        } else {
            // The caller doesn't want return values.
        }
        return n;
    }

    void TypeCheckFunctionDef(SubFunction &sf, const Node *call_context) {
        if (sf.typechecked) return;
        Output(OUTPUT_DEBUG, "function start: ", SignatureWithFreeVars(sf, nullptr));
        Scope scope;
        scope.sf = &sf;
        scope.call_context = call_context;
        scopes.push_back(scope);
        if (!sf.parent->anonymous) named_scopes.push_back(scope);
        sf.typechecked = true;
        for (auto &fv : sf.freevars.v) UpdateCurrentSid(fv.sid);
        for (auto &dyn : sf.dynscoperedefs.v) UpdateCurrentSid(dyn.sid);
        auto backup_vars = [&](ArgVector &in, ArgVector &backup) {
            int i = 0;
            for (auto &arg : in.v) {
                // Need to not overwrite nested/recursive calls. e.g. map(): map(): ..
                backup.v[i].sid = arg.sid->Current();
                arg.sid->type = arg.type;
                RevertCurrentSid(arg.sid);
                i++;
            }
        };
        auto backup_args = sf.args; backup_vars(sf.args, backup_args);
        auto backup_locals = sf.locals; backup_vars(sf.locals, backup_locals);
        // FIXME: this would not be able to typecheck recursive functions with multiret.
        if (!sf.fixedreturntype) sf.returntypes[0] = NewTypeVar();
        sf.coresumetype = sf.iscoroutine ? NewTypeVar() : type_any;
        auto start_promoted_vars = flowstack.size();
        TypeCheckList(sf.body, true, sf.reqret);
        CleanUpFlow(start_promoted_vars);
        auto &last = sf.body->children.back();
        auto ret = Is<Return>(last);
        if (!ret || ret->subfunction_idx != sf.idx)
            last = RetVal(last, nullptr, &sf, 0, *last);
        for (auto &back : backup_args.v)   RevertCurrentSid(back.sid);
        for (auto &back : backup_locals.v) RevertCurrentSid(back.sid);
        for (auto type : sf.returntypes) if (sf.reqret && type->HasVariable()) {
            // If this function return something with a variable in it, then it likely will get
            // bound by the caller. If the function then gets reused without specialization, it will
            // get the wrong return type, so we force specialization for subsequent calls of this
            // function. FIXME: check in which cases this is typically true, since its expensive
            // if done without reason.
            sf.mustspecialize = true;
        }
        if (!sf.parent->anonymous) named_scopes.pop_back();
        scopes.pop_back();
        Output(OUTPUT_DEBUG, "function end ", Signature(sf), " returns ",
                             TypeName(sf.returntypes[0]));
    }

    Struct *FindStructSpecialization(Struct *given, const Constructor *cons) {
        // This code is somewhat similar to function specialization, but not similar enough to
        // share. If they're all typed, we bail out early:
        if (!given->generic) return given;
        auto head = given->first;
        assert(cons->Arity() == head->fields.size());
        // Now find a match:
        auto struc = head->next;
        for (; struc; struc = struc->next) {
            int i = 0;
            for (auto &arg : cons->children) {
                auto &field = struc->fields.v[i++];
                if (field.flags & AF_GENERIC && !ExactType(arg->exptype, field.type)) goto fail;
            }
            return struc;  // Found a match.
            fail:;
        }
        string s;
        for (auto &arg : cons->children) s += " " + TypeName(arg->exptype);
        TypeError("no specialization of " + given->first->name + " matches these types:" + s,
                  *cons);
        return nullptr;
    }

    void CheckIfSpecialization(Struct *spec_struc, TypeRef given, const Node &n,
                               string_view argname, string_view req = string_view(),
                               bool subtypeok = false, string_view context = string_view()) {
        auto givenu = given->UnWrapped();
        if (given->t != V_STRUCT ||
            (!spec_struc->IsSpecialization(givenu->struc) &&
             (!subtypeok || !st.IsSuperTypeOrSame(spec_struc, givenu->struc)))) {
            TypeError(req.data() ? req : spec_struc->name, given, n, argname, context);
        }
    }

    void CheckGenericArg(TypeRef otype, TypeRef argtype, string_view argname, const Node &n,
                         string_view context) {
        // Check if argument is a generic struct type, or wrapped in vector/nilable.
        if (otype->t != V_ANY) {
            auto u = otype->UnWrapped();
            assert(u->t == V_STRUCT);
            if (otype->EqNoIndex(*argtype)) {
                CheckIfSpecialization(u->struc, argtype, n, argname, TypeName(otype), true,
                                      context);
            } else {
                // This likely generates either an error, or contains an unbound var that will get
                // bound.
                SubTypeT(argtype, otype, n, argname, context);
                //TypeError(TypeName(otype), argtype, n, argname, context);
            }
        }
    }

    bool FreeVarsSameAsCurrent(SubFunction *sf, bool prespecialize) {
        for (auto &freevar : sf->freevars.v) {
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

    SubFunction *CloneFunction(SubFunction *csf) {
        Output(OUTPUT_DEBUG, "cloning: ", csf->parent->name);
        auto sf = st.CreateSubFunction();
        sf->SetParent(*csf->parent, csf->parent->subf);
        // Any changes here make sure this corresponds what happens in Inline() in the optimizer.
        st.CloneIds(*sf, *csf);
        sf->body = (List *)csf->body->Clone();
        sf->freevarchecked = true;
        sf->logvarcallgraph = csf->logvarcallgraph;
        return sf;
    }

    TypeRef TypeCheckMatchingCall(SubFunction *sf, List *call_args, SubFunction *&chosen,
                                  Node *call_context) {
        // Here we have a SubFunction witch matching specialized types.
        sf->numcallers++;
        Function &f = *sf->parent;
        size_t i = 0;
        // See if this is going to be a coroutine.
        for (auto &c : call_args->children) if (i < f.nargs()) /* see below */ {
            if (Is<CoClosure>(c))
                sf->iscoroutine = true;
            i++;
        }
        if (!f.istype) TypeCheckFunctionDef(*sf, call_context);
        // Finally check all the manually typed args. We do this after checking the function
        // definition, since SubType below can cause specializations of the current function
        // to be typechecked with strongly typed function value arguments.
        i = 0;
        for (auto &c : call_args->children) if (i < f.nargs()) /* see below */ {
            auto &arg = sf->args.v[i];
            if (!(arg.flags & AF_GENERIC))
                SubType(c, arg.type, ArgName(i), f.name);
            i++;
        }
        chosen = sf;
        for (auto &freevar : sf->freevars.v) {
            // New freevars may have been added during the function def typecheck above.
            // In case their types differ from the flow-sensitive value at the callsite (here),
            // we want to override them.
            freevar.type = freevar.sid->Current()->type;
        }
        return sf->returntypes[0];
    }

    TypeRef TypeCheckCall(SubFunction *csf, List *call_args, SubFunction *&chosen,
                          Node *call_context, bool reqret) {
        Function &f = *csf->parent;
        if (f.multimethod) {
            if (!f.subf->numcallers) {
                // Simplistic: typechecked with actual argument types.
                // Should attempt static picking as well, if static pick succeeds, specialize.
                for (auto sf = f.subf; sf; sf = sf->next) {
                    sf->numcallers++;
                    TypeCheckFunctionDef(*sf, call_context);
                }
                // FIXME: Don't need to do this on every call.
                f.multimethodretval = f.subf->returntypes[0];
                for (auto sf = f.subf->next; sf; sf = sf->next) {
                    // FIXME: Lift these limits?
                    if (sf->returntypes.size() != 1)
                        TypeError("multi-methods can currently return only 1 value.",
                                  *call_context);
                    if (!ConvertsTo(sf->returntypes[0], f.multimethodretval, false))
                        TypeError("multi-method " + f.name +
                                  " has inconsistent return value types: " +
                                  TypeName(sf->returntypes[0]) + " and " +
                                  TypeName(f.multimethodretval), *call_args);
                }
            }
            return f.multimethodretval;
        } else if (f.istype) {
            // Function types are always fully typed.
            return TypeCheckMatchingCall(csf, call_args, chosen, call_context);
        } else {
            if (csf->logvarcallgraph) {
                // Mark call-graph up to here as using logvars, if it hasn't been already.
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                    if (it->sf->logvarcallgraph) break;
                    it->sf->logvarcallgraph = true;
                }
            }
            // Check if we need to specialize: generic args, free vars and need of retval
            // must match previous calls.
            SubFunction *sf = csf;
            if (sf->typechecked) {
                // Check if any existing specializations match.
                for (sf = f.subf; sf; sf = sf->next) {
                    if (sf->typechecked && !sf->mustspecialize && !sf->logvarcallgraph) {
                        size_t i = 0;
                        // We check against f.nargs because HOFs are allowed to call a function
                        // value with more arguments than it needs (if we're called from
                        // TypeCheckDynCall). Optimizer always removes these.
                        for (auto c : call_args->children) if (i < f.nargs()) {
                            auto &arg = sf->args.v[i++];
                            if (arg.flags & AF_GENERIC &&
                                !ExactType(c->exptype, arg.type)) goto fail;
                        }
                        if (FreeVarsSameAsCurrent(sf, false) && reqret == sf->reqret) {
                            // This function can be reused.
                            // But first make sure to add any freevars this call caused to be
                            // added to its parents also to the current parents, just in case
                            // they're different.
                            for (auto &fv : sf->freevars.v) CheckFreeVariable(*fv.sid);
                            return TypeCheckMatchingCall(sf, call_args, chosen, call_context);
                        }
                        fail:;
                    }
                }
                // No fit. Specialize existing function, or its clone.
                sf = CloneFunction(csf);
            }
            // Now specialize.
            sf->reqret = reqret;
            size_t i = 0;
            for (auto c : call_args->children) if (i < f.nargs()) /* see above */ {
                auto &arg = sf->args.v[i];
                if (arg.flags & AF_GENERIC) {
                    arg.type = c->exptype;  // Specialized to arg.
                    CheckGenericArg(f.orig_args.v[i].type, arg.type, arg.sid->id->name,
                                    *c, f.name);
                    Output(OUTPUT_DEBUG, "arg: ", arg.sid->id->name, ":", TypeName(arg.type));
                }
                i++;
            }
            // This must be the correct freevar specialization.
            assert(!f.anonymous || sf->freevarchecked);
            assert(!sf->freevars.v.size());
            Output(OUTPUT_DEBUG, "specialization: ", Signature(*sf));
            return TypeCheckMatchingCall(sf, call_args, chosen, call_context);
        }
    }

    SubFunction *PreSpecializeFunction(SubFunction *hsf) {
        // Don't pre-specialize named functions, because this is not their call-site.
        if (!hsf->parent->anonymous) return hsf;
        hsf = hsf->parent->subf;
        auto sf = hsf;
        if (sf->freevarchecked) {
            // See if there's an existing match.
            for (; sf; sf = sf->next) if (sf->freevarchecked) {
                if (FreeVarsSameAsCurrent(sf, true)) return sf;
            }
            sf = CloneFunction(hsf);
        } else {
            // First time this function has ever been touched.
            sf->freevarchecked = true;
        }
        assert(!sf->freevars.v.size());
        // Output without arg types, since those are yet to be overwritten.
        Output(OUTPUT_DEBUG, "pre-specialization: ",
               SignatureWithFreeVars(*sf, nullptr, false));
        return sf;
    }

    TypeRef TypeCheckDynCall(SpecIdent *fval, List *args, SubFunction *&fspec, bool reqret) {
        auto &ftype = fval->type;
        auto nargs = args->Arity();
        if (ftype->IsFunction()) {
            // We can statically typecheck this dynamic call. Happens for almost all non-escaping
            // closures.
            auto sf = ftype->sf;
            if (nargs < sf->parent->nargs())
                TypeError("function value called with too few arguments", *args);
            // In the case of too many args, TypeCheckCall will ignore them (and optimizer will
            // remove them).
            auto type = TypeCheckCall(sf, args, fspec, args, reqret);
            ftype = &fspec->thistype;
            return type;
        } else if (ftype->t == V_YIELD) {
            // V_YIELD must have perculated up from a coroutine call.
            if (nargs > 1)
                TypeError("coroutine yield call must at most one argument", *args);
            for (auto scope = named_scopes.rbegin(); scope != named_scopes.rend(); ++scope) {
                auto sf = scope->sf;
                if (!sf->iscoroutine) continue;
                // What yield returns to returnvalue()
                auto type = args->Arity() ? args->children[0]->exptype : type_any;
                RetVal(nullptr, type, sf, 0, *args);
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
                    for (auto &dyn : ssf->dynscoperedefs.v)
                        sf->coyieldsave.Add(dyn);
                }
                for (auto &cys : sf->coyieldsave.v) UpdateCurrentSid(cys.sid);
                return sf->coresumetype;
            }
            TypeError("yield function called outside scope of coroutine", *args);
            return type_any;
        } else {
            // We have to do this call entirely at runtime. We take any args, and return any.
            // FIXME: the body T_FUN that created this function value hasn't been typechecked
            // at all, meaning its contents is all T_ANY. This is not necessary esp if the function
            // had no args, or all typed args, but we have no way of telling which T_FUN's
            // will end up this way.
            // For now, just error.
            TypeError("function call value has type: " + TypeName(ftype), *args);
            return type_any;
        }
    }

    TypeRef TypeCheckBranch(bool iftrue, const Node *condition, Node *&bodycall,
                            bool reqret) {
        auto flowstart = CheckFlowTypeChanges(iftrue, condition);
        bodycall = bodycall->TypeCheck(*this, reqret);
        CleanUpFlow(flowstart);
        return bodycall->exptype;
    }

    void CheckFlowTypeIdOrDot(const Node &n, TypeRef type) {
        FlowItem fi(n, type);
        if (fi.IsValid()) flowstack.push_back(fi);
    }

    void CheckFlowTypeChangesSub(bool iftrue, const Node *condition) {
        auto type = condition->exptype;
        if (auto c = Is<IsType>(condition)) {
            if (iftrue) CheckFlowTypeIdOrDot(*c->child, c->giventype);
        } else if (auto c = Is<Not>(condition)) {
            CheckFlowTypeChangesSub(!iftrue, c->child);
        } else if (auto c = Is<ToBool>(condition)) {
            CheckFlowTypeChangesSub(iftrue, c->child);
        } else {
            if (iftrue && type->t == V_NIL) CheckFlowTypeIdOrDot(*condition, type->Element());
        }
    }

    void CheckFlowTypeChangesAndOr(bool iftrue, const BinOp *condition) {
        // AND only works for then, and OR only for else.
        if (iftrue == (Is<And>(condition) != nullptr)) {
            // This is a bit clumsy, but allows for a chain of &'s without allowing mixed
            // operators.
            auto tobool = Is<ToBool>(condition->left);
            if (typeid(*condition->left) == typeid(*condition) ||
                (tobool && typeid(*tobool->child) == typeid(*condition))) {
                CheckFlowTypeChanges(iftrue, condition->left);
            } else {
                CheckFlowTypeChangesSub(iftrue, condition->left);
            }
            CheckFlowTypeChangesSub(iftrue, condition->right);
        }
    }

    size_t CheckFlowTypeChanges(bool iftrue, const Node *condition) {
        auto start = flowstack.size();
        if (auto c = Is<Or>(condition)) {
            CheckFlowTypeChangesAndOr(iftrue, c);
        } else if (auto c = Is<And>(condition)) {
            CheckFlowTypeChangesAndOr(iftrue, c);
        } else if (auto c = Is<ToBool>(condition)) {
            CheckFlowTypeChanges(iftrue, c->child);
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
        for (auto it = flowstack.rbegin(); it != flowstack.rend(); ++it) {
            auto &flow = *it;
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
        for (auto it = flowstack.rbegin(); it != flowstack.rend(); ++it) {
            auto &flow = *it;
            if (flow.sid == left.sid &&	flow.DerefsEqual(left)) {
                return flow.now;
            }
        }
        return left.now;
    }

    void CleanUpFlow(size_t start) {
        while (flowstack.size() > start) flowstack.pop_back();
    }

    Node *TypeCheckAndOr(Node *n, bool only_true_type, bool reqret, TypeRef &promoted_type) {
        // only_true_type supports patterns like ((a & b) | c) where the type of a doesn't matter,
        // and the overal type should be the union of b and c.
        // Or a? | b, which should also be the union of a and b.
        if (!Is<And>(n) && !Is<Or>(n)) {
            n = n->TypeCheck(*this, reqret);
        }
        // We check again, since TypeCheck may have removed a coercion op.
        if (!Is<And>(n) && !Is<Or>(n)) {
            promoted_type = n->exptype;
            if (promoted_type->t == V_NIL && only_true_type)
                promoted_type = promoted_type->Element();
            return n;
        }
        auto ao = dynamic_cast<BinOp *>(n);
        assert(ao);
        TypeRef tleft, tright;
        ao->left = TypeCheckAndOr(ao->left, Is<Or>(ao), true, tleft);
        auto flowstart = CheckFlowTypeChanges(Is<And>(ao), ao->left);
        ao->right = TypeCheckAndOr(ao->right, only_true_type, reqret, tright);
        CleanUpFlow(flowstart);
        if (only_true_type && Is<And>(ao)) {
            ao->exptype = tright;
        } else {
            ao->exptype = Union(tleft, tright, false);
            if (ao->exptype->t == V_ANY) {
                // Special case: we may have just merged scalar and reference types, and unlike
                // elsewhere, we don't want to box the scalars to make everything references, since
                // typically they are just tested and thrown away. Instead, we force all values to
                // bools.
                MakeBool(ao->left);
                MakeBool(ao->right);
                ao->exptype = type_int;
            }
        }
        promoted_type = ao->exptype;
        return n;
    }

    void CheckReturnValues(size_t nretvals, size_t i, string_view name, const Node &n) {
        if (nretvals <= i) {
            parser.Error(cat("function ", name, " returns ", nretvals, " values, ",
                             i + 1, " requested"), &n);
        }
    }

    void CheckReadOnly(Node &n) {
        auto dot = Is<Dot>(&n);
        if (dot && dot->child->exptype->t == V_STRUCT &&
            dot->child->exptype->struc->readonly)
            TypeError("cannot write to field of value: " + dot->child->exptype->struc->name, n);
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
            // Since this function may have been cloned since, we also accept any specialization.
            if (sid.id->sf_def == sf ||
                (sid.id->sf_def->parent == sf->parent && !sf->parent->multimethod))
                break;
            // We use the id's type, not the flow sensitive type, just in case there's multiple uses
            // of the var. This will get corrected after the call this is part of.
            if (sf->freevars.Add(Arg(&sid, sid.type, AF_GENERIC))) {
                //Output(OUTPUT_DEBUG, "freevar added: ", id.name, " (", TypeName(id.type),
                //                     ") in ", sf->parent->name);
            }
        }
    }

    void TypeCheckList(List *n, bool onlylast, bool reqret = true) {
        for (auto &c : n->children)
            c = c->TypeCheck(*this, onlylast && c != n->children.back() ? false : reqret);
    }

    TypeRef TypeCheckId(SpecIdent *sid) {
        auto type = sid->type;
        CheckFreeVariable(*sid);
        return type;
    }

    Node *TypeCheckCoercion(Unary &n) {
        // These have been added by another specialization.
        // We could check if they still apply, but even more robust is just to remove them,
        // and let them be regenerated if need be.
        auto c = n.child->TypeCheck(*this, true);
        n.child = nullptr;
        delete &n;
        return c;
    }

    // TODO: Can't do this transform ahead of time, since it often depends upon the input args.
    TypeRef ToVStruct(int flen, TypeRef type, Node *exp, const NativeFun *nf, bool test_overloads,
                      size_t argn, const Node &errorn) {
        // See if we can promote the type to one of the standard vector types
        // (xy/xyz/xyzw).
        if (flen) {
            if (type->t == V_NIL) {
                return ToVStruct(flen, type->sub, exp, nf, test_overloads, argn, errorn)->
                    Wrap(st.NewType(), V_NIL);
            }
            auto etype = exp ? exp->exptype : nullptr;
            auto e = etype;
            size_t i = 0;
            for (auto vt = type; vt->t == V_VECTOR && i < SymbolTable::NUM_VECTOR_TYPE_WRAPPINGS;
                vt = vt->sub) {
                if (vt->sub->Numeric()) {
                    // Check if we allow any vector length.
                    if (!e.Null() && flen == -1 && e->t == V_STRUCT) {
                        flen = (int)e->struc->fields.size();
                    }
                    if (!etype.Null() && flen == -1 && etype->t == V_VAR) {
                        // Special case for "F}?" style types that can be matched against a 
                        // DefaultArg, would be good to solve this more elegantly..
                        // FIXME: don't know arity, but it doesn't matter, so we pick 2..
                        return st.VectorType(vt, i, 2);
                    }
                    if (flen >= 2 && flen <= 4) {
                        if (!e.Null() && e->t == V_STRUCT && (int)e->struc->fields.size() == flen &&
                            e->struc->sametype == vt->sub) {
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
        }
        return type;
    };

    void Stats() {
        if (min_output_level > OUTPUT_INFO) return;
        int origsf = 0, multisf = 0, clonesf = 0;
        size_t orignodes = 0, clonenodes = 0;
        typedef pair<size_t, Function *> Pair;
        vector<Pair> funstats;
        for (auto f : st.functiontable) funstats.push_back({ 0, f });
        for (auto sf : st.subfunctiontable) {
            auto count = sf->body ? sf->body->Count() : 0;
            if (sf->parent->multimethod) {
                multisf++;
                orignodes += count;
            } else if (!sf->next)        {
                origsf++;
                orignodes += count;
            } else {
                clonesf++;
                clonenodes += count;
                funstats[sf->parent->idx].first += count;
            }
        }
        Output(OUTPUT_INFO, "SF count: multi: ", multisf, ", orig: ", origsf, ", cloned: ",
                            clonesf);
        Output(OUTPUT_INFO, "Node count: orig: ", orignodes, ", cloned: ", clonenodes);
        sort(funstats.begin(), funstats.end(),
            [](const Pair &a, const Pair &b) { return a.first > b.first; });
        for (auto &[fsize, f] : funstats) if (fsize > orignodes / 100) {
            auto &pos = f->subf->body->line;
            Output(OUTPUT_INFO, "Most clones: ", f->name, " (", st.filenames[pos.fileidx],
                                ":", pos.line, ") -> ", fsize, " nodes accross ",
                                f->NumSubf() - 1, " clones (+1 orig)");
        }
    }
};

Node *List::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    assert(false);  // Parent calls TypeCheckList.
    return this;
}

Node *Unary::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    assert(false);
    return this;
}

Node *BinOp::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    assert(false);
    return this;
}

Node *Inlined::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    assert(false);  // Generated after type-checker in optimizer.
    return this;
}

Node *Or::TypeCheck(TypeChecker &tc, bool reqret) {
    TypeRef dummy;
    return tc.TypeCheckAndOr(this, false, reqret, dummy);
}

Node *And::TypeCheck(TypeChecker &tc, bool reqret) {
    TypeRef dummy;
    return tc.TypeCheckAndOr(this, false, reqret, dummy);
}

Node *If::TypeCheck(TypeChecker &tc, bool reqret) {
    condition = condition->TypeCheck(tc, true);
    Value cval;
    bool isconst = condition->ConstVal(tc, cval);
    if (!Is<DefaultVal>(falsepart)) {
        if (!isconst) {
            auto tleft = tc.TypeCheckBranch(true, condition, truepart, reqret);
            auto tright = tc.TypeCheckBranch(false, condition, falsepart, reqret);
            // FIXME: this is a bit of a hack. Much better if we had an actual type
            // to signify NORETURN, to be taken into account in more places.
            auto truec = AssertIs<Call>(truepart);
            auto falsec = AssertIs<Call>(falsepart);
            if (Is<Return>(truec->sf->body->children.back())) {
                exptype = tright;
            } else if (Is<Return>(falsec->sf->body->children.back())) {
                exptype = tleft;
            } else {
                exptype = tc.Union(tleft, tright, true);
                // These will potentially make either body from T_CALL into some
                // coercion.
				tc.SubType(truepart, exptype, "then branch", *this);
                tc.SubType(falsepart, exptype, "else branch", *this);
            }
        } else if (cval.True()) {
            // Ignore the else part, optimizer guaranteed to cull it.
            exptype = tc.TypeCheckBranch(true, condition, truepart, reqret);
        } else {
            // Ignore the then part, optimizer guaranteed to cull it.
            exptype = tc.TypeCheckBranch(false, condition, falsepart, reqret);
        }
    } else {
        // No else: this always returns nil.
        if (!isconst || cval.True()) {
            tc.TypeCheckBranch(true, condition, truepart, false);
            tc.MakeNil(truepart);
        }
        // Else constant == false: this if-then will get optimized out entirely, ignore
        // it. Bind the var in T_DEFAULTVAL regardless, since it will stay in the AST.
        falsepart = falsepart->TypeCheck(tc, reqret);
        tc.SubType(falsepart, type_function_nil, "else", *this);
        exptype = type_any;
    }
    return this;
}

Node *While::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    condition = condition->TypeCheck(tc, true);
    auto condc = AssertIs<Call>(condition);
    tc.TypeCheckBranch(true, condc->sf->body->children[0], body, false);
    // Currently always return V_NIL
    exptype = type_any;
    return this;
}

Node *For::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    iter = iter->TypeCheck(tc, true);
    auto itertype = iter->exptype;
    if (itertype->t == V_INT || itertype->t == V_STRING)
        itertype = type_int;
    else if (itertype->t == V_VECTOR)
        itertype = itertype->Element();
    else if (itertype->t == V_STRUCT && itertype->struc->sametype->Numeric())
        itertype = itertype->struc->sametype;
    else tc.TypeError("for can only iterate over int / string / vector / numeric struct, not: " + 
        TypeName(itertype), *this);
    auto bodyc = AssertIs<Call>(body);
    auto &args = bodyc->children;
    if (args.size()) {
        args[0]->exptype = itertype;  // ForLoopElem
        if (args.size() > 1) args[1]->exptype = type_int;  // ForLoopCounter
    }
    body = body->TypeCheck(tc, false);
    // Currently always return V_NIL
    exptype = type_any;
    return this;
}

Node *ForLoopElem::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    // Ignore, already been assigned a type in For.
    return this;
}

Node *ForLoopCounter::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    // Ignore, already been assigned a type in For.
    return this;
}

Node *Switch::TypeCheck(TypeChecker &tc, bool reqret) {
    // TODO: much like If, should only typecheck one case if the value is constant, and do
    // the corresponding work in the optimizer.
    value->TypeCheck(tc, true);
    auto ptype = value->exptype;
    if (!ptype->Numeric() && ptype->t != V_STRING)
        tc.TypeError("switch value must be int / float / string", *this);
    exptype = nullptr;
    bool have_default = false;
    for (auto n : cases->children) {
        auto cas = AssertIs<Case>(n);
        cas->TypeCheck(tc, reqret);
        if (cas->pattern->children.empty()) have_default = true;
        for (auto c : cas->pattern->children) {
            tc.SubTypeT(c->exptype, ptype, *c, "", "case");
        }
        exptype = exptype.Null() ? cas->body->exptype
                                 : tc.Union(exptype, cas->body->exptype, true);
    }
    assert(!exptype.Null());
    for (auto n : cases->children) {
        auto cas = AssertIs<Case>(n);
        tc.SubType(cas->body, exptype, "", "case block");
    }
    if (reqret && !have_default)
        tc.TypeError("switch that returns a value must have a default case", *this);
    return this;
}

Node *Case::TypeCheck(TypeChecker &tc, bool reqret) {
    tc.TypeCheckList(pattern, false);
    body = body->TypeCheck(tc, reqret);
    exptype = body->exptype;
    return this;
}

Node *Range::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    start = start->TypeCheck(tc, true);
    end = end->TypeCheck(tc, true);
    exptype = start->exptype;
    if (exptype->t != end->exptype->t || !exptype->Numeric())
        tc.TypeError("range can only be two equal numeric types", *this);
    return this;
}

Node *CoDot::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    coroutine = coroutine->TypeCheck(tc, true);
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
    return this;
}

Node *Define::TypeCheck(TypeChecker &tc, bool reqret) {
    return AssignList::TypeCheck(tc, reqret);
}

Node *AssignList::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    auto def = Is<Define>(this);
    for (auto &sid : sids) {
        tc.UpdateCurrentSid(sid);
        if (def) {
            // We have to use a variable here because the init exp may be a function
            // call that causes this variable to be used/assigned.
            sid->type = !def->giventype.Null() ? def->giventype : tc.NewTypeVar();
        } else {
            tc.TypeCheckId(sid);
        }
    }
    child = child->TypeCheck(tc, true);
    size_t i = 0;
    for (auto sid : sids) {
        auto vartype = child->exptype;
        if (auto call = Is<Call>(child)) {
            tc.CheckReturnValues(call->sf->returntypes.size(), i, call->sf->parent->name, *this);
            vartype = call->sf->returntypes[i];
        } else if (auto nc = Is<NativeCall>(child)) {
            // For the 0th value, we prefer the existing type, see NativeCall below.
            if (i) {
                tc.CheckReturnValues(nc->nf->retvals.v.size(), i, nc->nf->name, *this);
                assert(nc->nf->retvals.v[i].flags == AF_NONE);  // See NativeCall below.
                auto &arg = nc->nf->retvals.v[i];
                vartype = tc.ToVStruct(arg.fixed_len, arg.type, nullptr, nc->nf, false, 0, *this);
            }
        } else if (auto mr = Is<MultipleReturn>(child)) {
            if (i >= mr->Arity())
                tc.parser.Error("right hand side does not return enough values", this);
            vartype = mr->children[i]->exptype;
        }
        if (def) {
            if (!def->giventype.Null()) {
                vartype = def->giventype;
                // Have to subtype the initializer value, as that node may contain
                // unbound vars (a:[int] = []) or values that that need to be coerced
                // (a:float = 1)
                tc.SubType(child, vartype, "initializer", "definition");
            }
            // Must SubType here rather than assignment, since sid->type is var
            // that may have been bound by the initializer already.
            tc.SubTypeT(vartype, sid->type, *def, "initializer");
            sid->type = vartype;
            Output(OUTPUT_DEBUG, "var: ", sid->id->name, ":", TypeName(vartype));
            if (sid->id->logvar) {
                for (auto &sc : tc.scopes)
                    if (sc.sf->iscoroutine)
                        tc.TypeError("can\'t use log variable inside coroutine", *def);
            }
        } else {
            FlowItem fi(sid);
            assert(fi.IsValid());
            tc.AssignFlowDemote(fi, vartype, false);
            tc.SubTypeT(vartype, sid->type, *this, "right");
        }
        i++;
    }
    return this;
}

Node *IntConstant::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    exptype = type_int;
    return this;
}

Node *FloatConstant::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    exptype = type_float;
    return this;
}

Node *StringConstant::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    exptype = type_string;
    return this;
}

Node *Nil::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    exptype = !giventype.Null() ? giventype : tc.NewTypeVar()->Wrap(tc.st.NewType(), V_NIL);
    return this;
}

Node *Plus::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Minus::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Multiply::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Divide::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *Mod::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOp(*this);
}

Node *PlusEq::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *MultiplyEq::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *MinusEq::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *DivideEq::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *ModEq::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckMathOpEq(*this);
}

Node *NotEqual::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *Equal::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *GreaterThanEq::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *LessThanEq::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *GreaterThan::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *LessThan::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckComp(*this);
}

Node *Not::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    child = child->TypeCheck(tc, true);
    exptype = type_int;
    return this;
}

Node *BitAnd::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *BitOr::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *Xor::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *ShiftLeft::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *ShiftRight::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckBitOp(*this);
}

Node *Negate::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    child = child->TypeCheck(tc, true);
    tc.SubType(child, type_int, "negated value", *this);
    exptype = type_int;
    return this;
}

Node *PostDecr::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *PostIncr::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *PreDecr::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *PreIncr::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckPlusPlus(*this);
}

Node *UnaryMinus::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    child = child->TypeCheck(tc, true);
    exptype = child->exptype;
    if (!exptype->Numeric() &&
        (exptype->t != V_STRUCT || !exptype->struc->sametype->Numeric()))
        tc.TypeError("numeric / numeric struct", exptype, *this);
    return this;
}

Node *IdentRef::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    tc.UpdateCurrentSid(sid);
    exptype = tc.TypeCheckId(sid);
    FlowItem fi(*this, exptype);
    assert(fi.IsValid());
    exptype = tc.UseFlow(fi);
    return this;
}

Node *Assign::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    left = left->TypeCheck(tc, true);
    right = right->TypeCheck(tc, true);
    tc.CheckReadOnly(*left);
    FlowItem fi(*left, left->exptype);
    if (fi.IsValid()) {
        left->exptype = tc.AssignFlowDemote(fi, right->exptype, true);
    }
    tc.SubType(right, left->exptype, "right", *this);
    if (fi.IsValid()) tc.AssignFlowPromote(*left, right->exptype);
    exptype = left->exptype;
    if (fi.IsValid()) exptype = tc.UseFlow(fi);
    return this;
}

Node *DefaultVal::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    // This is used only as a default value for native call arguments. The variable
    // makes it equal to whatever the function expects, then codegen can use that type
    // to generate a correct value.
    exptype = tc.NewTypeVar();
    return this;
}
    
Node *NativeCall::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    tc.TypeCheckList(this, false);
    if (nf->first->overloads) {
        // Multiple overloads available, figure out which we want to call.
        auto cnf = nf->first;
        auto nargs = Arity();
        for (; cnf; cnf = cnf->overloads) {
            if (cnf->args.v.size() != nargs) continue;
            size_t i = 0;
            for (auto &arg : cnf->args.v) {
                if (!tc.ConvertsTo(children[i]->exptype,
                                   tc.ToVStruct(arg.fixed_len, arg.type, children[i], nf, true,
                                                i + 1, *this),
                                   arg.type->t != V_STRING, false, true)) goto nomatch;
                i++;
            }
            nf = cnf;
            break;
            nomatch:;
        }
        if (!cnf)
            tc.NatCallError("arguments match no overloads of ", nf, *this);
    }
    vector<TypeRef> argtypes(children.size());
    size_t i = 0;
    for (auto &c : children) {
        auto &arg = nf->args.v[i];
        auto argtype = tc.ToVStruct(arg.fixed_len, arg.type, children[i], nf, false, i + 1, *this);
        bool typed = false;
        if (argtype->t == V_NIL && argtype->sub->Numeric() && !Is<DefaultVal>(c)) {
            // This is somewhat of a hack, because we conflate V_NIL with being optional
            // for native functions, but we don't want numeric types to be nilable.
            // Codegen has a special case for T_DEFAULTVAL however.
            argtype = argtype->sub;
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
            if (argtype->t == V_VECTOR) argtype = tc.NewTypeVar()->Wrap(tc.st.NewType());
            else if (argtype->t == V_ANY) argtype = tc.NewTypeVar();
            else assert(0);
        }
        if (arg.flags & NF_CORESUME) {
            // Specialized typechecking for resume()
            assert(argtypes[0]->t == V_COROUTINE);
            auto sf = argtypes[0]->sf;
            if (sf) {
                tc.SubType(c, sf->coresumetype, "resume value", *c);
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
        if (!typed)
            tc.SubType(c, argtype, tc.ArgName(i), nf->name);
        auto actualtype = c->exptype;
        if (actualtype->IsFunction()) {
            // We must assume this is going to get called and type-check it
            auto sf = actualtype->sf;
            if (sf->args.v.size()) {
                // we have no idea what args.
                assert(0);
                tc.TypeError("function passed to " + nf->name +
                             " cannot take any arguments", *this);
            }
            auto chosen = sf;
            List args(tc.parser.lex);
            tc.TypeCheckCall(sf, &args, chosen, c, false);
            assert(sf == chosen);
        }
        argtypes[i] = actualtype;
        i++;
    }

    exptype = type_any;  // no retvals
    if (nf->retvals.v.size()) {
        // multiple retvals taken care of by T_DEF / T_ASSIGNLIST
        auto &ret = nf->retvals.v[0];
        int sa = 0;
        switch (ret.flags) {
            case NF_SUBARG3: sa++;
            case NF_SUBARG2: sa++;
            case NF_SUBARG1: {
                exptype = argtypes[sa];
                auto nftype = nf->args.v[sa].type;

                if (nftype->t == V_TYPEID) {
                    assert(!sa);  // assumes always first.
                    auto tin = AssertIs<TypeOf>(children[0]);
                    if (!Is<DefaultVal>(tin->child)) exptype = tin->child->exptype;
                }

                if (ret.type->t == V_NIL) {
                    if (!IsRef(exptype->t))
                        tc.TypeError(cat("argument ", sa + 1, " to ", nf->name,
                                    " can't be scalar"), *this);
                    exptype = exptype->Wrap(tc.st.NewType(), V_NIL);
                } else if (nftype->t == V_VECTOR && ret.type->t != V_VECTOR) {
                    exptype = exptype->sub;
                } else if (nftype->t == V_COROUTINE || nftype->t == V_FUNCTION) {
                    auto sf = exptype->sf;
                    assert(sf);
                    // In theory it is possible this hasn't been generated yet..
                    exptype = sf->returntypes[0];
                }
                break;
            }
            case NF_ANYVAR:
                exptype = ret.type->t == V_VECTOR ? tc.NewTypeVar()->Wrap(tc.st.NewType())
                                                  : tc.NewTypeVar();
                break;
            default:
                assert(ret.flags == AF_NONE);
                exptype = ret.type;
                break;
        }
        exptype = tc.ToVStruct(ret.fixed_len, exptype, Arity() ? children[0] : nullptr, nf, false,
                               0, *this);
    }

    if (nf->name == "assert") {
        // Special case, add to flow:
        tc.CheckFlowTypeChanges(true, children[0]);
        // Also make result non-nil, if it was.
        if (exptype->t == V_NIL) exptype = exptype->Element();
    }

    return this;
}

Node *Call::TypeCheck(TypeChecker &tc, bool reqret) {
    sf = tc.PreSpecializeFunction(sf);
    tc.TypeCheckList(this, false);
    exptype = tc.TypeCheckCall(sf, this, sf, this, reqret);
    return this;
}

Node *FunRef::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    if (sf) {
        sf = tc.PreSpecializeFunction(sf);
        exptype = &sf->thistype;
    } else {
        exptype = type_any;
    }
    return this;
}

Node *DynCall::TypeCheck(TypeChecker &tc, bool reqret) {
    tc.UpdateCurrentSid(sid);
    tc.TypeCheckId(sid);
    //if (sid->type->IsFunction()) sid->type = &tc.PreSpecializeFunction(sid->type->sf)->thistype;
    tc.TypeCheckList(this, false);
    exptype = tc.TypeCheckDynCall(sid, this, sf, reqret);
    return this;
}

Node *Return::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    child = child->TypeCheck(tc, true);
    auto sf = subfunction_idx < 0 ? nullptr : tc.st.subfunctiontable[subfunction_idx];
    for (int i = (int)tc.scopes.size() - 1; i >= 0; i--) {
        if (sf && tc.scopes[i].sf->parent == sf->parent) {
            sf = tc.scopes[i].sf;  // Take specialized version.
            subfunction_idx = sf->idx;
            break;
        }
        if (tc.scopes[i].sf->iscoroutine) tc.TypeError("cannot return out of coroutine", *this);
    }
    if (subfunction_idx < 0) return this;  // return from program
    auto nsf = tc.TopScope(tc.named_scopes);
    if (nsf != sf) {
        // This is a non-local "return from".
        if (!sf->typechecked)
            tc.parser.Error("return from " + sf->parent->name +
                            " called out of context", this);
    }
    if (!Is<DefaultVal>(child)) {
        exptype = child->exptype;
        if (auto mrs = Is<MultipleReturn>(child)) {
            int i = 0;
            for (auto mr : mrs->children) {
                mr = tc.RetVal(mr, nullptr, sf, i, *mr);
                if (!i) exptype = mr->exptype;
                i++;
            }
        } else {
            auto call = Is<Call>(child);
            if (call && call->sf->returntypes.size() > 1) {
                // Multi-ret pass-thru:
                size_t i = 0;
                for (auto &rettype : call->sf->returntypes)
                    tc.RetVal(nullptr, rettype, sf, i++, *child);
            } else {
                child = tc.RetVal(child, nullptr, sf, 0, *child);
            }
        }
    } else {
        exptype = type_any;
        tc.RetVal(nullptr, exptype, sf, 0, *this);
    }
    return this;
}

Node *TypeAnnotation::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    exptype = giventype;
    return this;
}

Node *IsType::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    child = child->TypeCheck(tc, true);
    exptype = type_int;
    return this;
}

Node *Constructor::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    tc.TypeCheckList(this, false);
    exptype = giventype;
    if (exptype->t == V_VECTOR && exptype->sub->t == V_ANY) {
        if (Arity()) {
            // No type was specified.. first find union of all elements.
            TypeRef u(nullptr);
            for (auto c : children) {
                u = u.Null() ? c->exptype : tc.Union(u, c->exptype, true);
            }
            exptype = u->Wrap(tc.st.NewType());
        } else {
            // special case for empty vectors
            exptype = tc.NewTypeVar()->Wrap(tc.st.NewType());
        }
    }
    if (exptype->t == V_STRUCT) {
        auto struc = tc.FindStructSpecialization(exptype->struc, this);
        exptype = &struc->thistype;
    }
    size_t i = 0;
    for (auto &c : children) {
        TypeRef elemtype = exptype->t == V_STRUCT ? exptype->struc->fields.v[i].type
                                                  : exptype->Element();
        tc.SubType(c, elemtype, tc.ArgName(i), *this);
        i++;
    }
    return this;
}

Node *Dot::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    child = child->TypeCheck(tc, true);
    auto smtype = child->exptype;
    auto stype = maybe && smtype->t == V_NIL ? smtype->Element() : smtype;
    if (stype->t != V_STRUCT)
        tc.TypeError("struct/value", stype, *this, "object");
    auto struc = stype->struc;
    auto fieldidx = struc->Has(fld);
    if (fieldidx < 0)
        tc.TypeError("type " + struc->name + " has no field named " + fld->name, *this);
    auto &uf = struc->fields.v[fieldidx];
    if (maybe && !IsRefNil(uf.type->t))
        tc.TypeError(cat("cannot dereference scalar field ", fld->name, " with ?."), *this);
    exptype = maybe && smtype->t == V_NIL && uf.type->t != V_NIL
            ? uf.type->Wrap(tc.st.NewType(), V_NIL)
            : uf.type;
    FlowItem fi(*this, exptype);
    if (fi.IsValid()) exptype = tc.UseFlow(fi);
    return this;
}

Node *Indexing::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    object = object->TypeCheck(tc, true);
    index = index->TypeCheck(tc, true);
    auto vtype = object->exptype;
    if (vtype->t == V_STRUCT && vtype->struc->sametype->t != V_ANY) {}
    else if (vtype->t != V_VECTOR && vtype->t != V_STRING)
        tc.TypeError("vector/string/struct", vtype, *this, "container");
    auto itype = index->exptype;
    switch (itype->t) {
        case V_INT:
            exptype = vtype->t == V_VECTOR
                ? vtype->Element()
                : (vtype->t == V_STRUCT ? vtype->struc->sametype : type_int);
            break;
        case V_STRUCT: {
            if (vtype->t != V_VECTOR)
                tc.TypeError("multi-dimensional indexing on non-vector", *this);
            auto &struc = *itype->struc;
            exptype = vtype;
            for (auto &field : struc.fields.v) {
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
    return this;
}

Node *Seq::TypeCheck(TypeChecker &tc, bool reqret) {
    head = head->TypeCheck(tc, false);
    tail = tail->TypeCheck(tc, reqret);
    exptype = tail->exptype;
    return this;
}

Node *CoClosure::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    exptype = type_function_cocl;
    return this;
}

Node *CoRoutine::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    call = call->TypeCheck(tc, true);
    auto sf = AssertIs<Call>(call)->sf;
    assert(sf->iscoroutine);
    auto ct = tc.st.NewType();
    *ct = Type(V_COROUTINE, sf);
    exptype = ct;
    return this;
}

Node *TypeOf::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    child = child->TypeCheck(tc, true);
    exptype = type_typeid;
    return this;
}

Node *MultipleReturn::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    tc.TypeCheckList(this, false);
    return this;
}

Node *NativeRef::TypeCheck(TypeChecker & /*tc*/, bool /*reqret*/) {
    return this;
}

Node *StructRef::TypeCheck(TypeChecker &/*tc*/, bool /*reqret*/) {
    /*
    for (auto &f : st->fields.v) {
        if (f.defaultval && f.type->t == V_ANY && !(f.flags & AF_GENERIC) && f.defaultval->exptype.Null()) {
            f.defaultval->TypeCheck(tc, true);
            f.type = f.defaultval->exptype;
        }
    }
    */
    return this;
}

Node *ToFloat::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckCoercion(*this);
}

Node *ToString::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckCoercion(*this);
}

Node *ToAny::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckCoercion(*this);
}

Node *ToNil::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckCoercion(*this);
}

Node *ToBool::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckCoercion(*this);
}

Node *ToInt::TypeCheck(TypeChecker &tc, bool /*reqret*/) {
    return tc.TypeCheckCoercion(*this);
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

}  // namespace lobster
