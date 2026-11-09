/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Giving the loader's ARC names an NT name to point at
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
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

VOID
IopMarkBootPartition(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ANSI_STRING ArcName;
    NTSTATUS Status;

    PAGED_CODE();

    if (LoaderBlock->ArcBootDeviceName == NULL) return;

    RtlInitAnsiString(&ArcName, LoaderBlock->ArcBootDeviceName);

    Status = RtlAnsiStringToUnicodeString(NULL, &ArcName, FALSE);

    IoLoaderArcBootDeviceName = LoaderBlock->ArcBootDeviceName;

    UNREFERENCED_PARAMETER(Status);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesDisk(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN BOOLEAN SingleDisk,
    OUT PBOOLEAN FoundBoot)
{
    PARC_DISK_SIGNATURE ArcSignature;
    PCONFIGURATION_INFORMATION Config;
    UNICODE_STRING DeviceName;
    UNICODE_STRING ArcName;
    ANSI_STRING AnsiArcName;
    WCHAR DeviceBuffer[64];
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    ULONG Disk;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(SingleDisk);

    *FoundBoot = FALSE;

    if (LoaderBlock->ArcDiskInformation == NULL) return STATUS_SUCCESS;

    Config = IoGetConfigurationInformation();

    Entry = LoaderBlock->ArcDiskInformation->DiskSignatureListHead.Flink;
    while (Entry !=
           &LoaderBlock->ArcDiskInformation->DiskSignatureListHead)
    {
        ArcSignature = CONTAINING_RECORD(Entry,
                                         ARC_DISK_SIGNATURE,
                                         ListEntry);
        Entry = Entry->Flink;

        if (ArcSignature->ArcName == NULL) continue;

        for (Disk = 0; Disk < Config->DiskCount; Disk++)
        {
            Status = RtlStringCchPrintfW(DeviceBuffer,
                                         RTL_NUMBER_OF(DeviceBuffer),
                                         L"\\Device\\Harddisk%lu\\Partition0",
                                         Disk);
            if (!NT_SUCCESS(Status)) continue;

            RtlInitUnicodeString(&DeviceName, DeviceBuffer);
            RtlInitAnsiString(&AnsiArcName, ArcSignature->ArcName);

            Status = RtlAnsiStringToUnicodeString(&ArcName,
                                                  &AnsiArcName,
                                                  TRUE);
            if (!NT_SUCCESS(Status)) continue;

            Status = IopCreateArcName(&ArcName, &DeviceName);

            RtlFreeUnicodeString(&ArcName);

            if (NT_SUCCESS(Status))
            {
                *FoundBoot = TRUE;
                break;
            }
        }
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
    PCONFIGURATION_INFORMATION Config;
    UNICODE_STRING DeviceName;
    UNICODE_STRING ArcName;
    ANSI_STRING AnsiArcName;
    WCHAR DeviceBuffer[64];
    NTSTATUS Status;
    ULONG Drive;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(SingleDisk);

    *FoundBoot = FALSE;

    if (LoaderBlock->ArcBootDeviceName == NULL) return STATUS_SUCCESS;

    Config = IoGetConfigurationInformation();

    RtlInitAnsiString(&AnsiArcName, LoaderBlock->ArcBootDeviceName);

    Status = RtlAnsiStringToUnicodeString(&ArcName, &AnsiArcName, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    for (Drive = 0; Drive < Config->CdRomCount; Drive++)
    {
        Status = RtlStringCchPrintfW(DeviceBuffer,
                                     RTL_NUMBER_OF(DeviceBuffer),
                                     L"\\Device\\CdRom%lu",
                                     Drive);
        if (!NT_SUCCESS(Status)) continue;

        RtlInitUnicodeString(&DeviceName, DeviceBuffer);

        Status = IopCreateArcName(&ArcName, &DeviceName);
        if (NT_SUCCESS(Status))
        {
            *FoundBoot = TRUE;
            break;
        }
    }

    RtlFreeUnicodeString(&ArcName);

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

    IopMarkBootPartition(LoaderBlock);

    Status = IopCreateArcNamesDisk(LoaderBlock, FALSE, &FoundBoot);
    if (!NT_SUCCESS(Status)) return Status;

    if (FoundBoot) return STATUS_SUCCESS;

    Status = IopCreateArcNamesCd(LoaderBlock, FALSE, &FoundBoot);
    if (!NT_SUCCESS(Status)) return Status;

    if (!FoundBoot)
    {
        DPRINT1("I/O: no device answered to the ARC boot name\n");
    }

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopReassignSystemRoot(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    OUT PANSI_STRING NtBootPath)
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

    Status = RtlAnsiStringToUnicodeString(&ArcName, &AnsiBootPath, TRUE);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_IO);
        return Status;
    }

    Status = RtlAppendUnicodeStringToString(&TargetName, &ArcName);

    RtlFreeUnicodeString(&ArcName);

    if (!NT_SUCCESS(Status))
    {
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

    if (!NT_SUCCESS(Status)) return Status;

    *NtBootPath = AnsiBootPath;

    return STATUS_SUCCESS;
}
