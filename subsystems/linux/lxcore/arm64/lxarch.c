/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AArch64 Linux system call table and register state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxcore.h"
#include <reactos/arm64/sysreg.h>

#define LX_EM_AARCH64     183
#define LX_HWCAP_FP       (1ULL << 0)
#define LX_HWCAP_ASIMD    (1ULL << 1)
#define LX_HWCAP_AES      (1ULL << 3)
#define LX_HWCAP_PMULL    (1ULL << 4)
#define LX_HWCAP_SHA1     (1ULL << 5)
#define LX_HWCAP_SHA2     (1ULL << 6)
#define LX_HWCAP_CRC32    (1ULL << 7)
#define LX_HWCAP_ATOMICS  (1ULL << 8)
#define LX_HWCAP_FPHP     (1ULL << 9)
#define LX_HWCAP_ASIMDHP  (1ULL << 10)
#define LX_HWCAP_CPUID    (1ULL << 11)
#define LX_HWCAP_ASIMDRDM (1ULL << 12)
#define LX_HWCAP_JSCVT    (1ULL << 13)
#define LX_HWCAP_FCMA     (1ULL << 14)
#define LX_HWCAP_LRCPC    (1ULL << 15)
#define LX_HWCAP_DCPOP    (1ULL << 16)
#define LX_HWCAP_SHA3     (1ULL << 17)
#define LX_HWCAP_SM3      (1ULL << 18)
#define LX_HWCAP_SM4      (1ULL << 19)
#define LX_HWCAP_ASIMDDP  (1ULL << 20)
#define LX_HWCAP_SHA512   (1ULL << 21)
#define LX_HWCAP_ASIMDFHM (1ULL << 23)
#define LX_HWCAP_DIT      (1ULL << 24)
#define LX_HWCAP_USCAT    (1ULL << 25)
#define LX_HWCAP_ILRCPC   (1ULL << 26)
#define LX_HWCAP_FLAGM    (1ULL << 27)
#define LX_HWCAP_SSBS     (1ULL << 28)
#define LX_HWCAP_SB       (1ULL << 29)
#define LX_HWCAP2_DCPODP  (1ULL << 0)
#define LX_HWCAP2_FLAGM2  (1ULL << 7)
#define LX_HWCAP2_FRINT   (1ULL << 8)
#define LX_HWCAP2_I8MM    (1ULL << 13)
#define LX_HWCAP2_BF16    (1ULL << 14)
#define LX_HWCAP2_DGH     (1ULL << 15)
#define LX_HWCAP2_RNG     (1ULL << 16)
#define LX_ID_FIELD(Value, Shift) ((ULONG)(((Value) >> (Shift)) & 0xF))

typedef struct _LX_ARM64_ID_REGISTER
{
    UCHAR Crm;
    UCHAR Op2;
    ULONG64 Visible;
} LX_ARM64_ID_REGISTER;

static const LX_ARM64_ID_REGISTER LxArm64IdRegisters[] =
{
    { 0, 0, 0x00000000FFFFFFFFULL },
    { 2, 5, 0x00000000000FFFF0ULL },
    { 3, 0, 0x0000000000000F00ULL },
    { 3, 1, 0x00000000F00FFF00ULL },
    { 4, 0, 0x000F000F00FF0000ULL },
    { 4, 1, 0x000000000F000FFFULL },
    { 4, 4, 0x0FF0FF0F0FFF00FFULL },
    { 6, 0, 0xF0FFFFFFF0FFFFF0ULL },
    { 6, 1, 0x00FFF0FFFFFFFFFFULL },
    { 6, 2, 0x00FF000000FFFFFFULL },
    { 7, 0, 0xF000000000000000ULL },
    { 7, 1, 0x0000F00000000000ULL },
    { 7, 2, 0x0000000F00000000ULL },
};

static
ULONG64
LxArm64ReadIdRegister(
    _In_ ULONG Crm,
    _In_ ULONG Op2)
{
    ULONG64 Value = 0;

    switch ((Crm << 3) | Op2)
    {
        case 0x00: Value = ARM64_READ_SYSREG(MIDR_EL1); break;
        case 0x15: Value = ARM64_READ_SYSREG(S3_0_C0_C2_5); break;
        case 0x18: Value = ARM64_READ_SYSREG(S3_0_C0_C3_0); break;
        case 0x19: Value = ARM64_READ_SYSREG(S3_0_C0_C3_1); break;
        case 0x20: Value = ARM64_READ_SYSREG(S3_0_C0_C4_0); break;
        case 0x21: Value = ARM64_READ_SYSREG(S3_0_C0_C4_1); break;
        case 0x24: Value = ARM64_READ_SYSREG(S3_0_C0_C4_4); break;
        case 0x30: Value = ARM64_READ_SYSREG(S3_0_C0_C6_0); break;
        case 0x31: Value = ARM64_READ_SYSREG(S3_0_C0_C6_1); break;
        case 0x32: Value = ARM64_READ_SYSREG(S3_0_C0_C6_2); break;
        case 0x38: Value = ARM64_READ_SYSREG(S3_0_C0_C7_0); break;
        case 0x39: Value = ARM64_READ_SYSREG(S3_0_C0_C7_1); break;
        case 0x3A: Value = ARM64_READ_SYSREG(S3_0_C0_C7_2); break;
        default: break;
    }
    return Value;
}

static
BOOLEAN
LxArm64EmulateIdRegister(
    _In_ ULONG Instruction,
    _Out_ PULONG64 Value)
{
    ULONG Crm = (Instruction >> 8) & 0xF, Op2 = (Instruction >> 5) & 0x7;
    ULONG Index;

    if ((Instruction & 0xFFFFF000) != 0xD5380000)
        return FALSE;
    if ((Crm == 1) || (Crm > 7))
        return FALSE;
    *Value = 0;
    if ((Crm == 0) && (Op2 == 5))
        *Value = 0x80000000ULL;
    for (Index = 0; Index < RTL_NUMBER_OF(LxArm64IdRegisters); Index++)
    {
        if ((LxArm64IdRegisters[Index].Crm == Crm) && (LxArm64IdRegisters[Index].Op2 == Op2))
        {
            *Value = LxArm64ReadIdRegister(Crm, Op2) & LxArm64IdRegisters[Index].Visible;
            break;
        }
    }
    return TRUE;
}

#define LX_A64_FRAME_SIZE       4688
#define LX_A64_UC               128
#define LX_A64_UC_STACK         (LX_A64_UC + 16)
#define LX_A64_UC_SIGMASK       (LX_A64_UC + 40)
#define LX_A64_MC               (LX_A64_UC + 176)
#define LX_A64_MC_FAULT         (LX_A64_MC + 0)
#define LX_A64_MC_REGS          (LX_A64_MC + 8)
#define LX_A64_MC_SP            (LX_A64_MC + 256)
#define LX_A64_MC_PC            (LX_A64_MC + 264)
#define LX_A64_MC_PSTATE        (LX_A64_MC + 272)
#define LX_A64_MC_RESERVED      (LX_A64_MC + 288)
#define LX_A64_FPSIMD_MAGIC     0x46508001
#define LX_A64_FPSIMD_SIZE      528
#define LX_A64_PSTATE_NZCV      0xF0000000UL

