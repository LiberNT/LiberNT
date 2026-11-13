/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 processor identification from CPUCFG
 *
 * There is no device tree and no firmware feature call on this port: every
 * fact below comes from a CPUCFG word, [3A5000] Table 54, or from a CSR the
 * loader already programmed. The record is per port state (one boot
 * processor identifies the machine), not a per-processor structure.
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

KI_LA64_PROCESSOR KiLa64Processor;

static
ULONG
KiLa64ReadDmw(
    _In_ ULONG Index)
{
    ULONG64 Value = 0;

    switch (Index)
    {
        case 0: __asm__ __volatile__("csrrd %0, 0x180" : "=r"(Value)); break;
        case 1: __asm__ __volatile__("csrrd %0, 0x181" : "=r"(Value)); break;
        case 2: __asm__ __volatile__("csrrd %0, 0x182" : "=r"(Value)); break;
        default: __asm__ __volatile__("csrrd %0, 0x183" : "=r"(Value)); break;
    }
    return (ULONG)Value;
}

/* Stable-counter frequency = crystal frequency (CPUCFG4, Hz) scaled by
 * CPUCFG5.[15:0] CC_MUL / CPUCFG5.[31:16] CC_DIV. A zero divisor or a zero
 * crystal means the part does not describe one, which is reported as zero
 * rather than guessed. */
static
ULONG64
KiLa64ComputeCounterFrequency(
    _In_ ULONG CpuCfg4,
    _In_ ULONG CpuCfg5)
{
    ULONG Multiply = CpuCfg5 & 0xFFFF;
    ULONG Divide = (CpuCfg5 >> 16) & 0xFFFF;

    if ((CpuCfg4 == 0) || (Divide == 0))
        return 0;

    return ((ULONG64)CpuCfg4 * Multiply) / Divide;
}

VOID
NTAPI
KiLa64IdentifyProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKI_LA64_PROCESSOR Processor = &KiLa64Processor;
    PLOONGARCH64_LOADER_BLOCK LaBlock = &LoaderBlock->u.LoongArch64;
    ULONG CpuCfg1, CpuCfg2, CpuCfg3, CpuCfg4, CpuCfg5, Index;
    ULONG64 CpuId;

    RtlZeroMemory(Processor, sizeof(*Processor));

    __asm__ __volatile__("csrrd %0, 0x20" : "=r"(CpuId));
    Processor->CpuId = (ULONG)CpuId;

    CpuCfg1 = KiLa64ReadCpucfg(1);
    CpuCfg2 = KiLa64ReadCpucfg(2);
    CpuCfg3 = KiLa64ReadCpucfg(3);
    CpuCfg4 = KiLa64ReadCpucfg(4);
    CpuCfg5 = KiLa64ReadCpucfg(5);

    Processor->ProcessorId = KiLa64ReadCpucfg(0);
    Processor->CpuCfg1 = CpuCfg1;
    Processor->CpuCfg2 = CpuCfg2;
    Processor->CpuCfg3 = CpuCfg3;
    Processor->CpuCfg4 = CpuCfg4;
    Processor->CpuCfg5 = CpuCfg5;
    Processor->CounterFrequency = KiLa64ComputeCounterFrequency(CpuCfg4, CpuCfg5);

    for (Index = 0; Index < RTL_NUMBER_OF(Processor->Dmw); Index++)
        Processor->Dmw[Index] = KiLa64ReadDmw(Index);

    /* CPUCFG1.ARCH == 2 is the only encoding that describes this port's
     * instruction set. The 48-bit split the address space layer assumes
     * (PGD indices 256-511 in the kernel half) needs VALEN == 48, which the
     * handoff contract G-1 records as a boot precondition; a part that
     * reports anything else leaves Valid clear so startup refuses to run. */
    Processor->Valid =
        ((CpuCfg1 & LA64_CPUCFG1_ARCH_MASK) == LA64_CPUCFG1_ARCH_LA64) &&
        (((CpuCfg1 >> LA64_CPUCFG1_VALEN_SHIFT) & LA64_CPUCFG1_VALEN_MASK) == 48);

    UNREFERENCED_PARAMETER(LaBlock);
}

ULONG64
NTAPI
KiLa64QueryCounterFrequency(VOID)
{
    return KiLa64Processor.CounterFrequency;
}

/* The one architectural time source is RDTIME.D on timer 0 ([V1-7.6.1]); the
 * HAL reads the same counter for KeQueryPerformanceCounter, so recording the
 * frequency here is what makes the two agree. The part exposes no core-clock
 * counter, so the number published as MHz is the counter frequency: it is the
 * only rate this machine can state about itself. */
VOID
NTAPI
KiLa64SaveProcessorClock(_In_ PKPRCB Prcb)
{
    ULONG64 Frequency = KiLa64QueryCounterFrequency();

    if (Frequency < 1000000ULL)
        return;

    Prcb->MHz = (ULONG)((Frequency + 500000ULL) / 1000000ULL);
}

VOID
NTAPI
KiLa64ReportProcessorFeatures(VOID)
{
    PKI_LA64_PROCESSOR Processor = &KiLa64Processor;

    DPRINT1("[la64] CPU 0x%08lx: cpucfg1 0x%08lx cpucfg2 0x%08lx cpucfg3 0x%08lx\n",
            Processor->CpuId, Processor->CpuCfg1, Processor->CpuCfg2, Processor->CpuCfg3);
    DPRINT1("[la64] crystal %lu Hz, counter %I64u Hz, FP %s LSX %s LASX %s\n",
            Processor->CpuCfg4,
            Processor->CounterFrequency,
            (Processor->CpuCfg2 & LA64_CPUCFG2_FP) ? "yes" : "no",
            (Processor->CpuCfg2 & LA64_CPUCFG2_LSX) ? "yes" : "no",
            (Processor->CpuCfg2 & LA64_CPUCFG2_LASX) ? "yes" : "no");
    DPRINT1("[la64] DMW %08lx %08lx %08lx %08lx, identified %s\n",
            Processor->Dmw[0], Processor->Dmw[1], Processor->Dmw[2], Processor->Dmw[3],
            Processor->Valid ? "yes" : "no (unsupported CPUCFG1)");
}
