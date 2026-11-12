/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The event registered for time-slip notification is set once the kernel debugger let the machine go on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(KdTimeSlipEvent)
{
    OBJECT_ATTRIBUTES Attributes;
    LARGE_INTEGER Timeout;
    HANDLE Event;
    NTSTATUS Status;

    if (skip(KD_DEBUGGER_ENABLED, "The kernel debugger is not enabled\n"))
        return;

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateEvent(&Event, EVENT_ALL_ACCESS, &Attributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwSetSystemInformation(SystemTimeSlipNotification, &Event, sizeof(Event));
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* Hidden test: something must answer this stop, such as KDBG with "cont" in its start-up file */
    DbgBreakPoint();

    Timeout.QuadPart = -1000 * 10000LL;
    Status = ZwWaitForSingleObject(Event, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ZwClose(Event);
}

/* EOF */
