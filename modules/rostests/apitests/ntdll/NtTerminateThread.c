/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test ending threads that wait in the kernel or never started
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *PNT_WAIT_FOR_ALERT_BY_THREAD_ID)(PVOID Address, PLARGE_INTEGER Timeout);

static PNT_WAIT_FOR_ALERT_BY_THREAD_ID pNtWaitForAlertByThreadId;
static HANDLE WaitEvent;
static volatile LONG ThreadReady;

static DWORD WINAPI AlertWaitThread(PVOID Context)
{
    InterlockedExchange(&ThreadReady, 1);
    pNtWaitForAlertByThreadId(Context, NULL);
    return 0xdead;
}

static DWORD WINAPI EventWaitThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    InterlockedExchange(&ThreadReady, 1);
    NtWaitForSingleObject(WaitEvent, FALSE, NULL);
    return 0xdead;
}

static HANDLE FlushPipe;

static DWORD WINAPI FlushWaitThread(PVOID Context)
{
    IO_STATUS_BLOCK IoStatus;

    UNREFERENCED_PARAMETER(Context);

    InterlockedExchange(&ThreadReady, 1);
    NtFlushBuffersFile(FlushPipe, &IoStatus);
    return 0xdead;
}

static DWORD WINAPI NeverRunThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    return 0xdead;
}

static void EndWaitingThread(LPTHREAD_START_ROUTINE Routine, PVOID Context, NTSTATUS ExitStatus, const char *Name)
{
    LARGE_INTEGER Interval;
    DWORD ExitCode = 0;
    HANDLE Thread;
    NTSTATUS Status;
    ULONG Tries;

    InterlockedExchange(&ThreadReady, 0);
    Thread = CreateThread(NULL, 0, Routine, Context, 0, NULL);
    ok(Thread != NULL, "%s: CreateThread failed: %lu\n", Name, GetLastError());
    if (!Thread)
        return;

    Interval.QuadPart = -10 * 1000 * 10;
    for (Tries = 0; Tries < 100 && !ThreadReady; Tries++)
        NtDelayExecution(FALSE, &Interval);
    NtDelayExecution(FALSE, &Interval);

    Status = NtTerminateThread(Thread, ExitStatus);
    ok_hex(Status, STATUS_SUCCESS);
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s: the thread did not end\n", Name);
    ok(GetExitCodeThread(Thread, &ExitCode), "%s: GetExitCodeThread failed: %lu\n", Name, GetLastError());
    ok(ExitCode == (DWORD)ExitStatus, "%s: exit code %lx, expected %lx\n", Name, ExitCode, (DWORD)ExitStatus);
    CloseHandle(Thread);
}

START_TEST(NtTerminateThread)
{
    DWORD ExitCode = 0;
    HANDLE Thread;
    NTSTATUS Status;

    pNtWaitForAlertByThreadId = (PNT_WAIT_FOR_ALERT_BY_THREAD_ID)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                                "NtWaitForAlertByThreadId");
    if (pNtWaitForAlertByThreadId)
        EndWaitingThread(AlertWaitThread, (PVOID)&ThreadReady, 0x1234, "alert by thread id");
    else
        skip("NtWaitForAlertByThreadId is not available\n");

    WaitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(WaitEvent != NULL, "CreateEvent failed: %lu\n", GetLastError());
    EndWaitingThread(EventWaitThread, NULL, 0x4321, "event wait");
    CloseHandle(WaitEvent);

    /* A flush of a pipe nobody reads waits until the thread is ended */
    FlushPipe = CreateNamedPipeW(L"\\\\.\\pipe\\NtTerminateThreadFlush",
                                 PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_WAIT,
                                 1,
                                 4096,
                                 4096,
                                 0,
                                 NULL);
    ok(FlushPipe != INVALID_HANDLE_VALUE, "CreateNamedPipe failed: %lu\n", GetLastError());
    if (FlushPipe != INVALID_HANDLE_VALUE)
    {
        OVERLAPPED Overlapped = { 0 };
        HANDLE Client;
        DWORD Written = 0;
        BOOL Done;

        Client = CreateFileW(L"\\\\.\\pipe\\NtTerminateThreadFlush", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        ok(Client != INVALID_HANDLE_VALUE, "CreateFile failed: %lu\n", GetLastError());
        Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        Done = WriteFile(FlushPipe, "unread data", 11, &Written, &Overlapped);
        if (!Done && GetLastError() == ERROR_IO_PENDING)
            Done = GetOverlappedResult(FlushPipe, &Overlapped, &Written, TRUE);
        ok(Done && Written == 11, "WriteFile failed: %lu, written %lu\n", GetLastError(), Written);
        CloseHandle(Overlapped.hEvent);

        if (Client != INVALID_HANDLE_VALUE)
        {
            EndWaitingThread(FlushWaitThread, NULL, 0x5678, "pipe flush");
            CloseHandle(Client);
        }
        CloseHandle(FlushPipe);
    }

    Thread = CreateThread(NULL, 0, NeverRunThread, NULL, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;
    Status = NtTerminateThread(Thread, 199);
    ok_hex(Status, STATUS_SUCCESS);
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "the suspended thread did not end\n");
    ok(GetExitCodeThread(Thread, &ExitCode), "GetExitCodeThread failed: %lu\n", GetLastError());
    ok(ExitCode == 199, "suspended thread exit code %lu, expected 199\n", ExitCode);
    CloseHandle(Thread);
}
