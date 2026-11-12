/*
 * PROJECT:     LiberNT Apple CPU Performance Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance states of the Apple silicon CPU clusters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/exfuncs.h>
#include <ntstrsafe.h>
#include <reactos/arm64/sysreg.h>
#include <reactos/drivers/fdtdrv.h>
#include "governor.h"

#define CPUFREQ_FAILURES_LOGGED     3
#define CPUFREQ_100NS_PER_MS        10000ULL

typedef struct _APPLE_CPUFREQ
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    PUCHAR Base;
    ULONG Size;
    ULONG64 Address;
    CPUFREQ_CPUS Cpus;
    CPUFREQ_TABLE Table;
    ULONG ProcessorCount;
    KAFFINITY Processors;
    KAFFINITY Housekeeping;
    CPUFREQ_GOVERNOR Governor;
    PKTHREAD Thread;
    KEVENT StopEvent;
    KTIMER Timer;
    BOOLEAN QueryFailed;
    ULONG64 IdleCycles[CPUFREQ_MAX_CPUS];
    ULONG64 TotalCycles[CPUFREQ_MAX_CPUS];
    ULONG64 NextIdleCycles[CPUFREQ_MAX_CPUS];
    ULONG64 NextTotalCycles[CPUFREQ_MAX_CPUS];
    ULONG64 LastSampleTime;
    ULONG64 ReportStart;
    ULONG64 StateTime[CPUFREQ_MAX_STATES];
    ULONG64 BusyTime;
    ULONG BusyPeak;
    ULONG Samples;
    ULONG Changes;
    ULONG Failures;
} APPLE_CPUFREQ, *PAPPLE_CPUFREQ;

static KSTART_ROUTINE CpufreqGovernorThread;
static KIPI_BROADCAST_WORKER CpufreqReadMpidr;

static
ULONG
CpufreqMhz(
    _In_opt_ const CPUFREQ_STATE *State)
{
    return State ? (ULONG)(State->Hz / 1000000) : 0;
}

static
ULONG_PTR
NTAPI
CpufreqReadMpidr(
    _In_ ULONG_PTR Argument)
{
    PULONG64 Mpidr = (PULONG64)Argument;
    ULONG Processor = KeGetCurrentProcessorNumberEx(NULL);

    if (Processor < CPUFREQ_MAX_CPUS)
        Mpidr[Processor] = ARM64_READ_SYSREG(MPIDR_EL1);
    return 0;
}

static
BOOLEAN
CpufreqSampleBusy(
    _Inout_ PAPPLE_CPUFREQ Cpufreq,
    _Out_ PULONG BusyPermille)
{
    ULONG Size = Cpufreq->ProcessorCount * sizeof(ULONG64);
    ULONG Processor, Busy, Returned;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Cpufreq->ProcessorCount && Cpufreq->ProcessorCount <= CPUFREQ_MAX_CPUS);

    *BusyPermille = 0;
    Status = ZwQuerySystemInformation(SystemProcessorIdleCycleTimeInformation, Cpufreq->NextIdleCycles, Size,
                                      &Returned);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQuerySystemInformation(SystemProcessorCycleTimeInformation, Cpufreq->NextTotalCycles, Size,
                                          &Returned);
    }
    if (!NT_SUCCESS(Status))
    {
        if (!Cpufreq->QueryFailed)
            DbgPrint("applecpufreq: cluster %I64x cannot read the processor cycle times: %lx\n", Cpufreq->Address,
                     Status);
        Cpufreq->QueryFailed = TRUE;
        return FALSE;
    }
    for (Processor = 0; Processor < Cpufreq->ProcessorCount; ++Processor)
    {
        if (Cpufreq->Processors & ((KAFFINITY)1 << Processor))
        {
            Busy = CpufreqBusyPermille(Cpufreq->NextIdleCycles[Processor] - Cpufreq->IdleCycles[Processor],
                                       Cpufreq->NextTotalCycles[Processor] - Cpufreq->TotalCycles[Processor]);
            *BusyPermille = max(*BusyPermille, Busy);
        }
        Cpufreq->IdleCycles[Processor] = Cpufreq->NextIdleCycles[Processor];
        Cpufreq->TotalCycles[Processor] = Cpufreq->NextTotalCycles[Processor];
    }
    ASSERT(*BusyPermille <= 1000);
    return TRUE;
}

static
VOID
CpufreqReport(
    _Inout_ PAPPLE_CPUFREQ Cpufreq,
    _In_ ULONG64 Now,
    _In_ const CPUFREQ_READING *Reading)
{
    CHAR Line[480];
    PCHAR End;
    size_t Remaining;
    ULONG64 Total = 0, Share;
    ULONG Index, Average;

    PAGED_CODE();

    for (Index = 0; Index < Cpufreq->Table.Count; ++Index)
        Total += Cpufreq->StateTime[Index];
    if (!Total)
        return;
    Average = (ULONG)(Cpufreq->BusyTime / Total);
    RtlStringCbPrintfExA(Line, sizeof(Line), &End, &Remaining, 0,
                         "applecpufreq: cluster %I64x, last %I64u s: busiest CPU %lu.%lu%% busy (peak %lu.%lu%%), "
                         "%lu samples, %lu changes, %lu failed, MHz:%%",
                         Cpufreq->Address, (Now - Cpufreq->ReportStart) / (1000 * CPUFREQ_100NS_PER_MS),
                         Average / 10, Average % 10, Cpufreq->BusyPeak / 10, Cpufreq->BusyPeak % 10,
                         Cpufreq->Samples, Cpufreq->Changes, Cpufreq->Failures);
    for (Index = 0; Index < Cpufreq->Table.Count; ++Index)
    {
        if (!Cpufreq->StateTime[Index])
            continue;
        Share = (Cpufreq->StateTime[Index] * 1000) / Total;
        RtlStringCbPrintfExA(End, Remaining, &End, &Remaining, 0, " %lu:%lu.%lu",
                             CpufreqMhz(&Cpufreq->Table.State[Index]), (ULONG)(Share / 10), (ULONG)(Share % 10));
    }
    RtlStringCbPrintfExA(End, Remaining, &End, &Remaining, 0, "; now state %lu (%lu MHz), next sample in %lu ms\n",
                         Reading->Current, CpufreqMhz(CpufreqFindLevel(&Cpufreq->Table, Reading->Current)),
                         CpufreqGovernorPeriodMs(&Cpufreq->Governor));
    DbgPrint("%s", Line);

    RtlZeroMemory(Cpufreq->StateTime, sizeof(Cpufreq->StateTime));
    Cpufreq->BusyTime = 0;
    Cpufreq->BusyPeak = 0;
    Cpufreq->Samples = 0;
    Cpufreq->Changes = 0;
    Cpufreq->Failures = 0;
    Cpufreq->ReportStart = Now;
}

static
VOID
CpufreqGovern(
    _Inout_ PAPPLE_CPUFREQ Cpufreq)
{
    CPUFREQ_READING Reading, After;
    ULONG64 Now, Elapsed, QpcTimeStamp;
    ULONG Busy, Running, Desired, Polls, Actual;
    NTSTATUS Status;

    PAGED_CODE();

    Now = KeQueryInterruptTimePrecise(&QpcTimeStamp);
    ASSERT(Now >= Cpufreq->LastSampleTime);
    Elapsed = Now - Cpufreq->LastSampleTime;
    Cpufreq->LastSampleTime = Now;
    if (!CpufreqSampleBusy(Cpufreq, &Busy))
        return;

    CpufreqRead(Cpufreq->Base, &Reading);
    Running = CpufreqFindIndex(&Cpufreq->Table, Reading.Current);
    if (Running == CPUFREQ_NO_INDEX)
        Running = Cpufreq->Governor.Index;
    ASSERT(Running < Cpufreq->Table.Count);

    Cpufreq->StateTime[Running] += Elapsed;
    Cpufreq->BusyTime += (ULONG64)Busy * Elapsed;
    Cpufreq->BusyPeak = max(Cpufreq->BusyPeak, Busy);
    Cpufreq->Samples++;

    Desired = CpufreqGovernorUpdate(&Cpufreq->Governor, &Cpufreq->Table, Running, Busy);
    if (Cpufreq->Table.State[Desired].Level != Reading.Target)
    {
        Status = CpufreqRequest(Cpufreq->Base, Cpufreq->Table.State[Desired].Level, &After, &Polls);
        if (NT_SUCCESS(Status))
        {
            Cpufreq->Changes++;
            Reading = After;
        }
        else
        {
            if (++Cpufreq->Failures <= CPUFREQ_FAILURES_LOGGED)
            {
                DbgPrint("applecpufreq: cluster %I64x could not switch to state %lu (%lu MHz): %lx, at state %lu, "
                         "target %lu, command %I64x, %lu polls\n",
                         Cpufreq->Address, Cpufreq->Table.State[Desired].Level,
                         CpufreqMhz(&Cpufreq->Table.State[Desired]), Status, After.Current, After.Target,
                         After.Command, Polls);
            }
            Actual = CpufreqFindIndex(&Cpufreq->Table, After.Current);
            CpufreqGovernorInitialize(&Cpufreq->Governor, &Cpufreq->Table,
                                      (Actual == CPUFREQ_NO_INDEX) ? Running : Actual);
            Reading = After;
        }
    }

    if (Now - Cpufreq->ReportStart >= CPUFREQ_REPORT_PERIOD_MS * CPUFREQ_100NS_PER_MS)
        CpufreqReport(Cpufreq, Now, &Reading);
}

static
VOID
NTAPI
CpufreqGovernorThread(
    _In_ PVOID Context)
{
    PAPPLE_CPUFREQ Cpufreq = Context;
    PVOID Objects[2];
    LARGE_INTEGER DueTime;
    ULONG64 QpcTimeStamp;
    ULONG Busy, Period;
    NTSTATUS Status;

    PAGED_CODE();

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
    KeSetSystemAffinityThreadEx(Cpufreq->Housekeeping);
    Objects[0] = &Cpufreq->StopEvent;
    Objects[1] = &Cpufreq->Timer;
    Cpufreq->LastSampleTime = KeQueryInterruptTimePrecise(&QpcTimeStamp);
    Cpufreq->ReportStart = Cpufreq->LastSampleTime;
    (VOID)CpufreqSampleBusy(Cpufreq, &Busy);

    for (;;)
    {
        Period = CpufreqGovernorPeriodMs(&Cpufreq->Governor);
        DueTime.QuadPart = -(LONGLONG)(Period * CPUFREQ_100NS_PER_MS);
        KeSetCoalescableTimer(&Cpufreq->Timer, DueTime, 0, Period / 4, NULL);
        Status = KeWaitForMultipleObjects(RTL_NUMBER_OF(Objects), Objects, WaitAny, Executive, KernelMode, FALSE,
                                          NULL, NULL);
        if (Status != STATUS_WAIT_1)
            break;
        CpufreqGovern(Cpufreq);
    }
    ASSERT(Status == STATUS_WAIT_0);
    KeCancelTimer(&Cpufreq->Timer);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
CpufreqStop(
    _Inout_ PAPPLE_CPUFREQ Cpufreq)
{
    PAGED_CODE();

    if (Cpufreq->Thread)
    {
        KeSetEvent(&Cpufreq->StopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Cpufreq->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Cpufreq->Thread);
        Cpufreq->Thread = NULL;
    }
    if (Cpufreq->Base)
        MmUnmapIoSpace(Cpufreq->Base, Cpufreq->Size);
    Cpufreq->Base = NULL;
    Cpufreq->Size = 0;
    if (Cpufreq->TreeOpen)
        FdtDriverClose(&Cpufreq->Tree);
    Cpufreq->TreeOpen = FALSE;
    RtlZeroMemory(&Cpufreq->Cpus, sizeof(Cpufreq->Cpus));
}

static
NTSTATUS
CpufreqStartGovernor(
    _Inout_ PAPPLE_CPUFREQ Cpufreq,
    _In_ ULONG Index)
{
    ULONG64 Mpidr[CPUFREQ_MAX_CPUS];
    CPUFREQ_CPUS Efficient;
    CPUFREQ_READING Reading;
    OBJECT_ATTRIBUTES Attributes;
    KAFFINITY Housekeeping;
    HANDLE Handle;
    ULONG Processor, Polls;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(!Cpufreq->Thread);
    ASSERT(Index < Cpufreq->Table.Count);

    Cpufreq->ProcessorCount = min(KeQueryActiveProcessorCount(NULL), CPUFREQ_MAX_CPUS);
    for (Processor = 0; Processor < CPUFREQ_MAX_CPUS; ++Processor)
        Mpidr[Processor] = MAXULONG64;
    KeIpiGenericCall(CpufreqReadMpidr, (ULONG_PTR)Mpidr);
    Cpufreq->Processors = (KAFFINITY)CpufreqMapProcessors(Mpidr, Cpufreq->ProcessorCount, &Cpufreq->Cpus);
    if (!Cpufreq->Processors)
    {
        Status = CpufreqRequest(Cpufreq->Base, Cpufreq->Table.State[0].Level, &Reading, &Polls);
        DbgPrint("applecpufreq: cluster %I64x has none of its %lu CPUs running, parked at state %lu (%lu MHz): %lx, "
                 "now at state %lu\n",
                 Cpufreq->Address, Cpufreq->Cpus.Count, Cpufreq->Table.State[0].Level,
                 CpufreqMhz(&Cpufreq->Table.State[0]), Status, Reading.Current);
        return STATUS_SUCCESS;
    }
    Cpufreq->Housekeeping = Cpufreq->Processors;
    if (NT_SUCCESS(CpufreqFindEfficientCpus(&Cpufreq->Tree.Fdt, &Efficient)))
    {
        Housekeeping = (KAFFINITY)CpufreqMapProcessors(Mpidr, Cpufreq->ProcessorCount, &Efficient);
        if (Housekeeping)
            Cpufreq->Housekeeping = Housekeeping;
    }

    CpufreqGovernorInitialize(&Cpufreq->Governor, &Cpufreq->Table, Index);
    KeInitializeEvent(&Cpufreq->StopEvent, NotificationEvent, FALSE);
    KeInitializeTimerEx(&Cpufreq->Timer, SynchronizationTimer);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, CpufreqGovernorThread, Cpufreq);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(Handle, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Cpufreq->Thread,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        Cpufreq->Thread = NULL;
        KeSetEvent(&Cpufreq->StopEvent, IO_NO_INCREMENT, FALSE);
        ZwWaitForSingleObject(Handle, FALSE, NULL);
    }
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    DbgPrint("applecpufreq: cluster %I64x governs processors %Ix from processors %Ix: target %lu%% busy, top state "
             "from %lu%%, lower after %lu samples, every %lu ms (%lu ms once idle), report every %lu s\n",
             Cpufreq->Address, Cpufreq->Processors, Cpufreq->Housekeeping, CPUFREQ_TARGET_PERMILLE / 10,
             CPUFREQ_RACE_PERMILLE / 10, CPUFREQ_DECREASE_SAMPLES, CPUFREQ_BUSY_PERIOD_MS, CPUFREQ_IDLE_PERIOD_MS,
             CPUFREQ_REPORT_PERIOD_MS / 1000);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CpufreqStart(
    _Inout_ PAPPLE_CPUFREQ Cpufreq)
{
    const CPUFREQ_STATE *Highest;
    CPUFREQ_READING Before, After;
    const VOID *Compatible;
    ULONG TableNode, Length, Index, Polls;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(!Cpufreq->TreeOpen && !Cpufreq->Base && !Cpufreq->Thread);

    Status = FdtDriverOpen(Cpufreq->Pdo, &Cpufreq->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Cpufreq->TreeOpen = TRUE;
    Compatible = FdtGetProperty(&Cpufreq->Tree.Fdt, Cpufreq->Tree.Node, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8112-cluster-cpufreq"))
        return STATUS_NOT_SUPPORTED;
    Status = CpufreqFindCpus(&Cpufreq->Tree.Fdt, Cpufreq->Tree.Node, &Cpufreq->Cpus);
    if (!NT_SUCCESS(Status))
        return Status;
    TableNode = FdtDriverFindPhandle(&Cpufreq->Tree, Cpufreq->Cpus.TablePhandle);
    if (TableNode == FDT_NO_NODE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Status = CpufreqParseTable(&Cpufreq->Tree.Fdt, TableNode, &Cpufreq->Table);
    if (!NT_SUCCESS(Status))
        return Status;
    Cpufreq->Base = FdtDriverMapReg(&Cpufreq->Tree, Cpufreq->Tree.Node, 0, &Cpufreq->Address, &Cpufreq->Size);
    if (!Cpufreq->Base)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Cpufreq->Size < CPUFREQ_REGISTER_SPAN)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    DbgPrint("applecpufreq: cluster %I64x runs CPUs %I64x..%I64x (%lu) with %lu performance states\n",
             Cpufreq->Address, Cpufreq->Cpus.First, Cpufreq->Cpus.Last, Cpufreq->Cpus.Count, Cpufreq->Table.Count);
    for (Index = 0; Index < Cpufreq->Table.Count; ++Index)
    {
        DbgPrint("applecpufreq: cluster %I64x state %lu = %lu MHz, %lu ns to switch\n", Cpufreq->Address,
                 Cpufreq->Table.State[Index].Level, CpufreqMhz(&Cpufreq->Table.State[Index]),
                 Cpufreq->Table.State[Index].LatencyNs);
    }
    CpufreqRead(Cpufreq->Base, &Before);
    DbgPrint("applecpufreq: cluster %I64x is at state %lu (%lu MHz), target %lu, command %I64x, status %08lx\n",
             Cpufreq->Address, Before.Current, CpufreqMhz(CpufreqFindLevel(&Cpufreq->Table, Before.Current)),
             Before.Target, Before.Command, Before.Status);

    /* TODO: move the policy into the kernel processor power manager once it issues PEP_NOTIFY_PPM_PERF_SET */
    Highest = &Cpufreq->Table.State[Cpufreq->Table.Count - 1];
    Status = CpufreqRequest(Cpufreq->Base, Highest->Level, &After, &Polls);
    DbgPrint("applecpufreq: cluster %I64x asked for state %lu (%lu MHz): %s, now at state %lu (%lu MHz), "
             "target %lu, command %I64x, status %08lx, %lu polls, %lx\n",
             Cpufreq->Address, Highest->Level, CpufreqMhz(Highest),
             (NT_SUCCESS(Status) && After.Current == Highest->Level) ? "reached" : "NOT reached", After.Current,
             CpufreqMhz(CpufreqFindLevel(&Cpufreq->Table, After.Current)), After.Target, After.Command,
             After.Status, Polls, Status);
    if (!NT_SUCCESS(Status))
        return Status;
    Index = CpufreqFindIndex(&Cpufreq->Table, After.Current);
    Status = CpufreqStartGovernor(Cpufreq, (Index == CPUFREQ_NO_INDEX) ? Cpufreq->Table.Count - 1 : Index);
    if (!NT_SUCCESS(Status))
        DbgPrint("applecpufreq: cluster %I64x has no governor: %lx\n", Cpufreq->Address, Status);
    return Status;
}

