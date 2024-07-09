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

struct UDT;

#ifdef NDEBUG
SlabAlloc *g_current_slaballoc;
#endif

#ifdef NDEBUG
    #define USE_CURRENT_SLABALLOCATOR \
        static void *operator new(size_t size) noexcept { \
            return g_current_slaballoc->alloc_small(size); \
        } \
        static void operator delete(void *ptr, size_t) noexcept { \
            return g_current_slaballoc->dealloc_small(ptr); \
        }
#else
    #define USE_CURRENT_SLABALLOCATOR 
#endif

struct Ident : Named {
    size_t scopelevel;
    Line line;

    bool isprivate = false;
    bool single_assignment = true;  // not declared const but def only, exp may or may not be const
    bool constant = false;          // declared const
    bool static_constant = false;   // not declared const but def only, exp is const.
    bool read = false;              // has been read at least once.
    bool predeclaration = false;

    SpecIdent *cursid = nullptr;

    TypeRef giventype = nullptr;

    Ident(string_view _name, int _idx, size_t _sl, Line &_line)
        : Named(_name, _idx), scopelevel(_sl), line(_line) {}

    void Assign(Lex &lex) {
        single_assignment = false;
        if (constant) lex.Error("variable " + name + " is constant");
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
    int idx, sidx = -1;             // Into specidents, and into vm ordering.
    SubFunction *sf_def = nullptr;  // Where it is defined, including anonymous functions.
    bool used_as_freevar = false;   // determined in codegen.
    bool withtype = false;

    SpecIdent(Ident *_id, TypeRef _type, int idx, bool withtype)
        : id(_id), type(_type), idx(idx), withtype(withtype) {}
    int Idx() const {
        assert(sidx >= 0);
        return sidx;
    }
    SpecIdent *&Current() {
        return id->cursid;
    }

    USE_CURRENT_SLABALLOCATOR
};

struct Enum;

struct EnumVal : Named {
    bool isprivate = false;
    int64_t val = 0;
    Enum *e = nullptr;

    EnumVal(string_view _name, int _idx) : Named(_name, _idx) {}
};

struct Enum : Named {
    bool isprivate = false;
    vector<unique_ptr<EnumVal>> vals;
    Type thistype;
    bool flags = false;

    Enum(string_view _name, int _idx) : Named(_name, _idx) {
        thistype = Type{ this };
    }

    EnumVal *Lookup(int64_t q) {
        for (auto &v : vals)
            if (v.get()->val == q) return v.get();
        return nullptr;
    }

    flatbuffers::Offset<bytecode::Enum> Serialize(flatbuffers::FlatBufferBuilder &fbb) {
        vector<flatbuffers::Offset<bytecode::EnumVal>> valoffsets;
        for (auto &v : vals)
            valoffsets.push_back(bytecode::CreateEnumVal(fbb, fbb.CreateString(v->name), v->val));
        return bytecode::CreateEnum(fbb, fbb.CreateString(name), fbb.CreateVector(valoffsets),
                                    flags);
    }
};

// Only still needed because we have no idea which struct it refers to at parsing time.
struct SharedField : Named {
    SharedField(string_view _name, int _idx) : Named(_name, _idx) {}
    SharedField() : SharedField("", 0) {}
};

struct Field {
    SharedField *id;
    TypeRef giventype;
    Node *defaultval;
    bool isprivate;
    bool in_scope;  // For tracking scopes of ones declared by `member`.
    Line defined_in;

    Field(SharedField *_id, TypeRef _type, Node *_defaultval, bool isprivate,
          bool in_scope, const Line &defined_in)
        : id(_id),
          giventype(_type),
          defaultval(_defaultval),
          isprivate(isprivate),
          in_scope(in_scope),
          defined_in(defined_in) {}
    Field(const Field &o);
    ~Field();
};

struct SField {
    TypeRef type;
    int slot = -1;
};

struct TypeVariable {
    string_view name;
    Type thistype;

    TypeVariable(string_view name) : name(name), thistype(this) {}
};

struct GenericTypeVariable {
    TypeRef type;  // Either given or resolved, depending on context.
    TypeVariable *tv;

    GenericTypeVariable() : tv(nullptr) {}
    GenericTypeVariable(TypeRef _type, TypeVariable *_tv) : type(_type), tv(_tv) {}
};

struct DispatchEntry {
    SubFunction *sf = nullptr;          // if !is_switch_dispatch
    int case_index = -1;                // if is_switch_dispatch
    bool is_switch_dispatch = false;
    bool is_dispatch_root = false;
    // Shared return type if root of dispatch.
    TypeRef returntype = nullptr;
    int returned_thru_to_max = -1;
    size_t subudts_size = 0;  // At time of creation.
};

// This contains the declaration-side stuff for any UDT, and may contain
// generics and generic types.
struct GUDT : Named {
    Line line;
    vector<GenericTypeVariable> generics;
    UDT *first = nullptr;  // Specializations
    vector<Field> fields;
    TypeRef gsuperclass;
    bool is_struct = false;
    bool is_abstract = false;
    bool isprivate = false;
    bool predeclaration = false;
    //bool is_generic = false;
    bool has_subclasses = false;
    bool has_constructor_function = false;
    SpecUDT unspecialized;
    Type unspecialized_type;
    map<string_view, string_view> attributes;

