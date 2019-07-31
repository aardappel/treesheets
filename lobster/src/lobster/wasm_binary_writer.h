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

#ifndef WASM_BINARY_WRITER_H
#define WASM_BINARY_WRITER_H

#include "assert.h"
#include "cstring"
#include "vector"
#include "string"
#include "string_view"

// Stand-alone single header WASM module writer class.
// Takes care of the "heavy lifting" of generating the binary format
// correctly, and provide a friendly code generation API, that may
// be useful outside of Lobster as well.

// Documentation and example of the API in this file:
// http://aardappel.github.io/lobster/implementation_wasm.html
// (and see also test cases in wasm_binary_writer_test.h)
// Main use of this API in towasm.cpp.

namespace WASM {

enum class Section {
    None = -1,
    Custom = 0,
    Type,
    Import,
    Function,
    Table,
    Memory,
    Global,
    Export,
    Start,
    Element,
    Code,
    Data,
};

enum {
    I32 = 0x7F,
    I64 = 0x7E,
    F32 = 0x7D,
    F64 = 0x7C,
    ANYFUNC = 0x70,
    FUNC = 0x60,
    VOID = 0x40,
};

class BinaryWriter {
    std::vector<uint8_t> &buf;
    Section cur_section = Section::None;
    Section last_known_section = Section::None;
    size_t section_size = 0;
    size_t section_count = 0;
    size_t section_data = 0;
    size_t section_index_in_file = 0;
    size_t section_index_in_file_code = 0;
    size_t section_index_in_file_data = 0;
    size_t segment_payload_start = 0;
    size_t num_function_imports = 0;
    size_t num_global_imports = 0;
    size_t num_function_decls = 0;
    struct Function {
        std::string name;
        bool import;
        bool local;
    };
    std::vector<Function> function_symbols;
    size_t function_body_start = 0;
    size_t data_section_size = 0;
    struct DataSegment {
        std::string name;
        size_t align;
        size_t size;
        bool local;
    };
    std::vector<DataSegment> data_segments;
    struct Reloc {
        uint8_t type;
        size_t src_offset;    // Where we are doing the relocation.
        size_t sym_index;     // If is_function.
        size_t target_index;  // Index inside thing we're referring to, e.g. addend.
        bool is_function;
    };
    std::vector<Reloc> code_relocs;
    std::vector<Reloc> data_relocs;

    template<typename T> void UInt8(T v) {
        buf.push_back(static_cast<uint8_t>(v));
    }

    template<typename T> void UInt16(T v) {
        UInt8(v & 0xFF);
        UInt8(v >> 8);
    }

    template<typename T> void UInt32(T v) {
        UInt16(v & 0xFFFF);
        UInt16(v >> 16);
    }

    template<typename T> void UInt64(T v) {
        UInt32(v & 0xFFFFFFFF);
        UInt32(v >> 32);
    }

    template<typename T, typename U> U Bits(T v) {
        static_assert(sizeof(T) == sizeof(U), "");
        U u;
        memcpy(&u, &v, sizeof(T));
        return u;
    }

    template<typename T> void ULEB(T v) {
        for (;;) {
            UInt8(v & 0x7F);
            v = (T)(v >> 7);
            if (!v) break;
            buf.back() |= 0x80;
        }
    }

    template<typename T> void SLEB(T v) {
        auto negative = v < 0;
        for (;;) {
            UInt8(v & 0x7F);
            auto sign = v & 0x40;
            v = (T)(v >> 7);
            if (negative) v |= T(0x7F) << (sizeof(T) * 8 - 7);
            if ((!v && !sign) || (v == -1 && sign)) break;
            buf.back() |= 0x80;
        }
    }

    enum { PATCHABLE_ULEB_SIZE = 5 };

    size_t PatchableLEB() {
        auto pos = buf.size();
        for (size_t i = 0; i < PATCHABLE_ULEB_SIZE - 1; i++) UInt8(0x80);
        UInt8(0x00);
        return pos;
    }

    void PatchULEB(size_t pos, size_t v) {
        for (size_t i = 0; i < PATCHABLE_ULEB_SIZE; i++) {
            buf[pos + i] |= v & 0x7F;
            v >>= 7;
        }
        assert(!v);
    }

    void Chars(std::string_view chars) {
        for (auto c : chars) UInt8(c);
    }

    size_t LenChars(std::string_view chars) {
        ULEB(chars.size());
        auto pos = buf.size();
        Chars(chars);
        return pos;
    }

