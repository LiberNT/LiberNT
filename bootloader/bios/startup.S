/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     BOOTMGR startup stub: real-mode entry, boot application mapping and the BIOS call gate
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bios.h>

#ifdef _M_AMD64
#define BIOS_LONG_MODE 1
#endif

#define R(x)        ((x) - BiosStub)
#define L(x)        (BIOS_STUB_BASE + R(x))
#define SCRATCH(x)  (BIOS_STUB_BASE + BIOS_STUB_SCRATCH_OFFSET + (x))

#define IDT_LINEAR          SCRATCH(0)
#define IDT_ENTRIES         32
#define COM1                0x3F8
#define COM1_STATUS         0x3FD
#define CR0_PE              0x00000001
#define CR0_MP              0x00000002
#define CR0_EM              0x00000004
#define CR0_PG              0x80000000
#define CR4_PAE             0x00000020
#define CR4_OSFXSR          0x00000200
#define CR4_OSXMMEXCPT      0x00000400
#define MSR_EFER            0xC0000080
#define EFER_LME            0x00000100
#define EFER_NXE            0x00000800

    .text
    .code16

BiosStub:
    jmp RealEntry
    .balign 8
    .ascii "BOOTMGR\0"

    .org BIOS_STARTUP_INFO_OFFSET
BiosStartupInfo:
    .long BIOS_STARTUP_MAGIC
    .long BIOS_STARTUP_VERSION
    .long 0
    .long 0
    .quad 0
    .long L(BiosCallGate)
    .long 0
    .long 0
    .long 0
    .long 0
    .long 0

    .balign 8
BiosGdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF
    .quad 0x00009A020000FFFF
    .quad 0x000092020000FFFF
BiosGdtEnd:

    .balign 8
    .word 0
BiosGdtr:
    .word BiosGdtEnd - BiosGdt - 1
    .long L(BiosGdt)

    .balign 8
    .word 0
BiosIdtr:
#ifdef BIOS_LONG_MODE
    .word IDT_ENTRIES * 16 - 1
#else
    .word IDT_ENTRIES * 8 - 1
#endif
    .long IDT_LINEAR
    .long 0

    .balign 8
    .word 0
BiosRealIdtr:
    .word 0x3FF
    .long 0

    .balign 8
BiosCallRegisters:
    .fill BR_SIZE, 1, 0
    .balign 8
BiosSavedStack:
    .quad 0
BiosSavedRegisters:
    .quad 0
BiosSavedCr3:
    .quad 0

BiosMessageA20:
    .asciz "BOOTMGR: the A20 line could not be enabled.\r\n"
BiosMessageCpu:
    .asciz "BOOTMGR: this processor does not run 64-bit code.\r\n"
BiosMessageLoader:
    .asciz "BOOTMGR: not started by a LiberNT boot sector.\r\n"
BiosMessageImage:
    .asciz "BOOTMGR is damaged: its boot application image is not valid.\r\n"
BiosMessageReturn:
    .asciz "BOOTMGR: the boot manager returned.\r\n"
BiosMessageFault:
    .asciz "\r\n*** BOOTMGR processor fault "
BiosMessageAt:
    .asciz " at "
BiosMessageError:
    .asciz " error "
BiosMessageCr2:
    .asciz " cr2 "

/* Entered at 2000:0000 with the first 64 KB of the file here and the whole file at BIOS_FILE_BASE. */
RealEntry:
    cli
    cld
    ljmp $BIOS_STUB_SEGMENT, $R(RealNormalized)

RealNormalized:
    movw $BIOS_STUB_SEGMENT, %bp
    movw %bp, %ds
    movw %bp, %es
    movw %bp, %ss
    movl $BIOS_STUB_REAL_STACK, %esp
    cmpl $BIOS_LOADER_MAGIC, %eax
    jne RealNoLoader

    movzbl %dl, %eax
    movl %eax, R(BiosStartupInfo) + BSI_BOOT_DRIVE
    movzbl %dh, %eax
    movl %eax, R(BiosStartupInfo) + BSI_BOOT_MEDIUM
    movl %ebx, R(BiosStartupInfo) + BSI_PARTITION_START
    movl %edi, R(BiosStartupInfo) + BSI_PARTITION_START + 4
    movl %ecx, R(BiosStartupInfo) + BSI_FILE_SIZE
    sti

    call RealEnableA20
    jc RealNoA20

