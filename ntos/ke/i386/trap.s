/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 trap, interrupt and system call entry stubs
 * COPYRIGHT:   Copyright Alex Ionescu <alex@relsoft.net>
 *              Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>
#include <internal/i386/asmmacro.S>

MACRO(GENERATE_IDT_STUB, Vector)
#ifdef _USE_ML
idt _KiUnexpectedInterrupt&Vector, INT_32_DPL0
#else
idt _KiUnexpectedInterrupt\Vector, INT_32_DPL0
#endif
ENDM

MACRO(GENERATE_INT_HANDLER, Vector)

#ifdef _USE_ML
_KiUnexpectedInterrupt&Vector:
#else
_KiUnexpectedInterrupt\Vector:
#endif

    push (Vector - 128)
    jmp _KiEndUnexpectedRange@0

ENDM

.data
ASSUME CS:nothing

.align 16

PUBLIC _KiIdt
_KiIdt:

idt _KiTrap00,         INT_32_DPL0
idt _KiTrap01,         INT_32_DPL0
idt _KiTrap02,         INT_32_DPL0
idt _KiTrap03,         INT_32_DPL3
idt _KiTrap04,         INT_32_DPL3
idt _KiTrap05,         INT_32_DPL0
idt _KiTrap06,         INT_32_DPL0
idt _KiTrap07,         INT_32_DPL0
idt _KiTrap08,         INT_32_DPL0
idt _KiTrap09,         INT_32_DPL0
idt _KiTrap0A,         INT_32_DPL0
idt _KiTrap0B,         INT_32_DPL0
idt _KiTrap0C,         INT_32_DPL0
idt _KiTrap0D,         INT_32_DPL0
idt _KiTrap0E,         INT_32_DPL0
idt _KiTrap0F,         INT_32_DPL0
idt _KiTrap10,         INT_32_DPL0
idt _KiTrap11,         INT_32_DPL0
idt _KiTrap0F,         INT_32_DPL0
idt _KiTrap0F,         INT_32_DPL0
REPEAT 21
idt _KiTrap0F,         INT_32_DPL0
ENDR
idt _KiRaiseSecurityCheckFailure, INT_32_DPL3

idt _KiGetTickCount,   INT_32_DPL3
idt _KiCallbackReturn, INT_32_DPL3
idt _KiRaiseAssertion, INT_32_DPL3
idt _KiDebugService,   INT_32_DPL3
idt _KiSystemService,  INT_32_DPL3
idt _KiTrap0F,         INT_32_DPL0
i = HEX(30)
REPEAT 208
    GENERATE_IDT_STUB %i
    i = i + 1
ENDR

PUBLIC _KiIdtDescriptor
_KiIdtDescriptor:
    .short 0
    .short HEX(7FF)
    .long _KiIdt

PUBLIC _KiUnexpectedEntrySize
_KiUnexpectedEntrySize:
    .long _KiUnexpectedInterrupt49 - _KiUnexpectedInterrupt48

.code

PUBLIC _KiStartUnexpectedRange@0
_KiStartUnexpectedRange@0:
i = HEX(30)
REPEAT 208
    GENERATE_INT_HANDLER %i
    i = i + 1
ENDR

TRAP_ENTRY KiTrap00, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap01, KI_PUSH_FAKE_ERROR_CODE
TASK_ENTRY KiTrap02, KI_NMI
TRAP_ENTRY KiTrap03, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap04, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap05, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap06, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap07, KI_PUSH_FAKE_ERROR_CODE
TASK_ENTRY KiTrap08, 0
TRAP_ENTRY KiTrap09, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap0A, 0
TRAP_ENTRY KiTrap0B, 0
TRAP_ENTRY KiTrap0C, 0
TRAP_ENTRY KiTrap0D, 0
TRAP_ENTRY KiTrap0E, 0
TRAP_ENTRY KiTrap0F, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap10, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap11, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiTrap13, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiRaiseSecurityCheckFailure, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiGetTickCount, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiCallbackReturn, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiRaiseAssertion, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiDebugService, KI_PUSH_FAKE_ERROR_CODE
TRAP_ENTRY KiUnexpectedInterruptTail, 0