    bool StartsWithCount() {
        return cur_section != Section::Custom &&
               cur_section != Section::Start;
    }

    enum {
        R_WASM_FUNCTION_INDEX_LEB = 0,
        R_WASM_TABLE_INDEX_SLEB = 1,
        R_WASM_TABLE_INDEX_I32 = 2,
        R_WASM_MEMORY_ADDR_LEB = 3,
        R_WASM_MEMORY_ADDR_SLEB = 4,
        R_WASM_MEMORY_ADDR_I32 = 5,
        R_WASM_TYPE_INDEX_LEB = 6,
        R_WASM_GLOBAL_INDEX_LEB = 7,
        R_WASM_FUNCTION_OFFSET_I32 = 8,
        R_WASM_SECTION_OFFSET_I32 = 9,
        R_WASM_EVENT_INDEX_LEB = 10,
    };


    void RelocULEB(uint8_t reloc_type, size_t sym_index, size_t target_index, bool is_function) {
        code_relocs.push_back({ reloc_type,
                                buf.size() - section_data,
                                sym_index,
                                target_index,
                                is_function });
        // A relocatable LEB typically can be 0, since all information about
        // this value is stored in the relocation itself. But putting
        // a meaningful value here will help with reading the output of
        // objdump.
        PatchULEB(PatchableLEB(), is_function ? sym_index : target_index);
    }

  public:

    explicit BinaryWriter(std::vector<uint8_t> &dest) : buf(dest) {
        Chars(std::string_view("\0asm", 4));
        UInt32(1);
    }

    // Call Begin/EndSection pairs for each segment type, in order.
    // In between, call the Add functions below corresponding to the section
    // type.
    void BeginSection(Section st, std::string_view name = "") {
        // Call EndSection before calling another BeginSection.
        assert(cur_section == Section::None);
        cur_section = st;
        if (st == Section::Code)
            section_index_in_file_code = section_index_in_file;
        if (st == Section::Data)
            section_index_in_file_data = section_index_in_file;
        UInt8(st);
        section_size = PatchableLEB();
        if (st == Section::Custom) {
            LenChars(name);
        } else {
            // Known sections must be created in order and only once.
            assert(st > last_known_section);
            last_known_section = st;
        }
        section_count = 0;
        section_data = buf.size();
        if (StartsWithCount()) PatchableLEB();
    }

    void EndSection(Section st) {
        assert(cur_section == st);
        (void)st;
        // Most sections start with a "count" field.
        if (StartsWithCount()) {
            PatchULEB(section_data, section_count);
        }
        // Patch up the size of this section.
        PatchULEB(section_size, buf.size() - section_size - PATCHABLE_ULEB_SIZE);
        cur_section = Section::None;
        section_index_in_file++;
    }

    size_t AddType(const std::vector<unsigned> &params, const std::vector<unsigned> &returns) {
        assert(cur_section == Section::Type);
        ULEB(FUNC);
        ULEB(params.size());
        for (auto p : params) ULEB(p);
        ULEB(returns.size());
        for (auto r : returns) ULEB(r);
        return section_count++;
    }

    enum {
        EXTERNAL_FUNCTION,
        EXTERNAL_TABLE,
        EXTERNAL_MEMORY,
        EXTERNAL_GLOBAL,
    };

    size_t AddImportLinkFunction(std::string_view name, size_t tidx) {
        LenChars("");  // Module, unused.
        LenChars(name);
        ULEB(EXTERNAL_FUNCTION);
        ULEB(tidx);
        function_symbols.push_back({ std::string(name), true, true });
        section_count++;
        return num_function_imports++;
    }

    size_t AddImportGlobal(std::string_view name, uint8_t type, bool is_mutable) {
        LenChars("");  // Module, unused.
        LenChars(name);
        ULEB(EXTERNAL_GLOBAL);
        UInt8(type);
        ULEB<unsigned>(is_mutable);
        section_count++;
        return num_global_imports++;
    }

    size_t GetNumFunctionImports() { return num_function_imports; }
    size_t GetNumGlobalImports() { return num_global_imports; }

    void AddFunction(size_t tidx) {
        assert(cur_section == Section::Function);
        ULEB(tidx);
        num_function_decls++;
        section_count++;
    }

    size_t GetNumDefinedFunctions() { return num_function_decls; }

