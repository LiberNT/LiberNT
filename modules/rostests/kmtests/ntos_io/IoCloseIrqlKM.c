/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IRQL at which IRP_MJ_CLOSE reaches the driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT CloseSeen;
static PDEVICE_OBJECT CloseDevice;
static volatile KIRQL CloseIrql;
static PDRIVER_DISPATCH SavedDispatch[IRP_MJ_MAXIMUM_FUNCTION + 1];
static const UCHAR HookedFunctions[] = { IRP_MJ_CREATE, IRP_MJ_CLEANUP, IRP_MJ_CLOSE };

static
NTSTATUS
NTAPI
TestDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UCHAR MajorFunction = IoGetCurrentIrpStackLocation(Irp)->MajorFunction;

    if (DeviceObject != CloseDevice)
        return SavedDispatch[MajorFunction](DeviceObject, Irp);

    if (MajorFunction == IRP_MJ_CLOSE)
        CloseIrql = KeGetCurrentIrql();
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    if (MajorFunction == IRP_MJ_CLOSE)
        KeSetEvent(&CloseSeen, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static
VOID
HookDispatch(
    _In_ BOOLEAN Hook)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(HookedFunctions); i++)
    {
        PVOID *Slot = (PVOID *)&KmtDriverObject->MajorFunction[HookedFunctions[i]];

        if (Hook)
            SavedDispatch[HookedFunctions[i]] = InterlockedExchangePointer(Slot, (PVOID)TestDispatch);
        else
            InterlockedExchangePointer(Slot, (PVOID)SavedDispatch[HookedFunctions[i]]);
    }
}

static
VOID
TestLastReferenceAtApcLevel(
    _In_ PUNICODE_STRING Name)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    PFILE_OBJECT FileObject;
    LARGE_INTEGER Timeout;
    HANDLE Handle;
    NTSTATUS Status;
    KIRQL OldIrql;

    InitializeObjectAttributes(&Attributes, Name, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle, FILE_READ_ATTRIBUTES, &Attributes, &IoStatus, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status)) return;

    KeRaiseIrql(APC_LEVEL, &OldIrql);
    ObDereferenceObject(FileObject);
    KeLowerIrql(OldIrql);

    Timeout.QuadPart = -10LL * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&CloseSeen, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(CloseIrql, PASSIVE_LEVEL);
}

START_TEST(IoCloseIrqlKM)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KmtestCloseIrql");
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject, 0, &Name, FILE_DEVICE_UNKNOWN, 0, FALSE, &CloseDevice);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    CloseDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    KeInitializeEvent(&CloseSeen, NotificationEvent, FALSE);
    CloseIrql = HIGH_LEVEL;
    HookDispatch(TRUE);
    TestLastReferenceAtApcLevel(&Name);
    /* A close still on its way must not reach the original dispatch, which does not know this device */
    if (KeReadStateEvent(&CloseSeen))
        HookDispatch(FALSE);

    IoDeleteDevice(CloseDevice);
    CloseDevice = NULL;
}
