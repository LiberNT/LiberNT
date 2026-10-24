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

#include "bldrsup.h"
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

/*
 * BIG FIXME!!
 * ===========
 *
 * bootsup.c can deal with MBR code (actually it'll have at some point
 * to share or give it to partlist.c, because when we'll support GPT disks,
 * things will change a bit).
 * And, bootsup.c can manage initializing / adding boot entries into NTLDR
 * and FREELDR, and installing the latter, and saving the old MBR / boot
 * sectors in files.
 */

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


static VOID
CreateFreeLoaderReactOSEntries(
    IN PVOID BootStoreHandle,
    IN PCWSTR ArcPath)
{
    UCHAR xxBootEntry[FIELD_OFFSET(BOOT_STORE_ENTRY, OsOptions) + sizeof(NTOS_OPTIONS)];
    PBOOT_STORE_ENTRY BootEntry = (PBOOT_STORE_ENTRY)&xxBootEntry;
    PNTOS_OPTIONS Options = (PNTOS_OPTIONS)&BootEntry->OsOptions;
    BOOT_STORE_OPTIONS BootOptions;

    BootEntry->Version = FreeLdr;
    BootEntry->BootFilePath = NULL;

    BootEntry->OsOptionsLength = sizeof(NTOS_OPTIONS);
    RtlCopyMemory(Options->Signature,
                  NTOS_OPTIONS_SIGNATURE,
                  RTL_FIELD_SIZE(NTOS_OPTIONS, Signature));

    Options->OsLoadPath = ArcPath;

    /* ReactOS */
    // BootEntry->BootEntryKey = MAKESTRKEY(L"ReactOS");
    BootEntry->FriendlyName = L"\"LiberNT\"";
    Options->OsLoadOptions  = L"/FASTDETECT";
    AddBootStoreEntry(BootStoreHandle, BootEntry, MAKESTRKEY(L"ReactOS"));

    /* DefaultOS=ReactOS */
    BootOptions.NextBootEntryKey = MAKESTRKEY(L"ReactOS");

#if DBG
    if (IsUnattendedSetup)
#endif
    {
        /* Timeout=0 for unattended or non debug */
        BootOptions.Timeout = 0;
    }
#if DBG
    else
    {
        /* Timeout=10 */
        BootOptions.Timeout = 10;
    }
#endif

    SetBootStoreOptions(BootStoreHandle, &BootOptions,
                        BOOT_OPTIONS_TIMEOUT | BOOT_OPTIONS_NEXT_BOOTENTRY_KEY);
}

static NTSTATUS
CreateFreeLoaderIniForReactOS(
    IN PCWSTR IniPath,
    IN PCWSTR ArcPath)
{
    NTSTATUS Status;
    PVOID BootStoreHandle;

    /* Initialize the INI file and create the common FreeLdr sections */
    Status = OpenBootStore(&BootStoreHandle, IniPath, FreeLdr,
                           BS_CreateAlways /* BS_OpenAlways */, BS_ReadWriteAccess);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Add the ReactOS entries */
    CreateFreeLoaderReactOSEntries(BootStoreHandle, ArcPath);

    /* Close the INI file */
    CloseBootStore(BootStoreHandle);
    return STATUS_SUCCESS;
}

static NTSTATUS
CreateFreeLoaderIniForReactOSAndBootSector(
    IN PCWSTR IniPath,
    IN PCWSTR ArcPath,
    IN PCWSTR Section,
    IN PCWSTR Description,
    IN PCWSTR BootPath,
    IN PCWSTR BootSector)
{
    NTSTATUS Status;
    PVOID BootStoreHandle;
    UCHAR xxBootEntry[FIELD_OFFSET(BOOT_STORE_ENTRY, OsOptions) + sizeof(BOOTSECTOR_OPTIONS)];
    PBOOT_STORE_ENTRY BootEntry = (PBOOT_STORE_ENTRY)&xxBootEntry;
    PBOOTSECTOR_OPTIONS Options = (PBOOTSECTOR_OPTIONS)&BootEntry->OsOptions;
    WCHAR BootPathBuffer[MAX_PATH] = L"";

    /* Since the BootPath given here is in NT format
     * (not ARC), we need to hack-generate a mapping */
    ULONG DiskNumber = 0, PartitionNumber = 0;
    PCWSTR PathComponent = NULL;

    /* From the NT path, compute the disk, partition and path components */
    // NOTE: this function doesn't support stuff like \Device\FloppyX ...
    if (NtPathToDiskPartComponents(BootPath, &DiskNumber, &PartitionNumber, &PathComponent))
    {
        DPRINT1("BootPath = '%S' points to disk #%d, partition #%d, path '%S'\n",
               BootPath, DiskNumber, PartitionNumber, PathComponent);

        /* HACK-build a possible ARC path:
         * Hard disk path: multi(0)disk(0)rdisk(x)partition(y)[\path] */
        RtlStringCchPrintfW(BootPathBuffer, _countof(BootPathBuffer),
                            L"multi(0)disk(0)rdisk(%lu)partition(%lu)",
                            DiskNumber, PartitionNumber);
        if (PathComponent && *PathComponent &&
            (PathComponent[0] != L'\\' || PathComponent[1]))
        {
            RtlStringCchCatW(BootPathBuffer, _countof(BootPathBuffer),
                             PathComponent);
        }
    }
    else
    {
        PCWSTR Path = BootPath;

        if ((_wcsnicmp(Path, L"\\Device\\Floppy", 14) == 0) &&
            (Path += 14) && iswdigit(*Path))
        {
            DiskNumber = wcstoul(Path, (PWSTR*)&PathComponent, 10);
            if (PathComponent && *PathComponent && *PathComponent != L'\\')
                PathComponent = NULL;

            /* HACK-build a possible ARC path:
             * Floppy disk path: multi(0)disk(0)fdisk(x)[\path] */
            RtlStringCchPrintfW(BootPathBuffer, _countof(BootPathBuffer),
                                L"multi(0)disk(0)fdisk(%lu)", DiskNumber);
            if (PathComponent && *PathComponent &&
                (PathComponent[0] != L'\\' || PathComponent[1]))
            {
                RtlStringCchCatW(BootPathBuffer, _countof(BootPathBuffer),
                                 PathComponent);
            }
        }
        else
        {
            /* HACK: Just keep the unresolved NT path and hope for the best... */

            /* Remove any trailing backslash if needed */
            UNICODE_STRING RootPartition;
            RtlInitUnicodeString(&RootPartition, BootPath);
            TrimTrailingPathSeparators_UStr(&RootPartition);

            /* RootPartition is BootPath without counting any trailing
             * path separator. Because of this, we need to copy the string
             * in the buffer, instead of just using a pointer to it. */
            RtlStringCchPrintfW(BootPathBuffer, _countof(BootPathBuffer),
                                L"%wZ", &RootPartition);

            DPRINT1("Unhandled NT path '%S'\n", BootPath);
        }
    }

    /* Initialize the INI file and create the common FreeLdr sections */
    Status = OpenBootStore(&BootStoreHandle, IniPath, FreeLdr,
                           BS_CreateAlways /* BS_OpenAlways */, BS_ReadWriteAccess);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Add the ReactOS entries */
    CreateFreeLoaderReactOSEntries(BootStoreHandle, ArcPath);

    BootEntry->Version = FreeLdr;
    BootEntry->BootFilePath = NULL;

    BootEntry->OsOptionsLength = sizeof(BOOTSECTOR_OPTIONS);
    RtlCopyMemory(Options->Signature,
                  BOOTSECTOR_OPTIONS_SIGNATURE,
                  RTL_FIELD_SIZE(BOOTSECTOR_OPTIONS, Signature));

    Options->BootPath = BootPathBuffer;
    Options->FileName = BootSector;

    // BootEntry->BootEntryKey = MAKESTRKEY(Section);
    BootEntry->FriendlyName = Description;
    AddBootStoreEntry(BootStoreHandle, BootEntry, MAKESTRKEY(Section));

    /* Close the INI file */
    CloseBootStore(BootStoreHandle);
    return STATUS_SUCCESS;
}

