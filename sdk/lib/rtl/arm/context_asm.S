/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) context capture and restore
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* CONTEXT offsets (winnt.h, _M_ARM). */
#define CxContextFlags 0x00
#define CxR0  0x04
#define CxR1  0x08
#define CxR2  0x0C
#define CxSp  0x38
#define CxLr  0x3C
#define CxPc  0x40
#define CxCpsr 0x44
#define CxFpscr 0x48
#define CxD   0x50
#define CONTEXT_FULL_ARM 0x00200007

    .syntax unified
    .thumb
    .text
    .align 2

/* VOID RtlCaptureContext(PCONTEXT ContextRecord): the state at the return address. */
    .global RtlCaptureContext
    .thumb_func
RtlCaptureContext:
    str r1, [r0, #CxR1]
    str r0, [r0, #CxR0]
    add r1, r0, #CxR2
    stm r1, {r2-r12}
    mov r1, sp
    str r1, [r0, #CxSp]
    str lr, [r0, #CxLr]
    bic r1, lr, #1
    str r1, [r0, #CxPc]
    mrs r1, apsr
    orr r1, r1, #0x20
    str r1, [r0, #CxCpsr]
    vmrs r1, fpscr
    str r1, [r0, #CxFpscr]
    add r1, r0, #CxD
    vstmia r1!, {d0-d15}
    vstmia r1, {d16-d31}
    movw r1, #(CONTEXT_FULL_ARM & 0xFFFF)
    movt r1, #(CONTEXT_FULL_ARM >> 16)
    str r1, [r0, #CxContextFlags]
    ldr r1, [r0, #CxR1]
    bx lr

/*
 * VOID RtlpRestoreContextInternal(PCONTEXT ContextRecord): never returns. The
 * target pc is staged one word below the target stack and popped last.
 */
    .global RtlpRestoreContextInternal
    .thumb_func
RtlpRestoreContextInternal:
    add r1, r0, #CxD
    vldmia r1!, {d0-d15}
    vldmia r1, {d16-d31}
    ldr r1, [r0, #CxFpscr]
    vmsr fpscr, r1
    ldr r1, [r0, #CxCpsr]
    msr APSR_nzcvq, r1
    ldr r2, [r0, #CxSp]
    ldr r3, [r0, #CxPc]
    orr r3, r3, #1
    sub r2, r2, #4
    str r3, [r2]
    mov sp, r2
    ldr lr, [r0, #CxLr]
    add r0, r0, #CxR0
    ldm r0, {r0-r12}
    pop {pc}
