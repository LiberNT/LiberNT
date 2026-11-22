/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 UEFI stack switch and kernel handoff
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global _changestack
    .thumb_func
_changestack:
    push    {r4, lr}
    mov     r4, sp
    mov     r0, sp
    movw    r1, :lower16:BasicStack
    movt    r1, :upper16:BasicStack
    ldr     r1, [r1]
    bic     r1, r1, #7
    mov     sp, r1
    bl      ExecuteLoaderCleanly
    mov     sp, r4
    pop     {r4, pc}

    .global _exituefi
    .thumb_func
_exituefi:
    push    {r4, lr}
    mov     r4, sp
    movw    r1, :lower16:UefiServiceStack
    movt    r1, :upper16:UefiServiceStack
    ldr     r1, [r1]
    cbz     r1, 1f
    bic     r1, r1, #7
    mov     sp, r1
1:
    bl      UefiExitBootServices
    mov     sp, r4
    pop     {r4, pc}

    .balign 4
    .global ArmEnterKernel
    .thumb_func
ArmEnterKernel:
    cpsid   if
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    mov     r7, r3
    ldr     r8, [sp]
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #1
    mcr     p15, 0, r0, c1, c0, 0
    isb
    mov     r1, #0
    mcr     p15, 0, r1, c7, c5, 0
    mcr     p15, 0, r1, c7, c5, 6
    dsb     sy
    isb
    orr     r8, r8, #1
    bx      r8

    .balign 4
    .global ArmEnterKernelStart
ArmEnterKernelStart:
    .thumb_func
ArmEnterKernelStub:
    movw    r0, #0x00FF
    movt    r0, #0x4444
    mcr     p15, 0, r0, c10, c2, 0
    mcr     p15, 0, r0, c10, c2, 1
    movw    r0, #0x3500
    movt    r0, #0x8080
    mcr     p15, 0, r0, c2, c0, 2
    mov     r1, #0
    mcrr    p15, 0, r7, r1, c2
    mcrr    p15, 1, r1, r1, c2
    mcr     p15, 0, r1, c8, c7, 0
    mcr     p15, 0, r1, c7, c5, 0
    mcr     p15, 0, r1, c7, c5, 6
    dsb     sy
    isb
    mrc     p15, 0, r0, c1, c0, 0
    movw    r1, #0x1805
    orr     r0, r0, r1
    bic     r0, r0, #0x2
    bic     r0, r0, #0x30000000
    mcr     p15, 0, r0, c1, c0, 0
    isb
    mov     sp, r6
    orr     r4, r4, #1
    mov     r0, r5
    bx      r4
    .global ArmEnterKernelEnd
ArmEnterKernelEnd:
