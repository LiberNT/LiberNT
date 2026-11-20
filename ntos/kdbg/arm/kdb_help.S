/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KDBG helper to switch to the debugger stack and call a routine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

/* VOID NTAPI KdbpStackSwitchAndCall(PVOID NewStack, VOID (*Function)(VOID)) */
    .global KdbpStackSwitchAndCall
    .thumb_func
KdbpStackSwitchAndCall:
    push {r4, r11, lr}
    sub sp, sp, #4
    mov r4, sp
    bic r0, r0, #7
    mov sp, r0
    blx r1
    mov sp, r4
    add sp, sp, #4
    pop {r4, r11, pc}
