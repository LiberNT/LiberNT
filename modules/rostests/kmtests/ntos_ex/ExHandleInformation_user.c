/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The handle list read while the caller's buffer and the processes change
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>

#define QUERY_BUFFER_SIZE (4 * 1024 * 1024)
#define QUERY_ROUNDS 200
#define TOGGLE_ROUNDS 2000000
#define CHURN_ROUNDS 24

typedef struct _CHURN_CONTEXT
{
    PUCHAR Page;
    volatile LONG Stop;
} CHURN_CONTEXT, *PCHURN_CONTEXT;

static
DWORD
WINAPI
TogglePage(
    _In_ PVOID Parameter)
{
    PCHURN_CONTEXT Context = Parameter;
    DWORD Old;
    ULONG Round;

    for (Round = 0; (Round < TOGGLE_ROUNDS) && !Context->Stop; Round++)
    {
        VirtualProtect(Context->Page, PAGE_SIZE, PAGE_NOACCESS, &Old);
        VirtualProtect(Context->Page, PAGE_SIZE, PAGE_READWRITE, &Old);
    }

    return 0;
}

static
DWORD
WINAPI
ChurnProcesses(
    _In_ PVOID Parameter)
{
    PCHURN_CONTEXT Context = Parameter;
    WCHAR Path[MAX_PATH];
    STARTUPINFOW Startup;
    PROCESS_INFORMATION Process;
    ULONG Round;

    if (GetModuleFileNameW(NULL, Path, RTL_NUMBER_OF(Path)) == 0)
        return 1;

    for (Round = 0; (Round < CHURN_ROUNDS) && !Context->Stop; Round++)
    {
        RtlZeroMemory(&Startup, sizeof(Startup));
        Startup.cb = sizeof(Startup);

        if (!CreateProcessW(Path, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Process))
            return 1;

        TerminateProcess(Process.hProcess, 0);
        WaitForSingleObject(Process.hProcess, 10000);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }

    return 0;
}

static
ULONG
QueryWhile(
    _In_ PUCHAR Buffer,
    _In_ HANDLE Thread)
{
    ULONG Unexpected = 0;
    ULONG Length;
    ULONG Round;
    NTSTATUS Status;

    for (Round = 0; Round < QUERY_ROUNDS; Round++)
    {
        Status = NtQuerySystemInformation(SystemExtendedHandleInformation, Buffer, QUERY_BUFFER_SIZE, &Length);
        if ((Status != STATUS_SUCCESS) &&
            (Status != STATUS_ACCESS_VIOLATION) &&
            (Status != STATUS_INFO_LENGTH_MISMATCH))
        {
            trace("round %lu: status 0x%lx\n", Round, Status);
            Unexpected++;
        }

        if (WaitForSingleObject(Thread, 0) == WAIT_OBJECT_0)
            break;
    }

    return Unexpected;
}

static
VOID
TestWalkWhile(
    _In_ PCSTR Name,
    _In_ LPTHREAD_START_ROUTINE Routine)
{
    CHURN_CONTEXT Context;
    HANDLE Thread;
    PUCHAR Buffer;
    ULONG Unexpected;

    Buffer = VirtualAlloc(NULL, QUERY_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Buffer != NULL, "%s: VirtualAlloc failed with %lu\n", Name, GetLastError());
    if (Buffer == NULL)
        return;

    Context.Page = Buffer + 8 * PAGE_SIZE;
    Context.Stop = 0;

    Thread = CreateThread(NULL, 0, Routine, &Context, 0, NULL);
    ok(Thread != NULL, "%s: CreateThread failed with %lu\n", Name, GetLastError());
    if (Thread == NULL)
    {
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return;
    }

    Unexpected = QueryWhile(Buffer, Thread);

    InterlockedExchange(&Context.Stop, 1);
    ok(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0, "%s: thread did not end\n", Name);
    CloseHandle(Thread);

    ok(Unexpected == 0, "%s: %lu queries failed unexpectedly\n", Name, Unexpected);

    VirtualFree(Buffer, 0, MEM_RELEASE);
}

START_TEST(ExHandleInformation)
{
    TestWalkWhile("buffer protection changing", TogglePage);
    TestWalkWhile("processes coming and going", ChurnProcesses);
}
