/*
 * PROJECT:     LiberNT Apple CPU Performance Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance states of the Apple silicon CPU clusters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/fdtdrv.h>
#include "cluster.h"

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
} APPLE_CPUFREQ, *PAPPLE_CPUFREQ;

static
ULONG
CpufreqMhz(
    _In_opt_ const CPUFREQ_STATE *State)
{
    return State ? (ULONG)(State->Hz / 1000000) : 0;
}

static
VOID
CpufreqStop(
    _Inout_ PAPPLE_CPUFREQ Cpufreq)
{
    PAGED_CODE();

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
CpufreqStart(
    _Inout_ PAPPLE_CPUFREQ Cpufreq)
{
    const CPUFREQ_STATE *Highest;
    CPUFREQ_READING Before, After;
    const VOID *Compatible;
    ULONG TableNode, Length, Index, Polls;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(!Cpufreq->TreeOpen && !Cpufreq->Base);

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

    /* TODO: demand-based scaling through PEP_NOTIFY_PPM_PERF_SET; the cluster stays at its highest state */
    Highest = &Cpufreq->Table.State[Cpufreq->Table.Count - 1];
    Status = CpufreqRequest(Cpufreq->Base, Highest->Level, &After, &Polls);
    DbgPrint("applecpufreq: cluster %I64x asked for state %lu (%lu MHz): %s, now at state %lu (%lu MHz), "
             "target %lu, command %I64x, status %08lx, %lu polls, %lx\n",
             Cpufreq->Address, Highest->Level, CpufreqMhz(Highest),
             (NT_SUCCESS(Status) && After.Current == Highest->Level) ? "reached" : "NOT reached", After.Current,
             CpufreqMhz(CpufreqFindLevel(&Cpufreq->Table, After.Current)), After.Target, After.Command,
             After.Status, Polls, Status);
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
