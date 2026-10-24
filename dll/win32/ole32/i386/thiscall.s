/*
 * PROJECT:     LiberNT ole32.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x86 thiscall wrappers for MSVC builds
 * COPYRIGHT:   Copyright 2019 Alexandre Julliard
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code

MACRO(DEFINE_THISCALL_WRAPPER, func, args)
EXTERN _&func&@&args:PROC
PUBLIC ___thiscall_&func
___thiscall_&func:
    pop eax
    push ecx
    push eax
    jmp _&func&@&args
ENDM

DEFINE_THISCALL_WRAPPER memory_allocator_Allocate, 8
DEFINE_THISCALL_WRAPPER memory_allocator_Free, 8

END
