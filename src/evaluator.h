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
    // The first thing that went wrong in an evaluation, see Fail().
    enum { FAIL_NONE, FAIL_UNKNOWNOP, FAIL_NOINPUT, FAIL_NOARGUMENT, FAIL_NOVARIABLE };
    int failure {FAIL_NONE};
    wxString failedname;
    const Cell *failedcell {nullptr};  // only if it is in the document
    const Cell *evalroot {nullptr};

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

    // Notes the first failure, with the operation or variable cell c it happened at. Cells
    // outside the document are copies made during the evaluation, which can't be selected.
    nullptr_t Fail(int what, const Cell *c) {
        if (failure == FAIL_NONE) {
            failure = what;
            failedname = c->text.t;
            auto *top = c;
            while (top->parent != nullptr) { top = top->parent; }
            failedcell = top == evalroot ? c : nullptr;
        }
        return nullptr;
    }

    // Returns the variables it assigned.
    VariableMap Eval(const Cell *root) {
        failure = FAIL_NONE;
        failedcell = nullptr;
        evalroot = root;
        root->Eval(*this);
        return std::exchange(vars, {});
    }
};

/*
    Calculates the results of formula cells (CT_FORMULA), which become their text. A formula is
    arithmetic (+ - * / and parentheses) over numbers and SUM, MIN, MAX, AVG, MEDIAN and COUNT of
    a grid, which is named by the text of its cell, e.g. =SUM(Income) - SUM(Expenses). A name on
    its own, e.g. =Unit price * Quantity, is the one number in its grid (#VALUE! if there is not
    exactly one). Names may be in quotes, e.g. =SUM("Sales, 2025"), which allows any characters
    in them but quotes. The results of other formulas in that grid count as numbers, and their
    errors carry over. With TREE in front, e.g. TREESUM, they also count the data in all its
    sub-grids, but not the formulas there, which would count subtotals twice.
    SUBSET(name, columns, rows) is part of a grid, e.g. =MAX(SUBSET("Sales 2025", 0, 0-2)), with
    columns and rows counted from 0 at the top left, each one number, an inclusive range, a range
    open at one end (3- or -4) or - for all of them. On its own, it is its one number, like a name.
    TREESUBSET(name, columns, rows, levels) also takes the same columns and rows of the sub-grids
    of those cells, and so on, but counts only the levels in the range, 0 being the named grid,
    e.g. =SUM(TREESUBSET(Projects, 1, -, 1-)). It skips the formulas in sub-grids like TREE.
    Instead of a name, both take another SUBSET() or TREESUBSET(), whose cells' grids they use,
    e.g. =SUM(SUBSET(SUBSET(Projects, 1, 0), 2-, -)) for columns 2 on in the grid of cell (1, 0).
    A name that is no grid can be a variable that the last run of the program assigned (see
    Evaluator::Assign()), with the numbers of its grid, or its text. Until the program runs, its
    variables are #N/A.
*/
struct FormulaEvaluator {
    enum { BUSY, OK, FAILED };
    const Cell *editing;  // whose text is the formula being typed, so it has no result yet
    const VariableMap &vars;
    std::optional<std::set<wxString>> assigned;  // see Numbers()
    std::unordered_map<const Cell *, int> state;
    vector<Cell *> changed;

    FormulaEvaluator(const Cell *_editing, const VariableMap &_vars)
        : editing(_editing), vars(_vars) {}

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

    // Calls f on the cells with a grid around `self` (with `all` on all cells) in the order names
    // are looked up in: breadth first in the grid that `self` is in, then in each grid around
    // that one, until f returns true.
    template<typename F> static void ForNearest(const Cell *self, F f, bool all = false) {
        for (auto *scope = self->parent; scope != nullptr; scope = scope->parent) {
            vector<Cell *> level {scope};
            vector<Cell *> next;
            while (!level.empty()) {
                for (auto *c : level) {
                    if (!c->grid && !all) { continue; }
                    if (f(c)) { return; }
                    if (!c->grid) { continue; }
                    for (auto &child : c->grid->cells) { next.push_back(child.get()); }
                }
                level.swap(next);
                next.clear();
            }
        }
    }

