/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot entry that installs a downloaded LiberNT image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <objbase.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <ndk/setypes.h>
#include <ndk/cmfuncs.h>

#include "update.h"

#define NDEBUG
#include <debug.h>

#define BCD_BOOTMGR             L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define BCD_APPLICATION_DEVICE  0x11000001
#define BCD_APPLICATION_PATH    0x12000002
#define BCD_DESCRIPTION         0x12000004
#define BCD_LOAD_OPTIONS        0x12000030
#define BCD_OS_DEVICE           0x21000001
#define BCD_DEFAULT_OBJECT      0x23000003
#define BCD_DISPLAY_ORDER       0x24000001

#define BCD_DEVICE_PREFIX       16
#define BCD_DEVICE_SIZE         0x08
#define BCD_DEVICE_MIN          0x10
#define BCD_ORDER_MAX           4096
#define BCD_ELEMENT_MAX         1024

#define BCD_OBJECTS_MAX         128
#define BCD_ELEMENT_TYPE        0x10
#define BCD_ELEMENT_PARTITION   0x20
#define BCD_ELEMENT_STYLE       0x34
#define BCD_ELEMENT_DISK        0x38
#define BCD_ELEMENT_PARTITION_SIZE 0x58
#define BCD_DEVICE_PARTITION    6
#define BCD_STYLE_GPT           0
#define BCD_STYLE_MBR           1
#define LAYOUT_PARTITIONS       128
#define BCD_OPTIONS_CHARS       512

#define INSTALL_IMAGE_OPTION    L"INSTALLIMAGE="
#define INSTALL_HASH_OPTION     L"INSTALLSHA256="

static LONG
OpenElement(
    _In_ HKEY Store,
    _In_ PCWSTR Id,
    _In_ DWORD Element,
    _In_ REGSAM Access,
    _In_ BOOL Create,
    _Out_ PHKEY Key)
{
    WCHAR Path[96];

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"Objects\\%s\\Elements\\%08x", Id, Element)))
        return ERROR_BUFFER_OVERFLOW;
    if (Create)
        return RegCreateKeyExW(Store, Path, 0, NULL, REG_OPTION_BACKUP_RESTORE, Access, NULL, Key, NULL);
    return RegOpenKeyExW(Store, Path, REG_OPTION_BACKUP_RESTORE, Access, Key);
}

