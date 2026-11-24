/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installed application registry, install tracking and uninstall
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "arp.hpp"
#include "manifest.hpp"
#include "portable.hpp"
#include "source.hpp"
#include "util.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>
#include <wctype.h>

namespace lntget
{

namespace
{

constexpr const wchar_t *UninstallKey = ArpUninstallKey;
constexpr wchar_t MachineEnvironmentKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
constexpr wchar_t TrackingKey[] = L"Software\\LiberNT\\lntget\\Installed";
constexpr DWORD UninstallRegistrationTimeoutMs = 120000;
constexpr DWORD UninstallRegistrationRecheckMs = 1000;

void TraceArp(const std::string &message)
{
    const std::string line = "[LNTGET:ARP] " + message + "\n";
    OutputDebugStringA(line.c_str());
}

bool HasWow64View()
{
    if (sizeof(void *) == 8)
        return true;
    BOOL wow64 = FALSE;
    return IsWow64Process(GetCurrentProcess(), &wow64) && wow64;
}

REGSAM ViewFlags(bool machine, bool wow64View)
{
    if (!machine || !HasWow64View())
        return 0;
    return wow64View ? KEY_WOW64_32KEY : KEY_WOW64_64KEY;
}

bool ReadString(HKEY key, const wchar_t *name, std::wstring &value)
{
    value.clear();
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t))
        return false;
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(buffer.data()), &bytes) != ERROR_SUCCESS)
        return false;
    buffer.back() = L'\0';
    value.assign(buffer.data());
    if (type == REG_EXPAND_SZ && !value.empty())
    {
        const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (needed)
        {
            std::vector<wchar_t> expanded(needed);
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed))
                value.assign(expanded.data());
        }
    }
    return !value.empty();
}

DWORD ReadDword(HKEY key, const wchar_t *name)
{
    DWORD value = 0;
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(&value), &bytes) == ERROR_SUCCESS &&
           type == REG_DWORD ? value : 0;
}

