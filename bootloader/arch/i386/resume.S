/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 hibernation resume trampoline
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

PUBLIC _OslHiberTrampoline
PUBLIC _OslHiberTrampolineEnd

#ifdef _USE_ML
#define HJ_OPS 8
#define HJ_STUBBASE 24
#define HJ_STUBENTRY 32
#define HJ_STUBGDTR 46
#define HJ_STACK HEX(1000)
#else
.equ HJ_OPS, 8
.equ HJ_STUBBASE, 24
.equ HJ_STUBENTRY, 32
.equ HJ_STUBGDTR, 46
.equ HJ_STACK, HEX(1000)
#endif

.code32
_OslHiberTrampoline:
    cli
    cld
    mov ebx, [esp + 4]
    lea esp, [ebx + HJ_STACK]

    mov eax, cr0
    and eax, HEX(7FFFFFFF)
    mov cr0, eax

    mov edx, [ebx + HJ_OPS]
OslHiberPage:
    test edx, edx
    jz OslHiberDone
    mov ebp, [edx + 8]
    lea eax, [edx + 16]
    push edx
    mov edx, eax
OslHiberNext:
    test ebp, ebp
    jz OslHiberPageDone
    mov edi, [edx]
    mov esi, [edx + 8]
    mov ecx, 1024
    test esi, esi
    jz OslHiberZero
    rep movsd
    jmp OslHiberAdvance
OslHiberZero:
    xor eax, eax
    rep stosd
OslHiberAdvance:
    add edx, 16
    dec ebp
    jmp OslHiberNext
OslHiberPageDone:
    pop edx
    mov edx, [edx]
    jmp OslHiberPage

OslHiberDone:
    lgdt lXdtPrefix [ebx + HJ_STUBGDTR]
    mov ebp, [ebx + HJ_STUBBASE]
    mov esi, [ebx + HJ_STUBENTRY]
    push HEX(08)
    push esi
    retf
_OslHiberTrampolineEnd:

END
