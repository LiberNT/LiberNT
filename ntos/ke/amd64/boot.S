/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Move the boot processor onto its kernel boot stack
 * COPYRIGHT:   Copyright 2010 Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ksamd64.inc>

EXTERN KiSystemStartupBootStack:PROC

.code64

PUBLIC KiSwitchToBootStack
.PROC KiSwitchToBootStack
    mov ax, HEX(18)
    mov ss, ax
    mov rsp, rcx
    /* The frame starts 8 bytes off, as if a return address had been pushed */
    sub rsp, HEX(308)
    .ENDPROLOG

    jmp KiSystemStartupBootStack
.ENDP

END
