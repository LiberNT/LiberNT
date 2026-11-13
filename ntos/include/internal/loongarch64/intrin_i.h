/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 kernel intrinsics
 */

#pragma once

/* LoongArch64 instruction intrinsics come from the CRT architecture header
 * (sdk/include/vcruntime/mingw32/intrin_loongarch64.h). The kernel-private
 * interrupt and barrier primitives that must not use the bare compiler
 * intrinsics live in internal/loongarch64/ke.h; see the note there about
 * _disable(). */
#include <intrin.h>
