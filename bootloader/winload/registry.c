/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SYSTEM hive, national language data and boot driver loading
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

#define OSL_SYSTEM_HIVE     "system32\\config\\SYSTEM"
#define OSL_SYSTEM_HIVE_LOG "system32\\config\\SYSTEM.LOG1"
#define OSL_NLS_NAME_CHARS 32
#define OSL_NLS_ANSI       0
#define OSL_NLS_OEM        1
#define OSL_NLS_CASE       2
#define OSL_NLS_FILES      3
#define OSL_DEFAULT_DPI    LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT

static CMHIVE OslHive;

PHHIVE OslSystemHive;
HCELL_INDEX OslControlSet = HCELL_NIL;

static
PVOID
OslLoadFile(
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot,
    _In_ PCSTR RelativePath,
    _In_ TYPE_OF_MEMORY MemoryType,
    _Out_ PULONG Size)
{
    CHAR Path[OSL_PATH_CHARS];
    PVOID File, Memory;

    *Size = 0;

    if (!NT_SUCCESS(RtlStringCbPrintfA(Path, sizeof(Path), "%s%s", SystemRoot, RelativePath)))
        return NULL;

    File = FsReadFileA(Device, Path, Size);
    if (!File)
        return NULL;

    Memory = MmAllocateMemoryWithType(*Size, MemoryType);
    if (Memory)
        RtlCopyMemory(Memory, File, *Size);

    FwFreePages(File, *Size);
    return Memory;
}

static
BOOLEAN
OslRecoverSystemHive(
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot,
    _Inout_ PVOID* Hive,
    _Inout_ PULONG Size)
{
    CHAR Path[OSL_PATH_CHARS];
    PVOID Log, Grown;
    ULONG LogSize, Required, NewSize;
    BOOLEAN Recovered = FALSE;

    if (!NT_SUCCESS(RtlStringCbPrintfA(Path, sizeof(Path), "%s%s", SystemRoot, OSL_SYSTEM_HIVE_LOG)))
        return FALSE;

    Log = FsReadFileA(Device, Path, &LogSize);
    if (!Log)
        return FALSE;

    if (HvpApplyIncrementalLog(*Hive, 0, Log, LogSize, FALSE, NULL, &Required))
    {
        NewSize = HBLOCK_SIZE + Required;
        if (NewSize > *Size)
        {
            Grown = MmAllocateMemoryWithType(NewSize, LoaderRegistryData);
            if (Grown)
            {
                RtlCopyMemory(Grown, *Hive, *Size);
                RtlZeroMemory((PUCHAR)Grown + *Size, NewSize - *Size);
                MmFreeMemory(*Hive);
                *Hive = Grown;
                *Size = NewSize;
            }
        }

        if (NewSize <= *Size)
            Recovered = HvpApplyIncrementalLog(*Hive, Required, Log, LogSize, TRUE, NULL, &Required);
    }

    FwFreePages(Log, LogSize);
    return Recovered;
}

static
BOOLEAN
OslInitializeSystemHive(
    _In_ PVOID Hive,
    _In_ ULONG Size)
{
    CM_CHECK_REGISTRY_STATUS CheckStatus;

    if (((PHBASE_BLOCK)Hive)->Length > Size - HBLOCK_SIZE)
        return FALSE;

    RtlZeroMemory(&OslHive, sizeof(OslHive));

    if (!NT_SUCCESS(HvInitialize(&OslHive.Hive,
                                 HINIT_FLAT,
                                 0,
                                 HFILE_TYPE_PRIMARY,
                                 Hive,
                                 CmpAllocate,
                                 CmpFree,
                                 NULL,
                                 NULL,
                                 1,
                                 NULL)))
    {
        return FALSE;
    }

    CheckStatus = CmCheckRegistry(&OslHive,
                                  CM_CHECK_REGISTRY_BOOTLOADER_PURGE_VOLATILES |
                                  CM_CHECK_REGISTRY_VALIDATE_HIVE);
    return CM_CHECK_REGISTRY_SUCCESS(CheckStatus);
}

