/*
 * PROJECT:     LiberNT xaudio2_9.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x86 voice callback wrapper for MSVC builds
 * COPYRIGHT:   Copyright 2015 Andrew Eikum for CodeWeavers
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code

PUBLIC _call_on_voice_processing_pass_start
_call_on_voice_processing_pass_start:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ebx
    sub esp, 4
    push dword ptr [ebp + 12]
    push dword ptr [ebp + 8]
    mov eax, [ebp + 8]
    mov eax, [eax]
    call dword ptr [eax]
    lea esp, [ebp - 12]
    pop ebx
    pop edi
    pop esi
    pop ebp
    ret

END
