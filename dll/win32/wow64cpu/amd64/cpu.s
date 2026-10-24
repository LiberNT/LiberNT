/*
 * PROJECT:     LiberNT WoW64 layer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x64 asm code for cpu.c
 * COPYRIGHT:   Copyright 2021 Alexandre Julliard
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code64

EXTERN Wow64SystemServiceEx:PROC
EXTERN cs32_sel:DWORD
EXTERN ss32_sel:DWORD

PUBLIC syscall_32to64
.PROC syscall_32to64
    .PUSHREG rbp
    .PUSHREG rbx
    .PUSHREG rsi
    .PUSHREG rdi
    .ALLOCSTACK HEX(28)
    .ENDPROLOG
    xchg rsp, r14
    mov [r13 + HEX(9c)], edi
    mov [r13 + HEX(a0)], esi
    mov [r13 + HEX(a4)], ebx
    mov [r13 + HEX(b4)], ebp
    mov edx, [r14]
    mov [r13 + HEX(b8)], edx
    mov edx, dword ptr [cs32_sel]
    mov [r13 + HEX(bc)], edx
    pushfq
    pop rdx
    mov [r13 + HEX(c0)], edx
    lea rdx, [r14 + 4]
    mov [r13 + HEX(c4)], edx
    mov rcx, rax
    lea rdx, [r14 + 8]
    call Wow64SystemServiceEx
    mov [r13 + HEX(b0)], eax
syscall_32to64_return::
    mov edi, [r13 + HEX(9c)]
    mov esi, [r13 + HEX(a0)]
    mov ebx, [r13 + HEX(a4)]
    mov ebp, [r13 + HEX(b4)]
    btr dword ptr [r13 - 4], 0
    jc @F
    mov edx, [r13 + HEX(b8)]
    mov [rsp], edx
    mov edx, [r13 + HEX(bc)]
    mov [rsp + 4], edx
    mov r14d, [r13 + HEX(c4)]
    xchg rsp, r14
    jmp fword ptr [r14]
@@:
    mov r14, rsp
    mov edx, [r13 + HEX(a8)]
    mov ecx, [r13 + HEX(ac)]
    mov eax, [r13 + HEX(c8)]
    mov [rsp + HEX(20)], rax
    mov ds, eax
    mov es, eax
    mov fs, word ptr [r13 + HEX(90)]
    mov eax, [r13 + HEX(c4)]
    mov [rsp + HEX(18)], rax
    mov eax, [r13 + HEX(c0)]
    mov [rsp + HEX(10)], rax
    mov eax, [r13 + HEX(bc)]
    mov [rsp + 8], rax
    mov eax, [r13 + HEX(b8)]
    mov [rsp], rax
    mov eax, [r13 + HEX(b0)]
    iretq
.ENDP

PUBLIC BTCpuSimulate
.PROC BTCpuSimulate
    push rbp
    .PUSHREG rbp
    push rbx
    .PUSHREG rbx
    push rsi
    .PUSHREG rsi
    push rdi
    .PUSHREG rdi
    sub rsp, HEX(28)
    .ALLOCSTACK HEX(28)
    .ENDPROLOG
    mov r12, gs:[HEX(30)]
    mov rcx, [r12 + HEX(1488)]
    lea r13, [rcx + 4]
    mov eax, dword ptr [cs32_sel]
    mov [r13 + HEX(bc)], eax
    mov eax, dword ptr [ss32_sel]
    mov [r13 + HEX(c8)], eax
    jmp syscall_32to64_return
.ENDP

END
