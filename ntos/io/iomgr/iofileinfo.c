/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Asking a file or a volume what it is and telling it what to be
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

UCHAR IopQueryOperationLength[FileMaximumInformation] =
{
    [FileBasicInformation] = sizeof(FILE_BASIC_INFORMATION),
    [FileStandardInformation] = sizeof(FILE_STANDARD_INFORMATION),
    [FileInternalInformation] = sizeof(FILE_INTERNAL_INFORMATION),
    [FileEaInformation] = sizeof(FILE_EA_INFORMATION),
    [FileAccessInformation] = sizeof(FILE_ACCESS_INFORMATION),
    [FileNameInformation] = sizeof(FILE_NAME_INFORMATION),
    [FilePositionInformation] = sizeof(FILE_POSITION_INFORMATION),
    [FileModeInformation] = sizeof(FILE_MODE_INFORMATION),
    [FileAlignmentInformation] = sizeof(FILE_ALIGNMENT_INFORMATION),
    [FileAllInformation] =
        FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation.FileName),
    [FileAlternateNameInformation] = sizeof(FILE_NAME_INFORMATION),
    [FileStreamInformation] = sizeof(FILE_STREAM_INFORMATION),
    [FilePipeInformation] = sizeof(FILE_PIPE_INFORMATION),
    [FilePipeLocalInformation] = sizeof(FILE_PIPE_LOCAL_INFORMATION),
    [FilePipeRemoteInformation] = sizeof(FILE_PIPE_REMOTE_INFORMATION),
    [FileMailslotQueryInformation] = sizeof(FILE_MAILSLOT_QUERY_INFORMATION),
    [FileCompressionInformation] = sizeof(FILE_COMPRESSION_INFORMATION),
    [FileObjectIdInformation] = sizeof(FILE_OBJECTID_INFORMATION),
    [FileQuotaInformation] = sizeof(FILE_QUOTA_INFORMATION),
    [FileReparsePointInformation] = sizeof(FILE_REPARSE_POINT_INFORMATION),
    [FileNetworkOpenInformation] = sizeof(FILE_NETWORK_OPEN_INFORMATION),
    [FileAttributeTagInformation] = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION),
    [FileIoCompletionNotificationInformation] =
        sizeof(FILE_IO_COMPLETION_NOTIFICATION_INFORMATION),
    [FileIoPriorityHintInformation] =
        sizeof(FILE_IO_PRIORITY_HINT_INFORMATION),
    [FileNormalizedNameInformation] = sizeof(FILE_NAME_INFORMATION),
    [FileNumaNodeInformation] = sizeof(FILE_NUMA_NODE_INFORMATION),
    [FileIdInformation] = sizeof(FILE_ID_INFORMATION),
    [FileStatInformation] = sizeof(FILE_STAT_INFORMATION),
    [FileStatLxInformation] = sizeof(FILE_STAT_LX_INFORMATION),
    [FileCaseSensitiveInformation] = sizeof(FILE_CASE_SENSITIVE_INFORMATION)
};

UCHAR IopSetOperationLength[FileMaximumInformation] =
{
    [FileBasicInformation] = sizeof(FILE_BASIC_INFORMATION),
    [FileRenameInformation] = sizeof(FILE_RENAME_INFORMATION),
    [FileLinkInformation] = sizeof(FILE_LINK_INFORMATION),
    [FileDispositionInformation] = sizeof(FILE_DISPOSITION_INFORMATION),
    [FilePositionInformation] = sizeof(FILE_POSITION_INFORMATION),
    [FileModeInformation] = sizeof(FILE_MODE_INFORMATION),
    [FileAllocationInformation] = sizeof(FILE_ALLOCATION_INFORMATION),
    [FileEndOfFileInformation] = sizeof(FILE_END_OF_FILE_INFORMATION),
    [FilePipeInformation] = sizeof(FILE_PIPE_INFORMATION),
    [FileMailslotSetInformation] = sizeof(FILE_MAILSLOT_SET_INFORMATION),
    [FileObjectIdInformation] = sizeof(FILE_OBJECTID_INFORMATION),
    [FileCompletionInformation] = sizeof(FILE_COMPLETION_INFORMATION),
    [FileMoveClusterInformation] = sizeof(FILE_MOVE_CLUSTER_INFORMATION),
    [FileQuotaInformation] = sizeof(FILE_QUOTA_INFORMATION),
    [FileTrackingInformation] = sizeof(FILE_TRACKING_INFORMATION),
    [FileValidDataLengthInformation] =
        sizeof(FILE_VALID_DATA_LENGTH_INFORMATION),
    [FileShortNameInformation] = sizeof(UNICODE_STRING),
    [FileIoCompletionNotificationInformation] =
        sizeof(FILE_IO_COMPLETION_NOTIFICATION_INFORMATION),
    [FileIoPriorityHintInformation] =
        sizeof(FILE_IO_PRIORITY_HINT_INFORMATION),
    [FileDispositionInformationEx] = sizeof(FILE_DISPOSITION_INFORMATION_EX),
    [FileRenameInformationEx] = sizeof(FILE_RENAME_INFORMATION),
    [FileLinkInformationEx] = sizeof(FILE_LINK_INFORMATION),
    [FileCaseSensitiveInformation] = sizeof(FILE_CASE_SENSITIVE_INFORMATION)
};

