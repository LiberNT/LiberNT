/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process map query declarations referenced by the felix86 sources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>

#define PROCMAP_QUERY              0
#define PROCMAP_QUERY_VMA_READABLE 1

struct procmap_query
{
    uint64_t size;
    uint64_t query_flags;
    uint64_t query_addr;
    uint64_t vma_flags;
};
