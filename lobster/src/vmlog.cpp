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

namespace lobster {

VMLog::VMLog(VM &_vm) : vm(_vm) {}

void VMLog::LogInit(const bytecode::BytecodeFile *bcf) {
    logvars.resize(bcf->logvars()->size());
    flatbuffers::uoffset_t i = 0;
    for (auto &l : logvars) {
        auto sid = bcf->logvars()->Get(i++);
        l.read = 0;
        l.type = &vm.GetTypeInfo((type_elem_t)bcf->specidents()->Get(sid)->typeidx());
    }
}

void VMLog::LogPurge() {
    for (auto &l : logvars) {
        if (IsRefNil(l.type->t)) {
            for (size_t i = l.read; i < l.values.size(); i++) l.values[i].DECRTNIL();
        }
        l.values.resize(l.read);
    }
}
void VMLog::LogFrame() {
    LogPurge();
    for (auto &l : logvars) l.read = 0;
};


Value VMLog::LogGet(Value def, int idx) {
    auto &l = logvars[idx];
    bool isref = IsRefNil(l.type->t);
    if (l.read == l.values.size()) {  // Value doesn't exist yet.
        // Already write value, so it can be written to regardless of wether it existed or not.
        l.values.push_back(def);
        if (isref) def.INCRTNIL();
        l.read++;
        return def;
    } else {
        // Get existing value, ignore default.
        auto v = l.values[l.read++];
        if (isref) { v.INCRTNIL(); def.DECRTNIL(); }
        return v;
    }
}

void VMLog::LogWrite(Value newval, int idx) {
    auto &l = logvars[idx];
    bool isref = IsRefNil(l.type->t);
    assert(l.read > 0);
    auto &slot = l.values[l.read - 1];
    if (isref) { slot.DECRTNIL(); newval.INCRTNIL(); }
    slot = newval;
}

void VMLog::LogCleanup() {
    for (auto &l : logvars) l.read = 0;
    LogPurge();
}

void VMLog::LogMark() {
    for (auto &l : logvars) {
        if (IsRefNil(l.type->t)) for (auto v : l.values) v.MarkRef();
    }
}

}  // namespace lobster
