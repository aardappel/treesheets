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

struct Parser {
    NativeRegistry &natreg;
    Lex lex;
    Node *root = nullptr;
    SymbolTable &st;
    vector<Function *> functionstack;
    vector<string_view> trailingkeywordedfunctionvaluestack;
    struct ForwardFunctionCall {
        size_t maxscopelevel;
        GenericCall *n;
        bool has_firstarg;
        SymbolTable::WithStackElem wse;
    };
    vector<ForwardFunctionCall> forwardfunctioncalls;
    bool call_noparens = false;
    set<string> pakfiles;

    Parser(NativeRegistry &natreg, string_view _src, SymbolTable &_st, string_view _stringsource)
        : natreg(natreg), lex(_src, _st.filenames, _stringsource), st(_st) {}

    ~Parser() {
        delete root;
    }

    void Error(string_view err, const Node *what = nullptr) {
        lex.Error(err, what ? &what->line : nullptr);
    }

    void Warn(string_view warn, const Node *what = nullptr) {
        lex.Warn(warn, what ? &what->line : nullptr);
    }

    void Parse() {
        auto sf = st.ScopeStart();
        st.toplevel = sf;
        auto &f = st.CreateFunction("__top_level_expression", "");
        f.overloads.push_back(nullptr);
        sf->SetParent(f, f.overloads[0]);
        f.anonymous = true;
        lex.Include("stdtype.lobster");
        sf->body = ParseStatements(T_ENDOFFILE);
        ImplicitReturn(sf);
        st.ScopeCleanup();
        root = new Call(lex, sf);
        assert(forwardfunctioncalls.empty());
    }

    List *ParseStatements(TType terminator) {
        auto list = new List(lex);
        for (;;) {
            ParseTopExp(list);
            if (lex.token == T_ENDOFINCLUDE) {
                st.EndOfInclude();
                lex.PopIncludeContinue();
            } else if (!IsNext(T_LINEFEED)) {
                break;
            }
            if (Either(T_ENDOFFILE, T_DEDENT)) break;
        }
        auto b = list->children.back();
        if (Is<EnumRef>(b) || Is<UDTRef>(b) || Is<FunRef>(b) || Is<Define>(b)) {
            if (terminator == T_ENDOFFILE) list->Add(new IntConstant(lex, 0));
            else Error("last expression in list can\'t be a definition");
        }
        Expect(terminator);
        CleanupStatements(list);
        return list;
    }

    void CleanupStatements(List *list) {
        ResolveForwardFunctionCalls();
        list->children.erase(remove_if(list->children.begin(), list->children.end(),
                                       [&](auto def) {
            if (auto er = Is<EnumRef>(def)) {
                st.UnregisterEnum(er->e);
            } else if (auto sr = Is<UDTRef>(def)) {
                st.UnregisterStruct(sr->udt, lex);
            } else if (auto fr = Is<FunRef>(def)) {
                auto f = fr->sf->parent;
                if (!f->anonymous) st.UnregisterFun(f);
                delete fr;
                return true;
            } else if (auto d = Is<Define>(def)) {
                for (auto p : d->sids) {
                    auto id = p.first->id;
                    id->static_constant =
                        id->single_assignment && d->child->IsConstInit();
                    if (id->single_assignment && !id->constant && d->line.fileidx == 0)
                        Warn("use \'let\' to declare: " + id->name, d);
                }
            } else if (auto r = Is<Return>(def)) {
                if (r != list->children.back())
                    Error("return must be last in block");
            }
            return false;
        }), list->children.end());
    }

    void ParseTopExp(List *list, bool isprivate = false) {
        switch(lex.token) {
            case T_NAMESPACE:
                if (st.scopelevels.size() != 1 || isprivate)
                    Error("namespace must be used at file scope");
                lex.Next();
                st.current_namespace = lex.sattr;
                Expect(T_IDENT);
                break;
            case T_PRIVATE:
                if (st.scopelevels.size() != 1 || isprivate)
                    Error("private must be used at file scope");
                lex.Next();
                ParseTopExp(list, true);
                break;
            case T_INCLUDE: {
                if (isprivate)
                    Error("include cannot be private");
                lex.Next();
                if (IsNext(T_FROM)) {
                    auto fn = lex.StringVal();
                    Expect(T_STR);
                    AddDataDir(move(fn));
                } else {
                    string fn;
                    if (lex.token == T_STR) {
                        fn = lex.StringVal();
                        lex.Next();
                    } else {
                        fn = lex.sattr;
                        Expect(T_IDENT);
                        while (IsNext(T_DOT)) {
                            fn += "/";
                            fn += lex.sattr;
                            Expect(T_IDENT);
                        }
                        fn += ".lobster";
                    }
                    Expect(T_LINEFEED);
                    lex.Include(fn);
                    ParseTopExp(list);
                }
                break;
            }
            case T_STRUCT:
                ParseTypeDecl(true,  isprivate, list);
                break;
            case T_CLASS:
                ParseTypeDecl(false, isprivate, list);
                break;
            case T_FUN: {
                lex.Next();
                list->Add(ParseNamedFunctionDefinition(isprivate, nullptr));
                break;
            }
            case T_ENUM:
            case T_ENUM_FLAGS: {
                bool incremental = lex.token == T_ENUM;
                lex.Next();
                int64_t cur = incremental ? 0 : 1;
                auto enumname = st.MaybeNameSpace(ExpectId(), !isprivate);
                auto def = st.EnumLookup(enumname, lex, true);
                def->isprivate = isprivate;  // FIXME: not used?
                Expect(T_COLON);
                Expect(T_INDENT);
                for (;;) {
                    auto evname = st.MaybeNameSpace(ExpectId(), !isprivate);
                    if (IsNext(T_ASSIGN)) {
                        cur = lex.IntVal();
                        Expect(T_INT);
                    }
                    auto ev = st.EnumValLookup(evname, lex, true);
                    ev->isprivate = isprivate;  // FIXME: not used?
                    ev->val = cur;
                    ev->e = def;
                    def->vals.emplace_back(ev);
                    if (incremental) cur++; else cur *= 2;
                    if (!IsNext(T_LINEFEED) || Either(T_ENDOFFILE, T_DEDENT)) break;
                }
                Expect(T_DEDENT);
                list->Add(new EnumRef(lex, def));
                break;
            }
            case T_VAR:
            case T_CONST: {
                auto isconst = lex.token == T_CONST;
                lex.Next();
                auto def = new Define(lex, nullptr, nullptr);
                for (;;) {
                    auto idname = ExpectId();
                    bool withtype = lex.token == T_TYPEIN;
                    TypeRef type = nullptr;
                    if (lex.token == T_COLON || withtype) {
                        lex.Next();
                        ParseType(type, withtype);
                    }
                    auto id = st.LookupDef(idname, lex, false, true, withtype);
                    if (isconst)  id->constant = true;
                    if (isprivate) id->isprivate = true;
                    def->sids.push_back({ id->cursid, type });
                    if (!IsNext(T_COMMA)) break;
                }
                if (IsNext(T_LOGASSIGN)) {
                    for (auto p : def->sids) st.MakeLogVar(p.first->id);
                } else {
                    Expect(T_ASSIGN);
                }
                def->child = ParseMultiRet(ParseOpExp());
                list->Add(def);
                break;
            }
            default: {
                if (isprivate)
                    Error("private only applies to declarations");
                if (IsNextId()) {
                    // Regular assign is handled in normal expression parsing below.
                    if (lex.token == T_COMMA) {
                        auto al = new AssignList(lex, Modify(IdentUseOrWithStruct(lastid)));
                        while (IsNext(T_COMMA))
                            al->children.push_back(Modify(IdentUseOrWithStruct(ExpectId())));
                        Expect(T_ASSIGN);
                        al->children.push_back(ParseMultiRet(ParseOpExp()));
                        list->Add(al);
                        break;
                    } else {
                        lex.Undo(T_IDENT, lastid);
                    }
                }
                list->Add(ParseExpStat());
                break;
            }
        }
    }

