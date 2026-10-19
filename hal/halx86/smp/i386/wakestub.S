/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 real-mode entry stub for S2/S3 resume and processor restart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>

PUBLIC _HalpWakeEntry16
PUBLIC _HalpWakeEntryProt32
PUBLIC _HalpWakeEntryJump32
PUBLIC _HalpWakeEntryProcState
PUBLIC _HalpWakeEntryHigh
PUBLIC _HalpWakeEntryPageDir
PUBLIC _HalpWakeEntryCr4
PUBLIC _HalpWakeEntryGdtr
PUBLIC _HalpWakeEntryGdt
PUBLIC _HalpWakeEntry16End
PUBLIC _HalpSleepSaveContext
PUBLIC _HalpSleepReadXcr0
PUBLIC _HalpSleepResume

.equ OFF_PROCSTATE, _HalpWakeEntryProcState - _HalpWakeEntry16
.equ OFF_HIGH, _HalpWakeEntryHigh - _HalpWakeEntry16
.equ OFF_PAGEDIR, _HalpWakeEntryPageDir - _HalpWakeEntry16
.equ OFF_CR4, _HalpWakeEntryCr4 - _HalpWakeEntry16
.equ OFF_GDTR, _HalpWakeEntryGdtr - _HalpWakeEntry16

.code16
_HalpWakeEntry16:
    cli
    cld

    xor eax, eax
    mov ax, cs
    shl eax, 4
    mov ebp, eax

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    data32 lgdt cs:[OFF_GDTR]

    mov eax, cr0
    and eax, HEX(DFFFFFFF)
    mov cr0, eax
    wbinvd
    and eax, HEX(BFFFFFFF)
    or eax, 1
    mov cr0, eax

    .byte HEX(66)
    .byte HEX(EA)
_HalpWakeEntryJump32:
    .long 0
    .word HEX(08)
.endcode16

.code32
_HalpWakeEntryProt32:
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, [ebp + OFF_CR4]
    mov cr4, eax
    mov eax, [ebp + OFF_PAGEDIR]
    mov cr3, eax
    mov eax, cr0
    or eax, HEX(80000001)
    mov cr0, eax

    mov esi, [ebp + OFF_PROCSTATE]
    mov eax, [ebp + OFF_HIGH]
    jmp eax

.align 4
_HalpWakeEntryProcState:
    .long 0
_HalpWakeEntryHigh:
    .long 0
_HalpWakeEntryPageDir:
    .long 0
_HalpWakeEntryCr4:
    .long 0
_HalpWakeEntryGdtr:
    .word 0
    .long 0
    .word 0
.align 8
_HalpWakeEntryGdt:
    .quad HEX(0000000000000000)
    .quad HEX(00CF9A000000FFFF)
    .quad HEX(00CF92000000FFFF)
_HalpWakeEntry16End:

_HalpSleepSaveContext:
    mov ecx, [esp + 4]
    mov [ecx + PsContextFrame + CsEbx], ebx
    mov [ecx + PsContextFrame + CsEsi], esi
    mov [ecx + PsContextFrame + CsEdi], edi
    mov [ecx + PsContextFrame + CsEbp], ebp
    lea eax, [esp + 4]
    mov [ecx + PsContextFrame + CsEsp], eax
    mov eax, [esp]
    mov [ecx + PsContextFrame + CsEip], eax
    xor eax, eax
    mov ax, ds
    mov [ecx + PsContextFrame + CsSegDs], eax
    mov ax, es
    mov [ecx + PsContextFrame + CsSegEs], eax
    mov ax, fs
    mov [ecx + PsContextFrame + CsSegFs], eax
    mov ax, gs
    mov [ecx + PsContextFrame + CsSegGs], eax
    mov ax, ss
    mov [ecx + PsContextFrame + CsSegSs], eax

    push ebx
    push edi
    push esi
    mov esi, cr0
    mov eax, esi
    and eax, HEX(FFFFFFF7)
    mov cr0, eax

    mov edi, [ecx + ProcessorStateLength + 8]
    mov eax, [ecx + ProcessorStateLength]
    mov edx, [ecx + ProcessorStateLength + 4]
    mov ebx, eax
    or ebx, edx
    jz HalpSleepSaveFxsr
    mov ebx, [ecx + ProcessorStateLength + 16]
    or ebx, [ecx + ProcessorStateLength + 20]
    jnz HalpSleepSaveSupervisor
    xsave [edi]
    jmp HalpSleepSaveDone
