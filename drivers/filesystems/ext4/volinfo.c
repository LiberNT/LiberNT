/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Volume information queries and the volume label
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

/* Windows SDK winnt.h values */
#ifndef FILE_SUPPORTS_POSIX_UNLINK_RENAME
#define FILE_SUPPORTS_POSIX_UNLINK_RENAME 0x00000400
#endif
#ifndef FILE_SUPPORTS_OPEN_BY_FILE_ID
#define FILE_SUPPORTS_OPEN_BY_FILE_ID 0x01000000
#endif

static
PCWSTR
Ext4FileSystemName(
    _In_ PEXT4_VCB Vcb)
{
    struct ext4_sblock *Super = &Vcb->Core.Fs.sb;

    if (ext4_sb_feature_incom(Super, EXT4_FINCOM_EXTENTS)) return L"ext4";
    if (ext4_sb_feature_com(Super, EXT4_FCOM_HAS_JOURNAL)) return L"ext3";
    return L"ext2";
}

static
NTSTATUS
Ext4QueryVolumeWorker(
    _In_ PEXT4_VCB Vcb,
    _In_ FS_INFORMATION_CLASS Class,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG_PTR Information)
{
    ULONG SectorsPerUnit = max(Vcb->BlockSize / Vcb->DeviceSectorSize, 1);
    E4_STATFS Stat;

    *Information = 0;

    Ext4LockCore(Vcb);
    E4StatFs(&Vcb->Core, &Stat);
    Ext4UnlockCore(Vcb);

    switch (Class)
    {
        case FileFsVolumeInformation:
        {
            PFILE_FS_VOLUME_INFORMATION Info = Buffer;
            ULONG Fixed = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
            E4_TIME Created;
            ULONG Copied;

            if (Length < Fixed) return STATUS_INFO_LENGTH_MISMATCH;
            RtlZeroMemory(Info, Fixed);
            Created.Seconds = ext4_get32(&Vcb->Core.Fs.sb, mkfs_time);
            Created.Nanoseconds = 0;
            Info->VolumeCreationTime = Ext4TimeToNt(&Created);
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
            Info->TotalAllocationUnits.QuadPart = (LONGLONG)Stat.Blocks;
            Info->AvailableAllocationUnits.QuadPart = (LONGLONG)Stat.AvailableBlocks;
            Info->SectorsPerAllocationUnit = SectorsPerUnit;
            Info->BytesPerSector = Vcb->DeviceSectorSize;
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case FileFsFullSizeInformation:
        {
            PFILE_FS_FULL_SIZE_INFORMATION Info = Buffer;

            if (Length < sizeof(*Info)) return STATUS_INFO_LENGTH_MISMATCH;
            Info->TotalAllocationUnits.QuadPart = (LONGLONG)Stat.Blocks;
            Info->CallerAvailableAllocationUnits.QuadPart = (LONGLONG)Stat.AvailableBlocks;
            Info->ActualAvailableAllocationUnits.QuadPart = (LONGLONG)Stat.FreeBlocks;
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
            PCWSTR FsName = Ext4FileSystemName(Vcb);
            ULONG Fixed = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);
            ULONG Name = (ULONG)wcslen(FsName) * sizeof(WCHAR);
            ULONG Copied;

            if (Length < Fixed) return STATUS_INFO_LENGTH_MISMATCH;
            Info->FileSystemAttributes = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK |
                                         FILE_SUPPORTS_REPARSE_POINTS | FILE_SUPPORTS_HARD_LINKS | FILE_SUPPORTS_SPARSE_FILES |
                                         FILE_SUPPORTS_EXTENDED_ATTRIBUTES | FILE_SUPPORTS_OPEN_BY_FILE_ID |
                                         FILE_SUPPORTS_POSIX_UNLINK_RENAME | (Vcb->Writable ? 0 : FILE_READ_ONLY_VOLUME);
            Info->MaximumComponentNameLength = EXT4_NAME_BYTES;
            Info->FileSystemNameLength = Name;
            Copied = min(Length - Fixed, Name) & ~(sizeof(WCHAR) - 1);
            RtlCopyMemory(Info->FileSystemName, FsName, Copied);
            *Information = Fixed + Copied;
            return (Copied < Name) ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
        }

        case FileFsSectorSizeInformation:
        {
            PFILE_FS_SECTOR_SIZE_INFORMATION Info = Buffer;

            if (Length < sizeof(*Info)) return STATUS_INFO_LENGTH_MISMATCH;
            RtlZeroMemory(Info, sizeof(*Info));
            Info->LogicalBytesPerSector = Vcb->DeviceSectorSize;
            Info->PhysicalBytesPerSectorForAtomicity = Vcb->DeviceSectorSize;
            Info->PhysicalBytesPerSectorForPerformance = Vcb->DeviceSectorSize;
            Info->FileSystemEffectivePhysicalBytesPerSectorForAtomicity = Vcb->DeviceSectorSize;
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
NTAPI
Ext4DispatchQueryVolumeInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Fcb->Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    Status = Ext4QueryVolumeWorker(Fcb->Vcb, Stack->Parameters.QueryVolume.FsInformationClass,
                                   Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.QueryVolume.Length, &Information);
    return Ext4CompleteRequest(Irp, Status, Information);
}

static
NTSTATUS
Ext4SetLabel(
    _In_ PEXT4_VCB Vcb,
    _In_reads_bytes_(Length) const FILE_FS_LABEL_INFORMATION *Info,
    _In_ ULONG Length)
{
    CHAR Utf8[16] = { 0 };
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
        Status = Ext4UnicodeToUtf8(&Label, Utf8, sizeof(Utf8), &Utf8Length);
        if (!NT_SUCCESS(Status)) return STATUS_INVALID_VOLUME_LABEL;
    }

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4SetLabel(&Vcb->Core, Utf8, Utf8Length));
    Ext4UnlockCore(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    Vcb->Vpb->VolumeLabelLength = Label.Length;
    RtlCopyMemory(Vcb->Vpb->VolumeLabel, Label.Buffer, Label.Length);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Ext4DispatchSetVolumeInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXT4_FCB Fcb = FileObject ? FileObject->FsContext : NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (Ext4IsFsDevice(DeviceObject) || Fcb == NULL) return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Fcb->Vcb->Dismounted) return Ext4CompleteRequest(Irp, STATUS_VOLUME_DISMOUNTED, 0);

    switch (Stack->Parameters.SetVolume.FsInformationClass)
    {
        case FileFsLabelInformation:
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(&Fcb->Vcb->Resource, TRUE);
            Status = Ext4SetLabel(Fcb->Vcb, Irp->AssociatedIrp.SystemBuffer, Stack->Parameters.SetVolume.Length);
            ExReleaseResourceLite(&Fcb->Vcb->Resource);
            KeLeaveCriticalRegion();
            break;

        default:
            Status = Fcb->Vcb->Writable ? STATUS_INVALID_PARAMETER : STATUS_MEDIA_WRITE_PROTECTED;
            break;
    }

    return Ext4CompleteRequest(Irp, Status, 0);
}
