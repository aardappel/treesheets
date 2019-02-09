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
#include "lobster/disasm.h"

#include "lobster/engine.h"

// FIXME: This makes SDL not modular, but without it it will miss the SDLMain indirection.
#include "lobster/sdlincludes.h"
#include "lobster/sdlinterface.h"

using namespace lobster;

int main(int argc, char* argv[]) {
    #ifdef _WIN32
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        InitUnhandledExceptionFilter(argc, argv);
    #endif
    Output(OUTPUT_INFO, "Lobster running...");
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
        bool to_cpp = false;
        bool dump_builtins = false;
        bool dump_names = false;
        const char *default_lpak = "default.lpak";
        const char *lpak = nullptr;
        const char *fn = nullptr;
        vector<string> program_args;
        string helptext = "\nUsage:\n"
            "lobster [ OPTIONS ] [ FILE ] [ -- ARGS ]\n"
            "Compile & run FILE, or omit FILE to load default.lpak\n"
            "-w                     Wait for input before exiting.\n"
            "-b                     Compile to pakfile, don't run.\n"
            "--to-cpp               Compile to C++ code, don't run.\n"
            "--parsedump            Also dump parse tree.\n"
            "--disasm               Also dump bytecode disassembly.\n"
            "--verbose              Output additional informational text.\n"
            "--debug                Output compiler internal logging.\n"
            "--silent               Only output errors.\n"
            "--noconsole            Close console window (Windows).\n"
            "--gen-builtins-html    Write builtin commands help file.\n"
            "--gen-builtins-names   Write builtin commands - just names.\n"
            "--non-interactive-test Quit after running 1 frame.\n";
        int arg = 1;
        for (; arg < argc; arg++) {
            if (argv[arg][0] == '-') {
                string a = argv[arg];
                if (a == "-w") { wait = true; }
                else if (a == "-b") { lpak = default_lpak; }
                else if (a == "--to-cpp") { to_cpp = true; }
                else if (a == "--parsedump") { parsedump = true; }
                else if (a == "--disasm") { disasm = true; }
                else if (a == "--verbose") { min_output_level = OUTPUT_INFO; }
                else if (a == "--debug") { min_output_level = OUTPUT_DEBUG; }
                else if (a == "--silent") { min_output_level = OUTPUT_ERROR; }
                else if (a == "--noconsole") { SetConsole(false); }
                else if (a == "--gen-builtins-html") { dump_builtins = true; }
                else if (a == "--gen-builtins-names") { dump_names = true; }
                else if (a == "--non-interactive-test") { SDLTestMode(); }
                else if (a == "--") { arg++; break; }
                // process identifier supplied by OS X
                else if (a.substr(0, 5) == "-psn_") { from_bundle = true; }
                else THROW_OR_ABORT("unknown command line argument: " + (argv[arg] + helptext));
            } else {
                if (fn) THROW_OR_ABORT("more than one file specified" + helptext);
                fn = argv[arg];
            }
        }
        for (; arg < argc; arg++) { program_args.push_back(argv[arg]); }

        #ifdef __IOS__
            //fn = "totslike.lobster";  // FIXME: temp solution
        #endif

        if (!InitPlatform(argv[0], fn ? fn : default_lpak, from_bundle, SDLLoadFile))
            THROW_OR_ABORT(string("cannot find location to read/write data on this platform!"));

        NativeRegistry natreg;
        RegisterCoreEngineBuiltins(natreg);

        string bytecode;
        if (!fn) {
            if (!LoadPakDir(default_lpak))
                THROW_OR_ABORT("Lobster programming language compiler/runtime (version " __DATE__
                               ")\nno arguments given - cannot load " + (default_lpak + helptext));
            // This will now come from the pakfile.
            if (!LoadByteCode(bytecode))
                THROW_OR_ABORT(string("Cannot load bytecode from pakfile!"));
        } else {
            Output(OUTPUT_INFO, "compiling...");
            string dump;
            string pakfile;
            Compile(natreg, StripDirPart(fn), nullptr, bytecode, parsedump ? &dump : nullptr,
                lpak ? &pakfile : nullptr, dump_builtins, dump_names);
            if (parsedump) {
                WriteFile("parsedump.txt", false, dump);
            }
            if (lpak) {
                WriteFile(lpak, true, pakfile);
                return 0;
            }
        }
        if (disasm) {
            ostringstream ss;
            DisAsm(natreg, ss, bytecode);
            WriteFile("disasm.txt", false, ss.str());
        }
        if (to_cpp) {
            // FIXME: make less hard-coded.
            FILE *f = fopen((StripFilePart(argv[0]) +
                            "../dev/compiled_lobster/src/compiled_lobster.cpp").c_str(), "w");
            if (f) {
                ostringstream ss;
                ToCPP(natreg, ss, bytecode);
                fputs(ss.str().c_str(), f);
                fclose(f);
            }
        } else {
            EngineRunByteCode(natreg, fn, bytecode, nullptr, nullptr, program_args);
        }
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        Output(OUTPUT_ERROR, s);
        if (from_bundle) SDLMessageBox("Lobster", s.c_str());
        if (wait) {
            Output(OUTPUT_PROGRAM, "press <ENTER> to continue:\n");
            getchar();
        }
        #ifdef _WIN32
            _CrtSetDbgFlag(0);  // Don't bother with memory leaks when there was an error.
        #endif
        EngineExit(1);
    }
    #endif
    EngineExit(0);
    return 0;
}