    void ParseTypeDecl(bool is_struct, bool isprivate, List *parent_list) {
        lex.Next();
        auto sname = st.MaybeNameSpace(ExpectId(), !isprivate);
        UDT *udt = &st.StructDecl(sname, lex, is_struct);
        auto parse_sup = [&] () {
            ExpectId();
            auto sup = &st.StructUse(lastid, lex);
            if (sup == udt) Error("can\'t inherit from: " + lastid);
            if (is_struct != sup->is_struct)
                Error("class/struct must match parent");
            return sup;
        };
        auto parse_specializers = [&] () {
            int i = 0;
            if (IsNext(T_LT)) {
                for (;;) {
                    if (udt->generics.empty()) Error("too many type specializers");
                    udt->generics.erase(udt->generics.begin());
                    TypeRef type;
                    ParseType(type, false);
                    auto def = IsNext(T_ASSIGN) ? ParseFactor() : nullptr;
                    for (auto &field : udt->fields.v) {
                        if (field.genericref == i) {
                            field.type = type;
                            // We don't reset genericref here, because its used to know which
                            // fields to select a specialization on.
                            if (def) {
                                if (field.defaultval) Error("field already has a default value");
                                field.defaultval = def;
                            }
                        }
                    }
                    i++;
                    if (lex.token == T_GT) {
                        lex.OverrideCont(false);  // T_GT here should not continue the line.
                        lex.Next();
                        break;
                    }
                    Expect(T_COMMA);
                }
            }
            return i;
        };
        if (IsNext(T_ASSIGN)) {
            // A specialization of an existing struct
            auto sup = parse_sup();
            udt = sup->CloneInto(udt);
            udt->idx = (int)st.udttable.size() - 1;
            udt->name = sname;
            if (!parse_specializers())
                Error("no specialization types specified");
            if (isprivate != sup->isprivate)
                Error("specialization must have same privacy level");
            if (sup->predeclaration)
                Error("must specialization fully defined type");
            if (udt->superclass) {
                // This points to a generic version of the superclass of this class.
                // See if we can find a matching specialization instead.
                // FIXME: better to move this to start of typecheck just in case more to be
                // declared.
                auto sti = udt->superclass->next;
                for (; sti; sti = sti->next) {
                    for (size_t i = 0; i < sti->fields.size(); i++)
                        if (sti->fields.v[i].type != udt->fields.v[i].type)
                            goto fail;
                    goto done;
                    fail:;
                }
                done:
                udt->superclass = sti;  // Either a match or nullptr.
            }
        } else if (Either(T_COLON, T_LT)) {
            // A regular struct declaration
            udt->isprivate = isprivate;
            if (IsNext(T_LT)) {
                for (;;) {
                    auto id = ExpectId();
                    for (auto &g : udt->generics)
                        if (g.name == id)
                            Error("re-declaration of generic type");
                    udt->generics.push_back({ id });
                    if (IsNext(T_GT)) break;
                    Expect(T_COMMA);
                }
            }
            Expect(T_COLON);
            if (lex.token == T_IDENT) {
                auto sup = parse_sup();
                if (sup) {
                    if (sup->predeclaration) sup->predeclaration = false;  // Empty base class.
                    udt->superclass = sup;
                    udt->generics = sup->generics;
                    for (auto &fld : sup->fields.v) {
                        udt->fields.v.push_back(fld);
                    }
                }
                parse_specializers();
            }
            if (IsNext(T_INDENT)) {
                bool fieldsdone = false;
                for (;;) {
                    if (IsNext(T_FUN)) {
                        fieldsdone = true;
                        parent_list->Add(ParseNamedFunctionDefinition(false, udt));
                    }
                    else {
                        if (fieldsdone) Error("fields must be declared before methods");
                        ExpectId();
                        auto &sfield = st.FieldDecl(lastid);
                        TypeRef type = type_any;
                        int genericref = -1;
                        if (IsNext(T_COLON)) {
                            genericref = ParseType(type, false, udt);
                        }
                        Node *defaultval = IsNext(T_ASSIGN) ? ParseExp() : nullptr;
                        udt->fields.v.push_back(Field(&sfield, type, genericref, defaultval));
                    }
                    if (!IsNext(T_LINEFEED) || Either(T_ENDOFFILE, T_DEDENT)) break;
                }
                Expect(T_DEDENT);
            }
            if (udt->fields.v.empty() && udt->is_struct)
                Error("structs cannot be empty");
        } else {
            // A pre-declaration.
            udt->predeclaration = true;
        }
        parent_list->Add(new UDTRef(lex, udt));
    }