bool HasValue(HKEY key, const wchar_t *name)
{
    return RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void EnumerateView(bool machine, bool wow64View, std::vector<ArpEntry> &entries)
{
    HKEY uninstall = nullptr;
    if (RegOpenKeyExW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, UninstallKey, 0,
                      KEY_READ | ViewFlags(machine, wow64View), &uninstall) != ERROR_SUCCESS)
        return;

    for (DWORD index = 0;; ++index)
    {
        wchar_t keyName[256]{};
        DWORD keyLength = ARRAYSIZE(keyName);
        const LONG enumeration = RegEnumKeyExW(uninstall, index, keyName, &keyLength, nullptr, nullptr, nullptr, nullptr);
        if (enumeration == ERROR_NO_MORE_ITEMS)
            break;
        if (enumeration != ERROR_SUCCESS)
            continue;

        HKEY key = nullptr;
        if (RegOpenKeyExW(uninstall, keyName, 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        ArpEntry entry;
        entry.machine = machine;
        entry.wow64View = wow64View;
        entry.keyName = keyName;
        ReadString(key, L"DisplayName", entry.name);
        if (entry.name.empty() || ReadDword(key, L"SystemComponent") || HasValue(key, L"ParentKeyName"))
        {
            RegCloseKey(key);
            continue;
        }
        ReadString(key, L"DisplayVersion", entry.version);
        ReadString(key, L"Publisher", entry.publisher);
        ReadString(key, L"InstallLocation", entry.location);
        ReadString(key, L"UninstallString", entry.uninstall);
        ReadString(key, L"QuietUninstallString", entry.quietUninstall);
        entry.windowsInstaller = ReadDword(key, L"WindowsInstaller") != 0;
        entry.innoSetup = HasValue(key, L"Inno Setup: App Path");
        std::wstring installerType;
        if (ReadString(key, L"WinGetInstallerType", installerType) && _wcsicmp(installerType.c_str(), L"portable") == 0)
        {
            entry.portable = true;
            entry.installDirectoryCreated = ReadDword(key, L"InstallDirectoryCreated") != 0;
            entry.installDirectoryAddedToPath = ReadDword(key, L"InstallDirectoryAddedToPath") != 0;
            entry.symlinks = ReadArpMultiString(key, L"LntGetSymlinks");
            entry.pathEntries = ReadArpMultiString(key, L"LntGetPathEntries");
            entry.filesTracked = HasValue(key, L"LntGetFiles");
            entry.files = ReadArpMultiString(key, L"LntGetFiles");
            std::wstring symlink;
            if (entry.symlinks.empty() && ReadString(key, L"SymlinkFullPath", symlink))
                entry.symlinks.push_back(symlink);
        }
        entry.estimatedBytes = static_cast<unsigned long long>(ReadDword(key, L"EstimatedSize")) * 1024;
        RegCloseKey(key);
        entries.push_back(entry);
    }
    RegCloseKey(uninstall);
}

bool IsProductCode(const std::wstring &value)
{
    if (value.size() != 38 || value[0] != L'{' || value[37] != L'}')
        return false;
    for (std::size_t index = 1; index < 37; ++index)
    {
        const wchar_t character = value[index];
        if (index == 9 || index == 14 || index == 19 || index == 24)
        {
            if (character != L'-')
                return false;
        }
        else if (!iswxdigit(character))
        {
            return false;
        }
    }
    return true;
}

std::wstring CommandExecutable(const std::wstring &command)
{
    std::size_t start = 0;
    while (start < command.size() && command[start] == L' ')
        ++start;
    if (start < command.size() && command[start] == L'"')
    {
        const std::size_t end = command.find(L'"', start + 1);
        return end == std::wstring::npos ? std::wstring() : command.substr(start + 1, end - start - 1);
    }
    std::wstring lower(command.substr(start));
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
    const std::size_t exe = lower.find(L".exe");
    return exe == std::wstring::npos ? std::wstring() : command.substr(start, exe + 4);
}

bool IsNsisExecutable(const std::wstring &path)
{
    static constexpr char Signature[] = "NullsoftInst";
    if (path.empty())
        return false;
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, 64ull * 1024 * 1024))
        return false;
    const char *begin = reinterpret_cast<const char *>(bytes.data());
    return std::search(begin, begin + bytes.size(), Signature, Signature + sizeof(Signature) - 1) != begin + bytes.size();
}

std::wstring AppendNsisSwitch(const std::wstring &command, const wchar_t *option)
{
    /* NSIS reads everything after _?= as the install directory */
    const std::size_t directory = command.find(L" _?=");
    if (directory == std::wstring::npos)
        return command + L" " + option;
    return command.substr(0, directory) + L" " + option + command.substr(directory);
}

std::wstring EntrySubKey(const ArpEntry &entry)
{
    return std::wstring(UninstallKey) + L"\\" + entry.keyName;
}

bool EntryRegistered(const ArpEntry &entry)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(entry.machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, EntrySubKey(entry).c_str(), 0,
                      KEY_READ | ViewFlags(entry.machine, entry.wow64View), &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

class RegistrationWatch
{
public:
    explicit RegistrationWatch(const ArpEntry &entry)
    {
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event_ && RegOpenKeyExW(entry.machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, UninstallKey, 0,
                                    KEY_NOTIFY | ViewFlags(entry.machine, entry.wow64View), &key_) != ERROR_SUCCESS)
            key_ = nullptr;
        Arm();
    }

    ~RegistrationWatch()
    {
        if (key_) RegCloseKey(key_);
        if (event_) CloseHandle(event_);
    }

    void Wait(DWORD milliseconds)
    {
        if (!armed_)
        {
            Sleep(milliseconds);
            return;
        }
        if (WaitForSingleObject(event_, milliseconds) == WAIT_OBJECT_0)
            Arm();
    }

private:
    void Arm()
    {
        armed_ = key_ && event_ &&
                 RegNotifyChangeKeyValue(key_, FALSE, REG_NOTIFY_CHANGE_NAME, event_, TRUE) == ERROR_SUCCESS;
    }

    HKEY key_ = nullptr;
    HANDLE event_ = nullptr;
    bool armed_ = false;
};

