/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot Configuration Data store access
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#define BCD_ELEMENT_DEVICE_OFFSET   0x10
#define BCD_DEVICE_SIZE_OFFSET      0x08
#define BCD_DEVICE_PARTITION_OFFSET 0x10
#define BCD_DEVICE_LOCAL_OFFSET     0x20
#define BCD_DEVICE_STYLE_OFFSET     0x24
#define BCD_DEVICE_DISK_OFFSET      0x28
#define BCD_DEVICE_SIGNATURE_OFFSET 0x28
#define BCD_DEVICE_HEADER_LENGTH    0x10
#define BCD_DEVICE_PARTITION_LENGTH 0x48
#define BCD_DISK_TYPE_OFFSET        0x10
#define BCD_DISK_FILE_OFFSET        0x28
#define BCD_FILE_LENGTH_OFFSET      0x04
#define BCD_FILE_TYPE_OFFSET        0x08
#define BCD_FILE_DEVICE_OFFSET      0x0C

#define BCD_DEVICE_DISK      0
#define BCD_DEVICE_BOOT      5
#define BCD_DEVICE_PARTITION 6
#define BCD_DISK_RAMDISK     3
#define BCD_FILE_VERSION     1
#define BCD_FILE_PATH        5
#define BCD_PARTITION_GPT    0
#define BCD_PARTITION_MBR    1

static CMHIVE BcdHive;
static HCELL_INDEX BcdObjects = HCELL_NIL;

HCELL_INDEX
BlRegSubKey(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Key,
    _In_ PCWSTR Name)
{
    UNICODE_STRING String;
    PCM_KEY_NODE Node;
    HCELL_INDEX Cell;

    if (Key == HCELL_NIL)
        return HCELL_NIL;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Key);
    if (!Node)
        return HCELL_NIL;

    RtlInitUnicodeString(&String, Name);
    Cell = CmpFindSubKeyByName(Hive, Node, &String);
    HvReleaseCell(Hive, Key);
    return Cell;
}

PUCHAR
BlRegValue(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Key,
    _In_ PCWSTR Name,
    _Out_ PULONG Type,
    _Out_ PULONG Length)
{
    UNICODE_STRING String;
    PCM_KEY_NODE Node;
    PCM_KEY_VALUE Value;
    HCELL_INDEX Cell;
    PUCHAR Data;

    if (Key == HCELL_NIL)
        return NULL;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Key);
    if (!Node)
        return NULL;

    RtlInitUnicodeString(&String, Name);
    Cell = CmpFindValueByName(Hive, Node, &String);
    HvReleaseCell(Hive, Key);
    if (Cell == HCELL_NIL)
        return NULL;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
    if (!Value)
        return NULL;

    *Type = Value->Type;
    Data = (PUCHAR)CmpValueToData(Hive, Value, Length);
    HvReleaseCell(Hive, Cell);
    return Data;
}

static
HCELL_INDEX
BcdSubKey(
    _In_ HCELL_INDEX Key,
    _In_ PCWSTR Name)
{
    return BlRegSubKey(&BcdHive.Hive, Key, Name);
}

static
PUCHAR
BcdValueData(
    _In_ HCELL_INDEX Key,
    _In_ PCWSTR Name,
    _Out_ PULONG Type,
    _Out_ PULONG Length)
{
    return BlRegValue(&BcdHive.Hive, Key, Name, Type, Length);
}

static
PCWSTR
BcdListEntry(
    _In_ PCWSTR List,
    _In_ ULONG Length,
    _In_ ULONG Index)
{
    ULONG Chars = Length / sizeof(WCHAR);
    ULONG Start = 0;
    ULONG i;

    for (i = 0; i < Chars; i++)
    {
        if (List[i] != UNICODE_NULL)
            continue;
        if (i == Start)
            return NULL;
        if (Index-- == 0)
            return &List[Start];
        Start = i + 1;
    }

    return NULL;
}

static
PUCHAR
BcdElementData(
    _In_ HCELL_INDEX Elements,
    _In_ ULONG Element,
    _Out_ PULONG Type,
    _Out_ PULONG Length)
{
    WCHAR Name[9];
    HCELL_INDEX Key;

    RtlStringCbPrintfW(Name, sizeof(Name), L"%08lx", Element);
    Key = BcdSubKey(Elements, Name);
    if (Key == HCELL_NIL)
        return NULL;

    return BcdValueData(Key, L"Element", Type, Length);
}

