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

    if (AccessState->RemainingDesiredAccess == 0 &&
        AccessState->PreviouslyGrantedAccess == 0)
    {
        return STATUS_ACCESS_DENIED;
    }

    if (!DeviceOnlyOpen &&
        (!(DeviceObject->Characteristics & FILE_DEVICE_SECURE_OPEN) ||
         DeviceObject->Vpb != NULL))
    {
        /* TODO: no traverse check against the device of a volume */
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
        RtlZeroMemory(&OpenPacket->LocalFileObject->ObjectHeader, sizeof(OBJECT_HEADER));
        OpenPacket->LocalFileObject->ObjectHeader.PointerCount = 1;
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
                FileObject->DeviceObject = NULL;
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

typedef struct _IOP_NAME_PIECE
{
    const VOID *Buffer;
    ULONG Length;
} IOP_NAME_PIECE, *PIOP_NAME_PIECE;

/*
 * Gives the lookup a new complete name made of the pieces handed in. A piece
 * may lie inside the name being replaced, so the new buffer is filled before
 * the old one goes.
 */
static
NTSTATUS
IopReplaceCompleteName(
    _Inout_ PUNICODE_STRING CompleteName,
    _In_reads_(Count) const IOP_NAME_PIECE *Pieces,
    _In_ ULONG Count)
{
    ULONG Length = 0;
    ULONG Maximum;
    PUCHAR Buffer;
    ULONG Offset = 0;
    ULONG Index;

    for (Index = 0; Index < Count; Index++) Length += Pieces[Index].Length;

    if (Length > MAXUSHORT - sizeof(UNICODE_NULL)) return STATUS_NAME_TOO_LONG;

    /* The name buffer is freed by its length, so it must not come out the size the lookaside hands out */
    Maximum = Length + sizeof(UNICODE_NULL);
    if (Maximum == OBP_NAME_LOOKASIDE_MAX_SIZE) Maximum += sizeof(UNICODE_NULL);

    Buffer = ExAllocatePoolWithTag(PagedPool, Maximum, OB_NAME_TAG);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Count; Index++)
    {
        RtlCopyMemory(&Buffer[Offset],
                      Pieces[Index].Buffer,
                      Pieces[Index].Length);
        Offset += Pieces[Index].Length;
    }

    *(PWCHAR)&Buffer[Length] = UNICODE_NULL;

    ObpFreeObjectNameBuffer(CompleteName);

    CompleteName->Buffer = (PWCHAR)Buffer;
    CompleteName->Length = (USHORT)Length;
    CompleteName->MaximumLength = (USHORT)Maximum;

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopQueryObjectPath(
    _In_ PVOID Object,
    _Out_ POBJECT_NAME_INFORMATION *NameInfo)
{
    POBJECT_NAME_INFORMATION Info;
    ULONG Length = 512;
    ULONG ReturnLength;
    NTSTATUS Status;

    *NameInfo = NULL;

    for (;;)
    {
        Info = ExAllocatePoolWithTag(PagedPool, Length, TAG_IO_NAME);
        if (Info == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = ObQueryNameString(Object, Info, Length, &ReturnLength);
        if (NT_SUCCESS(Status))
        {
            *NameInfo = Info;
            return STATUS_SUCCESS;
        }

        ExFreePoolWithTag(Info, TAG_IO_NAME);

        if ((Status != STATUS_INFO_LENGTH_MISMATCH &&
             Status != STATUS_BUFFER_OVERFLOW) ||
            ReturnLength <= Length)
        {
            return Status;
        }

        Length = ReturnLength;
    }
}

/*
 * A relative symbolic link is read from the directory that holds it. The
 * lookup can only start again from the top, so the name it is given is the
 * whole path of that directory with the link's own text after it, and the
 * dots are then walked away within the volume: a link cannot climb out of it.
 */
static
NTSTATUS
IopResolveRelativeSymbolicLink(
    _In_ PFILE_OBJECT FileObject,
    _In_ POPEN_PACKET OpenPacket,
    _In_ const IOP_NAME_PIECE *Substitute,
    _In_ ULONG TailLength,
    _Inout_ PUNICODE_STRING CompleteName)
{
    PFILE_OBJECT RelatedFileObject = OpenPacket->RelatedFileObject;
    POBJECT_NAME_INFORMATION RelatedPath = NULL;
    POBJECT_NAME_INFORMATION DevicePath = NULL;
    const WCHAR *Link = FileObject->FileName.Buffer;
    ULONG LinkLength = FileObject->FileName.Length - TailLength;
    IOP_NAME_PIECE Pieces[5];
    ULONG Count = 0;
    ULONG VolumeOffset;
    USHORT NewLength;
    NTSTATUS Status;

    /* What stays of the link's own name is the directory part, separator included */
    while (LinkLength != 0 &&
           Link[LinkLength / sizeof(WCHAR) - 1] != OBJ_NAME_PATH_SEPARATOR)
    {
        LinkLength -= sizeof(WCHAR);
    }

    if (RelatedFileObject == NULL)
    {
        if (CompleteName->Length < FileObject->FileName.Length)
        {
            return STATUS_IO_REPARSE_DATA_INVALID;
        }

        VolumeOffset = CompleteName->Length - FileObject->FileName.Length;

        Pieces[Count].Buffer = CompleteName->Buffer;
        Pieces[Count].Length = VolumeOffset + LinkLength;
        Count++;
    }
    else
    {
        Status = IopQueryObjectPath(RelatedFileObject, &RelatedPath);
        if (!NT_SUCCESS(Status)) return Status;

        Status = IopQueryObjectPath(RelatedFileObject->DeviceObject,
                                    &DevicePath);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(RelatedPath, TAG_IO_NAME);
            return Status;
        }

        VolumeOffset = DevicePath->Name.Length;
        ExFreePoolWithTag(DevicePath, TAG_IO_NAME);

        if (RelatedPath->Name.Length < VolumeOffset)
        {
            ExFreePoolWithTag(RelatedPath, TAG_IO_NAME);
            return STATUS_IO_REPARSE_DATA_INVALID;
        }

        Pieces[Count].Buffer = RelatedPath->Name.Buffer;
        Pieces[Count].Length = RelatedPath->Name.Length;
        Count++;

        if (RelatedPath->Name.Length == VolumeOffset ||
            RelatedPath->Name.Buffer[RelatedPath->Name.Length / sizeof(WCHAR) - 1] !=
                OBJ_NAME_PATH_SEPARATOR)
        {
            Pieces[Count].Buffer = L"\\";
            Pieces[Count].Length = sizeof(WCHAR);
            Count++;
        }

        Pieces[Count].Buffer = Link;
        Pieces[Count].Length = LinkLength;
        Count++;
    }

    Pieces[Count++] = *Substitute;

    Pieces[Count].Buffer = (PUCHAR)FileObject->FileName.Buffer +
                           FileObject->FileName.Length - TailLength;
    Pieces[Count].Length = TailLength;
    Count++;

    Status = IopReplaceCompleteName(CompleteName, Pieces, Count);

    if (RelatedPath != NULL) ExFreePoolWithTag(RelatedPath, TAG_IO_NAME);

    if (!NT_SUCCESS(Status)) return Status;

    Status = FsRtlRemoveDotsFromPath(
                 &CompleteName->Buffer[VolumeOffset / sizeof(WCHAR)],
                 (USHORT)(CompleteName->Length - VolumeOffset),
                 &NewLength);
    if (!NT_SUCCESS(Status)) return Status;

    CompleteName->Length = (USHORT)(VolumeOffset + NewLength);

    return STATUS_SUCCESS;
}

/*
 * A file system that meets a reparse point it does not follow itself completes
 * the create with STATUS_REPARSE and says why. A name it rewrote, or a volume
 * to mount again, only needs the lookup started over. A mount point or a
 * symbolic link comes with its reparse data, and the name to start over with
 * is what the data stands for followed by the part of the name the file system
 * had not read yet.
 */
static
NTSTATUS
IopResolveReparse(
    _In_ PIRP Irp,
    _In_ PFILE_OBJECT FileObject,
    _In_ POPEN_PACKET OpenPacket,
    _Inout_ PUNICODE_STRING CompleteName)
{
    PREPARSE_DATA_BUFFER Reparse;
    ULONG_PTR Tag = Irp->IoStatus.Information;
    IOP_NAME_PIECE Pieces[2];
    ULONG PathLength, Offset, Length;
    ULONG TailLength;
    BOOLEAN Relative = FALSE;
    PUCHAR Path;
    NTSTATUS Status;

    PAGED_CODE();

    Reparse = (PREPARSE_DATA_BUFFER)Irp->Tail.Overlay.AuxiliaryBuffer;
    Irp->Tail.Overlay.AuxiliaryBuffer = NULL;

    if (Tag == IO_REMOUNT)
    {
        if (Reparse != NULL) ExFreePool(Reparse);
        return STATUS_REPARSE;
    }

    if (Tag == IO_REPARSE)
    {
        if (Reparse != NULL) ExFreePool(Reparse);

        Pieces[0].Buffer = FileObject->FileName.Buffer;
        Pieces[0].Length = FileObject->FileName.Length;

        Status = IopReplaceCompleteName(CompleteName, Pieces, 1);

        return NT_SUCCESS(Status) ? STATUS_REPARSE : Status;
    }

    if (Reparse == NULL) return STATUS_IO_REPARSE_DATA_INVALID;

    if (Tag != IO_REPARSE_TAG_MOUNT_POINT && Tag != IO_REPARSE_TAG_SYMLINK)
    {
        ExFreePool(Reparse);
        return STATUS_IO_REPARSE_TAG_NOT_HANDLED;
    }

    if (OpenPacket->Options & IO_STOP_ON_SYMLINK)
    {
        /* The caller asked to be shown the reparse data and frees it */
        OpenPacket->Information = (ULONG_PTR)Reparse;
        return STATUS_STOPPED_ON_SYMLINK;
    }

    if (Tag == IO_REPARSE_TAG_SYMLINK)
    {
        PathLength = FIELD_OFFSET(REPARSE_DATA_BUFFER,
                                  SymbolicLinkReparseBuffer.PathBuffer) -
                     REPARSE_DATA_BUFFER_HEADER_SIZE;
        Path = (PUCHAR)Reparse->SymbolicLinkReparseBuffer.PathBuffer;
        Offset = Reparse->SymbolicLinkReparseBuffer.SubstituteNameOffset;
        Length = Reparse->SymbolicLinkReparseBuffer.SubstituteNameLength;
    }
    else
    {
        PathLength = FIELD_OFFSET(REPARSE_DATA_BUFFER,
                                  MountPointReparseBuffer.PathBuffer) -
                     REPARSE_DATA_BUFFER_HEADER_SIZE;
        Path = (PUCHAR)Reparse->MountPointReparseBuffer.PathBuffer;
        Offset = Reparse->MountPointReparseBuffer.SubstituteNameOffset;
        Length = Reparse->MountPointReparseBuffer.SubstituteNameLength;
    }

    TailLength = Reparse->Reserved;

    if (Reparse->ReparseDataLength < PathLength ||
        Offset + Length > Reparse->ReparseDataLength - PathLength ||
        Length == 0 ||
        ((Offset | Length | TailLength) & 1) != 0 ||
        TailLength > FileObject->FileName.Length)
    {
        ExFreePool(Reparse);
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (Tag == IO_REPARSE_TAG_SYMLINK)
    {
        Relative = BooleanFlagOn(Reparse->SymbolicLinkReparseBuffer.Flags,
                                 SYMLINK_FLAG_RELATIVE);
    }

    Pieces[0].Buffer = Path + Offset;
    Pieces[0].Length = Length;

    if (Relative)
    {
        Status = IopResolveRelativeSymbolicLink(FileObject,
                                                OpenPacket,
                                                &Pieces[0],
                                                TailLength,
                                                CompleteName);
    }
    else
    {
        Pieces[1].Buffer = (PUCHAR)FileObject->FileName.Buffer +
                           FileObject->FileName.Length - TailLength;
        Pieces[1].Length = TailLength;

        /* A substitute name that ends on a separator already supplies the one the rest of the path starts with */
        if (Pieces[1].Length != 0 &&
            ((const WCHAR *)Pieces[0].Buffer)[Pieces[0].Length / sizeof(WCHAR) - 1] == OBJ_NAME_PATH_SEPARATOR &&
            ((const WCHAR *)Pieces[1].Buffer)[0] == OBJ_NAME_PATH_SEPARATOR)
        {
            Pieces[1].Buffer = (const UCHAR *)Pieces[1].Buffer + sizeof(WCHAR);
            Pieces[1].Length -= sizeof(WCHAR);
        }

        Status = IopReplaceCompleteName(CompleteName, Pieces, 2);
    }

    ExFreePool(Reparse);

    return NT_SUCCESS(Status) ? STATUS_REPARSE : Status;
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

    if (OpenPacket->Options & IO_FORCE_ACCESS_CHECK)
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
NTAPI
IopQueryOnlyComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* The packet is finished here, so no completion APC touches the stack file object */
static
NTSTATUS
IopQueryOpenedFile(
    _In_ PFILE_OBJECT FileObject,
    _In_ POPEN_PACKET OpenPacket)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT DeviceObject;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    ASSERT(OpenPacket->QueryOnly);
    ASSERT(OpenPacket->FileInformation != NULL);
    ASSERT(FileObject == (PFILE_OBJECT)&OpenPacket->LocalFileObject->FileObjectBody);

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->AssociatedIrp.SystemBuffer = OpenPacket->FileInformation;
    Irp->UserIosb = &IoStatusBlock;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_API | IRP_BUFFERED_IO;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_QUERY_INFORMATION;
    StackPtr->FileObject = FileObject;
    StackPtr->DeviceObject = DeviceObject;
    StackPtr->Parameters.QueryFile.Length = OpenPacket->FileInformationLength;
    StackPtr->Parameters.QueryFile.FileInformationClass =
        OpenPacket->FileInformationClass;

    IoSetCompletionRoutine(Irp, IopQueryOnlyComplete, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);

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
        Status = IopResolveReparse(Irp, FileObject, OpenPacket, CompleteName);

        /* The lookup starts over from the top, so nothing it opens next is relative to a file */
        OpenPacket->RelatedFileObject = NULL;
        OpenPacket->FileObject = NULL;
        *Object = NULL;

        IoFreeIrp(Irp);

        if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly)
        {
            FileObject->DeviceObject = NULL;
            ObDereferenceObject(FileObject);
        }

        if (Status != STATUS_REPARSE) OpenPacket->FinalStatus = Status;

        return Status;
    }

    IoFreeIrp(Irp);

    if (!NT_SUCCESS(Status))
    {
        OpenPacket->FinalStatus = Status;
        OpenPacket->FileObject = NULL;
        *Object = NULL;

        if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly)
        {
            FileObject->DeviceObject = NULL;
            ObDereferenceObject(FileObject);
        }

        return Status;
    }

    /* The opened file answers the query and is closed; the open itself stays a success */
    if (OpenPacket->QueryOnly)
    {
        OpenPacket->FinalStatus = IopQueryOpenedFile(FileObject, OpenPacket);
        IopCloseLocalFileObject(OpenPacket->LocalFileObject);
        OpenPacket->FileObject = NULL;
        OpenPacket->ParseCheck = 1;
        *Object = NULL;
        return Status;
    }

    if (OpenPacket->DeleteOnly)
    {
        IopCloseLocalFileObject(OpenPacket->LocalFileObject);
        OpenPacket->FileObject = NULL;
        OpenPacket->FinalStatus = Status;
        OpenPacket->ParseCheck = 1;
        *Object = NULL;
        return Status;
    }

    FileObject->Flags |= FO_HANDLE_CREATED;
    KeSetEvent(&FileObject->Event, IO_NO_INCREMENT, FALSE);

    OpenPacket->FileObject = FileObject;
    OpenPacket->FinalStatus = Status;
    OpenPacket->ParseCheck = 1;

    *Object = FileObject;

    return Status;
}

