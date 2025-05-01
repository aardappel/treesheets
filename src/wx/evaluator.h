
/*
    A structure describing an operation.
*/
struct Operation {
    virtual ~Operation() {};
    const char *args;

    virtual unique_ptr<Cell> run() const { return nullptr; }
    virtual double runn(double a) const { return 0; }
    virtual unique_ptr<Cell> runl(Grid *l) const { return nullptr; }
    virtual void rung(Grid *g) const {}
    virtual unique_ptr<Cell> runc(unique_ptr<Cell> c) const { return nullptr; }
    virtual double runnn(double a, double b) const { return 0; }
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
        WX_CLEAR_HASH_MAP(wxHashMapOperation, ops);
        ClearVars();
    }

    void ClearVars() {
        WX_CLEAR_HASH_MAP(wxHashMapCell, vars);
    }

    #define OP(_n, _c, _args, _f)           \
        {                                   \
            struct _op : Operation {        \
                _op() { args = _args; };    \
                _f const override { return _c; }; \
            };                              \
            ops[L## #_n] = new _op();       \
        }

    #define OPNN(_n, _c) OP(_n, _c, "nn", double runnn(double a, double b))
    #define OPN(_n, _c) OP(_n, _c, "n", double runn(double a))
    #define OPT(_n, _c) OP(_n, _c, "t", unique_ptr<Cell> runc(unique_ptr<Cell> c))
    #define OPC(_n, _c) OP(_n, _c, "c", unique_ptr<Cell> runc(unique_ptr<Cell> c))
    #define OPL(_n, _c) OP(_n, _c, "l", unique_ptr<Cell> runl(Grid *a))
    #define OPG(_n, _c) OP(_n, _c, "g", void rung(Grid *a))

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
        OPT(graph, (c->Graph(), std::move(c)));
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

    unique_ptr<Cell> Lookup(const wxString &name) {
        wxHashMapCell::iterator lookup = vars.find(name);
        return (lookup != vars.end()) ? lookup->second->Clone(nullptr) : nullptr;
    }

    bool IsValidSymbol(wxString const &symbol) const { return !symbol.IsEmpty(); }
    void SetSymbol(wxString const &symbol, unique_ptr<Cell> val) {
        if (!this->IsValidSymbol(symbol)) { return; }
        Cell *old = vars[symbol];
        DELETEP(old);
        vars[symbol] = val.release();
    }

    void Assign(const Cell *sym, const Cell *val) {
        this->SetSymbol(sym->text.t, val->Clone(nullptr));
        if (sym->grid && val->grid) this->DestructuringAssign(sym->grid, val->Clone(nullptr));
    }

    void DestructuringAssign(Grid const *names, unique_ptr<Cell> val) {
        Grid const *ng = names;
        Grid const *vg = val->grid;
        if (ng->xs == vg->xs && ng->ys == vg->ys) {
            loop(x, ng->xs) loop(y, ng->ys) {
                Cell *nc = ng->C(x, y);
                Cell *vc = vg->C(x, y);
                this->SetSymbol(nc->text.t, vc->Clone(nullptr));
            }
        }
    }

    Operation *FindOp(wxString &name) { return ops[name]; }

    unique_ptr<Cell> Execute(const Operation *op) { return op->run(); }

    unique_ptr<Cell> Execute(const Operation *op, unique_ptr<Cell> left) {
        Text &t = left->text;
        Grid *g = left->grid;
        switch (op->args[0]) {
            case 'n':
                if (t.t.Len()) {
                    t.SetNum(op->runn(t.GetNum()));
                    return left;
                } else if (g) {
                    foreachcellingrid(c, g) c =
                        Execute(op, unique_ptr<Cell>(c)).release()->SetParent(left.get());
                }
                break;
            case 't':
                if (t.t.Len()) {
                    return op->runc(std::move(left));
                } else if (g) {
                    foreachcellingrid(c, g) c =
                        Execute(op, unique_ptr<Cell>(c)).release()->SetParent(left.get());
                }
                break;
            case 'l':
                if (g) {
                    if (g->xs == 1 || g->ys == 1) {
                        return op->runl(g);
                    } else {
                        vector<unique_ptr<Grid>> gs;
                        g->Split(gs, vert);
                        g = new Grid(vert ? gs.size() : 1, vert ? 1 : gs.size());
                        auto c = make_unique<Cell>(nullptr, left.get(), CT_DATA, g);
                        loopv(i, gs) {
                            auto v = op->runl(gs[i].get()).release();
                            g->C(vert ? i : 0, vert ? 0 : i) = v->SetParent(c.get());
                        }
                        return c;
                    }
                }
                break;
            case 'g':
                if (g) op->rung(g);
                break;
            case 'c': return op->runc(std::move(left));
        }
        return left;
    }

    unique_ptr<Cell> Execute(const Operation *op, unique_ptr<Cell> left, const Cell *_right) {
        auto right = _right->Eval(*this);
        if (!right) return left;
        Text &t1 = left->text;
        Text &t2 = right->text;
        Grid *g1 = left->grid;
        Grid *g2 = right->grid;
        switch (op->args[0]) {
            case 'n':
                if (t1.t.Len() && t2.t.Len()) {
                    t1.SetNum(op->runnn(t1.GetNum(), t2.GetNum()));
                } else if (g1 && g2 && g1->xs == g2->xs && g1->ys == g2->ys) {
                    Grid *g = new Grid(g1->xs, g1->ys);
                    auto c = make_unique<Cell>(nullptr, left.get(), CT_DATA, g);
                    loop(x, g->xs) loop(y, g->ys) {
                        Cell *&c1 = g1->C(x, y);
                        Cell *&c2 = g2->C(x, y);
                        g->C(x, y) =
                            Execute(op, unique_ptr<Cell>(c1), c2).release()->SetParent(c.get());
                        c1 = nullptr;
                    }
                    return c;
                } else if (g1 && t2.t.Len()) {
                    foreachcellingrid(c, g1) {
                        c = Execute(op, unique_ptr<Cell>(c), right.get())
                                .release()
                                ->SetParent(left.get());
                    }
                }
                break;
        }
        return left;
    }

    // IF is sofar the only ternary
    unique_ptr<Cell> Execute(const Operation *op, unique_ptr<Cell> left, const Cell *a,
                             const Cell *b) {
        Text &l = left->text;
        if (!l.t.Len()) return left;
        bool cond = l.GetNum() != 0;
        return (cond ? a : b)->Eval(*this);
    }

    void Eval(const Cell *root) {
        root->Eval(*this);
        ClearVars();
    }
};