BOOLEAN
OslLoadSystemHive(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot)
{
    UNICODE_STRING Default = RTL_CONSTANT_STRING(L"Default");
    BOOLEAN AutoSelect;
    BOOLEAN Recovered = FALSE;
    PVOID Hive;
    ULONG Size;

    Hive = OslLoadFile(Device, SystemRoot, OSL_SYSTEM_HIVE, LoaderRegistryData, &Size);
    if (!Hive || Size < HBLOCK_SIZE)
        return FALSE;

    if (((PHBASE_BLOCK)Hive)->Sequence1 != ((PHBASE_BLOCK)Hive)->Sequence2)
        Recovered = OslRecoverSystemHive(Device, SystemRoot, &Hive, &Size);

    if (!OslInitializeSystemHive(Hive, Size))
    {
        if (Recovered ||
            !OslRecoverSystemHive(Device, SystemRoot, &Hive, &Size) ||
            !OslInitializeSystemHive(Hive, Size))
        {
            return FALSE;
        }

        Recovered = TRUE;
    }

    ((PHBASE_BLOCK)Hive)->BootRecover = Recovered ? HBOOT_BOOT_RECOVERED_BY_HIVE_LOG
                                                  : HBOOT_NO_BOOT_RECOVER;

    OslSystemHive = &OslHive.Hive;
    OslControlSet = CmpFindControlSet(OslSystemHive,
                                      OslSystemHive->BaseBlock->RootCell,
                                      &Default,
                                      &AutoSelect);
    if (OslControlSet == HCELL_NIL)
        return FALSE;

    LoaderBlock->RegistryBase = PaToVa(Hive);
    LoaderBlock->RegistryLength = Size;
    return TRUE;
}

static
HCELL_INDEX
OslOpenControlKey(
    _In_ PCWSTR First,
    _In_opt_ PCWSTR Second,
    _In_opt_ PCWSTR Third)
{
    HCELL_INDEX Key = BlRegSubKey(OslSystemHive, OslControlSet, First);

    if (Second)
        Key = BlRegSubKey(OslSystemHive, Key, Second);
    if (Third)
        Key = BlRegSubKey(OslSystemHive, Key, Third);

    return Key;
}

static
BOOLEAN
OslQueryString(
    _In_ HCELL_INDEX Key,
    _In_ PCWSTR Name,
    _Out_writes_z_(Chars) PWSTR Buffer,
    _In_ ULONG Chars)
{
    PUCHAR Data;
    ULONG Type, Length;

    Data = BlRegValue(OslSystemHive, Key, Name, &Type, &Length);
    if (!Data || Type != REG_SZ)
        return FALSE;

    return NT_SUCCESS(RtlStringCchCopyNW(Buffer, Chars, (PCWSTR)Data, Length / sizeof(WCHAR)));
}

ULONG
OslQueryDpi(VOID)
{
    WCHAR Profile[8];
    HCELL_INDEX Key;
    PUCHAR Data;
    ULONG Type, Length;
    ULONG Current = 0;

    Key = OslOpenControlKey(L"Control", L"IDConfigDB", NULL);
    Data = BlRegValue(OslSystemHive, Key, L"CurrentConfig", &Type, &Length);
    if (Data && Type == REG_DWORD && Length == sizeof(ULONG))
        Current = LeUlong(Data);

    RtlStringCchPrintfW(Profile, RTL_NUMBER_OF(Profile), L"%04lu", Current);
    Key = OslOpenControlKey(L"Hardware Profiles", Profile, L"Software");
    Key = BlRegSubKey(OslSystemHive, Key, L"Fonts");
    Data = BlRegValue(OslSystemHive, Key, L"LogPixels", &Type, &Length);
    if (!Data ||
        Type != REG_DWORD ||
        Length != sizeof(ULONG) ||
        LeUlong(Data) < LOADER_PARAMETER_FRAMEBUFFER_DPI_MIN ||
        LeUlong(Data) > LOADER_PARAMETER_FRAMEBUFFER_DPI_MAX)
    {
        return OSL_DEFAULT_DPI;
    }

    return LeUlong(Data);
}

