/*
 * PROJECT:     LiberNT kernelbase.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x86 fiber switch for MSVC builds
 * COPYRIGHT:   Copyright 2019 Alexandre Julliard
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code

PUBLIC _switch_fiber@8
_switch_fiber@8:
    mov ecx, [esp + 4]
    mov [ecx + HEX(9c)], edi
    mov [ecx + HEX(a0)], esi
    mov [ecx + HEX(a4)], ebx
    mov [ecx + HEX(b4)], ebp
    mov eax, [esp]
    mov [ecx + HEX(b8)], eax
    lea eax, [esp + 12]
    mov [ecx + HEX(c4)], eax
    mov ecx, [esp + 8]
    mov edi, [ecx + HEX(9c)]
    mov esi, [ecx + HEX(a0)]
    mov ebx, [ecx + HEX(a4)]
    mov ebp, [ecx + HEX(b4)]
    mov esp, [ecx + HEX(c4)]
    jmp dword ptr [ecx + HEX(b8)]

END