HalpSleepSaveSupervisor:
    or eax, [ecx + ProcessorStateLength + 16]
    or edx, [ecx + ProcessorStateLength + 20]
    xsaves [edi]
    jmp HalpSleepSaveDone
HalpSleepSaveFxsr:
    mov eax, cr4
    test eax, HEX(200)
    jz HalpSleepSaveFnsave
    fxsave [edi]
    jmp HalpSleepSaveDone
HalpSleepSaveFnsave:
    fnsave [edi]
    frstor [edi]
HalpSleepSaveDone:
    mov cr0, esi
    pop esi
    pop edi
    pop ebx
    xor eax, eax
    ret

_HalpSleepReadXcr0:
    xor ecx, ecx
    xgetbv
    ret

_HalpSleepResume:
    mov eax, [esi + PsSpecialRegisters + SrCr3]
    mov cr3, eax
    mov esp, [esi + PsContextFrame + CsEsp]
    mov eax, [esi + PsSpecialRegisters + SrCr4]
    mov cr4, eax
    mov eax, [esi + PsSpecialRegisters + SrCr0]
    and eax, HEX(FFFFFFF7)
    mov cr0, eax

    lgdt [esi + PsSpecialRegisters + SrGdtr + 2]
    lidt [esi + PsSpecialRegisters + SrIdtr + 2]

    push KGDT_R0_CODE
    push offset HalpSleepResumeKernelCs
    retf

HalpSleepResumeKernelCs:
    mov eax, [esi + PsContextFrame + CsSegSs]
    mov ss, ax
    mov eax, [esi + PsContextFrame + CsSegDs]
    mov ds, ax
    mov eax, [esi + PsContextFrame + CsSegEs]
    mov es, ax
    mov eax, [esi + PsContextFrame + CsSegFs]
    mov fs, ax
    mov eax, [esi + PsContextFrame + CsSegGs]
    mov gs, ax

    movzx eax, word ptr [esi + PsSpecialRegisters + SrTr]
    and eax, HEX(FFF8)
    add eax, [esi + PsSpecialRegisters + SrGdtr + 4]
    and byte ptr [eax + 5], HEX(FD)
    ltr word ptr [esi + PsSpecialRegisters + SrTr]
    lldt word ptr [esi + PsSpecialRegisters + SrLdtr]

    mov eax, [esi + PsSpecialRegisters + SrKernelDr0]
    mov dr0, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr1]
    mov dr1, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr2]
    mov dr2, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr3]
    mov dr3, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr6]
    mov dr6, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr7]
    mov dr7, eax

    mov edi, [esi + ProcessorStateLength + 8]
    mov eax, [esi + ProcessorStateLength]
    mov edx, [esi + ProcessorStateLength + 4]
    mov ebx, eax
    or ebx, edx
    jz HalpSleepRestoreFxsr
    xor ecx, ecx
    xsetbv
    mov eax, [esi + ProcessorStateLength + 16]
    mov edx, [esi + ProcessorStateLength + 20]
    mov ebx, eax
    or ebx, edx
    jnz HalpSleepRestoreSupervisor
    mov eax, [esi + ProcessorStateLength]
    mov edx, [esi + ProcessorStateLength + 4]
    xrstor [edi]
    jmp HalpSleepRestoreDone
HalpSleepRestoreSupervisor:
    mov ecx, HEX(0DA0)
    wrmsr
    mov eax, [esi + ProcessorStateLength]
    mov edx, [esi + ProcessorStateLength + 4]
    or eax, [esi + ProcessorStateLength + 16]
    or edx, [esi + ProcessorStateLength + 20]
    xrstors [edi]
    jmp HalpSleepRestoreDone
HalpSleepRestoreFxsr:
    mov eax, cr4
    test eax, HEX(200)
    jz HalpSleepRestoreFrstor
    fxrstor [edi]
    jmp HalpSleepRestoreDone
HalpSleepRestoreFrstor:
    frstor [edi]
HalpSleepRestoreDone:
    mov eax, [esi + PsSpecialRegisters + SrCr0]
    mov cr0, eax

    mov ebx, [esi + PsContextFrame + CsEbx]
    mov edi, [esi + PsContextFrame + CsEdi]
    mov ebp, [esi + PsContextFrame + CsEbp]
    mov esp, [esi + PsContextFrame + CsEsp]
    push dword ptr [esi + PsContextFrame + CsEip]
    mov esi, [esi + PsContextFrame + CsEsi]
    mov eax, 1
    ret
END