static LONG
CopyKeyTree(
    _In_ HKEY Source,
    _In_ HKEY Target)
{
    WCHAR Name[256];
    BYTE Data[BCD_ELEMENT_MAX * 4];
    DWORD Index, Chars, Size, Type;
    HKEY From, To;
    LONG Error = ERROR_SUCCESS;

    for (Index = 0; Error == ERROR_SUCCESS; Index++)
    {
        Chars = ARRAYSIZE(Name);
        Size = sizeof(Data);
        if (RegEnumValueW(Source, Index, Name, &Chars, NULL, &Type, Data, &Size) != ERROR_SUCCESS)
            break;
        Error = RegSetValueExW(Target, Name, 0, Type, Data, Size);
    }
    for (Index = 0; Error == ERROR_SUCCESS; Index++)
    {
        Chars = ARRAYSIZE(Name);
        if (RegEnumKeyExW(Source, Index, Name, &Chars, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        Error = RegOpenKeyExW(Source, Name, REG_OPTION_BACKUP_RESTORE, KEY_READ, &From);
        if (Error != ERROR_SUCCESS)
            break;
        Error = RegCreateKeyExW(Target, Name, 0, NULL, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, NULL, &To, NULL);
        if (Error == ERROR_SUCCESS)
        {
            Error = CopyKeyTree(From, To);
            RegCloseKey(To);
        }
        RegCloseKey(From);
    }
    return Error;
}

static LONG
DeleteKeyTree(
    _In_ HKEY Root,
    _In_ PCWSTR Path)
{
    WCHAR Name[256];
    DWORD Chars;
    HKEY hKey;
    LONG Error;

    Error = RegOpenKeyExW(Root, Path, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, &hKey);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (;;)
    {
        Chars = ARRAYSIZE(Name);
        if (RegEnumKeyExW(hKey, 0, Name, &Chars, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        Error = DeleteKeyTree(hKey, Name);
        if (Error != ERROR_SUCCESS)
            break;
    }
    if (Error == ERROR_SUCCESS)
        Error = RtlNtStatusToDosError(NtDeleteKey(hKey));
    RegCloseKey(hKey);
    return Error;
}

DWORD
OpenBcdStore(
    _Out_ PHKEY Store)
{
    BOOLEAN Enabled;

    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &Enabled);
    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &Enabled);
    return RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"BCD00000000", REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, Store);
}

static LONG
ReadElement(
    _In_ HKEY Store,
    _In_ PCWSTR Id,
    _In_ DWORD Element,
    _In_ DWORD ExpectedType,
    _Out_writes_bytes_(*Size) PVOID Data,
    _Inout_ PDWORD Size)
{
    DWORD Type;
    HKEY Key;
    LONG Error;

    Error = OpenElement(Store, Id, Element, KEY_QUERY_VALUE, FALSE, &Key);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RegQueryValueExW(Key, L"Element", NULL, &Type, Data, Size);
    RegCloseKey(Key);
    if (Error == ERROR_SUCCESS && Type != ExpectedType)
        Error = ERROR_INVALID_DATA;
    return Error;
}

static LONG
WriteElement(
    _In_ HKEY Store,
    _In_ PCWSTR Id,
    _In_ DWORD Element,
    _In_ DWORD Type,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ DWORD Size)
{
    HKEY Key;
    LONG Error;

    Error = OpenElement(Store, Id, Element, KEY_SET_VALUE, TRUE, &Key);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RegSetValueExW(Key, L"Element", 0, Type, Data, Size);
    RegCloseKey(Key);
    return Error;
}

static LONG
ReadString(
    _In_ HKEY Store,
    _In_ PCWSTR Id,
    _In_ DWORD Element,
    _Out_writes_(cchText) PWSTR Text,
    _In_ DWORD cchText)
{
    DWORD Size = cchText * sizeof(WCHAR);
    LONG Error;

    ZeroMemory(Text, cchText * sizeof(WCHAR));
    Error = ReadElement(Store, Id, Element, REG_SZ, Text, &Size);
    Text[cchText - 1] = UNICODE_NULL;
    return Error;
}

static LONG
WriteString(
    _In_ HKEY Store,
    _In_ PCWSTR Id,
    _In_ DWORD Element,
    _In_ PCWSTR Text)
{
    return WriteElement(Store, Id, Element, REG_SZ, Text, (DWORD)((wcslen(Text) + 1) * sizeof(WCHAR)));
}

static LONG
UpdateDisplayOrder(
    _In_ HKEY Store,
    _In_ PCWSTR EntryId,
    _In_ BOOL Add)
{
    PWSTR Order, Updated, Item, Out;
    DWORD Size = BCD_ORDER_MAX * sizeof(WCHAR);
    SIZE_T Length;
    LONG Error;

    Order = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, 2 * (BCD_ORDER_MAX + UPDATE_ID_CHARS + 2) * sizeof(WCHAR));
    if (!Order)
        return ERROR_NOT_ENOUGH_MEMORY;
    Updated = Order + BCD_ORDER_MAX + UPDATE_ID_CHARS + 2;

    Error = ReadElement(Store, BCD_BOOTMGR, BCD_DISPLAY_ORDER, REG_MULTI_SZ, Order, &Size);
    if (Error == ERROR_FILE_NOT_FOUND)
        Error = ERROR_SUCCESS;
    if (Error == ERROR_SUCCESS)
    {
        Out = Updated;
        if (Add)
        {
            StringCchCopyW(Out, UPDATE_ID_CHARS, EntryId);
            Out += wcslen(Out) + 1;
        }
        for (Item = Order; *Item && Item < Order + BCD_ORDER_MAX; Item += Length + 1)
        {
            Length = wcslen(Item);
            if (_wcsicmp(Item, EntryId) == 0)
                continue;
            CopyMemory(Out, Item, (Length + 1) * sizeof(WCHAR));
            Out += Length + 1;
        }
        *Out++ = UNICODE_NULL;
        Error = WriteElement(Store, BCD_BOOTMGR, BCD_DISPLAY_ORDER, REG_MULTI_SZ, Updated,
                             (DWORD)((Out - Updated) * sizeof(WCHAR)));
    }

    HeapFree(ProcessHeap, 0, Order);
    return Error;
}

static DWORD
CreateEntryId(
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId)
{
    GUID Guid;
    HRESULT hr = CoCreateGuid(&Guid);

    if (FAILED(hr))
        return (DWORD)hr;
    return SUCCEEDED(StringCchPrintfW(EntryId, UPDATE_ID_CHARS,
                                      L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                                      Guid.Data1, Guid.Data2, Guid.Data3, Guid.Data4[0], Guid.Data4[1],
                                      Guid.Data4[2], Guid.Data4[3], Guid.Data4[4], Guid.Data4[5],
                                      Guid.Data4[6], Guid.Data4[7])) ? ERROR_SUCCESS : ERROR_BUFFER_OVERFLOW;
}

