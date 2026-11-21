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

Status InstallPackageDependencies(SourceManager &source, const InstallerEntry &installer, const SelectionOptions &selection,
                                  unsigned depth = 0);

} // namespace lntget
