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

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#include "stdint.h"

#include "flatbuffers/idl.h"

#ifdef _WIN32
    #define VC_EXTRALEAN
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #ifndef __ANDROID__
        #include <glob.h>
    #endif
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace lobster {

template<typename T, bool B> T Read(VM &vm, iint i, const LString *s) {
    if ((uint64_t)i > (uint64_t)s->len - sizeof(T)) vm.IDXErr(i, s->len - sizeof(T), s);
    return ReadValLE<T, B>(s, i);
}

template<typename T, bool B> Value WriteVal(StackPtr &sp, VM &vm, const Value &str, const Value &idx,
                                            const Value &val) {
    auto i = idx.ival();
    if (i < 0) vm.IDXErr(i, 0, str.sval());
    Push(sp,  WriteValLE<T, B>(vm, str.sval(), i, val.ifval<T>()));
    return Value(i + ssizeof<T>());
}

template<bool B> Value WriteStr(StackPtr &sp, VM &vm, const Value &str, const Value &idx, LString *s,
                                iint extra) {
    auto i = idx.ival();
    if (i < 0) vm.IDXErr(i, 0, str.sval());
    Push(sp,  WriteMem<B>(vm, str.sval(), i, s->data(), s->len + extra));
    return Value(i + s->len + extra);
}

template<typename T, bool B> Value ReadVal(StackPtr &sp, VM &vm, const Value &str, const Value &idx) {
    auto i = idx.ival();
    auto val = Read<T, B>(vm, i, str.sval());
    Push(sp,  val);
    return Value(i + ssizeof<T>());
}

template<typename T, bool IF, bool OF, bool ST>
Value ReadField(VM &vm, const Value &str, const Value &idx, const Value &vidx, const Value &def) {
    auto i = idx.ival();
    auto vtable = Read<flatbuffers::soffset_t, false>(vm, i, str.sval());
    auto vi = i - vtable;
    auto vtable_size = Read<flatbuffers::voffset_t, false>(vm, vi, str.sval());
    auto vo = vidx.ival();
    if ((uint64_t)vo < (uint64_t)vtable_size) {
        auto field_offset = Read<flatbuffers::voffset_t, false>(vm, vi + vo, str.sval());
        if (field_offset) {
            auto start = i + field_offset;
            if constexpr (ST) return Value(start);
            auto val = Read<T, false>(vm, start, str.sval());
            if constexpr (OF) return Value (val + start);
            return Value(val);
        }
    }
    return def;
}

Value FieldPresent(VM &vm, const Value &str, const Value &idx, const Value &vidx) {
    auto i = idx.ival();
    auto vtable = Read<flatbuffers::soffset_t, false>(vm, i, str.sval());
    auto vi = i - vtable;
    auto vtable_size = Read<flatbuffers::voffset_t, false>(vm, vi, str.sval());
    auto vo = vidx.ival();
    if ((uint64_t)vo < (uint64_t)vtable_size) {
        auto field_offset = Read<flatbuffers::voffset_t, false>(vm, vi + vo, str.sval());
        if (field_offset) {
            return Value(true);
        }
    }
    return Value(false);
}

LString *GetString(VM &vm, iint fi, LString *buf) {
    if (fi) {
        auto len = Read<flatbuffers::uoffset_t, false>(vm, fi, buf);
        auto fdata = fi + ssizeof<flatbuffers::uoffset_t>();
        // Read zero terminator just to make sure all string data is in bounds.
        Read<char, false>(vm, fdata + len, buf);
        return vm.NewString(buf->strv().substr(fdata, len));
    } else {
        return vm.NewString(0);
    }
}

Value ParseSchemas(VM &vm, flatbuffers::Parser &parser, const Value &schema,
                   const Value &includes) {
    vector<string> dirs_storage;
    for (iint i = 0; i < includes.vval()->len; i++) {
        auto dir = flatbuffers::ConCatPathFileName(string(ProjectDir()),
                                                   string(includes.vval()->At(i).sval()->strv()));
        dirs_storage.push_back(dir);
    }
    vector<const char *> dirs;
    for (auto &dir : dirs_storage) dirs.push_back(dir.c_str());
    dirs.push_back(nullptr);
    if (!parser.Parse(schema.sval()->data(), dirs.data())) {
        return Value(vm.NewString(parser.error_));
    }
    return NilVal();
}

void AddFile(NativeRegistry &nfr) {

nfr("scan_folder", "folder,rel", "SB?", "S]?I]?",
    "returns two vectors representing all elements in a folder, the first vector containing all"
    " names, the second vector containing sizes in bytes (or -1 if a directory)."
    " set rel use a relative path, default is absolute."
    " Returns nil if folder couldn't be scanned.",
    [](StackPtr &sp, VM &vm, Value &fld, Value &rel) {
        vector<pair<string, int64_t>> dir;
        auto ok = rel.True()
            ? ScanDir(fld.sval()->strv(), dir)
            : ScanDirAbs(fld.sval()->strv(), dir);
        if (!ok) {
            Push(sp, NilVal());
            return NilVal();
        }
        auto nlist = (LVector *)vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);
        auto slist = (LVector *)vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_INT);
        for (auto &[name, size] : dir) {
            nlist->Push(vm, Value(vm.NewString(name)));
            slist->Push(vm, Value(size));
        }
        Push(sp, Value(nlist));
        return Value(slist);
    });

