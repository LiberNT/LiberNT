/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that file services given the name of another kind of object leave no handle behind
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define ROUNDS 64

static DWORD
HandleCount(VOID)
{
    DWORD Count = 0;

    ok(GetProcessHandleCount(GetCurrentProcess(), &Count), "GetProcessHandleCount failed, error %lu\n", GetLastError());
    return Count;
}

START_TEST(NtDeleteFile)
{
    WCHAR NameBuffer[96];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_BASIC_INFORMATION Basic;
    FILE_NETWORK_OPEN_INFORMATION Network;
    HANDLE Event;
    NTSTATUS Status;
    DWORD Before, After;
    ULONG Round;

    _snwprintf(NameBuffer, RTL_NUMBER_OF(NameBuffer), L"\\BaseNamedObjects\\LiberntNtDeleteFile%lu", GetCurrentProcessId());
    NameBuffer[RTL_NUMBER_OF(NameBuffer) - 1] = UNICODE_NULL;
    RtlInitUnicodeString(&Name, NameBuffer);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        skip("No named event to aim the file services at\n");
        return;
    }

    /* The event's owner may delete it, so the open by name inside the service succeeds: it must not stay open */
    Before = HandleCount();
    for (Round = 0; Round < ROUNDS; Round++)
    {
        Status = NtDeleteFile(&ObjectAttributes);
        ok(!NT_SUCCESS(Status), "Round %lu: NtDeleteFile on an event returned 0x%lx\n", Round, Status);
    }
    After = HandleCount();
    trace("NtDeleteFile on an event: status 0x%lx, %lu handles before, %lu after\n", Status, Before, After);
    ok(After == Before, "NtDeleteFile left %ld handle(s) open\n", (LONG)(After - Before));

    Before = HandleCount();
    for (Round = 0; Round < ROUNDS; Round++)
    {
        Status = NtQueryAttributesFile(&ObjectAttributes, &Basic);
        ok(!NT_SUCCESS(Status), "Round %lu: NtQueryAttributesFile on an event returned 0x%lx\n", Round, Status);
    }
    After = HandleCount();
    trace("NtQueryAttributesFile on an event: status 0x%lx, %lu handles before, %lu after\n", Status, Before, After);
    ok(After == Before, "NtQueryAttributesFile left %ld handle(s) open\n", (LONG)(After - Before));

    Before = HandleCount();
    for (Round = 0; Round < ROUNDS; Round++)
    {
        Status = NtQueryFullAttributesFile(&ObjectAttributes, &Network);
        ok(!NT_SUCCESS(Status), "Round %lu: NtQueryFullAttributesFile on an event returned 0x%lx\n", Round, Status);
    }
    After = HandleCount();
    trace("NtQueryFullAttributesFile on an event: status 0x%lx, %lu handles before, %lu after\n", Status, Before, After);
    ok(After == Before, "NtQueryFullAttributesFile left %ld handle(s) open\n", (LONG)(After - Before));

    /* The event itself is untouched: it can still be signalled and is still the one the name finds */
    Status = NtSetEvent(Event, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = NtClose(Event);
    ok_ntstatus(Status, STATUS_SUCCESS);
}
