/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) exception filter and termination handler calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

/*
 * LONG __C_ExecuteHandler(PVOID Argument, PVOID EstablisherFrame, PVOID Handler, PULONG NonVolatileRegisters)
 * Funclets run with the establishing frame's r4-r11, which the dispatcher captured.
 */
    .global __C_ExecuteHandler
    .seh_proc __C_ExecuteHandler
    .thumb_func
__C_ExecuteHandler:
    push.w {r4-r11, lr}
    .seh_save_regs_w {r4-r11, lr}
    sub sp, sp, #4
    .seh_stackalloc 4
    .seh_endprologue
    cbz r3, 1f
    ldm r3, {r4-r11}
1:  blx r2
    .seh_startepilogue
    add sp, sp, #4
    .seh_stackalloc 4
    pop.w {r4-r11, pc}
    .seh_save_regs_w {r4-r11, pc}
    .seh_endepilogue
    .seh_endproc
