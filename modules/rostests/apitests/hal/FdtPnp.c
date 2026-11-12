/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unit tests for the HAL Plug and Play enumeration of device-tree devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <ndk/ntndk.h>
#include <winreg.h>
#include <wchar.h>
#include <libernt/fdtlib.h>

#define UNIT_TEST

typedef enum _BUS_QUERY_ID_TYPE
{
    BusQueryDeviceID = 0,
    BusQueryHardwareIDs = 1,
    BusQueryCompatibleIDs = 2,
    BusQueryInstanceID = 3,
    BusQueryDeviceSerialNumber = 4,
    BusQueryContainerID = 5
} BUS_QUERY_ID_TYPE;

typedef struct _DEVICE_OBJECT DEVICE_OBJECT, *PDEVICE_OBJECT;

typedef struct _INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PVOID InterfaceReference;
    PVOID InterfaceDereference;
} INTERFACE, *PINTERFACE;

#define PagedPool 1
#define PLUGPLAY_REGKEY_DEVICE 1

static
PVOID
ExAllocatePoolZero(ULONG PoolType, SIZE_T NumberOfBytes, ULONG Tag)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NumberOfBytes);
}

static ULONG MockDeviceTreeWrites;
static ULONG MockDeviceTreeWriteSize;

static
NTSTATUS
MockZwOpenKey(PHANDLE Key, ACCESS_MASK Access, POBJECT_ATTRIBUTES Attributes)
{
    *Key = (HANDLE)(ULONG_PTR)0x1234;
    return STATUS_SUCCESS;
}

static
NTSTATUS
MockZwCreateKey(PHANDLE Key, ACCESS_MASK Access, POBJECT_ATTRIBUTES Attributes, ULONG TitleIndex,
                PUNICODE_STRING Class, ULONG Options, PULONG Disposition)
{
    *Key = (HANDLE)(ULONG_PTR)0x1235;
    return STATUS_SUCCESS;
}