static DWORD
CopyEntry(
    _In_ HKEY Store,
    _In_ PCWSTR SourceId,
    _In_ PCWSTR TargetId)
{
    WCHAR Path[64];
    HKEY Source, Target;
    LONG Error;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"Objects\\%s", SourceId);
    Error = RegOpenKeyExW(Store, Path, REG_OPTION_BACKUP_RESTORE, KEY_READ, &Source);
    if (Error != ERROR_SUCCESS)
        return Error;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"Objects\\%s", TargetId);
    Error = RegCreateKeyExW(Store, Path, 0, NULL, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, NULL, &Target, NULL);
    if (Error == ERROR_SUCCESS)
    {
        Error = CopyKeyTree(Source, Target);
        RegCloseKey(Target);
    }
    RegCloseKey(Source);
    return Error;
}

static DWORD
WriteUpgradeElements(
    _In_ HKEY Store,
    _In_ PCWSTR EntryId,
    _In_ PCWSTR BaseId,
    _In_ PCWSTR Description,
    _In_ PCWSTR LoaderPath,
    _In_ PCWSTR ImagePath,
    _In_ PCWSTR ImageHash)
{
    BYTE Device[BCD_ELEMENT_MAX];
    WCHAR Options[BCD_OPTIONS_CHARS];
    WCHAR BaseOptions[BCD_OPTIONS_CHARS];
    DWORD DeviceSize = sizeof(Device), ParentSize;
    LONG Error;

    Error = ReadElement(Store, BaseId, BCD_OS_DEVICE, REG_BINARY, Device, &DeviceSize);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (DeviceSize < BCD_DEVICE_PREFIX + BCD_DEVICE_MIN)
        return ERROR_INVALID_DATA;
    ParentSize = *(PULONG)(Device + BCD_DEVICE_PREFIX + BCD_DEVICE_SIZE);
    if (ParentSize < BCD_DEVICE_MIN || BCD_DEVICE_PREFIX + ParentSize > DeviceSize)
        return ERROR_INVALID_DATA;

    if (ReadString(Store, BaseId, BCD_LOAD_OPTIONS, BaseOptions, ARRAYSIZE(BaseOptions)) != ERROR_SUCCESS)
        BaseOptions[0] = UNICODE_NULL;
    if (FAILED(StringCchPrintfW(Options, ARRAYSIZE(Options), BaseOptions[0] ? L"%s %s%s %s%s" : L"%s%s%s %s%s",
                                BaseOptions, INSTALL_IMAGE_OPTION, ImagePath, INSTALL_HASH_OPTION, ImageHash)))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    Error = WriteElement(Store, EntryId, BCD_APPLICATION_DEVICE, REG_BINARY, Device, BCD_DEVICE_PREFIX + ParentSize);
    if (Error == ERROR_SUCCESS)
        Error = WriteString(Store, EntryId, BCD_APPLICATION_PATH, LoaderPath);
    if (Error == ERROR_SUCCESS)
        Error = WriteString(Store, EntryId, BCD_DESCRIPTION, Description);
    if (Error == ERROR_SUCCESS)
        Error = WriteString(Store, EntryId, BCD_LOAD_OPTIONS, Options);
    return Error;
}

static BOOL
IsStagedEntry(
    _In_ HKEY Store,
    _In_ PCWSTR EntryId,
    _In_ PCWSTR LoaderPath)
{
    WCHAR Path[MAX_PATH];

    return ReadString(Store, EntryId, BCD_APPLICATION_PATH, Path, ARRAYSIZE(Path)) == ERROR_SUCCESS &&
           _wcsicmp(Path, LoaderPath) == 0;
}

static BOOL
IsBootEntry(
    _In_ HKEY Store,
    _In_ PCWSTR EntryId,
    _In_ PCWSTR LoaderPath)
{
    WCHAR Path[MAX_PATH];

    return EntryId[0] &&
           ReadString(Store, EntryId, BCD_APPLICATION_PATH, Path, ARRAYSIZE(Path)) == ERROR_SUCCESS &&
           _wcsicmp(Path, LoaderPath) != 0;
}

