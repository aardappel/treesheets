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

const int LOBSTER_BYTECODE_FORMAT_VERSION = 15;
const int MAX_RETURN_VALUES = 64;

// Any type specialized ops below must always have this ordering.
enum MathOp {
    MOP_ADD, MOP_SUB, MOP_MUL, MOP_DIV, MOP_MOD, MOP_LT, MOP_GT, MOP_LE, MOP_GE, MOP_EQ, MOP_NE
};

#define ILUNKNOWNARITY 9

#define ILBASENAMES \
    F(PUSHINT, 1) \
    F(PUSHINT64, 2) \
    F(PUSHFLT, 1) \
    F(PUSHFLT64, 2) \
    F(PUSHSTR, 1) \
    F(PUSHNIL, 0) \
    F(PUSHVAR, 1) \
    F(PUSHVARV, 2) \
    F(VPUSHIDXI, 0) F(VPUSHIDXV, 1) F(VPUSHIDXIS, 2) F(VPUSHIDXVS, 3) F(NPUSHIDXI, 1) F(SPUSHIDXI, 0) \
    F(PUSHFLD, 1) F(PUSHFLDMREF, 1) F(PUSHFLDV, 2) F(PUSHFLD2V, 2) F(PUSHFLDV2V, 3) \
    F(PUSHLOC, 1) F(PUSHLOCV, 2) \
    F(BCALLRETV, 1) F(BCALLREFV, 1) F(BCALLUNBV, 1) \
    F(BCALLRET0, 1) F(BCALLREF0, 1) F(BCALLUNB0, 1) \
    F(BCALLRET1, 1) F(BCALLREF1, 1) F(BCALLUNB1, 1) \
    F(BCALLRET2, 1) F(BCALLREF2, 1) F(BCALLUNB2, 1) \
    F(BCALLRET3, 1) F(BCALLREF3, 1) F(BCALLUNB3, 1) \
    F(BCALLRET4, 1) F(BCALLREF4, 1) F(BCALLUNB4, 1) \
    F(BCALLRET5, 1) F(BCALLREF5, 1) F(BCALLUNB5, 1) \
    F(BCALLRET6, 1) F(BCALLREF6, 1) F(BCALLUNB6, 1) \
    F(BCALLRET7, 1) F(BCALLREF7, 1) F(BCALLUNB7, 1) \
    F(ASSERT, 3) F(ASSERTR, 3) \
    F(CONT1, 1) \
    F(FUNSTART, ILUNKNOWNARITY) \
    F(ENDSTATEMENT, 2) \
    F(NEWVEC, 2) F(NEWOBJECT, 1) \
    F(POP, 0) F(POPREF, 0) F(POPV, 1) F(POPVREF, 1) \
    F(DUP, 0) \
    F(EXIT, 1) F(ABORT, 0) \
    F(IADD, 0)  F(ISUB, 0)  F(IMUL, 0)  F(IDIV, 0)  F(IMOD, 0) \
    F(ILT, 0)  F(IGT, 0)  F(ILE, 0)  F(IGE, 0)  F(IEQ, 0) F(INE, 0) \
    F(FADD, 0)  F(FSUB, 0)  F(FMUL, 0)  F(FDIV, 0)  F(FMOD, 0) \
    F(FLT, 0)  F(FGT, 0)  F(FLE, 0)  F(FGE, 0)  F(FEQ, 0) F(FNE, 0) \
    F(SADD, 0)  F(SSUB, 0)  F(SMUL, 0)  F(SDIV, 0)  F(SMOD, 0) \
    F(SLT, 0)  F(SGT, 0)  F(SLE, 0)  F(SGE, 0)  F(SEQ, 0) F(SNE, 0) \
    F(IVVADD, 1) F(IVVSUB, 1) F(IVVMUL, 1) F(IVVDIV, 1) F(IVVMOD, 1) \
    F(IVVLT, 1) F(IVVGT, 1) F(IVVLE, 1) F(IVVGE, 1) \
    F(FVVADD, 1) F(FVVSUB, 1) F(FVVMUL, 1) F(FVVDIV, 1) F(FVVMOD, 1) \
    F(FVVLT, 1) F(FVVGT, 1) F(FVVLE, 1) F(FVVGE, 1) \
    F(IVSADD, 1) F(IVSSUB, 1) F(IVSMUL, 1) F(IVSDIV, 1) F(IVSMOD, 1) \
    F(IVSLT, 1) F(IVSGT, 1) F(IVSLE, 1) F(IVSGE, 1) \
    F(FVSADD, 1) F(FVSSUB, 1) F(FVSMUL, 1) F(FVSDIV, 1) F(FVSMOD, 1) \
    F(FVSLT, 1) F(FVSGT, 1) F(FVSLE, 1) F(FVSGE, 1) \
    F(AEQ, 0) F(ANE, 0) \
    F(STEQ, 1) F(STNE, 1) \
    F(LEQ, 0) F(LNE, 0) \
    F(IUMINUS, 0) F(FUMINUS, 0) F(IVUMINUS, 1) F(FVUMINUS, 1) \
    F(LOGNOT, 0) F(LOGNOTREF, 0) \
    F(BINAND, 0) F(BINOR, 0) F(XOR, 0) F(ASL, 0) F(ASR, 0) F(NEG, 0) \
    F(I2F, 0) F(A2S, 1) F(E2B, 0) F(E2BREF, 0) F(ST2S, 1) \
    F(RETURN, 2) \
    F(ISTYPE, 1) F(COCL, 0) F(COEND, 0) \
    F(LOGREAD, 1) F(LOGWRITE, 2) \
    F(FORLOOPI, 0) F(IFORELEM, 0) F(SFORELEM, 0) F(VFORELEM, 0) F(VFORELEMREF, 0) \
    F(INCREF, 1) F(KEEPREF, 2)

