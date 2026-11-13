/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Which disk the system came off, and where its own directory sits on it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* Where the names the firmware uses for its devices are kept */
#define FSTUB_ARC_NAME_PREFIX L"\\ArcName\\"

/* What a firmware name puts in front of the partition it names */
#define FSTUB_ARC_PARTITION_TAG "partition("

#define FSTUB_SYSTEM_PATH_MAX 256

/* PRIVATE FUNCTIONS **********************************************************/

/* How long a run of characters is, up to the terminator */
static
SIZE_T
FstubAnsiLength(
    _In_ PCSTR String)
{
    SIZE_T Length = 0;

    while (String[Length] != '\0') Length++;

    return Length;
}

static
SIZE_T
FstubArcDeviceLength(
    _In_ PCSTR ArcName)
{
    SIZE_T Length = FstubAnsiLength(ArcName);
    SIZE_T TagLength = FstubAnsiLength(FSTUB_ARC_PARTITION_TAG);
    SIZE_T i;
    SIZE_T j;

    if (Length < TagLength) return Length;

    for (i = 0; i + TagLength <= Length; i++)
    {
        for (j = 0; j < TagLength; j++)
        {
            if (ArcName[i + j] != FSTUB_ARC_PARTITION_TAG[j]) break;
        }

        if (j == TagLength) return i;
    }

    return Length;
}

