/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Bootloader support functions
 * COPYRIGHT:   ...
 *              Copyright 2017-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include <ntddstor.h> // For STORAGE_DEVICE_NUMBER
#include <ntddscsi.h>

#include "devutils.h"
#include "filesup.h"
#include "partlist.h"
#include "bootcode.h"
#include "fsutil.h"

#include "setuplib.h"
extern BOOLEAN IsUnattendedSetup; // HACK

#include "bootsup.h"

#define NDEBUG
#include <debug.h>

#define PCAT_BOOT_SOURCE_DIR        L"Windows\\Boot\\PCAT"
#define PCAT_OS_LOADER_PATH         L"\\Windows\\system32\\winload.exe"

/* FUNCTIONS ****************************************************************/

static VOID
TrimTrailingPathSeparators_UStr(
    IN OUT PUNICODE_STRING UnicodeString)
{
    while (UnicodeString->Length >= sizeof(WCHAR) &&
           UnicodeString->Buffer[UnicodeString->Length / sizeof(WCHAR) - 1] == OBJ_NAME_PATH_SEPARATOR)
    {
        UnicodeString->Length -= sizeof(WCHAR);
    }
}



static
NTSTATUS
InstallMbrBootCode(
    IN PCWSTR SrcPath,
    IN HANDLE DstPath,
    IN HANDLE DiskHandle);

static
NTSTATUS
InstallBootCodeToDisk(
    IN PCWSTR SrcPath,
    IN PCWSTR RootPath,
    IN PFS_INSTALL_BOOTCODE InstallBootCode)
{
    NTSTATUS Status, LockStatus;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE PartitionHandle;

    /*
     * Open the root partition from which the bootcode (MBR, VBR) parameters
     * will be obtained; this is also where we will write the updated bootcode.
     * Remove any trailing backslash if needed.
     */
    RtlInitUnicodeString(&Name, RootPath);
    TrimTrailingPathSeparators_UStr(&Name);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&PartitionHandle,
                        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT /* | FILE_SEQUENTIAL_ONLY */);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Lock the volume */
    LockStatus = NtFsControlFile(PartitionHandle, NULL, NULL, NULL, &IoStatusBlock, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("Unable to lock the volume before installing boot code. Status 0x%08x.\n", LockStatus);
    }

    /* The whole disk has no file system to dismount, and RAW fails I/O on the handle that dismounted it */
    if (InstallBootCode != InstallMbrBootCode)
    {
        Status = NtFsControlFile(PartitionHandle, NULL, NULL, NULL, &IoStatusBlock, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Unable to dismount the volume before installing boot code. Status 0x%08x.\n", Status);
        }
    }

    /* Install the bootcode (MBR, VBR) */
    Status = InstallBootCode(SrcPath, PartitionHandle, PartitionHandle);

    /* Unlock the volume */
    if (NT_SUCCESS(LockStatus))
    {
        LockStatus = NtFsControlFile(PartitionHandle, NULL, NULL, NULL, &IoStatusBlock, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0);
        if (!NT_SUCCESS(LockStatus))
        {
            DPRINT1("Unable to unlock the volume after installing boot code. Status 0x%08x. Expect problems.\n", LockStatus);
        }
    }

    /* Close the partition */
    NtClose(PartitionHandle);

    return Status;
}


static
NTSTATUS
InstallMbrBootCode(
    IN PCWSTR SrcPath,      // MBR source file (on the installation medium)
    IN HANDLE DstPath,      // Where to save the bootsector built from the source + disk information
    IN HANDLE DiskHandle)   // Disk holding the (old) MBR information
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    BOOTCODE OrigBootSector = {0};
    BOOTCODE NewBootSector  = {0};

C_ASSERT(sizeof(PARTITION_SECTOR) == SECTORSIZE);

    /* Allocate and read the current original MBR bootsector */
    Status = ReadBootCodeByHandle(&OrigBootSector,
                                  DiskHandle,
                                  sizeof(PARTITION_SECTOR));
    if (!NT_SUCCESS(Status))
        return Status;

    /* Allocate and read the new bootsector from SrcPath */
    RtlInitUnicodeString(&Name, SrcPath);
    Status = ReadBootCodeFromFile(&NewBootSector,
                                  &Name,
                                  sizeof(PARTITION_SECTOR));
    if (!NT_SUCCESS(Status))
    {
        FreeBootCode(&OrigBootSector);
        return Status;
    }

    /*
     * Copy the disk signature, the reserved fields and
     * the partition table from the old MBR to the new one.
     */
    RtlCopyMemory(&((PPARTITION_SECTOR)NewBootSector.BootCode)->Signature,
                  &((PPARTITION_SECTOR)OrigBootSector.BootCode)->Signature,
                  sizeof(PARTITION_SECTOR) -
                  FIELD_OFFSET(PARTITION_SECTOR, Signature)
                  /* Length of partition table */);

    /* Free the original bootsector */
    FreeBootCode(&OrigBootSector);

    /* Write the new bootsector to DstPath */
    FileOffset.QuadPart = 0ULL;
    Status = NtWriteFile(DstPath,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         NewBootSector.BootCode,
                         NewBootSector.Length,
                         &FileOffset,
                         NULL);

    /* Free the new bootsector */
    FreeBootCode(&NewBootSector);

    return Status;
}

static
NTSTATUS
InstallMbrBootCodeToDisk(
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCWSTR DestinationDevicePathBuffer)
{
    WCHAR SourceMbrPathBuffer[MAX_PATH];

    CombinePaths(SourceMbrPathBuffer, ARRAYSIZE(SourceMbrPathBuffer), 3,
                 SourceRootPath->Buffer, PCAT_BOOT_SOURCE_DIR, L"mbr.bin");

    DPRINT1("Install MBR bootcode: %S ==> %S\n",
            SourceMbrPathBuffer, DestinationDevicePathBuffer);

    return InstallBootCodeToDisk(SourceMbrPathBuffer,
                                 DestinationDevicePathBuffer,
                                 InstallMbrBootCode);
}

#define EFI_VARIABLE_BOOT_ATTRIBUTES    0x00000007
#define EFI_LOAD_OPTION_ACTIVE          0x00000001
#define EFI_LOAD_OPTION_HEADER_SIZE     6
#define EFI_DEVICE_PATH_HEADER_SIZE     4
#define EFI_MEDIA_DEVICE_PATH           0x04
#define EFI_MEDIA_HARDDRIVE_DP          0x01
#define EFI_MEDIA_FILEPATH_DP           0x04
#define EFI_END_DEVICE_PATH_TYPE        0x7F
#define EFI_END_ENTIRE_DEVICE_PATH      0xFF
#define EFI_HARDDRIVE_NODE_SIZE         42
#define EFI_HARDDRIVE_NUMBER_OFFSET     4
#define EFI_HARDDRIVE_START_OFFSET      8
#define EFI_HARDDRIVE_SIZE_OFFSET       16
#define EFI_HARDDRIVE_SIGNATURE_OFFSET  24
#define EFI_HARDDRIVE_FORMAT_OFFSET     40
#define EFI_HARDDRIVE_TYPE_OFFSET       41
#define EFI_PARTITION_FORMAT_MBR        0x01
#define EFI_PARTITION_FORMAT_GPT        0x02
#define EFI_SIGNATURE_TYPE_MBR          0x01
#define EFI_SIGNATURE_TYPE_GUID         0x02
#define EFI_LOAD_OPTION_MAX             4096
#define EFI_BOOT_ORDER_MAX              512
#define EFI_BOOT_OPTION_SEARCH_MAX      0x1000
#define EFI_BOOT_MANAGER_DESCRIPTION    L"LiberNT Boot Manager"
#define EFI_BOOT_MANAGER_FILE           L"\\EFI\\LiberNT\\Boot\\bootmgfw.efi"
#define EFI_WINDOWS_BOOT_MANAGER_FILE   L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
#define EFI_HOST_SETUP_DIRECTORY        L"EFI\\LiberNT\\Setup"
#define EFI_HOST_SETUP_MANAGER_FILE     L"\\EFI\\LiberNT\\Setup\\bootmgfw.efi"
#define GPT_HEADER_ENTRIES_LBA_OFFSET   72
#define GPT_HEADER_ENTRY_COUNT_OFFSET   80
#define GPT_HEADER_ENTRY_SIZE_OFFSET    84
#define GPT_ENTRY_PARTITION_ID_OFFSET   16
#define GPT_ENTRY_MIN_SIZE              128
#define GPT_ENTRY_MAX_COUNT             1024
#define MBR_PARTITION_TABLE_OFFSET      446
#define MBR_PARTITION_ENTRY_SIZE        16
#define MBR_PARTITION_START_OFFSET      8

static GUID EfiGlobalVariableGuid = {0x8BE4DF61, 0x93CA, 0x11D2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C}};

static
BOOLEAN
AreFilesIdentical(
    _In_ PCWSTR FirstPath,
    _In_ PCWSTR SecondPath)
{
    HANDLE FirstFile, FirstSection, SecondFile, SecondSection;
    PVOID FirstBase, SecondBase;
    ULONG FirstSize, SecondSize;
    BOOLEAN Identical = FALSE;

    if (!NT_SUCCESS(OpenAndMapFile(NULL, FirstPath, &FirstFile, &FirstSize, &FirstSection, &FirstBase, FALSE)))
        return FALSE;

    if (NT_SUCCESS(OpenAndMapFile(NULL, SecondPath, &SecondFile, &SecondSize, &SecondSection, &SecondBase, FALSE)))
    {
        Identical = (FirstSize == SecondSize) && RtlEqualMemory(FirstBase, SecondBase, FirstSize);
        UnMapAndCloseFile(SecondFile, SecondSection, SecondBase);
    }

    UnMapAndCloseFile(FirstFile, FirstSection, FirstBase);
    return Identical;
}

static
VOID
GetEfiLoaderSourceDir(
    _In_ PCUNICODE_STRING SourceRootPath,
    _Out_writes_(Chars) PWSTR SrcDir,
    _In_ SIZE_T Chars)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\Setup");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"SystemPartition");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    NTSTATUS Status;
    ULONG Length;
    union
    {
        KEY_VALUE_PARTIAL_INFORMATION Info;
        UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + MAX_PATH * sizeof(WCHAR)];
    } Value;

    CombinePaths(SrcDir, Chars, 2, SourceRootPath->Buffer, L"efi\\boot");
    if (DoesDirExist(NULL, SrcDir))
        return;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes)))
        return;

    Status = NtQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation,
                             &Value, sizeof(Value) - sizeof(WCHAR), &Length);
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status) || Value.Info.Type != REG_SZ || Value.Info.DataLength < sizeof(WCHAR))
        return;

    ((PWCHAR)Value.Info.Data)[Value.Info.DataLength / sizeof(WCHAR)] = UNICODE_NULL;
    CombinePaths(SrcDir, Chars, 2, (PCWSTR)Value.Info.Data, L"EFI\\BOOT");
}

