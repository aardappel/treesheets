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

string ToWASM(NativeRegistry &natreg, vector<uint8_t> &dest, string_view bytecode_buffer) {
    WASM::BinaryWriter bw(dest);
    map<int, int> function_ids;  // FIXME: remove the need for this.
    struct Block {
        bool forward;
        int id;
        NativeHint h;
    };
    vector<Block> blocks;
    int cur_block = -1;
    NativeHint last_conditional_hint = NH_NONE;

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

    auto bcf = bytecode::GetBytecodeFile(bytecode_buffer.data());
    if (!FLATBUFFERS_LITTLEENDIAN) return "native code gen requires little endian";
    auto code = (const int *)bcf->bytecode()->Data();  // Assumes we're on a little-endian machine.
    auto function_lookup = CreateFunctionLookUp(bcf);
    auto len = bcf->bytecode()->Length();
    auto ip = code;
    // Skip past 1st jump.
    assert(*ip == IL_JUMP);
    ip++;

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
    #define S_ARGS0 TI_I_II
    #define S_ARGS1 TI_I_III
    #define S_ARGS2 TI_I_IIII
    #define S_ARGS3 TI_I_IIIII
    #define S_ARGS9 TI_I_III  // ILUNKNOWN
    #define S_ARGSN(N) S_ARGS##N
    #define C_ARGS0 TI_I_III
    #define C_ARGS1 TI_I_IIII
    #define C_ARGS2 TI_I_IIIII
    #define C_ARGS3 TI_I_IIIIII
    #define C_ARGS9 TI_I_IIII  // ILUNKNOWN
    #define C_ARGSN(N) C_ARGS##N
    #define F(N, A, USE, DEF) bw.AddImportLinkFunction("CVM_" #N, S_ARGSN(A));
        ILBASENAMES
    #undef F
    #define F(N, A, USE, DEF) bw.AddImportLinkFunction("CVM_" #N, C_ARGSN(A));
        ILCALLNAMES
    #undef F
    #define F(N, A, USE, DEF) bw.AddImportLinkFunction("CVM_" #N, S_ARGSN(A));
        ILVARARGNAMES
    #undef F
    #define F(N, A, USE, DEF) bw.AddImportLinkFunction("CVM_" #N, TI_I_II);
        ILJUMPNAMES1
    #undef F
    #define F(N, A, USE, DEF) bw.AddImportLinkFunction("CVM_" #N, TI_I_III);
        ILJUMPNAMES2
    #undef F
    size_t import_erccm = bw.AddImportLinkFunction("RunCompiledCodeMain", TI_I_IIIII);
    size_t import_gnct = bw.AddImportLinkFunction("CVM_GetNextCallTarget", TI_I_I);
    size_t import_drop = bw.AddImportLinkFunction("CVM_Drop", TI_I_I);
    size_t import_gl_frame = bw.AddImportLinkFunction("GLFrame", TI_I_II);
    bw.EndSection(WASM::Section::Import);

    bw.BeginSection(WASM::Section::Function);
    bw.AddFunction(TI_I_II);  // main(), defined function 0.
    bw.AddFunction(TI_I_II);  // compiled_entry_point(), defined function 1.
    // All functions follow here, which have id's 2..
    int num_emitted_functions = 2;

    auto starting_ip = code + *ip++;
    int starting_point = -1;
    while (ip < code + len) {
        int id = (int)(ip - code);
        if (*ip == IL_FUNSTART || ip == starting_ip) {
            bw.AddFunction(TI_I_II);
            function_ids[id] = (int)bw.GetNumFunctionImports() + num_emitted_functions++;
            starting_point = id;
        }
        int opc = *ip++;
        assert(opc >= 0 && opc < IL_MAX_OPS);
        ParseOpAndGetArity(opc, ip);
    }

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
    bw.EmitI32ConstDataRef(1, 0);  // Bytecode, for data refs.
    bw.EmitI32Const((int)bytecode_buffer.size());
    bw.EmitI32ConstDataRef(0, 0);  // vtables.
    bw.EmitCall(import_erccm);
    bw.EmitEndFunction();

    // Emit compiled_entry_point
    bw.AddCode({}, "compiled_entry_point", false);
    bw.EmitGetLocal(0 /*VM*/);
    bw.EmitGetLocal(1 /*sp*/);
    bw.EmitCall(function_ids[starting_point]);
    bw.EmitEndFunction();

    ip = code + 2;  // Past first IL_JUMP.
    while (ip < code + len) {
        int id = (int)(ip - code);
        int opc = *ip++;
        if (opc == IL_FUNSTART || ip - 1 == starting_ip) {
            auto it = function_lookup.find(id);
            auto f = it != function_lookup.end() ? it->second : nullptr;
            bw.AddCode({}, "fun_" + std::to_string(id) +
                           (f ? "_" + f->name()->string_view() : ""), true);
        }
        auto args = ip;
        auto arity = ParseOpAndGetArity(opc, ip);
        auto is_vararg = ILArity()[opc] == ILUNKNOWN;
        if (opc == IL_JUMP) {
            auto id = args[0];
            int nesting = 0;
            for (auto &b : reverse(blocks)) {
                if (id <= cur_block) {  // Backwards.
                    if (!b.forward) {
                        assert(b.h == NH_LOOP_BACK);
                        bw.EmitBr(nesting);
                        goto done;
                    }
                } else {  // Forwards.
                    // This is a break, or the jump skipping an "else" or switch-cases.
                    if (b.h == NH_JUMPOUT_START) {
                        // skipping an "else" or switch-cases.
                        bw.EmitBr(nesting);
                        goto done;
                    } else if (!b.forward) {
                        // A break.
                        assert(b.h == NH_LOOP_BACK);
                        bw.EmitBr(nesting + 1);  // The block around the loop.
                        goto done;
                    }
                }
                nesting++;
            }
            assert(false);  // Jump target not found!
            done:;
        } else if ((opc >= IL_JUMPFAIL && opc <= IL_JUMPNOFAILR) ||
                   (opc >= IL_IFOR && opc <= IL_VFOR) ||
                   opc == IL_JUMPIFUNWOUND) {
            auto id = args[opc == IL_JUMPIFUNWOUND ? 1 : 0];
            assert(id >= 0 && id > cur_block);  // Only forward currently.
            auto df = opc == IL_JUMPIFUNWOUND ? args[0] : -1;
            // FIXME: this is very clumsy, shorten this for common cases!
            size_t blockdepth = 0;
            if (last_conditional_hint != NH_NONE) {
                for (auto &b : reverse(blocks)) {
                    if (b.h == last_conditional_hint - 1) goto found;
                    blockdepth++;
                }
                assert(false);  // block not found!
                found:
                last_conditional_hint = NH_NONE;
            } else {
                bw.EmitBlock(WASM::VOID);
                blocks.push_back({ true, id, NH_COND_JUMP });
            }
            bw.EmitGetLocal(0 /*VM*/);
            bw.EmitGetLocal(1 /*SP*/);
            if (df >= 0) bw.EmitI32Const(df);
            bw.EmitCall((size_t)opc);
            bw.EmitTeeLocal(1 /*SP*/);
            bw.EmitI64Load(0);  // False if we should jump.
            bw.EmitGetLocal(1 /*SP*/);
            bw.EmitCall(import_drop);  // FIXME: don't know sizeof(Value) here (!)
            bw.EmitSetLocal(1 /*SP*/);
            bw.EmitI64Const(0);
            bw.EmitI64Eq();
            bw.EmitBrIf(blockdepth);
        } else if (opc == IL_JUMP_TABLE) {
            auto mini = *args++;
            auto maxi = *args++;
            auto n = maxi - mini + 2;
            vector<size_t> targets;
            set<int> block_order;
            size_t default_target = 0;
            // Find the unique block targets we're going to have, in sorted order
            // (same order as the bytecode).
            for (int i = 0; i < n; i++) {
                block_order.insert(args[i]);
            }
            // Now fill in the block nesting counts into the jump table.
            for (int i = 0; i < n; i++) {
                auto block_it = block_order.find(args[i]);
                assert(block_it != block_order.end());
                auto depth = std::distance(block_order.begin(), block_it);  // FIXME: inefficient.
                if (i < n - 1) targets.push_back(depth);
                else default_target = depth;
            }
            // Emit the start of all blocks.
            for (auto id : reverse(block_order)) {
                bw.EmitBlock(WASM::VOID);
                blocks.push_back({ true, id, NH_JUMPTABLE_TO_CASE });
            }
            // Fixme: reduced the cost of this.
            bw.EmitGetLocal(1 /*SP*/);
            bw.EmitI64Load(0);  // Value to switch on
            bw.EmitGetLocal(1 /*SP*/);
            bw.EmitCall(import_drop);  // FIXME: don't know sizeof(Value) here (!)
            bw.EmitSetLocal(1 /*SP*/);
            bw.EmitI64Const(mini);
            bw.EmitI64Sub();
            bw.EmitI32WrapI64();
            bw.EmitBrTable(targets, default_target);
        } else if (opc == IL_NATIVEHINT) {
            auto h = (NativeHint)args[0];
            switch (h) {
                case NH_BLOCK_START:
                case NH_JUMPTABLE_CASE_START:
                    // Terminate previous blocks.
                    while (!blocks.empty()) {
                        auto &b = blocks.back();
                        assert(b.id == -1 || b.id >= id);
                        if (b.id != id) break;
                        bw.EmitEnd();
                        blocks.pop_back();
                    }
                    cur_block = id;
                    break;
                case NH_JUMPTABLE_END:
                    break;
                case NH_LOOP_BACK:
                    bw.EmitBlock(WASM::VOID);
                    bw.EmitLoop(WASM::VOID);
                    blocks.push_back({ false, -1, h });
                    break;
                case NH_LOOP_REMOVE:
                    assert(!blocks.empty() && blocks.back().h == NH_LOOP_BACK);
                    bw.EmitEnd();
                    bw.EmitEnd();
                    blocks.pop_back();
                    break;
                // absolute forward jumps.
                case NH_JUMPOUT_START:
                    bw.EmitBlock(WASM::VOID);
                    blocks.push_back({ true, -1, h });
                    break;
                case NH_JUMPOUT_END:
                    assert(!blocks.empty() && blocks.back().h == NH_JUMPOUT_START);
                    bw.EmitEnd();
                    blocks.pop_back();
                    break;
                // conditional labelled forward jumps.
                case NH_SWITCH_RANGE_BLOCK:
                case NH_SWITCH_THISCASE_BLOCK:
                case NH_SWITCH_NEXTCASE_BLOCK:
                    bw.EmitBlock(WASM::VOID);
                    blocks.push_back({ true, -1, h });
                    break;
                case NH_SWITCH_RANGE_END:
                case NH_SWITCH_THISCASE_END:
                case NH_SWITCH_NEXTCASE_END:
                    assert(!blocks.empty() && blocks.back().h == h - 2);
                    bw.EmitEnd();
                    blocks.pop_back();
                    break;
                case NH_SWITCH_RANGE_JUMP:
                case NH_SWITCH_THISCASE_JUMP:
                case NH_SWITCH_NEXTCASE_JUMP:
                    last_conditional_hint = h;
                    break;
            }
        } else if (ISBCALL(opc) && natreg.nfuns[args[0]]->IsGLFrame()) {
            bw.EmitGetLocal(0 /*VM*/);
            bw.EmitGetLocal(1 /*SP*/);
            bw.EmitCall(import_gl_frame);
            bw.EmitSetLocal(1 /*SP*/);
        } else {
            bw.EmitGetLocal(0 /*VM*/);
            bw.EmitGetLocal(1 /*SP*/);
            if (is_vararg) {
                if (arity) bw.EmitI32ConstDataRef(1, (const char *)args - bytecode_buffer.data());
                else bw.EmitI32Const(0);  // nullptr
            }
            int target = -1;
            if (opc == IL_PUSHFUN) {
                target = args[0];
            }
            if (!is_vararg) {
                for (int i = 0; i < arity; i++) bw.EmitI32Const(args[i]);
            }
            if (target >= 0) { bw.EmitI32ConstFunctionRef(function_ids[target]); }
            bw.EmitCall((size_t)opc);  // Opcodes are the 0..N of imports.
            bw.EmitSetLocal(1 /*SP*/);

            auto EmitCallIndirect = [&]() {
                bw.EmitGetLocal(0 /*VM*/);
                bw.EmitGetLocal(1 /*SP*/);
                bw.EmitGetLocal(0 /*VM*/);
                bw.EmitCall(import_gnct);
                bw.EmitCallIndirect(TI_I_II);
                bw.EmitSetLocal(1 /*SP*/);
            };

            if (opc == IL_CALL) {
                bw.EmitGetLocal(0 /*VM*/);
                bw.EmitGetLocal(1 /*SP*/);
                bw.EmitCall(function_ids[args[0]]);
                bw.EmitSetLocal(1 /*SP*/);
            } else if (opc == IL_CALLV || opc == IL_DDCALL) {
                EmitCallIndirect();
            } else if (opc == IL_RETURN || opc == IL_RETURNANY) {
                bw.EmitGetLocal(1 /*SP*/);
                bw.EmitReturn();
            } else if (opc == IL_CALLVCOND) {
                bw.EmitGetLocal(0 /*VM*/);
                bw.EmitCall(import_gnct);
                bw.EmitIf(WASM::VOID);
                EmitCallIndirect();
                bw.EmitEnd();
            }
        }
        if (opc == IL_EXIT || opc == IL_ABORT) {
            bw.EmitGetLocal(1 /*SP*/);
            bw.EmitReturn();
        }
        if (ip == code + len || *ip == IL_FUNSTART || ip == starting_ip) {
            bw.EmitEndFunction();
            assert(blocks.empty());
        }
    }
    bw.EndSection(WASM::Section::Code);

    bw.BeginSection(WASM::Section::Data);
    vector<int> wid;
    for (auto id : *bcf->vtables()) {
        wid.push_back(id >= 0 ? function_ids[id] : -1);
    }
    bw.AddData(string_view((char *)wid.data(), wid.size() * sizeof(int)), "vtables", sizeof(int));
    int i = 0;
    for (auto id : *bcf->vtables()) {
        if (id >= 0) bw.DataFunctionRef(function_ids[id], i * sizeof(int));
        i++;
    }
    // TODO: don't really want to store all of this.
    bw.AddData(bytecode_buffer, "static_data", 16);
    bw.EndSection(WASM::Section::Data);

    bw.Finish();
    return "";
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
