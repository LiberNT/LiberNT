/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Waits on behalf of a user thread that another thread terminates
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <kmt_test.h>
#include "KeWaitTerminate.h"

static KMT_MESSAGE_HANDLER TestMessageHandler;
static KEVENT NeverSignaled;
static WAIT_TERMINATE_CASE Cases[WAIT_TERMINATE_CASES];
static LARGE_INTEGER Timeout = { .QuadPart = -10000LL * WAIT_TERMINATE_TIMEOUT_MS };

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    *DeviceName = L"KeWaitTerminate";
    *Flags = TESTENTRY_NO_EXCLUSIVE_DEVICE;
    KeInitializeEvent(&NeverSignaled, NotificationEvent, FALSE);
    RtlZeroMemory(Cases, sizeof(Cases));
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
}

static
NTSTATUS
TestMessageHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_ PVOID Buffer OPTIONAL,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    PWAIT_TERMINATE_CASE Request = Buffer;
    PWAIT_TERMINATE_CASE Case;
    LARGE_INTEGER Start, End;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Request == NULL || InLength < sizeof(*Request) || *OutLength < sizeof(*Request) ||
        Request->Index >= WAIT_TERMINATE_CASES)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Case = &Cases[Request->Index];

    switch (ControlCode)
    {
        case IOCTL_WAIT_TERMINATE_WAIT:
            Case->Index = Request->Index;
            Case->Alertable = Request->Alertable;
            Case->UserMode = Request->UserMode;
            Case->Status = STATUS_PENDING;
            Case->Milliseconds = 0;
            InterlockedExchange((PLONG)&Case->Entered, 1);
            KeQuerySystemTime(&Start);
            Status = KeWaitForSingleObject(&NeverSignaled,
                                           UserRequest,
                                           Case->UserMode ? UserMode : KernelMode,
                                           (BOOLEAN)Case->Alertable,
                                           &Timeout);
            KeQuerySystemTime(&End);
            Case->Milliseconds = (ULONG)((End.QuadPart - Start.QuadPart) / 10000);
            InterlockedExchange(&Case->Status, Status);
            break;

        case IOCTL_WAIT_TERMINATE_QUERY:
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    RtlCopyMemory(Request, Case, sizeof(*Request));
    *OutLength = sizeof(*Request);
    return STATUS_SUCCESS;
}
