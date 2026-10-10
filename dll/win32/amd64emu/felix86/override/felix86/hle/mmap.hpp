/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     felix86 guest mapping queries; the NT host tracks no mappings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <optional>

struct Mapper {
    std::optional<int> get_region_protections(void* address) {
        return std::nullopt;
    }
};
