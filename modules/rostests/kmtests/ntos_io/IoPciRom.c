/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Expansion ROM reads through the PCI bus interface of a display adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

static
PDEVICE_OBJECT
FindDisplayStackTop(VOID)
{
    UNICODE_STRING PciName = RTL_CONSTANT_STRING(L"\\Driver\\PCI");
    PDEVICE_OBJECT *List, Top, Found = NULL;
    PDRIVER_OBJECT PciDriver;
    ULONG Count = 0, Index, Length;
    WCHAR ClassName[32];
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&PciName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&PciDriver);
    if (!NT_SUCCESS(Status))
        return NULL;

    IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'mRpK');
    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(PciDriver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count);
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
        {
            if (Found == NULL && (List[Index]->Flags & DO_BUS_ENUMERATED_DEVICE) != 0 &&
                NT_SUCCESS(IoGetDeviceProperty(List[Index], DevicePropertyClassName, sizeof(ClassName), ClassName, &Length)) &&
                _wcsicmp(ClassName, L"Display") == 0)
            {
                Top = IoGetAttachedDeviceReference(List[Index]);
                if (Top->DriverObject != PciDriver)
                    Found = Top;
                else
                    ObDereferenceObject(Top);
            }
            ObDereferenceObject(List[Index]);
        }
        ExFreePoolWithTag(List, 'mRpK');
    }

    ObDereferenceObject(PciDriver);
    return Found;
}

static
NTSTATUS
QueryBusInterface(
    _In_ PDEVICE_OBJECT Target,
    _Out_ PBUS_INTERFACE_STANDARD Interface)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    RtlZeroMemory(Interface, sizeof(*Interface));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Target, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(*Interface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(Target, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

START_TEST(IoPciRom)
{
    BUS_INTERFACE_STANDARD Bus;
    PDEVICE_OBJECT Target;
    UCHAR Rom[16];
    USHORT CommandBefore = 0, CommandAfter = 0;
    ULONG RomBarBefore = 0, RomBarAfter = 0;
    ULONG Read;
    NTSTATUS Status;

    Target = FindDisplayStackTop();
    if (skip(Target != NULL, "no display adapter on PCI\n"))
        return;

    Status = QueryBusInterface(Target, &Bus);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && Bus.GetBusData != NULL && Bus.SetBusData != NULL, "no bus interface\n"))
    {
        ObDereferenceObject(Target);
        return;
    }

    ok_eq_ulong(Bus.GetBusData(Bus.Context, PCI_WHICHSPACE_CONFIG, &CommandBefore,
                               FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(CommandBefore)),
                (ULONG)sizeof(CommandBefore));
    ok_eq_ulong(Bus.GetBusData(Bus.Context, PCI_WHICHSPACE_CONFIG, &RomBarBefore,
                               FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.ROMBaseAddress), sizeof(RomBarBefore)),
                (ULONG)sizeof(RomBarBefore));

    RtlFillMemory(Rom, sizeof(Rom), 0xCC);
    Read = Bus.GetBusData(Bus.Context, PCI_WHICHSPACE_ROM, Rom, 0, sizeof(Rom));
    ok_eq_ulong(Read, (ULONG)sizeof(Rom));
    ok(Rom[0] == 0x55 && Rom[1] == 0xAA, "ROM signature %02x %02x\n", Rom[0], Rom[1]);

    ok_eq_ulong(Bus.GetBusData(Bus.Context, PCI_WHICHSPACE_ROM, Rom, 0x7FFFFFF0, sizeof(Rom)), 0UL);
    ok_eq_ulong(Bus.SetBusData(Bus.Context, PCI_WHICHSPACE_ROM, Rom, 0, sizeof(Rom)), 0UL);

    ok_eq_ulong(Bus.GetBusData(Bus.Context, PCI_WHICHSPACE_CONFIG, &CommandAfter,
                               FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(CommandAfter)),
                (ULONG)sizeof(CommandAfter));
    ok_eq_ulong(Bus.GetBusData(Bus.Context, PCI_WHICHSPACE_CONFIG, &RomBarAfter,
                               FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.ROMBaseAddress), sizeof(RomBarAfter)),
                (ULONG)sizeof(RomBarAfter));
    ok_eq_hex(CommandAfter, CommandBefore);
    ok_eq_hex(RomBarAfter, RomBarBefore);

    if (Bus.InterfaceDereference != NULL)
        Bus.InterfaceDereference(Bus.Context);
    ObDereferenceObject(Target);
}