    Node *ParseNamedFunctionDefinition(bool isprivate, UDT *self) {
        // TODO: also exclude functions from namespacing whose first arg is a type namespaced to
        // current namespace (which is same as !self).
        auto idname = st.MaybeNameSpace(ExpectId(), !isprivate && !self);
        if (natreg.FindNative(idname))
            Error("cannot override built-in function: " + idname);
        return ParseFunction(&idname, isprivate, true, true, "", false, false, self);
    }

    void ImplicitReturn(SubFunction *sf) {
        // Anonymous functions and one-liners have an implicit return value.
        auto &stats = sf->body->children;
        if (!Is<Return>(stats.back())) {
            // Conversely, if named functions have no return at the end, we should
            // ensure any value accidentally available gets ignored and does not become a return
            // value.
            auto make_void = !sf->parent->anonymous;
            // All function bodies end in return, simplifying code downstream.
            stats.back() = new Return(stats.back()->line, stats.back(), sf, make_void);
        }
    }

    Node *ParseFunction(string_view *name,
                        bool isprivate, bool parens, bool parseargs,
                        string_view context,
                        bool expfunval = false, bool parent_noparens = false, UDT *self = nullptr) {
        auto sf = st.ScopeStart();
        if (parens) Expect(T_LEFTPAREN);
        size_t nargs = 0;
        auto SetArgFlags = [&](Arg &arg, bool withtype) {
            if (!st.IsGeneric(arg.type)) arg.flags &= ~AF_GENERIC;
            if (withtype) arg.flags |= AF_WITHTYPE;
        };
        if (self) {
            nargs++;
            auto id = st.LookupDef("this", lex, false, false, true);
            auto &arg = sf->args.v.back();
            arg.type = &self->thistype;
            st.AddWithStruct(arg.type, id, lex, sf);
            SetArgFlags(arg, true);
        }
        if (lex.token != T_RIGHTPAREN && parseargs) {
            for (;;) {
                ExpectId();
                nargs++;
                auto id = st.LookupDef(lastid, lex, false, false, false);
                auto &arg = sf->args.v.back();
                bool withtype = lex.token == T_TYPEIN;
                if (parens && (lex.token == T_COLON || withtype)) {
                    lex.Next();
                    ParseType(arg.type, withtype, nullptr, nullptr, &sf->args.v.back());
                    if (withtype) st.AddWithStruct(arg.type, id, lex, sf);
                    if (nargs == 1 && IsUDT(arg.type->t)) self = arg.type->udt;
                }
                SetArgFlags(arg, withtype);
                if (!IsNext(T_COMMA)) break;
            }
        }
        if (parens) Expect(T_RIGHTPAREN);
        sf->method_of = self;
        auto &f = name ? st.FunctionDecl(*name, nargs, lex) : st.CreateFunction("", context);
        if (name && self) {
            for (auto isf : f.overloads) {
                if (isf->method_of == self) {
                    // FIXME: this currently disallows static overloads on the other args, that
                    // would be nice to add.
                    Error("method " + *name + " already declared for type: " + self->name);
                }
            }
        }
        f.overloads.push_back(nullptr);
        sf->SetParent(f, f.overloads.back());
        if (!expfunval) {
            if (IsNext(T_CODOT)) {  // Return type decl.
                ParseType(sf->fixedreturntype, false, nullptr, sf);
            }
            if (!IsNext(T_COLON)) {
                if (lex.token == T_IDENT || !name) Expect(T_COLON);
                if (f.istype || f.overloads.size() > 1)
                    Error("redefinition of function type: " + *name);
                f.istype = true;
                sf->typechecked = true;
                // Any untyped args truely mean "any", they should not be specialized (we wouldn't know
                // which specialization that refers to).
                for (auto &arg : sf->args.v) {
                    if (arg.flags & AF_GENERIC) arg.flags = AF_NONE;
                    // No idea what the function is going to be, so have to default to borrow.
                    arg.sid->lt = LT_BORROW;
                }
                if (sf->fixedreturntype.Null()) Error("missing return type");
                sf->returntype = sf->fixedreturntype;
                sf->reqret = sf->returntype->NumValues();
            }
        }
        if (name) {
            if (f.overloads.size() > 1) {
                // We could check here for "double declaration", but since that entails
                // detecting what is a legit overload or not, this is in general better left to the
                // type checker.
                if (!f.nargs()) Error("double declaration: " + f.name);
                for (auto [i, arg] : enumerate(sf->args.v)) {
                    if (arg.flags & AF_GENERIC && !i)
                        Error("first argument of overloaded function must be typed: " + f.name);
                }
                if (isprivate != f.isprivate)
                    Error("inconsistent private annotation of multiple function implementations"
                          " for: " + *name);
            }
            f.isprivate = isprivate;
            functionstack.push_back(&f);
        } else {
            f.anonymous = true;
        }
        // Parse the body.
        if (!f.istype) {
            if (expfunval) {
                sf->body = (new List(lex))->Add(ParseExp(parent_noparens));
            } else if (IsNext(T_INDENT)) {
                sf->body = ParseStatements(T_DEDENT);
            } else {
                sf->body = new List(lex);
                sf->body->children.push_back(ParseExpStat());
                CleanupStatements(sf->body);
            }
            ImplicitReturn(sf);
        }
        for (auto &arg : sf->args.v) {
            if (arg.sid->id->anonymous_arg) {
                if (name) Error("cannot use anonymous argument: " + arg.sid->id->name +
                    " in named function: " + f.name, sf->body);
                if (nargs) Error("cannot mix anonymous argument: " + arg.sid->id->name +
                    " with declared arguments in function", sf->body);
            }
        }
        st.ScopeCleanup();
        if (name) functionstack.pop_back();
        // Keep copy or arg types from before specialization.
        f.orig_args = sf->args;  // not used for multimethods
        return new FunRef(lex, sf);
    }

