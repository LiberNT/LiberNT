/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x86-64 FXSAVE image of the current thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code64

PUBLIC LxAmd64Fxsave
.PROC LxAmd64Fxsave
    .endprolog
    fxsave64 [rcx]
    ret
.ENDP

PUBLIC LxAmd64Fxrstor
.PROC LxAmd64Fxrstor
    .endprolog
    fxrstor64 [rcx]
    ret
.ENDP

END