    GUDT(string_view _name, int _idx, bool is_struct, Line &line)
        : Named(_name, _idx),
          line(line),
          is_struct(is_struct),
          unspecialized(this),
          unspecialized_type(&unspecialized) {
    }

    bool IsGeneric() {
        return !generics.empty();
    }

    int Has(SharedField *fld) {
        for (auto &uf : fields) {
            if (uf.id == fld) return int(&uf - &fields[0]);
        }
        return -1;
    }
};

// This is a fully specialized instance of a GUDT.
struct UDT : Named {
    GUDT &g;
    UDT *next = nullptr;  // Other specializations of this GUDT.
    vector<TypeRef> bound_generics;
    vector<SField> sfields;
    UDT *ssuperclass = nullptr;
    bool hasref = false;
    bool unnamed_specialization = false;
    Type thistype;  // convenient place to store the type corresponding to this.
    TypeRef sametype = type_undefined;  // If all fields are int/float, this allows vector ops.
    type_elem_t typeinfo = (type_elem_t)-1;  // Runtime type.
    int numslots = -1;
    int vtable_start = -1;
    int serializable_id = -1;
    vector<UDT *> subudts;  // Including self.
    bool subudts_dispatched = false;
    // Subset of methods that participate in dynamic dispatch. Order in this table determines
    // vtable layout and is compatible with sub/super classes.
    // Multiple specializations of a method may be in here.
    // Methods whose dispatch can be determined statically for the current program do not end up
    // in here.
    vector<DispatchEntry> dispatch_table;

    UDT(string_view _name, int _idx, GUDT &g) : Named(_name, _idx), g(g) {
        thistype = g.is_struct ? Type { V_STRUCT_R, this } : Type { V_CLASS, this };
    }

    vector<GenericTypeVariable> GetBoundGenerics() {
        auto generics = g.generics;
        for (auto [i, gtv] : enumerate(generics)) {
            gtv.type = { bound_generics[i] };
        }
        return generics;
    }

    bool ComputeSizes(int depth = 0) {
        if (numslots >= 0) return true;
        if (depth > 16) return false;  // Simple protection against recursive references.
        int size = 0;
        for (auto &sfield : sfields) {
            sfield.slot = size;
            if (IsStruct(sfield.type->t)) {
                if (!sfield.type->udt->ComputeSizes(depth + 1)) return false;
                size += sfield.type->udt->numslots;
            } else {
                size++;
            }
        }
        numslots = size;
        return true;
    }

    flatbuffers::Offset<bytecode::UDT> Serialize(flatbuffers::FlatBufferBuilder &fbb) {
        vector<flatbuffers::Offset<bytecode::Field>> fieldoffsets;
        for (auto [i, sfield] : enumerate(sfields))
            fieldoffsets.push_back(
                bytecode::CreateField(fbb, fbb.CreateString(g.fields[i].id->name), sfield.slot));
        return bytecode::CreateUDT(fbb, fbb.CreateString(name), idx, fbb.CreateVector(fieldoffsets),
                                   numslots, ssuperclass ? ssuperclass->idx : -1, typeinfo);
    }
};

GUDT *GetGUDTSuper(TypeRef type) {
    return type->t == V_UNDEFINED ? nullptr : (type->t == V_UUDT ? type->spec_udt->gudt : &type->udt->g);
}

GUDT *GetGUDTAny(TypeRef type) {
    return type->t == V_UUDT ? type->spec_udt->gudt : (IsUDT(type->t) ? &type->udt->g : nullptr);
}

inline TypeRef SingleNonGenericSpecialization(GUDT &gudt) {
    // Not generic, so must have just 1 specialization:
    assert(!gudt.IsGeneric() && gudt.first && !gudt.first->next);
    return &gudt.first->thistype;
}

