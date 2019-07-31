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

// lobster.cpp : Defines the entry point for the console application.
//
#include "lobster/stdafx.h"

#include "lobster/lex.h"
#include "lobster/idents.h"
#include "lobster/node.h"

#include "lobster/compiler.h"

#include "lobster/parser.h"
#include "lobster/typecheck.h"
#include "lobster/optimizer.h"
#include "lobster/codegen.h"

namespace lobster {

const Type g_type_int(V_INT);
const Type g_type_float(V_FLOAT);
const Type g_type_string(V_STRING);
const Type g_type_any(V_ANY);
const Type g_type_vector_int(V_VECTOR, &g_type_int);
const Type g_type_vector_float(V_VECTOR, &g_type_float);
const Type g_type_function_null(V_FUNCTION);
const Type g_type_function_cocl(V_YIELD);
const Type g_type_coroutine(V_COROUTINE);
const Type g_type_resource(V_RESOURCE);
const Type g_type_typeid(V_TYPEID);
const Type g_type_void(V_VOID);
const Type g_type_function_void(V_VOID, &g_type_function_null);
const Type g_type_undefined(V_UNDEFINED);

TypeRef type_int = &g_type_int;
TypeRef type_float = &g_type_float;
TypeRef type_string = &g_type_string;
TypeRef type_any = &g_type_any;
TypeRef type_vector_int = &g_type_vector_int;
TypeRef type_vector_float = &g_type_vector_float;
TypeRef type_function_null = &g_type_function_null;
TypeRef type_function_cocl = &g_type_function_cocl;
TypeRef type_coroutine = &g_type_coroutine;
TypeRef type_resource = &g_type_resource;
TypeRef type_typeid = &g_type_typeid;
TypeRef type_void = &g_type_void;
TypeRef type_function_void = &g_type_function_void;
TypeRef type_undefined = &g_type_undefined;

const Type g_type_vector_any(V_VECTOR, &g_type_any);
const Type g_type_vector_string(V_VECTOR, &g_type_string);
const Type g_type_vector_vector_int(V_VECTOR, &g_type_vector_int);
const Type g_type_vector_vector_float(V_VECTOR, &g_type_vector_float);
const Type g_type_vector_vector_vector_float(V_VECTOR, &g_type_vector_vector_float);

TypeRef WrapKnown(TypeRef elem, ValueType with) {
    if (with == V_VECTOR) {
        switch (elem->t) {
            case V_ANY:    return &g_type_vector_any;
            case V_INT:    return elem->e ? nullptr : type_vector_int;
            case V_FLOAT:  return type_vector_float;
            case V_STRING: return &g_type_vector_string;
            case V_VECTOR: switch (elem->sub->t) {
                case V_INT:   return &g_type_vector_vector_int;
                case V_FLOAT: return &g_type_vector_vector_float;
                case V_VECTOR: switch (elem->sub->sub->t) {
                    case V_FLOAT: return &g_type_vector_vector_vector_float;
                    default: return nullptr;
                }
                default: return nullptr;
            }
            default: return nullptr;
        }
    } else if (with == V_NIL) {
        switch (elem->t) {
            case V_ANY:       { static const Type t(V_NIL, &g_type_any); return &t; }
            case V_INT:       { static const Type t(V_NIL, &g_type_int); return elem->e ? nullptr : &t; }
            case V_FLOAT:     { static const Type t(V_NIL, &g_type_float); return &t; }
            case V_STRING:    { static const Type t(V_NIL, &g_type_string); return &t; }
            case V_FUNCTION:  { static const Type t(V_NIL, &g_type_function_null); return &t; }
            case V_RESOURCE:  { static const Type t(V_NIL, &g_type_resource); return &t; }
            case V_COROUTINE: { static const Type t(V_NIL, &g_type_coroutine); return &t; }
            case V_VECTOR: switch (elem->sub->t) {
                case V_INT:    { static const Type t(V_NIL, &g_type_vector_int); return &t; }
                case V_FLOAT:  { static const Type t(V_NIL, &g_type_vector_float); return &t; }
                case V_STRING: { static const Type t(V_NIL, &g_type_vector_string); return &t; }
                default: return nullptr;
            }
            default: return nullptr;
        }
    } else {
        return nullptr;
    }
}

bool IsCompressed(string_view filename) {
    auto dot = filename.find_last_of('.');
    if (dot == string_view::npos) return false;
    auto ext = filename.substr(dot);
    return ext == ".lbc" || ext == ".lobster" || ext == ".materials";
}

static const uchar *magic = (uchar *)"LPAK";
static const size_t magic_size = 4;
static const size_t header_size = magic_size + sizeof(int64_t) * 3;
static const char *bcname = "bytecode.lbc";

template <typename T> int64_t LE(T x) { return flatbuffers::EndianScalar((int64_t)x); };

void BuildPakFile(string &pakfile, string &bytecode, set<string> &files) {
    // All offsets in 64bit, just in-case we ever want pakfiles > 4GB :)
    // Since we're building this in memory, they can only be created by a 64bit build.
    vector<int64_t> filestarts;
    vector<int64_t> namestarts;
    vector<int64_t> uncompressed;
    vector<string> filenames;
    auto add_file = [&](string_view buf, string_view filename) {
        filestarts.push_back(LE(pakfile.size()));
        filenames.push_back(string(filename));
        LOG_INFO("adding to pakfile: ", filename);
        if (IsCompressed(filename)) {
            string out;
            WEntropyCoder<true>((uchar *)buf.data(), buf.length(), buf.length(), out);
            pakfile += out;
            uncompressed.push_back(buf.length());
        } else {
            pakfile += buf;
            uncompressed.push_back(-1);
        }
    };
    // Start with a magic id, just for the hell of it.
    pakfile.insert(pakfile.end(), magic, magic + magic_size);
    // Bytecode always first entry.
    add_file(bytecode, bcname);
    // Followed by all files.
    files.insert("data/shaders/default.materials");  // If it hadn't already been added.
    string buf;
    for (auto &filename : files) {
        auto l = LoadFile(filename, &buf);
        if (l >= 0) {
            add_file(buf, filename);
        } else {
            vector<pair<string, int64_t>> dir;
            if (!ScanDir(filename, dir))
                THROW_OR_ABORT("cannot load file/dir for pakfile: " + filename);
            for (auto &[name, size] : dir) {
                auto fn = filename + name;
                if (size >= 0 && LoadFile(fn, &buf) >= 0)
                    add_file(buf, fn);
            }
        }
    }
    // Now we can write the directory, first the names:
    auto dirstart = LE(pakfile.size());
    for (auto &filename : filenames) {
        namestarts.push_back(LE(pakfile.size()));
        pakfile.insert(pakfile.end(), filename.c_str(), filename.c_str() + filename.length() + 1);
    }
    // Then the starting offsets and other data:
    pakfile.insert(pakfile.end(), (uchar *)uncompressed.data(),
        (uchar *)(uncompressed.data() + uncompressed.size()));
    pakfile.insert(pakfile.end(), (uchar *)filestarts.data(),
        (uchar *)(filestarts.data() + filestarts.size()));
    pakfile.insert(pakfile.end(), (uchar *)namestarts.data(),
        (uchar *)(namestarts.data() + namestarts.size()));
    auto num = LE(filestarts.size());
    // Finally the "header" (or do we call this a "tailer" ? ;)
    auto header_start = pakfile.size();
    auto version = LE(1);
    pakfile.insert(pakfile.end(), (uchar *)&num, (uchar *)(&num + 1));
    pakfile.insert(pakfile.end(), (uchar *)&dirstart, (uchar *)(&dirstart + 1));
    pakfile.insert(pakfile.end(), (uchar *)&version, (uchar *)(&version + 1));
    pakfile.insert(pakfile.end(), magic, magic + magic_size);
    assert(pakfile.size() - header_start == header_size);
    (void)header_start;
}

// This just loads the directory part of a pakfile such that subsequent LoadFile calls know how
// to load from it.
bool LoadPakDir(const char *lpak) {
    // This supports reading from a pakfile > 4GB even on a 32bit system! (as long as individual
    // files in it are <= 4GB).
    auto plen = LoadFile(lpak, nullptr, 0, 0);
    if (plen < 0) return false;
    string header;
    if (LoadFile(lpak, &header, plen - (int64_t)header_size, header_size) < 0 ||
        memcmp(header.c_str() + header_size - magic_size, magic, magic_size)) return false;
    auto read_unaligned64 = [](const void *p) {
        int64_t r;
        memcpy(&r, p, sizeof(int64_t));
        return LE(r);
    };
    auto num = (size_t)read_unaligned64(header.c_str());
    auto dirstart = read_unaligned64((int64_t *)header.c_str() + 1);
    auto version = read_unaligned64((int64_t *)header.c_str() + 2);
    if (version > 1) return false;
    if (dirstart > plen) return false;
    string dir;
    if (LoadFile(lpak, &dir, dirstart, plen - dirstart - (int64_t)header_size) < 0)
        return false;
    auto namestarts = (int64_t *)(dir.c_str() + dir.length()) - num;
    auto filestarts = namestarts - num;
    auto uncompressed = filestarts - num;
    for (size_t i = 0; i < num; i++) {
        auto name = string_view(dir.c_str() + (read_unaligned64(namestarts + i) - dirstart));
        auto off = read_unaligned64(filestarts + i);
        auto end = i < num + 1 ? read_unaligned64(filestarts + i + 1) : dirstart;
        auto len = end - off;
        LOG_INFO("pakfile dir: ", name, " : ", len);
        AddPakFileEntry(lpak, name, off, len, read_unaligned64(uncompressed + i));
    }
    return true;
}

bool LoadByteCode(string &bytecode) {
    if (LoadFile(bcname, &bytecode) < 0) return false;
    flatbuffers::Verifier verifier((const uchar *)bytecode.c_str(), bytecode.length());
    auto ok = bytecode::VerifyBytecodeFileBuffer(verifier);
    assert(ok);
    return ok;
}

void RegisterBuiltin(NativeRegistry &nfr, const char *name,
                     void (* regfun)(NativeRegistry &)) {
    LOG_DEBUG("subsystem: ", name);
    nfr.NativeSubSystemStart(name);
    regfun(nfr);
}

void DumpBuiltins(NativeRegistry &nfr, bool justnames, const SymbolTable &st) {
    string s;
    if (justnames) {
        for (auto nf : nfr.nfuns) { s += nf->name; s += "|"; }
        WriteFile("builtin_functions_names.txt", false, s);
        return;
    }
    s = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n"
        "<html>\n<head>\n<title>lobster builtin function reference</title>\n"
        "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n"
        "<style type=\"text/css\">"
        "table.a, tr.a, td.a {font-size: 10pt;border: 1pt solid #DDDDDD;"
        " border-Collapse: collapse; max-width:1200px}</style>\n"
        "</head>\n<body><center><table border=0><tr><td>\n<p>"
        "lobster builtin functions:"
        "(file auto generated by compiler, do not modify)</p>\n\n";
    int cursubsystem = -1;
    bool tablestarted = false;
    for (auto nf : nfr.nfuns) {
        if (nf->subsystemid != cursubsystem) {
            if (tablestarted) s += "</table>\n";
            tablestarted = false;
            s += cat("<h3>", nfr.subsystems[nf->subsystemid], "</h3>\n");
            cursubsystem = nf->subsystemid;
        }
        if (!tablestarted) {
            s += "<table class=\"a\" border=1 cellspacing=0 cellpadding=4>\n";
            tablestarted = true;
        }
        s += cat("<tr class=\"a\" valign=top><td class=\"a\"><tt><b>", nf->name, "</b>(");
        int last_non_nil = -1;
        for (auto [i, a] : enumerate(nf->args.v)) {
            if (a.type->t != V_NIL) last_non_nil = (int)i;
        }
        for (auto [i, a] : enumerate(nf->args.v)) {
            auto argname = nf->args.GetName(i);
            if (i) s +=  ", ";
            s += argname;
            s += "<font color=\"#666666\">";
            if (a.type->t != V_ANY) {
                s += ":";
                s += a.flags & NF_BOOL
                    ? "bool"
                    : TypeName(a.type->ElementIfNil(), a.fixed_len, &st);
            }
            s += "</font>";
            if (a.type->t == V_NIL && (int)i > last_non_nil)
                s += a.type->sub->Numeric() ? " = 0" : " = nil";
        }
        s += ")";
        if (nf->retvals.v.size()) {
            s += " -> ";
            for (auto [i, a] : enumerate(nf->retvals.v)) {
                s += "<font color=\"#666666\">";
                s += TypeName(a.type, a.fixed_len, &st);
                s += "</font>";
                if (i < nf->retvals.v.size() - 1) s += ", ";
            }
        }
        s += cat("</tt></td><td class=\"a\">", nf->help, "</td></tr>\n");
    }
    s += "</table>\n</td></tr></table></center></body>\n</html>\n";
    WriteFile("builtin_functions_reference.html", false, s);
}

void Compile(NativeRegistry &nfr, string_view fn, string_view stringsource, string &bytecode,
    string *parsedump, string *pakfile, bool dump_builtins, bool dump_names, bool return_value,
    int runtime_checks) {
    SymbolTable st;
    Parser parser(nfr, fn, st, stringsource);
    parser.Parse();
    TypeChecker tc(parser, st, return_value);
    // Optimizer is not optional, must always run at least one pass, since TypeChecker and CodeGen
    // rely on it culling const if-thens and other things.
    Optimizer opt(parser, st, tc);
    if (parsedump) *parsedump = parser.DumpAll(true);
    CodeGen cg(parser, st, return_value, runtime_checks);
    st.Serialize(cg.code, cg.code_attr, cg.type_table, cg.vint_typeoffsets, cg.vfloat_typeoffsets,
        cg.lineinfo, cg.sids, cg.stringtable, cg.speclogvars, bytecode, cg.vtables);
    if (pakfile) BuildPakFile(*pakfile, bytecode, parser.pakfiles);
    if (dump_builtins) DumpBuiltins(nfr, false, st);
    if (dump_names) DumpBuiltins(nfr, true, st);
}

Value CompileRun(VM &parent_vm, Value &source, bool stringiscode, const vector<string> &args) {
    string_view fn = stringiscode ? "string" : source.sval()->strv();  // fixme: datadir + sanitize?
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        auto vmargs = VMArgs {
            parent_vm.nfr, fn, {}, nullptr, nullptr, 0, args
        };
        Compile(parent_vm.nfr, fn, stringiscode ? source.sval()->strv() : string_view(),
                vmargs.bytecode_buffer, nullptr, nullptr, false, false, true, RUNTIME_ASSERT);
        #ifdef VM_COMPILED_CODE_MODE
            // FIXME: Sadly since we modify how the VM operates under compiled code, we can't run in
            // interpreted mode anymore.
            THROW_OR_ABORT(string("cannot execute bytecode in compiled mode"));
        #endif
        VM vm(std::move(vmargs));
        vm.EvalProgram();
        auto ret = vm.evalret;
        parent_vm.Push(Value(parent_vm.NewString(ret)));
        return Value();
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        parent_vm.Push(Value(parent_vm.NewString("nil")));
        return Value(parent_vm.NewString(s));
    }
    #endif
}

