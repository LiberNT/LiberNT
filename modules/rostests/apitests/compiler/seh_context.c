/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that an SEH filter receives the context of the faulting instruction
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winternl.h>
#include <ntstatus.h>
#include <pseh/pseh2.h>

static CONTEXT g_FaultContext;
static LONG g_VectoredHits;
static volatile LONG *g_FaultPage;
static BOOL g_TopLevelSameContext;
static LONG g_TopLevelHits;

static
LONG
WINAPI
RecordFaultContext(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    if (Pointers->ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION &&
        Pointers->ExceptionRecord->ExceptionInformation[1] == (ULONG_PTR)g_FaultPage)
    {
        g_FaultContext = *Pointers->ContextRecord;
        g_VectoredHits++;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static
LONG
CompareFilterContext(
    _In_ PEXCEPTION_POINTERS Pointers,
    _Out_ PBOOL SameContext,
    _Out_ PVOID *FaultAddress)
{
    *SameContext = !memcmp(&g_FaultContext, Pointers->ContextRecord, sizeof(CONTEXT));
    *FaultAddress = Pointers->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}

static
VOID
FaultInGuardedFrame(
    _Out_ PBOOL SameContext,
    _Out_ PBOOL Handled,
    _Out_ PVOID *FaultAddress)
{
    *Handled = FALSE;
    _SEH2_TRY
    {
        *g_FaultPage = 0;
    }
    _SEH2_EXCEPT(CompareFilterContext(_SEH2_GetExceptionInformation(), SameContext, FaultAddress))
    {
        *Handled = TRUE;
    }
    _SEH2_END;
}

static
LONG
WINAPI
TopLevelFilter(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    DWORD OldProtect;

    if (Pointers->ExceptionRecord->ExceptionCode != STATUS_ACCESS_VIOLATION ||
        Pointers->ExceptionRecord->ExceptionInformation[1] != (ULONG_PTR)g_FaultPage)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    g_TopLevelHits++;
    g_TopLevelSameContext = !memcmp(&g_FaultContext, Pointers->ContextRecord, sizeof(CONTEXT));
    if (!VirtualProtect((PVOID)g_FaultPage, PAGE_SIZE, PAGE_READWRITE, &OldProtect))
        return EXCEPTION_CONTINUE_SEARCH;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static
DWORD
WINAPI
FaultWithoutGuard(
    _In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    *g_FaultPage = 1;
    return 0;
}

static
VOID
TestUnhandledFilterContext(VOID)
{
    LPTOP_LEVEL_EXCEPTION_FILTER OldFilter;
    PVOID Handler;
    HANDLE Thread;
    DWORD ExitCode = 0xFFFFFFFF;

    g_VectoredHits = 0;
    Handler = AddVectoredExceptionHandler(TRUE, RecordFaultContext);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed\n");
    OldFilter = SetUnhandledExceptionFilter(TopLevelFilter);

    Thread = CreateThread(NULL, 0, FaultWithoutGuard, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (Thread != NULL)
    {
        ok_long(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
        ok(GetExitCodeThread(Thread, &ExitCode), "GetExitCodeThread failed, error %lu\n", GetLastError());
        CloseHandle(Thread);
    }

    SetUnhandledExceptionFilter(OldFilter);
    if (Handler != NULL)
        RemoveVectoredExceptionHandler(Handler);

    ok_long(ExitCode, 0);
    ok_long(g_VectoredHits, 1);
    ok_long(g_TopLevelHits, 1);
    ok_long(*g_FaultPage, 1);
    ok(g_TopLevelSameContext, "The unhandled exception filter received a context other than the faulting one\n");
}

START_TEST(seh_context)
{
    PVOID Handler;
    BOOL SameContext = FALSE;
    BOOL Handled = FALSE;
    PVOID FaultAddress = NULL;

    g_FaultPage = VirtualAlloc(NULL, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    ok(g_FaultPage != NULL, "VirtualAlloc failed, error %lu\n", GetLastError());
    if (g_FaultPage == NULL)
        return;

    Handler = AddVectoredExceptionHandler(TRUE, RecordFaultContext);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed\n");
    if (Handler != NULL)
    {
        FaultInGuardedFrame(&SameContext, &Handled, &FaultAddress);
        RemoveVectoredExceptionHandler(Handler);

        ok(Handled, "The __except block did not run\n");
        ok_long(g_VectoredHits, 1);
        ok(FaultAddress != NULL, "The exception address is NULL\n");
        ok(SameContext, "The filter received a context other than the faulting one\n");
    }

    TestUnhandledFilterContext();
    VirtualFree((PVOID)g_FaultPage, 0, MEM_RELEASE);
    g_FaultPage = NULL;
}