C_ASSERT(LX_A64_MC_RESERVED + 4096 == LX_A64_FRAME_SIZE);

PLX_SYSCALL_ENTRY
LxArchSyscallEntry(
    _In_ ULONG Number)
{
    return LxGenericSyscallEntry(Number);
}

ULONG
LxArchElfMachine(VOID)
{
    return LX_EM_AARCH64;
}

static const PCSTR LxArm64HwcapNames[] =
{
    "fp", "asimd", "evtstrm", "aes", "pmull", "sha1", "sha2", "crc32", "atomics", "fphp", "asimdhp", "cpuid",
    "asimdrdm", "jscvt", "fcma", "lrcpc", "dcpop", "sha3", "sm3", "sm4", "asimddp", "sha512", "sve", "asimdfhm",
    "dit", "uscat", "ilrcpc", "flagm", "ssbs", "sb", "paca", "pacg"
};

static const PCSTR LxArm64Hwcap2Names[] =
{
    "dcpodp", "sve2", "sveaes", "svepmull", "svebitperm", "svesha3", "svesm4", "flagm2", "frint", "svei8mm",
    "svef32mm", "svef64mm", "svebf16", "i8mm", "bf16", "dgh", "rng"
};

static
VOID
LxArm64Hwcaps(
    _Out_ PULONG64 Hwcap,
    _Out_ PULONG64 Hwcap2)
{
    ULONG64 Pfr0 = LxArm64ReadIdRegister(4, 0), Pfr1 = LxArm64ReadIdRegister(4, 1);
    ULONG64 Isar0 = LxArm64ReadIdRegister(6, 0), Isar1 = LxArm64ReadIdRegister(6, 1);
    ULONG64 Mmfr2 = LxArm64ReadIdRegister(7, 2);
    ULONG Fp = LX_ID_FIELD(Pfr0, 16), Simd = LX_ID_FIELD(Pfr0, 20), Aes = LX_ID_FIELD(Isar0, 4);
    ULONG Sha2 = LX_ID_FIELD(Isar0, 12), Ts = LX_ID_FIELD(Isar0, 52), Lrcpc = LX_ID_FIELD(Isar1, 20);
    ULONG Dpb = LX_ID_FIELD(Isar1, 0);
    ULONG64 Value = LX_HWCAP_CPUID, Value2 = 0;

    if (Fp != 0xF)
        Value |= LX_HWCAP_FP | ((Fp == 1) ? LX_HWCAP_FPHP : 0);
    if (Simd != 0xF)
        Value |= LX_HWCAP_ASIMD | ((Simd == 1) ? LX_HWCAP_ASIMDHP : 0);
    Value |= (Aes >= 1) ? LX_HWCAP_AES : 0;
    Value |= (Aes >= 2) ? LX_HWCAP_PMULL : 0;
    Value |= (LX_ID_FIELD(Isar0, 8) >= 1) ? LX_HWCAP_SHA1 : 0;
    Value |= (Sha2 >= 1) ? LX_HWCAP_SHA2 : 0;
    Value |= (Sha2 >= 2) ? LX_HWCAP_SHA512 : 0;
    Value |= (LX_ID_FIELD(Isar0, 16) >= 1) ? LX_HWCAP_CRC32 : 0;
    Value |= (LX_ID_FIELD(Isar0, 20) >= 2) ? LX_HWCAP_ATOMICS : 0;
    Value |= (LX_ID_FIELD(Isar0, 28) >= 1) ? LX_HWCAP_ASIMDRDM : 0;
    Value |= (LX_ID_FIELD(Isar0, 32) >= 1) ? LX_HWCAP_SHA3 : 0;
    Value |= (LX_ID_FIELD(Isar0, 36) >= 1) ? LX_HWCAP_SM3 : 0;
    Value |= (LX_ID_FIELD(Isar0, 40) >= 1) ? LX_HWCAP_SM4 : 0;
    Value |= (LX_ID_FIELD(Isar0, 44) >= 1) ? LX_HWCAP_ASIMDDP : 0;
    Value |= (LX_ID_FIELD(Isar0, 48) >= 1) ? LX_HWCAP_ASIMDFHM : 0;
    Value |= (Ts >= 1) ? LX_HWCAP_FLAGM : 0;
    Value |= (LX_ID_FIELD(Isar1, 12) >= 1) ? LX_HWCAP_JSCVT : 0;
    Value |= (LX_ID_FIELD(Isar1, 16) >= 1) ? LX_HWCAP_FCMA : 0;
    Value |= (Lrcpc >= 1) ? LX_HWCAP_LRCPC : 0;
    Value |= (Lrcpc >= 2) ? LX_HWCAP_ILRCPC : 0;
    Value |= (Dpb >= 1) ? LX_HWCAP_DCPOP : 0;
    Value |= (LX_ID_FIELD(Isar1, 36) >= 1) ? LX_HWCAP_SB : 0;
    Value |= (LX_ID_FIELD(Pfr0, 48) >= 1) ? LX_HWCAP_DIT : 0;
    Value |= (LX_ID_FIELD(Pfr1, 4) >= 2) ? LX_HWCAP_SSBS : 0;
    Value |= (LX_ID_FIELD(Mmfr2, 32) >= 1) ? LX_HWCAP_USCAT : 0;
    Value2 |= (Dpb >= 2) ? LX_HWCAP2_DCPODP : 0;
    Value2 |= (Ts >= 2) ? LX_HWCAP2_FLAGM2 : 0;
    Value2 |= (LX_ID_FIELD(Isar1, 32) >= 1) ? LX_HWCAP2_FRINT : 0;
    Value2 |= (LX_ID_FIELD(Isar1, 52) >= 1) ? LX_HWCAP2_I8MM : 0;
    Value2 |= (LX_ID_FIELD(Isar1, 44) >= 1) ? LX_HWCAP2_BF16 : 0;
    Value2 |= (LX_ID_FIELD(Isar1, 48) >= 1) ? LX_HWCAP2_DGH : 0;
    Value2 |= (LX_ID_FIELD(Isar0, 60) >= 1) ? LX_HWCAP2_RNG : 0;
    *Hwcap = Value;
    *Hwcap2 = Value2;
}

ULONG64
LxArchHwcap(VOID)
{
    ULONG64 Hwcap, Hwcap2;

    LxArm64Hwcaps(&Hwcap, &Hwcap2);
    return Hwcap;
}

ULONG64
LxArchHwcap2(VOID)
{
    ULONG64 Hwcap, Hwcap2;

    LxArm64Hwcaps(&Hwcap, &Hwcap2);
    return Hwcap2;
}