static
NTSTATUS
InstallEfiLoaderFiles(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ BOOLEAN KeepForeignFiles,
    _Out_opt_ PBOOLEAN ForeignFilesKept)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE DirectoryHandle;
    BOOLEAN RestartScan = TRUE;
    WCHAR SrcDir[MAX_PATH];
    WCHAR DstDir[MAX_PATH];
    WCHAR SrcPath[MAX_PATH];
    WCHAR DstPath[MAX_PATH];
    WCHAR FileName[MAX_PATH];
    WCHAR OwnManagerPath[MAX_PATH];
    union
    {
        FILE_DIRECTORY_INFORMATION Info;
        UCHAR Buffer[sizeof(FILE_DIRECTORY_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    } Entry;

    if (ForeignFilesKept)
        *ForeignFilesKept = FALSE;

    GetEfiLoaderSourceDir(SourceRootPath, SrcDir, ARRAYSIZE(SrcDir));
    CombinePaths(DstDir, ARRAYSIZE(DstDir), 2, SystemRootPath->Buffer, L"EFI\\BOOT");
    CombinePaths(OwnManagerPath, ARRAYSIZE(OwnManagerPath), 2, SystemRootPath->Buffer, &EFI_BOOT_MANAGER_FILE[1]);

    RtlInitUnicodeString(&Name, SrcDir);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&DirectoryHandle,
                        FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_OBJECT_NAME_NOT_FOUND && Status != STATUS_OBJECT_PATH_NOT_FOUND)
            DPRINT1("NtOpenFile(%S) failed (Status 0x%08lx)\n", SrcDir, Status);
        return STATUS_SUCCESS;
    }

    Status = SetupCreateDirectory(DstDir);
    while (NT_SUCCESS(Status))
    {
        Status = NtQueryDirectoryFile(DirectoryHandle,
                                      NULL,
                                      NULL,
                                      NULL,
                                      &IoStatusBlock,
                                      &Entry,
                                      sizeof(Entry),
                                      FileDirectoryInformation,
                                      TRUE,
                                      NULL,
                                      RestartScan);
        if (Status == STATUS_NO_MORE_FILES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
            break;

        RestartScan = FALSE;
        if (Entry.Info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        RtlStringCchCopyNW(FileName, ARRAYSIZE(FileName),
                           Entry.Info.FileName,
                           Entry.Info.FileNameLength / sizeof(WCHAR));
        CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SrcDir, FileName);
        CombinePaths(DstPath, ARRAYSIZE(DstPath), 2, DstDir, FileName);

        if (KeepForeignFiles &&
            DoesFileExist(NULL, DstPath) &&
            !AreFilesIdentical(DstPath, OwnManagerPath))
        {
            DPRINT1("Keep %S, which belongs to another operating system\n", DstPath);
            if (ForeignFilesKept)
                *ForeignFilesKept = TRUE;
            continue;
        }

        DPRINT1("Copy: %S ==> %S\n", SrcPath, DstPath);
        Status = SetupCopyFile(SrcPath, DstPath, FALSE);
    }

    NtClose(DirectoryHandle);
    return Status;
}

#define BCD_DEVICE_ELEMENT_SIZE     0x58
#define BCD_DEVICE_TYPE_OFFSET      0x10
#define BCD_DEVICE_SIZE_OFFSET      0x18
#define BCD_DEVICE_PARTITION_OFFSET 0x20
#define BCD_DEVICE_STYLE_OFFSET     0x34
#define BCD_DEVICE_DISK_OFFSET      0x38
#define BCD_DEVICE_PARTITION        6
#define BCD_DEVICE_BODY_SIZE        0x48
#define BCD_PARTITION_STYLE_MBR     1
#define BCD_BOOTMGR_OBJECT          L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define BCD_OSLOADER_OBJECT         L"{354fa6e5-5e45-49af-91bf-1fd2754e470a}"
#define BCD_APPLICATION_DEVICE      L"11000001"
#define BCD_OS_DEVICE               L"21000001"
#define BCD_APPLICATION_PATH        L"12000002"
#define BCD_DISPLAY_ORDER           L"24000001"
#define BCD_OBJECT_ID_CHARS         38
#define BCD_MAX_KEPT_ENTRIES        32
#define BCD_MAX_NEW_ID_TRIES        16
#define BCD_TEMPLATE_PATH           L"Windows\\System32\\config\\BCD-Template"
#define BCD_BOOT_MANAGER_PATTERN    L"boot*.efi"
#define BCD_RELATIVE_PARTITION      L"partition("
#define BCD_SCSI_ADAPTER            L"scsi("
#define BCD_SCSI_DISK               L")disk("
#define BCD_SCSI_RDISK              L")rdisk("
#define BCD_SCSI_PARTITION          L")partition("
#define BCD_DISK_PREFIX             L"\\Harddisk"
#define BCD_PARTITION_PREFIX        L"\\Partition"

static
BOOLEAN
ParseArcPathNumber(
    _Inout_ PCWSTR* Text,
    _In_ PCWSTR Prefix,
    _Out_ PULONG Value)
{
    SIZE_T Length = wcslen(Prefix);
    PWSTR End;

    if (_wcsnicmp(*Text, Prefix, Length) != 0)
        return FALSE;

    *Value = wcstoul(*Text + Length, &End, 10);
    if (End == *Text + Length)
        return FALSE;

    *Text = End;
    return TRUE;
}

static
NTSTATUS
GetScsiArcPathPartition(
    _In_ PCWSTR ArcPath,
    _Out_ PULONG DiskNumber,
    _Out_ PULONG PartitionNumber)
{
    SYSTEM_DEVICE_INFORMATION DeviceInformation;
    IO_STATUS_BLOCK IoStatusBlock;
    SCSI_ADDRESS Address;
    WCHAR Path[MAX_PATH];
    ULONG Port, Bus, Target, Partition, i;
    HANDLE Handle;
    NTSTATUS Status;

    if (!ParseArcPathNumber(&ArcPath, BCD_SCSI_ADAPTER, &Port) ||
        !ParseArcPathNumber(&ArcPath, BCD_SCSI_DISK, &Bus) ||
        !ParseArcPathNumber(&ArcPath, BCD_SCSI_RDISK, &Target) ||
        !ParseArcPathNumber(&ArcPath, BCD_SCSI_PARTITION, &Partition))
    {
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    }

    Status = NtQuerySystemInformation(SystemDeviceInformation,
                                      &DeviceInformation,
                                      sizeof(DeviceInformation),
                                      NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < DeviceInformation.NumberOfDisks; i++)
    {
        RtlStringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Device\\Harddisk%lu\\Partition0", i);
        if (!NT_SUCCESS(pOpenDevice(Path, &Handle)))
            continue;

        Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                       IOCTL_SCSI_GET_ADDRESS,
                                       NULL, 0,
                                       &Address, sizeof(Address));
        NtClose(Handle);
        if (NT_SUCCESS(Status) &&
            Address.PortNumber == Port &&
            Address.PathId == Bus &&
            Address.TargetId == Target)
        {
            *DiskNumber = i;
            *PartitionNumber = Partition;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_OBJECT_PATH_NOT_FOUND;
}

static
BOOLEAN
FindPartitionByTablePosition(
    _In_ const DRIVE_LAYOUT_INFORMATION_EX* Layout,
    _In_ ULONG Position,
    _Out_ PPARTITION_INFORMATION_EX Partition)
{
    ULONG Index, Count = 0;

    for (Index = 0; Index < Layout->PartitionCount; Index++)
    {
        const PARTITION_INFORMATION_EX* Entry = &Layout->PartitionEntry[Index];

        if (Entry->PartitionStyle == PARTITION_STYLE_MBR &&
            (Entry->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
             IsContainerPartition(Entry->Mbr.PartitionType)))
        {
            continue;
        }

        if (++Count == Position)
        {
            *Partition = *Entry;
            return TRUE;
        }
    }

    return FALSE;
}

static
NTSTATUS
GetBcdPartitionElement(
    _In_ ULONG DiskNumber,
    _In_ ULONG PartitionNumber,
    _In_ BOOLEAN TablePosition,
    _Out_writes_bytes_(BCD_DEVICE_ELEMENT_SIZE) PUCHAR Element)
{
    union
    {
        DRIVE_LAYOUT_INFORMATION_EX Layout;
        UCHAR Buffer[FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + 128 * sizeof(PARTITION_INFORMATION_EX)];
    } Drive;
    PARTITION_INFORMATION_EX Partition;
    IO_STATUS_BLOCK IoStatusBlock;
    WCHAR Path[MAX_PATH];
    HANDLE Handle;
    NTSTATUS Status;

    if (!TablePosition)
    {
        RtlStringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Device\\Harddisk%lu\\Partition%lu", DiskNumber, PartitionNumber);
        Status = pOpenDevice(Path, &Handle);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                       IOCTL_DISK_GET_PARTITION_INFO_EX,
                                       NULL, 0, &Partition, sizeof(Partition));
        NtClose(Handle);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    RtlStringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Device\\Harddisk%lu\\Partition0", DiskNumber);
    Status = pOpenDevice(Path, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                   IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                   NULL, 0, &Drive, sizeof(Drive));
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    if (TablePosition && !FindPartitionByTablePosition(&Drive.Layout, PartitionNumber, &Partition))
        return STATUS_OBJECT_PATH_NOT_FOUND;

    RtlZeroMemory(Element, BCD_DEVICE_ELEMENT_SIZE);
    *(PULONG)(Element + BCD_DEVICE_TYPE_OFFSET) = BCD_DEVICE_PARTITION;
    *(PULONG)(Element + BCD_DEVICE_SIZE_OFFSET) = BCD_DEVICE_BODY_SIZE;

    if (Partition.PartitionStyle == PARTITION_STYLE_GPT)
    {
        RtlCopyMemory(Element + BCD_DEVICE_PARTITION_OFFSET, &Partition.Gpt.PartitionId, sizeof(GUID));
        RtlCopyMemory(Element + BCD_DEVICE_DISK_OFFSET, &Drive.Layout.Gpt.DiskId, sizeof(GUID));
    }
    else
    {
        *(PULONGLONG)(Element + BCD_DEVICE_PARTITION_OFFSET) = Partition.StartingOffset.QuadPart;
        *(PULONG)(Element + BCD_DEVICE_STYLE_OFFSET) = BCD_PARTITION_STYLE_MBR;
        *(PULONG)(Element + BCD_DEVICE_DISK_OFFSET) = Drive.Layout.Mbr.Signature;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
SetBcdElement(
    _In_ PCWSTR Object,
    _In_ PCWSTR Element,
    _In_ ULONG Type,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ ULONG Size)
{
    static UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Element");
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    WCHAR Path[MAX_PATH];
    HANDLE Key;
    NTSTATUS Status;

    RtlStringCchPrintfW(Path, ARRAYSIZE(Path),
                        L"\\Registry\\Machine\\SetupBcd\\Objects\\%s\\Elements\\%s",
                        Object, Element);
    RtlInitUnicodeString(&KeyName, Path);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtCreateKey(&Key, KEY_SET_VALUE, &ObjectAttributes, 0, NULL, REG_OPTION_BACKUP_RESTORE, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtSetValueKey(Key, &ValueName, 0, Type, (PVOID)Data, Size);
    NtClose(Key);
    return Status;
}

static
NTSTATUS
DeleteBcdElement(
    _In_ PCWSTR Object,
    _In_ PCWSTR Element)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    WCHAR Path[MAX_PATH];
    HANDLE Key;
    NTSTATUS Status;

    RtlStringCchPrintfW(Path, ARRAYSIZE(Path),
                        L"\\Registry\\Machine\\SetupBcd\\Objects\\%s\\Elements\\%s",
                        Object, Element);
    RtlInitUnicodeString(&KeyName, Path);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtOpenKeyEx(&Key, DELETE, &ObjectAttributes, REG_OPTION_BACKUP_RESTORE);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtDeleteKey(Key);
    NtClose(Key);
    return Status;
}

typedef struct _BCD_KEPT_ELEMENT
{
    struct _BCD_KEPT_ELEMENT *Next;
    WCHAR Name[16];
    ULONG Type;
    ULONG Size;
    UCHAR Data[ANYSIZE_ARRAY];
} BCD_KEPT_ELEMENT, *PBCD_KEPT_ELEMENT;

typedef struct _BCD_KEPT_ENTRY
{
    WCHAR Id[BCD_OBJECT_ID_CHARS + 1];
    ULONG Type;
    PBCD_KEPT_ELEMENT Elements;
} BCD_KEPT_ENTRY, *PBCD_KEPT_ENTRY;

typedef struct _BCD_KEPT_ENTRIES
{
    ULONG Count;
    BCD_KEPT_ENTRY Entry[BCD_MAX_KEPT_ENTRIES];
} BCD_KEPT_ENTRIES, *PBCD_KEPT_ENTRIES;

static
NTSTATUS
OpenSetupBcdKey(
    _In_opt_ HANDLE Root,
    _In_ PCWSTR Name,
    _In_ ACCESS_MASK Access,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    WCHAR Path[MAX_PATH];
    NTSTATUS Status;

    if (Root)
        Status = RtlStringCchCopyW(Path, ARRAYSIZE(Path), Name);
    else
        Status = RtlStringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Registry\\Machine\\SetupBcd\\Objects\\%s", Name);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, Path);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, Root, NULL);
    if (Create)
        return NtCreateKey(Key, Access, &ObjectAttributes, 0, NULL, REG_OPTION_BACKUP_RESTORE, NULL);
    return NtOpenKeyEx(Key, Access, &ObjectAttributes, REG_OPTION_BACKUP_RESTORE);
}

static
PKEY_VALUE_PARTIAL_INFORMATION
QueryBcdValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name)
{
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    UNICODE_STRING ValueName;
    ULONG Length = 0;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        return NULL;

    Info = RtlAllocateHeap(ProcessHeap, 0, Length);
    if (!Info)
        return NULL;

    Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Info, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, Info);
        return NULL;
    }

    ASSERT(FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + Info->DataLength <= Length);
    return Info;
}