    // The cell named `name`: the one with a grid nearest to `self` (see ForNearest()), but not a
    // variable assignment, whose grid holds the names it assigns to, or else variable `name`.
    // Without it, error says why.
    Cell *Named(const Cell *self, const wxString &name, wxString &error) {
        Cell *named = nullptr;
        ForNearest(self, [&](Cell *c) {
            if (c->text.t == name && c->celltype != CT_VARD) { named = c; }
            return named != nullptr;
        });
        if (named != nullptr) { return named; }
        if (auto it = vars.find(name); it != vars.end()) { return it->second.get(); }
        if (!assigned) { assigned = Assigned(self); }
        error = assigned->contains(name) ? L"#N/A" : L"#NAME?";
        return nullptr;
    }

    // The cell that an internal link in `self` to `name` goes to: the nearest other one with that
    // text, looked up like the names in formulas (see ForNearest()).
    static Cell *LinkTarget(const Cell *self, const wxString &name) {
        Cell *target = nullptr;
        auto named = [&](Cell *c) {
            if (c != self && c->parent != nullptr && c->text.t == name) { target = c; }
            return target != nullptr;
        };
        ForNearest(self, named, true);
        return target;
    }

    static bool IsNameChar(wxChar c) { return wxIsalnum(c) || c == '_'; }
    static bool IsSubset(const wxString &function) {
        return function == L"SUBSET" || function == L"TREESUBSET";
    }

    // Whether n can be used on its own in a formula: words of letters, digits and _, separated by
    // spaces, e.g. =Unit price * 2.
    static bool IsBareName(const wxString &n) {
        return !n.IsEmpty() && !wxIsdigit(n[0]) && IsNameChar(n[0]) && IsNameChar(n.Last()) &&
               std::all_of(n.begin(), n.end(), [](wxChar c) { return IsNameChar(c) || c == ' '; });
    }

    // The names of grids that a formula in `self` can use, the nearest first, then those of
    // variables.
    static vector<wxString> Names(const Cell *self) {
        vector<wxString> names;
        std::set<wxString> seen;
        auto add = [&](const wxString &n) {
            if (!n.IsEmpty() && n.find('"') == wxString::npos && seen.insert(n).second) {
                names.push_back(n);
            }
            return names.size() >= 100;
        };
        ForNearest(self, [&](Cell *c) { return c->celltype != CT_VARD && add(c->text.t); });
        for (auto &n : Assigned(self)) {
            if (add(n)) { break; }
        }
        return names;
    }

    // The names that the variable assignments in the document of `self` assign to, also those in
    // their grids (see Evaluator::Assign()).
    static std::set<wxString> Assigned(const Cell *self) {
        std::set<wxString> names;
        auto add = [&](auto &add, const Cell *c) -> void {
            if (c->celltype == CT_VARD) {
                names.insert(c->text.t);
                if (c->grid) {
                    for (auto &n : c->grid->cells) { names.insert(n->text.t); }
                }
            }
            if (c->grid) {
                for (auto &child : c->grid->cells) { add(add, child.get()); }
            }
        };
        while (self->parent != nullptr) { self = self->parent; }
        add(add, self);
        names.erase(wxString());
        return names;
    }

