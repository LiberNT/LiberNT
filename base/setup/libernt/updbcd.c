/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot entry that installs a downloaded LiberNT image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <objbase.h>
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

#define INSTALL_IMAGE_OPTION    L"INSTALLIMAGE="

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
    _In_ PCWSTR ImagePath)
{
    BYTE Device[BCD_ELEMENT_MAX];
    WCHAR Options[512];
    WCHAR BaseOptions[448];
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
    if (FAILED(StringCchPrintfW(Options, ARRAYSIZE(Options), BaseOptions[0] ? L"%s %s%s" : L"%s%s%s",
                                BaseOptions, INSTALL_IMAGE_OPTION, ImagePath)))
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

DWORD
AddBcdUpgradeEntry(
    _In_ HKEY Store,
    _In_ PCWSTR Description,
    _In_ PCWSTR LoaderPath,
    _In_ PCWSTR ImagePath,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR PreviousDefault)
{
    LONG Error;

    EntryId[0] = UNICODE_NULL;
    Error = ReadString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, PreviousDefault, UPDATE_ID_CHARS);
    if (Error == ERROR_SUCCESS)
        Error = CreateEntryId(EntryId);
    if (Error == ERROR_SUCCESS)
        Error = CopyEntry(Store, PreviousDefault, EntryId);
    if (Error == ERROR_SUCCESS)
        Error = WriteUpgradeElements(Store, EntryId, PreviousDefault, Description, LoaderPath, ImagePath);
    if (Error == ERROR_SUCCESS)
        Error = UpdateDisplayOrder(Store, EntryId, TRUE);
    if (Error == ERROR_SUCCESS)
        Error = WriteString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, EntryId);

    if (Error != ERROR_SUCCESS && EntryId[0])
        RemoveBcdUpgradeEntry(Store, EntryId, PreviousDefault);
    else
        RegFlushKey(Store);
    DPRINT1("Upgrade boot entry %S after %S: %ld\n", EntryId, PreviousDefault, Error);
    return Error;
}

DWORD
RemoveBcdUpgradeEntry(
    _In_ HKEY Store,
    _In_ PCWSTR EntryId,
    _In_ PCWSTR PreviousDefault)
{
    WCHAR Default[UPDATE_ID_CHARS];
    WCHAR Path[64];
    LONG Error = ERROR_SUCCESS;

    if (ReadString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, Default, ARRAYSIZE(Default)) == ERROR_SUCCESS &&
        _wcsicmp(Default, EntryId) == 0 && PreviousDefault[0])
    {
        Error = WriteString(Store, BCD_BOOTMGR, BCD_DEFAULT_OBJECT, PreviousDefault);
    }
    UpdateDisplayOrder(Store, EntryId, FALSE);

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"Objects\\%s", EntryId);
    DeleteKeyTree(Store, Path);
    RegFlushKey(Store);
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
