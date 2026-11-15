/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test what user-mode handlers see for a breakpoint instruction and that they can resume after it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#if defined(_M_IX86)
#define CONTEXT_PC(Context) ((Context)->Eip)
#define BREAKPOINT_SIZE 1
#elif defined(_M_AMD64)
#define CONTEXT_PC(Context) ((Context)->Rip)
#define BREAKPOINT_SIZE 1
#elif defined(_M_ARM64)
#define CONTEXT_PC(Context) ((Context)->Pc)
#define BREAKPOINT_SIZE 4
#endif

#ifdef BREAKPOINT_SIZE

static EXCEPTION_RECORD SeenRecord;
static ULONG_PTR SeenPc;
static volatile LONG SeenCount;

static
LONG
CALLBACK
BreakpointHandler(
    _In_ PEXCEPTION_POINTERS Info)
{
    if (Info->ExceptionRecord->ExceptionCode != STATUS_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;

    SeenRecord = *Info->ExceptionRecord;
    SeenPc = CONTEXT_PC(Info->ContextRecord);
    InterlockedIncrement(&SeenCount);

    CONTEXT_PC(Info->ContextRecord) += BREAKPOINT_SIZE;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static
BOOLEAN
IsBreakpointInstruction(
    _In_ PVOID Address)
{
#if defined(_M_ARM64)
    return (*(volatile ULONG *)Address == 0xD43E0000);
#else
    return (*(volatile UCHAR *)Address == 0xCC);
#endif
}

START_TEST(Breakpoint)
{
    PVOID Handler;

    Handler = AddVectoredExceptionHandler(1, BreakpointHandler);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed with %lu\n", GetLastError());
    if (Handler == NULL)
        return;

    __debugbreak();
    RemoveVectoredExceptionHandler(Handler);

    ok_long(SeenCount, 1);
    ok_hex(SeenRecord.ExceptionCode, STATUS_BREAKPOINT);
    ok_hex(SeenRecord.ExceptionFlags, 0);
    ok_size_t(SeenPc, (ULONG_PTR)SeenRecord.ExceptionAddress);
    ok(IsBreakpointInstruction(SeenRecord.ExceptionAddress), "No breakpoint instruction at %p\n", SeenRecord.ExceptionAddress);
    ok_dec(SeenRecord.NumberParameters, 1);
    ok_size_t(SeenRecord.ExceptionInformation[0], 0);
}

#else

START_TEST(Breakpoint)
{
    skip("No breakpoint layout for this architecture\n");
}

#endif