#ifdef BIOS_LONG_MODE
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x00200000, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    cmpl %eax, %ecx
    je RealNoCpu
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb RealNoCpu
    movl $0x80000001, %eax
    cpuid
    btl $29, %edx
    jnc RealNoCpu
#endif

    cli
    lgdtl R(BiosGdtr)
    movl %cr0, %eax
    orl $CR0_PE, %eax
    movl %eax, %cr0
    ljmpl $BIOS_SEL_CODE32, $L(Protected32)

RealNoLoader:
    movw $R(BiosMessageLoader), %si
    jmp RealFail
RealNoA20:
    movw $R(BiosMessageA20), %si
    jmp RealFail
#ifdef BIOS_LONG_MODE
RealNoCpu:
    movw $R(BiosMessageCpu), %si
    jmp RealFail
#endif

RealFail:
    sti
    call RealPrint
RealHalt:
    hlt
    jmp RealHalt

RealPrint:
    lodsb
    testb %al, %al
    jz 1f
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp RealPrint
1:
    ret

/* Returns ZF clear when A20 is enabled. */
RealCheckA20:
    pushw %ds
    pushw %es
    pushw %si
    pushw %di
    xorw %ax, %ax
    movw %ax, %ds
    notw %ax
    movw %ax, %es
    movw $0x0500, %si
    movw $0x0510, %di
    movb (%si), %al
    pushw %ax
    movb %es:(%di), %al
    pushw %ax
    movb $0x00, (%si)
    movb $0xFF, %es:(%di)
    cmpb $0xFF, (%si)
    popw %ax
    movb %al, %es:(%di)
    popw %ax
    movb %al, (%si)
    popw %di
    popw %si
    popw %es
    popw %ds
    ret

RealWaitKbc:
    movw $0xFFFF, %cx
1:
    inb $0x64, %al
    testb $0x02, %al
    loopnz 1b
    ret

RealEnableA20:
    call RealCheckA20
    jnz RealA20Done
    movw $0x2401, %ax
    int $0x15
    call RealCheckA20
    jnz RealA20Done
    inb $0x92, %al
    testb $0x02, %al
    jnz 1f
    orb $0x02, %al
    andb $0xFE, %al
    outb %al, $0x92
1:
    call RealCheckA20
    jnz RealA20Done
    call RealWaitKbc
    movb $0xD1, %al
    outb %al, $0x64
    call RealWaitKbc
    movb $0xDF, %al
    outb %al, $0x60
    call RealWaitKbc
    movw $0x100, %dx
2:
    call RealCheckA20
    jnz RealA20Done
    decw %dx
    jnz 2b
    stc
    ret
RealA20Done:
    clc
    ret

    .code32
Protected32:
    movw $BIOS_SEL_DATA32, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $(BIOS_STACK_BASE + BIOS_STACK_SIZE), %esp

    movl $1, %eax
    cpuid
    movl %cr0, %eax
    andl $~CR0_EM, %eax
    orl $CR0_MP, %eax
    movl %eax, %cr0
    fninit
    btl $25, %edx
    jnc 1f
    movl %cr4, %eax
    orl $(CR4_OSFXSR | CR4_OSXMMEXCPT), %eax
    movl %eax, %cr4
1:
    call Pm32BuildIdt
    lidtl L(BiosIdtr)

    movl $(BIOS_FILE_BASE + BIOS_STUB_IMAGE_OFFSET), %esi
    call Pm32MapImage
    jc Pm32BadImage

