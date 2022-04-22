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

struct Optimizer {
    Parser &parser;
    SymbolTable &st;
    TypeChecker &tc;
    size_t total_changes = 0;
    vector<SubFunction *> sfstack;
    bool functions_removed = false;

    Optimizer(Parser &_p, SymbolTable &_st, TypeChecker &_tc)
        : parser(_p), st(_st), tc(_tc) {
        // We don't optimize parser.root, it only contains a single call.
        for (auto f : parser.st.functiontable) {
            again:
            for (auto &ov : f->overloads) {
                auto sf = ov.sf;
                if (sf && sf->typechecked) {
                    for (; sf; sf = sf->next) {
                        functions_removed = false;
                        OptimizeFunction(*sf);
                        if (functions_removed) goto again;
                    }
                }
            }
        }
        LOG_INFO("optimizer: ", total_changes, " optimizations");
    }

    void OptimizeFunction(SubFunction &sf) {
        if (sf.optimized) return;
        sf.optimized = true;
        if (!sf.sbody) return;
        if (!sf.typechecked) {
            delete sf.sbody;
            sf.sbody = nullptr;
            return;
        }
        sfstack.push_back(&sf);
        auto nb = sf.sbody->Optimize(*this);
        assert(nb == sf.sbody);
        (void)nb;
        sfstack.pop_back();
    }

    void Changed() { total_changes++; }

    Node *Typed(TypeRef type, Lifetime lt, Node *n) {
        n->exptype = type;
        n->lt = lt;
        return n;
    }
};

Node *Node::Optimize(Optimizer &opt) {
    assert(exptype->t != V_UNDEFINED);
    for (size_t i = 0; i < Arity(); i++) {
        Children()[i] = Children()[i]->Optimize(opt);
    }
    Value cval = NilVal();
    auto t = ConstVal(&opt.tc, cval);
    if (t == V_VOID) return this;
    Node *r;
    switch (t) {
        case V_INT:   r = new IntConstant(line, cval.ival()); break;
        case V_FLOAT: r = new FloatConstant(line, cval.fval()); break;
        case V_NIL:   r = new Nil(line, { exptype }); break;
        default:      assert(false); return this;
    }
    r = opt.Typed(exptype, LT_ANY, r);
    delete this;
    opt.Changed();
    return r->Optimize(opt);
}

Node *Constructor::Optimize(Optimizer &opt) {
    // Special case that really is not an optimisation but should run at end of type checking:
    // "seal" empty lists to a type.
    if (exptype->t == V_VECTOR && children.empty() && exptype->sub->t == V_VAR) {
        opt.tc.UnifyVar(type_int, exptype->sub);
    }
    return Node::Optimize(opt);
}

Node *Nil::Optimize(Optimizer &opt) {
    // Special case that really is not an optimisation but should run at end of type checking:
    // "seal" unknown nils to a type.
    if (exptype->t == V_NIL && exptype->sub->t == V_VAR) {
        opt.tc.UnifyVar(type_string, exptype->sub);
    }
    return this;
}

Node *IntConstant::Optimize(Optimizer &) {
    return this;
}

Node *FloatConstant::Optimize(Optimizer &) {
    return this;
}

