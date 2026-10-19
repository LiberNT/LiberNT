/*
 * PROJECT:     LiberNT FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 hibernation resume transfer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include "../../winldr.h"
#include "../../hiber.h"

#define I386_HIBER_CODE      0x100
#define I386_HIBER_STACK     0x100
#define I386_HIBER_PAGES     0x100000

typedef struct _I386_HIBER_JUMP
{
    ULONG64 Reserved0;
    ULONG64 Operations;
    ULONG64 Reserved1;
    ULONG64 StubBase;
    ULONG64 StubEntry;
    USHORT Reserved[3];
    USHORT StubGdtLimit;
    ULONG64 StubGdtBase;
} I386_HIBER_JUMP, *PI386_HIBER_JUMP;

C_ASSERT(FIELD_OFFSET(I386_HIBER_JUMP, Operations) == 8);
C_ASSERT(FIELD_OFFSET(I386_HIBER_JUMP, StubBase) == 24);
C_ASSERT(FIELD_OFFSET(I386_HIBER_JUMP, StubEntry) == 32);
C_ASSERT(FIELD_OFFSET(I386_HIBER_JUMP, StubGdtLimit) == 46);
C_ASSERT(sizeof(I386_HIBER_JUMP) <= I386_HIBER_CODE);

typedef VOID (__cdecl *PI386_HIBER_TRAMPOLINE)(PI386_HIBER_JUMP Jump);

extern UCHAR OslHiberTrampoline[];
extern UCHAR OslHiberTrampolineEnd[];

static PI386_HIBER_JUMP I386HiberJump;

BOOLEAN
WinLdrArchPrepareResume(
    _In_ PLNT_HIBER_HEADER Header,
    _In_ ULONG64 HighestPage)
{
    SIZE_T CodeSize = (SIZE_T)(OslHiberTrampolineEnd - OslHiberTrampoline);
    PI386_HIBER_JUMP Jump;
    ULONG64 Frame;
    PUCHAR Page;

    if (HighestPage > I386_HIBER_PAGES ||
        Header->HighestPage > I386_HIBER_PAGES ||
        CodeSize > PAGE_SIZE - I386_HIBER_CODE - I386_HIBER_STACK)
    {
        return FALSE;
    }

    if (!WinLdrHiberAllocatePage(&Frame))
        return FALSE;
    Page = (PUCHAR)(ULONG_PTR)(Frame << PAGE_SHIFT);

    RtlZeroMemory(Page, PAGE_SIZE);
    RtlCopyMemory(Page + I386_HIBER_CODE, OslHiberTrampoline, CodeSize);

    Jump = (PI386_HIBER_JUMP)Page;
    Jump->StubBase = Header->LowStubPhysical;
    Jump->StubEntry = Header->LowStubPhysical + Header->LowStubProt32Offset;
    Jump->StubGdtLimit = (USHORT)Header->LowStubGdtLimit;
    Jump->StubGdtBase = Header->LowStubPhysical + Header->LowStubGdtOffset;
    I386HiberJump = Jump;
    return TRUE;
}

DECLSPEC_NORETURN
VOID
WinLdrArchResume(
    _In_ ULONG64 Operations)
{
    I386HiberJump->Operations = Operations;
    ((PI386_HIBER_TRAMPOLINE)((PUCHAR)I386HiberJump + I386_HIBER_CODE))(I386HiberJump);

    for (;;)
        ;
}
