/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a file object is and what the object manager asks of it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
IopSendCloseIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PFILE_OBJECT FileObject,
    _In_ UCHAR MajorFunction)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return;

    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_API | IRP_CLOSE_OPERATION;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = MajorFunction;
    StackPtr->FileObject = FileObject;
    StackPtr->DeviceObject = DeviceObject;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
}

NTSTATUS
NTAPI
IopAllocateFileObjectExtension(
    IN PFILE_OBJECT FileObject)
{
    PFILE_OBJECT_EXTENSION Extension;

    if (FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION)
    {
        return STATUS_SUCCESS;
    }

    Extension = ExAllocatePoolZero(NonPagedPool,
                                   sizeof(FILE_OBJECT_EXTENSION),
                                   TAG_IO);
    if (Extension == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (InterlockedCompareExchangePointer(&FileObject->FileObjectExtension,
                                          Extension,
                                          NULL) != NULL)
    {
        ExFreePoolWithTag(Extension, TAG_IO);
        return STATUS_SUCCESS;
    }

    FileObject->Flags |= FO_FILE_OBJECT_HAS_EXTENSION;

    return STATUS_SUCCESS;
}

/* OBJECT MANAGER METHODS *****************************************************/

VOID
NTAPI
IopDeleteFile(
    IN PVOID ObjectBody)
{
    PFILE_OBJECT FileObject = ObjectBody;
    PDEVICE_OBJECT DeviceObject;
    PVPB Vpb;

    if (FileObject->DeviceObject == NULL) return;

    if (!(FileObject->Flags & FO_DIRECT_DEVICE_OPEN))
    {
        DeviceObject = IoGetRelatedDeviceObject(FileObject);
    }
    else
    {
        DeviceObject = FileObject->DeviceObject;
    }

    if (DeviceObject == NULL) return;

    if (!(FileObject->Flags & FO_HANDLE_CREATED) ||
        (FileObject->Flags & FO_FILE_OPEN_CANCELLED))
    {
        IopSendCloseIrp(DeviceObject, FileObject, IRP_MJ_CLOSE);
    }

    Vpb = FileObject->Vpb;
    if (Vpb != NULL && !(FileObject->Flags & FO_DIRECT_DEVICE_OPEN))
    {
        IopDereferenceVpbAndFree(Vpb);
    }

    if (FileObject->FileName.Buffer != NULL)
    {
        ExFreePool(FileObject->FileName.Buffer);
        FileObject->FileName.Buffer = NULL;
        FileObject->FileName.Length = 0;
        FileObject->FileName.MaximumLength = 0;
    }

    if (FileObject->CompletionContext != NULL)
    {
        ObDereferenceObject(FileObject->CompletionContext->Port);
        ExFreePool(FileObject->CompletionContext);
        FileObject->CompletionContext = NULL;
    }

    if (FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION)
    {
        PVOID Extension = FileObject->FileObjectExtension;

        FileObject->FileObjectExtension = NULL;
        FileObject->Flags &= ~FO_FILE_OBJECT_HAS_EXTENSION;

        if (Extension != NULL) ExFreePoolWithTag(Extension, TAG_IO);
    }

    IopDecrementDeviceObjectRef(DeviceObject, FALSE);
}

VOID
NTAPI
IopCloseFile(
    IN PEPROCESS Process OPTIONAL,
    IN PVOID Object,
    IN ULONG_PTR ProcessHandleCount,
    IN ULONG_PTR SystemHandleCount)
{
    PFILE_OBJECT FileObject = Object;
    PDEVICE_OBJECT DeviceObject;

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (SystemHandleCount != 1) return;
    if (FileObject->DeviceObject == NULL) return;
    if (FileObject->Flags & FO_DIRECT_DEVICE_OPEN)
    {
        DeviceObject = FileObject->DeviceObject;
    }
    else
    {
        DeviceObject = IoGetRelatedDeviceObject(FileObject);
    }

    if (DeviceObject == NULL) return;

    if (FileObject->LockOperation)
    {
        /* TODO: no fast unlock-all path; the file system is asked instead */
        DPRINT("I/O: byte-range locks are left to the file system on close\n");
    }

    IopSendCloseIrp(DeviceObject, FileObject, IRP_MJ_CLEANUP);

    IopDecrementDeviceObjectHandleCount(DeviceObject);
}

NTSTATUS
NTAPI
IopQueryNameInternal(
    IN PVOID ObjectBody,
    IN BOOLEAN HasName,
    IN BOOLEAN QueryDosName,
    OUT POBJECT_NAME_INFORMATION ObjectNameInfo,
    IN ULONG Length,
    OUT PULONG ReturnLength,
    IN KPROCESSOR_MODE PreviousMode)
{
    POBJECT_NAME_INFORMATION DeviceName = NULL;
    PFILE_OBJECT FileObject = ObjectBody;
    UNICODE_STRING DosName;
    ULONG DeviceLength = 0;
    ULONG TotalLength;
    NTSTATUS Status;
    PWCHAR Cursor;
    BOOLEAN DosNameUsed = FALSE;

    UNREFERENCED_PARAMETER(HasName);
    UNREFERENCED_PARAMETER(PreviousMode);

    RtlInitEmptyUnicodeString(&DosName, NULL, 0);

    if (QueryDosName && FileObject->DeviceObject != NULL)
    {
        Status = IoVolumeDeviceToDosName(FileObject->DeviceObject, &DosName);
        if (NT_SUCCESS(Status))
        {
            DosNameUsed = TRUE;
            DeviceLength = DosName.Length;
        }
    }

    if (!DosNameUsed)
    {
        if (FileObject->DeviceObject == NULL) return STATUS_OBJECT_PATH_INVALID;

        Status = ObQueryNameString(FileObject->DeviceObject,
                                   NULL,
                                   0,
                                   &DeviceLength);
        if (Status != STATUS_INFO_LENGTH_MISMATCH &&
            Status != STATUS_BUFFER_OVERFLOW &&
            Status != STATUS_BUFFER_TOO_SMALL)
        {
            return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
        }

        DeviceName = ExAllocatePoolZero(PagedPool, DeviceLength, TAG_IO_NAME);
        if (DeviceName == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = ObQueryNameString(FileObject->DeviceObject,
                                   DeviceName,
                                   DeviceLength,
                                   &DeviceLength);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(DeviceName, TAG_IO_NAME);
            return Status;
        }

        DeviceLength = DeviceName->Name.Length;
    }

    TotalLength = (ULONG)sizeof(OBJECT_NAME_INFORMATION) + DeviceLength +
                  FileObject->FileName.Length + sizeof(WCHAR);

    *ReturnLength = TotalLength;

    if (Length < TotalLength)
    {
        if (DeviceName != NULL) ExFreePoolWithTag(DeviceName, TAG_IO_NAME);
        if (DosNameUsed) ExFreePool(DosName.Buffer);

        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Cursor = (PWCHAR)(ObjectNameInfo + 1);

    ObjectNameInfo->Name.Buffer = Cursor;
    ObjectNameInfo->Name.MaximumLength =
        (USHORT)(DeviceLength + FileObject->FileName.Length + sizeof(WCHAR));
    ObjectNameInfo->Name.Length =
        (USHORT)(DeviceLength + FileObject->FileName.Length);

    if (DosNameUsed)
    {
        RtlCopyMemory(Cursor, DosName.Buffer, DeviceLength);
    }
    else
    {
        RtlCopyMemory(Cursor, DeviceName->Name.Buffer, DeviceLength);
    }

    Cursor += DeviceLength / sizeof(WCHAR);

    if (FileObject->FileName.Length != 0)
    {
        RtlCopyMemory(Cursor,
                      FileObject->FileName.Buffer,
                      FileObject->FileName.Length);
        Cursor += FileObject->FileName.Length / sizeof(WCHAR);
    }

    *Cursor = UNICODE_NULL;

    if (DeviceName != NULL) ExFreePoolWithTag(DeviceName, TAG_IO_NAME);
    if (DosNameUsed) ExFreePool(DosName.Buffer);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopQueryName(
    IN PVOID ObjectBody,
    IN BOOLEAN HasName,
    OUT POBJECT_NAME_INFORMATION ObjectNameInfo,
    IN ULONG Length,
    OUT PULONG ReturnLength,
    IN KPROCESSOR_MODE PreviousMode)
{
    return IopQueryNameInternal(ObjectBody,
                                HasName,
                                FALSE,
                                ObjectNameInfo,
                                Length,
                                ReturnLength,
                                PreviousMode);
}

NTSTATUS
NTAPI
IoQueryFileDosDeviceName(
    _In_ PFILE_OBJECT FileObject,
    _Out_ POBJECT_NAME_INFORMATION *ObjectNameInformation)
{
    POBJECT_NAME_INFORMATION NameInfo;
    ULONG ReturnLength = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopQueryNameInternal(FileObject,
                                  TRUE,
                                  TRUE,
                                  NULL,
                                  0,
                                  &ReturnLength,
                                  KernelMode);
    if (Status != STATUS_INFO_LENGTH_MISMATCH) return Status;

    NameInfo = ExAllocatePoolZero(PagedPool, ReturnLength, TAG_IO_NAME);
    if (NameInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = IopQueryNameInternal(FileObject,
                                  TRUE,
                                  TRUE,
                                  NameInfo,
                                  ReturnLength,
                                  &ReturnLength,
                                  KernelMode);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(NameInfo, TAG_IO_NAME);
        return Status;
    }

    *ObjectNameInformation = NameInfo;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopGetSetSecurityObject(
    IN PVOID ObjectBody,
    IN SECURITY_OPERATION_CODE OperationCode,
    IN PSECURITY_INFORMATION SecurityInformation,
    IN OUT PSECURITY_DESCRIPTOR SecurityDescriptor,
    IN OUT PULONG BufferLength,
    OUT PSECURITY_DESCRIPTOR *OldSecurityDescriptor,
    IN POOL_TYPE PoolType,
    IN OUT PGENERIC_MAPPING GenericMapping,
    IN KPROCESSOR_MODE AccessMode)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    BOOLEAN LocalFileObject;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(OldSecurityDescriptor);
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(AccessMode);

    if (OperationCode == DeleteSecurityDescriptor) return STATUS_SUCCESS;

    LocalFileObject = (((PFILE_OBJECT)ObjectBody)->Type == IO_TYPE_FILE);

    /*
     * A descriptor handed over at creation is the object's from then on: a
     * device keeps it, and a file object has nowhere to keep one because what
     * it may be opened with is its device's.
     */
    if (OperationCode == AssignSecurityDescriptor)
    {
        if (!LocalFileObject)
        {
            PEXTENDED_DEVOBJ_EXTENSION Extension =
                IoGetDevObjExtension((PDEVICE_OBJECT)ObjectBody);

            ExAcquireResourceExclusiveLite(&IopSecurityResource, TRUE);
            Extension->ExtensionFlags &= ~DOE_DEFAULT_SD_PRESENT;
            ((PDEVICE_OBJECT)ObjectBody)->SecurityDescriptor =
                SecurityDescriptor;
            ExReleaseResourceLite(&IopSecurityResource);

            return STATUS_SUCCESS;
        }

        SeDeassignSecurity(&SecurityDescriptor);
        return STATUS_SUCCESS;
    }

    if (!LocalFileObject)
    {
        return IopSetDeviceSecurityDescriptor(ObjectBody,
                                              SecurityInformation,
                                              SecurityDescriptor,
                                              BufferLength,
                                              PoolType,
                                              GenericMapping);
    }

    FileObject = ObjectBody;

    if (!(FileObject->Flags & FO_DIRECT_DEVICE_OPEN))
    {
        DeviceObject = IoGetRelatedDeviceObject(FileObject);
    }
    else
    {
        DeviceObject = FileObject->DeviceObject;
    }

    if (DeviceObject == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->RequestorMode = KernelMode;
    Irp->Flags = IRP_SYNCHRONOUS_API;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;

    ObReferenceObject(FileObject);

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->FileObject = FileObject;
    StackPtr->DeviceObject = DeviceObject;

    if (OperationCode == QuerySecurityDescriptor)
    {
        StackPtr->MajorFunction = IRP_MJ_QUERY_SECURITY;
        StackPtr->Parameters.QuerySecurity.SecurityInformation =
            *SecurityInformation;
        StackPtr->Parameters.QuerySecurity.Length = *BufferLength;
        Irp->UserBuffer = SecurityDescriptor;
    }
    else
    {
        StackPtr->MajorFunction = IRP_MJ_SET_SECURITY;
        StackPtr->Parameters.SetSecurity.SecurityInformation =
            *SecurityInformation;
        StackPtr->Parameters.SetSecurity.SecurityDescriptor =
            SecurityDescriptor;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (OperationCode == QuerySecurityDescriptor)
    {
        if (Status == STATUS_BUFFER_OVERFLOW ||
            Status == STATUS_BUFFER_TOO_SMALL)
        {
            *BufferLength = (ULONG)IoStatusBlock.Information;
        }
        else if (NT_SUCCESS(Status))
        {
            *BufferLength = (ULONG)IoStatusBlock.Information;
        }
    }

    return Status;
}

NTSTATUS
IopSetDeviceSecurityDescriptor(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PULONG BufferLength,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    NTSTATUS Status;

    PAGED_CODE();

    Extension = IoGetDevObjExtension(DeviceObject);

    ExAcquireResourceExclusiveLite(&IopSecurityResource, TRUE);

    if (Extension->ExtensionFlags & DOE_DEFAULT_SD_PRESENT)
    {
        Extension->ExtensionFlags &= ~DOE_DEFAULT_SD_PRESENT;
        DeviceObject->SecurityDescriptor = NULL;
    }

    Status = SeSetSecurityDescriptorInfo(DeviceObject,
                                         SecurityInformation,
                                         SecurityDescriptor,
                                         &DeviceObject->SecurityDescriptor,
                                         PoolType,
                                         GenericMapping);

    ExReleaseResourceLite(&IopSecurityResource);

    UNREFERENCED_PARAMETER(BufferLength);

    return Status;
}

NTSTATUS
NTAPI
IopSetDeviceSecurityDescriptors(
    IN PDEVICE_OBJECT UpperDeviceObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN PSECURITY_INFORMATION SecurityInformation,
    IN PSECURITY_DESCRIPTOR SecurityDescriptor,
    IN POOL_TYPE PoolType,
    IN PGENERIC_MAPPING GenericMapping)
{
    PDEVICE_OBJECT Current = UpperDeviceObject;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG BufferLength = 0;

    PAGED_CODE();

    while (Current != NULL)
    {
        Status = IopSetDeviceSecurityDescriptor(Current,
                                                SecurityInformation,
                                                SecurityDescriptor,
                                                &BufferLength,
                                                PoolType,
                                                GenericMapping);
        if (!NT_SUCCESS(Status)) return Status;

        if (Current == PhysicalDeviceObject) break;

        Current = IoGetDevObjExtension(Current)->AttachedTo;
    }

    return Status;
}

/* STREAM FILE OBJECTS ********************************************************/

static
PFILE_OBJECT
IopCreateStreamFileObject(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN Lite,
    _Out_opt_ PHANDLE FileObjectHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PFILE_OBJECT CreatedFileObject;
    PDEVICE_OBJECT Target;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (FileObject != NULL)
    {
        Target = FileObject->DeviceObject;
    }
    else
    {
        Target = DeviceObject;
    }

    if (Target == NULL) return NULL;

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ObCreateObject(KernelMode,
                            IoFileObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(FILE_OBJECT),
                            0,
                            0,
                            (PVOID *)&CreatedFileObject);
    if (!NT_SUCCESS(Status)) return NULL;

    RtlZeroMemory(CreatedFileObject, sizeof(FILE_OBJECT));

    CreatedFileObject->Type = IO_TYPE_FILE;
    CreatedFileObject->Size = sizeof(FILE_OBJECT);
    CreatedFileObject->DeviceObject = Target;
    CreatedFileObject->Flags = FO_STREAM_FILE;

    if (FileObject != NULL)
    {
        CreatedFileObject->Vpb = FileObject->Vpb;
        CreatedFileObject->RelatedFileObject = FileObject;
    }
    else if (Target->Vpb != NULL)
    {
        CreatedFileObject->Vpb = Target->Vpb;
    }

    KeInitializeEvent(&CreatedFileObject->Lock, SynchronizationEvent, FALSE);
    KeInitializeEvent(&CreatedFileObject->Event, NotificationEvent, FALSE);
    KeInitializeSpinLock(&CreatedFileObject->IrpListLock);
    InitializeListHead(&CreatedFileObject->IrpList);

    if (CreatedFileObject->Vpb != NULL)
    {
        InterlockedIncrement((PLONG)&CreatedFileObject->Vpb->ReferenceCount);
    }

    if (!Lite)
    {
        Status = ObInsertObject(CreatedFileObject,
                                NULL,
                                FILE_READ_DATA,
                                1,
                                (PVOID *)&CreatedFileObject,
                                (FileObjectHandle != NULL) ? &Handle : NULL);
        if (!NT_SUCCESS(Status)) return NULL;

        if (FileObjectHandle != NULL) *FileObjectHandle = Handle;
    }

    IopReferenceDeviceObject(Target);

    CreatedFileObject->Flags |= FO_HANDLE_CREATED;

    return CreatedFileObject;
}

PFILE_OBJECT
NTAPI
IoCreateStreamFileObject(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    return IopCreateStreamFileObject(FileObject, DeviceObject, FALSE, NULL);
}

PFILE_OBJECT
NTAPI
IoCreateStreamFileObjectEx(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _Out_opt_ PHANDLE FileObjectHandle)
{
    return IopCreateStreamFileObject(FileObject,
                                     DeviceObject,
                                     FALSE,
                                     FileObjectHandle);
}

PFILE_OBJECT
NTAPI
IoCreateStreamFileObjectLite(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    return IopCreateStreamFileObject(FileObject, DeviceObject, TRUE, NULL);
}

/* FILE OBJECT ATTRIBUTES *****************************************************/

NTSTATUS
NTAPI
IoSetFileOrigin(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Remote)
{
    BOOLEAN AlreadyRemote = (FileObject->Flags & FO_REMOTE_ORIGIN) != 0;

    /* Asking for the state it is already in is the caller losing track of it */
    if ((Remote != FALSE) == (AlreadyRemote != FALSE))
    {
        return STATUS_INVALID_PARAMETER_MIX;
    }

    if (Remote)
    {
        FileObject->Flags |= FO_REMOTE_ORIGIN;
    }
    else
    {
        FileObject->Flags &= ~FO_REMOTE_ORIGIN;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
IoIsFileOriginRemote(
    _In_ PFILE_OBJECT FileObject)
{
    return (FileObject->Flags & FO_REMOTE_ORIGIN) != 0;
}

PGENERIC_MAPPING
NTAPI
IoGetFileObjectGenericMapping(VOID)
{
    return &IopFileMapping;
}

BOOLEAN
NTAPI
IoIsOperationSynchronous(
    _In_ PIRP Irp)
{
    PFILE_OBJECT FileObject;

    if (Irp->Flags & IRP_PAGING_IO)
    {
        return (Irp->Flags & IRP_SYNCHRONOUS_PAGING_IO) != 0;
    }

    if (Irp->Flags & IRP_SYNCHRONOUS_API) return TRUE;

    FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    if (FileObject == NULL) return FALSE;

    return (FileObject->Flags & FO_SYNCHRONOUS_IO) != 0;
}

NTSTATUS
NTAPI
IoCheckDesiredAccess(
    _Inout_ PACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK GrantedAccess)
{
    PAGED_CODE();

    RtlMapGenericMask(DesiredAccess, &IopFileMapping);

    if ((*DesiredAccess & ~GrantedAccess) != 0) return STATUS_ACCESS_DENIED;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoComputeDesiredAccessFileObject(
    IN PFILE_OBJECT FileObject,
    IN PACCESS_MASK DesiredAccess)
{
    *DesiredAccess = 0;

    if (FileObject->ReadAccess) *DesiredAccess |= FILE_READ_DATA;
    if (FileObject->WriteAccess) *DesiredAccess |= FILE_WRITE_DATA;
    if (FileObject->DeleteAccess) *DesiredAccess |= DELETE;

    if (*DesiredAccess == 0) return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoReplaceFileObjectName(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(FileNameLength) PWSTR NewFileName,
    _In_ USHORT FileNameLength)
{
    UNICODE_STRING NewName;
    PWSTR Buffer;

    PAGED_CODE();

    if (FileNameLength <= FileObject->FileName.MaximumLength)
    {
        RtlCopyMemory(FileObject->FileName.Buffer, NewFileName, FileNameLength);
        FileObject->FileName.Length = FileNameLength;
        return STATUS_SUCCESS;
    }

    Buffer = ExAllocatePoolUninitialized(PagedPool,
                                         FileNameLength,
                                         TAG_IO_NAME);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, NewFileName, FileNameLength);

    NewName.Buffer = Buffer;
    NewName.Length = FileNameLength;
    NewName.MaximumLength = FileNameLength;

    if (FileObject->FileName.Buffer != NULL)
    {
        ExFreePool(FileObject->FileName.Buffer);
    }

    FileObject->FileName = NewName;

    return STATUS_SUCCESS;
}

PVOID
NTAPI
IoGetFileObjectFilterContext(
    IN PFILE_OBJECT FileObject)
{
    PFILE_OBJECT_EXTENSION Extension;

    if (!(FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION)) return NULL;

    Extension = FileObject->FileObjectExtension;
    if (Extension == NULL) return NULL;

    return Extension->FilterContext;
}

NTSTATUS
NTAPI
IoChangeFileObjectFilterContext(
    IN PFILE_OBJECT FileObject,
    IN PVOID FilterContext,
    IN BOOLEAN Define)
{
    PFILE_OBJECT_EXTENSION Extension;

    if (!(FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Extension = FileObject->FileObjectExtension;
    if (Extension == NULL) return STATUS_INVALID_PARAMETER;

    if (Define)
    {
        if (InterlockedCompareExchangePointer(&Extension->FilterContext,
                                              FilterContext,
                                              NULL) != NULL)
        {
            return STATUS_ALREADY_COMMITTED;
        }

        return STATUS_SUCCESS;
    }

    if (InterlockedCompareExchangePointer(&Extension->FilterContext,
                                          NULL,
                                          FilterContext) != FilterContext)
    {
        return STATUS_NOT_FOUND;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopAcquireFileObjectLock(
    _In_ PFILE_OBJECT FileObject,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN Alertable,
    _Out_ PBOOLEAN LockFailed)
{
    NTSTATUS Status;

    PAGED_CODE();

    ObReferenceObject(FileObject);
    InterlockedIncrement((PLONG)&FileObject->Waiters);

    while (TRUE)
    {
        if (InterlockedExchange((PLONG)&FileObject->Busy, TRUE) == FALSE)
        {
            InterlockedDecrement((PLONG)&FileObject->Waiters);
            *LockFailed = FALSE;
            return STATUS_SUCCESS;
        }

        Status = KeWaitForSingleObject(&FileObject->Lock,
                                       Executive,
                                       AccessMode,
                                       Alertable,
                                       NULL);
        if (Status == STATUS_SUCCESS) continue;

        InterlockedDecrement((PLONG)&FileObject->Waiters);
        ObDereferenceObject(FileObject);

        *LockFailed = TRUE;
        return Status;
    }
}

BOOLEAN
NTAPI
IoFastQueryNetworkAttributes(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG OpenOptions,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION Buffer)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopQueryAttributesFile(ObjectAttributes,
                                    FileNetworkOpenInformation,
                                    sizeof(FILE_NETWORK_OPEN_INFORMATION),
                                    Buffer,
                                    KernelMode);

    IoStatus->Status = Status;
    IoStatus->Information = NT_SUCCESS(Status)
                                ? sizeof(FILE_NETWORK_OPEN_INFORMATION)
                                : 0;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(OpenOptions);

    return NT_SUCCESS(Status);
}

BOOLEAN
NTAPI
IoIsValidNameGraftingBuffer(
    _In_ PIRP Irp,
    _In_ PREPARSE_DATA_BUFFER ReparseBuffer)
{
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    USHORT PathBufferLength;

    UNREFERENCED_PARAMETER(Irp);

    if (ReparseBuffer == NULL) return FALSE;
    if (ReparseBuffer->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT &&
        ReparseBuffer->ReparseTag != IO_REPARSE_TAG_SYMLINK)
    {
        return FALSE;
    }

    if (ReparseBuffer->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
    {
        SubstituteNameOffset =
            ReparseBuffer->MountPointReparseBuffer.SubstituteNameOffset;
        SubstituteNameLength =
            ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength;
        PrintNameOffset = ReparseBuffer->MountPointReparseBuffer.PrintNameOffset;
        PrintNameLength = ReparseBuffer->MountPointReparseBuffer.PrintNameLength;
        PathBufferLength = (USHORT)(ReparseBuffer->ReparseDataLength -
                                    REPARSE_DATA_BUFFER_HEADER_SIZE);
    }
    else
    {
        SubstituteNameOffset =
            ReparseBuffer->SymbolicLinkReparseBuffer.SubstituteNameOffset;
        SubstituteNameLength =
            ReparseBuffer->SymbolicLinkReparseBuffer.SubstituteNameLength;
        PrintNameOffset =
            ReparseBuffer->SymbolicLinkReparseBuffer.PrintNameOffset;
        PrintNameLength =
            ReparseBuffer->SymbolicLinkReparseBuffer.PrintNameLength;
        PathBufferLength = (USHORT)(ReparseBuffer->ReparseDataLength -
                                    REPARSE_DATA_BUFFER_HEADER_SIZE);
    }

    if ((SubstituteNameLength % sizeof(WCHAR)) != 0) return FALSE;
    if ((PrintNameLength % sizeof(WCHAR)) != 0) return FALSE;
    if (SubstituteNameOffset > PathBufferLength) return FALSE;
    if (SubstituteNameLength > PathBufferLength - SubstituteNameOffset)
    {
        return FALSE;
    }
    if (PrintNameOffset > PathBufferLength) return FALSE;
    if (PrintNameLength > PathBufferLength - PrintNameOffset) return FALSE;

    return TRUE;
}
