/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AArch32 implementation of the ARM64 system register accessors used by code shared with ARMv7
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ARM64_AARCH32_MRC(Op1, CRn, CRm, Op2) \
    __extension__ ({ unsigned int _Arm32Value; \
                     __asm__ __volatile__("mrc p15, " #Op1 ", %0, c" #CRn ", c" #CRm ", " #Op2 : "=r"(_Arm32Value) :: "memory"); \
                     (unsigned long long)_Arm32Value; })
#define ARM64_AARCH32_MCR(Op1, CRn, CRm, Op2, Value) \
    __asm__ __volatile__("mcr p15, " #Op1 ", %0, c" #CRn ", c" #CRm ", " #Op2 :: "r"((unsigned int)(Value)) : "memory")
#define ARM64_AARCH32_MRRC(Op1, CRm) \
    __extension__ ({ unsigned int _Arm32Low, _Arm32High; \
                     __asm__ __volatile__("mrrc p15, " #Op1 ", %0, %1, c" #CRm : "=r"(_Arm32Low), "=r"(_Arm32High) :: "memory"); \
                     ((unsigned long long)_Arm32High << 32) | _Arm32Low; })
#define ARM64_AARCH32_MCRR(Op1, CRm, Value) \
    do { unsigned long long _Arm32Value = (Value); \
         __asm__ __volatile__("mcrr p15, " #Op1 ", %0, %1, c" #CRm \
                              :: "r"((unsigned int)_Arm32Value), "r"((unsigned int)(_Arm32Value >> 32)) : "memory"); } while (0)

#define ARM64_READ_SYSREG(Reg)              ARM64_AARCH32_READ_##Reg()
#define ARM64_WRITE_SYSREG(Reg, Value)      ARM64_AARCH32_WRITE_##Reg(Value)

#define ARM64_AARCH32_READ_MIDR_EL1()       ARM64_AARCH32_MRC(0, 0, 0, 0)
#define ARM64_AARCH32_READ_CTR_EL0()        ARM64_AARCH32_MRC(0, 0, 0, 1)
#define ARM64_AARCH32_READ_MPIDR_EL1()      ARM64_AARCH32_MRC(0, 0, 0, 5)
/* ID_PFR1.GIC (bits 31:28) reports the GIC system register interface that ID_AA64PFR0_EL1.GIC (bits 27:24) reports. */
#define ARM64_AARCH32_READ_ID_AA64PFR0_EL1() (((ARM64_AARCH32_MRC(0, 0, 1, 1) >> 28) & 0xF) << 24)
#define ARM64_AARCH32_READ_CPACR_EL1()      ARM64_AARCH32_MRC(0, 1, 0, 2)
#define ARM64_AARCH32_READ_ISR_EL1()        ARM64_AARCH32_MRC(0, 12, 1, 0)
#define ARM64_AARCH32_READ_CNTFRQ_EL0()     ARM64_AARCH32_MRC(0, 14, 0, 0)
#define ARM64_AARCH32_READ_CNTVCT_EL0()     ARM64_AARCH32_MRRC(1, 14)
#define ARM64_AARCH32_READ_CNTP_CTL_EL0()   ARM64_AARCH32_MRC(0, 14, 2, 1)
#define ARM64_AARCH32_WRITE_CNTP_CTL_EL0(Value) ARM64_AARCH32_MCR(0, 14, 2, 1, Value)
#define ARM64_AARCH32_READ_CNTV_CTL_EL0()   ARM64_AARCH32_MRC(0, 14, 3, 1)
#define ARM64_AARCH32_WRITE_CNTV_CTL_EL0(Value) ARM64_AARCH32_MCR(0, 14, 3, 1, Value)
#define ARM64_AARCH32_READ_CNTV_CVAL_EL0()  ARM64_AARCH32_MRRC(3, 14)
#define ARM64_AARCH32_WRITE_CNTV_CVAL_EL0(Value) ARM64_AARCH32_MCRR(3, 14, Value)

#define ARM64_AARCH32_READ_ICC_PMR_EL1()    ARM64_AARCH32_MRC(0, 4, 6, 0)
#define ARM64_AARCH32_WRITE_ICC_PMR_EL1(Value) ARM64_AARCH32_MCR(0, 4, 6, 0, Value)
#define ARM64_AARCH32_READ_ICC_IAR1_EL1()   ARM64_AARCH32_MRC(0, 12, 12, 0)
#define ARM64_AARCH32_WRITE_ICC_EOIR1_EL1(Value) ARM64_AARCH32_MCR(0, 12, 12, 1, Value)
#define ARM64_AARCH32_WRITE_ICC_BPR1_EL1(Value) ARM64_AARCH32_MCR(0, 12, 12, 3, Value)
#define ARM64_AARCH32_READ_ICC_SRE_EL1()    ARM64_AARCH32_MRC(0, 12, 12, 5)
#define ARM64_AARCH32_WRITE_ICC_SRE_EL1(Value) ARM64_AARCH32_MCR(0, 12, 12, 5, Value)
#define ARM64_AARCH32_READ_ICC_IGRPEN1_EL1() ARM64_AARCH32_MRC(0, 12, 12, 7)
#define ARM64_AARCH32_WRITE_ICC_IGRPEN1_EL1(Value) ARM64_AARCH32_MCR(0, 12, 12, 7, Value)
#define ARM64_AARCH32_WRITE_ICC_SGI1R_EL1(Value) ARM64_AARCH32_MCRR(0, 12, Value)

#define ARM64_AARCH32_DAIF_A 0x100
#define ARM64_AARCH32_DAIF_I 0x080
#define ARM64_AARCH32_DAIF_F 0x040

static __inline__ unsigned long long
Arm64Aarch32ReadDaif(void)
{
    unsigned int Cpsr;

    __asm__ __volatile__("mrs %0, cpsr" : "=r"(Cpsr) :: "memory");
    return Cpsr & (ARM64_AARCH32_DAIF_A | ARM64_AARCH32_DAIF_I | ARM64_AARCH32_DAIF_F);
}

static __inline__ void
Arm64Aarch32WriteDaif(unsigned long long Daif)
{
    if (Daif & ARM64_AARCH32_DAIF_A)
        __asm__ __volatile__("cpsid a" ::: "memory");
    else
        __asm__ __volatile__("cpsie a" ::: "memory");
    if (Daif & ARM64_AARCH32_DAIF_I)
        __asm__ __volatile__("cpsid i" ::: "memory");
    else
        __asm__ __volatile__("cpsie i" ::: "memory");
    if (Daif & ARM64_AARCH32_DAIF_F)
        __asm__ __volatile__("cpsid f" ::: "memory");
    else
        __asm__ __volatile__("cpsie f" ::: "memory");
}

#define ARM64_AARCH32_READ_DAIF()           Arm64Aarch32ReadDaif()
#define ARM64_AARCH32_WRITE_DAIF(Value)     Arm64Aarch32WriteDaif(Value)

#define ARM64_DAIFSET(Imm)                  ARM64_AARCH32_DAIFSET_##Imm()
#define ARM64_AARCH32_DAIFSET_3()           __asm__ __volatile__("cpsid if" ::: "memory")

#define ARM64_DSB(Option)                   ARM64_AARCH32_DSB_##Option()
#define ARM64_AARCH32_DSB_SY()              __asm__ __volatile__("dsb sy" ::: "memory")
#define ARM64_AARCH32_DSB_ISH()             __asm__ __volatile__("dsb ish" ::: "memory")
#define ARM64_AARCH32_DSB_ISHST()           __asm__ __volatile__("dsb ishst" ::: "memory")
#define ARM64_DMB(Option)                   ARM64_AARCH32_DMB_##Option()
#define ARM64_AARCH32_DMB_ISH()             __asm__ __volatile__("dmb ish" ::: "memory")
#define ARM64_AARCH32_DMB_ISHST()           __asm__ __volatile__("dmb ishst" ::: "memory")
/* ARMv7 has no load-only barrier; the full barrier of the same domain also orders loads. */
#define ARM64_AARCH32_DMB_LD()              __asm__ __volatile__("dmb sy" ::: "memory")
#define ARM64_AARCH32_DMB_ISHLD()           __asm__ __volatile__("dmb ish" ::: "memory")
#define ARM64_ISB()                         __asm__ __volatile__("isb" ::: "memory")

#define ARM64_DC(Op, Address)               ARM64_AARCH32_DC_##Op(Address)
#define ARM64_AARCH32_DC_IVAC(Address)      ARM64_AARCH32_MCR(0, 7, 6, 1, (unsigned int)(Address))
#define ARM64_AARCH32_DC_CVAC(Address)      ARM64_AARCH32_MCR(0, 7, 10, 1, (unsigned int)(Address))
#define ARM64_AARCH32_DC_CIVAC(Address)     ARM64_AARCH32_MCR(0, 7, 14, 1, (unsigned int)(Address))
#define ARM64_IC_ALL(Op)                    ARM64_AARCH32_IC_##Op()
#define ARM64_AARCH32_IC_IALLU()            ARM64_AARCH32_MCR(0, 7, 5, 0, 0)

#define ARM64_WFI()                         __asm__ __volatile__("wfi" ::: "memory")
#define ARM64_WFE()                         __asm__ __volatile__("wfe" ::: "memory")
#define ARM64_SEV()                         __asm__ __volatile__("sev" ::: "memory")
#define ARM64_YIELD()                       __asm__ __volatile__("yield" ::: "memory")
#define ARM64_NOP()                         __asm__ __volatile__("nop")
#define ARM64_COMPILER_BARRIER()            __asm__ __volatile__("" ::: "memory")
