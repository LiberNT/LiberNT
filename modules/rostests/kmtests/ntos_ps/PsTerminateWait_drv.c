/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A user thread terminated while it waits in kernel mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <kmt_test.h>
#include "PsTerminateWait.h"

static KMT_MESSAGE_HANDLER TestMessageHandler;
static KEVENT TestEvent;
static volatile LONG TestStatus;
static volatile LONG TestWaited;

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    PAGED_CODE();

    *DeviceName = L"PsTerminateWait";
    KeInitializeEvent(&TestEvent, NotificationEvent, FALSE);
    TestStatus = STATUS_PENDING;
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    PAGED_CODE();
}

static
NTSTATUS
TestMessageHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(InLength);

    switch (ControlCode)
    {
        case IOCTL_TERMWAIT_WAIT:
        case IOCTL_TERMWAIT_NONALERTABLE:
            Timeout.QuadPart = -50000000LL;
            InterlockedExchange(&TestWaited, 1);
            Status = KeWaitForSingleObject(&TestEvent, Executive, KernelMode,
                                           ControlCode == IOCTL_TERMWAIT_WAIT, &Timeout);
            InterlockedExchange(&TestStatus, Status);
            *OutLength = 0;
            return STATUS_SUCCESS;

        case IOCTL_TERMWAIT_RESULT:
            if (Buffer == NULL || *OutLength < 2 * sizeof(LONG))
                return STATUS_BUFFER_TOO_SMALL;
            ((PLONG)Buffer)[0] = InterlockedCompareExchange(&TestStatus, 0, 0);
            ((PLONG)Buffer)[1] = InterlockedExchange(&TestWaited, 0);
            InterlockedExchange(&TestStatus, STATUS_PENDING);
            *OutLength = 2 * sizeof(LONG);
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}
