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
    #include <sapi.h>
    #include <comdef.h>
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

#ifndef PLATFORM_ES3
    #include "subprocess.h"
#endif

// Dirs to load files relative to, they typically contain, and will be searched in this order:
// - The project specific files. This is where the bytecode file you're running or the main
//   .lobster file you're compiling reside.
// - The standard lobster files. On windows this is where lobster.exe resides, on apple
//   platforms it's the Resource folder in the bundle.
// - The same as writedir below (to be able to load files the program has been written).
// - Any additional dirs declared with "include from".
vector<string> data_dirs;

// Folders to write to, usually the same as project dir, special folder on mobile platforms.
vector<string> write_dirs;

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

int hwthreads = 2;
int hwcores = 1;
bool hwpopcount = true;
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
        #ifdef _M_ARM64
            hwpopcount = false;
        #else
            int ci[4];
            __cpuid(ci, 1);
            if (!((ci[2] >> 23) & 1)) hwpopcount = false;
        #endif
    #endif
}

int NumHWThreads() { return hwthreads; }
int NumHWCores() { return hwcores; }


string_view StripFilePart(string_view filepath) {
    auto fpos = filepath.find_last_of(FILESEP);
    return fpos != string_view::npos ? filepath.substr(0, fpos + 1) : "";
}

string StripDirPart(string_view_nt filepath) {
    auto fp = filepath.c_str();
    auto fpos = strrchr(fp, FILESEP);
    if (!fpos) fpos = strrchr(fp, ':');
    return fpos ? fpos + 1 : string(filepath.sv);
}

string_view StripTrailing(string_view in, string_view tail) {
    if (in.size() >= tail.size() && in.substr(in.size() - tail.size()) == tail)
        return in.substr(0, in.size() - tail.size());
    return in;
}


bool IsAbsolute(string_view filename) {
    return filename.size() > 1 && (filename[0] == '/' || filename[0] == '\\' || filename[1] == ':');
}

