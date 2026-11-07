/*
 * PROJECT:     LiberNT Apple USB-C Power Delivery Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SN201202x (ACE3) port controllers behind the Apple silicon SPMI controllers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/appleacepd.h>

#define APPLE_PD_TAG            'dPpA'
#define APPLE_PD_MAX_PORTS      4
#define APPLE_PD_POLL_MS        500
#define APPLE_PD_LABEL          32

typedef struct _APPLE_PD_PORT
{
    APPLE_ACE_PD Pd;
    CHAR Label[APPLE_PD_LABEL];
    BOOLEAN Ready;
    APPLE_ACE_PD_STATE Last;
} APPLE_PD_PORT, *PAPPLE_PD_PORT;

typedef struct _APPLE_PD_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    APPLE_SPMI Spmi;
    APPLE_PD_PORT Ports[APPLE_PD_MAX_PORTS];
    ULONG PortCount;
    KTIMER Timer;
    KDPC Dpc;
    PIO_WORKITEM WorkItem;
    volatile LONG WorkQueued;
    volatile LONG Running;
} APPLE_PD_DEVICE, *PAPPLE_PD_DEVICE;

static
VOID
ApplePdLog(
    _In_ PAPPLE_PD_PORT Port,
    _In_z_ PCSTR Reason,
    _In_ PAPPLE_ACE_PD_STATE State)
{
    ULONG Data = State->DataStatus;

    DbgPrint("applepd: %s %s: %s, %s, data %s%s%s%s as %s, %s side up, power %04x, events %I64x\n", Port->Label, Reason,
             (State->Status & APPLE_ACE_STATUS_PLUG_PRESENT) ? "plugged" : "empty",
             (State->Status & APPLE_ACE_STATUS_PORT_ROLE) ? "source" : "sink",
             (Data & APPLE_ACE_DATA_CONNECTION) ? "connected" : "none", (Data & APPLE_ACE_DATA_USB2) ? " USB2" : "",
             (Data & APPLE_ACE_DATA_USB3) ? " USB3" : "", (Data & APPLE_ACE_DATA_DP) ? " DP" : "",
             (Data & APPLE_ACE_DATA_USB_DEVICE_ROLE) ? "device" : "host",
             (State->Status & APPLE_ACE_STATUS_UPSIDE_DOWN) ? "B" : "A", State->PowerStatus, State->Events);
}

static
VOID
ApplePdPollPorts(
    _In_ PAPPLE_PD_DEVICE Device)
{
    APPLE_ACE_PD_STATE State;
    PAPPLE_PD_PORT Port;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < Device->PortCount; ++Index)
    {
        Port = &Device->Ports[Index];
        if (!Port->Ready)
            continue;
        Status = AppleAcePdPoll(&Port->Pd, &State);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("applepd: %s stopped answering: %lx\n", Port->Label, Status);
            Port->Ready = FALSE;
            continue;
        }
        if (State.Events || State.Status != Port->Last.Status || State.DataStatus != Port->Last.DataStatus)
            ApplePdLog(Port, "changed", &State);
        Port->Last = State;
    }
}

static
VOID
NTAPI
ApplePdWork(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PAPPLE_PD_DEVICE Device = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (InterlockedCompareExchange(&Device->Running, 1, 1))
        ApplePdPollPorts(Device);
    InterlockedExchange(&Device->WorkQueued, 0);
}

static
VOID
NTAPI
ApplePdTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PAPPLE_PD_DEVICE Device = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    if (!InterlockedCompareExchange(&Device->Running, 1, 1) || InterlockedExchange(&Device->WorkQueued, 1))
        return;
    IoQueueWorkItem(Device->WorkItem, ApplePdWork, DelayedWorkQueue, Device);
}

static
ULONG
ApplePdFindInterrupt(
    _In_ PAPPLE_PD_DEVICE Device,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name)
{
    const CHAR *Names, *Entry;
    const ULONG *Interrupts, *CellCount;
    ULONG Cursor = 0, Index, Cells, NamesLength, Length, EntryLength;

    CellCount = RiscvFdtGetProperty(&Device->Tree.Fdt, Device->Tree.Node, "#interrupt-cells", &Length);
    Cells = (CellCount && Length >= sizeof(ULONG)) ? RiscvFdtReadBigEndian32(CellCount) : 0;
    Names = RiscvFdtGetProperty(&Device->Tree.Fdt, Node, "interrupt-names", &NamesLength);
    Interrupts = RiscvFdtGetProperty(&Device->Tree.Fdt, Node, "interrupts", &Length);
    if (!Cells || !Interrupts)
        return MAXULONG;
    for (Index = 0; (Entry = RiscvFdtNextString(Names, NamesLength, &Cursor, &EntryLength)) != NULL; ++Index)
    {
        if (EntryLength == strlen(Name) && !memcmp(Entry, Name, EntryLength))
            return (Length >= (Index + 1) * Cells * sizeof(ULONG)) ? RiscvFdtReadBigEndian32(&Interrupts[Index * Cells])
                                                                   : MAXULONG;
    }
    return MAXULONG;
}

static
VOID
ApplePdReadLabel(
    _In_ PAPPLE_PD_DEVICE Device,
    _In_ ULONG Node,
    _Out_writes_(APPLE_PD_LABEL) PCHAR Label)
{
    const CHAR *Text;
    ULONG Child, Length;

    Text = RiscvFdtNodeName(&Device->Tree.Fdt, Node, &Length);
    for (Child = RiscvFdtFirstChild(&Device->Tree.Fdt, Node); Child != RISCV_FDT_NO_NODE;
         Child = RiscvFdtNextSibling(&Device->Tree.Fdt, Child))
    {
        const CHAR *Candidate = RiscvFdtGetProperty(&Device->Tree.Fdt, Child, "label", &Length);

        if (Candidate && Length > 1)
        {
            Text = Candidate;
            --Length;
            break;
        }
    }
    if (!Text)
        Length = 0;
    Length = min(Length, APPLE_PD_LABEL - 1);
    RtlCopyMemory(Label, Text, Length);
    Label[Length] = '\0';
}

static
VOID
ApplePdStopHardware(
    _In_ PAPPLE_PD_DEVICE Device)
{
    LARGE_INTEGER Interval;

    InterlockedExchange(&Device->Running, 0);
    KeCancelTimer(&Device->Timer);
    KeFlushQueuedDpcs();
    Interval.QuadPart = -10000LL;
    while (InterlockedCompareExchange(&Device->WorkQueued, 0, 0))
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    AppleSpmiClose(&Device->Spmi);
    Device->PortCount = 0;
    if (Device->TreeOpen)
    {
        FdtDriverClose(&Device->Tree);
        Device->TreeOpen = FALSE;
    }
}

static
NTSTATUS
ApplePdStartHardware(
    _In_ PAPPLE_PD_DEVICE Device)
{
    APPLE_ACE_PD_STATE State;
    PAPPLE_PD_PORT Port;
    LARGE_INTEGER DueTime;
    const VOID *Compatible, *Reg;
    ULONG Child, Length, RegLength;
    UCHAR Previous = 0xff;
    NTSTATUS Status;

    Status = FdtDriverOpen(Device->Pdo, &Device->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Device->TreeOpen = TRUE;
    for (Child = RiscvFdtFirstChild(&Device->Tree.Fdt, Device->Tree.Node);
         Child != RISCV_FDT_NO_NODE && Device->PortCount < APPLE_PD_MAX_PORTS;
         Child = RiscvFdtNextSibling(&Device->Tree.Fdt, Child))
    {
        Compatible = RiscvFdtGetProperty(&Device->Tree.Fdt, Child, "compatible", &Length);
        Reg = RiscvFdtGetProperty(&Device->Tree.Fdt, Child, "reg", &RegLength);
        if (!RiscvFdtStringListContains(Compatible, Length, "apple,sn201202x") || !Reg || RegLength < sizeof(ULONG))
            continue;
        Port = &Device->Ports[Device->PortCount++];
        RtlZeroMemory(Port, sizeof(*Port));
        Port->Pd.Spmi = &Device->Spmi;
        Port->Pd.Sid = (UCHAR)(RiscvFdtReadBigEndian32(Reg) & 0x0f);
        Port->Pd.WakeInterrupt = ApplePdFindInterrupt(Device, Child, "wake");
        ApplePdReadLabel(Device, Child, Port->Label);
    }
    if (!Device->PortCount)
        return STATUS_SUCCESS;

    Status = ApplePmgrPowerOnDevice(&Device->Tree, Device->Tree.Node);
    if (NT_SUCCESS(Status))
        Status = AppleSpmiOpen(&Device->Tree, Device->Tree.Node, &Device->Spmi);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applepd: SPMI controller with %lu port controllers is not usable: %lx\n", Device->PortCount, Status);
        return Status;
    }
    for (Child = 0; Child < Device->PortCount; ++Child)
    {
        Port = &Device->Ports[Child];
        Status = AppleAcePdInitialize(&Port->Pd, &Previous);
        DbgPrint("applepd: %s at SID %u was in power state %u, set to S0: %lx\n", Port->Label, Port->Pd.Sid, Previous,
                 Status);
        if (!NT_SUCCESS(Status))
            continue;
        Status = AppleAcePdPoll(&Port->Pd, &State);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("applepd: %s status read failed: %lx\n", Port->Label, Status);
            continue;
        }
        Port->Last = State;
        Port->Ready = TRUE;
        ApplePdLog(Port, "is up", &State);
    }
    InterlockedExchange(&Device->Running, 1);
    DueTime.QuadPart = -10000LL * APPLE_PD_POLL_MS;
    KeSetTimerEx(&Device->Timer, DueTime, APPLE_PD_POLL_MS, &Device->Dpc);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ApplePdCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
ApplePdPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PD_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT Lower = Device->LowerDevice;
    KEVENT Event;
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, ApplePdCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Lower, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
            {
                Status = ApplePdStartHardware(Device);
                if (!NT_SUCCESS(Status))
                    ApplePdStopHardware(Device);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            ApplePdStopHardware(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            ApplePdStopHardware(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Lower, Irp);
            IoFreeWorkItem(Device->WorkItem);
            IoDetachDevice(Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Lower, Irp);
}

static
NTSTATUS
NTAPI
ApplePdPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PD_DEVICE Device = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
ApplePdPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PD_DEVICE Device = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
ApplePdAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT DeviceObject;
    PAPPLE_PD_DEVICE Device;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_PD_DEVICE), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    Device = DeviceObject->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->DeviceObject = DeviceObject;
    Device->Pdo = Pdo;
    KeInitializeTimer(&Device->Timer);
    KeInitializeDpc(&Device->Dpc, ApplePdTimerDpc, Device);
    Device->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (!Device->WorkItem)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Device->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, Pdo);
    if (!Device->LowerDevice)
    {
        IoFreeWorkItem(Device->WorkItem);
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }
    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
ApplePdUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = ApplePdPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = ApplePdPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = ApplePdPower;
    DriverObject->DriverExtension->AddDevice = ApplePdAddDevice;
    DriverObject->DriverUnload = ApplePdUnload;
    return STATUS_SUCCESS;
}