int CompareVersionPart(const std::string &left, const std::string &right)
{
    std::size_t leftDigits = 0;
    std::size_t rightDigits = 0;
    while (leftDigits < left.size() && left[leftDigits] >= '0' && left[leftDigits] <= '9') ++leftDigits;
    while (rightDigits < right.size() && right[rightDigits] >= '0' && right[rightDigits] <= '9') ++rightDigits;
    std::string leftNumber = left.substr(0, leftDigits);
    std::string rightNumber = right.substr(0, rightDigits);
    leftNumber.erase(0, std::min(leftNumber.find_first_not_of('0'), leftNumber.size()));
    rightNumber.erase(0, std::min(rightNumber.find_first_not_of('0'), rightNumber.size()));
    if (leftNumber.size() != rightNumber.size())
        return leftNumber.size() < rightNumber.size() ? -1 : 1;
    if (leftNumber != rightNumber)
        return leftNumber < rightNumber ? -1 : 1;
    const std::string leftSuffix = AsciiLower(left.substr(leftDigits));
    const std::string rightSuffix = AsciiLower(right.substr(rightDigits));
    if (leftSuffix == rightSuffix)
        return 0;
    if (leftSuffix.empty())
        return 1;
    if (rightSuffix.empty())
        return -1;
    return leftSuffix < rightSuffix ? -1 : 1;
}

std::vector<std::string> SplitVersion(const std::string &version)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;)
    {
        const std::size_t dot = version.find('.', start);
        parts.push_back(Trim(version.substr(start, dot == std::string::npos ? std::string::npos : dot - start)));
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    while (parts.size() > 1 && (parts.back().empty() || parts.back().find_first_not_of('0') == std::string::npos))
        parts.pop_back();
    return parts;
}

std::vector<std::wstring> SplitPathVariable(const std::wstring &value)
{
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    for (;;)
    {
        const std::size_t end = value.find(L';', start);
        const std::wstring part = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!part.empty())
            parts.push_back(part);
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return parts;
}

bool SamePathEntry(std::wstring left, std::wstring right)
{
    while (left.size() > 3 && (left[left.size() - 1] == L'\\' || left[left.size() - 1] == L'/')) left.pop_back();
    while (right.size() > 3 && (right[right.size() - 1] == L'\\' || right[right.size() - 1] == L'/')) right.pop_back();
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

Status UpdatePathVariable(bool machine, const std::wstring &directory, bool append)
{
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, machine ? MachineEnvironmentKey : L"Environment",
                                  0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        return Status::Fail(static_cast<DWORD>(result), "cannot open the PATH variable: " + WindowsErrorMessage(static_cast<DWORD>(result)));
    std::wstring current;
    DWORD type = REG_EXPAND_SZ;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && bytes >= sizeof(wchar_t))
    {
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, L"Path", nullptr, &type, reinterpret_cast<BYTE *>(buffer.data()), &bytes) == ERROR_SUCCESS)
            current.assign(buffer.data());
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        type = REG_EXPAND_SZ;
    std::vector<std::wstring> parts = SplitPathVariable(current);
    const auto existing = std::find_if(parts.begin(), parts.end(),
                                       [&directory](const std::wstring &part) { return SamePathEntry(part, directory); });
    bool changed = false;
    if (append && existing == parts.end())
    {
        parts.push_back(directory);
        changed = true;
    }
    else if (!append && existing != parts.end())
    {
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                                   [&directory](const std::wstring &part) { return SamePathEntry(part, directory); }),
                    parts.end());
        changed = true;
    }
    if (changed)
    {
        std::wstring updated;
        for (const std::wstring &part : parts)
        {
            if (!updated.empty()) updated.push_back(L';');
            updated += part;
        }
        result = RegSetValueExW(key, L"Path", 0, type, reinterpret_cast<const BYTE *>(updated.c_str()),
                                static_cast<DWORD>((updated.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        return Status::Fail(static_cast<DWORD>(result), "cannot update the PATH variable: " + WindowsErrorMessage(static_cast<DWORD>(result)));
    if (changed)
    {
        TraceArp(std::string(append ? "added " : "removed ") + Utf8FromWide(directory) + (append ? " to the " : " from the ") +
                 (machine ? "machine PATH" : "user PATH"));
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
                            SMTO_ABORTIFHUNG, 5000, &ignored);
    }
    return Status::Ok();
}

} // namespace

const wchar_t ArpUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

bool ReadArpString(HKEY key, const wchar_t *name, std::wstring &value)
{
    return ReadString(key, name, value);
}

std::vector<std::wstring> ReadArpMultiString(HKEY key, const wchar_t *name)
{
    std::vector<std::wstring> values;
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_MULTI_SZ ||
        bytes < sizeof(wchar_t))
        return values;
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(buffer.data()), &bytes) != ERROR_SUCCESS)
        return values;
    for (const wchar_t *item = buffer.data(); *item; item += wcslen(item) + 1)
        values.push_back(item);
    return values;
}

