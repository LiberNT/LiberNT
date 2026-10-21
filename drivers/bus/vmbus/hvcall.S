/*
 * PROJECT:     LiberNT Hyper-V VMBus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Executable page the hypervisor overlays with its hypercall code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code64

.align 4096
PUBLIC HvHypercallPage
HvHypercallPage:
    mov rax, HEX(FFFF)
    ret
    .fill 4088, 1, HEX(CC)

END
