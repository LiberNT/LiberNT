/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Identification of the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applepmp.h>
#include <reactos/drivers/fdtdrv.h>

#define AGX_TAG                     'xgAA'
#define AGX_ASC_CONTROL             0x44
#define AGX_ASC_CONTROL_RUN         0x10
#define AGX_SGX_ID_WINDOW           0xd04000
#define AGX_SGX_CORE_WINDOW         0xe00000
#define AGX_SGX_WINDOW_SIZE         0x4000
#define AGX_ID_VERSION              0x00
#define AGX_ID_UNKNOWN08            0x08
#define AGX_ID_COUNTS1              0x10
#define AGX_ID_COUNTS2              0x14
#define AGX_ID_UNKNOWN18            0x18
#define AGX_ID_CLUSTERS             0x1c
#define AGX_CORE_MASKS              0x1500
#define AGX_MAX_CORE_MASKS          3

typedef struct _APPLE_AGX
{
    PFILE_OBJECT PmpFile;
    PDEVICE_OBJECT Pmp;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    PUCHAR Asc;
    ULONG AscSize;
    PUCHAR Id;
    ULONG IdSize;
    PUCHAR Core;
    ULONG CoreSize;
} APPLE_AGX, *PAPPLE_AGX;

static
PCSTR
AgxGeneration(
    _In_ ULONG Version)
{
    switch ((Version >> 24) & 0xff)
    {
        case 4: return "G13";
        case 5: return "G14";
        case 6: return "G14X";
        case 7: return "G15";
        default: return "unknown";
    }
}

static
PCSTR
AgxVariant(
    _In_ ULONG Version)
{
    switch ((Version >> 16) & 0xff)
    {
        case 1: return "P";
        case 2: return "G";
        case 3: return "S";
        case 4: return "C";
        default: return "unknown";
    }
}

/* The graphics block answers only after the power processor has voted for it, so a read without the
 * vote can fault the machine. */
