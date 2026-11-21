/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable package installation in the WinGet layout
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include "portable.hpp"
#include "installer.hpp"
#include "manifest.hpp"
#include "util.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace lntget
{

namespace
{

constexpr char WinGetSourceIdentifier[] = "Microsoft.Winget.Source_8wekyb3d8bbwe";

void TracePortable(const std::string &message)
{
    const std::string line = "[LNTGET:PORTABLE] " + message + "\n";
    OutputDebugStringA(line.c_str());
}

std::wstring KnownFolder(int folder)
{
    wchar_t path[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, folder, nullptr, SHGFP_TYPE_CURRENT, path)))
        return std::wstring();
    return path;
}

std::wstring PortableRoot(bool machineScope)
{
    if (machineScope)
        return JoinPath(JoinPath(KnownFolder(CSIDL_PROGRAM_FILES), L"WinGet"), L"Packages");
    return JoinPath(JoinPath(JoinPath(KnownFolder(CSIDL_LOCAL_APPDATA), L"Microsoft"), L"WinGet"), L"Packages");
}

std::wstring LinksDirectory(bool machineScope)
{
    if (machineScope)
        return JoinPath(JoinPath(KnownFolder(CSIDL_PROGRAM_FILES), L"WinGet"), L"Links");
    return JoinPath(JoinPath(JoinPath(KnownFolder(CSIDL_LOCAL_APPDATA), L"Microsoft"), L"WinGet"), L"Links");
}

std::wstring WithExecutableExtension(std::wstring name)
{
    const std::size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos || _wcsicmp(name.c_str() + dot, L".exe") != 0)
        name += L".exe";
    return name;
}

std::wstring FileNamePart(const std::wstring &path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool SafeAlias(const std::wstring &alias)
{
    return !alias.empty() && alias.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos && alias != L"." && alias != L"..";
}

Status EnsureDirectoryPath(const std::wstring &path)
{
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return Status::Ok();
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS)
        return Status::Fail(static_cast<DWORD>(result), "cannot create " + Utf8FromWide(path) + ": " +
                            WindowsErrorMessage(static_cast<DWORD>(result)));
    return Status::Ok();
}

std::wstring RelativeNestedPath(const std::string &relativePath)
{
    std::wstring path = WideFromUtf8(relativePath);
    for (wchar_t &character : path)
    {
        if (character == L'/')
            character = L'\\';
    }
    while (path.size() >= 2 && path[0] == L'.' && path[1] == L'\\')
        path.erase(0, 2);
    if (path.empty() || path[0] == L'\\' || path.find(L':') != std::wstring::npos || path.find(L"..") != std::wstring::npos)
        return std::wstring();
    return path;
}

struct PortableLink
{
    std::wstring target;
    std::wstring alias;
};

Status SetString(HKEY key, const wchar_t *name, const std::wstring &value)
{
    const LONG result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    return result == ERROR_SUCCESS ? Status::Ok() :
           Status::Fail(static_cast<DWORD>(result), "cannot write the portable registration: " + WindowsErrorMessage(static_cast<DWORD>(result)));
}

Status SetDword(HKEY key, const wchar_t *name, DWORD value)
{
    const LONG result = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    return result == ERROR_SUCCESS ? Status::Ok() :
           Status::Fail(static_cast<DWORD>(result), "cannot write the portable registration: " + WindowsErrorMessage(static_cast<DWORD>(result)));
}

Status SetMultiString(HKEY key, const wchar_t *name, const std::vector<std::wstring> &values)
{
    std::wstring data;
    for (const std::wstring &value : values)
    {
        data += value;
        data.push_back(L'\0');
    }
    data.push_back(L'\0');
    const LONG result = RegSetValueExW(key, name, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE *>(data.data()),
                                       static_cast<DWORD>(data.size() * sizeof(wchar_t)));
    return result == ERROR_SUCCESS ? Status::Ok() :
           Status::Fail(static_cast<DWORD>(result), "cannot write the portable registration: " + WindowsErrorMessage(static_cast<DWORD>(result)));
}

std::wstring InstallDate()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[16]{};
    swprintf(text, ARRAYSIZE(text), L"%04u%02u%02u", now.wYear, now.wMonth, now.wDay);
    return text;
}

Status CopyIntoPackage(const std::wstring &source, const std::wstring &target)
{
    if (!CopyFileW(source.c_str(), target.c_str(), FALSE))
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot copy " + Utf8FromWide(FileNamePart(target)) + " into the package: " + WindowsErrorMessage(error));
    }
    return Status::Ok();
}

