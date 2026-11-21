/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Package dependency installation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>

#include "dependencies.hpp"
#include "arp.hpp"
#include "installer.hpp"
#include "manifest.hpp"
#include "util.hpp"

#include <cstdio>

namespace lntget
{

namespace
{

constexpr unsigned MaximumDependencyDepth = 8;

bool DependencySatisfied(const std::vector<InstalledPackage> &installed, const PackageDependency &dependency)
{
    for (const InstalledPackage &package : installed)
    {
        if (!package.correlated || !AsciiEquals(package.packageId, dependency.id))
            continue;
        if (dependency.minimumVersion.empty() ||
            CompareVersions(Utf8FromWide(package.entry.version), dependency.minimumVersion) >= 0)
            return true;
    }
    return false;
}

} // namespace

Status InstallPackageDependencies(SourceManager &source, const InstallerEntry &installer, const SelectionOptions &selection,
                                  unsigned depth)
{
    if (installer.packageDependencies.empty())
        return Status::Ok();
    if (depth >= MaximumDependencyDepth)
        return Status::Fail(ERROR_CIRCULAR_DEPENDENCY, "the package dependency chain is deeper than 8 packages");

    std::vector<InstalledPackage> installed;
    Status status = CollectInstalledPackages(&source, installed);
    if (!status)
        return status;
    for (const PackageDependency &dependency : installer.packageDependencies)
    {
        if (DependencySatisfied(installed, dependency))
        {
            std::printf("Dependency %s is already installed.\n", dependency.id.c_str());
            continue;
        }
        std::printf("Installing dependency %s%s%s\n", dependency.id.c_str(),
                    dependency.minimumVersion.empty() ? "" : " >= ", dependency.minimumVersion.c_str());
        PackageQuery query;
        query.text = dependency.id;
        query.field = SearchField::Id;
        query.exact = true;
        PackageRecord package;
        status = source.Resolve(query, package);
        if (!status)
            return Status::Fail(status.code, "dependency " + dependency.id + ": " + status.message);
        if (!dependency.minimumVersion.empty() && CompareVersions(package.version, dependency.minimumVersion) < 0)
            return Status::Fail(ERROR_NOT_FOUND, "dependency " + dependency.id + " " + dependency.minimumVersion +
                                " is newer than the catalog version " + package.version);
        std::string yaml;
        Manifest manifest;
        status = source.FetchInstallerManifest(package, yaml);
        if (status) status = ParseInstallerManifest(yaml, manifest);
        if (status && (!AsciiEquals(package.id, manifest.id) || package.version != manifest.version))
            status = Status::Fail(ERROR_BAD_FORMAT, "manifest identity does not match the source index");
        if (status && manifest.hasAgreements && !selection.acceptPackageAgreements)
            status = Status::Fail(ERROR_CANCELLED, "package agreements must be accepted with --accept-package-agreements");
        SelectionOptions dependencySelection;
        dependencySelection.mode = selection.mode == InstallMode::Interactive ? InstallMode::Default : selection.mode;
        dependencySelection.acceptPackageAgreements = selection.acceptPackageAgreements;
        InstallerEntry dependencyInstaller;
        if (status) status = SelectInstaller(manifest, dependencySelection, dependencyInstaller);
        if (status) status = InstallPackageDependencies(source, dependencyInstaller, selection, depth + 1);
        if (!status)
            return Status::Fail(status.code, "dependency " + dependency.id + ": " + status.message);

        InstallerService service;
        std::wstring path;
        status = service.Download(package, dependencyInstaller, nullptr, path);
        DWORD exitCode = 0;
        if (status) status = service.Install(package, dependencyInstaller, path, dependencySelection.mode, exitCode);
        if (!status)
            return Status::Fail(status.code, "dependency " + dependency.id + ": " + status.message);
        std::printf("Installed dependency %s %s.\n", package.id.c_str(), package.version.c_str());
    }
    return Status::Ok();
}

} // namespace lntget
