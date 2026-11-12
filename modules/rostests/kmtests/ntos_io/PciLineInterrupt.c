/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A PCI function's line interrupt requirement is the line its pin is routed to
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static UNICODE_STRING PciDriverName = RTL_CONSTANT_STRING(L"\\Driver\\pci");

static
NTSTATUS
QueryPnp(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ UCHAR MinorFunction,
    _Out_ PVOID *Information)
{
    PDEVICE_OBJECT Top;
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    *Information = NULL;
    Top = IoGetAttachedDeviceReference(Pdo);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Top, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
    {
        ObDereferenceObject(Top);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = MinorFunction;

    Status = IoCallDriver(Top, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    ObDereferenceObject(Top);
    if (NT_SUCCESS(Status))
        *Information = (PVOID)IoStatus.Information;
    return Status;
}

static
BOOLEAN
BootLine(
    _In_opt_ PCM_RESOURCE_LIST Boot,
    _Out_ PULONG Line)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;

    *Line = 0;
    if (Boot == NULL || Boot->Count == 0)
        return FALSE;

    for (Index = 0; Index < Boot->List[0].PartialResourceList.Count; Index++)
    {
        Descriptor = &Boot->List[0].PartialResourceList.PartialDescriptors[Index];
        if (Descriptor->Type == CmResourceTypeInterrupt &&
            !(Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            *Line = Descriptor->u.Interrupt.Level;
            return TRUE;
        }
    }

    return FALSE;
}

static
ULONG
CheckRequirements(
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST Requirements,
    _In_ ULONG Line)
{
    PIO_RESOURCE_LIST List = &Requirements->List[0];
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Alternative;
    ULONG Index;
    ULONG Checked = 0;

    for (Alternative = 0; Alternative < Requirements->AlternativeLists; Alternative++)
    {
        for (Index = 0; Index < List->Count; Index++)
        {
            Descriptor = &List->Descriptors[Index];
            if (Descriptor->Type != CmResourceTypeInterrupt ||
                (Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                continue;
            }

            ok(Descriptor->u.Interrupt.MinimumVector == Line &&
               Descriptor->u.Interrupt.MaximumVector == Line,
               "slot 0x%lx list %lu: line interrupt range 0x%lx-0x%lx, expected the routed line 0x%lx only\n",
               Requirements->SlotNumber,
               Alternative,
               Descriptor->u.Interrupt.MinimumVector,
               Descriptor->u.Interrupt.MaximumVector,
               Line);
            Checked++;
        }

        List = (PIO_RESOURCE_LIST)&List->Descriptors[List->Count];
    }

    return Checked;
}

START_TEST(PciLineInterrupt)
{
    PDRIVER_OBJECT PciDriver;
    PDEVICE_OBJECT *Devices = NULL;
    PIO_RESOURCE_REQUIREMENTS_LIST Requirements;
    PCM_RESOURCE_LIST Boot;
    ULONG DeviceCount = 0;
    ULONG Routed = 0;
    ULONG Checked = 0;
    ULONG Index;
    ULONG Line;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&PciDriverName, OBJ_CASE_INSENSITIVE, NULL, 0,
                                     IoDriverObjectType, KernelMode, NULL, (PVOID *)&PciDriver);
    if (skip(NT_SUCCESS(Status), "no PCI bus driver: 0x%08lx\n", Status))
        return;

    Status = IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &DeviceCount);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "sizing the device list: 0x%08lx\n", Status);
    if (DeviceCount != 0)
    {
        Devices = ExAllocatePoolZero(NonPagedPool, DeviceCount * sizeof(*Devices), 'tlLP');
        ok(Devices != NULL, "no memory for %lu devices\n", DeviceCount);
    }

    if (Devices != NULL)
    {
        Status = IoEnumerateDeviceObjectList(PciDriver, Devices, DeviceCount * sizeof(*Devices), &DeviceCount);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            DeviceCount = 0;

        for (Index = 0; Index < DeviceCount; Index++)
        {
            if (Devices[Index]->Flags & DO_BUS_ENUMERATED_DEVICE)
            {
                Boot = NULL;
                Requirements = NULL;
                if (NT_SUCCESS(QueryPnp(Devices[Index], IRP_MN_QUERY_RESOURCES, (PVOID *)&Boot)) &&
                    BootLine(Boot, &Line) &&
                    NT_SUCCESS(QueryPnp(Devices[Index], IRP_MN_QUERY_RESOURCE_REQUIREMENTS, (PVOID *)&Requirements)) &&
                    Requirements != NULL)
                {
                    Routed++;
                    Checked += CheckRequirements(Requirements, Line);
                }

                if (Requirements != NULL)
                    ExFreePool(Requirements);
                if (Boot != NULL)
                    ExFreePool(Boot);
            }

            ObDereferenceObject(Devices[Index]);
        }

        ExFreePoolWithTag(Devices, 'tlLP');
    }

    ObDereferenceObject(PciDriver);

    trace("%lu PCI functions with a routed line, %lu line interrupt requirements checked\n", Routed, Checked);
    if (skip(Routed != 0, "no PCI function reports a routed line interrupt\n"))
        return;
    ok(Checked != 0, "no PCI function with a routed line asks for a line interrupt\n");
}