string GetMainDirFromExePath(string_view argv_0) {
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

int64_t DefaultLoadFile(string_view_nt absfilename, string *dest, int64_t start, int64_t len) {
    LOG_INFO("DefaultLoadFile: ", absfilename);
    auto f = fopen(absfilename.c_str(), "rb");
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

bool InitPlatform(string _maindir, string_view auxfilepath, bool from_bundle,
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
                auto write_dir = StripFilePart(path) + "Documents/";
                write_dirs.push_back(write_dir);
                data_dirs.push_back(write_dir);
            #else
                // FIXME: This should probably be ~/Library/Application Support/AppName,
                // but for now this works for non-app store apps.
                write_dirs.push_back(resources_dir);
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
        // For some reason, the above SDL functionality doesn't actually work,
        // we have to use the relative path only to access APK files:
        data_dirs.clear();  // FIXME.
        data_dirs.push_back("");
        if (externalstoragepath) {
            auto write_dir = externalstoragepath + string_view("/");
            write_dirs.push_back(write_dir);
            data_dirs.push_back(write_dir);
        }
    #else  // Linux, Windows, and OS X console mode.

        if (!auxfilepath.empty()) {
            projectdir = StripFilePart(SanitizePath(auxfilepath));
            data_dirs.push_back(projectdir);
            write_dirs.push_back(projectdir);
        } else {
            write_dirs.push_back(maindir);
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
    auto fpath = SanitizePath(path);
    // Add slash at the end if missing, otherwise when loading it will fail.
    if (!fpath.empty() && fpath[fpath.length() - 1] != FILESEP) fpath = fpath + FILESEP;
    if (!IsAbsolute(fpath)) fpath = projectdir + fpath;
    for (auto &dir : data_dirs) if (dir == fpath) goto skipd;
    data_dirs.push_back(fpath);
    skipd:
    // FIXME: this is not the greatest solution, maybe we should should separate
    // setting these from import dirs.
    for (auto &dir : write_dirs) if (dir == fpath) return;
    write_dirs.push_back(fpath);
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

string last_abs_path_loaded;
string LastAbsPathLoaded() { return last_abs_path_loaded; }

int64_t LoadFileFromAny(string_view filename, string *dest, int64_t start, int64_t len) {
    if (IsAbsolute(filename)) {
        // Absolute filename.
        return cur_loader(last_abs_path_loaded = string(filename), dest, start, len);
    }
    for (auto &dir : data_dirs) {
        auto l = cur_loader(last_abs_path_loaded = dir + filename, dest, start, len);
        if (l >= 0) return l;
    }
    LOG_DEBUG("LoadFileFromAny: ", filename, " file not found");
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

FILE *OpenFor(string_view filename, const char *mode, bool allow_absolute) {
    if (IsAbsolute(filename)) {
        if (allow_absolute) {
            auto f = fopen(SanitizePath(filename).c_str(), mode);
            if (f) return f;
        }
    } else {
        for (auto &wd : write_dirs) {
            auto f = fopen((wd + SanitizePath(filename)).c_str(), mode);
            if (f) return f;
        }
    }
    return nullptr;
}

FILE *OpenForWriting(string_view filename, bool binary, bool allow_absolute) {
    auto f = OpenFor(filename, binary ? "wb" : "w", allow_absolute);
    LOG_INFO("write: ", filename);
    return f;
}

FILE *OpenForReading(string_view filename, bool binary, bool allow_absolute) {
    auto f = OpenFor(filename, binary ? "rb" : "r", allow_absolute);
    LOG_INFO("read: ", filename);
    return f;
}

bool WriteFile(string_view filename, bool binary, string_view contents, bool allow_absolute) {
    FILE *f = OpenForWriting(filename, binary, allow_absolute);
    size_t written = 0;
    if (f) {
        written = fwrite(contents.data(), contents.size(), 1, f);
        fclose(f);
    }
    return written == 1;
}

bool RenameFile(string_view oldfilename, string_view newfilename) {
    int result = rename(SanitizePath(oldfilename).c_str(), SanitizePath(newfilename).c_str());
    return result == 0;
}

bool FileExists(string_view filename, bool allow_absolute) {
    auto f = OpenFor(filename, "rb", allow_absolute);
    if (f) fclose(f);
    return f;
}

bool FileDelete(string_view relfilename) {
    // FIXME: not super safe? tries to delete in every import dir.
    for (auto &wd : write_dirs) {
        if (remove((wd + SanitizePath(relfilename)).c_str()) == 0) return true;
    }
    return false;
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
    // First check the pakfile.
    for (auto [prfn, tup] : pakfile_registry) {
        if (prfn.find(reldir) == 0) {
            auto pos = reldir.size();
            if (prfn.find_first_of("/\\", pos) == pos) pos++;  // Skip first separator if any.
            if (prfn.find_first_of("/\\", pos) != string::npos) continue;  // Item in subdir.
            dest.push_back({ prfn.substr(pos), get<2>(tup) });
        }
    }
    // Even if we found things in pakfile, we scan filesystem additionally, since LoadFile
    // supports loading from both in this order too.
    auto srfn = SanitizePath(reldir);
    for (auto &dir : data_dirs) {
        if (ScanDirAbs(dir + srfn, dest)) return true;
    }
    return !dest.empty();  // If we found some in pak, missing filesystem dir is not a failure.
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
    #ifndef PLATFORM_ES3
        struct subprocess_s subprocess;
        int result = subprocess_create(cmdl,
            subprocess_option_inherit_environment
            | subprocess_option_enable_async, &subprocess);
        if (result) return -1;
        if (stdins) {
            FILE *p_stdin = subprocess_stdin(&subprocess);
            fputs(stdins, p_stdin);
            fclose(p_stdin);
        }
        const unsigned int buflen = 256;
        const char buf[buflen] = "";
        auto read_async = [&](unsigned int (*subprocess_read)(subprocess_s*, char *const, const unsigned int)) {
            for (;;) {
                unsigned int readlength = subprocess_read(&subprocess, (char *const)buf, buflen);
                if (!readlength) break;
                out.append(buf, readlength);
            }
        };
        read_async(subprocess_read_stdout);
        read_async(subprocess_read_stderr);
        int process_return;
        result = subprocess_join(&subprocess, &process_return);
        subprocess_destroy(&subprocess);
        if (result) {
            return -1;
        }
        return process_return;
    #else
        (void)cmdl;
        (void)stdins;
        (void)out;
        return -1;
    #endif
}

vector<string> text_to_speech_q;
void QueueTextToSpeech(string_view text) {
    text_to_speech_q.emplace_back(string(text));
}

#ifdef _WIN32
ISpVoice *voice = NULL;
#endif

bool TextToSpeechInit() {
    #ifdef _WIN32
        if (FAILED(::CoInitializeEx(NULL, COINITBASE_MULTITHREADED))) return false;
    #endif
    return true;
}

void StopTextToSpeech() {
    text_to_speech_q.clear();
    #ifdef _WIN32
        if (!voice) return;
        voice->Release();
        voice = NULL;
    #endif
}

bool TextToSpeechUpdate() {
    #ifdef _WIN32
        // First see if we have active speech, and if so, don't start a new one.
        if (voice) {
            SPVOICESTATUS status;
            if (FAILED(voice->GetStatus(&status, NULL))) return false;
            if (status.dwRunningState != SPRS_DONE) return true;
            voice->Release();
            voice = NULL;
        }
        // Have something new to start?
        if (text_to_speech_q.empty()) return true;
        _bstr_t wide(text_to_speech_q.front().c_str());
        text_to_speech_q.erase(text_to_speech_q.begin());
        if (FAILED(
                CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void **)&voice)))
            return false;
        voice->Speak(wide, SPF_IS_XML | SPF_ASYNC, NULL);
        return true;
    #else
        return false;
    #endif
}
