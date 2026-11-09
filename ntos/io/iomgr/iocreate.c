/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Opening and creating files, pipes and mailslots
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

BOOLEAN
IopCheckBackupRestorePrivilege(
    _In_ PACCESS_STATE AccessState,
    _Inout_ PULONG CreateOptions,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ ULONG Disposition)
{
    ACCESS_MASK Backup = 0;
    ACCESS_MASK Restore = 0;
    BOOLEAN Granted = FALSE;

    PAGED_CODE();

    if (!(*CreateOptions & FILE_OPEN_FOR_BACKUP_INTENT)) return FALSE;

    Backup = READ_CONTROL | ACCESS_SYSTEM_SECURITY | FILE_GENERIC_READ |
             FILE_TRAVERSE;
    Restore = WRITE_DAC | WRITE_OWNER | ACCESS_SYSTEM_SECURITY | DELETE |
              FILE_GENERIC_WRITE | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;

    if (SeSinglePrivilegeCheck(SeBackupPrivilege, PreviousMode))
    {
        AccessState->PreviouslyGrantedAccess |=
            AccessState->RemainingDesiredAccess & Backup;
        AccessState->RemainingDesiredAccess &= ~Backup;
        Granted = TRUE;
    }

    if (Disposition != FILE_OPEN &&
        SeSinglePrivilegeCheck(SeRestorePrivilege, PreviousMode))
    {
        AccessState->PreviouslyGrantedAccess |=
            AccessState->RemainingDesiredAccess & Restore;
        AccessState->RemainingDesiredAccess &= ~Restore;
        Granted = TRUE;
    }

    if (!Granted) *CreateOptions &= ~FILE_OPEN_FOR_BACKUP_INTENT;

    return Granted;
}

static
NTSTATUS
IopCaptureEaBuffer(
    _In_ PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PVOID *CapturedBuffer)
{
    NTSTATUS Status;
    ULONG ErrorOffset = 0;
    PVOID Buffer;

    *CapturedBuffer = NULL;

    if (EaBuffer == NULL || EaLength == 0) return STATUS_SUCCESS;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, EaLength, TAG_EA);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(EaBuffer, EaLength, sizeof(ULONG));
            RtlCopyMemory(Buffer, EaBuffer, EaLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(Buffer, TAG_EA);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Buffer, EaBuffer, EaLength);
    }

    Status = IoCheckEaBufferValidity(Buffer, EaLength, &ErrorOffset);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_EA);
        return Status;
    }

    *CapturedBuffer = Buffer;

    return STATUS_SUCCESS;
}

