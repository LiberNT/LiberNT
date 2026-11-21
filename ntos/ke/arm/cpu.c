/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 processor identification, caches and TLB maintenance
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern UCHAR KiArmVectorTable[];

ULONG KiArmDcacheLineSize = 64;
ULONG KiArmIcacheLineSize = 64;

VOID
NTAPI
KiArmInitializeVectors(VOID)
{
    KiArmWriteCp15(12, 0, 0, 0, (ULONG_PTR)KiArmVectorTable & ~(ULONG_PTR)1);
    KiArmIsb();
}

VOID
NTAPI
KiArmIdentifyProcessor(
    _Inout_ PKPRCB Prcb)
{
    ULONG Midr = KiArmReadCp15(0, 0, 0, 0);
    ULONG Ctr = KiArmReadCp15(0, 0, 0, 1);
    ULONG Isar0 = KiArmReadCp15(0, 0, 2, 0);
    ULONG Features = 0;

    /* MIDR (ARM ARM B4.1.105): implementer, variant, part number, revision. */
    Prcb->CpuVendor = (UCHAR)(Midr >> 24);
    Prcb->ProcessorModel = (USHORT)((Midr >> 4) & 0xFFF);
    Prcb->ProcessorRevision = (USHORT)(((Midr >> 20) & 0xF) << 8 | (Midr & 0xF));

    /* CTR DminLine and IminLine are log2 of the line length in words. */
    KiArmDcacheLineSize = 4 << ((Ctr >> 16) & 0xF);
    KiArmIcacheLineSize = 4 << (Ctr & 0xF);

    /* ID_ISAR0 bits 27:24: hardware divide in the Thumb instruction set. */
    if ((Isar0 >> 24) & 0xF)
        Features |= KI_ARM_FEATURE_DIVIDE;
    Prcb->FeatureBits = Features;
    Prcb->MaxBreakpoints = 0;
    Prcb->MaxWatchpoints = 0;
    Prcb->MHz = 1000;
    Prcb->CycleCounterFrequency = KiArmReadCp15(14, 0, 0, 0);
}

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    return KeGetCurrentPrcb()->Number;
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    KiArmDsbIsh();
    KiArmWriteCp15(8, 0, 3, 0, 0);
    KiArmWriteCp15(7, 0, 5, 6, 0);
    KiArmDsbIsh();
    KiArmIsb();
}

VOID
NTAPI
KeFlushProcessTb(VOID)
{
    KeFlushCurrentTb();
}

static
VOID
KiArmCleanInvalidateDcacheRange(
    _In_ ULONG_PTR Start,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    ULONG_PTR Line = KiArmDcacheLineSize;
    ULONG_PTR Address = Start & ~(Line - 1);
    ULONG_PTR End = Start + Length;

    KiArmDsbIsh();
    for (; Address < End; Address += Line)
    {
        if (Invalidate)
            KiArmWriteCp15(7, 0, 14, 1, Address);
        else
            KiArmWriteCp15(7, 0, 10, 1, Address);
    }
    KiArmDsb();
}

VOID
NTAPI
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    ULONG_PTR Line = KiArmIcacheLineSize;
    ULONG_PTR Address, End;

    if (!BaseAddress)
    {
        KiArmDsbIsh();
        KiArmWriteCp15(7, 0, 1, 0, 0);
        KiArmWriteCp15(7, 0, 5, 6, 0);
        KiArmDsbIsh();
        KiArmIsb();
        return;
    }

    /* Make the new instructions visible to the point of unification first. */
    KiArmCleanInvalidateDcacheRange((ULONG_PTR)BaseAddress, FlushSize, FALSE);
    Address = (ULONG_PTR)BaseAddress & ~(Line - 1);
    End = (ULONG_PTR)BaseAddress + FlushSize;
    for (; Address < End; Address += Line)
        KiArmWriteCp15(7, 0, 5, 1, Address);
    KiArmWriteCp15(7, 0, 5, 6, 0);
    KiArmDsbIsh();
    KiArmIsb();
}

VOID
NTAPI
KeSweepDcache(
    _In_ BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(AllProcessors);
    KiArmDsb();
}

VOID
NTAPI
KiArmFlushDmaRange(
    _In_ PVOID Address,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    KiArmCleanInvalidateDcacheRange((ULONG_PTR)Address, Length, Invalidate);
}

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState)
{
    PKARM_ARCH_STATE Arch = &ProcessorState->ArchState;

    Arch->Cp15_Cr0_CpuId = KiArmReadCp15(0, 0, 0, 0);
    Arch->Cp15_Cr1_Control = KiArmReadCp15(1, 0, 0, 0);
    Arch->Cp15_Cr1_AuxControl = KiArmReadCp15(1, 0, 0, 1);
    Arch->Cp15_Cr1_Cpacr = KiArmReadCp15(1, 0, 0, 2);
    Arch->Cp15_Cr2_TtbControl = KiArmReadCp15(2, 0, 0, 2);
    Arch->Cp15_Cr2_Ttb0 = (ULONG)KiArmReadCp15_64(0, 2);
    Arch->Cp15_Cr2_Ttb1 = (ULONG)KiArmReadCp15_64(1, 2);
    Arch->Cp15_Cr3_Dacr = KiArmReadCp15(3, 0, 0, 0);
    Arch->Cp15_Cr5_Dfsr = KiArmReadCp15(5, 0, 0, 0);
    Arch->Cp15_Cr5_Ifsr = KiArmReadCp15(5, 0, 0, 1);
    Arch->Cp15_Cr6_Dfar = KiArmReadCp15(6, 0, 0, 0);
    Arch->Cp15_Cr6_Ifar = KiArmReadCp15(6, 0, 0, 2);
    Arch->Cp15_Cr10_PrimaryMemoryRemap = KiArmReadCp15(10, 0, 2, 0);
    Arch->Cp15_Cr10_NormalMemoryRemap = KiArmReadCp15(10, 0, 2, 1);
    Arch->Cp15_Cr12_VBARns = KiArmReadCp15(12, 0, 0, 0);
    Arch->Cp15_Cr13_ContextId = KiArmReadCp15(13, 0, 0, 1);
    ProcessorState->SpecialRegisters.Cp15_Cr13_UsrRW = KiArmReadCp15(13, 0, 0, 2);
    ProcessorState->SpecialRegisters.Cp15_Cr13_UsrRO = KiArmReadCp15(13, 0, 0, 3);
    ProcessorState->SpecialRegisters.Cp15_Cr13_SvcRW = KiArmReadCp15(13, 0, 0, 4);
}

VOID
NTAPI
KiRestoreProcessorControlState(
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(ProcessorState);
}
