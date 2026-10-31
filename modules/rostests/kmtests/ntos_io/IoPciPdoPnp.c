/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PnP IRPs a PCI PDO does not handle keep their incoming status
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
PDEVICE_OBJECT
FindPciPdo(VOID)
{
    UNICODE_STRING PciName = RTL_CONSTANT_STRING(L"\\Driver\\PCI");
    PDEVICE_OBJECT *List, Found = NULL;
    PDRIVER_OBJECT PciDriver;
    ULONG Count = 0, Index;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&PciName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&PciDriver);
    if (!NT_SUCCESS(Status))
        return NULL;

    IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'pPpK');
    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(PciDriver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count);
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
        {
            if (Found == NULL && (List[Index]->Flags & DO_BUS_ENUMERATED_DEVICE) != 0)
                Found = List[Index];
            else
                ObDereferenceObject(List[Index]);
        }
        ExFreePoolWithTag(List, 'pPpK');
    }

    ObDereferenceObject(PciDriver);
    return Found;
}

static
NTSTATUS
SendPnp(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ UCHAR Minor,
    _In_ NTSTATUS InitialStatus,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Pdo, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = InitialStatus;
    Irp->IoStatus.Information = 0;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = Minor;
    Status = IoCallDriver(Pdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    *Information = IoStatus.Information;
    return Status;
}

START_TEST(IoPciPdoPnp)
{
    PDEVICE_OBJECT Pdo;
    ULONG_PTR Information;
    NTSTATUS Status;

    Pdo = FindPciPdo();
    if (Pdo == NULL)
    {
        skip(FALSE, "no PCI PDO\n");
        return;
    }

    Status = SendPnp(Pdo, IRP_MN_DEVICE_ENUMERATED, STATUS_NOT_SUPPORTED, &Information);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    Status = SendPnp(Pdo, IRP_MN_DEVICE_ENUMERATED, STATUS_SUCCESS, &Information);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = SendPnp(Pdo, IRP_MN_FILTER_RESOURCE_REQUIREMENTS, STATUS_NOT_SUPPORTED, &Information);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_ulongptr(Information, 0);

    ObDereferenceObject(Pdo);
}