PCSTR
LxArchPlatform(VOID)
{
    return "aarch64";
}

PCSTR
LxArchCpuinfo(VOID)
{
    static CHAR Text[1024];
    static volatile LONG Ready;
    ULONG64 Hwcap, Hwcap2, Midr, Frequency;
    SIZE_T Used;
    ULONG Bit;

    if (ReadAcquire(&Ready))
        return Text;
    LxArm64Hwcaps(&Hwcap, &Hwcap2);
    Midr = ARM64_READ_SYSREG(MIDR_EL1);
    Frequency = ARM64_READ_SYSREG(CNTFRQ_EL0) / 5000;
    RtlStringCbPrintfA(Text, sizeof(Text), "BogoMIPS\t: %llu.%02llu\nFeatures\t:", Frequency / 100, Frequency % 100);
    for (Bit = 0; Bit < RTL_NUMBER_OF(LxArm64HwcapNames); Bit++)
    {
        if (Hwcap & (1ULL << Bit))
        {
            RtlStringCbCatA(Text, sizeof(Text), " ");
            RtlStringCbCatA(Text, sizeof(Text), LxArm64HwcapNames[Bit]);
        }
    }
    for (Bit = 0; Bit < RTL_NUMBER_OF(LxArm64Hwcap2Names); Bit++)
    {
        if (Hwcap2 & (1ULL << Bit))
        {
            RtlStringCbCatA(Text, sizeof(Text), " ");
            RtlStringCbCatA(Text, sizeof(Text), LxArm64Hwcap2Names[Bit]);
        }
    }
    Used = strlen(Text);
    RtlStringCbPrintfA(Text + Used, sizeof(Text) - Used,
                       "\nCPU implementer\t: 0x%02llx\nCPU architecture: 8\nCPU variant\t: 0x%llx\n"
                       "CPU part\t: 0x%03llx\nCPU revision\t: %llu\n",
                       (Midr >> 24) & 0xFF, (Midr >> 20) & 0xF, (Midr >> 4) & 0xFFF, Midr & 0xF);
    WriteRelease(&Ready, 1);
    return Text;
}

PCSTR
LxArchConfigName(VOID)
{
    return "arm64";
}

PCSTR
LxArchKernelConfig(VOID)
{
    return "CONFIG_ARM64=y\n";
}

VOID
LxArchGetSyscall(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PULONG Number,
    _Out_writes_(6) PULONG64 Args)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    *Number = (ULONG)TrapFrame->X8;
    for (Index = 0; Index < 6; Index++)
        Args[Index] = TrapFrame->X[Index];
}

VOID
LxArchSetSyscallResult(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ LONG64 Result)
{
    TrapFrame->X0 = (ULONG64)Result;
}

BOOLEAN
LxArchEmulateInstruction(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ULONG Instruction;
    ULONG64 Value;
    ULONG Rt;

    if (ExceptionRecord->ExceptionCode != STATUS_ILLEGAL_INSTRUCTION)
        return FALSE;
    if (LxCopyFromUser(&Instruction, TrapFrame->Pc, sizeof(Instruction)) != 0)
        return FALSE;
    if ((Instruction & 0xFFFFFFE0) == 0xD53B0020)
        Value = ARM64_READ_SYSREG(CTR_EL0);
    else if (!LxArm64EmulateIdRegister(Instruction, &Value))
        return FALSE;
    Rt = Instruction & 0x1F;
    if (Rt <= 18)
        TrapFrame->X[Rt] = Value;
    else if ((Rt <= 28) && (ExceptionFrame != NULL))
        (&ExceptionFrame->X19)[Rt - 19] = Value;
    else if (Rt == 29)
        TrapFrame->Fp = Value;
    else if (Rt == 30)
        TrapFrame->Lr = Value;
    else if (Rt != 31)
        return FALSE;
    TrapFrame->Pc += 4;
    return TRUE;
}

typedef struct _LX_ARM64_CORE
{
    UCHAR Implementer;
    USHORT Part;
} LX_ARM64_CORE;

static const LX_ARM64_CORE LxArm64SsbSafe[] =
{
    { 0x41, 0xD03 }, { 0x41, 0xD04 }, { 0x41, 0xD05 }, { 0x42, 0x100 }, { 0x51, 0x803 }, { 0x51, 0x805 },
};

static const LX_ARM64_CORE LxArm64MeltdownSafe[] =
{
    { 0x41, 0xD03 }, { 0x41, 0xD04 }, { 0x41, 0xD05 }, { 0x41, 0xD07 }, { 0x41, 0xD08 }, { 0x41, 0xD09 },
    { 0x42, 0x100 }, { 0x42, 0x516 }, { 0x43, 0x0AF }, { 0x48, 0xD01 }, { 0x4E, 0x004 }, { 0x51, 0x800 },
    { 0x51, 0x801 }, { 0x51, 0x803 }, { 0x51, 0x805 },
};

static const LX_ARM64_CORE LxArm64SpectreV2Safe[] =
{
    { 0x41, 0xD03 }, { 0x41, 0xD04 }, { 0x41, 0xD05 }, { 0x42, 0x100 }, { 0x48, 0xD01 }, { 0x51, 0x801 },
    { 0x51, 0x803 }, { 0x51, 0x805 },
};

static const LX_ARM64_CORE LxArm64BhbSafe[] =
{
    { 0x41, 0xD03 }, { 0x41, 0xD04 }, { 0x41, 0xD05 }, { 0x41, 0xD46 }, { 0x41, 0xD80 }, { 0x42, 0x100 },
    { 0x51, 0x801 }, { 0x51, 0x803 }, { 0x51, 0x805 },
};

static
BOOLEAN
LxArm64CoreListed(
    _In_ ULONG64 Midr,
    _In_reads_(Count) const LX_ARM64_CORE *List,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if ((((Midr >> 24) & 0xFF) == List[Index].Implementer) && (((Midr >> 4) & 0xFFF) == List[Index].Part))
            return TRUE;
    }
    return FALSE;
}

LONG
LxArchSpecStoreBypassMode(VOID)
{
    static volatile LONG Cached = MINLONG;
    ULONG64 Midr;
    LONG Mode;

    Mode = ReadNoFence(&Cached);
    if (Mode != MINLONG)
        return Mode;
    Midr = ARM64_READ_SYSREG(MIDR_EL1);
    Mode = (LX_ID_FIELD(LxArm64ReadIdRegister(4, 1), 4) != 0) ? LX_SPEC_MODE_DYNAMIC : LX_SPEC_MODE_VULNERABLE;
    if (LxArm64CoreListed(Midr, LxArm64SsbSafe, RTL_NUMBER_OF(LxArm64SsbSafe)))
        Mode = LX_SPEC_MODE_UNAFFECTED;
    InterlockedExchange(&Cached, Mode);
    return Mode;
}

