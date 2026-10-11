/*
    A structure describing an operation.
*/
struct Operation {
    virtual ~Operation() = default;
    const char *args {};

    virtual unique_ptr<Cell> run() const { return nullptr; }
    virtual double runn(double a) const { return 0; }
    virtual unique_ptr<Cell> runl(const shared_ptr<Grid> &l) const { return nullptr; }
    virtual void rung(const shared_ptr<Grid> &g) const {}
    virtual unique_ptr<Cell> runc(unique_ptr<Cell> c) const { return nullptr; }
    virtual double runnn(double a, double b) const { return 0; }
};

using OperationMap = std::unordered_map<wxString, unique_ptr<Operation>>;
using VariableMap = std::unordered_map<wxString, unique_ptr<Cell>>;

/*
    Provides running evaluation of a grid.
*/
struct Evaluator {
    OperationMap ops;
    VariableMap vars;
    bool vert {false};

    ~Evaluator() = default;

    void ClearVars() {
        vars.clear();
    }

    #define OP(_n, _c, _args, _f)           \
        {                                   \
            struct _op : Operation {        \
                _op() { args = _args; };    \
                _f const override { return _c; }; \
            };                              \
            ops[L## #_n] = make_unique<_op>();       \
        }

    #define OPNN(_n, _c) OP(_n, _c, "nn", double runnn(double a, double b))
    #define OPN(_n, _c) OP(_n, _c, "n", double runn(double a))
    #define OPT(_n, _c) OP(_n, _c, "t", unique_ptr<Cell> runc(unique_ptr<Cell> c))
    #define OPC(_n, _c) OP(_n, _c, "c", unique_ptr<Cell> runc(unique_ptr<Cell> c))
    #define OPL(_n, _c) OP(_n, _c, "l", unique_ptr<Cell> runl(const shared_ptr<Grid> &a))
    #define OPG(_n, _c) OP(_n, _c, "g", void rung(const shared_ptr<Grid> &a))

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
        OPL(min, a->Aggregate(aggregate::Min))
        OPL(max, a->Aggregate(aggregate::Max))
        OPL(avg, a->Aggregate(aggregate::Avg))
        OPL(median, a->Aggregate(aggregate::Median))
        OPG(transpose, a->Transpose())
        struct _if : Operation {
            _if() { args = "nLL"; };
        };
        ops[L"if"] = make_unique<_if>();
    }

    int InferCellType(Text &t) { return ops.contains(t.t) ? CT_CODE : CT_DATA; }

    unique_ptr<Cell> Lookup(const wxString &name) {
        auto it = vars.find(name);
        return (it != vars.end()) ? it->second->Clone(nullptr) : nullptr;
    }

    static bool IsValidSymbol(wxString const &symbol) { return !symbol.IsEmpty(); }
    void SetSymbol(wxString const &symbol, unique_ptr<Cell> val) {
        if (!treesheets::Evaluator::IsValidSymbol(symbol)) { return; }
        vars[symbol] = std::move(val);
    }

    void Assign(const Cell *sym, const Cell *val) {
        this->SetSymbol(sym->text.t, val->Clone(nullptr));
        if (sym->grid && val->grid) { this->DestructuringAssign(sym->grid, val->Clone(nullptr)); }
    }

    void DestructuringAssign(const shared_ptr<Grid> &names, unique_ptr<Cell> val) {
        Grid const *ng = names.get();
        Grid const *vg = val->grid.get();
        if (ng->xs == vg->xs && ng->ys == vg->ys) {
            loop(x, ng->xs) loop(y, ng->ys) {
                this->SetSymbol(ng->C(x, y)->text.t, vg->C(x, y)->Clone(nullptr));
            }
        }
    }

    Operation *FindOp(wxString &name) {
        auto it = ops.find(name);
        return it != ops.end() ? it->second.get() : nullptr;
    }

    static unique_ptr<Cell> Execute(const Operation *op) { return op->run(); }

