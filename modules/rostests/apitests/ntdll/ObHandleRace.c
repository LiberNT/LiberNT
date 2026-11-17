/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Close a handle value from another thread while the handle is being created
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#define RACE_ITERATIONS 100000
#define RACE_CLOSERS 3

typedef enum _RACE_KIND
{
    RaceCreate,
    RaceOpen,
    RaceDuplicate
} RACE_KIND;

static volatile HANDLE RaceHandle;
static volatile LONG RaceStop;
static HANDLE RaceSource;

static
DWORD
WINAPI
RaceCloser(
    _In_ PVOID Parameter)
{
    HANDLE Handle;

    UNREFERENCED_PARAMETER(Parameter);
    while (!RaceStop)
    {
        Handle = RaceHandle;
        if (Handle != NULL && Handle != RaceSource)
            NtClose(Handle);
    }
    return 0;
}

static
NTSTATUS
RaceMakeHandle(
    _In_ RACE_KIND Kind,
    _In_ PUNICODE_STRING Name,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    switch (Kind)
    {
        case RaceCreate:
            return NtCreateEvent(Handle, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);

        case RaceOpen:
            InitializeObjectAttributes(&ObjectAttributes, Name, 0, NULL, NULL);
            return NtOpenEvent(Handle, EVENT_ALL_ACCESS, &ObjectAttributes);

        default:
            return NtDuplicateObject(NtCurrentProcess(), RaceSource, NtCurrentProcess(), Handle,
                                     0, 0, DUPLICATE_SAME_ACCESS);
    }
}

static
VOID
RaceRun(
    _In_ RACE_KIND Kind,
    _In_ PUNICODE_STRING Name)
{
    LARGE_INTEGER Frequency, Start, Now;
    ULONG Iterations = 0, Created = 0, ClosedUnderUs = 0, BadCreate = 0, BadClose = 0;
    NTSTATUS Status, LastBad = STATUS_SUCCESS;
    OBJECT_BASIC_INFORMATION Basic;
    HANDLE Closers[RACE_CLOSERS], Handle;
    ULONG i;

    RaceHandle = NULL;
    RaceStop = FALSE;
    for (i = 0; i < RACE_CLOSERS; i++)
    {
        Closers[i] = CreateThread(NULL, 0, RaceCloser, NULL, 0, NULL);
        ok(Closers[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
        if (Closers[i] == NULL)
        {
            RaceStop = TRUE;
            while (i-- != 0)
            {
                WaitForSingleObject(Closers[i], INFINITE);
                CloseHandle(Closers[i]);
            }
            return;
        }
    }

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    do
    {
        Handle = NULL;
        Status = RaceMakeHandle(Kind, Name, &Handle);
        if (!NT_SUCCESS(Status))
        {
            BadCreate++;
            LastBad = Status;
        }
        else
        {
            Created++;
            RaceHandle = Handle;
            Status = NtClose(Handle);
            if (Status == STATUS_INVALID_HANDLE)
                ClosedUnderUs++;
            else if (Status != STATUS_SUCCESS)
            {
                BadClose++;
                LastBad = Status;
            }
        }
        Iterations++;
        QueryPerformanceCounter(&Now);
    } while (Iterations < RACE_ITERATIONS && Now.QuadPart - Start.QuadPart < Frequency.QuadPart);

    RaceStop = TRUE;
    for (i = 0; i < RACE_CLOSERS; i++)
    {
        WaitForSingleObject(Closers[i], INFINITE);
        CloseHandle(Closers[i]);
    }

    ok(BadCreate == 0, "kind %d: %lu creates failed, last 0x%lx\n", Kind, BadCreate, LastBad);
    ok(BadClose == 0, "kind %d: %lu closes failed, last 0x%lx\n", Kind, BadClose, LastBad);
    ok(Created != 0, "kind %d: no handle in %lu iterations\n", Kind, Iterations);
    trace("kind %d: %lu iterations, %lu created, %lu closed by the other thread\n", Kind, Iterations, Created, ClosedUnderUs);

    if (RaceSource != NULL)
    {
        Status = NtQueryObject(RaceSource, ObjectBasicInformation, &Basic, sizeof(Basic), NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            ok(Basic.HandleCount == 1, "kind %d: source handle count %lu\n", Kind, Basic.HandleCount);
        Status = NtSetEvent(RaceSource, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
    }
}

START_TEST(ObHandleRace)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    WCHAR Buffer[96];
    NTSTATUS Status;

    RaceSource = NULL;
    RaceRun(RaceCreate, NULL);

    _snwprintf(Buffer, RTL_NUMBER_OF(Buffer) - 1, L"\\BaseNamedObjects\\LiberNTObHandleRace%lu", GetCurrentProcessId());
    Buffer[RTL_NUMBER_OF(Buffer) - 1] = UNICODE_NULL;
    RtlInitUnicodeString(&Name, Buffer);
    InitializeObjectAttributes(&ObjectAttributes, &Name, 0, NULL, NULL);
    Status = NtCreateEvent(&RaceSource, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RaceRun(RaceOpen, &Name);
    RaceRun(RaceDuplicate, NULL);
    NtClose(RaceSource);
}
