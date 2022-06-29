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

#ifndef LOBSTER_IDENTS
#define LOBSTER_IDENTS

#include "lobster/natreg.h"

#define FLATBUFFERS_DEBUG_VERIFICATION_FAILURE
#include "lobster/bytecode_generated.h"

namespace lobster {

struct NativeFun;
struct SymbolTable;

struct Node;
struct Block;

struct Function;
struct SubFunction;

struct SpecIdent;

struct Ident : Named {
    size_t scopelevel;

    bool single_assignment = true;  // not declared const but def only, exp may or may not be const
    bool constant = false;          // declared const
    bool static_constant = false;   // not declared const but def only, exp is const.
    bool read = false;              // has been read at least once.

    SpecIdent *cursid = nullptr;

    Ident(string_view _name, int _idx, size_t _sl)
        : Named(_name, _idx), scopelevel(_sl) {}

    void Assign(Lex &lex) {
        single_assignment = false;
        if (constant)
            lex.Error("variable " + name + " is constant");
    }

    Ident *Read() {
        read = true;
        return this;
    }

    flatbuffers::Offset<bytecode::Ident> Serialize(flatbuffers::FlatBufferBuilder &fbb,
                                                   bool is_top_level) {
        return bytecode::CreateIdent(fbb, fbb.CreateString(name), constant, is_top_level);
    }
};

struct SpecIdent {
    Ident *id;
    TypeRef type;
    Lifetime lt = LT_UNDEF;
    int idx, sidx = -1;     // Into specidents, and into vm ordering.
    SubFunction *sf_def = nullptr;  // Where it is defined, including anonymous functions.
    bool used_as_freevar = false;  // determined in codegen.

    SpecIdent(Ident *_id, TypeRef _type, int idx)
        : id(_id), type(_type), idx(idx){}
    int Idx() { assert(sidx >= 0); return sidx; }
    SpecIdent *&Current() { return id->cursid; }
};

struct Enum;

struct EnumVal : Named {
    int64_t val = 0;
    Enum *e = nullptr;

    EnumVal(string_view _name, int _idx) : Named(_name, _idx) {}
};

struct Enum : Named {
    vector<unique_ptr<EnumVal>> vals;
    Type thistype;
    bool flags = false;

    Enum(string_view _name, int _idx) : Named(_name, _idx) {
        thistype = Type { this };
    }

    flatbuffers::Offset<bytecode::Enum> Serialize(flatbuffers::FlatBufferBuilder &fbb) {
        vector<flatbuffers::Offset<bytecode::EnumVal>> valoffsets;
        for (auto &v : vals)
            valoffsets.push_back(
                bytecode::CreateEnumVal(fbb, fbb.CreateString(v->name), v->val));
        return bytecode::CreateEnum(fbb, fbb.CreateString(name), fbb.CreateVector(valoffsets),
               flags);
    }
};

// Only still needed because we have no idea which struct it refers to at parsing time.
struct SharedField : Named {
    SharedField(string_view _name, int _idx) : Named(_name, _idx) {}
    SharedField() : SharedField("", 0) {}
};

inline string TypeName(TypeRef type, int flen = 0);
bool EarlyResolve(const TypeRef type, TypeRef *out);

struct GivenResolve {
    UnresolvedTypeRef giventype;

    private:
    TypeRef resolvedtype_;
    public:

    bool was_resolved = false;

    // Callers are cool with either resolved or unresolved.
    TypeRef utype() const {
        assert(!resolvedtype_.Null());
        return resolvedtype_;
    }

    // Callers want resolved, if it is unresolved this will likely cause issues.
    TypeRef resolvedtype() const {
        // FIXME: turn this into just an assert once we're more confident this can't happen in the wild.
        // or if it happens in reasonable cases that can't be fixed, make it a normal error.
        if (!was_resolved)
            THROW_OR_ABORT("internal error: unresolved type used: " + TypeName(resolvedtype_));
        assert(was_resolved);
        return utype();
    }

    bool resolved_null() const {
        return resolvedtype_.Null();
    }

    void set_resolvedtype(TypeRef type) {
        resolvedtype_ = type;
        if (!type.Null()) was_resolved = true;
    }

    UDT *resolved_udt() const {
        if (resolvedtype_.Null()) return nullptr;
        assert(IsUDT(resolvedtype_->t));
        return resolvedtype_->udt;
    }