#define LX_A64_SCTLR_DSSBS      (1ULL << 44)
#define LX_VULN_UNAFFECTED      0
#define LX_VULN_MITIGATED       1
#define LX_VULN_VULNERABLE      2

typedef struct _LX_ARM64_EXPOSURE
{
    LONG Meltdown;
    LONG SpectreV2;
    LONG Bhb;
    LONG Ssb;
} LX_ARM64_EXPOSURE, *PLX_ARM64_EXPOSURE;

static
VOID
LxArm64ExposureThisCpu(
    _Inout_ PLX_ARM64_EXPOSURE Exposure)
{
    ULONG64 Midr = ARM64_READ_SYSREG(MIDR_EL1);
    ULONG64 Pfr0 = LxArm64ReadIdRegister(4, 0), Pfr1 = LxArm64ReadIdRegister(4, 1), Mmfr1 = LxArm64ReadIdRegister(7, 1);
    ULONG Csv2 = LX_ID_FIELD(Pfr0, 56);
    LONG State;

    State = ((LX_ID_FIELD(Pfr0, 60) != 0) || LxArm64CoreListed(Midr, LxArm64MeltdownSafe, RTL_NUMBER_OF(LxArm64MeltdownSafe))) ?
            LX_VULN_UNAFFECTED : LX_VULN_VULNERABLE;
    Exposure->Meltdown = max(Exposure->Meltdown, State);
    State = ((Csv2 != 0) || LxArm64CoreListed(Midr, LxArm64SpectreV2Safe, RTL_NUMBER_OF(LxArm64SpectreV2Safe))) ?
            LX_VULN_UNAFFECTED : LX_VULN_VULNERABLE;
    Exposure->SpectreV2 = max(Exposure->SpectreV2, State);
    if ((Csv2 >= 3) || LxArm64CoreListed(Midr, LxArm64BhbSafe, RTL_NUMBER_OF(LxArm64BhbSafe)))
        State = LX_VULN_UNAFFECTED;
    else if (LX_ID_FIELD(Mmfr1, 60) != 0)
        State = LX_VULN_MITIGATED;
    else
        State = LX_VULN_VULNERABLE;
    Exposure->Bhb = max(Exposure->Bhb, State);
    if (LxArm64CoreListed(Midr, LxArm64SsbSafe, RTL_NUMBER_OF(LxArm64SsbSafe)))
        State = LX_VULN_UNAFFECTED;
    else if ((LX_ID_FIELD(Pfr1, 4) != 0) && !(ARM64_READ_SYSREG(SCTLR_EL1) & LX_A64_SCTLR_DSSBS))
        State = LX_VULN_MITIGATED;
    else
        State = LX_VULN_VULNERABLE;
    Exposure->Ssb = max(Exposure->Ssb, State);
}

static
const LX_ARM64_EXPOSURE *
LxArm64QueryExposure(VOID)
{
    static LX_ARM64_EXPOSURE Cached;
    static volatile LONG Ready;
    LX_ARM64_EXPOSURE Exposure;
    GROUP_AFFINITY Affinity, Previous;
    PROCESSOR_NUMBER Number;
    ULONG Count, Index;

    if (ReadAcquire(&Ready))
        return &Cached;
    RtlZeroMemory(&Exposure, sizeof(Exposure));
    Count = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    for (Index = 0; Index < Count; Index++)
    {
        if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(Index, &Number)))
            continue;
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Group = Number.Group;
        Affinity.Mask = (KAFFINITY)1 << Number.Number;
        KeSetSystemGroupAffinityThread(&Affinity, &Previous);
        LxArm64ExposureThisCpu(&Exposure);
        KeRevertToUserGroupAffinityThread(&Previous);
    }
    Cached = Exposure;
    InterlockedExchange(&Ready, 1);
    return &Cached;
}

/* TODO: the NT kernel has no kernel unmapping at EL0, no branch predictor hardening, no Spectre-BHB clearing and no user pointer sanitization, so affected cores read Vulnerable for Meltdown, Spectre v2 and BHB and every core reads Vulnerable for Spectre v1. */
/* FIXME: cores without CSV2 or SSBS read Vulnerable without asking firmware (SMCCC_ARCH_WORKAROUND_1/2) whether they need the workaround. */
PCSTR
LxArchCpuVulnerability(
    _In_ PCSTR Name)
{
    const LX_ARM64_EXPOSURE *Exposure;

    if (strcmp(Name, "spectre_v1") == 0)
        return "Vulnerable";
    if ((strcmp(Name, "meltdown") != 0) && (strcmp(Name, "spectre_v2") != 0) && (strcmp(Name, "spec_store_bypass") != 0))
        return "Not affected";
    Exposure = LxArm64QueryExposure();
    if (strcmp(Name, "meltdown") == 0)
        return (Exposure->Meltdown == LX_VULN_UNAFFECTED) ? "Not affected" : "Vulnerable";
    if (strcmp(Name, "spec_store_bypass") == 0)
    {
        if (Exposure->Ssb == LX_VULN_UNAFFECTED)
            return "Not affected";
        if (Exposure->Ssb == LX_VULN_MITIGATED)
            return "Mitigation: Speculative Store Bypass disabled via prctl";
        return "Vulnerable";
    }
    if (Exposure->SpectreV2 != LX_VULN_UNAFFECTED)
        return "Vulnerable";
    if (Exposure->Bhb == LX_VULN_UNAFFECTED)
        return "Not affected";
    if (Exposure->Bhb == LX_VULN_MITIGATED)
        return "Mitigation: CSV2, BHB";
    return "Mitigation: CSV2, but not BHB";
}

VOID
LxArchApplySpeculation(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    if (LxArchSpecStoreBypassMode() != LX_SPEC_MODE_DYNAMIC)
        return;
    if (Thread->SpecSsb & LX_SPEC_SSB_DISABLE)
        TrapFrame->Spsr &= ~0x1000ULL;
    else
        TrapFrame->Spsr |= 0x1000ULL;
}

#define LX_A64_PSTATE_RES0 (0xFFFFFFFF00000000ULL | 0x0C000000ULL | 0x00C00000ULL | 0x001FE000ULL | 0x20ULL)
#define LX_A64_PSTATE_MODE  0x1FULL
#define LX_A64_PSTATE_DAIF  0x3C0ULL

#define LX_A64_HW_TYPE_EXECUTE  0
#define LX_A64_HW_TYPE_RW       3
#define LX_A64_HW_LEN_4         0x0F
#define LX_A64_HW_PRIVILEGE_EL0 (2UL << 1)

/* FIXME: the NT CONTEXT holds 8 breakpoints and 2 watchpoints, so slots the hardware has beyond those are not offered. */
static
ULONG
LxArchHwSlots(
    _In_ BOOLEAN Watch)
{
    ULONG64 Dfr0 = ARM64_READ_SYSREG(S3_0_C0_C5_0);
    ULONG Slots = (ULONG)((Dfr0 >> (Watch ? 20 : 12)) & 0xF) + 1;

    return min(Slots, Watch ? ARM64_MAX_WATCHPOINTS : ARM64_MAX_BREAKPOINTS);
}

