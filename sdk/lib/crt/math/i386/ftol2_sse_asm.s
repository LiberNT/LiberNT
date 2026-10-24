/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Floating point conversion routine for SSE2 code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

EXTERN __ftol:PROC
PUBLIC __ftol2_sse

.code

__ftol2_sse:
    jmp __ftol

END
