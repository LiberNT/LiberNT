/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTFS volume boot record: loads \BOOTMGR and starts it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bios.h>

#define VBR_BASE            0x7C00
#define A(x)                ((x) - Vbr + VBR_BASE)
#define VBR_BYTES           0x2000

#define BPB_BYTES_PER_SEC   (VBR_BASE + 0x0B)
#define BPB_SEC_PER_CLUS    (VBR_BASE + 0x0D)
#define BPB_HIDDEN          (VBR_BASE + 0x1C)
#define BPB_MFT_LCN         (VBR_BASE + 0x30)
#define BPB_CLUS_PER_FRS    (VBR_BASE + 0x40)
#define BPB_CLUS_PER_INDEX  (VBR_BASE + 0x44)

#define VAR_DRIVE           0x0500
#define VAR_SPC             0x0504
#define VAR_CLUSTER_BYTES   0x0508
#define VAR_FRS_BYTES       0x050C
#define VAR_INDEX_BYTES     0x0510
#define VAR_INDEX_UNIT      0x0514
#define VAR_ALLOCATION      0x0518
#define VAR_RUNS            0x051A
#define VAR_FILE_SIZE       0x051C
#define VAR_FILE_OFFSET     0x0520
#define VAR_BYTES           0x0524
#define VAR_SECTOR          0x0528
#define VAR_RUN_LEFT        0x052C
#define VAR_MAX_CHUNK       0x0530
#define VAR_CHUNK           0x0534
#define VAR_S_OFFSET        0x0538
#define VAR_S_LEFT          0x053C
#define VAR_MAP_TARGET      0x0570
#define VAR_MAP_VCN         0x0574
#define VAR_MAP_LCN         0x0578
#define VAR_MAP_LENGTH      0x057C
#define MOVE_GDT            0x0540
#define MFT_RUNS            0x0600
#define MFT_RUNS_MAX        0x01FF
#define RECORD_BUFFER       0xA000
#define FILE_BUFFER         0xB000
#define INDEX_BUFFER        0xC000
#define MAX_RECORD          0x1000
#define MAX_INDEX           0x4000
#define BOUNCE_SEGMENT      (BIOS_TRANSFER_BASE >> 4)

#define SIGNATURE_FILE      0x454C4946
#define SIGNATURE_INDX      0x58444E49
#define ATTRIBUTE_END       0xFFFFFFFF
#define ATTRIBUTE_DATA      0x80
#define ATTRIBUTE_INDEX_ROOT 0x90
#define ATTRIBUTE_ALLOCATION 0xA0
#define ROOT_RECORD         5
#define ENTRY_SUBNODE       0x01
#define ENTRY_LAST          0x02
#define DATA_NOT_PLAIN      0xC0FF

    .text
    .code16

Vbr:
    jmp VbrStart
    nop
    .org 0x54

VbrStart:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $VBR_BASE, %sp
    sti
    cld
    movb %dl, VAR_DRIVE

    movb $0x41, %ah
    movw $0x55AA, %bx
    int $0x13
    jc VbrDiskError
    cmpw $0xAA55, %bx
    jne VbrDiskError
    testb $0x01, %cl
    jz VbrDiskError

    movw BPB_BYTES_PER_SEC, %cx
    movw $VBR_BYTES, %ax
    xorw %dx, %dx
    divw %cx
    movw %cx, %bx
    addw $VBR_BASE, %bx
    movw %ax, %cx
    decw %cx
    movl $1, %eax
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
    movb VAR_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc VbrDiskError
    addw $0x10, %sp
    popal
    ret

VbrDiskError:
    /* TODO: read through CHS addressing when the INT 13h extensions are missing */
    movw $A(VbrMessageDisk), %si
    jmp VbrFail
VbrMissing:
    movw $A(VbrMessageMissing), %si
    jmp VbrFail
VbrCompressed:
    movw $A(VbrMessageCompressed), %si
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

VbrMessageDisk:
    .asciz "\r\nA disk read error occurred"
VbrMessageMissing:
    .asciz "\r\nBOOTMGR is missing"
VbrMessageCompressed:
    .asciz "\r\nBOOTMGR is compressed"
VbrMessageRestart:
    .asciz "\r\nPress Ctrl+Alt+Del to restart\r\n"
VbrName:
    .ascii "BOOTMGR"

    .org 0x1FE
    .word 0xAA55

