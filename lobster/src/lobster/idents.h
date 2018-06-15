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

struct NativeFun;
struct SymbolTable;

struct Node;
struct List;

struct SubFunction;

struct SpecIdent;

struct Ident : Named {
    int line;
    size_t scopelevel;

    // TODO: remove this from Ident, only makes sense during parsing really.
    SubFunction *sf_def;    // Where it is defined, including anonymous functions.

    bool single_assignment;
    bool constant;
    bool static_constant;
    bool anonymous_arg;
    bool logvar;

    SpecIdent *cursid;

    Ident(string_view _name, int _l, int _idx, size_t _sl)
        : Named(_name, _idx), line(_l),
          scopelevel(_sl), sf_def(nullptr),
          single_assignment(true), constant(false), static_constant(false), anonymous_arg(false),
          logvar(false), cursid(nullptr) {}

    void Assign(Lex &lex) {
        single_assignment = false;
        if (constant)
            lex.Error("variable " + name + " is constant");
    }

    flatbuffers::Offset<bytecode::Ident> Serialize(flatbuffers::FlatBufferBuilder &fbb,
                                                   SubFunction *toplevel) {
        return bytecode::CreateIdent(fbb, fbb.CreateString(name), constant, sf_def == toplevel);
    }
};

struct SpecIdent {
    Ident *id;
    TypeRef type;
    int logvaridx;
    int sidx;

    SpecIdent(Ident *_id, TypeRef _type)
        : id(_id), type(_type), logvaridx(-1), sidx(-1) {}
    int Idx() { assert(sidx >= 0); return sidx; }
    SpecIdent *&Current() { return id->cursid; }
};

// Only still needed because we have no idea which struct it refers to at parsing time.
struct SharedField : Named {
    SharedField(string_view _name, int _idx) : Named(_name, _idx) {}
    SharedField() : SharedField("", 0) {}
};

struct Field : Typed {
    SharedField *id;
    int fieldref;
    Node *defaultval;

    Field() : Typed(), id(nullptr), fieldref(-1), defaultval(nullptr) {}
    Field(SharedField *_id, TypeRef _type, ArgFlags _flags, int _fieldref, Node *_defaultval)
        : Typed(_type, _flags), id(_id),
          fieldref(_fieldref), defaultval(_defaultval) {}
    Field(const Field &o);
    ~Field();
};

struct FieldVector : GenericArgs {
    vector<Field> v;

    FieldVector(int nargs) : v(nargs) {}

    size_t size() const { return v.size(); }
    const Typed *GetType(size_t i) const { return &v[i]; }
    string_view GetName(size_t i) const { return v[i].id->name; }
};

struct Struct : Named {
    FieldVector fields;
    Struct *next, *first;
    Struct *superclass;
    Struct *firstsubclass, *nextsubclass;  // Used in codegen.
    bool readonly;
    bool generic;
    bool predeclaration;
    Type thistype;         // convenient place to store the type corresponding to this.
    TypeRef sametype;      // If all fields are int/float, this allows vector ops.
    type_elem_t typeinfo;  // Runtime type.

    Struct(string_view _name, int _idx)
        : Named(_name, _idx), fields(0), next(nullptr), first(this), superclass(nullptr),
          firstsubclass(nullptr), nextsubclass(nullptr),
          readonly(false), generic(false), predeclaration(false),
          thistype(V_STRUCT, this),
          sametype(type_any),
          typeinfo((type_elem_t)-1) {}
    Struct() : Struct("", 0) {}

    int Has(SharedField *fld) {
        for (auto &uf : fields.v) if (uf.id == fld) return int(&uf - &fields.v[0]);
        return -1;
    }

    Struct *CloneInto(Struct *st) {
        *st = *this;
        st->thistype = Type(V_STRUCT, st);
        st->next = next;
        st->first = first;
        next = st;
        return st;
    }