ACCESS_MASK IopQueryOperationAccess[FileMaximumInformation] =
{
    [FileBasicInformation] = FILE_READ_ATTRIBUTES,
    [FileFullEaInformation] = FILE_READ_EA,
    [FileAllInformation] = FILE_READ_ATTRIBUTES,
    [FilePipeInformation] = FILE_READ_ATTRIBUTES,
    [FilePipeLocalInformation] = FILE_READ_ATTRIBUTES,
    [FilePipeRemoteInformation] = FILE_READ_ATTRIBUTES,
    [FileNetworkOpenInformation] = FILE_READ_ATTRIBUTES,
    [FileAttributeTagInformation] = FILE_READ_ATTRIBUTES,
    [FileStatInformation] = FILE_READ_ATTRIBUTES,
    [FileStatLxInformation] = FILE_READ_ATTRIBUTES | FILE_READ_EA,
    [FileCaseSensitiveInformation] = FILE_READ_ATTRIBUTES
};

ACCESS_MASK IopSetOperationAccess[FileMaximumInformation] =
{
    [FileBasicInformation] = FILE_WRITE_ATTRIBUTES,
    [FileRenameInformation] = DELETE,
    [FileDispositionInformation] = DELETE,
    [FileFullEaInformation] = FILE_WRITE_EA,
    [FileAllocationInformation] = FILE_WRITE_DATA,
    [FileEndOfFileInformation] = FILE_WRITE_DATA,
    [FilePipeInformation] = FILE_WRITE_ATTRIBUTES,
    [FileMailslotSetInformation] = FILE_WRITE_ATTRIBUTES,
    [FileMoveClusterInformation] = FILE_WRITE_DATA,
    [FileTrackingInformation] = FILE_WRITE_DATA,
    [FileValidDataLengthInformation] = FILE_WRITE_DATA,
    [FileShortNameInformation] = DELETE,
    [FileDispositionInformationEx] = DELETE,
    [FileRenameInformationEx] = DELETE,
    [FileCaseSensitiveInformation] = FILE_WRITE_ATTRIBUTES
};

UCHAR IopQueryFsOperationLength[FileFsMaximumInformation] =
{
    [FileFsVolumeInformation] = sizeof(FILE_FS_VOLUME_INFORMATION),
    [FileFsSizeInformation] = sizeof(FILE_FS_SIZE_INFORMATION),
    [FileFsDeviceInformation] = sizeof(FILE_FS_DEVICE_INFORMATION),
    [FileFsAttributeInformation] = sizeof(FILE_FS_ATTRIBUTE_INFORMATION),
    [FileFsControlInformation] = sizeof(FILE_FS_CONTROL_INFORMATION),
    [FileFsFullSizeInformation] = sizeof(FILE_FS_FULL_SIZE_INFORMATION),
    [FileFsObjectIdInformation] = sizeof(FILE_FS_OBJECTID_INFORMATION),
    [FileFsDriverPathInformation] = sizeof(FILE_FS_DRIVER_PATH_INFORMATION),
    [FileFsVolumeFlagsInformation] = 0xFF,
    [FileFsSectorSizeInformation] = sizeof(FILE_FS_SECTOR_SIZE_INFORMATION)
};

UCHAR IopSetFsOperationLength[FileFsMaximumInformation] =
{
    [FileFsLabelInformation] = sizeof(FILE_FS_LABEL_INFORMATION),
    [FileFsControlInformation] = sizeof(FILE_FS_CONTROL_INFORMATION),
    [FileFsObjectIdInformation] = sizeof(FILE_FS_OBJECTID_INFORMATION),
    [FileFsVolumeFlagsInformation] = 0xFF
};

ULONG IopQueryFsOperationAccess[FileFsMaximumInformation] =
{
    [FileFsControlInformation] = FILE_READ_DATA,
    [FileFsVolumeFlagsInformation] = 0xFFFFFFFF
};

