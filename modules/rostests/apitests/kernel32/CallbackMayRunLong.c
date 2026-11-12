/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests CallbackMayRunLong through kernel32 and kernelbase
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_CALLBACK_MAY_RUN_LONG)(PTP_CALLBACK_INSTANCE);

typedef struct _MAY_RUN_LONG_CALL
{
    PFN_CALLBACK_MAY_RUN_LONG Function;
    HANDLE Done;
    BOOL Result;
    DWORD ThreadId;
} MAY_RUN_LONG_CALL, *PMAY_RUN_LONG_CALL;

static
VOID
CALLBACK
MayRunLongCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _Inout_opt_ PVOID Context)
{
    PMAY_RUN_LONG_CALL Call = Context;

    Call->ThreadId = GetCurrentThreadId();
    Call->Result = Call->Function(Instance);
    SetEvent(Call->Done);
}

static
BOOL
RunInCallback(
    _In_ PFN_CALLBACK_MAY_RUN_LONG Function,
    _In_opt_ PTP_CALLBACK_ENVIRON Environment,
    _Out_ PBOOL Result)
{
    MAY_RUN_LONG_CALL Call;
    DWORD Wait;

    *Result = 0xdeadbeef;
    Call.Function = Function;
    Call.Result = 0xdeadbeef;
    Call.ThreadId = 0;
    Call.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Call.Done != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (!Call.Done)
        return FALSE;

    if (!TrySubmitThreadpoolCallback(MayRunLongCallback, &Call, Environment))
    {
        ok(FALSE, "TrySubmitThreadpoolCallback failed with %lu\n", GetLastError());
        CloseHandle(Call.Done);
        return FALSE;
    }

    Wait = WaitForSingleObject(Call.Done, 5000);
    ok(Wait == WAIT_OBJECT_0, "The callback did not finish: %lu\n", Wait);
    ok(Call.ThreadId != GetCurrentThreadId(), "The callback ran on the submitting thread\n");
    CloseHandle(Call.Done);
    if (Wait != WAIT_OBJECT_0)
        return FALSE;

    *Result = Call.Result;
    return TRUE;
}

static
void
TestEntryPoint(
    _In_ PFN_CALLBACK_MAY_RUN_LONG Function,
    _In_ PCSTR Name)
{
    TP_CALLBACK_ENVIRON Environment;
    PTP_POOL Pool;
    BOOL Result;

    if (RunInCallback(Function, NULL, &Result))
        ok(Result == TRUE, "%s in the default pool returned %d\n", Name, Result);

    Pool = CreateThreadpool(NULL);
    ok(Pool != NULL, "CreateThreadpool failed with %lu\n", GetLastError());
    if (!Pool)
        return;
    SetThreadpoolThreadMaximum(Pool, 1);

    ZeroMemory(&Environment, sizeof(Environment));
    Environment.Version = 3;
    Environment.Pool = Pool;
    Environment.CallbackPriority = TP_CALLBACK_PRIORITY_NORMAL;
    Environment.Size = sizeof(Environment);

    if (RunInCallback(Function, &Environment, &Result))
        ok(Result == FALSE, "%s in a pool of one thread returned %d\n", Name, Result);

    CloseThreadpool(Pool);
}

START_TEST(CallbackMayRunLong)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    PFN_CALLBACK_MAY_RUN_LONG pKernel32 = NULL, pKernelBase = NULL;

    pKernel32 = (PFN_CALLBACK_MAY_RUN_LONG)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CallbackMayRunLong");
    if (KernelBase)
        pKernelBase = (PFN_CALLBACK_MAY_RUN_LONG)GetProcAddress(KernelBase, "CallbackMayRunLong");

    if (!pKernel32)
        skip("kernel32!CallbackMayRunLong is missing\n");
    if (!pKernelBase)
        skip("kernelbase!CallbackMayRunLong is missing\n");

    if (pKernel32)
        TestEntryPoint(pKernel32, "kernel32!CallbackMayRunLong");
    if (pKernelBase)
        TestEntryPoint(pKernelBase, "kernelbase!CallbackMayRunLong");
}