    GivenResolve()
        : giventype({ nullptr }),
          resolvedtype_(nullptr) {}
    GivenResolve(const GivenResolve &gr)
        : giventype(gr.giventype),
          resolvedtype_(gr.resolvedtype_),
          was_resolved(gr.was_resolved) {}
    GivenResolve(UnresolvedTypeRef utype)
        : giventype(utype),
          resolvedtype_(utype.utr),
          was_resolved(EarlyResolve(resolvedtype_, &resolvedtype_)) {}
    GivenResolve(UnresolvedTypeRef utype, TypeRef type)
        : giventype(utype),
          resolvedtype_(type),
          was_resolved(EarlyResolve(resolvedtype_, &resolvedtype_)) {}
};

struct Field : GivenResolve {
    SharedField *id;
    Node *defaultval;
    int slot = -1;
    bool isprivate;
    Line defined_in;

    Field(SharedField *_id, UnresolvedTypeRef _type, Node *_defaultval, bool isprivate,
          const Line &defined_in)
        : GivenResolve(_type),
          id(_id),
          defaultval(_defaultval),
          isprivate(isprivate), defined_in(defined_in) {}
    Field(const Field &o);
    ~Field();
};

struct TypeVariable {
    string_view name;
    Type thistype;

    TypeVariable(string_view name) : name(name), thistype(this) {}
};

struct BoundTypeVariable : GivenResolve {
    TypeVariable *tv;

    BoundTypeVariable() : tv(nullptr) {}
    BoundTypeVariable(UnresolvedTypeRef _type, TypeVariable *_tv) : GivenResolve(_type), tv(_tv) {}

    void Resolve(TypeRef type) {
        set_resolvedtype(type);
        if (giventype.utr.Null()) giventype = { type };
    }
};

struct DispatchEntry {
    SubFunction *sf = nullptr;
    bool is_dispatch_root = false;
    // Shared return type if root of dispatch.
    TypeRef returntype = nullptr;
    int returned_thru_to_max = -1;
    size_t subudts_size = 0;  // At time of creation.
};

struct UDT : Named {
    vector<Field> fields;
    vector<BoundTypeVariable> generics;
    UDT *next = nullptr, *first = this;  // Specializations
    GivenResolve superclass;
    bool is_struct = false, hasref = false;
    bool predeclaration = false;
    bool constructed = false;  // Is this instantiated anywhere in the code?
    bool unnamed_specialization = false;
    bool is_generic = false;
    Type thistype;  // convenient place to store the type corresponding to this.
    TypeRef sametype = type_undefined;  // If all fields are int/float, this allows vector ops.
    SpecUDT unspecialized;
    Type unspecialized_type;
    type_elem_t typeinfo = (type_elem_t)-1;  // Runtime type.
    int numslots = -1;
    int vtable_start = -1;
    bool has_subclasses = false;
    vector<UDT *> subudts;  // Including self.
    bool subudts_dispatched = false;
    // Subset of methods that participate in dynamic dispatch. Order in this table determines
    // vtable layout and is compatible with sub/super classes.
    // Multiple specializations of a method may be in here.
    // Methods whose dispatch can be determined statically for the current program do not end up
    // in here.
    vector<DispatchEntry> dispatch;

    UDT(string_view _name, int _idx, bool is_struct)
        : Named(_name, _idx), is_struct(is_struct), unspecialized(this),
          unspecialized_type(&unspecialized) {
        thistype = is_struct ? Type { V_STRUCT_R, this } : Type { V_CLASS, this };
    }

    int Has(SharedField *fld) {
        for (auto &uf : fields) if (uf.id == fld) return int(&uf - &fields[0]);
        return -1;
    }

    UDT *CloneInto(UDT *st, string_view sname, vector<UDT *> &udttable) {
        *st = *this;
        st->is_generic = false;
        st->thistype.udt = st;
        st->unspecialized.udt = st;
        st->unspecialized.is_generic = false;
        st->unspecialized_type.spec_udt = &st->unspecialized;
        st->idx = (int)udttable.size() - 1;
        st->name = sname;
        st->numslots = -1;
        st->next = next;
        st->first = first;
        next = st;
        return st;
    }

    bool FullyBound() {
        for (auto &g : generics) {
            if (g.giventype.utr.Null() || g.giventype.utr->t == V_TYPEVAR) return false;
        }
        return true;
    }

    bool IsSpecialization(UDT *other) {
        if (!FullyBound()) {
            for (auto udt = first->next; udt; udt = udt->next)
                if (udt == other)
                    return true;
            return false;
        } else {
            return this == other;
        }
    }