static
ULONG
LxArchHwLength(
    _In_ ULONG Mask)
{
    ULONG Length = 0;

    while (Mask & 1)
    {
        Length++;
        Mask >>= 1;
    }
    return (Mask == 0) ? Length : 0;
}

static
LONG
LxArchHwEncode(
    _In_ BOOLEAN Watch,
    _In_ ULONG Current,
    _In_ BOOLEAN HaveControl,
    _In_ ULONG Control,
    _Inout_ PULONG64 Address,
    _Out_ PULONG Result)
{
    ULONG Type, Length, Mask, Offset, Enabled;
    ULONG64 Aligned;

    if (Current != 0)
    {
        Type = (Current >> 3) & 3;
        Mask = (Current >> 5) & 0xFF;
        while ((Mask != 0) && !(Mask & 1))
            Mask >>= 1;
        Length = LxArchHwLength(Mask);
        Enabled = Current & 1;
    }
    else
    {
        Type = Watch ? LX_A64_HW_TYPE_RW : LX_A64_HW_TYPE_EXECUTE;
        Length = 4;
        Enabled = 0;
    }
    if (HaveControl)
    {
        Enabled = Control & 1;
        if (Enabled)
        {
            Type = (Control >> 3) & 3;
            Mask = (Control >> 5) & 0xFF;
            if (Mask == 0)
                return -LX_EINVAL;
            for (Offset = 0; !(Mask & 1); Offset++)
                Mask >>= 1;
            Length = LxArchHwLength(Mask);
            if ((Length == 0) || (Watch ? (Type == LX_A64_HW_TYPE_EXECUTE) : (Type != LX_A64_HW_TYPE_EXECUTE)))
                return -LX_EINVAL;
            *Address += Offset;
        }
    }
    if (*Address > (ULONG64)(ULONG_PTR)MM_HIGHEST_USER_ADDRESS)
        return -LX_EINVAL;
    if (Type == LX_A64_HW_TYPE_EXECUTE)
        Length = 4;
    Offset = (ULONG)(*Address & ((Type == LX_A64_HW_TYPE_EXECUTE) ? 3 : 7));
    Aligned = *Address - Offset;
    *Address = Aligned;
    *Result = (((((1UL << Length) - 1) << Offset) & 0xFF) << 5) | (Type << 3) | LX_A64_HW_PRIVILEGE_EL0 | Enabled;
    return 0;
}

static
LONG
LxArchSetHwDebug(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Watch,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Count = LxArchHwSlots(Watch), Slot, Offset, Control, Encoded;
    ULONG64 Address;
    LONG Error;

    for (Slot = 0, Offset = 8; Offset < Length; Slot++, Offset += 16)
    {
        if (Length - Offset < sizeof(ULONG64))
            return -LX_EINVAL;
        if (Slot >= Count)
            return -LX_ENOSPC;
        RtlCopyMemory(&Address, Buffer + Offset, sizeof(Address));
        Control = 0;
        if (Length - Offset >= sizeof(ULONG64) + sizeof(ULONG))
            RtlCopyMemory(&Control, Buffer + Offset + sizeof(ULONG64), sizeof(Control));
        Error = LxArchHwEncode(Watch, Watch ? Context->Wcr[Slot] : Context->Bcr[Slot],
                               Length - Offset >= sizeof(ULONG64) + sizeof(ULONG), Control, &Address, &Encoded);
        if (Error != 0)
            return Error;
        if (Watch)
        {
            Context->Wvr[Slot] = Address;
            Context->Wcr[Slot] = Encoded;
        }
        else
        {
            Context->Bvr[Slot] = Address;
            Context->Bcr[Slot] = Encoded;
        }
    }
    return 0;
}

/* FIXME: a single step that ends on an armed breakpoint address is reported as the breakpoint hit. */
BOOLEAN
LxArchHardwareBreakpointHit(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PULONG64 Address)
{
    ULONG Slot;

    if (ExceptionRecord->ExceptionCode != STATUS_SINGLE_STEP)
        return FALSE;
    if (ExceptionRecord->NumberParameters >= 2)
    {
        *Address = (ULONG64)ExceptionRecord->ExceptionInformation[0];
        return TRUE;
    }
    if (!TrapFrame->DebugRegistersValid)
        return FALSE;
    for (Slot = 0; Slot < ARM64_MAX_BREAKPOINTS; Slot++)
    {
        if ((TrapFrame->Bcr[Slot] & 1) && (TrapFrame->Bvr[Slot] == (TrapFrame->Pc & ~3ULL)))
        {
            *Address = TrapFrame->Pc;
            return TRUE;
        }
    }
    return FALSE;
}

LONG
LxArchPtraceGetRegset(
    _In_ PCONTEXT Context,
    _In_ ULONG64 Tls,
    _In_ LONG SyscallNr,
    _In_ ULONG Type,
    _Out_writes_bytes_(LX_PTRACE_REGSET_MAX) PUCHAR Buffer,
    _Out_ PULONG Size,
    _Out_ PULONG Unit)
{
    PULONG64 Registers = (PULONG64)Buffer;
    PULONG Words = (PULONG)Buffer;
    ULONG Index;

    RtlZeroMemory(Buffer, LX_PTRACE_REGSET_MAX);
    switch (Type)
    {
        case LX_NT_PRSTATUS:
            for (Index = 0; Index < 29; Index++)
                Registers[Index] = Context->X[Index];
            Registers[29] = Context->Fp;
            Registers[30] = Context->Lr;
            Registers[31] = Context->Sp;
            Registers[32] = Context->Pc;
            Registers[33] = Context->Cpsr & ~(LX_A64_PSTATE_RES0 | LX_A64_PSTATE_MODE | LX_A64_PSTATE_DAIF);
            *Size = 34 * sizeof(ULONG64);
            *Unit = sizeof(ULONG64);
            return 0;

        case LX_NT_PRFPREG:
            RtlCopyMemory(Buffer, Context->V, sizeof(Context->V));
            Words[128] = Context->Fpsr;
            Words[129] = Context->Fpcr;
            *Size = 132 * sizeof(ULONG);
            *Unit = sizeof(ULONG);
            return 0;

        case LX_NT_ARM_TLS:
            Registers[0] = Tls;
            *Size = 2 * sizeof(ULONG64);
            *Unit = sizeof(ULONG64);
            return 0;

        case LX_NT_ARM_HW_BREAK:
        case LX_NT_ARM_HW_WATCH:
        {
            BOOLEAN Watch = (Type == LX_NT_ARM_HW_WATCH);
            ULONG Count = LxArchHwSlots(Watch);

            RtlZeroMemory(Buffer, 66 * sizeof(ULONG));
            Words[0] = ((ULONG)(ARM64_READ_SYSREG(S3_0_C0_C5_0) & 0xF) << 8) | Count;
            for (Index = 0; Index < Count; Index++)
            {
                Registers[1 + 2 * Index] = Watch ? Context->Wvr[Index] : Context->Bvr[Index];
                Words[4 + 4 * Index] = Watch ? Context->Wcr[Index] : Context->Bcr[Index];
            }
            *Size = 66 * sizeof(ULONG);
            *Unit = sizeof(ULONG);
            return 0;
        }

        case LX_NT_ARM_SYSTEM_CALL:
            Words[0] = (ULONG)SyscallNr;
            *Size = sizeof(LONG);
            *Unit = sizeof(LONG);
            return 0;

        default:
            return -LX_EINVAL;
    }
}

