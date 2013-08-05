#pragma once
#include "stdafx.h"
#include "evaluator.fwd.h"
#include "text.fwd.h"
#include "cell.fwd.h"
#include "grid.fwd.h"
namespace treesheets {

struct Operation
{
    const char *args;

    virtual Cell *run();

    virtual double runn(double a);
    virtual Cell * runt(Text *t);
    virtual Cell * runl(Grid *l);
    virtual Cell * rung(Grid *g);
    virtual Cell * runc(Cell *c);

    virtual double runnn (double a, double b);
};

WX_DECLARE_STRING_HASH_MAP(Operation *, wxHashMapOperation);
WX_DECLARE_STRING_HASH_MAP(Cell *, wxHashMapCell);

struct Evaluator
{
    wxHashMapOperation ops;
    wxHashMapCell vars;
    bool vert;
    ~Evaluator();
    void Init();
    int InferCellType(Text &t);
    Cell *Lookup(wxString &name);
    void Assign(wxString &name, Cell *val);
    Operation *FindOp(wxString &name);
    Cell *Execute(Operation *op);
    Cell *Execute(Operation *op, Cell *left);
    Cell *Execute(Operation *op, Cell *left, Cell *right);
    Cell *Execute(Operation *op, Cell *left, Cell *a, Cell *b);  // IF is sofar the only ternary
    void Eval(Cell *root);
};
}