Status AppendPathVariable(bool machine, const std::wstring &directory)
{
    return UpdatePathVariable(machine, directory, true);
}

Status RemovePathVariable(bool machine, const std::wstring &directory)
{
    return UpdatePathVariable(machine, directory, false);
}

std::string ArpEntry::Identity() const
{
    std::string architecture;
    if (wow64View)
    {
        architecture = "X86";
    }
    else
    {
        switch (CurrentMachineArchitecture())
        {
            case MachineArchitecture::X86: architecture = "X86"; break;
            case MachineArchitecture::X64: architecture = "X64"; break;
            case MachineArchitecture::Arm: architecture = "Arm"; break;
            case MachineArchitecture::Arm64: architecture = "Arm64"; break;
            case MachineArchitecture::Riscv64: architecture = "Riscv64"; break;
            default: architecture = "Unknown"; break;
        }
    }
    return std::string("ARP\\") + (machine ? "Machine\\" : "User\\") + architecture + "\\" + Utf8FromWide(keyName);
}

std::vector<ArpEntry> EnumerateArpEntries()
{
    std::vector<ArpEntry> entries;
    EnumerateView(false, false, entries);
    EnumerateView(true, false, entries);
    if (HasWow64View())
        EnumerateView(true, true, entries);
    std::sort(entries.begin(), entries.end(), [](const ArpEntry &left, const ArpEntry &right) {
        const int order = lstrcmpiW(left.name.c_str(), right.name.c_str());
        return order ? order < 0 : left.Identity() < right.Identity();
    });
    return entries;
}

ArpSnapshot TakeArpSnapshot()
{
    ArpSnapshot snapshot;
    for (const ArpEntry &entry : EnumerateArpEntries())
        snapshot[entry.Identity()] = entry.version;
    return snapshot;
}

std::vector<std::string> ChangedArpIdentities(const ArpSnapshot &before, const ArpSnapshot &after)
{
    std::vector<std::string> changed;
    for (const auto &entry : after)
    {
        const auto previous = before.find(entry.first);
        if (previous == before.end() || previous->second != entry.second)
            changed.push_back(entry.first);
    }
    return changed;
}

