/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The memory resource notification events the system publishes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

/* The high level is three times the low one, and the low one is at most 64 MB,
 * so this much available memory means plenty on any machine */
#define MM_PLENTY_PAGES ((3 * 64 * 1024 * 1024) / PAGE_SIZE)

static
HANDLE
OpenCondition(
    _In_ PCWSTR Path)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE Handle = NULL;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    /* The rights CreateMemoryResourceNotification asks for */
    Status = ZwOpenEvent(&Handle, EVENT_QUERY_STATE | SYNCHRONIZE, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return NULL;

    return Handle;
}

static
BOOLEAN
QueryCondition(
    _In_ HANDLE Handle,
    _Out_ PLONG State)
{
    EVENT_BASIC_INFORMATION Info = {0};
    NTSTATUS Status;

    *State = -1;

    Status = ZwQueryEvent(Handle, EventBasicInformation, &Info, sizeof(Info), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* Every waiter has to see the condition, so it is a notification event */
    ok_eq_int(Info.EventType, NotificationEvent);

    *State = Info.EventState;
    return TRUE;
}

START_TEST(MmMemoryNotification)
{
    SYSTEM_PERFORMANCE_INFORMATION Performance = {0};
    LONG Low = -1, High = -1;
    HANDLE LowHandle, HighHandle;

    LowHandle = OpenCondition(L"\\KernelObjects\\LowMemoryCondition");
    HighHandle = OpenCondition(L"\\KernelObjects\\HighMemoryCondition");
    if (skip(LowHandle != NULL && HighHandle != NULL, "No memory resource notification events\n"))
    {
        if (LowHandle != NULL) ObCloseHandle(LowHandle, KernelMode);
        if (HighHandle != NULL) ObCloseHandle(HighHandle, KernelMode);
        return;
    }

    if (QueryCondition(LowHandle, &Low) && QueryCondition(HighHandle, &High))
    {
        /* The two conditions are exclusive, and neither holds in between */
        ok(Low == 0 || High == 0, "Both conditions signalled at once\n");

        if (NT_SUCCESS(ZwQuerySystemInformation(SystemPerformanceInformation, &Performance, sizeof(Performance), NULL)) &&
            Performance.AvailablePages > MM_PLENTY_PAGES)
        {
            ok_eq_int(High, 1);
            ok_eq_int(Low, 0);
        }
    }

    ObCloseHandle(LowHandle, KernelMode);
    ObCloseHandle(HighHandle, KernelMode);
}
