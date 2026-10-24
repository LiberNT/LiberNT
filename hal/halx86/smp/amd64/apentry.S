/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup trampoline
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Clean-room AP bring-up modelled on the Linux real-mode trampoline
 * (arch/x86/realmode/rm/trampoline_64.S): the AP starts in 16-bit real
 * mode at the SIPI vector, switches to 32-bit protected mode, enables
 * PAE + EFER.LME + paging to reach 64-bit long mode, then loads the real
 * kernel CR3/GDT/IDT from the KPROCESSOR_STATE and enters KiSystemStartup.
 *
 * The low part (HalpAPEntry16 .. HalpAPEntry16End) is copied to a page
 * below 1 MB; HalpAPEntry64 runs in place at its kernel address. The far
 * jump immediates, the temporary PML4 root, the ProcessorState pointer and
 * the trampoline GDTR base are patched in by HalStartNextProcessor.
 */

#include <asm.inc>
#include <ksamd64.inc>

PUBLIC HalpAPEntry16
PUBLIC HalpAPEntryProt32
PUBLIC HalpAPEntryLong64
PUBLIC HalpAPEntryJump32
PUBLIC HalpAPEntryJump64
PUBLIC HalpAPEntryProcState
PUBLIC HalpAPEntryHigh
PUBLIC HalpAPEntryPmlRoot
PUBLIC HalpAPEntryGdtr
PUBLIC HalpAPEntryGdt
PUBLIC HalpAPEntry16End
PUBLIC HalpAPEntry64
PUBLIC HalpSleepSaveContext
PUBLIC HalpSleepReadXcr0
PUBLIC HalpSleepResume

#ifdef _USE_ML
.code64
#include "apentry16.inc"
#else
.equ OFF_PROCSTATE, HalpAPEntryProcState - HalpAPEntry16
.equ OFF_HIGH, HalpAPEntryHigh - HalpAPEntry16
.equ OFF_PMLROOT, HalpAPEntryPmlRoot - HalpAPEntry16
.equ OFF_GDTR, HalpAPEntryGdtr - HalpAPEntry16

.code16
HalpAPEntry16:
    cli
    cld

    /* ebp = physical base of this stub (CS << 4) */
    xor eax, eax
    mov ax, cs
    shl eax, 4
    mov ebp, eax

    /* Flat segments so cs-relative data access works */
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    /* Load the trampoline GDT (limit/base patched in by C) */
    data32 lgdt cs:[OFF_GDTR]

    /*
     * INIT leaves the AP with caching disabled. Flush any firmware cache
     * state, then clear CR0.NW/CD before executing the kernel trampoline.
     */
    mov eax, cr0
    and eax, HEX(DFFFFFFF)
    mov cr0, eax
    wbinvd
    and eax, HEX(BFFFFFFF)
    or eax, 1
    mov cr0, eax

    /* Far jump into 32-bit protected mode (offset patched in by C) */
    .byte HEX(66)
    .byte HEX(EA)
HalpAPEntryJump32:
    .long 0
    .word HEX(08)
.endcode16

.code32
HalpAPEntryProt32:
    /* Reload the 32-bit data segments */
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    /* Enable PAE */
    mov eax, cr4
    or eax, HEX(20)
    mov cr4, eax

    /* Load the temporary PML4 (patched in by C) */
    mov eax, [ebp + OFF_PMLROOT]
    mov cr3, eax

    /* Set EFER.LME and EFER.NXE (the kernel page tables use the NX bit, so
       NXE must be enabled before loading a kernel CR3 or the NX bit reads as
       a reserved bit and every access faults) */
    mov ecx, HEX(C0000080)
    rdmsr
    or eax, HEX(900)
    wrmsr

    /* Enable paging -> IA-32e compatibility mode */
    mov eax, cr0
    or eax, HEX(80000001)
    mov cr0, eax

    /* Far jump into 64-bit long mode (offset patched in by C) */
    .byte HEX(EA)
HalpAPEntryJump64:
    .long 0
    .word HEX(18)

.code64
HalpAPEntryLong64:
    /* rbp still holds the stub base. Fetch the ProcessorState pointer. */
    mov rcx, [rbp + OFF_PROCSTATE]

    /* Switch to the AP kernel stack (mapped by the temporary PML4) */
    mov rsp, [rcx + PsContextFrame + CxRsp]

    /* Jump to the in-place restore code (high kernel VA) */
    mov rax, [rbp + OFF_HIGH]
    jmp rax

/* Data, copied with the stub and patched in by HalStartNextProcessor */
.align 8
HalpAPEntryProcState:
    .quad 0
HalpAPEntryHigh:
    .quad 0
HalpAPEntryPmlRoot:
    .long 0
    .long 0
HalpAPEntryGdtr:
    .word 0
    .long 0
    .word 0