static
NTSTATUS
MockZwSetValueKey(HANDLE Key, PUNICODE_STRING Name, ULONG TitleIndex, ULONG Type, PVOID Data, ULONG Size)
{
    if (Name && Name->Length == sizeof(L"DeviceTree") - sizeof(WCHAR) &&
        !memcmp(Name->Buffer, L"DeviceTree", Name->Length))
    {
        MockDeviceTreeWrites++;
        MockDeviceTreeWriteSize = Size;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
MockZwClose(HANDLE Handle)
{
    return STATUS_SUCCESS;
}

static
NTSTATUS
MockIoOpenDeviceRegistryKey(PDEVICE_OBJECT DeviceObject, ULONG KeyType, ACCESS_MASK Access, PHANDLE Key)
{
    return STATUS_NOT_IMPLEMENTED;
}

#define ZwOpenKey MockZwOpenKey
#define ZwCreateKey MockZwCreateKey
#define ZwSetValueKey MockZwSetValueKey
#define ZwClose MockZwClose
#define IoOpenDeviceRegistryKey MockIoOpenDeviceRegistryKey

static const VOID *MockBlob;
static ULONG MockBlobSize;

BOOLEAN
NTAPI
HalpQueryDeviceTree(const VOID **Blob, PULONG Size)
{
    *Blob = MockBlob;
    *Size = MockBlobSize;
    return MockBlob != NULL;
}

BOOLEAN
NTAPI
HalpTranslateDeviceTreeInterrupt(const CHAR *Compatible, ULONG CompatibleLength, const ULONG *Cells,
                                 ULONG CellCount, PULONG Gsi, PBOOLEAN EdgeTriggered)
{
    if (!FdtStringListContains(Compatible, CompatibleLength, "test,aic") || CellCount != 3 || Cells[0] != 0)
        return FALSE;
    *Gsi = 32 + Cells[1];
    *EdgeTriggered = (Cells[2] & 3) != 0;
    return TRUE;
}

#include "../../../../hal/arch/common/generic/fdtpnp.c"

#include "fdtbuild.h"

static
VOID
BuildTree(VOID)
{
    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 2);
    FdtStrings("compatible", LIST("test,board"));
    FdtBeginNode("cpus");
    FdtStrings("compatible", LIST("test,cpus"));
    FdtEndNode();
    FdtBeginNode("memory@10000000000");
    FdtStrings("compatible", LIST("test,memory"));
    FdtCells("reg", 4, 0x100, 0, 2, 0);
    FdtEndNode();
    FdtBeginNode("chosen");
    FdtStrings("compatible", LIST("test,chosen"));
    FdtEndNode();
    FdtBeginNode("soc");
    FdtStrings("compatible", LIST("simple-bus"));
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 2);
    FdtProperty("ranges", NULL, 0);

    FdtBeginNode("interrupt-controller@28e100000");
    FdtStrings("compatible", LIST("test,aic"));
    FdtProperty("interrupt-controller", NULL, 0);
    FdtCells("#interrupt-cells", 1, 3);
    FdtCells("reg", 4, 2, 0x8e100000, 0, 0x10000);
    FdtCells("phandle", 1, 1);
    FdtEndNode();

    FdtBeginNode("usb@702280000");
    FdtStrings("compatible", LIST("apple,t6030-dwc3\0apple,t8103-dwc3"));
    FdtCells("reg", 8, 7, 0x2280000, 0, 0xcd00, 7, 0x228cd00, 0, 0x3200);
    FdtCells("interrupt-parent", 1, 1);
    FdtCells("interrupts", 3, 0, 0x43f, 4);
    FdtEndNode();

    FdtBeginNode("fifo@37ab30000");
    FdtStrings("compatible", LIST("apple,t6030-dockchannel\0apple,dockchannel"));
    FdtCells("reg", 4, 3, 0x7ab14000, 0, 0x4000);
    FdtCells("interrupt-parent", 1, 1);
    FdtCells("interrupts", 3, 0, 0x3ab, 4);
    FdtProperty("interrupt-controller", NULL, 0);
    FdtCells("#interrupt-cells", 1, 2);
    FdtEndNode();

    FdtBeginNode("disabled@1000");
    FdtStrings("compatible", LIST("test,disabled"));
    FdtStrings("status", LIST("disabled"));
    FdtCells("reg", 4, 0, 0x1000, 0, 0x100);
    FdtEndNode();

    FdtBeginNode("timer");
    FdtStrings("compatible", LIST("arm,armv8-timer"));
    FdtEndNode();

    FdtBeginNode("gpio-keys");
    FdtStrings("compatible", LIST("gpio-keys"));
    FdtEndNode();

    FdtBeginNode("bus@200000000");
    FdtStrings("compatible", LIST("simple-bus"));
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtCells("ranges", 4, 0, 2, 0, 0x100000);
    FdtBeginNode("child@1000");
    FdtStrings("compatible", LIST("test,child"));
    FdtCells("reg", 2, 0x1000, 0x100);
    FdtCells("interrupts-extended", 4, 1, 0, 5, 1);
    FdtEndNode();
    FdtEndNode();

    FdtBeginNode("pcie@10000000");
    FdtStrings("compatible", LIST("pci-host-ecam-generic"));
    FdtStrings("device_type", LIST("pci"));
    FdtCells("reg", 4, 0x40, 0x10000000, 0, 0x10000000);
    FdtCells("bus-range", 2, 0, 0xff);
    FdtCells("#address-cells", 1, 3);
    FdtCells("#size-cells", 1, 2);
    FdtCells("ranges", 21,
             0x01000000, 0, 0, 0, 0x3eff0000, 0, 0x10000,
             0x02000000, 0, 0x10000000, 0, 0x10000000, 0, 0x2eff0000,
             0x43000000, 0x80, 0, 0x80, 0, 0x80, 0);
    FdtEndNode();

    FdtEndNode();
    FdtEndNode();
    FdtFinish();
}

static
ULONG
FindDevice(PHALP_FDT_DEVICE_NODE Devices, ULONG Count, const CHAR *Name)
{
    ULONG Index, Length;
    const CHAR *NodeName;

    for (Index = 0; Index < Count; ++Index)
    {
        NodeName = FdtNodeName(&HalpFdt, Devices[Index].Node, &Length);
        if (NodeName && Length == strlen(Name) && !memcmp(NodeName, Name, Length))
            return Index;
    }
    return MAXULONG;
}

