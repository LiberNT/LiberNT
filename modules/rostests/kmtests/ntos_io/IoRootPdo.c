/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the root enumerator's devices answer to the PnP requests a function driver sends them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <kmt_test.h>

static
NTSTATUS
NTAPI
RootPdoCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
RootPdoSend(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ UCHAR Minor,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    *Information = 0;
    Irp = IoAllocateIrp(Pdo->StackSize, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = Minor;
    IoSetCompletionRoutine(Irp, RootPdoCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(Pdo, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;
    *Information = Irp->IoStatus.Information;
    IoFreeIrp(Irp);
    return Status;
}

static
BOOLEAN
RootPdoIsRegistryDevice(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PBOOLEAN HasBootConfiguration)
{
    UCHAR Buffer[512];
    ULONG Length = 0;
    NTSTATUS Status;

    *HasBootConfiguration = FALSE;
    /* A device a driver reported has no hardware IDs of its own; an installed root device has them in its key */
    Status = IoGetDeviceProperty(Pdo, DevicePropertyHardwareID, sizeof(Buffer), Buffer, &Length);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Length = 0;
    Status = IoGetDeviceProperty(Pdo, DevicePropertyBootConfiguration, sizeof(Buffer), Buffer, &Length);
    *HasBootConfiguration = (BOOLEAN)(NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL);
    return TRUE;
}

START_TEST(IoRootPdo)
{
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(L"\\Driver\\PnpManager");
    PDRIVER_OBJECT Root;
    PDEVICE_OBJECT Device;
    ULONG_PTR First, Second;
    BOOLEAN HasBootConfiguration;
    ULONG Plain = 0, Configured = 0;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&RootName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&Root);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "the root enumerator's driver object is not available\n"))
        return;

    for (Device = Root->DeviceObject; Device != NULL; Device = Device->NextDevice)
    {
        if (!RootPdoIsRegistryDevice(Device, &HasBootConfiguration))
            continue;

        {
            ULONG Value = 0xA5A5A5A5, Length = 0;

            /* The root enumerator answers no bus information, so Windows 11 has no bus type or number to report */
            Status = IoGetDeviceProperty(Device, DevicePropertyLegacyBusType, sizeof(Value), &Value, &Length);
            ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
            Status = IoGetDeviceProperty(Device, DevicePropertyBusNumber, sizeof(Value), &Value, &Length);
            ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        }

        if (!HasBootConfiguration)
        {
            /* Windows 11 answers both with success and no list for an installed root device that has none */
            if (Plain++ >= 4)
                continue;
            Status = RootPdoSend(Device, IRP_MN_QUERY_RESOURCES, &First);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok(First == 0, "a root device without a boot configuration reported resources %p\n", (PVOID)First);
            if (NT_SUCCESS(Status) && First != 0)
                ExFreePool((PVOID)First);

            Status = RootPdoSend(Device, IRP_MN_QUERY_RESOURCE_REQUIREMENTS, &First);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok(First == 0, "a root device without a boot configuration reported requirements %p\n", (PVOID)First);
            if (NT_SUCCESS(Status) && First != 0)
                ExFreePool((PVOID)First);
            continue;
        }

        /* Each answer belongs to the requester, so two answers are two lists the requester frees */
        Configured++;
        Status = RootPdoSend(Device, IRP_MN_QUERY_RESOURCES, &First);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = RootPdoSend(Device, IRP_MN_QUERY_RESOURCES, &Second);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(First == 0 || First != Second, "two resource answers share the list %p\n", (PVOID)First);
        if (First != 0 && First != Second)
            ExFreePool((PVOID)First);
        if (Second != 0)
            ExFreePool((PVOID)Second);
    }

    ok(Plain != 0, "no installed root device without a boot configuration was found\n");
    trace("%lu root devices without and %lu with a boot configuration\n", Plain, Configured);
    ObDereferenceObject(Root);
}