#ifdef BIOS_LONG_MODE
    movl $BIOS_PAGE_TABLE_BASE, %edi
    movl $(BIOS_PAGE_TABLE_SIZE / 4), %ecx
    xorl %eax, %eax
    rep stosl

    movl $(BIOS_PAGE_TABLE_BASE + 0x1003), BIOS_PAGE_TABLE_BASE
    movl $(BIOS_PAGE_TABLE_BASE + 0x2003), BIOS_PAGE_TABLE_BASE + 0x1000
    movl $(BIOS_PAGE_TABLE_BASE + 0x3003), BIOS_PAGE_TABLE_BASE + 0x1008
    movl $(BIOS_PAGE_TABLE_BASE + 0x4003), BIOS_PAGE_TABLE_BASE + 0x1010
    movl $(BIOS_PAGE_TABLE_BASE + 0x5003), BIOS_PAGE_TABLE_BASE + 0x1018

    movl $(BIOS_PAGE_TABLE_BASE + 0x2000), %edi
    movl $0x00000083, %eax
    movl $2048, %ecx
1:
    movl %eax, (%edi)
    addl $0x00200000, %eax
    addl $8, %edi
    decl %ecx
    jnz 1b

    movl $BIOS_PAGE_TABLE_BASE, %eax
    movl %eax, L(BiosSavedCr3)
    call Pm32EnterLong
    ljmp $BIOS_SEL_CODE64, $L(Long64)
#else
    pushl $L(BiosStartupInfo)
    call *L(BiosStartupInfo) + BSI_ENTRY
    addl $4, %esp
    jmp Pm32Returned
#endif

Pm32BadImage:
    movl $L(BiosMessageImage), %esi
    call Pm32Print
    jmp Pm32Halt

Pm32Returned:
    movl $L(BiosMessageReturn), %esi
    call Pm32Print
Pm32Halt:
    cli
    hlt
    jmp Pm32Halt

#ifdef BIOS_LONG_MODE
/* Turns long mode on with the page tables in BiosSavedCr3; returns in compatibility mode. */
Pm32EnterLong:
    movl %cr4, %eax
    orl $CR4_PAE, %eax
    movl %eax, %cr4
    movl L(BiosSavedCr3), %eax
    movl %eax, %cr3
    movl $0x80000001, %eax
    cpuid
    movl $MSR_EFER, %ecx
    btl $20, %edx
    rdmsr
    jnc 1f
    orl $EFER_NXE, %eax
1:
    orl $EFER_LME, %eax
    wrmsr
    movl %cr0, %eax
    orl $CR0_PG, %eax
    movl %eax, %cr0
    ret
#endif

/* ESI = PE file; maps it at its preferred base, sets CF when it is not usable. */
Pm32MapImage:
    cmpw $0x5A4D, (%esi)
    jne Pm32MapFail
    movl 0x3C(%esi), %ebx
    cmpl $0x1000, %ebx
    jae Pm32MapFail
    addl %esi, %ebx
    cmpl $0x00004550, (%ebx)
    jne Pm32MapFail
    movzwl 0x18(%ebx), %eax
#ifdef BIOS_LONG_MODE
    cmpw $0x020B, %ax
    jne Pm32MapFail
    cmpl $0, 0x34(%ebx)
    jne Pm32MapFail
    movl 0x30(%ebx), %edx
#else
    cmpw $0x010B, %ax
    jne Pm32MapFail
    movl 0x34(%ebx), %edx
#endif
    cmpl $BIOS_FILE_LIMIT, %edx
    jb Pm32MapFail
    movl %edx, L(BiosStartupInfo) + BSI_IMAGE_BASE
    movl 0x50(%ebx), %ecx
    movl %ecx, L(BiosStartupInfo) + BSI_IMAGE_SIZE
    movl 0x28(%ebx), %eax
    addl %edx, %eax
    movl %eax, L(BiosStartupInfo) + BSI_ENTRY

    movl %edx, %edi
    xorl %eax, %eax
    rep stosb

    pushl %esi
    movl %edx, %edi
    movl 0x54(%ebx), %ecx
    rep movsb
    popl %esi

    movzwl 0x06(%ebx), %ebp
    movzwl 0x14(%ebx), %eax
    leal 0x18(%ebx, %eax), %ebx
Pm32MapSection:
    testl %ebp, %ebp
    jz Pm32MapDone
    movl 0x10(%ebx), %ecx
    movl 0x08(%ebx), %eax
    testl %eax, %eax
    jz 1f
    cmpl %eax, %ecx
    jbe 1f
    movl %eax, %ecx