    // What formula s may continue with: a function or one of `names` after "=", an operator or
    // "(", and any of `names` in the parentheses of a function.
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
            auto function = open;
            while (function > 0 && wxIsalpha(s[function - 1])) { function--; }
            bool subset = IsSubset(s.Mid(function, open - function).Upper());
            for (auto f : {L"SUBSET(", L"TREESUBSET("}) {
                if (wxString(f).StartsWith(typed.Upper())) { res.Add(s.Left(start) + f); }
            }
            // Names with a ) in them, or a , in SUBSET(), only in quotes.
            wxString quote = typed.StartsWith("\"") ? "\"" : "";
            for (auto &n : names) {
                if (n.StartsWith(typed.Mid(quote.Len())) &&
                    (!quote.IsEmpty() || n.find(subset ? ',' : ')') == wxString::npos)) {
                    res.Add(s.Left(start) + quote + n + quote + (subset ? ", " : ")"));
                }
            }
            return;
        }
        auto start = s.Len();
        while (start > 1 && (IsNameChar(s[start - 1]) || s[start - 1] == ' ')) { start--; }
        if (wxString("=+-*/(").find(s[start - 1]) == wxString::npos) { return; }
        while (start < s.Len() && s[start] == ' ') { start++; }
        auto typed = s.Mid(start);
        auto upper = typed.Upper();
        for (auto prefix : {L"", L"TREE"}) {
            for (auto &[n, f] : Functions()) {
                auto function = prefix + wxString(n);
                if (function.StartsWith(upper)) { res.Add(s.Left(start) + function + "("); }
            }
        }
        for (auto f : {L"SUBSET(", L"TREESUBSET("}) {
            if (wxString(f).StartsWith(upper)) { res.Add(s.Left(start) + f); }
        }
        for (auto &n : names) {
            if (n.StartsWith(typed) && IsBareName(n)) { res.Add(s.Left(start) + n); }
        }
    }

    // The numbers in the grid of the cell named `name` (see Named()), or its own if it has no
    // grid, except formula cell `self`, and with `tree` also those of the data cells in its
    // sub-grids, depth first.
    wxString Numbers(Cell *self, const wxString &name, vector<double> &numbers, bool tree) {
        wxString error;
        auto *c = Named(self, name, error);
        if (c == nullptr) { return error; }
        return c->grid ? Numbers(self, c->grid.get(), numbers, tree)
                       : Numbers(self, c, numbers, tree);
    }

    wxString Numbers(Cell *self, Grid *g, vector<double> &numbers, bool tree,
                     bool sub = false) {
        for (auto &c : g->cells) {
            if (auto error = Numbers(self, c.get(), numbers, tree, sub); !error.IsEmpty()) {
                return error;
            }
        }
        return {};
    }

    wxString Numbers(Cell *self, Cell *c, vector<double> &numbers, bool tree, bool sub = false) {
        if (c != self && !(sub && c->celltype == CT_FORMULA)) {
            if (c->celltype == CT_FORMULA) {
                if (auto error = Update(c); !error.IsEmpty()) { return error; }
            }
            double d;
            if (c->text.GetStrictNum(d)) { numbers.push_back(d); }
        }
        return tree && c->grid ? Numbers(self, c->grid.get(), numbers, true, true) : wxString();
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
            vector<double> numbers;
            if (i < s.Len() && s[i] == '"') {
                wxString name;
                if (!Name(')', name)) { return Fail(L"#ERROR!"); }
                return One(fe.Numbers(self, name, numbers, false), numbers);
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
            // Words separated by spaces, e.g. =Unit price * 2, but SUM (Costs) is still a function.
            while (i < s.Len() && IsNameChar(s[i])) {
                auto next = ++i;
                while (next < s.Len() && s[next] == ' ') { next++; }
                if (next < s.Len() && IsNameChar(s[next])) { i = next; }
            }
            auto function = s.Mid(start, i - start);
            if (function.IsEmpty()) { return Fail(L"#ERROR!"); }
            if (!Eat('(')) { return One(fe.Numbers(self, function, numbers, false), numbers); }
            function.MakeUpper();
            if (IsSubset(function)) { return One(Subset(numbers, false, function), numbers); }
            wxString rest;
            auto tree = function.StartsWith(L"TREE", &rest);
            if (tree) { function = rest; }
            for (auto &[n, f] : Functions()) {
                if (function != n) { continue; }
                auto e = Argument(numbers, tree);
                return e.IsEmpty() ? f(numbers) : Fail(e);
            }
            return Fail(L"#NAME?");
        }
        // The numbers in the parentheses of a function: the name of a grid, or SUBSET() of one.
        wxString Argument(vector<double> &numbers, bool tree) {
            Space();
            auto start = i;
            while (i < s.Len() && wxIsalpha(s[i])) { i++; }
            auto function = s.Mid(start, i - start).Upper();
            if (IsSubset(function) && Eat('(')) {
                auto e = Subset(numbers, tree, function);
                return !e.IsEmpty() ? e : Eat(')') ? wxString() : wxString(L"#ERROR!");
            }
            i = start;
            wxString name;
            if (!Name(')', name) || !Eat(')')) { return L"#ERROR!"; }
            return fe.Numbers(self, name, numbers, tree);
        }
        // A name, until `end` or in quotes.
        bool Name(wxChar end, wxString &name) {
            auto quoted = Eat('"');
            auto e = s.find(quoted ? wxChar('"') : end, i);
            if (e == wxString::npos) { return false; }
            name = s.Mid(i, e - i);
            if (!quoted) { name.Trim(); }
            i = quoted ? e + 1 : e;
            return true;
        }
        // The one number of a name or SUBSET() on its own, unless there is an error e.
        double One(const wxString &e, const vector<double> &numbers) {
            if (!e.IsEmpty()) { return Fail(e); }
            return numbers.size() == 1 ? numbers[0] : Fail(L"#VALUE!");
        }
        // The rest of SUBSET(name, columns, rows) or TREESUBSET(name, columns, rows, levels), with
        // TREE in front also their sub-grids, which adds nothing to TREESUBSET.
        wxString Subset(vector<double> &numbers, bool tree, const wxString &function) {
            vector<std::pair<Cell *, bool>> cells;
            if (auto e = Cells(cells, function); !e.IsEmpty()) { return e; }
            for (auto [c, sub] : cells) {
                auto e = fe.Numbers(self, c, numbers, tree && function != L"TREESUBSET", sub);
                if (!e.IsEmpty()) { return e; }
            }
            return {};
        }
        // The cells of the rest of a SUBSET() or TREESUBSET(), and whether each one is in a
        // sub-grid. Instead of a name, it can take another SUBSET() or TREESUBSET(), whose cells'
        // grids it then uses.
        wxString Cells(vector<std::pair<Cell *, bool>> &cells, const wxString &function) {
            vector<Grid *> grids;
            Space();
            auto start = i;
            while (i < s.Len() && wxIsalpha(s[i])) { i++; }
            auto inner = s.Mid(start, i - start).Upper();
            wxString name;
            auto nested = IsSubset(inner) && Eat('(');
            if (nested) {
                vector<std::pair<Cell *, bool>> picked;
                if (auto e = Cells(picked, inner); !e.IsEmpty()) { return e; }
                for (auto [c, sub] : picked) {
                    if (c->grid) { grids.push_back(c->grid.get()); }
                }
            } else {
                i = start;
                if (!Name(',', name)) { return L"#ERROR!"; }
            }
            int x0, x1, y0, y1, l0 = 0, l1 = 0;
            bool levels = function == L"TREESUBSET";
            if (!Eat(',') || !Range(x0, x1) || !Eat(',') || !Range(y0, y1) ||
                (levels && (!Eat(',') || !Range(l0, l1))) || !Eat(')')) {
                return L"#ERROR!";
            }
            if (!nested) {
                wxString error;
                auto *named = fe.Named(self, name, error);
                if (named == nullptr) { return error; }
                if (named->grid) { grids.push_back(named->grid.get()); }
            }
            if (grids.empty()) { return L"#VALUE!"; }
            // Clipped to each grid, so that it can grow and shrink.
            auto add = [&](auto &add, Grid *g, int level) -> void {
                for (int y = y0; y <= min(y1, g->ys - 1); y++) {
                    for (int x = x0; x <= min(x1, g->xs - 1); x++) {
                        auto *c = g->C(x, y).get();
                        if (level >= l0) { cells.emplace_back(c, level > 0); }
                        if (level < l1 && c->grid) { add(add, c->grid.get(), level + 1); }
                    }
                }
            };
            for (auto *g : grids) { add(add, g, 0); }
            return {};
        }
        // A column or row number, or an inclusive range of them like 0-3, 3- (to the end), -4
        // (from the start) or - for all.
        bool Range(int &lo, int &hi) {
            auto from = Index(lo);
            if (!from) { lo = 0; }
            if (!Eat('-')) {
                hi = lo;
                return from;
            }
            if (!Index(hi)) { hi = INT_MAX; }
            return true;
        }
        bool Index(int &n) {
            Space();
            auto start = i;
            while (i < s.Len() && wxIsdigit(s[i])) { i++; }
            unsigned long l;
            if (i == start || !s.Mid(start, i - start).ToULong(&l)) { return false; }
            n = static_cast<int>(min(l, static_cast<unsigned long>(INT_MAX)));
            return true;
        }
    };
};
