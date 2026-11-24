/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verified installer download and execution
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "installer.hpp"
#include "hash.hpp"
#include "manifest.hpp"
#include "portable.hpp"
#include "util.hpp"

#include "minizip/ioapi.h"
#include "minizip/iowin32.h"
#include "minizip/unzip.h"

#include <algorithm>
#include <cstdio>

namespace lntget
{

namespace
{

constexpr unsigned VerifiedDownloadAttempts = 2;
constexpr unsigned long long MaximumExtractedBytes = 8ull * 1024 * 1024 * 1024;
constexpr unsigned long MaximumArchiveEntries = 200000;

void TraceInstaller(const std::string &message)
{
    const std::string line = "[LNTGET:INSTALLER] " + message + "\n";
    OutputDebugStringA(line.c_str());
}

std::wstring SafePathComponent(const std::string &value)
{
    std::wstring result = WideFromUtf8(value);
    for (wchar_t &character : result)
    {
        if (character == L'<' || character == L'>' || character == L':' || character == L'"' || character == L'/' ||
            character == L'\\' || character == L'|' || character == L'?' || character == L'*')
            character = L'_';
    }
    return result.empty() ? L"package" : result;
}

std::string InstallerArguments(const InstallerEntry &installer, InstallMode mode)
{
    if (mode == InstallMode::Interactive)
        return installer.switches.interactive;
    if (mode == InstallMode::Silent)
    {
        if (!installer.switches.silent.empty())
            return installer.switches.silent;
    }
    else
    {
        if (!installer.switches.silentWithProgress.empty())
            return installer.switches.silentWithProgress;
        if (!installer.switches.silent.empty())
            return installer.switches.silent;
    }
    const bool progress = mode == InstallMode::Default;
    if (AsciiEquals(installer.type, "inno"))
        return progress ? "/SILENT /SUPPRESSMSGBOXES /NORESTART /SP-" : "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-";
    if (AsciiEquals(installer.type, "nullsoft")) return "/S";
    if (AsciiEquals(installer.type, "burn") || AsciiEquals(installer.type, "wix") || AsciiEquals(installer.type, "msi"))
        return progress ? "/passive /norestart" : "/quiet /norestart";
    return std::string();
}

std::string InstallerCommandArguments(const InstallerEntry &installer, InstallMode mode)
{
    std::string arguments = InstallerArguments(installer, mode);
    if (!installer.switches.custom.empty())
    {
        if (!arguments.empty()) arguments.push_back(' ');
        arguments += installer.switches.custom;
    }
    return arguments;
}

void TrackInstalledPackage(const PackageRecord &package, const InstallerEntry &installer, const ArpSnapshot &before,
                           TrackedPackage *tracked)
{
    TrackedPackage record;
    record.id = package.id;
    record.version = package.version;
    record.installerType = installer.type;
    record.arpIdentities = ChangedArpIdentities(before, TakeArpSnapshot());
    if (record.arpIdentities.empty())
    {
        for (const TrackedPackage &previous : LoadTrackedPackages())
        {
            if (AsciiEquals(previous.id, package.id))
                record.arpIdentities = previous.arpIdentities;
        }
    }
    if (record.arpIdentities.empty())
    {
        TraceInstaller("the installer registered no installed application for " + package.id);
        return;
    }
    for (const std::string &identity : record.arpIdentities)
        TraceInstaller("installed " + package.id + " as " + identity);
    const Status status = RecordTrackedPackage(record);
    if (!status)
    {
        std::fprintf(stderr, "lntget: %s\n", status.message.c_str());
        return;
    }
    if (tracked)
        *tracked = record;
}

bool SafeRelativeArchivePath(const std::string &name, std::wstring &relative)
{
    std::wstring path = WideFromUtf8(name);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (path.size() >= 2 && path[0] == L'.' && path[1] == L'\\')
        path.erase(0, 2);
    if (path.empty() || path[0] == L'\\' || path.find(L':') != std::wstring::npos)
        return false;
    std::size_t start = 0;
    for (;;)
    {
        const std::size_t end = path.find(L'\\', start);
        const std::wstring component = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (component == L".." || (component.empty() && end != std::wstring::npos && end + 1 < path.size()))
            return false;
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    relative = path;
    return true;
}

Status EnsureDirectoryTree(const std::wstring &root, const std::wstring &relativeDirectory)
{
    std::wstring current = root;
    std::size_t start = 0;
    while (start < relativeDirectory.size())
    {
        const std::size_t end = relativeDirectory.find(L'\\', start);
        const std::wstring component = relativeDirectory.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!component.empty())
        {
            current = JoinPath(current, component);
            Status status = EnsureDirectory(current);
            if (!status) return status;
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return Status::Ok();
}

void DeleteTree(const std::wstring &directory)
{
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(JoinPath(directory, L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring child = JoinPath(directory, name);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                DeleteTree(child);
            else
            {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(directory.c_str());
}

Status ExtractArchive(const std::wstring &archivePath, const std::wstring &destination)
{
    DeleteTree(destination);
    Status status = EnsureDirectory(destination);
    if (!status) return status;
    zlib_filefunc64_def functions{};
    fill_win32_filefunc64W(&functions);
    unzFile archive = unzOpen2_64(archivePath.c_str(), &functions);
    if (!archive)
        return Status::Fail(ERROR_BAD_FORMAT, "the downloaded archive is not a valid zip file");

    unsigned long long total = 0;
    unsigned long entries = 0;
    int result = unzGoToFirstFile(archive);
    while (result == UNZ_OK)
    {
        char name[1024]{};
        unz_file_info64 information{};
        if (unzGetCurrentFileInfo64(archive, &information, name, sizeof(name) - 1, nullptr, 0, nullptr, 0) != UNZ_OK ||
            ++entries > MaximumArchiveEntries)
        {
            unzClose(archive);
            return Status::Fail(ERROR_BAD_FORMAT, "the downloaded archive has an invalid entry");
        }
        std::wstring relative;
        if (!SafeRelativeArchivePath(name, relative))
        {
            unzClose(archive);
            return Status::Fail(ERROR_BAD_FORMAT, std::string("the downloaded archive contains an unsafe path: ") + name);
        }
        total += information.uncompressed_size;
        if (total > MaximumExtractedBytes)
        {
            unzClose(archive);
            return Status::Fail(ERROR_FILE_TOO_LARGE, "the downloaded archive expands beyond 8 GiB");
        }
        if (relative[relative.size() - 1] == L'\\')
        {
            status = EnsureDirectoryTree(destination, relative);
        }
        else
        {
            const std::size_t slash = relative.rfind(L'\\');
            status = slash == std::wstring::npos ? Status::Ok() : EnsureDirectoryTree(destination, relative.substr(0, slash));
            if (status)
            {
                if (unzOpenCurrentFile(archive) != UNZ_OK)
                    status = Status::Fail(ERROR_BAD_COMPRESSION_BUFFER, std::string("cannot decompress ") + name);
                else
                {
                    std::vector<uint8_t> bytes;
                    bytes.reserve(static_cast<std::size_t>(information.uncompressed_size));
                    uint8_t buffer[64 * 1024];
                    int read = 0;
                    while ((read = unzReadCurrentFile(archive, buffer, sizeof(buffer))) > 0)
                        bytes.insert(bytes.end(), buffer, buffer + read);
                    const int closed = unzCloseCurrentFile(archive);
                    if (read < 0 || closed != UNZ_OK || bytes.size() != information.uncompressed_size)
                        status = Status::Fail(ERROR_CRC, std::string("archive entry failed decompression or CRC validation: ") + name);
                    else
                        status = WriteFileBytes(JoinPath(destination, relative), bytes);
                }
            }
        }
        if (!status)
        {
            unzClose(archive);
            return status;
        }
        result = unzGoToNextFile(archive);
    }
    unzClose(archive);
    if (result != UNZ_END_OF_LIST_OF_FILE)
        return Status::Fail(ERROR_BAD_FORMAT, "the downloaded archive directory is corrupt");
    TraceInstaller("extracted " + DecimalString(entries) + " archive entries, " + DecimalString(total) + " bytes");
    return Status::Ok();
}

bool SamePath(const std::wstring &left, const std::wstring &right)
{
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

} // namespace

Status ExtractArchiveInto(const std::wstring &archivePath, const std::wstring &destination)
{
    return ExtractArchive(archivePath, destination);
}

void DeleteDirectoryTree(const std::wstring &directory)
{
    DeleteTree(directory);
}

std::wstring InstallerService::DefaultPackageDirectory(const PackageRecord &package) const
{
    return JoinPath(JoinPath(JoinPath(TemporaryDirectory(), L"lntget"), SafePathComponent(package.id)), SafePathComponent(package.version));
}

Status InstallerService::Download(const PackageRecord &package, const InstallerEntry &installer, const std::wstring *directory,
                                  std::wstring &path, DownloadProgressCallback *progress, InstallerStageCallback *stage)
{
    const std::wstring targetDirectory = directory ? *directory : DefaultPackageDirectory(package);
    Status status = EnsureDirectory(targetDirectory);
    if (!status)
        return status;
    path = JoinPath(targetDirectory, FileNameFromUrl(installer.url));
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && VerifyFileSha256(path, installer.sha256))
    {
        std::printf("Using the verified installer already downloaded for %s %s.\n", package.name.c_str(), package.version.c_str());
        if (stage) stage->Stage(InstallerDownloadStage::Verified);
        return Status::Ok();
    }
    for (unsigned attempt = 0; attempt < VerifiedDownloadAttempts; ++attempt)
    {
        std::printf("Downloading %s %s [%s]\n", package.name.c_str(), package.version.c_str(), installer.architecture.c_str());
        if (stage) stage->Stage(InstallerDownloadStage::Downloading);
        status = http_.Download(WideFromUtf8(installer.url), path, std::wstring(), ~0ull, progress);
        if (!status)
            return status;
        std::printf("Verifying installer SHA-256...\n");
        if (stage) stage->Stage(InstallerDownloadStage::Verifying);
        status = VerifyFileSha256(path, installer.sha256);
        if (status)
            break;
        DeleteFileW(path.c_str());
        TraceInstaller("verification attempt " + DecimalString(attempt + 1) + "/" + DecimalString(VerifiedDownloadAttempts) + " failed: " + status.message);
        if (status.code != ERROR_CRC || attempt + 1 == VerifiedDownloadAttempts)
            return status;
        TraceInstaller("discarded untrusted download and retrying from the vendor URL");
    }
    std::printf("Installer hash verified.\n");
    if (stage) stage->Stage(InstallerDownloadStage::Verified);
    return Status::Ok();
}

void InstallerService::RemoveInstaller(const PackageRecord &package, const std::wstring &path) const
{
    const std::wstring versionDirectory = DefaultPackageDirectory(package);
    if (!SamePath(ParentPath(path), versionDirectory))
        return;
    DeleteTree(path + L".extracted");
    if (!DeleteFileW(path.c_str()))
        TraceInstaller("cannot remove the installer " + Utf8FromWide(path) + ": " + WindowsErrorMessage(GetLastError()));
    if (RemoveDirectoryW(versionDirectory.c_str()))
        RemoveDirectoryW(ParentPath(versionDirectory).c_str());
}

Status InstallerService::Execute(const std::wstring &commandLine, const std::wstring &workingDirectory, DWORD &exitCode) const
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process))
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot launch installer: " + WindowsErrorMessage(error));
    }
    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    if (waitResult != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, &exitCode))
    {
        const DWORD error = GetLastError();
        CloseHandle(process.hProcess);
        return Status::Fail(error, "cannot wait for installer: " + WindowsErrorMessage(error));
    }
    CloseHandle(process.hProcess);
    return Status::Ok();
}

Status InstallerService::Install(const PackageRecord &package, const InstallerEntry &installer, const std::wstring &path,
                                 InstallMode mode, DWORD &exitCode, TrackedPackage *tracked, InstallLockWait *wait)
{
    const std::wstring ownedPath(path);
    HANDLE lockedInstaller = CreateFileW(ownedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lockedInstaller == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return Status::Fail(error, "cannot lock installer for execution: " + WindowsErrorMessage(error));
    }
    Status status = VerifyFileSha256(ownedPath, installer.sha256);
    if (!status)
    {
        CloseHandle(lockedInstaller);
        return Status::Fail(status.code, "installer changed after download verification: " + status.message);
    }
    InstallLock lock;
    status = lock.Acquire(wait);
    if (!status)
    {
        CloseHandle(lockedInstaller);
        return status;
    }

    if (AsciiEquals(EffectiveInstallerType(installer), "portable"))
    {
        /* FIXME: portable packages always install for the current user; --scope machine is not honored */
        std::printf("Installing portable package...\n");
        const ArpSnapshot before = TakeArpSnapshot();
        status = InstallPortable(package, installer, path, false);
        CloseHandle(lockedInstaller);
        if (!status)
            return status;
        TrackInstalledPackage(package, installer, before, tracked);
        RemoveInstaller(package, path);
        return Status::Ok();
    }

    InstallerEntry effective(installer);
    std::wstring installerPath(path);
    if (AsciiEquals(installer.type, "zip"))
    {
        std::wstring relative;
        if (installer.nestedFiles.size() != 1 || !SafeRelativeArchivePath(installer.nestedFiles[0].relativePath, relative))
        {
            CloseHandle(lockedInstaller);
            return Status::Fail(ERROR_BAD_FORMAT, "the manifest names an invalid nested installer");
        }
        const std::wstring extracted = path + L".extracted";
        std::printf("Extracting archive...\n");
        status = ExtractArchive(path, extracted);
        if (!status)
        {
            CloseHandle(lockedInstaller);
            return status;
        }
        installerPath = JoinPath(extracted, relative);
        if (GetFileAttributesW(installerPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            CloseHandle(lockedInstaller);
            return Status::Fail(ERROR_FILE_NOT_FOUND, "the archive does not contain " + installer.nestedFiles[0].relativePath);
        }
        effective.type = installer.nestedType;
    }

    std::wstring commandLine;
    const std::string arguments = InstallerCommandArguments(effective, mode);

    if (AsciiEquals(effective.type, "wix") || AsciiEquals(effective.type, "msi"))
    {
        commandLine = L"msiexec.exe /i ";
        commandLine += QuoteCommandArgument(installerPath);
    }
    else
    {
        commandLine = QuoteCommandArgument(installerPath);
    }
    if (!arguments.empty())
    {
        commandLine.push_back(L' ');
        commandLine += WideFromUtf8(arguments);
    }

    std::printf("Starting installer (%s, %s)...\n", effective.type.c_str(), installer.architecture.c_str());
    const ArpSnapshot before = TakeArpSnapshot();
    status = Execute(commandLine, ParentPath(installerPath), exitCode);
    CloseHandle(lockedInstaller);
    if (!status)
        return status;
    const bool manifestSuccess = std::find(installer.successCodes.begin(), installer.successCodes.end(), exitCode) != installer.successCodes.end();
    if (exitCode != ERROR_SUCCESS && exitCode != ERROR_SUCCESS_REBOOT_INITIATED && exitCode != ERROR_SUCCESS_REBOOT_REQUIRED && !manifestSuccess)
    {
        const auto expected = installer.expectedReturnCodes.find(exitCode);
        const std::string response = expected == installer.expectedReturnCodes.end() ? std::string() : " (" + expected->second + ")";
        return Status::Fail(exitCode, "installer returned exit code " + DecimalString(exitCode) + response);
    }
    TrackInstalledPackage(package, effective, before, tracked);
    RemoveInstaller(package, path);
    return Status::Ok();
}

Status RunInstallerSelfTests()
{
    std::wstring relative;
    if (!SafeRelativeArchivePath(".\\HxDSetup.exe", relative) || relative != L"HxDSetup.exe" ||
        !SafeRelativeArchivePath("bin/tool.exe", relative) || relative != L"bin\\tool.exe" ||
        !SafeRelativeArchivePath("docs/", relative) || relative != L"docs\\" ||
        SafeRelativeArchivePath("../evil.exe", relative) || SafeRelativeArchivePath("a\\..\\..\\b", relative) ||
        SafeRelativeArchivePath("C:\\Windows\\x.dll", relative) || SafeRelativeArchivePath("/etc/x", relative) ||
        SafeRelativeArchivePath("a//b", relative))
        return Status::Fail(ERROR_ASSERTION_FAILURE, "archive path validation self-test failed");
    InstallerEntry installer;
    installer.type = "exe";
    installer.switches.silent = "/quiet";
    installer.switches.silentWithProgress = "/passive";
    if (InstallerArguments(installer, InstallMode::Silent) != "/quiet" ||
        InstallerArguments(installer, InstallMode::Default) != "/passive")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "silent installer mode self-test failed");
    installer.switches.interactive = "/interactive";
    if (InstallerArguments(installer, InstallMode::Interactive) != "/interactive")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "interactive installer mode self-test failed");
    InstallerEntry msi;
    msi.type = "wix";
    msi.switches.custom = "ADD_CMAKE_TO_PATH=System";
    if (InstallerCommandArguments(msi, InstallMode::Silent) != "/quiet /norestart ADD_CMAKE_TO_PATH=System" ||
        InstallerCommandArguments(msi, InstallMode::Default) != "/passive /norestart ADD_CMAKE_TO_PATH=System" ||
        InstallerCommandArguments(msi, InstallMode::Interactive) != "ADD_CMAKE_TO_PATH=System")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "MSI custom switch self-test failed");
    InstallerEntry typed;
    typed.type = "inno";
    if (InstallerArguments(typed, InstallMode::Silent) != "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-" ||
        InstallerArguments(typed, InstallMode::Default) != "/SILENT /SUPPRESSMSGBOXES /NORESTART /SP-")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "Inno default switch self-test failed");
    typed.type = "burn";
    if (InstallerArguments(typed, InstallMode::Silent) != "/quiet /norestart" ||
        InstallerArguments(typed, InstallMode::Default) != "/passive /norestart")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "Burn default switch self-test failed");
    typed.type = "nullsoft";
    if (InstallerArguments(typed, InstallMode::Silent) != "/S" || InstallerArguments(typed, InstallMode::Default) != "/S")
        return Status::Fail(ERROR_ASSERTION_FAILURE, "Nullsoft default switch self-test failed");
    return Status::Ok();
}

} // namespace lntget
