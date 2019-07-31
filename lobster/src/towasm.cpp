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

// Include this first to ensure it is free of dependencies.
#include "lobster/wasm_binary_writer.h"
#include "lobster/wasm_binary_writer_test.h"

#include "lobster/stdafx.h"

#include "lobster/disasm.h"  // Some shared bytecode utilities.
#include "lobster/compiler.h"
#include "lobster/tonative.h"

namespace lobster {

class WASMGenerator : public NativeGenerator {
    WASM::BinaryWriter bw;

    size_t import_erccm  = 0, import_snct = 0, import_gnct = 0;

    const bytecode::Function *next_block = nullptr;
  public:

    explicit WASMGenerator(vector<uint8_t> &dest) : bw(dest) {}

    enum {
        TI_I_,
        TI_I_I,
        TI_I_II,
        TI_I_III,
        TI_I_IIII,
        TI_I_IIIII,
        TI_I_IIIIII,
        TI_V_,
        TI_V_I,
        TI_V_II,
        TI_V_III,
        TI_V_IIII,
    };

    void FileStart() override {
        bw.BeginSection(WASM::Section::Type);
        // NOTE: this must match the enum above.
        bw.AddType({}, { WASM::I32 });
        bw.AddType({ WASM::I32 }, { WASM::I32 });
        bw.AddType({ WASM::I32, WASM::I32 }, { WASM::I32 });
        bw.AddType({ WASM::I32, WASM::I32, WASM::I32 }, { WASM::I32 });
        bw.AddType({ WASM::I32, WASM::I32, WASM::I32, WASM::I32 }, { WASM::I32 });
        bw.AddType({ WASM::I32, WASM::I32, WASM::I32, WASM::I32, WASM::I32 }, { WASM::I32 });
        bw.AddType({ WASM::I32, WASM::I32, WASM::I32, WASM::I32, WASM::I32, WASM::I32 }, { WASM::I32 });
        bw.AddType({}, {});
        bw.AddType({ WASM::I32 }, {});
        bw.AddType({ WASM::I32, WASM::I32 }, {});
        bw.AddType({ WASM::I32, WASM::I32, WASM::I32 }, {});
        bw.AddType({ WASM::I32, WASM::I32, WASM::I32, WASM::I32 }, {});
        bw.EndSection(WASM::Section::Type);

        bw.BeginSection(WASM::Section::Import);
        #define S_ARGS0 TI_V_I
        #define S_ARGS1 TI_V_II
        #define S_ARGS2 TI_V_III
        #define S_ARGS3 TI_V_IIII
        #define S_ARGS9 TI_V_II  // ILUNKNOWNARITY
        #define S_ARGSN(N) S_ARGS##N
        #define C_ARGS0 TI_V_II
        #define C_ARGS1 TI_V_III
        #define C_ARGS2 TI_V_IIII
        #define C_ARGS3 TI_V_IIIII
        #define C_ARGS9 TI_V_III  // ILUNKNOWNARITY
        #define C_ARGSN(N) C_ARGS##N
        #define F(N, A) bw.AddImportLinkFunction("CVM_" #N, S_ARGSN(A));
            LVALOPNAMES
        #undef F
        #define F(N, A) bw.AddImportLinkFunction("CVM_" #N, S_ARGSN(A));
            ILBASENAMES
        #undef F
        #define F(N, A) bw.AddImportLinkFunction("CVM_" #N, C_ARGSN(A));
            ILCALLNAMES
        #undef F
        #define F(N, A) bw.AddImportLinkFunction("CVM_" #N, TI_I_I);
            ILJUMPNAMES
        #undef F
        import_erccm = bw.AddImportLinkFunction("EngineRunCompiledCodeMain", TI_I_IIIIII);
        import_snct = bw.AddImportLinkFunction("CVM_SetNextCallTarget", TI_V_II);
        import_gnct = bw.AddImportLinkFunction("CVM_GetNextCallTarget", TI_I_I);
        bw.EndSection(WASM::Section::Import);

        bw.BeginSection(WASM::Section::Function);
        bw.AddFunction(TI_I_II);  // main(), defined function 0.
        // All blocks follow here, which have id's 1..N-1.
    }

    void DeclareBlock(int /*id*/) override {
        bw.AddFunction(TI_I_I);
    }

    void BeforeBlocks(int start_id, string_view bytecode_buffer) override {
        bw.EndSection(WASM::Section::Function);

        // We need this (and Element below) to be able to use call_indirect.
        bw.BeginSection(WASM::Section::Table);
        bw.AddTable();
        bw.EndSection(WASM::Section::Table);

        bw.BeginSection(WASM::Section::Memory);
        bw.AddMemory(1);
        bw.EndSection(WASM::Section::Memory);

        // Don't emit a Start section, since this will be determined by the
        // linker (and where-ever the main() symbol ends up).
        /*
        bw.BeginSection(WASM::Section::Start);
        bw.AddStart(0);
        bw.EndSection(WASM::Section::Start);
        */

        // This initializes the Table declared above. Needed for call_indirect.
        // For now we use a utility function that maps all functions ids 1:1 to the table.
        bw.BeginSection(WASM::Section::Element);
        bw.AddElementAllFunctions();
        bw.EndSection(WASM::Section::Element);

        bw.BeginSection(WASM::Section::Code);

        // Emit main().
        bw.AddCode({}, "main", false);
        bw.EmitGetLocal(0 /*argc*/);
        bw.EmitGetLocal(1 /*argv*/);
        bw.EmitI32ConstFunctionRef(bw.GetNumFunctionImports() + start_id);
        bw.EmitI32ConstDataRef(1, 0);  // Bytecode, for data refs.
        bw.EmitI32Const((int)bytecode_buffer.size());
        bw.EmitI32ConstDataRef(0, 0);  // vtables.
        bw.EmitCall(import_erccm);
        bw.EmitEndFunction();
    }

