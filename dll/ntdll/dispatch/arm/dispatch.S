/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 user-mode entry points the kernel redirects threads to
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ksarm.h>

    .syntax unified
    .thumb
    .text

.macro DISPATCHER Name
    .globl \Name
    .def \Name; .scl 2; .type 32; .endef
    .seh_proc \Name
    .p2align 2
    .thumb_func
\Name:
.endm

/* sp: normal routine, normal context, argument 1, argument 2, alertable, pad, CONTEXT */
DISPATCHER KiUserApcDispatcher
    .seh_custom 0xee, 0x02
    nop
    .seh_stackalloc 0x18
    .seh_endprologue
    ldr r0, [sp, #0x04]
    ldr r1, [sp, #0x08]
    ldr r2, [sp, #0x0c]
    add r3, sp, #0x18
    ldr r12, [sp]
    blx r12
    add r0, sp, #0x18
    ldr r1, [sp, #0x10]
    bl NtContinue
    bl RtlRaiseStatus
    udf #1
    .seh_endproc

/* sp: CONTEXT, EXCEPTION_RECORD */
DISPATCHER KiUserExceptionDispatcher
    .seh_custom 0xee, 0x02
    nop
    .seh_endprologue
    add r0, sp, #CONTEXT_FRAME_LENGTH
    mov r1, sp
    bl RtlDispatchException
    cbz r0, 1f
    mov r0, sp
    movs r1, #0
    bl NtContinue
    b 2f
1:
    add r0, sp, #CONTEXT_FRAME_LENGTH
    mov r1, sp
    movs r2, #0
    bl NtRaiseException
2:
    bl RtlRaiseStatus
    udf #1
    .seh_endproc

/* sp: UCALLOUT_FRAME */
DISPATCHER KiUserCallbackDispatcher
    .seh_custom 0xee, 0x01
    nop
    .seh_save_regs {lr}
    nop
    .seh_stackalloc 0xc
    .seh_endprologue
    ldr r0, [sp, #CkBuffer]
    ldr r1, [sp, #CkLength]
    ldr r2, [sp, #CkApiNumber]
    mrc p15, 0, r3, c13, c0, 2
    ldr r3, [r3, #TePeb]
    ldr r3, [r3, #PeKernelCallbackTable]
    ldr r12, [r3, r2, lsl #2]
    blx r12
    mov r2, r0
    movs r0, #0
    movs r1, #0
    bl NtCallbackReturn
    bl RtlRaiseStatus
    udf #1
    .seh_endproc

/* Entered as if called from the interrupted code, whose address the kernel left in lr. */
DISPATCHER KiRaiseUserExceptionDispatcher
    push {r4, lr}
    .seh_save_regs {r4, lr}
    sub sp, sp, #ExceptionRecordLength
    .seh_stackalloc ExceptionRecordLength
    .seh_endprologue
    mrc p15, 0, r4, c13, c0, 2
    ldr r4, [r4, #TeExceptionCode]
    str r4, [sp, #ErExceptionCode]
    movs r0, #0
    str r0, [sp, #ErExceptionFlags]
    str r0, [sp, #ErExceptionRecord]
    str r0, [sp, #ErNumberParameters]
    ldr r0, [sp, #(ExceptionRecordLength + 4)]
    bic r0, r0, #1
    str r0, [sp, #ErExceptionAddress]
    mov r0, sp
    bl RtlRaiseException
    mov r0, r4
    add sp, sp, #ExceptionRecordLength
    pop {r4, pc}
    .seh_endproc

/* The APC dispatcher hands the thread's start CONTEXT over in r3. */
DISPATCHER LdrInitializeThunk
    .seh_endprologue
    mov r0, r3
    b LdrpInit
    .seh_endproc
