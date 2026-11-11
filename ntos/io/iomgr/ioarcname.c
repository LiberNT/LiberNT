/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Giving the loader's ARC names an NT name to point at
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <ntddcdrm.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ANSI_STRING IoArcBootDeviceName;
ANSI_STRING IoArcHalDeviceName;
PCHAR IoLoaderArcBootDeviceName = NULL;

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopArcNamePrefix =
    RTL_CONSTANT_STRING(L"\\ArcName\\");

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
IopCreateArcName(
    _In_ PUNICODE_STRING ArcName,
    _In_ PUNICODE_STRING DeviceName)
{
    UNICODE_STRING FullName;
    NTSTATUS Status;
    PWCHAR Buffer;
    USHORT Length;

    PAGED_CODE();

    Length = IopArcNamePrefix.Length + ArcName->Length + sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    FullName.Buffer = Buffer;
    FullName.Length = 0;
    FullName.MaximumLength = Length;

    RtlAppendUnicodeStringToString(&FullName,
                                   (PUNICODE_STRING)&IopArcNamePrefix);
    RtlAppendUnicodeStringToString(&FullName, ArcName);

    Status = IoCreateSymbolicLink(&FullName, DeviceName);

    ExFreePoolWithTag(Buffer, TAG_IO);

    return Status;
}

BOOLEAN
IopVerifyDiskSignature(
    _In_ PDRIVE_LAYOUT_INFORMATION_EX DriveLayout,
    _In_ PARC_DISK_SIGNATURE ArcDiskSignature,
    _Out_ PULONG Signature)
{
    ULONG Index;

    *Signature = 0;

    if (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR)
    {
        if (DriveLayout->Mbr.Signature != ArcDiskSignature->Signature)
        {
            return FALSE;
        }

        *Signature = DriveLayout->Mbr.Signature;

        if (!ArcDiskSignature->ValidPartitionTable) return TRUE;

        for (Index = 0; Index < DriveLayout->PartitionCount; Index++)
        {
            if (DriveLayout->PartitionEntry[Index].Mbr.PartitionType != 0)
            {
                return TRUE;
            }
        }

        return TRUE;
    }

    if (DriveLayout->PartitionStyle != PARTITION_STYLE_GPT) return FALSE;
    if (!ArcDiskSignature->IsGpt) return FALSE;

    return RtlCompareMemory(&DriveLayout->Gpt.DiskId,
                            ArcDiskSignature->GptSignature,
                            sizeof(GUID)) == sizeof(GUID);
}

static
NTSTATUS
IopArcWaitIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PKEVENT Event,
    _In_ PIO_STATUS_BLOCK IoStatusBlock)
{
    NTSTATUS Status;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock->Status;
    }

    return Status;
}

static
NTSTATUS
IopArcDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _Out_writes_bytes_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        NULL,
                                        0,
                                        Output,
                                        OutputLength,
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    return IopArcWaitIrp(DeviceObject, Irp, &Event, &IoStatusBlock);
}

/* Reads Length bytes at ByteOffset, rounded up to whole sectors of the device */
static
PULONG
IopArcReadBytes(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG GeometryIoControlCode,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;
    DISK_GEOMETRY Geometry;
    ULONG ReadLength;
    NTSTATUS Status;
    PVOID Buffer;
    KEVENT Event;
    PIRP Irp;

    Status = IopArcDeviceControl(DeviceObject,
                                 GeometryIoControlCode,
                                 &Geometry,
                                 sizeof(Geometry));
    if (!NT_SUCCESS(Status) || Geometry.BytesPerSector == 0) return NULL;

    ReadLength = ROUND_UP(Length, Geometry.BytesPerSector);

    Buffer = ExAllocatePoolZero(NonPagedPool, ReadLength, TAG_IO);
    if (Buffer == NULL) return NULL;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Offset.QuadPart = (LONGLONG)ByteOffset;

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
                                       DeviceObject,
                                       Buffer,
                                       ReadLength,
                                       &Offset,
                                       &Event,
                                       &IoStatusBlock);
    if (Irp == NULL)
    {
        ExFreePoolWithTag(Buffer, TAG_IO);
        return NULL;
    }

    IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;

    Status = IopArcWaitIrp(DeviceObject, Irp, &Event, &IoStatusBlock);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_IO);
        return NULL;
    }

    return Buffer;
}

