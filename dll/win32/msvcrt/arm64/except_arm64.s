/*
 * PROJECT:     LiberNT msvcrt.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 asm code for except_arm64.c
 * COPYRIGHT:   Copyright 2011-2017 Wine team
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .align 4
    .seh_proc call_exc_handler
    .global call_exc_handler
call_exc_handler:
    stp x29, x30, [sp, #-96]!
    .seh_save_fplr_x 96
    stp x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    stp x21, x22, [sp, #32]
    .seh_save_regp x21, 32
    stp x23, x24, [sp, #48]
    .seh_save_regp x23, 48
    stp x25, x26, [sp, #64]
    .seh_save_regp x25, 64
    stp x27, x28, [sp, #80]
    .seh_save_regp x27, 80
    str x1, [sp, #-16]!
    .seh_stackalloc 16
    .seh_endprologue
    ldp x19, x20, [x3, #0]
    ldp x21, x22, [x3, #16]
    ldp x23, x24, [x3, #32]
    ldp x25, x26, [x3, #48]
    ldp x27, x28, [x3, #64]
    ldr x29, [x3, #80]
    blr x0
    add sp, sp, 16
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #96
    ret
    .seh_endproc

    .align 4
    .seh_proc __C_ExecuteExceptionFilter
    .global __C_ExecuteExceptionFilter
__C_ExecuteExceptionFilter:
    stp x29, x30, [sp, #-96]!
    .seh_save_fplr_x 96
    stp x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    stp x21, x22, [sp, #32]
    .seh_save_regp x21, 32
    stp x23, x24, [sp, #48]
    .seh_save_regp x23, 48
    stp x25, x26, [sp, #64]
    .seh_save_regp x25, 64
    stp x27, x28, [sp, #80]
    .seh_save_regp x27, 80
    .seh_endprologue
    ldp x19, x20, [x3, #0]
    ldp x21, x22, [x3, #16]
    ldp x23, x24, [x3, #32]
    ldp x25, x26, [x3, #48]
    ldp x27, x28, [x3, #64]
    ldr x1, [x3, #80]
    blr x2
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #96
    ret
    .seh_endproc
