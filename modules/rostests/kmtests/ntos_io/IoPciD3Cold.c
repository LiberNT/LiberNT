/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3cold support interface of PCI functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wdmguid.h>
#include <acpiioct.h>

#define NDEBUG
#include <debug.h>

#define MAX_FUNCTIONS 64
#define TEST_DEVICE_ID 0x00051B36UL

typedef struct _TEST_FUNCTION
{
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Top;
    BUS_INTERFACE_STANDARD Bus;
    ULONG Id;
} TEST_FUNCTION, *PTEST_FUNCTION;

typedef struct _POWER_REQUEST
{
    KEVENT Event;
    NTSTATUS Status;
} POWER_REQUEST, *PPOWER_REQUEST;

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
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'cDpK');
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
        ExFreePoolWithTag(List, 'cDpK');
    }
    ObDereferenceObject(PciDriver);
}

static
VOID
NTAPI
PowerRequestComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ POWER_STATE PowerState,
    _In_opt_ PVOID Context,
    _In_ PIO_STATUS_BLOCK IoStatus)
{
    PPOWER_REQUEST Request = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);

    Request->Status = IoStatus->Status;
    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
SetDevicePower(
    _In_ PTEST_FUNCTION Function,
    _In_ DEVICE_POWER_STATE State)
{
    POWER_REQUEST Request;
    POWER_STATE PowerState;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);
    Request.Status = STATUS_PENDING;
    PowerState.DeviceState = State;
    Status = PoRequestPowerIrp(Function->Pdo, IRP_MN_SET_POWER, PowerState, PowerRequestComplete, &Request, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Timeout.QuadPart = -10LL * 1000 * 1000 * 10;
    if (KeWaitForSingleObject(&Request.Event, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
        return STATUS_TIMEOUT;
    return Request.Status;
}

static
ULONG
EvaluateInteger(
    _In_ PTEST_FUNCTION Function,
    _In_ ULONG MethodName)
{
    ACPI_EVAL_INPUT_BUFFER Input;
    ACPI_EVAL_OUTPUT_BUFFER Output;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    RtlZeroMemory(&Input, sizeof(Input));
    RtlZeroMemory(&Output, sizeof(Output));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.MethodNameAsUlong = MethodName;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, Function->Top, &Input, sizeof(Input),
                                        &Output, sizeof(Output), FALSE, &Event, &IoStatus);
    if (Irp == NULL)
        return MAXULONG;

    Status = IoCallDriver(Function->Top, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (!NT_SUCCESS(Status) || Output.Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output.Count != 1 ||
        Output.Argument[0].Type != ACPI_METHOD_ARGUMENT_INTEGER)
    {
        return MAXULONG;
    }
    return Output.Argument[0].Argument;
}

static
VOID
TestInterfaceShape(
    _In_ PTEST_FUNCTION Function)
{
    D3COLD_LAST_TRANSITION_STATUS LastTransition;
    D3COLD_SUPPORT_INTERFACE Interface;
    DEVICE_WAKE_DEPTH Depth;
    BOOLEAN Supported;
    NTSTATUS Status;

    Status = QueryInterface(Function->Top, &GUID_D3COLD_SUPPORT_INTERFACE, sizeof(Interface), D3COLD_SUPPORT_INTERFACE_VERSION, &Interface);
    ok(Status == STATUS_SUCCESS, "D3cold interface on id %08lx: 0x%lx\n", Function->Id, Status);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_uint(Interface.Size, sizeof(Interface));
    ok_eq_uint(Interface.Version, D3COLD_SUPPORT_INTERFACE_VERSION);
    ok(Interface.Context != NULL, "context is NULL\n");
    ok(Interface.SetD3ColdSupport != NULL && Interface.GetIdleWakeInfo != NULL && Interface.GetD3ColdCapability != NULL &&
       Interface.GetBusDriverD3ColdSupport != NULL && Interface.GetLastTransitionStatus != NULL,
       "missing D3cold routine on id %08lx\n", Function->Id);
    if (Interface.GetBusDriverD3ColdSupport == NULL || Interface.GetD3ColdCapability == NULL ||
        Interface.GetIdleWakeInfo == NULL || Interface.GetLastTransitionStatus == NULL)
    {
        Interface.InterfaceDereference(Interface.Context);
        return;
    }

    Supported = FALSE;
    ok_eq_hex(Interface.GetBusDriverD3ColdSupport(Interface.Context, &Supported), STATUS_SUCCESS);
    ok_eq_bool(Supported, TRUE);
    Supported = 2;
    ok_eq_hex(Interface.GetD3ColdCapability(Interface.Context, &Supported), STATUS_SUCCESS);
    ok(Supported == TRUE || Supported == FALSE, "capability %u on id %08lx\n", Supported, Function->Id);

    Depth = DeviceWakeDepthMaximum;
    Status = Interface.GetIdleWakeInfo(Interface.Context, PowerSystemWorking, &Depth);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Depth < DeviceWakeDepthMaximum, "S0 depth %d on id %08lx\n", Depth, Function->Id);
    Status = Interface.GetIdleWakeInfo(Interface.Context, PowerSystemShutdown, &Depth);
    ok(!NT_SUCCESS(Status), "S5 idle wake on id %08lx: 0x%lx\n", Function->Id, Status);

    LastTransition = LastDStateTransitionD3cold + 1;
    Interface.GetLastTransitionStatus(Interface.Context, &LastTransition);
    ok(LastTransition <= LastDStateTransitionD3cold, "last transition %d on id %08lx\n", LastTransition, Function->Id);
    Interface.InterfaceDereference(Interface.Context);

    Status = QueryInterface(Function->Top, &GUID_D3COLD_SUPPORT_INTERFACE, sizeof(Interface), D3COLD_SUPPORT_INTERFACE_VERSION + 1, &Interface);
    ok(!NT_SUCCESS(Status), "D3cold interface version 2: 0x%lx\n", Status);
    Status = QueryInterface(Function->Top, &GUID_D3COLD_SUPPORT_INTERFACE, sizeof(INTERFACE), D3COLD_SUPPORT_INTERFACE_VERSION, &Interface);
    ok(!NT_SUCCESS(Status), "D3cold interface size %u: 0x%lx\n", (ULONG)sizeof(INTERFACE), Status);
}

static
VOID
CheckTransition(
    _In_ PTEST_FUNCTION Function,
    _In_ PD3COLD_SUPPORT_INTERFACE Interface,
    _In_ D3COLD_LAST_TRANSITION_STATUS ExpectedLast,
    _In_ ULONG ExpectedPowerOn)
{
    D3COLD_LAST_TRANSITION_STATUS LastTransition;
    ULONG Id = 0;

    ok_eq_hex(SetDevicePower(Function, PowerDeviceD3), STATUS_SUCCESS);
    LastTransition = LastDStateTransitionStatusUnknown;
    Interface->GetLastTransitionStatus(Interface->Context, &LastTransition);
    ok_eq_int(LastTransition, ExpectedLast);
    ok_eq_ulong(EvaluateInteger(Function, 'ATSP'), ExpectedPowerOn);

    ok_eq_hex(SetDevicePower(Function, PowerDeviceD0), STATUS_SUCCESS);
    LastTransition = LastDStateTransitionStatusUnknown;
    Interface->GetLastTransitionStatus(Interface->Context, &LastTransition);
    ok_eq_int(LastTransition, ExpectedLast);
    ok_eq_ulong(EvaluateInteger(Function, 'ATSP'), 1UL);
    Function->Bus.GetBusData(Function->Bus.Context, PCI_WHICHSPACE_CONFIG, &Id, 0, sizeof(Id));
    ok_eq_hex(Id, Function->Id);
}

static
VOID
TestTransitions(
    _In_ PTEST_FUNCTION Function)
{
    D3COLD_LAST_TRANSITION_STATUS LastTransition;
    D3COLD_SUPPORT_INTERFACE Interface;
    DEVICE_WAKE_DEPTH Depth;
    BOOLEAN Supported;
    NTSTATUS Status;

    if (skip(Function->Top == Function->Pdo, "test function %08lx has a driver\n", Function->Id))
        return;

    Status = QueryInterface(Function->Top, &GUID_D3COLD_SUPPORT_INTERFACE, sizeof(Interface), D3COLD_SUPPORT_INTERFACE_VERSION, &Interface);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Supported = FALSE;
    ok_eq_hex(Interface.GetD3ColdCapability(Interface.Context, &Supported), STATUS_SUCCESS);
    ok_eq_bool(Supported, TRUE);
    Depth = DeviceWakeDepthMaximum;
    ok_eq_hex(Interface.GetIdleWakeInfo(Interface.Context, PowerSystemWorking, &Depth), STATUS_SUCCESS);
    ok_eq_int(Depth, DeviceWakeDepthD3cold);
    ok_eq_hex(Interface.GetIdleWakeInfo(Interface.Context, PowerSystemSleeping3, &Depth), STATUS_NOT_IMPLEMENTED);
    LastTransition = LastDStateTransitionD3cold;
    Interface.GetLastTransitionStatus(Interface.Context, &LastTransition);
    ok_eq_int(LastTransition, LastDStateTransitionStatusUnknown);

    ok_eq_hex(SetDevicePower(Function, PowerDeviceD0), STATUS_SUCCESS);
    ok_eq_ulong(EvaluateInteger(Function, 'ATSP'), 1UL);

    Interface.SetD3ColdSupport(Interface.Context, TRUE);
    CheckTransition(Function, &Interface, LastDStateTransitionD3cold, 0);
    ok_eq_ulong(EvaluateInteger(Function, 'FFOP'), 1UL);

    Interface.SetD3ColdSupport(Interface.Context, FALSE);
    CheckTransition(Function, &Interface, LastDStateTransitionD3hot, 1);
    ok_eq_ulong(EvaluateInteger(Function, 'FFOP'), 1UL);

    Interface.InterfaceDereference(Interface.Context);
}

START_TEST(IoPciD3Cold)
{
    PTEST_FUNCTION TestFunction = NULL;
    ULONG Index;

    CollectFunctions();
    if (skip(FunctionCount != 0, "no PCI functions\n"))
        return;

    for (Index = 0; Index < FunctionCount; Index++)
    {
        TestInterfaceShape(&Functions[Index]);
        if (Functions[Index].Id == TEST_DEVICE_ID)
            TestFunction = &Functions[Index];
    }
    trace("%lu PCI functions\n", FunctionCount);

    if (!skip(TestFunction != NULL, "no PCI test device with the D3cold SSDT\n"))
        TestTransitions(TestFunction);

    for (Index = 0; Index < FunctionCount; Index++)
    {
        if (Functions[Index].Bus.InterfaceDereference != NULL)
            Functions[Index].Bus.InterfaceDereference(Functions[Index].Bus.Context);
        ObDereferenceObject(Functions[Index].Top);
        ObDereferenceObject(Functions[Index].Pdo);
    }
}