static
NTSTATUS
AgxPowerVote(
    _Inout_ PAPPLE_AGX Agx,
    _In_ BOOLEAN Enable)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_PMP_DEVICE_NAME);
    APPLE_PMP_POWER_REQUEST Request;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    if (!Agx->Pmp)
    {
        Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &Agx->PmpFile, &Agx->Pmp);
        if (!NT_SUCCESS(Status))
        {
            Agx->Pmp = NULL;
            Agx->PmpFile = NULL;
            return Status;
        }
    }
    Request.Device = APPLE_PMP_DEVICE_GRAPHICS;
    Request.Enable = Enable;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_APPLE_PMP_SET_POWER, Agx->Pmp, &Request, sizeof(Request), NULL, 0,
                                        FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(Agx->Pmp, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
VOID
AgxStop(
    _Inout_ PAPPLE_AGX Agx)
{
    if (Agx->Asc)
        AgxPowerVote(Agx, FALSE);
    if (Agx->PmpFile)
        ObDereferenceObject(Agx->PmpFile);
    Agx->PmpFile = NULL;
    Agx->Pmp = NULL;
    if (Agx->Core)
        MmUnmapIoSpace(Agx->Core, Agx->CoreSize);
    Agx->Core = NULL;
    if (Agx->Id)
        MmUnmapIoSpace(Agx->Id, Agx->IdSize);
    Agx->Id = NULL;
    if (Agx->Asc)
        MmUnmapIoSpace(Agx->Asc, Agx->AscSize);
    Agx->Asc = NULL;
    if (Agx->TreeOpen)
        FdtDriverClose(&Agx->Tree);
    Agx->TreeOpen = FALSE;
}

static
PUCHAR
AgxMapWindow(
    _In_ ULONG64 Base,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = (LONGLONG)(Base + Offset);
    return MmMapIoSpace(Address, Size, MmNonCached);
}

static
NTSTATUS
AgxStart(
    _Inout_ PAPPLE_AGX Agx)
{
    ULONG64 AscBase, AscLength, SgxBase, SgxLength;
    ULONG Version, Counts1, Clusters, Control, Index, Mask;
    ULONG Masks[AGX_MAX_CORE_MASKS] = { 0 };
    ULONG CoresPerCluster, ClusterCount, Active = 0;
    NTSTATUS Status;

    Status = FdtDriverOpen(Agx->Pdo, &Agx->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Agx->TreeOpen = TRUE;
    if (!FdtDriverReadReg(&Agx->Tree, Agx->Tree.Node, 0, &AscBase, &AscLength) ||
        !FdtDriverReadReg(&Agx->Tree, Agx->Tree.Node, 1, &SgxBase, &SgxLength) ||
        AscLength < AGX_ASC_CONTROL + sizeof(ULONG) ||
        SgxLength < AGX_SGX_CORE_WINDOW + AGX_SGX_WINDOW_SIZE)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = ApplePmgrPowerOnDevice(&Agx->Tree, Agx->Tree.Node);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the graphics power domain did not turn on: %lx\n", Status);
        return Status;
    }
    Status = AgxPowerVote(Agx, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the power processor did not vote for the graphics block: %lx\n", Status);
        return Status;
    }

    Agx->AscSize = AGX_SGX_WINDOW_SIZE;
    Agx->IdSize = AGX_SGX_WINDOW_SIZE;
    Agx->CoreSize = AGX_SGX_WINDOW_SIZE;
    Agx->Asc = AgxMapWindow(AscBase, 0, Agx->AscSize);
    Agx->Id = AgxMapWindow(SgxBase, AGX_SGX_ID_WINDOW, Agx->IdSize);
    Agx->Core = AgxMapWindow(SgxBase, AGX_SGX_CORE_WINDOW, Agx->CoreSize);
    if (!Agx->Asc || !Agx->Id || !Agx->Core)
        return STATUS_INSUFFICIENT_RESOURCES;

    Control = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Asc + AGX_ASC_CONTROL));
    Version = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_VERSION));
    Counts1 = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_COUNTS1));
    Clusters = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_CLUSTERS));
    DbgPrint("appleagx: identity %08lx %08lx %08lx %08lx %08lx %08lx\n", Version,
             READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_UNKNOWN08)), Counts1,
             READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_COUNTS2)),
             READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_UNKNOWN18)), Clusters);

    CoresPerCluster = Counts1 & 0xff;
    ClusterCount = ((Counts1 >> 8) & 0xff) * ((Counts1 >> 16) & 0xf);
    for (Index = 0; Index < AGX_MAX_CORE_MASKS; ++Index)
    {
        Masks[Index] = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Core + AGX_CORE_MASKS + 4 * Index));
        for (Mask = Masks[Index]; Mask; Mask &= Mask - 1)
            ++Active;
    }
    DbgPrint("appleagx: %s%s revision %lu.%lu, %lu clusters of %lu cores, %lu active (masks %08lx %08lx %08lx)\n",
             AgxGeneration(Version), AgxVariant(Version), (Version >> 8) & 0xff, Version & 0xff, ClusterCount,
             CoresPerCluster, Active, Masks[0], Masks[1], Masks[2]);
    DbgPrint("appleagx: coprocessor control %08lx (%s), firmware not started\n", Control,
             (Control & AGX_ASC_CONTROL_RUN) ? "running" : "halted");
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AgxPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_AGX Agx = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Agx->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
            {
                Status = AgxStart(Agx);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("appleagx: the graphics processor was not identified: %lx\n", Status);
                    AgxStop(Agx);
                }
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            AgxStop(Agx);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            AgxStop(Agx);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Agx->Lower, Irp);
            IoDetachDevice(Agx->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Agx->Lower, Irp);
}

static
NTSTATUS
NTAPI
AgxPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_AGX Agx = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Agx->Lower, Irp);
}

static
NTSTATUS
NTAPI
AgxPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_AGX Agx = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Agx->Lower, Irp);
}

static
NTSTATUS
NTAPI
AgxAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PAPPLE_AGX Agx;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_AGX), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Agx = Fdo->DeviceExtension;
    RtlZeroMemory(Agx, sizeof(*Agx));
    Agx->Self = Fdo;
    Agx->Pdo = Pdo;
    Agx->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Agx->Lower)
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
        DriverObject->MajorFunction[Index] = AgxPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AgxPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AgxPower;
    DriverObject->DriverExtension->AddDevice = AgxAddDevice;
    return STATUS_SUCCESS;
}
