/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Master boot record: starts the volume boot record of the active partition
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define MBR_BASE        0x0600
#define MBR_LOAD        0x7C00
#define MBR_TABLE       0x01BE
#define A(x)            ((x) - Mbr + MBR_BASE)

    .text
    .code16

Mbr:
    cli
    xorw %ax, %ax
    movw %ax, %ss
    movw $MBR_LOAD, %sp
    movw %ax, %ds
    movw %ax, %es
    sti
    cld
    movw $MBR_LOAD, %si
    movw $MBR_BASE, %di
    movw $0x100, %cx
    rep movsw
    ljmp $0, $A(MbrRelocated)

MbrRelocated:
    movw $(MBR_BASE + MBR_TABLE), %bp
    movw $4, %cx
MbrScan:
    cmpb $0x80, (%bp)
    je MbrActive
    cmpb $0x00, (%bp)
    jne MbrInvalid
    addw $16, %bp
    loop MbrScan
    int $0x18

MbrActive:
    movw %bp, %si
1:
    addw $16, %si
    cmpw $(MBR_BASE + MBR_TABLE + 64), %si
    jae MbrRead
    cmpb $0x00, (%si)
    je 1b

MbrInvalid:
    movw $A(MbrMessageTable), %si
    jmp MbrFail

MbrRead:
    movb $0x41, %ah
    movw $0x55AA, %bx
    int $0x13
    jc MbrLoadError
    cmpw $0xAA55, %bx
    jne MbrLoadError
    testb $0x01, %cl
    jz MbrLoadError

    pushl $0
    pushl 8(%bp)
    pushw $0
    pushw $MBR_LOAD
    pushw $1
    pushw $0x10
    movw %sp, %si
    movb $0x42, %ah
    int $0x13
    jc MbrLoadError
    addw $0x10, %sp

    cmpw $0xAA55, MBR_LOAD + 0x1FE
    jne MbrMissing

    movw %bp, %si
    ljmp $0, $MBR_LOAD

MbrLoadError:
    /* TODO: read the volume boot record through CHS addressing when the INT 13h extensions are missing */
    movw $A(MbrMessageLoad), %si
    jmp MbrFail
MbrMissing:
    movw $A(MbrMessageMissing), %si

MbrFail:
    lodsb
    testb %al, %al
    jz MbrHalt
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp MbrFail
MbrHalt:
    hlt
    jmp MbrHalt

MbrMessageTable:
    .asciz "Invalid partition table"
MbrMessageLoad:
    .asciz "Error loading operating system"
MbrMessageMissing:
    .asciz "Missing operating system"

    .org 440