    void AddTable() {
        assert(cur_section == Section::Table);
        UInt8(WASM::ANYFUNC);  // Currently only option.
        ULEB(0);  // Flags: no maximum.
        ULEB(0);  // Initial length.
        section_count++;
    }

    void AddMemory(size_t initial_pages) {
        assert(cur_section == Section::Memory);
        ULEB(0);  // Flags: no maximum.
        ULEB(initial_pages);
        section_count++;
    }

    // You MUST emit an init exp after calling this, e.g. EmitI32Const(0); EmitEnd();
    void AddGlobal(uint8_t type, bool is_mutable) {
      assert(cur_section == Section::Global);
      UInt8(type);
      ULEB<unsigned>(is_mutable);
      section_count++;
    }

    void AddExportFunction(std::string_view name, size_t fidx) {
        assert(cur_section == Section::Export);
        LenChars(name);
        ULEB(EXTERNAL_FUNCTION);
        ULEB(fidx);
    }

    void AddExportGlobal(std::string_view name, size_t gidx) {
        assert(cur_section == Section::Export);
        LenChars(name);
        ULEB(EXTERNAL_GLOBAL);
        ULEB(gidx);
    }

    void AddStart(size_t fidx) {
        assert(cur_section == Section::Start);
        ULEB(fidx);
    }

    // Simple 1:1 mapping of function ids.
    // TODO: add more flexible Element functions later.
    void AddElementAllFunctions() {
        assert(cur_section == Section::Element);
        ULEB(0);  // Table index, always 0 for now.
        EmitI32Const(0);  // Offset.
        EmitEnd();
        auto total_funs = num_function_imports + num_function_decls;
        ULEB(total_funs);
        for (size_t i = 0; i < total_funs; i++) ULEB(i);
        section_count++;
    }

    // After calling this, use the Emit Functions below to add to the function body,
    // and be sure to end with EmitEndFunction.
    void AddCode(const std::vector<unsigned> &locals, std::string_view name,
                 bool local) {
        assert(cur_section == Section::Code);
        assert(!function_body_start);
        function_body_start = PatchableLEB();
        std::vector<std::pair<unsigned, unsigned>> entries;
        for (auto l : locals) {
            if (entries.empty() || entries.back().second != l) {
                entries.emplace_back(std::pair { 1, l });
            } else {
                entries.back().first++;
            }
        }
        ULEB(entries.size());
        for (auto &e : entries) {
            ULEB(e.first);
            ULEB(e.second);
        }
        function_symbols.push_back({ std::string(name), false, local });
        section_count++;
    }

    // --- CONTROL FLOW ---

    void EmitUnreachable() { UInt8(0x00); }
    void EmitNop() { UInt8(0x01); }
    void EmitBlock(uint8_t block_type) { UInt8(0x02); UInt8(block_type); }
    void EmitLoop(uint8_t block_type) { UInt8(0x03); UInt8(block_type); }
    void EmitIf(uint8_t block_type) { UInt8(0x04); UInt8(block_type); }
    void EmitElse() { UInt8(0x05); }
    void EmitEnd() { UInt8(0x0B); }
    void EmitBr(size_t relative_depth) { UInt8(0x0C); ULEB(relative_depth); }
    void EmitBrIf(size_t relative_depth) { UInt8(0x0D); ULEB(relative_depth); }
    void EmitBrTable(const std::vector<size_t> &targets, size_t default_target) {
        UInt8(0x0E);
        ULEB(targets.size());
        for (auto t : targets) ULEB(t);
        ULEB(default_target);
    }
    void EmitReturn() { UInt8(0x0F); }

    // --- CALL OPERATORS ---

    // fun_idx is 0..N-1 imports followed by N..M-1 defined functions.
    void EmitCall(size_t fun_idx) {
        UInt8(0x10);
        RelocULEB(R_WASM_FUNCTION_INDEX_LEB, fun_idx, 0, true);
    }

    void EmitCallIndirect(size_t type_index) {
        UInt8(0x11);
        RelocULEB(R_WASM_TYPE_INDEX_LEB, 0, type_index, false);
        ULEB(0);
    }

    // --- PARAMETRIC OPERATORS

    void EmitDrop() { UInt8(0x1A); }
    void EmitSelect() { UInt8(0x1B); }

    // --- VARIABLE ACCESS ---

