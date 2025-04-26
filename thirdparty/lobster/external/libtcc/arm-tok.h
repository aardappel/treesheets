/* ------------------------------------------------------------------ */
/* WARNING: relative order of tokens is important.                    */

/* register */

 DEF_ASM(r0)
 DEF_ASM(r1)
 DEF_ASM(r2)
 DEF_ASM(r3)
 DEF_ASM(r4)
 DEF_ASM(r5)
 DEF_ASM(r6)
 DEF_ASM(r7)
 DEF_ASM(r8)
 DEF_ASM(r9)
 DEF_ASM(r10)
 DEF_ASM(r11) /* fp */
 DEF_ASM(r12) /* ip[c] */
 DEF_ASM(r13) /* sp */
 DEF_ASM(r14) /* lr */
 DEF_ASM(r15) /* pc */

/* register macros */

 DEF_ASM(fp) /* alias for r11 */
 DEF_ASM(ip) /* alias for r12 */
 DEF_ASM(sp) /* alias for r13 */
 DEF_ASM(lr) /* alias for r14 */
 DEF_ASM(pc) /* alias for r15 */

 /* coprocessors */

 DEF_ASM(p0)
 DEF_ASM(p1)
 DEF_ASM(p2)
 DEF_ASM(p3)
 DEF_ASM(p4)
 DEF_ASM(p5)
 DEF_ASM(p6)
 DEF_ASM(p7)
 DEF_ASM(p8)
 DEF_ASM(p9)
 DEF_ASM(p10)
 DEF_ASM(p11)
 DEF_ASM(p12)
 DEF_ASM(p13)
 DEF_ASM(p14)
 DEF_ASM(p15)

 /* coprocessor registers */

 DEF_ASM(c0)
 DEF_ASM(c1)
 DEF_ASM(c2)
 DEF_ASM(c3)
 DEF_ASM(c4)
 DEF_ASM(c5)
 DEF_ASM(c6)
 DEF_ASM(c7)
 DEF_ASM(c8)
 DEF_ASM(c9)
 DEF_ASM(c10)
 DEF_ASM(c11)
 DEF_ASM(c12)
 DEF_ASM(c13)
 DEF_ASM(c14)
 DEF_ASM(c15)

 /* single-precision VFP registers */

 DEF_ASM(s0)
 DEF_ASM(s1)
 DEF_ASM(s2)
 DEF_ASM(s3)
 DEF_ASM(s4)
 DEF_ASM(s5)
 DEF_ASM(s6)
 DEF_ASM(s7)
 DEF_ASM(s8)
 DEF_ASM(s9)
 DEF_ASM(s10)
 DEF_ASM(s11)
 DEF_ASM(s12)
 DEF_ASM(s13)
 DEF_ASM(s14)
 DEF_ASM(s15)
 DEF_ASM(s16)
 DEF_ASM(s17)
 DEF_ASM(s18)
 DEF_ASM(s19)
 DEF_ASM(s20)
 DEF_ASM(s21)
 DEF_ASM(s22)
 DEF_ASM(s23)
 DEF_ASM(s24)
 DEF_ASM(s25)
 DEF_ASM(s26)
 DEF_ASM(s27)
 DEF_ASM(s28)
 DEF_ASM(s29)
 DEF_ASM(s30)
 DEF_ASM(s31)

 /* double-precision VFP registers */

 DEF_ASM(d0)
 DEF_ASM(d1)
 DEF_ASM(d2)
 DEF_ASM(d3)
 DEF_ASM(d4)
 DEF_ASM(d5)
 DEF_ASM(d6)
 DEF_ASM(d7)
 DEF_ASM(d8)
 DEF_ASM(d9)
 DEF_ASM(d10)
 DEF_ASM(d11)
 DEF_ASM(d12)
 DEF_ASM(d13)
 DEF_ASM(d14)
 DEF_ASM(d15)

 /* VFP status registers */

 DEF_ASM(fpsid)
 DEF_ASM(fpscr)
 DEF_ASM(fpexc)

 /* VFP magical ARM register */

 DEF_ASM(apsr_nzcv)

 /* data processing directives */

 DEF_ASM(asl)

 /* instructions that have no condition code */

 DEF_ASM(cdp2)
 DEF_ASM(ldc2)
 DEF_ASM(ldc2l)
 DEF_ASM(stc2)
 DEF_ASM(stc2l)