static
PDRIVE_LAYOUT_INFORMATION_EX
IopArcGetDriveLayout(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    ULONG Size;
    NTSTATUS Status;

    for (Size = PAGE_SIZE; Size <= 16 * PAGE_SIZE; Size *= 2)
    {
        Layout = ExAllocatePoolZero(NonPagedPool, Size, TAG_IO);
        if (Layout == NULL) return NULL;

        Status = IopArcDeviceControl(DeviceObject,
                                     IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                     Layout,
                                     Size);
        if (NT_SUCCESS(Status)) return Layout;

        ExFreePoolWithTag(Layout, TAG_IO);

        if (Status != STATUS_BUFFER_TOO_SMALL) return NULL;
    }

    return NULL;
}

/* The loader stores the value that brings the sum of the sector's ULONGs to zero */
static
BOOLEAN
IopArcChecksumMatches(
    _In_reads_(Count) PULONG Words,
    _In_ ULONG Count,
    _In_ ULONG CheckSum)
{
    ULONG Sum = CheckSum;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Sum += Words[Index];
    }

    return Sum == 0;
}

static
NTSTATUS
IopArcLinkName(
    _In_ PCSTR ArcName,
    _In_ PCWSTR DeviceName)
{
    UNICODE_STRING UnicodeArcName;
    UNICODE_STRING UnicodeDeviceName;
    ANSI_STRING AnsiArcName;
    NTSTATUS Status;

    RtlInitAnsiString(&AnsiArcName, ArcName);
    Status = RtlAnsiStringToUnicodeString(&UnicodeArcName, &AnsiArcName, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&UnicodeDeviceName, DeviceName);
    Status = IopCreateArcName(&UnicodeArcName, &UnicodeDeviceName);

    RtlFreeUnicodeString(&UnicodeArcName);
    return Status;
}

static
VOID
IopArcLinkDisk(
    _In_ PARC_DISK_SIGNATURE ArcSignature,
    _In_ ULONG Disk,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout)
{
    PPARTITION_INFORMATION_EX Partition;
    CHAR ArcName[256];
    WCHAR DeviceName[64];
    NTSTATUS Status;
    ULONG Index;

    Status = RtlStringCchPrintfW(DeviceName,
                                 RTL_NUMBER_OF(DeviceName),
                                 L"\\Device\\Harddisk%lu\\Partition0",
                                 Disk);
    if (NT_SUCCESS(Status)) IopArcLinkName(ArcSignature->ArcName, DeviceName);

    for (Index = 0; Index < Layout->PartitionCount; Index++)
    {
        Partition = &Layout->PartitionEntry[Index];

        if (Partition->PartitionNumber == 0) continue;

        if (Partition->PartitionStyle == PARTITION_STYLE_MBR &&
            (Partition->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
             IsContainerPartition(Partition->Mbr.PartitionType)))
        {
            continue;
        }

        Status = RtlStringCchPrintfA(ArcName,
                                     RTL_NUMBER_OF(ArcName),
                                     "%spartition(%lu)",
                                     ArcSignature->ArcName,
                                     Partition->PartitionNumber);
        if (!NT_SUCCESS(Status)) continue;

        Status = RtlStringCchPrintfW(DeviceName,
                                     RTL_NUMBER_OF(DeviceName),
                                     L"\\Device\\Harddisk%lu\\Partition%lu",
                                     Disk,
                                     Partition->PartitionNumber);
        if (!NT_SUCCESS(Status)) continue;

        Status = IopArcLinkName(ArcName, DeviceName);
    }
}

