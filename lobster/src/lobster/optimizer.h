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
    SubFunction *cursf = nullptr;

    Optimizer(Parser &_p, SymbolTable &_st, TypeChecker &_tc)
        : parser(_p), st(_st), tc(_tc) {
        // We don't optimize parser.root, it only contains a single call.
        for (auto f : parser.st.functiontable) {
            for (auto sf : f->overloads) {
                if (sf && sf->typechecked) {
                    for (; sf; sf = sf->next) if (sf->body) {
                        cursf = sf;
                        auto nb = sf->body->Optimize(*this, nullptr);
                        assert(nb == sf->body);
                        (void)nb;
                    }
                }
            }
        }
        LOG_INFO("optimizer: ", total_changes, " optimizations");
    }

    void Changed() { total_changes++; }

    Node *Typed(TypeRef type, Lifetime lt, Node *n) {
        n->exptype = type;
        n->lt = lt;
        return n;
    }
};

Node *Node::Optimize(Optimizer &opt, Node * /*parent_maybe*/) {
    for (size_t i = 0; i < Arity(); i++)
        Children()[i] = Children()[i]->Optimize(opt, this);
    return this;
}

Node *If::Optimize(Optimizer &opt, Node * /*parent_maybe*/) {
    // This optimzation MUST run, since it deletes untypechecked code.
    condition = condition->Optimize(opt, this);
    Value cval;
    if (condition->ConstVal(opt.tc, cval)) {
        auto &branch = cval.True() ? truepart : falsepart;
        auto other  = cval.True() ? falsepart : truepart;
        auto r = branch->Optimize(opt, this);
        branch = nullptr;
        if (auto call = Is<Call>(other)) {
            if (!call->sf->typechecked) {
                // Typechecker did not typecheck this function for use in this if-then,
                // but neither did any other instances, so it can be removed.
                // Since this function is not specialized, it may be referenced by
                // multiple specialized parents, so we don't care if it was already
                // removed.
                call->sf->parent->RemoveSubFunction(call->sf);
                call->sf = nullptr;
            }
        } else if (Is<DefaultVal>(other)) {
        } else {
            assert(false);  // deal with coercions.
        }
        delete this;
        opt.Changed();
        return r;
    } else {
        truepart = truepart->Optimize(opt, this);
        falsepart = falsepart->Optimize(opt, this);
        return this;
    }
}

Node *IsType::Optimize(Optimizer &opt, Node *parent_maybe) {
    Value cval;
    child = child->Optimize(opt, this);
    if (ConstVal(opt.tc, cval)) {
        auto r = opt.Typed(exptype, LT_ANY, new IntConstant(line, cval.ival()));
        if (child->HasSideEffects()) {
            child->exptype = type_void;
            r = opt.Typed(exptype, LT_ANY, new Seq(line, child, r));
            child = nullptr;
        }
        delete this;
        opt.Changed();
        return r->Optimize(opt, parent_maybe);
    }
    return this;
}

Node *DynCall::Optimize(Optimizer &opt, Node *parent_maybe) {
    Node::Optimize(opt, parent_maybe);
    if (!sf) return this;
    // This optimization MUST run, to remove redundant arguments.
    // Note that sf is not necessarily the same as sid->type->sf, since a
    // single function variable may have 1 specialization per call.
    for (auto[i, c] : enumerate(children)) {
        if (i >= sf->parent->nargs()) {
            opt.Changed();
            delete c;
        }
    }
    children.resize(sf->parent->nargs());
    // Now convert it to a Call if possible. This also allows it to be inlined.
    // We rely on all these DynCalls being converted in the first pass, and only
    // potentially inlined in the second for this increase to not cause problems.
    sf->numcallers++;
    if (sf->parent->istype) return this;
    auto c = new Call(line, sf);
    c->children.insert(c->children.end(), children.begin(), children.end());
    children.clear();
    auto r = opt.Typed(exptype, lt, c);
    delete this;
    opt.Changed();
    return r->Optimize(opt, parent_maybe);
}

Node *Call::Optimize(Optimizer &opt, Node *parent_maybe) {
    Node::Optimize(opt, parent_maybe);
    // Check if we should inline this call.
    // FIXME: Reduce these requirements where possible.
    // FIXME: currently a function called 10x whose body is only a gigantic for loop will be inlined,
    // because the for body does not count towards its nodes. maybe inline all fors first?
    // Always inline for bodies.
    if (!parent_maybe || typeid(*parent_maybe) != typeid(For)) {
        if (!sf->parent->anonymous ||
            sf->num_returns > 1 ||       // Implied by anonymous, but here for clarity.
            vtable_idx >= 0 ||
            sf->iscoroutine ||
            sf->returntype->NumValues() > 1 ||
            (sf->numcallers > 1 && sf->body->Count() >= 8))  // FIXME: configurable.
            return this;
    }
    auto AddToLocals = [&](const ArgVector &av) {
        for (auto &arg : av.v) {
            // We have to check if the sid already exists, since inlining the same function
            // multiple times in the same parent can cause this. This variable is shared
            // between the copies in the parent, second use overwrites the first etc.
            for (auto &loc : opt.cursf->locals.v) if (loc.sid == arg.sid) goto already;
            opt.cursf->locals.v.push_back(arg);
            arg.sid->sf_def = opt.cursf;
            already:;
        }
    };
    AddToLocals(sf->args);
    AddToLocals(sf->locals);
    int ai = 0;
    auto list = new Inlined(line);
    for (auto c : children) {
        auto &arg = sf->args.v[ai];
        list->Add(opt.Typed(type_void, LT_ANY, new Define(line, arg.sid, c)));
        ai++;
    }
    // TODO: triple-check this similar in semantics to what happens in CloneFunction() in the
    // typechecker.
    if (sf->numcallers <= 1) {
        list->children.insert(list->children.end(), sf->body->children.begin(),
                              sf->body->children.end());
        sf->body->children.clear();
        bool wasremoved = sf->parent->RemoveSubFunction(sf);
        assert(wasremoved);
        (void)wasremoved;
    } else {
        for (auto c : sf->body->children) {
            auto nc = c->Clone();
            list->children.push_back(nc);
            nc->Iterate([](Node *i) {
                if (auto call = Is<Call>(i)) call->sf->numcallers++;
            });
        }
        sf->numcallers--;
    }
    // Remove single return statement pointing to function that is now gone.
    auto ret = Is<Return>(list->children.back());
    assert(ret);
    if (ret->sf == sf) {
        assert(ret->child->exptype->NumValues() <= 1);
        assert(sf->num_returns <= 1);
        list->children.back() = ret->child;
        ret->child = nullptr;
        delete ret;
    }
    auto r = opt.Typed(exptype, sf->ltret, list);
    children.clear();
    delete this;
    opt.Changed();
    return r->Optimize(opt, parent_maybe);
}

}  // namespace lobster


