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

#ifndef LOBSTER_NODE
#define LOBSTER_NODE

namespace lobster {

typedef const function<void (Node *)> &IterateFun;

struct TypeChecker;
struct CodeGen;

struct Node {
    Line line;
    TypeRef exptype;
    virtual ~Node() {};
    virtual size_t Arity() const { return 0; }
    virtual Node **Children() { return nullptr; }
    virtual Node *Clone() = 0;
    virtual bool IsConstInit() const { return false; }
    virtual string_view Name() const = 0;
    virtual void Dump(ostringstream &ss) const { ss << Name(); }
    void Iterate(IterateFun f) {
        f(this);
        auto ch = Children();
        if (ch) for (size_t i = 0; i < Arity(); i++) ch[i]->Iterate(f);
    }
    // Used in the optimizer to see if this node can be discarded without consequences.
    virtual bool SideEffect() const = 0;  // Just this node.
    bool HasSideEffects() {  // Transitively.
        bool se = false;
        Iterate([&](Node *n) { se = se || n->SideEffect(); });
        return se;
    }
    size_t Count() {
        size_t count = 0;
        Iterate([&](Node *) { count++; });
        return count;
    }
    // Used by type-checker to and optimizer.
    // If it returns true, sets val to a value that gives the correct True().
    // Also sets correct scalar values.
    virtual bool ConstVal(TypeChecker &, Value &) const { return false; }
    virtual Node *TypeCheck(TypeChecker &tc, bool reqret) = 0;
    virtual void Generate(CodeGen &cg, int retval) const = 0;
  protected:
    Node(const Line &ln) : line(ln) {}
    Node() : line(0, 0) {}
};

template<typename T> T *DoClone(T *dest, T *src) {
    *dest = *src;  // Copy contructor copies all values & non-owned.
    for (size_t i = 0; i < src->Arity(); i++) dest->Children()[i] = src->Children()[i]->Clone();
    return dest;
}

#define SHARED_SIGNATURE(NAME, STR, SE) \
    string_view Name() const { return STR; } \
    bool SideEffect() const { return SE; } \
    Node *TypeCheck(TypeChecker &tc, bool reqret); \
    void Generate(CodeGen &cg, int retval) const; \
    Node *Clone() { return DoClone<NAME>(new NAME(), this); } \
  protected: \
    NAME() {};  // Only used by clone.

#define ZERO_NODE(NAME, STR, SE, METHODS) \
struct NAME : Node { \
    NAME(const Line &ln) : Node(ln) {} \
    SHARED_SIGNATURE(NAME, STR, SE) \
    METHODS \
};

#define UNARY_NODE(NAME, STR, SE, A, METHODS) \
struct NAME : Node { \
    Node *A; \
    NAME(const Line &ln, Node *_a) : Node(ln), A(_a) {} \
    ~NAME() { delete A; } \
    size_t Arity() const { return 1; } \
    Node **Children() { return &A; } \
    SHARED_SIGNATURE(NAME, STR, SE) \
    METHODS \
};

#define UNOP_NODE(NAME, STR, SE, METHODS) \
struct NAME : Unary { \
    NAME(const Line &ln, Node *_a) : Unary(ln, _a) {} \
    SHARED_SIGNATURE(NAME, STR, SE) \
    METHODS \
};

#define BINARY_NODE_T(NAME, STR, SE, AT, A, BT, B, METHODS) \
struct NAME : Node { \
    AT *A; BT *B; \
    NAME(const Line &ln, AT *_a, BT *_b) : Node(ln), A(_a), B(_b) {}; \
    ~NAME() { delete A; delete B; } \
    size_t Arity() const { return 2; } \
    Node **Children() { return (Node **)&A; } \
    SHARED_SIGNATURE(NAME, STR, SE) \
    METHODS \
};
#define BINARY_NODE(NAME, STR, SE, A, B, METHODS) \
    BINARY_NODE_T(NAME, STR, SE, Node, A, Node, B, METHODS)

#define BINOP_NODE(NAME, STR, SE, METHODS) \
struct NAME : BinOp { \
    NAME(const Line &ln, Node *_a, Node *_b) : BinOp(ln, _a, _b) {}; \
    SHARED_SIGNATURE(NAME, STR, SE) \
    METHODS \
};

#define TERNARY_NODE(NAME, STR, SE, A, B, C, METHODS) \
struct NAME : Node { \
    Node *A, *B, *C; \
    NAME(const Line &ln, Node *_a, Node *_b, Node *_c) : Node(ln), A(_a), B(_b), C(_c) {} \
    ~NAME() { delete A; delete B; delete C; } \
    size_t Arity() const { return 3; } \
    Node **Children() { return &A; } \
    SHARED_SIGNATURE(NAME, STR, SE) \
    METHODS \
};

#define NARY_NODE(NAME, STR, SE, METHODS) \
struct NAME : Node { \
    vector<Node *> children; \
    NAME(const Line &ln) : Node(ln) {}; \
    ~NAME() { for (auto n : children) delete n; } \
    size_t Arity() const { return children.size(); } \
    Node **Children() { return children.data(); } \
    NAME *Add(Node *a) { children.push_back(a); return this; }; \
    SHARED_SIGNATURE(NAME, STR, SE) \
    METHODS \
};

struct TypeAnnotation : Node {
    TypeRef giventype;
    TypeAnnotation(const Line &ln, TypeRef tr) : Node(ln), giventype(tr) {}
    void Dump(ostringstream &ss) const { ss << TypeName(giventype); }
    SHARED_SIGNATURE(TypeAnnotation, "type", false)
};

#define CONSTVALMETHOD bool ConstVal(TypeChecker &tc, Value &val) const;

// generic node types
NARY_NODE(List, "list", false, )
UNARY_NODE(Unary, "unary", false, child, )
BINARY_NODE(BinOp, "binop", false, left, right, )

BINOP_NODE(Plus, TName(T_PLUS), false, )
BINOP_NODE(Minus, TName(T_MINUS), false, )
BINOP_NODE(Multiply, TName(T_MULT), false, )
BINOP_NODE(Divide, TName(T_DIV), false, )
BINOP_NODE(Mod, TName(T_MOD), false, )
BINOP_NODE(PlusEq, TName(T_PLUSEQ), true, )
BINOP_NODE(MinusEq, TName(T_MINUSEQ), true, )
BINOP_NODE(MultiplyEq, TName(T_MULTEQ), true, )
BINOP_NODE(DivideEq, TName(T_DIVEQ), true, )
BINOP_NODE(ModEq, TName(T_MODEQ), true, )
BINOP_NODE(And, TName(T_AND), false, CONSTVALMETHOD)
BINOP_NODE(Or, TName(T_OR), false, CONSTVALMETHOD)
UNARY_NODE(Not, TName(T_NOT), false, child, CONSTVALMETHOD)
UNOP_NODE(PreIncr, TName(T_INCR), true, )
UNOP_NODE(PreDecr, TName(T_DECR), true, )
BINOP_NODE(Equal, TName(T_EQ), false, )
BINOP_NODE(NotEqual, TName(T_NEQ), false, )
BINOP_NODE(LessThan, TName(T_LT), false, )
BINOP_NODE(GreaterThan, TName(T_GT), false, )
BINOP_NODE(LessThanEq, TName(T_LTEQ), false, )
BINOP_NODE(GreaterThanEq, TName(T_GTEQ), false, )
BINOP_NODE(BitAnd, TName(T_BITAND), false, )
BINOP_NODE(BitOr, TName(T_BITOR), false, )
BINOP_NODE(Xor, TName(T_XOR), false, )
UNARY_NODE(Negate, TName(T_NEG), false, child, )
BINOP_NODE(ShiftLeft, TName(T_ASL), false, )
BINOP_NODE(ShiftRight, TName(T_ASR), false, )
BINOP_NODE(Assign, TName(T_ASSIGN), true, )
BINOP_NODE(DynAssign, TName(T_DYNASSIGN), true, )
BINOP_NODE(LogAssign, TName(T_LOGASSIGN), true, )
BINARY_NODE(CoDot, TName(T_CODOT), false, coroutine, variable, )
ZERO_NODE(DefaultVal, "default value", false, )
UNARY_NODE(TypeOf, TName(T_TYPEOF), false, child, )
UNARY_NODE(CoRoutine, TName(T_COROUTINE), true, call, )

ZERO_NODE(CoClosure, "coroutine yield", false, )
BINARY_NODE(Seq, "statements", false, head, tail, )
BINARY_NODE(Indexing, "indexing operation", false, object, index, )
UNOP_NODE(PostIncr, TName(T_INCR), true, )
UNOP_NODE(PostDecr, TName(T_DECR), true, )
UNARY_NODE(UnaryMinus, TName(T_MINUS), false, child, )
UNOP_NODE(ToFloat, "tofloat", false, )
UNOP_NODE(ToString, "tostring", false, )
UNOP_NODE(ToAny, "toany", false, )
UNOP_NODE(ToNil, "tonil", false, )
UNOP_NODE(ToBool, "tobool", false, )
UNOP_NODE(ToInt, "toint", false, )
TERNARY_NODE(If, "if", false, condition, truepart, falsepart, )
BINARY_NODE(While, "while", false, condition, body, )
BINARY_NODE(For, "for", false, iter, body, )
ZERO_NODE(ForLoopElem, "for loop element", false, )
ZERO_NODE(ForLoopCounter, "for loop counter", false, )
NARY_NODE(Inlined, "inlined", false, )
BINARY_NODE_T(Switch, "switch", false, Node, value, List, cases, )
BINARY_NODE_T(Case, "case", false, List, pattern, Node, body, )
BINARY_NODE(Range, "range", false, start, end, )

struct Nil : Node {
	TypeRef giventype;
    Nil(const Line &ln, TypeRef tr) : Node(ln), giventype(tr) {}
    bool ConstVal(TypeChecker &, Value &val) const {
        val = Value();
        return true;
    }
    SHARED_SIGNATURE(Nil, TName(T_NIL), false)
};

struct IdentRef : Node {
    SpecIdent *sid;
    IdentRef(const Line &ln, SpecIdent *_sid)
        : Node(ln), sid(_sid) {}
    bool IsConstInit() const { return sid->id->static_constant; }
    void Dump(ostringstream &ss) const { ss << sid->id->name; }
    SHARED_SIGNATURE(IdentRef, TName(T_IDENT), false)
};

struct IntConstant : Node {
    int64_t integer;
    IntConstant(const Line &ln, int64_t i) : Node(ln), integer(i) {}
    bool IsConstInit() const { return true; }
    void Dump(ostringstream &ss) const { ss << integer; }
    bool ConstVal(TypeChecker &, Value &val) const {
        val = Value(integer);  // FIXME: this clips.
        return true;
    }
    SHARED_SIGNATURE(IntConstant, TName(T_INT), false)
};

struct FloatConstant : Node {
    double flt;
    FloatConstant(const Line &ln, double f) : Node(ln), flt(f) {}
    bool IsConstInit() const { return true; }
    void Dump(ostringstream &ss) const { ss << flt; }
    bool ConstVal(TypeChecker &, Value &val) const {
        val = Value(flt);
        return true;
    }
    SHARED_SIGNATURE(FloatConstant, TName(T_FLOAT), false)
};

struct StringConstant : Node {
    string str;
    StringConstant(const Line &ln, string_view s) : Node(ln), str(s) {}
    bool IsConstInit() const { return true; }
    void Dump(ostringstream &ss) const { EscapeAndQuote(str, ss); }
    SHARED_SIGNATURE(StringConstant, TName(T_STR), false)
};

struct StructRef : Node {
    Struct *st;
    StructRef(const Line &ln, Struct *_st) : Node(ln), st(_st) {}
    void Dump(ostringstream &ss) const { ss << "struct " << st->name; }
    SHARED_SIGNATURE(StructRef, TName(T_STRUCT), false)
};

struct FunRef : Node {
    SubFunction *sf;
    FunRef(const Line &ln, SubFunction *_sf) : Node(ln), sf(_sf) {}
    bool IsConstInit() const { return true; }
    void Dump(ostringstream &ss) const {
        if (sf) ss << "(def " << sf->parent->name << ")"; else ss << "<>";
    }
    SHARED_SIGNATURE(FunRef, TName(T_FUN), false)
};

struct NativeRef : Node {
    NativeFun *nf;
    NativeRef(const Line &ln, NativeFun *_nf) : Node(ln), nf(_nf) {}
    void Dump(ostringstream &ss) const { ss << nf->name; }
    SHARED_SIGNATURE(NativeRef, "native function", true)
};

struct Constructor : List {
    TypeRef giventype;
    Constructor(const Line &ln, TypeRef _type) : List(ln), giventype(_type) {};
    bool IsConstInit() const {
        for (auto n : children) {
            if (!n->IsConstInit()) return false;
        }
        return true;
    }
    SHARED_SIGNATURE(Constructor, "constructor", false)
};

struct Call : List {
    SubFunction *sf;
    Call(const Line &ln, SubFunction *_sf) : List(ln), sf(_sf) {};
    void Dump(ostringstream &ss) const { ss << sf->parent->name; }
    SHARED_SIGNATURE(Call, "call", true)
};

struct DynCall : Call {
    SpecIdent *sid;
    DynCall(const Line &ln, SubFunction *_sf, SpecIdent *_sid)
        : Call(ln, _sf), sid(_sid) {};
    void Dump(ostringstream &ss) const { ss << sid->id->name; }
    SHARED_SIGNATURE(DynCall, "dynamic call", true)
};

struct NativeCall : List {
    NativeFun *nf;
    NativeCall(const Line &ln, NativeFun *_nf) : List(ln), nf(_nf) {};
    void Dump(ostringstream &ss) const { ss << nf->name; }
    SHARED_SIGNATURE(NativeCall, "native call", true)
};

struct Return : Unary {
    int subfunction_idx;
    Return(const Line &ln, Node *_a, int sfi) : Unary(ln, _a), subfunction_idx(sfi) {}
    SHARED_SIGNATURE(Return, TName(T_RETURN), true)
};

struct MultipleReturn : List {
    MultipleReturn(const Line &ln) : List(ln) {};
    SHARED_SIGNATURE(MultipleReturn, "multiple return", false)
};

struct AssignList : Unary {
    vector<SpecIdent *> sids;
    AssignList(const Line &ln, SpecIdent *sid, Node *_a)
        : Unary(ln, _a) { if (sid) sids.push_back(sid); }
    void Dump(ostringstream &ss) const {
        for (auto sid : sids) ss << sid->id->name << " ";
        ss << Name();
    }
    SHARED_SIGNATURE(AssignList, "assign list", true)
};

struct Define : AssignList {
    TypeRef giventype;
    Define(const Line &ln, SpecIdent *sid, Node *_a, TypeRef gt)
        : AssignList(ln, sid, _a), giventype(gt) {}
    SHARED_SIGNATURE(Define, TName(T_DEF), true)
};

struct Dot : Unary {
    SharedField *fld;
    bool maybe;
    Dot(const Line &ln, Node *_a, SharedField *_fld, bool _maybe)
        : Unary(ln, _a), fld(_fld), maybe(_maybe) {}
    void Dump(ostringstream &ss) const { ss << Name() << fld->name; }
    SHARED_SIGNATURE(Dot, TName(maybe ? T_DOTMAYBE : T_DOT), false)
};

struct IsType : Unary {
    TypeRef giventype;
    IsType(const Line &ln, Node *_a, TypeRef t) : Unary(ln, _a), giventype(t) {}
    void Dump(ostringstream &ss) const { ss << Name() << ":" << TypeName(giventype); }
    CONSTVALMETHOD
    SHARED_SIGNATURE(IsType, TName(T_IS), false)
};

inline string Dump(Node &n, int indent) {
    ostringstream ss;
    n.Dump(ss);
    string s = ss.str();
    auto arity = n.Arity();
    if (!arity) return s;
    bool ml = false;
    auto ch = n.Children();
    vector<string> sv;
    size_t total = 0;
    for (size_t i = 0; i < arity; i++) {
        auto a = Dump(*ch[i], indent + 2);
        a += ":";
        a += TypeName(ch[i]->exptype);
        if (a[0] == ' ') ml = true;
        total += a.length();
        sv.push_back(a);
    }
    if (total > 60) ml = true;
    if (ml) {
        s = string(indent, ' ') + "(" + s;
        s += "\n";
        for (size_t i = 0; i < arity; i++) {
            if (i) s += "\n";
            if (sv[i][0] != ' ') s += string(indent + 2, ' ');
            s += sv[i];
        }
        return s + ")";
    } else {
        for (size_t i = 0; i < arity; i++) s += " " + sv[i];
        return "(" + s + ")";
    }
}

}  // namespace lobster

#endif  // LOBSTER_NODE