void AddCompiler(NativeRegistry &nfr) {  // it knows how to call itself!

nfr("compile_run_code", "code,args", "SS]", "SS?",
    "compiles and runs lobster source, sandboxed from the current program (in its own VM)."
    " the argument is a string of code. returns the return value of the program as a string,"
    " with an error string as second return value, or nil if none. using parse_data(),"
    " two program can communicate more complex data structures even if they don't have the same"
    " version of struct definitions.",
    [](VM &vm, Value &filename, Value &args) {
        return CompileRun(vm, filename, true, ValueToVectorOfStrings(args));
    });

nfr("compile_run_file", "filename,args", "SS]", "SS?",
    "same as compile_run_code(), only now you pass a filename.",
    [](VM &vm, Value &filename, Value &args) {
        return CompileRun(vm, filename, false, ValueToVectorOfStrings( args));
    });

}

void RegisterCoreLanguageBuiltins(NativeRegistry &nfr) {
    extern void AddBuiltins(NativeRegistry &nfr); RegisterBuiltin(nfr, "builtin",   AddBuiltins);
    extern void AddCompiler(NativeRegistry &nfr); RegisterBuiltin(nfr, "compiler",  AddCompiler);
    extern void AddFile(NativeRegistry &nfr);     RegisterBuiltin(nfr, "file",      AddFile);
    extern void AddReader(NativeRegistry &nfr);   RegisterBuiltin(nfr, "parsedata", AddReader);
}

