/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Byte-range locks, device control, Plug and Play and requests the volume does not serve
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

NTSTATUS
NTAPI
Ext4DispatchLockControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXT4_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    NTSTATUS Status;

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL || Fcb->Directory || Fcb->Volume)
        return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Fcb->MainResource, TRUE);
    Status = FsRtlProcessFileLock(&Fcb->FileLock, Irp, NULL);
    Fcb->Header.IsFastIoPossible = FsRtlAreThereCurrentFileLocks(&Fcb->FileLock) ? FastIoIsQuestionable : FastIoIsPossible;
    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();

    return Status;
}

NTSTATUS
NTAPI
Ext4DispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXT4_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Fcb->Vcb->TargetDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
Ext4PnpDone(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
Ext4ForwardAndWait(
    _In_ PEXT4_VCB Vcb,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, Ext4PnpDone, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(Vcb->TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

NTSTATUS
NTAPI
Ext4DispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXT4_VCB Vcb = Ext4DeviceVcb(DeviceObject);
    PEXT4_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Vcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Fcb != NULL) Vcb = Fcb->Vcb;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_REMOVE_DEVICE:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
            Status = (Vcb->OpenHandles != 0 || Vcb->Locked) ? STATUS_ACCESS_DENIED : STATUS_SUCCESS;
            if (NT_SUCCESS(Status))
            {
                Vcb->Locked = TRUE;
                Vcb->Vpb->Flags |= VPB_LOCKED;
            }
            ExReleaseResourceLite(&Vcb->Resource);
            KeLeaveCriticalRegion();
            if (!NT_SUCCESS(Status)) return Ext4CompleteRequest(Irp, Status, 0);

            Status = Ext4ForwardAndWait(Vcb, Irp);
            if (!NT_SUCCESS(Status))
            {
                KeEnterCriticalRegion();
                ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
                Vcb->Locked = FALSE;
                Vcb->Vpb->Flags &= ~VPB_LOCKED;
                ExReleaseResourceLite(&Vcb->Resource);
                KeLeaveCriticalRegion();
            }
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
            if (Vcb->Locked && Vcb->LockFileObject == NULL)
            {
                Vcb->Locked = FALSE;
                Vcb->Vpb->Flags &= ~VPB_LOCKED;
            }
            ExReleaseResourceLite(&Vcb->Resource);
            KeLeaveCriticalRegion();
            break;

        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = Ext4ForwardAndWait(Vcb, Irp);
            Ext4DismountVolume(Vcb);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Vcb->TargetDeviceObject, Irp);
}

/* TODO: Linux owner and mode are not translated into an NT security descriptor; every file answers with one that
 * lets everyone in, and a descriptor set on a file is accepted and dropped */
NTSTATUS
NTAPI
Ext4DispatchQuerySecurity(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXT4_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;
    SECURITY_INFORMATION Wanted = Stack->Parameters.QuerySecurity.SecurityInformation;
    ULONG Length = Stack->Parameters.QuerySecurity.Length;
    UCHAR AclBuffer[sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE];
    SECURITY_DESCRIPTOR Descriptor;
    PSECURITY_DESCRIPTOR Pointer = &Descriptor;
    PACL Acl = (PACL)AclBuffer;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    Status = RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status)) Status = RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    if (NT_SUCCESS(Status)) Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, SeExports->SeWorldSid);
    if (NT_SUCCESS(Status)) Status = RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE);
    if (NT_SUCCESS(Status)) Status = RtlSetOwnerSecurityDescriptor(&Descriptor, SeExports->SeAliasAdminsSid, FALSE);
    if (NT_SUCCESS(Status)) Status = RtlSetGroupSecurityDescriptor(&Descriptor, SeExports->SeAliasAdminsSid, FALSE);
    if (!NT_SUCCESS(Status)) return Ext4CompleteRequest(Irp, Status, 0);

    _SEH2_TRY
    {
        Status = SeQuerySecurityDescriptorInfo(&Wanted, Irp->UserBuffer, &Length, &Pointer);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Status == STATUS_BUFFER_TOO_SMALL) Status = STATUS_BUFFER_OVERFLOW;
    return Ext4CompleteRequest(Irp, Status, NT_ERROR(Status) ? 0 : Length);
}

NTSTATUS
NTAPI
Ext4DispatchSetSecurity(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PEXT4_FCB Fcb = Stack->FileObject ? Stack->FileObject->FsContext : NULL;

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (!Fcb->Vcb->Writable) return Ext4CompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);
    return Ext4CompleteRequest(Irp, STATUS_SUCCESS, 0);
}
