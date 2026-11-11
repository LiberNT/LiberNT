/*
 * PROJECT:     LiberNT Apple AOP Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple Always-On Processor: coprocessor boot and the functions it serves
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <initguid.h>
#include <reactos/drivers/appleaop.h>
#include "appleaop.h"
#include <reactos/drivers/applepmgr.h>

#define AOP_DART_WINDOW                 0x40000000ULL
#define AOP_DMA_ALIGN                   0x4000

typedef struct _AOP_CHILD_INFO
{
    PCWSTR Id;
    PCWSTR Description;
    APPLE_AOP_FUNCTION Function;
} AOP_CHILD_INFO;

static const AOP_CHILD_INFO AopChildInfo[AOP_FUNCTIONS] =
{
    { L"APPLEAOP\\ALS", L"Apple ambient light sensor", AppleAopAmbientLight },
    { L"APPLEAOP\\LAS", L"Apple lid angle sensor", AppleAopLidAngle },
    { L"APPLEAOP\\AUDIO", L"Apple built-in microphones", AppleAopMicrophones },
};

static const AOP_CONFIG AopConfigs[] =
{
    { "apple,t8103-aop", 0x20000, 128, 1 },
    { "apple,t8112-aop", 0x20000, 128, 0 },
    { "apple,t6000-aop", 0x20000, 64, 0 },
    { "apple,t6020-aop", 0x10000000000ULL, 64, 0 },
};

#define AOP_IS_CHILD(DeviceObject) (*(PULONG)(DeviceObject)->DeviceExtension != AOP_TYPE_BUS)

ULONG64
AopNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

VOID
AopLock(
    _Inout_ PAOP_DEVICE Device)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ExAcquireFastMutex(&Device->Lock);
    ASSERT(Device->LockOwner == NULL);
    Device->LockOwner = KeGetCurrentThread();
}

VOID
AopUnlock(
    _Inout_ PAOP_DEVICE Device)
{
    ASSERT(Device->LockOwner == KeGetCurrentThread());
    Device->LockOwner = NULL;
    ExReleaseFastMutex(&Device->Lock);
}

PAOP_DMA
AopAllocateShared(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PAOP_DMA Dma;
    NTSTATUS Status;

    ASSERT(Device->LockOwner == KeGetCurrentThread());
    if (!Size || Size > 0x1000000 || Device->DmaCount == AOP_MAX_DMA)
        return NULL;
    Dma = &Device->Dma[Device->DmaCount];
    RtlZeroMemory(Dma, sizeof(*Dma));
    Dma->Size = (Size + AOP_DMA_ALIGN - 1) & ~(AOP_DMA_ALIGN - 1);
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Dma->Virtual = MmAllocateContiguousMemorySpecifyCache(Dma->Size, Low, High, Boundary, MmNonCached);
    if (!Dma->Virtual)
        return NULL;
    RtlZeroMemory(Dma->Virtual, Dma->Size);
    Dma->Physical = (ULONG64)MmGetPhysicalAddress(Dma->Virtual).QuadPart;
    Dma->Iova = Dma->Physical;
    if (Device->DartAdopted)
    {
        Status = AppleDartAdoptedMap(&Device->Dart, Dma->Physical, Dma->Size, &Dma->Iova);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("appleaop: %lu bytes at %I64x not mapped for the AOP: %lx\n", Dma->Size, Dma->Physical, Status);
            MmFreeContiguousMemorySpecifyCache(Dma->Virtual, Dma->Size, MmNonCached);
            RtlZeroMemory(Dma, sizeof(*Dma));
            return NULL;
        }
    }
    KeMemoryBarrier();
    Device->DmaCount++;
    return Dma;
}

static
VOID
AopFreeShared(
    _Inout_ PAOP_DEVICE Device)
{
    ULONG Index;

    for (Index = 0; Index < Device->DmaCount; ++Index)
    {
        if (Device->Dma[Index].Virtual)
            MmFreeContiguousMemorySpecifyCache(Device->Dma[Index].Virtual, Device->Dma[Index].Size, MmNonCached);
    }
    RtlZeroMemory(Device->Dma, sizeof(Device->Dma));
    Device->DmaCount = 0;
}

static
BOOLEAN
NTAPI
AopShmemSetup(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Iova,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    PAOP_DEVICE Device = Context;
    PAOP_DMA Dma;

    *Virtual = NULL;
    *Preassigned = FALSE;
    if (*Iova)
    {
        DbgPrint("appleaop: endpoint %u asked for %lu bytes at its own address %I64x, refused\n", Endpoint, Size, *Iova);
        return FALSE;
    }
    Dma = AopAllocateShared(Device, Size);
    if (!Dma)
        return FALSE;
    *Iova = Dma->Iova;
    *Virtual = Dma->Virtual;
    return TRUE;
}

static
const AOP_CONFIG *
AopFindConfig(
    _In_ PFDT_DRIVER_TREE Tree)
{
    const VOID *Compatible;
    ULONG Length, Index;

    Compatible = FdtGetProperty(&Tree->Fdt, Tree->Node, "compatible", &Length);
    for (Index = 0; Compatible && Index < RTL_NUMBER_OF(AopConfigs); ++Index)
    {
        if (FdtStringListContains(Compatible, Length, AopConfigs[Index].Compatible))
            return &AopConfigs[Index];
    }
    return NULL;
}

static
NTSTATUS
AopSetupDart(
    _Inout_ PAOP_DEVICE Device)
{
    const VOID *Iommus;
    ULONG64 Phandle, Sid;
    ULONG Length;
    NTSTATUS Status;

    Iommus = FdtGetProperty(&Device->Tree.Fdt, Device->Tree.Node, "iommus", &Length);
    if (!Iommus)
    {
        DbgPrint("appleaop: the tree gives the AOP no IOMMU, buffers are given by physical address\n");
        Device->DartIdentity = TRUE;
        return STATUS_SUCCESS;
    }
    if (!FdtReadCells(Iommus, Length, 0, 1, &Phandle) || !FdtReadCells(Iommus, Length, 1, 1, &Sid))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Device->DartNode = FdtDriverFindPhandle(&Device->Tree, (ULONG)Phandle);
    Device->DartSid = (ULONG)Sid;
    Status = AppleDartMapStream(&Device->Tree, Device->DartNode, Device->DartSid, "appleaop");
    if (NT_SUCCESS(Status))
    {
        Device->DartIdentity = TRUE;
        DbgPrint("appleaop: DART stream %lu gives the AOP physical addresses\n", Device->DartSid);
        return STATUS_SUCCESS;
    }
    if (Status != STATUS_ACCESS_DENIED)
    {
        DbgPrint("appleaop: DART stream %lu not set up: %lx\n", Device->DartSid, Status);
        return Status;
    }
    Status = AppleDartAdopt(&Device->Tree, Device->DartNode, Device->DartSid, Device->Config->Ec0p,
                            Device->Config->Ec0p + AOP_DART_WINDOW, "appleaop", &Device->Dart);
    DbgPrint("appleaop: DART stream %lu is locked, firmware tables adopted for %I64x-%I64x: %lx\n", Device->DartSid,
             Device->Config->Ec0p, Device->Config->Ec0p + AOP_DART_WINDOW, Status);
    if (NT_SUCCESS(Status))
        Device->DartAdopted = TRUE;
    return Status;
}

static
VOID
AopLogSummary(
    _Inout_ PAOP_DEVICE Device)
{
    CHAR Names[160];
    ULONG Index, Used = 0, Present = 0, Running = 0, Length;

    Names[0] = '\0';
    AopLock(Device);
    for (Index = 0; Index < AFK_ENDPOINT_COUNT; ++Index)
    {
        Present += Device->Endpoints[Index].Present;
        Running += Device->Endpoints[Index].Started;
    }
    for (Index = 0; Index < Device->ServiceCount; ++Index)
    {
        Length = (ULONG)strlen(Device->Services[Index].Name);
        if (Used + Length + 2 > sizeof(Names))
            break;
        if (Used)
            Names[Used++] = ' ';
        RtlCopyMemory(Names + Used, Device->Services[Index].Name, Length);
        Used += Length;
        Names[Used] = '\0';
    }
    DbgPrint("appleaop: AOP %s, %lu of %lu AFK endpoints running, %lu services: %s\n",
             Device->Rtkit.Failed ? "crashed" : "running", Running, Present, Device->ServiceCount,
             Used ? Names : "none");
    AopUnlock(Device);
}

static
VOID
AopCreateChildren(
    _Inout_ PAOP_DEVICE Device)
{
    PDEVICE_OBJECT ChildObject;
    PAOP_CHILD Child;
    BOOLEAN Created = FALSE;
    ULONG Sensor;
    KIRQL OldIrql;
    BOOLEAN Listening;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ExAcquireFastMutex(&Device->ChildLock);
    for (Sensor = 0; Sensor < AOP_FUNCTIONS; ++Sensor)
    {
        KeAcquireSpinLock(&Device->SensorLock, &OldIrql);
        Listening = Device->Sensors[Sensor].Listening;
        KeReleaseSpinLock(&Device->SensorLock, OldIrql);
        if (!Listening || Device->Sensors[Sensor].Child)
            continue;
        Status = IoCreateDevice(Device->DeviceObject->DriverObject, sizeof(AOP_CHILD), NULL, FILE_DEVICE_UNKNOWN,
                                FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN, FALSE, &ChildObject);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("appleaop: %S device not created: %lx\n", AopChildInfo[Sensor].Id, Status);
            continue;
        }
        Child = ChildObject->DeviceExtension;
        RtlZeroMemory(Child, sizeof(*Child));
        Child->Type = Sensor + 1;
        Child->Self = ChildObject;
        Child->Parent = Device;
        Child->Sensor = Sensor;
        ChildObject->Flags |= DO_POWER_PAGABLE;
        ChildObject->Flags &= ~DO_DEVICE_INITIALIZING;
        Device->Sensors[Sensor].Child = ChildObject;
        Created = TRUE;
        DbgPrint("appleaop: %S device created\n", AopChildInfo[Sensor].Id);
    }
    if (Created)
        Device->ChildrenReported = TRUE;
    ExReleaseFastMutex(&Device->ChildLock);
    if (Created)
        IoInvalidateDeviceRelations(Device->Pdo, BusRelations);
}

static
VOID
NTAPI
AopWorker(
    _In_ PVOID Context)
{
    PAOP_DEVICE Device = Context;
    PVOID Objects[2] = { &Device->StopEvent, &Device->WorkEvent };
    LARGE_INTEGER Interval;
    ULONG64 Elapsed;
    ULONG WaitMs;
    BOOLEAN Failed;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    while (!Device->Stop)
    {
        AopLock(Device);
        AppleRtkitPoll(&Device->Rtkit);
        Failed = Device->Rtkit.Failed;
        AopUnlock(Device);
        if (Failed && !Device->FailureLogged)
        {
            Device->FailureLogged = TRUE;
            DbgPrint("appleaop: the AOP firmware failed, its functions stop here\n");
        }
        if (!Failed && InterlockedExchange(&Device->ServicesPending, 0))
        {
            AopSensorsSetup(Device);
            AopCreateChildren(Device);
        }
        Elapsed = AopNowMs() - Device->StartedMs;
        if (!Device->SummaryLogged && Elapsed >= AOP_SUMMARY_DELAY_MS)
        {
            Device->SummaryLogged = TRUE;
            AopLogSummary(Device);
        }
        WaitMs = AppleRtkitPollInterval(&Device->Rtkit, AOP_POLL_INTERVAL_MS);
        if (!Device->SummaryLogged)
            WaitMs = (ULONG)min((ULONG64)WaitMs, AOP_SUMMARY_DELAY_MS - Elapsed);
        Interval.QuadPart = -(LONGLONG)WaitMs * 10000;
        KeWaitForMultipleObjects(RTL_NUMBER_OF(Objects), Objects, WaitAny, Executive, KernelMode, FALSE,
                                 WaitMs == APPLE_RTKIT_NO_POLL ? NULL : &Interval, NULL);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
AopStopHardware(
    _Inout_ PAOP_DEVICE Device)
{
    KIRQL OldIrql;
    ULONG Sensor;
    BOOLEAN CpuRunning;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (Device->Thread)
    {
        InterlockedExchange(&Device->Stop, 1);
        KeSetEvent(&Device->StopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Device->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Device->Thread);
        Device->Thread = NULL;
    }
    KeAcquireSpinLock(&Device->SensorLock, &OldIrql);
    for (Sensor = 0; Sensor < AOP_FUNCTIONS; ++Sensor)
    {
        Device->Sensors[Sensor].Listening = FALSE;
        Device->Sensors[Sensor].Callback = NULL;
        Device->Sensors[Sensor].CallbackContext = NULL;
    }
    KeReleaseSpinLock(&Device->SensorLock, OldIrql);
    AopLock(Device);
    if (Device->Running)
        AfkStopEndpoints(Device);
    Device->Running = FALSE;
    AopUnlock(Device);
    CpuRunning = !Device->Asc ||
                 (READ_REGISTER_ULONG((volatile ULONG *)(Device->Asc + AOP_ASC_CPU_CONTROL)) & AOP_ASC_CPU_CONTROL_RUN);
    if (CpuRunning && Device->DmaCount)
        DbgPrint("appleaop: the AOP still runs, its %lu shared buffers stay allocated\n", Device->DmaCount);
    else
        AopFreeShared(Device);
    if (Device->DartAdopted)
        AppleDartAdoptedRelease(&Device->Dart);
    Device->DartAdopted = FALSE;
    AppleRtkitDisconnectInterrupt(&Device->Rtkit);
    if (Device->Mailbox)
        MmUnmapIoSpace(Device->Mailbox, Device->MailboxSize);
    if (Device->Asc)
        MmUnmapIoSpace(Device->Asc, Device->AscSize);
    if (Device->Aop)
        MmUnmapIoSpace(Device->Aop, Device->AopSize);
    Device->Mailbox = NULL;
    Device->Asc = NULL;
    Device->Aop = NULL;
    if (Device->TreeOpen)
        FdtDriverClose(&Device->Tree);
    Device->TreeOpen = FALSE;
}

static
NTSTATUS
AopStartHardware(
    _Inout_ PAOP_DEVICE Device,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    ULONG MailboxPhandle, MailboxNode, Control, Index, Gsi;
    ULONG64 AscPhysical = 0, MailboxPhysical = 0;
    HANDLE ThreadHandle;
    BOOLEAN Edge;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!Device->Running && !Device->Thread && !Device->TreeOpen && !Device->Rtkit.Interrupt);
    RtlZeroMemory(&Device->Rtkit, sizeof(Device->Rtkit));
    RtlZeroMemory(Device->Endpoints, sizeof(Device->Endpoints));
    for (Index = 0; Index < AFK_ENDPOINT_COUNT; ++Index)
        Device->Endpoints[Index].Index = (UCHAR)(AFK_ENDPOINT_START + Index);
    RtlZeroMemory(Device->Services, sizeof(Device->Services));
    Device->ServiceCount = 0;
    Device->ServicesPending = 0;
    Device->FailureLogged = FALSE;
    Device->SummaryLogged = FALSE;
    Device->DartIdentity = FALSE;
    Device->DartAdopted = FALSE;
    Status = FdtDriverOpen(Device->Pdo, &Device->Tree);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaop: device tree unavailable: %lx\n", Status);
        return Status;
    }
    Device->TreeOpen = TRUE;
    Device->Config = AopFindConfig(&Device->Tree);
    if (!Device->Config)
    {
        DbgPrint("appleaop: the AOP node names no known AOP generation\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (!FdtReadU32(&Device->Tree.Fdt, Device->Tree.Node, "mboxes", &MailboxPhandle) ||
        (MailboxNode = FdtDriverFindPhandle(&Device->Tree, MailboxPhandle)) == FDT_NO_NODE)
    {
        DbgPrint("appleaop: the AOP has no mailbox\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Status = ApplePmgrPowerOnDevice(&Device->Tree, Device->Tree.Node);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaop: AOP power domains did not turn on: %lx\n", Status);
        return Status;
    }
    Device->Aop = FdtDriverMapReg(&Device->Tree, Device->Tree.Node, 0, &Device->AopPhysical, &Device->AopSize);
    Device->Asc = FdtDriverMapReg(&Device->Tree, Device->Tree.Node, 1, &AscPhysical, &Device->AscSize);
    Device->Mailbox = FdtDriverMapReg(&Device->Tree, MailboxNode, 0, &MailboxPhysical, &Device->MailboxSize);
    DbgPrint("appleaop: %s, memory %I64x+%lx, processor %I64x+%lx, mailbox %I64x+%lx\n", Device->Config->Compatible,
             Device->AopPhysical, Device->AopSize, AscPhysical, Device->AscSize, MailboxPhysical,
             Device->MailboxSize);
    if (!Device->Aop || !Device->Asc || !Device->Mailbox)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Device->AopSize < AOP_MMIO_SIZE || Device->AscSize < AOP_ASC_SIZE)
    {
        DbgPrint("appleaop: the AOP regions are smaller than expected\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = AopSetupDart(Device);
    if (!NT_SUCCESS(Status))
        return Status;
    AopPatchBootArguments(Device);

    Device->Rtkit.Name = "appleaop";
    Device->Rtkit.Mailbox = Device->Mailbox;
    Device->Rtkit.Context = Device;
    Device->Rtkit.ShmemSetup = AopShmemSetup;
    Device->Rtkit.Receive = AfkReceive;
    KeInitializeEvent(&Device->WorkEvent, SynchronizationEvent, FALSE);
    Status = AppleRtkitQueryInterrupt(&Device->Tree, Device->Tree.Node, &Gsi, &Edge);
    if (NT_SUCCESS(Status))
        AppleRtkitConnectInterrupt(&Device->Rtkit, Raw, Translated, Gsi, &Device->WorkEvent);
    else
        DbgPrint("appleaop: the AOP mailbox declares no receive interrupt (%lx), polling\n", Status);
    Control = READ_REGISTER_ULONG((volatile ULONG *)(Device->Asc + AOP_ASC_CPU_CONTROL));
    WRITE_REGISTER_ULONG((volatile ULONG *)(Device->Asc + AOP_ASC_CPU_CONTROL), Control | AOP_ASC_CPU_CONTROL_RUN);
    DbgPrint("appleaop: processor control %08lx -> %08lx\n", Control,
             READ_REGISTER_ULONG((volatile ULONG *)(Device->Asc + AOP_ASC_CPU_CONTROL)));

    AopLock(Device);
    Status = AppleRtkitWake(&Device->Rtkit, AOP_BOOT_TIMEOUT_MS);
    if (NT_SUCCESS(Status))
    {
        Device->Running = TRUE;
        AfkStartEndpoints(Device);
    }
    AopUnlock(Device);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaop: the AOP did not boot: %lx\n", Status);
        return Status;
    }

    Device->StartedMs = AopNowMs();
    KeInitializeEvent(&Device->StopEvent, NotificationEvent, FALSE);
    Device->Stop = 0;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, AopWorker, Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&Device->Thread,
                                       NULL);
    ZwClose(ThreadHandle);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Device->Stop, 1);
        KeSetEvent(&Device->StopEvent, IO_NO_INCREMENT, FALSE);
        Device->Thread = NULL;
    }
    return Status;
}

static
PAOP_DEVICE
AopChildParent(
    _In_ PVOID Context)
{
    PAOP_CHILD Child = Context;

    ASSERT(Child && Child->Type != AOP_TYPE_BUS && Child->Sensor < AOP_FUNCTIONS);
    return Child->Parent;
}

static
VOID
NTAPI
AopInterfaceReference(
    _In_ PVOID Context)
{
    PAOP_CHILD Child = Context;

    InterlockedIncrement(&Child->References);
}

static
VOID
NTAPI
AopInterfaceDereference(
    _In_ PVOID Context)
{
    PAOP_CHILD Child = Context;
    LONG References = InterlockedDecrement(&Child->References);

    ASSERT(References >= 0);
    UNREFERENCED_PARAMETER(References);
}

static
NTSTATUS
NTAPI
AopInterfaceSubscribe(
    _In_ PVOID Context,
    _In_opt_ PAPPLE_AOP_SAMPLE_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext)
{
    PAOP_CHILD Child = Context;
    PAOP_DEVICE Device = AopChildParent(Context);
    KIRQL OldIrql;

    if (!Device)
        return STATUS_NO_SUCH_DEVICE;
    KeAcquireSpinLock(&Device->SensorLock, &OldIrql);
    Device->Sensors[Child->Sensor].Callback = Callback;
    Device->Sensors[Child->Sensor].CallbackContext = Callback ? CallbackContext : NULL;
    KeReleaseSpinLock(&Device->SensorLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AopInterfaceReadSample(
    _In_ PVOID Context,
    _Out_ PULONG Value)
{
    PAOP_CHILD Child = Context;
    PAOP_DEVICE Device = AopChildParent(Context);
    NTSTATUS Status = STATUS_DEVICE_NOT_READY;
    KIRQL OldIrql;

    *Value = 0;
    if (!Device)
        return STATUS_NO_SUCH_DEVICE;
    KeAcquireSpinLock(&Device->SensorLock, &OldIrql);
    if (Device->Sensors[Child->Sensor].HaveValue)
    {
        *Value = Device->Sensors[Child->Sensor].Value;
        Status = STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Device->SensorLock, OldIrql);
    return Status;
}

static
NTSTATUS
NTAPI
AopInterfaceCall(
    _In_ PVOID Context,
    _In_ USHORT Subtype,
    _In_reads_bytes_(RequestLength) const VOID *Request,
    _In_ ULONG RequestLength,
    _Out_writes_bytes_opt_(ReplyLength) PVOID Reply,
    _In_ ULONG ReplyLength,
    _Out_ PULONG ReturnCode)
{
    PAOP_CHILD Child = Context;
    PAOP_DEVICE Device = AopChildParent(Context);
    LONG Service;
    KIRQL OldIrql;

    *ReturnCode = 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!Device)
        return STATUS_NO_SUCH_DEVICE;
    KeAcquireSpinLock(&Device->SensorLock, &OldIrql);
    Service = Device->Sensors[Child->Sensor].Listening ? Device->Sensors[Child->Sensor].Service : -1;
    KeReleaseSpinLock(&Device->SensorLock, OldIrql);
    if (Service < 0)
        return STATUS_DEVICE_NOT_READY;
    return AopEpicCall(Device, (ULONG)Service, Subtype, Request, RequestLength, Reply, ReplyLength, NULL, ReturnCode);
}

static
NTSTATUS
AopCopyId(
    _In_z_ PCWSTR Source,
    _In_ BOOLEAN MultiString,
    _Out_ PULONG_PTR Information)
{
    SIZE_T Size = (wcslen(Source) + (MultiString ? 2 : 1)) * sizeof(WCHAR);
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, AOP_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Copy, Size);
    RtlCopyMemory(Copy, Source, wcslen(Source) * sizeof(WCHAR));
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AopChildQueryInterface(
    _In_ PAOP_CHILD Child,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ NTSTATUS Status)
{
    PAPPLE_AOP_FUNCTION_INTERFACE Interface;

    if (!IsEqualGUID(Stack->Parameters.QueryInterface.InterfaceType, &GUID_APPLE_AOP_FUNCTION_INTERFACE))
        return Status;
    if (Stack->Parameters.QueryInterface.Size < sizeof(*Interface) ||
        Stack->Parameters.QueryInterface.Version < APPLE_AOP_FUNCTION_INTERFACE_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Child->Parent)
        return STATUS_NO_SUCH_DEVICE;
    Interface = (PAPPLE_AOP_FUNCTION_INTERFACE)Stack->Parameters.QueryInterface.Interface;
    RtlZeroMemory(Interface, sizeof(*Interface));
    Interface->Size = sizeof(*Interface);
    Interface->Version = APPLE_AOP_FUNCTION_INTERFACE_VERSION;
    Interface->Context = Child;
    Interface->InterfaceReference = AopInterfaceReference;
    Interface->InterfaceDereference = AopInterfaceDereference;
    Interface->Function = AopChildInfo[Child->Sensor].Function;
    Interface->Subscribe = AopInterfaceSubscribe;
    Interface->ReadSample = AopInterfaceReadSample;
    Interface->Call = AopInterfaceCall;
    AopInterfaceReference(Child);
    return STATUS_SUCCESS;
}

static
VOID
AopChildUnsubscribe(
    _In_ PAOP_CHILD Child)
{
    KIRQL OldIrql;

    if (!Child->Parent)
        return;
    KeAcquireSpinLock(&Child->Parent->SensorLock, &OldIrql);
    Child->Parent->Sensors[Child->Sensor].Callback = NULL;
    Child->Parent->Sensors[Child->Sensor].CallbackContext = NULL;
    KeReleaseSpinLock(&Child->Parent->SensorLock, OldIrql);
}

static
NTSTATUS
AopChildPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAOP_CHILD Child = DeviceObject->DeviceExtension;
    const AOP_CHILD_INFO *Info = &AopChildInfo[Child->Sensor];
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;
    BOOLEAN Delete;

    ASSERT(Child->Self == DeviceObject && Child->Sensor < AOP_FUNCTIONS);
    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                    Status = AopCopyId(Info->Id, FALSE, &Irp->IoStatus.Information);
                    break;
                case BusQueryHardwareIDs:
                    Status = AopCopyId(Info->Id, TRUE, &Irp->IoStatus.Information);
                    break;
                case BusQueryInstanceID:
                    Status = AopCopyId(L"0", FALSE, &Irp->IoStatus.Information);
                    break;
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
                Status = AopCopyId(Info->Description, FALSE, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 || Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                PDEVICE_RELATIONS Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), AOP_TAG);

                if (!Relations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Relations->Count = 1;
                Relations->Objects[0] = DeviceObject;
                ObReferenceObject(DeviceObject);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_QUERY_INTERFACE:
            Status = AopChildQueryInterface(Child, Stack, Status);
            break;

        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            AopChildUnsubscribe(Child);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            AopChildUnsubscribe(Child);
            Status = STATUS_SUCCESS;
            Delete = FALSE;
            if (!Child->Parent)
            {
                Delete = TRUE;
            }
            else
            {
                ExAcquireFastMutex(&Child->Parent->ChildLock);
                if (!Child->Parent->ChildrenReported)
                {
                    Child->Parent->Sensors[Child->Sensor].Child = NULL;
                    Delete = TRUE;
                }
                ExReleaseFastMutex(&Child->Parent->ChildLock);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            if (Delete)
                IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
AopCompletion(
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
AopBusRelations(
    _Inout_ PAOP_DEVICE Device,
    _Inout_ PIRP Irp)
{
    PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
    PDEVICE_RELATIONS Relations;
    ULONG Count = Previous ? Previous->Count : 0, Sensor;

    ExAcquireFastMutex(&Device->ChildLock);
    Relations = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + AOP_FUNCTIONS + 1]),
                                      AOP_TAG);
    if (!Relations)
    {
        ExReleaseFastMutex(&Device->ChildLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Relations->Count = Count;
    if (Count)
        RtlCopyMemory(Relations->Objects, Previous->Objects, Count * sizeof(PDEVICE_OBJECT));
    for (Sensor = 0; Sensor < AOP_FUNCTIONS && Device->ChildrenReported; ++Sensor)
    {
        if (!Device->Sensors[Sensor].Child)
            continue;
        Relations->Objects[Relations->Count++] = Device->Sensors[Sensor].Child;
        ObReferenceObject(Device->Sensors[Sensor].Child);
    }
    ExReleaseFastMutex(&Device->ChildLock);
    if (Previous)
        ExFreePool(Previous);
    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    return STATUS_SUCCESS;
}

static
VOID
AopDeleteChildren(
    _Inout_ PAOP_DEVICE Device)
{
    PDEVICE_OBJECT Children[AOP_FUNCTIONS];
    PAOP_CHILD Child;
    ULONG Sensor;

    ExAcquireFastMutex(&Device->ChildLock);
    Device->ChildrenReported = FALSE;
    for (Sensor = 0; Sensor < AOP_FUNCTIONS; ++Sensor)
    {
        Children[Sensor] = Device->Sensors[Sensor].Child;
        Device->Sensors[Sensor].Child = NULL;
    }
    ExReleaseFastMutex(&Device->ChildLock);
    for (Sensor = 0; Sensor < AOP_FUNCTIONS; ++Sensor)
    {
        if (!Children[Sensor])
            continue;
        Child = Children[Sensor]->DeviceExtension;
        AopChildUnsubscribe(Child);
        Child->Parent = NULL;
        IoDeleteDevice(Children[Sensor]);
    }
}

static
NTSTATUS
NTAPI
AopPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAOP_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT Lower;
    KEVENT Event;
    NTSTATUS Status;

    if (AOP_IS_CHILD(DeviceObject))
        return AopChildPnp(DeviceObject, Irp);
    Lower = Device->LowerDevice;
    switch (Stack->MinorFunction)
    {
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, AopCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Lower, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED)
            {
                FDT_DRIVER_TREE Tree;
                ULONG Gsi = 0;
                BOOLEAN Edge = FALSE;
                NTSTATUS FilterStatus = FdtDriverOpen(Device->Pdo, &Tree);

                if (NT_SUCCESS(FilterStatus))
                {
                    FilterStatus = AppleRtkitQueryInterrupt(&Tree, Tree.Node, &Gsi, &Edge);
                    FdtDriverClose(&Tree);
                }
                if (NT_SUCCESS(FilterStatus))
                    FilterStatus = AppleAddInterruptRequirement(Irp, Gsi, Edge);
                DbgPrint("appleaop: requesting the mailbox interrupt %lu: %lx\n", Gsi, FilterStatus);
                if (NT_SUCCESS(FilterStatus))
                    Status = STATUS_SUCCESS;
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, AopCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Lower, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
            {
                Status = AopStartHardware(Device, Stack->Parameters.StartDevice.AllocatedResources,
                                          Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
                if (!NT_SUCCESS(Status))
                    AopStopHardware(Device);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                Status = AopBusRelations(Device, Irp);
                Irp->IoStatus.Status = Status;
                if (!NT_SUCCESS(Status))
                {
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }
            }
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            AopStopHardware(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            AopDeleteChildren(Device);
            AopStopHardware(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Lower, Irp);
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
AopPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAOP_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (AOP_IS_CHILD(DeviceObject))
    {
        Status = Irp->IoStatus.Status;
        if (Stack->MinorFunction == IRP_MN_SET_POWER || Stack->MinorFunction == IRP_MN_QUERY_POWER)
            Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AopPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAOP_DEVICE Device = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (AOP_IS_CHILD(DeviceObject))
    {
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AopCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AopAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT DeviceObject;
    PAOP_DEVICE Device;
    ULONG Sensor;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(AOP_DEVICE), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    Device = DeviceObject->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->Type = AOP_TYPE_BUS;
    Device->DeviceObject = DeviceObject;
    Device->Pdo = Pdo;
    Device->DartNode = FDT_NO_NODE;
    ExInitializeFastMutex(&Device->Lock);
    ExInitializeFastMutex(&Device->ChildLock);
    KeInitializeSpinLock(&Device->SensorLock);
    for (Sensor = 0; Sensor < AOP_FUNCTIONS; ++Sensor)
        Device->Sensors[Sensor].Service = -1;
    Device->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, Pdo);
    if (!Device->LowerDevice)
    {
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
AopUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = AopCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AopCreateClose;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AopPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AopPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = AopPassThrough;
    DriverObject->DriverExtension->AddDevice = AopAddDevice;
    DriverObject->DriverUnload = AopUnload;
    return STATUS_SUCCESS;
}
