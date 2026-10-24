/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     EH4 local unwind entry point for x86
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

EXTERN __except_handler4_local_unwind:PROC

PUBLIC __local_unwind4

.code

__local_unwind4:
    jmp __except_handler4_local_unwind

END