nfr("read_file", "file,textmode", "SI?", "S?",
    "returns the contents of a file as a string, or nil if the file can't be found."
    " you may use either \\ or / as path separators",
    [](StackPtr &, VM &vm, Value &file, Value &textmode) {
        string buf;
        auto l = LoadFile(file.sval()->strv(), &buf, 0, -1, textmode.False());
        if (l < 0) return NilVal();
        auto s = vm.NewString(buf);
        return Value(s);
    });

nfr("write_file", "file,contents,textmode", "SSI?", "B",
    "creates a file with the contents of a string, returns false if writing wasn't possible",
    [](StackPtr &, VM &, Value &file, Value &contents, Value &textmode) {
        auto ok = WriteFile(file.sval()->strv(), textmode.False(), contents.sval()->strv());
        return Value(ok);
    });

nfr("vector_to_buffer", "vec,width", "A]*I?", "S",
    "converts a vector of ints/floats (or structs of them) to a buffer, where"
    " each scalar is written with \"width\" bytes (1/2/4/8, default 4). Returns nil if the"
    " type couldn't be converted. Uses native endianness.",
    [](StackPtr &, VM &vm, Value &vec, Value &width) {
        auto v = vec.vval();
        auto &ti = vm.GetTypeInfo(v->SingleType(vm));
        if (ti.t != V_INT && ti.t != V_FLOAT)
            vm.Error("vector_to_buffer: non-numeric data");
        auto w = width.intval();
        if (!w) w = 4;
        if (w != 1 && w != 2 && w != 4 && w != 8)
            vm.Error("vector_to_buffer: width out of range");
        if (ti.t == V_FLOAT && (w == 1 || w == 2))
            vm.Error("vector_to_buffer: 8/16 floats not supported yet");
        auto nelems = v->len * v->width;
        auto s = vm.NewString(nelems * w);
        auto buf = (uint8_t *)s->data();
        if (ti.t == V_INT) {
            for (iint i = 0; i < nelems; i++) {
                auto x = v->AtSlot(i).ival();
                #if FLATBUFFERS_LITTLEENDIAN
                    memcpy(buf, &x, w);
                #else
                    memcpy(buf, (uint8_t *)&x + (8 - w), w);
                #endif
                buf += w;
            }
        } else {
            for (iint i = 0; i < nelems; i++) {
                auto x = v->AtSlot(i).fval();
                if (w == sizeof(double)) {
                    memcpy(buf, &x, sizeof(double));
                } else {
                    auto xf = (float)x;
                    memcpy(buf, &xf, sizeof(float));
                }
                buf += w;
            }
        }
        return Value(s);
    });