1:
    pushl %esi
    movl %edx, %edi
    addl 0x0C(%ebx), %edi
    addl 0x14(%ebx), %esi
    rep movsb
    popl %esi
    addl $40, %ebx
    decl %ebp
    jmp Pm32MapSection
Pm32MapDone:
    clc
    ret
Pm32MapFail:
    stc
    ret

Pm32BuildIdt:
    movl $IDT_LINEAR, %edi
    movl $L(BiosFaultStubs), %eax
    movl $IDT_ENTRIES, %ecx
1:
    movw %ax, (%edi)
#ifdef BIOS_LONG_MODE
    movw $BIOS_SEL_CODE64, 2(%edi)
#else
    movw $BIOS_SEL_CODE32, 2(%edi)
#endif
    movw $0x8E00, 4(%edi)
    movl %eax, %edx
    shrl $16, %edx
    movw %dx, 6(%edi)
#ifdef BIOS_LONG_MODE
    movl $0, 8(%edi)
    movl $0, 12(%edi)
    addl $16, %edi
#else
    addl $8, %edi
#endif
    addl $16, %eax
    decl %ecx
    jnz 1b
    ret

/* Writes the string at ESI to the screen through the VGA text buffer and to COM1. */
Pm32Print:
    pushl %edi
    movl $0xB8000, %edi
1:
    lodsb
    testb %al, %al
    jz 2f
    call Pm32SerialPut
    cmpb $0x20, %al
    jb 1b
    movb $0x0F, %ah
    movw %ax, (%edi)
    addl $2, %edi
    jmp 1b
2:
    popl %edi
    ret

Pm32SerialPut:
    pushl %eax
    pushl %edx
    pushl %ecx
    movl $0x10000, %ecx
    movw $COM1_STATUS, %dx
1:
    inb %dx, %al
    testb $0x20, %al
    jnz 2f
    decl %ecx
    jnz 1b
2:
    popl %ecx
    popl %edx
    popl %eax
    pushl %edx
    movw $COM1, %dx
    outb %al, %dx
    popl %edx
    ret

#ifdef BIOS_LONG_MODE
    .code64
Long64:
    movw $BIOS_SEL_DATA32, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $(BIOS_STACK_BASE + BIOS_STACK_SIZE), %esp
    lidt L(BiosIdtr)
    subq $0x20, %rsp
    movl $L(BiosStartupInfo), %ecx
    movl L(BiosStartupInfo) + BSI_ENTRY, %eax
    call *%rax
    addq $0x20, %rsp
    pushq $BIOS_SEL_CODE32
    pushq $L(Pm32Returned)
    lretq
#endif

/* 32-bit: cdecl (Interrupt, Registers); 64-bit: Microsoft x64 (RCX = Interrupt, RDX = Registers). */
#ifdef BIOS_LONG_MODE
    .code64
BiosCallGate:
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushfq
    cli
    movb %cl, L(BiosIntNumber)
    movq %rdx, L(BiosSavedRegisters)
    movq %rdx, %rsi
    movl $L(BiosCallRegisters), %edi
    movl $BR_SIZE, %ecx
    rep movsb
    movq %rsp, L(BiosSavedStack)
    movq %cr3, %rax
    movq %rax, L(BiosSavedCr3)
    pushq $BIOS_SEL_CODE32
    pushq $L(GateCompat32)
    lretq

    .code32
GateCompat32:
    movw $BIOS_SEL_DATA32, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl %cr0, %eax
    andl $~CR0_PG, %eax
    movl %eax, %cr0
    movl $MSR_EFER, %ecx
    rdmsr
    andl $~EFER_LME, %eax
    wrmsr
    jmp GateToReal
#else
    .code32
BiosCallGate:
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    pushfl
    cli
    movl 24(%esp), %eax
    movb %al, L(BiosIntNumber)
    movl 28(%esp), %esi
    movl %esi, L(BiosSavedRegisters)
    movl $L(BiosCallRegisters), %edi
    movl $BR_SIZE, %ecx
    rep movsb
    movl %esp, L(BiosSavedStack)
    jmp GateToReal