LONG
LxArchPtraceSetRegset(
    _Inout_ PCONTEXT Context,
    _Inout_ PULONG64 Tls,
    _Inout_ PLONG SyscallNr,
    _In_ ULONG Type,
    _In_reads_bytes_(LX_PTRACE_REGSET_MAX) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PULONG64 Registers = (PULONG64)Buffer;
    PULONG Words = (PULONG)Buffer;
    ULONG64 Pstate;
    ULONG Index;

    switch (Type)
    {
        case LX_NT_PRSTATUS:
            Pstate = Registers[33] & ~LX_A64_PSTATE_RES0;
            if ((Pstate & (LX_A64_PSTATE_MODE | LX_A64_PSTATE_DAIF)) != 0)
                return -LX_EINVAL;
            for (Index = 0; Index < 29; Index++)
                Context->X[Index] = Registers[Index];
            Context->Fp = Registers[29];
            Context->Lr = Registers[30];
            Context->Sp = Registers[31];
            Context->Pc = Registers[32];
            Context->Cpsr = (ULONG)((Context->Cpsr & (LX_A64_PSTATE_RES0 | LX_A64_PSTATE_MODE | LX_A64_PSTATE_DAIF)) |
                                    Pstate);
            return 0;

        case LX_NT_PRFPREG:
            RtlCopyMemory(Context->V, Buffer, sizeof(Context->V));
            Context->Fpsr = Words[128];
            Context->Fpcr = Words[129];
            return 0;

        case LX_NT_ARM_TLS:
            *Tls = Registers[0];
            return 0;

        case LX_NT_ARM_HW_BREAK:
        case LX_NT_ARM_HW_WATCH:
            return LxArchSetHwDebug(Context, Type == LX_NT_ARM_HW_WATCH, Buffer, Length);

        case LX_NT_ARM_SYSTEM_CALL:
            *SyscallNr = (LONG)Words[0];
            return 0;

        default:
            return -LX_EINVAL;
    }
}

BOOLEAN
LxArchPtraceSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    if (Enable)
        Context->Cpsr |= 0x200000;
    else
        Context->Cpsr &= ~0x200000;
    return TRUE;
}

BOOLEAN
LxArchPersonalityAllowed(
    _In_ ULONG Personality)
{
    return (Personality & 0xFF) != LX_PER_LINUX32;
}

ULONG
LxArchExecPersonality(
    _In_ ULONG Personality)
{
    return Personality & ~LX_PER_READ_IMPLIES_EXEC;
}

VOID
LxArchMmapRndBits(
    _Out_ PULONG Minimum,
    _Out_ PULONG Maximum)
{
    *Minimum = 18;
    *Maximum = 30;
}

ULONG64
LxArchStackRndMask(VOID)
{
    return 0x3FFFF;
}

LONG
LxArchPtraceSyscallNumber(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ LONG Recorded)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    return Recorded;
}

LONG64
LxArchPtraceSyscallReturn(
    _In_ PCONTEXT Context)
{
    return (LONG64)Context->X0;
}

VOID
LxArchClassifyFault(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _Inout_ PLONG Signal,
    _Inout_ PLONG Code)
{
    ULONG Instruction;

    if ((ExceptionRecord->ExceptionCode != STATUS_ILLEGAL_INSTRUCTION) &&
        (ExceptionRecord->ExceptionCode != STATUS_BREAKPOINT) &&
        (ExceptionRecord->ExceptionCode != STATUS_ASSERTION_FAILURE) &&
        (ExceptionRecord->ExceptionCode != STATUS_INTEGER_DIVIDE_BY_ZERO) &&
        (ExceptionRecord->ExceptionCode != STATUS_STACK_BUFFER_OVERRUN))
    {
        return;
    }
    if (LxCopyFromUser(&Instruction, TrapFrame->Pc, sizeof(Instruction)) != 0)
        return;
    if ((Instruction & 0xFFE0001F) == 0xD4200000)
    {
        *Signal = LX_SIGTRAP;
        *Code = LX_TRAP_BRKPT;
    }
}

VOID
LxArchRestartSyscall(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Argument)
{
    TrapFrame->X0 = Argument;
    TrapFrame->Pc -= 4;
}

VOID
LxArchSetExecContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Pc,
    _In_ ULONG64 Sp)
{
    RtlZeroMemory(TrapFrame->X, sizeof(TrapFrame->X));
    TrapFrame->Fp = 0;
    TrapFrame->Lr = 0;
    TrapFrame->Sp = Sp;
    TrapFrame->Pc = Pc;
    TrapFrame->Spsr = 0;

    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->X19 = ExceptionFrame->X20 = ExceptionFrame->X21 = 0;
        ExceptionFrame->X22 = ExceptionFrame->X23 = ExceptionFrame->X24 = 0;
        ExceptionFrame->X25 = ExceptionFrame->X26 = ExceptionFrame->X27 = 0;
        ExceptionFrame->X28 = 0;
        ExceptionFrame->Fp = 0;
        ExceptionFrame->Lr = 0;
        ExceptionFrame->Fpcr = 0;
        ExceptionFrame->Fpsr = 0;
    }

    if (TrapFrame->VfpState != NULL)
    {
        RtlZeroMemory(TrapFrame->VfpState->V, sizeof(TrapFrame->VfpState->V));
        TrapFrame->VfpState->Fpcr = 0;
        TrapFrame->VfpState->Fpsr = 0;
    }
    TrapFrame->DebugRegistersValid = FALSE;
    RtlZeroMemory(TrapFrame->Bcr, sizeof(TrapFrame->Bcr));
    RtlZeroMemory(TrapFrame->Bvr, sizeof(TrapFrame->Bvr));
    RtlZeroMemory(TrapFrame->Wcr, sizeof(TrapFrame->Wcr));
    RtlZeroMemory(TrapFrame->Wvr, sizeof(TrapFrame->Wvr));
}