static
VOID
TestIds(PHALP_FDT_DEVICE_NODE Device)
{
    static const WCHAR HardwareIds[] = L"FDT\\apple_t6030-dwc3\0FDT\\apple_t8103-dwc3\0";
    PWCHAR Id;
    NTSTATUS Status;

    Status = HalpFdtQueryId(Device->Node, Device->Parent, BusQueryDeviceID, &Id);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(!wcscmp(Id, L"FDT\\apple_t6030-dwc3"), "Device ID is '%ls'\n", Id);
        HeapFree(GetProcessHeap(), 0, Id);
    }

    Status = HalpFdtQueryId(Device->Node, Device->Parent, BusQueryHardwareIDs, &Id);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(!memcmp(Id, HardwareIds, sizeof(HardwareIds)), "Hardware IDs start with '%ls'\n", Id);
        HeapFree(GetProcessHeap(), 0, Id);
    }

    Status = HalpFdtQueryId(Device->Node, Device->Parent, BusQueryInstanceID, &Id);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(!wcscmp(Id, L"0000000702280000"), "Instance ID is '%ls'\n", Id);
        HeapFree(GetProcessHeap(), 0, Id);
    }

    Id = (PWCHAR)(ULONG_PTR)1;
    Status = HalpFdtQueryId(Device->Node, Device->Parent, BusQueryCompatibleIDs, &Id);
    ok_hex(Status, STATUS_NOT_SUPPORTED);
    ok(Id == NULL, "Compatible IDs are %p\n", Id);
}

static
VOID
TestUsbResources(PHALP_FDT_DEVICE_NODE Device)
{
    PCM_RESOURCE_LIST List;
    PIO_RESOURCE_REQUIREMENTS_LIST Requirements;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PIO_RESOURCE_DESCRIPTOR Requirement;
    NTSTATUS Status;

    Status = HalpFdtQueryResources(Device->Node, Device->Parent, &List);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_int(List->Count, 1);
    ok_int(List->List[0].InterfaceType, Internal);
    ok_int(List->List[0].PartialResourceList.Count, 3);
    Descriptor = List->List[0].PartialResourceList.PartialDescriptors;
    ok_int(Descriptor[0].Type, CmResourceTypeMemory);
    ok_int(Descriptor[0].ShareDisposition, CmResourceShareShared);
    ok_int(Descriptor[0].Flags, CM_RESOURCE_MEMORY_READ_WRITE);
    ok(Descriptor[0].u.Memory.Start.QuadPart == 0x702280000LL, "Memory 0 at %I64x\n", Descriptor[0].u.Memory.Start.QuadPart);
    ok_hex(Descriptor[0].u.Memory.Length, 0xcd00);
    ok_int(Descriptor[1].Type, CmResourceTypeMemory);
    ok(Descriptor[1].u.Memory.Start.QuadPart == 0x70228cd00LL, "Memory 1 at %I64x\n", Descriptor[1].u.Memory.Start.QuadPart);
    ok_hex(Descriptor[1].u.Memory.Length, 0x3200);
    ok_int(Descriptor[2].Type, CmResourceTypeInterrupt);
    ok_int(Descriptor[2].ShareDisposition, CmResourceShareShared);
    ok_int(Descriptor[2].Flags, CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE);
    ok_int(Descriptor[2].u.Interrupt.Level, 32 + 0x43f);
    ok_int(Descriptor[2].u.Interrupt.Vector, 32 + 0x43f);
    ok(Descriptor[2].u.Interrupt.Affinity == (KAFFINITY)-1, "Affinity %Ix\n", Descriptor[2].u.Interrupt.Affinity);
    HeapFree(GetProcessHeap(), 0, List);

    Status = HalpFdtQueryRequirements(Device->Node, Device->Parent, &Requirements);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_int(Requirements->InterfaceType, Internal);
    ok_int(Requirements->AlternativeLists, 1);
    ok_int(Requirements->List[0].Count, 3);
    ok_int(Requirements->ListSize, FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
                                   3 * sizeof(IO_RESOURCE_DESCRIPTOR));
    Requirement = Requirements->List[0].Descriptors;
    ok_int(Requirement[0].Type, CmResourceTypeMemory);
    ok(Requirement[0].u.Memory.MinimumAddress.QuadPart == 0x702280000LL, "Minimum %I64x\n",
       Requirement[0].u.Memory.MinimumAddress.QuadPart);
    ok(Requirement[0].u.Memory.MaximumAddress.QuadPart == 0x70228ccffLL, "Maximum %I64x\n",
       Requirement[0].u.Memory.MaximumAddress.QuadPart);
    ok_hex(Requirement[0].u.Memory.Length, 0xcd00);
    ok_int(Requirement[0].u.Memory.Alignment, 1);
    ok_int(Requirement[2].Type, CmResourceTypeInterrupt);
    ok_int(Requirement[2].u.Interrupt.MinimumVector, 32 + 0x43f);
    ok_int(Requirement[2].u.Interrupt.MaximumVector, 32 + 0x43f);
    HeapFree(GetProcessHeap(), 0, Requirements);
}

