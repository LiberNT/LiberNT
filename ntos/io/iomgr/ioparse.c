/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Turning a name the object manager hands over into an open file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
IopCheckDeviceAccess(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN DeviceOnlyOpen)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    ACCESS_MASK GrantedAccess = 0;
    SECURITY_SUBJECT_CONTEXT Subject;
    NTSTATUS AccessStatus;
    PPRIVILEGE_SET Privileges = NULL;
    BOOLEAN Allowed;

    PAGED_CODE();

    if (AccessMode == KernelMode) return STATUS_SUCCESS;

    if (!DeviceOnlyOpen &&
        !(DeviceObject->Characteristics & FILE_DEVICE_SECURE_OPEN))
    {
        return STATUS_SUCCESS;
    }

    ExAcquireResourceSharedLite(&IopSecurityResource, TRUE);

    SecurityDescriptor = DeviceObject->SecurityDescriptor;
    if (SecurityDescriptor == NULL)
    {
        ExReleaseResourceLite(&IopSecurityResource);
        return STATUS_SUCCESS;
    }

    SeCaptureSubjectContext(&Subject);
    SeLockSubjectContext(&Subject);

    Allowed = SeAccessCheck(SecurityDescriptor,
                            &Subject,
                            TRUE,
                            AccessState->RemainingDesiredAccess,
                            AccessState->PreviouslyGrantedAccess,
                            &Privileges,
                            &IopFileMapping,
                            AccessMode,
                            &GrantedAccess,
                            &AccessStatus);

    if (Privileges != NULL)
    {
        SeAppendPrivileges(AccessState, Privileges);
        SeFreePrivileges(Privileges);
    }

    SeUnlockSubjectContext(&Subject);
    SeReleaseSubjectContext(&Subject);

    ExReleaseResourceLite(&IopSecurityResource);

    if (!Allowed) return AccessStatus;

    AccessState->PreviouslyGrantedAccess |= GrantedAccess;
    AccessState->RemainingDesiredAccess &= ~(GrantedAccess | MAXIMUM_ALLOWED);

    return STATUS_SUCCESS;
}

static
PFILE_OBJECT
IopAllocateFileObject(
    _In_ POPEN_PACKET OpenPacket,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PUNICODE_STRING RemainingName,
    _In_ ULONG Attributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PNTSTATUS Status)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PFILE_OBJECT FileObject;
    PWCHAR NameBuffer;

    PAGED_CODE();

    if (OpenPacket->QueryOnly || OpenPacket->DeleteOnly)
    {
        FileObject = (PFILE_OBJECT)&OpenPacket->LocalFileObject->FileObjectBody;
        RtlZeroMemory(FileObject, sizeof(FILE_OBJECT));
        *Status = STATUS_SUCCESS;
    }
    else
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   NULL,
                                   Attributes,
                                   NULL,
                                   NULL);

        *Status = ObCreateObject(KernelMode,
                                 IoFileObjectType,
                                 &ObjectAttributes,
                                 AccessMode,
                                 NULL,
                                 sizeof(FILE_OBJECT),
                                 0,
                                 0,
                                 (PVOID *)&FileObject);
        if (!NT_SUCCESS(*Status)) return NULL;

        RtlZeroMemory(FileObject, sizeof(FILE_OBJECT));
    }

    FileObject->Type = IO_TYPE_FILE;
    FileObject->Size = sizeof(FILE_OBJECT);
    FileObject->DeviceObject = DeviceObject;
    FileObject->RelatedFileObject = OpenPacket->RelatedFileObject;

    KeInitializeEvent(&FileObject->Lock, SynchronizationEvent, FALSE);
    KeInitializeEvent(&FileObject->Event, NotificationEvent, FALSE);
    KeInitializeSpinLock(&FileObject->IrpListLock);
    InitializeListHead(&FileObject->IrpList);

    if (OpenPacket->CreateOptions &
        (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT))
    {
        FileObject->Flags |= FO_SYNCHRONOUS_IO;

        if (OpenPacket->CreateOptions & FILE_SYNCHRONOUS_IO_ALERT)
        {
            FileObject->Flags |= FO_ALERTABLE_IO;
        }
    }

    if (OpenPacket->CreateOptions & FILE_NO_INTERMEDIATE_BUFFERING)
    {
        FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;
    }

    if (OpenPacket->CreateOptions & FILE_WRITE_THROUGH)
    {
        FileObject->Flags |= FO_WRITE_THROUGH;
    }

    if (OpenPacket->CreateOptions & FILE_SEQUENTIAL_ONLY)
    {
        FileObject->Flags |= FO_SEQUENTIAL_ONLY;
    }

    if (OpenPacket->CreateOptions & FILE_RANDOM_ACCESS)
    {
        FileObject->Flags |= FO_RANDOM_ACCESS;
    }

    if (!(Attributes & OBJ_CASE_INSENSITIVE))
    {
        FileObject->Flags |= FO_OPENED_CASE_SENSITIVE;
    }

    if (RemainingName->Length != 0)
    {
        NameBuffer = ExAllocatePoolUninitialized(
            NonPagedPool,
            RemainingName->Length + sizeof(WCHAR),
            TAG_IO_NAME);
        if (NameBuffer == NULL)
        {
            if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly)
            {
                ObDereferenceObject(FileObject);
            }

            *Status = STATUS_INSUFFICIENT_RESOURCES;
            return NULL;
        }

        FileObject->FileName.Buffer = NameBuffer;
        FileObject->FileName.Length = RemainingName->Length;
        FileObject->FileName.MaximumLength =
            RemainingName->Length + sizeof(WCHAR);

        RtlCopyMemory(NameBuffer,
                      RemainingName->Buffer,
                      RemainingName->Length);
        NameBuffer[RemainingName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    return FileObject;
}