static
BOOLEAN
OslQueryNlsFile(
    _In_ PCWSTR Group,
    _In_ PCWSTR Selector,
    _Out_writes_z_(Chars) PSTR FileName,
    _In_ ULONG Chars)
{
    WCHAR Name[OSL_NLS_NAME_CHARS];
    WCHAR File[OSL_NLS_NAME_CHARS];
    HCELL_INDEX Key = OslOpenControlKey(L"Control", L"NLS", Group);

    if (!OslQueryString(Key, Selector, Name, RTL_NUMBER_OF(Name)) ||
        !OslQueryString(Key, Name, File, RTL_NUMBER_OF(File)))
    {
        return FALSE;
    }

    return NT_SUCCESS(RtlStringCchPrintfA(FileName, Chars, "system32\\%ws", File));
}

BOOLEAN
OslLoadNlsData(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot)
{
    CHAR Names[OSL_NLS_FILES][OSL_PATH_CHARS];
    CHAR Path[OSL_PATH_CHARS];
    PNLS_DATA_BLOCK Nls = LoaderBlock->NlsData;
    PVOID Files[OSL_NLS_FILES] = {NULL};
    ULONG Sizes[OSL_NLS_FILES] = {0};
    ULONG Offsets[OSL_NLS_FILES];
    PUCHAR Block;
    ULONG Total = 0;
    ULONG i;
    BOOLEAN Loaded = TRUE;

    if (!OslQueryNlsFile(L"CodePage", L"ACP", Names[OSL_NLS_ANSI], sizeof(Names[0])) ||
        !OslQueryNlsFile(L"CodePage", L"OEMCP", Names[OSL_NLS_OEM], sizeof(Names[0])) ||
        !OslQueryNlsFile(L"Language", L"Default", Names[OSL_NLS_CASE], sizeof(Names[0])))
    {
        return FALSE;
    }

    for (i = 0; i < OSL_NLS_FILES; i++)
    {
        if (i == OSL_NLS_OEM && _stricmp(Names[OSL_NLS_OEM], Names[OSL_NLS_ANSI]) == 0)
        {
            Offsets[i] = Offsets[OSL_NLS_ANSI];
            continue;
        }

        RtlStringCbPrintfA(Path, sizeof(Path), "%s%s", SystemRoot, Names[i]);
        Files[i] = FsReadFileA(Device, Path, &Sizes[i]);
        if (!Files[i])
        {
            FwPrint(L"%hs could not be read.\r\n", Path);
            Loaded = FALSE;
            break;
        }

        Offsets[i] = Total;
        Total += ALIGN_UP_BY(Sizes[i], MM_PAGE_SIZE);
    }

    Block = Loaded ? MmAllocateMemoryWithType(Total, LoaderNlsData) : NULL;

    for (i = 0; i < OSL_NLS_FILES; i++)
    {
        if (!Files[i])
            continue;

        if (Block)
            RtlCopyMemory(Block + Offsets[i], Files[i], Sizes[i]);

        FwFreePages(Files[i], Sizes[i]);
    }

    if (!Block)
        return FALSE;

    Nls->AnsiCodePageData = PaToVa(Block + Offsets[OSL_NLS_ANSI]);
    Nls->OemCodePageData = PaToVa(Block + Offsets[OSL_NLS_OEM]);
    Nls->UnicodeCodePageData = PaToVa(Block + Offsets[OSL_NLS_CASE]);
    return TRUE;
}

static const PCWSTR OslLegacyBootServices[] =
{
    L"Floppy",
    L"Vga",
    L"VgaSave",
    L"Sacdrv",
    L"BusLogic",
};