#define ILCALLNAMES \
    F(CALL, 1) F(CALLV, 0) F(CALLVCOND, 0) F(DDCALL, 2) \
    F(PUSHFUN, 1) F(CORO, ILUNKNOWNARITY) F(YIELD, 0)

#define ILJUMPNAMES \
    F(JUMP, 1) \
    F(JUMPFAIL, 1) F(JUMPFAILR, 1) F(JUMPFAILN, 1) \
    F(JUMPNOFAIL, 1) F(JUMPNOFAILR, 1) \
    F(IFOR, 1) F(SFOR, 1) F(VFOR, 1)

#define LVALOPNAMES \
    LVAL(WRITE, 0)  LVAL(WRITER, 0)  LVAL(WRITEREF, 0)  LVAL(WRITERREF, 0) \
    LVAL(WRITEV, 1) LVAL(WRITERV, 1) LVAL(WRITEREFV, 1) LVAL(WRITERREFV, 1) \
    LVAL(IADD, 0)   LVAL(IADDR, 0)   LVAL(ISUB, 0)   LVAL(ISUBR, 0)   LVAL(IMUL, 0)   LVAL(IMULR, 0)   LVAL(IDIV, 0)   LVAL(IDIVR, 0) \
    LVAL(IMOD, 0)   LVAL(IMODR, 0) \
    LVAL(FADD, 0)   LVAL(FADDR, 0)   LVAL(FSUB, 0)   LVAL(FSUBR, 0)   LVAL(FMUL, 0)   LVAL(FMULR, 0)   LVAL(FDIV, 0)   LVAL(FDIVR, 0) \
    LVAL(IVVADD, 1) LVAL(IVVADDR, 1) LVAL(IVVSUB, 1) LVAL(IVVSUBR, 1) LVAL(IVVMUL, 1) LVAL(IVVMULR, 1) LVAL(IVVDIV, 1) LVAL(IVVDIVR, 1) \
    LVAL(IVVMOD, 1) LVAL(IVVMODR, 1) \
    LVAL(FVVADD, 1) LVAL(FVVADDR, 1) LVAL(FVVSUB, 1) LVAL(FVVSUBR, 1) LVAL(FVVMUL, 1) LVAL(FVVMULR, 1) LVAL(FVVDIV, 1) LVAL(FVVDIVR, 1) \
    LVAL(IVSADD, 1) LVAL(IVSADDR, 1) LVAL(IVSSUB, 1) LVAL(IVSSUBR, 1) LVAL(IVSMUL, 1) LVAL(IVSMULR, 1) LVAL(IVSDIV, 1) LVAL(IVSDIVR, 1) \
    LVAL(IVSMOD, 1) LVAL(IVSMODR, 1) \
    LVAL(FVSADD, 1) LVAL(FVSADDR, 1) LVAL(FVSSUB, 1) LVAL(FVSSUBR, 1) LVAL(FVSMUL, 1) LVAL(FVSMULR, 1) LVAL(FVSDIV, 1) LVAL(FVSDIVR, 1) \
    LVAL(SADD, 0)   LVAL(SADDR, 0) \
    LVAL(IPP, 0) LVAL(IPPR, 0) LVAL(IMM, 0) LVAL(IMMR, 0) LVAL(IPPP, 0) LVAL(IPPPR, 0) LVAL(IMMP, 0) LVAL(IMMPR, 0) \
    LVAL(FPP, 0) LVAL(FPPR, 0) LVAL(FMM, 0) LVAL(FMMR, 0) LVAL(FPPP, 0) LVAL(FPPPR, 0) LVAL(FMMP, 0) LVAL(FMMPR, 0)

enum LVALOP {
    #define LVAL(N, V) LVO_##N,
        LVALOPNAMES
    #undef LVAL
};

#define NUMBASELVALOPS 6  // HAS to match LVAL below!
#define GENLVALOP(LV, OP) (IL_##LV##_WRITE + (OP) * NUMBASELVALOPS)  // WRITE assumed to be first!
#define ILADD00 0
#define ILADD01 1
#define ILADD10 1
#define ILADD11 2
#define ILADD(X, Y) ILADD##X##Y
#define LVAL(N, V) F(VAR_##N, ILADD(1, V)) F(FLD_##N, ILADD(1, V)) F(LOC_##N, ILADD(1, V)) \
                   F(IDXVI_##N, ILADD(0, V)) F(IDXVV_##N, ILADD(1, V)) F(IDXNI_##N, ILADD(0, V))
// This assumes VAR is first!
#define ISLVALVARINS(O) O >= IL_VAR_WRITE && O <= IL_VAR_FMMPR && (O % NUMBASELVALOPS) == 0

#define ISBCALL(O) (O >= IL_BCALLRETV && O <= IL_BCALLUNB7)

#define ILNAMES LVALOPNAMES ILBASENAMES ILCALLNAMES ILJUMPNAMES

enum ILOP {
    #define F(N, A) IL_##N,
        ILNAMES
    #undef F
    IL_MAX_OPS
};

inline const char **ILNames() {
    #define F(N, A) #N,
        static const char *ilnames[] = { ILNAMES };
    #undef F
    return ilnames;
}

inline const int *ILArity() {
    #define F(N, A) A,
        static const int ilarity[] = { ILNAMES };
    #undef F
    return ilarity;
}

}

#endif  // LOBSTER_IL