    bool ComputeSizes(int depth = 0) {
        if (numslots >= 0 || is_generic) return true;
        if (depth > 16) return false;  // Simple protection against recursive references.
        int size = 0;
        for (auto &uf : fields) {
            assert(!uf.resolved_null());
            uf.slot = size;
            if (IsStruct(uf.resolvedtype()->t)) {
                if (!uf.resolved_udt()->ComputeSizes(depth + 1)) return false;
                size += uf.resolved_udt()->numslots;
            } else {
                size++;
            }
        }
        numslots = size;
        return true;
    }

    flatbuffers::Offset<bytecode::UDT> Serialize(flatbuffers::FlatBufferBuilder &fbb) {
        vector<flatbuffers::Offset<bytecode::Field>> fieldoffsets;
        for (auto f : fields)
            fieldoffsets.push_back(
                bytecode::CreateField(fbb, fbb.CreateString(f.id->name), f.slot));
        return bytecode::CreateUDT(fbb, fbb.CreateString(name), idx,
                                        fbb.CreateVector(fieldoffsets), numslots);
    }
};


// This tries to resolve types for simple situations ahead of ResolveTypeVars so many forms of
// circular references can be broken.
inline bool EarlyResolve(const TypeRef type, TypeRef *out) {
    if (type.Null()) return false;
    switch (type->t) {
        case V_UUDT:
            if (!out || type->spec_udt->is_generic) return false;
            *out = &type->spec_udt->udt->thistype;
            return true;
        case V_TYPEVAR:
            return false;
        case V_NIL:
        case V_VECTOR:
        case V_VAR:
        case V_TYPEID:
            return type->sub && EarlyResolve(type->sub, nullptr);
        case V_TUPLE:
            for (auto &te : *type->tup)
                if (!EarlyResolve(te.type, nullptr)) return false;
            return true;
        default:
            return true;
    }
}

inline int ValWidth(TypeRef type) {
    assert(type->t != V_TUPLE);  // You need ValWidthMulti
    return IsStruct(type->t) ? type->udt->numslots : 1;
}

inline int ValWidthMulti(TypeRef type, size_t nvals) {
    int n = 0;
    for (size_t i = 0; i < nvals; i++) {
        n += ValWidth(type->Get(i));
    }
    return n;
}

inline const Field *FindSlot(const UDT &udt, int i) {
    for (auto &f : udt.fields) {
        if (i >= f.slot && i < f.slot + ValWidth(f.resolvedtype())) {
            return IsStruct(f.resolvedtype()->t) ? FindSlot(*f.resolved_udt(), i - f.slot) : &f;
        }
    }
    assert(false);
    return nullptr;
}

struct Arg {
    TypeRef type = type_undefined;
    SpecIdent *sid = nullptr;
    bool withtype = false;

    Arg() = default;
    Arg(const Arg &o) = default;
    Arg(SpecIdent *_sid, TypeRef _type, bool _withtype)
        : type(_type), sid(_sid), withtype(_withtype) {}
};

struct Function;

struct SubFunction {
    int idx;
    vector<Arg> args;
    vector<Arg> locals;
    vector<Arg> freevars;       // any used from outside this scope
    vector<UnresolvedTypeRef> giventypes;  // before specialization, includes typevars. FIXME: Only needed once per overload
    UnresolvedTypeRef returngiventype = { nullptr };
    TypeRef returntype = type_undefined;
    size_t num_returns = 0;
    size_t num_returns_non_local = 0;
    size_t reqret = 0;  // Do the caller(s) want values to be returned?
    const Lifetime ltret = LT_KEEP;
    vector<pair<const SubFunction *, TypeRef>> reuse_return_events;
    vector<Node *> reuse_assign_events;
    bool isrecursivelycalled = false;
    Block *sbody = nullptr;
    SubFunction *next = nullptr;
    Function *parent = nullptr;
    int subbytecodestart = 0;
    bool typechecked = false;
    bool freevarchecked = false;
    bool mustspecialize = false;
    bool isdynamicfunctionvalue = false;
    bool consumes_vars_on_return = false;
    bool optimized = false;
    int returned_thru_to_max = -1;  // >=0: there exist return statements that may skip the caller.
    UDT *method_of = nullptr;
    int numcallers = 0;
    Type thistype { V_FUNCTION, this };  // convenient place to store the type corresponding to this
    vector<BoundTypeVariable> generics;

    SubFunction(int _idx) : idx(_idx) {}

    void SetParent(Function &f, SubFunction *&link) {
        parent = &f;
        next = link;
        link = this;
    }

    bool Add(vector<Arg> &v, const Arg &in) {
        for (auto &arg : v)
            if (arg.sid->id == in.sid->id)
                return false;
        v.push_back(in);
        return true;
    }