Status LinkAlias(const std::wstring &target, const std::wstring &alias)
{
    const std::wstring link = JoinPath(ParentPath(target), alias);
    if (_wcsicmp(link.c_str(), target.c_str()) == 0)
        return Status::Ok();
    DeleteFileW(link.c_str());
    if (CreateHardLinkW(link.c_str(), target.c_str(), nullptr))
        return Status::Ok();
    TracePortable("hard link " + Utf8FromWide(link) + " failed with " + DecimalString(GetLastError()) + "; copying instead");
    return CopyIntoPackage(target, link);
}

} // namespace

std::string PortableProductCode(const std::string &packageId)
{
    std::string code = packageId + "_" + WinGetSourceIdentifier;
    for (char &character : code)
    {
        if (static_cast<unsigned char>(character) < 0x20 || std::strchr("\\/:*?\"<>|", character))
            character = '_';
    }
    return code;
}

Status InstallPortable(const PackageRecord &package, const InstallerEntry &installer, const std::wstring &downloadPath,
                       bool machineScope)
{
    const std::string productCode = PortableProductCode(package.id);
    const std::wstring installDirectory = JoinPath(PortableRoot(machineScope), WideFromUtf8(productCode));
    const std::wstring linksDirectory = LinksDirectory(machineScope);
    const bool directoryExisted = GetFileAttributesW(installDirectory.c_str()) != INVALID_FILE_ATTRIBUTES;
    Status status = EnsureDirectoryPath(installDirectory);
    if (!status) return status;
    status = EnsureDirectoryPath(linksDirectory);
    if (!status) return status;

    std::vector<PortableLink> links;
    if (AsciiEquals(installer.type, "zip"))
    {
        std::printf("Extracting archive...\n");
        status = ExtractArchiveInto(downloadPath, installDirectory);
        if (!status) return status;
        for (const NestedInstallerFile &file : installer.nestedFiles)
        {
            const std::wstring relative = RelativeNestedPath(file.relativePath);
            if (relative.empty())
                return Status::Fail(ERROR_BAD_FORMAT, "the manifest names an invalid portable file: " + file.relativePath);
            PortableLink link;
            link.target = JoinPath(installDirectory, relative);
            link.alias = WithExecutableExtension(file.portableCommandAlias.empty() ? FileNamePart(relative) :
                                                 WideFromUtf8(file.portableCommandAlias));
            if (GetFileAttributesW(link.target.c_str()) == INVALID_FILE_ATTRIBUTES)
                return Status::Fail(ERROR_FILE_NOT_FOUND, "the archive does not contain " + file.relativePath);
            links.push_back(link);
        }
    }
    else
    {
        PortableLink link;
        const std::wstring fileName = WithExecutableExtension(FileNamePart(downloadPath));
        link.target = JoinPath(installDirectory, fileName);
        link.alias = WithExecutableExtension(installer.commands.empty() ? fileName : WideFromUtf8(installer.commands[0]));
        status = CopyIntoPackage(downloadPath, link.target);
        if (!status) return status;
        links.push_back(link);
    }

    std::vector<std::wstring> pathEntries;
    std::vector<std::wstring> symlinks;
    std::wstring firstTarget;
    for (const PortableLink &link : links)
    {
        if (!SafeAlias(link.alias))
            return Status::Fail(ERROR_BAD_FORMAT, "the manifest names an invalid command alias: " + Utf8FromWide(link.alias));
        status = LinkAlias(link.target, link.alias);
        if (!status) return status;
        if (firstTarget.empty()) firstTarget = link.target;
        const std::wstring symlink = JoinPath(linksDirectory, link.alias);
        DeleteFileW(symlink.c_str());
        if (CreateSymbolicLinkW(symlink.c_str(), link.target.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        {
            symlinks.push_back(symlink);
            continue;
        }
        TracePortable("symbolic link " + Utf8FromWide(symlink) + " failed with " + DecimalString(GetLastError()) +
                      "; adding the package directory to PATH");
        const std::wstring directory = ParentPath(link.target);
        status = AppendPathVariable(machineScope, directory);
        if (!status) return status;
        if (std::find_if(pathEntries.begin(), pathEntries.end(),
                         [&directory](const std::wstring &entry) { return _wcsicmp(entry.c_str(), directory.c_str()) == 0; }) == pathEntries.end())
            pathEntries.push_back(directory);
    }
    if (!symlinks.empty())
    {
        status = AppendPathVariable(machineScope, linksDirectory);
        if (!status) return status;
    }

    HKEY key = nullptr;
    const std::wstring subKey = std::wstring(ArpUninstallKey) + L"\\" + WideFromUtf8(productCode);
    const LONG result = RegCreateKeyExW(machineScope ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0,
                                        KEY_SET_VALUE | (machineScope ? KEY_WOW64_64KEY : 0), nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        return Status::Fail(static_cast<DWORD>(result), "cannot register the portable package: " + WindowsErrorMessage(static_cast<DWORD>(result)));
    const std::wstring uninstall = L"\"" + JoinPath(KnownFolder(CSIDL_SYSTEM), L"lntget.exe") +
                                   L"\" uninstall --product-code " + WideFromUtf8(productCode);
    status = SetString(key, L"DisplayName", WideFromUtf8(package.name));
    if (status) status = SetString(key, L"DisplayVersion", WideFromUtf8(package.version));
    if (status) status = SetString(key, L"InstallDate", InstallDate());
    if (status) status = SetString(key, L"UninstallString", uninstall);
    if (status) status = SetString(key, L"WinGetInstallerType", L"portable");
    if (status) status = SetString(key, L"WinGetPackageIdentifier", WideFromUtf8(package.id));
    if (status) status = SetString(key, L"WinGetSourceIdentifier", WideFromUtf8(WinGetSourceIdentifier));
    if (status) status = SetString(key, L"InstallLocation", installDirectory);
    if (status) status = SetString(key, L"TargetFullPath", firstTarget);
    if (status && !symlinks.empty()) status = SetString(key, L"SymlinkFullPath", symlinks[0]);
    if (status) status = SetMultiString(key, L"LntGetSymlinks", symlinks);
    if (status) status = SetMultiString(key, L"LntGetPathEntries", pathEntries);
    if (status) status = SetDword(key, L"InstallDirectoryCreated", directoryExisted ? 0 : 1);
    if (status) status = SetDword(key, L"InstallDirectoryAddedToPath", pathEntries.empty() ? 0 : 1);
    RegCloseKey(key);
    if (!status) return status;
    for (const std::wstring &symlink : symlinks)
        std::printf("Command alias:  %s\n", Utf8FromWide(FileNamePart(symlink)).c_str());
    if (!symlinks.empty() || !pathEntries.empty())
        std::printf("PATH was updated; restart the shell to use the new command.\n");
    TracePortable("installed " + package.id + " into " + Utf8FromWide(installDirectory));
    return Status::Ok();
}

Status UninstallPortable(const ArpEntry &entry)
{
    assert(entry.portable);
    for (const std::wstring &symlink : entry.symlinks)
    {
        if (!DeleteFileW(symlink.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        {
            const DWORD error = GetLastError();
            return Status::Fail(error, "cannot remove " + Utf8FromWide(symlink) + ": " + WindowsErrorMessage(error));
        }
    }
    for (const std::wstring &directory : entry.pathEntries)
    {
        const Status status = RemovePathVariable(entry.machine, directory);
        if (!status) return status;
    }
    if (entry.installDirectoryAddedToPath && entry.pathEntries.empty() && !entry.location.empty())
    {
        const Status status = RemovePathVariable(entry.machine, entry.location);
        if (!status) return status;
    }
    if (entry.installDirectoryCreated && !entry.location.empty())
    {
        DeleteDirectoryTree(entry.location);
        if (GetFileAttributesW(entry.location.c_str()) != INVALID_FILE_ATTRIBUTES)
            return Status::Fail(ERROR_SHARING_VIOLATION, "cannot remove " + Utf8FromWide(entry.location) + "; close the application and retry");
    }
    const std::wstring subKey = std::wstring(ArpUninstallKey) + L"\\" + entry.keyName;
    const LONG result = RegDeleteKeyExW(entry.machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, subKey.c_str(),
                                        entry.machine ? (entry.wow64View ? KEY_WOW64_32KEY : KEY_WOW64_64KEY) : 0, 0);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        return Status::Fail(static_cast<DWORD>(result), "cannot remove the portable registration: " + WindowsErrorMessage(static_cast<DWORD>(result)));
    TracePortable("uninstalled " + entry.Identity());
    return Status::Ok();
}

Status RunPortableSelfTests()
{
    if (PortableProductCode("Rufus.Rufus") != "Rufus.Rufus_Microsoft.Winget.Source_8wekyb3d8bbwe")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "portable product code self-test failed");
    if (WithExecutableExtension(L"tool") != L"tool.exe" || WithExecutableExtension(L"Tool.EXE") != L"Tool.EXE" ||
        WithExecutableExtension(L"tool-1.2") != L"tool-1.2.exe")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "portable alias extension self-test failed");
    if (RelativeNestedPath(".\\bin/tool.exe") != L"bin\\tool.exe" || !RelativeNestedPath("..\\tool.exe").empty() ||
        !RelativeNestedPath("C:\\tool.exe").empty() || SafeAlias(L"..\\x.exe") || !SafeAlias(L"tool.exe"))
        return Status::Fail(ERROR_ASSERTION_FAILURE, "portable path validation self-test failed");
    return Status::Ok();
}

} // namespace lntget