/* Sectors 1 to 15 of the volume, the rest of the $Boot file. */
VbrStage2:
    movzbl BPB_SEC_PER_CLUS, %eax
    movl %eax, VAR_SPC
    movzwl BPB_BYTES_PER_SEC, %ecx
    mull %ecx
    movl %eax, VAR_CLUSTER_BYTES

    movsbl BPB_CLUS_PER_FRS, %ecx
    call VbrUnitBytes
    movl %eax, VAR_FRS_BYTES
    cmpl $MAX_RECORD, %eax
    ja VbrMissing
    movsbl BPB_CLUS_PER_INDEX, %ecx
    call VbrUnitBytes
    movl %eax, VAR_INDEX_BYTES
    cmpl $MAX_INDEX, %eax
    ja VbrMissing
    movl VAR_CLUSTER_BYTES, %ecx
    cmpl %ecx, %eax
    jae 1f
    movl $512, %ecx
1:
    movl %ecx, VAR_INDEX_UNIT

    movzwl BPB_BYTES_PER_SEC, %ecx
    movl $0x8000, %eax
    xorl %edx, %edx
    divl %ecx
    movl %eax, VAR_MAX_CHUNK

    /* Read $MFT record 0 through a run that starts at the MFT LCN, then keep the real $MFT runs. */
    movw $MFT_RUNS, %di
    movb $0x44, (%di)
    movl $0x7FFFFFFF, 1(%di)
    movl BPB_MFT_LCN, %eax
    movl %eax, 5(%di)
    movb $0, 9(%di)
    xorl %eax, %eax
    movw $RECORD_BUFFER, %bx
    call VbrReadRecord
    movl $ATTRIBUTE_DATA, %eax
    call VbrFindAttribute
    jc VbrMissing
    cmpb $1, 8(%di)
    jne VbrMissing
    movw 0x20(%di), %si
    movw 4(%di), %cx
    subw %si, %cx
    addw %di, %si
    cmpw $MFT_RUNS_MAX, %cx
    jbe 1f
    movw $MFT_RUNS_MAX, %cx
1:
    movw $MFT_RUNS, %di
    rep movsb
    movb $0, (%di)

    /* Walk the B+ tree of the root directory index down to BOOTMGR. */
    movl $ROOT_RECORD, %eax
    movw $RECORD_BUFFER, %bx
    call VbrReadRecord
    movw $0, VAR_ALLOCATION
    movl $ATTRIBUTE_ALLOCATION, %eax
    call VbrFindAttribute
    jc 1f
    movw 0x20(%di), %ax
    addw %di, %ax
    movw %ax, VAR_ALLOCATION
1:
    movl $ATTRIBUTE_INDEX_ROOT, %eax
    call VbrFindAttribute
    jc VbrMissing
    addw 0x14(%di), %di
    leaw 0x10(%di), %si
    addw (%si), %si

VbrSearchEntry:
    testb $ENTRY_LAST, 0x0C(%si)
    jnz VbrSearchDown
    call VbrCompareName
    testb %al, %al
    jz VbrFoundEntry
    cmpb $2, %al
    je VbrSearchDown
    movw 8(%si), %ax
    testw %ax, %ax
    jz VbrMissing
    addw %ax, %si
    jmp VbrSearchEntry

VbrSearchDown:
    testb $ENTRY_SUBNODE, 0x0C(%si)
    jz VbrMissing
    movw VAR_ALLOCATION, %cx
    testw %cx, %cx
    jz VbrMissing
    movw %cx, VAR_RUNS
    movw 8(%si), %bx
    addw %si, %bx
    movl -8(%bx), %eax
    mull VAR_INDEX_UNIT
    movl VAR_INDEX_BYTES, %ecx
    movw $INDEX_BUFFER, %bx
    call VbrReadStream
    movw $INDEX_BUFFER, %si
    cmpl $SIGNATURE_INDX, (%si)
    jne VbrMissing
    call VbrFixup
    addw $0x18, %si
    addw (%si), %si
    jmp VbrSearchEntry

