/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Package dependency installation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "source.hpp"

namespace lntget
{

struct DependencyStep
{
    PackageRecord package;
    std::string minimumVersion;
    Manifest manifest;
    InstallerEntry installer;
};

Status PlanPackageDependencies(SourceManager &source, const InstallerEntry &installer, const SelectionOptions &selection,
                               std::vector<DependencyStep> &plan);
Status InstallDependencyPlan(SourceManager &source, const std::vector<DependencyStep> &plan, const SelectionOptions &selection);
Status InstallPackageDependencies(SourceManager &source, const InstallerEntry &installer, const SelectionOptions &selection);

} // namespace lntget