    bool IsSpecialization(Struct *other) {
        if (generic) {
            for (auto struc = first->next; struc; struc = struc->next)
                if (struc == other)
                    return true;
            return false;
        } else {
            return this == other;
        }
    }

    void Resolve(Field &field) {
        if (field.fieldref >= 0) field.type = fields.v[field.fieldref].type;
    }

    flatbuffers::Offset<bytecode::Struct> Serialize(flatbuffers::FlatBufferBuilder &fbb) {
        return bytecode::CreateStruct(fbb, fbb.CreateString(name), idx, (int)fields.size());
    }
};

struct Arg : Typed {
    SpecIdent *sid;

    Arg() : Typed(), sid(nullptr) {}
    Arg(const Arg &o) : Typed(o), sid(o.sid) {}
    Arg(SpecIdent *_sid, TypeRef _type, ArgFlags _flags) : Typed(_type, _flags), sid(_sid) {}
};

struct ArgVector : GenericArgs {
    vector<Arg> v;

    ArgVector(int nargs) : v(nargs) {}

    size_t size() const { return v.size(); }
    const Typed *GetType(size_t i) const { return &v[i]; }
    string_view GetName(size_t i) const { return v[i].sid->id->name; }

    bool Add(const Arg &in) {
        for (auto &arg : v)
            if (arg.sid->id == in.sid->id)
                return false;
        v.push_back(in);
        return true;
    }
};

struct Function;

struct SubFunction {
    int idx;
    ArgVector args;
    ArgVector locals;
    ArgVector dynscoperedefs; // any lhs of <-
    ArgVector freevars;       // any used from outside this scope, could overlap with dynscoperedefs
    vector<TypeRef> returntypes;
    bool reqret;  // Do the caller(s) want values to be returned?
    bool iscoroutine;
    ArgVector coyieldsave;
    TypeRef coresumetype;
    type_elem_t cotypeinfo;
    List *body;
    SubFunction *next;
    Function *parent;
    int subbytecodestart;
    bool typechecked, freevarchecked, mustspecialize, fixedreturntype, logvarcallgraph;
    int numcallers;
    Type thistype;       // convenient place to store the type corresponding to this

    SubFunction(int _idx)
        : idx(_idx),
          args(0), locals(0), dynscoperedefs(0), freevars(0), reqret(true),
          iscoroutine(false), coyieldsave(0), cotypeinfo((type_elem_t)-1),
          body(nullptr), next(nullptr), parent(nullptr), subbytecodestart(0),
          typechecked(false), freevarchecked(false), mustspecialize(false),
          fixedreturntype(false), logvarcallgraph(false), numcallers(0),
          thistype(V_FUNCTION, this) {
        returntypes.push_back(type_any);  // functions always have at least 1 return value.
    }

    void SetParent(Function &f, SubFunction *&link) {
        parent = &f;
        next = link;
        link = this;
    }

    ~SubFunction();
};

struct Function : Named {
    int bytecodestart;
    // functions with the same name and args, but different types (dynamic dispatch |
    // specialization)
    SubFunction *subf;
    // functions with the same name but different number of args (overloaded)
    Function *sibf;
    // if false, subfunctions can be generated by type specialization as opposed to programmer
    // implemented dynamic dispatch
    bool multimethod;
    TypeRef multimethodretval;
    // does not have a programmer specified name
    bool anonymous;
    // its merely a function type, has no body, but does have a set return type.
    bool istype;
    // Store the original types the function was declared with, before specialization.
    ArgVector orig_args;
    size_t scopelevel;
    // 0 for anonymous functions, and for named functions to indicate no return has happened yet.
    // 0 implies 1, all function return at least 1 value.
    int nretvals;

    Function(string_view _name, int _idx, size_t _sl)
     : Named(_name, _idx), bytecodestart(0),  subf(nullptr), sibf(nullptr),
       multimethod(false), anonymous(false), istype(false), orig_args(0),
       scopelevel(_sl), nretvals(0) {
    }
    ~Function() {}

    size_t nargs() { return subf->args.v.size(); }