Status RecordTrackedPackage(const TrackedPackage &package)
{
    assert(!package.id.empty());
    const std::wstring path = std::wstring(TrackingKey) + L"\\" + WideFromUtf8(package.id);
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        return Status::Fail(static_cast<DWORD>(result), "cannot record the installed package: " + WindowsErrorMessage(static_cast<DWORD>(result)));

    const std::wstring version = WideFromUtf8(package.version);
    const std::wstring type = WideFromUtf8(package.installerType);
    std::wstring identities;
    for (const std::string &identity : package.arpIdentities)
    {
        identities += WideFromUtf8(identity);
        identities.push_back(L'\0');
    }
    identities.push_back(L'\0');
    result = RegSetValueExW(key, L"Version", 0, REG_SZ, reinterpret_cast<const BYTE *>(version.c_str()),
                            static_cast<DWORD>((version.size() + 1) * sizeof(wchar_t)));
    if (result == ERROR_SUCCESS)
        result = RegSetValueExW(key, L"InstallerType", 0, REG_SZ, reinterpret_cast<const BYTE *>(type.c_str()),
                                static_cast<DWORD>((type.size() + 1) * sizeof(wchar_t)));
    if (result == ERROR_SUCCESS)
        result = RegSetValueExW(key, L"Arp", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE *>(identities.data()),
                                static_cast<DWORD>(identities.size() * sizeof(wchar_t)));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        return Status::Fail(static_cast<DWORD>(result), "cannot record the installed package: " + WindowsErrorMessage(static_cast<DWORD>(result)));
    return Status::Ok();
}

Status RemoveTrackedPackage(const std::string &id)
{
    assert(!id.empty());
    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, TrackingKey, 0, KEY_READ | KEY_WRITE, &key);
    if (result == ERROR_FILE_NOT_FOUND)
        return Status::Ok();
    if (result != ERROR_SUCCESS)
        return Status::Fail(static_cast<DWORD>(result), "cannot open the installed package records: " + WindowsErrorMessage(static_cast<DWORD>(result)));
    result = RegDeleteKeyW(key, WideFromUtf8(id).c_str());
    RegCloseKey(key);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        return Status::Fail(static_cast<DWORD>(result), "cannot remove the installed package record: " + WindowsErrorMessage(static_cast<DWORD>(result)));
    return Status::Ok();
}

std::vector<TrackedPackage> LoadTrackedPackages()
{
    std::vector<TrackedPackage> packages;
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, TrackingKey, 0, KEY_READ, &root) != ERROR_SUCCESS)
        return packages;
    for (DWORD index = 0;; ++index)
    {
        wchar_t name[256]{};
        DWORD length = ARRAYSIZE(name);
        const LONG enumeration = RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (enumeration == ERROR_NO_MORE_ITEMS)
            break;
        if (enumeration != ERROR_SUCCESS)
            continue;
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        TrackedPackage package;
        package.id = Utf8FromWide(name);
        std::wstring value;
        if (ReadString(key, L"Version", value)) package.version = Utf8FromWide(value);
        if (ReadString(key, L"InstallerType", value)) package.installerType = Utf8FromWide(value);
        DWORD type = 0;
        DWORD bytes = 0;
        if (RegQueryValueExW(key, L"Arp", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && type == REG_MULTI_SZ &&
            bytes >= sizeof(wchar_t))
        {
            std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
            if (RegQueryValueExW(key, L"Arp", nullptr, &type, reinterpret_cast<BYTE *>(buffer.data()), &bytes) == ERROR_SUCCESS)
            {
                for (const wchar_t *item = buffer.data(); *item; item += wcslen(item) + 1)
                    package.arpIdentities.push_back(Utf8FromWide(item));
            }
        }
        RegCloseKey(key);
        packages.push_back(package);
    }
    RegCloseKey(root);
    return packages;
}

int CompareVersions(const std::string &left, const std::string &right)
{
    const std::vector<std::string> leftParts = SplitVersion(left);
    const std::vector<std::string> rightParts = SplitVersion(right);
    const std::size_t count = std::max(leftParts.size(), rightParts.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::string leftPart = index < leftParts.size() ? leftParts[index] : "0";
        const std::string rightPart = index < rightParts.size() ? rightParts[index] : "0";
        const int order = CompareVersionPart(leftPart, rightPart);
        if (order)
            return order;
    }
    return 0;
}