    int ParseType(TypeRef &dest, bool withtype, UDT *udt = nullptr,
                  SubFunction *sfreturntype = nullptr, Arg *funarg = nullptr) {
        switch(lex.token) {
            case T_INTTYPE:   dest = type_int;        lex.Next(); break;
            case T_FLOATTYPE: dest = type_float;      lex.Next(); break;
            case T_STRTYPE:   dest = type_string;     lex.Next(); break;
            case T_COROUTINE: dest = type_coroutine;  lex.Next(); break;
            case T_RESOURCE:  dest = type_resource;   lex.Next(); break;
            case T_LAZYEXP: {
                lex.Next();
                if (!funarg) {
                  Error("lazy_expression cannot be used outside function signature");
                }
                funarg->flags = ArgFlags(funarg->flags | AF_EXPFUNVAL);
                return -1;
            }
            case T_IDENT: {
                if (udt) {
                    for (auto [i, gen] : enumerate(udt->generics)) {
                        if (gen.name == lex.sattr) {
                            lex.Next();
                            dest = type_undefined;
                            return (int)i;
                        }
                    }
                }
                auto f = st.FindFunction(lex.sattr);
                if (f && f->istype) {
                    dest = &f->overloads[0]->thistype;
                    lex.Next();
                    break;
                }
                auto e = st.EnumLookup(lex.sattr, lex, false);
                if (e) {
                    dest = &e->thistype;
                    lex.Next();
                    break;
                }
                dest = &st.StructUse(lex.sattr, lex).thistype;
                lex.Next();
                break;
            }
            case T_LEFTBRACKET: {
                lex.Next();
                TypeRef elem;
                ParseType(elem, false);
                Expect(T_RIGHTBRACKET);
                dest = st.Wrap(elem, V_VECTOR);
                break;
            }
            case T_VOIDTYPE:
                if (sfreturntype) {
                    lex.Next();
                    dest = type_void;
                    sfreturntype->reqret = 0;
                    break;
                }
                // FALL-THRU:
            default:
                Error("illegal type syntax: " + lex.TokStr());
        }
        if (IsNext(T_QUESTIONMARK)) {
            if (!IsNillable(dest->t))
                Error("value types can\'t be made nilable");
            dest = st.Wrap(dest, V_NIL);
        }
        if (withtype && !IsUDT(dest->t)) Error(":: must be used with a class type");
        return -1;
    }

    void ParseFunArgs(List *list, bool coroutine, Node *derefarg, string_view fname = "",
                      GenericArgs *args = nullptr, bool noparens = false) {
        if (derefarg) {
            CheckArg(args, 0, fname);
            list->Add(derefarg);
            if (IsNext(T_LEFTPAREN)) {
                ParseFunArgsRec(list, false, false, args, 1, fname, noparens);
            }
        } else {
            if (!noparens) Expect(T_LEFTPAREN);
            ParseFunArgsRec(list, coroutine, false, args, 0, fname, noparens);
        }
    }

    void ParseFunArgsRec(List *list, bool coroutine, bool needscomma, GenericArgs *args,
                         size_t thisarg, string_view fname, bool noparens /* this call */) {
        if (!noparens && IsNext(T_RIGHTPAREN)) {
            if (call_noparens) {  // This call is an arg to a call that has no parens.
                // Don't unnecessarily parse funvals. Means "if f(x):" parses as expected.
                return;
            }
            ParseTrailingFunctionValues(list, coroutine, args, thisarg, fname);
            return;
        }
        if (needscomma) Expect(T_COMMA);
        CheckArg(args, thisarg, fname);
        if (args && (args->GetFlags(thisarg) & AF_EXPFUNVAL)) {
            list->Add(ParseFunction(nullptr, false, false, false, args->GetName(thisarg), true,
                                    noparens));
        } else {
            list->Add(ParseExp(noparens));
        }

        if (noparens) {
            if (lex.token == T_COLON)
                ParseTrailingFunctionValues(list, coroutine, args, thisarg + 1, fname);
        } else {
            ParseFunArgsRec(list, coroutine, !noparens, args, thisarg + 1, fname, noparens);
        }
    }

    void CheckArg(GenericArgs *args, size_t thisarg, string_view fname) {
        if (args && thisarg == args->size())
            Error("too many arguments passed to function " + fname);
    }

    void ParseTrailingFunctionValues(List *list, bool coroutine, GenericArgs *args, size_t thisarg,
                                     string_view fname) {
        if (args && thisarg + 1 < args->size())
            trailingkeywordedfunctionvaluestack.push_back(args->GetName(thisarg + 1));
        auto name = args && thisarg < args->size() ? args->GetName(thisarg) : "";
        Node *e = nullptr;
        switch (lex.token) {
            case T_COLON:
                e = ParseFunction(nullptr, false, false, false, name);
                break;
            case T_IDENT:
                // skip if this function value starts with an ID that's equal to the parents next
                // keyworded function val ID, e.g. "else" in: if(..): currentcall(..) else: ..
                // FIXME: if you forget : after else, it is going to try to declare any following
                // identifier as the first arg of a new function, leading to weird errors.
                // Should ideally know here how many args to expect.
                if (trailingkeywordedfunctionvaluestack.empty() ||
                    trailingkeywordedfunctionvaluestack.back() != lex.sattr)
                    e = ParseFunction(nullptr, false, false, true, name);
                break;
            case T_LEFTPAREN:
                e = ParseFunction(nullptr, false, true, true, name);
                break;
            default:
                break;
        }
        if (args && thisarg + 1 < args->size()) trailingkeywordedfunctionvaluestack.pop_back();
        if (!e) {
            if (coroutine) {
                e = new CoClosure(lex);
                coroutine = false;
            } else {
                return;
            }
        }
        list->Add(e);
        CheckArg(args, thisarg, fname);
        thisarg++;
        bool islf = lex.token == T_LINEFEED;
        if (args && thisarg < args->size() && (lex.token == T_IDENT || islf)) {
            if (islf) lex.Next();
            if (lex.token == T_IDENT && args->GetName(thisarg) == lex.sattr) {
                lex.Next();
                ParseTrailingFunctionValues(list, coroutine, args, thisarg, fname);
            } else {
                lex.PushCur();
                if (islf) lex.Push(T_LINEFEED);
                lex.Next();
            }
        }
    }

