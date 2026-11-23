/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     El Torito no-emulation boot image: loads \BOOTMGR from an ISO 9660 medium and starts it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bios.h>

#define ETFS_BASE           0x7C00
#define A(x)                ((x) - Etfs + ETFS_BASE)
#define BLOCK               2048
#define BLOCK_BUFFER        0x9000
#define CHUNK_BLOCKS        16
#define BOUNCE_SEGMENT      (BIOS_TRANSFER_BASE >> 4)
#define HARD_DISK           0x80
#define PROMPT_TICKS        91
#define DOT_TICKS           18

#define VAR_DRIVE           0x0500
#define VAR_LBA             0x0504
#define VAR_FILE_SIZE       0x0508
#define VAR_FILE_OFFSET     0x050C
#define VAR_BYTES           0x0510
#define VAR_LEFT            0x0514
#define VAR_TICK            0x0518
#define VAR_DOT             0x051C
#define MOVE_GDT            0x0530
#define BDA_TICKS           0x046C

    .text
    .code16

Etfs:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $ETFS_BASE, %sp
    sti
    cld
    ljmp $0, $A(EtfsStart)

EtfsStart:
    movb %dl, VAR_DRIVE
    cmpb $HARD_DISK, %dl
    je EtfsFromCd
    call EtfsHardDiskBootable
    jnc EtfsFromCd

    movw $A(EtfsMessagePrompt), %si
    call EtfsPrint
    movl BDA_TICKS, %eax
    movl %eax, VAR_TICK
    movl %eax, VAR_DOT
EtfsWait:
    movb $0x01, %ah
    int $0x16
    jnz EtfsKey
    movl BDA_TICKS, %eax
    movl %eax, %edx
    subl VAR_DOT, %edx
    cmpl $DOT_TICKS, %edx
    jb 1f
    movl %eax, VAR_DOT
    movw $A(EtfsMessageDot), %si
    call EtfsPrint
1:
    movl BDA_TICKS, %eax
    subl VAR_TICK, %eax
    cmpl $PROMPT_TICKS, %eax
    jb EtfsWait
    jmp EtfsChainHardDisk

EtfsKey:
    xorb %ah, %ah
    int $0x16
    movw $A(EtfsMessageNewLine), %si
    call EtfsPrint

EtfsFromCd:
    movl $16, %eax
    movw $BLOCK_BUFFER, %bx
    movw $1, %cx
    call EtfsRead
    cmpl $0x30304443, BLOCK_BUFFER + 1
    jne EtfsBadMedium
    movl BLOCK_BUFFER + 156 + 2, %eax
    movl BLOCK_BUFFER + 156 + 10, %ecx
    addl $(BLOCK - 1), %ecx
    shrl $11, %ecx
    movl %ecx, VAR_LEFT

EtfsDirBlock:
    cmpl $0, VAR_LEFT
    je EtfsMissing
    decl VAR_LEFT
    movl %eax, VAR_LBA
    movw $BLOCK_BUFFER, %bx
    movw $1, %cx
    call EtfsRead
    movw $BLOCK_BUFFER, %di
EtfsRecord:
    movzbw (%di), %bx
    testw %bx, %bx
    jz EtfsNextBlock
    testb $0x02, 25(%di)
    jnz EtfsNextRecord
    movzbw 32(%di), %cx
    cmpw $7, %cx
    jb EtfsNextRecord
    pushw %di
    leaw 33(%di), %si
    movw $A(EtfsName), %di
    movw $7, %dx
1:
    lodsb
    cmpb $'a', %al
    jb 2f
    cmpb $'z', %al
    ja 2f
    subb $0x20, %al
2:
    cmpb (%di), %al
    jne 3f
    incw %di
    decw %dx
    jnz 1b
3:
    popw %di
    jnz EtfsNextRecord
    cmpw $7, %cx
    je EtfsFound
    cmpb $';', 40(%di)
    je EtfsFound
    cmpb $'.', 40(%di)
    je EtfsFound
EtfsNextRecord:
    addw %bx, %di
    cmpw $(BLOCK_BUFFER + BLOCK), %di
    jb EtfsRecord
EtfsNextBlock:
    movl VAR_LBA, %eax
    incl %eax
    jmp EtfsDirBlock

EtfsFound:
    movl 2(%di), %eax
    movl %eax, VAR_LBA
    movl 10(%di), %eax
    movl %eax, VAR_FILE_SIZE
    movl $0, VAR_FILE_OFFSET

    movw $MOVE_GDT, %di
    movw $24, %cx
    xorw %ax, %ax
    rep stosw
    movw $0xFFFF, MOVE_GDT + 0x10
    movl $(0x93000000 | BIOS_TRANSFER_BASE), MOVE_GDT + 0x12
    movw $0xFFFF, MOVE_GDT + 0x18
    movb $0x93, MOVE_GDT + 0x1D

EtfsChunk:
    movl VAR_FILE_SIZE, %ecx
    subl VAR_FILE_OFFSET, %ecx
    addl $(BLOCK - 1), %ecx
    shrl $11, %ecx
    cmpl $CHUNK_BLOCKS, %ecx
    jbe 1f
    movl $CHUNK_BLOCKS, %ecx