static
VOID
TestTranslatedChild(PHALP_FDT_DEVICE_NODE Device)
{
    PCM_RESOURCE_LIST List;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    Status = HalpFdtQueryResources(Device->Node, Device->Parent, &List);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_int(List->List[0].PartialResourceList.Count, 2);
    Descriptor = List->List[0].PartialResourceList.PartialDescriptors;
    ok_int(Descriptor[0].Type, CmResourceTypeMemory);
    ok(Descriptor[0].u.Memory.Start.QuadPart == 0x200001000LL, "Translated memory at %I64x\n",
       Descriptor[0].u.Memory.Start.QuadPart);
    ok_hex(Descriptor[0].u.Memory.Length, 0x100);
    ok_int(Descriptor[1].Type, CmResourceTypeInterrupt);
    ok_int(Descriptor[1].Flags, CM_RESOURCE_INTERRUPT_LATCHED);
    ok_int(Descriptor[1].u.Interrupt.Vector, 37);
    HeapFree(GetProcessHeap(), 0, List);
}

static
VOID
TestPciHost(PHALP_FDT_DEVICE_NODE Device)
{
    PCM_RESOURCE_LIST List;
    PIO_RESOURCE_REQUIREMENTS_LIST Requirements;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PIO_RESOURCE_DESCRIPTOR Requirement;
    PWCHAR Id;
    NTSTATUS Status;

    Status = HalpFdtQueryId(Device->Node, Device->Parent, BusQueryDeviceID, &Id);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(!wcscmp(Id, L"ACPI\\PNP0A03"), "Device ID is '%ls'\n", Id);
        HeapFree(GetProcessHeap(), 0, Id);
    }
    Status = HalpFdtQueryId(Device->Node, Device->Parent, BusQueryHardwareIDs, &Id);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(!memcmp(Id, L"*PNP0A03\0", sizeof(L"*PNP0A03\0")), "Hardware IDs start with '%ls'\n", Id);
        HeapFree(GetProcessHeap(), 0, Id);
    }

    Status = HalpFdtQueryResources(Device->Node, Device->Parent, &List);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_int(List->List[0].InterfaceType, PCIBus);
        ok_int(List->List[0].PartialResourceList.Count, 2);
        Descriptor = List->List[0].PartialResourceList.PartialDescriptors;
        ok_int(Descriptor[0].Type, CmResourceTypeBusNumber);
        ok_int(Descriptor[0].u.BusNumber.Start, 0);
        ok_int(Descriptor[0].u.BusNumber.Length, 256);
        ok_int(Descriptor[1].Type, CmResourceTypeMemory);
        ok_hex(Descriptor[1].Flags, CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_WINDOW_DECODE);
        ok(Descriptor[1].u.Memory.Start.QuadPart == 0x10000000, "Window at %I64x\n", Descriptor[1].u.Memory.Start.QuadPart);
        ok_hex(Descriptor[1].u.Memory.Length, 0x2eff0000);
        HeapFree(GetProcessHeap(), 0, List);
    }

    Status = HalpFdtQueryRequirements(Device->Node, Device->Parent, &Requirements);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_int(Requirements->InterfaceType, PCIBus);
        ok_int(Requirements->List[0].Count, 2);
        Requirement = Requirements->List[0].Descriptors;
        ok_int(Requirement[0].Type, CmResourceTypeBusNumber);
        ok_int(Requirement[0].u.BusNumber.MinBusNumber, 0);
        ok_int(Requirement[0].u.BusNumber.MaxBusNumber, 255);
        ok_int(Requirement[1].Type, CmResourceTypeMemory);
        ok(Requirement[1].u.Memory.MaximumAddress.QuadPart == 0x3efeffff, "Window ends at %I64x\n",
           Requirement[1].u.Memory.MaximumAddress.QuadPart);
        HeapFree(GetProcessHeap(), 0, Requirements);
    }
}