//
// I think this function can be generalizable as:
// "find the corresponding 'ReactOS' boot entry in this loader config file
// (here abstraction comes there), and if none, add a new one".
//

typedef struct _ENUM_REACTOS_ENTRIES_DATA
{
    ULONG i;
    BOOLEAN UseExistingEntry;
    PCWSTR ArcPath;
    WCHAR SectionName[80];
    WCHAR OsName[80];
} ENUM_REACTOS_ENTRIES_DATA, *PENUM_REACTOS_ENTRIES_DATA;

// PENUM_BOOT_ENTRIES_ROUTINE
static NTSTATUS
NTAPI
EnumerateReactOSEntries(
    IN BOOT_STORE_TYPE Type,
    IN PBOOT_STORE_ENTRY BootEntry,
    IN PVOID Parameter OPTIONAL)
{
    NTSTATUS Status;
    PENUM_REACTOS_ENTRIES_DATA Data = (PENUM_REACTOS_ENTRIES_DATA)Parameter;
    PNTOS_OPTIONS Options = (PNTOS_OPTIONS)&BootEntry->OsOptions;
    WCHAR SystemPath[MAX_PATH];

    /* We have a boot entry */

    /* Check for supported boot type "Windows2003" */
    if (BootEntry->OsOptionsLength < sizeof(NTOS_OPTIONS) ||
        RtlCompareMemory(&BootEntry->OsOptions /* Signature */,
                         NTOS_OPTIONS_SIGNATURE,
                         RTL_FIELD_SIZE(NTOS_OPTIONS, Signature)) !=
                         RTL_FIELD_SIZE(NTOS_OPTIONS, Signature))
    {
        /* This is not a ReactOS entry */
        // DPRINT("    An installation '%S' of unsupported type '%S'\n",
               // BootEntry->FriendlyName, BootEntry->Version ? BootEntry->Version : L"n/a");
        DPRINT("    An installation '%S' of unsupported type %lu\n",
               BootEntry->FriendlyName, BootEntry->OsOptionsLength);
        /* Continue the enumeration */
        goto SkipThisEntry;
    }

    /* BootType is Windows2003, now check OsLoadPath */
    if (!Options->OsLoadPath || !*Options->OsLoadPath)
    {
        /* Certainly not a ReactOS installation */
        DPRINT1("    A Win2k3 install '%S' without an ARC path?!\n", BootEntry->FriendlyName);
        /* Continue the enumeration */
        goto SkipThisEntry;
    }

    if (_wcsicmp(Options->OsLoadPath, Data->ArcPath) != 0)
    {
        /* Not found, retry with a quoted path */
        Status = RtlStringCchPrintfW(SystemPath, ARRAYSIZE(SystemPath), L"\"%s\"", Data->ArcPath);
        if (!NT_SUCCESS(Status) || _wcsicmp(Options->OsLoadPath, SystemPath) != 0)
        {
            /*
             * This entry is a ReactOS entry, but the SystemRoot
             * does not match the one we are looking for.
             */
            /* Continue the enumeration */
            goto SkipThisEntry;
        }
    }

    DPRINT("    Found a candidate Win2k3 install '%S' with ARC path '%S'\n",
           BootEntry->FriendlyName, Options->OsLoadPath);
    // DPRINT("    Found a Win2k3 install '%S' with ARC path '%S'\n",
           // BootEntry->FriendlyName, Options->OsLoadPath);

    DPRINT("EnumerateReactOSEntries: OsLoadPath: '%S'\n", Options->OsLoadPath);

    Data->UseExistingEntry = TRUE;
    RtlStringCchCopyW(Data->OsName, ARRAYSIZE(Data->OsName), BootEntry->FriendlyName);

    /* We have found our entry, stop the enumeration now! */
    return STATUS_NO_MORE_ENTRIES;

SkipThisEntry:
    Data->UseExistingEntry = FALSE;
    if (Type == FreeLdr && wcscmp(Data->SectionName, (PWSTR)BootEntry->BootEntryKey)== 0)
    {
        RtlStringCchPrintfW(Data->SectionName, ARRAYSIZE(Data->SectionName),
                            L"ReactOS_%lu", Data->i);
        RtlStringCchPrintfW(Data->OsName, ARRAYSIZE(Data->OsName),
                            L"\"LiberNT %lu\"", Data->i);
        Data->i++;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
UpdateFreeLoaderIni(
    IN PCWSTR IniPath,
    IN PCWSTR ArcPath)
{
    NTSTATUS Status;
    PVOID BootStoreHandle;
    ENUM_REACTOS_ENTRIES_DATA Data;
    UCHAR xxBootEntry[FIELD_OFFSET(BOOT_STORE_ENTRY, OsOptions) + sizeof(NTOS_OPTIONS)];
    PBOOT_STORE_ENTRY BootEntry = (PBOOT_STORE_ENTRY)&xxBootEntry;
    PNTOS_OPTIONS Options = (PNTOS_OPTIONS)&BootEntry->OsOptions;

    /* Open the INI file */
    Status = OpenBootStore(&BootStoreHandle, IniPath, FreeLdr,
                           BS_OpenExisting /* BS_OpenAlways */, BS_ReadWriteAccess);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Find an existing usable or an unused section name */
    Data.UseExistingEntry = TRUE;
    Data.i = 1;
    Data.ArcPath = ArcPath;
    RtlStringCchCopyW(Data.SectionName, ARRAYSIZE(Data.SectionName), L"ReactOS");
    RtlStringCchCopyW(Data.OsName, ARRAYSIZE(Data.OsName), L"\"LiberNT\"");

    //
    // FIXME: We temporarily use EnumerateBootStoreEntries, until
    // both QueryBootStoreEntry and ModifyBootStoreEntry get implemented.
    //
    Status = EnumerateBootStoreEntries(BootStoreHandle, EnumerateReactOSEntries, &Data);

    /* Create a new "ReactOS" entry if there is none already existing that suits us */
    if (!Data.UseExistingEntry)
    {
        // RtlStringCchPrintfW(Data.SectionName, ARRAYSIZE(Data.SectionName), L"ReactOS_%lu", Data.i);
        // RtlStringCchPrintfW(Data.OsName, ARRAYSIZE(Data.OsName), L"\"LiberNT %lu\"", Data.i);

        BootEntry->Version = FreeLdr;
        BootEntry->BootFilePath = NULL;

        BootEntry->OsOptionsLength = sizeof(NTOS_OPTIONS);
        RtlCopyMemory(Options->Signature,
                      NTOS_OPTIONS_SIGNATURE,
                      RTL_FIELD_SIZE(NTOS_OPTIONS, Signature));

        Options->OsLoadPath = ArcPath;

        // BootEntry->BootEntryKey = MAKESTRKEY(Data.SectionName);
        BootEntry->FriendlyName = Data.OsName;
        Options->OsLoadOptions  = NULL; // L"";
        AddBootStoreEntry(BootStoreHandle, BootEntry, MAKESTRKEY(Data.SectionName));
    }

    /* Close the INI file */
    CloseBootStore(BootStoreHandle);
    return STATUS_SUCCESS;
}

static
NTSTATUS
UpdateBootIni(
    IN PCWSTR IniPath,
    IN PCWSTR EntryName,    // ~= ArcPath
    IN PCWSTR EntryValue)
{
    NTSTATUS Status;
    PVOID BootStoreHandle;
    ENUM_REACTOS_ENTRIES_DATA Data;

    // NOTE: Technically it would be "BootSector"...
    UCHAR xxBootEntry[FIELD_OFFSET(BOOT_STORE_ENTRY, OsOptions) + sizeof(NTOS_OPTIONS)];
    PBOOT_STORE_ENTRY BootEntry = (PBOOT_STORE_ENTRY)&xxBootEntry;
    PNTOS_OPTIONS Options = (PNTOS_OPTIONS)&BootEntry->OsOptions;

    /* Open the INI file */
    Status = OpenBootStore(&BootStoreHandle, IniPath, NtLdr,
                           BS_OpenExisting /* BS_OpenAlways */, BS_ReadWriteAccess);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Find an existing usable or an unused section name */
    Data.UseExistingEntry = TRUE;
    // Data.i = 1;
    Data.ArcPath = EntryName;
    // RtlStringCchCopyW(Data.SectionName, ARRAYSIZE(Data.SectionName), L"ReactOS");
    RtlStringCchCopyW(Data.OsName, ARRAYSIZE(Data.OsName), L"\"LiberNT\"");

    //
    // FIXME: We temporarily use EnumerateBootStoreEntries, until
    // both QueryBootStoreEntry and ModifyBootStoreEntry get implemented.
    //
    Status = EnumerateBootStoreEntries(BootStoreHandle, EnumerateReactOSEntries, &Data);

    /* If either the key was not found, or contains something else, add a new one */
    if (!Data.UseExistingEntry /* ||
        ( (Status == STATUS_NO_MORE_ENTRIES) && wcscmp(Data.OsName, EntryValue) ) */)
    {
        BootEntry->Version = NtLdr;
        BootEntry->BootFilePath = NULL;

        BootEntry->OsOptionsLength = sizeof(NTOS_OPTIONS);
        RtlCopyMemory(Options->Signature,
                      NTOS_OPTIONS_SIGNATURE,
                      RTL_FIELD_SIZE(NTOS_OPTIONS, Signature));

        Options->OsLoadPath = EntryName;

        // BootEntry->BootEntryKey = MAKESTRKEY(Data.SectionName);
        // BootEntry->FriendlyName = Data.OsName;
        BootEntry->FriendlyName = EntryValue;
        Options->OsLoadOptions  = NULL; // L"";
        AddBootStoreEntry(BootStoreHandle, BootEntry, MAKESTRKEY(0 /*Data.SectionName*/));
    }

    /* Close the INI file */
    CloseBootStore(BootStoreHandle);
    return STATUS_SUCCESS; // Status;
}


static
BOOLEAN
IsThereAValidBootSector(
    IN PCWSTR RootPath)
{
    /*
     * We first demand that the bootsector has a valid signature at its end.
     * We then check the first 3 bytes (as a ULONG) of the bootsector for a
     * potential "valid" instruction (the BIOS starts execution of the bootsector
     * at its beginning). Currently this criterium is that this ULONG must be
     * non-zero. If both these tests pass, then the bootsector is valid; otherwise
     * it is invalid and certainly needs to be overwritten.
     */

    BOOLEAN IsValid = FALSE;
    NTSTATUS Status;
    UNICODE_STRING RootPartition;
    BOOTCODE BootSector = {0};

    /* Allocate and read the root partition bootsector.
     * Remove any trailing backslash if needed. */
    RtlInitUnicodeString(&RootPartition, RootPath);
    TrimTrailingPathSeparators_UStr(&RootPartition);
    Status = ReadBootCodeFromFile(&BootSector, &RootPartition, SECTORSIZE);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* Check for the existence of the bootsector signature */
    IsValid = (*(PUSHORT)((PUCHAR)BootSector.BootCode + 0x1FE) == 0xAA55);
    if (IsValid)
    {
        /* Check for the first instruction encoded on three bytes */
        IsValid = (((*(PULONG)BootSector.BootCode) & 0x00FFFFFF) != 0x00000000);
    }
    if (IsValid)
    {
        PUCHAR Code = (PUCHAR)BootSector.BootCode;
        ULONG Index;

        IsValid = FALSE;
        for (Index = 0x60; Index < 0x1FE; Index++)
        {
            if (Code[Index] != 0)
            {
                IsValid = TRUE;
                break;
            }
        }
    }

    /* Free the bootsector and return */
    FreeBootCode(&BootSector);
    return IsValid;
}

static
NTSTATUS
SaveBootSector(
    IN PCWSTR RootPath,
    IN PCWSTR DstPath,
    IN ULONG Length)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    // LARGE_INTEGER FileOffset;
    BOOTCODE BootSector = {0};

    /* Allocate and read the root partition bootsector.
     * Remove any trailing backslash if needed. */
    RtlInitUnicodeString(&Name, RootPath);
    TrimTrailingPathSeparators_UStr(&Name);
    Status = ReadBootCodeFromFile(&BootSector, &Name, Length);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Write the bootsector to DstPath */
    RtlInitUnicodeString(&Name, DstPath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtCreateFile(&FileHandle,
                          GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_SUPERSEDE,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        FreeBootCode(&BootSector);
        return Status;
    }

    Status = NtWriteFile(FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         BootSector.BootCode,
                         BootSector.Length,
                         NULL,
                         NULL);
    NtClose(FileHandle);

    /* Free the bootsector and return */
    FreeBootCode(&BootSector);
    return Status;
}


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

    Status = NtFsControlFile(PartitionHandle, NULL, NULL, NULL, &IoStatusBlock, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Unable to dismount the volume before installing boot code. Status 0x%08x.\n", Status);
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
InstallBootCodeToFile(
    IN PCWSTR SrcPath,
    IN PCWSTR DstPath,
    IN PCWSTR RootPath,
    IN PFS_INSTALL_BOOTCODE InstallBootCode)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE PartitionHandle, FileHandle;

    /*
     * Open the root partition from which the bootcode (MBR, VBR)
     * parameters will be obtained.
     *
     * FIXME? It might be possible that we need to also open it for writing
     * access in case we really need to still write the second portion of
     * the boot sector ????
     *
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
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT /* | FILE_SEQUENTIAL_ONLY */);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Open or create the file where the new bootsector will be saved */
    RtlInitUnicodeString(&Name, DstPath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtCreateFile(&FileHandle,
                          GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_SUPERSEDE, // FILE_OVERWRITE_IF
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateFile() failed (Status %lx)\n", Status);
        NtClose(PartitionHandle);
        return Status;
    }

    /* Install the bootcode (MBR, VBR) */
    Status = InstallBootCode(SrcPath, FileHandle, PartitionHandle);

    /* Close the file and the partition */
    NtClose(FileHandle);
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
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCWSTR DestinationDevicePathBuffer)
{
    NTSTATUS Status;
    WCHAR SourceMbrPathBuffer[MAX_PATH];
    WCHAR DstPath[MAX_PATH];

#if 0
    /*
     * The DestinationDevicePathBuffer parameter has been built with
     * the following instruction by the caller; I'm not yet sure whether
     * I actually want this function to build the path instead, hence
     * I keep this code here but disabled for now...
     */
    WCHAR DestinationDevicePathBuffer[MAX_PATH];
    RtlStringCchPrintfW(DestinationDevicePathBuffer, ARRAYSIZE(DestinationDevicePathBuffer),
                        L"\\Device\\Harddisk%d\\Partition0",
                        DiskNumber);
#endif

    CombinePaths(SourceMbrPathBuffer, ARRAYSIZE(SourceMbrPathBuffer), 2,
                 SourceRootPath->Buffer, L"\\loader\\dosmbr.bin");

    if (IsThereAValidBootSector(DestinationDevicePathBuffer))
    {
        /* Save current MBR */
        CombinePaths(DstPath, ARRAYSIZE(DstPath), 2,
                     SystemRootPath->Buffer, L"mbr.old");

        DPRINT1("Save MBR: %S ==> %S\n", DestinationDevicePathBuffer, DstPath);
        Status = SaveBootSector(DestinationDevicePathBuffer, DstPath, sizeof(PARTITION_SECTOR));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SaveBootSector() failed (Status %lx)\n", Status);
            // Don't care if we succeeded or not saving the old MBR, just go ahead.
        }
    }

    DPRINT1("Install MBR bootcode: %S ==> %S\n",
            SourceMbrPathBuffer, DestinationDevicePathBuffer);

    /* Install the MBR */
    return InstallBootCodeToDisk(SourceMbrPathBuffer,
                                 DestinationDevicePathBuffer,
                                 InstallMbrBootCode);
}

static
NTSTATUS
InstallBootloaderFiles(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath)
{
    NTSTATUS Status;
    WCHAR SrcPath[MAX_PATH];
    WCHAR DstPath[MAX_PATH];

    /* Copy FreeLoader to the system partition, always overwriting the older version */
    CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\freeldr.sys");
    CombinePaths(DstPath, ARRAYSIZE(DstPath), 2, SystemRootPath->Buffer, L"freeldr.sys");

    DPRINT("Copy: %S ==> %S\n", SrcPath, DstPath);
    Status = SetupCopyFile(SrcPath, DstPath, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SetupCopyFile() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    /* Copy rosload to the system partition, always overwriting the older version */
    CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\rosload.exe");
    CombinePaths(DstPath, ARRAYSIZE(DstPath), 2, SystemRootPath->Buffer, L"rosload.exe");

    DPRINT("Copy: %S ==> %S\n", SrcPath, DstPath);
    Status = SetupCopyFile(SrcPath, DstPath, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SetupCopyFile() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
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
NTSTATUS
GetBcdPartitionElement(
    _In_ ULONG DiskNumber,
    _In_ ULONG PartitionNumber,
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
                                   IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                   NULL, 0, &Drive, sizeof(Drive));
    NtClose(Handle);
    if (!NT_SUCCESS(Status))
        return Status;

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
    _In_reads_bytes_(BCD_DEVICE_ELEMENT_SIZE) PUCHAR Data)
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

    Status = NtSetValueKey(Key, &ValueName, 0, REG_BINARY, Data, BCD_DEVICE_ELEMENT_SIZE);
    NtClose(Key);
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
    _In_ PCUNICODE_STRING DestinationArcPath)
{
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
    ULONG Disk, Number;
    BOOLEAN PrivilegeSet[2] = {FALSE, FALSE};
    NTSTATUS Status;

    Status = GetPartitionDeviceNumber(SystemRootPath, &DeviceNumber);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = GetBcdPartitionElement(DeviceNumber.DeviceNumber, DeviceNumber.PartitionNumber, SystemElement);
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

    Status = GetBcdPartitionElement(Disk, Number, OsElement);
    if (!NT_SUCCESS(Status))
        return Status;

    CombinePaths(Path, ARRAYSIZE(Path), 2, SystemRootPath->Buffer, L"EFI\\LiberNT\\Boot");
    Status = SetupCreateDirectory(Path);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION)
        return Status;

    CombinePaths(Path, ARRAYSIZE(Path), 2, SystemRootPath->Buffer, L"EFI\\LiberNT\\Boot\\bootmgfw.efi");
    Status = SetupCopyFile(BootManagerPath, Path, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    CombinePaths(StorePath, ARRAYSIZE(StorePath), 2, SystemRootPath->Buffer, L"EFI\\LiberNT\\Boot\\BCD");
    Status = SetupCopyFile(TemplatePath, StorePath, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&FileName, StorePath);
    InitializeObjectAttributes(&KeyAttributes, &MountPoint, OBJ_CASE_INSENSITIVE, NULL, NULL);
    InitializeObjectAttributes(&FileAttributes, &FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &PrivilegeSet[0]);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &PrivilegeSet[1]);
    if (NT_SUCCESS(Status))
    {
        Status = NtLoadKey(&KeyAttributes, &FileAttributes);
        if (NT_SUCCESS(Status))
        {
            Status = SetBcdElement(BCD_BOOTMGR_OBJECT, BCD_APPLICATION_DEVICE, SystemElement);
            if (NT_SUCCESS(Status))
                Status = SetBcdElement(BCD_OSLOADER_OBJECT, BCD_APPLICATION_DEVICE, OsElement);
            if (NT_SUCCESS(Status))
                Status = SetBcdElement(BCD_OSLOADER_OBJECT, BCD_OS_DEVICE, OsElement);

            NtUnloadKey(&KeyAttributes);
        }

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
IsWindowsBootManagerOption(
    _In_reads_bytes_(OptionLength) const UCHAR* Option,
    _In_ ULONG OptionLength)
{
    static const WCHAR WindowsFile[] = EFI_WINDOWS_BOOT_MANAGER_FILE;
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
            if (Chars == ARRAYSIZE(WindowsFile) - 1 &&
                _wcsnicmp((const WCHAR*)(Option + Offset + EFI_DEVICE_PATH_HEADER_SIZE), WindowsFile, Chars) == 0)
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
    _In_ BOOLEAN AfterWindows)
{
    STORAGE_DEVICE_NUMBER DeviceNumber;
    UCHAR Option[EFI_LOAD_OPTION_MAX];
    UCHAR Existing[EFI_LOAD_OPTION_MAX];
    USHORT Order[EFI_BOOT_ORDER_MAX / sizeof(USHORT) + 1];
    PUCHAR FilePath, Node;
    WCHAR Name[16];
    ULONG DescriptionSize, FileSize, OptionSize, Length, OrderCount, Number = 0, Position = 0, i, j;
    USHORT FilePathLength;
    BOOLEAN Found = FALSE, PrivilegeSet = FALSE;
    NTSTATUS Status;

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

    if (NT_SUCCESS(Status))
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
                IsWindowsBootManagerOption(Existing, Length))
            {
                Position = i + 1;
                break;
            }
        }
        RtlMoveMemory(&Order[Position + 1], &Order[Position], (j - Position) * sizeof(USHORT));
        Order[Position] = (USHORT)Number;
        Status = SetFirmwareVariable(L"BootOrder", Order, (j + 1) * sizeof(USHORT));
    }

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, PrivilegeSet, FALSE, &PrivilegeSet);
    return Status;
}

