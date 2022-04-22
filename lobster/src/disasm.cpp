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

const int *DisAsmIns(NativeRegistry &nfr, string &sd, const int *ip, const int *code,
                     const type_elem_t *typetable, const bytecode::BytecodeFile *bcf,
                     int line) {
    auto ilnames = ILNames();
    auto ilarity = ILArity();
    if (code) {
        auto li = LookupLine(ip, code, bcf);
        // FIXME: some indication of the filename, maybe with a table index?
        append(sd, "I ", ip - code, " \t");
        append(sd, "L ", li->line(), " \t");
    } else if (line >= 0) {
        append(sd, "L ", line, " \t");
    }
    auto ins_start = ip;
    int opc = *ip++;
    if (opc < 0 || opc >= IL_MAX_OPS) {
        append(sd, "ILLEGAL INSTRUCTION: ", opc);
        return nullptr;
    }
    if (opc < 0 || opc >= IL_MAX_OPS) {
        append(sd, opc, " ?");
        return ip;
    }
    auto arity = ilarity[opc];
    int regs = *ip++;
    append(sd, "R ", regs, "\t");
    append(sd, ilnames[opc], " ");
    switch(opc) {
        case IL_PUSHINT64:
        case IL_PUSHFLT64: {
            auto a = *ip++;
            auto v = Int64FromInts(a, *ip++);
            if (opc == IL_PUSHINT64) append(sd, v);
            else {
                int2float64 i2f;
                i2f.i = v;
                sd += to_string_float(i2f.f);
            }
            break;
        }

        case IL_KEEPREF:
        case IL_KEEPREFLOOP:
            append(sd, *ip++, " ");
            append(sd, *ip++);
            break;

        case IL_RETURN: {
            auto id = *ip++;
            auto nrets = *ip++;
            append(sd, bcf->functions()->Get(id)->name()->string_view(), " ", nrets);
            break;
        }

        case IL_CALL: {
            auto bc = *ip++;
            if (code) {
                assert(code[bc] == IL_FUNSTART);
                auto id = code[bc + 2];
                auto nargs = code[bc + 4];
                append(sd, nargs, " ", bcf->functions()->Get(id)->name()->string_view(), " ", bc);
            } else {
                append(sd, " ", bc);
            }
            break;
        }

        case IL_NEWVEC: {
            ip++;  // ti
            auto nargs = *ip++;
            append(sd, "vector ", nargs);
            break;
        }
        case IL_ST2S:
        case IL_NEWOBJECT: {
            auto ti = (TypeInfo *)(typetable + *ip++);
            sd += bcf->udts()->Get(ti->structidx)->name()->string_view();
            break;
        }

        case IL_BCALLRETV:
        case IL_BCALLRET0:
        case IL_BCALLRET1:
        case IL_BCALLRET2:
        case IL_BCALLRET3:
        case IL_BCALLRET4:
        case IL_BCALLRET5:
        case IL_BCALLRET6: {
            int a = *ip++;
            ip++;  // has_ret
            sd += nfr.nfuns[a]->name;
            break;
        }

        case IL_PUSHVARVL:
        case IL_PUSHVARVF:
            sd += IdName(bcf, *ip++, typetable, true);
            append(sd, " ", *ip++);
            break;

        case IL_LVAL_VARL:
        case IL_LVAL_VARF:
        case IL_PUSHVARL:
        case IL_PUSHVARF:
            sd += IdName(bcf, *ip++, typetable, false);
            break;

        case IL_PUSHFLT:
            sd += to_string_float(*(float *)ip);
            ip++;
            break;

        case IL_PUSHSTR:
            EscapeAndQuote(bcf->stringtable()->Get(*ip++)->string_view(), sd);
            break;

        case IL_JUMP_TABLE: {
            auto mini = *ip++;
            auto maxi = *ip++;
            auto n = maxi - mini + 2;
            append(sd, mini, "..", maxi, " [ ");
            while (n--) append(sd, *ip++, " ");
            sd += "]";
            break;
        }

        case IL_FUNSTART: {
            auto fidx = *ip++;
            sd += (fidx >= 0 ? bcf->functions()->Get(fidx)->name()->string_view() : "__dummy");
            auto regs = *ip++;
            sd += "(";
            int n = *ip++;
            while (n--) append(sd, IdName(bcf, *ip++, typetable, false), " ");
            n = *ip++;
            sd += "=> ";
            while (n--) append(sd, IdName(bcf, *ip++, typetable, false), " ");
            auto keepvars = *ip++;
            if (keepvars) append(sd, "K:", keepvars, " ");
            n = *ip++;  // owned
            while (n--) append(sd, "O:", IdName(bcf, *ip++, typetable, false), " ");
            append(sd, "R:", regs, " ");
            sd += ")";
            break;
        }

        default:
            for (int i = 0; i < arity; i++) {
                if (i) sd += ' ';
                append(sd, *ip++);
            }
            break;
    }
    assert(arity == ILUNKNOWN || ip - ins_start == arity + 2);
    (void)ins_start;
    return ip;
}

void DisAsm(NativeRegistry &nfr, string &sd, string_view bytecode_buffer) {
    auto bcf = bytecode::GetBytecodeFile(bytecode_buffer.data());
    assert(FLATBUFFERS_LITTLEENDIAN);
    auto code = (const int *)bcf->bytecode()->Data();  // Assumes we're on a little-endian machine.
    auto typetable = (const type_elem_t *)bcf->typetable()->Data();  // Same.
    auto len = bcf->bytecode()->size();
    const int *ip = code;
    while (ip < code + len) {
        if (*ip == IL_FUNSTART) sd += "------- ------- ---\n";
        ip = DisAsmIns(nfr, sd, ip, code, typetable, bcf, -1);
        sd += "\n";
        if (!ip) break;
    }
}

}  // namespace lobster
