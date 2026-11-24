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

#include <cassert>
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

SelectionOptions DependencySelection(const SelectionOptions &selection)
{
    SelectionOptions dependencySelection;
    dependencySelection.mode = selection.mode == InstallMode::Interactive ? InstallMode::Default : selection.mode;
    dependencySelection.acceptPackageAgreements = selection.acceptPackageAgreements;
    return dependencySelection;
}

DependencyStep *PlannedDependency(std::vector<DependencyStep> &plan, const std::string &id)
{
    for (DependencyStep &step : plan)
    {
        if (AsciiEquals(step.package.id, id))
            return &step;
    }
    return nullptr;
}

Status CheckCatalogVersion(const PackageDependency &dependency, const std::string &version)
{
    if (!dependency.minimumVersion.empty() && CompareVersions(version, dependency.minimumVersion) < 0)
        return Status::Fail(ERROR_NOT_FOUND, "dependency " + dependency.id + " " + dependency.minimumVersion +
                            " is newer than the catalog version " + version);
    return Status::Ok();
}

Status PlanDependencies(SourceManager &source, const InstallerEntry &installer, const SelectionOptions &selection,
                        const std::vector<InstalledPackage> &installed, unsigned depth, std::vector<DependencyStep> &plan)
{
    if (installer.packageDependencies.empty())
        return Status::Ok();
    if (depth >= MaximumDependencyDepth)
        return Status::Fail(ERROR_CIRCULAR_DEPENDENCY, "the package dependency chain is deeper than 8 packages");

    for (const PackageDependency &dependency : installer.packageDependencies)
    {
        if (DependencySatisfied(installed, dependency))
            continue;
        if (DependencyStep *planned = PlannedDependency(plan, dependency.id))
        {
            const Status status = CheckCatalogVersion(dependency, planned->package.version);
            if (!status)
                return status;
            if (!dependency.minimumVersion.empty() &&
                (planned->minimumVersion.empty() || CompareVersions(dependency.minimumVersion, planned->minimumVersion) > 0))
                planned->minimumVersion = dependency.minimumVersion;
            continue;
        }
        PackageQuery query;
        query.text = dependency.id;
        query.field = SearchField::Id;
        query.exact = true;
        DependencyStep step;
        Status status = source.Resolve(query, step.package);
        if (!status)
            return Status::Fail(status.code, "dependency " + dependency.id + ": " + status.message);
        status = CheckCatalogVersion(dependency, step.package.version);
        if (!status)
            return status;
        step.minimumVersion = dependency.minimumVersion;
        std::string yaml;
        status = source.FetchInstallerManifest(step.package, yaml);
        if (status) status = ParseInstallerManifest(yaml, step.manifest);
        if (status && (!AsciiEquals(step.package.id, step.manifest.id) || step.package.version != step.manifest.version))
            status = Status::Fail(ERROR_BAD_FORMAT, "manifest identity does not match the source index");
        if (status) status = SelectInstaller(step.manifest, DependencySelection(selection), step.installer);
        if (status) status = PlanDependencies(source, step.installer, selection, installed, depth + 1, plan);
        if (!status)
            return Status::Fail(status.code, "dependency " + dependency.id + ": " + status.message);
        assert(!PlannedDependency(plan, step.package.id));
        plan.push_back(step);
    }
    return Status::Ok();
}

} // namespace

Status PlanPackageDependencies(SourceManager &source, const InstallerEntry &installer, const SelectionOptions &selection,
                               std::vector<DependencyStep> &plan)
{
    plan.clear();
    if (installer.packageDependencies.empty())
        return Status::Ok();
    std::vector<InstalledPackage> installed;
    Status status = CollectInstalledPackages(&source, installed);
    if (status)
        status = PlanDependencies(source, installer, selection, installed, 0, plan);
    if (!status)
        plan.clear();
    return status;
}

Status InstallDependencyPlan(SourceManager &source, const std::vector<DependencyStep> &plan, const SelectionOptions &selection,
                             InstallLockWait *wait)
{
    for (const DependencyStep &step : plan)
    {
        if (step.manifest.hasAgreements && !selection.acceptPackageAgreements)
            return Status::Fail(ERROR_CANCELLED, "dependency " + step.package.id +
                                ": package agreements must be accepted with --accept-package-agreements");
    }
    for (const DependencyStep &step : plan)
    {
        std::vector<InstalledPackage> installed;
        Status status = CollectInstalledPackages(&source, installed);
        if (!status)
            return status;
        PackageDependency dependency;
        dependency.id = step.package.id;
        dependency.minimumVersion = step.minimumVersion;
        if (DependencySatisfied(installed, dependency))
        {
            std::printf("Dependency %s is already installed.\n", step.package.id.c_str());
            continue;
        }
        std::printf("Installing dependency %s %s\n", step.package.id.c_str(), step.package.version.c_str());
        InstallerService service;
        std::wstring path;
        status = service.Download(step.package, step.installer, nullptr, path);
        DWORD exitCode = 0;
        if (status) status = service.Install(step.package, step.installer, path, DependencySelection(selection).mode, exitCode, nullptr, wait);
        if (!status)
            return Status::Fail(status.code, "dependency " + step.package.id + ": " + status.message);
        std::printf("Installed dependency %s %s.\n", step.package.id.c_str(), step.package.version.c_str());
    }
    return Status::Ok();
}

Status InstallPackageDependencies(SourceManager &source, const InstallerEntry &installer, const SelectionOptions &selection)
{
    std::vector<DependencyStep> plan;
    Status status = PlanPackageDependencies(source, installer, selection, plan);
    if (status)
        status = InstallDependencyPlan(source, plan, selection);
    return status;
}

} // namespace lntget
