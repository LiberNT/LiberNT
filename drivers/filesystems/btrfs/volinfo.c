/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Volume information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

#define BTRFS_FS_NAME L"Btrfs"

static
NTSTATUS
BtrfsQueryVolumeWorker(
    _In_ PBTRFS_VCB Vcb,
    _In_ FS_INFORMATION_CLASS Class,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    ULONGLONG Units = Vcb->Super.TotalBytes / Vcb->SectorSize;
    LONGLONG Used = (LONGLONG)Vcb->Super.BytesUsed + Vcb->BytesUsedDelta + (LONGLONG)Vcb->DelallocBytes;
    ULONGLONG Free = (Used >= 0 && Vcb->Super.TotalBytes > (ULONGLONG)Used) ? (Vcb->Super.TotalBytes - (ULONGLONG)Used) / Vcb->SectorSize : 0;
    ULONG SectorsPerUnit = max(Vcb->SectorSize / Vcb->DeviceSectorSize, 1);

    *Information = 0;

    switch (Class)
    {
        case FileFsVolumeInformation:
        {
            PFILE_FS_VOLUME_INFORMATION Info = Buffer;
            ULONG Fixed = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
            ULONG Copied;
            BTRFS_INODE_ITEM Root;

            if (Length < Fixed) return STATUS_INFO_LENGTH_MISMATCH;
            RtlZeroMemory(Info, Fixed);
            if (NT_SUCCESS(BtrfsLoadInode(Vcb, &Vcb->DefaultRoot, Vcb->DefaultRoot.RootDirId, &Root)))
                Info->VolumeCreationTime = BtrfsTime(Root.CreateTime.Seconds ? &Root.CreateTime : &Root.ChangeTime);
            Info->VolumeSerialNumber = Vcb->Vpb->SerialNumber;
            Info->VolumeLabelLength = Vcb->Vpb->VolumeLabelLength;
            Info->SupportsObjects = FALSE;
            Copied = min(Length - Fixed, (ULONG)Vcb->Vpb->VolumeLabelLength) & ~(sizeof(WCHAR) - 1);
            RtlCopyMemory(Info->VolumeLabel, Vcb->Vpb->VolumeLabel, Copied);
            *Information = Fixed + Copied;
            return (Copied < Vcb->Vpb->VolumeLabelLength) ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
        }

        case FileFsSizeInformation:
        {
            PFILE_FS_SIZE_INFORMATION Info = Buffer;

            if (Length < sizeof(*Info)) return STATUS_INFO_LENGTH_MISMATCH;
            Info->TotalAllocationUnits.QuadPart = (LONGLONG)Units;
            Info->AvailableAllocationUnits.QuadPart = (LONGLONG)Free;
            Info->SectorsPerAllocationUnit = SectorsPerUnit;
            Info->BytesPerSector = Vcb->DeviceSectorSize;
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileFsFullSizeInformation:
        {
            PFILE_FS_FULL_SIZE_INFORMATION Info = Buffer;

            if (Length < sizeof(*Info)) return STATUS_INFO_LENGTH_MISMATCH;
            Info->TotalAllocationUnits.QuadPart = (LONGLONG)Units;
            Info->CallerAvailableAllocationUnits.QuadPart = (LONGLONG)Free;
            Info->ActualAvailableAllocationUnits.QuadPart = (LONGLONG)Free;
            Info->SectorsPerAllocationUnit = SectorsPerUnit;
            Info->BytesPerSector = Vcb->DeviceSectorSize;
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileFsDeviceInformation:
        {
            PFILE_FS_DEVICE_INFORMATION Info = Buffer;

            if (Length < sizeof(*Info)) return STATUS_INFO_LENGTH_MISMATCH;
            Info->DeviceType = FILE_DEVICE_DISK;
            Info->Characteristics = Vcb->TargetDeviceObject->Characteristics | (Vcb->Writable ? 0 : FILE_READ_ONLY_DEVICE);
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileFsAttributeInformation:
        {
            PFILE_FS_ATTRIBUTE_INFORMATION Info = Buffer;
            ULONG Fixed = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);
            ULONG Name = sizeof(BTRFS_FS_NAME) - sizeof(WCHAR);
            ULONG Copied;

            if (Length < Fixed) return STATUS_INFO_LENGTH_MISMATCH;
            Info->FileSystemAttributes = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK |
                                         FILE_SUPPORTS_REPARSE_POINTS | FILE_SUPPORTS_HARD_LINKS |
                                         (Vcb->Writable ? 0 : FILE_READ_ONLY_VOLUME);
            Info->MaximumComponentNameLength = BTRFS_NAME_LENGTH;
            Info->FileSystemNameLength = Name;
            Copied = min(Length - Fixed, Name) & ~(sizeof(WCHAR) - 1);
            RtlCopyMemory(Info->FileSystemName, BTRFS_FS_NAME, Copied);
            *Information = Fixed + Copied;
            return (Copied < Name) ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
        }

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
NTAPI
BtrfsDispatchQueryVolumeInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Fcb->Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    Status = BtrfsQueryVolumeWorker(Fcb->Vcb, Stack->Parameters.QueryVolume.FsInformationClass,
                                    Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.QueryVolume.Length, &Information);
    return BtrfsCompleteRequest(Irp, Status, Information);
}

static
NTSTATUS
BtrfsSetLabel(
    _In_ PBTRFS_VCB Vcb,
    _In_reads_bytes_(Length) const FILE_FS_LABEL_INFORMATION *Info,
    _In_ ULONG Length)
{
    CHAR Utf8[BTRFS_LABEL_SIZE];
    UNICODE_STRING Label;
    ULONG Utf8Length = 0;
    NTSTATUS Status;

    if (Length < FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel) ||
        Info->VolumeLabelLength > Length - FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel) ||
        Info->VolumeLabelLength > MAXIMUM_VOLUME_LABEL_LENGTH || (Info->VolumeLabelLength % sizeof(WCHAR)) != 0)
    {
        return STATUS_INVALID_VOLUME_LABEL;
    }
    if (!Vcb->Writable) return STATUS_MEDIA_WRITE_PROTECTED;

    Label.Buffer = (PWCHAR)Info->VolumeLabel;
    Label.Length = Label.MaximumLength = (USHORT)Info->VolumeLabelLength;
    if (Label.Length != 0)
    {
        Status = BtrfsUnicodeToUtf8(&Label, Utf8, sizeof(Utf8) - 1, &Utf8Length);
        if (!NT_SUCCESS(Status)) return STATUS_INVALID_VOLUME_LABEL;
    }

    BtrfsAcquireTreeExclusive(Vcb);
    Status = BtrfsStartTransaction(Vcb);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(Vcb->Super.Label, BTRFS_LABEL_SIZE);
        RtlCopyMemory(Vcb->Super.Label, Utf8, Utf8Length);
        Status = BtrfsCommit(Vcb);
    }
    BtrfsReleaseTree(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    Vcb->Vpb->VolumeLabelLength = Label.Length;
    RtlCopyMemory(Vcb->Vpb->VolumeLabel, Label.Buffer, Label.Length);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
BtrfsDispatchSetVolumeInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PBTRFS_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (BtrfsIsFsDevice(DeviceObject) || Fcb == NULL) return BtrfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Fcb->Vcb->Dismounted) return BtrfsCompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    switch (Stack->Parameters.SetVolume.FsInformationClass)
    {
        case FileFsLabelInformation:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Fcb->Vcb->Resource, TRUE);
            Status = BtrfsSetLabel(Fcb->Vcb, Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.SetVolume.Length);
            ExReleaseResourceLite(&Fcb->Vcb->Resource);
            KeLeaveCriticalRegion();
            break;

        default:
            Status = Fcb->Vcb->Writable ? STATUS_INVALID_PARAMETER : STATUS_MEDIA_WRITE_PROTECTED;
            break;
    }

    return BtrfsCompleteRequest(Irp, Status, 0);
}
