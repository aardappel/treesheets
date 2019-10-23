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

#ifndef LOBSTER_DISASM
#define LOBSTER_DISASM

#include "lobster/natreg.h"

#define FLATBUFFERS_DEBUG_VERIFICATION_FAILURE
#include "lobster/bytecode_generated.h"

namespace lobster {

inline string IdName(const bytecode::BytecodeFile *bcf, int i) {
    auto idx = bcf->specidents()->Get(i)->ididx();
    int j = i;
    // FIXME: this theoretically can span 2 specializations of the same var.
    while (j && bcf->specidents()->Get(j - 1)->ididx() == idx) j--;
    auto basename = bcf->idents()->Get(idx)->name()->
    #ifdef __ANDROID__
        str();
    #else
        string_view();
    #endif
    return j == i ? string(basename) : cat(basename, '+', i - j);
}

const bytecode::LineInfo *LookupLine(const int *ip, const int *code,
                                     const bytecode::BytecodeFile *bcf);

const int *DisAsmIns(NativeRegistry &natreg, ostringstream &ss, const int *ip, const int *code,
                     const type_elem_t *typetable, const bytecode::BytecodeFile *bcf);

void DisAsm(NativeRegistry &natreg, ostringstream &ss, string_view bytecode_buffer);

}  // namespace lobster

#endif  // LOBSTER_DISASM
