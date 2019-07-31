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
#include "lobster/disasm.h"

namespace lobster {

const bytecode::LineInfo *LookupLine(const int *ip, const int *code,
                                            const bytecode::BytecodeFile *bcf) {
    auto lineinfo = bcf->lineinfo();
    int pos = int(ip - code);
    int start = 0;
    auto size = lineinfo->size();
    assert(size);
    for (;;) {  // quick hardcoded binary search
        if (size == 1) return lineinfo->Get(start);
        auto nsize = size / 2;
        if (lineinfo->Get(start + nsize)->bytecodestart() <= pos) {
            start += nsize;
            size -= nsize;
        } else {
            size = nsize;
        }
    }
}

const int *DisAsmIns(NativeRegistry &nfr, ostringstream &ss, const int *ip, const int *code,
                            const type_elem_t *typetable, const bytecode::BytecodeFile *bcf) {
    auto ilnames = ILNames();
    auto ilarity = ILArity();
    auto li = LookupLine(ip, code, bcf);
    // FIXME: some indication of the filename, maybe with a table index?
    ss << "I " << int(ip - code) << " \tL " << li->line() << " \t";
    if (*ip < 0 || *ip >= IL_MAX_OPS) {
        ss << "ILLEGAL INSTRUCTION: " << *ip;
        return nullptr;
    }
    ss << ilnames[*ip] << ' ';
    auto arity = ilarity[*ip];
    auto ins_start = ip;
    int opc = *ip++;
    if (opc < 0 || opc >= IL_MAX_OPS) {
        ss << opc << " ?";
        return ip;
    }
    int is_struct = 0;
    switch(opc) {
        case IL_PUSHINT64:
        case IL_PUSHFLT64: {
            auto a = *ip++;
            auto v = Int64FromInts(a, *ip++);
            if (opc == IL_PUSHINT64) ss << v;
            else {
                int2float64 i2f;
                i2f.i = v;
                ss << i2f.f;
            }
            break;
        }

        case IL_LOGWRITE:
        case IL_KEEPREF:
            ss << *ip++ << ' ';
            ss << *ip++;
            break;

        case IL_RETURN: {
            auto id = *ip++;
            ip++;  // retvals
            ss << bcf->functions()->Get(id)->name()->string_view();
            break;
        }

        case IL_CALL: {
            auto bc = *ip++;
            assert(code[bc] == IL_FUNSTART);
            auto id = code[bc + 1];
            auto nargs = code[bc];
            ss << nargs << ' ' << bcf->functions()->Get(id)->name()->string_view();
            ss << ' ' << bc;
            break;
        }

        case IL_NEWVEC: {
            ip++;  // ti
            auto nargs = *ip++;
            ss << "vector " << nargs;
            break;
        }
        case IL_ST2S:
        case IL_NEWOBJECT: {
            auto ti = (TypeInfo *)(typetable + *ip++);
            ss << bcf->udts()->Get(ti->structidx)->name()->string_view();
            break;
        }

        case IL_BCALLRETV:
        case IL_BCALLRET0:
        case IL_BCALLRET1:
        case IL_BCALLRET2:
        case IL_BCALLRET3:
        case IL_BCALLRET4:
        case IL_BCALLRET5:
        case IL_BCALLRET6:
        case IL_BCALLREFV:
        case IL_BCALLREF0:
        case IL_BCALLREF1:
        case IL_BCALLREF2:
        case IL_BCALLREF3:
        case IL_BCALLREF4:
        case IL_BCALLREF5:
        case IL_BCALLREF6:
        case IL_BCALLUNBV:
        case IL_BCALLUNB0:
        case IL_BCALLUNB1:
        case IL_BCALLUNB2:
        case IL_BCALLUNB3:
        case IL_BCALLUNB4:
        case IL_BCALLUNB5:
        case IL_BCALLUNB6: {
            int a = *ip++;
            ss << nfr.nfuns[a]->name;
            break;
        }

        #undef LVAL
        #define LVAL(N, V) case IL_VAR_##N: is_struct = V; goto var;
            LVALOPNAMES
        #undef LVAL
        case IL_PUSHVARV:
            is_struct = 1;
        case IL_PUSHVAR:
        var:
            ss << IdName(bcf, *ip++);
            if (is_struct) ss << ' ' << *ip++;
            break;

        case IL_PUSHFLT:
            ss << *(float *)ip;
            ip++;
            break;

        case IL_PUSHSTR:
            EscapeAndQuote(bcf->stringtable()->Get(*ip++)->string_view(), ss);
            break;

        case IL_FUNSTART: {
            auto fidx = *ip++;
            ss << (fidx >= 0 ? bcf->functions()->Get(fidx)->name()->string_view() : "__dummy");
            ss << "(";
            int n = *ip++;
            while (n--) ss << IdName(bcf, *ip++) << ' ';
            n = *ip++;
            ss << "=> ";
            while (n--) ss << IdName(bcf, *ip++) << ' ';
            auto keepvars = *ip++;
            if (keepvars) ss << "K:" << keepvars << ' ';
            n = *ip++;  // owned
            while (n--) ss << "O:" << IdName(bcf, *ip++) << ' ';
            ss << ")";
            break;
        }

        case IL_CORO: {
            ss << *ip++;
            ip++;  // typeinfo
            int n = *ip++;
            for (int i = 0; i < n; i++) ss <<" v" << *ip++;
            break;
        }

        default:
            for (int i = 0; i < arity; i++) {
                if (i) ss << ' ';
                ss << *ip++;
            }
            break;
    }
    assert(arity == ILUNKNOWNARITY || ip - ins_start == arity + 1);
    (void)ins_start;
    return ip;
}

void DisAsm(NativeRegistry &nfr, ostringstream &ss, string_view bytecode_buffer) {
    auto bcf = bytecode::GetBytecodeFile(bytecode_buffer.data());
    assert(FLATBUFFERS_LITTLEENDIAN);
    auto code = (const int *)bcf->bytecode()->Data();  // Assumes we're on a little-endian machine.
    auto typetable = (const type_elem_t *)bcf->typetable()->Data();  // Same.
    auto len = bcf->bytecode()->Length();
    const int *ip = code;
    while (ip < code + len) {
        ip = DisAsmIns(nfr, ss, ip, code, typetable, bcf);
        ss << "\n";
        if (!ip) break;
    }
}

}  // namespace lobster
