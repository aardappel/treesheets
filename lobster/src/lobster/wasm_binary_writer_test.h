// Copyright 2019 Wouter van Oortmerssen. All rights reserved.
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

#ifndef WASM_BINARY_WRITER_TEST_H
#define WASM_BINARY_WRITER_TEST_H

#include "wasm_binary_writer.h"

using namespace std::string_view_literals;

namespace WASM {

// This is a simple test of the instruction encoding. The function returns a binary that
// when written to a file should pass e.g. wasm-validate.
std::vector<uint8_t> SimpleBinaryWriterTest() {
    std::vector<uint8_t> vec;
    BinaryWriter bw(vec);
    // Write a (function) type section, to be referred to by functions below.
    // For any of these sections, if you write them out of order, or don't match
    // begin/end, you'll get an assert.
    // As with everything, to refer to things in wasm, use a 0 based index.
    bw.BeginSection(WASM::Section::Type);
    // A list of arguments followed by a list of return values.
    // You don't have to use the return value, but it may make referring to this
    // type easier.
    auto type_ii_i = bw.AddType({ WASM::I32, WASM::I32 }, { WASM::I32 });  // 0
    auto type_i_v = bw.AddType({ WASM::I32 }, {});  // 1
    bw.EndSection(WASM::Section::Type);

    // Import some functions, from the runtime compiled in other modules.
    // For our example that will just be the printing function.
    // Note: we assume this function has been declared with: extern "C"
    // You can link against C++ functions as well if you don't mind dealing
    // with name mangling.
    bw.BeginSection(WASM::Section::Import);
    auto import_print = bw.AddImportLinkFunction("print", type_i_v);  // 0
    bw.EndSection(WASM::Section::Import);

    // Declare all the functions we will generate. Note this is just the type,
    // the body of the code will follow below.
    bw.BeginSection(WASM::Section::Function);
    bw.AddFunction(type_ii_i);  // main()
    bw.AddFunction(type_i_v);  // TestAllInstructions()
    bw.EndSection(WASM::Section::Function);

    // We need this (and Element below) to be able to use call_indirect.
    bw.BeginSection(WASM::Section::Table);
    bw.AddTable();
    bw.EndSection(WASM::Section::Table);

    // Declare the linear memory we want to use, with 1 initial page.
    bw.BeginSection(WASM::Section::Memory);
    bw.AddMemory(1);
    bw.EndSection(WASM::Section::Memory);

    // Declare a global, used in the tests below.
    bw.BeginSection(WASM::Section::Global);
    bw.AddGlobal(WASM::I32, true);
    bw.EmitI32Const(0);
    bw.EmitEnd();
    bw.EndSection(WASM::Section::Global);

    // Here we'd normally declare a "Start" section, but the linker will
    // take care for that for us.

    // This initializes the Table declared above. Needed for call_indirect.
    // For now we use a utility function that maps all functions ids 1:1 to the table.
    bw.BeginSection(WASM::Section::Element);
    bw.AddElementAllFunctions();
    bw.EndSection(WASM::Section::Element);

    // Now the exciting part: emitting function bodies.
    bw.BeginSection(WASM::Section::Code);

    // A list of 0 local types,
    bw.AddCode({}, "main", false);
    // Refers to data segment 0 at offset 0 below. This emits an i32.const
    // instruction, whose immediate value will get relocated to refer to the
    // data correctly.
    bw.EmitI32ConstDataRef(0, 0);
    bw.EmitCall(import_print);
    bw.EmitI32Const(0);  // Return value.
    bw.EmitEndFunction();

    // Test each instruction at least once. Needs to have correct stack inputs to pass
    // wasm-validate etc, but is otherwise not meant to be meaningfull to execute.
    bw.AddCode({}, "TestAllInstructions", false);
    // Control flow.
    bw.EmitNop();
    bw.EmitBlock(WASM::VOID);
        bw.EmitI32Const(true); bw.EmitBrIf(0);
        bw.EmitBr(0);
    bw.EmitEnd();
    bw.EmitLoop(WASM::VOID);
        bw.EmitI32Const(false); bw.EmitBrIf(0);
    bw.EmitEnd();
    bw.EmitI32Const(false); bw.EmitIf(WASM::I32);
        bw.EmitI32Const(1);
    bw.EmitElse();
        bw.EmitI32Const(2);
    bw.EmitEnd();
    bw.EmitDrop();
    bw.EmitBlock(WASM::VOID);
        bw.EmitI32Const(2);
        bw.EmitBrTable({}, 0);
    bw.EmitEnd();
    bw.EmitI32Const(0);
    bw.EmitI32ConstFunctionRef(import_print);
    bw.EmitCallIndirect(type_i_v);
    bw.EmitI32Const(1); bw.EmitI32Const(2); bw.EmitI32Const(true); bw.EmitSelect();
    // Variables.
    bw.EmitGetLocal(0);
    bw.EmitTeeLocal(0);
    bw.EmitSetLocal(0);
    bw.EmitGetGlobal(0);
    bw.EmitSetGlobal(0);
    // Memory.
    for (int i = 0; i < 14; i++) bw.EmitI32Const(0);  // Address.
    bw.EmitI32Load(0); bw.EmitDrop();
    bw.EmitI64Load(0); bw.EmitDrop();
    bw.EmitF32Load(0); bw.EmitDrop();
    bw.EmitF64Load(0); bw.EmitDrop();
    bw.EmitI32Load8S(0); bw.EmitDrop();
    bw.EmitI32Load8U(0); bw.EmitDrop();
    bw.EmitI32Load16S(0); bw.EmitDrop();
    bw.EmitI32Load16U(0); bw.EmitDrop();
    bw.EmitI64Load8S(0); bw.EmitDrop();
    bw.EmitI64Load8U(0); bw.EmitDrop();
    bw.EmitI64Load16S(0); bw.EmitDrop();
    bw.EmitI64Load16U(0); bw.EmitDrop();
    bw.EmitI64Load32S(0); bw.EmitDrop();
    bw.EmitI64Load32U(0); bw.EmitDrop();
    for (int i = 0; i < 11; i++) bw.EmitI32Const(0);  // Address.
    bw.EmitI32Const(0); bw.EmitI32Store(0);
    bw.EmitI64Const(0); bw.EmitI64Store(0);
    bw.EmitF32Const(0); bw.EmitF32Store(0);
    bw.EmitF64Const(0); bw.EmitF64Store(0);
    bw.EmitI32Const(0); bw.EmitI32Store8(0);
    bw.EmitI32Const(0); bw.EmitI32Store16(0);
    bw.EmitI64Const(0); bw.EmitI64Store8(0);
    bw.EmitI64Const(0); bw.EmitI64Store16(0);
    bw.EmitI64Const(0); bw.EmitI64Store32(0);
    bw.EmitCurrentMemory();
    bw.EmitDrop();
    bw.EmitGrowMemory();
    // Equality operators.
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Eqz(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Eq(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Ne(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32LtS(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32LtU(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32GtS(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32GtU(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32LeS(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32LeU(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32GeS(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32GeU(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Eqz(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Eq(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Ne(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64LtS(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64LtU(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64GtS(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64GtU(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64LeS(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64LeU(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64GeS(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64GeU(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Eq(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Ne(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Lt(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Gt(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Le(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Ge(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Eq(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Ne(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Lt(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Gt(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Le(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Ge(); bw.EmitDrop();
    // Numeric operators.
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Clz(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Ctz(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32PopCnt(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Add(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Sub(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Mul(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32DivS(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32DivU(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32RemS(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32RemU(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32And(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Or(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Xor(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32Shl(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32ShrS(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32ShrU(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32RotL(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI32Const(0); bw.EmitI32RotR(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Clz(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Ctz(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64PopCnt(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Add(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Sub(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Mul(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64DivS(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64DivU(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64RemS(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64RemU(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64And(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Or(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Xor(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64Shl(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64ShrS(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64ShrU(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64RotL(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitI64Const(0); bw.EmitI64RotR(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Abs(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Neg(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Ceil(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Floor(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Trunc(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Nearest(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Sqrt(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Add(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Sub(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Mul(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Div(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Min(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32Max(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF32Const(0); bw.EmitF32CopySign(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Abs(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Neg(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Ceil(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Floor(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Trunc(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Nearest(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Sqrt(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Add(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Sub(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Mul(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Div(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Min(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64Max(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF64Const(0); bw.EmitF64CopySign(); bw.EmitDrop();
    // Coversion operations.
    bw.EmitI64Const(0); bw.EmitI32WrapI64(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitI32TruncSF32(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitI32TruncUF32(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitI32TruncSF64(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitI32TruncUF64(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI64ExtendSI32(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitI64ExtendUI32(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitI64TruncSF32(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitI64TruncUF32(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitI64TruncSF64(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitI64TruncUF64(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitF32ConvertSI32(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitF32ConvertUI32(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitF32ConvertSI64(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitF32ConvertUI64(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitF32DemoteF64(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitF64ConvertSI32(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitF64ConvertUI32(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitF64ConvertSI64(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitF64ConvertUI64(); bw.EmitDrop();
    bw.EmitF32Const(0); bw.EmitF64PromoteF32(); bw.EmitDrop();
    // Reinterpretations.
    bw.EmitF32Const(0); bw.EmitI32ReinterpretF32(); bw.EmitDrop();
    bw.EmitF64Const(0); bw.EmitI64ReinterpretF64(); bw.EmitDrop();
    bw.EmitI32Const(0); bw.EmitF32ReinterpretI32(); bw.EmitDrop();
    bw.EmitI64Const(0); bw.EmitF64ReinterpretI64(); bw.EmitDrop();
    // More control flow.
    bw.EmitReturn();
    bw.EmitUnreachable();
    bw.EmitEndFunction();

    bw.EndSection(WASM::Section::Code);

    // Add all our static data.
    bw.BeginSection(WASM::Section::Data);

    // This is our first segment, we referred to this above as 0.
    auto hello = "Hello, World\n\0"sv;
    // Data, name, and alignment.
    bw.AddData(hello, "hello", 0);

    // Create another segment, this time with function references.
    int function_ref = (int)bw.GetNumFunctionImports() + 0;  // Refers to main()
    bw.AddData(std::string_view((char *)&function_ref, sizeof(int)), "funids", sizeof(int));
    bw.DataFunctionRef(function_ref, 0);  // Reloc it.

    bw.EndSection(WASM::Section::Data);

    // This call does all the remaining work of generating the linking
    // information, and wrapping up the file.
    bw.Finish();
    return vec;
}

}  // namespace WASM

#endif  // WASM_BINARY_WRITER_TEST_H