#define ARM_INSTRUCTION_GROUP(tok) ((((tok) - TOK_ASM_nopeq) & 0xFFFFFFF0) + TOK_ASM_nopeq)

/* Note: condition code is 4 bits */
#define DEF_ASM_CONDED(x) \
  DEF(TOK_ASM_ ## x ## eq, #x "eq") \
  DEF(TOK_ASM_ ## x ## ne, #x "ne") \
  DEF(TOK_ASM_ ## x ## cs, #x "cs") \
  DEF(TOK_ASM_ ## x ## cc, #x "cc") \
  DEF(TOK_ASM_ ## x ## mi, #x "mi") \
  DEF(TOK_ASM_ ## x ## pl, #x "pl") \
  DEF(TOK_ASM_ ## x ## vs, #x "vs") \
  DEF(TOK_ASM_ ## x ## vc, #x "vc") \
  DEF(TOK_ASM_ ## x ## hi, #x "hi") \
  DEF(TOK_ASM_ ## x ## ls, #x "ls") \
  DEF(TOK_ASM_ ## x ## ge, #x "ge") \
  DEF(TOK_ASM_ ## x ## lt, #x "lt") \
  DEF(TOK_ASM_ ## x ## gt, #x "gt") \
  DEF(TOK_ASM_ ## x ## le, #x "le") \
  DEF(TOK_ASM_ ## x, #x) \
  DEF(TOK_ASM_ ## x ## rsvd, #x "rsvd")

/* Note: condition code is 4 bits */
#define DEF_ASM_CONDED_WITH_SUFFIX(x, y) \
  DEF(TOK_ASM_ ## x ## eq ## _ ## y, #x "eq." #y) \
  DEF(TOK_ASM_ ## x ## ne ## _ ## y, #x "ne." #y) \
  DEF(TOK_ASM_ ## x ## cs ## _ ## y, #x "cs." #y) \
  DEF(TOK_ASM_ ## x ## cc ## _ ## y, #x "cc." #y) \
  DEF(TOK_ASM_ ## x ## mi ## _ ## y, #x "mi." #y) \
  DEF(TOK_ASM_ ## x ## pl ## _ ## y, #x "pl." #y) \
  DEF(TOK_ASM_ ## x ## vs ## _ ## y, #x "vs." #y) \
  DEF(TOK_ASM_ ## x ## vc ## _ ## y, #x "vc." #y) \
  DEF(TOK_ASM_ ## x ## hi ## _ ## y, #x "hi." #y) \
  DEF(TOK_ASM_ ## x ## ls ## _ ## y, #x "ls." #y) \
  DEF(TOK_ASM_ ## x ## ge ## _ ## y, #x "ge." #y) \
  DEF(TOK_ASM_ ## x ## lt ## _ ## y, #x "lt." #y) \
  DEF(TOK_ASM_ ## x ## gt ## _ ## y, #x "gt." #y) \
  DEF(TOK_ASM_ ## x ## le ## _ ## y, #x "le." #y) \
  DEF(TOK_ASM_ ## x ## _ ## y, #x "." #y) \
  DEF(TOK_ASM_ ## x ## rsvd ## _ ## y, #x "rsvd." #y)

#define DEF_ASM_CONDED_VFP_F32_F64(x) \
  DEF_ASM_CONDED_WITH_SUFFIX(x, f32) \
  DEF_ASM_CONDED_WITH_SUFFIX(x, f64)

#define DEF_ASM_CONDED_WITH_TWO_SUFFIXES(x, y, z) \
  DEF(TOK_ASM_ ## x ## eq ## _ ## y ## _ ## z, #x "eq." #y "." #z) \
  DEF(TOK_ASM_ ## x ## ne ## _ ## y ## _ ## z, #x "ne." #y "." #z) \
  DEF(TOK_ASM_ ## x ## cs ## _ ## y ## _ ## z, #x "cs." #y "." #z) \
  DEF(TOK_ASM_ ## x ## cc ## _ ## y ## _ ## z, #x "cc." #y "." #z) \
  DEF(TOK_ASM_ ## x ## mi ## _ ## y ## _ ## z, #x "mi." #y "." #z) \
  DEF(TOK_ASM_ ## x ## pl ## _ ## y ## _ ## z, #x "pl." #y "." #z) \
  DEF(TOK_ASM_ ## x ## vs ## _ ## y ## _ ## z, #x "vs." #y "." #z) \
  DEF(TOK_ASM_ ## x ## vc ## _ ## y ## _ ## z, #x "vc." #y "." #z) \
  DEF(TOK_ASM_ ## x ## hi ## _ ## y ## _ ## z, #x "hi." #y "." #z) \
  DEF(TOK_ASM_ ## x ## ls ## _ ## y ## _ ## z, #x "ls." #y "." #z) \
  DEF(TOK_ASM_ ## x ## ge ## _ ## y ## _ ## z, #x "ge." #y "." #z) \
  DEF(TOK_ASM_ ## x ## lt ## _ ## y ## _ ## z, #x "lt." #y "." #z) \
  DEF(TOK_ASM_ ## x ## gt ## _ ## y ## _ ## z, #x "gt." #y "." #z) \
  DEF(TOK_ASM_ ## x ## le ## _ ## y ## _ ## z, #x "le." #y "." #z) \
  DEF(TOK_ASM_ ## x ## _ ## y ## _ ## z, #x "." #y "." #z) \
  DEF(TOK_ASM_ ## x ## rsvd ## _ ## y ## _ ## z, #x "rsvd." #y "." #z)

/* Note: add new tokens after nop (MUST always use DEF_ASM_CONDED) */

 DEF_ASM_CONDED(nop)
 DEF_ASM_CONDED(wfe)
 DEF_ASM_CONDED(wfi)
 DEF_ASM_CONDED(swi)
 DEF_ASM_CONDED(svc)

 /* misc */
 DEF_ASM_CONDED(clz)

 /* size conversion */

 DEF_ASM_CONDED(sxtb)
 DEF_ASM_CONDED(sxth)
 DEF_ASM_CONDED(uxtb)
 DEF_ASM_CONDED(uxth)
 DEF_ASM_CONDED(movt)
 DEF_ASM_CONDED(movw)

 /* multiplication */

 DEF_ASM_CONDED(mul)
 DEF_ASM_CONDED(muls)
 DEF_ASM_CONDED(mla)
 DEF_ASM_CONDED(mlas)
 DEF_ASM_CONDED(smull)
 DEF_ASM_CONDED(smulls)
 DEF_ASM_CONDED(umull)
 DEF_ASM_CONDED(umulls)
 DEF_ASM_CONDED(smlal)
 DEF_ASM_CONDED(smlals)
 DEF_ASM_CONDED(umlal)
 DEF_ASM_CONDED(umlals)

 /* load/store */

 DEF_ASM_CONDED(ldr)
 DEF_ASM_CONDED(ldrb)
 DEF_ASM_CONDED(str)
 DEF_ASM_CONDED(strb)
 DEF_ASM_CONDED(ldrex)
 DEF_ASM_CONDED(ldrexb)
 DEF_ASM_CONDED(strex)
 DEF_ASM_CONDED(strexb)
 DEF_ASM_CONDED(ldrh)
 DEF_ASM_CONDED(ldrsh)
 DEF_ASM_CONDED(ldrsb)
 DEF_ASM_CONDED(strh)

 DEF_ASM_CONDED(stmda)
 DEF_ASM_CONDED(ldmda)
 DEF_ASM_CONDED(stm)
 DEF_ASM_CONDED(ldm)
 DEF_ASM_CONDED(stmia)
 DEF_ASM_CONDED(ldmia)
 DEF_ASM_CONDED(stmdb)
 DEF_ASM_CONDED(ldmdb)
 DEF_ASM_CONDED(stmib)
 DEF_ASM_CONDED(ldmib)

 DEF_ASM_CONDED(ldc)
 DEF_ASM_CONDED(ldcl)
 DEF_ASM_CONDED(stc)
 DEF_ASM_CONDED(stcl)

 /* instruction macros */

 DEF_ASM_CONDED(push)
 DEF_ASM_CONDED(pop)

 /* branches */

 DEF_ASM_CONDED(b)
 DEF_ASM_CONDED(bl)
 DEF_ASM_CONDED(bx)
 DEF_ASM_CONDED(blx)

 /* data processing instructions; order is important */

 DEF_ASM_CONDED(and)
 DEF_ASM_CONDED(ands)
 DEF_ASM_CONDED(eor)
 DEF_ASM_CONDED(eors)
 DEF_ASM_CONDED(sub)
 DEF_ASM_CONDED(subs)
 DEF_ASM_CONDED(rsb)
 DEF_ASM_CONDED(rsbs)
 DEF_ASM_CONDED(add)
 DEF_ASM_CONDED(adds)
 DEF_ASM_CONDED(adc)
 DEF_ASM_CONDED(adcs)
 DEF_ASM_CONDED(sbc)
 DEF_ASM_CONDED(sbcs)
 DEF_ASM_CONDED(rsc)
 DEF_ASM_CONDED(rscs)
 DEF_ASM_CONDED(tst)
 DEF_ASM_CONDED(tsts) // necessary here--but not useful to the user
 DEF_ASM_CONDED(teq)
 DEF_ASM_CONDED(teqs) // necessary here--but not useful to the user
 DEF_ASM_CONDED(cmp)
 DEF_ASM_CONDED(cmps) // necessary here--but not useful to the user
 DEF_ASM_CONDED(cmn)
 DEF_ASM_CONDED(cmns) // necessary here--but not useful to the user
 DEF_ASM_CONDED(orr)
 DEF_ASM_CONDED(orrs)
 DEF_ASM_CONDED(mov)
 DEF_ASM_CONDED(movs)
 DEF_ASM_CONDED(bic)
 DEF_ASM_CONDED(bics)
 DEF_ASM_CONDED(mvn)
 DEF_ASM_CONDED(mvns)

 DEF_ASM_CONDED(lsl)
 DEF_ASM_CONDED(lsls)
 DEF_ASM_CONDED(lsr)
 DEF_ASM_CONDED(lsrs)
 DEF_ASM_CONDED(asr)
 DEF_ASM_CONDED(asrs)
 DEF_ASM_CONDED(ror)
 DEF_ASM_CONDED(rors)
 DEF_ASM_CONDED(rrx)
 DEF_ASM_CONDED(rrxs)

 DEF_ASM_CONDED(cdp)
 DEF_ASM_CONDED(mcr)
 DEF_ASM_CONDED(mrc)

 // Floating point high-level instructions

 DEF_ASM_CONDED(vldr)
 DEF_ASM_CONDED(vstr)

 DEF_ASM_CONDED_VFP_F32_F64(vmla)
 DEF_ASM_CONDED_VFP_F32_F64(vmls)
 DEF_ASM_CONDED_VFP_F32_F64(vnmls)
 DEF_ASM_CONDED_VFP_F32_F64(vnmla)
 DEF_ASM_CONDED_VFP_F32_F64(vmul)
 DEF_ASM_CONDED_VFP_F32_F64(vnmul)
 DEF_ASM_CONDED_VFP_F32_F64(vadd)
 DEF_ASM_CONDED_VFP_F32_F64(vsub)
 DEF_ASM_CONDED_VFP_F32_F64(vdiv)
 DEF_ASM_CONDED_VFP_F32_F64(vneg)
 DEF_ASM_CONDED_VFP_F32_F64(vabs)
 DEF_ASM_CONDED_VFP_F32_F64(vsqrt)
 DEF_ASM_CONDED_VFP_F32_F64(vcmp)
 DEF_ASM_CONDED_VFP_F32_F64(vcmpe)
 DEF_ASM_CONDED_VFP_F32_F64(vmov)

 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvtr, s32, f64)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvtr, s32, f32)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvtr, u32, f64)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvtr, u32, f32)

 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, s32, f64)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, s32, f32)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, u32, f64)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, u32, f32)

 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, f64, s32)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, f32, s32)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, f64, u32)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, f32, u32)

 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, f64, f32)
 DEF_ASM_CONDED_WITH_TWO_SUFFIXES(vcvt, f32, f64)

 DEF_ASM_CONDED(vpush)
 DEF_ASM_CONDED(vpop)
 DEF_ASM_CONDED(vldm)
 DEF_ASM_CONDED(vldmia)
 DEF_ASM_CONDED(vldmdb)
 DEF_ASM_CONDED(vstm)
 DEF_ASM_CONDED(vstmia)
 DEF_ASM_CONDED(vstmdb)
 DEF_ASM_CONDED(vmsr)
 DEF_ASM_CONDED(vmrs)