    void EmitGetLocal(size_t local) { UInt8(0x20); ULEB(local); }
    void EmitSetLocal(size_t local) { UInt8(0x21); ULEB(local); }
    void EmitTeeLocal(size_t local) { UInt8(0x22); ULEB(local); }
    void EmitGetGlobal(size_t global) { UInt8(0x23); ULEB(global); }
    void EmitSetGlobal(size_t global) { UInt8(0x24); ULEB(global); }

    // --- MEMORY ACCESS ---

    void EmitI32Load(size_t off, size_t flags = 2) { UInt8(0x28); ULEB(flags); ULEB(off); }
    void EmitI64Load(size_t off, size_t flags = 3) { UInt8(0x29); ULEB(flags); ULEB(off); }
    void EmitF32Load(size_t off, size_t flags = 2) { UInt8(0x2A); ULEB(flags); ULEB(off); }
    void EmitF64Load(size_t off, size_t flags = 3) { UInt8(0x2B); ULEB(flags); ULEB(off); }
    void EmitI32Load8S(size_t off, size_t flags = 0) { UInt8(0x2C); ULEB(flags); ULEB(off); }
    void EmitI32Load8U(size_t off, size_t flags = 0) { UInt8(0x2D); ULEB(flags); ULEB(off); }
    void EmitI32Load16S(size_t off, size_t flags = 1) { UInt8(0x2E); ULEB(flags); ULEB(off); }
    void EmitI32Load16U(size_t off, size_t flags = 1) { UInt8(0x2F); ULEB(flags); ULEB(off); }
    void EmitI64Load8S(size_t off, size_t flags = 0) { UInt8(0x30); ULEB(flags); ULEB(off); }
    void EmitI64Load8U(size_t off, size_t flags = 0) { UInt8(0x31); ULEB(flags); ULEB(off); }
    void EmitI64Load16S(size_t off, size_t flags = 1) { UInt8(0x32); ULEB(flags); ULEB(off); }
    void EmitI64Load16U(size_t off, size_t flags = 1) { UInt8(0x33); ULEB(flags); ULEB(off); }
    void EmitI64Load32S(size_t off, size_t flags = 2) { UInt8(0x34); ULEB(flags); ULEB(off); }
    void EmitI64Load32U(size_t off, size_t flags = 2) { UInt8(0x35); ULEB(flags); ULEB(off); }

    void EmitI32Store(size_t off, size_t flags = 2) { UInt8(0x36); ULEB(flags); ULEB(off); }
    void EmitI64Store(size_t off, size_t flags = 3) { UInt8(0x37); ULEB(flags); ULEB(off); }
    void EmitF32Store(size_t off, size_t flags = 2) { UInt8(0x38); ULEB(flags); ULEB(off); }
    void EmitF64Store(size_t off, size_t flags = 3) { UInt8(0x39); ULEB(flags); ULEB(off); }
    void EmitI32Store8(size_t off, size_t flags = 0) { UInt8(0x3A); ULEB(flags); ULEB(off); }
    void EmitI32Store16(size_t off, size_t flags = 1) { UInt8(0x3B); ULEB(flags); ULEB(off); }
    void EmitI64Store8(size_t off, size_t flags = 0) { UInt8(0x3C); ULEB(flags); ULEB(off); }
    void EmitI64Store16(size_t off, size_t flags = 1) { UInt8(0x3D); ULEB(flags); ULEB(off); }
    void EmitI64Store32(size_t off, size_t flags = 2) { UInt8(0x3E); ULEB(flags); ULEB(off); }

    void EmitCurrentMemory() { UInt8(0x3F); ULEB(0); }
    void EmitGrowMemory() { UInt8(0x40); ULEB(0); }

    // --- CONSTANTS ---

    void EmitI32Const(int32_t v) { UInt8(0x41); SLEB(v); }
    void EmitI64Const(int64_t v) { UInt8(0x42); SLEB(v); }
    void EmitF32Const(float v) { UInt8(0x43); UInt32(Bits<float, uint32_t>(v)); }
    void EmitF64Const(double v) { UInt8(0x44); UInt64(Bits<double, uint64_t>(v)); }

    // Getting the address of data in a data segment, encoded as a i32.const + reloc.
    void EmitI32ConstDataRef(size_t segment, size_t addend) {
        UInt8(0x41);
        RelocULEB(R_WASM_MEMORY_ADDR_SLEB, segment, addend, false );
    }

    // fun_idx is 0..N-1 imports followed by N..M-1 defined functions.
    void EmitI32ConstFunctionRef(size_t fun_idx) {
        UInt8(0x41);
        RelocULEB(R_WASM_TABLE_INDEX_SLEB, fun_idx, 0, true);
    }