static
BOOLEAN
InstallBcdFromTemplate(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ BOOLEAN AfterWindows,
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

    *Status = InstallBcdStore(SystemRootPath, TemplatePath, BootManagerPath, DestinationArcPath);
    if (!NT_SUCCESS(*Status))
    {
        DPRINT1("Writing the boot configuration data failed (Status %lx)\n", *Status);
        return TRUE;
    }

    EntryStatus = InstallFirmwareBootEntry(SystemRootPath, AfterWindows);
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
    _In_ BOOLEAN AfterWindows)
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

    if (InstallBcdFromTemplate(SystemRootPath, SourceRootPath, DestinationArcPath, AfterWindows,
                               &FirmwareEntry, &Status))
    {
        if (NT_SUCCESS(Status) && FallbackKept && !FirmwareEntry)
            Status = InstallEfiLoaderFiles(SystemRootPath, SourceRootPath, FALSE, NULL);
        return Status;
    }

    if (DoesFileExist_2(SystemRootPath->Buffer, L"freeldr.ini"))
    {
        DPRINT1("Update existing 'freeldr.ini' on the EFI system partition\n");
        Status = UpdateFreeLoaderIni(SystemRootPath->Buffer, DestinationArcPath->Buffer);
    }
    else
    {
        DPRINT1("Create new 'freeldr.ini' on the EFI system partition\n");
        Status = CreateFreeLoaderIniForReactOS(SystemRootPath->Buffer, DestinationArcPath->Buffer);
    }
    if (!NT_SUCCESS(Status))
        DPRINT1("Writing 'freeldr.ini' failed (Status %lx)\n", Status);
    return Status;
}