static
PKEY_VALUE_PARTIAL_INFORMATION
QueryBcdElement(
    _In_ PCWSTR Object,
    _In_ PCWSTR Element)
{
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    WCHAR Name[MAX_PATH];
    HANDLE Key;

    if (!NT_SUCCESS(RtlStringCchPrintfW(Name, ARRAYSIZE(Name), L"%s\\Elements\\%s", Object, Element)) ||
        !NT_SUCCESS(OpenSetupBcdKey(NULL, Name, KEY_QUERY_VALUE, FALSE, &Key)))
    {
        return NULL;
    }

    Info = QueryBcdValue(Key, L"Element");
    NtClose(Key);
    return Info;
}

static
VOID
FreeBcdKeptEntry(
    _Inout_ PBCD_KEPT_ENTRY Entry)
{
    PBCD_KEPT_ELEMENT Element;

    while (Entry->Elements)
    {
        Element = Entry->Elements;
        Entry->Elements = Element->Next;
        RtlFreeHeap(ProcessHeap, 0, Element);
    }
}

static
BOOLEAN
KeepBcdEntry(
    _In_ PCWSTR Id,
    _Out_ PBCD_KEPT_ENTRY Entry)
{
    union
    {
        KEY_BASIC_INFORMATION Basic;
        UCHAR Buffer[FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + sizeof(((PBCD_KEPT_ELEMENT)NULL)->Name)];
    } Key;
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    PBCD_KEPT_ELEMENT Element;
    WCHAR Name[MAX_PATH];
    HANDLE Elements, Handle;
    ULONG Index, Length;
    NTSTATUS Status;

    RtlZeroMemory(Entry, sizeof(*Entry));
    if (!NT_SUCCESS(RtlStringCchCopyW(Entry->Id, ARRAYSIZE(Entry->Id), Id)) ||
        !NT_SUCCESS(RtlStringCchPrintfW(Name, ARRAYSIZE(Name), L"%s\\Description", Id)) ||
        !NT_SUCCESS(OpenSetupBcdKey(NULL, Name, KEY_QUERY_VALUE, FALSE, &Handle)))
    {
        return FALSE;
    }

    Info = QueryBcdValue(Handle, L"Type");
    NtClose(Handle);
    if (!Info || Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
    {
        if (Info)
            RtlFreeHeap(ProcessHeap, 0, Info);
        return FALSE;
    }
    Entry->Type = *(PULONG)Info->Data;
    RtlFreeHeap(ProcessHeap, 0, Info);

    if (!NT_SUCCESS(RtlStringCchPrintfW(Name, ARRAYSIZE(Name), L"%s\\Elements", Id)) ||
        !NT_SUCCESS(OpenSetupBcdKey(NULL, Name, KEY_ENUMERATE_SUB_KEYS, FALSE, &Elements)))
    {
        return FALSE;
    }

    for (Index = 0; ; Index++)
    {
        Status = NtEnumerateKey(Elements, Index, KeyBasicInformation, &Key, sizeof(Key) - sizeof(WCHAR), &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
            break;

        ASSERT(Key.Basic.NameLength % sizeof(WCHAR) == 0);
        ASSERT(Key.Basic.NameLength < sizeof(Element->Name));
        Key.Basic.Name[Key.Basic.NameLength / sizeof(WCHAR)] = UNICODE_NULL;

        Status = OpenSetupBcdKey(Elements, Key.Basic.Name, KEY_QUERY_VALUE, FALSE, &Handle);
        if (!NT_SUCCESS(Status))
            break;
        Info = QueryBcdValue(Handle, L"Element");
        NtClose(Handle);
        if (!Info)
        {
            Status = STATUS_REGISTRY_CORRUPT;
            break;
        }

        Element = RtlAllocateHeap(ProcessHeap, 0, FIELD_OFFSET(BCD_KEPT_ELEMENT, Data[Info->DataLength]));
        if (!Element)
        {
            RtlFreeHeap(ProcessHeap, 0, Info);
            Status = STATUS_NO_MEMORY;
            break;
        }

        RtlCopyMemory(Element->Name, Key.Basic.Name, Key.Basic.NameLength + sizeof(WCHAR));
        Element->Type = Info->Type;
        Element->Size = Info->DataLength;
        RtlCopyMemory(Element->Data, Info->Data, Info->DataLength);
        RtlFreeHeap(ProcessHeap, 0, Info);

        Element->Next = Entry->Elements;
        Entry->Elements = Element;
    }

    NtClose(Elements);
    if (!NT_SUCCESS(Status))
    {
        FreeBcdKeptEntry(Entry);
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
IsBcdEntryForOsDevice(
    _In_ PBCD_KEPT_ENTRY Entry,
    _In_reads_bytes_(BCD_DEVICE_ELEMENT_SIZE) const UCHAR *OsElement)
{
    PBCD_KEPT_ELEMENT Element;

    for (Element = Entry->Elements; Element; Element = Element->Next)
    {
        if (_wcsicmp(Element->Name, BCD_OS_DEVICE) == 0)
        {
            return Element->Size == BCD_DEVICE_ELEMENT_SIZE &&
                   RtlEqualMemory(Element->Data, OsElement, BCD_DEVICE_ELEMENT_SIZE);
        }
    }

    return FALSE;
}

/* Saves the entries of a mounted store that start another installation than the one at OsElement */
static
VOID
KeepOtherBcdEntries(
    _In_reads_bytes_(BCD_DEVICE_ELEMENT_SIZE) const UCHAR *OsElement,
    _Inout_ PBCD_KEPT_ENTRIES Kept)
{
    PKEY_VALUE_PARTIAL_INFORMATION Order;
    WCHAR Id[BCD_OBJECT_ID_CHARS + 1];
    PCWCH Chars;
    ULONG Count, i, Length = 0;

    ASSERT(Kept->Count == 0);

    Order = QueryBcdElement(BCD_BOOTMGR_OBJECT, BCD_DISPLAY_ORDER);
    if (!Order)
        return;
    if (Order->Type != REG_MULTI_SZ)
    {
        RtlFreeHeap(ProcessHeap, 0, Order);
        return;
    }

    Chars = (PCWCH)Order->Data;
    Count = Order->DataLength / sizeof(WCHAR);
    for (i = 0; i <= Count; i++)
    {
        if (i < Count && Chars[i] != UNICODE_NULL)
        {
            if (Length < BCD_OBJECT_ID_CHARS + 1)
                Id[Length] = Chars[i];
            Length++;
            continue;
        }

        if (Length == BCD_OBJECT_ID_CHARS && Kept->Count < BCD_MAX_KEPT_ENTRIES)
        {
            Id[Length] = UNICODE_NULL;
            if (KeepBcdEntry(Id, &Kept->Entry[Kept->Count]))
            {
                if (IsBcdEntryForOsDevice(&Kept->Entry[Kept->Count], OsElement))
                    FreeBcdKeptEntry(&Kept->Entry[Kept->Count]);
                else
                    Kept->Count++;
            }
        }
        Length = 0;
    }

    ASSERT(Kept->Count <= BCD_MAX_KEPT_ENTRIES);
    RtlFreeHeap(ProcessHeap, 0, Order);
}

static
BOOLEAN
BcdObjectExists(
    _In_ PCWSTR Id)
{
    HANDLE Key;

    if (!NT_SUCCESS(OpenSetupBcdKey(NULL, Id, KEY_QUERY_VALUE, FALSE, &Key)))
        return FALSE;
    NtClose(Key);
    return TRUE;
}

static
NTSTATUS
NewBcdObjectId(
    _Out_writes_(BCD_OBJECT_ID_CHARS + 1) PWSTR Id)
{
    static ULONG Seed;
    LARGE_INTEGER Counter;
    ULONG Words[4], i, Try;
    GUID Guid;
    NTSTATUS Status;

    if (Seed == 0)
    {
        NtQueryPerformanceCounter(&Counter, NULL);
        Seed = Counter.LowPart ^ Counter.HighPart ^ HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess);
    }

    for (Try = 0; Try < BCD_MAX_NEW_ID_TRIES; Try++)
    {
        for (i = 0; i < ARRAYSIZE(Words); i++)
            Words[i] = (RtlRandom(&Seed) << 16) ^ RtlRandom(&Seed);
        C_ASSERT(sizeof(Words) == sizeof(Guid));
        RtlCopyMemory(&Guid, Words, sizeof(Guid));
        Guid.Data3 = (Guid.Data3 & 0x0FFF) | 0x4000;
        Guid.Data4[0] = (Guid.Data4[0] & 0x3F) | 0x80;

        Status = RtlStringCchPrintfW(Id, BCD_OBJECT_ID_CHARS + 1,
                                     L"{%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                                     Guid.Data1, Guid.Data2, Guid.Data3,
                                     Guid.Data4[0], Guid.Data4[1], Guid.Data4[2], Guid.Data4[3],
                                     Guid.Data4[4], Guid.Data4[5], Guid.Data4[6], Guid.Data4[7]);
        if (!NT_SUCCESS(Status))
            return Status;
        ASSERT(wcslen(Id) == BCD_OBJECT_ID_CHARS);
        if (!BcdObjectExists(Id))
            return STATUS_SUCCESS;
    }

    return STATUS_OBJECT_NAME_COLLISION;
}

static
NTSTATUS
CreateBcdObject(
    _In_ PCWSTR Id,
    _In_ ULONG Type)
{
    static UNICODE_STRING TypeName = RTL_CONSTANT_STRING(L"Type");
    HANDLE Object, Key;
    NTSTATUS Status;

    Status = OpenSetupBcdKey(NULL, Id, KEY_CREATE_SUB_KEY, TRUE, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = OpenSetupBcdKey(Object, L"Elements", KEY_SET_VALUE, TRUE, &Key);
    if (NT_SUCCESS(Status))
    {
        NtClose(Key);
        Status = OpenSetupBcdKey(Object, L"Description", KEY_SET_VALUE, TRUE, &Key);
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtSetValueKey(Key, &TypeName, 0, REG_DWORD, &Type, sizeof(Type));
        NtClose(Key);
    }

    NtClose(Object);
    return Status;
}

/* Adds the kept entries to the mounted store, after its own entries in the boot menu */
static
NTSTATUS
RestoreBcdEntries(
    _Inout_ PBCD_KEPT_ENTRIES Kept)
{
    PKEY_VALUE_PARTIAL_INFORMATION Order;
    PBCD_KEPT_ELEMENT Element;
    PBCD_KEPT_ENTRY Entry;
    PWCHAR NewOrder;
    ULONG i, Chars, Used;
    NTSTATUS Status;

    ASSERT(Kept->Count != 0);
    ASSERT(Kept->Count <= BCD_MAX_KEPT_ENTRIES);

    for (i = 0; i < Kept->Count; i++)
    {
        Entry = &Kept->Entry[i];
        if (BcdObjectExists(Entry->Id))
        {
            Status = NewBcdObjectId(Entry->Id);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        Status = CreateBcdObject(Entry->Id, Entry->Type);
        if (!NT_SUCCESS(Status))
            return Status;

        for (Element = Entry->Elements; Element; Element = Element->Next)
        {
            Status = SetBcdElement(Entry->Id, Element->Name, Element->Type, Element->Data, Element->Size);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    Order = QueryBcdElement(BCD_BOOTMGR_OBJECT, BCD_DISPLAY_ORDER);
    if (!Order || Order->Type != REG_MULTI_SZ)
    {
        if (Order)
            RtlFreeHeap(ProcessHeap, 0, Order);
        return STATUS_REGISTRY_CORRUPT;
    }

    Used = Order->DataLength / sizeof(WCHAR);
    while (Used != 0 && ((PCWCH)Order->Data)[Used - 1] == UNICODE_NULL)
        Used--;
    Chars = Used + (Used != 0) + Kept->Count * (BCD_OBJECT_ID_CHARS + 1) + 1;
    NewOrder = RtlAllocateHeap(ProcessHeap, 0, Chars * sizeof(WCHAR));
    if (!NewOrder)
    {
        RtlFreeHeap(ProcessHeap, 0, Order);
        return STATUS_NO_MEMORY;
    }

    RtlCopyMemory(NewOrder, Order->Data, Used * sizeof(WCHAR));
    RtlFreeHeap(ProcessHeap, 0, Order);
    if (Used != 0)
        NewOrder[Used++] = UNICODE_NULL;
    for (i = 0; i < Kept->Count; i++)
    {
        ASSERT(wcslen(Kept->Entry[i].Id) == BCD_OBJECT_ID_CHARS);
        RtlCopyMemory(NewOrder + Used, Kept->Entry[i].Id, (BCD_OBJECT_ID_CHARS + 1) * sizeof(WCHAR));
        Used += BCD_OBJECT_ID_CHARS + 1;
    }
    NewOrder[Used++] = UNICODE_NULL;
    ASSERT(Used == Chars);

    Status = SetBcdElement(BCD_BOOTMGR_OBJECT, BCD_DISPLAY_ORDER, REG_MULTI_SZ, NewOrder, Used * sizeof(WCHAR));
    RtlFreeHeap(ProcessHeap, 0, NewOrder);
    return Status;
}

static
NTSTATUS
GetPartitionDeviceNumber(
    _In_ PCUNICODE_STRING RootPath,
    _Out_ PSTORAGE_DEVICE_NUMBER DeviceNumber)
{
    UNICODE_STRING RootPartition = *RootPath;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE Handle;
    NTSTATUS Status;

    TrimTrailingPathSeparators_UStr(&RootPartition);
    Status = pOpenDeviceEx_UStr(&RootPartition, &Handle, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                   IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                   NULL, 0, DeviceNumber, sizeof(*DeviceNumber));
    NtClose(Handle);
    return Status;
}

static
NTSTATUS
InstallBcdStore(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCWSTR TemplatePath,
    _In_ PCWSTR BootManagerPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ BOOLEAN PcAt)
{
    PCWSTR StoreDirectory = PcAt ? L"Boot" : L"EFI\\LiberNT\\Boot";
    PCWSTR BootManagerFile = PcAt ? L"bootmgr" : L"EFI\\LiberNT\\Boot\\bootmgfw.efi";
    static UNICODE_STRING MountPoint = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SetupBcd");
    OBJECT_ATTRIBUTES KeyAttributes, FileAttributes;
    UCHAR SystemElement[BCD_DEVICE_ELEMENT_SIZE];
    UCHAR OsElement[BCD_DEVICE_ELEMENT_SIZE];
    STORAGE_DEVICE_NUMBER DeviceNumber;
    UNICODE_STRING FileName;
    UNICODE_STRING NtPath;
    WCHAR StorePath[MAX_PATH];
    WCHAR Path[MAX_PATH];
    PCWSTR Text;
    ULONG Disk, Number, i;
    BCD_KEPT_ENTRIES Kept;
    BOOLEAN PrivilegeSet[2] = {FALSE, FALSE};
    NTSTATUS Status;

    Status = GetPartitionDeviceNumber(SystemRootPath, &DeviceNumber);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = GetBcdPartitionElement(DeviceNumber.DeviceNumber, DeviceNumber.PartitionNumber, FALSE, SystemElement);
    if (!NT_SUCCESS(Status))
        return Status;

    Text = DestinationArcPath->Buffer;
    Disk = DeviceNumber.DeviceNumber;
    Number = DeviceNumber.PartitionNumber;

    if (_wcsnicmp(Text, BCD_RELATIVE_PARTITION, ARRAYSIZE(BCD_RELATIVE_PARTITION) - 1) == 0)
    {
        Number = wcstoul(Text + ARRAYSIZE(BCD_RELATIVE_PARTITION) - 1, NULL, 10);
    }
    else if (_wcsnicmp(Text, BCD_SCSI_ADAPTER, ARRAYSIZE(BCD_SCSI_ADAPTER) - 1) == 0)
    {
        Status = GetScsiArcPathPartition(Text, &Disk, &Number);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else if (*Text != UNICODE_NULL && *Text != OBJ_NAME_PATH_SEPARATOR)
    {
        RtlInitEmptyUnicodeString(&NtPath, Path, sizeof(Path));
        if (!ArcPathToNtPath(&NtPath, Text, NULL))
            return STATUS_OBJECT_PATH_NOT_FOUND;

        Text = wcsstr(Path, BCD_DISK_PREFIX);
        if (!Text)
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        Disk = wcstoul(Text + ARRAYSIZE(BCD_DISK_PREFIX) - 1, NULL, 10);

        Text = wcsstr(Text, BCD_PARTITION_PREFIX);
        if (!Text)
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        Number = wcstoul(Text + ARRAYSIZE(BCD_PARTITION_PREFIX) - 1, NULL, 10);
    }

    Status = GetBcdPartitionElement(Disk, Number, *DestinationArcPath->Buffer != UNICODE_NULL && *DestinationArcPath->Buffer != OBJ_NAME_PATH_SEPARATOR, OsElement);
    if (!NT_SUCCESS(Status))
        return Status;

    CombinePaths(StorePath, ARRAYSIZE(StorePath), 3, SystemRootPath->Buffer, StoreDirectory, L"BCD");
    RtlInitUnicodeString(&FileName, StorePath);
    InitializeObjectAttributes(&KeyAttributes, &MountPoint, OBJ_CASE_INSENSITIVE, NULL, NULL);
    InitializeObjectAttributes(&FileAttributes, &FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &PrivilegeSet[0]);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &PrivilegeSet[1]);
    if (NT_SUCCESS(Status))
    {
        /* The store of another LiberNT installation keeps its entries; any other store is replaced */
        Kept.Count = 0;
        if (DoesFileExist(NULL, StorePath) &&
            (!PcAt || GetPcatBootManagerOwner(SystemRootPath->Buffer) == BootManagerLiberNT) &&
            NT_SUCCESS(NtLoadKey(&KeyAttributes, &FileAttributes)))
        {
            KeepOtherBcdEntries(OsElement, &Kept);
            NtUnloadKey(&KeyAttributes);
        }

        CombinePaths(Path, ARRAYSIZE(Path), 2, SystemRootPath->Buffer, StoreDirectory);
        Status = SetupCreateDirectory(Path);
        if (Status == STATUS_OBJECT_NAME_COLLISION)
            Status = STATUS_SUCCESS;

        if (NT_SUCCESS(Status))
        {
            CombinePaths(Path, ARRAYSIZE(Path), 2, SystemRootPath->Buffer, BootManagerFile);
            Status = SetupCopyFile(BootManagerPath, Path, FALSE);
        }
        if (NT_SUCCESS(Status))
            Status = SetupCopyFile(TemplatePath, StorePath, FALSE);
        if (NT_SUCCESS(Status))
            Status = NtLoadKey(&KeyAttributes, &FileAttributes);
        if (NT_SUCCESS(Status))
        {
            Status = SetBcdElement(BCD_BOOTMGR_OBJECT, BCD_APPLICATION_DEVICE, REG_BINARY,
                                   SystemElement, sizeof(SystemElement));
            if (NT_SUCCESS(Status))
                Status = SetBcdElement(BCD_OSLOADER_OBJECT, BCD_APPLICATION_DEVICE, REG_BINARY,
                                       OsElement, sizeof(OsElement));
            if (NT_SUCCESS(Status))
                Status = SetBcdElement(BCD_OSLOADER_OBJECT, BCD_OS_DEVICE, REG_BINARY,
                                       OsElement, sizeof(OsElement));
            if (NT_SUCCESS(Status) && PcAt)
                Status = SetBcdElement(BCD_OSLOADER_OBJECT, BCD_APPLICATION_PATH, REG_SZ,
                                       PCAT_OS_LOADER_PATH, sizeof(PCAT_OS_LOADER_PATH));
            if (NT_SUCCESS(Status) && PcAt)
                Status = DeleteBcdElement(BCD_BOOTMGR_OBJECT, BCD_APPLICATION_PATH);
            if (NT_SUCCESS(Status) && Kept.Count != 0)
                Status = RestoreBcdEntries(&Kept);

            NtUnloadKey(&KeyAttributes);
        }

        for (i = 0; i < Kept.Count; i++)
            FreeBcdKeptEntry(&Kept.Entry[i]);

        RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, PrivilegeSet[1], FALSE, &PrivilegeSet[1]);
    }

    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, PrivilegeSet[0], FALSE, &PrivilegeSet[0]);
    return Status;
}

static
NTSTATUS
ReadDiskBytes(
    _In_ HANDLE Handle,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PVOID* Buffer)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    NTSTATUS Status;

    *Buffer = RtlAllocateHeap(ProcessHeap, 0, Length);
    if (!*Buffer)
        return STATUS_NO_MEMORY;

    FileOffset.QuadPart = Offset;
    Status = NtReadFile(Handle, NULL, NULL, NULL, &IoStatusBlock, *Buffer, Length, &FileOffset, NULL);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, *Buffer);
        *Buffer = NULL;
    }

    return Status;
}

static
NTSTATUS
GetFirmwarePartitionNode(
    _In_ ULONG DiskNumber,
    _In_ ULONG PartitionNumber,
    _Out_writes_bytes_(EFI_HARDDRIVE_NODE_SIZE) PUCHAR Node)
{
    union
    {
        DRIVE_LAYOUT_INFORMATION_EX Layout;
        UCHAR Buffer[FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + 128 * sizeof(PARTITION_INFORMATION_EX)];
    } Drive;
    PARTITION_INFORMATION_EX Partition;
    DISK_GEOMETRY Geometry;
    IO_STATUS_BLOCK IoStatusBlock;
    WCHAR Path[MAX_PATH];
    PUCHAR Header = NULL, Entries = NULL;
    ULONGLONG EntriesLba;
    ULONG EntryCount, EntrySize, Slot = 0, i;
    HANDLE Handle;
    NTSTATUS Status;

    RtlStringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Device\\Harddisk%lu\\Partition%lu", DiskNumber, PartitionNumber);
    Status = pOpenDevice(Path, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                   IOCTL_DISK_GET_PARTITION_INFO_EX,
                                   NULL, 0, &Partition, sizeof(Partition));
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlStringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Device\\Harddisk%lu\\Partition0", DiskNumber);
    Status = pOpenDevice(Path, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL, 0, &Geometry, sizeof(Geometry));
    if (NT_SUCCESS(Status))
    {
        Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                       IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                       NULL, 0, &Drive, sizeof(Drive));
    }
    if (NT_SUCCESS(Status) && Geometry.BytesPerSector == 0)
        Status = STATUS_INVALID_DEVICE_STATE;
    if (NT_SUCCESS(Status))
        Status = ReadDiskBytes(Handle, Partition.PartitionStyle == PARTITION_STYLE_GPT ? Geometry.BytesPerSector : 0,
                               Geometry.BytesPerSector, (PVOID*)&Header);

    if (NT_SUCCESS(Status) && Partition.PartitionStyle == PARTITION_STYLE_GPT)
    {
        EntriesLba = *(PULONGLONG)(Header + GPT_HEADER_ENTRIES_LBA_OFFSET);
        EntryCount = *(PULONG)(Header + GPT_HEADER_ENTRY_COUNT_OFFSET);
        EntrySize = *(PULONG)(Header + GPT_HEADER_ENTRY_SIZE_OFFSET);
        if (EntrySize < GPT_ENTRY_MIN_SIZE || EntryCount == 0 || EntryCount > GPT_ENTRY_MAX_COUNT)
            Status = STATUS_DISK_CORRUPT_ERROR;
        else
            Status = ReadDiskBytes(Handle, EntriesLba * Geometry.BytesPerSector,
                                   ALIGN_UP_BY(EntryCount * EntrySize, Geometry.BytesPerSector),
                                   (PVOID*)&Entries);

        for (i = 0; NT_SUCCESS(Status) && i < EntryCount && Slot == 0; i++)
        {
            if (RtlEqualMemory(Entries + i * EntrySize + GPT_ENTRY_PARTITION_ID_OFFSET,
                               &Partition.Gpt.PartitionId, sizeof(GUID)))
            {
                Slot = i + 1;
            }
        }
    }
    else if (NT_SUCCESS(Status))
    {
        for (i = 0; i < 4 && Slot == 0; i++)
        {
            if ((ULONGLONG)*(PULONG)(Header + MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE +
                                     MBR_PARTITION_START_OFFSET) * Geometry.BytesPerSector ==
                (ULONGLONG)Partition.StartingOffset.QuadPart)
            {
                Slot = i + 1;
            }
        }
    }

    NtClose(Handle);
    if (Entries)
        RtlFreeHeap(ProcessHeap, 0, Entries);
    if (Header)
        RtlFreeHeap(ProcessHeap, 0, Header);

    if (NT_SUCCESS(Status) && Slot == 0)
        Status = STATUS_NOT_FOUND;
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Node, EFI_HARDDRIVE_NODE_SIZE);
    Node[0] = EFI_MEDIA_DEVICE_PATH;
    Node[1] = EFI_MEDIA_HARDDRIVE_DP;
    *(PUSHORT)(Node + 2) = EFI_HARDDRIVE_NODE_SIZE;
    *(PULONG)(Node + EFI_HARDDRIVE_NUMBER_OFFSET) = Slot;
    *(PULONGLONG)(Node + EFI_HARDDRIVE_START_OFFSET) = Partition.StartingOffset.QuadPart / Geometry.BytesPerSector;
    *(PULONGLONG)(Node + EFI_HARDDRIVE_SIZE_OFFSET) = Partition.PartitionLength.QuadPart / Geometry.BytesPerSector;

    if (Partition.PartitionStyle == PARTITION_STYLE_GPT)
    {
        RtlCopyMemory(Node + EFI_HARDDRIVE_SIGNATURE_OFFSET, &Partition.Gpt.PartitionId, sizeof(GUID));
        Node[EFI_HARDDRIVE_FORMAT_OFFSET] = EFI_PARTITION_FORMAT_GPT;
        Node[EFI_HARDDRIVE_TYPE_OFFSET] = EFI_SIGNATURE_TYPE_GUID;
    }
    else
    {
        *(PULONG)(Node + EFI_HARDDRIVE_SIGNATURE_OFFSET) = Drive.Layout.Mbr.Signature;
        Node[EFI_HARDDRIVE_FORMAT_OFFSET] = EFI_PARTITION_FORMAT_MBR;
        Node[EFI_HARDDRIVE_TYPE_OFFSET] = EFI_SIGNATURE_TYPE_MBR;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
QueryFirmwareVariable(
    _In_ PCWSTR Name,
    _Out_writes_bytes_opt_(*Length) PVOID Value,
    _Inout_ PULONG Length)
{
    UNICODE_STRING VariableName;

    RtlInitUnicodeString(&VariableName, Name);
    return NtQuerySystemEnvironmentValueEx(&VariableName, &EfiGlobalVariableGuid, Value, Length, NULL);
}

static
NTSTATUS
SetFirmwareVariable(
    _In_ PCWSTR Name,
    _In_reads_bytes_(Length) PVOID Value,
    _In_ ULONG Length)
{
    UNICODE_STRING VariableName;

    RtlInitUnicodeString(&VariableName, Name);
    return NtSetSystemEnvironmentValueEx(&VariableName, &EfiGlobalVariableGuid, Value, Length,
                                         EFI_VARIABLE_BOOT_ATTRIBUTES);
}

static
BOOLEAN
IsSameLoadOption(
    _In_reads_bytes_(OptionLength) const UCHAR* Option,
    _In_ ULONG OptionLength,
    _In_reads_bytes_(FilePathLength) const UCHAR* FilePath,
    _In_ USHORT FilePathLength)
{
    ULONG Offset = EFI_LOAD_OPTION_HEADER_SIZE;

    if (OptionLength < EFI_LOAD_OPTION_HEADER_SIZE ||
        *(const USHORT*)(Option + sizeof(ULONG)) != FilePathLength)
    {
        return FALSE;
    }

    while (Offset + sizeof(WCHAR) <= OptionLength && *(const WCHAR*)(Option + Offset) != UNICODE_NULL)
        Offset += sizeof(WCHAR);
    Offset += sizeof(WCHAR);

    return Offset + FilePathLength <= OptionLength &&
           RtlEqualMemory(Option + Offset, FilePath, FilePathLength);
}

static
BOOLEAN
IsLoadOptionForFile(
    _In_reads_bytes_(OptionLength) const UCHAR* Option,
    _In_ ULONG OptionLength,
    _In_ PCWSTR File)
{
    ULONG Offset = EFI_LOAD_OPTION_HEADER_SIZE, End, NodeLength, Chars;

    if (OptionLength < EFI_LOAD_OPTION_HEADER_SIZE)
        return FALSE;

    while (Offset + sizeof(WCHAR) <= OptionLength && *(const WCHAR*)(Option + Offset) != UNICODE_NULL)
        Offset += sizeof(WCHAR);
    Offset += sizeof(WCHAR);

    End = Offset + *(const USHORT*)(Option + sizeof(ULONG));
    if (End > OptionLength)
        return FALSE;

    while (Offset + EFI_DEVICE_PATH_HEADER_SIZE <= End && Option[Offset] != EFI_END_DEVICE_PATH_TYPE)
    {
        NodeLength = *(const USHORT*)(Option + Offset + 2);
        if (NodeLength < EFI_DEVICE_PATH_HEADER_SIZE || Offset + NodeLength > End)
            return FALSE;

        if (Option[Offset] == EFI_MEDIA_DEVICE_PATH && Option[Offset + 1] == EFI_MEDIA_FILEPATH_DP)
        {
            Chars = (NodeLength - EFI_DEVICE_PATH_HEADER_SIZE) / sizeof(WCHAR);
            while (Chars && ((const WCHAR*)(Option + Offset + EFI_DEVICE_PATH_HEADER_SIZE))[Chars - 1] == UNICODE_NULL)
                Chars--;
            if (Chars == wcslen(File) &&
                _wcsnicmp((const WCHAR*)(Option + Offset + EFI_DEVICE_PATH_HEADER_SIZE), File, Chars) == 0)
            {
                return TRUE;
            }
        }
        Offset += NodeLength;
    }

    return FALSE;
}

static
NTSTATUS
InstallFirmwareBootEntry(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ ULONG_PTR Options)
{
    BOOLEAN AfterWindows = !!(Options & BOOT_OPTION_AFTER_WINDOWS);
    STORAGE_DEVICE_NUMBER DeviceNumber;
    UCHAR Option[EFI_LOAD_OPTION_MAX];
    UCHAR Existing[EFI_LOAD_OPTION_MAX];
    USHORT Order[EFI_BOOT_ORDER_MAX / sizeof(USHORT) + 1];
    PUCHAR FilePath, Node;
    WCHAR Name[16];
    ULONG DescriptionSize, FileSize, OptionSize, Length, OrderCount, Number = 0, Position = 0, i, j;
    USHORT FilePathLength, Next;
    BOOLEAN Found = FALSE, PrivilegeSet = FALSE;
    NTSTATUS Status, NextStatus;

    Status = GetPartitionDeviceNumber(SystemRootPath, &DeviceNumber);
    if (!NT_SUCCESS(Status))
        return Status;

    DescriptionSize = sizeof(EFI_BOOT_MANAGER_DESCRIPTION);
    FileSize = sizeof(EFI_BOOT_MANAGER_FILE);
    FilePathLength = (USHORT)(EFI_HARDDRIVE_NODE_SIZE + EFI_DEVICE_PATH_HEADER_SIZE + FileSize +
                              EFI_DEVICE_PATH_HEADER_SIZE);
    OptionSize = EFI_LOAD_OPTION_HEADER_SIZE + DescriptionSize + FilePathLength;

    RtlZeroMemory(Option, sizeof(Option));
    *(PULONG)Option = EFI_LOAD_OPTION_ACTIVE;
    *(PUSHORT)(Option + sizeof(ULONG)) = FilePathLength;
    RtlCopyMemory(Option + EFI_LOAD_OPTION_HEADER_SIZE, EFI_BOOT_MANAGER_DESCRIPTION, DescriptionSize);

    FilePath = Option + EFI_LOAD_OPTION_HEADER_SIZE + DescriptionSize;
    Status = GetFirmwarePartitionNode(DeviceNumber.DeviceNumber, DeviceNumber.PartitionNumber, FilePath);
    if (!NT_SUCCESS(Status))
        return Status;

    Node = FilePath + EFI_HARDDRIVE_NODE_SIZE;
    Node[0] = EFI_MEDIA_DEVICE_PATH;
    Node[1] = EFI_MEDIA_FILEPATH_DP;
    *(PUSHORT)(Node + 2) = (USHORT)(EFI_DEVICE_PATH_HEADER_SIZE + FileSize);
    RtlCopyMemory(Node + EFI_DEVICE_PATH_HEADER_SIZE, EFI_BOOT_MANAGER_FILE, FileSize);

    Node += EFI_DEVICE_PATH_HEADER_SIZE + FileSize;
    Node[0] = EFI_END_DEVICE_PATH_TYPE;
    Node[1] = EFI_END_ENTIRE_DEVICE_PATH;
    *(PUSHORT)(Node + 2) = EFI_DEVICE_PATH_HEADER_SIZE;

    Status = RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &PrivilegeSet);
    if (!NT_SUCCESS(Status))
        return Status;

    Length = EFI_BOOT_ORDER_MAX;
    Status = QueryFirmwareVariable(L"BootOrder", Order, &Length);
    if (Status == STATUS_VARIABLE_NOT_FOUND)
    {
        Length = 0;
        Status = STATUS_SUCCESS;
    }
    OrderCount = Length / sizeof(USHORT);

    for (i = 0; NT_SUCCESS(Status) && i < OrderCount && !Found; i++)
    {
        RtlStringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Order[i]);
        Length = sizeof(Existing);
        if (NT_SUCCESS(QueryFirmwareVariable(Name, Existing, &Length)) &&
            IsSameLoadOption(Existing, Length, FilePath, FilePathLength))
        {
            Number = Order[i];
            Found = TRUE;
        }
    }

    if (NT_SUCCESS(Status) && !Found)
    {
        for (Number = 0; Number < EFI_BOOT_OPTION_SEARCH_MAX; Number++)
        {
            RtlStringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Number);
            Length = 0;
            if (QueryFirmwareVariable(Name, NULL, &Length) == STATUS_VARIABLE_NOT_FOUND)
                break;
        }

        if (Number >= EFI_BOOT_OPTION_SEARCH_MAX)
            Status = STATUS_NO_MORE_ENTRIES;
    }

    if (NT_SUCCESS(Status))
    {
        RtlStringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Number);
        Status = SetFirmwareVariable(Name, Option, OptionSize);
    }

    if (NT_SUCCESS(Status) && !(Found && (Options & BOOT_OPTION_KEEP_ORDER)))
    {
        for (i = 0, j = 0; i < OrderCount; i++)
        {
            if (Order[i] != Number)
                Order[j++] = Order[i];
        }
        for (i = 0; AfterWindows && i < j; i++)
        {
            RtlStringCchPrintfW(Name, ARRAYSIZE(Name), L"Boot%04X", Order[i]);
            Length = sizeof(Existing);
            if (NT_SUCCESS(QueryFirmwareVariable(Name, Existing, &Length)) &&
                IsLoadOptionForFile(Existing, Length, EFI_WINDOWS_BOOT_MANAGER_FILE))
            {
                Position = i + 1;
                break;
            }
        }
        RtlMoveMemory(&Order[Position + 1], &Order[Position], (j - Position) * sizeof(USHORT));
        Order[Position] = (USHORT)Number;
        Status = SetFirmwareVariable(L"BootOrder", Order, (j + 1) * sizeof(USHORT));
    }

    if (NT_SUCCESS(Status) && AfterWindows)
    {
        Next = (USHORT)Number;
        NextStatus = SetFirmwareVariable(L"BootNext", &Next, sizeof(Next));
        DPRINT1("Boot%04X starts once after Setup: 0x%08lx\n", Next, NextStatus);
    }

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, PrivilegeSet, FALSE, &PrivilegeSet);
    return Status;
}