    // --- COMPARISON OPERATORS ---

    void EmitI32Eqz() { UInt8(0x45); }
    void EmitI32Eq() { UInt8(0x46); }
    void EmitI32Ne() { UInt8(0x47); }
    void EmitI32LtS() { UInt8(0x48); }
    void EmitI32LtU() { UInt8(0x49); }
    void EmitI32GtS() { UInt8(0x4A); }
    void EmitI32GtU() { UInt8(0x4B); }
    void EmitI32LeS() { UInt8(0x4C); }
    void EmitI32LeU() { UInt8(0x4D); }
    void EmitI32GeS() { UInt8(0x4E); }
    void EmitI32GeU() { UInt8(0x4F); }

    void EmitI64Eqz() { UInt8(0x50); }
    void EmitI64Eq() { UInt8(0x51); }
    void EmitI64Ne() { UInt8(0x52); }
    void EmitI64LtS() { UInt8(0x53); }
    void EmitI64LtU() { UInt8(0x54); }
    void EmitI64GtS() { UInt8(0x55); }
    void EmitI64GtU() { UInt8(0x56); }
    void EmitI64LeS() { UInt8(0x57); }
    void EmitI64LeU() { UInt8(0x58); }
    void EmitI64GeS() { UInt8(0x59); }
    void EmitI64GeU() { UInt8(0x5A); }

    void EmitF32Eq() { UInt8(0x5B); }
    void EmitF32Ne() { UInt8(0x5C); }
    void EmitF32Lt() { UInt8(0x5D); }
    void EmitF32Gt() { UInt8(0x5E); }
    void EmitF32Le() { UInt8(0x5F); }
    void EmitF32Ge() { UInt8(0x60); }

    void EmitF64Eq() { UInt8(0x61); }
    void EmitF64Ne() { UInt8(0x62); }
    void EmitF64Lt() { UInt8(0x63); }
    void EmitF64Gt() { UInt8(0x64); }
    void EmitF64Le() { UInt8(0x65); }
    void EmitF64Ge() { UInt8(0x66); }

    // --- NUMERIC OPERATORS

    void EmitI32Clz() { UInt8(0x67); }
    void EmitI32Ctz() { UInt8(0x68); }
    void EmitI32PopCnt() { UInt8(0x69); }
    void EmitI32Add() { UInt8(0x6A); }
    void EmitI32Sub() { UInt8(0x6B); }
    void EmitI32Mul() { UInt8(0x6C); }
    void EmitI32DivS() { UInt8(0x6D); }
    void EmitI32DivU() { UInt8(0x6E); }
    void EmitI32RemS() { UInt8(0x6F); }
    void EmitI32RemU() { UInt8(0x70); }
    void EmitI32And() { UInt8(0x71); }
    void EmitI32Or() { UInt8(0x72); }
    void EmitI32Xor() { UInt8(0x73); }
    void EmitI32Shl() { UInt8(0x74); }
    void EmitI32ShrS() { UInt8(0x75); }
    void EmitI32ShrU() { UInt8(0x76); }
    void EmitI32RotL() { UInt8(0x77); }
    void EmitI32RotR() { UInt8(0x78); }

    void EmitI64Clz() { UInt8(0x79); }
    void EmitI64Ctz() { UInt8(0x7A); }
    void EmitI64PopCnt() { UInt8(0x7B); }
    void EmitI64Add() { UInt8(0x7C); }
    void EmitI64Sub() { UInt8(0x7D); }
    void EmitI64Mul() { UInt8(0x7E); }
    void EmitI64DivS() { UInt8(0x7F); }
    void EmitI64DivU() { UInt8(0x80); }
    void EmitI64RemS() { UInt8(0x81); }
    void EmitI64RemU() { UInt8(0x82); }
    void EmitI64And() { UInt8(0x83); }
    void EmitI64Or() { UInt8(0x84); }
    void EmitI64Xor() { UInt8(0x85); }
    void EmitI64Shl() { UInt8(0x86); }
    void EmitI64ShrS() { UInt8(0x87); }
    void EmitI64ShrU() { UInt8(0x88); }
    void EmitI64RotL() { UInt8(0x89); }
    void EmitI64RotR() { UInt8(0x8A); }

