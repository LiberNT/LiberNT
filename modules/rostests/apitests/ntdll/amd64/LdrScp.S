/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <asm.inc>

.code64
PUBLIC TestScpDispatch
TestScpDispatch:
    mov rax, [rsp + 56]
    jmp qword ptr [rsp + 48]
END