    unique_ptr<Cell> Execute(const Operation *op, unique_ptr<Cell> left) {
        Text &t = left->text;
        shared_ptr<Grid> g = left->grid;
        switch (op->args[0]) {
            case 'n':
                if (!t.t.IsEmpty()) {
                    t.SetNum(op->runn(t.GetNum()));
                    return left;
                } else if (g) {
                    foreachcellingrid(c, g) {
                        unique_ptr<Cell> temp = std::move(c);
                        c = Execute(op, std::move(temp));
                        c->SetParent(left.get());
                    }
                }
                break;
            case 't':
                if (!t.t.IsEmpty()) {
                    return op->runc(std::move(left));
                } else if (g) {
                    foreachcellingrid(c, g) {
                        unique_ptr<Cell> temp = std::move(c);
                        c = Execute(op, std::move(temp));
                        c->SetParent(left.get());
                    }
                }
                break;
            case 'l':
                if (g) {
                    if (g->xs == 1 || g->ys == 1) {
                        return op->runl(g);
                    } else {
                        vector<shared_ptr<Grid>> gs;
                        g->Split(gs, vert);
                        g = make_shared<Grid>(vert ? gs.size() : 1, vert ? 1 : gs.size());
                        auto c = make_unique<Cell>(nullptr, left.get(), CT_DATA, g);
                        loopv(i, gs) {
                            auto res = op->runl(gs[i]);
                            res->SetParent(c.get());
                            g->C(vert ? i : 0, vert ? 0 : i) = std::move(res);
                        }
                        return c;
                    }
                }
                break;
            case 'g':
                if (g) { op->rung(g); }
                break;
            case 'c': return op->runc(std::move(left));
        }
        return left;
    }

