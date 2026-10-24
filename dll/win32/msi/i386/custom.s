/*
 * PROJECT:     LiberNT msi.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x86 custom action entry point wrapper for MSVC builds
 * COPYRIGHT:   Copyright 2005 Aric Stewart for CodeWeavers
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code

PUBLIC _custom_proc_wrapper
_custom_proc_wrapper:
    push ebp
    mov ebp, esp
    sub esp, 4
    push dword ptr [ebp + 12]
    call dword ptr [ebp + 8]
    leave
    ret

END
