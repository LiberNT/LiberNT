/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 real-mode entry stub setup for S2/S3 resume and processor restart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <hal.h>
#include <smp.h>
#include <hiberfil.h>
#define NDEBUG
#include <debug.h>

extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;

extern UCHAR HalpWakeEntry16[];
extern UCHAR HalpWakeEntry16End[];
extern UCHAR HalpWakeEntryProt32[];
extern UCHAR HalpWakeEntryJump32[];
extern UCHAR HalpWakeEntryProcState[];
extern UCHAR HalpWakeEntryHigh[];
extern UCHAR HalpWakeEntryPageDir[];
extern UCHAR HalpWakeEntryCr4[];
extern UCHAR HalpWakeEntryGdtr[];
extern UCHAR HalpWakeEntryGdt[];

#define StubOffset(sym)     ((ULONG_PTR)(sym) - (ULONG_PTR)HalpWakeEntry16)
#define StubVa(sym)         ((PUCHAR)HalpLowStub + StubOffset(sym))
#define StubPa(sym)         ((ULONG)(HalpLowStubPhysicalAddress.QuadPart + StubOffset(sym)))

static
ULONG
HalpSetupWakeMappings(VOID)
{
    PMMPDE PageDirectory = (PMMPDE)((PUCHAR)HalpLowStub + PAGE_SIZE);
    PMMPTE LowPageTable = (PMMPTE)((PUCHAR)HalpLowStub + 2 * PAGE_SIZE);
    PMMPTE LowStubPte = MiAddressToPte(HalpLowStub);
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG StartPti;
    ULONG Index;

    RtlCopyMemory(PageDirectory, MiAddressToPde(NULL), PAGE_SIZE);
    RtlZeroMemory(LowPageTable, PAGE_SIZE);

    PhysicalAddress = MmGetPhysicalAddress(LowPageTable);
    PageDirectory[0].u.Long = 0;
    PageDirectory[0].u.Hard.PageFrameNumber = (ULONG)(PhysicalAddress.QuadPart >> PAGE_SHIFT);
    PageDirectory[0].u.Hard.Valid = 1;
    PageDirectory[0].u.Hard.Write = 1;

    StartPti = MiAddressToPteOffset(HalpLowStubPhysicalAddress.LowPart);
    ASSERT(StartPti + HALP_LOW_STUB_SIZE_IN_PAGES < PTE_PER_PAGE);
    for (Index = 0; Index < HALP_LOW_STUB_SIZE_IN_PAGES; Index++)
        LowPageTable[StartPti + Index] = LowStubPte[Index];

    PhysicalAddress = MmGetPhysicalAddress(PageDirectory);
    ASSERT(PhysicalAddress.QuadPart < 0x100000000ULL);
    return PhysicalAddress.LowPart;
}

ULONG
HalpPrepareLowStub(
    _In_ PKPROCESSOR_STATE ProcessorState,
    _In_ PVOID HighEntry)
{
    ULONG PageDirectory;

    if (HalpLowStub == NULL)
        return 0;

    PageDirectory = HalpSetupWakeMappings();
    if (!PageDirectory)
        return 0;

    RtlCopyMemory(HalpLowStub, HalpWakeEntry16, StubOffset(HalpWakeEntry16End));

    *(PULONG)StubVa(HalpWakeEntryJump32) = StubPa(HalpWakeEntryProt32);
    *(PULONG)StubVa(HalpWakeEntryProcState) = (ULONG)(ULONG_PTR)ProcessorState;
    *(PULONG)StubVa(HalpWakeEntryHigh) = (ULONG)(ULONG_PTR)HighEntry;
    *(PULONG)StubVa(HalpWakeEntryPageDir) = PageDirectory;
    *(PULONG)StubVa(HalpWakeEntryCr4) = ProcessorState->SpecialRegisters.Cr4 & ~CR4_PGE;

    *(PUSHORT)StubVa(HalpWakeEntryGdtr) = (3 * sizeof(ULONG64)) - 1;
    *(PULONG)(StubVa(HalpWakeEntryGdtr) + 2) = StubPa(HalpWakeEntryGdt);

    return HalpLowStubPhysicalAddress.LowPart;
}

VOID
HalpQueryLowStubResumeTarget(
    _Out_ PVOID ResumeTarget)
{
    PLNT_HIBER_RESUME_TARGET Target = ResumeTarget;

    Target->StubPhysical = HalpLowStubPhysicalAddress.QuadPart;
    Target->StubPages = HALP_LOW_STUB_SIZE_IN_PAGES;
    Target->Prot32Offset = (ULONG)StubOffset(HalpWakeEntryProt32);
    Target->GdtOffset = (ULONG)StubOffset(HalpWakeEntryGdt);
    Target->GdtLimit = (3 * sizeof(ULONG64)) - 1;
}