static
VOID
OslDisableLegacyBootServices(VOID)
{
    UNICODE_STRING StartName = RTL_CONSTANT_STRING(L"Start");
    HCELL_INDEX Services, Service, Cell;
    PCM_KEY_NODE Node;
    PCM_KEY_VALUE Value;
    ULONG Length, Index;

    Services = BlRegSubKey(OslSystemHive, OslControlSet, L"Services");
    for (Index = 0; Index < RTL_NUMBER_OF(OslLegacyBootServices); Index++)
    {
        Service = BlRegSubKey(OslSystemHive, Services, OslLegacyBootServices[Index]);
        if (Service == HCELL_NIL)
            continue;

        Node = (PCM_KEY_NODE)HvGetCell(OslSystemHive, Service);
        if (!Node)
            continue;
        Cell = CmpFindValueByName(OslSystemHive, Node, &StartName);
        HvReleaseCell(OslSystemHive, Service);
        if (Cell == HCELL_NIL)
            continue;

        Value = (PCM_KEY_VALUE)HvGetCell(OslSystemHive, Cell);
        if (!Value)
            continue;
        if (Value->Type == REG_DWORD &&
            CmpIsKeyValueSmall(&Length, Value->DataLength) &&
            Length == sizeof(ULONG) &&
            Value->Data != SERVICE_DISABLED)
        {
            Value->Data = SERVICE_DISABLED;
        }
        HvReleaseCell(OslSystemHive, Cell);
    }
}

BOOLEAN
OslLoadBootDrivers(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot)
{
    CHAR Directory[OSL_PATH_CHARS], RelativePath[OSL_PATH_CHARS];
    PLIST_ENTRY Head = &LoaderBlock->BootDriverListHead;
    PLIST_ENTRY Link, Next;
    PBOOT_DRIVER_NODE Node;
    PBOOT_DRIVER_LIST_ENTRY Driver;
    PLDR_DATA_TABLE_ENTRY Module;
    PSTR FileName;

    OslDisableLegacyBootServices();

    if (!CmpFindDrivers(OslSystemHive, OslControlSet, BootLoad, FsGetDriverName(Device), Head) ||
        !CmpSortDriverList(OslSystemHive, OslControlSet, Head) ||
        !CmpResolveDriverDependencies(Head))
    {
        return FALSE;
    }

    for (Link = Head->Flink; Link != Head; Link = Next)
    {
        Next = Link->Flink;
        Node = CONTAINING_RECORD(Link, BOOT_DRIVER_NODE, ListEntry.Link);
        Driver = &Node->ListEntry;

        if (!NT_SUCCESS(RtlStringCbPrintfA(RelativePath, sizeof(RelativePath), "%wZ", &Driver->FilePath)))
        {
            RemoveEntryList(Link);
            continue;
        }

        FileName = strrchr(RelativePath, '\\');
        FileName = FileName ? FileName + 1 : RelativePath;

        RtlStringCbPrintfA(Directory, sizeof(Directory), "%s%s", SystemRoot, RelativePath);
        Directory[strlen(SystemRoot) + (FileName - RelativePath)] = ANSI_NULL;

        if (!PeLoadModule(&LoaderBlock->LoadOrderListHead,
                          Device,
                          Directory,
                          FileName,
                          FileName,
                          LoaderBootDriver,
                          0,
                          &Module))
        {
            FwPrint(L"The boot driver %wZ could not be loaded.\r\n", &Driver->FilePath);
            RemoveEntryList(Link);
            continue;
        }

        Driver->LdrEntry = PaToVa(Module);
        Driver->RegistryPath.Buffer = PaToVa(Driver->RegistryPath.Buffer);
        Driver->FilePath.Buffer = PaToVa(Driver->FilePath.Buffer);
        Node->Name.Buffer = PaToVa(Node->Name.Buffer);
        if (Node->Group.Buffer)
            Node->Group.Buffer = PaToVa(Node->Group.Buffer);
    }

    return TRUE;
}

VOID
OslLoadDriverDatabase(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot)
{
    PVOID Database;
    ULONG Size;

    Database = OslLoadFile(Device, SystemRoot, "AppPatch\\drvmain.sdb", LoaderRegistryData, &Size);
    if (!Database)
        return;

    LoaderBlock->Extension->DrvDBImage = PaToVa(Database);
    LoaderBlock->Extension->DrvDBSize = Size;
}
