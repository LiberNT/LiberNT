/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Argument widths of the native system services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define EMU_ARGUMENT_BITS   3
#define EMU_ARGUMENT_MASK   7
#define EMU_ARGUMENT_NATIVE 0
#define EMU_ARGUMENT_INT32  1
#define EMU_ARGUMENT_INT16  2
#define EMU_ARGUMENT_INT8   3
#define EMU_ARGUMENT_UINT16 4
#define EMU_ARGUMENT_UINT8  5

#ifdef __cplusplus
extern "C" {
#endif

extern const ULONG64 EmuNtSignatures[];

#ifdef __cplusplus
}
#endif
