/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCI Express root port and link quiescent interfaces of PCI functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

#define MAX_FUNCTIONS 64
#define NO_PORT_TYPE 0xFF

typedef struct _TEST_FUNCTION
{
    PDEVICE_OBJECT Top;
    BOOLEAN Started;
    BUS_INTERFACE_STANDARD Bus;
    ULONG BusNumber;
    ULONG Id;
    UCHAR HeaderType;
    UCHAR BaseClass;
    UCHAR SubClass;
    UCHAR PortType;
    UCHAR SecondaryBus;
} TEST_FUNCTION, *PTEST_FUNCTION;

static CONST GUID RootPortInterfaceGuid =
    { 0x83a7734a, 0x84c7, 0x4161, { 0x9a, 0x98, 0x60, 0x00, 0xed, 0x0c, 0x4a, 0x33 } };
static CONST GUID LinkQuiescentInterfaceGuid =
    { 0x146cd41c, 0xdae3, 0x4437, { 0x8a, 0xff, 0x2a, 0xf3, 0xf0, 0x38, 0x09, 0x9b } };

static TEST_FUNCTION Functions[MAX_FUNCTIONS];
static ULONG FunctionCount;

static
NTSTATUS
QueryInterface(
    _In_ PDEVICE_OBJECT Target,
    _In_ CONST GUID *Type,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _Out_writes_bytes_(Size) PVOID Interface)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    RtlZeroMemory(Interface, Size);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Target, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = Type;
    Stack->Parameters.QueryInterface.Size = Size;
    Stack->Parameters.QueryInterface.Version = Version;
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

static
UCHAR
ReadPortType(
    _In_ PTEST_FUNCTION Function,
    _In_ UCHAR CapabilityPointer)
{
    UCHAR Offset = CapabilityPointer;
    UCHAR Header[2];
    USHORT Capabilities;
    ULONG Hops;

    for (Hops = 0; Offset >= PCI_COMMON_HDR_LENGTH && Hops < 48; Hops++)
    {
        if (Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, Header, Offset, sizeof(Header)) != sizeof(Header))
            return NO_PORT_TYPE;
        if (Header[0] == PCI_CAPABILITY_ID_PCI_EXPRESS)
        {
            if (Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &Capabilities, Offset + 2, sizeof(Capabilities)) != sizeof(Capabilities))
                return NO_PORT_TYPE;
            return (UCHAR)((Capabilities >> 4) & 0xF);
        }
        Offset = Header[1] & ~3;
    }
    return NO_PORT_TYPE;
}

static
BOOLEAN
DescribeFunction(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PTEST_FUNCTION Function)
{
    PCI_COMMON_HEADER Header;
    ULONG Length;

    RtlZeroMemory(Function, sizeof(*Function));
    if (!NT_SUCCESS(IoGetDeviceProperty(Pdo, DevicePropertyBusNumber, sizeof(Function->BusNumber), &Function->BusNumber, &Length)))
        return FALSE;

    Function->Top = IoGetAttachedDeviceReference(Pdo);
    Function->Started = Function->Top != Pdo;
    if (!NT_SUCCESS(QueryInterface(Function->Top, &GUID_BUS_INTERFACE_STANDARD, sizeof(Function->Bus), 1, &Function->Bus)) ||
        Function->Bus.GetBusData == NULL ||
        Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &Header, 0, sizeof(Header)) != sizeof(Header))
    {
        ObDereferenceObject(Function->Top);
        return FALSE;
    }

    Function->Id = *(PULONG)&Header;
    Function->HeaderType = PCI_CONFIGURATION_TYPE(&Header);
    Function->BaseClass = Header.BaseClass;
    Function->SubClass = Header.SubClass;
    Function->SecondaryBus = Function->HeaderType == PCI_BRIDGE_TYPE ? Header.u.type1.SecondaryBus : 0;
    Function->PortType = (Header.Status & PCI_STATUS_CAPABILITIES_LIST) ?
        ReadPortType(Function, Function->HeaderType == PCI_BRIDGE_TYPE ? Header.u.type1.CapabilitiesPtr : Header.u.type0.CapabilitiesPtr) :
        NO_PORT_TYPE;
    return TRUE;
}

static
VOID
CollectFunctions(VOID)
{
    UNICODE_STRING PciName = RTL_CONSTANT_STRING(L"\\Driver\\PCI");
    PDEVICE_OBJECT *List;
    PDRIVER_OBJECT PciDriver;
    ULONG Count = 0, Index;
    NTSTATUS Status;

    FunctionCount = 0;
    Status = ObReferenceObjectByName(&PciName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&PciDriver);
    if (!NT_SUCCESS(Status))
        return;

    IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'xEpK');
    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(PciDriver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count);
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
        {
            if (FunctionCount < MAX_FUNCTIONS && (List[Index]->Flags & DO_BUS_ENUMERATED_DEVICE) != 0 &&
                DescribeFunction(List[Index], &Functions[FunctionCount]))
            {
                FunctionCount++;
            }
            ObDereferenceObject(List[Index]);
        }
        ExFreePoolWithTag(List, 'xEpK');
    }
    ObDereferenceObject(PciDriver);
}

static
PTEST_FUNCTION
RootPortAbove(
    _In_ PTEST_FUNCTION Function)
{
    ULONG Index;

    if (Function->HeaderType != PCI_DEVICE_TYPE ||
        (Function->PortType != PciExpressEndpoint && Function->PortType != PciExpressLegacyEndpoint))
    {
        return NULL;
    }
    for (Index = 0; Index < FunctionCount; Index++)
    {
        if (Functions[Index].HeaderType == PCI_BRIDGE_TYPE && Functions[Index].PortType == PciExpressRootPort &&
            Functions[Index].SecondaryBus == Function->BusNumber)
        {
            return &Functions[Index];
        }
    }
    return NULL;
}

