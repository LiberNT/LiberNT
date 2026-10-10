/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     felix86 GDB JIT interface hooks; the NT host registers no blocks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <cstddef>
#include <cstdio>
#include "felix86/common/types.hpp"

struct gdb_line_mapping {
    int line;
    u64 pc;
};

struct felix86_jit_block_t {
    FILE* file;
    u64 host_start;
    u64 host_end;
    u64 guest_address;
    u64 line_count;
    gdb_line_mapping* lines;
};

struct GDBJIT {
    static felix86_jit_block_t* createBlock(size_t line_count) {
        return nullptr;
    }

    void fire(felix86_jit_block_t* block) {
    }
};