VOID
LxArchCaptureContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PCONTEXT Context)
{
    ULONG Index;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
    Context->Cpsr = TrapFrame->Spsr;
    for (Index = 0; Index <= 18; Index++)
        Context->X[Index] = TrapFrame->X[Index];

    if (ExceptionFrame != NULL)
    {
        Context->X19 = ExceptionFrame->X19;
        Context->X20 = ExceptionFrame->X20;
        Context->X21 = ExceptionFrame->X21;
        Context->X22 = ExceptionFrame->X22;
        Context->X23 = ExceptionFrame->X23;
        Context->X24 = ExceptionFrame->X24;
        Context->X25 = ExceptionFrame->X25;
        Context->X26 = ExceptionFrame->X26;
        Context->X27 = ExceptionFrame->X27;
        Context->X28 = ExceptionFrame->X28;
        Context->Fp = ExceptionFrame->Fp;
        Context->Lr = ExceptionFrame->Lr;
    }
    else
    {
        Context->Fp = TrapFrame->Fp;
        Context->Lr = TrapFrame->Lr;
    }

    Context->Sp = TrapFrame->Sp;
    Context->Pc = TrapFrame->Pc;

    if (TrapFrame->VfpState != NULL)
    {
        Context->Fpcr = TrapFrame->VfpState->Fpcr;
        Context->Fpsr = TrapFrame->VfpState->Fpsr;
        RtlCopyMemory(Context->V, TrapFrame->VfpState->V, sizeof(Context->V));
    }

    if (TrapFrame->DebugRegistersValid)
    {
        for (Index = 0; Index < ARM64_MAX_BREAKPOINTS; Index++)
        {
            Context->Bcr[Index] = TrapFrame->Bcr[Index];
            Context->Bvr[Index] = TrapFrame->Bvr[Index];
        }
        for (Index = 0; Index < ARM64_MAX_WATCHPOINTS; Index++)
        {
            Context->Wcr[Index] = TrapFrame->Wcr[Index];
            Context->Wvr[Index] = TrapFrame->Wvr[Index];
        }
    }
    Context->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
}

ULONG
LxArchCoreRegisters(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_writes_(LX_CORE_MAX_REGISTERS) PULONG64 Registers)
{
    CONTEXT Context;
    ULONG Index;

    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);
    for (Index = 0; Index < 29; Index++)
        Registers[Index] = Context.X[Index];
    Registers[29] = Context.Fp;
    Registers[30] = Context.Lr;
    Registers[31] = Context.Sp;
    Registers[32] = Context.Pc;
    Registers[33] = Context.Cpsr;
    return 34;
}

VOID
LxArchSetForkContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG64 StackPointer)
{
    Context->X0 = 0;
    if (StackPointer != 0)
        Context->Sp = StackPointer;
}

VOID
LxArchSetThreadPointer(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Value)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    ARM64_WRITE_SYSREG(TPIDR_EL0, Value);
}

ULONG64
LxArchGetThreadPointer(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Value;

    UNREFERENCED_PARAMETER(TrapFrame);

    Value = ARM64_READ_SYSREG(TPIDR_EL0);
    return Value;
}

ULONG64
LxArchGetUserPc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Pc;
}

ULONG64
LxArchContextPc(
    _In_ PCONTEXT Context)
{
    return Context->Pc;
}

ULONG64
LxArchContextSp(
    _In_ PCONTEXT Context)
{
    return Context->Sp;
}

ULONG64
LxArchGetUserSp(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Sp;
}

ULONG
LxArchSigTrampoline(
    _Out_writes_bytes_(16) PUCHAR Code)
{
    static const ULONG Instructions[2] = { 0xD2801168, 0xD4000001 };

    RtlCopyMemory(Code, Instructions, sizeof(Instructions));
    return sizeof(Instructions);
}

static
VOID
LxArm64SetFpLr(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Fp,
    _In_ ULONG64 Lr)
{
    TrapFrame->Fp = Fp;
    TrapFrame->Lr = Lr;
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->Fp = Fp;
        ExceptionFrame->Lr = Lr;
    }
}

