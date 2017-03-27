
/*
    A structure describing an operation.
*/
struct Operation {
    const char *args;

    virtual Cell *run() { return nullptr; }
    virtual double runn(double a) { return 0; }
    virtual Cell *runt(Text *t) { return nullptr; }
    virtual Cell *runl(Grid *l) { return nullptr; }
    virtual Cell *rung(Grid *g) { return nullptr; }
    virtual Cell *runc(Cell *c) { return nullptr; }
    virtual double runnn(double a, double b) { return 0; }
};

WX_DECLARE_STRING_HASH_MAP(Operation *, wxHashMapOperation);
WX_DECLARE_STRING_HASH_MAP(Cell *, wxHashMapCell);

/*
    Provides running evaluation of a grid.
*/
struct Evaluator {
    wxHashMapOperation ops;
    wxHashMapCell vars;
    bool vert;

    ~Evaluator() {
        while (ops.size()) {
            delete ops.begin()->second;
            ops.erase(ops.begin());
        }
        while (vars.size()) {
            delete vars.begin()->second;
            vars.erase(vars.begin());
        }
    }

    #define OP(_n, _c, _args, _f)        \
        {                                \
            struct _op : Operation {     \
                _op() { args = _args; }; \
                _f { return _c; };       \
            };                           \
            ops[L## #_n] = new _op();    \
        }

    #define OPNN(_n, _c) OP(_n, _c, "nn", double runnn(double a, double b))
    #define OPN(_n, _c) OP(_n, _c, "n", double runn(double a))
    #define OPT(_n, _c) OP(_n, _c, "t", Cell *runt(Text *t))
    #define OPL(_n, _c) OP(_n, _c, "l", Cell *runl(Grid *a))
    #define OPG(_n, _c) OP(_n, _c, "g", Cell *rung(Grid *a))

    void Init() {
        OPNN(+, a + b);
        OPNN(-, a - b);
        OPNN(*, a * b);
        OPNN(/, b != 0 ? a / b : 0);
        OPNN(<, double(a < b));
        OPNN(>, double(a > b));
        OPNN(<=, double(a <= b));
        OPNN(>=, double(a >= b));
        OPNN(=, double(a == b));
        OPNN(==, double(a == b));
        OPNN(!=, double(a != b));
        OPNN(<>, double(a != b));
        OPN(inc, a + 1);
        OPN(dec, a - 1);
        OPN(neg, -a);
        OPT(graph, t->Graph());
        OPL(sum, a->Sum())
        OPG(transpose, a->Transpose())
        struct _if : Operation {
            _if() { args = "nLL"; };
        };
        ops[L"if"] = new _if();
    }

    int InferCellType(Text &t) {
        if (ops[t.t])
            return CT_CODE;
        else
            return CT_DATA;
    }

    Cell *Lookup(wxString &name) {
        wxHashMapCell::iterator lookup = vars.find(name);
        return (lookup != vars.end()) ? lookup->second->Clone(nullptr) : nullptr;
    }

    bool IsValidSymbol(wxString const &symbol) const { return !symbol.IsEmpty(); }
    void SetSymbol(wxString const &symbol, Cell *val) {
        if (!this->IsValidSymbol(symbol)) {
            DELETEP(val);
            return;
        }
        Cell *old = vars[symbol];
        DELETEP(old);
        vars[symbol] = val;
    }

    void Assign(Cell const *sym, Cell const *val) {
        this->SetSymbol(sym->text.t, val->Clone(nullptr));
        if (sym->grid && val->grid) this->DestructuringAssign(sym->grid, val->Clone(nullptr));
    }

    void DestructuringAssign(Grid const *names, Cell *val) {
        Grid const *ng = names;
        Grid const *vg = val->grid;
        if (ng->xs == vg->xs && ng->ys == vg->ys) {
            loop(x, ng->xs) loop(y, ng->ys) {
                Cell *nc = ng->C(x, y);
                Cell *vc = vg->C(x, y);
                this->SetSymbol(nc->text.t, vc->Clone(nullptr));
            }
        }
        DELETEP(val);
    }

    Operation *FindOp(wxString &name) { return ops[name]; }

    Cell *Execute(Operation *op) { return op->run(); }

    Cell *Execute(Operation *op, Cell *left) {
        Text &t = left->text;
        Grid *g = left->grid;
        switch (op->args[0]) {
            case 'n':
                if (t.t.Len())
                    return t.SetNum(op->runn(t.GetNum()));
                else if (g)
                    foreachcellingrid(c, g) c = Execute(op, c)->SetParent(left);
                break;
            case 't':
                if (t.t.Len())
                    return op->runt(&t);
                else if (g)
                    foreachcellingrid(c, g) c = Execute(op, c)->SetParent(left);
                break;
            case 'l':
                if (g) {
                    if (g->xs == 1 || g->ys == 1) return op->runl(g);
                    Vector<Grid *> gs;
                    g->Split(gs, vert);
                    g = new Grid(vert ? gs.size() : 1, vert ? 1 : gs.size());
                    Cell *c = new Cell(nullptr, left, CT_DATA, g);
                    loopv(i, gs) g->C(vert ? i : 0, vert ? 0 : i) = op->runl(gs[i])->SetParent(c);
                    gs.setsize_nd(0);
                    return c;
                }
                break;
            case 'g':
                if (g) return op->rung(g);
                break;
            case 'c': return op->runc(left);
        }
        return left;
    }

    Cell *Execute(Operation *op, Cell *left, Cell *right) {
        if (!(right = right->Eval(*this))) return left;
        Text &t1 = left->text;
        Text &t2 = right->text;
        Grid *g1 = left->grid;
        Grid *g2 = right->grid;
        switch (op->args[0]) {
            case 'n':
                if (t1.t.Len() && t2.t.Len())
                    t1.SetNum(op->runnn(t1.GetNum(), t2.GetNum()));
                else if (g1 && g2 && g1->xs == g2->xs && g1->ys == g2->ys) {
                    Grid *g = new Grid(g1->xs, g1->ys);
                    Cell *c = new Cell(nullptr, left, CT_DATA, g);
                    loop(x, g->xs) loop(y, g->ys) {
                        Cell *&c1 = g1->C(x, y);
                        Cell *&c2 = g2->C(x, y);
                        g->C(x, y) = Execute(op, c1, c2)->SetParent(c);
                        c1 = c2 = nullptr;
                    }
                    delete g1;
                    delete g2;
                    return c;
                } else if (g1 && t2.t.Len()) {
                    foreachcellingrid(c, g1) c =
                        Execute(op, c, right->Clone(nullptr))->SetParent(left);
                }
                break;
        }
        delete right;
        return left;
    }

    Cell *Execute(Operation *op, Cell *left, Cell *a, Cell *b)  // IF is sofar the only ternary
    {
        Text &l = left->text;
        if (!l.t.Len()) return left;
        bool cond = l.GetNum() != 0;
        delete left;
        return (cond ? a : b)->Eval(*this);
    }

    void Eval(Cell *root) {
        Cell *c = root->Eval(*this);
        DELETEP(c);
    }
};
