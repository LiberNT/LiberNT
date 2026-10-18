/*
 * PROJECT:     LiberNT Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot loader pages and self-test over the system Boot Configuration Data store
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "settings.h"
#include <winioctl.h>
#include <ntddstor.h>
#include <commdlg.h>

#define BCD_KEY            L"BCD00000000"
#define BCD_IMPORT_KEY     L"LiberNTBcdImport"
#define BCD_BOOTMGR        L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define BCD_TYPE_BOOTMGR   0x10100002
#define BCD_TYPE_OSLOADER  0x10200003
#define BCD_INHERIT_DEPTH  8
#define BCD_DEVICE_SIZE    0x58
#define BCD_DEVICE_TYPE    0x10
#define BCD_DEVICE_LENGTH  0x18
#define BCD_DEVICE_PART    0x20
#define BCD_DEVICE_STYLE   0x34
#define BCD_DEVICE_DISK    0x38
#define BCD_DEVICE_BODY    0x48
#define BCD_DEVICE_BOOT    5
#define BCD_DEVICE_PARTITION 6
#define BCD_STYLE_MBR      1
#define SELFTEST_KEY       L"SOFTWARE\\LiberNT\\BootLoaderSelfTest"
#define SELFTEST_TOKEN     L"BLSELFTEST"

extern "C" LONG NTAPI NtDeleteKey(HANDLE KeyHandle);
extern "C" ULONG NTAPI RtlNtStatusToDosError(LONG Status);

enum BcdElement : DWORD
{
    BcdApplicationDevice = 0x11000001,
    BcdApplicationPath = 0x12000002,
    BcdDescription = 0x12000004,
    BcdInheritedObjects = 0x14000006,
    BcdLoadOptions = 0x12000030,
    BcdOsDevice = 0x21000001,
    BcdSystemRoot = 0x22000002,
    BcdKernelPath = 0x22000011,
    BcdHalPath = 0x22000012,
    BcdDefaultObject = 0x23000003,
    BcdDisplayOrder = 0x24000001,
    BcdTimeout = 0x25000004,
    BcdUseVgaDriver = 0x26000040,
    BcdDisableBootDisplay = 0x26000041,
    BcdBootProcessorOnly = 0x26000060,
    BcdBootLog = 0x26000090,
    BcdVerboseLoad = 0x26000091,
    BcdKernelDebugger = 0x260000a0
};

struct BootEntry
{
    std::wstring Id;
    std::wstring Name;
    std::wstring Drive;
    std::wstring Root;
};

struct BootState
{
    std::vector<BootEntry> Entries;
    std::wstring Default;
    bool HasTimeout = false;
    ULONGLONG Timeout = 0;
};

struct BootOption
{
    DWORD Element;
    UINT Title;
    UINT Detail;
    LPCWSTR Icon;
};

static const BootOption BootOptions[] =
{
    { BcdKernelDebugger, IDS_BOOT_DEBUG, IDS_BOOT_DEBUG_DESC, L"code_20_regular" },
    { BcdBootLog, IDS_BOOT_LOG, IDS_BOOT_LOG_DESC, L"document_20_regular" },
    { BcdVerboseLoad, IDS_BOOT_SOS, IDS_BOOT_SOS_DESC, L"apps_list_20_regular" },
    { BcdUseVgaDriver, IDS_BOOT_BASEVIDEO, IDS_BOOT_BASEVIDEO_DESC, L"desktop_20_regular" },
    { BcdDisableBootDisplay, IDS_BOOT_QUIET, IDS_BOOT_QUIET_DESC, L"image_20_regular" },
    { BcdBootProcessorOnly, IDS_BOOT_ONECPU, IDS_BOOT_ONECPU_DESC, L"power_20_regular" },
};

static const ULONGLONG BootTimeouts[] = { 3, 5, 10, 30 };

static HKEY StoreRoot;

static std::wstring
ObjectKey(const std::wstring &id)
{
    return L"Objects\\" + id;
}

static std::wstring
ElementKey(const std::wstring &id, DWORD element)
{
    return ObjectKey(id) + Printf(L"\\Elements\\%08x", element);
}

static bool
ReadElement(HKEY root, const std::wstring &id, DWORD element, DWORD *type, std::vector<BYTE> &data)
{
    std::wstring key = ElementKey(id, element);
    DWORD size = 0;

    if (RegGetValueW(root, key.c_str(), L"Element", RRF_RT_ANY | RRF_NOEXPAND, type, NULL, &size) != ERROR_SUCCESS)
        return false;
    data.resize(size + sizeof(WCHAR) * 2);
    if (RegGetValueW(root, key.c_str(), L"Element", RRF_RT_ANY | RRF_NOEXPAND, type, data.data(), &size) !=
        ERROR_SUCCESS)
        return false;
    data.resize(size);
    return true;
}

static std::vector<std::wstring>
MultiString(const std::vector<BYTE> &data)
{
    std::vector<std::wstring> list;
    LPCWSTR text = (LPCWSTR)data.data();
    size_t count = data.size() / sizeof(WCHAR), start = 0;

    for (size_t i = 0; i < count; i++)
    {
        if (text[i] != UNICODE_NULL)
            continue;
        if (i > start)
            list.push_back(std::wstring(text + start, i - start));
        start = i + 1;
    }
    if (start < count)
        list.push_back(std::wstring(text + start, count - start));
    return list;
}

static bool
FindElement(HKEY root, const std::wstring &id, DWORD element, DWORD *type, std::vector<BYTE> &data, int depth)
{
    std::vector<BYTE> inherit;
    std::vector<std::wstring> parents;
    DWORD listType;

    if (ReadElement(root, id, element, type, data))
        return true;
    if (depth <= 0 || !ReadElement(root, id, BcdInheritedObjects, &listType, inherit) || listType != REG_MULTI_SZ)
        return false;
    parents = MultiString(inherit);
    for (size_t i = 0; i < parents.size(); i++)
    {
        if (FindElement(root, parents[i], element, type, data, depth - 1))
            return true;
    }
    return false;
}

static std::wstring
ElementString(HKEY root, const std::wstring &id, DWORD element)
{
    std::vector<BYTE> data;
    DWORD type;

    if (!FindElement(root, id, element, &type, data, BCD_INHERIT_DEPTH) || type != REG_SZ)
        return std::wstring();
    return std::wstring((LPCWSTR)data.data(), wcsnlen((LPCWSTR)data.data(), data.size() / sizeof(WCHAR)));
}

static std::vector<std::wstring>
ElementList(HKEY root, const std::wstring &id, DWORD element)
{
    std::vector<BYTE> data;
    DWORD type;

    if (!FindElement(root, id, element, &type, data, BCD_INHERIT_DEPTH) || type != REG_MULTI_SZ)
        return std::vector<std::wstring>();
    return MultiString(data);
}

static bool
ElementInteger(HKEY root, const std::wstring &id, DWORD element, ULONGLONG *value)
{
    std::vector<BYTE> data;
    DWORD type;

    *value = 0;
    if (!FindElement(root, id, element, &type, data, BCD_INHERIT_DEPTH) || type != REG_BINARY || data.empty())
        return false;
    memcpy(value, data.data(), min(data.size(), sizeof(*value)));
    return true;
}

static bool
ElementBoolean(HKEY root, const std::wstring &id, DWORD element)
{
    std::vector<BYTE> data;
    DWORD type;

    return FindElement(root, id, element, &type, data, BCD_INHERIT_DEPTH) && type == REG_BINARY && !data.empty() &&
           data[0] != 0;
}

static std::vector<BYTE>
ElementBytes(HKEY root, const std::wstring &id, DWORD element)
{
    std::vector<BYTE> data;
    DWORD type;

    if (!FindElement(root, id, element, &type, data, BCD_INHERIT_DEPTH) || type != REG_BINARY)
        data.clear();
    return data;
}

static LSTATUS
CreateKey(HKEY root, const std::wstring &sub, HKEY *key)
{
    return RegCreateKeyExW(root, sub.c_str(), 0, NULL, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, NULL, key, NULL);
}

static LSTATUS
DeleteTree(HKEY root, const std::wstring &sub)
{
    std::vector<std::wstring> children;
    WCHAR name[256];
    HKEY key;
    LSTATUS error = RegOpenKeyExW(root, sub.c_str(), REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, &key);

    if (error != ERROR_SUCCESS)
        return error;
    for (DWORD i = 0; ; i++)
    {
        DWORD length = _countof(name);

        if (RegEnumKeyExW(key, i, name, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        children.push_back(name);
    }
    for (size_t i = 0; i < children.size() && error == ERROR_SUCCESS; i++)
        error = DeleteTree(key, children[i]);
    if (error == ERROR_SUCCESS)
        error = (LSTATUS)RtlNtStatusToDosError(NtDeleteKey(key));
    RegCloseKey(key);
    return error;
}

static LSTATUS
CopyTree(HKEY from, HKEY to)
{
    WCHAR name[256];
    std::vector<BYTE> data;
    LSTATUS error = ERROR_SUCCESS;

    for (DWORD i = 0; error == ERROR_SUCCESS; i++)
    {
        DWORD length = _countof(name), size = 0, type;

        if (RegEnumValueW(from, i, name, &length, NULL, &type, NULL, &size) != ERROR_SUCCESS)
            break;
        data.resize(size + 1);
        length = _countof(name);
        error = RegEnumValueW(from, i, name, &length, NULL, &type, data.data(), &size);
        if (error == ERROR_SUCCESS)
            error = RegSetValueExW(to, name, 0, type, data.data(), size);
    }
    for (DWORD i = 0; error == ERROR_SUCCESS; i++)
    {
        DWORD length = _countof(name);
        HKEY source, target;

        if (RegEnumKeyExW(from, i, name, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        error = RegOpenKeyExW(from, name, 0, KEY_READ, &source);
        if (error != ERROR_SUCCESS)
            break;
        error = CreateKey(to, name, &target);
        if (error == ERROR_SUCCESS)
        {
            error = CopyTree(source, target);
            RegCloseKey(target);
        }
        RegCloseKey(source);
    }
    return error;
}

static DWORD
WriteElement(HKEY root, const std::wstring &id, DWORD element, DWORD type, const void *data, DWORD size)
{
    HKEY handle;
    LSTATUS error = CreateKey(root, ElementKey(id, element), &handle);

    if (error != ERROR_SUCCESS)
        return (DWORD)error;
    error = RegSetValueExW(handle, L"Element", 0, type, (const BYTE *)data, size);
    RegCloseKey(handle);
    return (DWORD)error;
}

static DWORD
WriteString(HKEY root, const std::wstring &id, DWORD element, const std::wstring &value)
{
    return WriteElement(root, id, element, REG_SZ, value.c_str(), (DWORD)((value.length() + 1) * sizeof(WCHAR)));
}

static DWORD
WriteList(HKEY root, const std::wstring &id, DWORD element, const std::vector<std::wstring> &list)
{
    std::vector<WCHAR> data;

    for (size_t i = 0; i < list.size(); i++)
    {
        data.insert(data.end(), list[i].begin(), list[i].end());
        data.push_back(UNICODE_NULL);
    }
    data.push_back(UNICODE_NULL);
    return WriteElement(root, id, element, REG_MULTI_SZ, data.data(), (DWORD)(data.size() * sizeof(WCHAR)));
}

static DWORD
WriteInteger(HKEY root, const std::wstring &id, DWORD element, ULONGLONG value)
{
    return WriteElement(root, id, element, REG_BINARY, &value, sizeof(value));
}

static DWORD
WriteBoolean(HKEY root, const std::wstring &id, DWORD element, bool value)
{
    BYTE data = value ? 1 : 0;

    return WriteElement(root, id, element, REG_BINARY, &data, sizeof(data));
}

static DWORD
DeleteElement(HKEY root, const std::wstring &id, DWORD element)
{
    LSTATUS error = DeleteTree(root, ElementKey(id, element));

    return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : (DWORD)error;
}

static DWORD
ObjectType(HKEY root, const std::wstring &id)
{
    std::wstring key = ObjectKey(id) + L"\\Description";
    DWORD type = 0, size = sizeof(type);

    if (RegGetValueW(root, key.c_str(), L"Type", RRF_RT_REG_DWORD, NULL, &type, &size) != ERROR_SUCCESS)
        return 0;
    return type;
}

static DWORD
Commit(HKEY root, DWORD error)
{
    LSTATUS flush = RegFlushKey(root);

    return error != ERROR_SUCCESS ? error : (DWORD)flush;
}

static DWORD
OpenStore(HKEY *root)
{
    static const LPCWSTR StorePaths[] = { L"\\EFI\\LiberNT\\Boot\\BCD", L"\\Boot\\BCD" };
    std::wstring partition, file;
    LSTATUS error;

    *root = StoreRoot;
    if (StoreRoot)
        return ERROR_SUCCESS;
    EnablePrivilege(SE_RESTORE_NAME);
    EnablePrivilege(SE_BACKUP_NAME);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BCD_KEY, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, &StoreRoot) ==
        ERROR_SUCCESS)
    {
        *root = StoreRoot;
        return ERROR_SUCCESS;
    }
    partition = ReadString(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"SystemPartition");
    if (partition.empty())
        return ERROR_FILE_NOT_FOUND;
    for (size_t i = 0; i < _countof(StorePaths) && file.empty(); i++)
    {
        std::wstring path = L"\\\\?\\GLOBALROOT" + partition + StorePaths[i];

        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            file = path;
    }
    if (file.empty())
        return ERROR_FILE_NOT_FOUND;
    error = RegLoadKeyW(HKEY_LOCAL_MACHINE, BCD_KEY, file.c_str());
    if (error == ERROR_SUCCESS)
        error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, BCD_KEY, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, &StoreRoot);
    *root = StoreRoot;
    return (DWORD)error;
}

static bool
VolumeDevice(WCHAR letter, std::vector<BYTE> &element)
{
    std::vector<BYTE> layout(0x10000);
    PARTITION_INFORMATION_EX partition;
    STORAGE_DEVICE_NUMBER number;
    PDRIVE_LAYOUT_INFORMATION_EX drive = (PDRIVE_LAYOUT_INFORMATION_EX)layout.data();
    std::wstring path = Printf(L"\\\\.\\%c:", letter);
    HANDLE handle;
    DWORD bytes;
    BOOL ok;

    handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    ok = DeviceIoControl(handle, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partition, sizeof(partition), &bytes,
                         NULL) &&
         DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &number, sizeof(number), &bytes, NULL);
    CloseHandle(handle);
    if (!ok)
        return false;
    path = Printf(L"\\\\.\\PhysicalDrive%lu", number.DeviceNumber);
    handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    ok = DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout.data(), (DWORD)layout.size(), &bytes,
                         NULL);
    CloseHandle(handle);
    if (!ok || drive->PartitionStyle != partition.PartitionStyle)
        return false;

    element.assign(BCD_DEVICE_SIZE, 0);
    *(PULONG)&element[BCD_DEVICE_TYPE] = BCD_DEVICE_PARTITION;
    *(PULONG)&element[BCD_DEVICE_LENGTH] = BCD_DEVICE_BODY;
    if (partition.PartitionStyle == PARTITION_STYLE_GPT)
    {
        memcpy(&element[BCD_DEVICE_PART], &partition.Gpt.PartitionId, sizeof(GUID));
        memcpy(&element[BCD_DEVICE_DISK], &drive->Gpt.DiskId, sizeof(GUID));
    }
    else if (partition.PartitionStyle == PARTITION_STYLE_MBR)
    {
        *(PULONGLONG)&element[BCD_DEVICE_PART] = (ULONGLONG)partition.StartingOffset.QuadPart;
        *(PULONG)&element[BCD_DEVICE_STYLE] = BCD_STYLE_MBR;
        *(PULONG)&element[BCD_DEVICE_DISK] = drive->Mbr.Signature;
    }
    else
    {
        return false;
    }
    return true;
}

static bool
SameDevice(const std::vector<BYTE> &a, const std::vector<BYTE> &b)
{
    return a.size() >= BCD_DEVICE_SIZE && b.size() >= BCD_DEVICE_SIZE &&
           !memcmp(&a[BCD_DEVICE_TYPE], &b[BCD_DEVICE_TYPE], BCD_DEVICE_SIZE - BCD_DEVICE_TYPE);
}

static std::vector<WCHAR>
LocalVolumes()
{
    std::vector<WCHAR> letters;
    DWORD drives = GetLogicalDrives();

    for (WCHAR letter = L'C'; letter <= L'Z'; letter++)
    {
        WCHAR root[] = { letter, L':', L'\\', UNICODE_NULL };
        UINT type;

        if (!(drives & (1u << (letter - L'A'))))
            continue;
        type = GetDriveTypeW(root);
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE)
            letters.push_back(letter);
    }
    return letters;
}

static std::wstring
DeviceLabel(const std::vector<BYTE> &device)
{
    std::vector<WCHAR> letters;

    if (device.size() < BCD_DEVICE_SIZE)
        return Str(IDS_BOOT_DEVICE_UNKNOWN);
    if (*(const ULONG *)&device[BCD_DEVICE_TYPE] == BCD_DEVICE_BOOT)
        return Str(IDS_BOOT_DEVICE_BOOT);
    if (*(const ULONG *)&device[BCD_DEVICE_TYPE] != BCD_DEVICE_PARTITION)
        return Str(IDS_BOOT_DEVICE_RAMDISK);
    letters = LocalVolumes();
    for (size_t i = 0; i < letters.size(); i++)
    {
        std::vector<BYTE> volume;

        if (VolumeDevice(letters[i], volume) && SameDevice(volume, device))
            return Printf(L"%c:", letters[i]);
    }
    return Str(IDS_BOOT_DEVICE_UNMOUNTED);
}

static BootState
ReadState(HKEY root)
{
    BootState state;
    std::vector<std::wstring> order = ElementList(root, BCD_BOOTMGR, BcdDisplayOrder);

    state.Default = ElementString(root, BCD_BOOTMGR, BcdDefaultObject);
    state.HasTimeout = ElementInteger(root, BCD_BOOTMGR, BcdTimeout, &state.Timeout);
    for (size_t i = 0; i < order.size(); i++)
    {
        BootEntry entry;

        if (ObjectType(root, order[i]) != BCD_TYPE_OSLOADER)
            continue;
        entry.Id = order[i];
        entry.Name = ElementString(root, entry.Id, BcdDescription);
        if (entry.Name.empty())
            entry.Name = entry.Id;
        entry.Root = ElementString(root, entry.Id, BcdSystemRoot);
        entry.Drive = DeviceLabel(ElementBytes(root, entry.Id, BcdOsDevice));
        state.Entries.push_back(entry);
    }
    return state;
}

static int
EntryIndex(const BootState &state, const std::wstring &id)
{
    for (size_t i = 0; i < state.Entries.size(); i++)
    {
        if (!lstrcmpiW(state.Entries[i].Id.c_str(), id.c_str()))
            return (int)i;
    }
    return -1;
}

static DWORD
SetDefaultEntry(HKEY root, const std::wstring &id)
{
    return Commit(root, WriteString(root, BCD_BOOTMGR, BcdDefaultObject, id));
}

static DWORD
SetMenuTimeout(HKEY root, bool wait, ULONGLONG seconds)
{
    return Commit(root, wait ? DeleteElement(root, BCD_BOOTMGR, BcdTimeout)
                             : WriteInteger(root, BCD_BOOTMGR, BcdTimeout, seconds));
}

static DWORD
MoveEntry(HKEY root, const std::wstring &id, int delta)
{
    std::vector<std::wstring> order = ElementList(root, BCD_BOOTMGR, BcdDisplayOrder);

    for (size_t i = 0; i < order.size(); i++)
    {
        size_t target = i + delta;

        if (lstrcmpiW(order[i].c_str(), id.c_str()))
            continue;
        if ((delta < 0 && i < (size_t)-delta) || target >= order.size())
            return ERROR_SUCCESS;
        std::swap(order[i], order[target]);
        return Commit(root, WriteList(root, BCD_BOOTMGR, BcdDisplayOrder, order));
    }
    return ERROR_NOT_FOUND;
}

static DWORD
RenameEntry(HKEY root, const std::wstring &id, const std::wstring &name)
{
    if (name.empty())
        return ERROR_INVALID_PARAMETER;
    return Commit(root, WriteString(root, id, BcdDescription, name));
}

static DWORD
DeleteEntry(HKEY root, const std::wstring &id)
{
    std::vector<std::wstring> order = ElementList(root, BCD_BOOTMGR, BcdDisplayOrder), kept;
    std::wstring defaultId = ElementString(root, BCD_BOOTMGR, BcdDefaultObject);
    DWORD error;

    for (size_t i = 0; i < order.size(); i++)
    {
        if (lstrcmpiW(order[i].c_str(), id.c_str()))
            kept.push_back(order[i]);
    }
    if (kept.empty())
        return ERROR_ACCESS_DENIED;
    error = WriteList(root, BCD_BOOTMGR, BcdDisplayOrder, kept);
    if (error == ERROR_SUCCESS && !lstrcmpiW(defaultId.c_str(), id.c_str()))
        error = WriteString(root, BCD_BOOTMGR, BcdDefaultObject, kept[0]);
    if (error == ERROR_SUCCESS)
        error = (DWORD)DeleteTree(root, ObjectKey(id));
    return Commit(root, error);
}

static std::wstring
NewObjectId()
{
    WCHAR text[64];
    GUID guid;

    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, text, _countof(text)))
        return std::wstring();
    CharLowerW(text);
    return text;
}

static DWORD
CopyEntry(HKEY root, const std::wstring &source, const std::wstring &name, std::wstring *created)
{
    std::wstring id = NewObjectId();
    std::vector<std::wstring> order;
    HKEY from, to;
    LSTATUS error;

    if (id.empty() || name.empty())
        return ERROR_INVALID_PARAMETER;
    error = RegOpenKeyExW(root, ObjectKey(source).c_str(), 0, KEY_READ, &from);
    if (error != ERROR_SUCCESS)
        return (DWORD)error;
    error = CreateKey(root, ObjectKey(id), &to);
    if (error == ERROR_SUCCESS)
    {
        error = CopyTree(from, to);
        RegCloseKey(to);
    }
    RegCloseKey(from);
    if (error == ERROR_SUCCESS)
        error = (LSTATUS)WriteString(root, id, BcdDescription, name);
    if (error == ERROR_SUCCESS)
    {
        order = ElementList(root, BCD_BOOTMGR, BcdDisplayOrder);
        order.push_back(id);
        error = (LSTATUS)WriteList(root, BCD_BOOTMGR, BcdDisplayOrder, order);
    }
    if (error != ERROR_SUCCESS)
    {
        DeleteTree(root, ObjectKey(id));
        return Commit(root, (DWORD)error);
    }
    *created = id;
    return Commit(root, ERROR_SUCCESS);
}

static std::wstring
TemplateEntry(HKEY root)
{
    BootState state = ReadState(root);

    if (EntryIndex(state, state.Default) >= 0)
        return state.Default;
    return state.Entries.empty() ? std::wstring() : state.Entries[0].Id;
}

static bool
VolumeHasSystem(HKEY root, const std::wstring &templateId, WCHAR letter)
{
    std::wstring path = ElementString(root, templateId, BcdApplicationPath);

    return !path.empty() && GetFileAttributesW(Printf(L"%c:%s", letter, path.c_str()).c_str()) != INVALID_FILE_ATTRIBUTES;
}

static DWORD
AddVolumeEntry(HKEY root, WCHAR letter, const std::wstring &name, std::wstring *created)
{
    std::wstring templateId = TemplateEntry(root), id;
    std::vector<BYTE> device;
    DWORD error;

    if (templateId.empty())
        return ERROR_NOT_FOUND;
    if (!VolumeHasSystem(root, templateId, letter))
        return ERROR_FILE_NOT_FOUND;
    if (!VolumeDevice(letter, device))
        return ERROR_NOT_SUPPORTED;
    error = CopyEntry(root, templateId, name, &id);
    if (error != ERROR_SUCCESS)
        return error;
    error = WriteElement(root, id, BcdApplicationDevice, REG_BINARY, device.data(), (DWORD)device.size());
    if (error == ERROR_SUCCESS)
        error = WriteElement(root, id, BcdOsDevice, REG_BINARY, device.data(), (DWORD)device.size());
    if (error != ERROR_SUCCESS)
    {
        DeleteEntry(root, id);
        return error;
    }
    *created = id;
    return Commit(root, ERROR_SUCCESS);
}

static DWORD
SetEntryOption(HKEY root, const std::wstring &id, DWORD element, bool value)
{
    return Commit(root, WriteBoolean(root, id, element, value));
}

static DWORD
SetKernelOptions(HKEY root, const std::wstring &id, const std::wstring &options)
{
    return Commit(root, options.empty() ? DeleteElement(root, id, BcdLoadOptions)
                                        : WriteString(root, id, BcdLoadOptions, options));
}

static DWORD
ExportStore(HKEY root, const std::wstring &file)
{
    EnablePrivilege(SE_BACKUP_NAME);
    DeleteFileW(file.c_str());
    return (DWORD)RegSaveKeyExW(root, file.c_str(), NULL, REG_LATEST_FORMAT);
}

static DWORD
ReplaceObjects(HKEY root, HKEY source)
{
    HKEY from, to;
    LSTATUS error;

    error = RegOpenKeyExW(source, L"Objects", 0, KEY_READ, &from);
    if (error != ERROR_SUCCESS)
        return (DWORD)error;
    error = DeleteTree(root, L"Objects");
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
        error = CreateKey(root, L"Objects", &to);
    if (error == ERROR_SUCCESS)
    {
        error = CopyTree(from, to);
        RegCloseKey(to);
    }
    RegCloseKey(from);
    return (DWORD)error;
}

static DWORD
ImportStore(HKEY root, const std::wstring &file)
{
    WCHAR temp[MAX_PATH], backup[MAX_PATH];
    HKEY source, saved;
    DWORD error;

    EnablePrivilege(SE_RESTORE_NAME);
    EnablePrivilege(SE_BACKUP_NAME);
    if (!GetTempPathW(_countof(temp), temp) || !GetTempFileNameW(temp, L"bcd", 0, backup))
        return GetLastError();
    error = ExportStore(root, backup);
    if (error != ERROR_SUCCESS)
        return error;
    error = (DWORD)RegLoadKeyW(HKEY_LOCAL_MACHINE, BCD_IMPORT_KEY, file.c_str());
    if (error != ERROR_SUCCESS)
        return error;
    error = (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, BCD_IMPORT_KEY, 0, KEY_READ, &source);
    if (error == ERROR_SUCCESS)
    {
        if (ObjectType(source, BCD_BOOTMGR) != BCD_TYPE_BOOTMGR)
            error = ERROR_BADDB;
        else if ((error = ReplaceObjects(root, source)) != ERROR_SUCCESS)
        {
            RegCloseKey(source);
            source = NULL;
            RegUnLoadKeyW(HKEY_LOCAL_MACHINE, BCD_IMPORT_KEY);
            if (RegLoadKeyW(HKEY_LOCAL_MACHINE, BCD_IMPORT_KEY, backup) == ERROR_SUCCESS)
            {
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BCD_IMPORT_KEY, 0, KEY_READ, &saved) == ERROR_SUCCESS)
                {
                    ReplaceObjects(root, saved);
                    RegCloseKey(saved);
                }
            }
        }
        if (source)
            RegCloseKey(source);
    }
    RegUnLoadKeyW(HKEY_LOCAL_MACHINE, BCD_IMPORT_KEY);
    DeleteFileW(backup);
    return Commit(root, error);
}

static std::wstring
TimeoutLabel(bool hasTimeout, ULONGLONG seconds)
{
    if (!hasTimeout)
        return Str(IDS_BOOT_MENU_WAIT);
    if (!seconds)
        return Str(IDS_BOOT_MENU_SKIP);
    return Format(IDS_BOOT_MENU_COUNTDOWN, (ULONG)min(seconds, (ULONGLONG)MAXDWORD));
}

static void
Report(SettingsHost *host, DWORD error)
{
    if (error != ERROR_SUCCESS)
        ShowError(host->Window(), error);
    host->Refresh();
}

static bool
PickFile(HWND owner, bool save, std::wstring &path)
{
    WCHAR file[MAX_PATH] = L"";
    std::wstring filter = Str(IDS_BOOT_BACKUP_FILTER);
    OPENFILENAMEW ofn = { sizeof(ofn) };

    std::replace(filter.begin(), filter.end(), L'|', L'\0');
    filter.push_back(L'\0');
    if (save)
        StringCchCopyW(file, _countof(file), L"BCD Backup.bcd");
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = _countof(file);
    ofn.lpstrDefExt = L"bcd";
    ofn.Flags = save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY
                     : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)))
        return false;
    path = file;
    return true;
}

static void
AddCopy(SettingsHost *host, HKEY root, const BootState &state)
{
    std::vector<std::wstring> names;
    std::wstring name, id;
    int source;

    for (size_t i = 0; i < state.Entries.size(); i++)
        names.push_back(state.Entries[i].Name);
    source = state.Entries.size() == 1 ? 0 : PickFromMenu(host->Window(), names, EntryIndex(state, state.Default));
    if (source < 0)
        return;
    name = Format(IDS_BOOT_COPY_NAME, state.Entries[source].Name.c_str());
    if (!InputBox(host->Window(), Str(IDS_BOOT_ADD_COPY), Str(IDS_BOOT_NAME), name))
        return;
    Report(host, CopyEntry(root, state.Entries[source].Id, name, &id));
}

static void
AddFromDrive(SettingsHost *host, HKEY root)
{
    std::vector<WCHAR> letters = LocalVolumes();
    std::vector<WCHAR> systems;
    std::vector<std::wstring> names;
    std::wstring templateId = TemplateEntry(root), name, id;
    int chosen;

    for (size_t i = 0; i < letters.size(); i++)
    {
        if (VolumeHasSystem(root, templateId, letters[i]))
        {
            systems.push_back(letters[i]);
            names.push_back(Format(IDS_BOOT_DRIVE_ITEM, letters[i]));
        }
    }
    if (systems.empty())
    {
        ShowError(host->Window(), ERROR_FILE_NOT_FOUND);
        return;
    }
    chosen = PickFromMenu(host->Window(), names, -1);
    if (chosen < 0)
        return;
    name = Format(IDS_BOOT_DRIVE_NAME, systems[chosen]);
    if (!InputBox(host->Window(), Str(IDS_BOOT_ADD_DRIVE), Str(IDS_BOOT_NAME), name))
        return;
    Report(host, AddVolumeEntry(root, systems[chosen], name, &id));
}

void
BuildBootLoader(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::vector<std::wstring> names, timeouts;
    std::vector<std::pair<bool, ULONGLONG>> timeoutValues;
    BootState state;
    HKEY root = NULL;
    DWORD error = ERROR_SUCCESS;
    int selected = 0;

    page.Group();
    page.Text(Str(IDS_BOOT_INTRO));
    if (page.Indexing)
    {
        page.Group(Str(IDS_BOOT_STARTUP));
        page.Add(RowKind::Choice, Str(IDS_BOOT_DEFAULT), Str(IDS_BOOT_DEFAULT_DESC), L"desktop_20_regular");
        page.Add(RowKind::Choice, Str(IDS_BOOT_MENU), Str(IDS_BOOT_MENU_DESC), L"history_20_regular");
        page.Group(Str(IDS_BOOT_BACKUP_GROUP));
        page.Add(RowKind::Button, Str(IDS_BOOT_EXPORT), Str(IDS_BOOT_EXPORT_DESC), L"arrow_download_20_regular");
        page.Add(RowKind::Button, Str(IDS_BOOT_IMPORT), Str(IDS_BOOT_IMPORT_DESC), L"arrow_counterclockwise_20_regular");
        return;
    }
    if (!IsAdmin())
        error = ERROR_ACCESS_DENIED;
    else
        error = OpenStore(&root);
    if (error != ERROR_SUCCESS)
    {
        page.Group();
        page.Info(Str(IDS_BOOT_UNAVAILABLE), Win32Message(error), L"power_20_regular");
        return;
    }
    state = ReadState(root);

    for (size_t i = 0; i < state.Entries.size(); i++)
        names.push_back(state.Entries[i].Name);
    timeoutValues.push_back(std::make_pair(true, 0ULL));
    for (size_t i = 0; i < _countof(BootTimeouts); i++)
        timeoutValues.push_back(std::make_pair(true, BootTimeouts[i]));
    timeoutValues.push_back(std::make_pair(false, 0ULL));
    if (state.HasTimeout && state.Timeout &&
        std::find(std::begin(BootTimeouts), std::end(BootTimeouts), state.Timeout) == std::end(BootTimeouts))
        timeoutValues.insert(timeoutValues.end() - 1, std::make_pair(true, state.Timeout));
    for (size_t i = 0; i < timeoutValues.size(); i++)
    {
        timeouts.push_back(TimeoutLabel(timeoutValues[i].first, timeoutValues[i].second));
        if (timeoutValues[i].first == state.HasTimeout &&
            (!state.HasTimeout || timeoutValues[i].second == state.Timeout))
            selected = (int)i;
    }

    page.Group(Str(IDS_BOOT_STARTUP));
    page.Choice(Str(IDS_BOOT_DEFAULT), Str(IDS_BOOT_DEFAULT_DESC), L"desktop_20_regular", names,
                EntryIndex(state, state.Default),
                [host, root, state](int index)
                {
                    if (index >= 0 && index < (int)state.Entries.size())
                        Report(host, SetDefaultEntry(root, state.Entries[index].Id));
                });
    page.Choice(Str(IDS_BOOT_MENU), Str(IDS_BOOT_MENU_DESC), L"history_20_regular", timeouts, selected,
                [host, root, timeoutValues](int index)
                {
                    if (index >= 0 && index < (int)timeoutValues.size())
                        Report(host, SetMenuTimeout(root, !timeoutValues[index].first, timeoutValues[index].second));
                });

    page.Group(Str(IDS_BOOT_SYSTEMS));
    for (size_t i = 0; i < state.Entries.size(); i++)
    {
        const BootEntry &entry = state.Entries[i];
        std::wstring detail = Format(IDS_BOOT_ENTRY_DETAIL, entry.Drive.c_str(), entry.Root.c_str());

        if (!lstrcmpiW(entry.Id.c_str(), state.Default.c_str()))
            detail = Str(IDS_BOOT_DEFAULT_TAG) + L" \x00B7 " + detail;
        page.Navigate(entry.Name, detail, L"desktop_20_regular", PageId::BootEntry, entry.Id);
    }
    page.Button(Str(IDS_BOOT_ADD_COPY), Str(IDS_BOOT_ADD_COPY_DESC), L"add_circle_20_regular", Str(IDS_ADD),
                [host, root, state](int) { AddCopy(host, root, state); });
    page.Button(Str(IDS_BOOT_ADD_DRIVE), Str(IDS_BOOT_ADD_DRIVE_DESC), L"storage_20_regular", Str(IDS_ADD),
                [host, root](int) { AddFromDrive(host, root); });

    page.Group(Str(IDS_BOOT_BACKUP_GROUP));
    page.Button(Str(IDS_BOOT_EXPORT), Str(IDS_BOOT_EXPORT_DESC), L"arrow_download_20_regular", Str(IDS_BOOT_EXPORT_ACTION),
                [host, root](int)
                {
                    std::wstring file;

                    if (PickFile(host->Window(), true, file))
                        Report(host, ExportStore(root, file));
                });
    page.Button(Str(IDS_BOOT_IMPORT), Str(IDS_BOOT_IMPORT_DESC), L"arrow_counterclockwise_20_regular",
                Str(IDS_BOOT_IMPORT_ACTION),
                [host, root](int)
                {
                    std::wstring file;

                    if (PickFile(host->Window(), false, file) &&
                        Confirm(host->Window(), Str(IDS_BOOT_IMPORT_CONFIRM), file, Str(IDS_BOOT_IMPORT_ACTION)))
                        Report(host, ImportStore(root, file));
                });
}

void
BuildBootEntry(PageBuilder &page)
{
    SettingsHost *host = page.Host;
    std::wstring id = page.Param, options;
    BootState state;
    HKEY root = NULL;
    DWORD error;
    int index;

    if (page.Indexing)
        return;
    error = IsAdmin() ? OpenStore(&root) : ERROR_ACCESS_DENIED;
    if (error != ERROR_SUCCESS)
    {
        page.Group();
        page.Info(Str(IDS_BOOT_UNAVAILABLE), Win32Message(error), L"power_20_regular");
        return;
    }
    state = ReadState(root);
    index = EntryIndex(state, id);
    if (index < 0)
    {
        page.Group();
        page.Info(Str(IDS_BOOT_UNAVAILABLE), Win32Message(ERROR_NOT_FOUND), L"power_20_regular");
        return;
    }
    const BootEntry entry = state.Entries[index];
    bool isDefault = !lstrcmpiW(entry.Id.c_str(), state.Default.c_str());

    page.Title = entry.Name;
    page.Hero(entry.Name, isDefault ? Str(IDS_BOOT_DEFAULT_TAG) : Format(IDS_BOOT_ENTRY_DETAIL, entry.Drive.c_str(),
                                                                       entry.Root.c_str()),
              L"desktop_20_regular").Action = Str(IDS_RENAME);
    page.Groups.back().Rows.back().Invoke = [host, root, entry](int)
    {
        std::wstring name = entry.Name;

        if (InputBox(host->Window(), Str(IDS_BOOT_RENAME), Str(IDS_BOOT_NAME), name) && !name.empty())
            Report(host, RenameEntry(root, entry.Id, name));
    };

    page.Group(Str(IDS_BOOT_ENTRY_GROUP));
    page.Button(Str(IDS_BOOT_SET_DEFAULT), Str(IDS_BOOT_SET_DEFAULT_DESC), L"checkmark_circle_20_regular",
                Str(IDS_BOOT_SET_DEFAULT_ACTION), [host, root, entry](int) { Report(host, SetDefaultEntry(root, entry.Id)); })
        .Enabled = !isDefault;
    SettingRow &position = page.Button(Str(IDS_BOOT_POSITION), Format(IDS_BOOT_POSITION_DESC, index + 1,
                                                                       (int)state.Entries.size()),
                                       L"arrow_sort_20_regular", std::wstring(),
                                       [host, root, entry](int action)
                                       { Report(host, MoveEntry(root, entry.Id, action == 0 ? -1 : 1)); });
    position.Actions.push_back(Str(IDS_BOOT_MOVE_UP));
    position.Actions.push_back(Str(IDS_BOOT_MOVE_DOWN));
    position.Enabled = state.Entries.size() > 1;
    page.Button(Str(IDS_BOOT_DELETE), Str(IDS_BOOT_DELETE_DESC), L"delete_20_regular", Str(IDS_DELETE),
                [host, root, entry](int)
                {
                    if (!Confirm(host->Window(), Format(IDS_BOOT_DELETE_CONFIRM, entry.Name.c_str()),
                                 Str(IDS_BOOT_DELETE_DESC), Str(IDS_DELETE)))
                        return;
                    Report(host, DeleteEntry(root, entry.Id));
                    host->Back();
                })
        .Enabled = state.Entries.size() > 1;

    page.Group(Str(IDS_BOOT_OPTIONS));
    for (size_t i = 0; i < _countof(BootOptions); i++)
    {
        DWORD element = BootOptions[i].Element;

        page.Toggle(Str(BootOptions[i].Title), Str(BootOptions[i].Detail), BootOptions[i].Icon,
                    ElementBoolean(root, entry.Id, element),
                    [host, root, entry, element](int on) { Report(host, SetEntryOption(root, entry.Id, element, on != 0)); });
    }
    options = ElementString(root, entry.Id, BcdLoadOptions);
    page.Button(Str(IDS_BOOT_KERNEL_OPTIONS), options.empty() ? Str(IDS_NONE) : options, L"code_20_regular",
                Str(IDS_EDIT),
                [host, root, entry, options](int)
                {
                    std::wstring value = options;

                    if (InputBox(host->Window(), Str(IDS_BOOT_KERNEL_OPTIONS), Str(IDS_BOOT_KERNEL_OPTIONS_DESC), value))
                        Report(host, SetKernelOptions(root, entry.Id, value));
                });

    page.Group(Str(IDS_BOOT_DETAILS));
    page.Info(Str(IDS_BOOT_DRIVE), entry.Drive);
    page.Info(Str(IDS_BOOT_LOADER), ElementString(root, entry.Id, BcdApplicationPath));
    page.Info(Str(IDS_BOOT_SYSTEM_ROOT), entry.Root);
    page.Info(Str(IDS_BOOT_KERNEL), ElementString(root, entry.Id, BcdKernelPath));
    page.Info(Str(IDS_BOOT_HAL), ElementString(root, entry.Id, BcdHalPath));
    page.Info(Str(IDS_BOOT_IDENTIFIER), entry.Id);
}

class SelfTest
{
public:
    explicit SelfTest(LPCWSTR log) :
        m_Log(log ? log : L"")
    {
    }

    void Check(bool condition, LPCWSTR format, ...)
    {
        WCHAR text[512];
        va_list args;

        va_start(args, format);
        StringCchVPrintfW(text, _countof(text), format, args);
        va_end(args);
        if (!condition)
            m_Failures++;
        m_Checks++;
        Line(Printf(L"%s %s", condition ? L"PASS" : L"FAIL", text));
    }

    void Line(const std::wstring &text)
    {
        std::wstring line = L"BLTEST: " + text + L"\r\n";
        HANDLE file;
        DWORD written;
        std::string utf8;
        int size;

        OutputDebugStringW(line.c_str());
        if (m_Log.empty())
            return;
        size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.length(), NULL, 0, NULL, NULL);
        utf8.resize(size);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.length(), &utf8[0], size, NULL, NULL);
        file = CreateFileW(m_Log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return;
        WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL);
        CloseHandle(file);
    }

    int Finish()
    {
        Line(Printf(L"DONE %lu checks, %lu failures", m_Checks, m_Failures));
        return (int)m_Failures;
    }

private:
    std::wstring m_Log;
    ULONG m_Checks = 0;
    ULONG m_Failures = 0;
};

static bool
SameState(const BootState &a, const BootState &b)
{
    if (a.Entries.size() != b.Entries.size() || lstrcmpiW(a.Default.c_str(), b.Default.c_str()) ||
        a.HasTimeout != b.HasTimeout || a.Timeout != b.Timeout)
        return false;
    for (size_t i = 0; i < a.Entries.size(); i++)
    {
        if (lstrcmpiW(a.Entries[i].Id.c_str(), b.Entries[i].Id.c_str()) || a.Entries[i].Name != b.Entries[i].Name)
            return false;
    }
    return true;
}

static bool
ObjectExists(HKEY root, const std::wstring &id)
{
    HKEY key;

    if (RegOpenKeyExW(root, ObjectKey(id).c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

static void
TestStoreOperations(SelfTest &test, HKEY root)
{
    BootState initial = ReadState(root), state;
    std::wstring copy, volume, backup, defaultId = initial.Default;
    WCHAR temp[MAX_PATH], windows[MAX_PATH];
    std::vector<BYTE> volumeDevice, defaultDevice, own;
    DWORD ownType;
    int index;
    WIN32_FILE_ATTRIBUTE_DATA info;

    test.Check(!initial.Entries.empty(), L"store lists %Iu systems", initial.Entries.size());
    test.Check(EntryIndex(initial, defaultId) >= 0, L"default %s is a listed system", defaultId.c_str());
    for (size_t i = 0; i < initial.Entries.size(); i++)
        test.Line(Printf(L"INFO system %Iu %s \"%s\" on %s%s", i, initial.Entries[i].Id.c_str(),
                         initial.Entries[i].Name.c_str(), initial.Entries[i].Drive.c_str(),
                         initial.Entries[i].Root.c_str()));
    test.Line(L"INFO menu " + TimeoutLabel(initial.HasTimeout, initial.Timeout));
    if (initial.Entries.empty())
        return;

    GetTempPathW(_countof(temp), temp);
    backup = std::wstring(temp) + L"bltest-backup.bcd";
    test.Check(ExportStore(root, backup) == ERROR_SUCCESS && GetFileAttributesExW(backup.c_str(), GetFileExInfoStandard,
                                                                                 &info) &&
               info.nFileSizeLow != 0,
               L"export to %s", backup.c_str());

    test.Check(CopyEntry(root, defaultId, L"Self-test copy", &copy) == ERROR_SUCCESS, L"copy the default system");
    state = ReadState(root);
    index = EntryIndex(state, copy);
    test.Check(index == (int)state.Entries.size() - 1 && state.Entries[index].Name == L"Self-test copy",
               L"copy %s is last in the menu", copy.c_str());
    test.Check(RenameEntry(root, copy, L"Self-test renamed") == ERROR_SUCCESS &&
               ElementString(root, copy, BcdDescription) == L"Self-test renamed", L"rename");
    test.Check(RenameEntry(root, copy, std::wstring()) == ERROR_INVALID_PARAMETER, L"empty name refused");
    test.Check(MoveEntry(root, copy, -1) == ERROR_SUCCESS && EntryIndex(ReadState(root), copy) == index - 1,
               L"move up");
    test.Check(MoveEntry(root, copy, 1) == ERROR_SUCCESS && EntryIndex(ReadState(root), copy) == index, L"move down");
    test.Check(MoveEntry(root, copy, 1) == ERROR_SUCCESS && EntryIndex(ReadState(root), copy) == index,
               L"move down at the end keeps the order");
    test.Check(SetDefaultEntry(root, copy) == ERROR_SUCCESS && !lstrcmpiW(ReadState(root).Default.c_str(), copy.c_str()),
               L"set default");
    test.Check(SetDefaultEntry(root, defaultId) == ERROR_SUCCESS &&
               !lstrcmpiW(ReadState(root).Default.c_str(), defaultId.c_str()), L"restore default");

    test.Check(SetMenuTimeout(root, false, 7) == ERROR_SUCCESS && ReadState(root).HasTimeout &&
               ReadState(root).Timeout == 7, L"countdown 7 seconds");
    test.Check(SetMenuTimeout(root, false, 0) == ERROR_SUCCESS && ReadState(root).HasTimeout &&
               ReadState(root).Timeout == 0, L"start right away");
    test.Check(SetMenuTimeout(root, true, 0) == ERROR_SUCCESS && !ReadState(root).HasTimeout, L"wait for a choice");
    test.Check(SetMenuTimeout(root, !initial.HasTimeout, initial.Timeout) == ERROR_SUCCESS, L"restore the menu timeout");

    for (size_t i = 0; i < _countof(BootOptions); i++)
    {
        DWORD element = BootOptions[i].Element;
        bool before = ElementBoolean(root, copy, element), defaultBefore = ElementBoolean(root, defaultId, element);

        test.Check(SetEntryOption(root, copy, element, !before) == ERROR_SUCCESS &&
                   ElementBoolean(root, copy, element) == !before &&
                   ElementBoolean(root, defaultId, element) == defaultBefore,
                   L"option %08lx toggled on the copy only", element);
        test.Check(SetEntryOption(root, copy, element, before) == ERROR_SUCCESS &&
                   ElementBoolean(root, copy, element) == before, L"option %08lx restored", element);
    }
    test.Check(SetKernelOptions(root, copy, SELFTEST_TOKEN L"=1") == ERROR_SUCCESS &&
               ElementString(root, copy, BcdLoadOptions) == SELFTEST_TOKEN L"=1", L"kernel options set");
    test.Check(SetKernelOptions(root, copy, std::wstring()) == ERROR_SUCCESS &&
               !ReadElement(root, copy, BcdLoadOptions, &ownType, own), L"kernel options cleared");

    defaultDevice = ElementBytes(root, defaultId, BcdOsDevice);
    GetWindowsDirectoryW(windows, _countof(windows));
    test.Check(VolumeDevice(windows[0], volumeDevice), L"partition element of %c:", windows[0]);
    test.Check(SameDevice(volumeDevice, defaultDevice), L"%c: matches the device of the default system", windows[0]);
    test.Check(DeviceLabel(defaultDevice) == Printf(L"%c:", windows[0]), L"default system shown on %c:", windows[0]);
    test.Check(AddVolumeEntry(root, windows[0], L"Self-test drive", &volume) == ERROR_SUCCESS &&
               SameDevice(ElementBytes(root, volume, BcdOsDevice), volumeDevice) &&
               SameDevice(ElementBytes(root, volume, BcdApplicationDevice), volumeDevice),
               L"add the system on %c:", windows[0]);

    test.Check(DeleteEntry(root, volume) == ERROR_SUCCESS && !ObjectExists(root, volume) &&
               EntryIndex(ReadState(root), volume) < 0, L"delete the drive entry");
    test.Check(SetDefaultEntry(root, copy) == ERROR_SUCCESS && DeleteEntry(root, copy) == ERROR_SUCCESS &&
               !ObjectExists(root, copy) && !lstrcmpiW(ReadState(root).Default.c_str(), ReadState(root).Entries[0].Id.c_str()),
               L"deleting the default moves the default to the first system");
    test.Check(SetDefaultEntry(root, defaultId) == ERROR_SUCCESS, L"restore default after delete");

    test.Check(CopyEntry(root, defaultId, L"Self-test before import", &copy) == ERROR_SUCCESS, L"change before import");
    test.Check(ImportStore(root, backup) == ERROR_SUCCESS, L"import %s", backup.c_str());
    state = ReadState(root);
    test.Check(!ObjectExists(root, copy) && SameState(state, initial), L"store matches the backup after import");
    test.Check(ImportStore(root, std::wstring(windows) + L"\\win.ini") != ERROR_SUCCESS && SameState(ReadState(root), initial),
               L"invalid backup refused, store unchanged");
    DeleteFileW(backup.c_str());
}

static void
ArmBootCheck(SelfTest &test, HKEY root, bool menu)
{
    BootState state = ReadState(root);
    std::wstring id;
    HKEY key;
    DWORD hasTimeout = state.HasTimeout, armedMenu = menu;

    test.Check(CopyEntry(root, state.Default, L"Boot loader self-test", &id) == ERROR_SUCCESS, L"create the boot entry");
    test.Check(SetEntryOption(root, id, BcdBootLog, true) == ERROR_SUCCESS &&
               SetEntryOption(root, id, BcdVerboseLoad, true) == ERROR_SUCCESS &&
               SetKernelOptions(root, id, SELFTEST_TOKEN) == ERROR_SUCCESS, L"set boot log, sos and kernel options");
    test.Check(SetDefaultEntry(root, id) == ERROR_SUCCESS && SetMenuTimeout(root, menu, 1) == ERROR_SUCCESS,
               menu ? L"make it the default and always show the menu" : L"make it the default with a 1 second countdown");
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, SELFTEST_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(key, L"Entry", 0, REG_SZ, (const BYTE *)id.c_str(), (DWORD)((id.length() + 1) * sizeof(WCHAR)));
        RegSetValueExW(key, L"Default", 0, REG_SZ, (const BYTE *)state.Default.c_str(),
                       (DWORD)((state.Default.length() + 1) * sizeof(WCHAR)));
        RegSetValueExW(key, L"HasTimeout", 0, REG_DWORD, (const BYTE *)&hasTimeout, sizeof(hasTimeout));
        RegSetValueExW(key, L"Timeout", 0, REG_QWORD, (const BYTE *)&state.Timeout, sizeof(state.Timeout));
        RegSetValueExW(key, L"Menu", 0, REG_DWORD, (const BYTE *)&armedMenu, sizeof(armedMenu));
        RegFlushKey(key);
        RegCloseKey(key);
    }
    test.Line(L"ARMED " + id);
}

static void
VerifyBootCheck(SelfTest &test, HKEY root)
{
    std::wstring id = ReadString(HKEY_LOCAL_MACHINE, SELFTEST_KEY, L"Entry");
    std::wstring savedDefault = ReadString(HKEY_LOCAL_MACHINE, SELFTEST_KEY, L"Default");
    std::wstring options = L" " + ReadString(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control",
                                             L"SystemStartOptions") + L" ";
    ULONGLONG timeout = 0;
    DWORD hasTimeout = ReadDword(HKEY_LOCAL_MACHINE, SELFTEST_KEY, L"HasTimeout", 1), size = sizeof(timeout);
    bool menu = ReadDword(HKEY_LOCAL_MACHINE, SELFTEST_KEY, L"Menu", 0) != 0;
    BootState state = ReadState(root);

    test.Line(L"INFO SystemStartOptions" + options);
    test.Check(!id.empty(), L"armed entry %s recorded", id.c_str());
    test.Check(!lstrcmpiW(state.Default.c_str(), id.c_str()) &&
               (menu ? !state.HasTimeout : state.HasTimeout && state.Timeout == 1),
               L"store still selects the self-test entry");
    test.Check(options.find(L" " SELFTEST_TOKEN L" ") != std::wstring::npos, L"kernel options reached the kernel");
    test.Check(options.find(L" BOOTLOG ") != std::wstring::npos, L"boot log option reached the kernel");
    test.Check(options.find(L" SOS ") != std::wstring::npos, L"sos option reached the kernel");
    RegGetValueW(HKEY_LOCAL_MACHINE, SELFTEST_KEY, L"Timeout", RRF_RT_REG_QWORD, NULL, &timeout, &size);
    test.Check(!savedDefault.empty() && SetDefaultEntry(root, savedDefault) == ERROR_SUCCESS &&
               SetMenuTimeout(root, !hasTimeout, timeout) == ERROR_SUCCESS && DeleteEntry(root, id) == ERROR_SUCCESS,
               L"restore the boot configuration");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, SELFTEST_KEY);
}

int
RunBootLoaderSelfTest(LPCWSTR mode, LPCWSTR log)
{
    SelfTest test(log);
    HKEY root = NULL, mounted;
    bool atBoot = RegOpenKeyExW(HKEY_LOCAL_MACHINE, BCD_KEY, 0, KEY_READ, &mounted) == ERROR_SUCCESS;
    DWORD error = OpenStore(&root);

    if (atBoot)
        RegCloseKey(mounted);
    test.Line(Printf(L"START %s", mode));
    test.Check(atBoot, L"system store mounted at HKLM\\%s before Settings opens it", BCD_KEY);
    test.Check(error == ERROR_SUCCESS, L"open the system store (error %lu)", error);
    if (error != ERROR_SUCCESS)
        return test.Finish();
    if (!lstrcmpiW(mode, L"bootloader"))
        TestStoreOperations(test, root);
    else if (!lstrcmpiW(mode, L"bootloader-arm"))
        ArmBootCheck(test, root, false);
    else if (!lstrcmpiW(mode, L"bootloader-arm-menu"))
        ArmBootCheck(test, root, true);
    else if (!lstrcmpiW(mode, L"bootloader-verify"))
        VerifyBootCheck(test, root);
    else
        test.Check(false, L"unknown mode %s", mode);
    return test.Finish();
}