    unique_ptr<Cell> Execute(const Operation *op, unique_ptr<Cell> left, const Cell *_right) {
        auto right = _right->Eval(*this);
        if (!right) { return left; }
        Text &t1 = left->text;
        Text &t2 = right->text;
        shared_ptr<Grid> g1 = left->grid;
        shared_ptr<Grid> g2 = right->grid;
        switch (op->args[0]) {
            case 'n':
                if (!t1.t.IsEmpty() && !t2.t.IsEmpty()) {
                    t1.SetNum(op->runnn(t1.GetNum(), t2.GetNum()));
                } else if (g1 && g2 && g1->xs == g2->xs && g1->ys == g2->ys) {
                    auto g = make_shared<Grid>(g1->xs, g1->ys);
                    auto c = make_unique<Cell>(nullptr, left.get(), CT_DATA, g);
                    loop(x, g->xs) loop(y, g->ys) {
                        unique_ptr<Cell> c1 = std::move(g1->C(x, y));
                        Cell *c2 = g2->C(x, y).get();
                        auto res = Execute(op, std::move(c1), c2);
                        res->SetParent(c.get());
                        g->C(x, y) = std::move(res);
                    }
                    return c;
                } else if (g1 && !t2.t.IsEmpty()) {
                    foreachcellingrid(c, g1) {
                        unique_ptr<Cell> res = Execute(op, std::move(c), right.get());
                        res->SetParent(left.get());
                        c = std::move(res);
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
        if (l.t.IsEmpty()) { return left; }
        bool cond = l.GetNum() != 0;
        return (cond ? a : b)->Eval(*this);
    }

    void Eval(const Cell *root) {
        root->Eval(*this);
        ClearVars();
    }
};

/*
    Calculates the results of formula cells (CT_FORMULA), which become their text. A formula is
    arithmetic (+ - * / and parentheses) over numbers and SUM, MIN, MAX, AVG, MEDIAN and COUNT of
    a grid, which is named by the text of its cell, e.g. =SUM(Income) - SUM(Expenses). The
    results of other formulas in that grid count as numbers, and their errors carry over. With
    RECURSIVE in front, e.g. RECURSIVESUM, they also count the data in all its sub-grids, but not
    the formulas there, which would count subtotals twice.
*/
struct FormulaEvaluator {
    enum { BUSY, OK, FAILED };
    const Cell *editing;  // whose text is the formula being typed, so it has no result yet
    std::unordered_map<const Cell *, int> state;
    vector<Cell *> changed;

    explicit FormulaEvaluator(const Cell *_editing) : editing(_editing) {}

    // Brings the result of formula cell c up to date, after those it depends on. Returns the
    // error, if any.
    wxString Update(Cell *c) {
        if (c == editing || !c->formula) { return {}; }
        if (auto it = state.find(c); it != state.end()) {
            if (it->second == BUSY) { return L"#CYCLE!"; }
            return it->second == FAILED ? c->text.t : wxString();
        }
        state[c] = BUSY;
        Parser p {*this, c, *c->formula};
        auto d = p.Expr();
        p.Space();
        if (p.i < p.s.Len()) { p.Fail(L"#ERROR!"); }
        if (!std::isfinite(d)) { p.Fail(L"#NUM!"); }
        state[c] = p.error.IsEmpty() ? OK : FAILED;
        auto result = p.error.IsEmpty() ? Text::NumText(d) : p.error;
        if (result != c->text.t) {
            c->text.SetText(result);
            changed.push_back(c);
        }
        return p.error;
    }

    using Aggregate = double (*)(const vector<double> &);
    static const auto &Functions() {
        static const std::pair<const wxChar *, Aggregate> functions[] = {
            {L"SUM", aggregate::Sum},
            {L"MIN", aggregate::Min},
            {L"MAX", aggregate::Max},
            {L"AVG", aggregate::Avg},
            {L"AVERAGE", aggregate::Avg},
            {L"MEDIAN", [](const vector<double> &v) { return aggregate::Median(v); }},
            {L"COUNT", [](const vector<double> &v) { return static_cast<double>(v.size()); }}};
        return functions;
    }

    // Calls f on the cells around `self` in the order names are looked up in: breadth first in
    // the grid that `self` is in, then in each grid around that one, until f returns true.
    template<typename F> static void ForNearest(const Cell *self, F f) {
        for (auto *scope = self->parent; scope != nullptr; scope = scope->parent) {
            vector<Cell *> level {scope};
            vector<Cell *> next;
            while (!level.empty()) {
                for (auto *c : level) {
                    if (!c->grid) { continue; }
                    if (f(c)) { return; }
                    for (auto &child : c->grid->cells) { next.push_back(child.get()); }
                }
                level.swap(next);
                next.clear();
            }
        }
    }

    // The grid of the cell whose text is `name`, the nearest one to `self` (see ForNearest()).
    static Grid *FindGrid(const Cell *self, const wxString &name) {
        Grid *g = nullptr;
        ForNearest(self, [&](Cell *c) {
            if (c->text.t == name) { g = c->grid.get(); }
            return g != nullptr;
        });
        return g;
    }

    // The names of grids that a formula in `self` can use, the nearest first.
    static vector<wxString> Names(const Cell *self) {
        vector<wxString> names;
        std::set<wxString> seen;
        ForNearest(self, [&](Cell *c) {
            auto &n = c->text.t;
            if (!n.IsEmpty() && n.find(')') == wxString::npos && seen.insert(n).second) {
                names.push_back(n);
            }
            return names.size() >= 100;
        });
        return names;
    }

    // What formula s may continue with: a function after "=", an operator or "(", and one of
    // `names` in the parentheses of a function.
    static void Completions(const vector<wxString> &names, const wxString &s, wxArrayString &res) {
        if (!s.StartsWith("=")) { return; }
        auto open = s.rfind('(');
        auto close = s.rfind(')');
        if (open != wxString::npos && (close == wxString::npos || close < open) && open > 0 &&
            wxIsalpha(s[open - 1])) {
            auto start = open + 1;
            while (start < s.Len() && wxIsspace(s[start])) { start++; }
            // Names must match exactly, also where Windows appends the rest to what was typed.
            auto typed = s.Mid(start);
            for (auto &n : names) {
                if (n.StartsWith(typed)) { res.Add(s.Left(start) + n + ")"); }
            }
            return;
        }
        auto start = s.Len();
        while (start > 1 && wxIsalpha(s[start - 1])) { start--; }
        auto before = start;
        while (before > 1 && wxIsspace(s[before - 1])) { before--; }
        if (wxString("=+-*/(").find(s[before - 1]) == wxString::npos) { return; }
        auto typed = s.Mid(start).Upper();
        for (auto recursive : {L"", L"RECURSIVE"}) {
            for (auto &[n, f] : Functions()) {
                auto function = recursive + wxString(n);
                if (function.StartsWith(typed)) { res.Add(s.Left(start) + function + "("); }
            }
        }
    }

    // The numbers in the grid named `name` (see FindGrid()), except formula cell `self`, and with
    // `recursive` also those of the data cells in its sub-grids, depth first.
    wxString Numbers(Cell *self, const wxString &name, vector<double> &numbers, bool recursive) {
        auto *g = FindGrid(self, name);
        if (g == nullptr) { return L"#NAME?"; }
        return Numbers(self, g, numbers, recursive);
    }

    wxString Numbers(Cell *self, Grid *g, vector<double> &numbers, bool recursive,
                     bool sub = false) {
        for (auto &c : g->cells) {
            if (c.get() != self && !(sub && c->celltype == CT_FORMULA)) {
                if (c->celltype == CT_FORMULA) {
                    if (auto error = Update(c.get()); !error.IsEmpty()) { return error; }
                }
                double d;
                if (c->text.GetStrictNum(d)) { numbers.push_back(d); }
            }
            if (recursive && c->grid) {
                if (auto error = Numbers(self, c->grid.get(), numbers, true, true);
                    !error.IsEmpty()) {
                    return error;
                }
            }
        }
        return {};
    }

    struct Parser {
        FormulaEvaluator &fe;
        Cell *self;
        const wxString &s;
        size_t i {1};  // after the "="
        wxString error;

        double Fail(const wxString &e) {
            if (error.IsEmpty()) { error = e; }
            return 0;
        }
        void Space() {
            while (i < s.Len() && wxIsspace(s[i])) { i++; }
        }
        bool Eat(wxChar ch) {
            Space();
            if (i >= s.Len() || s[i] != ch) { return false; }
            i++;
            return true;
        }
        double Expr() {
            auto v = Term();
            for (;;) {
                if (Eat('+')) {
                    v += Term();
                } else if (Eat('-')) {
                    v -= Term();
                } else {
                    return v;
                }
            }
        }
        double Term() {
            auto v = Unary();
            for (;;) {
                if (Eat('*')) {
                    v *= Unary();
                } else if (Eat('/')) {
                    auto d = Unary();
                    v = d != 0 ? v / d : Fail(L"#DIV/0!");
                } else {
                    return v;
                }
            }
        }
        double Unary() { return Eat('-') ? -Unary() : Eat('+') ? Unary() : Primary(); }
        double Primary() {
            if (Eat('(')) {
                auto v = Expr();
                return Eat(')') ? v : Fail(L"#ERROR!");
            }
            auto start = i;
            if (i < s.Len() && (wxIsdigit(s[i]) || s[i] == '.')) {
                while (i < s.Len() && (wxIsdigit(s[i]) || s[i] == '.')) { i++; }
                if (i < s.Len() && (s[i] == 'e' || s[i] == 'E')) {
                    i++;
                    if (i < s.Len() && (s[i] == '+' || s[i] == '-')) { i++; }
                    while (i < s.Len() && wxIsdigit(s[i])) { i++; }
                }
                double d;
                return s.Mid(start, i - start).ToCDouble(&d) ? d : Fail(L"#ERROR!");
            }
            while (i < s.Len() && wxIsalpha(s[i])) { i++; }
            auto function = s.Mid(start, i - start).Upper();
            auto close = s.find(')', i);
            if (function.IsEmpty() || !Eat('(') || close == wxString::npos) {
                return Fail(L"#ERROR!");
            }
            auto name = s.Mid(i, close - i).Trim().Trim(false);
            i = close + 1;
            wxString rest;
            auto recursive = function.StartsWith(L"RECURSIVE", &rest);
            if (recursive) { function = rest; }
            for (auto &[n, f] : Functions()) {
                if (function != n) { continue; }
                vector<double> numbers;
                auto e = fe.Numbers(self, name, numbers, recursive);
                return e.IsEmpty() ? f(numbers) : Fail(e);
            }
            return Fail(L"#NAME?");
        }
    };
};
