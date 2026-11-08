/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device reset interface of PCI functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

#define MAX_FUNCTIONS 64
#define TEST_DEVICE_ID 0x10541AF4UL
#define TEST_NVME_ID 0x00101B36UL

typedef struct _TEST_FUNCTION
{
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Top;
    BUS_INTERFACE_STANDARD Bus;
    ULONG Id;
} TEST_FUNCTION, *PTEST_FUNCTION;

typedef struct _RESET_COMPLETION
{
    KEVENT Event;
    NTSTATUS Status;
    LONG Calls;
} RESET_COMPLETION, *PRESET_COMPLETION;

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
BOOLEAN
DescribeFunction(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PTEST_FUNCTION Function)
{
    ULONG Id = 0;

    RtlZeroMemory(Function, sizeof(*Function));
    Function->Top = IoGetAttachedDeviceReference(Pdo);
    if (!NT_SUCCESS(QueryInterface(Function->Top, &GUID_BUS_INTERFACE_STANDARD, sizeof(Function->Bus), 1, &Function->Bus)) ||
        Function->Bus.GetBusData == NULL ||
        Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &Id, 0, sizeof(Id)) != sizeof(Id))
    {
        ObDereferenceObject(Function->Top);
        return FALSE;
    }

    ObReferenceObject(Pdo);
    Function->Pdo = Pdo;
    Function->Id = Id;
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
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'rDpK');
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
        ExFreePoolWithTag(List, 'rDpK');
    }
    ObDereferenceObject(PciDriver);
}

static
UCHAR
FindExpressCapability(
    _In_ PTEST_FUNCTION Function)
{
    UCHAR Offset = 0, Header[2];
    USHORT Status = 0;
    ULONG Iteration;

    if (Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &Status,
                                 FIELD_OFFSET(PCI_COMMON_HEADER, Status), sizeof(Status)) != sizeof(Status) ||
        !(Status & PCI_STATUS_CAPABILITIES_LIST) ||
        Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &Offset,
                                 FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.CapabilitiesPtr), sizeof(Offset)) != sizeof(Offset))
    {
        return 0;
    }
    for (Iteration = 0; Offset >= 0x40 && Iteration < 48; Iteration++)
    {
        if (Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, Header, Offset & ~3,
                                     sizeof(Header)) != sizeof(Header))
        {
            return 0;
        }
        if (Header[0] == PCI_CAPABILITY_ID_PCI_EXPRESS)
            return Offset & ~3;
        Offset = Header[1];
    }
    return 0;
}

static
BOOLEAN
HasFunctionLevelReset(
    _In_ PTEST_FUNCTION Function)
{
    PCI_EXPRESS_DEVICE_CAPABILITIES_REGISTER Capabilities;
    UCHAR Offset = FindExpressCapability(Function);

    Capabilities.AsULONG = 0;
    if (Offset == 0 ||
        Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &Capabilities.AsULONG,
                                 Offset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, DeviceCapabilities),
                                 sizeof(Capabilities.AsULONG)) != sizeof(Capabilities.AsULONG))
    {
        return FALSE;
    }
    return Capabilities.FunctionLevelResetCapability != 0;
}

static
VOID
TestInterfaceShape(
    _In_ PTEST_FUNCTION Function)
{
    DEVICE_RESET_INTERFACE_STANDARD Interface;
    BOOLEAN Flr = HasFunctionLevelReset(Function);
    NTSTATUS Status;

    Status = QueryInterface(Function->Top, &GUID_DEVICE_RESET_INTERFACE_STANDARD, sizeof(Interface),
                            DEVICE_RESET_INTERFACE_VERSION, &Interface);
    trace("function %08lx: FLR %u, top %wZ, query %lx size %u version %u types %lx\n", Function->Id, Flr,
          &Function->Top->DriverObject->DriverName, Status, Interface.Size, Interface.Version,
          Interface.SupportedResetTypes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_uint(Interface.Size, RTL_SIZEOF_THROUGH_FIELD(DEVICE_RESET_INTERFACE_STANDARD, Reserved));
    ok_eq_uint(Interface.Version, DEVICE_RESET_INTERFACE_VERSION);
    ok(Interface.DeviceReset != NULL, "function %08lx has no DeviceReset\n", Function->Id);
    ok(Interface.InterfaceReference != NULL && Interface.InterfaceDereference != NULL,
       "function %08lx has no reference routines\n", Function->Id);
    ok_eq_hex(Interface.SupportedResetTypes & (1UL << FunctionLevelDeviceReset), Flr ? (1UL << FunctionLevelDeviceReset) : 0UL);
    Interface.InterfaceDereference(Interface.Context);
}

static
VOID
TestQueryValidation(
    _In_ PTEST_FUNCTION Function)
{
    DEVICE_RESET_INTERFACE_STANDARD Interface;
    NTSTATUS Status;

    Status = QueryInterface(Function->Top, &GUID_DEVICE_RESET_INTERFACE_STANDARD, sizeof(Interface), 0, &Interface);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(Status))
        Interface.InterfaceDereference(Interface.Context);
    Status = QueryInterface(Function->Top, &GUID_DEVICE_RESET_INTERFACE_STANDARD, sizeof(INTERFACE),
                            DEVICE_RESET_INTERFACE_VERSION, &Interface);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    if (NT_SUCCESS(Status))
        Interface.InterfaceDereference(Interface.Context);
}