    int NumSubf() {
        int sum = 0;
        for (auto sf = subf; sf; sf = sf->next) sum++;
        return sum;
    }

    bool RemoveSubFunction(SubFunction *sf) {
        for (auto sfp = &subf; *sfp; sfp = &(*sfp)->next) if (*sfp == sf) {
            *sfp = sf->next;
            sf->next = nullptr;
            return true;
        }
        return false;
    }

    flatbuffers::Offset<bytecode::Function> Serialize(flatbuffers::FlatBufferBuilder &fbb) {
        return bytecode::CreateFunction(fbb, fbb.CreateString(name), bytecodestart);
    }
};

struct SymbolTable {
    unordered_map<string_view, Ident *> idents;  // Key points to value!
    vector<Ident *> identtable;
    vector<Ident *> identstack;
    vector<SpecIdent *> specidents;

    unordered_map<string_view, Struct *> structs;  // Key points to value!
    vector<Struct *> structtable;

    unordered_map<string_view, SharedField *> fields;  // Key points to value!
    vector<SharedField *> fieldtable;

    unordered_map<string_view, Function *> functions;  // Key points to value!
    vector<Function *> functiontable;
    vector<SubFunction *> subfunctiontable;
    SubFunction *toplevel;

    vector<string> filenames;

    vector<size_t> scopelevels;

    typedef pair<TypeRef, Ident *> WithStackElem;
    vector<WithStackElem> withstack;
    vector<size_t> withstacklevels;

    enum { NUM_VECTOR_TYPE_WRAPPINGS = 3 };
    vector<TypeRef> default_int_vector_types[NUM_VECTOR_TYPE_WRAPPINGS],
                    default_float_vector_types[NUM_VECTOR_TYPE_WRAPPINGS];

    // Used during parsing.
    vector<SubFunction *> defsubfunctionstack;

    vector<Type *> typelist;

    SymbolTable() : toplevel(nullptr) {}

    ~SymbolTable() {
        for (auto id : identtable)       delete id;
        for (auto sid : specidents)      delete sid;
        for (auto st : structtable)      delete st;
        for (auto f  : functiontable)    delete f;
        for (auto sf : subfunctiontable) delete sf;
        for (auto f  : fieldtable)       delete f;
        for (auto t  : typelist)         delete t;
    }

    Ident *Lookup(string_view name) {
        auto it = idents.find(name);
        return it == idents.end() ? nullptr : it->second;
    }

    Ident *LookupAny(string_view name) {
        for (auto id : identtable) if (id->name == name) return id;
        return nullptr;
    }

    Ident *LookupDef(string_view name, int line, Lex &lex, bool anonymous_arg, bool islocal,
                     bool withtype) {
        auto sf = defsubfunctionstack.back();
        auto existing_ident = Lookup(name);
        if (anonymous_arg && existing_ident && existing_ident->sf_def == sf) return existing_ident;
        Ident *ident = nullptr;
        if (LookupWithStruct(name, lex, ident))
            lex.Error("cannot define variable with same name as field in this scope: " + name);
        ident = new Ident(name, line, (int)identtable.size(), scopelevels.size());
        ident->anonymous_arg = anonymous_arg;
        ident->sf_def = sf;
        ident->cursid = NewSid(ident);
        (islocal ? sf->locals : sf->args).v.push_back(
            Arg(ident->cursid, type_any, AF_GENERIC | (withtype ? AF_WITHTYPE : AF_NONE)));
        if (existing_ident) {
            lex.Error("identifier redefinition / shadowing: " + ident->name);
        }
        idents[ident->name /* must be in value */] = ident;
        identstack.push_back(ident);
        identtable.push_back(ident);
        return ident;
    }

    Ident *LookupDynScopeRedef(string_view name, Lex &lex) {
        auto ident = Lookup(name);
        if (!ident)
            lex.Error("lhs of <- must refer to existing variable: " + name);
        defsubfunctionstack.back()->dynscoperedefs.Add(Arg(ident->cursid, type_any, AF_GENERIC));
        return ident;
    }