    Node *ParseMultiRet(Node *first) {
        if (lex.token != T_COMMA) return first;
        auto list = new MultipleReturn(lex);
        list->Add(first);
        while (IsNext(T_COMMA)) {
            list->Add(ParseOpExp());
        }
        return list;
    }

    Node *ParseExpStat() {
        if (IsNext(T_RETURN)) {
            Node *rv = nullptr;
            if (!Either(T_LINEFEED, T_DEDENT, T_FROM)) {
                rv = ParseMultiRet(ParseOpExp());
            } else {
                rv = new DefaultVal(lex);
            }
            auto sf = st.toplevel;
            if (IsNext(T_FROM)) {
                if(!IsNext(T_PROGRAM)) {
                    if (!IsNextId())
                        Error("return from: must be followed by function identifier or"
                              " \"program\"");
                    auto f = st.FindFunction(lastid);
                    if (!f)
                        Error("return from: not a known function");
                    if (f->sibf || f->overloads.size() > 1)
                        Error("return from: function must have single implementation");
                    sf = f->overloads[0];
                }
            } else {
                if (functionstack.size())
                    sf = functionstack.back()->overloads.back();
            }
            return new Return(lex, rv, sf, false);
        }
        auto e = ParseExp();
        while (IsNext(T_SEMICOLON)) {
            if (IsNext(T_LINEFEED)) {
                // specialized error for all the C-style language users
                Error("\';\' is not a statement terminator");
            }
            e = new Seq(lex, e, ParseExp());
        }
        return e;
    }

    Node *Modify(Node *e) {
        if (auto idr = Is<IdentRef>(e))
            idr->sid->id->Assign(lex);
        return e;
    }

    void CheckOpEq(Node *e) {
        if (!Is<IdentRef>(e) && !Is<CoDot>(e) && !Is<Indexing>(e) && !Is<GenericCall>(e))
            Error("illegal left hand side of assignment");
        Modify(e);
        lex.Next();
    }

    Node *ParseExp(bool parent_noparens = false) {
        DS<bool> ds(call_noparens, parent_noparens);
        auto e = ParseOpExp();
        switch (lex.token) {
            case T_ASSIGN:  CheckOpEq(e); return new Assign(lex, e, ParseExp());
            case T_PLUSEQ:  CheckOpEq(e); return new PlusEq(lex, e, ParseExp());
            case T_MINUSEQ: CheckOpEq(e); return new MinusEq(lex, e, ParseExp());
            case T_MULTEQ:  CheckOpEq(e); return new MultiplyEq(lex, e, ParseExp());
            case T_DIVEQ:   CheckOpEq(e); return new DivideEq(lex, e, ParseExp());
            case T_MODEQ:   CheckOpEq(e); return new ModEq(lex, e, ParseExp());
            default:        return e;
        }
    }

    Node *ParseOpExp(uint level = 6) {
        static TType ops[][4] = {
            { T_MULT, T_DIV, T_MOD, T_NONE },
            { T_PLUS, T_MINUS, T_NONE, T_NONE },
            { T_ASL, T_ASR, T_NONE, T_NONE },
            { T_BITAND, T_BITOR, T_XOR, T_NONE },
            { T_LT, T_GT, T_LTEQ, T_GTEQ },
            { T_EQ, T_NEQ, T_NONE, T_NONE },
            { T_AND, T_OR, T_NONE, T_NONE },
        };
        Node *exp = level ? ParseOpExp(level - 1) : ParseUnary();
        TType *o = &ops[level][0];
        while (Either(o[0], o[1]) || Either(o[2], o[3])) {
            TType op = lex.token;
            lex.Next();
            auto rhs = level ? ParseOpExp(level - 1) : ParseUnary();
            switch (op) {
                case T_MULT:   exp = new Multiply(lex, exp, rhs); break;
                case T_DIV:    exp = new Divide(lex, exp, rhs); break;
                case T_MOD:    exp = new Mod(lex, exp, rhs); break;
                case T_PLUS:   exp = new Plus(lex, exp, rhs); break;
                case T_MINUS:  exp = new Minus(lex, exp, rhs); break;
                case T_ASL:    exp = new ShiftLeft(lex, exp, rhs); break;
                case T_ASR:    exp = new ShiftRight(lex, exp, rhs); break;
                case T_BITAND: exp = new BitAnd(lex, exp, rhs); break;
                case T_BITOR:  exp = new BitOr(lex, exp, rhs); break;
                case T_XOR:    exp = new Xor(lex, exp, rhs); break;
                case T_LT:     exp = new LessThan(lex, exp, rhs); break;
                case T_GT:     exp = new GreaterThan(lex, exp, rhs); break;
                case T_LTEQ:   exp = new LessThanEq(lex, exp, rhs); break;
                case T_GTEQ:   exp = new GreaterThanEq(lex, exp, rhs); break;
                case T_EQ:     exp = new Equal(lex, exp, rhs); break;
                case T_NEQ:    exp = new NotEqual(lex, exp, rhs); break;
                case T_AND:    exp = new And(lex, exp, rhs); break;
                case T_OR:     exp = new Or(lex, exp, rhs); break;
                default: assert(false);
            }
        }
        return exp;
    }

    Node *UnaryArg() {
        auto t = lex.token;
        lex.Next();
        auto e = ParseUnary();
        return t == T_INCR || t == T_DECR ? Modify(e) : e;
    }

    Node *ParseUnary() {
        switch (lex.token) {
            case T_MINUS: return new UnaryMinus(lex, UnaryArg());
            case T_NOT:   return new Not(lex, UnaryArg());
            case T_NEG:   return new Negate(lex, UnaryArg());
            case T_INCR:  return new PreIncr(lex, UnaryArg());
            case T_DECR:  return new PreDecr(lex, UnaryArg());
            default:      return ParseDeref();
        }
    }

