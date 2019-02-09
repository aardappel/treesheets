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

// Misc platform specific stuff.

#include "lobster/stdafx.h"
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
    #define VC_EXTRALEAN
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #define FILESEP '\\'
    #include <intrin.h>
#else
    #include <sys/time.h>
	#ifndef PLATFORM_ES3
		#include <glob.h>
		#include <sys/stat.h>
	#endif
    #define FILESEP '/'
#endif

#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#ifndef __IOS__
#include <Carbon/Carbon.h>
#endif
#endif

#ifdef __ANDROID__
#include <android/log.h>
#include "sdlincludes.h"  // FIXME
#endif

// Main dir to load files relative to, on windows this is where lobster.exe resides, on apple
// platforms it's the Resource folder in the bundle.
string datadir;
// Auxiliary dir to load files from, this is where the bytecode file you're running or the main
// .lobster file you're compiling reside.
string auxdir;
// Folder to write to, usually the same as auxdir, special folder on mobile platforms.
string writedir;

string exefile;

FileLoader cur_loader = nullptr;

bool have_console = true;


#ifndef _WIN32   // Emulate QPC on *nix, thanks Lee.
struct LARGE_INTEGER {
    long long int QuadPart;
};

void QueryPerformanceCounter(LARGE_INTEGER *dst) {
    struct timeval t;
    gettimeofday (& t, nullptr);
    dst->QuadPart = t.tv_sec * 1000000LL + t.tv_usec;
}

void QueryPerformanceFrequency(LARGE_INTEGER *dst) {
    dst->QuadPart = 1000000LL;
}
#endif

static LARGE_INTEGER time_frequency, time_start;
void InitTime() {
    QueryPerformanceFrequency(&time_frequency);
    QueryPerformanceCounter(&time_start);
}

double SecondsSinceStart() {
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    return double(end.QuadPart - time_start.QuadPart) / double(time_frequency.QuadPart);
}

uint hwthreads = 2, hwcores = 1;
void InitCPU() {
    // This can fail and return 0, so default to 2 threads:
    hwthreads = max(2U, thread::hardware_concurrency());
    // As a baseline, assume desktop CPUs are hyperthreaded, and mobile ones are not.
    #ifdef PLATFORM_ES3
        hwcores = hwthreads;
    #else
        hwcores = max(1U, hwthreads / 2);
    #endif
    // On Windows, we can do better and actually count cores.
    #ifdef _WIN32
        DWORD buflen = 0;
        if (!GetLogicalProcessorInformation(nullptr, &buflen) && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(buflen / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
            if (GetLogicalProcessorInformation(buf.data(), &buflen)) {
                uint cores = 0;
                for (auto &lpi : buf) {
                    if (lpi.Relationship == RelationProcessorCore) cores++;
                }
                // Only overwrite our baseline if we actually found any cores.
                if (cores) hwcores = cores;
            }
        }
    #endif
}

uint NumHWThreads() { return hwthreads; }
uint NumHWCores() { return hwcores; }


string_view StripFilePart(string_view filepath) {
    auto fpos = filepath.find_last_of(FILESEP);
    return fpos != string_view::npos ? filepath.substr(0, fpos + 1) : "";
}

const char *StripDirPart(const char *filepath) {
    auto fpos = strrchr(filepath, FILESEP);
    if (!fpos) fpos = strrchr(filepath, ':');
    return fpos ? fpos + 1 : filepath;
}

bool InitPlatform(const char *exefilepath, const char *auxfilepath, bool from_bundle,
                      FileLoader loader) {
    InitTime();
    InitCPU();
    exefile = SanitizePath(exefilepath);
    cur_loader = loader;
    datadir = StripFilePart(exefile);
    auxdir = auxfilepath ? StripFilePart(SanitizePath(auxfilepath)) : datadir;
    writedir = auxdir;
    // FIXME: use SDL_GetBasePath() instead?
    #ifdef _WIN32
        have_console = GetConsoleWindow() != nullptr;
    #elif defined(__APPLE__)
        if (from_bundle) {
            have_console = false;
            // Default data dir is the Resources folder inside the .app bundle.
            CFBundleRef mainBundle = CFBundleGetMainBundle();
            CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
            char path[PATH_MAX];
            auto res = CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8 *)path,
                                                        PATH_MAX);
            CFRelease(resourcesURL);
            if (!res)
                return false;
            datadir = string_view(path) + "/";
            #ifdef __IOS__
                // There's probably a better way to do this in CF.
                writedir = StripFilePart(path) + "Documents/";
            #else
                // FIXME: This should probably be ~/Library/Application Support/AppName,
                // but for now this works for non-app store apps.
                writedir = datadir;
            #endif
        }
    #elif defined(__ANDROID__)
        // FIXME: removed SDL dependency for other platforms. So on Android, move calls to
        // SDL_AndroidGetInternalStoragePath into SDL and pass the results into here somehow.
        SDL_Init(0); // FIXME: Is this needed? bad dependency.
        auto internalstoragepath = SDL_AndroidGetInternalStoragePath();
        auto externalstoragepath = SDL_AndroidGetExternalStoragePath();
        Output(OUTPUT_INFO, internalstoragepath);
        Output(OUTPUT_INFO, externalstoragepath);
        if (internalstoragepath) datadir = internalstoragepath + string_view("/");
        if (externalstoragepath) writedir = externalstoragepath + string_view("/");
        // For some reason, the above SDL functionality doesn't actually work,
        // we have to use the relative path only to access APK files:
        datadir = "";
        auxdir = writedir;
    #endif
    (void)from_bundle;
    return true;
}