    void EmitF32Abs() { UInt8(0x8B); }
    void EmitF32Neg() { UInt8(0x8C); }
    void EmitF32Ceil() { UInt8(0x8D); }
    void EmitF32Floor() { UInt8(0x8E); }
    void EmitF32Trunc() { UInt8(0x8F); }
    void EmitF32Nearest() { UInt8(0x90); }
    void EmitF32Sqrt() { UInt8(0x91); }
    void EmitF32Add() { UInt8(0x92); }
    void EmitF32Sub() { UInt8(0x93); }
    void EmitF32Mul() { UInt8(0x94); }
    void EmitF32Div() { UInt8(0x95); }
    void EmitF32Min() { UInt8(0x96); }
    void EmitF32Max() { UInt8(0x97); }
    void EmitF32CopySign() { UInt8(0x98); }

    void EmitF64Abs() { UInt8(0x99); }
    void EmitF64Neg() { UInt8(0x9A); }
    void EmitF64Ceil() { UInt8(0x9B); }
    void EmitF64Floor() { UInt8(0x9C); }
    void EmitF64Trunc() { UInt8(0x9D); }
    void EmitF64Nearest() { UInt8(0x9E); }
    void EmitF64Sqrt() { UInt8(0x9F); }
    void EmitF64Add() { UInt8(0xA0); }
    void EmitF64Sub() { UInt8(0xA1); }
    void EmitF64Mul() { UInt8(0xA2); }
    void EmitF64Div() { UInt8(0xA3); }
    void EmitF64Min() { UInt8(0xA4); }
    void EmitF64Max() { UInt8(0xA5); }
    void EmitF64CopySign() { UInt8(0xA6); }

    // --- CONVERSION OPERATORS ---

    void EmitI32WrapI64() { UInt8(0xA7); }
    void EmitI32TruncSF32() { UInt8(0xA8); }
    void EmitI32TruncUF32() { UInt8(0xA9); }
    void EmitI32TruncSF64() { UInt8(0xAA); }
    void EmitI32TruncUF64() { UInt8(0xAB); }
    void EmitI64ExtendSI32() { UInt8(0xAC); }
    void EmitI64ExtendUI32() { UInt8(0xAD); }
    void EmitI64TruncSF32() { UInt8(0xAE); }
    void EmitI64TruncUF32() { UInt8(0xAF); }
    void EmitI64TruncSF64() { UInt8(0xB0); }
    void EmitI64TruncUF64() { UInt8(0xB1); }
    void EmitF32ConvertSI32() { UInt8(0xB2); }
    void EmitF32ConvertUI32() { UInt8(0xB3); }
    void EmitF32ConvertSI64() { UInt8(0xB4); }
    void EmitF32ConvertUI64() { UInt8(0xB5); }
    void EmitF32DemoteF64() { UInt8(0xB6); }
    void EmitF64ConvertSI32() { UInt8(0xB7); }
    void EmitF64ConvertUI32() { UInt8(0xB8); }
    void EmitF64ConvertSI64() { UInt8(0xB9); }
    void EmitF64ConvertUI64() { UInt8(0xBA); }
    void EmitF64PromoteF32() { UInt8(0xBB); }

    // --- REINTERPRETATIONS ---

    void EmitI32ReinterpretF32() { UInt8(0xBC); }
    void EmitI64ReinterpretF64() { UInt8(0xBD); }
    void EmitF32ReinterpretI32() { UInt8(0xBE); }
    void EmitF64ReinterpretI64() { UInt8(0xBF); }

    // --- END FUNCTION ---

    void EmitEndFunction() {
        assert(cur_section == Section::Code);
        EmitEnd();
        assert(function_body_start);
        PatchULEB(function_body_start,
            buf.size() - function_body_start - PATCHABLE_ULEB_SIZE);
        function_body_start = 0;
    }

    void AddData(std::string_view data, std::string_view symbol, size_t align,
                 bool local = true) {
        assert(cur_section == Section::Data);
        ULEB(0);  // Linear memory index.
        // Init exp: must use 32-bit for wasm32 target.
        EmitI32Const(static_cast<int32_t>(data_section_size));
        EmitEnd();
        segment_payload_start = LenChars(data);
        data_section_size += data.size();
        data_segments.push_back({ std::string(symbol), align, data.size(), local });
        section_count++;
    }

    // "off" is relative to the data in the last AddData call.
    void DataFunctionRef(size_t fid, size_t off) {
        assert(segment_payload_start);
        data_relocs.push_back({ R_WASM_TABLE_INDEX_I32,
                                off + (segment_payload_start - section_data),
                                fid,
                                0,
                                true });
    }