    Node *BuiltinControlClosure(Node *funval, size_t maxargs) {
        size_t clnargs = 0;
        auto fr = Is<FunRef>(funval);
        if (fr)
            clnargs = fr->sf->parent->nargs();
        else if (!Is<DefaultVal>(funval))
            Error("illegal body", funval);
        if (clnargs > maxargs)
            Error(cat("body has ", clnargs - maxargs, " parameters too many"), funval);
        if (Is<DefaultVal>(funval)) return funval;
        assert(fr);
        auto call = new Call(lex, fr->sf);
        delete fr;
        if (clnargs > 0) {
            call->Add(new ForLoopElem(lex));
            if (clnargs > 1) call->Add(new ForLoopCounter(lex));
        }
        return call;
    }

    Node *ParseFunctionCall(Function *f, NativeFun *nf, string_view idname, Node *firstarg,
                            bool coroutine, bool noparens) {
        auto wse = st.GetWithStackBack();
        // FIXME: move more of the code below into the type checker, and generalize the remaining
        // code to be as little dependent as possible on wether nf or f are available.
        // It should only parse args and construct a GenericCall.

        // We give precedence to builtins, unless we're calling a known function in a :: context.
        if (nf && (!f || !wse.id)) {
            auto nc = new GenericCall(lex, idname, nullptr, false);
            ParseFunArgs(nc, coroutine, firstarg, idname, &nf->args, noparens);
            for (auto [i, arg] : enumerate(nf->args.v)) {
                if (i >= nc->Arity()) {
                    auto &type = arg.type;
                    if (type->t == V_NIL) {
                        nc->Add(new DefaultVal(lex));
                    } else {
                        auto nargs = nc->Arity();
                        for (auto ol = nf->overloads; ol; ol = ol->overloads) {
                            // Typechecker will deal with it.
                            if (ol->args.v.size() == nargs) goto argsok;
                        }
                        Error("missing arg to builtin function: " + idname);
                    }
                }
            }
            argsok:
            // Special formats for these functions, for better type checking and performance
            auto convertnc = [&](Node *e) {
                nc->children.clear();
                delete nc;
                return e;
            };
            if (nf->name == "if") {
                return convertnc(new If(lex, nc->children[0],
                                             BuiltinControlClosure(nc->children[1], 0),
                                             BuiltinControlClosure(nc->children[2], 0)));

            } else if (nf->name == "while") {
                return convertnc(new While(lex, BuiltinControlClosure(nc->children[0], 0),
                                                BuiltinControlClosure(nc->children[1], 0)));
            } else if (nf->name == "for") {
                return convertnc(new For(lex, nc->children[0],
                                              BuiltinControlClosure(nc->children[1], 2)));
            }
            return nc;
        }
        auto id = st.Lookup(idname);
        // If both a var and a function are in scope, the deepest scope wins.
        // Note: <, because functions are inside their own scope.
        if (f && (!id || id->scopelevel < f->scopelevel)) {
            if (f->istype) Error("can\'t call function type: " + f->name);
            auto bestf = f;
            for (auto fi = f->sibf; fi; fi = fi->sibf)
                if (fi->nargs() > bestf->nargs()) bestf = fi;
            auto call = new GenericCall(lex, idname, nullptr, false);
            if (!firstarg) firstarg = SelfArg(f, wse);
            ParseFunArgs(call, coroutine, firstarg, idname, &bestf->overloads.back()->args, noparens);
            auto nargs = call->Arity();
            f = FindFunctionWithNargs(f, nargs, idname, nullptr);
            call->sf = f->overloads.back();
            return call;
        }
        if (id) {
            auto dc = new DynCall(lex, nullptr, id->cursid);
            ParseFunArgs(dc, coroutine, firstarg);
            return dc;
        } else {
            auto call = new GenericCall(lex, idname, nullptr, false);
            ParseFunArgs(call, coroutine, firstarg);
            ForwardFunctionCall ffc = { st.scopelevels.size(), call, !!firstarg, wse };
            forwardfunctioncalls.push_back(ffc);
            return call;
        }
    }

    IdentRef *SelfArg(const Function *f, const SymbolTable::WithStackElem &wse) {
        if (f->nargs()) {
            // If we're in the context of a withtype, calling a function that starts with an
            // arg of the same type we pass it in automatically.
            // This is maybe a bit very liberal, should maybe restrict it?
            for (auto sf : f->overloads) {
                if (wse.type == sf->args.v[0].type && sf->args.v[0].flags & AF_WITHTYPE) {
                    if (wse.id && wse.sf->parent != f) {  // Not in recursive calls.
                        return new IdentRef(lex, wse.id->cursid);
                    }
                    break;
                }
            }
        }
        return nullptr;
    }

    Function *FindFunctionWithNargs(Function *f, size_t nargs, string_view idname, Node *errnode) {
        for (; f; f = f->sibf)
            if (f->nargs() == nargs)
                return f;
        Error(cat("no version of function ", idname, " takes ", nargs, " arguments"), errnode);
        return nullptr;
    }

    void ResolveForwardFunctionCalls() {
        for (auto ffc = forwardfunctioncalls.begin(); ffc != forwardfunctioncalls.end(); ) {
            if (ffc->maxscopelevel >= st.scopelevels.size()) {
                auto f = st.FindFunction(ffc->n->name);
                if (f) {
                    if (!ffc->has_firstarg) {
                        auto self = SelfArg(f, ffc->wse);
                        if (self) ffc->n->children.insert(ffc->n->children.begin(), self);
                    }
                    ffc->n->sf = FindFunctionWithNargs(f,
                        ffc->n->Arity(), ffc->n->name, ffc->n)->overloads.back();
                    ffc = forwardfunctioncalls.erase(ffc);
                    continue;
                } else {
                    if (st.scopelevels.size() == 1)
                        Error("call to unknown function: " + ffc->n->name, ffc->n);
                    // Prevent it being found in sibling scopes.
                    ffc->maxscopelevel = st.scopelevels.size() - 1;
                }
            }
            ffc++;
        }
    }

