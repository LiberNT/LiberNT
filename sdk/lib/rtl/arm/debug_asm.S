/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM debug service entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global DbgUserBreakPoint
    .thumb_func
DbgUserBreakPoint:
    .global DbgBreakPoint
    .thumb_func
DbgBreakPoint:
    udf     #0xfe
    bx      lr

    .global DbgBreakPointWithStatus
    .thumb_func
DbgBreakPointWithStatus:
    .global RtlpBreakWithStatusInstruction
    .thumb_func
RtlpBreakWithStatusInstruction:
    udf     #0xfe
    bx      lr

    .global DebugService
    .thumb_func
DebugService:
    ldr     r12, [sp]
    udf     #0xfd
    bx      lr

    .global DebugService2
    .thumb_func
DebugService2:
    mov     r3, r1
    mov     r1, r0
    mov     r0, r2
    mov     r2, r3
    mov     r3, #0
    mov     r12, #0
    udf     #0xfd
    bx      lr