static
BOOLEAN
IsVgaController(
    _In_ PTEST_FUNCTION Function)
{
    return (Function->BaseClass == PCI_CLASS_DISPLAY_CTLR && Function->SubClass == PCI_SUBCLASS_VID_VGA_CTLR) ||
           (Function->BaseClass == PCI_CLASS_PRE_20 && Function->SubClass == PCI_SUBCLASS_PRE_20_VGA);
}

static
VOID
TestRootPortInterface(
    _In_ PTEST_FUNCTION Function,
    _In_opt_ PTEST_FUNCTION RootPort)
{
    PCI_EXPRESS_ROOT_PORT_INTERFACE Interface;
    ULONG Value;
    NTSTATUS Status;

    Status = QueryInterface(Function->Top, &RootPortInterfaceGuid, sizeof(Interface), 1, &Interface);
    if (RootPort == NULL)
    {
        ok(!NT_SUCCESS(Status), "root port interface on bus %lu id %08lx without a root port: 0x%lx\n",
           Function->BusNumber, Function->Id, Status);
        return;
    }

    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_uint(Interface.Size, sizeof(Interface));
    ok_eq_uint(Interface.Version, 1);

    Value = 0;
    ok_eq_ulong(Interface.ReadConfigSpace(Interface.Context, &Value, 0, sizeof(Value)), (ULONG)sizeof(Value));
    ok_eq_hex(Value, RootPort->Id);
    Value = 0;
    ok_eq_ulong(Interface.ReadConfigSpace(Interface.Context, &Value, PCI_EXTENDED_CONFIG_LENGTH - 2, sizeof(Value)), 0UL);
    ok_eq_hex(Value, 0xFFFFFFFFUL);
    ok_eq_ulong(Interface.WriteConfigSpace(Interface.Context, &Value, PCI_EXTENDED_CONFIG_LENGTH - 2, sizeof(Value)), 0UL);
    Interface.InterfaceDereference(Interface.Context);

    Status = QueryInterface(Function->Top, &RootPortInterfaceGuid, sizeof(Interface), 2, &Interface);
    ok(!NT_SUCCESS(Status), "root port interface version 2: 0x%lx\n", Status);
    Status = QueryInterface(Function->Top, &RootPortInterfaceGuid, sizeof(INTERFACE), 1, &Interface);
    ok(!NT_SUCCESS(Status), "root port interface size %u: 0x%lx\n", (ULONG)sizeof(INTERFACE), Status);
}

static
VOID
TestLinkQuiescentInterface(
    _In_ PTEST_FUNCTION Function,
    _In_ BOOLEAN Expected)
{
    PCI_EXPRESS_LINK_QUIESCENT_INTERFACE Interface;
    ULONG BarBefore = 0, BarAfter = 0;
    NTSTATUS Status;

    Status = QueryInterface(Function->Top, &LinkQuiescentInterfaceGuid, sizeof(Interface), 1, &Interface);
    if (!Expected)
    {
        ok(!NT_SUCCESS(Status), "link quiescent interface on bus %lu id %08lx class %02x/%02x: 0x%lx\n",
           Function->BusNumber, Function->Id, Function->BaseClass, Function->SubClass, Status);
        return;
    }

    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_uint(Interface.Size, sizeof(Interface));
    ok_eq_uint(Interface.Version, 1);

    Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &BarBefore,
                             FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[0]), sizeof(BarBefore));
    Status = Interface.PciExpressEnterLinkQuiescentMode(Interface.Context);
    ok_eq_hex(Status, Function->Started ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(Interface.PciExpressEnterLinkQuiescentMode(Interface.Context), STATUS_UNSUCCESSFUL);
        ok_eq_hex(Interface.PciExpressExitLinkQuiescentMode(Interface.Context), STATUS_SUCCESS);
    }
    Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &BarAfter,
                             FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[0]), sizeof(BarAfter));
    ok_eq_hex(BarAfter, BarBefore);
    Interface.InterfaceDereference(Interface.Context);
}

START_TEST(IoPciExpressInterfaces)
{
    ULONG Index, Endpoints = 0, BelowRootPort = 0, Quiescent = 0;

    CollectFunctions();
    if (skip(FunctionCount != 0, "no PCI functions\n"))
        return;

    for (Index = 0; Index < FunctionCount; Index++)
    {
        PTEST_FUNCTION Function = &Functions[Index];
        PTEST_FUNCTION RootPort = RootPortAbove(Function);
        BOOLEAN QuiescentExpected = RootPort != NULL && IsVgaController(Function);

        if (Function->HeaderType == PCI_DEVICE_TYPE && Function->PortType != NO_PORT_TYPE)
        {
            Endpoints++;
            TestRootPortInterface(Function, RootPort);
            TestLinkQuiescentInterface(Function, QuiescentExpected);
        }
        if (RootPort != NULL)
            BelowRootPort++;
        if (QuiescentExpected)
            Quiescent++;
    }
    trace("%lu PCI Express functions, %lu below a root port, %lu with link quiescent\n", Endpoints, BelowRootPort, Quiescent);
    skip(BelowRootPort != 0, "no PCI Express endpoint below a root port\n");

    for (Index = 0; Index < FunctionCount; Index++)
    {
        if (Functions[Index].Bus.InterfaceDereference != NULL)
            Functions[Index].Bus.InterfaceDereference(Functions[Index].Bus.Context);
        ObDereferenceObject(Functions[Index].Top);
    }
}