ALIGN 4
EXTERN @KiInterruptTemplateHandler@8:PROC
PUBLIC _KiInterruptTemplate
_KiInterruptTemplate:
    CFI_STARTPROC
    KiEnterTrap KI_PUSH_FAKE_ERROR_CODE
PUBLIC _KiInterruptTemplate2ndDispatch
_KiInterruptTemplate2ndDispatch:
    mov edx, 0
PUBLIC _KiInterruptTemplateObject
_KiInterruptTemplateObject:
#ifdef _USE_ML
    mov eax, offset @KiInterruptTemplateHandler@8
#else
    mov eax, offset "@KiInterruptTemplateHandler@8"
#endif
    jmp eax
PUBLIC _KiInterruptTemplateDispatch
_KiInterruptTemplateDispatch:
    CFI_ENDPROC

EXTERN @KiSystemServiceHandler@8:PROC
PUBLIC _KiSystemService
.PROC _KiSystemService
    FPO 0, 0, 0, 0, 1, FRAME_TRAP
    KiEnterTrap (KI_PUSH_FAKE_ERROR_CODE OR KI_NONVOLATILES_ONLY OR KI_DONT_SAVE_SEGS)
    KiCallHandler @KiSystemServiceHandler@8
.ENDP

PUBLIC _KiFastCallEntry
.PROC _KiFastCallEntry
    FPO 0, 0, 0, 0, 1, FRAME_TRAP
    KiEnterTrap (KI_FAST_SYSTEM_CALL OR KI_NONVOLATILES_ONLY OR KI_DONT_SAVE_SEGS)
    KiCallHandler @KiSystemServiceHandler@8
.ENDP

PUBLIC _KiFastCallEntryWithSingleStep
.PROC _KiFastCallEntryWithSingleStep
    FPO 0, 0, 0, 0, 1, FRAME_TRAP
    KiEnterTrap (KI_FAST_SYSTEM_CALL OR KI_NONVOLATILES_ONLY OR KI_DONT_SAVE_SEGS)
    or dword ptr [ecx + KTRAP_FRAME_EFLAGS], EFLAGS_TF
    KiCallHandler @KiSystemServiceHandler@8
.ENDP

PUBLIC _KiEndUnexpectedRange@0
_KiEndUnexpectedRange@0:
    add dword ptr[esp], 128
    jmp _KiUnexpectedInterruptTail

KiTrapExitStub KiSystemCallReturn,        (KI_RESTORE_EAX OR KI_RESTORE_EFLAGS OR KI_EXIT_JMP)
KiTrapExitStub KiSystemCallSysExitReturn, (KI_RESTORE_EAX OR KI_RESTORE_FS OR KI_RESTORE_EFLAGS OR KI_EXIT_SYSCALL)
KiTrapExitStub KiSystemCallTrapReturn,    (KI_RESTORE_EAX OR KI_RESTORE_FS OR KI_EXIT_IRET)

KiTrapExitStub KiEditedTrapReturn,        (KI_RESTORE_VOLATILES OR KI_RESTORE_EFLAGS OR KI_EDITED_FRAME OR KI_EXIT_RET)
KiTrapExitStub KiTrapReturn,              (KI_RESTORE_VOLATILES OR KI_RESTORE_SEGMENTS OR KI_EXIT_IRET)
KiTrapExitStub KiTrapReturnNoSegments,    (KI_RESTORE_VOLATILES OR KI_EXIT_IRET)
KiTrapExitStub KiTrapReturnNoSegmentsRet8,(KI_RESTORE_VOLATILES OR KI_RESTORE_EFLAGS OR KI_EXIT_RET8)

EXTERN _PsConvertToGuiThread@0:PROC

PUBLIC _KiConvertToGuiThread@0
_KiConvertToGuiThread@0:

    push ebx

    mov ebx, ebp
    sub ebx, esp

    call _PsConvertToGuiThread@0

    mov ebp, esp
    add ebp, ebx

    pop ebx

    ret

PUBLIC _KiSystemCallTrampoline@12
_KiSystemCallTrampoline@12:
    push ebp
    mov ebp, esp
    push esi
    push edi

    mov eax, [ebp + 8]

    mov esi, [ebp + 12]

    mov ecx, [ebp + 16]

    sub esp, ecx
    mov edi, esp
    shr ecx, 2
    rep movsd

    call eax

    pop edi
    pop esi
    leave

    ret 12
END
