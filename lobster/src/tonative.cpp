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
#include "lobster/disasm.h"  // Some shared bytecode utilities.
#include "lobster/compiler.h"
#include "lobster/tonative.h"

namespace lobster {

int ParseOpAndGetArity(int opc, const int *&ip) {
    auto arity = ILArity()[opc];
    auto ips = ip;
    switch(opc) {
        default: {
            assert(arity != ILUNKNOWNARITY);
            ip += arity;
            break;
        }
        case IL_CORO: {
            ip += 2;
            int n = *ip++;
            ip += n;
            arity = int(ip - ips);
            break;
        }
        case IL_FUNSTART: {
            ip++;  // function idx.
            int n = *ip++;
            ip += n;
            int m = *ip++;
            ip += m;
            ip++;  // keepvar
            int o = *ip++;  // ownedvar
            ip += o;
            arity = int(ip - ips);
            break;
        }
    }
    return arity;
}

string ToNative(NativeRegistry &natreg, NativeGenerator &ng,
              string_view bytecode_buffer) {
    auto bcf = bytecode::GetBytecodeFile(bytecode_buffer.data());
    assert(FLATBUFFERS_LITTLEENDIAN);
    auto code = (const int *)bcf->bytecode()->Data();  // Assumes we're on a little-endian machine.
    //auto typetable = (const type_elem_t *)bcf->typetable()->Data();  // Same.
    map<int, const bytecode::Function *> function_lookup;
    for (flatbuffers::uoffset_t i = 0; i < bcf->functions()->size(); i++) {
        auto f = bcf->functions()->Get(i);
        function_lookup[f->bytecodestart()] = f;
    }
    ng.FileStart();
    auto len = bcf->bytecode()->Length();
    vector<int> block_ids(bcf->bytecode_attr()->size(), -1);
    const int *ip = code;
    // Skip past 1st jump.
    assert(*ip == IL_JUMP);
    ip++;
    auto starting_point = *ip++;
    int block_id = 1;
    while (ip < code + len) {
        if (bcf->bytecode_attr()->Get((flatbuffers::uoffset_t)(ip - code)) & bytecode::Attr_SPLIT) {
            auto id = block_ids[ip - code] = block_id++;
            ng.DeclareBlock(id);
        }
        if ((false)) {  // Debug corrupt bytecode.
            ostringstream dss;
            DisAsmIns(natreg, dss, ip, code, (const type_elem_t *)bcf->typetable()->Data(), bcf);
            LOG_DEBUG(dss.str());
        }
        int opc = *ip++;
        if (opc < 0 || opc >= IL_MAX_OPS) {
            return cat("Corrupt bytecode: ", opc, " at: ", ip - 1 - code);
        }
        ParseOpAndGetArity(opc, ip);
    }
    ng.BeforeBlocks(block_ids[starting_point], bytecode_buffer);
    ip = code + 2;
    bool already_returned = false;
    while (ip < code + len) {
        int opc = *ip++;
        if (opc == IL_FUNSTART) {
            auto it = function_lookup.find((int)(ip - 1 - code));
            ng.FunStart(it != function_lookup.end() ? it->second : nullptr);
        }
        auto args = ip;
        if (bcf->bytecode_attr()->Get((flatbuffers::uoffset_t)(ip - 1 - code)) & bytecode::Attr_SPLIT) {
            auto cid = block_ids[args - 1 - code];
            ng.current_block_id = cid;
            ng.BlockStart(cid);
            already_returned = false;
        }
        auto arity = ParseOpAndGetArity(opc, ip);
        auto is_vararg = ILArity()[opc] == ILUNKNOWNARITY;
        ng.InstStart();
        if (opc == IL_JUMP) {
            already_returned = true;
            ng.EmitJump(block_ids[args[0]]);
        } else if ((opc >= IL_JUMPFAIL && opc <= IL_JUMPNOFAILR) ||
                   (opc >= IL_IFOR && opc <= IL_VFOR)) {
            auto id = block_ids[args[0]];
            assert(id >= 0);
            ng.EmitConditionalJump(opc, id);
        } else {
            ng.EmitOperands(bytecode_buffer.data(), args, arity, is_vararg);
            if (ISBCALL(opc) &&
                       natreg.nfuns[args[0]]->CanChangeControlFlow()) {
                ng.SetNextCallTarget(block_ids[ip - code]);
            }
            int target = -1;
            if (opc == IL_CALL || opc == IL_CALLV || opc == IL_CALLVCOND ||
                opc == IL_YIELD || opc == IL_DDCALL) {
                target = block_ids[ip - code];
            } else if (opc == IL_PUSHFUN || opc == IL_CORO) {
                target = block_ids[args[0]];
            }
            ng.EmitGenericInst(opc, args, arity, is_vararg, target);
            if (ISBCALL(opc)) {
                ng.Annotate(natreg.nfuns[args[0]]->name);
            } else if (opc == IL_PUSHVAR) {
                ng.Annotate(IdName(bcf, args[0]));
            } else if (ISLVALVARINS(opc)) {
                ng.Annotate(IdName(bcf, args[0]));
            } else if (opc == IL_PUSHSTR) {
                ostringstream css;
                EscapeAndQuote(bcf->stringtable()->Get(args[0])->string_view(), css);
                ng.Annotate(css.str());
            } else if (opc == IL_CALL) {
                auto fs = code + args[0];
                assert(*fs == IL_FUNSTART);
                fs++;
                ng.Annotate(bcf->functions()->Get(*fs)->name()->string_view());
            }
            if (opc == IL_CALL) {
                ng.EmitCall(block_ids[args[0]]);
                already_returned = true;
            } else if (opc == IL_CALLV || opc == IL_YIELD || opc == IL_COEND || opc == IL_RETURN ||
                       opc == IL_DDCALL ||
                       // FIXME: make resume a vm op.
                       (ISBCALL(opc) &&
                        natreg.nfuns[args[0]]->CanChangeControlFlow())) {
                ng.EmitCallIndirect();
                already_returned = true;
            } else if (opc == IL_CALLVCOND) {
                ng.EmitCallIndirectNull();
            }
        }
        ng.InstEnd();
        if (bcf->bytecode_attr()->Get((flatbuffers::uoffset_t)(ip - code)) & bytecode::Attr_SPLIT) {
            ng.BlockEnd(block_ids[ip - code], already_returned, opc == IL_EXIT);
        }
    }
    ng.CodeEnd();
    vector<int> vtables;
    for (auto bcs : *bcf->vtables()) {
        int id = -1;
        if (bcs >= 0) {
            id = block_ids[bcs];
            assert(id >= 0);
        }
        vtables.push_back(id);
    }
    ng.VTables(vtables);
    ng.FileEnd(block_ids[starting_point], bytecode_buffer);
    return "";
}

}
