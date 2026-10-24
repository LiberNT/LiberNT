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
#include "util.hpp"

#include <algorithm>
#include <cstdio>

namespace lntget
{

namespace
{

constexpr unsigned VerifiedDownloadAttempts = 2;

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
    if (AsciiEquals(installer.type, "inno")) return "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-";
    if (AsciiEquals(installer.type, "nullsoft")) return "/S";
    if (AsciiEquals(installer.type, "burn")) return "/quiet /norestart";
    return std::string();
}

} // namespace

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

Status InstallerService::Install(const InstallerEntry &installer, const std::wstring &path, InstallMode mode, DWORD &exitCode)
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

    std::wstring commandLine;
    std::string arguments = InstallerArguments(installer, mode);
    if (!installer.switches.custom.empty())
    {
        if (!arguments.empty()) arguments.push_back(' ');
        arguments += installer.switches.custom;
    }

    if (AsciiEquals(installer.type, "wix") || AsciiEquals(installer.type, "msi"))
    {
        commandLine = L"msiexec.exe /i ";
        commandLine += QuoteCommandArgument(path);
        if (mode != InstallMode::Interactive && arguments.empty())
            arguments = "/quiet /norestart";
    }
    else
    {
        commandLine = QuoteCommandArgument(path);
    }
    if (!arguments.empty())
    {
        commandLine.push_back(L' ');
        commandLine += WideFromUtf8(arguments);
    }

    std::printf("Starting installer (%s, %s)...\n", installer.type.c_str(), installer.architecture.c_str());
    status = Execute(commandLine, ParentPath(path), exitCode);
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
    return Status::Ok();
}

Status RunInstallerSelfTests()
{
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
    return Status::Ok();
}

} // namespace lntget