    ~SubFunction();
};

struct Overload : NonCopyable {
    SubFunction *sf = nullptr;
    Block *gbody = nullptr;

    Overload() {}

    ~Overload();

    Overload(Overload &&o) {
        *this = std::move(o);
    }

    Overload& operator=(Overload &&o) {
        std::swap(sf, o.sf);
        std::swap(gbody, o.gbody);
        return *this;

    }
};

struct Function : Named {
    // Start of all SubFunctions sequentially.
    int bytecodestart = 0;
    // functions with the same name and args, but different types (dynamic dispatch |
    // specialization)
    vector<Overload> overloads;
    // functions with the same name but different number of args (overloaded)
    Function *sibf = nullptr;
    // does not have a programmer specified name
    bool anonymous = false;
    // its merely a function type, has no body, but does have a set return type.
    bool istype = false;

    size_t scopelevel;

    vector<Node *> default_args;
    int first_default_arg = -1;

    Function(string_view _name, int _idx, size_t _sl)
        : Named(_name, _idx), scopelevel(_sl) {
    }

    ~Function();

    size_t nargs() const { return overloads[0].sf->args.size(); }

    int NumSubf() {
        int sum = 0;
        for (auto &ov : overloads) for (auto sf = ov.sf; sf; sf = sf->next) sum++;
        return sum;
    }

