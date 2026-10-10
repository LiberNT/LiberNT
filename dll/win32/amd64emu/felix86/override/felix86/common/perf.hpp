/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     felix86 perf map hooks; the NT host writes no perf map
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <string>
#include "felix86/common/types.hpp"

struct Perf {
    void addToFile(u64 address, u64 size, const std::string& symbol) {
    }
};
