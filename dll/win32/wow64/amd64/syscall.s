/*
 * PROJECT:     LiberNT WoW64 layer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x64 asm code for syscall.c
 * COPYRIGHT:   Copyright 2021 Alexandre Julliard
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code64

EXTERN RtlCaptureContext:PROC
EXTERN NtRaiseException:PROC
EXTERN RtlUnwind:PROC
EXTERN Wow64PassExceptionToGuest:PROC
EXTERN call_user_exception_dispatcher:PROC
EXTERN call_raise_user_exception_dispatcher:PROC
EXTERN pBTCpuSimulate:QWORD
EXTERN pBTCpuResetToConsistentState:QWORD

PUBLIC raise_exception_handler
.PROC raise_exception_handler
    sub rsp, HEX(28)
    .ALLOCSTACK HEX(28)
    .ENDPROLOG
    test dword ptr [rcx + 4], 6
    jnz @F
    mov r8, rcx
    mov rcx, rdx
    lea rdx, raise_exception_ret
    call RtlUnwind
    int 3
@@:
    mov eax, 1
    add rsp, HEX(28)
    ret
.ENDP

PUBLIC raise_exception
raise_exception PROC FRAME:raise_exception_handler
    sub rsp, HEX(4d8)
    .ALLOCSTACK HEX(4d8)
    .ENDPROLOG
    mov [rsp + HEX(4e0)], rcx
    mov [rsp + HEX(4e8)], rdx
    mov [rsp + HEX(4f0)], r8
    mov [rsp + HEX(4f8)], r9
    mov rcx, rsp
    call RtlCaptureContext
    mov rdx, rsp
    lea rax, @F
    mov [rdx + HEX(f8)], rax
    mov rcx, [rsp + HEX(4f8)]
    mov r8, [rsp + HEX(4f0)]
    call NtRaiseException
raise_exception_ret::
    mov rcx, [rsp + HEX(4e0)]
    mov rdx, [rsp + HEX(4e8)]
    mov r8, rsp
    call call_user_exception_dispatcher
@@:
    nop
    add rsp, HEX(4d8)
    ret
raise_exception ENDP

PUBLIC wow64_syscall_handler
.PROC wow64_syscall_handler
    sub rsp, HEX(28)
    .ALLOCSTACK HEX(28)
    .ENDPROLOG
    test dword ptr [rcx + 4], 6
    jnz @F
    mov r9d, [rcx]
    mov r8, rcx
    mov rcx, rdx
    lea rdx, wow64_syscall_ret
    call RtlUnwind
    int 3
@@:
    mov eax, 1
    add rsp, HEX(28)
    ret
.ENDP

PUBLIC wow64_syscall
wow64_syscall PROC FRAME:wow64_syscall_handler
    sub rsp, HEX(28)
    .ALLOCSTACK HEX(28)
    .ENDPROLOG
    call rdx
    jmp @F
wow64_syscall_ret::
    cmp eax, HEX(c0000008)
    jne @F
    mov ecx, eax
    call call_raise_user_exception_dispatcher
@@:
    add rsp, HEX(28)
    ret
wow64_syscall ENDP

PUBLIC cpu_simulate_handler
.PROC cpu_simulate_handler
    sub rsp, HEX(38)
    .ALLOCSTACK HEX(38)
    .ENDPROLOG
    test dword ptr [rcx + 4], 6
    jnz @F
    mov rsi, rcx
    mov [rsp + HEX(20)], rcx
    mov rdi, rdx
    mov [rsp + HEX(28)], r8
    lea rcx, [rsp + HEX(20)]
    call Wow64PassExceptionToGuest
    mov rcx, rdi
    lea rdx, cpu_simulate_loop
    mov r8, rsi
    call RtlUnwind
    int 3
@@:
    mov eax, 1
    add rsp, HEX(38)
    ret
.ENDP

PUBLIC cpu_simulate
cpu_simulate PROC FRAME:cpu_simulate_handler
    sub rsp, HEX(28)
    .ALLOCSTACK HEX(28)
    .ENDPROLOG
cpu_simulate_loop::
    call qword ptr [pBTCpuSimulate]
    jmp cpu_simulate_loop
cpu_simulate ENDP

PUBLIC Wow64PrepareForException
.PROC Wow64PrepareForException
    .ENDPROLOG
    sub rsp, HEX(38)
    mov r10, rcx
    mov ax, cs
    cmp [rdx + HEX(38)], ax
    je @F
    mov rsi, rsp
    mov ecx, HEX(5c0)
    mov edi, [rdx + HEX(4d4)]
    cmp ecx, edi
    cmovl ecx, edi
    add rcx, rdx
    sub rcx, rsi
    sub r14, rcx
    and r14, NOT HEX(0f)
    mov rdi, r14
    shr rcx, 3
    rep movsq
    mov rax, r14
    sub rax, rsp
    add r10, rax
    add rdx, rax
    mov rsp, r14
@@:
    lea rcx, [rsp + HEX(20)]
    mov [rcx], r10
    mov [rcx + 8], rdx
    mov rax, qword ptr [pBTCpuResetToConsistentState]
    test rax, rax
    jz @F
    call rax
@@:
    add rsp, HEX(38)
    ret
.ENDP

END