string SanitizePath(string_view path) {
    string r;
    for (auto c : path) {
        r += c == '\\' || c == '/' ? FILESEP : c;
    }
    return r;
}

map<string, tuple<string, int64_t, int64_t, int64_t>, less<>> pakfile_registry;

void AddPakFileEntry(string_view pakfilename, string_view relfilename, int64_t off,
                     int64_t len, int64_t uncompressed) {
    pakfile_registry[string(relfilename)] = make_tuple(pakfilename, off, len, uncompressed);
}

int64_t LoadFileFromAny(string_view srelfilename, string *dest, int64_t start, int64_t len) {
    auto l = cur_loader(auxdir + srelfilename, dest, start, len);
    if (l >= 0) return l;
    l = cur_loader(datadir + srelfilename, dest, start, len);
    if (l >= 0) return l;
    return cur_loader(writedir + srelfilename, dest, start, len);
}

int64_t LoadFile(string_view relfilename, string *dest, int64_t start, int64_t len) {
    assert(cur_loader);
    auto it = pakfile_registry.find(relfilename);
    if (it != pakfile_registry.end()) {
        auto &[fname, foff, flen, funcompressed] = it->second;
        auto l = LoadFileFromAny(fname, dest, foff, flen);
        if (l >= 0) {
            if (funcompressed >= 0) {
                string uncomp;
                WEntropyCoder<false>((const uchar *)dest->c_str(), dest->length(),
                                     (size_t)funcompressed, uncomp);
                dest->swap(uncomp);
                return funcompressed;
            } else {
                return l;
            }
        }
    }
    if (len > 0) Output(OUTPUT_INFO, "load: ", relfilename);
    return LoadFileFromAny(SanitizePath(relfilename), dest, start, len);
}

FILE *OpenForWriting(string_view relfilename, bool binary) {
    return fopen((writedir + SanitizePath(relfilename)).c_str(), binary ? "wb" : "w");
}

bool WriteFile(string_view relfilename, bool binary, string_view contents) {
    FILE *f = OpenForWriting(relfilename, binary);
    size_t written = 0;
    if (f) {
        written = fwrite(contents.data(), contents.size(), 1, f);
        fclose(f);
    }
    return written == 1;
}

// TODO: can now replace all this platform specific stuff with std::filesystem code.
// https://github.com/tvaneerd/cpp17_in_TTs/blob/master/ALL_IN_ONE.md
// http://en.cppreference.com/w/cpp/experimental/fs
bool ScanDirAbs(string_view absdir, vector<pair<string, int64_t>> &dest) {
    string folder = SanitizePath(absdir);
    #ifdef _WIN32
        WIN32_FIND_DATA fdata;
        HANDLE fh = FindFirstFile((folder + "\\*.*").c_str(), &fdata);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fdata.cFileName, ".") && strcmp(fdata.cFileName, "..")) {
                    ULONGLONG size =
                        (static_cast<ULONGLONG>(fdata.nFileSizeHigh) << (sizeof(uint) * 8)) |
                        fdata.nFileSizeLow;
                    dest.push_back(
                        { fdata.cFileName,
                          fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
                              ? -1
                              : (int64_t)size });
                }
            }
            while(FindNextFile(fh, &fdata));
            FindClose(fh);
            return true;
        }
    #elif !defined(PLATFORM_ES3)
        glob_t gl;
        string mask = folder + "/*";
        if (!glob(mask.c_str(), GLOB_MARK | GLOB_TILDE, nullptr, &gl)) {
            for (size_t fi = 0; fi < gl.gl_pathc; fi++) {
                string xFileName = gl.gl_pathv[fi];
                bool isDir = xFileName[xFileName.length()-1] == '/';
                if (isDir) xFileName = xFileName.substr(0, xFileName.length() - 1);
                string cFileName = xFileName.substr(xFileName.find_last_of('/') + 1);
                struct stat st;
                stat(gl.gl_pathv[fi], &st);
                dest.push_back({ cFileName, isDir ? -1 : (int64_t)st.st_size });
            }
            globfree(&gl);
            return true;
        }
    #endif
    return false;
}