static
VOID
ResetCompleted(
    _In_ NTSTATUS Status,
    _Inout_opt_ PVOID Context)
{
    PRESET_COMPLETION Completion = Context;

    ok_irql(PASSIVE_LEVEL);
    Completion->Status = Status;
    InterlockedIncrement(&Completion->Calls);
    KeSetEvent(&Completion->Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
ReadState(
    _In_ PTEST_FUNCTION Function,
    _In_ UCHAR Express,
    _Out_ PPCI_COMMON_HEADER Header,
    _Out_ PUSHORT Control)
{
    RtlZeroMemory(Header, sizeof(*Header));
    *Control = 0;
    Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, Header, 0, sizeof(*Header));
    Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, Control,
                             Express + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, DeviceControl), sizeof(*Control));
}

static
VOID
CheckStateKept(
    _In_ PTEST_FUNCTION Function,
    _In_ UCHAR Express,
    _In_ const PCI_COMMON_HEADER *Before,
    _In_ USHORT ControlBefore)
{
    PCI_COMMON_HEADER After;
    USHORT ControlAfter;
    ULONG Index;

    ReadState(Function, Express, &After, &ControlAfter);
    ok_eq_hex(After.VendorID, Before->VendorID);
    ok_eq_hex(After.DeviceID, Before->DeviceID);
    ok_eq_hex(After.Command, Before->Command);
    for (Index = 0; Index < PCI_TYPE0_ADDRESSES; Index++)
        ok_eq_hex(After.u.type0.BaseAddresses[Index], Before->u.type0.BaseAddresses[Index]);
    ok_eq_hex(After.u.type0.ROMBaseAddress, Before->u.type0.ROMBaseAddress);
    ok_eq_uint(After.u.type0.InterruptLine, Before->u.type0.InterruptLine);
    ok_eq_uint(After.CacheLineSize, Before->CacheLineSize);
    ok_eq_uint(After.LatencyTimer, Before->LatencyTimer);
    ok_eq_hex(ControlAfter & 0x7fff, ControlBefore & 0x7fff);
}

static
BOOLEAN
QueryReset(
    _In_ PTEST_FUNCTION Function,
    _Out_ PDEVICE_RESET_INTERFACE_STANDARD Interface)
{
    NTSTATUS Status;

    Status = QueryInterface(Function->Top, &GUID_DEVICE_RESET_INTERFACE_STANDARD, sizeof(*Interface),
                            DEVICE_RESET_INTERFACE_VERSION, Interface);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status);
}

static
VOID
TestUnstartedWithoutFlr(
    _In_ PTEST_FUNCTION Function)
{
    DEVICE_RESET_INTERFACE_STANDARD Interface;

    if (!QueryReset(Function, &Interface))
        return;
    ok_eq_hex(Interface.SupportedResetTypes, 0UL);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 0, NULL), STATUS_DEVICE_CONFIGURATION_ERROR);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, PlatformLevelDeviceReset, 0, NULL), STATUS_NOT_SUPPORTED);
    Interface.InterfaceDereference(Interface.Context);
}

static
VOID
TestUnstarted(
    _In_ PTEST_FUNCTION Function)
{
    FUNCTION_LEVEL_DEVICE_RESET_PARAMETERS Parameters;
    DEVICE_RESET_INTERFACE_STANDARD Interface;
    RESET_COMPLETION Completion;
    NTSTATUS Status;

    if (!QueryReset(Function, &Interface))
        return;
    ok_eq_hex(Interface.SupportedResetTypes, 1UL << FunctionLevelDeviceReset);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, PlatformLevelDeviceReset, 0, NULL), STATUS_NOT_SUPPORTED);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, (DEVICE_RESET_TYPE)7, 0, NULL), STATUS_NOT_SUPPORTED);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 1, NULL), STATUS_DEVICE_CONFIGURATION_ERROR);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 0, NULL), STATUS_DEVICE_CONFIGURATION_ERROR);
    RtlZeroMemory(&Parameters, sizeof(Parameters));
    ok_eq_hex(Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 0, &Parameters),
              STATUS_DEVICE_CONFIGURATION_ERROR);

    KeInitializeEvent(&Completion.Event, NotificationEvent, FALSE);
    Completion.Status = STATUS_PENDING;
    Completion.Calls = 0;
    Parameters.Size = sizeof(Parameters);
    Parameters.DeviceResetCompletion = ResetCompleted;
    Parameters.CompletionContext = &Completion;
    Status = Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 0, &Parameters);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok_eq_long(Completion.Calls, 1L);
    ok_eq_hex(Completion.Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    Interface.InterfaceDereference(Interface.Context);
}