nfr("ensure_size", "string,size,char,extra", "SkIII?", "S",
    "ensures a string is at least size characters. if it is, just returns the existing"
    " string, otherwise returns a new string of that size (with optionally extra bytes"
    " added), with any new characters set to"
    " char. You can specify a negative size to mean relative to the end, i.e. new"
    " characters will be added at the start. ",
    [](StackPtr &, VM &vm, Value &str, Value &size, Value &c, Value &extra) {
        auto asize = std::abs(size.ival());
        return str.sval()->len >= asize
            ? str
            : Value(vm.ResizeString(str.sval(), asize + extra.ival(), c.intval(), size.ival() < 0));
    });

auto write_val_desc1 =
    "writes a value as little endian to a string at location i. Uses ensure_size to"
    " make the string twice as long (with extra 0 bytes) if no space. Returns"
    " new string if resized,"
    " and the index of the location right after where the value was written. The"
    " _back version writes relative to the end (and writes before the index)";
auto write_val_desc2 = "(see write_int64_le)";
#define WRITEOP(N, T, B, D, S) \
    nfr(#N, "string,i,val", "SkI" S, "SI", D, \
        [](StackPtr &sp, VM &vm, Value &str, Value &idx, Value &val) { \
            return WriteVal<T, B>(sp, vm, str, idx, val); \
        });
WRITEOP(write_int64_le, int64_t, false, write_val_desc1, "I")
WRITEOP(write_int32_le, int32_t, false, write_val_desc2, "I")
WRITEOP(write_int16_le, int16_t, false, write_val_desc2, "I")
WRITEOP(write_int8_le, int8_t, false, write_val_desc2, "I")
WRITEOP(write_float64_le, double, false, write_val_desc2, "F")
WRITEOP(write_float32_le, float, false, write_val_desc2, "F")
WRITEOP(write_int64_le_back, int64_t, true, write_val_desc2, "I")
WRITEOP(write_int32_le_back, int32_t, true, write_val_desc2, "I")
WRITEOP(write_int16_le_back, int16_t, true, write_val_desc2, "I")
WRITEOP(write_int8_le_back, int8_t, true, write_val_desc2, "I")
WRITEOP(write_float64_le_back, double, true, write_val_desc2, "F")
WRITEOP(write_float32_le_back, float, true, write_val_desc2, "F")

nfr("write_substring", "string,i,substr,nullterm", "SkISI", "SI",
    "writes a substring into another string at i (see also write_int64_le)",
    [](StackPtr &sp, VM &vm, Value &str, Value &idx, Value &val, Value &term) {
        return WriteStr<false>(sp, vm, str, idx, val.sval(), term.True());
    });

nfr("write_substring_back", "string,i,substr,nullterm", "SkISI", "SI",
    "",
    [](StackPtr &sp, VM &vm, Value &str, Value &idx, Value &val, Value &term) {
        return WriteStr<true>(sp, vm, str, idx, val.sval(), term.True());
    });

nfr("compare_substring", "string_a,i_a,string_b,i_b,len", "SISII", "I",
    "returns if the two substrings are equal (0), or a < b (-1) or a > b (1).",
    [](StackPtr &, VM &vm, Value &str1, Value &idx1, Value &str2, Value &idx2,
                                    Value &len) {
        auto s1 = str1.sval();
        auto s2 = str2.sval();
        auto i1 = idx1.ival();
        auto i2 = idx2.ival();
        auto l = len.ival();
        if (l < 0 || i1 < 0 || i2 < 0 || i1 + l > s1->len || i2 + l > s2->len)
            vm.Error("compare_substring: index out of bounds");
        auto eq = memcmp(s1->data() + i1, s2->data() + i2, l);
        return Value(eq);
    });

auto read_val_desc1 =
    "reads a value as little endian from a string at location i. The value must be within"
    " bounds of the string. Returns the value, and the index of the location right after where"
    " the value was read. The"
    " _back version reads relative to the end (and reads before the index)";
auto read_val_desc2 = "(see read_int64_le)";
#define READOP(N, T, B, D, S) \
    nfr(#N, "string,i", "SI", S "I", D, \
        [](StackPtr &sp, VM &vm, Value &str, Value &idx) { return ReadVal<T, B>(sp, vm, str, idx); });