    Ident *LookupUse(string_view name, Lex &lex) {
        auto id = Lookup(name);
        if (!id)
            lex.Error("unknown identifier: " + name);
        return id;
    }

    void AddWithStruct(TypeRef t, Ident *id, Lex &lex) {
        if (t->t != V_STRUCT) lex.Error(":: can only be used with struct/value types");
        for (auto &wp : withstack)
            if (wp.first->struc == t->struc)
                lex.Error("type used twice in the same scope with ::");
        // FIXME: should also check if variables have already been defined in this scope that clash
        // with the struct, or do so in LookupUse
        assert(t->struc);
        withstack.push_back(make_pair(t, id));
    }

    SharedField *LookupWithStruct(string_view name, Lex &lex, Ident *&id) {
        auto fld = FieldUse(name);
        if (!fld) return nullptr;
        assert(!id);
        for (auto &wp : withstack) {
            if (wp.first->struc->Has(fld) >= 0) {
                if (id) lex.Error("access to ambiguous field: " + fld->name);
                id = wp.second;
            }
        }
        return id ? fld : nullptr;
    }

    WithStackElem *GetWithStackBack() {
        return withstack.size()
            ? &withstack.back()
            : nullptr;
    }

    void MakeLogVar(Ident *id) {
        id->logvar = true;
        defsubfunctionstack.back()->logvarcallgraph = true;
    }

    SubFunction *ScopeStart() {
        scopelevels.push_back(identstack.size());
        withstacklevels.push_back(withstack.size());
        auto sf = CreateSubFunction();
        defsubfunctionstack.push_back(sf);
        return sf;
    }

    void ScopeCleanup() {
        defsubfunctionstack.pop_back();
        while (identstack.size() > scopelevels.back()) {
            auto ident = identstack.back();
            auto it = idents.find(ident->name);
            if (it != idents.end()) {  // can already have been removed by private var cleanup
                idents.erase(it);
            }
            identstack.pop_back();
        }
        scopelevels.pop_back();
        while (withstack.size() > withstacklevels.back()) withstack.pop_back();
        withstacklevels.pop_back();
    }

    void UnregisterStruct(const Struct *st, Lex &lex) {
        if (st->predeclaration) lex.Error("pre-declared struct never defined: " + st->name);
        auto it = structs.find(st->name);
        if (it != structs.end()) structs.erase(it);
    }

    void UnregisterFun(Function *f) {
        auto it = functions.find(f->name);
        if (it != functions.end())  // it can already have been removed by another variation
            functions.erase(it);
    }

    void EndOfInclude() {
        auto it = idents.begin();
        while (it != idents.end()) {
            if (it->second->isprivate) {
                idents.erase(it++);
            } else
                it++;
        }
    }

    Struct &StructDecl(string_view name, Lex &lex) {
        auto stit = structs.find(name);
        if (stit != structs.end()) {
            if (!stit->second->predeclaration) lex.Error("double declaration of type: " + name);
            stit->second->predeclaration = false;
            return *stit->second;
        } else {
            auto st = new Struct(name, (int)structtable.size());
            structs[st->name /* must be in value */] = st;
            structtable.push_back(st);
            return *st;
        }
    }

    Struct &StructUse(string_view name, Lex &lex) {
        auto stit = structs.find(name);
        if (stit == structs.end()) lex.Error("unknown type: " + name);
        return *stit->second;
    }

    bool IsSuperTypeOrSame(const Struct *sup, const Struct *sub) {
        for (auto t = sub; t; t = t->superclass)
            if (t == sup)
                return true;
        return false;
    }

    const Struct *CommonSuperType(const Struct *a, const Struct *b) {
        if (a != b) for (;;) {
            a = a->superclass;
            if (!a) return nullptr;
            if (IsSuperTypeOrSame(a, b)) break;
        }
        return a;
    }

