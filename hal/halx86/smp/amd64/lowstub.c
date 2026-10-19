/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 real-mode entry stub shared by AP startup and S3 resume
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <smp.h>
#include <hiberfil.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;

/* Trampoline symbols (hal/halx86/smp/amd64/apentry.S) */
extern UCHAR HalpAPEntry16[];
extern UCHAR HalpAPEntry16End[];
extern UCHAR HalpAPEntryProt32[];
extern UCHAR HalpAPEntryLong64[];
extern UCHAR HalpAPEntryJump32[];
extern UCHAR HalpAPEntryJump64[];
extern UCHAR HalpAPEntryProcState[];
extern UCHAR HalpAPEntryHigh[];
extern UCHAR HalpAPEntryPmlRoot[];
extern UCHAR HalpAPEntryGdtr[];
extern UCHAR HalpAPEntryGdt[];

#define PTE_PRESENT_WRITE   0x003
#define PTE_LARGE_IDENTITY  0x083 /* Present | Write | PageSize, PFN 0 */

#define StubOffset(sym)     ((ULONG_PTR)(sym) - (ULONG_PTR)HalpAPEntry16)
#define StubVa(sym)         ((PUCHAR)HalpLowStub + StubOffset(sym))
#define StubPa(sym)         ((ULONG)(HalpLowStubPhysicalAddress.QuadPart + StubOffset(sym)))
#define StubPagePa(page)    (HalpLowStubPhysicalAddress.QuadPart + (page) * PAGE_SIZE)

/* FUNCTIONS *****************************************************************/

static
ULONG
HalpSetupTemporaryMappings(VOID)
{
    PULONG64 Pml4 = (PULONG64)((PUCHAR)HalpLowStub + 1 * PAGE_SIZE);
    PULONG64 Pdpt = (PULONG64)((PUCHAR)HalpLowStub + 2 * PAGE_SIZE);
    PULONG64 Pd = (PULONG64)((PUCHAR)HalpLowStub + 3 * PAGE_SIZE);

    /* Start from a copy of the running address space (kernel higher half) */
    RtlCopyMemory(Pml4, MiAddressToPxe((PVOID)0), PAGE_SIZE);

    /* The firmware-owned low pages are not guaranteed to arrive zeroed. */
    RtlZeroMemory(Pdpt, PAGE_SIZE);
    RtlZeroMemory(Pd, PAGE_SIZE);

    /* Identity-map the low 2 MB so the real-mode/protected-mode stub runs.
       The stub pages are one physically contiguous block. */
    Pml4[0] = (StubPagePa(2) & ~0xFFFULL) | PTE_PRESENT_WRITE;
    Pdpt[0] = (StubPagePa(3) & ~0xFFFULL) | PTE_PRESENT_WRITE;
    Pd[0] = PTE_LARGE_IDENTITY;

    ASSERT(StubPagePa(1) < 0x100000000ULL);
    return (ULONG)StubPagePa(1);
}

ULONG
HalpPrepareLowStub(
    _In_ PKPROCESSOR_STATE ProcessorState,
    _In_ PVOID HighEntry)
{
    ULONG TemporaryCr3;

    if (HalpLowStub == NULL)
        return 0;

    /* Build the temporary page tables used during the mode transition */
    TemporaryCr3 = HalpSetupTemporaryMappings();
    if (!TemporaryCr3)
        return 0;

    /* Copy the trampoline into low memory */
    RtlCopyMemory(HalpLowStub, HalpAPEntry16, StubOffset(HalpAPEntry16End));

    /* Patch the far-jump targets (selectors are already baked in) */
    *(PULONG)StubVa(HalpAPEntryJump32) = StubPa(HalpAPEntryProt32);
    *(PULONG)StubVa(HalpAPEntryJump64) = StubPa(HalpAPEntryLong64);

    /* Patch the data consumed by the trampoline */
    *(PULONG64)StubVa(HalpAPEntryProcState) = (ULONG64)(ULONG_PTR)ProcessorState;
    *(PULONG64)StubVa(HalpAPEntryHigh) = (ULONG64)(ULONG_PTR)HighEntry;
    *(PULONG)StubVa(HalpAPEntryPmlRoot) = TemporaryCr3;

    /* Trampoline GDTR: 4 entries, base points at the copied GDT */
    *(PUSHORT)StubVa(HalpAPEntryGdtr) = (4 * sizeof(ULONG64)) - 1;
    *(PULONG)(StubVa(HalpAPEntryGdtr) + 2) = StubPa(HalpAPEntryGdt);

    return HalpLowStubPhysicalAddress.LowPart;
}

VOID
HalpQueryLowStubResumeTarget(
    _Out_ PVOID ResumeTarget)
{
    PLNT_HIBER_RESUME_TARGET Target = ResumeTarget;

    Target->StubPhysical = HalpLowStubPhysicalAddress.QuadPart;
    Target->StubPages = HALP_LOW_STUB_SIZE_IN_PAGES;
    Target->Prot32Offset = (ULONG)StubOffset(HalpAPEntryProt32);
    Target->GdtOffset = (ULONG)StubOffset(HalpAPEntryGdt);
    Target->GdtLimit = (4 * sizeof(ULONG64)) - 1;
}