Status CollectInstalledPackages(SourceManager *source, std::vector<InstalledPackage> &packages)
{
    packages.clear();
    std::map<std::string, TrackedPackage> tracked;
    for (const TrackedPackage &package : LoadTrackedPackages())
    {
        for (const std::string &identity : package.arpIdentities)
            tracked[AsciiLower(identity)] = package;
    }
    std::map<std::string, PackageRecord> productCodes;
    std::map<std::string, PackageRecord> catalog;
    Status status = Status::Ok();
    if (source)
        status = source->LoadArpCorrelation(productCodes, catalog);

    for (const ArpEntry &entry : EnumerateArpEntries())
    {
        InstalledPackage package;
        package.entry = entry;
        const auto record = tracked.find(AsciiLower(entry.Identity()));
        if (record != tracked.end())
        {
            package.packageId = record->second.id;
            package.installerType = record->second.installerType;
            package.correlated = true;
        }
        else
        {
            const auto product = productCodes.find(AsciiLower(Utf8FromWide(entry.keyName)));
            if (product != productCodes.end())
            {
                package.packageId = product->second.id;
                package.correlated = true;
            }
        }
        if (package.correlated)
        {
            const auto latest = catalog.find(AsciiLower(package.packageId));
            if (latest != catalog.end())
            {
                package.packageName = latest->second.name;
                if (!entry.version.empty() && CompareVersions(latest->second.version, Utf8FromWide(entry.version)) > 0)
                    package.available = latest->second.version;
            }
        }
        packages.push_back(package);
    }
    return status;
}

std::wstring BuildUninstallCommand(const ArpEntry &entry, const std::string &installerType, InstallMode mode)
{
    const bool quiet = mode != InstallMode::Interactive;
    if (entry.portable)
        return entry.uninstall;
    if (entry.windowsInstaller && IsProductCode(entry.keyName))
    {
        std::wstring command = L"msiexec.exe /x " + entry.keyName;
        if (quiet)
            command += L" /quiet /norestart";
        return command;
    }
    if (quiet && !entry.quietUninstall.empty())
        return entry.quietUninstall;
    if (entry.uninstall.empty() || !quiet)
        return entry.uninstall;

    std::wstring command(entry.uninstall);
    if (entry.innoSetup || AsciiEquals(installerType, "inno"))
        command += L" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    else if (AsciiEquals(installerType, "nullsoft") || IsNsisExecutable(CommandExecutable(entry.uninstall)))
        command = AppendNsisSwitch(command, L"/S");
    else
        TraceArp("no silent uninstall switch is known for " + entry.Identity() + "; running its uninstaller as registered");
    return command;
}

Status UninstallArpEntry(const ArpEntry &entry, const std::string &installerType, InstallMode mode, DWORD &exitCode,
                         InstallLockWait *lockWait)
{
    exitCode = 0;
    InstallLock lock;
    Status status = lock.Acquire(lockWait);
    if (!status)
        return status;
    if (entry.portable)
        return UninstallPortable(entry);
    const std::wstring command = BuildUninstallCommand(entry, installerType, mode);
    if (command.empty())
        return Status::Fail(ERROR_NOT_SUPPORTED, Utf8FromWide(entry.name) + " did not register an uninstall command");

    RegistrationWatch watch(entry);
    TraceArp("uninstalling " + entry.Identity() + " with: " + Utf8FromWide(command));
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot start the uninstaller: " + WindowsErrorMessage(error));
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
    if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, &exitCode))
    {
        const DWORD error = GetLastError();
        CloseHandle(process.hProcess);
        return Status::Fail(error, "cannot wait for the uninstaller: " + WindowsErrorMessage(error));
    }
    CloseHandle(process.hProcess);
    TraceArp("uninstaller exit code " + DecimalString(exitCode));
    if (exitCode != ERROR_SUCCESS && exitCode != ERROR_SUCCESS_REBOOT_INITIATED && exitCode != ERROR_SUCCESS_REBOOT_REQUIRED)
        return Status::Fail(exitCode, "uninstaller returned exit code " + DecimalString(exitCode));

    const DWORD start = GetTickCount();
    while (EntryRegistered(entry))
    {
        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= UninstallRegistrationTimeoutMs)
            return Status::Fail(ERROR_TIMEOUT, "the uninstaller finished but " + Utf8FromWide(entry.name) + " is still registered");
        watch.Wait(std::min(UninstallRegistrationRecheckMs, UninstallRegistrationTimeoutMs - elapsed));
    }
    TraceArp("registration removed after " + DecimalString(GetTickCount() - start) + " ms");
    return Status::Ok();
}

