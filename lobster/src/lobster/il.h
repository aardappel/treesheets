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

// FlatBuffers takes care of backwards compatibility of all metadata, but not the actual bytecode.
// This needs to be bumped each time we make changes to the format.

namespace lobster {

const int LOBSTER_BYTECODE_FORMAT_VERSION = 11;
const int MAX_RETURN_VALUES = 16;

#define ILBASENAMES \
    F(PUSHINT, 1) \
    F(PUSHINT64, 1) \
    F(PUSHFLT, 1) \
    F(PUSHSTR, 1) \
    F(PUSHNIL, 0) \
    F(PUSHVAR, 1) F(PUSHVARREF, 1) F(LVALVAR, 2) \
    F(VPUSHIDXI, 0) F(VPUSHIDXV, 0) F(NPUSHIDXI, 0) F(SPUSHIDXI, 0) \
    F(VLVALIDXI, 1) F(NLVALIDXI, 1) F(LVALIDXV, 1) \
    F(PUSHFLD, 1) F(PUSHFLDM, 1) F(LVALFLD, 2) \
    F(PUSHLOC, 1) F(LVALLOC, 2) \
    F(BCALLRET0, 1) F(BCALLREF0, 1) F(BCALLUNB0, 1) \
    F(BCALLRET1, 1) F(BCALLREF1, 1) F(BCALLUNB1, 1) \
    F(BCALLRET2, 1) F(BCALLREF2, 1) F(BCALLUNB2, 1) \
    F(BCALLRET3, 1) F(BCALLREF3, 1) F(BCALLUNB3, 1) \
    F(BCALLRET4, 1) F(BCALLREF4, 1) F(BCALLUNB4, 1) \
    F(BCALLRET5, 1) F(BCALLREF5, 1) F(BCALLUNB5, 1) \
    F(BCALLRET6, 1) F(BCALLREF6, 1) F(BCALLUNB6, 1) \
    F(BCALLRET7, 1) F(BCALLREF7, 1) F(BCALLUNB7, 1) \
    F(CONT1, 1) \
    F(FUNSTART, -1) F(FUNEND, 1) F(FUNMULTI, -1) \
    F(NEWVEC, 2) F(NEWSTRUCT, 1) \
    F(POP, 0) F(POPREF, 0) \
    F(DUP, 0) F(DUPREF, 0) \
    F(EXIT, 1) \
    F(IADD, 0)  F(ISUB, 0)  F(IMUL, 0)  F(IDIV, 0)  F(IMOD, 0) \
    F(ILT, 0)  F(IGT, 0)  F(ILE, 0)  F(IGE, 0)  F(IEQ, 0) F(INE, 0) \
    F(FADD, 0)  F(FSUB, 0)  F(FMUL, 0)  F(FDIV, 0)  F(FMOD, 0) \
    F(FLT, 0)  F(FGT, 0)  F(FLE, 0)  F(FGE, 0)  F(FEQ, 0) F(FNE, 0) \
    F(SADD, 0)  F(SSUB, 0)  F(SMUL, 0)  F(SDIV, 0)  F(SMOD, 0) \
    F(SLT, 0)  F(SGT, 0)  F(SLE, 0)  F(SGE, 0)  F(SEQ, 0) F(SNE, 0) \
    F(IVVADD, 0) F(IVVSUB, 0) F(IVVMUL, 0) F(IVVDIV, 0) F(IVVMOD, 0) \
    F(IVVLT, 0) F(IVVGT, 0) F(IVVLE, 0) F(IVVGE, 0) \
    F(FVVADD, 0) F(FVVSUB, 0) F(FVVMUL, 0) F(FVVDIV, 0) F(FVVMOD, 0) \
    F(FVVLT, 0) F(FVVGT, 0) F(FVVLE, 0) F(FVVGE, 0) \
    F(IVSADD, 0) F(IVSSUB, 0) F(IVSMUL, 0) F(IVSDIV, 0) F(IVSMOD, 0) \
    F(IVSLT, 0) F(IVSGT, 0) F(IVSLE, 0) F(IVSGE, 0) \
    F(FVSADD, 0) F(FVSSUB, 0) F(FVSMUL, 0) F(FVSDIV, 0) F(FVSMOD, 0) \
    F(FVSLT, 0) F(FVSGT, 0) F(FVSLE, 0) F(FVSGE, 0) \
    F(AEQ, 0) F(ANE, 0) \
    F(IUMINUS, 0) F(FUMINUS, 0) F(IVUMINUS, 0) F(FVUMINUS, 0) \
    F(LOGNOT, 0) F(LOGNOTREF, 0) \
    F(BINAND, 0) F(BINOR, 0) F(XOR, 0) F(ASL, 0) F(ASR, 0) F(NEG, 0) \
    F(I2F, 0) F(A2S, 0) F(I2A, 0) F(F2A, 0) F(E2B, 0) F(E2BREF, 0) \
    F(RETURN, 3) \
    F(ISTYPE, 1) F(COCL, 0) F(COEND, 0) \
    F(LOGREAD, 1) F(LOGWRITE, 2) \
    F(FORLOOPI, 0) F(IFORELEM, 0) F(SFORELEM, 0) F(VFORELEM, 0) F(NFORELEM, 0)

#define ILCALLNAMES \
    F(CALL, 3) F(CALLMULTI, -1) F(CALLV, 1) F(CALLVCOND, 1) \
    F(PUSHFUN, 1) F(CORO, -1) F(YIELD, 1)

#define ILJUMPNAMES \
    F(JUMP, 1) \
    F(JUMPFAIL, 1) F(JUMPFAILREF, 1) F(JUMPFAILR, 1) F(JUMPFAILRREF, 1) \
    F(JUMPFAILN, 1) F(JUMPFAILNREF, 1) \
    F(JUMPNOFAIL, 1) F(JUMPNOFAILREF, 1) F(JUMPNOFAILR, 1) F(JUMPNOFAILRREF, 1) \
    F(IFOR, 1) F(SFOR, 1) F(VFOR, 1) F(NFOR, 1)

#define ILNAMES ILBASENAMES ILCALLNAMES ILJUMPNAMES

#define LVALOPNAMES \
    F(WRITE)  F(WRITER)  F(WRITEREF) F(WRITERREF) \
    F(IADD)   F(IADDR)   F(ISUB)   F(ISUBR)   F(IMUL)   F(IMULR)   F(IDIV)   F(IDIVR) \
    F(IMOD)   F(IMODR) \
    F(FADD)   F(FADDR)   F(FSUB)   F(FSUBR)   F(FMUL)   F(FMULR)   F(FDIV)   F(FDIVR) \
    F(IVVADD) F(IVVADDR) F(IVVSUB) F(IVVSUBR) F(IVVMUL) F(IVVMULR) F(IVVDIV) F(IVVDIVR) \
    F(IVVMOD) F(IVVMODR) \
    F(FVVADD) F(FVVADDR) F(FVVSUB) F(FVVSUBR) F(FVVMUL) F(FVVMULR) F(FVVDIV) F(FVVDIVR) \
    F(IVSADD) F(IVSADDR) F(IVSSUB) F(IVSSUBR) F(IVSMUL) F(IVSMULR) F(IVSDIV) F(IVSDIVR) \
    F(IVSMOD) F(IVSMODR) \
    F(FVSADD) F(FVSADDR) F(FVSSUB) F(FVSSUBR) F(FVSMUL) F(FVSMULR) F(FVSDIV) F(FVSDIVR) \
    F(SADD)   F(SADDR) \
    F(IPP) F(IPPR) F(IMM) F(IMMR) F(IPPP) F(IPPPR) F(IMMP) F(IMMPR) \
    F(FPP) F(FPPR) F(FMM) F(FMMR) F(FPPP) F(FPPPR) F(FMMP) F(FMMPR)

enum ILOP {
    #define F(N, A) IL_##N,
        ILNAMES
    #undef F
    IL_MAX_OPS
};

enum {
    #define F(N) LVO_##N,
        LVALOPNAMES
    #undef F
};

inline const char **ILNames() {
    #define F(N, A) #N,
        static const char *ilnames[] = { ILNAMES };
    #undef F
    return ilnames;
}

inline const char **LvalOpNames() {
    #define F(N) #N,
        static const char *lvalopnames[] = { LVALOPNAMES };
    #undef F
    return lvalopnames;
}

inline const int *ILArity() {
    #define F(N, A) A,
        static const int ilarity[] = { ILNAMES };
    #undef F
    return ilarity;
}

}
