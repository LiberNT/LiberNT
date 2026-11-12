/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the value each window manager service fails with when the thread cannot become a GUI thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

ULONG_PTR
DoSyscallWithUnalignedStack(
    _In_ ULONG64 SyscallNumber);

#ifdef _M_AMD64

#define SVC_(name, argcount) GuiService##name,
enum
{
#include <w32ksvc64.h>
    GuiServiceCount
};
#undef SVC_

/* The values measured on Windows, by service name */
#define SVC_FAILURE_(name, failure) [GuiService##name] = failure,
static const UCHAR GuiServiceFailure[GuiServiceCount] =
{
#include <w32kfail.h>
};
#undef SVC_FAILURE_

static ULONG g_CallbackCount;

static
LONG
WINAPI
FailCallback(
    struct _EXCEPTION_POINTERS *ExceptionInfo)
{
    g_CallbackCount++;

    /* user32 is not loaded, so the conversion callback faults; return from it */
    NtCallbackReturn(NULL, 0, 0xdeadbeef);
    return EXCEPTION_CONTINUE_SEARCH;
}

START_TEST(GuiServiceFailure)
{
    ULONG Index, Expected, Result;
    PVOID Handler;

    if (GetModuleHandleW(L"user32.dll") != NULL)
    {
        skip("user32.dll is loaded, the thread would become a GUI thread\n");
        return;
    }

    Handler = AddVectoredExceptionHandler(TRUE, FailCallback);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed\n");

    for (Index = 0; Index < GuiServiceCount; Index++)
    {
        switch (GuiServiceFailure[Index])
        {
            case SERVICE_FAILURE_ZERO:
                Expected = 0;
                break;
            case SERVICE_FAILURE_MINUS_ONE:
                Expected = 0xffffffff;
                break;
            default:
                Expected = (ULONG)STATUS_INVALID_SYSTEM_SERVICE;
                break;
        }

        Result = (ULONG)DoSyscallWithUnalignedStack(0x1000 + Index);
        ok(Result == Expected, "service %#lx: got %#lx, expected %#lx\n", 0x1000 + Index, Result, Expected);
    }

    /* A number past the end of the table names no service at all */
    Result = (ULONG)DoSyscallWithUnalignedStack(0x1000 + GuiServiceCount);
    ok(Result == (ULONG)STATUS_INVALID_SYSTEM_SERVICE, "service past the table: got %#lx\n", Result);

    ok(g_CallbackCount != 0, "the conversion callback was never called\n");

    RemoveVectoredExceptionHandler(Handler);
}

#else

START_TEST(GuiServiceFailure)
{
    skip("The failure values were measured on x64 Windows only\n");
}

#endif