static
VOID
TestNoRegisterDevice(PHALP_FDT_DEVICE_NODE Device)
{
    PCM_RESOURCE_LIST List = (PCM_RESOURCE_LIST)(ULONG_PTR)1;
    PWCHAR Id;
    NTSTATUS Status;

    Status = HalpFdtQueryId(Device->Node, Device->Parent, BusQueryInstanceID, &Id);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(!wcscmp(Id, L"gpio-keys"), "Instance ID is '%ls'\n", Id);
        HeapFree(GetProcessHeap(), 0, Id);
    }
    Status = HalpFdtQueryDeviceText(Device->Node, &Id);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(!wcscmp(Id, L"gpio-keys"), "Device text is '%ls'\n", Id);
        HeapFree(GetProcessHeap(), 0, Id);
    }
    Status = HalpFdtQueryResources(Device->Node, Device->Parent, &List);
    ok_hex(Status, STATUS_NOT_SUPPORTED);
    ok(List == NULL, "Resources are %p\n", List);
}

START_TEST(FdtPnp)
{
    HALP_FDT_DEVICE_NODE Devices[16];
    ULONG Count, Usb, Fifo, Child, Keys, Pci;

    BuildTree();
    MockBlob = Builder.Blob;
    MockBlobSize = Builder.BlobSize;
    ok(HalpFdtInitialize() == TRUE, "The device tree did not open\n");
    ok_int(MockDeviceTreeWrites, 1);
    ok_int(MockDeviceTreeWriteSize, Builder.BlobSize);

    Count = HalpFdtEnumerateDevices(Devices, RTL_NUMBER_OF(Devices));
    ok_int(Count, 5);
    Usb = FindDevice(Devices, Count, "usb@702280000");
    Fifo = FindDevice(Devices, Count, "fifo@37ab30000");
    Keys = FindDevice(Devices, Count, "gpio-keys");
    Child = FindDevice(Devices, Count, "child@1000");
    ok_int(Usb, 0);
    ok_int(Fifo, 1);
    ok_int(Keys, 2);
    ok_int(Child, 3);
    Pci = FindDevice(Devices, Count, "pcie@10000000");
    ok_int(Pci, 4);
    ok(FindDevice(Devices, Count, "interrupt-controller@28e100000") == MAXULONG, "The root interrupt controller was enumerated\n");
    ok(FindDevice(Devices, Count, "disabled@1000") == MAXULONG, "A disabled node was enumerated\n");
    ok(FindDevice(Devices, Count, "timer") == MAXULONG, "The architected timer was enumerated\n");
    ok(FindDevice(Devices, Count, "memory@10000000000") == MAXULONG, "The memory node was enumerated\n");

    ok_int(HalpFdtEnumerateDevices(Devices, 2), 2);

    if (Usb < Count)
    {
        TestIds(&Devices[Usb]);
        TestUsbResources(&Devices[Usb]);
    }
    if (Child < Count)
        TestTranslatedChild(&Devices[Child]);
    if (Keys < Count)
        TestNoRegisterDevice(&Devices[Keys]);
    if (Pci < Count)
        TestPciHost(&Devices[Pci]);
}
