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

#ifndef LOBSTER_IL
#define LOBSTER_IL

// FlatBuffers takes care of backwards compatibility of all metadata, but not the actual bytecode.
// This needs to be bumped each time we make changes to the format.

namespace lobster {

const int LOBSTER_BYTECODE_FORMAT_VERSION = 20;

// Any type specialized ops below must always have this ordering.
enum MathOp {
    MOP_ADD, MOP_SUB, MOP_MUL, MOP_DIV, MOP_MOD, MOP_LT, MOP_GT, MOP_LE, MOP_GE, MOP_EQ, MOP_NE
};

#define ILUNKNOWN 999999

#define ILBASENAMES \
    F(PUSHINT,      1, 0, 1) \
    F(PUSHINT64,    2, 0, 1) \
    F(PUSHFLT,      1, 0, 1) \
    F(PUSHFLT64,    2, 0, 1) \
    F(PUSHSTR,      1, 0, 1) \
    F(PUSHNIL,      0, 0, 1) \
    F(PUSHVARF,     1, 0, 1) \
    F(PUSHVARL,     1, 0, 1) \
    F(PUSHVARVF,    2, 0, ILUNKNOWN) \
    F(PUSHVARVL,    2, 0, ILUNKNOWN) \
    F(VPUSHIDXI,    0, 2, 1) \
    F(VPUSHIDXI2V,  0, 2, ILUNKNOWN) \
    F(VPUSHIDXV,    1, ILUNKNOWN, ILUNKNOWN) \
    F(VPUSHIDXIS,   1, 2, 1) \
    F(VPUSHIDXIS2V, 2, 2, ILUNKNOWN) \
    F(VPUSHIDXVS,   3, ILUNKNOWN, ILUNKNOWN) \
    F(NPUSHIDXI,    1, ILUNKNOWN, 1) \
    F(SPUSHIDXI,    0, 2, 1) \
    F(PUSHFLD,      1, 1, 1) \
    F(PUSHFLDMREF,  1, 1, 1) \
    F(PUSHFLDV,     2, ILUNKNOWN, 1) \
    F(PUSHFLD2V,    2, 1, ILUNKNOWN) \
    F(PUSHFLDV2V,   3, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRETV,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET0,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET1,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET2,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET3,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET4,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET5,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET6,    2, ILUNKNOWN, ILUNKNOWN) \
    F(BCALLRET7,    2, ILUNKNOWN, ILUNKNOWN) \
    F(ASSERT,       3, 1, 0) \
    F(ASSERTR,      3, 1, 1) \
    F(STATEMENT,    2, 0, 0) \
    F(PROFILE,      1, 0, 0) \
    F(NEWVEC,       2, ILUNKNOWN, 1) \
    F(NEWOBJECT,    1, ILUNKNOWN, 1) \
    F(POP,          0, 1, 0) \
    F(POPREF,       0, 1, 0) \
    F(POPV,         1, ILUNKNOWN, 0) \
    F(DUP,          0, 1, 2) \
    F(EXIT,         1, ILUNKNOWN, 0) \
    F(ABORT,        0, 0, 0) \
    F(IADD, 0, 2, 1) F(ISUB, 0, 2, 1) F(IMUL, 0, 2, 1) F(IDIV, 0, 2, 1) F(IMOD, 0, 2, 1) \
    F(ILT,  0, 2, 1) F(IGT,  0, 2, 1) F(ILE,  0, 2, 1) F(IGE,  0, 2, 1) F(IEQ,  0, 2, 1) F(INE, 0, 2, 1) \
    F(FADD, 0, 2, 1) F(FSUB, 0, 2, 1) F(FMUL, 0, 2, 1) F(FDIV, 0, 2, 1) F(FMOD, 0, 2, 1) \
    F(FLT,  0, 2, 1) F(FGT,  0, 2, 1) F(FLE,  0, 2, 1) F(FGE,  0, 2, 1) F(FEQ,  0, 2, 1) F(FNE, 0, 2, 1) \
    F(SADD, 0, 2, 1) F(SSUB, 0, 2, 1) F(SMUL, 0, 2, 1) F(SDIV, 0, 2, 1) F(SMOD, 0, 2, 1) \
    F(SLT,  0, 2, 1) F(SGT,  0, 2, 1) F(SLE,  0, 2, 1) F(SGE,  0, 2, 1) F(SEQ,  0, 2, 1) F(SNE, 0, 2, 1) \
    F(SADDN, 1, ILUNKNOWN, 1) \
    F(IVVADD, 1, ILUNKNOWN, ILUNKNOWN) F(IVVSUB, 1, ILUNKNOWN, ILUNKNOWN) F(IVVMUL, 1, ILUNKNOWN, ILUNKNOWN) F(IVVDIV, 1, ILUNKNOWN, ILUNKNOWN) F(IVVMOD, 1, ILUNKNOWN, ILUNKNOWN) \
    F(IVVLT,  1, ILUNKNOWN, ILUNKNOWN) F(IVVGT,  1, ILUNKNOWN, ILUNKNOWN) F(IVVLE,  1, ILUNKNOWN, ILUNKNOWN) F(IVVGE,  1, ILUNKNOWN, ILUNKNOWN) \
    F(FVVADD, 1, ILUNKNOWN, ILUNKNOWN) F(FVVSUB, 1, ILUNKNOWN, ILUNKNOWN) F(FVVMUL, 1, ILUNKNOWN, ILUNKNOWN) F(FVVDIV, 1, ILUNKNOWN, ILUNKNOWN) F(FVVMOD, 1, ILUNKNOWN, ILUNKNOWN) \
    F(FVVLT,  1, ILUNKNOWN, ILUNKNOWN) F(FVVGT,  1, ILUNKNOWN, ILUNKNOWN) F(FVVLE,  1, ILUNKNOWN, ILUNKNOWN) F(FVVGE,  1, ILUNKNOWN, ILUNKNOWN) \
    F(IVSADD, 1, ILUNKNOWN, ILUNKNOWN) F(IVSSUB, 1, ILUNKNOWN, ILUNKNOWN) F(IVSMUL, 1, ILUNKNOWN, ILUNKNOWN) F(IVSDIV, 1, ILUNKNOWN, ILUNKNOWN) F(IVSMOD, 1, ILUNKNOWN, ILUNKNOWN) \
    F(IVSLT,  1, ILUNKNOWN, ILUNKNOWN) F(IVSGT,  1, ILUNKNOWN, ILUNKNOWN) F(IVSLE,  1, ILUNKNOWN, ILUNKNOWN) F(IVSGE,  1, ILUNKNOWN, ILUNKNOWN) \
    F(FVSADD, 1, ILUNKNOWN, ILUNKNOWN) F(FVSSUB, 1, ILUNKNOWN, ILUNKNOWN) F(FVSMUL, 1, ILUNKNOWN, ILUNKNOWN) F(FVSDIV, 1, ILUNKNOWN, ILUNKNOWN) F(FVSMOD, 1, ILUNKNOWN, ILUNKNOWN) \
    F(FVSLT,  1, ILUNKNOWN, ILUNKNOWN) F(FVSGT,  1, ILUNKNOWN, ILUNKNOWN) F(FVSLE,  1, ILUNKNOWN, ILUNKNOWN) F(FVSGE,  1, ILUNKNOWN, ILUNKNOWN) \
    F(SIVADD, 1, ILUNKNOWN, ILUNKNOWN) F(SIVSUB, 1, ILUNKNOWN, ILUNKNOWN) F(SIVMUL, 1, ILUNKNOWN, ILUNKNOWN) F(SIVDIV, 1, ILUNKNOWN, ILUNKNOWN) F(SIVMOD, 1, ILUNKNOWN, ILUNKNOWN) \
    F(SIVLT,  1, ILUNKNOWN, ILUNKNOWN) F(SIVGT,  1, ILUNKNOWN, ILUNKNOWN) F(SIVLE,  1, ILUNKNOWN, ILUNKNOWN) F(SIVGE,  1, ILUNKNOWN, ILUNKNOWN) \
    F(SFVADD, 1, ILUNKNOWN, ILUNKNOWN) F(SFVSUB, 1, ILUNKNOWN, ILUNKNOWN) F(SFVMUL, 1, ILUNKNOWN, ILUNKNOWN) F(SFVDIV, 1, ILUNKNOWN, ILUNKNOWN) F(SFVMOD, 1, ILUNKNOWN, ILUNKNOWN) \
    F(SFVLT,  1, ILUNKNOWN, ILUNKNOWN) F(SFVGT,  1, ILUNKNOWN, ILUNKNOWN) F(SFVLE,  1, ILUNKNOWN, ILUNKNOWN) F(SFVGE,  1, ILUNKNOWN, ILUNKNOWN) \
    F(AEQ,  0, 2, 1) F(ANE,  0, 2, 1) \
    F(STEQ, 1, ILUNKNOWN, 1) F(STNE, 1, ILUNKNOWN, 1) \
    F(LEQ,  0, 2, 1) F(LNE,  0, 2, 1) \
    F(IUMINUS, 0, 1, 1) F(FUMINUS, 0, 1, 1) \
    F(IVUMINUS, 1, ILUNKNOWN, ILUNKNOWN) F(FVUMINUS, 1, ILUNKNOWN, ILUNKNOWN) \
    F(LOGNOT, 0, 1, 1) F(LOGNOTREF, 0, 1, 1) \
    F(BINAND, 0, 2, 1) F(BINOR, 0, 2, 1) F(XOR, 0, 2, 1) F(ASL, 0, 2, 1) F(ASR, 0, 2, 1) F(NEG, 0, 1, 1) \
    F(I2F, 0, 1, 1) F(A2S, 1, 1, 1) F(E2B, 0, 1, 1) F(E2BREF, 0, 1, 1) F(ST2S, 1, ILUNKNOWN, 1) \
    F(RETURNLOCAL, 1, ILUNKNOWN, 0) F(RETURNNONLOCAL, 2, ILUNKNOWN, 0) F(RETURNANY, 1, 0, 0) \
    F(ISTYPE, 1, 1, 1) \
    F(FORLOOPI, 0, 1, 2) \
    F(IFORELEM, 0, 2, 3) \
    F(SFORELEM, 0, 2, 3) \
    F(VFORELEM, 0, 2, 3) \
    F(VFORELEMREF, 0, 2, 3) \
    F(VFORELEM2S, 0, 2, ILUNKNOWN) \
    F(VFORELEMREF2S, 1, 2, ILUNKNOWN) \
    F(INCREF, 1, 0, 0) \
    F(KEEPREF, 2, 0, 0) \
    F(KEEPREFLOOP, 2, 0, 0) \
    F(GOTOFUNEXIT, 0, 0, 0) \
    F(CALL, 1, ILUNKNOWN, ILUNKNOWN) \
    F(CALLV, 0, ILUNKNOWN, ILUNKNOWN) \
    F(DDCALL, 2, ILUNKNOWN, ILUNKNOWN) \
    F(BLOCK_START, 0, 0, 0) \
    F(JUMP_TABLE_END, 0, 0, 0) \
    F(JUMP_TABLE_CASE_START, 0, 0, 0) \
    F(LVAL_VARF, 1, 0, 0) \
    F(LVAL_VARL, 1, 0, 0) \
    F(LVAL_FLD, 1, 1, 0) \
    F(LVAL_IDXVI, 1, 2, 0) \
    F(LVAL_IDXVV, 2, ILUNKNOWN, 0) \
    F(LVAL_IDXNI, 1, 2, 0) \
    F(LV_DUP, 0, 0, 1) \
    F(LV_DUPV, 1, 0, ILUNKNOWN) \
    F(LV_WRITE, 0, 1, 0)  F(LV_WRITEREF, 0, 1, 0)  \
    F(LV_WRITEV, 1, ILUNKNOWN, 0) F(LV_WRITEREFV, 2, ILUNKNOWN, 0) \
    F(LV_IADD, 0, 1, 0)   F(LV_ISUB, 0, 1, 0)   F(LV_IMUL, 0, 1, 0)   F(LV_IDIV, 0, 1, 0)   \
    F(LV_IMOD, 0, 1, 0)   \
    F(LV_BINAND, 0, 1, 0) F(LV_BINOR, 0, 1, 0)  F(LV_XOR, 0, 1, 0)    \
    F(LV_ASL, 0, 1, 0)    F(LV_ASR, 0, 1, 0)    \
    F(LV_FADD, 0, 1, 0)   F(LV_FSUB, 0, 1, 0)   F(LV_FMUL, 0, 1, 0)   F(LV_FDIV, 0, 1, 0)   \
    F(LV_FMOD, 0, 1, 0)   \
    F(LV_IVVADD, 1, ILUNKNOWN, 0) F(LV_IVVSUB, 1, ILUNKNOWN, 0) F(LV_IVVMUL, 1, ILUNKNOWN, 0) F(LV_IVVDIV, 1, ILUNKNOWN, 0) \
    F(LV_IVVMOD, 1, ILUNKNOWN, 0) \
    F(LV_FVVADD, 1, ILUNKNOWN, 0) F(LV_FVVSUB, 1, ILUNKNOWN, 0) F(LV_FVVMUL, 1, ILUNKNOWN, 0) F(LV_FVVDIV, 1, ILUNKNOWN, 0) \
    F(LV_FVVMOD, 1, ILUNKNOWN, 0) \
    F(LV_IVSADD, 1, 1, 0) F(LV_IVSSUB, 1, 1, 0) F(LV_IVSMUL, 1, 1, 0) F(LV_IVSDIV, 1, 1, 0) \
    F(LV_IVSMOD, 1, 1, 0) \
    F(LV_FVSADD, 1, 1, 0) F(LV_FVSSUB, 1, 1, 0) F(LV_FVSMUL, 1, 1, 0) F(LV_FVSDIV, 1, 1, 0) \
    F(LV_FVSMOD, 1, 1, 0) \
    F(LV_SADD, 0, 1, 0) \
    F(LV_IPP, 0, 0, 0) F(LV_IMM, 0, 0, 0) \
    F(LV_FPP, 0, 0, 0) F(LV_FMM, 0, 0, 0)

#define ILCALLNAMES \
    F(PUSHFUN, 1, 0, 1)

#define ILVARARGNAMES \
    F(JUMP_TABLE, ILUNKNOWN, 1, 0) \
    F(JUMP_TABLE_DISPATCH, ILUNKNOWN, 1, 0) \
    F(FUNSTART, ILUNKNOWN, 0, 0)

#define ILJUMPNAMES1 \
    F(JUMP, 1, 0, 0) \
    F(JUMPFAIL, 1, 1, 0) \
    F(JUMPFAILR, 1, 1, 1) \
    F(JUMPNOFAIL, 1, 1, 0) \
    F(JUMPNOFAILR, 1, 1, 1) \
    F(IFOR, 1, 2, 2) \
    F(SFOR, 1, 2, 2) \
    F(VFOR, 1, 2, 2)
#define ILJUMPNAMES2 \
    F(JUMPIFUNWOUND, 2, 0, 0) \
    F(JUMPIFSTATICLF, 2, 0, 0) \
    F(JUMPIFMEMBERLF, 2, 1, 0)

#define GENOP(OP) ((ILOP)(OP))

#define ILNAMES ILBASENAMES ILCALLNAMES ILVARARGNAMES ILJUMPNAMES1 ILJUMPNAMES2

enum ILOP {
    #define F(N, A, USE, DEF) IL_##N,
        ILNAMES
    #undef F
    IL_MAX_OPS
};

inline const char **ILNames() {
    #define F(N, A, USE, DEF) #N,
        static const char *ilnames[] = { ILNAMES };
    #undef F
    return ilnames;
}

inline const int *ILArity() {
    #define F(N, A, USE, DEF) A,
        static const int ilarity[] = { ILNAMES };
    #undef F
    return ilarity;
}

inline const int *ILUses() {
    #define F(N, A, USE, DEF) USE,
        static const int iluses[] = { ILNAMES };
    #undef F
    return iluses;
}

inline const int *ILDefs() {
    #define F(N, A, USE, DEF) DEF,
        static const int ildefs[] = { ILNAMES };
    #undef F
    return ildefs;
}

}

#endif  // LOBSTER_IL