static
VOID
IopSetFileObjectAccess(
    _In_ PFILE_OBJECT FileObject,
    _In_ ACCESS_MASK GrantedAccess,
    _In_ USHORT ShareAccess)
{
    FileObject->ReadAccess =
        (GrantedAccess & (FILE_EXECUTE | FILE_READ_DATA)) != 0;
    FileObject->WriteAccess =
        (GrantedAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
    FileObject->DeleteAccess = (GrantedAccess & DELETE) != 0;

    FileObject->SharedRead = (ShareAccess & FILE_SHARE_READ) != 0;
    FileObject->SharedWrite = (ShareAccess & FILE_SHARE_WRITE) != 0;
    FileObject->SharedDelete = (ShareAccess & FILE_SHARE_DELETE) != 0;
}

VOID
NTAPI
IopDoNameTransmogrify(
    IN PIRP Irp,
    IN PFILE_OBJECT FileObject,
    IN PREPARSE_DATA_BUFFER DataBuffer)
{
    PWCHAR Buffer;
    USHORT Length;

    PAGED_CODE();

    ASSERT(Irp->IoStatus.Status == STATUS_REPARSE);
    ASSERT(Irp->IoStatus.Information == IO_REPARSE_TAG_MOUNT_POINT);

    if (DataBuffer == NULL) return;

    Length = DataBuffer->MountPointReparseBuffer.SubstituteNameLength;
    if (Length == 0 || Length > MAXIMUM_FILENAME_LENGTH) return;

    Buffer = ExAllocatePoolUninitialized(PagedPool,
                                         Length + sizeof(WCHAR),
                                         TAG_IO_NAME);
    if (Buffer == NULL) return;

    RtlCopyMemory(Buffer,
                  (PUCHAR)DataBuffer->MountPointReparseBuffer.PathBuffer +
                      DataBuffer->MountPointReparseBuffer.SubstituteNameOffset,
                  Length);
    Buffer[Length / sizeof(WCHAR)] = UNICODE_NULL;

    if (FileObject->FileName.Buffer != NULL)
    {
        ExFreePool(FileObject->FileName.Buffer);
    }

    FileObject->FileName.Buffer = Buffer;
    FileObject->FileName.Length = Length;
    FileObject->FileName.MaximumLength = Length + sizeof(WCHAR);

    ExFreePool(DataBuffer);
    Irp->Tail.Overlay.AuxiliaryBuffer = NULL;
}

NTSTATUS
IopCheckTopDeviceHint(
    _In_ PEXTENDED_DEVOBJ_EXTENSION DeviceExtension,
    _Inout_ PDEVICE_OBJECT *DeviceObject,
    _In_ POPEN_PACKET OpenPacket)
{
    PDEVICE_OBJECT Hint;

    Hint = OpenPacket->DriverCreateContext.DeviceObjectHint;
    if (Hint == NULL) return STATUS_INVALID_PARAMETER;

    if (!IopVerifyDeviceObjectOnStack(DeviceExtension->DeviceObject, Hint))
    {
        return STATUS_INVALID_DEVICE_OBJECT_PARAMETER;
    }

    *DeviceObject = Hint;

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopSendCreateIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PFILE_OBJECT FileObject,
    _In_ POPEN_PACKET OpenPacket,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PIRP *CreatedIrp)
{
    PIO_SECURITY_CONTEXT SecurityContext;
    PIO_STACK_LOCATION StackPtr;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    PAGED_CODE();

    *CreatedIrp = NULL;

    SecurityContext = ExAllocatePoolZero(NonPagedPool,
                                         sizeof(IO_SECURITY_CONTEXT),
                                         TAG_IO);
    if (SecurityContext == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    SecurityContext->SecurityQos = SecurityQos;
    SecurityContext->AccessState = AccessState;
    SecurityContext->DesiredAccess = AccessState->RemainingDesiredAccess |
                                     AccessState->PreviouslyGrantedAccess;
    SecurityContext->FullCreateOptions = OpenPacket->CreateOptions;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        ExFreePoolWithTag(SecurityContext, TAG_IO);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp->Flags = IRP_CREATE_OPERATION | IRP_SYNCHRONOUS_API;
    Irp->RequestorMode = AccessMode;
    Irp->UserIosb = &Irp->IoStatus;
    Irp->UserEvent = NULL;
    Irp->Overlay.AllocationSize = OpenPacket->AllocationSize;
    Irp->AssociatedIrp.SystemBuffer = OpenPacket->EaBuffer;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;
    Irp->Tail.Overlay.DriverContext[0] = SecurityContext;

    /* The parameter list travels in this cell for as long as the create runs */
    Irp->UserBuffer = &OpenPacket->DriverCreateContext.ExtraCreateParameter;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->FileObject = FileObject;
    StackPtr->DeviceObject = DeviceObject;
    StackPtr->Flags = 0;

    if (OpenPacket->Options & IO_NO_PARAMETER_CHECKING)
    {
        StackPtr->Flags |= SL_FORCE_ACCESS_CHECK;
    }

    if (!(OpenPacket->CreateOptions & FILE_OPEN_REPARSE_POINT))
    {
        StackPtr->Flags |= SL_STOP_ON_SYMLINK;
    }

    if (OpenPacket->Options & IO_OPEN_TARGET_DIRECTORY)
    {
        StackPtr->Flags |= SL_OPEN_TARGET_DIRECTORY;
    }

    if (OpenPacket->Options & IO_OPEN_PAGING_FILE)
    {
        StackPtr->Flags |= SL_OPEN_PAGING_FILE;
    }

    switch (OpenPacket->CreateFileType)
    {
        case CreateFileTypeNone:
            StackPtr->MajorFunction = IRP_MJ_CREATE;
            StackPtr->Parameters.Create.SecurityContext = SecurityContext;
            StackPtr->Parameters.Create.Options =
                (OpenPacket->Disposition << 24) |
                (OpenPacket->CreateOptions & 0x00FFFFFF);
            StackPtr->Parameters.Create.FileAttributes =
                OpenPacket->FileAttributes;
            StackPtr->Parameters.Create.ShareAccess = OpenPacket->ShareAccess;
            StackPtr->Parameters.Create.EaLength = OpenPacket->EaLength;
            break;

        case CreateFileTypeNamedPipe:
            StackPtr->MajorFunction = IRP_MJ_CREATE_NAMED_PIPE;
            StackPtr->Parameters.CreatePipe.SecurityContext = SecurityContext;
            StackPtr->Parameters.CreatePipe.Options =
                (OpenPacket->Disposition << 24) |
                (OpenPacket->CreateOptions & 0x00FFFFFF);
            StackPtr->Parameters.CreatePipe.ShareAccess =
                OpenPacket->ShareAccess;
            StackPtr->Parameters.CreatePipe.Parameters =
                OpenPacket->MailslotOrPipeParameters;
            break;

        case CreateFileTypeMailslot:
            StackPtr->MajorFunction = IRP_MJ_CREATE_MAILSLOT;
            StackPtr->Parameters.CreateMailslot.SecurityContext =
                SecurityContext;
            StackPtr->Parameters.CreateMailslot.Options =
                (OpenPacket->Disposition << 24) |
                (OpenPacket->CreateOptions & 0x00FFFFFF);
            StackPtr->Parameters.CreateMailslot.ShareAccess =
                OpenPacket->ShareAccess;
            StackPtr->Parameters.CreateMailslot.Parameters =
                OpenPacket->MailslotOrPipeParameters;
            break;

        default:
            ExFreePoolWithTag(SecurityContext, TAG_IO);
            IoFreeIrp(Irp);
            return STATUS_INVALID_PARAMETER;
    }

    IoSetCompletionRoutine(Irp,
                           IopSynchronousCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    if (OpenPacket->DriverCreateContext.ExtraCreateParameter != NULL)
    {
        FsRtlpMarkCallerEcps(
            OpenPacket->DriverCreateContext.ExtraCreateParameter);
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    if (OpenPacket->DriverCreateContext.ExtraCreateParameter != NULL)
    {
        FsRtlpFreeAddedEcps(
            OpenPacket->DriverCreateContext.ExtraCreateParameter);
    }

    *CreatedIrp = Irp;

    return Status;
}

static
NTSTATUS
IopCompleteCreate(
    _In_ PIRP Irp,
    _In_ PFILE_OBJECT FileObject,
    _In_ POPEN_PACKET OpenPacket,
    _In_ NTSTATUS DispatchStatus,
    _Inout_ PUNICODE_STRING CompleteName,
    _Out_ PVOID *Object)
{
    NTSTATUS Status = DispatchStatus;

    PAGED_CODE();

    if (Irp->Tail.Overlay.DriverContext[0] != NULL)
    {
        ExFreePoolWithTag(Irp->Tail.Overlay.DriverContext[0], TAG_IO);
        Irp->Tail.Overlay.DriverContext[0] = NULL;
    }

    OpenPacket->Information = Irp->IoStatus.Information;

    if (Status == STATUS_REPARSE)
    {
        PREPARSE_DATA_BUFFER Reparse;

        if (Irp->IoStatus.Information == IO_REPARSE_TAG_MOUNT_POINT)
        {
            Reparse = (PREPARSE_DATA_BUFFER)Irp->Tail.Overlay.AuxiliaryBuffer;
            IopDoNameTransmogrify(Irp, FileObject, Reparse);
        }

        if (FileObject->FileName.Length != 0 &&
            CompleteName->MaximumLength >= FileObject->FileName.Length)
        {
            RtlCopyMemory(CompleteName->Buffer,
                          FileObject->FileName.Buffer,
                          FileObject->FileName.Length);
            CompleteName->Length = FileObject->FileName.Length;
        }

        OpenPacket->FileObject = NULL;
        *Object = NULL;

        IoFreeIrp(Irp);

        if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly)
        {
            ObDereferenceObject(FileObject);
        }

        return STATUS_REPARSE;
    }

    IoFreeIrp(Irp);

    if (!NT_SUCCESS(Status))
    {
        OpenPacket->FinalStatus = Status;
        OpenPacket->FileObject = NULL;
        *Object = NULL;

        if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly)
        {
            ObDereferenceObject(FileObject);
        }

        return Status;
    }

    FileObject->Flags |= FO_HANDLE_CREATED;

    OpenPacket->FileObject = FileObject;
    OpenPacket->FinalStatus = Status;
    OpenPacket->ParseCheck = 1;

    if (OpenPacket->QueryOnly || OpenPacket->DeleteOnly)
    {
        *Object = NULL;
        return Status;
    }

    *Object = FileObject;

    return Status;
}

/* PARSE METHODS **************************************************************/

NTSTATUS
NTAPI
IopParseDeviceEx(
    IN PVOID ParseObject,
    IN PVOID ObjectType,
    IN OUT PACCESS_STATE AccessState,
    IN KPROCESSOR_MODE AccessMode,
    IN ULONG Attributes,
    IN OUT PUNICODE_STRING CompleteName,
    IN OUT PUNICODE_STRING RemainingName,
    IN OUT PVOID Context OPTIONAL,
    IN PSECURITY_QUALITY_OF_SERVICE SecurityQos OPTIONAL,
    IN POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    OUT PVOID *Object)
{
    PEXTENDED_DEVOBJ_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject = ParseObject;
    PDEVICE_OBJECT TargetDevice;
    POPEN_PACKET OpenPacket = Context;
    PFILE_OBJECT FileObject;
    BOOLEAN DeviceOnlyOpen;
    BOOLEAN DirectOpen = FALSE;
    NTSTATUS Status;
    PVPB Vpb = NULL;
    PIRP Irp;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(ObjectType);
    UNREFERENCED_PARAMETER(ExtendedParameters);

    *Object = NULL;

    if (!IopValidateOpenPacket(OpenPacket)) return STATUS_OBJECT_TYPE_MISMATCH;

    if (OpenPacket->Options & IO_NO_PARAMETER_CHECKING)
    {
        AccessMode = KernelMode;
    }

    DeviceOnlyOpen = (RemainingName->Length == 0);

    if (!DeviceOnlyOpen && RemainingName->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    if (DeviceOnlyOpen &&
        (OpenPacket->RelatedFileObject == NULL) &&
        (OpenPacket->CreateOptions & FILE_DIRECTORY_FILE))
    {
        return STATUS_NOT_A_DIRECTORY;
    }

    if (!IopCheckDeviceAndDriver(OpenPacket, DeviceObject))
    {
        return STATUS_DELETE_PENDING;
    }

    Status = IopCheckDeviceAccess(DeviceObject,
                                  AccessState,
                                  AccessMode,
                                  DeviceOnlyOpen);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceExtension = IoGetDevObjExtension(DeviceObject);

    if (OpenPacket->InternalFlags & IOP_USE_TOP_LEVEL_DEVICE_HINT)
    {
        TargetDevice = DeviceObject;

        Status = IopCheckTopDeviceHint(DeviceExtension,
                                       &TargetDevice,
                                       OpenPacket);
        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        Vpb = IopCheckVpbMounted(OpenPacket,
                                 DeviceObject,
                                 RemainingName,
                                 &Status);
        if (!NT_SUCCESS(Status)) return Status;

        if (Vpb != NULL)
        {
            TargetDevice = IoGetAttachedDevice(Vpb->DeviceObject);
        }
        else
        {
            TargetDevice = IoGetAttachedDevice(DeviceObject);
            DirectOpen = TRUE;
        }
    }

    Status = IopReferenceDeviceObject(TargetDevice);
    if (!NT_SUCCESS(Status))
    {
        if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);
        return Status;
    }

    FileObject = IopAllocateFileObject(OpenPacket,
                                       TargetDevice,
                                       RemainingName,
                                       Attributes,
                                       AccessMode,
                                       &Status);
    if (FileObject == NULL)
    {
        IopDecrementDeviceObjectRef(TargetDevice, FALSE);
        if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);
        return Status;
    }

    FileObject->Vpb = Vpb;

    if (DirectOpen) FileObject->Flags |= FO_DIRECT_DEVICE_OPEN;

    if (OpenPacket->CreateOptions & FILE_DELETE_ON_CLOSE)
    {
        FileObject->Flags |= FO_DELETE_ON_CLOSE;
    }

    IopSetFileObjectAccess(FileObject,
                           AccessState->PreviouslyGrantedAccess,
                           OpenPacket->ShareAccess);

    if (OpenPacket->CreateOptions & FILE_OPEN_FOR_BACKUP_INTENT)
    {
        IopCheckBackupRestorePrivilege(AccessState,
                                       &OpenPacket->CreateOptions,
                                       AccessMode,
                                       OpenPacket->Disposition);
    }

    Status = IopSendCreateIrp(TargetDevice,
                              FileObject,
                              OpenPacket,
                              AccessState,
                              AccessMode,
                              SecurityQos,
                              &Irp);
    if (Irp == NULL)
    {
        if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly)
        {
            ObDereferenceObject(FileObject);
        }

        IopDecrementDeviceObjectRef(TargetDevice, FALSE);
        if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);

        return Status;
    }

    Status = IopCompleteCreate(Irp,
                               FileObject,
                               OpenPacket,
                               Status,
                               CompleteName,
                               Object);

    if (!NT_SUCCESS(Status) && Status != STATUS_REPARSE)
    {
        IopDecrementDeviceObjectRef(TargetDevice, FALSE);
        if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);
    }

    return Status;
}