static
NTSTATUS
NTAPI
CpufreqPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_CPUFREQ Cpufreq = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    PAGED_CODE();

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Cpufreq->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
            {
                Status = CpufreqStart(Cpufreq);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("applecpufreq: cluster %I64x did not start: %lx\n", Cpufreq->Address, Status);
                    CpufreqStop(Cpufreq);
                }
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            CpufreqStop(Cpufreq);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            CpufreqStop(Cpufreq);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Cpufreq->Lower, Irp);
            IoDetachDevice(Cpufreq->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Cpufreq->Lower, Irp);
}

static
NTSTATUS
NTAPI
CpufreqPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_CPUFREQ Cpufreq = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Cpufreq->Lower, Irp);
}

static
NTSTATUS
NTAPI
CpufreqPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_CPUFREQ Cpufreq = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Cpufreq->Lower, Irp);
}

static
NTSTATUS
NTAPI
CpufreqAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PAPPLE_CPUFREQ Cpufreq;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_CPUFREQ), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Cpufreq = Fdo->DeviceExtension;
    RtlZeroMemory(Cpufreq, sizeof(*Cpufreq));
    Cpufreq->Self = Fdo;
    Cpufreq->Pdo = Pdo;
    Cpufreq->Tree.Node = FDT_NO_NODE;
    Cpufreq->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Cpufreq->Lower)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = CpufreqPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = CpufreqPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = CpufreqPower;
    DriverObject->DriverExtension->AddDevice = CpufreqAddDevice;
    return STATUS_SUCCESS;
}
