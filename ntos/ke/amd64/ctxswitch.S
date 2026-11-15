/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 thread startup and context switch
 * COPYRIGHT:   Copyright Timo kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ksamd64.inc>
#include <trapamd64.inc>

EXTERN KiSwapContextResume:PROC

.code64

/* KiThreadStartup(StartRoutine rcx, StartContext rdx, P3 r8, P4 r9, SystemRoutine): first code of every thread */
PUBLIC KiThreadStartup
.PROC KiThreadStartup

    /* The KSTART_FRAME is already on the stack; this describes it as if a prolog had built it */
    .allocstack (5 * 8)
    .endprolog

    /* No nonvolatile register leaks into a new thread, whatever old code expects to find there */
    xor rbx, rbx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    mov rax, APC_LEVEL
    mov cr8, rax

    sti

    mov rcx, [rsp + SfP1Home]
    mov rdx, [rsp + SfP2Home]
    mov r8, [rsp + SfP3Home]
    call qword ptr [rsp + SfP4Home]

    add rsp, 5 * 8
    ret
.ENDP

PUBLIC KiInvalidSystemThreadStartupExit
.PROC KiInvalidSystemThreadStartupExit
    .endprolog

    int HEX(2C)
    nop
.ENDP

PUBLIC KiUserThreadStartupExit
.PROC KiUserThreadStartupExit
    .allocstack (KEXCEPTION_FRAME_LENGTH - 8)
    .savereg rbp, ExRbp
    .savereg rbx, ExRbx
    .savereg rdi, ExRdi
    .savereg rsi, ExRsi
    .savereg r12, ExR12
    .savereg r13, ExR13
    .savereg r14, ExR14
    .savereg r15, ExR15
    .savexmm128 xmm6, ExXmm6
    .savexmm128 xmm7, ExXmm7
    .savexmm128 xmm8, ExXmm8
    .savexmm128 xmm9, ExXmm9
    .savexmm128 xmm10, ExXmm10
    .savexmm128 xmm11, ExXmm11
    .savexmm128 xmm12, ExXmm12
    .savexmm128 xmm13, ExXmm13
    .savexmm128 xmm14, ExXmm14
    .savexmm128 xmm15, ExXmm15
    .endprolog

    RESTORE_EXCEPTION_STATE

    lea rcx, [rsp + 8]

    ret
.ENDP

/* KiSwapContextInternal: cl = wait IRQL of the old thread, rdx = old thread, r8 = new thread; returns its wait status */
.PROC KiSwapContextInternal

    push rbp
    .pushreg rbp
    sub rsp, 6 * 8
    .allocstack (6 * 8)
    .endprolog

.SwapBusySet:
    cmp byte ptr [r8 + ThSwapBusy], 0
    je .SwapBusyClear
    pause
    jmp .SwapBusySet
.SwapBusyClear:

    /* Charge execution time through the context-switch boundary. */
    pushfq
    cli
    mov r9, rdx
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov r10, gs:[PcStartCycles]
    mov gs:[PcStartCycles], rax
    test r10, r10
    jz .CycleTimeSeeded
    cmp rax, r10
    jb .CycleTimeSeeded
    sub rax, r10
    add [r9 + ThCycleTime], rax
    mov r11, [r9 + ThProcess]
    test r11, r11
    jz .ProcessCycleTimeCharged
    lock add [r11 + PrCycleTime], rax
.ProcessCycleTimeCharged:
    add gs:[PcCycleTime], rax
.CycleTimeSeeded:
    mov rdx, r9
    popfq

    mov [rsp + SwApcBypass], cl

    mov [rdx + KTHREAD_KernelStack], rsp

    mov rsp, [r8 + KTHREAD_KernelStack]

    /* User mode can leave any selector in GS; the new thread starts with the flat one */
    mov ax, gs
    cmp ax, (KGDT64_R3_DATA OR RPL_MASK)
    jne .ReloadGs
.GsReloaded:

    mov cl, [rsp + SwApcBypass]

    call KiSwapContextResume

    add rsp, 6 * 8
    pop rbp
    ret

.ReloadGs:
    mov r9, rdx
    mov ecx, MSR_GS_BASE
    pushfq
    cli
    swapgs
    rdmsr
    mov r10d, eax
    mov ax, (KGDT64_R3_DATA OR RPL_MASK)
    mov gs, ax
    mov eax, r10d
    wrmsr
    swapgs
    popfq
    mov rdx, r9
    jmp .GsReloaded

.ENDP

/* BOOLEAN KiSwapContext(KIRQL WaitIrql cl, PKTHREAD OldThread rdx) */
PUBLIC KiSwapContext
.PROC KiSwapContext

    GENERATE_EXCEPTION_FRAME

    mov r8, gs:[PcCurrentThread]
    call KiSwapContextInternal

    RESTORE_EXCEPTION_STATE

    ret
.ENDP

END