    void FunStart(const bytecode::Function *f) override {
        next_block = f;
    }

    void BlockStart(int id) override {
        bw.AddCode({}, "block" + std::to_string(id) +
                       (next_block ? "_" + next_block->name()->string_view() : ""), true);
        next_block = nullptr;
    }

    void InstStart() override {
    }

    void EmitJump(int id) override {
        if (id <= current_block_id) {
            // A backwards jump, go via the trampoline.
            bw.EmitI32ConstFunctionRef(bw.GetNumFunctionImports() + id);
        } else {
            // A forwards call, should be safe to tail-call.
            bw.EmitGetLocal(0 /*VM*/);
            bw.EmitCall(bw.GetNumFunctionImports() + id);
        }
        bw.EmitReturn();
    }

    void EmitConditionalJump(int opc, int id) override {
        bw.EmitGetLocal(0 /*VM*/);
        bw.EmitCall((size_t)opc);
        bw.EmitIf(WASM::VOID);
        EmitJump(id);
        bw.EmitEnd();
    }

    void EmitOperands(const char *base, const int *args, int arity, bool is_vararg) override {
        bw.EmitGetLocal(0 /*VM*/);
        if (is_vararg) {
            if (arity) bw.EmitI32ConstDataRef(1, (const char *)args - base);
            else bw.EmitI32Const(0);  // nullptr
        }
    }

    void SetNextCallTarget(int id) override {
        bw.EmitGetLocal(0 /*VM*/);
        bw.EmitI32ConstFunctionRef(bw.GetNumFunctionImports() + id);
        bw.EmitCall(import_snct);
    }

    void EmitGenericInst(int opc, const int *args, int arity, bool is_vararg, int target) override {
        if (!is_vararg) {
            for (int i = 0; i < arity; i++) bw.EmitI32Const(args[i]);
        }
        if (target >= 0) { bw.EmitI32ConstFunctionRef(bw.GetNumFunctionImports() + target); }
        bw.EmitCall((size_t)opc);  // Opcodes are the 0..N of imports.
    }

    void EmitCall(int id) override {
        EmitJump(id);
    }

    void EmitCallIndirect() override {
        bw.EmitGetLocal(0 /*VM*/);
        bw.EmitCall(import_gnct);
        bw.EmitReturn();
    }

    void EmitCallIndirectNull() override {
        bw.EmitGetLocal(0 /*VM*/);
        bw.EmitCall(import_gnct);
        bw.EmitIf(WASM::VOID);
        bw.EmitGetLocal(0 /*VM*/);
        bw.EmitCall(import_gnct);
        bw.EmitReturn();
        bw.EmitEnd();
    }

    void InstEnd() override {
    }

    void BlockEnd(int id, bool already_returned, bool is_exit) override {
        if (!already_returned) {
            if (is_exit) {
                bw.EmitGetLocal(0 /*VM*/);
                bw.EmitCall(import_gnct);
                bw.EmitReturn();
            } else {
                EmitJump(id);
            }
        }
        bw.EmitEndFunction();
    }

    void CodeEnd() override {
        bw.EndSection(WASM::Section::Code);
    }

    void VTables(vector<int> &vtables) override {
        bw.BeginSection(WASM::Section::Data);

        vector<int> wid;
        for (auto id : vtables) {
            wid.push_back(id >= 0 ? (int)bw.GetNumFunctionImports() + id : -1);
        }
        bw.AddData(string_view((char *)wid.data(), wid.size() * sizeof(int)), "vtables",
                   sizeof(int));
        for (auto [i, id] : enumerate(vtables)) {
            if (id >= 0) bw.DataFunctionRef(bw.GetNumFunctionImports() + id, i * sizeof(int));
        }
    }

    void FileEnd(int /*start_id*/, string_view bytecode_buffer) override {
        // TODO: don't really want to store all of this.
        bw.AddData(bytecode_buffer, "static_data", 16);
        bw.EndSection(WASM::Section::Data);

        bw.Finish();
    }

    void Annotate(string_view /*comment*/) override {
    }
};

string ToWASM(NativeRegistry &natreg, vector<uint8_t> &dest, string_view bytecode_buffer) {
    if (VM_DISPATCH_METHOD != VM_DISPATCH_TRAMPOLINE)
        return "WASM codegen: can only use trampoline mode";
    WASMGenerator wasmgen(dest);
    return ToNative(natreg, wasmgen, bytecode_buffer);
}

}

void unit_test_wasm(bool full) {
    auto vec = WASM::SimpleBinaryWriterTest();
    if (full) {
        auto f = OpenForWriting("simple_binary_writer_test.wasm", true);
        if (f) {
            fwrite(vec.data(), vec.size(), 1, f);
            fclose(f);
        }
    }
}
