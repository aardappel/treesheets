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
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
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

#ifdef __linux__
    #include <unistd.h>
#endif

#ifdef __APPLE__
    #include "CoreFoundation/CoreFoundation.h"
    #include <libproc.h>
    #ifndef __IOS__
        #include <Carbon/Carbon.h>
    #endif
#endif

#ifdef __ANDROID__
    #include <android/log.h>
    #include "lobster/sdlincludes.h"  // FIXME
#endif

#include "subprocess.h"

// Dirs to load files relative to, they typically contain, and will be searched in this order:
// - The project specific files. This is where the bytecode file you're running or the main
//   .lobster file you're compiling reside.
// - The standard lobster files. On windows this is where lobster.exe resides, on apple
//   platforms it's the Resource folder in the bundle.
// - The same as writedir below (to be able to load files the program has been written).
// - Any additional dirs declared with "include from".
vector<string> data_dirs;

// Folder to write to, usually the same as project dir, special folder on mobile platforms.
string write_dir;

string maindir;
string projectdir;
string_view ProjectDir() { return projectdir; }
string_view MainDir() { return maindir; }

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

int hwthreads = 2, hwcores = 1;
void InitCPU() {
    // This can fail and return 0, so default to 2 threads:
    hwthreads = max(2, (int)thread::hardware_concurrency());
    // As a baseline, assume desktop CPUs are hyperthreaded, and mobile ones are not.
    #ifdef PLATFORM_ES3
        hwcores = hwthreads;
    #else
        hwcores = max(1, hwthreads / 2);
    #endif
    // On Windows, we can do better and actually count cores.
    #ifdef _WIN32
        DWORD buflen = 0;
        if (!GetLogicalProcessorInformation(nullptr, &buflen) && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(buflen / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
            if (GetLogicalProcessorInformation(buf.data(), &buflen)) {
                int cores = 0;
                for (auto &lpi : buf) {
                    if (lpi.Relationship == RelationProcessorCore) cores++;
                }
                // Only overwrite our baseline if we actually found any cores.
                if (cores) hwcores = cores;
            }
        }
    #endif
}

int NumHWThreads() { return hwthreads; }
int NumHWCores() { return hwcores; }


string_view StripFilePart(string_view filepath) {
    auto fpos = filepath.find_last_of(FILESEP);
    return fpos != string_view::npos ? filepath.substr(0, fpos + 1) : "";
}

const char *StripDirPart(const char *filepath) {
    auto fpos = strrchr(filepath, FILESEP);
    if (!fpos) fpos = strrchr(filepath, ':');
    return fpos ? fpos + 1 : filepath;
}

string_view StripTrailing(string_view in, string_view tail) {
    if (in.size() >= tail.size() && in.substr(in.size() - tail.size()) == tail)
        return in.substr(0, in.size() - tail.size());
    return in;
}

string GetMainDirFromExePath(const char *argv_0) {
    string md = SanitizePath(argv_0);
    #ifdef _WIN32
        // Windows can pass just the exe name without a full path, which is useless.
        char winfn[MAX_PATH + 1];
        GetModuleFileName(NULL, winfn, MAX_PATH + 1);
        md = winfn;
    #endif
    #ifdef __linux__
        char path[PATH_MAX];
        iint length = readlink("/proc/self/exe", path, sizeof(path)-1);
        if (length != -1) {
          path[length] = '\0';
          md = string(path);
        }
    #endif
    #ifdef __APPLE__
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(getpid(), path, sizeof(path)) > 0) {
            md = string(path);
        }
    #endif
    md = StripTrailing(StripTrailing(StripFilePart(md), "bin/"), "bin\\");
    return md;
}

int64_t DefaultLoadFile(string_view absfilename, string *dest, int64_t start, int64_t len) {
    LOG_INFO("DefaultLoadFile: ", absfilename);
    auto f = fopen(null_terminated(absfilename), "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return -1;
    }
    auto filelen = ftell(f);
    if (!len) {  // Just the file length requested.
        fclose(f);
        return filelen;
    }
    if (len < 0) len = filelen;
    fseek(f, (long)start, SEEK_SET);  // FIXME: 32-bit on WIN32.
    dest->resize((size_t)len);
    auto rlen = fread(&(*dest)[0], 1, (size_t)len, f);
    fclose(f);
    return len != (int64_t)rlen ? -1 : len;
}

bool InitPlatform(string _maindir, const char *auxfilepath, bool from_bundle,
                      FileLoader loader) {
    maindir = _maindir;
    InitTime();
    InitCPU();
    cur_loader = loader;
    // FIXME: use SDL_GetBasePath() instead?
    #if defined(__APPLE__)
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
            auto resources_dir = string(path) + "/";
            data_dirs.push_back(resources_dir);
            #ifdef __IOS__
                // There's probably a better way to do this in CF.
                write_dir = StripFilePart(path) + "Documents/";
                data_dirs.push_back(write_dir);
            #else
                // FIXME: This should probably be ~/Library/Application Support/AppName,
                // but for now this works for non-app store apps.
                write_dir = resources_dir;
            #endif
            return true;
        }
    #else
        (void)from_bundle;
    #endif
    #if defined(__ANDROID__)
        // FIXME: removed SDL dependency for other platforms. So on Android, move calls to
        // SDL_AndroidGetInternalStoragePath into SDL and pass the results into here somehow.
        SDL_Init(0); // FIXME: Is this needed? bad dependency.
        auto internalstoragepath = SDL_AndroidGetInternalStoragePath();
        auto externalstoragepath = SDL_AndroidGetExternalStoragePath();
        LOG_INFO(internalstoragepath);
        LOG_INFO(externalstoragepath);
        if (internalstoragepath) data_dirs.push_back(internalstoragepath + string_view("/"));
        if (externalstoragepath) write_dir = externalstoragepath + string_view("/");
        // For some reason, the above SDL functionality doesn't actually work,
        // we have to use the relative path only to access APK files:
        data_dirs.clear();  // FIXME.
        data_dirs.push_back("");
        data_dirs.push_back(write_dir);
    #else  // Linux, Windows, and OS X console mode.

        if (auxfilepath) {
            projectdir = StripFilePart(SanitizePath(auxfilepath));
            data_dirs.push_back(projectdir);
            write_dir = projectdir;
        } else {
            write_dir = maindir;
        }
        data_dirs.push_back(maindir);
        #ifdef PLATFORM_DATADIR
            data_dirs.push_back(PLATFORM_DATADIR);
        #endif
        #ifdef _WIN32
            have_console = GetConsoleWindow() != nullptr;
        #endif
    #endif
    return true;
}