bool ScanDir(string_view reldir, vector<pair<string, int64_t>> &dest) {
    auto srfn = SanitizePath(reldir);
    return ScanDirAbs(auxdir + srfn, dest) ||
           ScanDirAbs(datadir + srfn, dest) ||
           ScanDirAbs(writedir + srfn, dest);
}

OutputType min_output_level = OUTPUT_WARN;

void Output(OutputType ot, const char *buf) {
    if (ot < min_output_level) return;
    #ifdef __ANDROID__
        auto tag = "lobster";
        switch (ot) {
            case OUTPUT_DEBUG:   __android_log_print(ANDROID_LOG_DEBUG, tag, "%s", buf); break;
            case OUTPUT_INFO:    __android_log_print(ANDROID_LOG_INFO,  tag, "%s", buf); break;
            case OUTPUT_WARN:    __android_log_print(ANDROID_LOG_WARN,  tag, "%s", buf); break;
            case OUTPUT_PROGRAM: __android_log_print(ANDROID_LOG_ERROR, tag, "%s", buf); break;
            case OUTPUT_ERROR:   __android_log_print(ANDROID_LOG_ERROR, tag, "%s", buf); break;
        }
    #elif defined(_WIN32)
        OutputDebugStringA("LOG: ");
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
        if (ot >= OUTPUT_INFO) {
            printf("%s\n", buf);
        }
    #elif defined(__IOS__)
        extern void IOSLog(const char *msg);
        IOSLog(buf);
    #else
        printf("%s\n", buf);
    #endif
    if (!have_console) {
        auto f = fopen((exefile + ".con.log").c_str(), "a");
        if (f) {
            fputs(buf, f);
            fputs("\n", f);
            fclose(f);
        }
    }
}

// Use this instead of assert to break on a condition and still be able to continue in the debugger.
void ConditionalBreakpoint(bool shouldbreak) {
    if (shouldbreak) {
        #ifdef _WIN32
            __debugbreak();
        #elif __GCC__
            __builtin_trap();
        #endif
    }
}

void MakeDPIAware() {
    #ifdef _WIN32
        // Without this, Windows scales the GL window if scaling is set in display settings.
        #ifndef DPI_ENUMS_DECLARED
            typedef enum PROCESS_DPI_AWARENESS
            {
                PROCESS_DPI_UNAWARE = 0,
                PROCESS_SYSTEM_DPI_AWARE = 1,
                PROCESS_PER_MONITOR_DPI_AWARE = 2
            } PROCESS_DPI_AWARENESS;
        #endif

        typedef BOOL (WINAPI * SETPROCESSDPIAWARE_T)(void);
        typedef HRESULT (WINAPI * SETPROCESSDPIAWARENESS_T)(PROCESS_DPI_AWARENESS);
        HMODULE shcore = LoadLibraryA("Shcore.dll");
        SETPROCESSDPIAWARENESS_T SetProcessDpiAwareness = NULL;
        if (shcore) {
            SetProcessDpiAwareness =
                (SETPROCESSDPIAWARENESS_T)GetProcAddress(shcore, "SetProcessDpiAwareness");
        }
        HMODULE user32 = LoadLibraryA("User32.dll");
        SETPROCESSDPIAWARE_T SetProcessDPIAware = NULL;
        if (user32) {
            SetProcessDPIAware =
                (SETPROCESSDPIAWARE_T)GetProcAddress(user32, "SetProcessDPIAware");
        }

        if (SetProcessDpiAwareness) {
            SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
        } else if (SetProcessDPIAware) {
            SetProcessDPIAware();
        }

        if (user32) FreeLibrary(user32);
        if (shcore) FreeLibrary(shcore);
    #endif
}

string GetDateTime() {
    time_t t;
    time(&t);
    auto tm = localtime(&t);
    char buf[1024];
    strftime(buf, sizeof(buf), "%F-%H-%M-%S", tm);
    return buf;
}

void SetConsole(bool on) {
    have_console = on;
    #ifdef _WIN32
        if (on) AllocConsole();
        else FreeConsole();
    #endif
}
