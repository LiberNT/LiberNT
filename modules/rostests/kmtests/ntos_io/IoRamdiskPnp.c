/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A RAM disk drive completes each PnP request it answers exactly once
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define RAMDISK_TEST_TIMEOUT (-2LL * 1000 * 1000 * 10)

typedef struct _PNP_COMPLETIONS
{
    LONG Count;
    NTSTATUS Status;
    KEVENT Done;
} PNP_COMPLETIONS, *PPNP_COMPLETIONS;

static
NTSTATUS
NTAPI
CountPnpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PPNP_COMPLETIONS Completions = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    InterlockedIncrement(&Completions->Count);
    Completions->Status = Irp->IoStatus.Status;
    return STATUS_CONTINUE_COMPLETION;
}

static
VOID
SendPnpRequest(
    _In_ PDEVICE_OBJECT Device,
    _In_ UCHAR Minor,
    _In_opt_ PDEVICE_CAPABILITIES Capabilities,
    _Out_ PULONG Count,
    _Out_ PNTSTATUS CompletionStatus,
    _Out_ PBOOLEAN TimedOut)
{
    PNP_COMPLETIONS Completions;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Timeout;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;
    PIRP Irp;

    Completions.Count = 0;
    Completions.Status = STATUS_UNSUCCESSFUL;
    KeInitializeEvent(&Completions.Done, NotificationEvent, FALSE);
    RtlZeroMemory(&IoStatus, sizeof(IoStatus));

    *Count = 0;
    *CompletionStatus = STATUS_UNSUCCESSFUL;
    *TimedOut = FALSE;

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Device, NULL, 0, NULL, &Completions.Done, &IoStatus);
    if (Irp == NULL)
    {
        *CompletionStatus = STATUS_INSUFFICIENT_RESOURCES;
        return;
    }

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = Minor;
    if (Capabilities != NULL) Stack->Parameters.DeviceCapabilities.Capabilities = Capabilities;
    IoSetCompletionRoutine(Irp, CountPnpCompletion, &Completions, TRUE, TRUE, TRUE);

    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
    {
        Timeout.QuadPart = RAMDISK_TEST_TIMEOUT;
        Status = KeWaitForSingleObject(&Completions.Done, Executive, KernelMode, FALSE, &Timeout);
        if (Status == STATUS_TIMEOUT)
        {
            *Count = (ULONG)Completions.Count;
            *TimedOut = TRUE;
            return;
        }
    }

    *Count = (ULONG)Completions.Count;
    *CompletionStatus = Completions.Status;
}

static
PDEVICE_OBJECT
FindRamdiskDrive(VOID)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\RamDisk");
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT *List, Drive = NULL;
    NTSTATUS Status;
    ULONG Count = 0, Index;

    Status = ObReferenceObjectByName(&DriverName,
                                     OBJ_CASE_INSENSITIVE,
                                     NULL,
                                     0,
                                     IoDriverObjectType,
                                     KernelMode,
                                     NULL,
                                     (PVOID *)&DriverObject);
    if (!NT_SUCCESS(Status)) return NULL;

    IoEnumerateDeviceObjectList(DriverObject, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool,
                                 (Count + 16) * sizeof(PDEVICE_OBJECT),
                                 'PmdR');
    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(DriverObject,
                                             List,
                                             (Count + 16) * sizeof(PDEVICE_OBJECT),
                                             &Count);

        for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
        {
            if (Drive == NULL &&
                (List[Index]->DeviceType == FILE_DEVICE_DISK ||
                 List[Index]->DeviceType == FILE_DEVICE_CD_ROM))
            {
                Drive = List[Index];
            }
            else
            {
                ObDereferenceObject(List[Index]);
            }
        }

        ExFreePoolWithTag(List, 'PmdR');
    }

    ObDereferenceObject(DriverObject);
    return Drive;
}

START_TEST(IoRamdiskPnp)
{
    DEVICE_CAPABILITIES Capabilities;
    PDEVICE_OBJECT Drive;
    ULONG Count;
    NTSTATUS CompletionStatus;
    BOOLEAN TimedOut;

    Drive = FindRamdiskDrive();
    if (skip(Drive != NULL, "the RAM disk bus reported no disk\n")) return;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Capabilities.Size = sizeof(Capabilities);
    Capabilities.Version = 1;

    SendPnpRequest(Drive,
                   IRP_MN_QUERY_CAPABILITIES,
                   &Capabilities,
                   &Count,
                   &CompletionStatus,
                   &TimedOut);
    ok(!TimedOut, "IRP_MN_QUERY_CAPABILITIES did not complete\n");
    ok_eq_ulong(Count, 1);
    ok_eq_hex(CompletionStatus, STATUS_SUCCESS);
    ok_eq_ulong(Capabilities.UniqueID, TRUE);
    ok_eq_ulong(Capabilities.SilentInstall, TRUE);
    ok_eq_ulong(Capabilities.RawDeviceOK, TRUE);
    ok_eq_ulong(Capabilities.NoDisplayInUI, TRUE);

    SendPnpRequest(Drive,
                   IRP_MN_START_DEVICE,
                   NULL,
                   &Count,
                   &CompletionStatus,
                   &TimedOut);
    ok(!TimedOut, "IRP_MN_START_DEVICE did not complete\n");
    ok_eq_ulong(Count, 1);
    ok(NT_SUCCESS(CompletionStatus), "IRP_MN_START_DEVICE completed with 0x%lx\n", CompletionStatus);

    ObDereferenceObject(Drive);
}
