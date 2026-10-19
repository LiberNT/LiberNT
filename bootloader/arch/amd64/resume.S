/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 hibernation resume trampoline
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

PUBLIC OslHiberTrampoline
PUBLIC OslHiberTrampolineEnd

.equ HJ_CR3, 0
.equ HJ_OPS, 8
.equ HJ_STUBBASE, 24
.equ HJ_STUBENTRY, 32
.equ HJ_STUBGDTR, 46
.equ HJ_CODE, HEX(100)
.equ HJ_STACK, HEX(1000)
.equ OFF_COMPAT, OslHiberCompat - OslHiberTrampoline

.code64
OslHiberTrampoline:
    cli
    cld
    mov rbx, rcx
    lea rsp, [rbx + HJ_STACK]

    mov rax, cr4
    btr rax, 17
    mov cr4, rax
    mov rax, [rbx + HJ_CR3]
    mov cr3, rax

    mov rdx, [rbx + HJ_OPS]
OslHiberPage:
    test rdx, rdx
    jz OslHiberDone
    mov r8, [rdx + 8]
    lea r9, [rdx + 16]
OslHiberNext:
    test r8, r8
    jz OslHiberPageDone
    mov rdi, [r9]
    mov rsi, [r9 + 8]
    mov ecx, 512
    test rsi, rsi
    jz OslHiberZero
    rep movsq
    jmp OslHiberAdvance
OslHiberZero:
    xor eax, eax
    rep stosq
OslHiberAdvance:
    add r9, 16
    dec r8
    jmp OslHiberNext
OslHiberPageDone:
    mov rdx, [rdx]
    jmp OslHiberPage

OslHiberDone:
    lgdt [rbx + HJ_STUBGDTR]
    mov ebp, [rbx + HJ_STUBBASE]
    mov esi, [rbx + HJ_STUBENTRY]
    lea rax, [rbx + HJ_CODE + OFF_COMPAT]
    push HEX(08)
    push rax
    .byte HEX(48), HEX(CB)

.code32
OslHiberCompat:
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr0
    and eax, HEX(7FFFFFFF)
    mov cr0, eax
    mov eax, cr4
    and eax, HEX(FFFFEFFF)
    mov cr4, eax
    jmp esi
OslHiberTrampolineEnd:

END