/* Whether two runs of characters say the same thing, ignoring case */
static
BOOLEAN
FstubAnsiEqual(
    _In_reads_(Length) PCSTR Left,
    _In_reads_(Length) PCSTR Right,
    _In_ SIZE_T Length)
{
    SIZE_T i;
    CHAR a;
    CHAR b;

    for (i = 0; i < Length; i++)
    {
        a = Left[i];
        b = Right[i];

        if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
        if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';

        if (a != b) return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
FstubFindArcSignature(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PCSTR ArcName,
    _Out_ PULONG Signature,
    _Out_ LPGUID DiskId,
    _Out_ PBOOLEAN IsGpt)
{
    PARC_DISK_SIGNATURE Entry;
    PLIST_ENTRY ListEntry;
    SIZE_T DeviceLength;
    SIZE_T EntryLength;

    *Signature = 0;
    RtlZeroMemory(DiskId, sizeof(*DiskId));
    *IsGpt = FALSE;

    if (LoaderBlock->ArcDiskInformation == NULL) return FALSE;

    DeviceLength = FstubArcDeviceLength(ArcName);

    for (ListEntry = LoaderBlock->ArcDiskInformation->DiskSignatureListHead.Flink;
         ListEntry != &LoaderBlock->ArcDiskInformation->DiskSignatureListHead;
         ListEntry = ListEntry->Flink)
    {
        Entry = CONTAINING_RECORD(ListEntry, ARC_DISK_SIGNATURE, ListEntry);

        if (Entry->ArcName == NULL) continue;

        EntryLength = FstubArcDeviceLength(Entry->ArcName);

        if (EntryLength != DeviceLength) continue;
        if (!FstubAnsiEqual(Entry->ArcName, ArcName, DeviceLength)) continue;

        *Signature = Entry->Signature;
        *IsGpt = Entry->IsGpt;

        if (Entry->IsGpt)
        {
            RtlCopyMemory(DiskId, Entry->GptSignature, sizeof(*DiskId));
        }

        return TRUE;
    }

    return FALSE;
}

/* Where on its disk the partition a firmware name points at begins */
static
NTSTATUS
FstubQueryArcPartitionOffset(
    _In_ PCSTR ArcName,
    _Out_ PLONGLONG StartingOffset)
{
    PARTITION_INFORMATION_EX Information;
    IO_STATUS_BLOCK IoStatus;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Prefix = RTL_CONSTANT_STRING(FSTUB_ARC_NAME_PREFIX);
    UNICODE_STRING Wide;
    UNICODE_STRING Name;
    ANSI_STRING Ansi;
    NTSTATUS Status;
    HANDLE Handle;
    PWSTR Buffer;
    USHORT Length;

    PAGED_CODE();

    *StartingOffset = 0;

    Ansi.Buffer = (PCHAR)ArcName;
    Ansi.Length = (USHORT)FstubAnsiLength(ArcName);
    Ansi.MaximumLength = Ansi.Length;

    Status = RtlAnsiStringToUnicodeString(&Wide, &Ansi, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Length = Prefix.Length + Wide.Length + sizeof(UNICODE_NULL);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_FSTUB);
    if (Buffer == NULL)
    {
        RtlFreeUnicodeString(&Wide);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlInitEmptyUnicodeString(&Name, Buffer, Length);
    RtlAppendUnicodeStringToString(&Name, &Prefix);
    RtlAppendUnicodeStringToString(&Name, &Wide);

    RtlFreeUnicodeString(&Wide);

    InitializeObjectAttributes(&Attributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);

    ExFreePoolWithTag(Buffer, TAG_FSTUB);

    if (!NT_SUCCESS(Status)) return Status;

    Status = ZwDeviceIoControlFile(Handle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_DISK_GET_PARTITION_INFO_EX,
                                   NULL,
                                   0,
                                   &Information,
                                   sizeof(Information));

    ZwClose(Handle);

    if (!NT_SUCCESS(Status)) return Status;

    *StartingOffset = Information.StartingOffset.QuadPart;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoGetBootDiskInformation(
    _Inout_ PBOOTDISK_INFORMATION BootDiskInformation,
    _In_ ULONG Size)
{
    PBOOTDISK_INFORMATION_EX Extended = (PBOOTDISK_INFORMATION_EX)BootDiskInformation;
    PLOADER_PARAMETER_BLOCK LoaderBlock = IopLoaderBlock;
    NTSTATUS Status;
    BOOLEAN IsGpt;
    ULONG Signature;
    GUID DiskId;

    PAGED_CODE();

    if (BootDiskInformation == NULL) return STATUS_INVALID_PARAMETER;

    if (Size != sizeof(BOOTDISK_INFORMATION) &&
        Size != sizeof(BOOTDISK_INFORMATION_EX))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (LoaderBlock == NULL) return STATUS_TOO_LATE;

    RtlZeroMemory(BootDiskInformation, Size);

    if (LoaderBlock->ArcBootDeviceName == NULL ||
        LoaderBlock->ArcHalDeviceName == NULL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    Status = FstubQueryArcPartitionOffset(
                 LoaderBlock->ArcBootDeviceName,
                 &BootDiskInformation->BootPartitionOffset);
    if (!NT_SUCCESS(Status)) return Status;

    if (FstubFindArcSignature(LoaderBlock,
                              LoaderBlock->ArcBootDeviceName,
                              &Signature,
                              &DiskId,
                              &IsGpt))
    {
        BootDiskInformation->BootDeviceSignature = Signature;

        if (Size == sizeof(BOOTDISK_INFORMATION_EX))
        {
            Extended->BootDeviceGuid = DiskId;
            Extended->BootDeviceIsGpt = IsGpt;
        }
    }

    Status = FstubQueryArcPartitionOffset(
                 LoaderBlock->ArcHalDeviceName,
                 &BootDiskInformation->SystemPartitionOffset);
    if (!NT_SUCCESS(Status)) return Status;

    if (FstubFindArcSignature(LoaderBlock,
                              LoaderBlock->ArcHalDeviceName,
                              &Signature,
                              &DiskId,
                              &IsGpt))
    {
        BootDiskInformation->SystemDeviceSignature = Signature;

        if (Size == sizeof(BOOTDISK_INFORMATION_EX))
        {
            Extended->SystemDeviceGuid = DiskId;
            Extended->SystemDeviceIsGpt = IsGpt;
        }
    }

    return STATUS_SUCCESS;
}

/* Asks the mount manager for a volume's letter, giving it the next free one if it has none */
static
UCHAR
FstubNextDriveLetter(
    _In_ PDEVICE_OBJECT MountManager,
    _In_ PCUNICODE_STRING DeviceName)
{
    PMOUNTMGR_DRIVE_LETTER_TARGET Target;
    MOUNTMGR_DRIVE_LETTER_INFORMATION Information;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    ULONG Size;
    NTSTATUS Status;

    Size = FIELD_OFFSET(MOUNTMGR_DRIVE_LETTER_TARGET, DeviceName) + DeviceName->Length;
    if (Size < sizeof(MOUNTMGR_DRIVE_LETTER_TARGET)) Size = sizeof(MOUNTMGR_DRIVE_LETTER_TARGET);

    Target = ExAllocatePoolZero(PagedPool, Size, TAG_FSTUB);
    if (Target == NULL) return 0;

    Target->DeviceNameLength = DeviceName->Length;
    RtlCopyMemory(Target->DeviceName, DeviceName->Buffer, DeviceName->Length);

    RtlZeroMemory(&Information, sizeof(Information));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER,
                                        MountManager,
                                        Target,
                                        Size,
                                        &Information,
                                        sizeof(Information),
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        ExFreePoolWithTag(Target, TAG_FSTUB);
        return 0;
    }

    Status = IoCallDriver(MountManager, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    ExFreePoolWithTag(Target, TAG_FSTUB);

    if (!NT_SUCCESS(Status)) return 0;

    return Information.CurrentDriveLetter;
}

static
VOID
FstubNextDriveLetterByName(
    _In_ PDEVICE_OBJECT MountManager,
    _In_ PCWSTR Format,
    _In_ ULONG First,
    _In_ ULONG Second)
{
    WCHAR Buffer[64];
    UNICODE_STRING Name;

    if (!NT_SUCCESS(RtlStringCbPrintfW(Buffer, sizeof(Buffer), Format, First, Second))) return;

    RtlInitUnicodeString(&Name, Buffer);
    FstubNextDriveLetter(MountManager, &Name);
}

/* Every partition a disk lays out gets a letter, unless the mount manager keeps it from one */
static
VOID
FstubAssignDiskLetters(
    _In_ PDEVICE_OBJECT MountManager,
    _In_ ULONG Disk)
{
    WCHAR Buffer[64];
    UNICODE_STRING Name;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DiskDevice;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX Partition;
    NTSTATUS Status;
    ULONG i;

    if (!NT_SUCCESS(RtlStringCbPrintfW(Buffer, sizeof(Buffer), L"\\Device\\Harddisk%lu\\Partition0", Disk))) return;

    RtlInitUnicodeString(&Name, Buffer);
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES, &FileObject, &DiskDevice);
    if (!NT_SUCCESS(Status)) return;

    Status = IoReadPartitionTableEx(DiskDevice, &Layout);
    ObDereferenceObject(FileObject);
    if (!NT_SUCCESS(Status)) return;

    for (i = 0; i < Layout->PartitionCount; i++)
    {
        Partition = &Layout->PartitionEntry[i];

        if (Partition->PartitionNumber == 0) continue;

        if (Partition->PartitionStyle == PARTITION_STYLE_MBR &&
            (Partition->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
             IsContainerPartition(Partition->Mbr.PartitionType)))
        {
            continue;
        }

        FstubNextDriveLetterByName(MountManager,
                                   L"\\Device\\Harddisk%lu\\Partition%lu",
                                   Disk,
                                   Partition->PartitionNumber);
    }

    ExFreePool(Layout);
}

static
VOID
FstubAutomaticDriveLetters(
    _In_ PDEVICE_OBJECT MountManager)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_MOUNTMGR_AUTO_DL_ASSIGNMENTS,
                                        MountManager,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL) return;

    if (IoCallDriver(MountManager, Irp) == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
}

/*
 * Gives the volumes there at boot their letters, the one the system starts
 * from first so it gets the first letter it can have, and hands back the
 * system directory as a path on that letter.
 */
VOID
FASTCALL
xHalIoAssignDriveLetters(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PSTRING NtDeviceName,
    _Out_ PUCHAR NtSystemPath,
    _Out_ PSTRING NtSystemPathString)
{
    UNICODE_STRING MountManagerName = RTL_CONSTANT_STRING(MOUNTMGR_DEVICE_NAME);
    UNICODE_STRING BootDevice;
    PCONFIGURATION_INFORMATION Configuration;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT MountManager;
    PCSTR BootPath;
    SIZE_T PathLength;
    UCHAR BootLetter = 0;
    ULONG i;

    PAGED_CODE();

    NtSystemPath[0] = '\0';
    NtSystemPathString->Buffer = (PCHAR)NtSystemPath;
    NtSystemPathString->Length = 0;
    NtSystemPathString->MaximumLength = FSTUB_SYSTEM_PATH_MAX;

    if (!NT_SUCCESS(IoGetDeviceObjectPointer(&MountManagerName,
                                             FILE_READ_ATTRIBUTES,
                                             &FileObject,
                                             &MountManager)))
    {
        return;
    }

    if (NtDeviceName != NULL && NtDeviceName->Buffer != NULL &&
        NT_SUCCESS(RtlAnsiStringToUnicodeString(&BootDevice, NtDeviceName, TRUE)))
    {
        BootLetter = FstubNextDriveLetter(MountManager, &BootDevice);
        RtlFreeUnicodeString(&BootDevice);
    }

    Configuration = IoGetConfigurationInformation();

    for (i = 0; i < Configuration->DiskCount; i++)
    {
        FstubAssignDiskLetters(MountManager, i);
    }

    for (i = 0; i < Configuration->CdRomCount; i++)
    {
        FstubNextDriveLetterByName(MountManager, L"\\Device\\CdRom%lu", i, 0);
    }

    for (i = 0; i < Configuration->FloppyCount; i++)
    {
        FstubNextDriveLetterByName(MountManager, L"\\Device\\Floppy%lu", i, 0);
    }

    FstubAutomaticDriveLetters(MountManager);

    ObDereferenceObject(FileObject);

    if (BootLetter == 0 || LoaderBlock == NULL || LoaderBlock->NtBootPathName == NULL) return;

    BootPath = LoaderBlock->NtBootPathName;
    PathLength = FstubAnsiLength(BootPath);

    /* The loader hands the path over with a separator on the end */
    while (PathLength != 0 && BootPath[PathLength - 1] == '\\') PathLength--;

    if (PathLength > FSTUB_SYSTEM_PATH_MAX - 3) PathLength = FSTUB_SYSTEM_PATH_MAX - 3;

    NtSystemPath[0] = BootLetter;
    NtSystemPath[1] = ':';
    RtlCopyMemory(&NtSystemPath[2], BootPath, PathLength);
    NtSystemPath[2 + PathLength] = '\0';

    NtSystemPathString->Length = (USHORT)(2 + PathLength);
}

/* The entry point the I/O manager calls once the disks are there */
VOID
FASTCALL
IoAssignDriveLetters(
    _In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock,
    _In_ PSTRING NtDeviceName,
    _Out_ PUCHAR NtSystemPath,
    _Out_ PSTRING NtSystemPathString)
{
    xHalIoAssignDriveLetters(LoaderBlock,
                             NtDeviceName,
                             NtSystemPath,
                             NtSystemPathString);
}

/* EOF */
