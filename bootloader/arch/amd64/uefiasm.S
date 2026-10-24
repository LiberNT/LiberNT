/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 loader stack switch and descriptor table load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ksamd64.inc>

EXTERN BasicStack:QWORD
EXTERN ExecuteLoaderCleanly:PROC

.code64

PUBLIC _changestack
_changestack:
    push rbx
    mov rbx, rsp
    mov rcx, rsp
    mov rsp, qword ptr BasicStack[rip]
    and rsp, -16
    sub rsp, HEX(20)
    call ExecuteLoaderCleanly
    mov rsp, rbx
    pop rbx
    ret

PUBLIC Amd64LoadDescriptorTables
Amd64LoadDescriptorTables:
    lgdt lXdtPrefix [rcx]
    lidt lXdtPrefix [rdx]
    mov ax, KGDT64_R0_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ax, KGDT64_SYS_TSS
    ltr ax
    pop rax
    push KGDT64_R0_CODE
    push rax
    .byte HEX(48), HEX(CB)

END