static
PVPB
IopReferenceRelatedVpb(
    _In_ PFILE_OBJECT RelatedFileObject)
{
    KIRQL OldIrql;
    PVPB Vpb;

    KeAcquireSpinLock(&IopVpbSpinLock, &OldIrql);

    Vpb = RelatedFileObject->Vpb;
    if (Vpb != NULL) Vpb->ReferenceCount++;

    KeReleaseSpinLock(&IopVpbSpinLock, OldIrql);

    return Vpb;
}

static
NTSTATUS
IopReferenceCreateTransaction(
    _In_ POPEN_PACKET OpenPacket,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PTXN_PARAMETER_BLOCK TxnParameters)
{
    PTXN_PARAMETER_BLOCK Supplied = OpenPacket->DriverCreateContext.TxnParameters;
    PKTRANSACTION Transaction;
    HANDLE Handle = NULL;
    NTSTATUS Status;
    PTEB32 Teb32;
    PTEB Teb;

    PAGED_CODE();

    RtlZeroMemory(TxnParameters, sizeof(*TxnParameters));

    if (Supplied != NULL)
    {
        if (Supplied->TransactionObject == NULL) return STATUS_SUCCESS;

        ObReferenceObject(Supplied->TransactionObject);
        *TxnParameters = *Supplied;
        return STATUS_SUCCESS;
    }

    if (AccessMode == KernelMode) return STATUS_SUCCESS;

    Teb = PsGetCurrentThreadTeb();
    if (Teb == NULL) return STATUS_SUCCESS;

    _SEH2_TRY
    {
        ProbeForRead(&Teb->CurrentTransactionHandle, sizeof(HANDLE), sizeof(ULONG));
        Handle = Teb->CurrentTransactionHandle;
        ProbeForRead(&Teb->WowTebOffset, sizeof(LONG), sizeof(LONG));
        if (Handle == NULL && Teb->WowTebOffset != 0)
        {
            Teb32 = (PTEB32)((PUCHAR)Teb + Teb->WowTebOffset);
            ProbeForRead(&Teb32->CurrentTransactionHandle, sizeof(ULONG), sizeof(ULONG));
            Handle = LongToHandle((LONG)Teb32->CurrentTransactionHandle);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Handle == NULL) return STATUS_SUCCESS;

    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       TmTransactionObjectType,
                                       AccessMode,
                                       (PVOID *)&Transaction,
                                       NULL);
    if (!NT_SUCCESS(Status)) return STATUS_CURRENT_TRANSACTION_NOT_VALID;

    TxnParameters->Length = sizeof(TXN_PARAMETER_BLOCK);
    TxnParameters->TxFsContext = TXF_MINIVERSION_DEFAULT_VIEW;
    TxnParameters->TransactionObject = Transaction;
    return STATUS_SUCCESS;
}

