/*
 * PROJECT:     LiberNT FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Long mode exception stubs for the PC-AT (BIOS) loader
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.code64

EXTERN FrLdrAmd64TrapHandler:PROC

MACRO(TrapStub, Vector)
    push 0
    push Vector
    jmp FrLdrTrapCommon
ENDM

MACRO(TrapStubWithError, Vector)
    push Vector
    jmp FrLdrTrapCommon
ENDM

FrLdrTrap00: TrapStub 0
FrLdrTrap01: TrapStub 1
FrLdrTrap02: TrapStub 2
FrLdrTrap03: TrapStub 3
FrLdrTrap04: TrapStub 4
FrLdrTrap05: TrapStub 5
FrLdrTrap06: TrapStub 6
FrLdrTrap07: TrapStub 7
FrLdrTrap08: TrapStubWithError 8
FrLdrTrap09: TrapStub 9
FrLdrTrap10: TrapStubWithError 10
FrLdrTrap11: TrapStubWithError 11
FrLdrTrap12: TrapStubWithError 12
FrLdrTrap13: TrapStubWithError 13
FrLdrTrap14: TrapStubWithError 14
FrLdrTrap15: TrapStub 15
FrLdrTrap16: TrapStub 16
FrLdrTrap17: TrapStubWithError 17
FrLdrTrap18: TrapStub 18
FrLdrTrap19: TrapStub 19
FrLdrTrap20: TrapStub 20
FrLdrTrap21: TrapStubWithError 21
FrLdrTrap22: TrapStub 22
FrLdrTrap23: TrapStub 23
FrLdrTrap24: TrapStub 24
FrLdrTrap25: TrapStub 25
FrLdrTrap26: TrapStub 26
FrLdrTrap27: TrapStub 27
FrLdrTrap28: TrapStub 28
FrLdrTrap29: TrapStubWithError 29
FrLdrTrap30: TrapStubWithError 30
FrLdrTrap31: TrapStub 31

FrLdrTrapCommon:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rcx, rsp
    sub rsp, 32
    call FrLdrAmd64TrapHandler

FrLdrTrapHalt:
    cli
    hlt
    jmp FrLdrTrapHalt

PUBLIC FrLdrTrapStubTable
FrLdrTrapStubTable:
    .quad FrLdrTrap00, FrLdrTrap01, FrLdrTrap02, FrLdrTrap03
    .quad FrLdrTrap04, FrLdrTrap05, FrLdrTrap06, FrLdrTrap07
    .quad FrLdrTrap08, FrLdrTrap09, FrLdrTrap10, FrLdrTrap11
    .quad FrLdrTrap12, FrLdrTrap13, FrLdrTrap14, FrLdrTrap15
    .quad FrLdrTrap16, FrLdrTrap17, FrLdrTrap18, FrLdrTrap19
    .quad FrLdrTrap20, FrLdrTrap21, FrLdrTrap22, FrLdrTrap23
    .quad FrLdrTrap24, FrLdrTrap25, FrLdrTrap26, FrLdrTrap27
    .quad FrLdrTrap28, FrLdrTrap29, FrLdrTrap30, FrLdrTrap31

PUBLIC FrLdrIdtPointer
FrLdrIdtPointer:
    .word 0
    .quad 0

END
