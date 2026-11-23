/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     FAT32 volume boot record: loads \BOOTMGR and starts it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bios.h>

#define VBR_BASE            0x7C00
#define A(x)                ((x) - Vbr + VBR_BASE)
#define VBR_EXTRA_SECTOR    14

#define BPB_BYTES_PER_SEC   (VBR_BASE + 0x0B)
#define BPB_SEC_PER_CLUS    (VBR_BASE + 0x0D)
#define BPB_RESERVED        (VBR_BASE + 0x0E)
#define BPB_FAT_COUNT       (VBR_BASE + 0x10)
#define BPB_HIDDEN          (VBR_BASE + 0x1C)
#define BPB_FAT_SIZE        (VBR_BASE + 0x24)
#define BPB_ROOT_CLUSTER    (VBR_BASE + 0x2C)
#define BPB_DRIVE           (VBR_BASE + 0x40)

#define VAR_FILE_OFFSET     0x0500
#define VAR_FILE_SIZE       0x0504
#define VAR_CLUSTER         0x0508
#define VAR_FAT_CACHE       0x050C
#define VAR_DATA_START      0x0510
#define VAR_SECTOR          0x0514
#define VAR_BYTES           0x0518
#define VAR_LEFT            0x051C
#define VAR_CHUNK           0x051E
#define VAR_MAX_CHUNK       0x0520
#define MOVE_GDT            0x0530
#define DIR_BUFFER          0x8000
#define FAT_BUFFER          0x9000
#define BOUNCE_SEGMENT      (BIOS_TRANSFER_BASE >> 4)
#define END_OF_CHAIN        0x0FFFFFF8

    .text
    .code16

Vbr:
    jmp VbrStart
    nop
    .org 0x5A

VbrStart:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $VBR_BASE, %sp
    sti
    cld
    movb %dl, BPB_DRIVE

    movb $0x41, %ah
    movw $0x55AA, %bx
    int $0x13
    jc VbrDiskError
    cmpw $0xAA55, %bx
    jne VbrDiskError
    testb $0x01, %cl
    jz VbrDiskError

    movl $VBR_EXTRA_SECTOR, %eax
    movw $(VBR_BASE + 0x200), %bx
    movw $1, %cx
    call VbrRead
    jmp VbrStage2

/* Reads CX sectors at volume sector EAX into ES:BX. */
VbrRead:
    pushal
    pushl $0
    addl BPB_HIDDEN, %eax
    pushl %eax
    pushw %es
    pushw %bx
    pushw %cx
    pushw $0x10
    movw %sp, %si
    movb BPB_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc VbrDiskError
    addw $0x10, %sp
    popal
    ret

VbrDiskError:
    /* TODO: read through CHS addressing when the INT 13h extensions are missing */
    movw $A(VbrMessageDisk), %si
VbrFail:
    call VbrPrint
    movw $A(VbrMessageRestart), %si
    call VbrPrint
VbrHalt:
    hlt
    jmp VbrHalt

VbrPrint:
    lodsb
    testb %al, %al
    jz 1f
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp VbrPrint
1:
    ret

/* EAX = first volume sector of the cluster in VAR_CLUSTER. */
VbrClusterSector:
    movl VAR_CLUSTER, %eax
    subl $2, %eax
    movzbl BPB_SEC_PER_CLUS, %ecx
    mull %ecx
    addl VAR_DATA_START, %eax
    ret

/* EAX = cluster -> EAX = the next cluster of its chain. */
VbrNextCluster:
    shll $2, %eax
    movzwl BPB_BYTES_PER_SEC, %ecx
    xorl %edx, %edx
    divl %ecx
    movzwl BPB_RESERVED, %ecx
    addl %ecx, %eax
    cmpl VAR_FAT_CACHE, %eax
    je 1f
    movl %eax, VAR_FAT_CACHE
    pushw %dx
    movw $FAT_BUFFER, %bx
    movw $1, %cx
    call VbrRead
    popw %dx
1:
    movw %dx, %bx
    movl FAT_BUFFER(%bx), %eax
    andl $0x0FFFFFFF, %eax
    ret

VbrMessageDisk:
    .asciz "\r\nA disk read error occurred"
VbrMessageMissing:
    .asciz "\r\nBOOTMGR is missing"
VbrMessageRestart:
    .asciz "\r\nPress Ctrl+Alt+Del to restart\r\n"
VbrName:
    .ascii "BOOTMGR    "

    .org 0x1FE
    .word 0xAA55