static
NTSTATUS
IopCheckTransactedOpen(
    _In_ PTXN_PARAMETER_BLOCK TxnParameters,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    PAGED_CODE();

    if (!TmIsTransactionActive(TxnParameters->TransactionObject))
    {
        return STATUS_TRANSACTION_NOT_ACTIVE;
    }

    if (TargetDevice->Flags & DO_SUPPORTS_TRANSACTIONS) return STATUS_SUCCESS;

    if (((DeviceObject->Characteristics | TargetDevice->Characteristics) & FILE_REMOTE_DEVICE) ||
        DeviceObject->DeviceType == FILE_DEVICE_MULTI_UNC_PROVIDER)
    {
        return STATUS_TRANSACTIONS_UNSUPPORTED_REMOTE;
    }

    return STATUS_TRANSACTIONAL_OPEN_NOT_ALLOWED;
}

static
NTSTATUS
IopAttachCreateTransaction(
    _In_ PFILE_OBJECT FileObject,
    _In_ PTXN_PARAMETER_BLOCK TxnParameters)
{
    PFILE_OBJECT_EXTENSION Extension;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopAllocateFileObjectExtension(FileObject);
    if (!NT_SUCCESS(Status)) return Status;

    Extension = FileObject->FileObjectExtension;
    Extension->TxnParameters = *TxnParameters;
    return STATUS_SUCCESS;
}

