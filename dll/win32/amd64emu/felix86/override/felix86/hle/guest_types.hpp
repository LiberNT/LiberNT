/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     felix86 guest structure used by the recompiler core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "felix86/common/types.hpp"

struct x86_user_desc {
    u32 entry_number = 0;
    u32 base_addr = 0;
    u32 limit = 0;
    u32 seg_32bit : 1 = 0;
    u32 contents : 2 = 0;
    u32 read_exec_only : 1 = 0;
    u32 limit_in_pages : 1 = 0;
    u32 seg_not_present : 1 = 0;
    u32 usable : 1 = 0;
};