static
NTSTATUS
InstallFatBootcodeToPartition(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ PCWSTR FileSystemName)
{
    NTSTATUS Status;
    BOOLEAN DoesFreeLdrExist;
    WCHAR SrcPath[MAX_PATH];
    WCHAR DstPath[MAX_PATH];

    /* FAT or FAT32 partition */
    DPRINT("System path: '%wZ'\n", SystemRootPath);

    /* Install the bootloader */
    Status = InstallBootloaderFiles(SystemRootPath, SourceRootPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallBootloaderFiles() failed (Status %lx)\n", Status);
        return Status;
    }

    Status = InstallEfiLoaderFiles(SystemRootPath, SourceRootPath, FALSE, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallEfiLoaderFiles() failed (Status %lx)\n", Status);
        return Status;
    }

    InstallBcdFromTemplate(SystemRootPath, SourceRootPath, DestinationArcPath, FALSE, NULL, &Status);

    /* Prepare for possibly updating 'freeldr.ini' */
    DoesFreeLdrExist = DoesFileExist_2(SystemRootPath->Buffer, L"freeldr.ini");
    if (DoesFreeLdrExist)
    {
        /* Update existing 'freeldr.ini' */
        DPRINT1("Update existing 'freeldr.ini'\n");
        Status = UpdateFreeLoaderIni(SystemRootPath->Buffer, DestinationArcPath->Buffer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("UpdateFreeLoaderIni() failed (Status %lx)\n", Status);
            return Status;
        }
    }

    /* Check for NT and other bootloaders */

    // FIXME: Check for Vista+ bootloader!
    /*** Status = FindBootStore(PartitionHandle, NtLdr, &Version); ***/
    /*** Status = FindBootStore(PartitionHandle, BootMgr, &Version); ***/
    if (DoesFileExist_2(SystemRootPath->Buffer, L"NTLDR") == TRUE ||
        DoesFileExist_2(SystemRootPath->Buffer, L"BOOT.INI") == TRUE)
    {
        /* Search root directory for 'NTLDR' and 'BOOT.INI' */
        DPRINT1("Found Microsoft Windows NT/2000/XP boot loader\n");

        /* Create or update 'freeldr.ini' */
        if (DoesFreeLdrExist == FALSE)
        {
            /* Create new 'freeldr.ini' */
            DPRINT1("Create new 'freeldr.ini'\n");
            Status = CreateFreeLoaderIniForReactOS(SystemRootPath->Buffer, DestinationArcPath->Buffer);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("CreateFreeLoaderIniForReactOS() failed (Status %lx)\n", Status);
                return Status;
            }

            /* Install new bootcode into a file */
            CombinePaths(DstPath, ARRAYSIZE(DstPath), 2, SystemRootPath->Buffer, L"bootsect.ros");

            if (_wcsicmp(FileSystemName, L"FAT32") == 0)
            {
                /* Install FAT32 bootcode */
                CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\fat32.bin");

                DPRINT1("Install FAT32 bootcode: %S ==> %S\n", SrcPath, DstPath);
                Status = InstallBootCodeToFile(SrcPath, DstPath,
                                               SystemRootPath->Buffer,
                                               InstallFat32BootCode);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("InstallBootCodeToFile(FAT32) failed (Status %lx)\n", Status);
                    return Status;
                }
            }
            else // if (wcsicmp(FileSystemName, L"FAT") == 0)
            {
                /* Install FAT16 bootcode */
                CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\fat.bin");

                DPRINT1("Install FAT16 bootcode: %S ==> %S\n", SrcPath, DstPath);
                Status = InstallBootCodeToFile(SrcPath, DstPath,
                                               SystemRootPath->Buffer,
                                               InstallFat16BootCode);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("InstallBootCodeToFile(FAT16) failed (Status %lx)\n", Status);
                    return Status;
                }
            }
        }

        /* Update 'boot.ini' */
        /* Windows' NTLDR loads an external bootsector file when the specified drive
           letter is C:, otherwise it will interpret it as a boot DOS path specifier. */
        DPRINT1("Update 'boot.ini'\n");
        Status = UpdateBootIni(SystemRootPath->Buffer,
                               L"C:\\bootsect.ros",
                               L"\"LiberNT\"");
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("UpdateBootIni() failed (Status %lx)\n", Status);
            return Status;
        }
    }
    else
    {
        /* Non-NT bootloaders: install our own bootloader */

        PCWSTR Section;
        PCWSTR Description;
        PCWSTR BootSector;

        /* Search for COMPAQ MS-DOS 1.x (1.11, 1.12, based on MS-DOS 1.25) boot loader */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"IOSYS.COM") == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"MSDOS.COM") == TRUE)
        {
            DPRINT1("Found COMPAQ MS-DOS 1.x (1.11, 1.12) / MS-DOS 1.25 boot loader\n");

            Section     = L"CPQDOS";
            Description = L"\"COMPAQ MS-DOS 1.x / MS-DOS 1.25\"";
            BootSector  = L"BOOTSECT.DOS";
        }
        else
        /* Search for Microsoft DOS or Windows 9x boot loader */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"IO.SYS") == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"MSDOS.SYS") == TRUE)
            // WINBOOT.SYS
        {
            DPRINT1("Found Microsoft DOS or Windows 9x boot loader\n");

            Section     = L"MSDOS";
            Description = L"\"MS-DOS/Windows\"";
            BootSector  = L"BOOTSECT.DOS";
        }
        else
        /* Search for IBM PC-DOS or DR-DOS 5.x boot loader */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"IBMIO.COM" ) == TRUE || // Some people refer to this file instead of IBMBIO.COM...
            DoesFileExist_2(SystemRootPath->Buffer, L"IBMBIO.COM") == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"IBMDOS.COM") == TRUE)
        {
            DPRINT1("Found IBM PC-DOS or DR-DOS 5.x or IBM OS/2 1.0\n");

            Section     = L"IBMDOS";
            Description = L"\"IBM PC-DOS or DR-DOS 5.x or IBM OS/2 1.0\"";
            BootSector  = L"BOOTSECT.DOS";
        }
        else
        /* Search for DR-DOS 3.x boot loader */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"DRBIOS.SYS") == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"DRBDOS.SYS") == TRUE)
        {
            DPRINT1("Found DR-DOS 3.x\n");

            Section     = L"DRDOS";
            Description = L"\"DR-DOS 3.x\"";
            BootSector  = L"BOOTSECT.DOS";
        }
        else
        /* Search for Dell Real-Mode Kernel (DRMK) OS */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"DELLBIO.BIN") == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"DELLRMK.BIN") == TRUE)
        {
            DPRINT1("Found Dell Real-Mode Kernel OS\n");

            Section     = L"DRMK";
            Description = L"\"Dell Real-Mode Kernel OS\"";
            BootSector  = L"BOOTSECT.DOS";
        }
        else
        /* Search for MS OS/2 1.x */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"OS2BOOT.COM") == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"OS2BIO.COM" ) == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"OS2DOS.COM" ) == TRUE)
        {
            DPRINT1("Found MS OS/2 1.x\n");

            Section     = L"MSOS2";
            Description = L"\"MS OS/2 1.x\"";
            BootSector  = L"BOOTSECT.OS2";
        }
        else
        /* Search for MS or IBM OS/2 */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"OS2BOOT") == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"OS2LDR" ) == TRUE ||
            DoesFileExist_2(SystemRootPath->Buffer, L"OS2KRNL") == TRUE)
        {
            DPRINT1("Found MS/IBM OS/2\n");

            Section     = L"IBMOS2";
            Description = L"\"MS/IBM OS/2\"";
            BootSector  = L"BOOTSECT.OS2";
        }
        else
        /* Search for FreeDOS boot loader */
        if (DoesFileExist_2(SystemRootPath->Buffer, L"kernel.sys") == TRUE)
        {
            DPRINT1("Found FreeDOS boot loader\n");

            Section     = L"FDOS";
            Description = L"\"FreeDOS\"";
            BootSector  = L"BOOTSECT.DOS";
        }
        else
        {
            /* No or unknown boot loader */
            DPRINT1("No or unknown boot loader found\n");

            Section     = L"Unknown";
            Description = L"\"Unknown Operating System\"";
            BootSector  = L"BOOTSECT.OLD";
        }

        /* Create or update 'freeldr.ini' */
        if (DoesFreeLdrExist == FALSE)
        {
            /* Create new 'freeldr.ini' */
            DPRINT1("Create new 'freeldr.ini'\n");

            if (IsThereAValidBootSector(SystemRootPath->Buffer))
            {
                Status = CreateFreeLoaderIniForReactOSAndBootSector(
                             SystemRootPath->Buffer, DestinationArcPath->Buffer,
                             Section, Description,
                             SystemRootPath->Buffer, BootSector);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("CreateFreeLoaderIniForReactOSAndBootSector() failed (Status %lx)\n", Status);
                    return Status;
                }

                /* Save current bootsector */
                CombinePaths(DstPath, ARRAYSIZE(DstPath), 2, SystemRootPath->Buffer, BootSector);

                DPRINT1("Save bootsector: %S ==> %S\n", SystemRootPath->Buffer, DstPath);
                Status = SaveBootSector(SystemRootPath->Buffer, DstPath, SECTORSIZE);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("SaveBootSector() failed (Status %lx)\n", Status);
                    return Status;
                }
            }
            else
            {
                Status = CreateFreeLoaderIniForReactOS(SystemRootPath->Buffer, DestinationArcPath->Buffer);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("CreateFreeLoaderIniForReactOS() failed (Status %lx)\n", Status);
                    return Status;
                }
            }

            /* Install new bootsector on the disk */
            if (_wcsicmp(FileSystemName, L"FAT32") == 0)
            {
                /* Install FAT32 bootcode */
                CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\fat32.bin");

                DPRINT1("Install FAT32 bootcode: %S ==> %S\n", SrcPath, SystemRootPath->Buffer);
                Status = InstallBootCodeToDisk(SrcPath, SystemRootPath->Buffer, InstallFat32BootCode);
                DPRINT1("Status: 0x%08X\n", Status);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("InstallBootCodeToDisk(FAT32) failed (Status %lx)\n", Status);
                    return Status;
                }
            }
            else // if (wcsicmp(FileSystemName, L"FAT") == 0)
            {
                /* Install FAT16 bootcode */
                CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\fat.bin");

                DPRINT1("Install FAT16 bootcode: %S ==> %S\n", SrcPath, SystemRootPath->Buffer);
                Status = InstallBootCodeToDisk(SrcPath, SystemRootPath->Buffer, InstallFat16BootCode);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("InstallBootCodeToDisk(FAT16) failed (Status %lx)\n", Status);
                    return Status;
                }
            }
        }
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
InstallBtrfsBootcodeToPartition(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath)
{
    NTSTATUS Status;
    BOOLEAN DoesFreeLdrExist;
    WCHAR SrcPath[MAX_PATH];
    WCHAR DstPath[MAX_PATH];

    /* BTRFS partition */
    DPRINT("System path: '%wZ'\n", SystemRootPath);

    /* Install the bootloader */
    Status = InstallBootloaderFiles(SystemRootPath, SourceRootPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallBootloaderFiles() failed (Status %lx)\n", Status);
        return Status;
    }

    /* Prepare for possibly updating 'freeldr.ini' */
    DoesFreeLdrExist = DoesFileExist_2(SystemRootPath->Buffer, L"freeldr.ini");
    if (DoesFreeLdrExist)
    {
        /* Update existing 'freeldr.ini' */
        DPRINT1("Update existing 'freeldr.ini'\n");
        Status = UpdateFreeLoaderIni(SystemRootPath->Buffer, DestinationArcPath->Buffer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("UpdateFreeLoaderIni() failed (Status %lx)\n", Status);
            return Status;
        }
    }

    /* Check for *nix bootloaders */

    /* Create or update 'freeldr.ini' */
    if (DoesFreeLdrExist == FALSE)
    {
        /* Create new 'freeldr.ini' */
        DPRINT1("Create new 'freeldr.ini'\n");

        /* Certainly SysLinux, GRUB, LILO... or an unknown boot loader */
        DPRINT1("*nix or unknown boot loader found\n");

        if (IsThereAValidBootSector(SystemRootPath->Buffer))
        {
            PCWSTR BootSector = L"BOOTSECT.OLD";

            Status = CreateFreeLoaderIniForReactOSAndBootSector(
                         SystemRootPath->Buffer, DestinationArcPath->Buffer,
                         L"Linux", L"\"Linux\"",
                         SystemRootPath->Buffer, BootSector);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("CreateFreeLoaderIniForReactOSAndBootSector() failed (Status %lx)\n", Status);
                return Status;
            }

            /* Save current bootsector */
            CombinePaths(DstPath, ARRAYSIZE(DstPath), 2, SystemRootPath->Buffer, BootSector);

            DPRINT1("Save bootsector: %S ==> %S\n", SystemRootPath->Buffer, DstPath);
            Status = SaveBootSector(SystemRootPath->Buffer, DstPath, BTRFS_BOOTSECTOR_SIZE);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SaveBootSector() failed (Status %lx)\n", Status);
                return Status;
            }
        }
        else
        {
            Status = CreateFreeLoaderIniForReactOS(SystemRootPath->Buffer, DestinationArcPath->Buffer);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("CreateFreeLoaderIniForReactOS() failed (Status %lx)\n", Status);
                return Status;
            }
        }

        /* Install new bootsector on the disk */
        /* Install BTRFS bootcode */
        CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\btrfs.bin");

        DPRINT1("Install BTRFS bootcode: %S ==> %S\n", SrcPath, SystemRootPath->Buffer);
        Status = InstallBootCodeToDisk(SrcPath, SystemRootPath->Buffer, InstallBtrfsBootCode);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("InstallBootCodeToDisk(BTRFS) failed (Status %lx)\n", Status);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
