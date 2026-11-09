/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Building a request on a driver's behalf
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
IopDescribeTransferBuffer(
    _In_ PIRP Irp,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG MajorFunction,
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length)
{
    LOCK_OPERATION Operation;
    PMDL Mdl;

    if (Buffer == NULL || Length == 0) return TRUE;

    if (!(DeviceObject->Flags & DO_BUFFERED_IO) &&
        !(DeviceObject->Flags & DO_DIRECT_IO))
    {
        Irp->UserBuffer = Buffer;
        return TRUE;
    }

    if (DeviceObject->Flags & DO_BUFFERED_IO)
    {
        Irp->AssociatedIrp.SystemBuffer =
            ExAllocatePoolWithTag(NonPagedPool, Length, TAG_IOBUF);
        if (Irp->AssociatedIrp.SystemBuffer == NULL) return FALSE;

        Irp->Flags |= IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;

        if (MajorFunction == IRP_MJ_WRITE)
        {
            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Buffer, Length);
            return TRUE;
        }

        Irp->Flags |= IRP_INPUT_OPERATION;
        Irp->UserBuffer = Buffer;

        return TRUE;
    }

    Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, Irp);
    if (Mdl == NULL) return FALSE;

    Operation = (MajorFunction == IRP_MJ_READ) ? IoWriteAccess : IoReadAccess;

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, KernelMode, Operation);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        IoFreeMdl(Mdl);
        Irp->MdlAddress = NULL;
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

PIRP
NTAPI
IoBuildSynchronousFsdRequest(
    _In_ ULONG MajorFunction,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_opt_ PVOID Buffer,
    _In_opt_ ULONG Length,
    _In_opt_ PLARGE_INTEGER StartingOffset,
    _In_ PKEVENT Event,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    PIO_STACK_LOCATION StackPtr;
    PIRP Irp;

    Irp = IoBuildAsynchronousFsdRequest(MajorFunction,
                                        DeviceObject,
                                        Buffer,
                                        Length,
                                        StartingOffset,
                                        IoStatusBlock);
    if (Irp == NULL) return NULL;

    Irp->UserEvent = Event;
    Irp->Flags |= IRP_SYNCHRONOUS_API;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    StackPtr = IoGetNextIrpStackLocation(Irp);
    UNREFERENCED_PARAMETER(StackPtr);

    return Irp;
}

PIRP
NTAPI
IoBuildAsynchronousFsdRequest(
    _In_ ULONG MajorFunction,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_opt_ PVOID Buffer,
    _In_opt_ ULONG Length,
    _In_opt_ PLARGE_INTEGER StartingOffset,
    _Out_opt_ PIO_STATUS_BLOCK IoStatusBlock)
{
    PIO_STACK_LOCATION StackPtr;
    PIRP Irp;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return NULL;

    Irp->UserIosb = IoStatusBlock;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->RequestorMode = KernelMode;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = (UCHAR)MajorFunction;

    if (MajorFunction == IRP_MJ_READ || MajorFunction == IRP_MJ_WRITE)
    {
        StackPtr->Parameters.Read.Length = Length;

        if (StartingOffset != NULL)
        {
            StackPtr->Parameters.Read.ByteOffset = *StartingOffset;
        }
        else
        {
            StackPtr->Parameters.Read.ByteOffset.QuadPart = 0;
        }

        if (!IopDescribeTransferBuffer(Irp,
                                       DeviceObject,
                                       MajorFunction,
                                       Buffer,
                                       Length))
        {
            IoFreeIrp(Irp);
            return NULL;
        }

        return Irp;
    }

    if (MajorFunction == IRP_MJ_FLUSH_BUFFERS ||
        MajorFunction == IRP_MJ_SHUTDOWN)
    {
        return Irp;
    }

    IoFreeIrp(Irp);
    return NULL;
}