static ULONG
FindStagedEntries(
    _In_ HKEY Store,
    _In_ PCWSTR LoaderPath,
    _Out_writes_(BCD_OBJECTS_MAX) WCHAR (*Entries)[UPDATE_ID_CHARS])
{
    WCHAR Name[UPDATE_ID_CHARS + 8];
    DWORD Index, Chars;
    ULONG Count = 0;
    HKEY Objects;

    if (RegOpenKeyExW(Store, L"Objects", REG_OPTION_BACKUP_RESTORE, KEY_READ, &Objects) != ERROR_SUCCESS)
        return 0;
    for (Index = 0; Count < BCD_OBJECTS_MAX; Index++)
    {
        Chars = ARRAYSIZE(Name);
        if (RegEnumKeyExW(Objects, Index, Name, &Chars, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (Chars < UPDATE_ID_CHARS && IsStagedEntry(Store, Name, LoaderPath))
            StringCchCopyW(Entries[Count++], UPDATE_ID_CHARS, Name);
    }
    RegCloseKey(Objects);
    return Count;
}

static LONG
FindInstalledDefault(
    _In_ HKEY Store,
    _In_ PCWSTR LoaderPath,
    _In_opt_ PCWSTR Hint,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId)
{
    PWSTR Order, Item;
    DWORD Size = BCD_ORDER_MAX * sizeof(WCHAR);
    LONG Error;

    if (Hint && IsBootEntry(Store, Hint, LoaderPath))
        return SUCCEEDED(StringCchCopyW(EntryId, UPDATE_ID_CHARS, Hint)) ? ERROR_SUCCESS : ERROR_BUFFER_OVERFLOW;

    Order = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, (BCD_ORDER_MAX + 2) * sizeof(WCHAR));
    if (!Order)
        return ERROR_NOT_ENOUGH_MEMORY;
    Error = ReadElement(Store, BCD_BOOTMGR, BCD_DISPLAY_ORDER, REG_MULTI_SZ, Order, &Size);
    if (Error == ERROR_SUCCESS)
    {
        Error = ERROR_NOT_FOUND;
        for (Item = Order; *Item && Item < Order + BCD_ORDER_MAX; Item += wcslen(Item) + 1)
        {
            if (IsBootEntry(Store, Item, LoaderPath))
            {
                Error = SUCCEEDED(StringCchCopyW(EntryId, UPDATE_ID_CHARS, Item)) ? ERROR_SUCCESS : ERROR_BUFFER_OVERFLOW;
                break;
            }
        }
    }
    HeapFree(ProcessHeap, 0, Order);
    return Error;
}

typedef struct _RUNNING_PARTITION
{
    DWORD Style;
    ULONGLONG Offset;
    ULONG Signature;
    GUID PartitionId;
} RUNNING_PARTITION, *PRUNNING_PARTITION;

static DWORD
GetRunningPartition(
    _Out_ PRUNNING_PARTITION Running)
{
    WCHAR Path[MAX_PATH], Drive[MAX_PATH];
    PARTITION_INFORMATION_EX Partition;
    STORAGE_DEVICE_NUMBER Number;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    DWORD Size = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[LAYOUT_PARTITIONS]), Returned, Error;
    HANDLE Device;

    ZeroMemory(Running, sizeof(*Running));
    if (!GetEnvironmentVariableW(L"SystemDrive", Drive, ARRAYSIZE(Drive)) ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\%s", Drive)))
    {
        return ERROR_PATH_NOT_FOUND;
    }

    Device = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Device == INVALID_HANDLE_VALUE)
        return GetLastError();
    Error = (DeviceIoControl(Device, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &Partition, sizeof(Partition),
                             &Returned, NULL) &&
             DeviceIoControl(Device, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number),
                             &Returned, NULL)) ? ERROR_SUCCESS : GetLastError();
    CloseHandle(Device);
    if (Error != ERROR_SUCCESS)
        return Error;

    Running->Style = Partition.PartitionStyle;
    Running->Offset = Partition.StartingOffset.QuadPart;
    Running->PartitionId = Partition.Gpt.PartitionId;
    if (Partition.PartitionStyle != PARTITION_STYLE_MBR)
        return Partition.PartitionStyle == PARTITION_STYLE_GPT ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;

    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Number.DeviceNumber)))
        return ERROR_PATH_NOT_FOUND;
    Layout = HeapAlloc(ProcessHeap, 0, Size);
    if (!Layout)
        return ERROR_NOT_ENOUGH_MEMORY;
    Device = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Device == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
    }
    else
    {
        Error = DeviceIoControl(Device, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, Layout, Size, &Returned, NULL)
            ? ERROR_SUCCESS : GetLastError();
        CloseHandle(Device);
    }
    if (Error == ERROR_SUCCESS)
        Running->Signature = Layout->Mbr.Signature;
    HeapFree(ProcessHeap, 0, Layout);
    return Error;
}

