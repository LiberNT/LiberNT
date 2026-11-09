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

VOID
FASTCALL
xHalIoAssignDriveLetters(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PSTRING NtDeviceName,
    _Out_ PUCHAR NtSystemPath,
    _Out_ PSTRING NtSystemPathString)
{
    PCSTR BootPath;
    SIZE_T DeviceLength = 0;
    SIZE_T PathLength;
    SIZE_T i;

    PAGED_CODE();

    NtSystemPath[0] = '\0';
    NtSystemPathString->Buffer = (PCHAR)NtSystemPath;
    NtSystemPathString->Length = 0;
    NtSystemPathString->MaximumLength = FSTUB_SYSTEM_PATH_MAX;

    if (LoaderBlock == NULL || LoaderBlock->NtBootPathName == NULL) return;

    if (NtDeviceName != NULL && NtDeviceName->Buffer != NULL)
    {
        DeviceLength = NtDeviceName->Length;

        if (DeviceLength > FSTUB_SYSTEM_PATH_MAX - 1)
        {
            DeviceLength = FSTUB_SYSTEM_PATH_MAX - 1;
        }

        for (i = 0; i < DeviceLength; i++)
        {
            NtSystemPath[i] = (UCHAR)NtDeviceName->Buffer[i];
        }
    }

    BootPath = LoaderBlock->NtBootPathName;
    PathLength = FstubAnsiLength(BootPath);

    /* The loader hands the path over with a separator on the end */
    while (PathLength != 0 && BootPath[PathLength - 1] == '\\') PathLength--;

    if (DeviceLength + PathLength > FSTUB_SYSTEM_PATH_MAX - 1)
    {
        PathLength = FSTUB_SYSTEM_PATH_MAX - 1 - DeviceLength;
    }

    for (i = 0; i < PathLength; i++)
    {
        NtSystemPath[DeviceLength + i] = (UCHAR)BootPath[i];
    }

    NtSystemPath[DeviceLength + PathLength] = '\0';

    NtSystemPathString->Length = (USHORT)(DeviceLength + PathLength);

    /* TODO: tell the mount manager which volume this path names */
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
