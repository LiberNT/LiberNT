/*
 * PROJECT:     LiberNT Driver Installer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device inventory, driver package install and install history
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "devices.hpp"
#include "resource.h"
#include "text.hpp"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <shlobj.h>
#include <sddl.h>

#include <algorithm>
#include <wctype.h>

namespace lntdrv
{

namespace
{

const WCHAR HistoryKey[] = L"SOFTWARE\\LiberNT\\Drivers\\History";
const WCHAR NamesKey[] = L"SOFTWARE\\LiberNT\\Drivers\\Names";
const WCHAR DownloadsSecurity[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
const size_t MaximumInfSize = 16 * 1024 * 1024;
const DWORD MsiTimeout = 10 * 60 * 1000;
const unsigned MaximumNesting = 3;

std::wstring Upper(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), ::towupper);
    return text;
}

bool StartsWith(const std::wstring &text, const WCHAR *prefix)
{
    return _wcsnicmp(text.c_str(), prefix, wcslen(prefix)) == 0;
}

bool HasExtension(const std::wstring &path, LPCWSTR extension)
{
    size_t length = wcslen(extension);
    return path.size() > length && _wcsicmp(path.c_str() + path.size() - length, extension) == 0;
}

std::vector<WCHAR> RawProperty(HDEVINFO set, SP_DEVINFO_DATA *data, DWORD property)
{
    DWORD type = 0, size = 0;
    std::vector<WCHAR> buffer;

    SetupDiGetDeviceRegistryPropertyW(set, data, property, &type, NULL, 0, &size);
    if (!size || (type != REG_SZ && type != REG_MULTI_SZ && type != REG_EXPAND_SZ))
        return buffer;
    buffer.resize(size / sizeof(WCHAR) + 2, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, data, property, &type, (PBYTE)buffer.data(), size, NULL))
        buffer.clear();
    return buffer;
}

std::wstring Property(HDEVINFO set, SP_DEVINFO_DATA *data, DWORD property)
{
    std::vector<WCHAR> buffer = RawProperty(set, data, property);
    return buffer.empty() ? std::wstring() : std::wstring(buffer.data());
}

std::vector<std::wstring> MultiProperty(HDEVINFO set, SP_DEVINFO_DATA *data, DWORD property)
{
    std::vector<WCHAR> buffer = RawProperty(set, data, property);
    std::vector<std::wstring> list;

    for (const WCHAR *item = buffer.data(); !buffer.empty() && *item; item += wcslen(item) + 1)
        list.push_back(item);
    return list;
}

std::wstring RegistryString(HKEY key, LPCWSTR name)
{
    WCHAR value[512];
    DWORD type = 0, size = sizeof(value) - sizeof(WCHAR);

    if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)value, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ))
    {
        return std::wstring();
    }
    value[size / sizeof(WCHAR)] = L'\0';
    return value;
}

DWORD RegistryDword(HKEY key, LPCWSTR name)
{
    DWORD value = 0, type = 0, size = sizeof(value);

    if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return 0;
    return value;
}

void SetString(HKEY key, LPCWSTR name, const std::wstring &value)
{
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value.c_str(), (DWORD)((value.size() + 1) * sizeof(WCHAR)));
}

void SetDword(HKEY key, LPCWSTR name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

bool IsSpecificId(const std::wstring &id)
{
    std::wstring upper = Upper(id);

    if (upper.empty() || upper[0] == L'*')
        return false;
    if (StartsWith(upper, L"ACPI\\"))
        return !StartsWith(upper, L"ACPI\\PNP");
    return upper.find(L"VEN_") != std::wstring::npos || upper.find(L"VID_") != std::wstring::npos ||
           upper.find(L"DEV_") != std::wstring::npos || upper.find(L"PID_") != std::wstring::npos;
}

DWORDLONG ParseVersion(const std::wstring &text)
{
    unsigned parts[4] = { 0, 0, 0, 0 };

    swscanf(text.c_str(), L"%u.%u.%u.%u", &parts[0], &parts[1], &parts[2], &parts[3]);
    return ((DWORDLONG)(parts[0] & 0xFFFF) << 48) | ((DWORDLONG)(parts[1] & 0xFFFF) << 32) |
           ((DWORDLONG)(parts[2] & 0xFFFF) << 16) | (DWORDLONG)(parts[3] & 0xFFFF);
}

void ListFiles(const std::wstring &directory, std::vector<std::wstring> &files)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);

    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        std::wstring name = data.cFileName, path = directory + L"\\" + name;

        if (name == L"." || name == L"..")
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ListFiles(path, files);
        else
            files.push_back(path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

struct CabinetContext
{
    std::wstring Directory;
    DWORD Error;
};

bool SafeCabinetName(const std::wstring &name)
{
    size_t start = 0;

    if (name.empty() || name[0] == L'\\' || name.find(L':') != std::wstring::npos)
        return false;
    while (start <= name.size())
    {
        size_t end = name.find(L'\\', start);
        std::wstring part = name.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);

        if (part.empty() || part == L"." || part == L"..")
            return false;
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return true;
}

UINT CALLBACK CabinetCallback(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2)
{
    CabinetContext *cabinet = (CabinetContext *)context;

    UNREFERENCED_PARAMETER(param2);
    switch (notification)
    {
        case SPFILENOTIFY_FILEINCABINET:
        {
            FILE_IN_CABINET_INFO_W *info = (FILE_IN_CABINET_INFO_W *)param1;
            std::wstring name = info->NameInCabinet, target;
            size_t slash;

            std::replace(name.begin(), name.end(), L'/', L'\\');
            if (!SafeCabinetName(name))
            {
                cabinet->Error = ERROR_INVALID_DATA;
                return FILEOP_ABORT;
            }
            target = cabinet->Directory + L"\\" + name;
            if (target.size() >= MAX_PATH)
            {
                cabinet->Error = ERROR_FILENAME_EXCED_RANGE;
                return FILEOP_ABORT;
            }
            slash = target.rfind(L'\\');
            if (slash > cabinet->Directory.size())
                SHCreateDirectoryExW(NULL, target.substr(0, slash).c_str(), NULL);
            memcpy(info->FullTargetName, target.c_str(), (target.size() + 1) * sizeof(WCHAR));
            return FILEOP_DOIT;
        }
        case SPFILENOTIFY_FILEEXTRACTED:
        {
            FILEPATHS_W *paths = (FILEPATHS_W *)param1;
            if (paths->Win32Error)
                cabinet->Error = paths->Win32Error;
            return paths->Win32Error;
        }
        case SPFILENOTIFY_NEEDNEWCABINET:
            cabinet->Error = ERROR_FILE_NOT_FOUND;
            return ERROR_FILE_NOT_FOUND;
        default:
            return NO_ERROR;
    }
}

enum class PackageKind
{
    Cabinet,
    Installer,
    Program,
    Unknown
};

PackageKind DetectKind(const std::wstring &path)
{
    static const BYTE Compound[] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE magic[8] = { 0 };
    DWORD read = 0;

    if (file == INVALID_HANDLE_VALUE)
        return PackageKind::Unknown;
    ReadFile(file, magic, sizeof(magic), &read, NULL);
    CloseHandle(file);
    if (read >= 4 && !memcmp(magic, "MSCF", 4))
        return PackageKind::Cabinet;
    if (read == sizeof(magic) && !memcmp(magic, Compound, sizeof(Compound)))
        return PackageKind::Installer;
    if (read >= 2 && magic[0] == 'M' && magic[1] == 'Z')
        return PackageKind::Program;
    return PackageKind::Unknown;
}

DWORD ExtractCabinet(const std::wstring &cabinet, const std::wstring &directory)
{
    CabinetContext context = { directory, ERROR_SUCCESS };
    int created = SHCreateDirectoryExW(NULL, directory.c_str(), NULL);

    if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS)
        return (DWORD)created;
    if (!SetupIterateCabinetW(cabinet.c_str(), 0, CabinetCallback, &context))
        return context.Error ? context.Error : (GetLastError() ? GetLastError() : ERROR_INVALID_DATA);
    return context.Error;
}

DWORD ExtractInstaller(const std::wstring &package, const std::wstring &directory)
{
    WCHAR system[MAX_PATH];
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;
    std::wstring command;
    DWORD code = ERROR_SUCCESS, wait;

    if (!GetSystemDirectoryW(system, _countof(system)))
        return GetLastError();
    command = L"\"" + std::wstring(system) + L"\\msiexec.exe\" /a \"" + package + L"\" /qn TARGETDIR=\"" + directory +
              L"\"";
    if (!CreateProcessW(NULL, &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
        return GetLastError();
    wait = WaitForSingleObject(process.hProcess, MsiTimeout);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        code = ERROR_TIMEOUT;
    }
    else if (!GetExitCodeProcess(process.hProcess, &code))
    {
        code = GetLastError();
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code == ERROR_SUCCESS_REBOOT_REQUIRED ? ERROR_SUCCESS : code;
}

DWORD ExtractNested(const std::wstring &directory, unsigned depth)
{
    std::vector<std::wstring> files;

    if (depth >= MaximumNesting)
        return ERROR_SUCCESS;
    ListFiles(directory, files);
    for (const std::wstring &file : files)
    {
        std::wstring target = file + L".files";
        DWORD error;

        if (DetectKind(file) != PackageKind::Cabinet || GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)
            continue;
        error = ExtractCabinet(file, target);
        if (!error)
            error = ExtractNested(target, depth + 1);
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

std::wstring MatchingId(HDEVINFO set, SP_DEVINFO_DATA *data, SP_DRVINFO_DATA_W *driver, const DeviceInfo &device)
{
    std::vector<BYTE> buffer;
    SP_DRVINFO_DETAIL_DATA_W *detail;
    std::vector<std::wstring> ids;
    const std::vector<std::wstring> *lists[] = { &device.HardwareIds, &device.CompatibleIds };
    DWORD size = 0;

    SetupDiGetDriverInfoDetailW(set, data, driver, NULL, 0, &size);
    if (size < sizeof(SP_DRVINFO_DETAIL_DATA_W))
        return std::wstring();
    buffer.resize(size + sizeof(WCHAR) * 2, 0);
    detail = (SP_DRVINFO_DETAIL_DATA_W *)buffer.data();
    detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
    if (!SetupDiGetDriverInfoDetailW(set, data, driver, detail, size, NULL))
        return std::wstring();
    if (detail->CompatIDsOffset != 1 && detail->HardwareID[0])
        ids.push_back(detail->HardwareID);
    if (detail->CompatIDsOffset > 1 && detail->CompatIDsLength)
    {
        const WCHAR *end = detail->HardwareID + detail->CompatIDsOffset + detail->CompatIDsLength;

        for (const WCHAR *id = detail->HardwareID + detail->CompatIDsOffset; id < end && *id; id += wcslen(id) + 1)
            ids.push_back(id);
    }
    for (const std::vector<std::wstring> *list : lists)
    {
        for (const std::wstring &own : *list)
        {
            for (const std::wstring &id : ids)
            {
                if (_wcsicmp(own.c_str(), id.c_str()) == 0)
                    return own;
            }
        }
    }
    return std::wstring();
}

bool BetterDriver(const PackageDriver &candidate, const PackageDriver &current)
{
    LONG order;

    if (candidate.Rank != current.Rank)
        return candidate.Rank < current.Rank;
    order = CompareFileTime(&candidate.Date, &current.Date);
    if (order)
        return order > 0;
    return candidate.Version > current.Version;
}

void Rescan()
{
    DEVINST root;

    if (CM_Locate_DevNodeW(&root, NULL, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
        CM_Reenumerate_DevNode(root, CM_REENUMERATE_SYNCHRONOUS);
}

}

DWORD EnumerateDevices(std::vector<DeviceInfo> &devices)
{
    HDEVINFO set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    SP_DEVINFO_DATA data = { sizeof(data) };

    devices.clear();
    if (set == INVALID_HANDLE_VALUE)
        return GetLastError();
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); i++)
    {
        DeviceInfo device;
        WCHAR id[MAX_DEVICE_ID_LEN];
        ULONG status = 0, problem = 0;
        HKEY key;

        if (CM_Get_Device_IDW(data.DevInst, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
            device.InstanceId = id;
        device.Name = Property(set, &data, SPDRP_FRIENDLYNAME);
        if (device.Name.empty())
            device.Name = Property(set, &data, SPDRP_DEVICEDESC);
        if (device.Name.empty())
            device.Name = device.InstanceId;
        device.Class = Property(set, &data, SPDRP_CLASS);
        device.Service = Property(set, &data, SPDRP_SERVICE);
        device.HardwareIds = MultiProperty(set, &data, SPDRP_HARDWAREID);
        device.CompatibleIds = MultiProperty(set, &data, SPDRP_COMPATIBLEIDS);
        if (CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0) == CR_SUCCESS)
        {
            device.Started = (status & DN_STARTED) != 0;
            device.Problem = (status & DN_HAS_PROBLEM) ? problem : 0;
        }
        key = SetupDiOpenDevRegKey(set, &data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key != INVALID_HANDLE_VALUE)
        {
            DWORD type = 0, size = sizeof(device.DriverDateData);

            device.DriverVersion = RegistryString(key, L"DriverVersion");
            device.DriverDate = RegistryString(key, L"DriverDate");
            device.Provider = RegistryString(key, L"ProviderName");
            device.Inf = RegistryString(key, L"InfPath");
            if (RegQueryValueExW(key, L"DriverDateData", NULL, &type, (LPBYTE)&device.DriverDateData, &size) !=
                    ERROR_SUCCESS || type != REG_BINARY || size != sizeof(device.DriverDateData))
            {
                device.DriverDateData.dwLowDateTime = device.DriverDateData.dwHighDateTime = 0;
            }
            RegCloseKey(key);
        }
        if (NeedsDriver(device))
        {
            std::wstring cached = CachedDeviceName(device);
            if (!cached.empty())
            {
                device.Name = cached;
                device.NameResolved = true;
            }
        }
        devices.push_back(device);
    }
    SetupDiDestroyDeviceInfoList(set);
    std::stable_sort(devices.begin(), devices.end(), [](const DeviceInfo &left, const DeviceInfo &right) {
        bool a = NeedsDriver(left) || left.Problem, b = NeedsDriver(right) || right.Problem;
        int order;

        if (a != b)
            return a;
        order = _wcsicmp(left.Class.c_str(), right.Class.c_str());
        if (order)
            return order < 0;
        return _wcsicmp(left.Name.c_str(), right.Name.c_str()) < 0;
    });
    return ERROR_SUCCESS;
}

bool FindDevice(const std::wstring &instanceId, DeviceInfo &device)
{
    std::vector<DeviceInfo> devices;

    EnumerateDevices(devices);
    for (const DeviceInfo &current : devices)
    {
        if (_wcsicmp(current.InstanceId.c_str(), instanceId.c_str()) == 0)
        {
            device = current;
            return true;
        }
    }
    return false;
}

bool HasDriver(const DeviceInfo &device)
{
    return !device.Inf.empty() || !device.Service.empty();
}

bool NeedsDriver(const DeviceInfo &device)
{
    return device.Problem == CM_PROB_FAILED_INSTALL || device.Problem == CM_PROB_NOT_CONFIGURED ||
           device.Problem == CM_PROB_REINSTALL || (!HasDriver(device) && !CatalogQueries(device).empty());
}

std::wstring DeviceStatus(const DeviceInfo &device)
{
    if (NeedsDriver(device))
        return LoadText(IDS_STATUS_MISSING);
    if (device.Problem)
        return FormatText(IDS_STATUS_PROBLEM, NumberText(device.Problem));
    return LoadText(device.Started ? IDS_STATUS_WORKING : IDS_STATUS_STOPPED);
}

std::wstring DeviceDetails(const DeviceInfo &device, const std::vector<HistoryEntry> &history)
{
    static const UINT Titles[] = { IDS_DETAIL_HARDWARE, IDS_DETAIL_COMPATIBLE, IDS_DETAIL_QUERIES };
    std::wstring text;
    std::vector<std::wstring> queries = CatalogQueries(device);

    text += FormatText(IDS_DETAIL_NAME, device.Name) + L"\r\n";
    text += FormatText(IDS_DETAIL_STATUS, DeviceStatus(device)) + L"\r\n";
    text += FormatText(IDS_DETAIL_CLASS, device.Class) + L"\r\n";
    text += FormatText(IDS_DETAIL_INSTANCE, device.InstanceId) + L"\r\n";
    text += FormatText(IDS_DETAIL_DRIVER, device.Provider, device.DriverVersion, device.DriverDate) + L"\r\n";
    text += FormatText(IDS_DETAIL_INF, device.Inf) + L"\r\n";
    text += FormatText(IDS_DETAIL_SOURCE, DriverSource(device, history)) + L"\r\n";
    text += FormatText(IDS_DETAIL_SERVICE, device.Service) + L"\r\n\r\n";
    for (UINT title : Titles)
    {
        const std::vector<std::wstring> &list = title == IDS_DETAIL_HARDWARE     ? device.HardwareIds
                                                : title == IDS_DETAIL_COMPATIBLE ? device.CompatibleIds
                                                                                 : queries;

        text += LoadText(title) + L"\r\n";
        for (const std::wstring &id : list)
            text += L"    " + id + L"\r\n";
        if (list.empty())
            text += L"    " + LoadText(IDS_DETAIL_NONE) + L"\r\n";
        text += L"\r\n";
    }
    return text;
}

std::vector<std::wstring> CatalogQueries(const DeviceInfo &device)
{
    std::vector<std::wstring> queries;
    const std::vector<std::wstring> *lists[] = { &device.HardwareIds, &device.CompatibleIds };

    if (StartsWith(device.InstanceId, L"ROOT\\") || StartsWith(device.InstanceId, L"SWD\\") ||
        StartsWith(device.InstanceId, L"HTREE\\"))
    {
        return queries;
    }
    for (const std::vector<std::wstring> *list : lists)
    {
        for (const std::wstring &id : *list)
        {
            bool seen = std::find_if(queries.begin(), queries.end(), [&id](const std::wstring &other) {
                return _wcsicmp(other.c_str(), id.c_str()) == 0;
            }) != queries.end();
            if (!seen && IsSpecificId(id))
                queries.push_back(id);
        }
    }
    return queries;
}

DriverOrder CompareVersionText(const DeviceInfo &device, const std::wstring &version)
{
    DWORDLONG installed, offered;

    if (NeedsDriver(device) || device.Inf.empty() || device.DriverVersion.empty())
        return DriverOrder::Missing;
    installed = ParseVersion(device.DriverVersion);
    offered = ParseVersion(version);
    if (offered == installed)
        return DriverOrder::Same;
    return offered > installed ? DriverOrder::Newer : DriverOrder::Older;
}

DriverOrder CompareWithInstalled(const DeviceInfo &device, const PackageDriver &driver)
{
    DWORDLONG installed;
    LONG order;

    if (NeedsDriver(device) || device.Inf.empty())
        return DriverOrder::Missing;
    order = CompareFileTime(&driver.Date, &device.DriverDateData);
    if (order)
        return order > 0 ? DriverOrder::Newer : DriverOrder::Older;
    installed = ParseVersion(device.DriverVersion);
    if (driver.Version == installed)
        return DriverOrder::Same;
    return driver.Version > installed ? DriverOrder::Newer : DriverOrder::Older;
}

std::wstring ReadInfText(const std::wstring &path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size;
    std::vector<BYTE> bytes;
    std::wstring text;
    DWORD read = 0;

    if (file == INVALID_HANDLE_VALUE)
        return text;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && (ULONGLONG)size.QuadPart <= MaximumInfSize)
    {
        bytes.resize((size_t)size.QuadPart);
        if (!ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, NULL))
            read = 0;
        bytes.resize(read);
    }
    CloseHandle(file);
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
    {
        text.assign((const WCHAR *)(bytes.data() + 2), (bytes.size() - 2) / sizeof(WCHAR));
    }
    else if (!bytes.empty())
    {
        UINT codePage = CP_ACP;
        size_t skip = 0;
        int length;

        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        {
            codePage = CP_UTF8;
            skip = 3;
        }
        length = MultiByteToWideChar(codePage, 0, (LPCSTR)bytes.data() + skip, (int)(bytes.size() - skip), NULL, 0);
        text.resize(length);
        MultiByteToWideChar(codePage, 0, (LPCSTR)bytes.data() + skip, (int)(bytes.size() - skip), &text[0], length);
    }
    return Upper(text);
}

std::wstring CachedDeviceName(const DeviceInfo &device)
{
    const std::wstring &id = device.HardwareIds.empty() ? device.InstanceId : device.HardwareIds.front();
    std::wstring name;
    HKEY key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, NamesKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        name = RegistryString(key, id.c_str());
        RegCloseKey(key);
    }
    return name;
}

void StoreDeviceName(const DeviceInfo &device, const std::wstring &name)
{
    const std::wstring &id = device.HardwareIds.empty() ? device.InstanceId : device.HardwareIds.front();
    HKEY key;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, NamesKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS)
    {
        SetString(key, id.c_str(), name);
        RegCloseKey(key);
    }
}

std::wstring LocalDeviceName(const DeviceInfo &device)
{
    HDEVINFO set = SetupDiCreateDeviceInfoList(NULL, NULL);
    SP_DEVINFO_DATA data = { sizeof(data) };
    SP_DRVINFO_DATA_W driver = { sizeof(driver) };
    std::wstring name;
    DWORD best = MAXDWORD;

    if (set == INVALID_HANDLE_VALUE)
        return name;
    if (SetupDiOpenDeviceInfoW(set, device.InstanceId.c_str(), NULL, 0, &data) &&
        SetupDiBuildDriverInfoList(set, &data, SPDIT_COMPATDRIVER))
    {
        for (DWORD i = 0; SetupDiEnumDriverInfoW(set, &data, SPDIT_COMPATDRIVER, i, &driver); i++)
        {
            SP_DRVINSTALL_PARAMS install = { sizeof(install) };

            if (driver.Description[0] && SetupDiGetDriverInstallParamsW(set, &data, &driver, &install) &&
                install.Rank < best)
            {
                best = install.Rank;
                name = driver.Description;
            }
        }
        SetupDiDestroyDriverInfoList(set, &data, SPDIT_COMPATDRIVER);
    }
    SetupDiDestroyDeviceInfoList(set);
    return name;
}

std::wstring DownloadsRoot()
{
    WCHAR root[MAX_PATH];

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, root)))
        return std::wstring();
    return std::wstring(root) + L"\\LiberNT\\Drivers";
}

std::wstring PackageDirectory(const std::wstring &updateId)
{
    std::wstring root = DownloadsRoot();
    return root.empty() ? root : root + L"\\" + updateId;
}

DWORD PrepareDownloads()
{
    std::wstring root = DownloadsRoot();
    PSECURITY_DESCRIPTOR descriptor = NULL;
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes) };
    DWORD error = ERROR_SUCCESS;
    int created;

    if (root.empty())
        return ERROR_PATH_NOT_FOUND;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(DownloadsSecurity, SDDL_REVISION_1, &descriptor, NULL))
        return GetLastError();
    attributes.lpSecurityDescriptor = descriptor;
    created = SHCreateDirectoryExW(NULL, root.c_str(), &attributes);
    if (created == ERROR_ALREADY_EXISTS || created == ERROR_FILE_EXISTS)
    {
        if (!SetFileSecurityW(root.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              descriptor))
        {
            error = GetLastError();
        }
    }
    else if (created != ERROR_SUCCESS)
    {
        error = (DWORD)created;
    }
    LocalFree(descriptor);
    return error;
}

void DeleteTree(const std::wstring &path)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &data);

    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::wstring name = data.cFileName, child = path + L"\\" + name;

            if (name == L"." || name == L"..")
                continue;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                DeleteTree(child);
            else
            {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(path.c_str());
}

void CleanupDownloads(DWORD days)
{
    std::wstring root = DownloadsRoot();
    WIN32_FIND_DATAW data;
    FILETIME now;
    ULARGE_INTEGER limit;
    HANDLE find;

    if (root.empty())
        return;
    GetSystemTimeAsFileTime(&now);
    limit.LowPart = now.dwLowDateTime;
    limit.HighPart = now.dwHighDateTime;
    limit.QuadPart -= (ULONGLONG)days * 24 * 60 * 60 * 10000000ULL;
    find = FindFirstFileW((root + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        ULARGE_INTEGER written;
        std::wstring name = data.cFileName;

        written.LowPart = data.ftLastWriteTime.dwLowDateTime;
        written.HighPart = data.ftLastWriteTime.dwHighDateTime;
        if (name != L"." && name != L".." && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            written.QuadPart < limit.QuadPart)
        {
            DeleteTree(root + L"\\" + name);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

DWORD ExtractPackage(const std::wstring &file, const std::wstring &directory)
{
    DWORD error;

    switch (DetectKind(file))
    {
        case PackageKind::Cabinet:
            error = ExtractCabinet(file, directory);
            break;
        case PackageKind::Installer:
            error = ExtractInstaller(file, directory);
            break;
        case PackageKind::Program:
            return ERROR_LNTDRV_PROGRAM_PACKAGE;
        default:
            return ERROR_INVALID_DATA;
    }
    return error ? error : ExtractNested(directory, 0);
}

std::vector<std::wstring> PackageInfs(const std::wstring &directory)
{
    std::vector<std::wstring> files, infs;

    ListFiles(directory, files);
    for (const std::wstring &file : files)
    {
        if (HasExtension(file, L".inf"))
            infs.push_back(file);
    }
    return infs;
}

DWORD FindPackageDriver(const DeviceInfo &device, const std::vector<std::wstring> &infs, PackageDriver &best)
{
    HDEVINFO set = SetupDiCreateDeviceInfoList(NULL, NULL);
    SP_DEVINFO_DATA data = { sizeof(data) };
    bool found = false;
    DWORD error = ERROR_SUCCESS;

    if (set == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!SetupDiOpenDeviceInfoW(set, device.InstanceId.c_str(), NULL, 0, &data))
    {
        error = GetLastError();
        SetupDiDestroyDeviceInfoList(set);
        return error;
    }
    for (const std::wstring &inf : infs)
    {
        SP_DEVINSTALL_PARAMS_W params = { sizeof(params) };
        SP_DRVINFO_DATA_W driver = { sizeof(driver) };

        SetupDiDestroyDriverInfoList(set, &data, SPDIT_COMPATDRIVER);
        if (inf.size() >= MAX_PATH || !SetupDiGetDeviceInstallParamsW(set, &data, &params))
            continue;
        params.Flags |= DI_ENUMSINGLEINF;
        memcpy(params.DriverPath, inf.c_str(), (inf.size() + 1) * sizeof(WCHAR));
        if (!SetupDiSetDeviceInstallParamsW(set, &data, &params) ||
            !SetupDiBuildDriverInfoList(set, &data, SPDIT_COMPATDRIVER))
        {
            continue;
        }
        for (DWORD i = 0; SetupDiEnumDriverInfoW(set, &data, SPDIT_COMPATDRIVER, i, &driver); i++)
        {
            SP_DRVINSTALL_PARAMS install = { sizeof(install) };
            PackageDriver candidate;

            if (!SetupDiGetDriverInstallParamsW(set, &data, &driver, &install))
                continue;
            candidate.HardwareId = MatchingId(set, &data, &driver, device);
            if (candidate.HardwareId.empty())
                continue;
            candidate.Inf = inf;
            candidate.Description = driver.Description;
            candidate.Date = driver.DriverDate;
            candidate.Version = driver.DriverVersion;
            candidate.Rank = install.Rank;
            if (!found || BetterDriver(candidate, best))
            {
                best = candidate;
                found = true;
            }
        }
    }
    SetupDiDestroyDriverInfoList(set, &data, SPDIT_COMPATDRIVER);
    SetupDiDestroyDeviceInfoList(set);
    return found ? ERROR_SUCCESS : ERROR_NO_COMPAT_DRIVERS;
}

DWORD InstallPackageDriver(HWND owner, const PackageDriver &driver, bool force, BOOL &reboot)
{
    reboot = FALSE;
    if (UpdateDriverForPlugAndPlayDevicesW(owner, driver.HardwareId.c_str(), driver.Inf.c_str(),
                                           force ? INSTALLFLAG_FORCE : 0, &reboot))
    {
        return ERROR_SUCCESS;
    }
    return GetLastError();
}

DWORD RemoveCatalogDriver(const HistoryEntry &entry, BOOL &reboot)
{
    DeviceInfo device;
    DWORD error = ERROR_SUCCESS;

    reboot = FALSE;
    if (entry.Inf.empty())
        return ERROR_LNTDRV_NOT_IN_USE;
    if (FindDevice(entry.InstanceId, device) && _wcsicmp(device.Inf.c_str(), entry.Inf.c_str()) == 0)
    {
        HDEVINFO set = SetupDiCreateDeviceInfoList(NULL, NULL);
        SP_DEVINFO_DATA data = { sizeof(data) };
        SP_REMOVEDEVICE_PARAMS remove = { { sizeof(SP_CLASSINSTALL_HEADER), DIF_REMOVE }, DI_REMOVEDEVICE_GLOBAL, 0 };
        SP_DEVINSTALL_PARAMS_W params = { sizeof(params) };

        if (set == INVALID_HANDLE_VALUE)
            return GetLastError();
        if (!SetupDiOpenDeviceInfoW(set, entry.InstanceId.c_str(), NULL, 0, &data) ||
            !SetupDiSetClassInstallParamsW(set, &data, &remove.ClassInstallHeader, sizeof(remove)) ||
            !SetupDiCallClassInstaller(DIF_REMOVE, set, &data))
        {
            error = GetLastError();
        }
        else if (SetupDiGetDeviceInstallParamsW(set, &data, &params) &&
                 (params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)))
        {
            reboot = TRUE;
        }
        SetupDiDestroyDeviceInfoList(set);
        if (error)
            return error;
    }
    if (!SetupUninstallOEMInfW(entry.Inf.c_str(), SUOI_FORCEDELETE, NULL) && GetLastError() != ERROR_FILE_NOT_FOUND)
        error = GetLastError();
    Rescan();
    return error;
}

DWORD RollBackDriver(HWND owner, const HistoryEntry &entry, BOOL &reboot)
{
    DeviceInfo device;
    PackageDriver driver;
    std::vector<std::wstring> infs;
    WCHAR windows[MAX_PATH];
    DWORD error;

    reboot = FALSE;
    if (!FindDevice(entry.InstanceId, device))
        return ERROR_NO_SUCH_DEVINST;
    if (entry.Inf.empty() || _wcsicmp(device.Inf.c_str(), entry.Inf.c_str()) != 0)
        return ERROR_LNTDRV_NOT_IN_USE;
    if (entry.PreviousInf.empty())
        return RemoveCatalogDriver(entry, reboot);
    if (!GetWindowsDirectoryW(windows, _countof(windows)))
        return GetLastError();
    infs.push_back(std::wstring(windows) + L"\\inf\\" + entry.PreviousInf);
    error = FindPackageDriver(device, infs, driver);
    if (error)
        return error;
    return InstallPackageDriver(owner, driver, true, reboot);
}

DWORD SaveHistory(HistoryEntry &entry)
{
    SYSTEMTIME now;
    WCHAR stamp[32] = L"", name[96] = L"";
    HKEY key;
    DWORD error;

    GetLocalTime(&now);
    _snwprintf(stamp, _countof(stamp) - 1, L"%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond);
    _snwprintf(name, _countof(name) - 1, L"%04u%02u%02u%02u%02u%02u%03u-%ls", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, entry.UpdateId.c_str());
    entry.Installed = stamp;
    entry.Key = name;
    error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, (std::wstring(HistoryKey) + L"\\" + name).c_str(), 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &key, NULL);
    if (error)
        return error;
    SetString(key, L"Action", entry.Action);
    SetString(key, L"UpdateId", entry.UpdateId);
    SetString(key, L"Title", entry.Title);
    SetString(key, L"Version", entry.Version);
    SetString(key, L"Device", entry.Device);
    SetString(key, L"InstanceId", entry.InstanceId);
    SetString(key, L"HardwareId", entry.HardwareId);
    SetString(key, L"Inf", entry.Inf);
    SetString(key, L"PreviousInf", entry.PreviousInf);
    SetString(key, L"PreviousVersion", entry.PreviousVersion);
    SetString(key, L"Installed", entry.Installed);
    SetDword(key, L"Result", entry.Result);
    SetDword(key, L"Reboot", entry.Reboot);
    RegCloseKey(key);
    return ERROR_SUCCESS;
}

std::vector<HistoryEntry> LoadHistory()
{
    std::vector<HistoryEntry> history;
    HKEY root;
    WCHAR name[256];

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, HistoryKey, 0, KEY_READ, &root) != ERROR_SUCCESS)
        return history;
    for (DWORD i = 0;; i++)
    {
        DWORD length = _countof(name);
        HKEY key;
        HistoryEntry entry;

        if (RegEnumKeyExW(root, i, name, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        entry.Key = name;
        entry.Action = RegistryString(key, L"Action");
        entry.UpdateId = RegistryString(key, L"UpdateId");
        entry.Title = RegistryString(key, L"Title");
        entry.Version = RegistryString(key, L"Version");
        entry.Device = RegistryString(key, L"Device");
        entry.InstanceId = RegistryString(key, L"InstanceId");
        entry.HardwareId = RegistryString(key, L"HardwareId");
        entry.Inf = RegistryString(key, L"Inf");
        entry.PreviousInf = RegistryString(key, L"PreviousInf");
        entry.PreviousVersion = RegistryString(key, L"PreviousVersion");
        entry.Installed = RegistryString(key, L"Installed");
        entry.Result = RegistryDword(key, L"Result");
        entry.Reboot = RegistryDword(key, L"Reboot");
        if (entry.Action.empty())
            entry.Action = L"install";
        RegCloseKey(key);
        history.push_back(entry);
    }
    RegCloseKey(root);
    std::stable_sort(history.begin(), history.end(), [](const HistoryEntry &left, const HistoryEntry &right) {
        return left.Key > right.Key;
    });
    return history;
}

const HistoryEntry *CatalogEntryInUse(const DeviceInfo &device, const std::vector<HistoryEntry> &history)
{
    if (!StartsWith(device.Inf, L"oem"))
        return NULL;
    for (const HistoryEntry &entry : history)
    {
        if (!entry.Result && entry.Action == L"install" &&
            _wcsicmp(entry.InstanceId.c_str(), device.InstanceId.c_str()) == 0 &&
            _wcsicmp(entry.Inf.c_str(), device.Inf.c_str()) == 0)
        {
            return &entry;
        }
    }
    return NULL;
}

std::wstring DriverSource(const DeviceInfo &device, const std::vector<HistoryEntry> &history)
{
    if (device.Inf.empty())
        return std::wstring();
    if (!StartsWith(device.Inf, L"oem"))
        return std::wstring();
    return LoadText(CatalogEntryInUse(device, history) ? IDS_SOURCE_CATALOG : IDS_SOURCE_THIRD_PARTY);
}

}