VbrFoundEntry:
    movl (%si), %eax
    movw $FILE_BUFFER, %bx
    call VbrReadRecord
    movw $FILE_BUFFER, %si
    movb 0x16(%si), %al
    andb $0x03, %al
    cmpb $0x01, %al
    jne VbrMissing
    movl $ATTRIBUTE_DATA, %eax
    call VbrFindAttribute
    /* TODO: follow $ATTRIBUTE_LIST when the $DATA of BOOTMGR lives in an extension record */
    jc VbrMissing
    cmpb $1, 8(%di)
    jne VbrMissing
    testw $DATA_NOT_PLAIN, 0x0C(%di)
    jnz VbrCompressed
    cmpl $0, 0x34(%di)
    jne VbrMissing
    movl 0x30(%di), %eax
    testl %eax, %eax
    jz VbrMissing
    movl %eax, VAR_FILE_SIZE
    movw 0x20(%di), %si
    addw %di, %si

    movl $0, VAR_FILE_OFFSET
    movl $0, VAR_MAP_LCN
    pushw %si
    movw $MOVE_GDT, %di
    movw $24, %cx
    xorw %ax, %ax
    rep stosw
    popw %si
    movw $0xFFFF, MOVE_GDT + 0x10
    movl $(0x93000000 | BIOS_TRANSFER_BASE), MOVE_GDT + 0x12
    movw $0xFFFF, MOVE_GDT + 0x18
    movb $0x93, MOVE_GDT + 0x1D

VbrFileRun:
    movb (%si), %cl
    testb %cl, %cl
    jz VbrMissing
    incw %si
    movb %cl, %ch
    shrb $4, %ch
    andb $0x0F, %cl
    xorb %dh, %dh
    call VbrReadNumber
    pushw %cx
    mull VAR_SPC
    popw %cx
    movl %eax, VAR_RUN_LEFT
    movb %ch, %cl
    testb %cl, %cl
    jz VbrCompressed
    movb $1, %dh
    call VbrReadNumber
    addl %eax, VAR_MAP_LCN
    movl VAR_MAP_LCN, %eax
    mull VAR_SPC
    movl %eax, VAR_SECTOR
    pushw %si

VbrFileChunk:
    movl VAR_RUN_LEFT, %ecx
    cmpl VAR_MAX_CHUNK, %ecx
    jbe 1f
    movl VAR_MAX_CHUNK, %ecx
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
    subl %eax, VAR_RUN_LEFT
    movl VAR_FILE_OFFSET, %eax
    cmpl VAR_FILE_SIZE, %eax
    jae VbrStart2
    cmpl $0, VAR_RUN_LEFT
    jne VbrFileChunk
    popw %si
    jmp VbrFileRun

VbrStart2:
    movl $BIOS_LOADER_MAGIC, %eax
    movb VAR_DRIVE, %dl
    movb $BIOS_MEDIUM_DISK, %dh
    movl BPB_HIDDEN, %ebx
    xorl %edi, %edi
    movl VAR_FILE_SIZE, %ecx
    ljmp $BIOS_STUB_SEGMENT, $0

/* ECX = clusters per unit, or minus the log2 of its bytes -> EAX = bytes per unit. */
VbrUnitBytes:
    testl %ecx, %ecx
    js 1f
    movl VAR_CLUSTER_BYTES, %eax
    mull %ecx
    ret
1:
    negl %ecx
    movl $1, %eax
    shll %cl, %eax
    ret

/* Reads file record EAX of the $MFT into BX and applies its update sequence. */
VbrReadRecord:
    pushw %bx
    mull VAR_FRS_BYTES
    movw $MFT_RUNS, VAR_RUNS
    movl VAR_FRS_BYTES, %ecx
    call VbrReadStream
    popw %si
    cmpl $SIGNATURE_FILE, (%si)
    jne VbrMissing
    call VbrFixup
    ret

/* Reads ECX bytes at byte EAX of the stream whose runs VAR_RUNS points to into ES:BX, one sector at a time. */
VbrReadStream:
    movl %eax, VAR_S_OFFSET
    movl %ecx, VAR_S_LEFT
1:
    movl VAR_S_OFFSET, %eax
    xorl %edx, %edx
    divl VAR_CLUSTER_BYTES
    pushl %edx
    movw VAR_RUNS, %si
    call VbrMapVcn
    mull VAR_SPC
    movl %eax, %ecx
    popl %eax
    xorl %edx, %edx
    movzwl BPB_BYTES_PER_SEC, %edi
    divl %edi
    addl %ecx, %eax
    movw $1, %cx
    call VbrRead
    movzwl BPB_BYTES_PER_SEC, %eax
    addw %ax, %bx
    addl %eax, VAR_S_OFFSET
    subl %eax, VAR_S_LEFT
    ja 1b
    ret