Status RunArpSelfTests()
{
    if (CompareVersions("26.06", "26.6") != 0 || CompareVersions("1.2", "1.10") >= 0 ||
        CompareVersions("2.0", "2") != 0 || CompareVersions("160.0.1", "159.0") <= 0 ||
        CompareVersions("1.0-beta", "1.0") >= 0 || CompareVersions("8.9.9.1", "8.9.9") <= 0)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "version comparison self-test failed");

    ArpEntry entry;
    entry.machine = true;
    entry.keyName = L"{23170F69-40C1-2702-2606-000001000000}";
    entry.windowsInstaller = true;
    entry.uninstall = L"MsiExec.exe /I{23170F69-40C1-2702-2606-000001000000}";
    if (BuildUninstallCommand(entry, std::string(), InstallMode::Silent) !=
        L"msiexec.exe /x {23170F69-40C1-2702-2606-000001000000} /quiet /norestart")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "MSI uninstall command self-test failed");
    if (BuildUninstallCommand(entry, std::string(), InstallMode::Interactive) !=
        L"msiexec.exe /x {23170F69-40C1-2702-2606-000001000000}")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "interactive MSI uninstall command self-test failed");

    entry = ArpEntry();
    entry.keyName = L"Example_is1";
    entry.innoSetup = true;
    entry.uninstall = L"\"C:\\Program Files\\Example\\unins000.exe\"";
    if (BuildUninstallCommand(entry, std::string(), InstallMode::Default) !=
        L"\"C:\\Program Files\\Example\\unins000.exe\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "Inno Setup uninstall command self-test failed");
    entry.quietUninstall = L"\"C:\\Program Files\\Example\\unins000.exe\" /SILENT";
    if (BuildUninstallCommand(entry, std::string(), InstallMode::Silent) != entry.quietUninstall ||
        BuildUninstallCommand(entry, std::string(), InstallMode::Interactive) != entry.uninstall)
        return Status::Fail(ERROR_ASSERTION_FAILURE, "registered quiet uninstall command self-test failed");
    entry = ArpEntry();
    entry.keyName = L"foobar2000 (x64)";
    entry.uninstall = L"\"C:\\Program Files\\foobar2000\\uninstall.exe\" _?=C:\\Program Files\\foobar2000";
    if (BuildUninstallCommand(entry, "nullsoft", InstallMode::Silent) !=
        L"\"C:\\Program Files\\foobar2000\\uninstall.exe\" /S _?=C:\\Program Files\\foobar2000")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "NSIS install directory uninstall command self-test failed");
    entry.uninstall = L"\"C:\\Program Files\\Notepad++\\uninstall.exe\"";
    if (BuildUninstallCommand(entry, "nullsoft", InstallMode::Silent) != L"\"C:\\Program Files\\Notepad++\\uninstall.exe\" /S")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "NSIS uninstall command self-test failed");
    if (CommandExecutable(L"C:\\Program Files\\App\\uninst.exe /x") != L"C:\\Program Files\\App\\uninst.exe" ||
        CommandExecutable(L"\"C:\\A B\\u.exe\" /S") != L"C:\\A B\\u.exe")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "uninstall executable parser self-test failed");

    ArpSnapshot before;
    before["ARP\\Machine\\X64\\A"] = L"1.0";
    before["ARP\\Machine\\X64\\B"] = L"1.0";
    ArpSnapshot after(before);
    after["ARP\\Machine\\X64\\B"] = L"2.0";
    after["ARP\\User\\X64\\C"] = L"";
    const std::vector<std::string> changed = ChangedArpIdentities(before, after);
    if (changed.size() != 2 || changed[0] != "ARP\\Machine\\X64\\B" || changed[1] != "ARP\\User\\X64\\C")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "installed application snapshot self-test failed");
    return Status::Ok();
}

} // namespace lntget