VOID
LxArchRestoreContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PCONTEXT Context)
{
    ULONG Index;

    for (Index = 0; Index <= 18; Index++)
        TrapFrame->X[Index] = Context->X[Index];
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->X19 = Context->X19;
        ExceptionFrame->X20 = Context->X20;
        ExceptionFrame->X21 = Context->X21;
        ExceptionFrame->X22 = Context->X22;
        ExceptionFrame->X23 = Context->X23;
        ExceptionFrame->X24 = Context->X24;
        ExceptionFrame->X25 = Context->X25;
        ExceptionFrame->X26 = Context->X26;
        ExceptionFrame->X27 = Context->X27;
        ExceptionFrame->X28 = Context->X28;
    }
    LxArm64SetFpLr(TrapFrame, ExceptionFrame, Context->Fp, Context->Lr);
    TrapFrame->Sp = Context->Sp;
    TrapFrame->Pc = Context->Pc;
    TrapFrame->Spsr = Context->Cpsr;
    if (TrapFrame->VfpState != NULL)
    {
        TrapFrame->VfpState->Fpcr = Context->Fpcr;
        TrapFrame->VfpState->Fpsr = Context->Fpsr;
        RtlCopyMemory(TrapFrame->VfpState->V, Context->V, sizeof(Context->V));
    }
    if ((Context->ContextFlags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
    {
        BOOLEAN Active = FALSE;

        for (Index = 0; Index < ARM64_MAX_BREAKPOINTS; Index++)
        {
            TrapFrame->Bcr[Index] = Context->Bcr[Index];
            TrapFrame->Bvr[Index] = Context->Bvr[Index];
            Active |= (Context->Bcr[Index] & 1) != 0;
        }
        for (Index = 0; Index < ARM64_MAX_WATCHPOINTS; Index++)
        {
            TrapFrame->Wcr[Index] = Context->Wcr[Index];
            TrapFrame->Wvr[Index] = Context->Wvr[Index];
            Active |= (Context->Wcr[Index] & 1) != 0;
        }
        TrapFrame->DebugRegistersValid = Active;
    }
}

BOOLEAN
LxArchSetupSignalFrame(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 StackTop,
    _In_ LONG Signal,
    _In_ PLX_SIGINFO Info,
    _In_ PLX_SIGACTION Action,
    _In_ ULONG64 Restorer,
    _In_ ULONG64 SavedMask,
    _In_ PLX_STACK AltStack)
{
    CONTEXT Context;
    PUCHAR Buffer;
    ULONG64 Record[2];
    ULONG64 Next, Frame;
    PULONG Fpsimd;
    LONG64 Error;

    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);

    Next = LX_ALIGN_DOWN(StackTop - sizeof(Record), 16);
    Frame = Next - LX_A64_FRAME_SIZE;

    Buffer = ExAllocatePoolZero(PagedPool, LX_A64_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return FALSE;

    RtlCopyMemory(Buffer, Info, sizeof(*Info));
    ((PLX_SIGINFO)Buffer)->Signo = Signal;
    RtlCopyMemory(Buffer + LX_A64_UC_STACK, AltStack, sizeof(*AltStack));
    *(PULONG64)(Buffer + LX_A64_UC_SIGMASK) = SavedMask;

    if ((Signal == LX_SIGSEGV) || (Signal == LX_SIGBUS) || (Signal == LX_SIGILL) ||
        (Signal == LX_SIGFPE) || (Signal == LX_SIGTRAP))
    {
        *(PULONG64)(Buffer + LX_A64_MC_FAULT) = Info->Address;
    }
    RtlCopyMemory(Buffer + LX_A64_MC_REGS, Context.X, sizeof(Context.X));
    *(PULONG64)(Buffer + LX_A64_MC_SP) = Context.Sp;
    *(PULONG64)(Buffer + LX_A64_MC_PC) = Context.Pc;
    *(PULONG64)(Buffer + LX_A64_MC_PSTATE) = Context.Cpsr;

    Fpsimd = (PULONG)(Buffer + LX_A64_MC_RESERVED);
    Fpsimd[0] = LX_A64_FPSIMD_MAGIC;
    Fpsimd[1] = LX_A64_FPSIMD_SIZE;
    Fpsimd[2] = Context.Fpsr;
    Fpsimd[3] = Context.Fpcr;
    RtlCopyMemory(&Fpsimd[4], Context.V, sizeof(Context.V));

    Record[0] = Context.Fp;
    Record[1] = Context.Lr;

    Error = LxCopyToUser(Frame, Buffer, LX_A64_FRAME_SIZE);
    if (Error == 0)
        Error = LxCopyToUser(Next, Record, sizeof(Record));
    ExFreePoolWithTag(Buffer, LX_TAG);
    if (Error != 0)
        return FALSE;

    TrapFrame->X0 = (ULONG64)Signal;
    TrapFrame->X1 = Frame;
    TrapFrame->X2 = Frame + LX_A64_UC;
    TrapFrame->Sp = Frame;
    TrapFrame->Pc = Action->Handler;
    LxArm64SetFpLr(TrapFrame, ExceptionFrame, Next, Restorer);
    return TRUE;
}

LONG64
LxArchSignalReturn(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PULONG64 Mask)
{
    ULONG64 Frame = TrapFrame->Sp;
    PULONG64 Regs;
    PULONG Fpsimd;
    PUCHAR Buffer;
    LONG64 Error;
    ULONG Index;

    *Mask = 0;
    if (Frame & 15)
        return -LX_EFAULT;

    Buffer = ExAllocatePoolZero(PagedPool, LX_A64_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return -LX_ENOMEM;

    Error = LxCopyFromUser(Buffer, Frame, LX_A64_FRAME_SIZE);
    Fpsimd = (PULONG)(Buffer + LX_A64_MC_RESERVED);
    if ((Error == 0) && ((Fpsimd[0] != LX_A64_FPSIMD_MAGIC) || (Fpsimd[1] != LX_A64_FPSIMD_SIZE)))
        Error = -LX_EINVAL;
    if (Error != 0)
    {
        ExFreePoolWithTag(Buffer, LX_TAG);
        return Error;
    }

    Regs = (PULONG64)(Buffer + LX_A64_MC_REGS);
    for (Index = 0; Index <= 18; Index++)
        TrapFrame->X[Index] = Regs[Index];
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->X19 = Regs[19];
        ExceptionFrame->X20 = Regs[20];
        ExceptionFrame->X21 = Regs[21];
        ExceptionFrame->X22 = Regs[22];
        ExceptionFrame->X23 = Regs[23];
        ExceptionFrame->X24 = Regs[24];
        ExceptionFrame->X25 = Regs[25];
        ExceptionFrame->X26 = Regs[26];
        ExceptionFrame->X27 = Regs[27];
        ExceptionFrame->X28 = Regs[28];
    }
    LxArm64SetFpLr(TrapFrame, ExceptionFrame, Regs[29], Regs[30]);
    TrapFrame->Sp = *(PULONG64)(Buffer + LX_A64_MC_SP);
    TrapFrame->Pc = *(PULONG64)(Buffer + LX_A64_MC_PC);
    TrapFrame->Spsr = (TrapFrame->Spsr & ~LX_A64_PSTATE_NZCV) |
                      ((ULONG)*(PULONG64)(Buffer + LX_A64_MC_PSTATE) & LX_A64_PSTATE_NZCV);

    if (TrapFrame->VfpState != NULL)
    {
        TrapFrame->VfpState->Fpsr = Fpsimd[2];
        TrapFrame->VfpState->Fpcr = Fpsimd[3];
        RtlCopyMemory(TrapFrame->VfpState->V, &Fpsimd[4], sizeof(TrapFrame->VfpState->V));
    }

    *Mask = *(PULONG64)(Buffer + LX_A64_UC_SIGMASK);
    ExFreePoolWithTag(Buffer, LX_TAG);
    return 0;
}

ULONG64
LxArchImageBase(
    _In_ BOOLEAN Interpreter)
{
    return Interpreter ? 0x00007FF000000000ULL : 0x0000555555550000ULL;
}

LONG64
LxArchReadSigaction(
    _In_ ULONG64 Address,
    _Out_ PLX_SIGACTION Action)
{
    return LxCopyFromUser(Action, Address, sizeof(*Action));
}

LONG64
LxArchWriteSigaction(
    _In_ ULONG64 Address,
    _In_ PLX_SIGACTION Action)
{
    return LxCopyToUser(Address, Action, sizeof(*Action));
}

BOOLEAN
LxArchDeliverAsync(
    _In_ PLX_THREAD Thread)
{
    struct
    {
        CONTEXT Context;
        KTRAP_FRAME TrapFrame;
        KEXCEPTION_FRAME ExceptionFrame;
        KARM64_VFP_STATE VfpState;
    } *Frames = LxAlloc(sizeof(*Frames));
    BOOLEAN Delivered = FALSE;

    if (Frames == NULL)
        return FALSE;
    Frames->Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT |
                                   CONTEXT_EXCEPTION_REQUEST;
    if (NT_SUCCESS(PsGetContextThread(PsGetCurrentThread(), &Frames->Context, KernelMode)) &&
        (Frames->Context.ContextFlags & CONTEXT_EXCEPTION_REPORTING) &&
        !(Frames->Context.ContextFlags & (CONTEXT_SERVICE_ACTIVE | CONTEXT_EXCEPTION_ACTIVE)) &&
        ((Frames->Context.Cpsr & LX_A64_PSTATE_MODE) == 0))
    {
        Frames->Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
        Frames->TrapFrame.VfpState = &Frames->VfpState;
        LxArchRestoreContext(&Frames->TrapFrame, &Frames->ExceptionFrame, &Frames->Context);
        LxSignalCheckPending(Thread, &Frames->TrapFrame, &Frames->ExceptionFrame);
        LxArchCaptureContext(&Frames->TrapFrame, &Frames->ExceptionFrame, &Frames->Context);
        PsSetContextThread(PsGetCurrentThread(), &Frames->Context, KernelMode);
        Delivered = TRUE;
    }
    LxFree(Frames);
    return Delivered;
}