.align 8
HalpAPEntryGdt:
    .quad HEX(0000000000000000)
    .quad HEX(00CF9A000000FFFF)
    .quad HEX(00CF92000000FFFF)
    .quad HEX(00209A0000000000)
HalpAPEntry16End:
#endif

.code64
HalpAPEntry64:
    /* rcx = ProcessorState. Switch to the real kernel address space. */
    mov rax, [rcx + PsSpecialRegisters + SrCr3]
    mov cr3, rax
    mov rax, [rcx + PsSpecialRegisters + SrCr4]
    mov cr4, rax
    mov rax, [rcx + PsSpecialRegisters + SrCr0]
    mov cr0, rax

    /* Load the real GDT/IDT (KDESCRIPTOR: +6 skips Pad[3] to reach Limit) */
    lgdt lXdtPrefix [rcx + PsSpecialRegisters + SrGdtr + 6]
    lidt lXdtPrefix [rcx + PsSpecialRegisters + SrIdtr + 6]

    /* Reload the segment registers */
    mov ax, KGDT64_R0_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax
    xor ax, ax
    mov fs, ax

    /* Load the task register */
    ltr word ptr [rcx + PsSpecialRegisters + SrTr]

    /* Restore the debug registers */
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr0]
    mov dr0, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr1]
    mov dr1, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr2]
    mov dr2, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr3]
    mov dr3, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr6]
    mov dr6, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr7]
    mov dr7, rax

    /* Reload the stack pointer from the context */
    mov rsp, [rcx + PsContextFrame + CxRsp]

    /* Restore flags (interrupts stay masked) */
    mov eax, [rcx + PsContextFrame + CxEFlags]
    push rax
    popfq

    /* Build a far return frame: CS = KGDT64_R0_CODE, RIP = KiSystemStartup.
       A far return is required to reload CS away from the trampoline GDT. */
    mov rax, [rcx + PsContextFrame + CxRip]
    mov r9, [rcx + PsContextFrame + CxRcx]
    push KGDT64_R0_CODE
    push rax

    /* rcx = first argument (LoaderBlock) */
    mov rcx, r9

    /* Clear the remaining general purpose registers */
    xor eax, eax
    xor ebx, ebx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d

    /* retfq (REX.W RETF) */
    .byte HEX(48)
    .byte HEX(CB)

/* ULONG HalpSleepSaveContext(PHALP_SLEEP_PROCESSOR State): 0 when saved, 1 when resumed */
HalpSleepSaveContext:
    mov [rcx + PsContextFrame + CxRbx], rbx
    mov [rcx + PsContextFrame + CxRbp], rbp
    mov [rcx + PsContextFrame + CxRsi], rsi
    mov [rcx + PsContextFrame + CxRdi], rdi
    mov [rcx + PsContextFrame + CxR12], r12
    mov [rcx + PsContextFrame + CxR13], r13
    mov [rcx + PsContextFrame + CxR14], r14
    mov [rcx + PsContextFrame + CxR15], r15
    movdqu [rcx + PsContextFrame + CxXmm6], xmm6
    movdqu [rcx + PsContextFrame + CxXmm7], xmm7
    movdqu [rcx + PsContextFrame + CxXmm8], xmm8
    movdqu [rcx + PsContextFrame + CxXmm9], xmm9
    movdqu [rcx + PsContextFrame + CxXmm10], xmm10
    movdqu [rcx + PsContextFrame + CxXmm11], xmm11
    movdqu [rcx + PsContextFrame + CxXmm12], xmm12
    movdqu [rcx + PsContextFrame + CxXmm13], xmm13
    movdqu [rcx + PsContextFrame + CxXmm14], xmm14
    movdqu [rcx + PsContextFrame + CxXmm15], xmm15
    stmxcsr [rcx + PsContextFrame + CxMxCsr]
    lea rax, [rsp + 8]
    mov [rcx + PsContextFrame + CxRsp], rax
    mov rax, [rsp]
    mov [rcx + PsContextFrame + CxRip], rax

    /* Extended state, mask and area follow the KPROCESSOR_STATE */
    mov r9, [rcx + ProcessorStateLength + 8]
    mov rax, [rcx + ProcessorStateLength]
    test rax, rax
    jz HalpSleepSaveFxsr
    mov r10, [rcx + ProcessorStateLength + 16]
    test r10, r10
    jnz HalpSleepSaveSupervisor
    mov rdx, rax
    shr rdx, 32
    xsave64 [r9]
    jmp HalpSleepSaveDone
HalpSleepSaveSupervisor:
    or rax, r10
    mov rdx, rax
    shr rdx, 32
    xsaves64 [r9]
    jmp HalpSleepSaveDone
HalpSleepSaveFxsr:
    fxsave64 [r9]
HalpSleepSaveDone:
    xor eax, eax
    ret