    Node *ParseDeref() {
        auto n = ParseFactor();
        // FIXME: it would be good to narrow the kind of factors these derefs can attach to,
        // since for some of them it makes no sense (e.g. function call with lambda args).
        for (;;) switch (lex.token) {
            case T_DOT:
            case T_CODOT: {
                auto op = lex.token;
                lex.Next();
                auto idname = ExpectId();
                if (op == T_CODOT) {
                    // Here we just look up ANY var with this name, only in the typechecker can we
                    // know if it exists inside the coroutine. Can cause error if used before
                    // coroutine is defined, error hopefully hints at that.
                    auto id = st.LookupAny(idname);
                    if (!id)
                        Error("coroutines have no variable named: " + idname);
                    n = new CoDot(lex, n, new IdentRef(lex, id->cursid));
                } else {
                    auto fld = st.FieldUse(idname);
                    auto f = st.FindFunction(idname);
                    auto nf = natreg.FindNative(idname);
                    if (fld || f || nf) {
                        if (fld && lex.token != T_LEFTPAREN) {
                            auto dot = new GenericCall(lex,
                                                       idname, f ? f->overloads.back() : nullptr,
                                                       true);
                            dot->Add(n);
                            n = dot;
                        } else {
                            n = ParseFunctionCall(f, nf, idname, n, false, false);
                        }
                    } else {
                        Error("unknown field/function: " + idname);
                    }
                }
                break;
            }
            case T_LEFTPAREN: {
                // Special purpose error to make this more understandable for the user.
                // FIXME: can remove this restriction if we make DynCall work with any node.
                Error("dynamic function value call must be on variable");
                return n;
            }
            case T_LEFTBRACKET: {
                lex.Next();
                n = new Indexing(lex, n, ParseExp());
                Expect(T_RIGHTBRACKET);
                break;
            }
            case T_INCR:
                n = new PostIncr(lex, Modify(n));
                lex.Next();
                return n;
            case T_DECR:
                n = new PostDecr(lex, Modify(n));
                lex.Next();
                return n;
            case T_IS: {
                lex.Next();
                auto is = new IsType(lex, n, TypeRef());
                ParseType(is->giventype, false);
                return is;
            }
            default:
                return n;
        }
    }

    Node *ParseFactor() {
        switch (lex.token) {
            case T_INT: {
                auto i = lex.IntVal();
                lex.Next();
                return new IntConstant(lex, i);
            }
            case T_FLOAT: {
                auto f = strtod(lex.sattr.data(), nullptr);
                lex.Next();
                return new FloatConstant(lex, f);
            }
            case T_STR: {
                string s = lex.StringVal();
                lex.Next();
                return new StringConstant(lex, s);
            }
            case T_NIL: {
                lex.Next();
                auto n = new Nil(lex, nullptr);
                if (IsNext(T_COLON)) {
                    ParseType(n->giventype, false);
                    if (!IsNillable(n->giventype->t)) Error("illegal nil type");
                    n->giventype = st.Wrap(n->giventype, V_NIL);
                }
                return n;
            }
            case T_LEFTPAREN: {
                lex.Next();
                auto n = ParseExp();
                Expect(T_RIGHTPAREN);
                return n;
            }
            case T_LEFTBRACKET: {
                lex.Next();
                auto constructor = new Constructor(lex, nullptr);
                ParseVector([this, &constructor] () {
                    constructor->Add(this->ParseExp());
                }, T_RIGHTBRACKET);
                if (IsNext(T_TYPEIN)) {
                    ParseType(constructor->giventype, false);
                    constructor->giventype = st.Wrap(constructor->giventype, V_VECTOR);
                }
                return constructor;
            }
            case T_FUN: {
                lex.Next();
                return ParseFunction(nullptr, false, true, true, "");
            }
            case T_COROUTINE: {
                lex.Next();
                auto idname = ExpectId();
                auto n = ParseFunctionCall(st.FindFunction(idname), nullptr, idname, nullptr, true,
                                           false);
                return new CoRoutine(lex, n);
            }
            case T_FLOATTYPE:
            case T_INTTYPE:
            case T_STRTYPE:
            case T_ANYTYPE: {
                // These are also used as built-in functions, so allow them to function as
                // identifier for calls.
                auto idname = lex.sattr;
                lex.Next();
                if (lex.token != T_LEFTPAREN) Error("type used as expression");
                return IdentFactor(idname);
            }
            case T_TYPEOF: {  // "return", ident or type.
                lex.Next();
                if (lex.token == T_RETURN) {
                    lex.Next();
                    return new TypeOf(lex, new DefaultVal(lex));
                }
                if (lex.token == T_IDENT) {
                    auto id = st.Lookup(lex.sattr);
                    if (id) {
                        lex.Next();
                        return new TypeOf(lex, new IdentRef(lex, id->cursid));
                    }
                }
                auto tn = new TypeAnnotation(lex, TypeRef());
                ParseType(tn->giventype, false);
                return new TypeOf(lex, tn);
            }
            case T_IDENT: {
                auto idname = lex.sattr;
                lex.Next();
                return IdentFactor(idname);
            }
            case T_PAKFILE: {
                lex.Next();
                string s = lex.StringVal();
                Expect(T_STR);
                pakfiles.insert(s);
                return new StringConstant(lex, s);
            }
            case T_SWITCH: {
                lex.Next();
                auto value = ParseExp(true);
                Expect(T_COLON);
                Expect(T_INDENT);
                bool have_default = false;
                auto cases = new List(lex);
                for (;;) {
                    List *pattern = new List(lex);
                    if (lex.token == T_DEFAULT) {
                        if (have_default) Error("cannot have more than one default in a switch");
                        lex.Next();
                        have_default = true;
                    } else {
                        Expect(T_CASE);
                        for (;;) {
                            auto f = ParseDeref();
                            if (lex.token == T_DOTDOT) {
                                lex.Next();
                                f = new Range(lex, f, ParseDeref());
                            }
                            pattern->Add(f);
                            if (lex.token == T_COLON) break;
                            Expect(T_COMMA);
                        }
                    }
                    auto body = BuiltinControlClosure(
                        ParseFunction(nullptr, false, false, false, "case"), 0);
                    cases->Add(new Case(lex, pattern, body));
                    if (!IsNext(T_LINEFEED)) break;
                    if (lex.token == T_DEDENT) break;
                }
                Expect(T_DEDENT);
                return new Switch(lex, value, cases);
            }
            default:
                Error("illegal start of expression: " + lex.TokStr());
                return nullptr;
        }
    }