NTSTATUS
IopCreateFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG Disposition,
    _In_ ULONG CreateOptions,
    _In_opt_ PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ CREATE_FILE_TYPE CreateFileType,
    _In_opt_ PVOID ExtraCreateParameters,
    _In_ ULONG Options,
    _In_ ULONG InternalFlags,
    _In_opt_ PIO_DRIVER_CREATE_CONTEXT DriverContext)
{
    KPROCESSOR_MODE AccessMode;
    DUMMY_FILE_OBJECT LocalFileObject;
    LARGE_INTEGER SafeAllocationSize;
    PVOID CapturedEaBuffer = NULL;
    OPEN_PACKET OpenPacket;
    NTSTATUS Status;
    HANDLE Handle = NULL;

    PAGED_CODE();

    if (Options & IO_NO_PARAMETER_CHECKING)
    {
        AccessMode = KernelMode;
    }
    else
    {
        AccessMode = ExGetPreviousMode();
    }

    if (AccessMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(FileHandle);
            ProbeForWriteIoStatusBlock(IoStatusBlock);

            if (AllocationSize != NULL)
            {
                SafeAllocationSize = ProbeForReadLargeInteger(AllocationSize);
            }
            else
            {
                SafeAllocationSize.QuadPart = 0;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (AllocationSize != NULL)
    {
        SafeAllocationSize = *AllocationSize;
    }
    else
    {
        SafeAllocationSize.QuadPart = 0;
    }

    if (!(Options & IO_NO_PARAMETER_CHECKING))
    {
        if (Disposition > FILE_MAXIMUM_DISPOSITION)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (CreateOptions & ~FILE_VALID_OPTION_FLAGS)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if ((CreateOptions & FILE_SYNCHRONOUS_IO_ALERT) &&
            (CreateOptions & FILE_SYNCHRONOUS_IO_NONALERT))
        {
            return STATUS_INVALID_PARAMETER;
        }

        if ((CreateOptions & (FILE_SYNCHRONOUS_IO_ALERT |
                              FILE_SYNCHRONOUS_IO_NONALERT)) &&
            !(DesiredAccess & SYNCHRONIZE))
        {
            return STATUS_INVALID_PARAMETER;
        }

        if ((CreateOptions & FILE_DIRECTORY_FILE) &&
            (CreateOptions & FILE_NON_DIRECTORY_FILE))
        {
            return STATUS_INVALID_PARAMETER;
        }

        if ((CreateOptions & FILE_DIRECTORY_FILE) &&
            Disposition != FILE_CREATE &&
            Disposition != FILE_OPEN &&
            Disposition != FILE_OPEN_IF)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if ((CreateOptions & FILE_DELETE_ON_CLOSE) && !(DesiredAccess & DELETE))
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (FileAttributes & ~FILE_ATTRIBUTE_VALID_FLAGS)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (ShareAccess & ~FILE_SHARE_VALID_FLAGS)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    Status = IopCaptureEaBuffer(EaBuffer,
                                EaLength,
                                AccessMode,
                                &CapturedEaBuffer);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&OpenPacket, sizeof(OpenPacket));

    OpenPacket.Type = IO_TYPE_OPEN_PACKET;
    OpenPacket.Size = sizeof(OPEN_PACKET);
    OpenPacket.OriginalAttributes = ObjectAttributes;
    OpenPacket.AllocationSize = SafeAllocationSize;
    OpenPacket.CreateOptions = CreateOptions;
    OpenPacket.FileAttributes = (USHORT)FileAttributes;
    OpenPacket.ShareAccess = (USHORT)ShareAccess;
    OpenPacket.EaBuffer = CapturedEaBuffer;
    OpenPacket.EaLength = EaLength;
    OpenPacket.Options = Options;
    OpenPacket.Disposition = Disposition;
    OpenPacket.CreateFileType = CreateFileType;
    OpenPacket.MailslotOrPipeParameters = ExtraCreateParameters;
    OpenPacket.Override = FALSE;
    OpenPacket.QueryOnly = FALSE;
    OpenPacket.DeleteOnly = FALSE;
    OpenPacket.FullAttributes = FALSE;
    OpenPacket.LocalFileObject = &LocalFileObject;
    OpenPacket.InternalFlags = InternalFlags;
    OpenPacket.AccessMode = AccessMode;

    if (DriverContext != NULL)
    {
        OpenPacket.DriverCreateContext = *DriverContext;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                NULL,
                                AccessMode,
                                NULL,
                                DesiredAccess,
                                &OpenPacket,
                                &Handle);

    if (CapturedEaBuffer != NULL)
    {
        ExFreePoolWithTag(CapturedEaBuffer, TAG_EA);
    }

    if (OpenPacket.ParseCheck == 0)
    {
        if (NT_SUCCESS(Status)) Status = STATUS_OBJECT_TYPE_MISMATCH;
    }
    else
    {
        Status = OpenPacket.FinalStatus;
    }

    if (AccessMode != KernelMode)
    {
        _SEH2_TRY
        {
            IoStatusBlock->Status = Status;
            IoStatusBlock->Information = OpenPacket.Information;
            *FileHandle = NT_SUCCESS(Status) ? Handle : NULL;
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
        IoStatusBlock->Information = OpenPacket.Information;
        *FileHandle = NT_SUCCESS(Status) ? Handle : NULL;
    }

    return Status;
}

NTSTATUS
IopOpenLinkOrRenameTarget(
    _Out_ PHANDLE Handle,
    _In_ PIRP Irp,
    _In_ PFILE_RENAME_INFORMATION RenameInfo,
    _In_ PFILE_OBJECT FileObject)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_OBJECT TargetFileObject;
    UNICODE_STRING TargetName;
    ACCESS_MASK DesiredAccess;
    NTSTATUS Status;

    PAGED_CODE();

    *Handle = NULL;

    TargetName.Buffer = RenameInfo->FileName;
    TargetName.Length = (USHORT)RenameInfo->FileNameLength;
    TargetName.MaximumLength = (USHORT)RenameInfo->FileNameLength;

    DesiredAccess = (StackPtr->Parameters.SetFile.FileInformationClass ==
                     FileRenameInformation)
                        ? DELETE
                        : FILE_ADD_FILE;

    InitializeObjectAttributes(&ObjectAttributes,
                               &TargetName,
                               FileObject->Flags & FO_OPENED_CASE_SENSITIVE
                                   ? 0
                                   : OBJ_CASE_INSENSITIVE,
                               RenameInfo->RootDirectory,
                               NULL);

    Status = IoCreateFile(Handle,
                          DesiredAccess,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_OPEN_REPARSE_POINT,
                          NULL,
                          0,
                          CreateFileTypeNone,
                          NULL,
                          IO_NO_PARAMETER_CHECKING | IO_OPEN_TARGET_DIRECTORY);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(*Handle,
                                       0,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&TargetFileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        NtClose(*Handle);
        *Handle = NULL;
        return Status;
    }

    if (IoGetRelatedDeviceObject(TargetFileObject) !=
        IoGetRelatedDeviceObject(FileObject))
    {
        ObDereferenceObject(TargetFileObject);
        NtClose(*Handle);
        *Handle = NULL;
        return STATUS_NOT_SAME_DEVICE;
    }

    StackPtr->Parameters.SetFile.FileObject = TargetFileObject;
    ObDereferenceObject(TargetFileObject);

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoCreateFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG Disposition,
    _In_ ULONG CreateOptions,
    _In_opt_ PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ CREATE_FILE_TYPE CreateFileType,
    _In_opt_ PVOID ExtraCreateParameters,
    _In_ ULONG Options)
{
    return IopCreateFile(FileHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         IoStatusBlock,
                         AllocationSize,
                         FileAttributes,
                         ShareAccess,
                         Disposition,
                         CreateOptions,
                         EaBuffer,
                         EaLength,
                         CreateFileType,
                         ExtraCreateParameters,
                         Options,
                         0,
                         NULL);
}

NTSTATUS
NTAPI
IoCreateFileEx(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG Disposition,
    _In_ ULONG CreateOptions,
    _In_opt_ PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ CREATE_FILE_TYPE CreateFileType,
    _In_opt_ PVOID InternalParameters,
    _In_ ULONG Options,
    _In_opt_ PIO_DRIVER_CREATE_CONTEXT DriverContext)
{
    return IopCreateFile(FileHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         IoStatusBlock,
                         AllocationSize,
                         FileAttributes,
                         ShareAccess,
                         Disposition,
                         CreateOptions,
                         EaBuffer,
                         EaLength,
                         CreateFileType,
                         InternalParameters,
                         Options,
                         0,
                         DriverContext);
}

NTSTATUS
NTAPI
IoCreateFileSpecifyDeviceObjectHint(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG Disposition,
    _In_ ULONG CreateOptions,
    _In_opt_ PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ CREATE_FILE_TYPE CreateFileType,
    _In_opt_ PVOID InternalParameters,
    _In_ ULONG Options,
    _In_opt_ PVOID DeviceObject)
{
    IO_DRIVER_CREATE_CONTEXT DriverContext;

    IoInitializeDriverCreateContext(&DriverContext);
    DriverContext.DeviceObjectHint = DeviceObject;

    return IopCreateFile(FileHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         IoStatusBlock,
                         AllocationSize,
                         FileAttributes,
                         ShareAccess,
                         Disposition,
                         CreateOptions,
                         EaBuffer,
                         EaLength,
                         CreateFileType,
                         InternalParameters,
                         Options,
                         IOP_USE_TOP_LEVEL_DEVICE_HINT,
                         &DriverContext);
}

NTSTATUS
NTAPI
NtCreateFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_opt_ PVOID EaBuffer,
    _In_ ULONG EaLength)
{
    return IopCreateFile(FileHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         IoStatusBlock,
                         AllocationSize,
                         FileAttributes,
                         ShareAccess,
                         CreateDisposition,
                         CreateOptions,
                         EaBuffer,
                         EaLength,
                         CreateFileTypeNone,
                         NULL,
                         0,
                         0,
                         NULL);
}

NTSTATUS
NTAPI
NtOpenFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG ShareAccess,
    _In_ ULONG OpenOptions)
{
    return IopCreateFile(FileHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         IoStatusBlock,
                         NULL,
                         0,
                         ShareAccess,
                         FILE_OPEN,
                         OpenOptions,
                         NULL,
                         0,
                         CreateFileTypeNone,
                         NULL,
                         0,
                         0,
                         NULL);
}

