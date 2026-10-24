/*
 * PROJECT:     LiberNT kernelbase.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x64 fiber switch for MSVC builds
 * COPYRIGHT:   Copyright 2019 Alexandre Julliard
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code64

PUBLIC switch_fiber
.PROC switch_fiber
    .endprolog
    mov [rcx + HEX(90)], rbx
    lea rax, [rsp + 8]
    mov [rcx + HEX(98)], rax
    mov [rcx + HEX(a0)], rbp
    mov [rcx + HEX(a8)], rsi
    mov [rcx + HEX(b0)], rdi
    mov [rcx + HEX(d8)], r12
    mov [rcx + HEX(e0)], r13
    mov [rcx + HEX(e8)], r14
    mov [rcx + HEX(f0)], r15
    mov rax, [rsp]
    mov [rcx + HEX(f8)], rax
    movdqa [rcx + HEX(200)], xmm6
    movdqa [rcx + HEX(210)], xmm7
    movdqa [rcx + HEX(220)], xmm8
    movdqa [rcx + HEX(230)], xmm9
    movdqa [rcx + HEX(240)], xmm10
    movdqa [rcx + HEX(250)], xmm11
    movdqa [rcx + HEX(260)], xmm12
    movdqa [rcx + HEX(270)], xmm13
    movdqa [rcx + HEX(280)], xmm14
    movdqa [rcx + HEX(290)], xmm15
    mov rbx, [rdx + HEX(90)]
    mov rbp, [rdx + HEX(a0)]
    mov rsi, [rdx + HEX(a8)]
    mov rdi, [rdx + HEX(b0)]
    mov r12, [rdx + HEX(d8)]
    mov r13, [rdx + HEX(e0)]
    mov r14, [rdx + HEX(e8)]
    mov r15, [rdx + HEX(f0)]
    movdqa xmm6, [rdx + HEX(200)]
    movdqa xmm7, [rdx + HEX(210)]
    movdqa xmm8, [rdx + HEX(220)]
    movdqa xmm9, [rdx + HEX(230)]
    movdqa xmm10, [rdx + HEX(240)]
    movdqa xmm11, [rdx + HEX(250)]
    movdqa xmm12, [rdx + HEX(260)]
    movdqa xmm13, [rdx + HEX(270)]
    movdqa xmm14, [rdx + HEX(280)]
    movdqa xmm15, [rdx + HEX(290)]
    mov rsp, [rdx + HEX(98)]
    jmp qword ptr [rdx + HEX(f8)]
.ENDP

END