static BOOL
IsRunningEntry(
    _In_ HKEY Store,
    _In_ PCWSTR EntryId,
    _In_ PRUNNING_PARTITION Running)
{
    BYTE Device[BCD_ELEMENT_MAX];
    DWORD Size = sizeof(Device);

    if (ReadElement(Store, EntryId, BCD_OS_DEVICE, REG_BINARY, Device, &Size) != ERROR_SUCCESS ||
        Size < BCD_ELEMENT_PARTITION_SIZE || *(PULONG)(Device + BCD_ELEMENT_TYPE) != BCD_DEVICE_PARTITION)
    {
        return FALSE;
    }

    if (Running->Style == PARTITION_STYLE_GPT)
    {
        return *(PULONG)(Device + BCD_ELEMENT_STYLE) == BCD_STYLE_GPT &&
               memcmp(Device + BCD_ELEMENT_PARTITION, &Running->PartitionId, sizeof(GUID)) == 0;
    }
    return *(PULONG)(Device + BCD_ELEMENT_STYLE) == BCD_STYLE_MBR &&
           *(PULONGLONG)(Device + BCD_ELEMENT_PARTITION) == Running->Offset &&
           *(PULONG)(Device + BCD_ELEMENT_DISK) == Running->Signature;
}

static LONG
FindRunningEntry(
    _In_ HKEY Store,
    _In_ PCWSTR LoaderPath,
    _In_ PCWSTR Default,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId)
{
    RUNNING_PARTITION Running;
    PWSTR Order, Item;
    DWORD Size = BCD_ORDER_MAX * sizeof(WCHAR);
    LONG Error;

    Error = GetRunningPartition(&Running);
    if (Error != ERROR_SUCCESS)
    {
        DPRINT1("The partition of the running system is unknown (%ld)\n", Error);
        return Error;
    }

    if (IsBootEntry(Store, Default, LoaderPath) && IsRunningEntry(Store, Default, &Running))
        return SUCCEEDED(StringCchCopyW(EntryId, UPDATE_ID_CHARS, Default)) ? ERROR_SUCCESS : ERROR_BUFFER_OVERFLOW;

    Order = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, (BCD_ORDER_MAX + 2) * sizeof(WCHAR));
    if (!Order)
        return ERROR_NOT_ENOUGH_MEMORY;
    Error = ReadElement(Store, BCD_BOOTMGR, BCD_DISPLAY_ORDER, REG_MULTI_SZ, Order, &Size);
    if (Error == ERROR_SUCCESS)
    {
        Error = ERROR_NOT_FOUND;
        for (Item = Order; *Item && Item < Order + BCD_ORDER_MAX; Item += wcslen(Item) + 1)
        {
            if (IsBootEntry(Store, Item, LoaderPath) && IsRunningEntry(Store, Item, &Running))
            {
                Error = SUCCEEDED(StringCchCopyW(EntryId, UPDATE_ID_CHARS, Item)) ? ERROR_SUCCESS : ERROR_BUFFER_OVERFLOW;
                break;
            }
        }
    }
    HeapFree(ProcessHeap, 0, Order);
    if (Error != ERROR_SUCCESS)
        DPRINT1("No boot entry starts the running system (%ld)\n", Error);
    return Error;
}

