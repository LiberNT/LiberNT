/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARMv8.5 RNDR read for MSVC builds
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 4
    .global KdNetArchReadRndr
KdNetArchReadRndr:
    mov     x1, x0
    mrs     x2, s3_3_c2_c4_0
    cset    w0, ne
    str     x2, [x1]
    ret