READOP(read_int64_le, int64_t, false, read_val_desc1, "I")
READOP(read_int32_le, int32_t, false, read_val_desc2, "I")
READOP(read_int16_le, int16_t, false, read_val_desc2, "I")
READOP(read_int8_le, int8_t, false, read_val_desc2, "I")
READOP(read_uint64_le, uint64_t, false, read_val_desc1, "I")
READOP(read_uint32_le, uint32_t, false, read_val_desc2, "I")
READOP(read_uint16_le, uint16_t, false, read_val_desc2, "I")
READOP(read_uint8_le, uint8_t, false, read_val_desc2, "I")
READOP(read_float64_le, double, false, read_val_desc2, "F")
READOP(read_float32_le, float, false, read_val_desc2, "F")
READOP(read_int64_le_back, int64_t, true, read_val_desc2, "I")
READOP(read_int32_le_back, int32_t, true, read_val_desc2, "I")
READOP(read_int16_le_back, int16_t, true, read_val_desc2, "I")
READOP(read_int8_le_back, int8_t, true, read_val_desc2, "I")
READOP(read_uint64_le_back, uint64_t, true, read_val_desc2, "I")
READOP(read_uint32_le_back, uint32_t, true, read_val_desc2, "I")
READOP(read_uint16_le_back, uint16_t, true, read_val_desc2, "I")
READOP(read_uint8_le_back, uint8_t, true, read_val_desc2, "I")
READOP(read_float64_le_back, double, true, read_val_desc2, "F")
READOP(read_float32_le_back, float, true, read_val_desc2, "F")

auto read_field_desc1 =
    "reads a flatbuffers field from a string at table location tablei, field vtable offset vo,"
    " and default value def. The value must be within"
    " bounds of the string. Returns the value (or default if the field was not present)";
auto read_field_desc2 = "(see flatbuffers_field_int64)";
#define READFOP(N, T, D, S) \
    nfr(#N, "string,tablei,vo,def", "SII" S, S, D, \
        [](StackPtr &, VM &vm, Value &str, Value &idx, Value &vidx, Value &def) { \
            auto val = ReadField<T, S[0] == 'F', false, false>(vm, str, idx, vidx, def); \
            return Value(val); \
        });
READFOP(flatbuffers_field_int64, int64_t, read_field_desc1, "I")
READFOP(flatbuffers_field_int32, int32_t, read_field_desc2, "I")
READFOP(flatbuffers_field_int16, int16_t, read_field_desc2, "I")
READFOP(flatbuffers_field_int8, int8_t, read_field_desc2, "I")
READFOP(flatbuffers_field_uint64, uint64_t, read_field_desc1, "I")
READFOP(flatbuffers_field_uint32, uint32_t, read_field_desc2, "I")
READFOP(flatbuffers_field_uint16, uint16_t, read_field_desc2, "I")
READFOP(flatbuffers_field_uint8, uint8_t, read_field_desc2, "I")
READFOP(flatbuffers_field_float64, double, read_field_desc2, "F")
READFOP(flatbuffers_field_float32, float, read_field_desc2, "F")

nfr("flatbuffers_field_string", "string,tablei,vo", "SII", "S",
    "reads a flatbuffer string field, returns \"\" if not present",
    [](StackPtr &, VM &vm, Value &str, Value &idx, Value &vidx) {
        auto fi = ReadField<flatbuffers::uoffset_t, false, true, false>(vm, str, idx, vidx,
                                                                        Value(0)).ival();
        auto ret = Value(GetString(vm, fi, str.sval()));
        return ret;
    });

nfr("flatbuffers_field_vector_len", "string,tablei,vo", "SII", "I",
    "reads a flatbuffer vector field length, or 0 if not present",
    [](StackPtr &, VM &vm, Value &str, Value &idx, Value &vidx) {
        auto fi = ReadField<flatbuffers::uoffset_t, false, true, false>(vm, str, idx, vidx,
                                                                        Value(0)).ival();
        Value ret(fi ? Read<flatbuffers::uoffset_t, false>(vm, fi, str.sval()) : 0);
        return ret;
    });

