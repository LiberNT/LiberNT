/*
 * PROJECT:     ReactOS C runtime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 memset
 * COPYRIGHT:   Copyright 2005-2020 Rich Felker, et al.
 */

#include <asm.inc>

.code64

PUBLIC memset
FUNC memset
    push rdi
    .pushreg rdi
    .endprolog

    mov rdi, rcx
    movzx eax, dl
    mov r9, HEX(0101010101010101)
    imul rax, r9

    cmp r8, 126
    ja .Large

    test r8d, r8d
    jz .Done

    mov [rdi], dl
    mov [rdi + r8 - 1], dl
    cmp r8d, 2
    jbe .Done

    mov [rdi + 1], ax
    mov [rdi + r8 - 3], ax
    cmp r8d, 6
    jbe .Done

    mov [rdi + 3], eax
    mov [rdi + r8 - 7], eax
    cmp r8d, 14
    jbe .Done

    mov [rdi + 7], rax
    mov [rdi + r8 - 15], rax
    cmp r8d, 30
    jbe .Done

    mov [rdi + 15], rax
    mov [rdi + 23], rax
    mov [rdi + r8 - 31], rax
    mov [rdi + r8 - 23], rax
    cmp r8d, 62
    jbe .Done

    mov [rdi + 31], rax
    mov [rdi + 39], rax
    mov [rdi + 47], rax
    mov [rdi + 55], rax
    mov [rdi + r8 - 63], rax
    mov [rdi + r8 - 55], rax
    mov [rdi + r8 - 47], rax
    mov [rdi + r8 - 39], rax

.Done:
    mov rax, rdi
    pop rdi
    ret

.Large:
    cmp r8, 2048
    jae .Stosb

    movq xmm0, rax
    punpcklqdq xmm0, xmm0
    movdqu [rdi], xmm0
    movdqu [rdi + r8 - 64], xmm0
    movdqu [rdi + r8 - 48], xmm0
    movdqu [rdi + r8 - 32], xmm0
    movdqu [rdi + r8 - 16], xmm0
    lea rcx, [rdi + 16]
    and rcx, -16
    lea rdx, [rdi + r8 - 64]

.Sse64Loop:
    movdqa [rcx], xmm0
    movdqa [rcx + 16], xmm0
    movdqa [rcx + 32], xmm0
    movdqa [rcx + 48], xmm0
    add rcx, 64
    cmp rcx, rdx
    jb .Sse64Loop
    mov rax, rdi
    pop rdi
    ret

.Stosb:
    mov r9, rdi
    mov rcx, r8
    cld
    rep stosb
    mov rax, r9
    pop rdi
    ret
ENDFUNC

END