1:
    movl %ecx, %eax
    shll $11, %eax
    movl %eax, VAR_BYTES
    movl VAR_LBA, %eax
    addl %ecx, VAR_LBA
    movw $BOUNCE_SEGMENT, %bx
    movw %bx, %es
    xorw %bx, %bx
    call EtfsRead
    movw %bx, %es

    movl VAR_FILE_OFFSET, %edx
    cmpl $BIOS_STUB_SIZE, %edx
    jae EtfsCopyHigh
    movl $BIOS_STUB_SIZE, %ecx
    subl %edx, %ecx
    cmpl VAR_BYTES, %ecx
    jbe 1f
    movl VAR_BYTES, %ecx
1:
    pushw %ds
    shrl $4, %edx
    addw $BIOS_STUB_SEGMENT, %dx
    movw %dx, %es
    movw $BOUNCE_SEGMENT, %dx
    movw %dx, %ds
    xorw %si, %si
    xorw %di, %di
    rep movsb
    popw %ds
    xorw %dx, %dx
    movw %dx, %es

EtfsCopyHigh:
    movl VAR_FILE_OFFSET, %eax
    addl $BIOS_FILE_BASE, %eax
    movw %ax, MOVE_GDT + 0x1A
    shrl $16, %eax
    movb %al, MOVE_GDT + 0x1C
    movb %ah, MOVE_GDT + 0x1F
    movl VAR_BYTES, %ecx
    shrl $1, %ecx
    movw $MOVE_GDT, %si
    movb $0x87, %ah
    int $0x15
    jc EtfsDiskError

    movl VAR_BYTES, %eax
    addl %eax, VAR_FILE_OFFSET
    movl VAR_FILE_OFFSET, %eax
    cmpl VAR_FILE_SIZE, %eax
    jb EtfsChunk

    movl $BIOS_LOADER_MAGIC, %eax
    movb VAR_DRIVE, %dl
    movb $BIOS_MEDIUM_CDROM, %dh
    xorl %ebx, %ebx
    xorl %edi, %edi
    movl VAR_FILE_SIZE, %ecx
    ljmp $BIOS_STUB_SEGMENT, $0

/* Reads CX 2048-byte blocks at EAX into ES:BX. */
EtfsRead:
    pushal
    pushl $0
    pushl %eax
    pushw %es
    pushw %bx
    pushw %cx
    pushw $0x10
    movw %sp, %si
    movb VAR_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc EtfsDiskError
    addw $0x10, %sp
    popal
    ret

EtfsBadMedium:
EtfsDiskError:
    movw $A(EtfsMessageDisk), %si
    jmp EtfsFail
EtfsMissing:
    movw $A(EtfsMessageMissing), %si
EtfsFail:
    call EtfsPrint
EtfsHalt:
    hlt
    jmp EtfsHalt

EtfsPrint:
    lodsb
    testb %al, %al
    jz 1f
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp EtfsPrint
1:
    ret

/* Sets CF when the first hard disk holds a master boot record with an active partition. */
EtfsHardDiskBootable:
    movb $0x41, %ah
    movw $0x55AA, %bx
    movb $HARD_DISK, %dl
    int $0x13
    jc EtfsNotBootable
    cmpw $0xAA55, %bx
    jne EtfsNotBootable
    pushl $0
    pushl $0
    pushw $0
    pushw $BLOCK_BUFFER
    pushw $1
    pushw $0x10
    movw %sp, %si
    movb $0x42, %ah
    movb $HARD_DISK, %dl
    int $0x13
    leaw 0x10(%esp), %sp
    jc EtfsNotBootable
    cmpw $0xAA55, BLOCK_BUFFER + 0x1FE
    jne EtfsNotBootable
    movw $(BLOCK_BUFFER + 0x1BE), %si
    movw $4, %cx
1:
    cmpb $0x80, (%si)
    je 2f
    addw $16, %si
    loop 1b
EtfsNotBootable:
    clc
    ret
2:
    stc
    ret

EtfsMessagePrompt:
    .asciz "Press any key to boot from CD or DVD."
EtfsMessageDot:
    .asciz "."
EtfsMessageNewLine:
    .asciz "\r\n"
EtfsMessageDisk:
    .asciz "CDBOOT: Couldn't read the boot medium\r\n"
EtfsMessageMissing:
    .asciz "CDBOOT: Couldn't find BOOTMGR\r\n"
EtfsName:
    .ascii "BOOTMGR"

/* Starts the hard disk: copies its master boot record over this image from code past the first 512 bytes. */
    .org 0x600
EtfsChainHardDisk:
    movw $A(EtfsMessageNewLine), %si
    call EtfsPrint
    movw $BLOCK_BUFFER, %si
    movw $ETFS_BASE, %di
    movw $0x100, %cx
    rep movsw
    movb $HARD_DISK, %dl
    ljmp $0, $ETFS_BASE

    .org BLOCK
