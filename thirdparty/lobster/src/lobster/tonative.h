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

#ifndef LOBSTER_TONATIVE
#define LOBSTER_TONATIVE

#include "lobster/natreg.h"

namespace lobster {

extern string ToCPP(NativeRegistry &natreg, string &sd, string_view bytecode_buffer, bool cpp,
                    int runtime_checks, string_view custom_pre_init_name, string_view aux_src_name);

extern bool RunC(const char *source,
                 const char *object_name /* save instead of run if non-null */,
                 string &error,
                 const void **imports,
                 const char **export_names,
                 function<bool (void **)> runf);

inline int ParseOpAndGetArity(int opc, const int *&ip, int &regso) {
    regso = *ip++;
    auto arity = ILArity()[opc];
    auto ips = ip;
    switch(opc) {
        default: {
            assert(arity != ILUNKNOWN);
            ip += arity;
            break;
        }
        case IL_JUMP_TABLE: {
            auto mini = *ip++;
            auto maxi = *ip++;
            auto n = maxi - mini + 2;
            ip += n;
            arity = int(ip - ips);
            break;
        }
        case IL_JUMP_TABLE_DISPATCH: {
            ip++;  // vtable_idx
            auto mini = *ip++;
            auto maxi = *ip++;
            auto n = maxi - mini + 2;
            ip += n;
            arity = int(ip - ips);
            break;
        }
        case IL_FUNSTART: {
            ip++;  // function idx.
            ip++;  // max regs.
            int n = *ip++;
            ip += n;
            int m = *ip++;
            ip += m;
            ip++;  // keepvar
            int o = *ip++;  // ownedvar
            ip += o;
            arity = int(ip - ips);
            break;
        }
    }
    return arity;
}

inline auto CreateFunctionLookUp(const bytecode::BytecodeFile *bcf) {
    map<int, const bytecode::Function *> fl;
    for (flatbuffers::uoffset_t i = 0; i < bcf->functions()->size(); i++) {
        auto f = bcf->functions()->Get(i);
        fl[f->bytecodestart()] = f;
    }
    return fl;
}

}  // namespace lobster;

#endif  // LOBSTER_TONATIVE
