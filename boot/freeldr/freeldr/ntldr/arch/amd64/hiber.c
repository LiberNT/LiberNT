/*
 * PROJECT:     LiberNT FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 hibernation resume transfer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include "../../winldr.h"
#include "../../hiber.h"

#define AMD64_HIBER_CODE     0x100
#define AMD64_HIBER_STACK    0x100
#define AMD64_PAGES_PER_GB   0x40000
#define AMD64_LARGE_PAGE     0x200000ULL
#define AMD64_PDE_LARGE      0x83
#define AMD64_PTE_TABLE      0x3

typedef struct _AMD64_HIBER_JUMP
{
    ULONG64 Cr3;
    ULONG64 Operations;
    ULONG64 Reserved0;
    ULONG64 StubBase;
    ULONG64 StubEntry;
    USHORT Reserved[3];
    USHORT StubGdtLimit;
    ULONG64 StubGdtBase;
} AMD64_HIBER_JUMP, *PAMD64_HIBER_JUMP;

C_ASSERT(FIELD_OFFSET(AMD64_HIBER_JUMP, Operations) == 8);
C_ASSERT(FIELD_OFFSET(AMD64_HIBER_JUMP, StubBase) == 24);
C_ASSERT(FIELD_OFFSET(AMD64_HIBER_JUMP, StubEntry) == 32);
C_ASSERT(FIELD_OFFSET(AMD64_HIBER_JUMP, StubGdtLimit) == 46);
C_ASSERT(sizeof(AMD64_HIBER_JUMP) <= AMD64_HIBER_CODE);

typedef VOID (*PAMD64_HIBER_TRAMPOLINE)(PAMD64_HIBER_JUMP Jump);

extern UCHAR OslHiberTrampoline[];
extern UCHAR OslHiberTrampolineEnd[];

static PAMD64_HIBER_JUMP Amd64HiberJump;

static
PULONG64
Amd64HiberTablePage(VOID)
{
    ULONG64 Frame;
    PULONG64 Table;

    if (!WinLdrHiberAllocatePage(&Frame))
        return NULL;

    Table = (PULONG64)(ULONG_PTR)(Frame << PAGE_SHIFT);
    RtlZeroMemory(Table, PAGE_SIZE);
    return Table;
}

BOOLEAN
WinLdrArchPrepareResume(
    _In_ PLNT_HIBER_HEADER Header,
    _In_ ULONG64 HighestPage)
{
    ULONG64 Gigabytes = (HighestPage + AMD64_PAGES_PER_GB - 1) / AMD64_PAGES_PER_GB;
    SIZE_T CodeSize = (SIZE_T)(OslHiberTrampolineEnd - OslHiberTrampoline);
    PAMD64_HIBER_JUMP Jump;
    PULONG64 Pml4, Pdpt = NULL, Pd;
    ULONG64 Frame;
    PUCHAR Page;
    ULONG64 i, j;

    if (Gigabytes == 0 || Gigabytes > 512 * 512 || CodeSize > PAGE_SIZE - AMD64_HIBER_CODE - AMD64_HIBER_STACK)
        return FALSE;

    if (!WinLdrHiberAllocatePage(&Frame))
        return FALSE;
    Page = (PUCHAR)(ULONG_PTR)(Frame << PAGE_SHIFT);

    Pml4 = Amd64HiberTablePage();
    if (!Pml4)
        return FALSE;

    for (i = 0; i < Gigabytes; i++)
    {
        if ((i % 512) == 0)
        {
            Pdpt = Amd64HiberTablePage();
            if (!Pdpt)
                return FALSE;
            Pml4[i / 512] = (ULONG_PTR)Pdpt | AMD64_PTE_TABLE;
        }

        Pd = Amd64HiberTablePage();
        if (!Pd)
            return FALSE;
        Pdpt[i % 512] = (ULONG_PTR)Pd | AMD64_PTE_TABLE;
        for (j = 0; j < 512; j++)
            Pd[j] = ((i * 512 + j) * AMD64_LARGE_PAGE) | AMD64_PDE_LARGE;
    }

    RtlZeroMemory(Page, PAGE_SIZE);
    RtlCopyMemory(Page + AMD64_HIBER_CODE, OslHiberTrampoline, CodeSize);

    Jump = (PAMD64_HIBER_JUMP)Page;
    Jump->Cr3 = (ULONG_PTR)Pml4;
    Jump->StubBase = Header->LowStubPhysical;
    Jump->StubEntry = Header->LowStubPhysical + Header->LowStubProt32Offset;
    Jump->StubGdtLimit = (USHORT)Header->LowStubGdtLimit;
    Jump->StubGdtBase = Header->LowStubPhysical + Header->LowStubGdtOffset;
    Amd64HiberJump = Jump;
    return TRUE;
}

DECLSPEC_NORETURN
VOID
WinLdrArchResume(
    _In_ ULONG64 Operations)
{
    Amd64HiberJump->Operations = Operations;
    ((PAMD64_HIBER_TRAMPOLINE)((PUCHAR)Amd64HiberJump + AMD64_HIBER_CODE))(Amd64HiberJump);

    for (;;)
        ;
}
