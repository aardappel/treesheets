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

#include "lobster/vmdata.h"
#include "lobster/natreg.h"

#include "stdint.h"

#ifdef _WIN32
    #define VC_EXTRALEAN
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #ifndef __ANDROID__
        #include <glob.h>
    #endif
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace lobster {

void AddFile() {
    STARTDECL(scan_folder) (Value &fld, Value &divisor) {
        vector<pair<string, int64_t>> dir;
        auto ok = ScanDirAbs(fld.sval()->str(), dir);
        fld.DECRT();
        if (!ok) {
            g_vm->Push(Value());
            return Value();
        }
        if (divisor.ival() <= 0) divisor.setival(1);
        auto nlist = (LVector *)g_vm->NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);
        auto slist = (LVector *)g_vm->NewVec(0, 0, TYPE_ELEM_VECTOR_OF_INT);
        for (auto &p : dir) {
            nlist->Push(Value(g_vm->NewString(p.first.c_str(), strlen(p.first.c_str()))));
            auto size = p.second;
            if (size >= 0) {
                size /= divisor.ival();
                if (sizeof(intp) == sizeof(int) && size > 0x7FFFFFFF) size = 0x7FFFFFFF;
            }
            slist->Push(Value(size));
        }
        g_vm->Push(Value(nlist));
        return Value(slist);
    }
    ENDDECL2(scan_folder, "folder,divisor", "SI", "S]?I]?",
        "returns two vectors representing all elements in a folder, the first vector containing all"
        " names, the second vector containing sizes (or -1 if a directory)."
        " Specify 1 as divisor to get sizes in bytes, 1024 for kb etc. Values > 0x7FFFFFFF will be"
        " clamped in 32-bit builds. Returns nil if folder couldn't be scanned.");

    STARTDECL(read_file) (Value &file) {
        string buf;
        auto l = LoadFile(file.sval()->str(), &buf);
        file.DECRT();
        if (l < 0) return Value();
        auto s = g_vm->NewString(buf);
        return Value(s);
    }
    ENDDECL1(read_file, "file", "S", "S?",
        "returns the contents of a file as a string, or nil if the file can't be found."
        " you may use either \\ or / as path separators");

    STARTDECL(write_file) (Value &file, Value &contents) {
        auto ok = WriteFile(file.sval()->str(), true, contents.sval()->str(), contents.sval()->len);
        file.DECRT();
        contents.DECRT();
        return Value(ok);
    }
    ENDDECL2(write_file, "file,contents", "SS", "I",
        "creates a file with the contents of a string, returns false if writing wasn't possible");
}

}