/* Second sector, read from reserved sector 14 of the volume. */
VbrStage2:
    movzwl BPB_BYTES_PER_SEC, %ecx
    movl $0x8000, %eax
    xorl %edx, %edx
    divl %ecx
    movw %ax, VAR_MAX_CHUNK

    movzbl BPB_FAT_COUNT, %eax
    mull BPB_FAT_SIZE
    movzwl BPB_RESERVED, %ecx
    addl %ecx, %eax
    movl %eax, VAR_DATA_START
    movl $0xFFFFFFFF, VAR_FAT_CACHE

    movl BPB_ROOT_CLUSTER, %eax
VbrDirCluster:
    movl %eax, VAR_CLUSTER
    call VbrClusterSector
    movzbw BPB_SEC_PER_CLUS, %cx
VbrDirSector:
    pushw %cx
    pushl %eax
    movw $DIR_BUFFER, %bx
    movw $1, %cx
    call VbrRead
    movw $DIR_BUFFER, %di
    movw BPB_BYTES_PER_SEC, %dx
    shrw $5, %dx
VbrDirEntry:
    cmpb $0x00, (%di)
    je VbrNotFound
    testb $0x18, 11(%di)
    jnz VbrDirNext
    pushw %di
    movw $A(VbrName), %si
    movw $11, %cx
    repe cmpsb
    popw %di
    je VbrFound
VbrDirNext:
    addw $32, %di
    decw %dx
    jnz VbrDirEntry
    popl %eax
    incl %eax
    popw %cx
    loop VbrDirSector

    movl VAR_CLUSTER, %eax
    call VbrNextCluster
    cmpl $END_OF_CHAIN, %eax
    jb VbrDirCluster

VbrNotFound:
    movw $A(VbrMessageMissing), %si
    jmp VbrFail

VbrFound:
    addw $6, %sp
    movl 0x1C(%di), %ecx
    movl %ecx, VAR_FILE_SIZE
    movw 0x14(%di), %ax
    shll $16, %eax
    movw 0x1A(%di), %ax
    movl $0, VAR_FILE_OFFSET

    movw $MOVE_GDT, %di
    movw $24, %cx
    xorw %bx, %bx
1:
    movw %bx, (%di)
    addw $2, %di
    loop 1b
    movw $0xFFFF, MOVE_GDT + 0x10
    movl $(0x93000000 | BIOS_TRANSFER_BASE), MOVE_GDT + 0x12
    movw $0xFFFF, MOVE_GDT + 0x18
    movb $0x93, MOVE_GDT + 0x1D

VbrFileCluster:
    movl %eax, VAR_CLUSTER
    call VbrClusterSector
    movl %eax, VAR_SECTOR
    movzbw BPB_SEC_PER_CLUS, %ax
    movw %ax, VAR_LEFT

VbrFileChunk:
    movw VAR_LEFT, %cx
    cmpw VAR_MAX_CHUNK, %cx
    jbe 1f
    movw VAR_MAX_CHUNK, %cx
1:
    movw %cx, VAR_CHUNK
    movl VAR_SECTOR, %eax
    movw $BOUNCE_SEGMENT, %bx
    movw %bx, %es
    xorw %bx, %bx
    call VbrRead
    movw %bx, %es

    movzwl VAR_CHUNK, %eax
    movzwl BPB_BYTES_PER_SEC, %ecx
    mull %ecx
    movl %eax, VAR_BYTES

    movl VAR_FILE_OFFSET, %edx
    cmpl $BIOS_STUB_SIZE, %edx
    jae VbrCopyHigh
    movl $BIOS_STUB_SIZE, %ecx
    subl %edx, %ecx
    cmpl %eax, %ecx
    jbe 1f
    movl %eax, %ecx
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

VbrCopyHigh:
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
    jc VbrDiskError

    movl VAR_BYTES, %eax
    addl %eax, VAR_FILE_OFFSET
    movzwl VAR_CHUNK, %eax
    addl %eax, VAR_SECTOR
    subw %ax, VAR_LEFT
    movl VAR_FILE_OFFSET, %eax
    cmpl VAR_FILE_SIZE, %eax
    jae VbrStart2
    cmpw $0, VAR_LEFT
    jne VbrFileChunk
    movl VAR_CLUSTER, %eax
    call VbrNextCluster
    cmpl $END_OF_CHAIN, %eax
    jb VbrFileCluster

VbrStart2:
    movl $BIOS_LOADER_MAGIC, %eax
    movb BPB_DRIVE, %dl
    movb $BIOS_MEDIUM_DISK, %dh
    movl BPB_HIDDEN, %ebx
    xorl %edi, %edi
    movl VAR_FILE_SIZE, %ecx
    ljmp $BIOS_STUB_SEGMENT, $0

    .org 0x400