/* ULONG64 HalpSleepReadXcr0(VOID) */
HalpSleepReadXcr0:
    xor ecx, ecx
    xgetbv
    shl rdx, 32
    or rax, rdx
    ret

/* Entered from the low stub in long mode: rcx = PHALP_SLEEP_PROCESSOR */
HalpSleepResume:
    mov rax, [rcx + PsSpecialRegisters + SrCr3]
    mov cr3, rax
    mov rax, [rcx + PsSpecialRegisters + SrCr4]
    mov cr4, rax
    mov rax, [rcx + PsSpecialRegisters + SrCr0]
    mov cr0, rax

    lgdt lXdtPrefix [rcx + PsSpecialRegisters + SrGdtr + 6]
    lidt lXdtPrefix [rcx + PsSpecialRegisters + SrIdtr + 6]

    mov ax, KGDT64_R0_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax
    xor ax, ax
    mov fs, ax

    /* The saved TSS descriptor is still marked busy: make it available */
    movzx eax, word ptr [rcx + PsSpecialRegisters + SrTr]
    and eax, HEX(FFF8)
    add rax, [rcx + PsSpecialRegisters + SrGdtr + 8]
    and byte ptr [rax + 5], HEX(FD)
    ltr word ptr [rcx + PsSpecialRegisters + SrTr]

    mov r8, rcx
    mov ecx, HEX(C0000101)
    mov eax, [r8 + PsSpecialRegisters + SrMsrGsBase]
    mov edx, [r8 + PsSpecialRegisters + SrMsrGsBase + 4]
    wrmsr
    mov ecx, HEX(C0000102)
    mov eax, [r8 + PsSpecialRegisters + SrMsrGsSwap]
    mov edx, [r8 + PsSpecialRegisters + SrMsrGsSwap + 4]
    wrmsr
    mov rcx, r8

    /* Reload CS from the kernel GDT */
    lea rax, [rip+HalpSleepResumeKernelCs]
    push KGDT64_R0_CODE
    push rax
    .byte HEX(48)
    .byte HEX(CB)

HalpSleepResumeKernelCs:
    mov r8, rcx
    mov r9, [r8 + ProcessorStateLength + 8]
    mov rax, [r8 + ProcessorStateLength]
    test rax, rax
    jz HalpSleepRestoreFxsr
    mov rdx, rax
    shr rdx, 32
    xor ecx, ecx
    xsetbv
    mov r10, [r8 + ProcessorStateLength + 16]
    test r10, r10
    jnz HalpSleepRestoreSupervisor
    mov rax, [r8 + ProcessorStateLength]
    mov rdx, rax
    shr rdx, 32
    xrstor64 [r9]
    jmp HalpSleepRestoreDone
HalpSleepRestoreSupervisor:
    mov ecx, HEX(0DA0)
    mov rax, r10
    mov rdx, r10
    shr rdx, 32
    wrmsr
    mov rax, [r8 + ProcessorStateLength]
    or rax, r10
    mov rdx, rax
    shr rdx, 32
    xrstors64 [r9]
    jmp HalpSleepRestoreDone
HalpSleepRestoreFxsr:
    fxrstor64 [r9]
HalpSleepRestoreDone:
    mov rcx, r8

    mov rbx, [rcx + PsContextFrame + CxRbx]
    mov rbp, [rcx + PsContextFrame + CxRbp]
    mov rsi, [rcx + PsContextFrame + CxRsi]
    mov rdi, [rcx + PsContextFrame + CxRdi]
    mov r12, [rcx + PsContextFrame + CxR12]
    mov r13, [rcx + PsContextFrame + CxR13]
    mov r14, [rcx + PsContextFrame + CxR14]
    mov r15, [rcx + PsContextFrame + CxR15]
    movdqu xmm6, [rcx + PsContextFrame + CxXmm6]
    movdqu xmm7, [rcx + PsContextFrame + CxXmm7]
    movdqu xmm8, [rcx + PsContextFrame + CxXmm8]
    movdqu xmm9, [rcx + PsContextFrame + CxXmm9]
    movdqu xmm10, [rcx + PsContextFrame + CxXmm10]
    movdqu xmm11, [rcx + PsContextFrame + CxXmm11]
    movdqu xmm12, [rcx + PsContextFrame + CxXmm12]
    movdqu xmm13, [rcx + PsContextFrame + CxXmm13]
    movdqu xmm14, [rcx + PsContextFrame + CxXmm14]
    movdqu xmm15, [rcx + PsContextFrame + CxXmm15]
    ldmxcsr [rcx + PsContextFrame + CxMxCsr]
    mov rsp, [rcx + PsContextFrame + CxRsp]
    mov rax, [rcx + PsContextFrame + CxRip]
    push rax
    mov eax, 1
    ret
END
