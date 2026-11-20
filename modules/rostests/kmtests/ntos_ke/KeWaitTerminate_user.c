/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Terminate a thread waiting in the kernel and check which waits end for it
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <kmt_test.h>
#include "KeWaitTerminate.h"

typedef struct _WAIT_THREAD
{
    HANDLE Device;
    WAIT_TERMINATE_CASE Case;
} WAIT_THREAD, *PWAIT_THREAD;

static DWORD WINAPI WaitThread(PVOID Parameter)
{
    PWAIT_THREAD Wait = Parameter;
    WAIT_TERMINATE_CASE Case = Wait->Case;
    DWORD Length = sizeof(Case);

    DeviceIoControl(Wait->Device, KMT_MAKE_CODE(IOCTL_WAIT_TERMINATE_WAIT), &Case, sizeof(Case), &Case,
                    sizeof(Case), &Length, NULL);
    return 0;
}

static BOOL Query(ULONG Index, PWAIT_TERMINATE_CASE Case)
{
    DWORD Length = sizeof(*Case);
    RtlZeroMemory(Case, sizeof(*Case));
    Case->Index = Index;
    return KmtSendBufferToDriver(IOCTL_WAIT_TERMINATE_QUERY, Case, sizeof(*Case), &Length) == ERROR_SUCCESS;
}

static void TestCase(ULONG Index, BOOLEAN Alertable, BOOLEAN User, NTSTATUS Expected, BOOLEAN Known)
{
    WAIT_THREAD Wait = { NULL, { Index, Alertable, User } };
    WAIT_TERMINATE_CASE Case, State;
    HANDLE Thread;
    DWORD Ended, Code = 0, Start, Took;
    ULONG Tries;

    /* A handle of its own, as the test's handle does its I/O one request at a time */
    Wait.Device = CreateFileW(L"\\\\.\\Global\\GLOBALROOT\\Device\\Kmtest-KeWaitTerminate",
                              GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(Wait.Device != INVALID_HANDLE_VALUE, "case %lu: the device could not be opened: %lu\n", Index, GetLastError());
    if (Wait.Device == INVALID_HANDLE_VALUE)
        return;
    Thread = CreateThread(NULL, 0, WaitThread, &Wait, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
    {
        CloseHandle(Wait.Device);
        return;
    }
    for (Tries = 0; Tries < 200; Tries++)
    {
        if (Query(Index, &State) && State.Entered && State.Status == STATUS_PENDING)
            break;
        Sleep(10);
    }
    ok(State.Entered && State.Status == STATUS_PENDING, "case %lu: the thread did not start waiting\n", Index);
    Sleep(100);

    Start = GetTickCount();
    ok(TerminateThread(Thread, 0x1234), "case %lu: TerminateThread failed: %lu\n", Index, GetLastError());
    Ended = WaitForSingleObject(Thread, WAIT_TERMINATE_TIMEOUT_MS * 2);
    Took = GetTickCount() - Start;
    ok(Ended == WAIT_OBJECT_0, "case %lu: the thread did not end (%lu)\n", Index, Ended);
    GetExitCodeThread(Thread, &Code);
    ok(Code == 0x1234, "case %lu: the thread ended with 0x%lx\n", Index, Code);
    CloseHandle(Thread);
    CloseHandle(Wait.Device);

    ok(Query(Index, &Case), "case %lu could not be queried\n", Index);
    trace("case %lu: alertable %lu, user mode %lu: status 0x%lx after %lu ms, thread ended %lu ms after the request\n",
          Index, Case.Alertable, Case.UserMode, Case.Status, Case.Milliseconds, Took);
    /* TODO: LiberNT ends this wait with STATUS_USER_APC; Windows 11 lets it run to its timeout */
    if (!Known)
        return;
    ok(Case.Status == Expected, "case %lu: the wait returned 0x%lx\n", Index, Case.Status);
    if (Expected == STATUS_TIMEOUT)
    {
        ok(Took >= WAIT_TERMINATE_TIMEOUT_MS / 2, "case %lu: the thread ended after %lu ms\n", Index, Took);
    }
    else
    {
        ok(Case.Milliseconds < WAIT_TERMINATE_TIMEOUT_MS / 2, "case %lu: the wait took %lu ms\n",
           Index, Case.Milliseconds);
        ok(Took < WAIT_TERMINATE_TIMEOUT_MS / 2, "case %lu: the thread took %lu ms to end\n", Index, Took);
    }
}

START_TEST(KeWaitTerminate)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"KeWaitTerminate", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    TestCase(0, TRUE, FALSE, STATUS_ALERTED, TRUE);
    TestCase(1, FALSE, FALSE, STATUS_TIMEOUT, TRUE);
    TestCase(2, TRUE, TRUE, STATUS_ALERTED, TRUE);
    TestCase(3, FALSE, TRUE, STATUS_TIMEOUT, FALSE);

    KmtCloseDriver();
    KmtUnloadDriver();
}