    bool RemoveSubFunction(SubFunction *sf) {
        for (auto [i, ov] : enumerate(overloads)) {
            for (auto sfp = &ov.sf; *sfp; sfp = &(*sfp)->next) {
                if (*sfp == sf) {
                    *sfp = sf->next;
                    sf->next = nullptr;
                    if (!ov.sf) overloads.erase(overloads.begin() + i);
                    return true;
                }
            }
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

    unordered_map<string_view, UDT *> udts;  // Key points to value!
    vector<UDT *> udttable;

    unordered_map<string_view, SharedField *> fields;  // Key points to value!
    vector<SharedField *> fieldtable;

    unordered_map<string_view, Function *> functions;  // Key points to value!
    unordered_map<string_view, Function *> operators;  // Key points to value!
    vector<Function *> functiontable;
    vector<SubFunction *> subfunctiontable;
    SubFunction *toplevel = nullptr;

    unordered_map<string_view, Enum *> enums;  // Key points to value!
    unordered_map<string_view, EnumVal *> enumvals;  // Key points to value!
    vector<Enum *> enumtable;

    vector<TypeVariable *> typevars;
    vector<vector<BoundTypeVariable> *> bound_typevars_stack;

    vector<string> filenames;

    vector<size_t> scopelevels;

    struct WithStackElem { UDT *udt; Ident *id = nullptr; SubFunction *sf = nullptr; };
    vector<WithStackElem> withstack;
    vector<size_t> withstacklevels;

    enum { NUM_VECTOR_TYPE_WRAPPINGS = 3 };
    vector<TypeRef> default_int_vector_types[NUM_VECTOR_TYPE_WRAPPINGS],
                    default_float_vector_types[NUM_VECTOR_TYPE_WRAPPINGS];
    Enum *default_bool_type = nullptr;

    // Used during parsing.
    vector<SubFunction *> defsubfunctionstack;

    vector<Type *> typelist;  // Used for constructing new vector types, variables, etc.
    vector<vector<Type::TupleElem> *> tuplelist;
    vector<SpecUDT *> specudts;

    string current_namespace;
    // FIXME: because we cleverly use string_view's into source code everywhere, we now have
    // no way to refer to constructed strings, and need to store them seperately :(
    // TODO: instead use larger buffers and constuct directly into those, so no temp string?
    vector<const char *> stored_names;

    ~SymbolTable() {
        for (auto id  : identtable)       delete id;
        for (auto sid : specidents)       delete sid;
        for (auto u   : udttable)         delete u;
        for (auto f   : functiontable)    delete f;
        for (auto e   : enumtable)        delete e;
        for (auto sf  : subfunctiontable) delete sf;
        for (auto f   : fieldtable)       delete f;
        for (auto t   : typelist)         delete t;
        for (auto t   : tuplelist)        delete t;
        for (auto n   : stored_names)     delete[] n;
        for (auto tv  : typevars)         delete tv;
        for (auto su  : specudts)         delete su;
    }

    string NameSpaced(string_view name) {
        assert(!current_namespace.empty());
        return cat(current_namespace, "_", name);
    }

    string_view StoreName(const string &s) {
        auto buf = new char[s.size()];
        memcpy(buf, s.data(), s.size());  // Look ma, no terminator :)
        stored_names.push_back(buf);
        return string_view(buf, s.size());
    }

    string_view MaybeNameSpace(string_view name, bool other_conditions) {
        return other_conditions && !current_namespace.empty() && scopelevels.size() == 1
            ? StoreName(NameSpaced(name))
            : name;
    }

    Ident *Lookup(string_view name) {
        if (!current_namespace.empty()) {
            auto it = idents.find(NameSpaced(name));
            if (it != idents.end()) return it->second->Read();
        }
        auto it = idents.find(name);
        if (it != idents.end()) return it->second->Read();
        return nullptr;
    }

    Ident *LookupAny(string_view name) {
        for (auto id : identtable) if (id->name == name) return id;
        return nullptr;
    }

    Ident *NewId(string_view name, SubFunction *sf) {
        auto ident = new Ident(name, (int)identtable.size(), scopelevels.size());
        ident->cursid = NewSid(ident, sf);
        identtable.push_back(ident);
        return ident;
    }

    Ident *LookupDef(string_view name, Lex &lex, bool islocal, bool withtype) {
        auto sf = defsubfunctionstack.back();
        Ident *ident = nullptr;
        if (LookupWithStruct(name, lex, ident))
            lex.Error("cannot define variable with same name as field in this scope: " + name);
        ident = NewId(name, sf);
        (islocal ? sf->locals : sf->args).push_back(
            Arg(ident->cursid, type_any, withtype));
        if (Lookup(name)) {
            lex.Error("identifier redefinition / shadowing: " + ident->name);
        }
        idents[ident->name /* must be in value */] = ident;
        identstack.push_back(ident);
        return ident;
    }

    void AddWithStruct(TypeRef type, Ident *id, Lex &lex, SubFunction *sf) {
        if (type->t != V_UUDT) lex.Error(":: can only be used with struct/class types");
        for (auto &wp : withstack)
            if (wp.udt == type->spec_udt->udt)
                lex.Error("type used twice in the same scope with ::");
        // FIXME: should also check if variables have already been defined in this scope that clash
        // with the struct, or do so in LookupUse
        assert(type->spec_udt->udt);
        withstack.push_back({ type->spec_udt->udt, id, sf });
    }

    SharedField *LookupWithStruct(string_view name, Lex &lex, Ident *&id) {
        auto fld = FieldUse(name);
        if (!fld) return nullptr;
        assert(!id);
        for (auto &wse : withstack) {
            if (wse.udt->Has(fld) >= 0) {
                if (id) lex.Error("access to ambiguous field: " + fld->name);
                id = wse.id;
            }
        }
        return id ? fld : nullptr;
    }

    WithStackElem GetWithStackBack() {
        return withstack.size()
            ? withstack.back()
            : WithStackElem();
    }

    void BlockScopeStart() {
        scopelevels.push_back(identstack.size());
        withstacklevels.push_back(withstack.size());
    }

    void BlockScopeCleanup() {
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

    SubFunction *FunctionScopeStart() {
        BlockScopeStart();
        auto sf = CreateSubFunction();
        defsubfunctionstack.push_back(sf);
        return sf;
    }

    void FunctionScopeCleanup() {
        defsubfunctionstack.pop_back();
        BlockScopeCleanup();
    }

    void Unregister(const Enum *e, unordered_map<string_view, Enum *> &dict) {
        auto it = dict.find(e->name);
        if (it != dict.end()) {
            for (auto &ev : e->vals) {
                auto it = enumvals.find(ev->name);
                assert(it != enumvals.end());
                enumvals.erase(it);
            }
            dict.erase(it);
        }
    }

    template<typename T> void Unregister(const T *x, unordered_map<string_view, T *> &dict) {
        auto it = dict.find(x->name);
        if (it != dict.end()) dict.erase(it);
    }

    template<typename T> void ErasePrivate(unordered_map<string_view, T *> &dict) {
        auto it = dict.begin();
        while (it != dict.end()) {
            auto n = it->second;
            it++;
            if (n->isprivate) Unregister(n, dict);
        }
    }

    void EndOfInclude() {
        current_namespace.clear();
        ErasePrivate(idents);
        ErasePrivate(udts);
        ErasePrivate(functions);
        ErasePrivate(enums);
    }

    Enum *EnumLookup(string_view name, Lex &lex, bool decl) {
        auto eit = enums.find(name);
        if (eit != enums.end()) {
            if (decl) lex.Error("double declaration of enum: " + name);
            return eit->second;
        }
        if (!decl) {
            if (!current_namespace.empty()) {
                eit = enums.find(NameSpaced(name));
                if (eit != enums.end()) return eit->second;
            }
            return nullptr;
        }
        auto e = new Enum(name, (int)enumtable.size());
        enumtable.push_back(e);
        enums[e->name /* must be in value */] = e;
        return e;
    }

    EnumVal *EnumValLookup(string_view name, Lex &lex, bool decl) {
        if (!decl) {
            if (!current_namespace.empty()) {
                auto evit = enumvals.find(NameSpaced(name));
                if (evit != enumvals.end()) return evit->second;
            }
        }
        auto evit = enumvals.find(name);
        if (evit != enumvals.end()) {
            if (decl) lex.Error("double declaration of enum value: " + name);
            return evit->second;
        }
        if (!decl) {
            return nullptr;
        }
        auto ev = new EnumVal(name, 0);
        enumvals[ev->name /* must be in value */] = ev;
        return ev;
    }

    UDT &StructDecl(string_view name, Lex &lex, bool is_struct) {
        auto uit = udts.find(name);
        if (uit != udts.end()) {
            if (!uit->second->predeclaration)
                lex.Error("double declaration of type: " + name);
            if (uit->second->is_struct != is_struct)
                lex.Error("class/struct previously declared as different kind");
            uit->second->predeclaration = false;
            return *uit->second;
        }
        auto st = new UDT(name, (int)udttable.size(), is_struct);
        udts[st->name /* must be in value */] = st;
        udttable.push_back(st);
        return *st;
    }

    UDT *LookupStruct(string_view name) {
        if (!current_namespace.empty()) {
            auto uit = udts.find(NameSpaced(name));
            if (uit != udts.end()) return uit->second;
        }
        auto uit = udts.find(name);
        if (uit != udts.end()) return uit->second;
        return nullptr;
    }

    UDT &StructUse(string_view name, Lex &lex) {
        auto udt = LookupStruct(name);
        if (!udt) lex.Error("unknown type: " + name);
        return *udt;
    }

    int SuperDistance(const UDT *super, const UDT *subclass) {
        int dist = 0;
        for (auto t = subclass; t; t = t->superclass.resolved_udt()) {
            if (t == super) return dist;
            dist++;
        }
        return -1;
    }

    const UDT *CommonSuperType(const UDT *a, const UDT *b) {
        if (a != b) for (;;) {
            if (SuperDistance(a, b) >= 0) break;
            a = a->superclass.resolved_udt();
            if (!a) return nullptr;
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

    Function &CreateFunction(string_view name) {
        auto fname = name.length() ? string(name) : cat("function", functiontable.size());
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
        auto &f = CreateFunction(name);
        if (fit != functions.end()) {
            f.sibf = fit->second->sibf;
            fit->second->sibf = &f;
        } else {
            functions[f.name /* must be in value */] = &f;
            // Store top level functions, for now only operators needed.
            if (scopelevels.size() == 2 && name.substr(0, 8) == TName(T_OPERATOR)) {
                operators[f.name /* must be in value */] = &f;
            }
        }
        return f;
    }

    Function *GetFirstFunction(string_view name) {
        auto it = functions.find(name);
        return it != functions.end() ? it->second : nullptr;
    }

    Function *FindFunction(string_view name) {
        if (!current_namespace.empty()) {
            auto it = functions.find(NameSpaced(name));
            if (it != functions.end()) return it->second;
        }
        auto it = functions.find(name);
        if (it != functions.end()) return it->second;
        return nullptr;
    }

    SpecIdent *NewSid(Ident *id, SubFunction *sf, TypeRef type = nullptr) {
        auto sid = new SpecIdent(id, type, (int)specidents.size());
        sid->sf_def = sf;
        specidents.push_back(sid);
        return sid;
    }

    void CloneSids(vector<Arg> &av, SubFunction *sf) {
        for (auto &a : av) {
            a.sid = NewSid(a.sid->id, sf);
        }
    }

    void CloneIds(SubFunction &sf, const SubFunction &o) {
        sf.args = o.args;     CloneSids(sf.args, &sf);
        sf.locals = o.locals; CloneSids(sf.locals, &sf);
        // Don't clone freevars, these will be accumulated in the new copy anew.
    }

    Type *NewType() {
        // These get allocated for very few nodes, given that most types are shared or stored in
        // their own struct.
        auto t = new Type();
        typelist.push_back(t);
        return t;
    }

    TypeRef NewTuple(size_t sz) {
        auto type = NewType();
        *type = Type(V_TUPLE);
        type->tup = new vector<Type::TupleElem>(sz);
        tuplelist.push_back(type->tup);
        return type;
    }

    TypeRef NewSpecUDT(UDT *udt) {
        auto su = new SpecUDT(udt);
        specudts.push_back(su);
        auto nt = NewType();
        *nt = Type(su);
        return nt;
    }

    TypeRef Wrap(TypeRef elem, ValueType with) {
        auto wt = WrapKnown(elem, with);
        return !wt.Null() ? wt : elem->Wrap(NewType(), with);
    }

    bool RegisterTypeVector(vector<TypeRef> *sv, const char **names) {
        if (sv[0].size()) return true;  // Already initialized.
        for (size_t i = 0; i < NUM_VECTOR_TYPE_WRAPPINGS; i++) {
            sv[i].push_back(nullptr);
            sv[i].push_back(nullptr);
        }
        for (auto name = names; *name; name++) {
            // Can't use stucts.find, since all are out of scope.
            for (auto udt : udttable) if (udt->name == *name) {
                for (size_t i = 0; i < NUM_VECTOR_TYPE_WRAPPINGS; i++) {
                    auto vt = TypeRef(&udt->thistype);
                    for (size_t j = 0; j < i; j++) vt = Wrap(vt, V_VECTOR);
                    sv[i].push_back(vt);
                }
                goto found;
            }
            return false;
            found:;
        }
        return true;
    }

    static const char **DefaultIntVectorTypeNames() {
        static const char *names[] = { "xy_i", "xyz_i", "xyzw_i", nullptr };
        return names;
    }

    static const char **DefaultFloatVectorTypeNames() {
        static const char *names[] = { "xy_f", "xyz_f", "xyzw_f", nullptr };
        return names;
    }

    static const char *GetVectorName(TypeRef type, int flen) {
        if (flen < 2 || flen > 4) return nullptr;
        if (type->t == V_INT) return DefaultIntVectorTypeNames()[flen - 2];
        if (type->t == V_FLOAT) return DefaultFloatVectorTypeNames()[flen - 2];
        return nullptr;
    }

    bool RegisterDefaultTypes() {
        // TODO: This isn't great hardcoded in the compiler, would be better if it was declared in
        // lobster code.
        for (auto e : enumtable) {
            if (e->name == "bool") {
                default_bool_type = e;
                break;
            }
        }
        return RegisterTypeVector(default_int_vector_types, DefaultIntVectorTypeNames()) &&
               RegisterTypeVector(default_float_vector_types, DefaultFloatVectorTypeNames()) &&
               default_bool_type;
    }

    TypeRef GetVectorType(TypeRef vt, size_t level, int arity) const {
        if (arity > 4) return nullptr;
        return vt->sub->t == V_INT
            ? default_int_vector_types[level][arity]
            : default_float_vector_types[level][arity];
    }

    bool IsGeneric(UnresolvedTypeRef type) {
        auto u = type.utr->UnWrapAll();
        return u->t == V_TYPEVAR ||
               (u->t == V_UUDT && u->spec_udt->is_generic) ||
               (IsUDT(u->t) && u->udt->is_generic);
    }

    bool IsNillable(TypeRef type) {
        return (IsRef(type->t) && type->t != V_STRUCT_R) ||
               (type->t == V_UUDT && !type->spec_udt->udt->is_struct);
    }

    TypeVariable *NewGeneric(string_view name) {
        auto tv = new TypeVariable { name };
        typevars.push_back(tv);
        return tv;
    }

    void Serialize(vector<int> &code,
                   vector<type_elem_t> &typetable,
                   vector<bytecode::LineInfo> &linenumbers,
                   vector<bytecode::SpecIdent> &sids,
                   vector<string_view> &stringtable,
                   string &bytecode,
                   vector<int> &vtables) {
        flatbuffers::FlatBufferBuilder fbb;
        // Always serialize this first! that way it can easily be left out of the generated C code.
        auto codevec = fbb.CreateVector(code);
        vector<flatbuffers::Offset<flatbuffers::String>> fns;
        for (auto &f : filenames) fns.push_back(fbb.CreateString(f));
        vector<flatbuffers::Offset<bytecode::Function>> functionoffsets;
        for (auto f : functiontable) functionoffsets.push_back(f->Serialize(fbb));
        vector<flatbuffers::Offset<bytecode::UDT>> udtoffsets;
        for (auto u : udttable) udtoffsets.push_back(u->Serialize(fbb));
        vector<flatbuffers::Offset<bytecode::Ident>> identoffsets;
        for (auto i : identtable) identoffsets.push_back(i->Serialize(fbb, i->scopelevel == 1));
        vector<flatbuffers::Offset<bytecode::Enum>> enumoffsets;
        for (auto e : enumtable) enumoffsets.push_back(e->Serialize(fbb));
        auto bcf = bytecode::CreateBytecodeFile(fbb,
            LOBSTER_BYTECODE_FORMAT_VERSION,
            codevec,
            fbb.CreateVector((vector<int> &)typetable),
            fbb.CreateVector<flatbuffers::Offset<flatbuffers::String>>(stringtable.size(),
                [&](size_t i) {
                    return fbb.CreateString(stringtable[i].data(), stringtable[i].size());
                }
            ),
            fbb.CreateVectorOfStructs(linenumbers),
            fbb.CreateVector(fns),
            fbb.CreateVector(functionoffsets),
            fbb.CreateVector(udtoffsets),
            fbb.CreateVector(identoffsets),
            fbb.CreateVectorOfStructs(sids),
            fbb.CreateVector(enumoffsets),
            fbb.CreateVector(vtables));
        bytecode::FinishBytecodeFileBuffer(fbb, bcf);
        bytecode.assign(fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    }
};

inline string TypeName(TypeRef type, int flen) {
    switch (type->t) {
        case V_STRUCT_R:
        case V_STRUCT_S:
        case V_CLASS: {
            string s = type->udt->name;
            if (type->udt->unnamed_specialization) {
                s += "<";
                for (auto [i, t] : enumerate(type->udt->generics)) {
                    if (i) s += ", ";
                    s += t.giventype.utr.Null()
                        ? t.tv->name
                             : TypeName(t.resolved_null() ? t.giventype.utr : t.resolvedtype());
                }
                s += ">";
            }
            return s;
        }
        case V_UUDT: {
            string s = type->spec_udt->udt->name;
            if (!type->spec_udt->specializers.empty() && !type->spec_udt->udt->FullyBound()) {
                // FIXME! merge with code above..
                s += "<";
                for (auto [i, t] : enumerate(type->spec_udt->specializers)) {
                    if (i) s += ", ";
                    s += TypeName(t);
                }
                s += ">";
            }
            return s;
        }
        case V_VECTOR:
            if (flen && type->Element()->Numeric()) {
                auto nvt = SymbolTable::GetVectorName(type->Element(), flen);
                if (nvt) return nvt;
                // FIXME: better names?
                return type->Element()->t == V_INT ? "vec_i" : "vec_f";
            } else {
                return type->Element()->t == V_VAR
                           ? "[]"
                           : "[" + TypeName(type->Element(), flen) + "]";
            }
        case V_FUNCTION:
            return type->sf // || type->sf->anonymous
                ? type->sf->parent->name
                : "function";
        case V_NIL:
            return type->Element()->t == V_VAR
                ? "nil"
                : TypeName(type->Element(), flen) + "?";
        case V_TUPLE: {
            string s = "(";
            for (auto [i, te] : enumerate(*type->tup)) {
                if (i) s += ", ";
                s += TypeName(te.type);
            }
            s += ")";
            return s;
        }
        case V_INT:
            return type->e ? type->e->name : "int";
        case V_TYPEID:
            return "typeid(" + TypeName(type->sub) + ")";
        case V_TYPEVAR:
            return string(type->tv->name);
        case V_RESOURCE:
            return type->rt ? cat("resource<", type->rt->name, ">") : "resource";
        default:
            return string(BaseTypeName(type->t));
    }
}

inline void FormatArg(string &r, string_view name, size_t i, TypeRef type) {
    if (i) r += ", ";
    r += name;
    if (type->t != V_ANY) {
        r += ":";
        r += TypeName(type);
    }
}

inline string Signature(const NativeFun &nf) {
    string r = nf.name;
    r += "(";
    for (auto [i, arg] : enumerate(nf.args)) {
        FormatArg(r, arg.name, i, arg.type);
    }
    r += ")";
    return r;
}

inline string Signature(const UDT &udt) {
    string r = udt.name;
    r += "{";
    for (auto [i, f] : enumerate(udt.fields)) {
        FormatArg(r, f.id->name, i, f.resolvedtype());
    }
    r += "}";
    return r;
}

inline string Signature(const SubFunction &sf) {
    string r = sf.parent->name;
    r += "(";
    for (auto [i, arg] : enumerate(sf.args)) {
        FormatArg(r, arg.sid->id->name, i, arg.type);
    }
    return r + ")";
}


}  // namespace lobster

#endif  // LOBSTER_IDENTS