Node *Call::Optimize(Optimizer &opt) {
    Node::Optimize(opt);
    assert(sf->numcallers > 0);
    auto parent = opt.sfstack.back();
    // FIXME: Reduce these requirements where possible.
    bool is_inlinable =
        !sf->isrecursivelycalled &&
        sf->num_returns <= 1 &&
        // Have to double-check this, since last return may be a return from.
        AssertIs<Return>(sf->sbody->children.back())->sf == sf &&
        // This may happen even if num_returns==1 when body of sf is a non-local return also,
        // See e.g. exception_handler
        // FIXME: if the function that caused this to be !=0 gets inlined, this needs to be
        // decremented so it can be inlined after all.
        sf->num_returns_non_local == 0 &&
        vtable_idx < 0 &&
        sf->returntype->NumValues() <= 1 &&
        // Because we inline so aggressively, it is possible to generate huuge functions,
        // which may cause a problem for our stack, or that of e.g. V8 in Wasm.
        parent->locals.size() < 1024;
    // Attempt to optimize function we're calling first, that way if it shrinks (or grows) it's
    // more or less likely to be inlined.
    if (is_inlinable) opt.OptimizeFunction(*sf);
    // Check if we should inline this call.
    if (!is_inlinable ||
        (sf->numcallers > 1 && sf->sbody->Count() >= 16)) { // FIXME: configurable.
        return this;
    }
    auto AddToLocals = [&](const vector<Arg> &av) {
        for (auto &arg : av) {
            // We have to check if the sid already exists, since inlining the same function
            // multiple times in the same parent can cause this. This variable is shared
            // between the copies in the parent, second use overwrites the first etc.
            // We generally have to keep using this sid rather than creating a new one, since
            // this function may call others that may refer to this sid, etc.
            for (auto &loc : parent->locals) if (loc.sid == arg.sid) goto already;
            parent->locals.push_back(arg);
            arg.sid->sf_def = parent;
            already:;
        }
    };
    AddToLocals(sf->args);
    AddToLocals(sf->locals);
    int ai = 0;
    auto list = new Block(line);
    for (auto c : children) {
        auto &arg = sf->args[ai];
        // NOTE: this introduces locals which potentially borrow, which the typechecker so far
        // never introduces.
        // We can't just force these to LT_KEEP, since the sids maybe shared with non-inlined
        // instances (e.g. in dynamic dispatch).
        // Borrowing could be problematic if 2 copies of the same function get inlined, since
        // that creates an overwite of a borrowed variable, but the codegen ensures the overwrite
        // does not decref.
        auto def = new Define(line, c);
        def->sids.push_back({ arg.sid, { arg.type } });
        list->Add(opt.Typed(type_void, LT_ANY, def));
        ai++;
    }
    // TODO: triple-check this similar in semantics to what happens in CloneFunction() in the
    // typechecker.
    if (sf->numcallers == 1) {
        list->children.insert(list->children.end(), sf->sbody->children.begin(),
                              sf->sbody->children.end());
        sf->sbody->children.clear();
        delete sf->sbody;
        sf->sbody = nullptr;
        opt.functions_removed = sf->parent->RemoveSubFunction(sf);
        assert(opt.functions_removed);
    } else {
        for (auto c : sf->sbody->children) {
            auto nc = c->Clone();
            list->children.push_back(nc);
            nc->Iterate([](Node *i) {
                if (auto call = Is<Call>(i)) call->sf->numcallers++;
                if (auto dcall = Is<DynCall>(i)) dcall->sf->numcallers++;
            });
        }
    }
    sf->numcallers--;
    // Remove single return statement pointing to function that is now gone.
    auto ret = AssertIs<Return>(list->children.back());
    if (ret->sf == sf) {
        assert(ret->child->exptype->NumValues() <= 1);
        assert(sf->num_returns <= 1);
        assert(sf->num_returns_non_local == 0);
        // This is not great: having to undo the optimization in Return::TypeCheck where this
        // flag was set.
        // Since the caller generally expects to keep the return value of the now inlined
        // function, and since the variables of this function are not dead yet (they can be called
        // multiple times if inside a loop body or inlined multiple times, which may cause the
        // var to be decreffed when overwritten on second use), we have to incref.
        // TODO: investigate if setting them to null at scope exit would be an alternative?
        if (sf->consumes_vars_on_return) {
            AssertIs<IdentRef>(ret->child);
            opt.tc.MakeLifetime(ret->child, LT_KEEP, 1, 0);
        }
        list->children.back() = ret->child;
        ret->child = nullptr;
        delete ret;
    }
    auto r = opt.Typed(exptype, sf->ltret, list);
    children.clear();
    r = r->Optimize(opt);
    delete this;  // Do this after, since Optimize may touch this same call.
    opt.Changed();
    return r;
}

}  // namespace lobster


