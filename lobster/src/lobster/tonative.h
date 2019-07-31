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

struct NativeGenerator {
    int current_block_id = -1;

    virtual ~NativeGenerator() {}

    virtual void FileStart() = 0;
    virtual void DeclareBlock(int id) = 0;
    virtual void BeforeBlocks(int start_id, string_view bytecode_buffer) = 0;
    virtual void FunStart(const bytecode::Function *f) = 0;
    virtual void BlockStart(int id) = 0;
    virtual void InstStart() = 0;
    virtual void EmitJump(int id) = 0;
    virtual void EmitConditionalJump(int opc, int id) = 0;
    virtual void EmitOperands(const char *base, const int *args, int arity, bool is_vararg) = 0;
    virtual void SetNextCallTarget(int id) = 0;
    virtual void EmitGenericInst(int opc, const int *args, int arity, bool is_vararg, int target) = 0;
    virtual void EmitCall(int id) = 0;
    virtual void EmitCallIndirect() = 0;
    virtual void EmitCallIndirectNull() = 0;
    virtual void InstEnd() = 0;
    virtual void BlockEnd(int id, bool already_returned, bool is_exit) = 0;
    virtual void CodeEnd() = 0;
    virtual void VTables(vector<int> &vtables) = 0;
    virtual void FileEnd(int start_id, string_view bytecode_buffer) = 0;
    virtual void Annotate(string_view comment) = 0;
};

extern string ToNative(NativeRegistry &natreg, NativeGenerator &ng,
                       string_view bytecode_buffer);

extern string ToCPP(NativeRegistry &natreg, ostringstream &ss,
                    string_view bytecode_buffer);
extern string ToWASM(NativeRegistry &natreg, vector<uint8_t> &dest,
                     string_view bytecode_buffer);

}  // namespace lobster;

// Test the wasm binary writer is working as expected.
void unit_test_wasm(bool full);

#endif  // LOBSTER_TONATIVE