#endif

    .code32
GateToReal:
    ljmp $BIOS_SEL_CODE16, $R(GatePm16)

    .code16
GatePm16:
    movw $BIOS_SEL_DATA16, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    lidtl R(BiosRealIdtr)
    movl %cr0, %eax
    andl $~CR0_PE, %eax
    movl %eax, %cr0
    ljmp $BIOS_STUB_SEGMENT, $R(GateReal16)

GateReal16:
    movw $BIOS_STUB_SEGMENT, %ax
    movw %ax, %ds
    movw %ax, %ss
    movl $BIOS_STUB_REAL_STACK, %esp
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movw R(BiosCallRegisters) + BR_ES, %es
    movl R(BiosCallRegisters) + BR_EAX, %eax
    movl R(BiosCallRegisters) + BR_EBX, %ebx
    movl R(BiosCallRegisters) + BR_ECX, %ecx
    movl R(BiosCallRegisters) + BR_EDX, %edx
    movl R(BiosCallRegisters) + BR_ESI, %esi
    movl R(BiosCallRegisters) + BR_EDI, %edi
    movl R(BiosCallRegisters) + BR_EBP, %ebp
    movw R(BiosCallRegisters) + BR_DS, %ds
    sti
    .byte 0xCD
BiosIntNumber:
    .byte 0x00
    cli
    movl %eax, %cs:R(BiosCallRegisters) + BR_EAX
    movl %ebx, %cs:R(BiosCallRegisters) + BR_EBX
    movl %ecx, %cs:R(BiosCallRegisters) + BR_ECX
    movl %edx, %cs:R(BiosCallRegisters) + BR_EDX
    movl %esi, %cs:R(BiosCallRegisters) + BR_ESI
    movl %edi, %cs:R(BiosCallRegisters) + BR_EDI
    movl %ebp, %cs:R(BiosCallRegisters) + BR_EBP
    movw %ds, %cs:R(BiosCallRegisters) + BR_DS
    movw %es, %cs:R(BiosCallRegisters) + BR_ES
    pushfl
    popl %eax
    movl %eax, %cs:R(BiosCallRegisters) + BR_EFLAGS
    cld

    movw $BIOS_STUB_SEGMENT, %ax
    movw %ax, %ds
    lgdtl R(BiosGdtr)
    movl %cr0, %eax
    orl $CR0_PE, %eax
    movl %eax, %cr0
    ljmpl $BIOS_SEL_CODE32, $L(GateBack32)

    .code32
GateBack32:
    movw $BIOS_SEL_DATA32, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
#ifdef BIOS_LONG_MODE
    call Pm32EnterLong
    ljmp $BIOS_SEL_CODE64, $L(GateBack64)

    .code64
GateBack64:
    movq L(BiosSavedStack), %rsp
    lidt L(BiosIdtr)
    movl $L(BiosCallRegisters), %esi
    movq L(BiosSavedRegisters), %rdi
    movl $BR_SIZE, %ecx
    rep movsb
    popfq
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    ret
#else
    movl L(BiosSavedStack), %esp
    lidtl L(BiosIdtr)
    movl $L(BiosCallRegisters), %esi
    movl L(BiosSavedRegisters), %edi
    movl $BR_SIZE, %ecx
    rep movsb
    popfl
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
#endif

#ifdef BIOS_LONG_MODE
    .code64
.macro FAULT_NOERROR vector
    .balign 16
    pushq $0
    pushq $\vector
    jmp BiosFaultCommon
.endm
.macro FAULT_ERROR vector
    .balign 16
    pushq $\vector
    jmp BiosFaultCommon
.endm
#else
    .code32
.macro FAULT_NOERROR vector
    .balign 16
    pushl $0
    pushl $\vector
    jmp BiosFaultCommon
.endm
.macro FAULT_ERROR vector
    .balign 16
    pushl $\vector
    jmp BiosFaultCommon
.endm
#endif

    .balign 16