    void ParseVector(const function<void()> &f, TType closing) {
        if (IsNext(closing)) return;
        assert(lex.token != T_INDENT);  // Not generated inside brackets/braces.
        for (;;) {
            f();
            if (!IsNext(T_COMMA) || lex.token == closing) break;
        }
        Expect(closing);
    }

    Node *IdentFactor(string_view idname) {
        if (IsNext(T_LEFTCURLY)) {
            auto &udt = st.StructUse(idname, lex);
            udt.constructed = true;
            vector<Node *> exps(udt.fields.size(), nullptr);
            ParseVector([&] () {
                auto id = lex.sattr;
                if (IsNext(T_IDENT)) {
                    if (IsNext(T_COLON)) {
                        auto fld = st.FieldUse(id);
                        auto field = udt.Has(fld);
                        if (field < 0) Error("unknown field: " + id);
                        if (exps[field]) Error("field initialized twice: " + id);
                        exps[field] = ParseExp();
                        return;
                    } else {
                        lex.Undo(T_IDENT, id);
                    }
                }
                // An initializer without a tag. Find first field without a default thats not
                // set yet.
                for (size_t i = 0; i < exps.size(); i++) {
                    if (!exps[i] && !udt.fields.v[i].defaultval) {
                        exps[i] = ParseExp();
                        return;
                    }
                }
                // Since this struct may be pre-declared, we allow to parse more initializers
                // than there are fields. We will catch this in the type checker.
                exps.push_back(ParseExp());
            }, T_RIGHTCURLY);
            // Now fill in defaults, check for missing fields, and construct list.
            auto constructor = new Constructor(lex, &udt.thistype);
            for (size_t i = 0; i < exps.size(); i++) {
                if (!exps[i]) {
                    if (udt.fields.v[i].defaultval)
                        exps[i] = udt.fields.v[i].defaultval->Clone();
                    else
                        Error("field not initialized: " + udt.fields.v[i].id->name);
                }
                constructor->Add(exps[i]);
            }
            return constructor;
        } else {
            // If we see "f(" the "(" is the start of an argument list, but for "f (", "(" is
            // part of an expression of a single argument with no extra "()".
            // This avoids things like "f (1 + 2) * 3" ("* 3" part of the single arg) being
            // interpreted as "f(1 + 2) * 3" (not part of the arg).
            // This is benign, since single arg calls with "()" work regardless of whitespace,
            // and multi-arg calls with whitespace will now error on the first "," (since we
            // don't have C's ","-operator).
            auto nf = natreg.FindNative(idname);
            auto f = st.FindFunction(idname);
            auto e = st.EnumLookup(idname, lex, false);
            if (lex.token == T_LEFTPAREN && lex.whitespacebefore == 0) {
                if (e && !f && !nf) {
                    lex.Next();
                    auto ec = new EnumCoercion(lex, ParseExp(), e);
                    Expect(T_RIGHTPAREN);
                    return ec;
                }
                return ParseFunctionCall(f, nf, idname, nullptr, false, false);
            }
            // Check for implicit variable.
            if (idname[0] == '_') {
                return new IdentRef(lex, st.LookupDef(idname, lex, true, false, false)->cursid);
            }
            auto id = st.Lookup(idname);
            // Check for function call without ().
            if (!id && (nf || f) && lex.whitespacebefore > 0) {
                return ParseFunctionCall(f, nf, idname, nullptr, false, true);
            }
            // Check for enum value.
            auto ev = st.EnumValLookup(idname, lex, false);
            if (ev) {
                auto ic = new IntConstant(lex, ev->val);
                ic->from = ev;
                return ic;
            }
            return IdentUseOrWithStruct(idname);
        }
    }

    Node *IdentUseOrWithStruct(string_view idname) {
        // Check for field reference in function with :: arguments.
        Ident *id = nullptr;
        auto fld = st.LookupWithStruct(idname, lex, id);
        if (fld) {
            auto dot = new GenericCall(lex, idname, nullptr, true);
            dot->Add(new IdentRef(lex, id->cursid));
            return dot;
        }
        // It's a regular variable.
        return new IdentRef(lex, st.LookupUse(idname, lex)->cursid);
    }

    bool IsNext(TType t) {
        bool isnext = lex.token == t;
        if (isnext) lex.Next();
        return isnext;
    }

    string_view lastid;

    bool IsNextId() {
        if (lex.token != T_IDENT) return false;
        lastid = lex.sattr;
        lex.Next();
        return true;
    }

    string_view ExpectId() {
        lastid = lex.sattr;
        Expect(T_IDENT);
        return lastid;
    }

    bool Either(TType t1, TType t2) {
        return lex.token == t1 || lex.token == t2;
    }
    bool Either(TType t1, TType t2, TType t3) {
        return lex.token == t1 || lex.token == t2 || lex.token == t3;
    }

    void Expect(TType t) {
        if (!IsNext(t))
            Error(lex.TokStr(t) + " expected, found: " + lex.TokStr());
    }

    string DumpAll(bool onlytypechecked = false) {
        string s;
        for (auto f : st.functiontable) {
            for (auto sf : f->overloads) {
                for (; sf; sf = sf->next) {
                    if (!onlytypechecked || sf->typechecked) {
                        s += "FUNCTION: " + f->name + "(";
                        for (auto &arg : sf->args.v) {
                            s += arg.sid->id->name + ":" + TypeName(arg.type) + " ";
                        }
                        s += ") -> ";
                        s += TypeName(sf->returntype);
                        s += "\n";
                        if (sf->body) s += DumpNode(*sf->body, 4, false);
                        s += "\n\n";
                    }
                }
            }
        }
        return s;
    }
};

}  // namespace lobster