NTSTATUS
NTAPI
RemoveHostSetupBoot(
    _In_ PCUNICODE_STRING SystemRootPath)
{
    static const PCWSTR Files[] = { L"bootmgfw.efi", L"winload.efi", L"BCD" };
    UCHAR Option[EFI_LOAD_OPTION_MAX];
    USHORT Order[EFI_BOOT_ORDER_MAX / sizeof(USHORT) + 1];
    FILE_DISPOSITION_INFORMATION Disposition;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;
    WCHAR Variable[16], Path[MAX_PATH];
    ULONG Length, OrderCount = 0, Number, i, j;
    BOOLEAN PrivilegeSet = FALSE, OrderChanged = FALSE;
    HANDLE Directory;
    NTSTATUS Status, Result = STATUS_SUCCESS;

    Status = RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &PrivilegeSet);
    if (NT_SUCCESS(Status))
    {
        Length = EFI_BOOT_ORDER_MAX;
        if (NT_SUCCESS(QueryFirmwareVariable(L"BootOrder", Order, &Length)))
            OrderCount = Length / sizeof(USHORT);

        for (Number = 0; Number < EFI_BOOT_OPTION_SEARCH_MAX; Number++)
        {
            RtlStringCchPrintfW(Variable, ARRAYSIZE(Variable), L"Boot%04X", Number);
            Length = sizeof(Option);
            if (!NT_SUCCESS(QueryFirmwareVariable(Variable, Option, &Length)) ||
                !IsLoadOptionForFile(Option, Length, EFI_HOST_SETUP_MANAGER_FILE))
            {
                continue;
            }

            Status = SetFirmwareVariable(Variable, NULL, 0);
            DPRINT1("Firmware option %S of Setup started on Windows removed: 0x%08lx\n", Variable, Status);
            if (!NT_SUCCESS(Status) && NT_SUCCESS(Result))
                Result = Status;
            for (i = 0, j = 0; i < OrderCount; i++)
            {
                if (Order[i] != Number)
                    Order[j++] = Order[i];
            }
            OrderChanged = OrderChanged || j != OrderCount;
            OrderCount = j;
        }

        if (OrderChanged)
        {
            Status = SetFirmwareVariable(L"BootOrder", Order, OrderCount * sizeof(USHORT));
            if (!NT_SUCCESS(Status) && NT_SUCCESS(Result))
                Result = Status;
        }
        RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, PrivilegeSet, FALSE, &PrivilegeSet);
    }
    else
    {
        Result = Status;
    }

    for (i = 0; i < ARRAYSIZE(Files); i++)
    {
        CombinePaths(Path, ARRAYSIZE(Path), 3, SystemRootPath->Buffer, EFI_HOST_SETUP_DIRECTORY, Files[i]);
        Status = SetupDeleteFile(Path, TRUE);
        if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND &&
            Status != STATUS_OBJECT_PATH_NOT_FOUND && NT_SUCCESS(Result))
        {
            Result = Status;
        }
    }

    CombinePaths(Path, ARRAYSIZE(Path), 2, SystemRootPath->Buffer, EFI_HOST_SETUP_DIRECTORY);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Directory, DELETE | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    if (NT_SUCCESS(Status))
    {
        Disposition.DeleteFile = TRUE;
        Status = NtSetInformationFile(Directory, &IoStatusBlock, &Disposition, sizeof(Disposition),
                                      FileDispositionInformation);
        NtClose(Directory);
    }
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND &&
        Status != STATUS_OBJECT_PATH_NOT_FOUND && NT_SUCCESS(Result))
    {
        Result = Status;
    }
    return Result;
}