NTSTATUS
NTAPI
NtCreateNamedPipeFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_ ULONG NamedPipeType,
    _In_ ULONG ReadMode,
    _In_ ULONG CompletionMode,
    _In_ ULONG MaximumInstances,
    _In_ ULONG InboundQuota,
    _In_ ULONG OutboundQuota,
    _In_opt_ PLARGE_INTEGER DefaultTimeout)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NAMED_PIPE_CREATE_PARAMETERS Parameters;

    PAGED_CODE();

    RtlZeroMemory(&Parameters, sizeof(Parameters));

    if (DefaultTimeout != NULL)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                Parameters.DefaultTimeout =
                    ProbeForReadLargeInteger(DefaultTimeout);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            Parameters.DefaultTimeout = *DefaultTimeout;
        }

        Parameters.TimeoutSpecified = TRUE;
    }

    if (NamedPipeType > FILE_PIPE_MESSAGE_TYPE) return STATUS_INVALID_PARAMETER;
    if (ReadMode > FILE_PIPE_MESSAGE_MODE) return STATUS_INVALID_PARAMETER;
    if (CompletionMode > FILE_PIPE_COMPLETE_OPERATION)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (MaximumInstances == 0) return STATUS_INVALID_PARAMETER;
    if (ShareAccess & ~(FILE_SHARE_READ | FILE_SHARE_WRITE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (CreateDisposition != FILE_CREATE &&
        CreateDisposition != FILE_OPEN &&
        CreateDisposition != FILE_OPEN_IF)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Parameters.NamedPipeType = NamedPipeType;
    Parameters.ReadMode = ReadMode;
    Parameters.CompletionMode = CompletionMode;
    Parameters.MaximumInstances = MaximumInstances;
    Parameters.InboundQuota = InboundQuota;
    Parameters.OutboundQuota = OutboundQuota;

    return IopCreateFile(FileHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         IoStatusBlock,
                         NULL,
                         FILE_ATTRIBUTE_NORMAL,
                         ShareAccess,
                         CreateDisposition,
                         CreateOptions,
                         NULL,
                         0,
                         CreateFileTypeNamedPipe,
                         &Parameters,
                         0,
                         0,
                         NULL);
}

NTSTATUS
NTAPI
NtCreateMailslotFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG CreateOptions,
    _In_ ULONG MailslotQuota,
    _In_ ULONG MaximumMessageSize,
    _In_ PLARGE_INTEGER ReadTimeout)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    MAILSLOT_CREATE_PARAMETERS Parameters;

    PAGED_CODE();

    RtlZeroMemory(&Parameters, sizeof(Parameters));

    if (ReadTimeout != NULL)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                Parameters.ReadTimeout = ProbeForReadLargeInteger(ReadTimeout);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            Parameters.ReadTimeout = *ReadTimeout;
        }

        Parameters.TimeoutSpecified = TRUE;
    }

    Parameters.MailslotQuota = MailslotQuota;
    Parameters.MaximumMessageSize = MaximumMessageSize;

    return IopCreateFile(FileHandle,
                         DesiredAccess,
                         ObjectAttributes,
                         IoStatusBlock,
                         NULL,
                         FILE_ATTRIBUTE_NORMAL,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         FILE_CREATE,
                         CreateOptions,
                         NULL,
                         0,
                         CreateFileTypeMailslot,
                         &Parameters,
                         0,
                         0,
                         NULL);
}

