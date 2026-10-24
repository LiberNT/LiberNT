/*
 * PROJECT:     LiberNT windowscodecs.dll
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

DEFINE_THISCALL_WRAPPER IMILUnknown1Impl_unknown1, 8
DEFINE_THISCALL_WRAPPER IMILUnknown1Impl_unknown3, 8
DEFINE_THISCALL_WRAPPER IMILUnknown1Impl_unknown8, 4

END
