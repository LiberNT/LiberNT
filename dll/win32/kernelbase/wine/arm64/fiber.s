/*
 * PROJECT:     LiberNT kernelbase.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 fiber switch for MSVC builds
 * COPYRIGHT:   Copyright 2019 Alexandre Julliard
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .align 4
    .seh_proc switch_fiber
    .global switch_fiber
switch_fiber:
    stp x19, x20, [x0, #0xa0]
    stp x21, x22, [x0, #0xb0]
    stp x23, x24, [x0, #0xc0]
    stp x25, x26, [x0, #0xd0]
    stp x27, x28, [x0, #0xe0]
    str x29, [x0, #0xf0]
    mov x2, sp
    str x2, [x0, #0x100]
    str x30, [x0, #0x108]
    ldp x19, x20, [x1, #0xa0]
    ldp x21, x22, [x1, #0xb0]
    ldp x23, x24, [x1, #0xc0]
    ldp x25, x26, [x1, #0xd0]
    ldp x27, x28, [x1, #0xe0]
    ldr x29, [x1, #0xf0]
    ldr x2, [x1, #0x100]
    ldr x30, [x1, #0x108]
    mov sp, x2
    ret
    .seh_endproc