PIRP
NTAPI
IoBuildDeviceIoControlRequest(
    _In_ ULONG IoControlCode,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ BOOLEAN InternalDeviceIoControl,
    _In_opt_ PKEVENT Event,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    PIO_STACK_LOCATION StackPtr;
    ULONG BufferLength;
    ULONG Method;
    PIRP Irp;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return NULL;

    Irp->UserIosb = IoStatusBlock;
    Irp->UserEvent = Event;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_API;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = InternalDeviceIoControl
                                  ? IRP_MJ_INTERNAL_DEVICE_CONTROL
                                  : IRP_MJ_DEVICE_CONTROL;
    StackPtr->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    StackPtr->Parameters.DeviceIoControl.InputBufferLength = InputBufferLength;
    StackPtr->Parameters.DeviceIoControl.OutputBufferLength = OutputBufferLength;

    Method = IO_METHOD_FROM_CTL_CODE(IoControlCode);

    if (Method == METHOD_BUFFERED)
    {
        BufferLength = max(InputBufferLength, OutputBufferLength);

        if (BufferLength != 0)
        {
            Irp->AssociatedIrp.SystemBuffer =
                ExAllocatePoolWithTag(NonPagedPool, BufferLength, TAG_IOBUF);
            if (Irp->AssociatedIrp.SystemBuffer == NULL)
            {
                IoFreeIrp(Irp);
                return NULL;
            }

            Irp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER |
                         IRP_SYNCHRONOUS_API;

            if (InputBuffer != NULL && InputBufferLength != 0)
            {
                RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer,
                              InputBuffer,
                              InputBufferLength);
            }

            if (OutputBuffer != NULL && OutputBufferLength != 0)
            {
                Irp->Flags |= IRP_INPUT_OPERATION;
            }
        }

        Irp->UserBuffer = OutputBuffer;

        return Irp;
    }

    if (Method == METHOD_IN_DIRECT || Method == METHOD_OUT_DIRECT)
    {
        if (InputBuffer != NULL && InputBufferLength != 0)
        {
            Irp->AssociatedIrp.SystemBuffer =
                ExAllocatePoolWithTag(NonPagedPool,
                                      InputBufferLength,
                                      TAG_IOBUF);
            if (Irp->AssociatedIrp.SystemBuffer == NULL)
            {
                IoFreeIrp(Irp);
                return NULL;
            }

            Irp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER |
                         IRP_SYNCHRONOUS_API;

            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer,
                          InputBuffer,
                          InputBufferLength);
        }

        if (OutputBuffer != NULL && OutputBufferLength != 0)
        {
            PMDL Mdl;

            Mdl = IoAllocateMdl(OutputBuffer,
                                OutputBufferLength,
                                FALSE,
                                FALSE,
                                Irp);
            if (Mdl == NULL)
            {
                if (Irp->AssociatedIrp.SystemBuffer != NULL)
                {
                    ExFreePoolWithTag(Irp->AssociatedIrp.SystemBuffer,
                                      TAG_IOBUF);
                }

                IoFreeIrp(Irp);
                return NULL;
            }

            _SEH2_TRY
            {
                MmProbeAndLockPages(Mdl,
                                    KernelMode,
                                    (Method == METHOD_IN_DIRECT) ? IoReadAccess
                                                                 : IoWriteAccess);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                IoFreeMdl(Mdl);
                Irp->MdlAddress = NULL;

                if (Irp->AssociatedIrp.SystemBuffer != NULL)
                {
                    ExFreePoolWithTag(Irp->AssociatedIrp.SystemBuffer,
                                      TAG_IOBUF);
                }

                IoFreeIrp(Irp);
                _SEH2_YIELD(return NULL);
            }
            _SEH2_END;
        }

        return Irp;
    }

    StackPtr->Parameters.DeviceIoControl.Type3InputBuffer = InputBuffer;
    Irp->UserBuffer = OutputBuffer;

    return Irp;
}

NTSTATUS
NTAPI
IoPageRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL Mdl,
    _In_ PLARGE_INTEGER Offset,
    _In_ PKEVENT Event,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    PIO_STACK_LOCATION StackPtr;
    PDEVICE_OBJECT DeviceObject;
    BOOLEAN Reserved = FALSE;
    PIRP Irp;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        Irp = IopAllocateReserveIrp(DeviceObject->StackSize);
        if (Irp == NULL)
        {
            if (MmIsFileObjectAPagingFile(FileObject))
            {
                InterlockedIncrement((PLONG)&IoPageReadIrpAllocationFailure);
            }
            else
            {
                InterlockedIncrement(
                    (PLONG)&IoPageReadNonPagefileIrpAllocationFailure);
            }

            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Reserved = TRUE;
    }

    UNREFERENCED_PARAMETER(Reserved);

    Irp->Flags = IRP_PAGING_IO | IRP_NOCACHE | IRP_SYNCHRONOUS_PAGING_IO |
                 IRP_INPUT_OPERATION;
    Irp->RequestorMode = KernelMode;
    Irp->UserIosb = IoStatusBlock;
    Irp->UserEvent = Event;
    Irp->MdlAddress = Mdl;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_READ;
    StackPtr->FileObject = FileObject;
    StackPtr->Parameters.Read.Length = MmGetMdlByteCount(Mdl);
    StackPtr->Parameters.Read.ByteOffset = *Offset;

    return IoCallDriver(DeviceObject, Irp);
}

NTSTATUS
NTAPI
IoSynchronousPageWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL Mdl,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PKEVENT Event,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    PIO_STACK_LOCATION StackPtr;
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->Flags = IRP_PAGING_IO | IRP_NOCACHE | IRP_SYNCHRONOUS_PAGING_IO;
    Irp->RequestorMode = KernelMode;
    Irp->UserIosb = IoStatusBlock;
    Irp->UserEvent = Event;
    Irp->MdlAddress = Mdl;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_WRITE;
    StackPtr->FileObject = FileObject;
    StackPtr->Parameters.Write.Length = MmGetMdlByteCount(Mdl);
    StackPtr->Parameters.Write.ByteOffset = *FileOffset;

    return IoCallDriver(DeviceObject, Irp);
}