NTSTATUS
NTAPI
NtDeleteFile(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    Status = IopCreateFile(&Handle,
                           DELETE,
                           ObjectAttributes,
                           &IoStatusBlock,
                           NULL,
                           0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE |
                               FILE_SHARE_DELETE,
                           FILE_OPEN,
                           FILE_DELETE_ON_CLOSE | FILE_OPEN_REPARSE_POINT,
                           NULL,
                           0,
                           CreateFileTypeNone,
                           NULL,
                           0,
                           0,
                           NULL);
    if (!NT_SUCCESS(Status)) return Status;

    NtClose(Handle);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoCheckEaBufferValidity(
    _In_ PFILE_FULL_EA_INFORMATION EaBuffer,
    _In_ ULONG EaLength,
    _Out_ PULONG ErrorOffset)
{
    PFILE_FULL_EA_INFORMATION Current = EaBuffer;
    ULONG Offset = 0;
    ULONG Remaining = EaLength;
    ULONG EntryLength;

    PAGED_CODE();

    *ErrorOffset = 0;

    if (EaLength == 0) return STATUS_SUCCESS;

    while (TRUE)
    {
        if (Remaining < FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName))
        {
            *ErrorOffset = Offset;
            return STATUS_EA_LIST_INCONSISTENT;
        }

        EntryLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
                      Current->EaNameLength + 1 + Current->EaValueLength;

        if (EntryLength > Remaining)
        {
            *ErrorOffset = Offset;
            return STATUS_EA_LIST_INCONSISTENT;
        }

        if (Current->EaName[Current->EaNameLength] != ANSI_NULL)
        {
            *ErrorOffset = Offset;
            return STATUS_EA_LIST_INCONSISTENT;
        }

        if (Current->NextEntryOffset == 0) return STATUS_SUCCESS;

        if (Current->NextEntryOffset < EntryLength ||
            (Current->NextEntryOffset % sizeof(ULONG)) != 0 ||
            Current->NextEntryOffset > Remaining)
        {
            *ErrorOffset = Offset;
            return STATUS_EA_LIST_INCONSISTENT;
        }

        Offset += Current->NextEntryOffset;
        Remaining -= Current->NextEntryOffset;
        Current = (PFILE_FULL_EA_INFORMATION)
            ((PUCHAR)Current + Current->NextEntryOffset);
    }
}

