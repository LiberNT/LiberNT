/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     felix86 emulator entry points provided by the NT host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "felix86/common/frame.hpp"
#include "felix86/common/state.hpp"

struct Emulator {
    static void* CompileNext(ThreadState* state);
};
