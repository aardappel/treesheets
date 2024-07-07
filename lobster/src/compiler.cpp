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

#include "lobster/il.h"
#include "lobster/natreg.h"

#include "lobster/lex.h"
#include "lobster/idents.h"
#include "lobster/node.h"

#include "lobster/compiler.h"

#include "lobster/parser.h"
#include "lobster/typecheck.h"
#include "lobster/constval.h"
#include "lobster/optimizer.h"
#include "lobster/codegen.h"
#include "lobster/tonative.h"

SlabAlloc *g_current_slaballoc = nullptr;

namespace lobster {

const Type g_type_int(V_INT);
const Type g_type_float(V_FLOAT);
const Type g_type_string(V_STRING);
const Type g_type_any(V_ANY);
const Type g_type_vector_any(V_VECTOR, &g_type_any);
const Type g_type_vector_int(V_VECTOR, &g_type_int);
const Type g_type_vector_float(V_VECTOR, &g_type_float);
const Type g_type_function_null_void(V_FUNCTION);  // no args, void return.
const Type g_type_resource(V_RESOURCE);
const Type g_type_vector_resource(V_VECTOR, &g_type_resource);
const Type g_type_typeid(V_TYPEID, &g_type_any);
const Type g_type_void(V_VOID);
const Type g_type_undefined(V_UNDEFINED);

TypeRef type_int = &g_type_int;
TypeRef type_float = &g_type_float;
TypeRef type_string = &g_type_string;
TypeRef type_any = &g_type_any;
TypeRef type_vector_int = &g_type_vector_int;
TypeRef type_vector_float = &g_type_vector_float;
TypeRef type_function_null_void = &g_type_function_null_void;
TypeRef type_resource = &g_type_resource;
TypeRef type_vector_resource = &g_type_vector_resource;
TypeRef type_typeid = &g_type_typeid;
TypeRef type_void = &g_type_void;
TypeRef type_undefined = &g_type_undefined;

const Type g_type_vector_string(V_VECTOR, &g_type_string);
const Type g_type_vector_vector_int(V_VECTOR, &g_type_vector_int);
const Type g_type_vector_vector_float(V_VECTOR, &g_type_vector_float);
const Type g_type_vector_vector_vector_float(V_VECTOR, &g_type_vector_vector_float);

ResourceType *g_resource_type_list = nullptr;

TypeRef WrapKnown(TypeRef elem, ValueType with) {
    if (with == V_VECTOR) {
        switch (elem->t) {
            case V_ANY:      return &g_type_vector_any;
            case V_INT:      return elem->e ? nullptr : type_vector_int;
            case V_FLOAT:    return type_vector_float;
            case V_STRING:   return &g_type_vector_string;
            case V_RESOURCE: return &elem->rt->thistypevec;
            case V_VECTOR:   switch (elem->sub->t) {
                case V_INT:    return elem->sub->e ? nullptr : &g_type_vector_vector_int;
                case V_FLOAT:  return &g_type_vector_vector_float;
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
            //case V_FUNCTION:  { static const Type t(V_NIL, &g_type_function_null); return &t; }
            case V_RESOURCE:  { return &elem->rt->thistypenil; }
            case V_VECTOR: switch (elem->sub->t) {
                case V_INT:    { static const Type t(V_NIL, &g_type_vector_int); return elem->sub->e ? nullptr : &t; }
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
    return ext == ".lbc" || ext == ".lobster" || ext == ".materials" || ext == ".glsl";
}

static const uint8_t *magic = (uint8_t *)"LPAK";
static const size_t magic_size = 4;
static const size_t header_size = magic_size + sizeof(int64_t) * 4;
static const char *bcname = "bytecode.lbc";
static const int64_t current_version = 2;

template <typename T> int64_t LE(T x) { return flatbuffers::EndianScalar((int64_t)x); };

string BuildPakFile(string &pakfile, string &bytecode, set<string> &files, uint64_t src_hash) {
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
            WEntropyCoder<true>((uint8_t *)buf.data(), buf.length(), buf.length(), out);
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
    function<string(const string &)> addrec;
    addrec = [&](const string &filename) -> string {
        auto l = LoadFile(filename, &buf);
        if (l >= 0) {
            add_file(buf, filename);
        } else {
            auto base = filename;
            auto pat = string{};
            auto pos = filename.find("#");
            if (pos != filename.npos) {
                pat = filename.substr(pos + 1);
                base = filename.substr(0, pos);
            }
            vector<pair<string, int64_t>> dir;
            if (!ScanDir(base, dir)) return "cannot load file/dir for pakfile: " + filename;
            for (auto &[name, size] : dir) {
                if (!pat.empty() && name.find(pat) == name.npos) continue;
                auto fn = base;
                if (fn.back() != '/') fn += "/";
                fn += name;
                auto err = addrec(fn);
                if (!err.empty()) return err;
            }
        }
        return "";
    };
    for (auto &filename : files) {
        auto err = addrec(filename);
        if (!err.empty()) return err;
    }
    // Now we can write the directory, first the names:
    auto dirstart = LE(pakfile.size());
    for (auto &filename : filenames) {
        namestarts.push_back(LE(pakfile.size()));
        pakfile.insert(pakfile.end(), filename.c_str(), filename.c_str() + filename.length() + 1);
    }
    // Then the starting offsets and other data:
    pakfile.insert(pakfile.end(), (uint8_t *)uncompressed.data(),
        (uint8_t *)(uncompressed.data() + uncompressed.size()));
    pakfile.insert(pakfile.end(), (uint8_t *)filestarts.data(),
        (uint8_t *)(filestarts.data() + filestarts.size()));
    pakfile.insert(pakfile.end(), (uint8_t *)namestarts.data(),
        (uint8_t *)(namestarts.data() + namestarts.size()));
    auto num = LE(filestarts.size());
    // Finally the "header" (or do we call this a "tailer" ? ;)
    auto header_start = pakfile.size();
    auto version = LE(current_version);
    auto src_hash_le = LE(src_hash);
    pakfile.insert(pakfile.end(), (uint8_t *)&src_hash_le, (uint8_t *)(&src_hash_le + 1));
    pakfile.insert(pakfile.end(), (uint8_t *)&num, (uint8_t *)(&num + 1));
    pakfile.insert(pakfile.end(), (uint8_t *)&dirstart, (uint8_t *)(&dirstart + 1));
    pakfile.insert(pakfile.end(), (uint8_t *)&version, (uint8_t *)(&version + 1));
    pakfile.insert(pakfile.end(), magic, magic + magic_size);
    assert(pakfile.size() - header_start == header_size);
    (void)header_start;
    return "";
}

// This just loads the directory part of a pakfile such that subsequent LoadFile calls know how
// to load from it.
bool LoadPakDir(const char *lpak, uint64_t &src_hash_dest) {
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
    auto src_hash = (uint64_t)read_unaligned64((int64_t *)header.c_str());
    auto num = read_unaligned64((int64_t *)header.c_str() + 1);
    auto dirstart = read_unaligned64((int64_t *)header.c_str() + 2);
    auto version = read_unaligned64((int64_t *)header.c_str() + 3);
    if (version != current_version) return false;
    if (dirstart > plen) return false;
    string dir;
    if (LoadFile(lpak, &dir, dirstart, plen - dirstart - (int64_t)header_size) < 0)
        return false;
    auto namestarts = (int64_t *)(dir.c_str() + dir.length()) - num;
    auto filestarts = namestarts - num;
    auto uncompressed = filestarts - num;
    for (int64_t i = 0; i < num; i++) {
        auto name = string_view(dir.c_str() + (read_unaligned64(namestarts + i) - dirstart));
        auto off = read_unaligned64(filestarts + i);
        auto end = i < num + 1 ? read_unaligned64(filestarts + i + 1) : dirstart;
        auto len = end - off;
        LOG_INFO("pakfile dir: ", name, " : ", len);
        AddPakFileEntry(lpak, name, off, len, read_unaligned64(uncompressed + i));
    }
    src_hash_dest = src_hash;
    return true;
}

bool LoadByteCode(string &bytecode) {
    if (LoadFile(bcname, &bytecode) < 0) return false;
    flatbuffers::Verifier verifier((const uint8_t *)bytecode.c_str(), bytecode.length());
    auto ok = bytecode::VerifyBytecodeFileBuffer(verifier);
    assert(ok);
    return ok;
}

void RegisterBuiltin(NativeRegistry &nfr, const char *ns, const char *name,
                     void (* regfun)(NativeRegistry &)) {
    LOG_DEBUG("subsystem: ", name);
    nfr.NativeSubSystemStart(ns, name);
    regfun(nfr);
}

void DumpBuiltinNames(NativeRegistry &nfr) {
    string s;
    for (auto nf : nfr.nfuns) {
        if (nfr.subsystems[nf->subsystemid] == "plugin") continue;
        s += nf->name;
        s += "|";
    }
    WriteFile("builtin_functions_names.txt", false, s, false);
}

string HTMLEscape(string_view in) {
    string s;
    for (auto c : in) {
        switch (c) {
            case '&': s += "&amp;"; break;
            case '<': s += "&lt;"; break;
            case '>': s += "&gt;"; break;
            default: s += c; break;
        }
    }
    return s;
}

void DumpBuiltinDoc(NativeRegistry &nfr) {
    string s =
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n"
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
        if (nfr.subsystems[nf->subsystemid] == "plugin") continue;
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
        for (auto [i, a] : enumerate(nf->args)) {
            if (a.type->t != V_NIL) last_non_nil = (int)i;
        }
        for (auto [i, a] : enumerate(nf->args)) {
            auto argname = nf->args[i].name;
            if (i) s +=  ", ";
            s += argname;
            s += "<font color=\"#666666\">";
            if (a.type->t != V_ANY) {
                s += ":";
                s += a.flags & NF_BOOL
                    ? "bool"
                    : HTMLEscape(TypeName(a.type->ElementIfNil(), a.fixed_len));
            }
            s += "</font>";
            if (a.type->t == V_NIL && (int)i > last_non_nil) {
                switch (a.type->sub->t) {
                    case V_INT:
                        if (a.flags & NF_BOOL)
                            append(s, " = ", a.default_val ? "true" : "false");
                        else
                            append(s, " = ", a.default_val);
                        break;
                    case V_FLOAT:
                        append(s, " = ", (float)a.default_val);
                        break;
                    default:
                        s += " = nil";
                }
            }
        }
        s += ")";
        if (nf->retvals.size()) {
            s += " -> ";
            for (auto [i, a] : enumerate(nf->retvals)) {
                s += "<font color=\"#666666\">";
                s += HTMLEscape(TypeName(a.type, a.fixed_len));
                s += "</font>";
                if (i < nf->retvals.size() - 1) s += ", ";
            }
        }
        s += cat("</tt></td><td class=\"a\">", HTMLEscape(nf->help), "</td></tr>\n");
    }
    s += "</table>\n</td></tr></table></center></body>\n</html>\n";
    WriteFile("builtin_functions_reference.html", false, s, false);
}

void PrepQuery(Query &query, vector<pair<string, string>> &filenames) {
    for (auto [i, fn] : enumerate(filenames)) {
        if (fn.first == query.file) {
            query.qloc.fileidx = (int)i;
            break;
        }
    }
    if (query.qloc.fileidx < 0) {
        THROW_OR_ABORT("query file not part of compilation: " + query.file);
    }
    query.qloc.line = parse_int<int>(query.line);
    query.filenames = &filenames;
}

void Compile(NativeRegistry &nfr, string_view fn, string_view stringsource, string &bytecode,
             string *parsedump, string *pakfile, bool return_value, int runtime_checks,
             Query *query, int max_errors, bool full_error) {
    #ifdef NDEBUG
        SlabAlloc slaballoc;
        if (g_current_slaballoc) THROW_OR_ABORT("nested slab allocator use");
        g_current_slaballoc = &slaballoc;
        struct SlabReset {
            ~SlabReset() {
                g_current_slaballoc = nullptr;
            }
        } slabreset;
    #endif
    vector<pair<string, string>> filenames;
    Lex lex(fn, filenames, nfr.namespaces, stringsource, max_errors);
    SymbolTable st(lex);
    Parser parser(nfr, lex, st);
    parser.Parse();
    if (query) PrepQuery(*query, filenames);
    TypeChecker tc(parser, st, return_value, query, full_error);
    if (query) tc.ProcessQuery();  // Failed to find location during type checking.
    if (lex.num_errors) THROW_OR_ABORT("errors encountered, aborting");
    tc.Stats(filenames);
    // Optimizer is not optional, must always run, since TypeChecker and CodeGen
    // rely on it culling const if-thens and other things.
    Optimizer opt(parser, st, tc, runtime_checks);
    if (parsedump) *parsedump = parser.DumpAll(true);
    CodeGen cg(parser, st, return_value, runtime_checks);
    auto src_hash = lex.HashAll();
    st.Serialize(cg.code, cg.type_table, cg.lineinfo, cg.sids, cg.stringtable, bytecode, cg.vtables,
                 filenames, cg.ser_ids, src_hash);
    if (pakfile) {
        auto err = BuildPakFile(*pakfile, bytecode, parser.pakfiles, src_hash);
        if (!err.empty()) THROW_OR_ABORT(err);
    }
}

pair<string, iint> RunTCC(NativeRegistry &nfr, string_view bytecode_buffer, string_view fn,
                          const char *object_name, vector<string> &&program_args, TraceMode trace,
                          bool compile_only, string &error, int runtime_checks, bool dump_leaks) {
    string sd;
    error = ToCPP(nfr, sd, bytecode_buffer, false, runtime_checks, "nullptr", "");
    if (!error.empty()) return { "", 0 };
    #if VM_JIT_MODE
        const char *export_names[] = { "compiled_entry_point", "vtables", nullptr };
        auto start_time = SecondsSinceStart();
        pair<string, iint> ret;
        auto ok = RunC(sd.c_str(), object_name, error, vm_ops_jit_table, export_names,
            [&](void **exports) -> bool {
                LOG_INFO("time to tcc (seconds): ", SecondsSinceStart() - start_time);
                if (compile_only) return true;
                auto vmargs = VMArgs {
                    nfr, string(fn), (uint8_t *)bytecode_buffer.data(),
                    bytecode_buffer.size(), std::move(program_args),
                    (fun_base_t *)exports[1], (fun_base_t)exports[0], trace, dump_leaks,
                    runtime_checks
                };
                lobster::VMAllocator vma(std::move(vmargs));
                vma.vm->EvalProgram();
                ret = vma.vm->evalret;
                return true;
            });
        if (!ok || !error.empty()) {
            // So we can see what the problem is..
            FILE *f = fopen((MainDir() + "compiled_lobster_jit_debug.c").c_str(), "w");
            if (f) {
                fputs(sd.c_str(), f);
                fclose(f);
            }
            error = "libtcc JIT error: " + string(fn) + ":\n" + error;
        }
        return ret;
    #else
        (void)fn;
        (void)object_name;
        (void)program_args;
        (void)trace;
        (void)compile_only;
        (void)dump_leaks;
        error = "cannot JIT code: libtcc not enabled";
        return { "", 0 };
    #endif
}

Value CompileRun(VM &parent_vm, StackPtr &parent_sp, Value &source, bool stringiscode,
                 vector<string> &&args) {
    string_view fn = stringiscode ? "string" : source.sval()->strv();  // fixme: datadir + sanitize?
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        int runtime_checks = RUNTIME_ASSERT;  // FIXME: let caller decide?
        string bytecode_buffer;
        Compile(parent_vm.nfr, fn, stringiscode ? source.sval()->strv() : string_view(),
                bytecode_buffer, nullptr, nullptr, true, runtime_checks, nullptr, 1, false);
        string error;
        auto ret = RunTCC(parent_vm.nfr, bytecode_buffer, fn, nullptr, std::move(args),
                          TraceMode::OFF, false, error, runtime_checks, true);
        if (!error.empty()) THROW_OR_ABORT(error);
        Push(parent_sp, Value(parent_vm.NewString(ret.first)));
        return NilVal();
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        Push(parent_sp, Value(parent_vm.NewString("nil")));
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
    [](StackPtr &sp, VM &vm, Value &filename, Value &args) {
        return CompileRun(vm, sp, filename, true, ValueToVectorOfStrings(args));
    });

nfr("compile_run_file", "filename,args", "SS]", "SS?",
    "same as compile_run_code(), only now you pass a filename.",
    [](StackPtr &sp, VM &vm, Value &filename, Value &args) {
        return CompileRun(vm, sp, filename, false, ValueToVectorOfStrings(args));
    });

}

void RegisterCoreLanguageBuiltins(NativeRegistry &nfr) {
    extern void AddBuiltins(NativeRegistry &nfr);
    extern void AddCompiler(NativeRegistry &nfr);
    extern void AddFile(NativeRegistry &nfr);
    extern void AddFlatBuffers(NativeRegistry &nfr);
    extern void AddReader(NativeRegistry &nfr);
    extern void AddMatrix(NativeRegistry &nfr);

    RegisterBuiltin(nfr, "", "builtin", AddBuiltins);
    RegisterBuiltin(nfr, "", "compiler", AddCompiler);
    RegisterBuiltin(nfr, "", "file", AddFile);
    RegisterBuiltin(nfr, "flatbuffers", "flatbuffers", AddFlatBuffers);
    RegisterBuiltin(nfr, "", "parsedata", AddReader);
    RegisterBuiltin(nfr, "matrix", "matrix", AddMatrix);
}

#if !LOBSTER_ENGINE
FileLoader EnginePreInit(NativeRegistry &nfr) {
    nfr.DoneRegistering();
    return DefaultLoadFile;
}
#endif

extern "C" int RunCompiledCodeMain(int argc, const char *const *argv, const uint8_t *bytecodefb,
                                   size_t static_size, const lobster::fun_base_t *vtables,
                                   void *custom_pre_init, const char *aux_src_path) {
    #ifdef _MSC_VER
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        InitUnhandledExceptionFilter(argc, (char **)argv);
    #endif
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        NativeRegistry nfr;
        RegisterCoreLanguageBuiltins(nfr);
        if (custom_pre_init) ((void (*)(NativeRegistry &))(custom_pre_init))(nfr);
        auto loader = EnginePreInit(nfr);
        min_output_level = OUTPUT_WARN;
        InitPlatform(GetMainDirFromExePath(argv[0]), aux_src_path, false, loader);
        auto from_lpak = true;
        uint64_t src_hash = 0;
        if (!LoadPakDir("default.lpak", src_hash)) {
            // FIXME: this is optional, we don't know if the compiled code wants to load this
            // file, so we don't error or even warn if this file can't be found.
            from_lpak = false;
        }
        auto vmargs = VMArgs {
            nfr,
            from_lpak ? string{} : StripDirPart(string_view_nt(argv[0])),
            bytecodefb,
            static_size,
            {},
            vtables,
            nullptr,
            TraceMode::OFF,
            false,
            RUNTIME_ASSERT
        };
        for (int arg = 1; arg < argc; arg++) { vmargs.program_args.push_back(argv[arg]); }
        lobster::VMAllocator vma(std::move(vmargs));
        if (from_lpak && src_hash != vma.vm->bcf->src_hash()) {
            THROW_OR_ABORT("lpak file from different version of the source code than the compiled code");
        }
        vma.vm->EvalProgram();
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        LOG_ERROR(s);
        return 1;
    }
    #endif
    return 0;
}

bool Type::Equal(const Type &o, bool allow_unresolved) const {
    if (this == &o) return true;
    if (t != o.t) {
        if (!allow_unresolved) return false;
        // Special case for V_UUDT, since sometime types are resolved in odd orders.
        // TODO: can the IsGeneric() be removed?
        switch (t) {
            case V_UUDT:
                return IsUDT(o.t) && spec_udt->gudt == &o.udt->g && !spec_udt->IsGeneric();
            case V_CLASS:
            case V_STRUCT_R:
            case V_STRUCT_S:
                return o.t == V_UUDT && o.spec_udt->gudt == &udt->g && !o.spec_udt->IsGeneric();
            default:
                return false;
        }
    }
    if (sub == o.sub) return true;  // Also compares sf/udt
    switch (t) {
        case V_VECTOR:
        case V_NIL:
            return sub->Equal(*o.sub, allow_unresolved);
        case V_UUDT:
            return spec_udt->Equal(*o.spec_udt);
        default:
            return false;
    }
}

SubFunction::~SubFunction() { if (sbody) delete sbody; }

Field::~Field() { delete defaultval; }

Field::Field(const Field &o)
    : id(o.id),
      giventype(o.giventype),
      defaultval(o.defaultval ? o.defaultval->Clone() : nullptr),
      isprivate(o.isprivate),
      in_scope(o.in_scope),
      defined_in(o.defined_in) {}

Function::~Function() {
    for (auto ov : overloads) delete ov;
    for (auto da : default_args) delete da;
}

Overload::~Overload() {
    delete gbody;
}

int Overload::NumSubf() {
    int sum = 0;
    for (auto csf = sf; csf; csf = csf->next)
        sum++;
    return sum;
}

bool SpecUDT::Equal(const SpecUDT &o) const {
    if (gudt != o.gudt ||
        IsGeneric() != o.IsGeneric() ||
        specializers.size() != o.specializers.size()) return false;
    for (auto [i, s] : enumerate(specializers)) {
        if (!s->Equal(*o.specializers[i])) return false;
    }
    return true;
}

bool SpecUDT::IsGeneric() const {
    assert(specializers.size() == gudt->generics.size());
    return !gudt->generics.empty();
}

}  // namespace lobster


#if STACK_PROFILING_ON
    vector<StackProfile> stack_profiles;
#endif

