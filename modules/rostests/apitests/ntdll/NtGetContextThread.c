/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test reading the registers of threads while they end
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define EXITING_THREADS 4000

static volatile LONG Readers;

static DWORD WINAPI EndAtOnce(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return 7;
}

static DWORD WINAPI ReadWhileEnding(PVOID Context)
{
    ULONG Index, Reads = 0, Ended = 0;
    CONTEXT Registers;
    HANDLE Thread;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Context);
    for (Index = 0; Index < EXITING_THREADS; Index++)
    {
        Thread = CreateThread(NULL, 0, EndAtOnce, NULL, 0, NULL);
        if (!Thread)
        {
            trace("CreateThread %lu failed: %lu\n", Index, GetLastError());
            return 1;
        }
        do
        {
            Registers.ContextFlags = CONTEXT_CONTROL;
            Status = NtGetContextThread(Thread, &Registers);
            if (NT_SUCCESS(Status))
                Reads++;
        } while (WaitForSingleObject(Thread, 0) == WAIT_TIMEOUT);
        Ended++;
        CloseHandle(Thread);
    }
    InterlockedAdd(&Readers, (LONG)(Reads != 0));
    return Ended == EXITING_THREADS ? 0 : 3;
}

START_TEST(NtGetContextThread)
{
    SYSTEM_INFO Info;
    HANDLE Workers[8];
    DWORD Count, Index, Result, ExitCode;

    GetSystemInfo(&Info);
    Count = min(max(Info.dwNumberOfProcessors, 2), ARRAYSIZE(Workers));
    for (Index = 0; Index < Count; Index++)
    {
        Workers[Index] = CreateThread(NULL, 0, ReadWhileEnding, NULL, 0, NULL);
        ok(Workers[Index] != NULL, "CreateThread failed: %lu\n", GetLastError());
        if (!Workers[Index])
            return;
    }
    Result = WaitForMultipleObjects(Count, Workers, TRUE, 120000);
    ok(Result < WAIT_OBJECT_0 + Count, "reading the registers of ending threads hung: %lu\n", Result);
    for (Index = 0; Index < Count; Index++)
    {
        ExitCode = 0xFFFFFFFF;
        GetExitCodeThread(Workers[Index], &ExitCode);
        ok(ExitCode == 0, "worker %lu ended with %lu\n", Index, ExitCode);
        CloseHandle(Workers[Index]);
    }
    ok(Readers != 0, "no register read succeeded\n");
}