static
VOID
TestStarted(
    _In_ PTEST_FUNCTION Function)
{
    FUNCTION_LEVEL_DEVICE_RESET_PARAMETERS Parameters;
    DEVICE_RESET_INTERFACE_STANDARD Interface;
    RESET_COMPLETION Completion;
    PCI_COMMON_HEADER Before;
    LARGE_INTEGER Timeout;
    USHORT ControlBefore;
    UCHAR Express;
    NTSTATUS Status;

    Express = FindExpressCapability(Function);
    if (skip(Express != 0 && HasFunctionLevelReset(Function), "test function %08lx has no FLR\n", Function->Id))
        return;
    if (!QueryReset(Function, &Interface))
        return;

    ok_eq_hex(Interface.SupportedResetTypes, 1UL << FunctionLevelDeviceReset);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, PlatformLevelDeviceReset, 0, NULL), STATUS_NOT_SUPPORTED);

    ReadState(Function, Express, &Before, &ControlBefore);
    trace("before reset: command %04x BAR0 %08lx BAR1 %08lx BAR4 %08lx ROM %08lx devctl %04x\n", Before.Command,
          Before.u.type0.BaseAddresses[0], Before.u.type0.BaseAddresses[1], Before.u.type0.BaseAddresses[4],
          Before.u.type0.ROMBaseAddress, ControlBefore);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 0, NULL), STATUS_SUCCESS);
    CheckStateKept(Function, Express, &Before, ControlBefore);
    ok_eq_hex(Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 1, NULL), STATUS_SUCCESS);
    CheckStateKept(Function, Express, &Before, ControlBefore);
    RtlZeroMemory(&Parameters, sizeof(Parameters));
    ok_eq_hex(Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 0, &Parameters), STATUS_SUCCESS);
    CheckStateKept(Function, Express, &Before, ControlBefore);

    KeInitializeEvent(&Completion.Event, NotificationEvent, FALSE);
    Completion.Status = STATUS_PENDING;
    Completion.Calls = 0;
    Parameters.Size = sizeof(Parameters);
    Parameters.DeviceResetCompletion = ResetCompleted;
    Parameters.CompletionContext = &Completion;
    Status = Interface.DeviceReset(Interface.Context, FunctionLevelDeviceReset, 0, &Parameters);
    trace("reset with a completion routine returned %lx, %ld calls so far\n", Status, Completion.Calls);
    ok(Status == STATUS_SUCCESS || Status == STATUS_PENDING, "reset with completion returned %lx\n", Status);
    Timeout.QuadPart = -10000LL * 3000;
    ok_eq_hex(KeWaitForSingleObject(&Completion.Event, Executive, KernelMode, FALSE, &Timeout), STATUS_SUCCESS);
    ok_eq_long(Completion.Calls, 1L);
    ok_eq_hex(Completion.Status, STATUS_SUCCESS);
    CheckStateKept(Function, Express, &Before, ControlBefore);
    Interface.InterfaceDereference(Interface.Context);
}

static
ULONG
FunctionBus(
    _In_ PTEST_FUNCTION Function)
{
    ULONG Bus = 0, Length = 0;

    if (!NT_SUCCESS(IoGetDeviceProperty(Function->Pdo, DevicePropertyBusNumber, sizeof(Bus), &Bus, &Length)))
        return 0;
    return Bus;
}

START_TEST(IoPciDeviceReset)
{
    PTEST_FUNCTION TestFunction = NULL, PlainFunction = NULL, StartedFunction = NULL;
    ULONG Index;

    CollectFunctions();
    if (skip(FunctionCount != 0, "no PCI functions\n"))
        return;

    for (Index = 0; Index < FunctionCount; Index++)
    {
        TestInterfaceShape(&Functions[Index]);
        if (Functions[Index].Id == TEST_DEVICE_ID && HasFunctionLevelReset(&Functions[Index]))
            TestFunction = &Functions[Index];
        else if (Functions[Index].Id == TEST_DEVICE_ID)
            PlainFunction = &Functions[Index];
        else if (Functions[Index].Id == TEST_NVME_ID && FunctionBus(&Functions[Index]) != 0)
            StartedFunction = &Functions[Index];
    }
    trace("%lu PCI functions\n", FunctionCount);

    if (!skip(PlainFunction != NULL, "no virtio-crypto test function on the root bus\n"))
        TestUnstartedWithoutFlr(PlainFunction);
    if (!skip(TestFunction != NULL, "no virtio-crypto test function behind a PCIe root port\n"))
    {
        TestQueryValidation(TestFunction);
        TestUnstarted(TestFunction);
    }
    if (!skip(StartedFunction != NULL, "no NVMe test controller behind a PCIe root port\n"))
        TestStarted(StartedFunction);

    for (Index = 0; Index < FunctionCount; Index++)
    {
        if (Functions[Index].Bus.InterfaceDereference != NULL)
            Functions[Index].Bus.InterfaceDereference(Functions[Index].Bus.Context);
        ObDereferenceObject(Functions[Index].Top);
        ObDereferenceObject(Functions[Index].Pdo);
    }
}