BiosFaultStubs:
    FAULT_NOERROR 0
    FAULT_NOERROR 1
    FAULT_NOERROR 2
    FAULT_NOERROR 3
    FAULT_NOERROR 4
    FAULT_NOERROR 5
    FAULT_NOERROR 6
    FAULT_NOERROR 7
    FAULT_ERROR 8
    FAULT_NOERROR 9
    FAULT_ERROR 10
    FAULT_ERROR 11
    FAULT_ERROR 12
    FAULT_ERROR 13
    FAULT_ERROR 14
    FAULT_NOERROR 15
    FAULT_NOERROR 16
    FAULT_ERROR 17
    FAULT_NOERROR 18
    FAULT_NOERROR 19
    FAULT_NOERROR 20
    FAULT_ERROR 21
    FAULT_NOERROR 22
    FAULT_NOERROR 23
    FAULT_NOERROR 24
    FAULT_NOERROR 25
    FAULT_NOERROR 26
    FAULT_NOERROR 27
    FAULT_NOERROR 28
    FAULT_ERROR 29
    FAULT_ERROR 30
    FAULT_NOERROR 31

#ifdef BIOS_LONG_MODE
    .code64
BiosFaultCommon:
    cli
    movq %rsp, %rbx
    movl $L(BiosMessageFault), %esi
    call Long64Print
    movq (%rbx), %rax
    call Long64PrintHex
    movl $L(BiosMessageAt), %esi
    call Long64Print
    movq 16(%rbx), %rax
    call Long64PrintHex
    movl $L(BiosMessageError), %esi
    call Long64Print
    movq 8(%rbx), %rax
    call Long64PrintHex
    movl $L(BiosMessageCr2), %esi
    call Long64Print
    movq %cr2, %rax
    call Long64PrintHex
1:
    hlt
    jmp 1b

Long64Print:
    lodsb
    testb %al, %al
    jz 1f
    call Long64SerialPut
    jmp Long64Print
1:
    ret

Long64PrintHex:
    pushq %rcx
    pushq %rdx
    movq %rax, %rdx
    movl $16, %ecx
1:
    rolq $4, %rdx
    movl %edx, %eax
    andl $0x0F, %eax
    cmpb $10, %al
    jb 2f
    addb $('A' - '0' - 10), %al
2:
    addb $'0', %al
    call Long64SerialPut
    decl %ecx
    jnz 1b
    popq %rdx
    popq %rcx
    ret

Long64SerialPut:
    pushq %rax
    pushq %rdx
    pushq %rcx
    movl $0x10000, %ecx
    movw $COM1_STATUS, %dx
1:
    inb %dx, %al
    testb $0x20, %al
    jnz 2f
    decl %ecx
    jnz 1b
2:
    popq %rcx
    popq %rdx
    popq %rax
    pushq %rdx
    movw $COM1, %dx
    outb %al, %dx
    popq %rdx
    ret
#else
    .code32
BiosFaultCommon:
    cli
    movl %esp, %ebx
    movl $L(BiosMessageFault), %esi
    call Pm32Print
    movl (%ebx), %eax
    call Pm32PrintHex
    movl $L(BiosMessageAt), %esi
    call Pm32Print
    movl 8(%ebx), %eax
    call Pm32PrintHex
    movl $L(BiosMessageError), %esi
    call Pm32Print
    movl 4(%ebx), %eax
    call Pm32PrintHex
    movl $L(BiosMessageCr2), %esi
    call Pm32Print
    movl %cr2, %eax
    call Pm32PrintHex
    jmp Pm32Halt
#endif

    .code32
/* Prints EAX in hexadecimal on COM1. */
Pm32PrintHex:
    pushl %ecx
    pushl %edx
    movl %eax, %edx
    movl $8, %ecx
1:
    roll $4, %edx
    movl %edx, %eax
    andl $0x0F, %eax
    cmpb $10, %al
    jb 2f
    addb $('A' - '0' - 10), %al
2:
    addb $'0', %al
    call Pm32SerialPut
    decl %ecx
    jnz 1b
    popl %edx
    popl %ecx
    ret

BiosStubCodeEnd:
    .org BIOS_STUB_IMAGE_OFFSET