VMArgs CompiledInit(int argc, char *argv[], const void *entry_point, const void *bytecodefb,
                    size_t static_size, const lobster::block_t *vtables, FileLoader loader,
                    NativeRegistry &nfr) {
    min_output_level = OUTPUT_INFO;
    InitPlatform("../../", "", false, loader);  // FIXME: path.
    auto vmargs = VMArgs {
        nfr, StripDirPart(argv[0]), {}, entry_point, bytecodefb, static_size, {},
        vtables, TraceMode::OFF
    };
    for (int arg = 1; arg < argc; arg++) { vmargs.program_args.push_back(argv[arg]); }
    return vmargs;
}

extern "C" int ConsoleRunCompiledCodeMain(int argc, char *argv[], const void *entry_point,
                                          const void *bytecodefb, size_t static_size,
                                          const lobster::block_t *vtables) {
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        NativeRegistry nfr;
        RegisterCoreLanguageBuiltins(nfr);
        lobster::VM vm(CompiledInit(argc, argv, entry_point, bytecodefb, static_size, vtables,
                                    DefaultLoadFile, nfr));
        vm.EvalProgram();
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        LOG_ERROR(s);
        return 1;
    }
    #endif
    return 0;
}

SubFunction::~SubFunction() { delete body; }

Field::~Field() { delete defaultval; }

Field::Field(const Field &o)
    : type(o.type), id(o.id), genericref(o.genericref),
      defaultval(o.defaultval ? o.defaultval->Clone() : nullptr) {}

}