InstallNtfsBootcodeToPartition(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath)
{
    NTSTATUS Status;
    BOOLEAN DoesFreeLdrExist;
    WCHAR SrcPath[MAX_PATH];
    WCHAR DstPath[MAX_PATH];

    /* NTFS partition */
    DPRINT("System path: '%wZ'\n", SystemRootPath);

    /* Install the bootloader */
    Status = InstallBootloaderFiles(SystemRootPath, SourceRootPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallBootloaderFiles() failed (Status %lx)\n", Status);
        return Status;
    }

    /* Prepare for possibly updating 'freeldr.ini' */
    DoesFreeLdrExist = DoesFileExist_2(SystemRootPath->Buffer, L"freeldr.ini");
    if (DoesFreeLdrExist)
    {
        /* Update existing 'freeldr.ini' */
        DPRINT1("Update existing 'freeldr.ini'\n");
        Status = UpdateFreeLoaderIni(SystemRootPath->Buffer, DestinationArcPath->Buffer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("UpdateFreeLoaderIni() failed (Status %lx)\n", Status);
            return Status;
        }

        return STATUS_SUCCESS;
    }

    /* Check for *nix bootloaders */

    DPRINT1("Create new 'freeldr.ini'\n");

    /* Certainly SysLinux, GRUB, LILO... or an unknown boot loader */
    DPRINT1("*nix or unknown boot loader found\n");

    if (IsThereAValidBootSector(SystemRootPath->Buffer))
    {
        PCWSTR BootSector = L"BOOTSECT.OLD";

        Status = CreateFreeLoaderIniForReactOSAndBootSector(
                     SystemRootPath->Buffer, DestinationArcPath->Buffer,
                     L"Linux", L"\"Linux\"",
                     SystemRootPath->Buffer, BootSector);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CreateFreeLoaderIniForReactOSAndBootSector() failed (Status %lx)\n", Status);
            return Status;
        }

        /* Save current bootsector */
        CombinePaths(DstPath, ARRAYSIZE(DstPath), 2, SystemRootPath->Buffer, BootSector);

        DPRINT1("Save bootsector: %S ==> %S\n", SystemRootPath->Buffer, DstPath);
        Status = SaveBootSector(SystemRootPath->Buffer, DstPath, NTFS_BOOTSECTOR_SIZE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SaveBootSector() failed (Status %lx)\n", Status);
            return Status;
        }
    }
    else
    {
        Status = CreateFreeLoaderIniForReactOS(SystemRootPath->Buffer, DestinationArcPath->Buffer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CreateFreeLoaderIniForReactOS() failed (Status %lx)\n", Status);
            return Status;
        }
    }

    /* Install new bootsector on the disk */

    /* Install NTFS bootcode */
    CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\ntfs.bin");

    DPRINT1("Install NTFS bootcode: %S ==> %S\n", SrcPath, SystemRootPath->Buffer);
    Status = InstallBootCodeToDisk(SrcPath, SystemRootPath->Buffer, InstallNtfsBootCode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InstallBootCodeToDisk(NTFS) failed (Status %lx)\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
InstallVBRToPartition(
    _In_ PCUNICODE_STRING SystemRootPath,
    _In_ PCUNICODE_STRING SourceRootPath,
    _In_ PCUNICODE_STRING DestinationArcPath,
    _In_ PCWSTR FileSystemName)
{
    if (_wcsicmp(FileSystemName, L"FAT")   == 0 ||
        _wcsicmp(FileSystemName, L"FAT32") == 0)
    {
        return InstallFatBootcodeToPartition(SystemRootPath,
                                             SourceRootPath,
                                             DestinationArcPath,
                                             FileSystemName);
    }
    else if (_wcsicmp(FileSystemName, L"NTFS") == 0)
    {
        return InstallNtfsBootcodeToPartition(SystemRootPath,
                                              SourceRootPath,
                                              DestinationArcPath);
    }
    else if (_wcsicmp(FileSystemName, L"BTRFS") == 0)
    {
        return InstallBtrfsBootcodeToPartition(SystemRootPath,
                                               SourceRootPath,
                                               DestinationArcPath);
    }
    /*
    else if (_wcsicmp(FileSystemName, L"EXT2")  == 0 ||
             _wcsicmp(FileSystemName, L"EXT3")  == 0 ||
             _wcsicmp(FileSystemName, L"EXT4")  == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }
    */
    else
    {
        /* Unknown file system */
        DPRINT1("Unknown file system '%S'\n", FileSystemName);
    }

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

    if (DiskStyle == PARTITION_STYLE_GPT)
        return InstallEfiSystemPartition(SystemRootPath, SourceRootPath, DestinationArcPath,
                                         !!(Options & BOOT_OPTION_AFTER_WINDOWS));

    // FIXME: We currently only support BIOS-based PCs
    // TODO: Support other platforms
    if (!IsBIOS)
        return STATUS_NOT_SUPPORTED;

    if (InstallType <= 1)
    {
        /* Step 1: Write the VBR */
        Status = InstallVBRToPartition(SystemRootPath,
                                       SourceRootPath,
                                       DestinationArcPath,
                                       FileSystem);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("InstallVBRToPartition() failed (Status 0x%08lx)\n", Status);
            return ERROR_WRITE_BOOT; // Status; STATUS_BAD_MASTER_BOOT_RECORD;
        }

        /* Step 2: Write the MBR if the disk containing the
         * system partition is MBR and not a super-floppy */
        if ((InstallType == 1) && (DiskStyle == PARTITION_STYLE_MBR) && !IsSuperFloppy)
        {
            WCHAR SystemDiskPath[MAX_PATH];
            RtlStringCchPrintfW(SystemDiskPath, _countof(SystemDiskPath),
                                L"\\Device\\Harddisk%d\\Partition0",
                                DiskNumber);
            Status = InstallMbrBootCodeToDisk(SystemRootPath,
                                              SourceRootPath,
                                              SystemDiskPath);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("InstallMbrBootCodeToDisk() failed (Status 0x%08lx)\n", Status);
                return ERROR_INSTALL_BOOTCODE; // Status; STATUS_BAD_MASTER_BOOT_RECORD;
            }
        }
    }
    else if (InstallType == 2)
    {
        WCHAR SrcPath[MAX_PATH];

        // FIXME: We currently only support FAT12 file system.
        if (_wcsicmp(FileSystem, L"FAT") != 0)
            return STATUS_NOT_SUPPORTED;

        // TODO: In the future, we'll be able to use InstallVBRToPartition()
        // directly, instead of re-doing manually the copy steps below.

        /* Install the bootloader to the boot partition */
        Status = InstallBootloaderFiles(SystemRootPath, SourceRootPath);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("InstallBootloaderFiles() failed (Status 0x%08lx)\n", Status);
            return Status;
        }

        /* Create new 'freeldr.ini' */
        DPRINT("Create new 'freeldr.ini'\n");
        Status = CreateFreeLoaderIniForReactOS(SystemRootPath->Buffer, DestinationArcPath->Buffer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CreateFreeLoaderIniForReactOS() failed (Status 0x%08lx)\n", Status);
            return Status;
        }

        /* Install FAT12 bootsector */
        CombinePaths(SrcPath, ARRAYSIZE(SrcPath), 2, SourceRootPath->Buffer, L"\\loader\\fat.bin");

        DPRINT1("Install FAT12 bootcode: %S ==> %S\n", SrcPath, SystemRootPath->Buffer);
        Status = InstallBootCodeToDisk(SrcPath, SystemRootPath->Buffer, InstallFat12BootCode);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("InstallBootCodeToDisk(FAT12) failed (Status 0x%08lx)\n", Status);
            return Status;
        }
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
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
 * Installs FreeLoader on the system and configure the boot entries.
 *
 * @todo
 * Split this function into just the InstallBootManager, and a separate one
 * for just the boot entries.
 *
 * @param[in]   ArchType
 * The target architecture.
 *
 * @param[in]   SystemRootPath
 * The system partition path, where the FreeLdr boot manager and its
 * settings are saved to.
 *
 * @param[in]   SourceRootPath
 * The installation source, where to copy the FreeLdr boot manager from.
 *
 * @param[in]   DestinationArcPath
 * The ReactOS installation path in ARC format.
 *
 * @param[in]   Options
 * For BIOS-based PCs:
 * LOBYTE:
 *      0: Install only on VBR;
 *      1: Install on both VBR and MBR.
 *      2: Install on removable disk.
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
    Status = InstallBootManagerAndBootEntriesWorker(
                ArchType, SystemRootPath,
                DiskNumber, PartitionStyle, IsSuperFloppy, FileSystem,
                SourceRootPath, DestinationArcPath, Options);

Quit:
    NtClose(DeviceHandle);
    return Status;
}

/* EOF */