static
BOOLEAN
InstallBcdFromTemplate(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ ULONG_PTR Options,
    _Out_opt_ PBOOLEAN FirmwareEntry,
    _Out_ PNTSTATUS Status)
{
    static UNICODE_STRING Pattern = RTL_CONSTANT_STRING(BCD_BOOT_MANAGER_PATTERN);
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;
    WCHAR TemplatePath[MAX_PATH];
    WCHAR SrcDir[MAX_PATH];
    WCHAR BootManagerPath[MAX_PATH];
    WCHAR FileName[MAX_PATH];
    HANDLE DirectoryHandle;
    NTSTATUS EntryStatus;
    union
    {
        FILE_DIRECTORY_INFORMATION Info;
        UCHAR Buffer[sizeof(FILE_DIRECTORY_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    } Entry;

    *Status = STATUS_SUCCESS;
    if (FirmwareEntry)
        *FirmwareEntry = FALSE;

    CombinePaths(TemplatePath, ARRAYSIZE(TemplatePath), 2, SourceRootPath->Buffer, BCD_TEMPLATE_PATH);
    if (!DoesFileExist(NULL, TemplatePath))
        return FALSE;

    GetEfiLoaderSourceDir(SourceRootPath, SrcDir, ARRAYSIZE(SrcDir));
    RtlInitUnicodeString(&Name, SrcDir);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    *Status = NtOpenFile(&DirectoryHandle,
                         FILE_LIST_DIRECTORY | SYNCHRONIZE,
                         &ObjectAttributes,
                         &IoStatusBlock,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(*Status))
        return TRUE;

    *Status = NtQueryDirectoryFile(DirectoryHandle, NULL, NULL, NULL, &IoStatusBlock,
                                   &Entry, sizeof(Entry), FileDirectoryInformation,
                                   TRUE, &Pattern, TRUE);
    NtClose(DirectoryHandle);
    if (!NT_SUCCESS(*Status))
        return TRUE;

    RtlStringCchCopyNW(FileName, ARRAYSIZE(FileName),
                       Entry.Info.FileName,
                       Entry.Info.FileNameLength / sizeof(WCHAR));
    CombinePaths(BootManagerPath, ARRAYSIZE(BootManagerPath), 2, SrcDir, FileName);

    *Status = InstallBcdStore(SystemRootPath, TemplatePath, BootManagerPath, DestinationArcPath, FALSE);
    if (!NT_SUCCESS(*Status))
    {
        DPRINT1("Writing the boot configuration data failed (Status %lx)\n", *Status);
        return TRUE;
    }

    EntryStatus = InstallFirmwareBootEntry(SystemRootPath, Options);
    if (!NT_SUCCESS(EntryStatus))
        DPRINT1("Writing the firmware boot entry failed (Status %lx)\n", EntryStatus);
    else if (FirmwareEntry)
        *FirmwareEntry = TRUE;
    return TRUE;
}

static
NTSTATUS
InstallEfiSystemPartition(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ ULONG_PTR Options)
{
    NTSTATUS Status;
    WCHAR SrcDir[MAX_PATH];
    WCHAR TemplatePath[MAX_PATH];
    BOOLEAN FallbackKept, FirmwareEntry;

    GetEfiLoaderSourceDir(SourceRootPath, SrcDir, ARRAYSIZE(SrcDir));
    if (!DoesDirExist(NULL, SrcDir))
    {
        DPRINT1("No EFI loader in %S\n", SrcDir);
        return STATUS_NOT_FOUND;
    }

    CombinePaths(TemplatePath, ARRAYSIZE(TemplatePath), 2, SourceRootPath->Buffer, BCD_TEMPLATE_PATH);
    Status = InstallEfiLoaderFiles(SystemRootPath, SourceRootPath,
                                   DoesFileExist(NULL, TemplatePath), &FallbackKept);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallEfiLoaderFiles() failed (Status %lx)\n", Status);
        return Status;
    }

    if (InstallBcdFromTemplate(SystemRootPath, SourceRootPath, DestinationArcPath, Options,
                               &FirmwareEntry, &Status))
    {
        if (NT_SUCCESS(Status) && FallbackKept && !FirmwareEntry)
            Status = InstallEfiLoaderFiles(SystemRootPath, SourceRootPath, FALSE, NULL);
        return Status;
    }

    DPRINT1("No boot configuration template in %wZ\n", SourceRootPath);
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
InstallPcatBootManager(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath)
{
    WCHAR TemplatePath[MAX_PATH];
    WCHAR BootManagerPath[MAX_PATH];

    CombinePaths(TemplatePath, ARRAYSIZE(TemplatePath), 2, SourceRootPath->Buffer, BCD_TEMPLATE_PATH);
    CombinePaths(BootManagerPath, ARRAYSIZE(BootManagerPath), 3,
                 SourceRootPath->Buffer, PCAT_BOOT_SOURCE_DIR, L"bootmgr");
    if (!DoesFileExist(NULL, TemplatePath) || !DoesFileExist(NULL, BootManagerPath))
    {
        DPRINT1("No PC/AT boot manager or boot configuration template in %wZ\n", SourceRootPath);
        return STATUS_NOT_FOUND;
    }

    if (GetPcatBootManagerOwner(SystemRootPath->Buffer) == BootManagerForeign)
    {
        DPRINT1("%wZ holds another system's boot manager, which stays in place\n", SystemRootPath);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    return InstallBcdStore(SystemRootPath, TemplatePath, BootManagerPath, DestinationArcPath, TRUE);
}

static
NTSTATUS
InstallVBRToPartition(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCWSTR FileSystemName)
{
    WCHAR SrcPath[MAX_PATH];

    if (_wcsicmp(FileSystemName, L"FAT32") == 0)
    {
        CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 3, SourceRootPath->Buffer, PCAT_BOOT_SOURCE_DIR, L"fat32.bin");
        DPRINT1("Install FAT32 bootcode: %S ==> %wZ\n", SrcPath, SystemRootPath);
        return InstallBootCodeToDisk(SrcPath, SystemRootPath->Buffer, InstallFat32BootCode);
    }

    if (_wcsicmp(FileSystemName, L"NTFS") == 0)
    {
        CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 3, SourceRootPath->Buffer, PCAT_BOOT_SOURCE_DIR, L"ntfs.bin");
        DPRINT1("Install NTFS bootcode: %S ==> %wZ\n", SrcPath, SystemRootPath);
        return InstallBootCodeToDisk(SrcPath, SystemRootPath->Buffer, InstallNtfsBootCode);
    }

    /* TODO: FAT12/FAT16 and exFAT volume boot records that start \bootmgr */
    DPRINT1("No volume boot record for file system '%S'\n", FileSystemName);
    return STATUS_NOT_SUPPORTED;
}


/* GENERIC FUNCTIONS *********************************************************/

/**
 * @brief
 * Helper for InstallBootManagerAndBootEntries().
 *
 * @param[in]   ArchType
 * @param[in]   SystemRootPath
 * See InstallBootManagerAndBootEntries() parameters.
 *
 * @param[in]   DiskNumber
 * The NT disk number of the system disk that contains the system partition.
 *
 * @param[in]   DiskStyle
 * The partitioning style of the system disk.
 *
 * @param[in]   IsSuperFloppy
 * Whether the system disk is a super-floppy.
 *
 * @param[in]   FileSystem
 * The file system of the system partition.
 *
 * @param[in]   SourceRootPath
 * @param[in]   DestinationArcPath
 * @param[in]   Options
 * See InstallBootManagerAndBootEntries() parameters.
 *
 * @return  An NTSTATUS code indicating success or failure.
 **/
static
NTSTATUS
InstallBootManagerAndBootEntriesWorker(
    _In_ ARCHITECTURE_TYPE ArchType,
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ ULONG DiskNumber, // const STORAGE_DEVICE_NUMBER* DeviceNumber,
    _In_ PARTITION_STYLE DiskStyle,
    _In_ BOOLEAN IsSuperFloppy,
    _In_ PCWSTR FileSystem,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ ULONG_PTR Options)
{
    NTSTATUS Status;
    BOOLEAN IsBIOS = ((ArchType == ARCH_PcAT) || (ArchType == ARCH_NEC98x86));
    UCHAR InstallType = (Options & 0x03);

    if (DiskStyle == PARTITION_STYLE_GPT || IsUefiFirmware())
        return InstallEfiSystemPartition(SystemRootPath, SourceRootPath, DestinationArcPath, Options);

    if (!IsBIOS)
        return STATUS_NOT_SUPPORTED;

    /* TODO: removable media without a partition table (install type 2) */
    if (InstallType > 1)
        return STATUS_NOT_SUPPORTED;

    Status = InstallPcatBootManager(SystemRootPath, SourceRootPath, DestinationArcPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallPcatBootManager() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    if (_wcsicmp(FileSystem, L"FAT") == 0 || _wcsicmp(FileSystem, L"FAT32") == 0)
    {
        Status = InstallEfiLoaderFiles(SystemRootPath, SourceRootPath, FALSE, NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("InstallEfiLoaderFiles() failed (Status %lx)\n", Status);
            return Status;
        }

        InstallBcdFromTemplate(SystemRootPath, SourceRootPath, DestinationArcPath, 0, NULL, &Status);
    }

    Status = InstallVBRToPartition(SystemRootPath, SourceRootPath, FileSystem);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallVBRToPartition() failed (Status 0x%08lx)\n", Status);
        return ERROR_WRITE_BOOT; // Status; STATUS_BAD_MASTER_BOOT_RECORD;
    }

    if ((InstallType == 1) && (DiskStyle == PARTITION_STYLE_MBR) && !IsSuperFloppy)
    {
        WCHAR SystemDiskPath[MAX_PATH];
        RtlStringCchPrintfW(SystemDiskPath, _countof(SystemDiskPath),
                            L"\\Device\\Harddisk%d\\Partition0",
                            DiskNumber);
        Status = InstallMbrBootCodeToDisk(SourceRootPath, SystemDiskPath);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("InstallMbrBootCodeToDisk() failed (Status 0x%08lx)\n", Status);
            return ERROR_INSTALL_BOOTCODE; // Status; STATUS_BAD_MASTER_BOOT_RECORD;
        }
    }

    return Status;
}


