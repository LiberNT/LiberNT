/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The handle list read while the caller's buffer and the processes change
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>

#define QUERY_BUFFER_SIZE (4 * 1024 * 1024)
#define QUERY_ROUNDS 50
#define TOGGLE_ROUNDS 2000000
#define CHURN_ROUNDS 6
#define CHURN_THREADS 2

typedef struct _CHURN_CONTEXT
{
    PUCHAR Page;
    PUCHAR Buffer;
    volatile LONG Stop;
    ULONG Children;
    ULONG Misses;
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
BOOLEAN
IsHandleListed(
    _In_ PUCHAR Buffer,
    _In_ DWORD ProcessId,
    _In_ HANDLE Handle)
{
    PSYSTEM_HANDLE_INFORMATION_EX Info = (PSYSTEM_HANDLE_INFORMATION_EX)Buffer;
    ULONG_PTR Index;
    ULONG Length;

    if (!NT_SUCCESS(NtQuerySystemInformation(SystemExtendedHandleInformation, Buffer, QUERY_BUFFER_SIZE, &Length)))
        return FALSE;

    for (Index = 0; Index < Info->NumberOfHandles; Index++)
    {
        if ((Info->Handles[Index].UniqueProcessId == ProcessId) &&
            (Info->Handles[Index].HandleValue == (ULONG_PTR)Handle))
        {
            return TRUE;
        }
    }

    return FALSE;
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
    HANDLE Event;
    HANDLE Remote;
    ULONG Round;

    if (GetModuleFileNameW(NULL, Path, RTL_NUMBER_OF(Path)) == 0)
        return 1;

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Event == NULL)
        return 1;

    for (Round = 0; (Round < CHURN_ROUNDS) && !Context->Stop; Round++)
    {
        RtlZeroMemory(&Startup, sizeof(Startup));
        Startup.cb = sizeof(Startup);

        if (!CreateProcessW(Path, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Process))
            break;

        Context->Children++;

        if (!DuplicateHandle(GetCurrentProcess(), Event, Process.hProcess, &Remote, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
            !IsHandleListed(Context->Buffer, Process.dwProcessId, Remote))
        {
            Context->Misses++;
        }

        TerminateProcess(Process.hProcess, 0);
        WaitForSingleObject(Process.hProcess, 10000);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }

    CloseHandle(Event);

    return 0;
}

static
ULONG
QueryWhile(
    _In_ PUCHAR Buffer,
    _In_ ULONG ThreadCount,
    _In_reads_(ThreadCount) PHANDLE Threads)
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

        if (WaitForMultipleObjects(ThreadCount, Threads, TRUE, 0) == WAIT_OBJECT_0)
            break;
    }

    return Unexpected;
}

START_TEST(ExHandleInformation)
{
    CHURN_CONTEXT Context;
    HANDLE Thread;
    PUCHAR Buffer;
    ULONG Unexpected;

    Buffer = VirtualAlloc(NULL, QUERY_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Buffer != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (Buffer == NULL)
        return;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Page = Buffer + 8 * PAGE_SIZE;

    Thread = CreateThread(NULL, 0, TogglePage, &Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread != NULL)
    {
        Unexpected = QueryWhile(Buffer, 1, &Thread);

        InterlockedExchange(&Context.Stop, 1);
        ok(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0, "the toggling thread did not end\n");
        CloseHandle(Thread);

        ok(Unexpected == 0, "%lu queries failed unexpectedly\n", Unexpected);
    }

    VirtualFree(Buffer, 0, MEM_RELEASE);
}

START_TEST(ExHandleListChurn)
{
    CHURN_CONTEXT Context[CHURN_THREADS];
    HANDLE Threads[CHURN_THREADS];
    PUCHAR Buffer;
    ULONG Unexpected;
    ULONG Started = 0;
    ULONG Index;

    Buffer = VirtualAlloc(NULL, QUERY_BUFFER_SIZE * (CHURN_THREADS + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Buffer != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (Buffer == NULL)
        return;

    for (Index = 0; Index < CHURN_THREADS; Index++)
    {
        RtlZeroMemory(&Context[Index], sizeof(Context[Index]));
        Context[Index].Buffer = Buffer + QUERY_BUFFER_SIZE * (Index + 1);

        Threads[Started] = CreateThread(NULL, 0, ChurnProcesses, &Context[Index], 0, NULL);
        ok(Threads[Started] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Threads[Started] != NULL)
            Started++;
    }

    if (Started != 0)
    {
        Unexpected = QueryWhile(Buffer, Started, Threads);
        ok(Unexpected == 0, "%lu queries failed unexpectedly\n", Unexpected);
    }

    for (Index = 0; Index < Started; Index++)
    {
        ok(WaitForSingleObject(Threads[Index], 60000) == WAIT_OBJECT_0, "churning thread %lu did not end\n", Index);
        CloseHandle(Threads[Index]);
    }

    for (Index = 0; Index < CHURN_THREADS; Index++)
    {
        ok(Context[Index].Children == CHURN_ROUNDS, "thread %lu made %lu processes\n", Index, Context[Index].Children);
        ok(Context[Index].Misses == 0, "thread %lu: %lu duplicated handles not listed under the child's id\n",
           Index, Context[Index].Misses);
    }

    VirtualFree(Buffer, 0, MEM_RELEASE);
}
