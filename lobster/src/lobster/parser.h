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
    Lex lex;
    List *root;
    SymbolTable &st;
    vector<Function *> functionstack;
    vector<string> trailingkeywordedfunctionvaluestack;
    struct ForwardFunctionCall { string idname; size_t maxscopelevel; Call *n; };
    vector<ForwardFunctionCall> forwardfunctioncalls;
    bool call_noparens;
    set<string> pakfiles;

    Parser(const char *_src, SymbolTable &_st, const char *_stringsource)
        : lex(_src, _st.filenames, _stringsource), root(nullptr), st(_st), call_noparens(false) {}

    ~Parser() {
        delete root;
    }

    void Error(string err, const Node *what = nullptr) {
        lex.Error(err, what ? &what->line : nullptr);
    }

    void Parse() {
        auto sf = st.ScopeStart();
        st.toplevel = sf;
        auto &f = st.CreateFunction("__top_level_expression", "");
        sf->SetParent(f, f.subf);
        f.anonymous = true;

        lex.Include("stdtype.lobster");

        sf->body = ParseStatements();
        st.ScopeCleanup();
        root = (new List(lex))
            ->Add(new FunRef(lex, sf))
            ->Add(new Call(lex, sf));
        Expect(T_ENDOFFILE);
        assert(forwardfunctioncalls.empty());
    }

    List *ParseStatements() {
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
        ResolveForwardFunctionCalls();
        for (auto def : list->children) {
            if (auto sr = Is<StructRef>(def)) {
                st.UnregisterStruct(sr->st, lex);
            } else if (auto fr = Is<FunRef>(def)) {
                auto f = fr->sf->parent;
                if (!f->anonymous) st.UnregisterFun(f);
            } else if (auto d = Is<Define>(def)) {
                for (auto sid : d->sids) {
                    sid->id->static_constant =
                        sid->id->single_assignment && d->child->IsConstInit();
                }
            }
        }
        return list;
    }

    void ParseVector(const function<void ()> &f, TType closing) {
        if (IsNext(closing)) return;
        bool indented = IsNext(T_INDENT);
        for (;;) {
            f();
            if (!IsNext(T_COMMA)) break;
        }
        if (indented) {
            IsNext(T_LINEFEED);
            Expect(T_DEDENT);
            Expect(T_LINEFEED);
        }
        Expect(closing);
    }

    void ParseIndentedorVector(const function<void()> &f, TType opening, TType closing) {
        bool isindent = IsNext(T_INDENT);
        if (!isindent) {
            Expect(opening);
            ParseVector(f, closing);
        } else {
            for (;;) {
                f();
                if (!IsNext(T_LINEFEED)) break;
                if (Either(T_ENDOFFILE, T_DEDENT)) break;
            }
            Expect(T_DEDENT);
        }
    }

    SpecIdent *DefineWith(const string &idname, bool isprivate, bool isdef, bool islogvar) {
        auto id = isdef ? st.LookupDef(idname, lex.errorline, lex, false, true, false)
                        : st.LookupUse(idname, lex);
        if (islogvar) st.MakeLogVar(id);
        if (isprivate) {
            if (!isdef) Error("assignment cannot be made private");
            id->isprivate = true;
        }
        return id->cursid;
    }

    AssignList *RecMultiDef(const string &idname, bool isprivate, int nids, bool &isdef,
                            bool &islogvar) {
        AssignList *al = nullptr;
        if (IsNextId()) {
            string id2 = lastid;
            nids++;
            if (Either(T_DEF, T_LOGASSIGN, T_ASSIGN)) {
                isdef = lex.token != T_ASSIGN;
                islogvar = lex.token == T_LOGASSIGN;
                lex.Next();
                int nrv;
                auto initial = ParseMultiRet(ParseOpExp(), nrv);
                auto id = DefineWith(id2, isprivate, isdef, islogvar);
                if (isdef) al = new Define(lex, id, initial, nullptr);
                else al = new AssignList(lex, id, initial);
                if (nrv > 1 && nrv != nids)
                    Error("number of values doesn't match number of variables");
            } else if (IsNext(T_COMMA)) {
                al = RecMultiDef(id2, isprivate, nids, isdef, islogvar);
            } else {
                lex.Undo(T_IDENT, id2);
            }
        }
        if (al) {
            auto sid = DefineWith(idname, isprivate, isdef, islogvar);
            al->sids.insert(al->sids.begin(), sid);
        } else {
            lex.Undo(T_COMMA);
            lex.Undo(T_IDENT, idname);
        }
        return al;
    }

    void ParseTopExp(List *list, bool isprivate = false) {
        switch(lex.token) {
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
                string fn = lex.sattr;
                Expect(T_STR);
                Expect(T_LINEFEED);
                lex.Include((char *)fn.c_str());
                ParseTopExp(list);
                break;
            }
            case T_VALUE:  ParseTypeDecl(true,  isprivate, list); break;
            case T_STRUCT: ParseTypeDecl(false, isprivate, list); break;
            case T_FUN: {
                lex.Next();
                list->Add(ParseNamedFunctionDefinition(isprivate, nullptr));
                break;
            }
            case T_ENUM: {
                lex.Next();
                bool incremental = IsNext(T_PLUS) || !IsNext(T_MULT);
                int cur = incremental ? 0 : 1;
                for (;;) {
                    ExpectId();
                    auto id = st.LookupDef(lastid, lex.errorline, lex, false, true, false);
                    id->constant = true;
                    if (isprivate) id->isprivate = true;
                    if (IsNext(T_ASSIGN)) {
                        cur = atoi(lex.sattr.c_str());
                        Expect(T_INT);
                    }
                    list->Add(new Define(lex, id->cursid, new IntConstant(lex, cur), nullptr));
                    if (lex.token != T_COMMA) break;
                    lex.Next();
                    if (incremental) cur++; else cur *= 2;
                }
                break;
            }
            case T_VAR:
            case T_CONST: {
                auto isconst = lex.token == T_CONST;
                lex.Next();
                ExpectId();
                Expect(T_ASSIGN);
                list->Add(ParseSingleVarDecl(isprivate, isconst, false, false));
                break;
                break;
            }
            default: {
                if (IsNextId()) {
                    auto d = ParseVarDecl(isprivate);
                    if (d) {
                        list->Add(d);
                        break;
                    }
                }
                if (isprivate)
                    Error("private only applies to declarations");
                list->Add(ParseExpStat());
                break;
            }
        }
    }

    void ParseTypeDecl(bool isvalue, bool isprivate, List *parent_list) {
        lex.Next();
        auto sname = ExpectId();
        Struct *struc = &st.StructDecl(lastid, lex);
        Struct *sup = nullptr;
        auto parse_sup = [&] () {
            ExpectId();
            sup = &st.StructUse(lastid, lex);
        };
        vector<pair<TypeRef, Node *>> spectypes;
        auto parse_specializers = [&] () {
            if (IsNext(T_LEFTPAREN)) {
                for (;;) {
                    spectypes.push_back(make_pair(nullptr, nullptr));
                    ParseType(spectypes.back().first, false);
                    spectypes.back().second = IsNext(T_ASSIGN) ? ParseExp() : nullptr;
                    if (IsNext(T_RIGHTPAREN)) break;
                    Expect(T_COMMA);
                }
            }
        };
        size_t specializer_i = 0;
        auto specialize_field = [&] (Field &field) {
            if (field.flags & AF_ANYTYPE) {
                if (specializer_i >= spectypes.size()) Error("too few type specializers");
                auto &p = spectypes[specializer_i++];
                field.type = p.first;
                if (p.second) { assert(!field.defaultval); field.defaultval = p.second; }
            }
        };
        if (IsNext(T_ASSIGN)) {
            // A specialization of an existing struct
            parse_sup();
            struc = sup->CloneInto(struc);
            struc->idx = (int)st.structtable.size() - 1;
            struc->name = sname;
            struc->generic = false;
            parse_specializers();
            if (!spectypes.size())
                Error("no specialization types specified");
            if (!sup->generic)
                Error("you can only specialize a generic struct/value");
            if (isvalue != sup->readonly)
                Error("specialization must use same struct/value keyword");
            if (isprivate != sup->isprivate)
                Error("specialization must have same privacy level");
            for (auto &field : struc->fields.v) {
                // We don't reset AF_ANYTYPE here, because its used to know which fields to select
                // a specialization on.
                specialize_field(field);
                struc->Resolve(field);
            }
            if (struc->superclass) {
                // This points to a generic version of the superclass of this class.
                // See if we can find a matching specialization instead.
                auto sti = struc->superclass->next;
                for (; sti; sti = sti->next) {
                    for (size_t i = 0; i < sti->fields.size(); i++)
                        if (sti->fields.v[i].type != struc->fields.v[i].type)
                            goto fail;
                    goto done;
                    fail:;
                }
                done:
                struc->superclass = sti;  // Either a match or nullptr.
            }
        } else if (Either(T_COLON, T_LEFTCURLY)) {
            // A regular struct declaration
            struc->readonly = isvalue;
            struc->isprivate = isprivate;
            if (IsNext(T_COLON) && lex.token != T_INDENT) {
                parse_sup();
                parse_specializers();
            }
            int fieldid = 0;
            if (sup) {
                struc->superclass = sup;
                for (auto &fld : sup->fields.v) {
                    struc->fields.v.push_back(fld);
                    auto &field = struc->fields.v.back();
                    // FIXME: must check if this type is a subtype if old type isn't V_ANY
                    if (spectypes.size()) specialize_field(field);
                    if (st.IsGeneric(field.type)) struc->generic = true;
                }
                fieldid = (int)sup->fields.size();
            }
            bool fieldsdone = false;
            auto finishfields = [&]() {
                if (!fieldsdone) {
                    // Loop thru fields again, because this type may have become generic just now,
                    // and may refer to itself.
                    for (auto &field : struc->fields.v) {
                        if (st.IsGeneric(field.type) && field.fieldref < 0)
                            field.flags = AF_ANYTYPE;
                    }
                    fieldsdone = true;
                }
            };
            ParseIndentedorVector([&] () {
                if (IsNext(T_FUN)) {
                    finishfields();
                    parent_list->Add(ParseNamedFunctionDefinition(false, struc));
                } else {
                    ExpectId();
                    if (fieldsdone) Error("fields must be declared before methods");
                    auto &sfield = st.FieldDecl(lastid);
                    TypeRef type;
                    int fieldref = -1;
                    if (IsNext(T_COLON)) {
                        fieldref = ParseType(type, false, struc);
                    }
                    Node *defaultval = IsNext(T_ASSIGN) ? ParseExp() : nullptr;
                    bool generic = st.IsGeneric(type) && fieldref < 0; // && !defaultval;
                    struc->fields.v.push_back(Field(&sfield, type, generic, fieldref, defaultval));
                    if (generic) struc->generic = true;
                }
            }, T_LEFTCURLY, T_RIGHTCURLY);
            finishfields();
        } else {
            // A pre-declaration.
            struc->predeclaration = true;
        }
        if (specializer_i < spectypes.size()) Error("too many type specializers");
        parent_list->Add(new StructRef(lex, struc));
    }

    Node *ParseSingleVarDecl(bool isprivate, bool constant, bool dynscope, bool logvar) {
        auto idname = lastid;
        auto e = ParseExp();
        auto id = dynscope
            ? st.LookupDynScopeRedef(idname, lex)
            : st.LookupDef(idname, lex.errorline, lex, false, true, false);
        if (dynscope)  id->Assign(lex);
        if (constant)  id->constant = true;
        if (isprivate) id->isprivate = true;
        if (logvar)    st.MakeLogVar(id);
        return new Define(lex, id->cursid, e, nullptr);
    }

    Node *ParseVarDecl(bool isprivate) {
        bool dynscope = lex.token == T_DYNASSIGN;
        bool constant = lex.token == T_DEFCONST;
        bool logvar = lex.token == T_LOGASSIGN;
        // codegen assumes these defs can only happen at toplevel
        if (lex.token == T_DEF || dynscope || constant || logvar) {
            lex.Next();
            return ParseSingleVarDecl(isprivate, constant, dynscope, logvar);
        }
        auto idname = lastid;
        bool withtype = lex.token == T_TYPEIN;
        if (lex.token == T_COLON || withtype) {
            lex.Next();
            TypeRef type;
            ParseType(type, withtype);
            Expect(T_ASSIGN);
            auto e = ParseExp();
            auto id = st.LookupDef(idname, lex.errorline, lex, false, true, withtype);
            if (isprivate) id->isprivate = true;
            return new Define(lex, id->cursid, e, type);
        }
        if (IsNext(T_COMMA)) {
            bool isdef = false;
            bool islogvar = false;
            auto e = RecMultiDef(idname, isprivate, 1, isdef, islogvar);
            if (e) return e;
        } else {
            lex.Undo(T_IDENT, idname);
        }
        return nullptr;
    }

    Node *ParseNamedFunctionDefinition(bool isprivate, Struct *self) {
        string idname = ExpectId();
        if (natreg.FindNative(idname))
            Error("cannot override built-in function: " + idname);
        return ParseFunction(&idname, isprivate, true, true, "", false, false, self);
    }

    Node *ParseFunction(const string *name,
                        bool isprivate, bool parens, bool parseargs,
                        const string &context,
                        bool expfunval = false, bool parent_noparens = false, Struct *self = nullptr) {
        auto sf = st.ScopeStart();
        if (parens) Expect(T_LEFTPAREN);
        size_t nargs = 0;
        if (self) {
            nargs++;
            auto id = st.LookupDef("this", lex.errorline, lex, false, false, true);
            auto type = &self->thistype;
            st.AddWithStruct(type, id, lex);
            sf->args.v.back().SetType(type, st.IsGeneric(type), true);
        }
        if (lex.token != T_RIGHTPAREN && parseargs) {
            for (;;) {
                ExpectId();
                nargs++;
                auto id = st.LookupDef(lastid, lex.errorline, lex, false, false, false);
                TypeRef type;
                bool withtype = lex.token == T_TYPEIN;
                if (parens && (lex.token == T_COLON || withtype)) {
                    lex.Next();
                    ParseType(type, withtype);
                    if (withtype) st.AddWithStruct(type, id, lex);
                }
                sf->args.v.back().SetType(type, st.IsGeneric(type), withtype);
                if (!IsNext(T_COMMA)) break;
            }
        }
        if (parens) Expect(T_RIGHTPAREN);
        auto &f = name ? st.FunctionDecl(*name, nargs, lex) : st.CreateFunction("", context);
        sf->SetParent(f, f.subf);
        if (name && IsNext(T_DEFCONST)) {
            if (f.istype || f.subf->next)
                Error("redefinition of function type: " + *name);
            f.istype = true;
            sf->typechecked = true;
            // Any untyped args truely mean "any", they should not be specialized (we wouldn't know
            // which specialization that refers to).
            for (auto &arg : f.subf->args.v) {
                if (arg.flags & AF_ANYTYPE) arg.flags = AF_NONE;
            }
            ParseType(sf->returntypes[0], false, nullptr, sf);
        } else {
            if (IsNext(T_CODOT)) {  // Return type decl.
                sf->fixedreturntype = true;
                ParseType(sf->returntypes[0], false, nullptr, sf);
            }
            if (!expfunval) Expect(T_COLON);
        }
        if (name) {
            if (f.subf->next) {
                f.multimethod = true;
                if (isprivate != f.isprivate)
                    Error("inconsistent private annotation of multiple function implementations"
                          " for " + *name);
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
                sf->body = ParseStatements();
                Expect(T_DEDENT);
            } else {
                sf->body = (new List(lex))->Add(ParseExpStat());
            }
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
        if (name) {
            functionstack.pop_back();

            if (!f.istype) {
                auto last = sf->body->children.back();
                auto ret = Is<Return>(last);
                if (!ret ||
                    ret->subfunction_idx != sf->idx /* return from */)
                    ReturnValues(f, 1);
                assert(f.nretvals);
            }
        }
        // Keep copy or arg types from before specialization.
        f.orig_args = sf->args;  // not used for multimethods
        return new FunRef(lex, sf);
    }

    int ParseType(TypeRef &dest, bool withtype, Struct *fieldrefstruct = nullptr,
                  SubFunction *sfreturntype = nullptr) {
        switch(lex.token) {
            case T_INTTYPE:   dest = type_int;        lex.Next(); break;
            case T_FLOATTYPE: dest = type_float;      lex.Next(); break;
            case T_STRTYPE:   dest = type_string;     lex.Next(); break;
            case T_COROUTINE: dest = type_coroutine;  lex.Next(); break;
            case T_RESOURCE:  dest = type_resource;   lex.Next(); break;
            case T_ANYTYPE:   dest = type_any;        lex.Next(); break;
            case T_IDENT: {
                if (fieldrefstruct) {
                    for (auto &field : fieldrefstruct->fields.v) {
                        if (field.id->name == lex.sattr) {
                            if (!(field.flags & AF_ANYTYPE))
                                Error("field reference must be to generic field: " + lex.sattr);
                            lex.Next();
                            dest = field.type;
                            return int(&field - &fieldrefstruct->fields.v[0]);
                        }
                    }
                }
                auto f = st.FindFunction(lex.sattr);
                if (f && f->istype) {
                    dest = &f->subf->thistype;
                } else {
                    auto &struc = st.StructUse(lex.sattr, lex);
                    dest = &struc.thistype;
                }
                lex.Next();
                break;
            }
            case T_LEFTBRACKET: {
                lex.Next();
                TypeRef elem;
                ParseType(elem, false);
                Expect(T_RIGHTBRACKET);
                dest = elem->Wrap(st.NewType());
                break;
            }
            case T_LEFTPAREN:
                if (sfreturntype)  {
                    lex.Next();
                    Expect(T_RIGHTPAREN);
                    dest = type_any;
                    sfreturntype->reqret = false;
                    break;
                }
                // FALL-THRU:
            default:
                Error("illegal type syntax: " + lex.TokStr());
        }
        if (IsNext(T_QUESTIONMARK)) {
            if (dest->Numeric()) Error("numeric types can\'t be made nilable");
            dest = dest->Wrap(st.NewType(), V_NIL);
        }
        if (withtype && dest->t != V_STRUCT) Error(":: must be used with a struct type");
        return -1;
    }

    void ParseFunArgs(List *list, bool coroutine, Node *derefarg, const char *fname = "",
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
                         size_t thisarg, const char *fname, bool noparens) {
        if (!noparens && IsNext(T_RIGHTPAREN)) {
            if (call_noparens) {
                // Don't unnecessarily parse funvals. Means "if f(x):" parses as expected.
                return;
            }
            ParseTrailingFunctionValues(list, coroutine, args, thisarg, fname);
            return;
        }
        if (needscomma) Expect(T_COMMA);
        CheckArg(args, thisarg, fname);
        if (args && args->GetType(thisarg)->flags == NF_EXPFUNVAL) {
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

    void CheckArg(GenericArgs *args, size_t thisarg, const char *fname) {
        if (args && thisarg == args->size())
            Error("too many arguments passed to function " + string(fname));
    }

    void ParseTrailingFunctionValues(List *list, bool coroutine, GenericArgs *args, size_t thisarg,
                                     const char *fname) {
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

    Node *ParseMultiRet(Node *first, int &nrv) {
        nrv = 1;
        if (lex.token != T_COMMA) return first;
        auto list = new MultipleReturn(lex);
        list->Add(first);
        while (IsNext(T_COMMA)) {
            list->Add(ParseOpExp());
            nrv++;
        }
        return list;
    }

    void ReturnValues(Function &f, int nrv) {
        if (f.nretvals && f.nretvals != nrv)
            Error(string("all return statements of this function must return the same number of"
                         " return values. previously: ") + to_string(f.nretvals));
        f.nretvals = nrv;
    }

    Node *ParseExpStat() {
        if (IsNext(T_RETURN)) {
            Node *rv = nullptr;
            int nrv = 0;
            if (!Either(T_LINEFEED, T_DEDENT, T_FROM)) {
                rv = ParseMultiRet(ParseOpExp(), nrv);
                if (auto call = Is<Call>(rv))
                    nrv = max(nrv, call->sf->parent->nretvals);
            } else {
                rv = new DefaultVal(lex);
            }
            int sfid = -2;
            if (IsNext(T_FROM)) {
                if(!IsNext(T_PROGRAM)) {
                    if (!IsNextId())
                        Error("return from: must be followed by function identifier or"
                              " \"program\"");
                    auto f = st.FindFunction(lastid);
                    if (!f)
                        Error("return from: not a known function");
                    if (f->sibf || f->multimethod)
                        Error("return from: function must have single implementation");
                    sfid = f->subf->idx;
                }
            } else {
                if (functionstack.size())
                    sfid = functionstack.back()->subf->idx;
            }
            if (sfid >= 0)
                ReturnValues(*st.subfunctiontable[sfid]->parent, nrv);
            else if (nrv > 1)
                Error("cannot return multiple values from top level");
            return new Return(lex, rv, sfid);
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

    void Modify(Node *e) {
        if (auto idr = Is<IdentRef>(e))
            idr->sid->id->Assign(lex);
    }

    void CheckOpEq(Node *e) {
        if (!Is<IdentRef>(e) && !Is<Dot>(e) && !Is<CoDot>(e) && !Is<Indexing>(e))
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
        }
        return e;
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
        if (t == T_INCR || t == T_DECR) Modify(e);
        return e;
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
            Error("body has " + to_string(clnargs - maxargs) + " parameters too many", funval);
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

    Node *ParseFunctionCall(Function *f, NativeFun *nf, const string &idname, Node *firstarg,
                            bool coroutine, bool noparens) {
        if (nf) {
            auto nc = new NativeCall(lex, nf);
            ParseFunArgs(nc, coroutine, firstarg, idname.c_str(), &nf->args, noparens);
            size_t i = 0;
            for (auto &arg : nf->args.v) {
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
                i++;
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
            auto call = new Call(lex, nullptr);
            if (!firstarg && f->nargs()) {
                auto wse = st.GetWithStackBack();
                // If we're in the context of a withtype, calling a function that starts with an
                // arg of the same type we pass it in automatically.
                // This is maybe a bit very liberal, should maybe restrict it?
                if (wse &&
                    wse->first == f->subf->args.v[0].type &&
                    f->subf->args.v[0].flags & AF_WITHTYPE) {
                    firstarg = new IdentRef(lex, wse->second->cursid);
                }
            }
            ParseFunArgs(call, coroutine, firstarg, idname.c_str(), &bestf->subf->args, noparens);
            auto nargs = call->Arity();
            f = FindFunctionWithNargs(f, nargs, idname, nullptr);
            call->sf = f->subf;
            return call;
        }
        if (id) {
            auto dc = new DynCall(lex, nullptr, id->cursid);
            ParseFunArgs(dc, coroutine, firstarg);
            return dc;
        } else {
            auto call = new Call(lex, nullptr);
            ParseFunArgs(call, coroutine, firstarg);
            ForwardFunctionCall ffc = { idname, st.scopelevels.size(), call };
            forwardfunctioncalls.push_back(ffc);
            return call;
        }
    }

    Function *FindFunctionWithNargs(Function *f, size_t nargs, const string &idname,
                                    Node *errnode) {
        for (; f; f = f->sibf)
            if (f->nargs() == nargs)
                return f;
        Error("no version of function " + idname + " takes " + to_string(nargs) + " arguments",
              errnode);
        return nullptr;
    }

    void ResolveForwardFunctionCalls() {
        for (auto ffc = forwardfunctioncalls.begin(); ffc != forwardfunctioncalls.end(); ) {
            if (ffc->maxscopelevel >= st.scopelevels.size()) {
                auto f = st.FindFunction(ffc->idname);
                if (f) {
                    ffc->n->sf = FindFunctionWithNargs(f,
                        ffc->n->Arity(), ffc->idname, ffc->n)->subf;
                    ffc = forwardfunctioncalls.erase(ffc);
                    continue;
                } else {
                    if (st.scopelevels.size() == 1)
                        Error("call to unknown function: " + ffc->idname, ffc->n);
                    // Prevent it being found in sibling scopes.
                    ffc->maxscopelevel = st.scopelevels.size() - 1;
                }
            }
            ffc++;
        }
    }

    Node *ParseDeref() {
        auto n = ParseFactor();
        for (;;) switch (lex.token) {
            case T_DOT:
            case T_DOTMAYBE:
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
                    SharedField *fld = st.FieldUse(idname);
                    if (fld)  {
                        n = new Dot(lex, n, fld, op == T_DOTMAYBE);
                    } else {
                        auto f = st.FindFunction(idname);
                        auto nf = natreg.FindNative(idname);
                        if ((f || nf) && op == T_DOT) {
                            n = ParseFunctionCall(f, nf, idname, n, false, false);
                        } else {
                            Error("not a type member or function: " + idname);
                        }
                    }
                }
                break;
            }
            case T_LEFTBRACKET: {
                lex.Next();
                n = new Indexing(lex, n, ParseExp());
                Expect(T_RIGHTBRACKET);
                break;
            }
            case T_INCR:
                Modify(n);
                n = new PostIncr(lex, n);
                lex.Next();
                return n;
            case T_DECR:
                Modify(n);
                n = new PostDecr(lex, n);
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
                int64_t i = atoll(lex.sattr.c_str());
                lex.Next();
                return new IntConstant(lex, i);
            }
            case T_FLOAT: {
                double f = atof(lex.sattr.c_str());
                lex.Next();
                return new FloatConstant(lex, f);
            }
            case T_STR: {
                string s = lex.sattr;
                lex.Next();
                return new StringConstant(lex, s);
            }
            case T_NIL: {
                lex.Next();
                auto n = new Nil(lex, nullptr);
                if (IsNext(T_COLON)) {
                    ParseType(n->giventype, false);
                    n->giventype = n->giventype->Wrap(st.NewType(), V_NIL);
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
                auto constructor = new Constructor(lex, type_vector_any);
                ParseVector([this, &constructor] () {
                    constructor->Add(this->ParseExp());
                }, T_RIGHTBRACKET);
                if (IsNext(T_TYPEIN)) {
                    ParseType(constructor->giventype, false);
                    constructor->giventype = constructor->giventype->Wrap(st.NewType());
                }
                return constructor;
            }
            case T_FUN: {
                lex.Next();
                return ParseFunction(nullptr, false, true, true, "");
            }
            case T_COROUTINE: {
                lex.Next();
                string idname = ExpectId();
                auto n = ParseFunctionCall(st.FindFunction(idname), nullptr, idname, nullptr, true,
                                           false);
                if (auto call = Is<Call>(n)) {
                    return new CoRoutine(lex, call);
                } else {
                    Error("coroutine constructor must be regular function call");
                }
            }
            case T_FLOATTYPE:
            case T_INTTYPE:
            case T_STRTYPE:
            case T_ANYTYPE: {
                // These are also used as built-in functions, so allow them to function as
                // identifier for calls.
                string idname = lex.sattr;
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
                string idname = lex.sattr;
                lex.Next();
                return IdentFactor(idname);
            }
            case T_PAKFILE: {
                lex.Next();
                string s = lex.sattr;
                Expect(T_STR);
                pakfiles.insert(s);
                return new StringConstant(lex, s);
            }
            default:
                Error("illegal start of expression: " + lex.TokStr());
                return nullptr;
        }
    }

    Node *IdentFactor(const string &idname) {
        if (IsNext(T_LEFTCURLY)) {
            auto &struc = st.StructUse(idname, lex);
            vector<Node *> exps(struc.fields.size(), nullptr);
            ParseVector([&] () {
                auto id = lex.sattr;
                if (IsNext(T_IDENT)) {
                    if (IsNext(T_COLON)) {
                        auto fld = st.FieldUse(id);
                        auto field = struc.Has(fld);
                        if (field < 0) Error("unknown field: " + id);
                        if (exps[field]) Error("field initialized twice: " + id);
                        exps[field] = ParseExp();
                        return;
                    } else {  // Undo
                        lex.PushCur();
                        lex.Push(T_IDENT, id);
                        lex.Next();
                    }
                }
                // An initializer without a tag. Find first field without a default thats not
                // set yet.
                for (size_t i = 0; i < exps.size(); i++) {
                    if (!exps[i] && !struc.fields.v[i].defaultval) {
                        exps[i] = ParseExp();
                        return;
                    }
                }
                Error("too many initializers for: " + struc.name);
            }, T_RIGHTCURLY);
            // Now fill in defaults, check for missing fields, and construct list.
            auto constructor = new Constructor(lex, &struc.thistype);
            for (size_t i = 0; i < exps.size(); i++) {
                if (!exps[i]) {
                    if (struc.fields.v[i].defaultval)
                        exps[i] = struc.fields.v[i].defaultval->Clone();
                    else
                        Error("field not initialized: " + struc.fields.v[i].id->name);
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
            // don't have C's ","-operator.
            auto nf = natreg.FindNative(idname);
            auto f = st.FindFunction(idname);
            if (lex.token == T_LEFTPAREN && lex.whitespacebefore == 0) {
                return ParseFunctionCall(f, nf, idname, nullptr, false, false);
            }
            // Check for implicit variable.
            if (idname[0] == '_') {
                return new IdentRef(lex, st.LookupDef(idname, lex.errorline, lex, true, false,
                                                      false)->cursid);
            }
            auto id = st.Lookup(idname);
            // Check for function call without ().
            if (!id && (nf || f) && lex.whitespacebefore > 0) {
                return ParseFunctionCall(f, nf, idname, nullptr, false, true);
            }
            // Check for field reference in function with :: arguments.
            id = nullptr;
            auto fld = st.LookupWithStruct(idname, lex, id);
            if (fld) {
                return new Dot(lex, new IdentRef(lex, id->cursid), fld, false);
            }
            // It's a regular variable.
            return new IdentRef(lex, st.LookupUse(idname, lex)->cursid);
        }
    }

    bool IsNext(TType t) {
        bool isnext = lex.token == t;
        if (isnext) lex.Next();
        return isnext;
    }

    string lastid;

    bool IsNextId() {
        if (lex.token != T_IDENT) return false;
        lastid = lex.sattr;
        lex.Next();
        return true;
    }

    const string &ExpectId() {
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
            for (auto sf = f->subf; sf; sf = sf->next) {
                if (!onlytypechecked || sf->typechecked) {
                    s += "FUNCTION: " + f->name + "(";
                    for (auto &arg : sf->args.v) {
                        s += arg.sid->id->name + ":" + TypeName(arg.type) + " ";
                    }
                    s += ")\n";
                    if (sf->body) s += Dump(*sf->body, 4);
                    s += "\n\n";
                }
            }
        }
        return s + "TOPLEVEL:\n" + Dump(*root, 0);
    }
};

}  // namespace lobster