NTSTATUS
GetDeviceInfo_UStr(
    _In_opt_ PCUNICODE_STRING DeviceName,
    _In_opt_ HANDLE DeviceHandle,
    _Out_ PFILE_FS_DEVICE_INFORMATION DeviceInfo)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    if (DeviceName && DeviceHandle)
        return STATUS_INVALID_PARAMETER_MIX;

    /* Open the device if a name has been given;
     * otherwise just use the provided handle. */
    if (DeviceName)
    {
        Status = pOpenDeviceEx_UStr(DeviceName, &DeviceHandle,
                                    FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot open device '%wZ' (Status 0x%08lx)\n",
                    DeviceName, Status);
            return Status;
        }
    }

    /* Query the device */
    Status = NtQueryVolumeInformationFile(DeviceHandle,
                                          &IoStatusBlock,
                                          DeviceInfo,
                                          sizeof(*DeviceInfo),
                                          FileFsDeviceInformation);
    if (!NT_SUCCESS(Status))
        DPRINT1("FileFsDeviceInformation failed (Status 0x%08lx)\n", Status);

    /* Close the device if we've opened it */
    if (DeviceName)
        NtClose(DeviceHandle);

    return Status;
}

NTSTATUS
GetDeviceInfo(
    _In_opt_ PCWSTR DeviceName,
    _In_opt_ HANDLE DeviceHandle,
    _Out_ PFILE_FS_DEVICE_INFORMATION DeviceInfo)
{
    UNICODE_STRING DeviceNameU;

    if (DeviceName && DeviceHandle)
        return STATUS_INVALID_PARAMETER_MIX;

    if (DeviceName)
        RtlInitUnicodeString(&DeviceNameU, DeviceName);

    return GetDeviceInfo_UStr(DeviceName ? &DeviceNameU : NULL,
                              DeviceName ? NULL : DeviceHandle,
                              DeviceInfo);
}


