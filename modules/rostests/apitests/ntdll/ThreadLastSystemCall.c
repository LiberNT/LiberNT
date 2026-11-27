/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test ThreadLastSystemCall, MutantOwnerInformation and the product SKU queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOLEAN (NTAPI *PFN_SKU)(VOID);

static HANDLE StartedEvent, Release, Mutant;

static
DWORD
WINAPI
WaitSingle(
    _In_ PVOID Parameter)
{
    SetEvent(StartedEvent);
    NtWaitForSingleObject(Release, FALSE, NULL);
    return 0;
}

static
DWORD
WINAPI
WaitMultiple(
    _In_ PVOID Parameter)
{
    HANDLE Handles[2] = { Release, StartedEvent };

    ResetEvent(StartedEvent);
    NtWaitForMultipleObjects(2, Handles, WaitAny, FALSE, NULL);
    return 0;
}

static
DWORD
WINAPI
DelayForever(
    _In_ PVOID Parameter)
{
    LARGE_INTEGER Forever;

    Forever.QuadPart = MINLONGLONG;
    SetEvent(StartedEvent);
    NtDelayExecution(FALSE, &Forever);
    return 0;
}

static
DWORD
WINAPI
OwnMutant(
    _In_ PVOID Parameter)
{
    WaitForSingleObject(Mutant, INFINITE);
    SetEvent(StartedEvent);
    WaitForSingleObject(Release, INFINITE);
    ReleaseMutex(Mutant);
    return 0;
}

static
BOOL
WaitUntilBlocked(
    _In_ HANDLE Thread,
    _Out_ PTHREAD_LAST_SYSCALL_INFORMATION LastCall)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt < 2000; Attempt++)
    {
        if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadLastSystemCall, LastCall, sizeof(*LastCall), NULL)))
            return TRUE;
        Sleep(1);
    }
    return FALSE;
}

