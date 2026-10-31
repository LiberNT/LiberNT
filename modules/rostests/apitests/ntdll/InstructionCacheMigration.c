/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a thread's local fence.i stays valid after it moves to another processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define ROUNDS 200
#define WARM_CALLS 64

typedef ULONG (*PCODE_FUNCTION)(VOID);

static
ULONG
EncodeLoadA0(
    _In_ ULONG Value)
{
    return ((Value & 0x7FF) << 20) | (10 << 7) | 0x13;
}

static
BOOLEAN
MoveToProcessor(
    _In_ ULONG Processor)
{
    if (!SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << Processor))
        return FALSE;
    return GetCurrentProcessorNumber() == Processor;
}

START_TEST(InstructionCacheMigration)
{
    DWORD_PTR ProcessMask, SystemMask;
    ULONG First = MAXULONG, Second = MAXULONG, Index, Round, Call, Stale = 0, Moved = 0, Result;
    volatile ULONG *Code;
    PCODE_FUNCTION Function;

    ok(GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask), "GetProcessAffinityMask failed\n");
    for (Index = 0; Index < sizeof(DWORD_PTR) * 8; Index++)
    {
        if (!(ProcessMask & ((DWORD_PTR)1 << Index)))
            continue;
        if (First == MAXULONG)
            First = Index;
        else if (Second == MAXULONG)
            Second = Index;
    }
    if (Second == MAXULONG)
    {
        skip("Needs two processors\n");
        return;
    }

    Code = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    ok(Code != NULL, "VirtualAlloc failed %lu\n", GetLastError());
    if (!Code)
        return;
    Function = (PCODE_FUNCTION)Code;
    Code[0] = EncodeLoadA0(0);
    Code[1] = 0x00008067;
    ok(FlushInstructionCache(GetCurrentProcess(), (PVOID)Code, 8), "FlushInstructionCache failed\n");

    for (Round = 1; Round <= ROUNDS; Round++)
    {
        if (!MoveToProcessor(Second))
            continue;
        for (Call = 0; Call < WARM_CALLS; Call++)
            Function();

        if (!MoveToProcessor(First))
            continue;
        Code[0] = EncodeLoadA0(Round);
        __asm__ __volatile__("fence.i" ::: "memory");

        if (!MoveToProcessor(Second))
            continue;
        Moved++;
        Result = Function();
        if (Result != Round)
            Stale++;
    }

    ok(Moved == ROUNDS, "Moved between processors %lu and %lu in %lu of %u rounds\n", First, Second, Moved, ROUNDS);
    ok(Stale == 0, "%lu of %lu calls ran stale code after moving to another processor\n", Stale, Moved);

    SetThreadAffinityMask(GetCurrentThread(), ProcessMask);
    VirtualFree((PVOID)Code, 0, MEM_RELEASE);
}
