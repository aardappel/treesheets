#include "evaluator.h"
#include "text.h"
#include "globals.h"
#include "cell.h"
#include "grid.h"
namespace treesheets {

Cell *Operation::run() { return NULL; }
double Operation::runn(double a) { return 0; }
Cell * Operation::runt(Text *t)  { return NULL; }
Cell * Operation::runl(Grid *l)  { return NULL; }
Cell * Operation::rung(Grid *g)  { return NULL; }
Cell * Operation::runc(Cell *c)  { return NULL; }
double Operation::runnn (double a, double b) { return 0; }

Evaluator::~Evaluator()
{
    while(ops.size())  { delete ops.begin()->second;  ops.erase(ops.begin());   }
    while(vars.size()) { delete vars.begin()->second; vars.erase(vars.begin()); }
}

#define OP(_n, _c, _args, _f)  { struct _op : Operation { _op() { args = _args; }; _f { return _c; }; }; ops[L ## #_n] = new _op(); }

#define OPNN(_n, _c) OP(_n, _c, "nn", double runnn(double a, double b))
#define OPN(_n, _c)  OP(_n, _c, "n",  double runn (double a))
#define OPT(_n, _c)  OP(_n, _c, "t",  Cell * runt (Text *t))
#define OPL(_n, _c)  OP(_n, _c, "l",  Cell * runl (Grid *a))
#define OPG(_n, _c)  OP(_n, _c, "g",  Cell * rung (Grid *a))

void Evaluator::Init()
{
    OPNN(+, a+b);
    OPNN(-, a-b);
    OPNN(*, a*b);
    OPNN(/, b!=0 ? a/b : 0);

    OPNN(<,  double(a<b));
    OPNN(>,  double(a>b));
    OPNN(<=, double(a<=b));
    OPNN(>=, double(a>=b));
    OPNN(=,  double(a==b));
    OPNN(==, double(a==b));
    OPNN(!=, double(a!=b));
    OPNN(<>, double(a!=b));

    OPN(inc, a+1);
    OPN(dec, a-1);
    OPN(neg, -a);
    
    OPT(graph, t->Graph());

    OPL(sum, a->Sum())
    OPG(transpose, a->Transpose())

    struct _if : Operation { _if() { args = "nLL"; }; };
    ops[L"if"] = new _if();
}

int Evaluator::InferCellType(Text &t)
{
    if(ops[t.t]) return CT_CODE;
    else return CT_DATA;
}

Cell *Evaluator::Lookup(wxString &name)
{
    return vars[name]->Clone(NULL);
}

void Evaluator::Assign(wxString &name, Cell *val)
{
    Cell *old = vars[name];
    DELETEP(old);
    vars[name] = val;
}

Operation *Evaluator::FindOp(wxString &name)
{
    return ops[name];
}

Cell *Evaluator::Execute(Operation *op)
{
    return op->run();
}

Cell *Evaluator::Execute(Operation *op, Cell *left)
{
    Text &t = left->text;
    Grid *g = left->grid;
    
    switch(op->args[0])
    {
        case 'n': if(t.t.Len()) return t.SetNum(op->runn(t.GetNum())); else if(g) foreachcellingrid(c, g) c = Execute(op, c)->SetParent(left); break;
        case 't': if(t.t.Len()) return op->runt(&t);                   else if(g) foreachcellingrid(c, g) c = Execute(op, c)->SetParent(left); break;
            
        case 'l':
            if(g)
            {
                if(g->xs==1 || g->ys==1) return op->runl(g);
                Vector<Grid *> gs;
                g->Split(gs, vert);
                g = new Grid(vert ? gs.size() : 1, vert ? 1 : gs.size());
                Cell *c = new Cell(NULL, left, CT_DATA, g);
                loopv(i, gs) g->C(vert ? i : 0, vert ? 0 : i) = op->runl(gs[i])->SetParent(c);
                gs.setsize_nd(0);
                return c;
            }
            break;
            
        case 'g': if(g) return op->rung(g); break;
        
        case 'c': return op->runc(left);
    }
    return left;
}

Cell *Evaluator::Execute(Operation *op, Cell *left, Cell *right)
{
    if(!(right = right->Eval(*this))) return left;
    Text &t1 = left->text;
    Text &t2 = right->text;
    Grid *g1 = left->grid;
    Grid *g2 = right->grid;
    switch(op->args[0])
    {
        case 'n':
            if(t1.t.Len() && t2.t.Len()) t1.SetNum(op->runnn(t1.GetNum(), t2.GetNum()));
            else if(g1 && g2 && g1->xs==g2->xs && g1->ys==g2->ys)
            {
                Grid *g = new Grid(g1->xs, g1->ys);
                Cell *c = new Cell(NULL, left, CT_DATA, g);
                loop(x, g->xs) loop(y, g->ys)
                {
                    Cell *&c1 = g1->C(x, y);
                    Cell *&c2 = g2->C(x, y);
                    g->C(x, y) = Execute(op, c1, c2)->SetParent(c);
                    c1 = c2 = NULL;
                }
                delete g1;
                delete g2;
                return c;
            }
            else if(g1 && t2.t.Len())
            {
                foreachcellingrid(c, g1) c = Execute(op, c, right->Clone(NULL))->SetParent(left);
            }
            break;
    }
    delete right;
    return left;
}

Cell *Evaluator::Execute(Operation *op, Cell *left, Cell *a, Cell *b)  // IF is sofar the only ternary
{
    Text &l = left->text;
    if(!l.t.Len()) return left;
    bool cond = l.GetNum()!=0;
    delete left;
    return (cond ? a : b)->Eval(*this);
}

void Evaluator::Eval(Cell *root)
{
    Cell *c = root->Eval(*this);
    DELETEP(c);
}
}