static
VOID
TestLastSystemCall(VOID)
{
    THREAD_LAST_SYSCALL_INFORMATION LastCall, Second;
    UCHAR Buffer[32];
    ULONG Length, Size, Increment, Adjustment;
    BOOL Disabled;
    NTSTATUS Status;
    HANDLE Thread, Limited;
    DWORD ThreadId;

    for (Size = 0; Size <= 32; Size += 4)
    {
        Length = 0xdead;
        Status = NtQueryInformationThread(NtCurrentThread(), ThreadLastSystemCall, Buffer, Size, &Length);
        if (Size == sizeof(THREAD_LAST_SYSCALL_INFORMATION) || Size == FIELD_OFFSET(THREAD_LAST_SYSCALL_INFORMATION, WaitTime))
            ok_ntstatus(Status, STATUS_INVALID_PARAMETER);
        else
            ok_ntstatus(Status, STATUS_INFO_LENGTH_MISMATCH);
        ok_long(Length, 0xdead);
    }

    Thread = CreateThread(NULL, 0, WaitSingle, NULL, 0, &ThreadId);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    WaitForSingleObject(StartedEvent, INFINITE);
    ok(WaitUntilBlocked(Thread, &LastCall), "the thread never blocked\n");
    ok(LastCall.FirstArgument == Release, "FirstArgument %p, expected %p\n", LastCall.FirstArgument, Release);
    ok(LastCall.SystemCallNumber != 0, "no system call number\n");
    GetSystemTimeAdjustment(&Adjustment, &Increment, &Disabled);
    ok(LastCall.WaitTime < 100000000ULL, "WaitTime %I64u\n", LastCall.WaitTime);

    FillMemory(Buffer, sizeof(Buffer), 0xcc);
    Length = 0xdead;
    Status = NtQueryInformationThread(Thread, ThreadLastSystemCall, Buffer, FIELD_OFFSET(THREAD_LAST_SYSCALL_INFORMATION, WaitTime), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_long(Length, FIELD_OFFSET(THREAD_LAST_SYSCALL_INFORMATION, WaitTime));
    ok(*(PVOID *)Buffer == Release, "short FirstArgument %p\n", *(PVOID *)Buffer);
    ok_int(*(USHORT *)(Buffer + sizeof(PVOID)), LastCall.SystemCallNumber);
    ok_hex(Buffer[sizeof(PVOID) + sizeof(USHORT)], 0xcc);
    ok_hex(Buffer[FIELD_OFFSET(THREAD_LAST_SYSCALL_INFORMATION, WaitTime)], 0xcc);

    Limited = OpenThread(THREAD_QUERY_INFORMATION, FALSE, ThreadId);
    ok_ntstatus(NtQueryInformationThread(Limited, ThreadLastSystemCall, &Second, sizeof(Second), NULL), STATUS_ACCESS_DENIED);
    CloseHandle(Limited);
    Limited = OpenThread(THREAD_GET_CONTEXT, FALSE, ThreadId);
    ok_ntstatus(NtQueryInformationThread(Limited, ThreadLastSystemCall, &Second, sizeof(Second), NULL), STATUS_SUCCESS);
    ok(Second.FirstArgument == Release, "FirstArgument through THREAD_GET_CONTEXT %p\n", Second.FirstArgument);
    ok(Second.WaitTime % Increment == 0, "WaitTime %I64u is not a whole number of %lu ticks\n", Second.WaitTime, Increment);
    CloseHandle(Limited);

    SetEvent(Release);
    WaitForSingleObject(Thread, INFINITE);
    ResetEvent(Release);
    ok_ntstatus(NtQueryInformationThread(Thread, ThreadLastSystemCall, &Second, sizeof(Second), NULL), STATUS_UNSUCCESSFUL);
    CloseHandle(Thread);

    Thread = CreateThread(NULL, 0, WaitMultiple, NULL, 0, NULL);
    ok(WaitUntilBlocked(Thread, &Second), "the thread never blocked\n");
    ok(Second.FirstArgument == (PVOID)2, "multiple wait FirstArgument %p\n", Second.FirstArgument);
    ok(Second.SystemCallNumber != LastCall.SystemCallNumber, "both waits report service %u\n", Second.SystemCallNumber);
    SetEvent(Release);
    WaitForSingleObject(Thread, INFINITE);
    ResetEvent(Release);
    CloseHandle(Thread);

    Thread = CreateThread(NULL, 0, DelayForever, NULL, 0, NULL);
    WaitForSingleObject(StartedEvent, INFINITE);
    ok(WaitUntilBlocked(Thread, &Second), "the thread never blocked\n");
    ok(Second.FirstArgument == (PVOID)FALSE, "delay FirstArgument %p\n", Second.FirstArgument);
    ok(Second.SystemCallNumber != LastCall.SystemCallNumber, "delay reports the wait service %u\n", Second.SystemCallNumber);
    TerminateThread(Thread, 0);
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
}

static
VOID
TestMutantOwner(VOID)
{
    MUTANT_OWNER_INFORMATION Owner;
    UCHAR Buffer[32];
    ULONG Length, Size;
    HANDLE Thread, Synchronize;
    DWORD ThreadId;

    Thread = CreateThread(NULL, 0, OwnMutant, NULL, 0, &ThreadId);
    WaitForSingleObject(StartedEvent, INFINITE);

    for (Size = 0; Size <= 24; Size += 4)
    {
        Length = 0xdead;
        if (Size == sizeof(MUTANT_OWNER_INFORMATION))
        {
            ok_ntstatus(NtQueryMutant(Mutant, MutantOwnerInformation, Buffer, Size, &Length), STATUS_SUCCESS);
            ok_long(Length, sizeof(MUTANT_OWNER_INFORMATION));
        }
        else
        {
            ok_ntstatus(NtQueryMutant(Mutant, MutantOwnerInformation, Buffer, Size, &Length), STATUS_INFO_LENGTH_MISMATCH);
            ok_long(Length, 0xdead);
        }
    }

    ok_ntstatus(NtQueryMutant(Mutant, MutantOwnerInformation, &Owner, sizeof(Owner), NULL), STATUS_SUCCESS);
    ok(Owner.ClientId.UniqueProcess == UlongToHandle(GetCurrentProcessId()), "owner process %p\n", Owner.ClientId.UniqueProcess);
    ok(Owner.ClientId.UniqueThread == UlongToHandle(ThreadId), "owner thread %p, expected %lu\n", Owner.ClientId.UniqueThread, ThreadId);

    ok(DuplicateHandle(GetCurrentProcess(), Mutant, GetCurrentProcess(), &Synchronize, SYNCHRONIZE, FALSE, 0), "DuplicateHandle failed\n");
    ok_ntstatus(NtQueryMutant(Synchronize, MutantOwnerInformation, &Owner, sizeof(Owner), NULL), STATUS_ACCESS_DENIED);
    CloseHandle(Synchronize);
    ok_ntstatus(NtQueryMutant(Mutant, (MUTANT_INFORMATION_CLASS)2, Buffer, sizeof(Owner), NULL), STATUS_INVALID_INFO_CLASS);

    SetEvent(Release);
    WaitForSingleObject(Thread, INFINITE);
    ResetEvent(Release);
    CloseHandle(Thread);

    FillMemory(&Owner, sizeof(Owner), 0xcc);
    ok_ntstatus(NtQueryMutant(Mutant, MutantOwnerInformation, &Owner, sizeof(Owner), NULL), STATUS_SUCCESS);
    ok(Owner.ClientId.UniqueProcess == NULL && Owner.ClientId.UniqueThread == NULL, "free mutant owner %p.%p\n",
       Owner.ClientId.UniqueProcess, Owner.ClientId.UniqueThread);

    WaitForSingleObject(Mutant, 0);
    ok_ntstatus(NtQueryMutant(Mutant, MutantOwnerInformation, &Owner, sizeof(Owner), NULL), STATUS_SUCCESS);
    ok(Owner.ClientId.UniqueThread == UlongToHandle(GetCurrentThreadId()), "self owner %p\n", Owner.ClientId.UniqueThread);
    ReleaseMutex(Mutant);
}

static
VOID
TestSku(VOID)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_SKU MultiSession = (PFN_SKU)GetProcAddress(Ntdll, "RtlIsMultiSessionSku");
    PFN_SKU MultiUsers = (PFN_SKU)GetProcAddress(Ntdll, "RtlIsMultiUsersInSessionSku");
    PFN_SKU StateSeparation = (PFN_SKU)GetProcAddress(Ntdll, "RtlIsStateSeparationEnabled");
    ULONG Flags = SharedUserData->SharedDataFlags;

    ok(MultiSession && MultiUsers && StateSeparation, "ntdll lacks a SKU export\n");
    if (!MultiSession || !MultiUsers || !StateSeparation)
        return;

    ok_int(MultiSession(), (Flags >> 8) & 1);
    ok_int(MultiUsers(), (Flags >> 9) & 1);
    ok_int(StateSeparation(), (Flags >> 10) & 1);
}

static
VOID
TestReadOnlyBuffer(VOID)
{
    PVOID Page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY);
    THREAD_BASIC_INFORMATION *Basic = Page;
    NTSTATUS Status;

    ok(Page != NULL, "VirtualAlloc failed\n");
    if (!Page)
        return;
    Status = NtQueryInformationThread(NtCurrentThread(), ThreadBasicInformation, Basic, sizeof(*Basic), NULL);
    ok_ntstatus(Status, STATUS_ACCESS_VIOLATION);
    VirtualFree(Page, 0, MEM_RELEASE);
}

START_TEST(ThreadLastSystemCall)
{
    StartedEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    Mutant = CreateMutexW(NULL, FALSE, NULL);

    TestLastSystemCall();
    TestMutantOwner();
    TestSku();
    TestReadOnlyBuffer();

    CloseHandle(Mutant);
    CloseHandle(Release);
    CloseHandle(StartedEvent);
}