ULONG IopSetFsOperationAccess[FileFsMaximumInformation] =
{
    [FileFsLabelInformation] = FILE_WRITE_DATA,
    [FileFsControlInformation] = FILE_WRITE_DATA,
    [FileFsObjectIdInformation] = FILE_WRITE_DATA,
    [FileFsVolumeFlagsInformation] = 0xFFFFFFFF
};

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
IopAnswerFromFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ACCESS_MASK GrantedAccess,
    _Out_ PVOID Buffer,
    _Out_ PULONG ReturnedLength,
    _Out_ PBOOLEAN Answered)
{
    *Answered = TRUE;

    switch (FileInformationClass)
    {
        case FileAccessInformation:
        {
            PFILE_ACCESS_INFORMATION Info = Buffer;

            Info->AccessFlags = GrantedAccess;
            *ReturnedLength = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileModeInformation:
        {
            PFILE_MODE_INFORMATION Info = Buffer;
            ULONG Mode = 0;

            if (FileObject->Flags & FO_WRITE_THROUGH)
            {
                Mode |= FILE_WRITE_THROUGH;
            }
            if (FileObject->Flags & FO_SEQUENTIAL_ONLY)
            {
                Mode |= FILE_SEQUENTIAL_ONLY;
            }
            if (FileObject->Flags & FO_NO_INTERMEDIATE_BUFFERING)
            {
                Mode |= FILE_NO_INTERMEDIATE_BUFFERING;
            }
            if (FileObject->Flags & FO_SYNCHRONOUS_IO)
            {
                Mode |= (FileObject->Flags & FO_ALERTABLE_IO)
                            ? FILE_SYNCHRONOUS_IO_ALERT
                            : FILE_SYNCHRONOUS_IO_NONALERT;
            }

            Info->Mode = Mode;
            *ReturnedLength = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileAlignmentInformation:
        {
            PFILE_ALIGNMENT_INFORMATION Info = Buffer;

            Info->AlignmentRequirement = DeviceObject->AlignmentRequirement;
            *ReturnedLength = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        default:
            break;
    }

    *Answered = FALSE;
    *ReturnedLength = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
IopQueryDeviceInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG InformationClass,
    _In_ ULONG Length,
    _Out_opt_ PVOID Information,
    _Out_opt_ PULONG ReturnedLength,
    _In_ BOOLEAN File)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT DeviceObject;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->AssociatedIrp.SystemBuffer = Information;
    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_API | IRP_BUFFERED_IO;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->FileObject = FileObject;
    StackPtr->DeviceObject = DeviceObject;

    if (File)
    {
        StackPtr->MajorFunction = IRP_MJ_QUERY_INFORMATION;
        StackPtr->Parameters.QueryFile.Length = Length;
        StackPtr->Parameters.QueryFile.FileInformationClass =
            (FILE_INFORMATION_CLASS)InformationClass;
    }
    else
    {
        StackPtr->MajorFunction = IRP_MJ_QUERY_VOLUME_INFORMATION;
        StackPtr->Parameters.QueryVolume.Length = Length;
        StackPtr->Parameters.QueryVolume.FsInformationClass =
            (FS_INFORMATION_CLASS)InformationClass;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (ReturnedLength != NULL)
    {
        *ReturnedLength = (ULONG)IoStatusBlock.Information;
    }

    return Status;
}

NTSTATUS
NTAPI
IopGetFileInformation(
    IN PFILE_OBJECT FileObject,
    IN ULONG Length,
    IN FILE_INFORMATION_CLASS FileInfoClass,
    OUT PVOID Buffer,
    OUT PULONG ReturnedLength)
{
    return IopQueryDeviceInformation(FileObject,
                                     FileInfoClass,
                                     Length,
                                     Buffer,
                                     ReturnedLength,
                                     TRUE);
}

NTSTATUS
IopGetBasicInformationFile(
    _In_ PFILE_OBJECT FileObject,
    _Out_ PFILE_BASIC_INFORMATION BasicInfo)
{
    ULONG ReturnedLength = 0;

    return IopQueryDeviceInformation(FileObject,
                                     FileBasicInformation,
                                     sizeof(FILE_BASIC_INFORMATION),
                                     BasicInfo,
                                     &ReturnedLength,
                                     TRUE);
}

NTSTATUS
IopQueryAttributesFile(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG FileInformationSize,
    _Out_ PVOID FileInformation,
    _In_ KPROCESSOR_MODE AccessMode)
{
    DUMMY_FILE_OBJECT LocalFileObject;
    OPEN_PACKET OpenPacket;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    RtlZeroMemory(&OpenPacket, sizeof(OpenPacket));

    OpenPacket.Type = IO_TYPE_OPEN_PACKET;
    OpenPacket.Size = sizeof(OPEN_PACKET);
    OpenPacket.CreateOptions = FILE_OPEN_REPARSE_POINT;
    OpenPacket.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE |
                             FILE_SHARE_DELETE;
    OpenPacket.Disposition = FILE_OPEN;
    OpenPacket.QueryOnly = TRUE;
    OpenPacket.FullAttributes =
        (FileInformationClass == FileNetworkOpenInformation);
    OpenPacket.LocalFileObject = &LocalFileObject;
    OpenPacket.FileInformationClass = FileInformationClass;
    OpenPacket.FileInformationLength = FileInformationSize;
    OpenPacket.FileInformation = FileInformation;
    OpenPacket.AccessMode = AccessMode;

    if (FileInformationClass == FileBasicInformation)
    {
        OpenPacket.BasicInformation = FileInformation;
    }
    else if (FileInformationClass == FileNetworkOpenInformation)
    {
        OpenPacket.NetworkInformation = FileInformation;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                NULL,
                                AccessMode,
                                NULL,
                                FILE_READ_ATTRIBUTES,
                                &OpenPacket,
                                &Handle);

    if (OpenPacket.ParseCheck == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_OBJECT_TYPE_MISMATCH : Status;
    }

    return OpenPacket.FinalStatus;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoCheckQuerySetFileInformation(
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG Length,
    _In_ BOOLEAN SetOperation)
{
    ULONG Required;

    PAGED_CODE();

    if (FileInformationClass >= FileMaximumInformation)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Required = SetOperation ? IopSetOperationLength[FileInformationClass]
                            : IopQueryOperationLength[FileInformationClass];
    if (Required == 0) return STATUS_INVALID_PARAMETER;

    if (Length < Required) return STATUS_INFO_LENGTH_MISMATCH;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoCheckQuerySetVolumeInformation(
    _In_ FS_INFORMATION_CLASS FsInformationClass,
    _In_ ULONG Length,
    _In_ BOOLEAN SetOperation)
{
    ULONG Required;

    PAGED_CODE();

    if (FsInformationClass >= FileFsMaximumInformation)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Required = SetOperation ? IopSetFsOperationLength[FsInformationClass]
                            : IopQueryFsOperationLength[FsInformationClass];
    if (Required == 0 || Required == 0xFF) return STATUS_INVALID_PARAMETER;

    if (Length < Required) return STATUS_INFO_LENGTH_MISMATCH;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoCheckFunctionAccess(
    _In_ ACCESS_MASK GrantedAccess,
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    ACCESS_MASK Required = 0;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Argument2);

    switch (MajorFunction)
    {
        case IRP_MJ_READ:
            Required = FILE_READ_DATA;
            break;

        case IRP_MJ_WRITE:
            Required = FILE_WRITE_DATA | FILE_APPEND_DATA;
            break;

        case IRP_MJ_QUERY_INFORMATION:
            if (Argument1 == NULL) return STATUS_SUCCESS;
            Required = IopQueryOperationAccess[
                *(PFILE_INFORMATION_CLASS)Argument1 < FileMaximumInformation
                    ? *(PFILE_INFORMATION_CLASS)Argument1
                    : 0];
            break;

        case IRP_MJ_SET_INFORMATION:
            if (Argument1 == NULL) return STATUS_SUCCESS;
            Required = IopSetOperationAccess[
                *(PFILE_INFORMATION_CLASS)Argument1 < FileMaximumInformation
                    ? *(PFILE_INFORMATION_CLASS)Argument1
                    : 0];
            break;

        case IRP_MJ_QUERY_VOLUME_INFORMATION:
            if (Argument1 == NULL) return STATUS_SUCCESS;
            Required = IopQueryFsOperationAccess[
                *(PFS_INFORMATION_CLASS)Argument1 < FileFsMaximumInformation
                    ? *(PFS_INFORMATION_CLASS)Argument1
                    : 0];
            break;

        case IRP_MJ_SET_VOLUME_INFORMATION:
            if (Argument1 == NULL) return STATUS_SUCCESS;
            Required = IopSetFsOperationAccess[
                *(PFS_INFORMATION_CLASS)Argument1 < FileFsMaximumInformation
                    ? *(PFS_INFORMATION_CLASS)Argument1
                    : 0];
            break;

        case IRP_MJ_QUERY_EA:
            Required = FILE_READ_EA;
            break;

        case IRP_MJ_SET_EA:
            Required = FILE_WRITE_EA;
            break;

        case IRP_MJ_FLUSH_BUFFERS:
            Required = FILE_WRITE_DATA | FILE_APPEND_DATA;
            break;

        case IRP_MJ_DIRECTORY_CONTROL:
            if (MinorFunction == IRP_MN_QUERY_DIRECTORY ||
                MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY)
            {
                Required = FILE_LIST_DIRECTORY;
            }
            break;

        case IRP_MJ_DEVICE_CONTROL:
        case IRP_MJ_FILE_SYSTEM_CONTROL:
            Required = (ACCESS_MASK)((IoControlCode >> 14) & 0x3);
            break;

        default:
            return STATUS_SUCCESS;
    }

    if (Required == 0) return STATUS_SUCCESS;

    if ((GrantedAccess & Required) == 0) return STATUS_ACCESS_DENIED;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoQueryFileInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG Length,
    _Out_ PVOID FileInformation,
    _Out_ PULONG ReturnedLength)
{
    return IopQueryDeviceInformation(FileObject,
                                     FileInformationClass,
                                     Length,
                                     FileInformation,
                                     ReturnedLength,
                                     TRUE);
}

NTSTATUS
NTAPI
IoQueryVolumeInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ FS_INFORMATION_CLASS FsInformationClass,
    _In_ ULONG Length,
    _Out_ PVOID FsInformation,
    _Out_ PULONG ReturnedLength)
{
    return IopQueryDeviceInformation(FileObject,
                                     FsInformationClass,
                                     Length,
                                     FsInformation,
                                     ReturnedLength,
                                     FALSE);
}

NTSTATUS
NTAPI
IoSetInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG Length,
    _In_ PVOID FileInformation)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT DeviceObject;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->AssociatedIrp.SystemBuffer = FileInformation;
    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_API | IRP_BUFFERED_IO;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_SET_INFORMATION;
    StackPtr->FileObject = FileObject;
    StackPtr->DeviceObject = DeviceObject;
    StackPtr->Parameters.SetFile.Length = Length;
    StackPtr->Parameters.SetFile.FileInformationClass = FileInformationClass;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
NTAPI
NtQueryInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PIO_STACK_LOCATION StackPtr;
    PDEVICE_OBJECT DeviceObject;
    OBJECT_HANDLE_INFORMATION HandleInfo;
    PFILE_OBJECT FileObject;
    IO_STATUS_BLOCK KernelIosb;
    ULONG ReturnedLength = 0;
    BOOLEAN Answered = FALSE;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status;
    PVOID Buffer;
    PIRP Irp;

    PAGED_CODE();

    if (FileInformationClass >= FileMaximumInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (IopQueryOperationLength[FileInformationClass] == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (Length < IopQueryOperationLength[FileInformationClass])
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(FileInformation, Length, sizeof(ULONG));
            ProbeForWriteIoStatusBlock(IoStatusBlock);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(
        FileHandle,
        IopQueryOperationAccess[FileInformationClass],
        IoFileObjectType,
        PreviousMode,
        (PVOID *)&FileObject,
        &HandleInfo);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    if (FileInformationClass == FilePositionInformation &&
        (FileObject->Flags & FO_SYNCHRONOUS_IO))
    {
        Status = IopLockFileObject(FileObject, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(FileObject);
            return Status;
        }

        _SEH2_TRY
        {
            ((PFILE_POSITION_INFORMATION)FileInformation)->CurrentByteOffset =
                FileObject->CurrentByteOffset;

            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(FILE_POSITION_INFORMATION);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);

        return Status;
    }

    Status = IopAnswerFromFileObject(FileObject,
                                     DeviceObject,
                                     FileInformationClass,
                                     HandleInfo.GrantedAccess,
                                     FileInformation,
                                     &ReturnedLength,
                                     &Answered);
    if (Answered)
    {
        _SEH2_TRY
        {
            IoStatusBlock->Status = Status;
            IoStatusBlock->Information = ReturnedLength;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ObDereferenceObject(FileObject);
        return Status;
    }

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        Status = IopLockFileObject(FileObject, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(FileObject);
            return Status;
        }

        Locked = TRUE;
    }

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buffer = ExAllocatePoolWithQuotaTag(NonPagedPool |
                                            POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                        Length,
                                        TAG_IOBUF);
    if (Buffer == NULL)
    {
        IoFreeIrp(Irp);
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->AssociatedIrp.SystemBuffer = Buffer;
    Irp->UserBuffer = FileInformation;
    Irp->UserIosb = IoStatusBlock;
    Irp->RequestorMode = PreviousMode;
    Irp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER |
                 IRP_INPUT_OPERATION | IRP_DEFER_IO_COMPLETION;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_QUERY_INFORMATION;
    StackPtr->FileObject = FileObject;
    StackPtr->Parameters.QueryFile.Length = Length;
    StackPtr->Parameters.QueryFile.FileInformationClass = FileInformationClass;

    UNREFERENCED_PARAMETER(KernelIosb);

    IopQueueIrpToThread(Irp);

    IopUpdateOperationCount(IopOtherTransfer);

    Status = IoCallDriver(DeviceObject, Irp);

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        if (Status == STATUS_PENDING)
        {
            Status = KeWaitForSingleObject(&FileObject->Event,
                                           Executive,
                                           PreviousMode,
                                           BooleanFlagOn(FileObject->Flags,
                                                         FO_ALERTABLE_IO),
                                           NULL);
            if (Status == STATUS_ALERTED || Status == STATUS_USER_APC)
            {
                IopAbortInterruptedIrp(&FileObject->Event, Irp);
            }

            Status = FileObject->FinalStatus;
        }

        if (Locked) IopUnlockFileObject(FileObject);
    }

    return Status;
}

NTSTATUS
NTAPI
NtSetInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_reads_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PIO_STACK_LOCATION StackPtr;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    HANDLE TargetHandle = NULL;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status;
    PVOID Buffer;
    PIRP Irp;

    PAGED_CODE();

    if (FileInformationClass >= FileMaximumInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (IopSetOperationLength[FileInformationClass] == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (Length < IopSetOperationLength[FileInformationClass])
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(FileInformation, Length, sizeof(ULONG));
            ProbeForWriteIoStatusBlock(IoStatusBlock);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(
        FileHandle,
        IopSetOperationAccess[FileInformationClass],
        IoFileObjectType,
        PreviousMode,
        (PVOID *)&FileObject,
        NULL);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    Buffer = ExAllocatePoolWithQuotaTag(NonPagedPool |
                                            POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                        Length,
                                        TAG_IOBUF);
    if (Buffer == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        RtlCopyMemory(Buffer, FileInformation, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_IOBUF);
        ObDereferenceObject(FileObject);
        return Status;
    }

    if (FileInformationClass == FileCompletionInformation)
    {
        PFILE_COMPLETION_INFORMATION Completion = Buffer;
        PIO_COMPLETION_CONTEXT Context;
        PVOID Port;

        Status = ObReferenceObjectByHandle(Completion->Port,
                                           IO_COMPLETION_MODIFY_STATE,
                                           IoCompletionType,
                                           PreviousMode,
                                           &Port,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Buffer, TAG_IOBUF);
            ObDereferenceObject(FileObject);
            return Status;
        }

        Context = ExAllocatePoolZero(NonPagedPool,
                                     sizeof(IO_COMPLETION_CONTEXT),
                                     TAG_IO);
        if (Context == NULL)
        {
            ObDereferenceObject(Port);
            ExFreePoolWithTag(Buffer, TAG_IOBUF);
            ObDereferenceObject(FileObject);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Context->Port = Port;
        Context->Key = Completion->Key;

        if (InterlockedCompareExchangePointer(
                (PVOID *)&FileObject->CompletionContext,
                Context,
                NULL) != NULL)
        {
            ExFreePoolWithTag(Context, TAG_IO);
            ObDereferenceObject(Port);
            ExFreePoolWithTag(Buffer, TAG_IOBUF);
            ObDereferenceObject(FileObject);
            return STATUS_INVALID_PARAMETER;
        }

        ExFreePoolWithTag(Buffer, TAG_IOBUF);

        _SEH2_TRY
        {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ObDereferenceObject(FileObject);
        return Status;
    }

    if (FileInformationClass == FileIoPriorityHintInformation)
    {
        PFILE_IO_PRIORITY_HINT_INFORMATION Hint = Buffer;
        PFILE_OBJECT_EXTENSION Extension;

        if (Hint->PriorityHint >= MaxIoPriorityTypes)
        {
            ExFreePoolWithTag(Buffer, TAG_IOBUF);
            ObDereferenceObject(FileObject);
            return STATUS_INVALID_PARAMETER;
        }

        Status = IopAllocateFileObjectExtension(FileObject);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Buffer, TAG_IOBUF);
            ObDereferenceObject(FileObject);
            return Status;
        }

        Extension = FileObject->FileObjectExtension;
        Extension->PriorityHint = Hint->PriorityHint;
        Extension->FoExtFlags |= IOP_FILE_OBJECT_EXT_PRIORITY_HINT;

        ExFreePoolWithTag(Buffer, TAG_IOBUF);

        _SEH2_TRY
        {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ObDereferenceObject(FileObject);
        return Status;
    }

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        Status = IopLockFileObject(FileObject, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Buffer, TAG_IOBUF);
            ObDereferenceObject(FileObject);
            return Status;
        }

        Locked = TRUE;
    }

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        ExFreePoolWithTag(Buffer, TAG_IOBUF);
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->AssociatedIrp.SystemBuffer = Buffer;
    Irp->UserIosb = IoStatusBlock;
    Irp->RequestorMode = PreviousMode;
    Irp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER |
                 IRP_DEFER_IO_COMPLETION;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_SET_INFORMATION;
    StackPtr->FileObject = FileObject;
    StackPtr->Parameters.SetFile.Length = Length;
    StackPtr->Parameters.SetFile.FileInformationClass = FileInformationClass;

    if (FileInformationClass == FileRenameInformation ||
        FileInformationClass == FileLinkInformation ||
        FileInformationClass == FileMoveClusterInformation ||
        FileInformationClass == FileRenameInformationEx ||
        FileInformationClass == FileLinkInformationEx)
    {
        Irp->Tail.Overlay.CurrentStackLocation = StackPtr;
        Irp->CurrentLocation--;

        Status = IopOpenLinkOrRenameTarget(&TargetHandle,
                                           Irp,
                                           Buffer,
                                           FileObject);

        Irp->CurrentLocation++;
        Irp->Tail.Overlay.CurrentStackLocation = StackPtr + 1;

        if (!NT_SUCCESS(Status))
        {
            IoFreeIrp(Irp);
            ExFreePoolWithTag(Buffer, TAG_IOBUF);
            if (Locked) IopUnlockFileObject(FileObject);
            ObDereferenceObject(FileObject);
            return Status;
        }

        StackPtr->Parameters.SetFile.DeleteHandle = TargetHandle;
    }

    IopQueueIrpToThread(Irp);

    IopUpdateOperationCount(IopOtherTransfer);

    Status = IoCallDriver(DeviceObject, Irp);

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        if (Status == STATUS_PENDING)
        {
            Status = KeWaitForSingleObject(&FileObject->Event,
                                           Executive,
                                           PreviousMode,
                                           BooleanFlagOn(FileObject->Flags,
                                                         FO_ALERTABLE_IO),
                                           NULL);
            if (Status == STATUS_ALERTED || Status == STATUS_USER_APC)
            {
                IopAbortInterruptedIrp(&FileObject->Event, Irp);
            }

            Status = FileObject->FinalStatus;
        }

        if (Locked) IopUnlockFileObject(FileObject);
    }

    if (TargetHandle != NULL) NtClose(TargetHandle);

    return Status;
}

NTSTATUS
NTAPI
NtQueryVolumeInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FsInformation,
    _In_ ULONG Length,
    _In_ FS_INFORMATION_CLASS FsInformationClass)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PIO_STACK_LOCATION StackPtr;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status;
    PVOID Buffer;
    PIRP Irp;

    PAGED_CODE();

    if (FsInformationClass >= FileFsMaximumInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (IopQueryFsOperationLength[FsInformationClass] == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (Length < IopQueryFsOperationLength[FsInformationClass] &&
        IopQueryFsOperationLength[FsInformationClass] != 0xFF)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(FsInformation, Length, sizeof(ULONG));
            ProbeForWriteIoStatusBlock(IoStatusBlock);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(
        FileHandle,
        IopQueryFsOperationAccess[FsInformationClass],
        IoFileObjectType,
        PreviousMode,
        (PVOID *)&FileObject,
        NULL);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        Status = IopLockFileObject(FileObject, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(FileObject);
            return Status;
        }

        Locked = TRUE;
    }

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buffer = ExAllocatePoolWithQuotaTag(NonPagedPool |
                                            POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                        Length,
                                        TAG_IOBUF);
    if (Buffer == NULL)
    {
        IoFreeIrp(Irp);
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->AssociatedIrp.SystemBuffer = Buffer;
    Irp->UserBuffer = FsInformation;
    Irp->UserIosb = IoStatusBlock;
    Irp->RequestorMode = PreviousMode;
    Irp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER |
                 IRP_INPUT_OPERATION | IRP_DEFER_IO_COMPLETION;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_QUERY_VOLUME_INFORMATION;
    StackPtr->FileObject = FileObject;
    StackPtr->Parameters.QueryVolume.Length = Length;
    StackPtr->Parameters.QueryVolume.FsInformationClass = FsInformationClass;

    IopQueueIrpToThread(Irp);

    IopUpdateOperationCount(IopOtherTransfer);

    Status = IoCallDriver(DeviceObject, Irp);

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        if (Status == STATUS_PENDING)
        {
            Status = KeWaitForSingleObject(&FileObject->Event,
                                           Executive,
                                           PreviousMode,
                                           BooleanFlagOn(FileObject->Flags,
                                                         FO_ALERTABLE_IO),
                                           NULL);
            if (Status == STATUS_ALERTED || Status == STATUS_USER_APC)
            {
                IopAbortInterruptedIrp(&FileObject->Event, Irp);
            }

            Status = FileObject->FinalStatus;
        }

        if (Locked) IopUnlockFileObject(FileObject);
    }

    return Status;
}

NTSTATUS
NTAPI
NtSetVolumeInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_reads_bytes_(Length) PVOID FsInformation,
    _In_ ULONG Length,
    _In_ FS_INFORMATION_CLASS FsInformationClass)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PIO_STACK_LOCATION StackPtr;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status;
    PVOID Buffer;
    PIRP Irp;

    PAGED_CODE();

    if (FsInformationClass >= FileFsMaximumInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (IopSetFsOperationLength[FsInformationClass] == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (Length < IopSetFsOperationLength[FsInformationClass] &&
        IopSetFsOperationLength[FsInformationClass] != 0xFF)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(FsInformation, Length, sizeof(ULONG));
            ProbeForWriteIoStatusBlock(IoStatusBlock);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(
        FileHandle,
        IopSetFsOperationAccess[FsInformationClass],
        IoFileObjectType,
        PreviousMode,
        (PVOID *)&FileObject,
        NULL);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        Status = IopLockFileObject(FileObject, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(FileObject);
            return Status;
        }

        Locked = TRUE;
    }

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buffer = ExAllocatePoolWithQuotaTag(NonPagedPool |
                                            POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                        Length,
                                        TAG_IOBUF);
    if (Buffer == NULL)
    {
        IoFreeIrp(Irp);
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        RtlCopyMemory(Buffer, FsInformation, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_IOBUF);
        IoFreeIrp(Irp);
        if (Locked) IopUnlockFileObject(FileObject);
        ObDereferenceObject(FileObject);
        return Status;
    }

    Irp->AssociatedIrp.SystemBuffer = Buffer;
    Irp->UserIosb = IoStatusBlock;
    Irp->RequestorMode = PreviousMode;
    Irp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER |
                 IRP_DEFER_IO_COMPLETION;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_SET_VOLUME_INFORMATION;
    StackPtr->FileObject = FileObject;
    StackPtr->Parameters.SetVolume.Length = Length;
    StackPtr->Parameters.SetVolume.FsInformationClass = FsInformationClass;

    IopQueueIrpToThread(Irp);

    IopUpdateOperationCount(IopOtherTransfer);

    Status = IoCallDriver(DeviceObject, Irp);

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        if (Status == STATUS_PENDING)
        {
            Status = KeWaitForSingleObject(&FileObject->Event,
                                           Executive,
                                           PreviousMode,
                                           BooleanFlagOn(FileObject->Flags,
                                                         FO_ALERTABLE_IO),
                                           NULL);
            if (Status == STATUS_ALERTED || Status == STATUS_USER_APC)
            {
                IopAbortInterruptedIrp(&FileObject->Event, Irp);
            }

            Status = FileObject->FinalStatus;
        }

        if (Locked) IopUnlockFileObject(FileObject);
    }

    return Status;
}

