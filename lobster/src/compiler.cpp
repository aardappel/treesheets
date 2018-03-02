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

#include "lobster/vmdata.h"
#include "lobster/natreg.h"

#include "lobster/vm.h"

#include "lobster/ttypes.h"
#include "lobster/lex.h"
#include "lobster/idents.h"
#include "lobster/node.h"
#include "lobster/parser.h"
#include "lobster/typecheck.h"
#include "lobster/optimizer.h"
#include "lobster/codegen.h"

#include "lobster/tocpp.h"

namespace lobster {

NativeRegistry natreg;
const Type g_type_int(V_INT);
const Type g_type_float(V_FLOAT);
const Type g_type_string(V_STRING);
const Type g_type_any(V_ANY);
const Type g_type_vector_any(V_VECTOR, &g_type_any);
const Type g_type_vector_int(V_VECTOR, &g_type_int);
const Type g_type_vector_float(V_VECTOR, &g_type_float);
const Type g_type_function_null(V_FUNCTION);
const Type g_type_function_cocl(V_YIELD);
const Type g_type_coroutine(V_COROUTINE);
const Type g_type_resource(V_RESOURCE);
const Type g_type_typeid(V_TYPEID);
const Type g_type_function_nil(V_NIL, &g_type_function_null);

TypeRef type_int = &g_type_int;
TypeRef type_float = &g_type_float;
TypeRef type_string = &g_type_string;
TypeRef type_any = &g_type_any;
TypeRef type_vector_any = &g_type_vector_any;
TypeRef type_vector_int = &g_type_vector_int;
TypeRef type_vector_float = &g_type_vector_float;
TypeRef type_function_null = &g_type_function_null;
TypeRef type_function_cocl = &g_type_function_cocl;
TypeRef type_coroutine = &g_type_coroutine;
TypeRef type_resource = &g_type_resource;
TypeRef type_typeid = &g_type_typeid;
TypeRef type_function_nil = &g_type_function_nil;

bool IsCompressed(const char *filename) {
    auto dot = strrchr(filename, '.');
    return dot && (strcmp(dot, ".lbc") ||
                   strcmp(dot, ".lobster") ||
                   strcmp(dot, ".materials"));
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
    auto add_file = [&](const string &buf, const char *filename) {
        filestarts.push_back(LE(pakfile.size()));
        filenames.push_back(filename);
        Output(OUTPUT_INFO, "adding to pakfile: %s", filename);
        if (IsCompressed(filename)) {
            string out;
            WEntropyCoder<true>((uchar *)buf.c_str(), buf.length(), buf.length(), out);
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
    files.insert("shaders/default.materials");  // If it hadn't already been added.
    string buf;
    for (auto &filename : files) {
        auto l = LoadFile(filename.c_str(), &buf);
        if (l >= 0) {
            add_file(buf, filename.c_str());
        } else {
            vector<pair<string, int64_t>> dir;
            if (!ScanDir(filename.c_str(), dir))
                throw "cannot load file/dir for pakfile: " + filename;
            for (auto &p : dir) {
                auto fn = filename + p.first;
                if (p.second >= 0 && LoadFile(fn.c_str(), &buf) >= 0)
                    add_file(buf, fn.c_str());
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
        auto name = (char *)(dir.c_str() + (read_unaligned64(namestarts + i) - dirstart));
        auto off = read_unaligned64(filestarts + i);
        auto end = i < num + 1 ? read_unaligned64(filestarts + i + 1) : dirstart;
        auto len = end - off;
        Output(OUTPUT_INFO, "pakfile dir: %s (%d)", name, (size_t)len);
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

void RegisterBuiltin(const char *name, void (* regfun)()) {
    Output(OUTPUT_DEBUG, "subsystem: %s", name);
    natreg.NativeSubSystemStart(name);
    regfun();
}

void DumpBuiltins(bool justnames, const SymbolTable &st) {
    if (justnames) {
        FILE *f = OpenForWriting("builtin_functions_names.txt", false);
        if (f) {
            for (auto nf : natreg.nfuns) fprintf(f, "%s ", nf->name.c_str());
        }
        fclose(f);
        return;
    }
    FILE *f = OpenForWriting("builtin_functions_reference.html", false);
    if (f) {
        fprintf(f, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n");
        fprintf(f, "<html>\n<head>\n<title>lobster builtin function reference</title>\n");
        fprintf(f, "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n");
        fprintf(f, "<style type=\"text/css\">");
        fprintf(f, "table.a, tr.a, td.a {font-size: 10pt;border: 1pt solid #DDDDDD;");
        fprintf(f, " border-Collapse: collapse; max-width:1200px}</style>\n");
        fprintf(f, "</head>\n<body><center><table border=0><tr><td>\n<p>");
        fprintf(f, "lobster builtin functions:");
        fprintf(f, "(file auto generated by compiler, do not modify)</p>\n\n");
        int cursubsystem = -1;
        bool tablestarted = false;
        for (auto nf : natreg.nfuns) {
            if (nf->subsystemid != cursubsystem) {
                if (tablestarted) fprintf(f, "</table>\n");
                tablestarted = false;
                fprintf(f, "<h3>%s</h3>\n", natreg.subsystems[nf->subsystemid].c_str());
                cursubsystem = nf->subsystemid;
            }
            if (!tablestarted) {
                fprintf(f, "<table class=\"a\" border=1 cellspacing=0 cellpadding=4>\n");
                tablestarted = true;
            }
            fprintf(f, "<tr class=\"a\" valign=top><td class=\"a\"><tt><b>%s</b>(",
                    nf->name.c_str());
            int i = 0;
            for (auto &a : nf->args.v) {
                fprintf(f, "%s%s%s<font color=\"#666666\">%s</font>%s",
                    a.type->t == V_NIL ? " [" : "",
                    i ? ", " : "",
                    nf->args.GetName(i).c_str(),
                    a.type->t == V_ANY ? "" : (string(":") +
                        TypeName(a.type->t == V_NIL ? a.type->Element() : a.type,
                                 a.fixed_len, &st)).c_str(),
                    a.type->t == V_NIL ? "]" : ""
                );
                i++;
            }
            fprintf(f, ")");
            if (nf->retvals.v.size()) {
                fprintf(f, " -> ");
                size_t i = 0;
                for (auto &a : nf->retvals.v) {
                    fprintf(f, "<font color=\"#666666\">%s</font>%s",
                                TypeName(a.type, a.fixed_len, &st).c_str(),
                                i++ < nf->retvals.v.size() - 1 ? ", " : "");
                }
            }
            fprintf(f, "</tt></td><td class=\"a\">%s</td></tr>\n", nf->help);
        }
        fprintf(f, "</table>\n</td></tr></table></center></body>\n</html>\n");
        fclose(f);
    }
}

void Compile(const char *fn, const char *stringsource, string &bytecode,
    string *parsedump = nullptr, string *pakfile = nullptr,
    bool dump_builtins = false, bool dump_names = false) {
    SymbolTable st;
    Parser parser(fn, st, stringsource);
    parser.Parse();
    TypeChecker tc(parser, st);
    // Optimizer is not optional, must always run at least one pass, since TypeChecker and CodeGen
    // rely on it culling const if-thens and other things.
    Optimizer opt(parser, st, tc, 100);
    if (parsedump) *parsedump = parser.DumpAll(true);
    CodeGen cg(parser, st);
    st.Serialize(cg.code, cg.code_attr, cg.type_table, cg.vint_typeoffsets, cg.vfloat_typeoffsets,
        cg.lineinfo, cg.sids, cg.stringtable, cg.speclogvars, bytecode);
    if (pakfile) BuildPakFile(*pakfile, bytecode, parser.pakfiles);
    if (dump_builtins) DumpBuiltins(false, st);
    if (dump_names) DumpBuiltins(true, st);
}

Value CompileRun(Value &source, bool stringiscode, const vector<string> &args) {
    string fn = stringiscode ? "string" : source.sval()->str();  // fixme: datadir + sanitize?
    SlabAlloc *parentpool = vmpool; vmpool = nullptr;
    VM        *parentvm   = g_vm;   g_vm = nullptr;
    try {
        string bytecode;
        Compile(fn.c_str(), stringiscode ? source.sval()->str() : nullptr, bytecode);
        //string s; DisAsm(s, bytecode); Output(OUTPUT_INFO, "%s", s.c_str());
        #ifdef VM_COMPILED_CODE_MODE
            // FIXME: Sadly since we modify how the VM operates under compiled code, we can't run in
            // interpreted mode anymore.
            throw string("cannot execute bytecode in compiled mode");
        #endif
        RunBytecode(fn.c_str(), bytecode, nullptr, nullptr, args);
        auto ret = g_vm->evalret;
        delete g_vm;
        assert(!vmpool && !g_vm);
        vmpool = parentpool;
        g_vm = parentvm;
        source.DECRT();
        g_vm->Push(Value(g_vm->NewString(ret)));
        return Value();
    }
    catch (string &s) {
        if (g_vm) delete g_vm;
        vmpool = parentpool;
        g_vm = parentvm;
        source.DECRT();
        g_vm->Push(Value(g_vm->NewString("nil")));
        return Value(g_vm->NewString(s));
    }
}

void AddCompiler() {  // it knows how to call itself!
    STARTDECL(compile_run_code) (Value &filename, Value &args) {
        return CompileRun(filename, true, VectorOfStrings(args));
    }
    ENDDECL2(compile_run_code, "code,args", "SS]", "SS?",
        "compiles and runs lobster source, sandboxed from the current program (in its own VM)."
        " the argument is a string of code. returns the return value of the program as a string,"
        " with an error string as second return value, or nil if none. using parse_data(),"
        " two program can communicate more complex data structures even if they don't have the same"
        " version of struct definitions.");

    STARTDECL(compile_run_file) (Value &filename, Value &args) {
        return CompileRun(filename, false, VectorOfStrings(args));
    }
    ENDDECL2(compile_run_file, "filename,args", "SS]", "SS?",
        "same as compile_run_code(), only now you pass a filename.");
}

void RegisterCoreLanguageBuiltins() {
    extern void AddBuiltins(); RegisterBuiltin("builtin",   AddBuiltins);
    extern void AddCompiler(); RegisterBuiltin("compiler",  AddCompiler);
    extern void AddFile();     RegisterBuiltin("file",      AddFile);
    extern void AddReader();   RegisterBuiltin("parsedata", AddReader);
}

SubFunction::~SubFunction() { delete body; }

Field::~Field() { delete defaultval; }

Field::Field(const Field &o)
    : Typed(o), id(o.id), fieldref(o.fieldref),
      defaultval(o.defaultval ? o.defaultval->Clone() : nullptr) {}

}
