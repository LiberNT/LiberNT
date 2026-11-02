/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTFS security descriptor query support
 */

#include "ntfspch.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdQuerySecurity)
#pragma alloc_text(PAGE, NtfsFsdSetSecurity)
#endif

static
NTSTATUS
NtfsBuildDefaultSecurityDescriptor(
    _Outptr_ PSECURITY_DESCRIPTOR *Descriptor)
{
    SECURITY_DESCRIPTOR Absolute;
    ULONG AclBuffer[(sizeof(ACL) + 2 * (FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + SECURITY_MAX_SID_SIZE)) / sizeof(ULONG)];
    PACL Dacl = (PACL)AclBuffer;
    ULONG Length;
    NTSTATUS Status;

    Length = sizeof(ACL) +
             FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + RtlLengthSid(SeExports->SeLocalSystemSid) +
             FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + RtlLengthSid(SeExports->SeAliasAdminsSid);
    Status = RtlCreateAcl(Dacl, Length, ACL_REVISION);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, FILE_ALL_ACCESS, SeExports->SeLocalSystemSid);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, FILE_ALL_ACCESS, SeExports->SeAliasAdminsSid);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlCreateSecurityDescriptor(&Absolute, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlSetOwnerSecurityDescriptor(&Absolute, SeExports->SeAliasAdminsSid, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlSetGroupSecurityDescriptor(&Absolute, SeExports->SeLocalSystemSid, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlSetDaclSecurityDescriptor(&Absolute, TRUE, Dacl, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Length = 0;
    Status = RtlAbsoluteToSelfRelativeSD(&Absolute, NULL, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return NT_SUCCESS(Status) ? STATUS_INTERNAL_ERROR : Status;

    *Descriptor = ExAllocatePoolWithTag(PagedPool, Length, TAG_NTFS);
    if (!*Descriptor)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = RtlAbsoluteToSelfRelativeSD(&Absolute, *Descriptor, &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(*Descriptor, TAG_NTFS);
        *Descriptor = NULL;
    }
    return Status;
}

NTSTATUS
NtfsReadRecordSecurityDescriptor(
    _In_ PNtfsFileRecord File,
    _Outptr_ PSECURITY_DESCRIPTOR *Descriptor)
{
    ULONG Length = 0;
    NTSTATUS Status;

    *Descriptor = NULL;
    Status = NtfsFileRecordReadSecurityDescriptor(File, NULL, &Length);
    /* Windows 11 applies O:BAG:SYD:(A;;FA;;;SY)(A;;FA;;;BA) to a record whose descriptor cannot be found. */
    if (Status == STATUS_NOT_FOUND)
        return NtfsBuildDefaultSecurityDescriptor(Descriptor);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;

    *Descriptor = ExAllocatePoolWithTag(PagedPool, Length, TAG_NTFS);
    if (!*Descriptor)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = NtfsFileRecordReadSecurityDescriptor(File, (PUCHAR)*Descriptor, &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(*Descriptor, TAG_NTFS);
        *Descriptor = NULL;
    }
    return Status;
}

NTSTATUS
NtfsCheckRecordAccess(
    _In_ PNtfsFileRecord File,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK PreviouslyGrantedAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PPRIVILEGE_SET* Privileges)
{
    PSECURITY_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    *Privileges = NULL;
    if (!DesiredAccess)
    {
        *GrantedAccess = PreviouslyGrantedAccess;
        return STATUS_SUCCESS;
    }
    if (AccessMode == KernelMode)
    {
        *GrantedAccess = DesiredAccess | PreviouslyGrantedAccess;
        if (DesiredAccess & MAXIMUM_ALLOWED)
            *GrantedAccess = IoGetFileObjectGenericMapping()->GenericAll | (*GrantedAccess & ~MAXIMUM_ALLOWED);
        return STATUS_SUCCESS;
    }

    Status = NtfsReadRecordSecurityDescriptor(File, &Descriptor);
    if (!NT_SUCCESS(Status))
        return Status;
    SeAccessCheck(Descriptor,
                  SubjectContext,
                  FALSE,
                  DesiredAccess,
                  PreviouslyGrantedAccess,
                  Privileges,
                  IoGetFileObjectGenericMapping(),
                  AccessMode,
                  GrantedAccess,
                  &Status);
    ExFreePoolWithTag(Descriptor, TAG_NTFS);
    return Status;
}

static NTSTATUS
NtfsQuerySecurityDescriptor(
    _In_ PFileContextBlock FileCB,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_writes_bytes_to_opt_(OutputLength, *ResultLength) PVOID Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG ResultLength)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    PSECURITY_DESCRIPTOR RawDescriptor;
    ULONG QueryLength;
    NTSTATUS Status;

    *ResultLength = 0;

    Status = NtfsReadRecordSecurityDescriptor(FileCB->FileRec, &RawDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;

    SecurityDescriptor = RawDescriptor;
    QueryLength = OutputLength;
    Status = SeQuerySecurityDescriptorInfo(&SecurityInformation,
                                           (PSECURITY_DESCRIPTOR)Output,
                                           &QueryLength,
                                           &SecurityDescriptor);
    *ResultLength = QueryLength;
    if (Status == STATUS_BUFFER_TOO_SMALL)
        Status = STATUS_BUFFER_OVERFLOW;

    ExFreePoolWithTag(RawDescriptor, TAG_NTFS);
    return Status;
}

_Function_class_(IRP_MJ_QUERY_SECURITY)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdQuerySecurity(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                     _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    SECURITY_INFORMATION SecurityInformation;
    PVOID Output;
    ULONG OutputLength;
    ULONG ResultLength = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject ||
        !IrpSp->FileObject ||
        !IrpSp->FileObject->FsContext)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Done;
    }

    FileCB = NtfsGetFileContext(IrpSp->FileObject);
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    if (!FileCB->FileRec || !VolCB || !VolCB->DiskVolume)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    SecurityInformation =
        IrpSp->Parameters.QuerySecurity.SecurityInformation;
    if (Irp->RequestorMode == UserMode)
    {
        if ((SecurityInformation &
             (OWNER_SECURITY_INFORMATION |
              GROUP_SECURITY_INFORMATION |
              DACL_SECURITY_INFORMATION)) &&
            !(FileCB->DesiredAccess & READ_CONTROL))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }

        if ((SecurityInformation & SACL_SECURITY_INFORMATION) &&
            !(FileCB->DesiredAccess & ACCESS_SYSTEM_SECURITY))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }
    }

    OutputLength = IrpSp->Parameters.QuerySecurity.Length;
    Output = GetBuffer(Irp);
    if (OutputLength != 0 && !Output)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Done;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(NtfsGetMainResource(FileCB), TRUE);
    NtfsAcquireMetadata(VolCB);
    Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (NT_SUCCESS(Status))
        Status = NtfsQuerySecurityDescriptor(FileCB,
                                             SecurityInformation,
                                             Output,
                                             OutputLength,
                                             &ResultLength);
    NtfsReleaseMetadata(VolCB);
    ExReleaseResourceLite(NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();

Done:
    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_OVERFLOW)
        Irp->IoStatus.Information = ResultLength;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

static NTSTATUS
NtfsSetSecurityDescriptor(
    _In_ PFileContextBlock FileCB,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR ModificationDescriptor)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;
    SECURITY_DESCRIPTOR NormalizedDescriptor;
    PACL Sacl;
    BOOLEAN Present, Defaulted;
    PSECURITY_DESCRIPTOR RawDescriptor;
    NTSTATUS Status;

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
    {
        Status = RtlGetSaclSecurityDescriptor(ModificationDescriptor, &Present, &Sacl, &Defaulted);
        if (!NT_SUCCESS(Status)) return Status;
        if (Present && Sacl && !Sacl->AceCount)
        {
            Status = RtlCreateSecurityDescriptor(&NormalizedDescriptor, SECURITY_DESCRIPTOR_REVISION);
            if (!NT_SUCCESS(Status)) return Status;
            NormalizedDescriptor.Control = ((PISECURITY_DESCRIPTOR)ModificationDescriptor)->Control & ~SE_SELF_RELATIVE;
            NormalizedDescriptor.Sbz1 = ((PISECURITY_DESCRIPTOR)ModificationDescriptor)->Sbz1;
            Status = RtlGetOwnerSecurityDescriptor(ModificationDescriptor, &NormalizedDescriptor.Owner, &Defaulted);
            if (!NT_SUCCESS(Status)) return Status;
            Status = RtlGetGroupSecurityDescriptor(ModificationDescriptor, &NormalizedDescriptor.Group, &Defaulted);
            if (!NT_SUCCESS(Status)) return Status;
            Status = RtlGetDaclSecurityDescriptor(ModificationDescriptor, &Present, &NormalizedDescriptor.Dacl, &Defaulted);
            if (!NT_SUCCESS(Status)) return Status;
            ModificationDescriptor = &NormalizedDescriptor;
        }
    }

    Status = NtfsReadRecordSecurityDescriptor(FileCB->FileRec, &RawDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;

    SecurityDescriptor = RawDescriptor;
    Status = SeSetSecurityDescriptorInfo(NULL,
                                         &SecurityInformation,
                                         ModificationDescriptor,
                                         &SecurityDescriptor,
                                         PagedPool,
                                         IoGetFileObjectGenericMapping());
    if (!NT_SUCCESS(Status))
    {
        SecurityDescriptor = NULL;
        goto Done;
    }

    Status = NtfsFileRecordSetSecurityDescriptor(FileCB->FileRec,
                                                 (const UCHAR*)SecurityDescriptor,
                                                 RtlLengthSecurityDescriptor(SecurityDescriptor));

Done:
    if (SecurityDescriptor && SecurityDescriptor != RawDescriptor)
        ExFreePool(SecurityDescriptor);
    ExFreePoolWithTag(RawDescriptor, TAG_NTFS);
    return Status;
}

_Function_class_(IRP_MJ_SET_SECURITY)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdSetSecurity(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                   _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    SECURITY_INFORMATION SecurityInformation;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject ||
        !IrpSp->FileObject ||
        !IrpSp->FileObject->FsContext)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Done;
    }

    FileCB = NtfsGetFileContext(IrpSp->FileObject);
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    if (!FileCB->FileRec || !VolCB || !VolCB->DiskVolume ||
        !IrpSp->Parameters.SetSecurity.SecurityDescriptor)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    SecurityInformation = IrpSp->Parameters.SetSecurity.SecurityInformation;
    if (Irp->RequestorMode == UserMode)
    {
        if ((SecurityInformation &
             (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) &&
            !(FileCB->DesiredAccess & WRITE_OWNER))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }

        if ((SecurityInformation & DACL_SECURITY_INFORMATION) &&
            !(FileCB->DesiredAccess & WRITE_DAC))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }

        if ((SecurityInformation & SACL_SECURITY_INFORMATION) &&
            !(FileCB->DesiredAccess & ACCESS_SYSTEM_SECURITY))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }
    }

    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Done;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
    NtfsAcquireMetadata(VolCB);
    Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsSetSecurityDescriptor(FileCB,
                                           SecurityInformation,
                                           IrpSp->Parameters.SetSecurity.SecurityDescriptor);
        if (NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY)
            InterlockedIncrement(&VolCB->DirGeneration);
    }
    NtfsReleaseMetadata(VolCB);
    ExReleaseResourceLite(NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();

Done:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
