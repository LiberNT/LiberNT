/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable package installation in the WinGet layout
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "arp.hpp"

namespace lntget
{

Status InstallPortable(const PackageRecord &package, const InstallerEntry &installer, const std::wstring &downloadPath,
                       bool machineScope);
Status UninstallPortable(const ArpEntry &entry);
std::string PortableProductCode(const std::string &packageId);
Status RunPortableSelfTests();

} // namespace lntget
