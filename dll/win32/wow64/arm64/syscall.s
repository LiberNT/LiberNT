/*
 * PROJECT:     LiberNT WoW64 layer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 asm code for syscall.c
 * COPYRIGHT:   Copyright 2021 Alexandre Julliard
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .align 4
    .seh_proc raise_exception
    .global raise_exception
raise_exception:
    sub sp, sp, #0x390
    .seh_stackalloc 0x390
    stp x29, x30, [sp, #-48]!
    .seh_save_fplr_x 48
    .seh_endprologue
    .seh_handler raise_exception_handler, @except
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    add x0, sp, #48
    bl RtlCaptureContext
    add x1, sp, #48
    adr x2, 1f
    str x2, [x1, #0x108]
    ldp x2, x0, [sp, #32]
    bl NtRaiseException
raise_exception_ret:
    ldp x0, x1, [sp, #16]
    add x2, sp, #48
    bl call_user_exception_dispatcher
1:  nop
    ldp x29, x30, [sp], #48
    add sp, sp, #0x390
    ret
    .seh_endproc

    .align 4
    .seh_proc raise_exception_handler
    .global raise_exception_handler
raise_exception_handler:
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    .seh_endprologue
    ldr w4, [x0, #4]
    tst w4, #6
    b.ne 1f
    mov x2, x0
    mov x0, x1
    adr x1, raise_exception_ret
    bl RtlUnwind
1:  mov w0, #1
    ldp x29, x30, [sp], #16
    ret
    .seh_endproc

    .align 4
    .seh_proc wow64_syscall
    .global wow64_syscall
wow64_syscall:
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    .seh_endprologue
    .seh_handler wow64_syscall_handler, @except
    blr x1
    b 1f
wow64_syscall_ret:
    eor w1, w0, #0xc0000000
    cmp w1, #8
    b.ne 1f
    bl call_raise_user_exception_dispatcher
1:  ldp x29, x30, [sp], #16
    ret
    .seh_endproc

    .align 4
    .seh_proc wow64_syscall_handler
    .global wow64_syscall_handler
wow64_syscall_handler:
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    .seh_endprologue
    ldr w4, [x0, #4]
    tst w4, #6
    b.ne 1f
    mov x2, x0
    mov x0, x1
    adr x1, wow64_syscall_ret
    ldr w3, [x2]
    bl RtlUnwind
1:  mov w0, #1
    ldp x29, x30, [sp], #16
    ret
    .seh_endproc

    .align 4
    .seh_proc cpu_simulate
    .global cpu_simulate
cpu_simulate:
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    .seh_endprologue
    .seh_handler cpu_simulate_handler, @except
.Lcpu_simulate_loop:
    adrp x16, pBTCpuSimulate
    ldr x16, [x16, :lo12:pBTCpuSimulate]
    blr x16
    b .Lcpu_simulate_loop
    .seh_endproc

    .align 4
    .seh_proc cpu_simulate_handler
    .global cpu_simulate_handler
cpu_simulate_handler:
    stp x29, x30, [sp, #-48]!
    .seh_save_fplr_x 48
    stp x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    .seh_endprologue
    mov x19, x0
    mov x20, x1
    ldr w4, [x0, #4]
    tst w4, #6
    b.ne 1f
    stp x0, x2, [sp, #32]
    add x0, sp, #32
    bl Wow64PassExceptionToGuest
    mov x0, x20
    adr x1, .Lcpu_simulate_loop
    mov x2, x19
    bl RtlUnwind
1:  mov w0, #1
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #48
    ret
    .seh_endproc