DWORD
RemoveBcdUpgradeEntries(
    _In_ HKEY Store,
    _In_ PCWSTR LoaderPath,
    _In_opt_ PCWSTR PreviousDefault)
{
    WCHAR (*Entries)[UPDATE_ID_CHARS];
    WCHAR Default[UPDATE_ID_CHARS], Installed[UPDATE_ID_CHARS], Path[64];
    ULONG Count, Index;
    LONG Error = ERROR_SUCCESS;

    Entries = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, BCD_OBJECTS_MAX * sizeof(*Entries));
    if (!Entries)
        return ERROR_NOT_ENOUGH_MEMORY;

    Count = FindStagedEntries(Store, LoaderPath, Entries);
    if (Count != 0 &&
        ReadString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, Default, ARRAYSIZE(Default)) == ERROR_SUCCESS &&
        IsStagedEntry(Store, Default, LoaderPath))
    {
        Error = FindInstalledDefault(Store, LoaderPath, PreviousDefault, Installed);
        if (Error == ERROR_SUCCESS)
            Error = WriteString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, Installed);
        DPRINT1("Default boot entry %S restored to %S: %ld\n", Default, Installed, Error);
    }

    for (Index = 0; Error == ERROR_SUCCESS && Index < Count; Index++)
    {
        UpdateDisplayOrder(Store, Entries[Index], FALSE);
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"Objects\\%s", Entries[Index]);
        DeleteKeyTree(Store, Path);
    }
    RegFlushKey(Store);

    if (Error == ERROR_SUCCESS && FindStagedEntries(Store, LoaderPath, Entries) != 0)
        Error = ERROR_INVALID_STATE;
    HeapFree(ProcessHeap, 0, Entries);
    DPRINT1("%lu staged boot entries removed: %ld\n", Count, Error);
    return Error;
}

DWORD
AddBcdUpgradeEntry(
    _In_ HKEY Store,
    _In_ PCWSTR Description,
    _In_ PCWSTR LoaderPath,
    _In_ PCWSTR ImagePath,
    _In_ PCWSTR ImageHash,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR PreviousDefault)
{
    WCHAR Check[UPDATE_ID_CHARS], Options[BCD_OPTIONS_CHARS], Running[UPDATE_ID_CHARS];
    LONG Error;

    EntryId[0] = UNICODE_NULL;
    PreviousDefault[0] = UNICODE_NULL;
    Error = RemoveBcdUpgradeEntries(Store, LoaderPath, NULL);
    if (Error == ERROR_SUCCESS)
        Error = ReadString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, PreviousDefault, UPDATE_ID_CHARS);
    if (Error == ERROR_SUCCESS && !IsBootEntry(Store, PreviousDefault, LoaderPath))
        Error = ERROR_INVALID_STATE;
    if (Error == ERROR_SUCCESS)
        Error = FindRunningEntry(Store, LoaderPath, PreviousDefault, Running);
    if (Error == ERROR_SUCCESS)
        Error = CreateEntryId(EntryId);
    if (Error == ERROR_SUCCESS)
        Error = CopyEntry(Store, Running, EntryId);
    if (Error == ERROR_SUCCESS)
        Error = WriteUpgradeElements(Store, EntryId, Running, Description, LoaderPath, ImagePath, ImageHash);
    if (Error == ERROR_SUCCESS)
        Error = UpdateDisplayOrder(Store, EntryId, TRUE);
    if (Error == ERROR_SUCCESS)
        Error = WriteString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, EntryId);
    if (Error == ERROR_SUCCESS)
        Error = RegFlushKey(Store);

    if (Error == ERROR_SUCCESS &&
        (ReadString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, Check, ARRAYSIZE(Check)) != ERROR_SUCCESS ||
         _wcsicmp(Check, EntryId) != 0 ||
         !IsStagedEntry(Store, EntryId, LoaderPath) ||
         ReadString(Store, EntryId, BCD_LOAD_OPTIONS, Options, ARRAYSIZE(Options)) != ERROR_SUCCESS ||
         !wcsstr(Options, ImageHash)))
    {
        Error = ERROR_INVALID_DATA;
    }

    if (Error != ERROR_SUCCESS && EntryId[0])
        RemoveBcdUpgradeEntries(Store, LoaderPath, PreviousDefault);
    DPRINT1("Upgrade boot entry %S after %S, cloned from %S: %ld\n", EntryId, PreviousDefault, Running, Error);
    return Error;
}

BOOL
IsLiveSystem(VOID)
{
    WCHAR Options[512];
    DWORD Size = sizeof(Options);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"SystemStartOptions",
                     RRF_RT_REG_SZ, NULL, Options, &Size) != ERROR_SUCCESS)
    {
        return FALSE;
    }
    _wcsupr(Options);
    return wcsstr(Options, L"MININT") != NULL;
}

ULONG
ReadInventoryBuild(
    _In_ PCWSTR Path)
{
    CHAR Text[512];
    PCHAR Build;
    DWORD Read = 0;
    HANDLE File;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return 0;
    if (!ReadFile(File, Text, sizeof(Text) - 1, &Read, NULL))
        Read = 0;
    CloseHandle(File);

    Text[Read] = ANSI_NULL;
    Build = strstr(Text, "\n; Build ");
    return Build ? strtoul(Build + 9, NULL, 10) : 0;
}
