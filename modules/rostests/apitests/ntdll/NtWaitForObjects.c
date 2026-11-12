/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the object manager wait system calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static LARGE_INTEGER NoWait;

static
HANDLE
CreateTestEvent(
    _In_ EVENT_TYPE Type,
    _In_ BOOLEAN Signaled)
{
    HANDLE Handle = NULL;
    NTSTATUS Status;

    Status = NtCreateEvent(&Handle, EVENT_ALL_ACCESS, NULL, Type, Signaled);
    ok_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Handle : NULL;
}

static
VOID
TestParameterValidation(VOID)
{
    HANDLE Event = CreateTestEvent(NotificationEvent, TRUE);
    HANDLE Handles[2];
    NTSTATUS Status;

    if (Event == NULL) return;

    Handles[0] = Event;
    Handles[1] = Event;

    Status = NtWaitForMultipleObjects(0, Handles, WaitAny, FALSE, &NoWait);
    ok_hex(Status, STATUS_INVALID_PARAMETER_1);

    Status = NtWaitForMultipleObjects(MAXIMUM_WAIT_OBJECTS + 1, Handles, WaitAny, FALSE, &NoWait);
    ok_hex(Status, STATUS_INVALID_PARAMETER_1);

    Status = NtWaitForMultipleObjects(1, Handles, (WAIT_TYPE)2, FALSE, &NoWait);
    ok_hex(Status, STATUS_INVALID_PARAMETER_3);

    Status = NtWaitForMultipleObjects(1, NULL, WaitAny, FALSE, &NoWait);
    ok_hex(Status, STATUS_ACCESS_VIOLATION);

    Status = NtWaitForMultipleObjects(1, Handles, WaitAny, FALSE, (PLARGE_INTEGER)(ULONG_PTR)8);
    ok_hex(Status, STATUS_ACCESS_VIOLATION);

    NtClose(Event);
}

static
VOID
TestWaitResults(VOID)
{
    HANDLE Signaled = CreateTestEvent(NotificationEvent, TRUE);
    HANDLE Clear = CreateTestEvent(NotificationEvent, FALSE);
    HANDLE Handles[2];
    NTSTATUS Status;

    if (Signaled == NULL || Clear == NULL)
    {
        if (Signaled) NtClose(Signaled);
        if (Clear) NtClose(Clear);
        return;
    }

    Status = NtWaitForSingleObject(Signaled, FALSE, &NoWait);
    ok_hex(Status, STATUS_WAIT_0);

    Status = NtWaitForSingleObject(Clear, FALSE, &NoWait);
    ok_hex(Status, STATUS_TIMEOUT);

    Status = NtWaitForSingleObject((HANDLE)(ULONG_PTR)0x1234, FALSE, &NoWait);
    ok_hex(Status, STATUS_INVALID_HANDLE);

    /* WaitAny reports the index of the object that satisfied the wait */
    Handles[0] = Clear;
    Handles[1] = Signaled;
    Status = NtWaitForMultipleObjects(2, Handles, WaitAny, FALSE, &NoWait);
    ok_hex(Status, STATUS_WAIT_1);

    Handles[0] = Signaled;
    Handles[1] = Clear;
    Status = NtWaitForMultipleObjects(2, Handles, WaitAny, FALSE, &NoWait);
    ok_hex(Status, STATUS_WAIT_0);

    /* WaitAll is only satisfied once every object is signaled */
    Status = NtWaitForMultipleObjects(2, Handles, WaitAll, FALSE, &NoWait);
    ok_hex(Status, STATUS_TIMEOUT);

    NtClose(Signaled);
    NtClose(Clear);
}

static
VOID
TestMaximumWaitObjects(VOID)
{
    HANDLE Handles[MAXIMUM_WAIT_OBJECTS];
    NTSTATUS Status;
    ULONG Index;
    ULONG Created;

    for (Created = 0; Created < MAXIMUM_WAIT_OBJECTS; Created++)
    {
        Status = NtCreateEvent(&Handles[Created], EVENT_ALL_ACCESS, NULL,
                               NotificationEvent, TRUE);
        if (!NT_SUCCESS(Status)) break;
    }

    ok_int(Created, MAXIMUM_WAIT_OBJECTS);

    if (Created == MAXIMUM_WAIT_OBJECTS)
    {
        /* A wait on the full dispatcher limit is legal and reports the first
         * object, even though it needs wait blocks beyond the thread's own */
        Status = NtWaitForMultipleObjects(MAXIMUM_WAIT_OBJECTS, Handles, WaitAny,
                                          FALSE, &NoWait);
        ok_hex(Status, STATUS_WAIT_0);

        Status = NtWaitForMultipleObjects(MAXIMUM_WAIT_OBJECTS, Handles, WaitAll,
                                          FALSE, &NoWait);
        ok_hex(Status, STATUS_WAIT_0);
    }

    for (Index = 0; Index < Created; Index++)
        NtClose(Handles[Index]);
}

static
VOID
TestSignalAndWait(VOID)
{
    HANDLE Signal = CreateTestEvent(SynchronizationEvent, FALSE);
    HANDLE Wait = CreateTestEvent(NotificationEvent, TRUE);
    HANDLE ReadOnly = NULL;
    NTSTATUS Status;

    if (Signal == NULL || Wait == NULL)
    {
        if (Signal) NtClose(Signal);
        if (Wait) NtClose(Wait);
        return;
    }

    /* The signal half runs even though the wait half returns at once */
    Status = NtSignalAndWaitForSingleObject(Signal, Wait, FALSE, &NoWait);
    ok_hex(Status, STATUS_WAIT_0);

    Status = NtWaitForSingleObject(Signal, FALSE, &NoWait);
    ok_hex(Status, STATUS_WAIT_0);

    /* The current process is not a signalable object */
    Status = NtSignalAndWaitForSingleObject(NtCurrentProcess(), Wait, FALSE, &NoWait);
    ok_hex(Status, STATUS_OBJECT_TYPE_MISMATCH);

    /* Signaling needs EVENT_MODIFY_STATE on the signaled handle */
    Status = NtDuplicateObject(NtCurrentProcess(), Signal,
                               NtCurrentProcess(), &ReadOnly,
                               SYNCHRONIZE, 0, 0);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtSignalAndWaitForSingleObject(ReadOnly, Wait, FALSE, &NoWait);
        ok_hex(Status, STATUS_ACCESS_DENIED);
        NtClose(ReadOnly);
    }

    /* The wait half needs SYNCHRONIZE on the waited handle */
    Status = NtSignalAndWaitForSingleObject(Signal, (HANDLE)(ULONG_PTR)0x1234,
                                            FALSE, &NoWait);
    ok_hex(Status, STATUS_INVALID_HANDLE);

    /* None of the calls that failed above may have signaled anything */
    Status = NtWaitForSingleObject(Signal, FALSE, &NoWait);
    ok_hex(Status, STATUS_TIMEOUT);

    NtClose(Signal);
    NtClose(Wait);
}

START_TEST(NtWaitForObjects)
{
    NoWait.QuadPart = 0;

    TestParameterValidation();
    TestWaitResults();
    TestMaximumWaitObjects();
    TestSignalAndWait();
}
