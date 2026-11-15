/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 fastcall interlocked list and arithmetic routines
 * COPYRIGHT:   Copyright Alex Ionescu <alex@relsoft.net>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>
#include <internal/i386/asmmacro.S>

.code32

/*
 * NOTE: These functions must obey the following rules:
 *  - Acquire locks only on MP systems.
 *  - Be safe at HIGH_LEVEL (no paged access).
 *  - Preserve flags.
 *  - Disable interrupts.
 */
PUBLIC @ExInterlockedAddLargeStatistic@8
@ExInterlockedAddLargeStatistic@8:

    lock add [ecx], edx

    jc .l1
    ret

.l1:

    lock adc dword ptr [ecx+4], 0

    ret

PUBLIC @ExInterlockedPopEntrySList@8
PUBLIC @InterlockedPopEntrySList@4
PUBLIC _ExpInterlockedPopEntrySListResume@0
PUBLIC _ExpInterlockedPopEntrySListFault@0
PUBLIC _ExpInterlockedPopEntrySListEnd@0
@ExInterlockedPopEntrySList@8:
@InterlockedPopEntrySList@4:

    push ebx
    push ebp

    mov ebp, ecx

_ExpInterlockedPopEntrySListResume@0:
    mov edx, [ebp+4]
    mov eax, [ebp]

    or eax, eax
    jz .l7

    lea ecx, [edx-1]

_ExpInterlockedPopEntrySListFault@0:
    mov ebx, [eax]
_ExpInterlockedPopEntrySListEnd@0:
    LOCK cmpxchg8b qword ptr [ebp]
    jnz _ExpInterlockedPopEntrySListResume@0

.l7:
    pop ebp
    pop ebx
    ret

PUBLIC @ExInterlockedPushEntrySList@12
@ExInterlockedPushEntrySList@12:

    pop [esp]

PUBLIC @InterlockedPushEntrySList@8
@InterlockedPushEntrySList@8:

    push ebx
    push ebp

    mov ebp, ecx
    mov ebx, edx

    mov edx, [ebp+4]
    mov eax, [ebp]

.l8:

    mov [ebx], eax

    lea ecx, [edx + HEX(10001)]

    LOCK cmpxchg8b qword ptr [ebp]
    jnz .l8

    pop ebp
    pop ebx
    ret

PUBLIC @ExInterlockedFlushSList@4
@ExInterlockedFlushSList@4:

    push ebx
    push ebp

    xor ebx, ebx

    mov ebp, ecx

    mov edx, [ebp+4]
    mov eax, [ebp]

.l9:

    or eax, eax
    jz .l10

    mov ecx, edx
    mov cx, bx

    LOCK cmpxchg8b qword ptr [ebp]
    jnz .l9

.l10:
    pop ebp
    pop ebx
    ret

END