/**
 * @brief
 * Installs the boot manager on the system partition and configures the boot entries.
 *
 * @todo
 * Split this function into just the InstallBootManager, and a separate one
 * for just the boot entries.
 *
 * @param[in]   ArchType
 * The target architecture.
 *
 * @param[in]   SystemRootPath
 * The system partition path, where the boot manager and its BCD store
 * are saved to.
 *
 * @param[in]   SourceRootPath
 * The installation source, where to copy the boot manager from.
 *
 * @param[in]   DestinationArcPath
 * The ReactOS installation path in ARC format.
 *
 * @param[in]   Options
 * For BIOS-based PCs:
 * LOBYTE:
 *      0: Install only on VBR;
 *      1: Install on both VBR and MBR.
 *
 * @return  An NTSTATUS code indicating success or failure.
 **/
NTSTATUS
NTAPI
InstallBootManagerAndBootEntries(
    _In_ ARCHITECTURE_TYPE ArchType,
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ ULONG_PTR Options)
{
    NTSTATUS Status;
    HANDLE DeviceHandle;
    FILE_FS_DEVICE_INFORMATION DeviceInfo;
    ULONG DiskNumber;
    PARTITION_STYLE PartitionStyle;
    BOOLEAN IsSuperFloppy;
    WCHAR FileSystem[MAX_PATH+1];

    /* Remove any trailing backslash if needed */
    UNICODE_STRING RootPartition = *SystemRootPath;
    TrimTrailingPathSeparators_UStr(&RootPartition);

    /* Open the volume */
    Status = pOpenDeviceEx_UStr(&RootPartition, &DeviceHandle,
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot open %wZ for bootloader installation (Status 0x%08lx)\n",
                &RootPartition, Status);
        return Status;
    }

    /* Retrieve the volume file system (it will also be mounted) */
    Status = GetFileSystemName_UStr(NULL, DeviceHandle,
                                    FileSystem, sizeof(FileSystem));
    if (!NT_SUCCESS(Status) || !*FileSystem)
    {
        DPRINT1("GetFileSystemName() failed (Status 0x%08lx)\n", Status);
        goto Quit;
    }

    /* Retrieve the device type and characteristics */
    Status = GetDeviceInfo_UStr(NULL, DeviceHandle, &DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("FileFsDeviceInformation failed (Status 0x%08lx)\n", Status);
        goto Quit;
    }

    /* Ignore volumes that are NOT on usual disks */
    if (DeviceInfo.DeviceType != FILE_DEVICE_DISK /*&&
        DeviceInfo.DeviceType != FILE_DEVICE_VIRTUAL_DISK*/)
    {
        DPRINT1("Invalid volume; device type %lu\n", DeviceInfo.DeviceType);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Quit;
    }


    /* Check whether this is a floppy or a partitionable device */
    if (DeviceInfo.Characteristics & FILE_FLOPPY_DISKETTE)
    {
        /* Floppies don't have partitions */
        // NOTE: See ntoskrnl/io/iomgr/rawfs.c!RawQueryFsSizeInfo()
        DiskNumber = ULONG_MAX;
        PartitionStyle = PARTITION_STYLE_MBR;
        IsSuperFloppy = TRUE;
    }
    else
    {
        IO_STATUS_BLOCK IoStatusBlock;
        STORAGE_DEVICE_NUMBER DeviceNumber;

        /* The maximum information a DISK_GEOMETRY_EX dynamic structure can contain */
        typedef struct _DISK_GEOMETRY_EX_INTERNAL
        {
            DISK_GEOMETRY Geometry;
            LARGE_INTEGER DiskSize;
            DISK_PARTITION_INFO Partition;
            /* Followed by: DISK_DETECTION_INFO Detection; unused here */
        } DISK_GEOMETRY_EX_INTERNAL;

        DISK_GEOMETRY_EX_INTERNAL DiskGeoEx;
        PARTITION_INFORMATION PartitionInfo;

        /* Retrieve the disk number. NOTE: Fails for floppy disks. */
        Status = NtDeviceIoControlFile(DeviceHandle,
                                       NULL, NULL, NULL,
                                       &IoStatusBlock,
                                       IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                       NULL, 0,
                                       &DeviceNumber, sizeof(DeviceNumber));
        if (!NT_SUCCESS(Status))
            goto Quit; /* This may be a dynamic volume, which is unsupported */
        ASSERT(DeviceNumber.DeviceType == DeviceInfo.DeviceType);
        if (DeviceNumber.DeviceNumber == ULONG_MAX)
        {
            DPRINT1("Invalid disk number reported, bail out\n");
            Status = STATUS_NOT_FOUND;
            goto Quit;
        }

        /* Retrieve the drive geometry. NOTE: Fails for floppy disks;
         * use IOCTL_DISK_GET_DRIVE_GEOMETRY instead. */
        Status = NtDeviceIoControlFile(DeviceHandle,
                                       NULL, NULL, NULL,
                                       &IoStatusBlock,
                                       IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                                       NULL, 0,
                                       &DiskGeoEx,
                                       sizeof(DiskGeoEx));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed (Status 0x%08lx)\n", Status);
            goto Quit;
        }

        if (DiskGeoEx.Partition.PartitionStyle == PARTITION_STYLE_GPT)
        {
            DiskNumber = DeviceNumber.DeviceNumber;
            PartitionStyle = PARTITION_STYLE_GPT;
            IsSuperFloppy = FALSE;
            goto InstallBootManager;
        }

        /*
         * Retrieve the volume's partition information.
         * NOTE: Fails for floppy disks.
         *
         * NOTE: We can use the non-EX IOCTL because the super-floppy test will
         * fail anyway if the disk is NOT MBR-partitioned. (If the disk is GPT,
         * the IOCTL would return only the MBR protective partition, but the
         * super-floppy test would fail due to the wrong partitioning style.)
         */
        Status = NtDeviceIoControlFile(DeviceHandle,
                                       NULL, NULL, NULL,
                                       &IoStatusBlock,
                                       IOCTL_DISK_GET_PARTITION_INFO,
                                       NULL, 0,
                                       &PartitionInfo,
                                       sizeof(PartitionInfo));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IOCTL_DISK_GET_PARTITION_INFO failed (Status 0x%08lx)\n", Status);
            goto Quit;
        }

        DiskNumber = DeviceNumber.DeviceNumber;
        PartitionStyle = DiskGeoEx.Partition.PartitionStyle;
        IsSuperFloppy = IsDiskSuperFloppy2(&DiskGeoEx.Partition,
                                           (PULONGLONG)&DiskGeoEx.DiskSize.QuadPart,
                                           &PartitionInfo);
    }

InstallBootManager:
    /* The boot record install locks the volume, which fails while this handle stays open */
    NtClose(DeviceHandle);
    return InstallBootManagerAndBootEntriesWorker(
                ArchType, SystemRootPath,
                DiskNumber, PartitionStyle, IsSuperFloppy, FileSystem,
                SourceRootPath, DestinationArcPath, Options);

Quit:
    NtClose(DeviceHandle);
    return Status;
}

/* EOF */