static
VOID
IopDetachLocalFileObjectExtension(
    _In_ POPEN_PACKET OpenPacket,
    _In_ PFILE_OBJECT FileObject)
{
    PTXN_PARAMETER_BLOCK TxnParameters;

    PAGED_CODE();

    /* A local file object the driver never opened is never closed, so what it holds goes back here */
    if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly) return;
    if (!(FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION)) return;

    TxnParameters = IoGetTransactionParameterBlock(FileObject);
    if (TxnParameters != NULL) ObDereferenceObject(TxnParameters->TransactionObject);

    ExFreePoolWithTag(FileObject->FileObjectExtension, TAG_IO);
    FileObject->FileObjectExtension = NULL;
    FileObject->Flags &= ~FO_FILE_OBJECT_HAS_EXTENSION;
}

/* PARSE METHODS **************************************************************/

#define IOP_DIRECT_DEVICE_OPEN_ACCESS (SYNCHRONIZE | FILE_READ_ATTRIBUTES)

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
    TXN_PARAMETER_BLOCK TxnParameters;
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

    /* The name was opened without a type, so generic rights become a file's rights only here */
    RtlMapGenericMask(&AccessState->RemainingDesiredAccess, &IopFileMapping);

    /* A name opened relative to a file is the file system's to read, whatever it holds */
    DeviceOnlyOpen = (RemainingName->Length == 0) &&
                     (OpenPacket->RelatedFileObject == NULL);

    if (OpenPacket->RelatedFileObject == NULL &&
        RemainingName->Length != 0 &&
        RemainingName->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
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
    else if (DeviceOnlyOpen &&
             (OpenPacket->RelatedFileObject == NULL) &&
             !(OpenPacket->CreateOptions & FILE_DIRECTORY_FILE) &&
             !((AccessState->RemainingDesiredAccess |
                AccessState->PreviouslyGrantedAccess) & ~IOP_DIRECT_DEVICE_OPEN_ACCESS))
    {
        /* A device opened only to query its attributes is not accessed, so nothing is mounted on it */
        TargetDevice = IoGetAttachedDevice(DeviceObject);
        DirectOpen = TRUE;
    }
    else if (OpenPacket->RelatedFileObject != NULL)
    {
        /* The new file lives on the volume the file it is relative to was opened on */
        Vpb = IopReferenceRelatedVpb(OpenPacket->RelatedFileObject);

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

    Status = IopReferenceCreateTransaction(OpenPacket, AccessMode, &TxnParameters);
    if (NT_SUCCESS(Status) && TxnParameters.TransactionObject != NULL)
    {
        Status = IopCheckTransactedOpen(&TxnParameters, DeviceObject, TargetDevice);
        if (!NT_SUCCESS(Status)) ObDereferenceObject(TxnParameters.TransactionObject);
    }
    if (!NT_SUCCESS(Status))
    {
        if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);
        return Status;
    }

    /* The file object names the device that was opened; requests go to the top of what serves it */
    Status = IopReferenceDeviceObject(DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        if (TxnParameters.TransactionObject != NULL) ObDereferenceObject(TxnParameters.TransactionObject);
        if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);
        return Status;
    }

    FileObject = IopAllocateFileObject(OpenPacket,
                                       DeviceObject,
                                       RemainingName,
                                       Attributes,
                                       AccessMode,
                                       &Status);
    if (FileObject != NULL && TxnParameters.TransactionObject != NULL)
    {
        Status = IopAttachCreateTransaction(FileObject, &TxnParameters);
        if (!NT_SUCCESS(Status))
        {
            if (!OpenPacket->QueryOnly && !OpenPacket->DeleteOnly)
            {
                FileObject->DeviceObject = NULL;
                ObDereferenceObject(FileObject);
            }
            FileObject = NULL;
        }
    }
    if (FileObject == NULL)
    {
        if (TxnParameters.TransactionObject != NULL) ObDereferenceObject(TxnParameters.TransactionObject);
        IopDecrementDeviceObjectRef(DeviceObject, FALSE);
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
            FileObject->DeviceObject = NULL;
            ObDereferenceObject(FileObject);
        }

        IopDetachLocalFileObjectExtension(OpenPacket, FileObject);
        IopDecrementDeviceObjectRef(DeviceObject, FALSE);
        if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);

        return Status;
    }

    Status = IopCompleteCreate(Irp,
                               FileObject,
                               OpenPacket,
                               Status,
                               CompleteName,
                               Object);

    /* A create the driver did not complete leaves the device and volume to this routine */
    if (!NT_SUCCESS(Status) || Status == STATUS_REPARSE)
    {
        IopDetachLocalFileObjectExtension(OpenPacket, FileObject);
        IopDecrementDeviceObjectRef(DeviceObject, FALSE);
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
    POPEN_PACKET OpenPacket = Context;

    PAGED_CODE();

    if (!IopValidateOpenPacket(OpenPacket)) return STATUS_OBJECT_TYPE_MISMATCH;

    if (FileObject->DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    OpenPacket->RelatedFileObject = FileObject;

    return IopParseDeviceEx(FileObject->DeviceObject,
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