static
PUCHAR
BcdFindElement(
    _In_ HCELL_INDEX Elements,
    _In_ ULONG Element,
    _Out_ PULONG Type,
    _Out_ PULONG Length,
    _In_ ULONG Depth)
{
    HCELL_INDEX Key;
    PUCHAR Data;
    PCWSTR List, Id;
    ULONG ListType, ListLength, Index;

    Data = BcdElementData(Elements, Element, Type, Length);
    if (Data || Depth == 0)
        return Data;

    List = (PCWSTR)BcdElementData(Elements,
                                  BcdLibraryObjectList_InheritedObjects,
                                  &ListType,
                                  &ListLength);
    if (!List || ListType != REG_MULTI_SZ)
        return NULL;

    for (Index = 0; (Id = BcdListEntry(List, ListLength, Index)) != NULL; Index++)
    {
        Key = BcdSubKey(BcdSubKey(BcdObjects, Id), L"Elements");
        if (Key == HCELL_NIL)
            continue;

        Data = BcdFindElement(Key, Element, Type, Length, Depth - 1);
        if (Data)
            return Data;
    }

    return NULL;
}

static
PVOID
BcdReadFile(
    _In_opt_ PVOID Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    return Device ? FwReadDeviceFile(Device, Path, Size) : FwReadBootFile(Path, Size);
}

static
BOOLEAN
BcdApplyLog(
    _In_opt_ PVOID Device,
    _In_ PCWSTR Path,
    _In_ PCWSTR Suffix,
    _Inout_ PVOID* Store,
    _Inout_ PULONG Size)
{
    WCHAR LogPath[BCD_PATH_CHARS];
    PVOID Log, Grown;
    ULONG LogSize, Required, NewSize;
    BOOLEAN Applied = FALSE;

    if (!NT_SUCCESS(RtlStringCchPrintfW(LogPath, RTL_NUMBER_OF(LogPath), L"%s%s", Path, Suffix)))
        return FALSE;

    Log = BcdReadFile(Device, LogPath, &LogSize);
    if (!Log)
        return FALSE;

    if (HvpApplyIncrementalLog(*Store, 0, Log, LogSize, FALSE, NULL, &Required))
    {
        NewSize = HBLOCK_SIZE + Required;
        if (NewSize > *Size)
        {
            Grown = FwAllocatePages(NewSize);
            if (Grown)
            {
                RtlCopyMemory(Grown, *Store, *Size);
                RtlZeroMemory((PUCHAR)Grown + *Size, NewSize - *Size);
                FwFreePages(*Store, *Size);
                *Store = Grown;
                *Size = NewSize;
            }
        }

        if (NewSize <= *Size)
            Applied = HvpApplyIncrementalLog(*Store, Required, Log, LogSize, TRUE, NULL, &Required);
    }

    FwFreePages(Log, LogSize);
    return Applied;
}

PVOID
BcdReadStoreFile(
    _In_opt_ PVOID Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    PVOID Store = BcdReadFile(Device, Path, Size);

    if (Store &&
        *Size >= HBLOCK_SIZE &&
        ((PHBASE_BLOCK)Store)->Sequence1 != ((PHBASE_BLOCK)Store)->Sequence2 &&
        !BcdApplyLog(Device, Path, L".LOG1", &Store, Size))
    {
        BcdApplyLog(Device, Path, L".LOG2", &Store, Size);
    }

    return Store;
}

