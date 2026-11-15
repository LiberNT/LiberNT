/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a PCI bridge asks for exactly the prefetchable window its registers decode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
NTSTATUS
QueryRequirements(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST *List)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    *List = NULL;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Pdo, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_RESOURCE_REQUIREMENTS;
    Status = IoCallDriver(Pdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (NT_SUCCESS(Status))
        *List = (PIO_RESOURCE_REQUIREMENTS_LIST)IoStatus.Information;
    return Status;
}

static
VOID
CheckBridge(
    _In_ PDEVICE_OBJECT Pdo,
    _Inout_ PULONG Checked)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PCI_COMMON_CONFIG Config;
    PCI_SLOT_NUMBER Slot;
    ULONGLONG Base, Limit, Length, Alignment, Minimum, Maximum;
    ULONG Bus, Address, Size, Index;
    BOOLEAN Found = FALSE;

    if (!NT_SUCCESS(IoGetDeviceProperty(Pdo, DevicePropertyBusNumber, sizeof(Bus), &Bus, &Size)) ||
        !NT_SUCCESS(IoGetDeviceProperty(Pdo, DevicePropertyAddress, sizeof(Address), &Address, &Size)))
    {
        return;
    }

    Slot.u.AsULONG = 0;
    Slot.u.bits.DeviceNumber = Address >> 16;
    Slot.u.bits.FunctionNumber = Address & 0xFFFF;
    if (HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, &Config, 0, PCI_COMMON_HDR_LENGTH) != PCI_COMMON_HDR_LENGTH ||
        PCI_CONFIGURATION_TYPE(&Config) != PCI_BRIDGE_TYPE ||
        (Config.u.type1.PrefetchBase & 0xF) != 1 ||
        Config.u.type1.PrefetchLimit < Config.u.type1.PrefetchBase)
    {
        return;
    }

    Base = ((ULONGLONG)Config.u.type1.PrefetchBaseUpper32 << 32) | ((ULONGLONG)(Config.u.type1.PrefetchBase & 0xFFF0) << 16);
    Limit = ((ULONGLONG)Config.u.type1.PrefetchLimitUpper32 << 32) | ((ULONGLONG)(Config.u.type1.PrefetchLimit & 0xFFF0) << 16) | 0xFFFFF;
    if (Limit < Base)
        return;
    Length = Limit - Base + 1;

    if (!NT_SUCCESS(QueryRequirements(Pdo, &List)) || List == NULL)
    {
        ok(FALSE, "Bridge %lu:%lx.%lx: no resource requirements\n", Bus, Address >> 16, Address & 0xFFFF);
        return;
    }

    for (Index = 0; Index < List->List[0].Count; Index++)
    {
        PIO_RESOURCE_DESCRIPTOR Descriptor = &List->List[0].Descriptors[Index];

        if ((Descriptor->Type != CmResourceTypeMemory && Descriptor->Type != CmResourceTypeMemoryLarge) ||
            !(Descriptor->Flags & CM_RESOURCE_MEMORY_WINDOW_DECODE) ||
            !(Descriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ||
            Descriptor->Option != IO_RESOURCE_PREFERRED)
        {
            continue;
        }

        Found = TRUE;
        Alignment = Minimum = Maximum = 0;
        Size = 0;
        ok_eq_ulonglong(RtlIoDecodeMemIoResource(Descriptor, &Alignment, &Minimum, &Maximum), Length);
        ok_eq_ulonglong(Minimum, Base);
        ok_eq_ulonglong(Maximum, Limit);
        trace("Bridge %lu:%lx.%lx prefetchable window 0x%I64x-0x%I64x type %u flags 0x%x\n",
              Bus, Address >> 16, Address & 0xFFFF, Base, Limit, Descriptor->Type, Descriptor->Flags);
    }
    ok(Found, "Bridge %lu:%lx.%lx: no preferred prefetchable window for 0x%I64x-0x%I64x\n",
       Bus, Address >> 16, Address & 0xFFFF, Base, Limit);
    (*Checked)++;
    ExFreePool(List);
}

START_TEST(IoPciBridgeWindow)
{
    UNICODE_STRING PciName = RTL_CONSTANT_STRING(L"\\Driver\\PCI");
    PDEVICE_OBJECT *List;
    PDRIVER_OBJECT PciDriver;
    ULONG Count = 0, Index, Checked = 0;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&PciName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&PciDriver);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'wBpK');
    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(PciDriver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count);
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
        {
            if (List[Index]->Flags & DO_BUS_ENUMERATED_DEVICE)
                CheckBridge(List[Index], &Checked);
            ObDereferenceObject(List[Index]);
        }
        ExFreePoolWithTag(List, 'wBpK');
    }
    ObDereferenceObject(PciDriver);

    if (Checked == 0)
        skip(FALSE, "No PCI bridge with a 64-bit prefetchable window\n");
}