static
NTSTATUS
IopQueryAttributesByName(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG Length,
    _Out_ PVOID FileInformation)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID LocalBuffer;
    NTSTATUS Status;

    PAGED_CODE();

    LocalBuffer = ExAllocatePoolZero(NonPagedPool, Length, TAG_IOBUF);
    if (LocalBuffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (PreviousMode != KernelMode)
    {
        Status = STATUS_SUCCESS;

        _SEH2_TRY
        {
            ProbeForWrite(FileInformation, Length, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(LocalBuffer, TAG_IOBUF);
            return Status;
        }
    }

    Status = IopQueryAttributesFile(ObjectAttributes,
                                    FileInformationClass,
                                    Length,
                                    LocalBuffer,
                                    PreviousMode);
    if (NT_SUCCESS(Status))
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                RtlCopyMemory(FileInformation, LocalBuffer, Length);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            RtlCopyMemory(FileInformation, LocalBuffer, Length);
        }
    }

    ExFreePoolWithTag(LocalBuffer, TAG_IOBUF);

    return Status;
}

NTSTATUS
NTAPI
NtQueryAttributesFile(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PFILE_BASIC_INFORMATION FileInformation)
{
    return IopQueryAttributesByName(ObjectAttributes,
                                    FileBasicInformation,
                                    sizeof(FILE_BASIC_INFORMATION),
                                    FileInformation);
}

NTSTATUS
NTAPI
NtQueryFullAttributesFile(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION FileInformation)
{
    return IopQueryAttributesByName(ObjectAttributes,
                                    FileNetworkOpenInformation,
                                    sizeof(FILE_NETWORK_OPEN_INFORMATION),
                                    FileInformation);
}

NTSTATUS
NTAPI
NtQueryInformationByName(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;

    PAGED_CODE();

    if (FileInformationClass >= FileMaximumInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (IopQueryOperationLength[FileInformationClass] == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (Length < IopQueryOperationLength[FileInformationClass])
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteIoStatusBlock(IoStatusBlock);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = IopQueryAttributesByName(ObjectAttributes,
                                      FileInformationClass,
                                      Length,
                                      FileInformation);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            IoStatusBlock->Status = Status;
            IoStatusBlock->Information = NT_SUCCESS(Status) ? Length : 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        IoStatusBlock->Status = Status;
        IoStatusBlock->Information = NT_SUCCESS(Status) ? Length : 0;
    }

    return Status;
}