    // Call this last, to finalize the buffer into a valid WASM module,
    // and to add linking/reloc sections based on the previous sections.
    void Finish() {
        assert(cur_section == Section::None);
        // If this assert fails, you likely have not matched the number of
        // AddFunction calls in a Function section with the number of AddCode
        // calls in a Code section.
        assert(num_function_imports + num_function_decls == function_symbols.size());
        // Linking section.
        {
            BeginSection(Section::Custom, "linking");
            ULEB(2);  // Version.
            enum {
                WASM_SEGMENT_INFO = 5,
                WASM_INIT_FUNCS = 6,
                WASM_COMDAT_INFO = 7,
                WASM_SYMBOL_TABLE = 8,
            };
            // Segment Info.
            {
                UInt8(WASM_SEGMENT_INFO);
                auto sisize = PatchableLEB();
                ULEB(data_segments.size());
                for (auto &ds : data_segments) {
                    LenChars(ds.name);
                    ULEB(ds.align);
                    ULEB(0);  // Flags. FIXME: any valid values?
                }
                PatchULEB(sisize, buf.size() - sisize - PATCHABLE_ULEB_SIZE);
            }
            // Symbol Table.
            {
                UInt8(WASM_SYMBOL_TABLE);
                auto stsize = PatchableLEB();
                enum {
                    SYMTAB_FUNCTION = 0,
                    SYMTAB_DATA = 1,
                    SYMTAB_GLOBAL = 2,
                    SYMTAB_SECTION = 3,
                    SYMTAB_EVENT = 4,
                };
                enum {
                    WASM_SYM_BINDING_WEAK = 1,
                    WASM_SYM_BINDING_LOCAL = 2,
                    WASM_SYM_VISIBILITY_HIDDEN = 4,
                    WASM_SYM_UNDEFINED = 16,
                    WASM_SYM_EXPORTED = 32,
                };
                ULEB(data_segments.size() + function_symbols.size());
                size_t segi = 0;
                for (auto &ds : data_segments) {
                    UInt8(SYMTAB_DATA);
                    ULEB(ds.local ? WASM_SYM_BINDING_LOCAL : WASM_SYM_EXPORTED);
                    LenChars(ds.name);
                    ULEB(segi++);
                    ULEB(0);  // Offset in segment, always 0 (1 seg per sym).
                    ULEB(ds.size);
                }
                size_t wasm_function = 0;
                for (auto &fs : function_symbols) {
                    UInt8(SYMTAB_FUNCTION);
                    ULEB(fs.import
                         ? WASM_SYM_UNDEFINED
                         : (fs.local ? WASM_SYM_BINDING_LOCAL
                                     : WASM_SYM_EXPORTED));
                    ULEB(wasm_function++);
                    if (!fs.import) {
                        LenChars(fs.name);
                    }
                }
                PatchULEB(stsize, buf.size() - stsize - PATCHABLE_ULEB_SIZE);
            }
            EndSection(Section::Custom);  // linking
        }
        // Reloc sections
        {
            auto EncodeReloc = [&](Reloc &r) {
                UInt8(r.type);
                ULEB(r.src_offset);
                ULEB(r.sym_index + (r.is_function ? data_segments.size() : 0));
                if (r.type == R_WASM_MEMORY_ADDR_LEB ||
                    r.type == R_WASM_MEMORY_ADDR_SLEB ||
                    r.type == R_WASM_MEMORY_ADDR_I32 ||
                    r.type == R_WASM_FUNCTION_OFFSET_I32 ||
                    r.type == R_WASM_SECTION_OFFSET_I32)
                    SLEB((ptrdiff_t)r.target_index);
            };

            BeginSection(Section::Custom, "reloc.CODE");
            ULEB(section_index_in_file_code);
            ULEB(code_relocs.size());
            for (auto &r : code_relocs) EncodeReloc(r);
            EndSection(Section::Custom);  // reloc.CODE

            BeginSection(Section::Custom, "reloc.DATA");
            ULEB(section_index_in_file_data);
            ULEB(data_relocs.size());
            for (auto &r : data_relocs) EncodeReloc(r);
            EndSection(Section::Custom);  // reloc.DATA
        }
    }

};

}  // namespace WASM

#endif  // WASM_BINARY_WRITER_H
