/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 loader stack switch and kernel transfer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>

EXTERN _BasicStack:DWORD
EXTERN _ExecuteLoaderCleanly:PROC

.code

PUBLIC __changestack
__changestack:
    push ebx
    mov ebx, esp
    mov eax, esp
    mov esp, dword ptr [_BasicStack]
    and esp, -16
    sub esp, 12
    push eax
    call _ExecuteLoaderCleanly
    mov esp, ebx
    pop ebx
    ret

PUBLIC _I386EnterKernel
_I386EnterKernel:
    cli
    mov eax, cr0
    and eax, HEX(7FFFFFFF)
    mov cr0, eax
    mov eax, cr4
    and eax, HEX(FFFFFFDF)
    mov cr4, eax
    mov eax, dword ptr [esp + 4]
    jmp eax

PUBLIC _I386TransferStart
_I386TransferStart:
    mov eax, dword ptr [esp + 8]
    mov cr3, eax
    mov eax, cr0
    or eax, HEX(80000000)
    mov cr0, eax
    mov eax, dword ptr [esp + 12]
    lgdt lXdtPrefix [eax]
    mov eax, dword ptr [esp + 16]
    lidt lXdtPrefix [eax]
    call I386TransferBase
I386TransferBase:
    pop eax
    add eax, I386TransferSegments - I386TransferBase
    push KGDT_R0_CODE
    push eax
    .byte HEX(CB)
I386TransferSegments:
    mov ax, KGDT_R0_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov ax, KGDT_R0_PCR
    mov fs, ax
    xor eax, eax
    mov gs, ax
    lldt ax
    mov ax, KGDT_TSS
    ltr ax
    mov eax, dword ptr [esp + 20]
    mov edx, dword ptr [esp + 24]
    push edx
    call eax
I386TransferHalt:
    hlt
    jmp I386TransferHalt
PUBLIC _I386TransferEnd
_I386TransferEnd:

END