    // Distance to the exact type "super".
int SuperDistance(const UDT *super, const UDT *subclass) {
    int dist = 0;
    for (auto t = subclass; t; t = t->ssuperclass) {
        if (t == super) return dist;
        dist++;
    }
    return -1;
}

int DistanceToSpecializedSuper(const GUDT *super, const UDT *subclass) {
    int dist = 0;
    for (auto t = subclass; t; t = t->ssuperclass) {
        for (auto s = super->first; s; s = s->next)
            if (t == s) return dist;
        dist++;
    }
    return -1;
}

int DistanceFromSpecializedSub(const UDT *super, const GUDT *subclass) {
    int dist = 0;
    for (auto t = subclass; t; t = GetGUDTSuper(t->gsuperclass)) {
        for (auto u = t->first; u; u = u->next)
            if (u == super) return dist;
        dist++;
    }
    return -1;
}

const UDT *CommonSuperType(const UDT *a, const UDT *b) {
    if (a != b) {
        for (;;) {
            if (SuperDistance(a, b) >= 0) break;
            a = a->ssuperclass;
            if (!a) return nullptr;
        }
    }
    return a;
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

inline const SField *FindSlot(const UDT &udt, int i) {
    for (auto &sfield : udt.sfields) {
        if (i >= sfield.slot && i < sfield.slot + ValWidth(sfield.type)) {
            return IsStruct(sfield.type->t) ? FindSlot(*sfield.type->udt, i - sfield.slot) : &sfield;
        }
    }
    assert(false);
    return nullptr;
}

struct Arg {
    TypeRef type = type_undefined;
    SpecIdent *sid = nullptr;

    Arg() = default;
    Arg(const Arg &o) = default;
    Arg(SpecIdent *_sid, TypeRef _type)
        : type(_type), sid(_sid) {}
};

struct Function;
struct SubFunction;

struct Overload {
    SubFunction *sf = nullptr;
    Block *gbody = nullptr;
    Line declared_at;
    bool isprivate;
    GUDT *method_of = nullptr;

    Overload(Line da, bool p) : declared_at(da), isprivate(p) {}

    ~Overload();

    int NumSubf();
};

struct SubFunction {
    int idx;
    vector<Arg> args;
    vector<Arg> locals;
    vector<Arg> freevars;       // any used from outside this scope
    vector<TypeRef> giventypes;  // before specialization, includes typevars. FIXME: Only needed once per overload
    TypeRef returngiventype = nullptr;
    TypeRef returntype = type_undefined;
    size_t num_returns = 0;
    size_t num_returns_non_local = 0;
    size_t reqret = 0;  // Do the caller(s) want values to be returned?
    const Lifetime ltret = LT_KEEP;
    vector<pair<const SubFunction *, TypeRef>> reuse_return_events;
    small_vector<Node *, 4> reuse_assign_events;
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
    bool explicit_generics = false;
    int returned_thru_to_max = -1;  // >=0: there exist return statements that may skip the caller.
    UDT *method_of = nullptr;
    int numcallers = 0;
    Type thistype { V_FUNCTION, this };  // convenient place to store the type corresponding to this
    vector<GenericTypeVariable> generics;
    map<string_view, string_view> attributes;
    Overload *lexical_parent = nullptr;
    Overload *overload = nullptr;
    size_t node_count = 0;

    SubFunction(int _idx) : idx(_idx) {}

    void SetParent(Function &f, Overload &ov) {
        parent = &f;
        next = ov.sf;
        ov.sf = this;
        ov.sf->overload = &ov;
    }

    bool AddFreeVar(SpecIdent &sid) {
        auto lower = std::lower_bound(freevars.begin(), freevars.end(), sid.id,
                                      [&](const Arg &e, Ident *id) {
                return e.sid->id < id;
            });
        if (lower != freevars.end() && lower->sid->id == sid.id) return true;
        freevars.insert(lower, Arg(&sid, sid.type));
        return false;
    }

    ~SubFunction();
};

struct Function : Named {
    // Start of all SubFunctions sequentially.
    int bytecodestart = 0;
    // functions with the same name and args, but different types (dynamic dispatch |
    // specialization)
    vector<Overload *> overloads;
    // functions with the same name but different number of args (overloaded)
    Function *sibf = nullptr;
    Function *first = this;
    // does not have a programmer specified name
    bool anonymous = false;
    // its merely a function type, has no body, but does have a set return type.
    bool istype = false;
    GUDT *is_constructor_of = nullptr;

    size_t scopelevel;

    small_vector<Node *, 4> default_args;
    int first_default_arg = -1;

    Function(string_view _name, int _idx, size_t _sl)
        : Named(_name, _idx), scopelevel(_sl) {
    }

    ~Function();

    size_t nargs() const { return overloads[0]->sf->args.size(); }

    int NumSubf() {
        int sum = 0;
        for (auto ov : overloads) sum += ov->NumSubf();
        return sum;
    }

    bool RemoveSubFunction(SubFunction *sf) {
        for (auto [i, ov] : enumerate(overloads)) {
            for (auto sfp = &ov->sf; *sfp; sfp = &(*sfp)->next) {
                if (*sfp == sf) {
                    *sfp = sf->next;
                    sf->next = nullptr;
                    if (!ov->sf) {
                        delete overloads[i];
                        overloads.erase(overloads.begin() + i);
                    }
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

template<> void ErasePrivate(unordered_map<string_view, UDT *> &dict) {
    auto it = dict.begin();
    while (it != dict.end()) {
        auto n = it->second;
        it++;
        if (n->g.isprivate) Unregister(n, dict);
    }
}

inline string TypeName(TypeRef type, int flen = 0);

struct SymbolTable {
    Lex &lex;

    unordered_map<string_view, Ident *> idents;  // Key points to value!
    vector<Ident *> identtable;
    vector<Ident *> identstack;
    vector<SpecIdent *> specidents;

    unordered_map<string_view, UDT *> udts;  // Key points to value!
    unordered_map<string_view, GUDT *> gudts;  // Key points to value!
    vector<UDT *> udttable;
    vector<GUDT *> gudttable;

    unordered_map<string_view, SharedField *> fields;  // Key points to value!
    vector<SharedField *> fieldtable;

    unordered_map<string, vector<Function *>> functions;
    unordered_map<string_view, Function *> operators;  // Key points to value!
    vector<Function *> functiontable;
    vector<SubFunction *> subfunctiontable;
    SubFunction *toplevel = nullptr;

    unordered_map<string_view, Enum *> enums;  // Key points to value!
    unordered_map<string_view, EnumVal *> enumvals;  // Key points to value!
    vector<Enum *> enumtable;

    vector<TypeVariable *> typevars;
    vector<vector<GenericTypeVariable>> bound_typevars_stack;

    vector<size_t> scopelevels;

    struct WithStackElem {
        GUDT *gudt = nullptr;
        Ident *id = nullptr;
        SubFunction *sf = nullptr;
        UDT *udt_tc = nullptr;  // Only in TC.
    };
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

    string_view current_namespace;
    vector<string_view> namespace_stack;

    // FIXME: because we cleverly use string_view's into source code everywhere, we now have
    // no way to refer to constructed strings, and need to store them seperately :(
    // TODO: instead use larger buffers and constuct directly into those, so no temp string?
    vector<const char *> stored_names;

    function<void(UDT &)> type_check_call_back;

    SymbolTable(Lex &lex) : lex(lex) {
        type_check_call_back = [](UDT &) {};
        namespace_stack.push_back({});
    }

    ~SymbolTable() {
        for (auto id  : identtable)       delete id;
        for (auto sid : specidents)       delete sid;
        for (auto u   : udttable)         delete u;
        for (auto gu  : gudttable)        delete gu;
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
    
    bool MaybeNameSpace(string_view name) {
        return !current_namespace.empty() && name.find(".") == name.npos;
    }

    string NameSpaced(string_view name, string_view ns) {
        return cat(ns, ".", name);
    }

    string NameSpaced(string_view name) {
        assert(MaybeNameSpace(name));
        return NameSpaced(name, current_namespace);
    }

    string_view StoreName(const string &s) {
        auto buf = new char[s.size()];
        memcpy(buf, s.data(), s.size());  // Look ma, no terminator :)
        stored_names.push_back(buf);
        return string_view(buf, s.size());
    }

    string_view MaybeMakeNameSpace(string_view name, bool other_conditions) {
        return other_conditions && scopelevels.size() == 1 && MaybeNameSpace(name)
            ? StoreName(NameSpaced(name))
            : name;
    }

    Ident *Lookup(string_view name) {
        if (MaybeNameSpace(name)) {
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

    Ident *NewId(string_view name, SubFunction *sf, bool withtype, size_t scopelevel, Line &line) {
        auto ident = new Ident(name, (int)identtable.size(), scopelevel, line);
        ident->cursid = NewSid(ident, sf, withtype);
        identtable.push_back(ident);
        idents[ident->name /* must be in value */] = ident;
        identstack.push_back(ident);
        return ident;
    }

    Ident *LookupDefWS(string_view name) {
        Ident *ident = nullptr;
        if (LookupWithStruct(name, ident))
            lex.Error("cannot define variable with same name as field in this scope: " + name);
        return Lookup(name);
    }

    Ident *LookupDef(string_view name, bool islocal, bool withtype) {
        auto ident = LookupDefWS(name);
        if (ident) {
            if (scopelevels.size() != ident->scopelevel)
                lex.Error(cat("identifier shadowing: ", name));
            if (!ident->predeclaration)
                lex.Error(cat("identifier redefinition: ", name));
            return ident;
        }
        auto sf = defsubfunctionstack.back();
        ident = NewId(name, sf, withtype, scopelevels.size(), lex);
        (islocal ? sf->locals : sf->args).push_back(
            Arg(ident->cursid, type_any));
        return ident;
    }

    Ident *LookupDefStatic(string_view name) {
        auto ident = LookupDefWS(name);
        if (ident)
            lex.Error(cat("identifier shadowing/redefinition: ", name));
        auto sf = defsubfunctionstack[0];
        // Is going to get removed as if it was part of the current function.
        ident = NewId(name, sf, false, 1, lex);
        sf->locals.push_back(Arg(ident->cursid, type_any));
        return ident;
    }

    void AddWithStruct(TypeRef type, Ident *id, SubFunction *sf) {
        auto gudt = GetGUDTAny(type);
        if (!gudt) lex.Error(":: can only be used with struct/class types");
        for (auto &wp : withstack)
            if (wp.gudt == gudt)
                lex.Error("type used twice in the same scope with ::");
        // FIXME: should also check if variables have already been defined in this scope that clash
        // with the struct, or do so in LookupUse
        withstack.push_back({ gudt, id, sf });
    }

    void AddWithStructTT(TypeRef type, Ident *id, SubFunction *sf) {
        assert(type->t != V_UUDT);
        withstack.push_back({ &type->udt->g, id, sf, type->udt });
    }

    SharedField *LookupWithStruct(string_view name, Ident *&id) {
        auto fld = FieldUse(name);
        if (!fld) return nullptr;
        assert(!id);
        for (auto &wse : withstack) {
            if (wse.gudt->Has(fld) >= 0) {
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
        if (!defsubfunctionstack.empty())
            sf->lexical_parent = defsubfunctionstack.back()->parent->overloads.back();
        defsubfunctionstack.push_back(sf);
        return sf;
    }

    void FunctionScopeCleanup(size_t count) {
        auto sf = defsubfunctionstack.back();
        sf->node_count = count;
        defsubfunctionstack.pop_back();
        BlockScopeCleanup();
    }

    template<typename F> void PopOutOfFunctionScope(F f) {
        // This is a bit of a hack, but we have the need to parse default expressions while
        // already inside a function scope, but that exp shouldn't touch the function scope.
        // So temp remove and put back is the easiest way around that, since starting the scope
        // later would affect the args being parsed (if this ever is a problem, could attempt to
        // parse those without processing first).
        auto sf = defsubfunctionstack.back();
        auto sl = scopelevels.back();
        auto ws = withstacklevels.back();
        defsubfunctionstack.pop_back();
        scopelevels.pop_back();
        withstacklevels.pop_back();
        f();
        withstacklevels.push_back(ws);
        scopelevels.push_back(sl);
        defsubfunctionstack.push_back(sf);
    }

    void    ister(const Enum *e, unordered_map<string_view, Enum *> &dict) {
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

    void Unregister(const Function *f) {
        auto &v = functions[f->name];
        if (!v.empty() && v.back() == f) {
            v.pop_back();
        }
    }

    void StartOfInclude() {
        namespace_stack.push_back(current_namespace);
        current_namespace = {};
    }

    void EndOfInclude() {
        current_namespace = namespace_stack.back();
        namespace_stack.pop_back();
        ErasePrivate(idents);
        ErasePrivate(gudts);
        ErasePrivate(udts);
        ErasePrivate(enums);
        // Note: can't remove functions here, because final function lookup is in typechecker.
    }

    Enum *EnumLookup(string_view name, bool decl) {
        auto eit = enums.find(name);
        if (eit != enums.end()) {
            if (decl) lex.Error("double declaration of enum: " + name);
            return eit->second;
        }
        if (!decl) {
            if (MaybeNameSpace(name)) {
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

    EnumVal *EnumValLookup(string_view name, bool decl) {
        if (!decl) {
            if (MaybeNameSpace(name)) {
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

    GUDT &StructDecl(string_view name, bool is_struct, Line &line) {
        auto udt = LookupSpecialization(name);
        if (udt && !udt->g.predeclaration)
            lex.Error("type previously declared as specialization: " + name);
        auto uit = gudts.find(name);
        if (uit != gudts.end()) {
            if (!uit->second->predeclaration)
                lex.Error("double declaration of type: " + name);
            if (uit->second->is_struct != is_struct)
                lex.Error("class/struct previously declared as different kind");
            uit->second->predeclaration = false;
            return *uit->second;
        }
        auto st = new GUDT(name, (int)gudttable.size(), is_struct, line);
        gudts[st->name /* must be in value */] = st;
        gudttable.push_back(st);
        return *st;
    }

    GUDT *LookupStruct(string_view name) {
        if (MaybeNameSpace(name)) {
            auto uit = gudts.find(NameSpaced(name));
            if (uit != gudts.end()) return uit->second;
        }
        auto uit = gudts.find(name);
        if (uit != gudts.end()) return uit->second;
        return nullptr;
    }

    GUDT &StructUse(string_view name) {
        auto gudt = LookupStruct(name);
        if (!gudt) lex.Error("unknown type: " + name);
        return *gudt;
    }

    UDT *MakeSpecialization(GUDT &gudt, string_view sname, bool named, bool from_generic) {
        auto st = new UDT(sname, (int)udttable.size(), gudt);
        st->thistype.udt = st;
        st->unnamed_specialization = !named;
        st->next = gudt.first;
        gudt.first = st;
        udttable.push_back(st);
        if (named) {
            if (LookupStruct(sname))
                lex.Error("specialization previously declared as type: " + sname);
            auto uit = udts.find(sname);
            if (uit != udts.end()) {
                lex.Error("double declaration of specialization: " + sname);
            }
        }
        if (named || !from_generic) {
            udts[st->name /* must be in value */] = st;
        }
        return st;
    }

    UDT *LookupSpecialization(string_view name) {
        if (MaybeNameSpace(name)) {
            auto uit = udts.find(NameSpaced(name));
            if (uit != udts.end()) return uit->second;
        }
        auto uit = udts.find(name);
        if (uit != udts.end()) return uit->second;
        return nullptr;
    }

    pair<GUDT *, UDT *> StructOrSpecializationUse(string_view name) {
        auto udt = LookupSpecialization(name);
        if (udt) return { &udt->g, udt };
        auto gudt = LookupStruct(name);
        if (gudt) return { gudt, nullptr };
        lex.Error("unknown type: " + name);
        return { nullptr, nullptr };
    }

    SharedField &FieldDecl(string_view name, GUDT *gudt) {
        auto fld = FieldUse(name);
        if (!fld) {
            fld = new SharedField(name, (int)fieldtable.size());
            fields[fld->name /* must be in value */] = fld;
            fieldtable.push_back(fld);
        }
        if (gudt->Has(fld) >= 0) {
            lex.Error("double declaration of field: " + name);
        }
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

    Function &FunctionDecl(const string &name, size_t nargs) {
        auto &v = functions[name];
        if (!v.empty() && v.back()->scopelevel == scopelevels.size()) {
            for (auto f = v.back(); f; f = f->sibf) {
                if (f->nargs() == nargs) {
                    return *f;
                }
            }
            auto &f = CreateFunction(name);
            f.first = v.back()->first;
            // Insert in sibf linked list such that highest nargs variants come first,
            // this is useful later when deciding which variant to use.
            Function **it = &v.back();
            for (; *it; it = &(*it)->sibf) {
                if (nargs > (*it)->nargs()) {
                    // Insert before this element.
                    f.sibf = *it;
                    if (it == &v.back()) {
                        // We have a new first.
                        for (auto g = &f; g; g = g->sibf) g->first = &f;
                    }
                    break;
                }
            }
            // If we got to the end of loop, just insert last.
            *it = &f;
            return f;
        } else {
            auto &f = CreateFunction(name);
            v.push_back(&f);
            // Store top level functions, for now only operators needed.
            if (scopelevels.size() == 2 && name.substr(0, 8) == TName(T_OPERATOR)) {
                operators[f.name /* must be in value */] = &f;
            }
            return f;
        }
    }

    void FunctionDeclTT(Function &f) {
        auto &v = functions[f.name];
        if (!v.empty()) {
            for (auto ff = v.back(); ff; ff = ff->sibf)
                if (ff == &f) return;
        }
        v.push_back(&f);
    }

    Function *GetFirstFunction(const string &name) {
        auto &v = functions[name];
        return v.empty() ? nullptr : v.back();
    }

    Function *FindFunction(string_view name) {
        if (MaybeNameSpace(name)) {
            auto &v = functions[NameSpaced(name)];
            if (!v.empty()) return v.back();
        }
        auto &v = functions[string(name)];
        if (!v.empty()) return v.back();
        return nullptr;
    }

    SpecIdent *NewSid(Ident *id, SubFunction *sf, bool withtype, TypeRef type = nullptr) {
        auto sid = new SpecIdent(id, type, (int)specidents.size(), withtype);
        sid->sf_def = sf;
        specidents.push_back(sid);
        return sid;
    }

    void CloneSids(vector<Arg> &av, SubFunction *sf) {
        for (auto &a : av) {
            a.sid = NewSid(a.sid->id, sf, a.sid->withtype);
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

    TypeRef NewTypeVar() {
        auto var = NewType();
        *var = Type(V_VAR);
        // Vars store a cycle of all vars its been unified with, starting with itself.
        var->sub = var;
        return var;
    }

    TypeRef NewNilTypeVar() {
        auto nil = NewType();
        *nil = Type(V_NIL);
        nil->sub = &*NewTypeVar();
        return nil;
    }

    TypeRef NewTuple(size_t sz) {
        auto type = NewType();
        *type = Type(V_TUPLE);
        type->tup = new vector<Type::TupleElem>(sz);
        tuplelist.push_back(type->tup);
        return type;
    }

    TypeRef NewSpecUDT(GUDT *gudt) {
        auto su = new SpecUDT(gudt);
        specudts.push_back(su);
        auto nt = NewType();
        *nt = Type(su);
        return nt;
    }

    TypeRef Wrap(TypeRef elem, ValueType with, const Line *errl = nullptr) {
        if (with == V_NIL && elem->t != V_VAR && elem->t != V_TYPEVAR && !IsNillable(elem))
            lex.Error("cannot construct nillable type from " + Q(TypeName(elem)), errl);
        auto wt = WrapKnown(elem, with);
        if (!wt.Null()) return wt;
        return elem->Wrap(NewType(), with);
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
        static const char *names[] = { "int2", "int3", "int4", nullptr };
        return names;
    }

    static const char **DefaultFloatVectorTypeNames() {
        static const char *names[] = { "float2", "float3", "float4", nullptr };
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

    bool IsGeneric(TypeRef type) {
        auto u = type->UnWrapAll();
        return u->t == V_TYPEVAR ||
               (u->t == V_UUDT && u->spec_udt->IsGeneric());
    }

    bool IsNillable(TypeRef type) {
        return (IsRef(type->t) && type->t != V_STRUCT_R) ||
               (type->t == V_UUDT && !type->spec_udt->gudt->is_struct);
    }

    TypeVariable *NewGeneric(string_view name) {
        auto tv = new TypeVariable { name };
        typevars.push_back(tv);
        return tv;
    }

    TypeRef ResolveTypeVars(TypeRef type, const Line &errl) {
        switch (type->t) {
            case V_NIL:
            case V_VECTOR: {
                auto nt = ResolveTypeVars({ type->Element() }, errl);
                if (&*nt != &*type->Element()) {
                    return Wrap(nt, type->t, &errl);
                }
                return type;
            }
            case V_TUPLE: {
                vector<TypeRef> types;
                bool same = true;
                for (auto [i, te] : enumerate(*type->tup)) {
                    auto tr = ResolveTypeVars({ te.type }, errl);
                    types.push_back(tr);
                    if (!tr->Equal(*te.type)) same = false;
                }
                if (same) return type;
                auto nt = NewTuple(type->tup->size());
                for (auto [i, te] : enumerate(*type->tup)) {
                    nt->Set(i, &*types[i], te.lt);
                }
                return nt;
            }
            case V_UUDT: {
                vector<TypeRef> types;
                for (auto s : type->spec_udt->specializers) {
                    auto t = ResolveTypeVars({ s }, errl);
                    types.push_back(&*t);
                }
                for (auto udti = type->spec_udt->gudt->first; udti; udti = udti->next) {
                    assert(udti->bound_generics.size() == types.size());
                    for (auto [i, gtype] : enumerate(udti->bound_generics)) {
                        if (!gtype->Equal(*types[i])) goto nomatch;
                    }
                    return &udti->thistype;
                    nomatch:;
                }
                // No existing specialization found, create a new one.
                auto udt =
                    MakeSpecialization(*type->spec_udt->gudt, type->spec_udt->gudt->name, false, true);
                if (udt->g.generics.size() != types.size()) {
                    // FIXME: this can happen for class foo<T> : bar<T, .. > where generics
                    // are inherited from bar.
                    lex.Error(cat("internal: missing specializers for ", Q(TypeName(type))),
                              &errl);
                }
                udt->bound_generics = types;
                ResolveFields(*udt, errl);
                type_check_call_back(*udt);
                return &udt->thistype;
            }
            case V_TYPEVAR: {
                for (auto &bvec : reverse(bound_typevars_stack)) {
                    for (auto &gtv : bvec) {
                        if (gtv.tv == type->tv && !gtv.type.Null()) return gtv.type;
                    }
                }
                lex.Error(cat("could not resolve type variable ", Q(type->tv->name)), &errl);
                return type;
            }
            default:
                return type;
        }
    }

    void PushSuperGenerics(UDT *u) {
        for (; u; u = u->ssuperclass) {
            bound_typevars_stack.push_back(u->GetBoundGenerics());
        }
    }

    void PopSuperGenerics(UDT *u) {
        for (; u; u = u->ssuperclass) {
            bound_typevars_stack.pop_back();
        }
    }

    void ResolveFields(UDT &udt, const Line &errl) {
        bound_typevars_stack.push_back(udt.GetBoundGenerics());
        auto supertype = ResolveTypeVars(udt.g.gsuperclass, errl);
        if (supertype->t != V_UNDEFINED) {
            assert(IsUDT(supertype->t));
            udt.ssuperclass = supertype->udt;
        }
        PushSuperGenerics(udt.ssuperclass);
        for (auto &field : udt.g.fields) {
            udt.sfields.push_back({ ResolveTypeVars(field.giventype, errl) });
        }
        PopSuperGenerics(udt.ssuperclass);
        bound_typevars_stack.pop_back();
        // NOTE: all users of sametype will only act on it if it is numeric, since
        // otherwise it would a scalar field to become any without boxing.
        if (udt.sfields.size() >= 1) {
            udt.sametype = udt.sfields[0].type;
            for (size_t i = 1; i < udt.sfields.size(); i++) {
                // Can't use Union here since it will bind variables, use simplified
                // alternative:
                if (!udt.sfields[i].type->Equal(*udt.sametype)) {
                    udt.sametype = type_undefined;
                    break;
                }
            }
        }
        // Update the type to the correct struct type.
        if (udt.g.is_struct) {
            for (auto &sfield : udt.sfields) {
                if (IsRefNil(sfield.type->t)) {
                    udt.hasref = true;
                    break;
                }
            }
            const_cast<ValueType &>(udt.thistype.t) = udt.hasref ? V_STRUCT_R : V_STRUCT_S;
        }
    }

    void Serialize(vector<int> &code,
                   vector<type_elem_t> &typetable,
                   vector<bytecode::LineInfo> &linenumbers,
                   vector<bytecode::SpecIdent> &sids,
                   vector<string_view> &stringtable,
                   string &bytecode,
                   vector<int> &vtables,
                   vector<pair<string, string>> &filenames,
                   vector<type_elem_t> &ser_ids,
                   uint64_t src_hash) {
        flatbuffers::FlatBufferBuilder fbb;
        // Always serialize this first! that way it can easily be left out of the generated C code.
        auto codevec = fbb.CreateVector(code);
        vector<flatbuffers::Offset<flatbuffers::String>> fns;
        for (auto &f : filenames) fns.push_back(fbb.CreateString(f.first));
        vector<flatbuffers::Offset<bytecode::Function>> functionoffsets;
        for (auto f : functiontable) functionoffsets.push_back(f->Serialize(fbb));
        vector<flatbuffers::Offset<bytecode::UDT>> udtoffsets;
        for (auto u : udttable) udtoffsets.push_back(u->Serialize(fbb));
        vector<flatbuffers::Offset<bytecode::Ident>> identoffsets;
        for (auto i : identtable) identoffsets.push_back(i->Serialize(fbb, i->scopelevel == 1));
        vector<flatbuffers::Offset<bytecode::Enum>> enumoffsets;
        for (auto e : enumtable) enumoffsets.push_back(e->Serialize(fbb));
        string build_info;
        auto time = std::time(nullptr);
        if (time) {
            auto tm = std::localtime(&time);
            if (tm) {
                auto ts = std::asctime(tm);
                build_info = string(ts, 24);
            }
        }
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
            fbb.CreateVector(vtables),
            fbb.CreateVector((vector<int> &)ser_ids),
            fbb.CreateString(build_info.c_str(), build_info.size()),
            src_hash);
        bytecode::FinishBytecodeFileBuffer(fbb, bcf);
        bytecode.assign(fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    }
};

inline string TypeName(TypeRef type, int flen);

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
    for (auto [i, f] : enumerate(udt.g.fields)) {
        FormatArg(r, f.id->name, i, udt.sfields[i].type);
    }
    r += "}";
    return r;
}

inline string Signature(const GUDT &gudt) {
    string r = gudt.name;
    r += "{";
    for (auto [i, f] : enumerate(gudt.fields)) {
        FormatArg(r, f.id->name, i, f.giventype);
    }
    r += "}";
    return r;
}

inline string Signature(const SubFunction &sf) {
    string r = sf.parent->name;
    if (!sf.generics.empty() && sf.explicit_generics) {
        r += "<";
        for (auto [i, gtv] : enumerate(sf.generics)) {
            if (i) r += ",";
            r += gtv.type.Null() ? gtv.tv->name : TypeName(gtv.type);
        }
        r += ">";
    }
    r += "(";
    for (auto [i, arg] : enumerate(sf.args)) {
        FormatArg(r, arg.sid->id->name, i, arg.type);
    }
    r += ")";
    if (sf.returntype->t != V_VOID && sf.returntype->t != V_UNDEFINED && sf.returntype->t != V_VAR) {
        r += "->";
        r += TypeName(sf.returntype);
    }
    return r;
}

inline string TypeName(TypeRef type, int flen) {
    switch (type->t) {
        case V_STRUCT_R:
        case V_STRUCT_S:
        case V_CLASS: {
            string s = type->udt->name;
            if (type->udt->unnamed_specialization && !type->udt->bound_generics.empty()) {
                s += "<";
                for (auto [i, t] : enumerate(type->udt->bound_generics)) {
                    if (i) s += ", ";
                    s += TypeName(t);
                }
                s += ">";
            }
            return s;
        }
        case V_UUDT: {
            string s = type->spec_udt->gudt->name;
            if (!type->spec_udt->specializers.empty()) {
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
            return type->sf
                ? Signature(*type->sf)
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

}  // namespace lobster

#endif  // LOBSTER_IDENTS
