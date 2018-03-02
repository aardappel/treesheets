
namespace lobster {

struct Optimizer {
    Parser &parser;
    SymbolTable &st;
    TypeChecker &tc;
    bool changes_this_pass;
    size_t total_changes;
    Node *dummy_node;
    SubFunction *cursf;

    Optimizer(Parser &_p, SymbolTable &_st, TypeChecker &_tc, int maxpasses)
        : parser(_p), st(_st), tc(_tc), changes_this_pass(true), total_changes(0),
          dummy_node(nullptr), cursf(nullptr) {
        int i = 0;
        // MUST run at least 1 pass, to guarantee certain unwanted code is gone.
        maxpasses = max(1, maxpasses);
        for (; changes_this_pass && i < maxpasses; i++) {
            changes_this_pass = false;
            // We don't optimize parser.root, it only contains a single call.
            for (auto f : parser.st.functiontable) {
                if (f->subf && f->subf->typechecked) {
                    for (auto sf = f->subf; sf; sf = sf->next) if (sf->body) {
                        cursf = sf;
                        auto nb = Optimize(sf->body, typeid(*sf->body));
                        assert(nb == sf->body);
                        (void)nb;
                    }
                }
            }
        }
        Output(OUTPUT_INFO, "optimizer: %d passes, %d optimizations", i, total_changes);
        assert(i);  // Must run at least one pass.
    }

    void Changed() { changes_this_pass = true; total_changes++; }

    Node *Typed(TypeRef type, Node *n) {
        Changed();
        n->exptype = type;
        return n;
    }

    Node *Optimize(Node *n, const std::type_info &parent_type) {
        Node *r = n;
        if (auto ifn = Is<If>(n)) {
            // This optimzation MUST run, since it deletes untypechecked code.
            ifn->condition = Optimize(ifn->condition, typeid(*n));
            Value cval;
            if (ifn->condition->ConstVal(tc, cval)) {
                Changed();
                auto &branch = cval.True() ? ifn->truepart : ifn->falsepart;
                auto other  = cval.True() ? ifn->falsepart : ifn->truepart;
                r = Optimize(branch, typeid(*n));
                branch = nullptr;
                if (auto tonil = Is<ToNil>(other)) {
                    other = tonil->child;
                }
                if (auto call = Is<Call>(other)) {
                    if (!call->sf->typechecked) {
                        // Typechecker did not typecheck this function for use in this if-then,
                        // but neither did any other instances, so it can be removed.
                        call->sf->parent->RemoveSubFunction(call->sf);
                        call->sf = nullptr;
                    }
                }
                delete ifn;
            } else {
                ifn->truepart = Optimize(ifn->truepart, typeid(*n));
                ifn->falsepart = Optimize(ifn->falsepart, typeid(*n));
            }
            return r;
        }
        for (size_t i = 0; i < n->Arity(); i++)
            n->Children()[i] = Optimize(n->Children()[i], typeid(*n));
        if (auto is = Is<IsType>(n)) {
            Value cval;
            if (is->ConstVal(tc, cval)) {
                r = Typed(type_int, new IntConstant(is->line, cval.ival()));
                if (is->child->HasSideEffects()) {
                    r = Typed(type_int, new Seq(is->line, is->child, r));
                    is->child = nullptr;
                }
                delete is;
            }
        } else if (auto dcall = Is<DynCall>(n)) {
            // This optimization MUST run, to remove redundant arguments.
            if (auto sf = dcall->sf) {
                // Note that this is not necessarily the same as dcall->sid->type->sf, since a
                // single function variable may have 1 specialization per call
                size_t i = 0;
                for (auto c : dcall->children) {
                    if (i++ >= sf->parent->nargs()) {
                        delete c;
                    }
                }
                dcall->children.resize(sf->parent->nargs());
                // Now convert it to a T_CALL if possible. This also allows it to be inlined.
                // We rely on all these DYNCALLs being converted in the first pass, and only
                // potentially inlined in the second for this increase to not cause problems.
                sf->numcallers++;
                if (!sf->parent->istype) {
                    auto c = new Call(dcall->line, sf);
                    c->children.insert(c->children.end(), dcall->children.begin(),
                        dcall->children.end());
                    dcall->children.clear();
                    r = Typed(dcall->exptype, c);
                    delete dcall;
                }
            }
        } else if (auto call = Is<Call>(n)) {
            auto sf = call->sf;
            // FIXME: Reduce these requirements where possible.
            // FIXME: currently a function called 10x whose body is only a gigantic for loop will be inlined,
            // because the for body does not count towards its nodes. maybe inline all fors first?
            if (parent_type == typeid(For) ||  // Always inline for bodies.
                (sf->parent->anonymous &&
                 !sf->iscoroutine &&
                 !sf->dynscoperedefs.size() &&
                 sf->returntypes.size() <= 1 &&
                 (sf->numcallers <= 1 || sf->body->Count() < 8)))  // FIXME: configurable.
            {
                r = Inline(*call, *sf);
            }
        }
        return r;
    }

    Node *Inline(Call &call, SubFunction &sf) {
        // Note that sf_def in these Ident's being moved is now not correct anymore, but the
        // only use for that field is to determine if the variable is "global" after the optimizer,
        // so we let that slip.
        cursf->locals.v.insert(cursf->locals.v.end(), sf.args.v.begin(), sf.args.v.end());
        cursf->locals.v.insert(cursf->locals.v.end(), sf.locals.v.begin(), sf.locals.v.end());
        int ai = 0;
        auto list = new Inlined(call.line);
        for (auto c : call.children) {
            auto &arg = sf.args.v[ai];
            list->Add(Typed(type_any, new Define(call.line, arg.sid, c, nullptr)));
            ai++;
        }
        call.children.clear();
        delete &call;
        // TODO: triple-check this similar in semantics to what happens in CloneFunction() in the
        // typechecker.
        if (sf.numcallers <= 1) {
            list->children.insert(list->children.end(), sf.body->children.begin(),
                                  sf.body->children.end());
            sf.body->children.clear();
            sf.parent->RemoveSubFunction(&sf);
        } else {
            for (auto c : sf.body->children) {
                auto nc = c->Clone();
                list->children.push_back(nc);
                nc->Iterate([](Node *i) {
                    if (auto call = Is<Call>(i)) call->sf->numcallers++;
                });
            }
            sf.numcallers--;
        }
        return Typed(sf.returntypes[0], list);
    }
};

}  // namespace lobster


