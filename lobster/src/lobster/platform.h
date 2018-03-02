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

// Platform independent file access:
typedef int64_t (* FileLoader)(const char *absfilename, string *dest, int64_t start, int64_t len);

// Call this at init to determine default folders to load stuff from.
// Also initializes anything else functions in this file need.
extern bool InitPlatform(const char *exefilepath, const char *auxfilepath, bool from_bundle,
                             FileLoader loader);

extern string StripFilePart(const char *filepath);
extern string StripDirPart(const char *filepath);

// Read all or part of a file.
// To read the whole file, pass -1 for len.
// To just obtain the file length but don't do any reading, pass 0 for len.
// Returns file length or read length, or -1 if failed.
extern int64_t LoadFile(const char *relfilename, string *dest, int64_t start = 0, int64_t len = -1);

extern FILE *OpenForWriting(const char *relfilename, bool binary);
extern bool WriteFile(const char *relfilename, bool binary, const char *data, size_t len);
extern string SanitizePath(const char *path);

extern void AddPakFileEntry(const char *pakfilename, const char *relfilename, int64_t off,
                            int64_t len, int64_t uncompressed);

extern bool ScanDir(const char *reldir, vector<pair<string, int64_t>> &dest);
extern bool ScanDirAbs(const char *absdir, vector<pair<string, int64_t>> &dest);

// Logging:

enum OutputType {
    // Temp spam, should eventually be removed, shown only at --debug.
    OUTPUT_DEBUG,
    // Output that helps understanding what the code is doing when not under a debugger,
    // shown with --verbose.
    OUTPUT_INFO,
    // Non-critical issues, e.g. SDL errors. This level shown by default.
    OUTPUT_WARN,
    // Output by the Lobster code.
    OUTPUT_PROGRAM,
    // Compiler & vm errors, program terminates after this. Only thing shown at --silent.
    OUTPUT_ERROR,
};

extern OutputType min_output_level;  // Defaults to showing OUTPUT_WARN and up.

extern void Output(OutputType ot, const char *msg, ...);

// Time:
extern double SecondsSinceStart();

// CPU:
extern uint NumHWThreads();
extern uint NumHWCores();

// Misc:
extern void ConditionalBreakpoint(bool shouldbreak);
extern void MakeDPIAware();

extern string GetDateTime();

extern void SetConsole(bool on);

#if defined(__IOS__) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    #define PLATFORM_ES3
#endif

#if defined(__IOS__) || defined(__ANDROID__)
    #define PLATFORM_TOUCH
#endif

#if !defined(PLATFORM_ES3) && !defined(__APPLE__)
	#define PLATFORM_WINNIX
#endif

#if defined(_WIN32)  // FIXME: Also make work on Linux/OS X.
    #define PLATFORM_VR
    #define PLATFORM_STEAMWORKS
#endif