static
BOOLEAN
IopArcNameIsPrefixOf(
    _In_ PCSTR ArcName,
    _In_ PANSI_STRING BootName)
{
    ANSI_STRING Prefix;

    RtlInitAnsiString(&Prefix, ArcName);
    return RtlPrefixString(&Prefix, BootName, TRUE);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesDisk(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN BOOLEAN SingleDisk,
    OUT PBOOLEAN FoundBoot)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PARC_DISK_SIGNATURE ArcSignature;
    PCONFIGURATION_INFORMATION Config;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    UNICODE_STRING DeviceName;
    WCHAR DeviceBuffer[64];
    PLIST_ENTRY ListHead;
    PLIST_ENTRY Entry;
    ULONG Signature;
    NTSTATUS Status;
    PULONG Sector;
    ULONG Disk;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(SingleDisk);

    *FoundBoot = FALSE;

    if (LoaderBlock->ArcDiskInformation == NULL) return STATUS_SUCCESS;

    Config = IoGetConfigurationInformation();
    ListHead = &LoaderBlock->ArcDiskInformation->DiskSignatureListHead;

    for (Disk = 0; Disk < Config->DiskCount; Disk++)
    {
        Status = RtlStringCchPrintfW(DeviceBuffer,
                                     RTL_NUMBER_OF(DeviceBuffer),
                                     L"\\Device\\Harddisk%lu\\Partition0",
                                     Disk);
        if (!NT_SUCCESS(Status)) continue;

        RtlInitUnicodeString(&DeviceName, DeviceBuffer);

        Status = IoGetDeviceObjectPointer(&DeviceName,
                                          FILE_READ_ATTRIBUTES,
                                          &FileObject,
                                          &DeviceObject);
        if (!NT_SUCCESS(Status)) continue;

        Layout = IopArcGetDriveLayout(DeviceObject);
        Sector = IopArcReadBytes(DeviceObject, IOCTL_DISK_GET_DRIVE_GEOMETRY, 0, 512);

        for (Entry = ListHead->Flink;
             Layout != NULL && Sector != NULL && Entry != ListHead;
             Entry = Entry->Flink)
        {
            ArcSignature = CONTAINING_RECORD(Entry, ARC_DISK_SIGNATURE, ListEntry);

            if (ArcSignature->ArcName == NULL) continue;
            if (!IopVerifyDiskSignature(Layout, ArcSignature, &Signature)) continue;

            /* Disks the loader saw without a signature are told apart by their first sector */
            if (!ArcSignature->IsGpt &&
                !IopArcChecksumMatches(Sector, 512 / sizeof(ULONG), ArcSignature->CheckSum))
            {
                continue;
            }

            IopArcLinkDisk(ArcSignature, Disk, Layout);

            if (IopArcNameIsPrefixOf(ArcSignature->ArcName, &IoArcBootDeviceName))
                *FoundBoot = TRUE;

            break;
        }

        if (Sector != NULL) ExFreePoolWithTag(Sector, TAG_IO);
        if (Layout != NULL) ExFreePoolWithTag(Layout, TAG_IO);
        ObDereferenceObject(FileObject);
    }

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesCd(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN BOOLEAN SingleDisk,
    OUT PBOOLEAN FoundBoot)
{
    PARC_DISK_SIGNATURE ArcSignature;
    PCONFIGURATION_INFORMATION Config;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    UNICODE_STRING DeviceName;
    WCHAR DeviceBuffer[64];
    PLIST_ENTRY ListHead;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    PULONG Sector;
    ULONG Drive;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(SingleDisk);

    *FoundBoot = FALSE;

    if (LoaderBlock->ArcDiskInformation == NULL) return STATUS_SUCCESS;

    Config = IoGetConfigurationInformation();
    ListHead = &LoaderBlock->ArcDiskInformation->DiskSignatureListHead;

    for (Drive = 0; Drive < Config->CdRomCount; Drive++)
    {
        Status = RtlStringCchPrintfW(DeviceBuffer,
                                     RTL_NUMBER_OF(DeviceBuffer),
                                     L"\\Device\\CdRom%lu",
                                     Drive);
        if (!NT_SUCCESS(Status)) continue;

        RtlInitUnicodeString(&DeviceName, DeviceBuffer);

        Status = IoGetDeviceObjectPointer(&DeviceName,
                                          FILE_READ_ATTRIBUTES,
                                          &FileObject,
                                          &DeviceObject);
        if (!NT_SUCCESS(Status)) continue;

        /* The loader sums the 2048-byte sector 16, where the volume descriptors start */
        Sector = IopArcReadBytes(DeviceObject, IOCTL_CDROM_GET_DRIVE_GEOMETRY, 16 * 2048, 2048);

        for (Entry = ListHead->Flink;
             Sector != NULL && Entry != ListHead;
             Entry = Entry->Flink)
        {
            ArcSignature = CONTAINING_RECORD(Entry, ARC_DISK_SIGNATURE, ListEntry);

            if (ArcSignature->ArcName == NULL) continue;
            if (strstr(ArcSignature->ArcName, "cdrom(") == NULL) continue;
            if (!IopArcChecksumMatches(Sector, 2048 / sizeof(ULONG), ArcSignature->CheckSum)) continue;

            IopArcLinkName(ArcSignature->ArcName, DeviceBuffer);

            if (IopArcNameIsPrefixOf(ArcSignature->ArcName, &IoArcBootDeviceName))
                *FoundBoot = TRUE;

            break;
        }

        if (Sector != NULL) ExFreePoolWithTag(Sector, TAG_IO);
        ObDereferenceObject(FileObject);
    }

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNames(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    BOOLEAN FoundBoot = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (LoaderBlock->ArcBootDeviceName != NULL)
    {
        RtlInitAnsiString(&IoArcBootDeviceName,
                          LoaderBlock->ArcBootDeviceName);
    }

    if (LoaderBlock->ArcHalDeviceName != NULL)
    {
        RtlInitAnsiString(&IoArcHalDeviceName,
                          LoaderBlock->ArcHalDeviceName);
    }

    /* TODO: ARC names for ramdisk and network boot devices */

    Status = IopCreateArcNamesDisk(LoaderBlock, FALSE, &FoundBoot);
    if (!NT_SUCCESS(Status)) return Status;

    if (FoundBoot) return STATUS_SUCCESS;

    Status = IopCreateArcNamesCd(LoaderBlock, FALSE, &FoundBoot);
    if (!NT_SUCCESS(Status)) return Status;

    if (!FoundBoot)
    {
        DPRINT1("I/O: no device answered to the ARC boot name %Z\n", &IoArcBootDeviceName);
    }

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
IopMarkBootPartition(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING DeviceName;
    PFILE_OBJECT FileObject;
    CHAR ArcName[256];
    ANSI_STRING AnsiName;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (LoaderBlock->ArcBootDeviceName == NULL) return;

    Status = RtlStringCchPrintfA(ArcName,
                                 RTL_NUMBER_OF(ArcName),
                                 "\\ArcName\\%s",
                                 LoaderBlock->ArcBootDeviceName);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE, (ULONG_PTR)LoaderBlock->ArcBootDeviceName, Status, 0, 0);

    RtlInitAnsiString(&AnsiName, ArcName);
    Status = RtlAnsiStringToUnicodeString(&DeviceName, &AnsiName, TRUE);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE, (ULONG_PTR)LoaderBlock->ArcBootDeviceName, Status, 0, 0);

    InitializeObjectAttributes(&ObjectAttributes,
                               &DeviceName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        FILE_READ_ATTRIBUTES,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        0);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE, (ULONG_PTR)&DeviceName, Status, 0, 0);

    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE, (ULONG_PTR)&DeviceName, Status, 0, 0);

    FileObject->DeviceObject->Flags |= DO_SYSTEM_BOOT_PARTITION;

    if (!IoInitializeDedicatedCrashDump(FileObject))
        IoInitializeBootPageFileCrashDump(&DeviceName);

    ObDereferenceObject(FileObject);
    ZwClose(Handle);
    RtlFreeUnicodeString(&DeviceName);
}

