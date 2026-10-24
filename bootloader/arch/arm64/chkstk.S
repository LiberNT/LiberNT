/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 stack probe entry for MSVC-built EFI applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text
    .align 2

    .global __chkstk
__chkstk:
    ret
