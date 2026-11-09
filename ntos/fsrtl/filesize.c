/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Asking a file system how long one of its files is
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
NTAPI
FsRtlpFileSizeComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->UserIosb != NULL) *Irp->UserIosb = Irp->IoStatus;

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
FsRtlpQueryStandardInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PFILE_STANDARD_INFORMATION Information)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    RtlZeroMemory(&IoStatus, sizeof(IoStatus));

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->UserEvent = NULL;
    Irp->UserIosb = &IoStatus;
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;
    Irp->AssociatedIrp.SystemBuffer = Information;
    Irp->Flags = IRP_SYNCHRONOUS_API;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_QUERY_INFORMATION;
    Stack->FileObject = FileObject;
    Stack->DeviceObject = DeviceObject;
    Stack->Parameters.QueryFile.Length = sizeof(*Information);
    Stack->Parameters.QueryFile.FileInformationClass =
        FileStandardInformation;

    IoSetCompletionRoutine(Irp,
                           FsRtlpFileSizeComplete,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    IoFreeIrp(Irp);

    return IoStatus.Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlGetFileSize(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PLARGE_INTEGER FileSize)
{
    FILE_STANDARD_INFORMATION Information;
    IO_STATUS_BLOCK IoStatus;
    PDEVICE_OBJECT DeviceObject;
    PFAST_IO_DISPATCH Fast;
    NTSTATUS Status;

    PAGED_CODE();

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    RtlZeroMemory(&Information, sizeof(Information));

    Fast = (DeviceObject->DriverObject != NULL) ?
        DeviceObject->DriverObject->FastIoDispatch : NULL;

    if ((Fast != NULL) &&
        (Fast->SizeOfFastIoDispatch >=
         FIELD_OFFSET(FAST_IO_DISPATCH, FastIoQueryStandardInfo) +
         sizeof(PVOID)) &&
        (Fast->FastIoQueryStandardInfo != NULL))
    {
        if (Fast->FastIoQueryStandardInfo(FileObject,
                                          TRUE,
                                          &Information,
                                          &IoStatus,
                                          DeviceObject))
        {
            if (NT_SUCCESS(IoStatus.Status))
            {
                *FileSize = Information.EndOfFile;
            }

            return IoStatus.Status;
        }
    }

    Status = FsRtlpQueryStandardInformation(FileObject,
                                            DeviceObject,
                                            &Information);

    if (NT_SUCCESS(Status)) *FileSize = Information.EndOfFile;

    return Status;
}

/* EOF */