NTSTATUS
NTAPI
IopParseDevice(
    IN PVOID ParseObject,
    IN PVOID ObjectType,
    IN OUT PACCESS_STATE AccessState,
    IN KPROCESSOR_MODE AccessMode,
    IN ULONG Attributes,
    IN OUT PUNICODE_STRING CompleteName,
    IN OUT PUNICODE_STRING RemainingName,
    IN OUT PVOID Context,
    IN PSECURITY_QUALITY_OF_SERVICE SecurityQos OPTIONAL,
    OUT PVOID *Object)
{
    return IopParseDeviceEx(ParseObject,
                            ObjectType,
                            AccessState,
                            AccessMode,
                            Attributes,
                            CompleteName,
                            RemainingName,
                            Context,
                            SecurityQos,
                            NULL,
                            Object);
}

NTSTATUS
NTAPI
IopParseFileEx(
    IN PVOID ParseObject,
    IN PVOID ObjectType,
    IN OUT PACCESS_STATE AccessState,
    IN KPROCESSOR_MODE AccessMode,
    IN ULONG Attributes,
    IN OUT PUNICODE_STRING CompleteName,
    IN OUT PUNICODE_STRING RemainingName,
    IN OUT PVOID Context OPTIONAL,
    IN PSECURITY_QUALITY_OF_SERVICE SecurityQos OPTIONAL,
    IN POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    OUT PVOID *Object)
{
    PFILE_OBJECT FileObject = ParseObject;
    PDEVICE_OBJECT DeviceObject;

    PAGED_CODE();

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    return IopParseDeviceEx(DeviceObject,
                            ObjectType,
                            AccessState,
                            AccessMode,
                            Attributes,
                            CompleteName,
                            RemainingName,
                            Context,
                            SecurityQos,
                            ExtendedParameters,
                            Object);
}

NTSTATUS
NTAPI
IopParseFile(
    IN PVOID ParseObject,
    IN PVOID ObjectType,
    IN OUT PACCESS_STATE AccessState,
    IN KPROCESSOR_MODE AccessMode,
    IN ULONG Attributes,
    IN OUT PUNICODE_STRING CompleteName,
    IN OUT PUNICODE_STRING RemainingName,
    IN OUT PVOID Context OPTIONAL,
    IN PSECURITY_QUALITY_OF_SERVICE SecurityQos OPTIONAL,
    OUT PVOID *Object)
{
    return IopParseFileEx(ParseObject,
                          ObjectType,
                          AccessState,
                          AccessMode,
                          Attributes,
                          CompleteName,
                          RemainingName,
                          Context,
                          SecurityQos,
                          NULL,
                          Object);
}