/* The device a firmware name stands for, as the name of a device object */
CODE_SEG("INIT")
static
NTSTATUS
IopArcQueryDeviceName(
    _In_ PUNICODE_STRING ArcLinkName,
    _Out_ PANSI_STRING NtDeviceName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Target;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               ArcLinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenSymbolicLinkObject(&Handle, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    Target.Length = 0;
    Target.MaximumLength = MAXUSHORT - 1;
    Target.Buffer = ExAllocatePoolZero(PagedPool, Target.MaximumLength, TAG_IO);
    if (Target.Buffer == NULL)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQuerySymbolicLinkObject(Handle, &Target, NULL);
    ZwClose(Handle);

    if (NT_SUCCESS(Status))
    {
        Status = RtlUnicodeStringToAnsiString(NtDeviceName, &Target, TRUE);
    }

    ExFreePoolWithTag(Target.Buffer, TAG_IO);
    return Status;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopReassignSystemRoot(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    OUT PANSI_STRING NtDeviceName)
{
    UNICODE_STRING LinkName = RTL_CONSTANT_STRING(L"\\SystemRoot");
    UNICODE_STRING TargetName;
    UNICODE_STRING ArcName;
    ANSI_STRING AnsiArcName;
    ANSI_STRING AnsiBootPath;
    NTSTATUS Status;
    PWCHAR Buffer;
    USHORT Length;
    HANDLE Handle;
    OBJECT_ATTRIBUTES ObjectAttributes;

    PAGED_CODE();

    if (LoaderBlock->ArcBootDeviceName == NULL ||
        LoaderBlock->NtBootPathName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlInitAnsiString(&AnsiArcName, LoaderBlock->ArcBootDeviceName);
    RtlInitAnsiString(&AnsiBootPath, LoaderBlock->NtBootPathName);

    Status = RtlAnsiStringToUnicodeString(&ArcName, &AnsiArcName, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Length = IopArcNamePrefix.Length + ArcName.Length +
             (USHORT)(AnsiBootPath.Length * sizeof(WCHAR)) + sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Buffer == NULL)
    {
        RtlFreeUnicodeString(&ArcName);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    TargetName.Buffer = Buffer;
    TargetName.Length = 0;
    TargetName.MaximumLength = Length;

    RtlAppendUnicodeStringToString(&TargetName,
                                   (PUNICODE_STRING)&IopArcNamePrefix);
    RtlAppendUnicodeStringToString(&TargetName, &ArcName);

    RtlFreeUnicodeString(&ArcName);

    if (!NT_SUCCESS(IopArcQueryDeviceName(&TargetName, NtDeviceName)))
    {
        RtlInitAnsiString(NtDeviceName, NULL);
    }

    Status = RtlAnsiStringToUnicodeString(&ArcName, &AnsiBootPath, TRUE);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeAnsiString(NtDeviceName);
        ExFreePoolWithTag(Buffer, TAG_IO);
        return Status;
    }

    Status = RtlAppendUnicodeStringToString(&TargetName, &ArcName);

    RtlFreeUnicodeString(&ArcName);

    if (!NT_SUCCESS(Status))
    {
        RtlFreeAnsiString(NtDeviceName);
        ExFreePoolWithTag(Buffer, TAG_IO);
        return Status;
    }

    while (TargetName.Length >= sizeof(WCHAR) &&
           TargetName.Buffer[TargetName.Length / sizeof(WCHAR) - 1] == L'\\')
    {
        TargetName.Length -= sizeof(WCHAR);
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT |
                                   OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenSymbolicLinkObject(&Handle, DELETE, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        ZwMakeTemporaryObject(Handle);
        ZwClose(Handle);
    }

    Status = IoCreateSymbolicLink(&LinkName, &TargetName);

    ExFreePoolWithTag(Buffer, TAG_IO);

    if (!NT_SUCCESS(Status)) RtlFreeAnsiString(NtDeviceName);

    return Status;
}