/* EAX = VCN in the runs at SI -> EAX = its LCN; a VCN past the runs or in a sparse run fails the boot. */
VbrMapVcn:
    pushw %cx
    pushw %dx
    pushw %si
    movl %eax, VAR_MAP_TARGET
    xorl %eax, %eax
    movl %eax, VAR_MAP_VCN
    movl %eax, VAR_MAP_LCN
1:
    movb (%si), %cl
    testb %cl, %cl
    jz VbrMissing
    incw %si
    movb %cl, %ch
    shrb $4, %ch
    andb $0x0F, %cl
    xorb %dh, %dh
    call VbrReadNumber
    movl %eax, VAR_MAP_LENGTH
    movb %ch, %cl
    testb %cl, %cl
    jz 2f
    movb $1, %dh
    call VbrReadNumber
    addl %eax, VAR_MAP_LCN
2:
    movl VAR_MAP_TARGET, %eax
    subl VAR_MAP_VCN, %eax
    cmpl VAR_MAP_LENGTH, %eax
    jae 3f
    testb %ch, %ch
    jz VbrMissing
    addl VAR_MAP_LCN, %eax
    popw %si
    popw %dx
    popw %cx
    ret
3:
    movl VAR_MAP_LENGTH, %eax
    addl %eax, VAR_MAP_VCN
    jmp 1b

/* Reads the CL-byte little-endian number at SI into EAX, sign-extended when DH is set, and moves SI past it. */
VbrReadNumber:
    pushw %bx
    pushw %cx
    movzbw %cl, %bx
    addw %si, %bx
    pushw %bx
    cmpb $4, %cl
    jbe 1f
    movb $4, %cl
1:
    movzbw %cl, %bx
    addw %si, %bx
    testb %dh, %dh
    jz 2f
    movsbl -1(%bx), %eax
    jmp 3f
2:
    movzbl -1(%bx), %eax
3:
    decw %bx
    decb %cl
    jz 5f
4:
    shll $8, %eax
    movb -1(%bx), %al
    decw %bx
    decb %cl
    jnz 4b
5:
    popw %si
    popw %cx
    popw %bx
    ret

/* Applies the update sequence of the record or index block at SI. */
VbrFixup:
    pushal
    movw 4(%si), %bx
    addw %si, %bx
    movw 6(%si), %cx
    decw %cx
    jz 2f
    movw (%bx), %ax
    leaw 510(%si), %di
1:
    addw $2, %bx
    cmpw %ax, (%di)
    jne VbrDiskError
    movw (%bx), %dx
    movw %dx, (%di)
    addw $512, %di
    loop 1b
2:
    popal
    ret

/* DI = the attribute of type EAX in the file record at SI, unnamed for $DATA; CF when there is none. */
VbrFindAttribute:
    movw %si, %di
    addw 0x14(%si), %di
1:
    cmpl $ATTRIBUTE_END, (%di)
    je 3f
    cmpw $0, 4(%di)
    je 3f
    cmpl %eax, (%di)
    jne 2f
    cmpl $ATTRIBUTE_DATA, %eax
    jne 4f
    cmpb $0, 9(%di)
    je 4f
2:
    addw 4(%di), %di
    jmp 1b
3:
    stc
    ret
4:
    clc
    ret

/* Compares "BOOTMGR" with the file name of the index entry at SI in NTFS collation: AL = 0 equal, 1 after it, 2 before it. */
VbrCompareName:
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %si
    pushw %di
    movzbw 0x50(%si), %dx
    leaw 0x52(%si), %si
    movw $A(VbrName), %di
    movw $7, %cx
    cmpw %dx, %cx
    jbe 1f
    movw %dx, %cx
1:
    testw %cx, %cx
    jz 4f
2:
    lodsw
    cmpw $'a', %ax
    jb 3f
    cmpw $'z', %ax
    ja 3f
    subw $0x20, %ax
3:
    movzbw (%di), %bx
    incw %di
    cmpw %ax, %bx
    jb 6f
    ja 5f
    loop 2b
4:
    cmpw $7, %dx
    ja 6f
    jb 5f
    xorb %al, %al
    jmp 7f
5:
    movb $1, %al
    jmp 7f
6:
    movb $2, %al
7:
    popw %di
    popw %si
    popw %dx
    popw %cx
    popw %bx
    ret

    .org VBR_BYTES
