/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) nested exception and collided unwind handler thunks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

/*
 * EXCEPTION_DISPOSITION RtlpExecuteHandlerForException(ExceptionRecord, EstablisherFrame,
 *     ContextRecord, DispatcherContext, ExceptionRoutine)
 * Runs a language handler in a frame guarded by RtlpExceptionHandler, which finds
 * the dispatcher context 16 bytes below this frame's establisher frame.
 */
    .global RtlpExecuteHandlerForException
    .seh_proc RtlpExecuteHandlerForException
    .thumb_func
RtlpExecuteHandlerForException:
    .seh_handler RtlpExceptionHandler, %except
    push.w {r3, r4, r11, lr}
    .seh_save_regs_w {r3, r4, r11, lr}
    .seh_endprologue
    ldr r12, [sp, #16]
    blx r12
    .seh_startepilogue
    pop.w {r3, r4, r11, pc}
    .seh_save_regs_w {r3, r4, r11, pc}
    .seh_endepilogue
    .seh_endproc

/* Same for termination handlers, guarded by RtlpUnwindHandler. */
    .global RtlpExecuteHandlerForUnwind
    .seh_proc RtlpExecuteHandlerForUnwind
    .thumb_func
RtlpExecuteHandlerForUnwind:
    .seh_handler RtlpUnwindHandler, %except
    push.w {r3, r4, r11, lr}
    .seh_save_regs_w {r3, r4, r11, lr}
    .seh_endprologue
    ldr r12, [sp, #16]
    blx r12
    .seh_startepilogue
    pop.w {r3, r4, r11, pc}
    .seh_save_regs_w {r3, r4, r11, pc}
    .seh_endepilogue
    .seh_endproc