nfr("flatbuffers_field_vector", "string,tablei,vo", "SII", "I",
    "returns a flatbuffer vector field element start, or 0 if not present",
    [](StackPtr &, VM &vm, Value &str, Value &idx, Value &vidx) {
        auto fi = ReadField<flatbuffers::uoffset_t, false, true, false>(vm, str, idx, vidx,
                                                                        Value(0)).ival();
        Value ret(fi ? fi + ssizeof<flatbuffers::uoffset_t>() : 0);
        return ret;
    });

nfr("flatbuffers_field_table", "string,tablei,vo", "SII", "I",
    "returns a flatbuffer table field start, or 0 if not present",
    [](StackPtr &, VM &vm, Value &str, Value &idx, Value &vidx) {
        auto ret = ReadField<flatbuffers::uoffset_t, false, true, false>(vm, str, idx, vidx,
                                                                         Value(0));
        return ret;
    });

nfr("flatbuffers_field_struct", "string,tablei,vo", "SII", "I",
    "returns a flatbuffer struct field start, or 0 if not present",
    [](StackPtr &, VM &vm, Value &str, Value &idx, Value &vidx) {
        auto ret = ReadField<flatbuffers::uoffset_t, false, false, true>(vm, str, idx, vidx,
                                                                         Value(0));
        return ret;
    });

nfr("flatbuffers_field_present", "string,tablei,vo", "SII", "B",
    "returns if a flatbuffer field is present (unequal to default)",
    [](StackPtr &, VM &vm, Value &str, Value &idx, Value &vidx) {
        return FieldPresent(vm, str, idx, vidx);
    });

nfr("flatbuffers_indirect", "string,index", "SI", "I",
    "returns a flatbuffer offset at index relative to itself",
    [](StackPtr &, VM &vm, Value &str, Value &idx) {
        auto off = Read<flatbuffers::uoffset_t, false>(vm, idx.ival(), str.sval());
        return Value(off + idx.ival());
    });

nfr("flatbuffers_string", "string,index", "SI", "S",
    "returns a flatbuffer string whose offset is at given index",
    [](StackPtr &, VM &vm, Value &str, Value &idx) {
        auto off = Read<flatbuffers::uoffset_t, false>(vm, idx.ival(), str.sval());
        auto ret = GetString(vm, off + idx.ival(), str.sval());
        return Value(ret);
    });

nfr("flatbuffers_binary_to_json", "schemas,binary,includedirs", "SSS]", "SS?",
    "returns a JSON string generated from the given binary and corresponding schema."
    "if there was an error parsing the schema, the error will be in the second return"
    "value, or nil for no error",
    [](StackPtr &sp, VM &vm, Value &schema, Value &binary, Value &includes) {
        flatbuffers::Parser parser;
        auto err = ParseSchemas(vm, parser, schema, includes);
        string json;
        if (err.False() && !GenerateText(parser, binary.sval()->data(), &json)) {
            err = vm.NewString("unable to generate text for FlatBuffer binary");
        }
        Push(sp, vm.NewString(json));
        return err;
    });

nfr("flatbuffers_json_to_binary", "schema,json,includedirs", "SSS]", "SS?",
    "returns a binary flatbuffer generated from the given json and corresponding schema."
    "if there was an error parsing the schema, the error will be in the second return"
    "value, or nil for no error",
    [](StackPtr &sp, VM &vm, Value &schema, Value &json, Value &includes) {
        flatbuffers::Parser parser;
        auto err = ParseSchemas(vm, parser, schema, includes);
        string binary;
        if (err.False()) {
            if (!parser.Parse(json.sval()->data())) {
                err = vm.NewString(parser.error_);
            } else {
                binary.assign((const char *)parser.builder_.GetBufferPointer(),
                                parser.builder_.GetSize());
            }
        }
        Push(sp, vm.NewString(binary));
        return err;
    });

}  // AddFile

}
