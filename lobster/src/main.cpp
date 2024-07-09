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

#include "lobster/compiler.h"
#include "lobster/il.h"
#include "lobster/disasm.h"
#include "lobster/tonative.h"

#if LOBSTER_ENGINE
    // FIXME: This makes SDL not modular, but without it it will miss the SDLMain indirection.
    #include "lobster/sdlincludes.h"
    #include "lobster/sdlinterface.h"
#endif

#ifndef GIT_COMMIT_INFOSTR
#define GIT_COMMIT_INFOSTR __DATE__ "|unknown"
#endif

using namespace lobster;

void unit_test_all() {
    // We don't really have unit tests, but let's collect some that always
    // run in debug mode:
    #ifdef NDEBUG
        return;
    #endif
    unit_test_varint();
    unit_test_unicode();
    unit_test_packed_vector();
}

int main(int argc, char* argv[]) {
    #ifdef _MSC_VER
    	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        InitUnhandledExceptionFilter(argc, argv);
    #endif
    LOG_INFO("Lobster running...");
    bool wait = false;
    bool from_bundle =
    #ifdef __IOS__
        true;
    #else
        false;
    #endif
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        bool parsedump = false;
        bool disasm = false;
        bool dump_builtins = false;
        bool dump_names = false;
        bool tcc_out = false;
        bool compile_only = false;
        bool non_interactive_test = false;
        bool full_error = false;
        int runtime_checks = RUNTIME_ASSERT;
        int max_errors = 1;
        const char *default_lpak = "default.lpak";
        const char *lpak = nullptr;
        string fn;
        string mainfile;
        vector<string> program_args;
        vector<string> imports;
        auto trace = TraceMode::OFF;
        auto jit_mode = true;
        Query query;
        string helptext = "\nUsage:\n"
            "lobster [ OPTIONS ] [ FILE ] [ -- ARGS ]\n"
            "Compile & run FILE, or omit FILE to load default.lpak\n"
            "--pak                  Compile to pakfile, don't run.\n"
            "--cpp                  Compile to C++ code, don't run (see implementation.md!).\n"
            "--import RELDIR        Additional dir (relative to FILE) to load imports from\n"
            "--main MAIN            if present, run this main program file after compiling FILE.\n"
            "--parsedump            Also dump parse tree.\n"
            "--disasm               Also dump bytecode disassembly.\n"
            "--verbose              Output additional informational text.\n"
            "--debug                Output compiler internal logging.\n"
            "--silent               Only output errors.\n"
            "--full-error           Output full compile time stack traces.\n"
            "--runtime-no-asserts   Compile with asserts off.\n"
            "--runtime-asserts      Compile with asserts on (default).\n"
            "--runtime-stack-traces Asserts on + code locations + stack traces.\n"
            "--runtime-debug        Also minimize inlining.\n"
            "--runtime-debug-dump   Also create debug dumps.\n"
            "--runtime-debugger     Also break into debugger on error.\n"
            "--noconsole            Close console window (Windows).\n"
            "--gen-builtins-html    Write builtin commands help file.\n"
            "--gen-builtins-names   Write builtin commands - just names.\n"
            #if LOBSTER_ENGINE
            "--non-interactive-test Quit after running 1 frame.\n"
            #endif
            "--trace                Log bytecode instructions (SLOW, Debug only).\n"
            "--trace-tail           Show last 50 bytecode instructions on error.\n"
            "--tcc-out              Output tcc .o file instead of running.\n"
            "--wait                 Wait for input before exiting.\n"
            "--query QUERY_ARGS     Queries about definitions in the program being compiled.\n"
            "--errors N             Output up to N errors (default 1).\n";
            int arg = 1;
        for (; arg < argc; arg++) {
            if (argv[arg][0] == '-') {
                string a = argv[arg];
                if      (a == "--wait") { wait = true; }
                else if (a == "--pak") { lpak = default_lpak; }
                else if (a == "--cpp") { jit_mode = false; }
                else if (a == "--parsedump") { parsedump = true; }
                else if (a == "--disasm") { disasm = true; }
                else if (a == "--verbose") { min_output_level = OUTPUT_INFO; }
                else if (a == "--debug") { min_output_level = OUTPUT_DEBUG; }
                else if (a == "--silent") { min_output_level = OUTPUT_ERROR; }
                else if (a == "--runtime-no-asserts") { runtime_checks = RUNTIME_NO_ASSERT; }
                else if (a == "--runtime-asserts") { runtime_checks = RUNTIME_ASSERT; }
                else if (a == "--runtime-stack-traces") { runtime_checks = RUNTIME_STACK_TRACE; }
                else if (a == "--runtime-debug") { runtime_checks = RUNTIME_DEBUG; }
                else if (a == "--runtime-debug-dump") { runtime_checks = RUNTIME_DEBUG_DUMP; }
                else if (a == "--runtime-debugger") { runtime_checks = RUNTIME_DEBUGGER; }
                else if (a == "--noconsole") { SetConsole(false); }
                else if (a == "--gen-builtins-html") { dump_builtins = true; }
                else if (a == "--gen-builtins-names") { dump_names = true; }
                else if (a == "--compile-only") { compile_only = true; }
                else if (a == "--full-error") { full_error = true; }
                #if LOBSTER_ENGINE
                else if (a == "--non-interactive-test") { non_interactive_test = true; SDLTestMode(); }
                #endif
                else if (a == "--trace") { trace = TraceMode::ON; runtime_checks = RUNTIME_DEBUG; }
                else if (a == "--trace-tail") { trace = TraceMode::TAIL; runtime_checks = RUNTIME_DEBUG; }
                else if (a == "--tcc-out") { tcc_out = true; }
                else if (a == "--import") {
                    arg++;
                    if (arg >= argc) THROW_OR_ABORT("missing import dir");
                    imports.push_back(argv[arg]);
                } else if (a == "--main") {
                    arg++;
                    if (arg >= argc) THROW_OR_ABORT("missing main file");
                    if (!mainfile.empty()) THROW_OR_ABORT("--main specified twice");
                    mainfile = SanitizePath(argv[arg]);
                } else if (a == "--") {
                    arg++;
                    break;
                } else if (a == "--errors") {
                    arg++;
                    if (arg >= argc) THROW_OR_ABORT("missing error count");
                    max_errors = std::max(1, std::max(100, parse_int<int>(string_view(argv[arg]))));
                } else if (a == "--query") {
                    arg++;
                    if (argc - arg < 4)
                        THROW_OR_ABORT("insufficient --query arguments");
                    query.kind = argv[arg++];
                    query.file = argv[arg++];
                    query.line = argv[arg++];
                    query.iden = argv[arg++];
                    for (; arg < argc; arg++) {
                        query.args.push_back(argv[arg]);
                    }
                    break;
                }
                // process identifier supplied by OS X
                else if (a.substr(0, 5) == "-psn_") { from_bundle = true; }
                else THROW_OR_ABORT("unknown command line argument: " + (argv[arg] + helptext));
            } else {
                if (!fn.empty()) THROW_OR_ABORT("more than one file specified" + helptext);
                fn = SanitizePath(argv[arg]);
            }
        }
        for (; arg < argc; arg++) { program_args.push_back(argv[arg]); }

        unit_test_all();

        #ifdef __IOS__
            //fn = "totslike.lobster";  // FIXME: temp solution
        #endif

        NativeRegistry nfr;
        RegisterCoreLanguageBuiltins(nfr);
        auto loader = EnginePreInit(nfr);

        if (!InitPlatform(GetMainDirFromExePath(argv[0]),
                          !mainfile.empty() ? mainfile
                                            : (!fn.empty() ? fn : string(default_lpak)),
                          from_bundle,
                          loader))
            THROW_OR_ABORT("cannot find location to read/write data on this platform!");
        if (!mainfile.empty() && !fn.empty()) AddDataDir(StripFilePart(fn));

        if (dump_builtins) { DumpBuiltinDoc(nfr); return 0; }
        if (dump_names) { DumpBuiltinNames(nfr); return 0; }

        LOG_INFO("lobster version " GIT_COMMIT_INFOSTR);

        for (auto &import : imports) AddDataDir(import);
        if (!fn.empty()) fn = StripDirPart(fn);

        string bytecode_buffer;
        if (fn.empty()) {
            uint64_t src_hash = 0;  // Don't care, from same file as bytecode.
            if (!LoadPakDir(default_lpak, src_hash))
                THROW_OR_ABORT(
                    "Lobster programming language compiler/runtime (version "
                               GIT_COMMIT_INFOSTR ")\nno arguments given - cannot load "
                               + (default_lpak + helptext));
            // This will now come from the pakfile.
            if (!LoadByteCode(bytecode_buffer))
                THROW_OR_ABORT("Cannot load bytecode from pakfile!");
        } else {
            LOG_INFO("compiling...");
            string dump;
            string pakfile;
            auto start_time = SecondsSinceStart();
            dump.clear();
            pakfile.clear();
            for (;;) {
                bytecode_buffer.clear();
                Compile(nfr, fn, {}, bytecode_buffer, parsedump ? &dump : nullptr,
                        lpak ? &pakfile : nullptr, false, runtime_checks,
                        !query.kind.empty() ? &query : nullptr, max_errors, full_error);
                if (mainfile.empty()) break;
                if (!FileExists(mainfile, true)) {
                    //LOG_WARN(mainfile, " does not exist, skipping");
                    break;
                }
                fn = StripDirPart(mainfile);
                mainfile.clear();
            }
            if (!query.kind.empty()) {
                // We don't actually produce anything in this mode.
                return 0;
            }
            LOG_INFO("time to compile (seconds): ", SecondsSinceStart() - start_time);
            if (parsedump) {
                WriteFile("parsedump.txt", false, dump, false);
            }
            if (lpak) {
                WriteFile(lpak, true, pakfile, false);
                return 0;
            }
        }
        if (disasm) {
            string sd;
            DisAsm(nfr, sd, bytecode_buffer);
            WriteFile("disasm.txt", false, sd, false);
        }
        if (jit_mode) {
            string error;
            auto ret = RunTCC(nfr,
                              bytecode_buffer,
                              !fn.empty() ? fn : "",
                              tcc_out ? "tcc_out.o" : nullptr,
                              std::move(program_args),
                              trace,
                              compile_only,
                              error,
                              runtime_checks,
                              !non_interactive_test);
            if (!error.empty())
                THROW_OR_ABORT(error);
            return (int)ret.second;
        } else {
            string sd;
            auto err = ToCPP(nfr, sd, bytecode_buffer, true, runtime_checks, "nullptr",
                             "main.lobster");
            if (!err.empty()) THROW_OR_ABORT(err);
            // FIXME: make less hard-coded.
            auto out = "dev/compiled_lobster/src/compiled_lobster.cpp";
            FILE *f = fopen((MainDir() + out).c_str(), "w");
            if (f) {
                fputs(sd.c_str(), f);
                fclose(f);
            } else {
                THROW_OR_ABORT(cat("cannot write: ", out));
            }
        }
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        LOG_ERROR(s);
        #if LOBSTER_ENGINE
            if (from_bundle) SDLMessageBox(string_view_nt("Lobster"), s);
        #endif
        if (wait) {
            LOG_PROGRAM("press <ENTER> to continue:\n");
            getchar();
        }
        #ifdef _MSC_VER
            _CrtSetDbgFlag(0);  // Don't bother with memory leaks when there was an error.
        #endif
        return 1;
    }
    #endif
    return 0;
}