    SharedField &FieldDecl(string_view name) {
        auto fld = FieldUse(name);
        if (fld) return *fld;
        fld = new SharedField(name, (int)fieldtable.size());
        fields[fld->name /* must be in value */] = fld;
        fieldtable.push_back(fld);
        return *fld;
    }

    SharedField *FieldUse(string_view name) {
        auto it = fields.find(name);
        return it != fields.end() ? it->second : nullptr;
    }

    SubFunction *CreateSubFunction() {
        auto sf = new SubFunction((int)subfunctiontable.size());
        subfunctiontable.push_back(sf);
        return sf;
    }

    Function &CreateFunction(string_view name, string_view context) {
        auto fname = name.length() ? string(name) : cat("function", functiontable.size(), context);
        auto f = new Function(fname, (int)functiontable.size(), scopelevels.size());
        functiontable.push_back(f);
        return *f;
    }

    Function &FunctionDecl(string_view name, size_t nargs, Lex &lex) {
        auto fit = functions.find(name);
        if (fit != functions.end()) {
            if (fit->second->scopelevel != scopelevels.size())
                lex.Error("cannot define a variation of function " + name +
                          " at a different scope level");
            for (auto f = fit->second; f; f = f->sibf)
                if (f->nargs() == nargs)
                    return *f;
        }
        auto &f = CreateFunction(name, "");
        if (fit != functions.end()) {
            f.sibf = fit->second->sibf;
            fit->second->sibf = &f;
        } else {
            functions[f.name /* must be in value */] = &f;
        }
        return f;
    }

    Function *FindFunction(string_view name) {
        auto it = functions.find(name);
        return it != functions.end() ? it->second : nullptr;
    }

    SpecIdent *NewSid(Ident *id, TypeRef type = nullptr) {
        auto sid = new SpecIdent(id, type);
        specidents.push_back(sid);
        return sid;
    }

    void CloneSids(ArgVector &av) {
        for (auto &a : av.v) a.sid = NewSid(a.sid->id);
    }

    void CloneIds(SubFunction &sf, const SubFunction &o) {
        sf.args = o.args;                     CloneSids(sf.args);
        sf.locals = o.locals;                 CloneSids(sf.locals);
        sf.dynscoperedefs = o.dynscoperedefs; // Set to correct one in TypeCheckFunctionDef.
        // Don't clone freevars, these will be accumulated in the new copy anew.
    }

    Type *NewType() {
        // FIXME: this potentially generates quite a bit of "garbage".
        // Instead, we could hash these, or store common type variants inside Struct etc.
        // A quick test revealed that 10% of nodes cause one of these to be allocated, so probably
        // not worth optimizing.
        auto t = new Type();
        typelist.push_back(t);
        return t;
    }

    bool RegisterTypeVector(vector<TypeRef> *sv, const char **names) {
        if (sv[0].size()) return true;  // Already initialized.
        for (size_t i = 0; i < NUM_VECTOR_TYPE_WRAPPINGS; i++) {
            sv[i].push_back(nullptr);
            sv[i].push_back(nullptr);
        }
        for (auto name = names; *name; name++) {
            // Can't use stucts.find, since all are out of scope.
            for (auto struc : structtable) if (struc->name == *name) {
                for (size_t i = 0; i < NUM_VECTOR_TYPE_WRAPPINGS; i++) {
                    auto vt = &struc->thistype;
                    for (size_t j = 0; j < i; j++) vt = vt->Wrap(NewType());
                    sv[i].push_back(vt);
                }
                goto found;
            }
            return false;
            found:;
        }
        return true;
    }

    bool RegisterDefaultVectorTypes() {
        // TODO: This isn't great hardcoded in the compiler, would be better if it was declared in
        // lobster code.
        static const char *default_int_vector_type_names[]   =
            { "xy_i", "xyz_i", "xyzw_i", nullptr };
        static const char *default_float_vector_type_names[] =
            { "xy_f", "xyz_f", "xyzw_f", nullptr };
        return RegisterTypeVector(default_int_vector_types, default_int_vector_type_names) &&
               RegisterTypeVector(default_float_vector_types, default_float_vector_type_names);
    }

