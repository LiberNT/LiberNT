/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests RtlGetThreadErrorMode against RtlSetThreadErrorMode and the TEB
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _ERROR_MODE_THREAD
{
    ULONG Before;
    ULONG After;
    NTSTATUS Status;
} ERROR_MODE_THREAD, *PERROR_MODE_THREAD;

static
DWORD
WINAPI
ErrorModeThread(
    _In_ PVOID Parameter)
{
    PERROR_MODE_THREAD Result = Parameter;

    Result->Before = RtlGetThreadErrorMode();
    Result->Status = RtlSetThreadErrorMode(RTL_SEM_NOGPFAULTERRORBOX, NULL);
    Result->After = RtlGetThreadErrorMode();
    return 0;
}

START_TEST(RtlGetThreadErrorMode)
{
    ERROR_MODE_THREAD Result = { 0xdeadbeef, 0xdeadbeef, 0xdeadbeef };
    ULONG OriginalMode, OldMode;
    BOOL Wow64 = FALSE;
    NTSTATUS Status;
    HANDLE Thread;
    DWORD Wait;

    if (!IsWow64Process(GetCurrentProcess(), &Wow64))
        Wow64 = FALSE;

    OriginalMode = RtlGetThreadErrorMode();
    if (!Wow64)
        ok(OriginalMode == NtCurrentTeb()->HardErrorMode, "Mode 0x%lx, TEB 0x%lx\n",
           OriginalMode, NtCurrentTeb()->HardErrorMode);

    Status = RtlSetThreadErrorMode(RTL_SEM_FAILCRITICALERRORS | RTL_SEM_NOGPFAULTERRORBOX | RTL_SEM_NOALIGNMENTFAULTEXCEPT, &OldMode);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(OldMode == OriginalMode, "Old mode 0x%lx, expected 0x%lx\n", OldMode, OriginalMode);
    ok_hex(RtlGetThreadErrorMode(), 0x70);
    if (!Wow64)
        ok_hex(NtCurrentTeb()->HardErrorMode, 0x70);

    Status = RtlSetThreadErrorMode(0x80, &OldMode);
    ok_ntstatus(Status, STATUS_INVALID_PARAMETER_1);
    ok_hex(RtlGetThreadErrorMode(), 0x70);

    Thread = CreateThread(NULL, 0, ErrorModeThread, &Result, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread)
    {
        Wait = WaitForSingleObject(Thread, 5000);
        ok(Wait == WAIT_OBJECT_0, "Thread wait returned %lu\n", Wait);
        CloseHandle(Thread);
        ok_ntstatus(Result.Status, STATUS_SUCCESS);
        ok_hex(Result.After, RTL_SEM_NOGPFAULTERRORBOX);
        trace("A new thread started with error mode 0x%lx\n", Result.Before);
    }
    ok_hex(RtlGetThreadErrorMode(), 0x70);

    Status = RtlSetThreadErrorMode(0, &OldMode);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_hex(OldMode, 0x70);
    ok_hex(RtlGetThreadErrorMode(), 0);
    if (!Wow64)
        ok_hex(NtCurrentTeb()->HardErrorMode, 0);

    RtlSetThreadErrorMode(OriginalMode, NULL);
}
