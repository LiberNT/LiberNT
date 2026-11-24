/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verified installer download and execution
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "arp.hpp"
#include "http.hpp"

namespace lntget
{

enum class InstallerDownloadStage
{
    Downloading,
    Verifying,
    Verified,
};

class InstallerStageCallback
{
public:
    virtual void Stage(InstallerDownloadStage stage) = 0;

protected:
    ~InstallerStageCallback() {}
};

class InstallerService
{
public:
    Status Download(const PackageRecord &package, const InstallerEntry &installer, const std::wstring *directory,
                    std::wstring &path, DownloadProgressCallback *progress = nullptr, InstallerStageCallback *stage = nullptr);
    Status Install(const PackageRecord &package, const InstallerEntry &installer, const std::wstring &path, InstallMode mode,
                   DWORD &exitCode, TrackedPackage *tracked = nullptr, InstallLockWait *wait = nullptr);

private:
    std::wstring DefaultPackageDirectory(const PackageRecord &package) const;
    void RemoveInstaller(const PackageRecord &package, const std::wstring &path) const;
    Status Execute(const std::wstring &commandLine, const std::wstring &workingDirectory, DWORD &exitCode) const;

    HttpClient http_;
};

Status ExtractArchiveInto(const std::wstring &archivePath, const std::wstring &destination);
void DeleteDirectoryTree(const std::wstring &directory);
Status RunInstallerSelfTests();

} // namespace lntget
