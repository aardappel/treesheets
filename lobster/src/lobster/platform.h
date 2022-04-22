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
typedef int64_t (* FileLoader)(string_view absfilename, string *dest, int64_t start, int64_t len);

// Call this at init to determine default folders to load stuff from.
string GetMainDirFromExePath(const char *argv_0);
// Then pass the result as maindir to InitPlatform.
// This also initializes anything else functions in this file need.
extern bool InitPlatform(string maindir, const char *auxfilepath, bool from_bundle,
                             FileLoader loader);
extern void AddDataDir(string_view path);  // Any additional dirs besides the above.
extern string_view ProjectDir();
extern string_view MainDir();

extern string_view StripFilePart(string_view filepath);
extern const char *StripDirPart(const char *filepath);

// Read all or part of a file.
// To read the whole file, pass -1 for len.
// To just obtain the file length but don't do any reading, pass 0 for len.
// Returns file length or read length, or -1 if failed.
extern int64_t LoadFile(string_view relfilename, string *dest, int64_t start = 0, int64_t len = -1,
                        bool binary = true);

// fopen based implementation of FileLoader above to pass to InitPlatform if needed.
extern int64_t DefaultLoadFile(string_view absfilename, string *dest, int64_t start, int64_t len);

extern FILE *OpenForWriting(string_view relfilename, bool binary);
extern FILE *OpenForReading(string_view relfilename, bool binary);
extern bool WriteFile(string_view relfilename, bool binary, string_view contents);
extern bool FileExists(string_view relfilename);
extern bool FileDelete(string_view relfilename);
extern string SanitizePath(string_view path);

extern void AddPakFileEntry(string_view pakfilename, string_view relfilename, int64_t off,
                            int64_t len, int64_t uncompressed);

extern bool ScanDir(string_view reldir, vector<pair<string, int64_t>> &dest);
extern bool ScanDirAbs(string_view absdir, vector<pair<string, int64_t>> &dest);

extern iint LaunchSubProcess(const char **cmdl, const char *stdins, string &out);

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

extern void LogOutput(OutputType ot, const char *buf);
inline void LogOutput(OutputType ot, const string &buf) { LogOutput(ot, buf.c_str()); };
template<typename ...Ts> void LogOutput(OutputType ot, const Ts&... args) {
    if (ot >= min_output_level) LogOutput(ot, cat(args...).c_str());
}
// This is to make it lazy: arguments are not constructed at all if level is too low.
#define LOG_DEBUG(...)   { if (min_output_level <= OUTPUT_DEBUG) \
                               LogOutput(OUTPUT_DEBUG, __VA_ARGS__); }
#define LOG_INFO(...)    { if (min_output_level <= OUTPUT_INFO) \
                               LogOutput(OUTPUT_INFO, __VA_ARGS__); }
#define LOG_WARN(...)    { if (min_output_level <= OUTPUT_WARN) \
                               LogOutput(OUTPUT_WARN, __VA_ARGS__); }
#define LOG_PROGRAM(...) { if (min_output_level <= OUTPUT_PROGRAM) \
                               LogOutput(OUTPUT_PROGRAM, __VA_ARGS__); }
#define LOG_ERROR(...)   { if (min_output_level <= OUTPUT_ERROR) \
                               LogOutput(OUTPUT_ERROR, __VA_ARGS__); }

// Time:
extern double SecondsSinceStart();

// CPU:
extern int NumHWThreads();
extern int NumHWCores();

// Misc:
extern void ConditionalBreakpoint(bool shouldbreak);
extern void CountingBreakpoint(int i = -1);
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

#if defined(_WIN32) && !defined(SKIP_SDKS)  // FIXME: Also make work on Linux/OS X.
    #define PLATFORM_VR
    #define PLATFORM_STEAMWORKS
#endif
