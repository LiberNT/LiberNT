/*
 * COPYRIGHT:   Copyright 2010 Timo Kreuzer <timo.kreuzer@reactos.org>
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
    .ENDPROLOG

    jmp KiSystemStartupBootStack
.ENDP

END