BOOLEAN
BcdOpenStore(
    _In_reads_bytes_(Size) PVOID HiveBase,
    _In_ ULONG Size)
{
    PHHIVE Hive = &BcdHive.Hive;
    CM_CHECK_REGISTRY_STATUS CheckStatus;
    NTSTATUS Status;

    BcdObjects = HCELL_NIL;

    if (Size < HBLOCK_SIZE || ((PHBASE_BLOCK)HiveBase)->Length > Size - HBLOCK_SIZE)
        return FALSE;

    Status = HvInitialize(Hive,
                          HINIT_FLAT,
                          0,
                          HFILE_TYPE_PRIMARY,
                          HiveBase,
                          CmpAllocate,
                          CmpFree,
                          NULL,
                          NULL,
                          1,
                          NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;

    CheckStatus = CmCheckRegistry(&BcdHive,
                                  CM_CHECK_REGISTRY_BOOTLOADER_PURGE_VOLATILES |
                                  CM_CHECK_REGISTRY_VALIDATE_HIVE);
    if (!CM_CHECK_REGISTRY_SUCCESS(CheckStatus))
        return FALSE;

    BcdObjects = BcdSubKey(Hive->BaseBlock->RootCell, L"Objects");
    return BcdObjects != HCELL_NIL;
}

BOOLEAN
BcdOpenObject(
    _In_ PCWSTR Id,
    _Out_ PBCD_OBJECT Object)
{
    HCELL_INDEX Key, Description;
    PUCHAR Data;
    ULONG Type, Length;

    Key = BcdSubKey(BcdObjects, Id);
    Description = BcdSubKey(Key, L"Description");
    Object->Elements = BcdSubKey(Key, L"Elements");
    if (Description == HCELL_NIL || Object->Elements == HCELL_NIL)
        return FALSE;

    Data = BcdValueData(Description, L"Type", &Type, &Length);
    if (!Data || Type != REG_DWORD || Length < sizeof(ULONG))
        return FALSE;

    Object->Type = LeUlong(Data);
    return TRUE;
}

BOOLEAN
BcdGetString(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_writes_z_(Chars) PWSTR Buffer,
    _In_ ULONG Chars)
{
    PUCHAR Data;
    ULONG Type, Length;

    Data = BcdFindElement(Object->Elements, Element, &Type, &Length, BCD_MAX_INHERIT_DEPTH);
    if (!Data || Type != REG_SZ || Chars == 0)
        return FALSE;

    Length = min(Length / sizeof(WCHAR), Chars - 1);
    RtlCopyMemory(Buffer, Data, Length * sizeof(WCHAR));
    Buffer[Length] = UNICODE_NULL;
    return TRUE;
}

BOOLEAN
BcdGetObjectId(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _In_ ULONG Index,
    _Out_writes_z_(BCD_ID_CHARS) PWSTR Id)
{
    PUCHAR Data;
    PCWSTR Entry;
    ULONG Type, Length;

    Data = BcdFindElement(Object->Elements, Element, &Type, &Length, BCD_MAX_INHERIT_DEPTH);
    if (!Data || (Type != REG_SZ && Type != REG_MULTI_SZ))
        return FALSE;

    if (Type == REG_SZ)
    {
        if (Index != 0)
            return FALSE;

        return NT_SUCCESS(RtlStringCchCopyNW(Id,
                                             BCD_ID_CHARS,
                                             (PCWSTR)Data,
                                             Length / sizeof(WCHAR)));
    }

    Entry = BcdListEntry((PCWSTR)Data, Length, Index);
    if (!Entry)
        return FALSE;

    return NT_SUCCESS(RtlStringCchCopyW(Id, BCD_ID_CHARS, Entry));
}

BOOLEAN
BcdGetInteger(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_ PULONGLONG Value)
{
    PUCHAR Data;
    ULONG Type, Length, i;

    Data = BcdFindElement(Object->Elements, Element, &Type, &Length, BCD_MAX_INHERIT_DEPTH);
    if (!Data || Type != REG_BINARY || Length == 0 || Length > sizeof(ULONGLONG))
        return FALSE;

    *Value = 0;
    for (i = 0; i < Length; i++)
        *Value |= (ULONGLONG)Data[i] << (8 * i);

    return TRUE;
}

BOOLEAN
BcdGetBoolean(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_ PBOOLEAN Value)
{
    PUCHAR Data;
    ULONG Type, Length;

    Data = BcdFindElement(Object->Elements, Element, &Type, &Length, BCD_MAX_INHERIT_DEPTH);
    if (!Data || Type != REG_BINARY || Length == 0)
        return FALSE;

    *Value = (Data[0] != 0);
    return TRUE;
}

static
BOOLEAN
BcdParseDevice(
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ ULONG Length,
    _Out_ PBCD_DEVICE Device,
    _Out_ PULONG Size)
{
    const UCHAR* File;
    ULONG FileLength, ParentSize, Chars, i;

    if (Length < BCD_DEVICE_HEADER_LENGTH)
        return FALSE;

    *Size = LeUlong(Data + BCD_DEVICE_SIZE_OFFSET);
    if (*Size < BCD_DEVICE_HEADER_LENGTH || *Size > Length)
        return FALSE;

    switch (LeUlong(Data))
    {
        case BCD_DEVICE_BOOT:
            Device->Type = BcdDeviceBoot;
            return TRUE;

        case BCD_DEVICE_PARTITION:
            if (*Size < BCD_DEVICE_PARTITION_LENGTH || LeUlong(Data + BCD_DEVICE_LOCAL_OFFSET) != 0)
                return FALSE;

            switch (LeUlong(Data + BCD_DEVICE_STYLE_OFFSET))
            {
                case BCD_PARTITION_GPT:
                    Device->Type = BcdDeviceGptPartition;
                    RtlCopyMemory(Device->Partition, Data + BCD_DEVICE_PARTITION_OFFSET, sizeof(Device->Partition));
                    RtlCopyMemory(Device->Disk, Data + BCD_DEVICE_DISK_OFFSET, sizeof(Device->Disk));
                    return TRUE;

                case BCD_PARTITION_MBR:
                    Device->Type = BcdDeviceMbrPartition;
                    Device->PartitionOffset = LeUlonglong(Data + BCD_DEVICE_PARTITION_OFFSET);
                    Device->Signature = LeUlong(Data + BCD_DEVICE_SIGNATURE_OFFSET);
                    return TRUE;
            }

            return FALSE;

        case BCD_DEVICE_DISK:
            if (Device->Ramdisk ||
                *Size < BCD_DISK_FILE_OFFSET + BCD_FILE_DEVICE_OFFSET ||
                LeUlong(Data + BCD_DISK_TYPE_OFFSET) != BCD_DISK_RAMDISK)
            {
                return FALSE;
            }

            File = Data + BCD_DISK_FILE_OFFSET;
            FileLength = LeUlong(File + BCD_FILE_LENGTH_OFFSET);
            if (LeUlong(File) != BCD_FILE_VERSION ||
                LeUlong(File + BCD_FILE_TYPE_OFFSET) != BCD_FILE_PATH ||
                FileLength < BCD_FILE_DEVICE_OFFSET ||
                FileLength > *Size - BCD_DISK_FILE_OFFSET)
            {
                return FALSE;
            }

            Device->Ramdisk = TRUE;
            if (!BcdParseDevice(File + BCD_FILE_DEVICE_OFFSET,
                                FileLength - BCD_FILE_DEVICE_OFFSET,
                                Device,
                                &ParentSize))
            {
                return FALSE;
            }

            File += BCD_FILE_DEVICE_OFFSET + ParentSize;
            Chars = (FileLength - BCD_FILE_DEVICE_OFFSET - ParentSize) / sizeof(WCHAR);
            for (i = 0; i < Chars && i < RTL_NUMBER_OF(Device->RamdiskPath) - 1; i++)
            {
                Device->RamdiskPath[i] = File[i * sizeof(WCHAR)] | (File[i * sizeof(WCHAR) + 1] << 8);
                if (Device->RamdiskPath[i] == UNICODE_NULL)
                    break;
            }

            Device->RamdiskPath[i] = UNICODE_NULL;
            return TRUE;
    }

    return FALSE;
}

BOOLEAN
BcdGetDevice(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_ PBCD_DEVICE Device)
{
    PUCHAR Data;
    ULONG Type, Length, Size;

    Data = BcdFindElement(Object->Elements, Element, &Type, &Length, BCD_MAX_INHERIT_DEPTH);
    if (!Data || Type != REG_BINARY || Length < BCD_ELEMENT_DEVICE_OFFSET)
        return FALSE;

    RtlZeroMemory(Device, sizeof(*Device));
    return BcdParseDevice(Data + BCD_ELEMENT_DEVICE_OFFSET,
                          Length - BCD_ELEMENT_DEVICE_OFFSET,
                          Device,
                          &Size);
}
