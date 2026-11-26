/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A user thread terminated while it waits in kernel mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <kmt_test.h>
#include "PsTerminateWait.h"

static DWORD WINAPI
TermWaitThread(
    _In_ PVOID Parameter)
{
    return KmtSendToDriver((DWORD)(ULONG_PTR)Parameter);
}

static VOID
TermWaitCase(
    _In_ DWORD ControlCode,
    _In_ PCSTR Label)
{
    LONG Result[2] = { 0, 0 };
    DWORD Length = sizeof(Result);
    DWORD Start, Elapsed, Wait;
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, TermWaitThread, (PVOID)(ULONG_PTR)ControlCode, 0, NULL);
    ok(Thread != NULL, "%s: CreateThread failed: %lu\n", Label, GetLastError());
    if (Thread == NULL)
        return;
    Sleep(500);
    ok(WaitForSingleObject(Thread, 0) == WAIT_TIMEOUT, "%s: the thread is not waiting\n", Label);
    Start = GetTickCount();
    ok(TerminateThread(Thread, 7), "%s: TerminateThread failed: %lu\n", Label, GetLastError());
    Wait = WaitForSingleObject(Thread, 10000);
    Elapsed = GetTickCount() - Start;
    trace("%s: thread ended %lu ms after TerminateThread (wait %lu)\n", Label, Elapsed, Wait);
    CloseHandle(Thread);
    ok(KmtSendBufferToDriver(IOCTL_TERMWAIT_RESULT, Result, 0, &Length) == ERROR_SUCCESS, "%s: no result\n", Label);
    trace("%s: kernel wait returned 0x%08lx (waited %ld)\n", Label, Result[0], Result[1]);
    ok(Result[1] == 1, "%s: the thread did not reach the wait\n", Label);
    if (ControlCode == IOCTL_TERMWAIT_WAIT)
    {
        ok(Wait == WAIT_OBJECT_0 && Elapsed < 2000, "%s: the thread ended after %lu ms (wait %lu)\n", Label, Elapsed, Wait);
        ok(Result[0] == STATUS_ALERTED, "%s: kernel wait returned 0x%08lx\n", Label, Result[0]);
    }
    else
    {
        ok(Result[0] == STATUS_TIMEOUT, "%s: kernel wait returned 0x%08lx\n", Label, Result[0]);
    }
}

START_TEST(PsTerminateWait)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"PsTerminateWait", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;
    TermWaitCase(IOCTL_TERMWAIT_WAIT, "alertable");
    TermWaitCase(IOCTL_TERMWAIT_NONALERTABLE, "not alertable");
    KmtCloseDriver();
    KmtUnloadDriver();
}
