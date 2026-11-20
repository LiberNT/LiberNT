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
    .thumb_func
__C_ExecuteHandler:
    push {r4-r11, lr}
    sub sp, sp, #4
    cbz r3, 1f
    ldm r3, {r4-r11}
1:  blx r2
    add sp, sp, #4
    pop {r4-r11, pc}
