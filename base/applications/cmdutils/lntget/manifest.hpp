/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WinGet YAML manifest reader and installer selection
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace lntget
{

Status ParseInstallerManifest(const std::string &yaml, Manifest &manifest);
Status ParseLocaleManifest(const std::string &yaml, PackageDetails &details);
MachineArchitecture CurrentMachineArchitecture();
std::string ArchitectureName(MachineArchitecture architecture);
Status SelectInstaller(const Manifest &manifest, const SelectionOptions &options, InstallerEntry &installer);
Status RunManifestSelfTests();

} // namespace lntget