void AddDataDir(string_view path) {
    for (auto &dir : data_dirs) if (dir == path) return;
    data_dirs.push_back(projectdir + SanitizePath(path));
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
    if (srelfilename.size() > 1 && (srelfilename[0] == FILESEP || srelfilename[1] == ':')) {
        // Absolute filename.
        return cur_loader(srelfilename, dest, start, len);
    }
    for (auto &dir : data_dirs) {
        auto l = cur_loader(dir + srelfilename, dest, start, len);
        if (l >= 0) return l;
    }
    return -1;
}

// We don't generally load in ways that allow stdio text mode conversions, so this function
// emulates them at best effort.
void TextModeConvert(string &s, bool binary) {
    if (binary) return;
    #ifdef _WIN32
        s.erase(remove(s.begin(), s.end(), '\r'), s.end());
    #endif
}

int64_t LoadFile(string_view relfilename, string *dest, int64_t start, int64_t len, bool binary) {
    assert(cur_loader);
    auto it = pakfile_registry.find(relfilename);
    if (it != pakfile_registry.end()) {
        auto &[fname, foff, flen, funcompressed] = it->second;
        auto l = LoadFileFromAny(fname, dest, foff, flen);
        if (l >= 0) {
            if (funcompressed >= 0) {
                string uncomp;
                WEntropyCoder<false>((const uint8_t *)dest->c_str(), dest->length(),
                                     (size_t)funcompressed, uncomp);
                dest->swap(uncomp);
                TextModeConvert(*dest, binary);
                return funcompressed;
            } else {
                TextModeConvert(*dest, binary);
                return l;
            }
        }
    }
    if (len > 0) LOG_INFO("load: ", relfilename);
    auto size = LoadFileFromAny(SanitizePath(relfilename), dest, start, len);
    TextModeConvert(*dest, binary);
    return size;
}

string WriteFileName(string_view relfilename) {
    return write_dir + SanitizePath(relfilename);
}

FILE *OpenForWriting(string_view relfilename, bool binary) {
    auto fn = WriteFileName(relfilename);
    LOG_INFO("write: ", fn);
    return fopen(fn.c_str(), binary ? "wb" : "w");
}

FILE *OpenForReading(string_view relfilename, bool binary) {
    auto fn = WriteFileName(relfilename);
    LOG_INFO("read: ", fn);
    return fopen(fn.c_str(), binary ? "rb" : "r");
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

bool FileExists(string_view relfilename) {
    auto f = fopen(WriteFileName(relfilename).c_str(), "rb");
    if (f) fclose(f);
    return f;
}

bool FileDelete(string_view relfilename) {
    return remove(WriteFileName(relfilename).c_str()) == 0;
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
                    auto size =
                        (static_cast<uint64_t>(fdata.nFileSizeHigh) << (sizeof(uint32_t) * 8)) |
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
    for (auto &dir : data_dirs) {
        if (ScanDirAbs(dir + srfn, dest)) return true;
    }
    return false;
}

OutputType min_output_level = OUTPUT_WARN;

void LogOutput(OutputType ot, const char *buf) {
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
        auto f = fopen((maindir + "lobster.exe.con.log").c_str(), "a");
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

// Insert without args to find out which iteration it gets to, then insert that iteration number.
void CountingBreakpoint(int i) {
    static int j = 0;
    if (i < 0) LOG_DEBUG("counting breakpoint: ", j);
    ConditionalBreakpoint(j++ == i);
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
    strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", tm);
    return buf;
}

void SetConsole(bool on) {
    have_console = on;
    #ifdef _WIN32
        if (on) AllocConsole();
        else FreeConsole();
    #endif
}

iint LaunchSubProcess(const char **cmdl, const char *stdins, string &out) {
    struct subprocess_s subprocess;
    int result = subprocess_create(cmdl, 0, &subprocess);
    if (result) return -1;
    if (stdins) {
        FILE *p_stdin = subprocess_stdin(&subprocess);
        fputs(stdins, p_stdin);
        fclose(p_stdin);
    }
    int process_return;
    result = subprocess_join(&subprocess, &process_return);
    if (result) {
        subprocess_destroy(&subprocess);
        return -1;
    }
    auto readall = [&](FILE *f) {
        for (;;) {
            auto c = getc(f);
            if (c < 0) break;
            out.push_back((char)c);
        }
    };
    readall(subprocess_stdout(&subprocess));
    readall(subprocess_stderr(&subprocess));
    subprocess_destroy(&subprocess);
    return process_return;
}