    TypeRef VectorType(TypeRef vt, size_t level, int arity) const {
        return vt->sub->t == V_INT
            ? default_int_vector_types[level][arity]
            : default_float_vector_types[level][arity];
    }

    bool IsGeneric(TypeRef type) {
        if (type->t == V_ANY) return true;
        auto u = type->UnWrapped();
        return u->t == V_STRUCT && u->struc->generic;
    }

    void Serialize(vector<int> &code,
                   vector<uchar> &code_attr,
                   vector<type_elem_t> &typetable,
                   vector<type_elem_t> &vint_typeoffsets,
                   vector<type_elem_t> &vfloat_typeoffsets,
                   vector<bytecode::LineInfo> &linenumbers,
                   vector<bytecode::SpecIdent> &sids,
                   vector<string_view> &stringtable,
                   vector<int> &speclogvars,
                   string &bytecode) {
        flatbuffers::FlatBufferBuilder fbb;
        vector<flatbuffers::Offset<flatbuffers::String>> fns;
        for (auto &f : filenames) fns.push_back(fbb.CreateString(f));
        vector<flatbuffers::Offset<bytecode::Function>> functionoffsets;
        for (auto f : functiontable) functionoffsets.push_back(f->Serialize(fbb));
        vector<flatbuffers::Offset<bytecode::Struct>> structoffsets;
        for (auto s : structtable) structoffsets.push_back(s->Serialize(fbb));
        vector<flatbuffers::Offset<bytecode::Ident>> identoffsets;
        for (auto i : identtable) identoffsets.push_back(i->Serialize(fbb, toplevel));
        auto bcf = bytecode::CreateBytecodeFile(fbb,
            LOBSTER_BYTECODE_FORMAT_VERSION,
            fbb.CreateVector(code),
            fbb.CreateVector(code_attr),
            fbb.CreateVector((vector<int> &)typetable),
            fbb.CreateVector<flatbuffers::Offset<flatbuffers::String>>(stringtable.size(),
                [&](size_t i) {
                    return fbb.CreateString(stringtable[i].data(), stringtable[i].size());
                }
            ),
            fbb.CreateVectorOfStructs(linenumbers),
            fbb.CreateVector(fns),
            fbb.CreateVector(functionoffsets),
            fbb.CreateVector(structoffsets),
            fbb.CreateVector(identoffsets),
            fbb.CreateVectorOfStructs(sids),
            fbb.CreateVector((vector<int> &)vint_typeoffsets),
            fbb.CreateVector((vector<int> &)vfloat_typeoffsets),
            fbb.CreateVector(speclogvars));
        bytecode::FinishBytecodeFileBuffer(fbb, bcf);
        bytecode.assign(fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    }
};

inline string TypeName(TypeRef type, int flen = 0, const SymbolTable *st = nullptr) {
    switch (type->t) {
    case V_STRUCT:
        return type->struc->name;
    case V_VECTOR:
        return flen && type->Element()->Numeric()
            ? (flen < 0
                ? (type->Element()->t == V_INT ? "xy_z_w_i" : "xy_z_w_f")  // FIXME: better names?
                : TypeName(st->VectorType(type, 0, flen)))
            : (type->Element()->t == V_VAR
                ? "[]"
                : "[" + TypeName(type->Element(), flen, st) + "]");
    case V_FUNCTION:
        return type->sf // || type->sf->anonymous
            ? type->sf->parent->name
            : "function";
    case V_NIL:
        return type->Element()->t == V_VAR
            ? "nil"
            : TypeName(type->Element(), flen, st) + "?";
    case V_COROUTINE:
        return type->sf
            ? "coroutine(" + type->sf->parent->name + ")"
            : "coroutine";
    default:
        return string(BaseTypeName(type->t));
    }
}

}  // namespace lobster
