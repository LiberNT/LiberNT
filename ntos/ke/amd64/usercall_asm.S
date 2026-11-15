/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 user-mode callback entry, return and stack switch
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ksamd64.inc>
#include <trapamd64.inc>

EXTERN KiInitiateUserApc:PROC
EXTERN KeUserCallbackDispatcher:QWORD

EXTERN KiUserModeCallout:PROC

.code64

/* NTSTATUS KiCallUserMode(PVOID *OutputBuffer rcx, PULONG OutputLength rdx) */
PUBLIC KiCallUserMode
.PROC KiCallUserMode

    GENERATE_EXCEPTION_FRAME

    mov [rsp + ExOutputBuffer], rcx
    mov [rsp + ExOutputLength], rdx

    mov rcx, rsp
    call KiUserModeCallout

    RESTORE_EXCEPTION_STATE

    ret

.ENDP

 /* KiUserCallbackExit(PKTRAP_FRAME rcx): back to user mode with only rsp and rip restored, the rest zeroed */
 PUBLIC KiUserCallbackExit
KiUserCallbackExit:

    mov rsp, rcx

    /* Zero the nonvolatiles for KiUserCallbackDispatcher before any pending user APC runs */
    xor rbx, rbx
    xor rdi, rdi
    xor rsi, rsi
    xor rbp, rbp
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    pxor xmm6, xmm6
    pxor xmm7, xmm7
    pxor xmm8, xmm8
    pxor xmm9, xmm9
    pxor xmm10, xmm10
    pxor xmm11, xmm11
    pxor xmm12, xmm12
    pxor xmm13, xmm13
    pxor xmm14, xmm14
    pxor xmm15, xmm15

    mov rdx, gs:[PcCurrentThread]
    HANDLE_USER_APCS rdx, rsp

    xor rax, rax
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5

    cli

    /* The callback is the outermost user level of its own kernel stack; NtCallbackReturn restores the caller's frame */
    mov rdx, gs:[PcCurrentThread]
    mov qword ptr [rdx + ThTrapFrame], 0

    RESTORE_USER_DEBUG_REGISTERS rsp

    mov rcx, qword ptr KeUserCallbackDispatcher[rip]
    mov r11, EFLAGS_IF_MASK

    mov rdx, gs:[PcCurrentThread]
    mov rdx, [rdx + ThApcState + AsProcess]
    mov rdx, [rdx + PrInstrumentationCallback]
    test rdx, rdx
    jz UserCallbackInstrumentationDone
    mov r10, rcx
    mov rcx, rdx
UserCallbackInstrumentationDone:
    xor edx, edx

    mov rsp, [rsp + KTRAP_FRAME_Rsp]

    swapgs

    sysretq

PUBLIC KiCallbackReturn
.PROC KiCallbackReturn

    .ENDPROLOG

    mov rsp, rcx

    mov eax, edx

    RESTORE_EXCEPTION_STATE

    ret

.ENDP

END
