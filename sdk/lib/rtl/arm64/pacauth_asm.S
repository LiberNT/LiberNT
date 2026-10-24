/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 AUTIB1716 helper for compilers without inline assembly
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.h>

    .text
    .align 4

    .global RtlpArm64AutiB1716
    .seh_proc RtlpArm64AutiB1716
RtlpArm64AutiB1716:
    .seh_endprologue
    mov     x17, x0
    mov     x16, x1
    hint    #0xe
    mov     x0, x17
    ret
    .seh_endproc
