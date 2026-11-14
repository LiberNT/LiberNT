/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Plug and Play queries a PCI bus FDO passes to the device below it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

static CONST GUID UnknownInterfaceGuid =
    { 0x6f1d3c2a, 0x8b4e, 0x4f7a, { 0x9c, 0x5d, 0x2e, 0x8b, 0x7a, 0x1f, 0x3c, 0x90 } };

static
NTSTATUS
SendPnpIrp(
    _In_ PDEVICE_OBJECT Target,
    _In_ PIO_STACK_LOCATION Parameters,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    *Information = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Target, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = Parameters->MinorFunction;
    Stack->Parameters = Parameters->Parameters;

    Status = IoCallDriver(Target, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    *Information = IoStatus.Information;
    return Status;
}

static
NTSTATUS
QueryInterface(
    _In_ PDEVICE_OBJECT Target,
    _In_ CONST GUID *Type,
    _Out_ PBUS_INTERFACE_STANDARD Interface)
{
    IO_STACK_LOCATION Parameters;
    ULONG_PTR Information;

    RtlZeroMemory(Interface, sizeof(*Interface));
    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.MinorFunction = IRP_MN_QUERY_INTERFACE;
    Parameters.Parameters.QueryInterface.InterfaceType = Type;
    Parameters.Parameters.QueryInterface.Size = sizeof(*Interface);
    Parameters.Parameters.QueryInterface.Version = 1;
    Parameters.Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    return SendPnpIrp(Target, &Parameters, &Information);
}

static
VOID
TestQueryInterface(
    _In_ ULONG Index,
    _In_ PDEVICE_OBJECT Top,
    _In_ PDEVICE_OBJECT Lower)
{
    BUS_INTERFACE_STANDARD ViaFdo, ViaLower;
    NTSTATUS FdoStatus, LowerStatus;

    FdoStatus = QueryInterface(Top, &UnknownInterfaceGuid, &ViaFdo);
    ok(FdoStatus == STATUS_NOT_SUPPORTED, "bus %lu: unknown interface 0x%lx\n", Index, FdoStatus);
    ok(ViaFdo.Size == 0 && ViaFdo.Context == NULL, "bus %lu: unknown interface filled in size %u\n", Index, ViaFdo.Size);

    FdoStatus = QueryInterface(Top, &GUID_BUS_INTERFACE_STANDARD, &ViaFdo);
    LowerStatus = QueryInterface(Lower, &GUID_BUS_INTERFACE_STANDARD, &ViaLower);
    ok(FdoStatus == LowerStatus, "bus %lu: bus interface 0x%lx through the FDO, 0x%lx below it\n", Index, FdoStatus, LowerStatus);
    if (NT_SUCCESS(FdoStatus) && NT_SUCCESS(LowerStatus))
    {
        ok(ViaFdo.GetDmaAdapter != NULL, "bus %lu: no GetDmaAdapter\n", Index);
        ok(ViaFdo.Size == ViaLower.Size && ViaFdo.Version == ViaLower.Version,
           "bus %lu: bus interface size %u version %u through the FDO, size %u version %u below it\n",
           Index, ViaFdo.Size, ViaFdo.Version, ViaLower.Size, ViaLower.Version);
        ok(ViaFdo.Context == ViaLower.Context, "bus %lu: bus interface context %p through the FDO, %p below it\n",
           Index, ViaFdo.Context, ViaLower.Context);
        ok(ViaFdo.GetDmaAdapter == ViaLower.GetDmaAdapter && ViaFdo.GetBusData == ViaLower.GetBusData &&
           ViaFdo.SetBusData == ViaLower.SetBusData && ViaFdo.TranslateBusAddress == ViaLower.TranslateBusAddress,
           "bus %lu: bus interface routines differ between the FDO and the device below it\n", Index);
    }
    if (NT_SUCCESS(FdoStatus) && ViaFdo.InterfaceDereference != NULL)
        ViaFdo.InterfaceDereference(ViaFdo.Context);
    if (NT_SUCCESS(LowerStatus) && ViaLower.InterfaceDereference != NULL)
        ViaLower.InterfaceDereference(ViaLower.Context);
}

static
VOID
TestQueryResources(
    _In_ ULONG Index,
    _In_ PDEVICE_OBJECT Top,
    _In_ PDEVICE_OBJECT Lower)
{
    PCM_RESOURCE_LIST ViaFdo, ViaLower;
    IO_STACK_LOCATION Parameters;
    NTSTATUS FdoStatus, LowerStatus;
    ULONG_PTR FdoInfo, LowerInfo;
    SIZE_T Length;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.MinorFunction = IRP_MN_QUERY_RESOURCES;
    FdoStatus = SendPnpIrp(Top, &Parameters, &FdoInfo);
    LowerStatus = SendPnpIrp(Lower, &Parameters, &LowerInfo);
    ViaFdo = NT_SUCCESS(FdoStatus) ? (PCM_RESOURCE_LIST)FdoInfo : NULL;
    ViaLower = NT_SUCCESS(LowerStatus) ? (PCM_RESOURCE_LIST)LowerInfo : NULL;

    ok(FdoStatus == LowerStatus, "bus %lu: resources 0x%lx through the FDO, 0x%lx below it\n", Index, FdoStatus, LowerStatus);
    ok((ViaFdo == NULL) == (ViaLower == NULL), "bus %lu: resource list %p through the FDO, %p below it\n", Index, ViaFdo, ViaLower);
    if (ViaFdo != NULL && ViaLower != NULL)
    {
        ok(ViaFdo->Count == ViaLower->Count, "bus %lu: %lu resource lists through the FDO, %lu below it\n",
           Index, ViaFdo->Count, ViaLower->Count);
        if (ViaFdo->Count != 0 && ViaLower->Count != 0 &&
            ViaFdo->List[0].PartialResourceList.Count == ViaLower->List[0].PartialResourceList.Count)
        {
            Length = ViaFdo->List[0].PartialResourceList.Count * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
            ok(RtlCompareMemory(ViaFdo->List[0].PartialResourceList.PartialDescriptors,
                                ViaLower->List[0].PartialResourceList.PartialDescriptors, Length) == Length,
               "bus %lu: resources differ between the FDO and the device below it\n", Index);
        }
        else if (ViaFdo->Count != 0 && ViaLower->Count != 0)
        {
            ok(0, "bus %lu: %lu resources through the FDO, %lu below it\n", Index,
               ViaFdo->List[0].PartialResourceList.Count, ViaLower->List[0].PartialResourceList.Count);
        }
    }
    if (ViaFdo != NULL)
        ExFreePool(ViaFdo);
    if (ViaLower != NULL)
        ExFreePool(ViaLower);
}

static
VOID
TestQueryResourceRequirements(
    _In_ ULONG Index,
    _In_ PDEVICE_OBJECT Top,
    _In_ PDEVICE_OBJECT Lower)
{
    PIO_RESOURCE_REQUIREMENTS_LIST ViaFdo, ViaLower;
    IO_STACK_LOCATION Parameters;
    NTSTATUS FdoStatus, LowerStatus;
    ULONG_PTR FdoInfo, LowerInfo;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.MinorFunction = IRP_MN_QUERY_RESOURCE_REQUIREMENTS;
    FdoStatus = SendPnpIrp(Top, &Parameters, &FdoInfo);
    LowerStatus = SendPnpIrp(Lower, &Parameters, &LowerInfo);
    ViaFdo = NT_SUCCESS(FdoStatus) ? (PIO_RESOURCE_REQUIREMENTS_LIST)FdoInfo : NULL;
    ViaLower = NT_SUCCESS(LowerStatus) ? (PIO_RESOURCE_REQUIREMENTS_LIST)LowerInfo : NULL;

    ok(FdoStatus == LowerStatus, "bus %lu: requirements 0x%lx through the FDO, 0x%lx below it\n", Index, FdoStatus, LowerStatus);
    ok((ViaFdo == NULL) == (ViaLower == NULL), "bus %lu: requirements %p through the FDO, %p below it\n", Index, ViaFdo, ViaLower);
    if (ViaFdo != NULL && ViaLower != NULL)
    {
        ok(ViaFdo->ListSize == ViaLower->ListSize, "bus %lu: requirements of %lu bytes through the FDO, %lu below it\n",
           Index, ViaFdo->ListSize, ViaLower->ListSize);
        if (ViaFdo->ListSize == ViaLower->ListSize)
        {
            ok(RtlCompareMemory(ViaFdo, ViaLower, ViaFdo->ListSize) == ViaFdo->ListSize,
               "bus %lu: requirements differ between the FDO and the device below it\n", Index);
        }
    }
    if (ViaFdo != NULL)
        ExFreePool(ViaFdo);
    if (ViaLower != NULL)
        ExFreePool(ViaLower);
}

START_TEST(IoPciBusFdo)
{
    UNICODE_STRING PciName = RTL_CONSTANT_STRING(L"\\Driver\\PCI");
    PDEVICE_OBJECT *List, Lower, Top;
    PDRIVER_OBJECT PciDriver;
    ULONG Count = 0, Index, Buses = 0;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&PciName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&PciDriver);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'FbpK');
    ok(List != NULL, "no memory for %lu device objects\n", Count);
    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(PciDriver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count);
        ok_eq_hex(Status, STATUS_SUCCESS);
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
        {
            Lower = IoGetLowerDeviceObject(List[Index]);
            if (Lower != NULL)
            {
                Top = IoGetAttachedDeviceReference(List[Index]);
                TestQueryInterface(Buses, Top, Lower);
                TestQueryResources(Buses, Top, Lower);
                TestQueryResourceRequirements(Buses, Top, Lower);
                ObDereferenceObject(Top);
                ObDereferenceObject(Lower);
                Buses++;
            }
            ObDereferenceObject(List[Index]);
        }
        ExFreePoolWithTag(List, 'FbpK');
    }
    ObDereferenceObject(PciDriver);

    trace("%lu PCI bus FDOs\n", Buses);
    ok(Buses != 0, "no PCI bus FDO\n");
}