NTSTATUS
NTAPI
IoCheckQuotaBufferValidity(
    _In_ PFILE_QUOTA_INFORMATION QuotaBuffer,
    _In_ ULONG QuotaLength,
    _Out_ PULONG ErrorOffset)
{
    PFILE_QUOTA_INFORMATION Current = QuotaBuffer;
    ULONG Remaining = QuotaLength;
    ULONG EntryLength;
    ULONG Offset = 0;

    PAGED_CODE();

    *ErrorOffset = 0;

    while (TRUE)
    {
        if (Remaining < FIELD_OFFSET(FILE_QUOTA_INFORMATION, Sid))
        {
            *ErrorOffset = Offset;
            return STATUS_DATATYPE_MISALIGNMENT;
        }

        if (Current->SidLength < FIELD_OFFSET(SID, SubAuthority))
        {
            *ErrorOffset = Offset;
            return STATUS_INVALID_SID;
        }

        EntryLength = FIELD_OFFSET(FILE_QUOTA_INFORMATION, Sid) +
                      Current->SidLength;

        if (EntryLength > Remaining)
        {
            *ErrorOffset = Offset;
            return STATUS_DATATYPE_MISALIGNMENT;
        }

        if (!RtlValidSid(&Current->Sid))
        {
            *ErrorOffset = Offset;
            return STATUS_INVALID_SID;
        }

        if (RtlLengthSid(&Current->Sid) != Current->SidLength)
        {
            *ErrorOffset = Offset;
            return STATUS_INVALID_SID;
        }

        if (Current->NextEntryOffset == 0) return STATUS_SUCCESS;

        if (Current->NextEntryOffset < EntryLength ||
            (Current->NextEntryOffset % sizeof(ULONG)) != 0 ||
            Current->NextEntryOffset > Remaining)
        {
            *ErrorOffset = Offset;
            return STATUS_DATATYPE_MISALIGNMENT;
        }

        Offset += Current->NextEntryOffset;
        Remaining -= Current->NextEntryOffset;
        Current = (PFILE_QUOTA_INFORMATION)
            ((PUCHAR)Current + Current->NextEntryOffset);
    }
}
