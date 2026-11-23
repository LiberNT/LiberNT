/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 trap, interrupt and system call entry and exit
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ksamd64.inc>
#include <trapamd64.inc>

EXTERN KiDispatchException:PROC
EXTERN KeBugCheckWithTf:PROC
EXTERN KeBugCheckEx:PROC
EXTERN KiRaiseSecurityCheckFailureHandler:PROC
EXTERN MmAccessFault:PROC
EXTERN KiSystemFatalException:PROC
EXTERN KiPageFaultIrqlBugCheck:PROC
EXTERN KiNpxNotAvailableFaultHandler:PROC
EXTERN KiGeneralProtectionFaultHandler:PROC
EXTERN KiInvalidOpcodeFaultHandler:PROC
EXTERN KiFloatingErrorHandler:PROC
EXTERN KiXmmExceptionHandler:PROC
EXTERN KiMcheckAbortHandler:PROC
EXTERN KiDeliverApc:PROC
EXTERN KiDpcInterruptHandler:PROC
EXTERN KiInvokeDeviceIsr:PROC
EXTERN KiIpiInterruptHandler:PROC
EXTERN PsConvertToGuiThread:PROC
EXTERN KiConvertToGuiThreadFailure:PROC
EXTERN MmCreateKernelStack:PROC
EXTERN MmDeleteKernelStack:PROC
EXTERN KdSetOwedBreakpoints:PROC
EXTERN KeAcquireSpinLockAtDpcLevel:PROC
EXTERN KeReleaseSpinLockFromDpcLevel:PROC
EXTERN KiHvApicMsrs:BYTE
EXTERN KiX2ApicMsrs:BYTE

MACRO(DispatchException, Status, Number, P1, P2, P3)
    mov eax, Status
    mov edx, Number
    xor r8d, r8d
    mov r9, P1
    mov r10, P2
    mov r11, P3
    call InternalDispatchException
ENDM

MACRO(DispatchExceptionFlags, Status, Flags, Number, P1, P2, P3)
    mov eax, Status
    mov edx, Number
    mov r8d, Flags
    mov r9, P1
    mov r10, P2
    mov r11, P3
    call InternalDispatchException
ENDM

MACRO(Fatal, BugcheckCode)

    mov ecx, BugcheckCode
    mov rdx, rbp
    call KiSystemFatalException
ENDM

.code64

ALIGN 8

MACRO(UnexpectedVectorStub, Vector)

    nop

/* The 8-bit push immediate is sign extended, so the vector is stored offset by -128 */
#ifdef _USE_ML
PUBLIC KxUnexpectedInterrupt&Vector
KxUnexpectedInterrupt&Vector:
#else
PUBLIC KxUnexpectedInterrupt\Vector
KxUnexpectedInterrupt\Vector:
#endif
    push (Vector - 128)
    jmp KiUnexpectedInterrupt
ENDM

PUBLIC KiUnexpectedRange
KiUnexpectedRange:
Vector = 0
REPEAT 256
    UnexpectedVectorStub %Vector
    Vector = Vector+1
ENDR
PUBLIC KiUnexpectedRangeEnd
KiUnexpectedRangeEnd:

ALIGN 16
PUBLIC KiInterruptDispatchTemplate
KiInterruptDispatchTemplate:

    /* The call pushes the address of the object's DispatchCode, then jumps to its DispatchAddress */
    call qword ptr KiInterruptDispatchTemplate[rip - KINTERRUPT_DispatchCode + KINTERRUPT_DispatchAddress]

/* In: rbp = trap frame, eax = exception code, edx = parameter count, r9-r11 = parameters */
FUNC InternalDispatchException

    sub rsp, EXCEPTION_RECORD_LENGTH + KEXCEPTION_FRAME_LENGTH
    .allocstack (EXCEPTION_RECORD_LENGTH + KEXCEPTION_FRAME_LENGTH)
    mov [rsp + KEXCEPTION_FRAME_Rbx], rbx
    .savereg rbx, KEXCEPTION_FRAME_Rbx
    mov [rsp + KEXCEPTION_FRAME_Rdi], rdi
    .savereg rdi, KEXCEPTION_FRAME_Rdi
    mov [rsp + KEXCEPTION_FRAME_Rsi], rsi
    .savereg rsi, KEXCEPTION_FRAME_Rsi
    mov [rsp + KEXCEPTION_FRAME_R12], r12
    .savereg r12, KEXCEPTION_FRAME_R12
    mov [rsp + KEXCEPTION_FRAME_R13], r13
    .savereg r13, KEXCEPTION_FRAME_R13
    mov [rsp + KEXCEPTION_FRAME_R14], r14
    .savereg r14, KEXCEPTION_FRAME_R14
    mov [rsp + KEXCEPTION_FRAME_R15], r15
    .savereg r15, KEXCEPTION_FRAME_R15
    movdqa [rsp + KEXCEPTION_FRAME_Xmm6], xmm6
    .savexmm128 xmm6, KEXCEPTION_FRAME_Xmm6
    movdqa [rsp + KEXCEPTION_FRAME_Xmm7], xmm7
    .savexmm128 xmm7, KEXCEPTION_FRAME_Xmm7
    movdqa [rsp + KEXCEPTION_FRAME_Xmm8], xmm8
    .savexmm128 xmm8, KEXCEPTION_FRAME_Xmm8
    movdqa [rsp + KEXCEPTION_FRAME_Xmm9], xmm9
    .savexmm128 xmm9, KEXCEPTION_FRAME_Xmm9
    movdqa [rsp + KEXCEPTION_FRAME_Xmm10], xmm10
    .savexmm128 xmm10, KEXCEPTION_FRAME_Xmm10
    movdqa [rsp + KEXCEPTION_FRAME_Xmm11], xmm11
    .savexmm128 xmm11, KEXCEPTION_FRAME_Xmm11
    movdqa [rsp + KEXCEPTION_FRAME_Xmm12], xmm12
    .savexmm128 xmm12, KEXCEPTION_FRAME_Xmm12
    movdqa [rsp + KEXCEPTION_FRAME_Xmm13], xmm13
    .savexmm128 xmm13, KEXCEPTION_FRAME_Xmm13
    movdqa [rsp + KEXCEPTION_FRAME_Xmm14], xmm14
    .savexmm128 xmm14, KEXCEPTION_FRAME_Xmm14
    movdqa [rsp + KEXCEPTION_FRAME_Xmm15], xmm15
    .savexmm128 xmm15, KEXCEPTION_FRAME_Xmm15
    .endprolog

    mov byte ptr [rbp + KTRAP_FRAME_ExceptionActive], KEXCEPTION_ACTIVE_EXCEPTION_FRAME

    lea rcx, [rsp + KEXCEPTION_FRAME_LENGTH]
    mov [rcx + EXCEPTION_RECORD_ExceptionCode], eax
    mov [rcx + EXCEPTION_RECORD_ExceptionFlags], r8d
    xor rax, rax
    mov [rcx + EXCEPTION_RECORD_ExceptionRecord], rax
    mov rax, [rbp + KTRAP_FRAME_Rip]
    cmp dword ptr [rcx + EXCEPTION_RECORD_ExceptionCode], STATUS_BREAKPOINT
    jne InternalDispatchExceptionAddress
    dec rax
InternalDispatchExceptionAddress:
    mov [rcx + EXCEPTION_RECORD_ExceptionAddress], rax
    mov [rcx + EXCEPTION_RECORD_NumberParameters], edx
    mov [rcx + EXCEPTION_RECORD_ExceptionInformation + HEX(00)], r9
    mov [rcx + EXCEPTION_RECORD_ExceptionInformation + HEX(08)], r10
    mov [rcx + EXCEPTION_RECORD_ExceptionInformation + HEX(10)], r11

    mov rax, [rbp + KTRAP_FRAME_Rbp]
    mov [rsp + KEXCEPTION_FRAME_Rbp], rax
    mov qword ptr [rsp + KEXCEPTION_FRAME_Return], 0

    mov rdx, rsp
    mov r8, rbp
    mov r9b, [r8 + KTRAP_FRAME_PreviousMode]
    mov byte ptr [rsp + KEXCEPTION_FRAME_P5], 1
    call KiDispatchException

    mov rbx, [rsp + KEXCEPTION_FRAME_Rbx]
    mov rdi, [rsp + KEXCEPTION_FRAME_Rdi]
    mov rsi, [rsp + KEXCEPTION_FRAME_Rsi]
    mov r12, [rsp + KEXCEPTION_FRAME_R12]
    mov r13, [rsp + KEXCEPTION_FRAME_R13]
    mov r14, [rsp + KEXCEPTION_FRAME_R14]
    mov r15, [rsp + KEXCEPTION_FRAME_R15]
    movdqa xmm6, [rsp + KEXCEPTION_FRAME_Xmm6]
    movdqa xmm7, [rsp + KEXCEPTION_FRAME_Xmm7]
    movdqa xmm8, [rsp + KEXCEPTION_FRAME_Xmm8]
    movdqa xmm9, [rsp + KEXCEPTION_FRAME_Xmm9]
    movdqa xmm10, [rsp + KEXCEPTION_FRAME_Xmm10]
    movdqa xmm11, [rsp + KEXCEPTION_FRAME_Xmm11]
    movdqa xmm12, [rsp + KEXCEPTION_FRAME_Xmm12]
    movdqa xmm13, [rsp + KEXCEPTION_FRAME_Xmm13]
    movdqa xmm14, [rsp + KEXCEPTION_FRAME_Xmm14]
    movdqa xmm15, [rsp + KEXCEPTION_FRAME_Xmm15]

    add rsp, EXCEPTION_RECORD_LENGTH + KEXCEPTION_FRAME_LENGTH
    ret
ENDFUNC

PUBLIC KiDivideErrorFault
FUNC KiDivideErrorFault

    EnterTrap TF_SAVE_ALL

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiDivideErrorDispatch

    sti

KiDivideErrorDispatch:

    DispatchException STATUS_INTEGER_DIVIDE_BY_ZERO, 0, 0, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiDebugTrapOrFault
FUNC KiDebugTrapOrFault

    EnterTrap (TF_SAVE_ALL OR TF_DEBUG)

    test word ptr [rbp + KTRAP_FRAME_SegCs], 3
    jz KiDebugTrapOrFaultKMode

    sti

KiDebugTrapOrFaultKMode:
    /* Do not single-step the exception dispatcher itself */
    and dword ptr [rbp + KTRAP_FRAME_EFlags], HEX(FFFFFEFF)

    DispatchException STATUS_SINGLE_STEP, 0, 0, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_DEBUG OR TF_CHECKUSERAPC)
ENDFUNC

EXTERN KiNmiInterruptHandler:PROC

FUNC KiNmiInterruptWithEf

    GENERATE_EXCEPTION_FRAME

    lea rcx, [rsp + KEXCEPTION_FRAME_LENGTH]
    lea rdx, [rsp]
    call KiNmiInterruptHandler

    RESTORE_EXCEPTION_STATE

    ret
ENDFUNC

PUBLIC KiNmiInterrupt
FUNC KiNmiInterrupt

    EnterTrap TF_SAVE_ALL

    call KiNmiInterruptWithEf

    ExitTrap TF_SAVE_ALL
ENDFUNC

PUBLIC KiBreakpointTrap
FUNC KiBreakpointTrap

    EnterTrap TF_SAVE_ALL

    test word ptr [rbp + KTRAP_FRAME_SegCs], 3
    jz KiBreakpointTrapKMode

    sti

KiBreakpointTrapKMode:

    DispatchException STATUS_BREAKPOINT, 1, BREAKPOINT_BREAK, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiOverflowTrap
FUNC KiOverflowTrap

    EnterTrap TF_SAVE_ALL

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiOverflowDispatch

    sti

KiOverflowDispatch:

    DispatchException STATUS_INTEGER_OVERFLOW, 3, 0, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiBoundFault
FUNC KiBoundFault

    EnterTrap TF_SAVE_ALL

    test word ptr [rbp + KTRAP_FRAME_SegCs], 3
    jnz KiBoundFaultUserMode

    Fatal EXCEPTION_BOUND_CHECK

KiBoundFaultUserMode:

    sti

    DispatchException STATUS_ARRAY_BOUNDS_EXCEEDED, 0, 0, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiInvalidOpcodeFault
FUNC KiInvalidOpcodeFault

    EnterTrap TF_SAVE_ALL

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiInvalidOpcodeDispatch

    sti

KiInvalidOpcodeDispatch:

    test word ptr [rbp + KTRAP_FRAME_SegCs], 3
    jz KiInvalidOpcodeKernel

    mov rcx, rbp
    call KiInvalidOpcodeFaultHandler
    DispatchException eax, 0, 0, 0, 0
    jmp KiInvalidOpcodeExit

KiInvalidOpcodeKernel:

    DispatchException STATUS_ILLEGAL_INSTRUCTION, 0, 0, 0, 0

KiInvalidOpcodeExit:

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiNpxNotAvailableFault
FUNC KiNpxNotAvailableFault

    EnterTrap TF_SAVE_ALL

    mov rcx, rbp
    call KiNpxNotAvailableFaultHandler

    test eax, eax
    jz KiNpxNotAvailableFaultExit

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiNpxNotAvailableDispatch
    sti

KiNpxNotAvailableDispatch:

    DispatchException eax, 3, 0, 0, 0

KiNpxNotAvailableFaultExit:

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiDoubleFaultAbort
FUNC KiDoubleFaultAbort

    /* VirtualBox can deliver this without pushing an error code, so realign the stack */
    and rsp, HEX(FFFFFFFFFFFFFFF0)

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    Fatal 8
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif
ENDFUNC

PUBLIC KiNpxSegmentOverrunAbort
FUNC KiNpxSegmentOverrunAbort

    EnterTrap TF_SAVE_ALL

    Fatal EXCEPTION_NPX_OVERRUN

#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif
ENDFUNC

PUBLIC KiInvalidTssFault
FUNC KiInvalidTssFault

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    Fatal EXCEPTION_INVALID_TSS
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif
ENDFUNC

PUBLIC KiSegmentNotPresentFault
FUNC KiSegmentNotPresentFault

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    Fatal EXCEPTION_SEGMENT_NOT_PRESENT
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif
ENDFUNC

PUBLIC KiStackFault
FUNC KiStackFault

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    test word ptr [rbp + KTRAP_FRAME_SegCs], 3
    jnz KiStackFaultUserMode

    Fatal EXCEPTION_STACK_FAULT
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif

KiStackFaultUserMode:
    sti
    DispatchException STATUS_ACCESS_VIOLATION, 2, 0, -1, 0
    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiGeneralProtectionFault
FUNC KiGeneralProtectionFault
    .pushframe code
    .endprolog

    /* An IRETQ that faults on its way to user mode does so after SWAPGS */
    test byte ptr [rsp + 8 + MfSegCs], 1
    jnz KiGeneralProtectionFaultTrap
    push rax
    mov rax, [rsp + 16 + MfRip]
    cmp word ptr [rax], HEX(0CF48)
    jne KiGpfKernelFault
    mov rax, [rsp + 16 + MfRsp]
    test byte ptr [rax + MfSegCs], 1
    jz KiGpfKernelFault
    push rcx
    mov rcx, [rsp + 8]
    mov [rax - 16], rcx
    mov rcx, [rsp + 16]
    mov [rax - 8], rcx
    pop rcx
    lea rsp, [rax - 16]
    pop rax
    jmp KiUserIretqFault

KiGpfKernelFault:
    pop rax
    jmp KiGeneralProtectionFaultTrap
ENDFUNC

FUNC KiGeneralProtectionFaultTrap

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiGpfHandler
    sti

KiGpfHandler:

    mov rcx, rbp
    call KiGeneralProtectionFaultHandler

    test eax, eax
    jge KiGpfExit

    cmp eax, STATUS_ACCESS_VIOLATION
    je DispatchAccessViolation

    DispatchException eax, 0, 0, 0, 0
    jmp KiGpfExit

DispatchAccessViolation:

    DispatchException eax, 2, 0, -1, 0

KiGpfExit:

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

FUNC KiUserIretqFault
    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiUserIretqFaultDispatch
    sti

KiUserIretqFaultDispatch:
    DispatchException STATUS_ACCESS_VIOLATION, 2, 0, -1, 0
    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiPageFault
FUNC KiPageFault

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    /* Link this frame so nested faults can recover the previous active trap */
    mov rax, gs:[PcCurrentThread]
    mov rcx, [rax + ThTrapFrame]
    mov [rbp + KTRAP_FRAME_TrapFrame], rcx
    mov [rax + ThTrapFrame], rbp

    mov rdx, cr2
    mov [rbp  + KTRAP_FRAME_FaultAddress], rdx

    /* Nested kernel page faults on an exhausted stack will recurse forever */
    test byte ptr [rbp + KTRAP_FRAME_SegCs], 1
    jnz SkipStackCheck
    test rcx, rcx
    jz SkipStackCheck
    /* A previous frame on another stack, such as the one a stack callout left, says nothing about this one */
    mov r10, [rax + ThStackLimit]
    mov r11, [rax + ThStackBase]
    cmp rcx, r10
    jb SkipStackCheck
    cmp rcx, r11
    jae SkipStackCheck
    cmp rbp, r11
    jae SkipStackCheck
    cmp rcx, rbp
    jbe KernelStackOverflow
    sub rcx, rbp
    cmp rcx, KTRAP_FRAME_LENGTH
    jbe KernelStackOverflow

SkipStackCheck:

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz IntsDisabled

    sti

IntsDisabled:

    /* Save the faulting IRQL; push 16 bytes to keep the call aligned */
    mov rax, cr8
    push rax
    sub rsp, HEX(28)

    mov ecx, [rbp + KTRAP_FRAME_ErrorCode]

    mov r8b, [rbp + KTRAP_FRAME_SegCs]
    and r8b, 1
    mov r9, rbp
    call MmAccessFault

    add rsp, HEX(28)
    pop rdx

    test eax, eax
    jge PageFaultSuccess

    /* An unresolved fault above APC_LEVEL is fatal */
    cmp edx, APC_LEVEL
    jbe PageFaultError
    mov rcx, rbp
    call KiPageFaultIrqlBugCheck
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif

PageFaultSuccess:

    call KdSetOwedBreakpoints

    jmp PageFaultReturn

PageFaultError:

    /* Exception dispatch may unwind without ever reaching PageFaultReturn */
    mov rcx, gs:[PcCurrentThread]
    mov rdx, [rbp + KTRAP_FRAME_TrapFrame]
    mov [rcx + ThTrapFrame], rdx

    /* ExceptionInformation[0] is the read, write or execute flag */
    mov r9d, [rbp + KTRAP_FRAME_ErrorCode]
    shr r9d, 1
    and r9d, 9

    /* Set parameter 2 to the faulting address saved on trap entry */
    mov r10, [rbp + KTRAP_FRAME_FaultAddress]

    cmp eax, STATUS_ACCESS_VIOLATION
    je AccessViolation
    cmp eax, STATUS_GUARD_PAGE_VIOLATION
    je SpecialCode
    cmp eax, STATUS_STACK_OVERFLOW
    je SpecialCode

InPageException:

    mov r11d, eax
    mov eax, STATUS_IN_PAGE_ERROR
    mov edx, 3
    xor r8d, r8d  // ExceptionFlags
    call InternalDispatchException
    jmp PageFaultReturn

AccessViolation:

    mov eax, KI_EXCEPTION_ACCESS_VIOLATION

SpecialCode:

    mov edx, 2
    xor r8d, r8d // ExceptionFlags
    call InternalDispatchException

PageFaultReturn:

    cli

    /* Restore the previous active trap frame */
    mov rcx, gs:[PcCurrentThread]
    mov rdx, [rbp + KTRAP_FRAME_TrapFrame]
    mov [rcx + ThTrapFrame], rdx

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)

KernelStackOverflow:
    /* Use the original faulting frame so the bugcheck points at the real cause */
    mov rax, gs:[PcCurrentThread]
    mov rdx, [rbp + KTRAP_FRAME_TrapFrame]
    mov [rax + ThTrapFrame], rdx
    mov ecx, HEX(0E)
    call KiSystemFatalException
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif
ENDFUNC

PUBLIC KiRaiseSecurityCheckFailure
FUNC KiRaiseSecurityCheckFailure

    EnterTrap TF_SAVE_ALL

    /* Point the trap frame at the INT 29 instruction. */
    /* INT 29h is two bytes; RIP is moved back onto it */
    sub qword ptr [rbp + KTRAP_FRAME_Rip], 2

    /* Kernel fast-fail is fatal and must preserve the original trap frame. */
    test word ptr [rbp + KTRAP_FRAME_SegCs], 3
    jnz KiRaiseSecurityCheckFailureUserMode

    mov rcx, rbp
    call KiRaiseSecurityCheckFailureHandler

#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif

KiRaiseSecurityCheckFailureUserMode:
    /* Dispatch a noncontinuable fast-fail exception to user mode. */
    sti
    mov r9, [rbp + KTRAP_FRAME_Rcx]
    DispatchExceptionFlags STATUS_STACK_BUFFER_OVERRUN, \
                           EXCEPTION_NONCONTINUABLE, 1, r9, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiFloatingErrorFault
FUNC KiFloatingErrorFault

    EnterTrap TF_SAVE_ALL

    mov rcx, rbp
    call KiFloatingErrorHandler

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiFloatingErrorDispatch
    sti

KiFloatingErrorDispatch:
    DispatchException eax, 1, 0, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiAlignmentFault
FUNC KiAlignmentFault

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    /* Only user mode can take an alignment check */
    test byte ptr [rbp + KTRAP_FRAME_SegCs], MODE_MASK
    jz KiAlignmentFaultFatal

    sti

    DispatchException STATUS_DATATYPE_MISALIGNMENT, 0, 0, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)

KiAlignmentFaultFatal:

    Fatal EXCEPTION_ALIGNMENT_CHECK
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif
ENDFUNC

PUBLIC KiMcheckAbort
FUNC KiMcheckAbort

    EnterTrap TF_SAVE_ALL

    mov rcx, rbp
    call KiMcheckAbortHandler

    Fatal HEX(12)
#ifdef _USE_ML
    jmp $
#else
9:  jmp 9b
#endif
ENDFUNC

PUBLIC KiXmmException
FUNC KiXmmException

    EnterTrap TF_SAVE_ALL

    mov rcx, rbp
    call KiXmmExceptionHandler

    test eax, eax
    jge KiXmmExit

    test dword ptr [rbp + KTRAP_FRAME_EFlags], EFLAGS_IF_MASK
    jz KiXmmDispatch
    sti

KiXmmDispatch:

    mov r10d, [rbp + KTRAP_FRAME_MxCsr]
    DispatchException eax, 2, 0, r10, 0

KiXmmExit:

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiRaiseAssertion
FUNC KiRaiseAssertion

    EnterTrap (TF_SAVE_ALL)

    /* INT 2Ch is two bytes, unlike INT 3; RIP is moved back onto it */
    sub qword ptr [rbp + KTRAP_FRAME_Rip], 2

    test byte ptr [rbp + KTRAP_FRAME_SegCs], 1
    jz KiRaiseAssertionDispatch
    sti

KiRaiseAssertionDispatch:

    DispatchException STATUS_ASSERTION_FAILURE, 0, 0, 0, 0

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiDebugServiceTrap
FUNC KiDebugServiceTrap

    EnterTrap TF_SAVE_ALL

    inc qword ptr [rbp + KTRAP_FRAME_Rip]

    cmp word ptr [rbp + KTRAP_FRAME_SegCs], (KGDT64_R3_CMCODE OR RPL_MASK)
    jne KiDebugServiceDispatch
    mov eax, dword ptr [rbp + KTRAP_FRAME_Rax]
    mov [rbp + KTRAP_FRAME_Rax], rax
    mov eax, dword ptr [rbp + KTRAP_FRAME_Rcx]
    mov [rbp + KTRAP_FRAME_Rcx], rax
    mov eax, dword ptr [rbp + KTRAP_FRAME_Rdx]
    mov [rbp + KTRAP_FRAME_Rdx], rax
    mov eax, ebx
    mov [rbp + KTRAP_FRAME_R8], rax
    mov eax, edi
    mov [rbp + KTRAP_FRAME_R9], rax

KiDebugServiceDispatch:
    test byte ptr [rbp + KTRAP_FRAME_SegCs], 1
    jz KiDebugServiceKMode
    sti

KiDebugServiceKMode:

    DispatchException STATUS_BREAKPOINT, 3, [rbp+KTRAP_FRAME_Rax], [rbp+KTRAP_FRAME_Rcx], [rbp+KTRAP_FRAME_Rdx]

    ExitTrap (TF_SAVE_ALL OR TF_CHECKUSERAPC)
ENDFUNC

PUBLIC KiApcInterrupt
.PROC KiApcInterrupt

    EnterTrap (TF_SAVE_ALL OR TF_IRQL)

    mov rax, APC_LEVEL
    mov cr8, rax

    cmp byte ptr KiHvApicMsrs[rip], 0
    jz KiApcInterruptCheckX2Apic
    mov ecx, HEX(40000070)
    xor eax, eax
    xor edx, edx
    wrmsr
    jmp KiApcInterruptEoiDone
KiApcInterruptCheckX2Apic:
    cmp byte ptr KiX2ApicMsrs[rip], 0
    jz KiApcInterruptMmioEoi
    mov ecx, HEX(80B)
    xor eax, eax
    xor edx, edx
    wrmsr
    jmp KiApcInterruptEoiDone
KiApcInterruptMmioEoi:
    mov dword ptr [APIC_EOI], 0
KiApcInterruptEoiDone:

    sti

    mov cl, [rbp + KTRAP_FRAME_SegCs]
    and cl, 1
    mov rdx, 0
    mov r8, rbp
    call KiDeliverApc

    cli

    mov rax, PASSIVE_LEVEL
    mov cr8, rax

    ExitTrap (TF_SAVE_ALL OR TF_IRQL)
.ENDP

EXTERN KiRetireDpcList:PROC
EXTERN KiRunStackCallout:PROC

/* KiRetireDpcListInDpcStack(PKPRCB rcx, PVOID DpcStack rdx) */
PUBLIC KiRetireDpcListInDpcStack
.PROC KiRetireDpcListInDpcStack
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog

    mov rsp, rdx

    sub rsp, 32

    call KiRetireDpcList

    mov rsp, rbp
    pop rbp
    ret
.ENDP

/*
 * VOID
 * KiCallOnKernelStack(
 *     PVOID Context,
 *     PVOID StackTop);
 */
PUBLIC KiCallOnKernelStack
.PROC KiCallOnKernelStack
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog

    /* Switch to the given stack; rcx still holds the context */
    mov rsp, rdx

    sub rsp, 32

    call KiRunStackCallout

    mov rsp, rbp
    pop rbp
    ret
.ENDP

PUBLIC KiDpcInterrupt
.PROC KiDpcInterrupt

    EnterTrap (TF_SAVE_ALL OR TF_IRQL)

    call KiDpcInterruptHandler

    /* The handler already sent the EOI */
    ExitTrap (TF_SAVE_ALL OR TF_IRQL)
.ENDP

PUBLIC KiIpiInterrupt
.PROC KiIpiInterrupt

    EnterTrap (TF_SAVE_ALL OR TF_IRQL)

    /* Service the hardware IPI */
    call KiIpiInterruptHandler

    ExitTrap (TF_SAVE_ALL OR TF_IRQL)
.ENDP

EXTERN KiUnexpectedInterruptHandler:PROC

PUBLIC KiUnexpectedInterrupt
FUNC KiUnexpectedInterrupt
    /* The error code is the vector minus 128 (sign-extended imm8 push in the stub) */
    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL)

    /* Report the stray vector so misrouted interrupts are visible */
    mov rcx, [rbp + KTRAP_FRAME_ErrorCode]
    call KiUnexpectedInterruptHandler

    /* End it where the volatile registers are still scratch: the LAPIC
     * accepted this vector, so leaving without an EOI would keep it
     * in-service and mask its whole priority class on this CPU forever
     * (wedges APC/DPC delivery below it) */
    cmp byte ptr KiHvApicMsrs[rip], 0
    jz KiUnexpectedEoiCheckX2Apic
    mov ecx, HEX(40000070)
    xor eax, eax
    xor edx, edx
    wrmsr
    jmp KiUnexpectedEoiDone
KiUnexpectedEoiCheckX2Apic:
    cmp byte ptr KiX2ApicMsrs[rip], 0
    jz KiUnexpectedEoiMmio
    mov ecx, HEX(80B)
    xor eax, eax
    xor edx, edx
    wrmsr
    jmp KiUnexpectedEoiDone
KiUnexpectedEoiMmio:
    mov dword ptr [APIC_EOI], 0
KiUnexpectedEoiDone:
    ExitTrap (TF_SAVE_ALL)
ENDFUNC

PUBLIC KiInterruptDispatch
FUNC KiInterruptDispatch

    EnterTrap (TF_HAS_ERROR_CODE OR TF_SAVE_ALL OR TF_IRQL)

    inc dword ptr gs:[PcInterruptCount];

    mov [rbp + KTRAP_FRAME_Rbx], rbx
    mov [rbp + KTRAP_FRAME_Rsi], rsi

    mov rbx, [rbp + KTRAP_FRAME_ErrorCode]

    /* Back from the pushed return address to the object: DispatchCode offset plus the 6-byte call */
    sub rbx, KINTERRUPT_DispatchCode + 6

    lea rsi, [rbx + KINTERRUPT_InterruptListEntry]

.DoDispatchInterrupt:

    movzx rax, byte ptr [rbx + KINTERRUPT_SynchronizeIrql]
    mov cr8, rax

    sti

    mov rcx, [rbx + KINTERRUPT_ActualLock]
    call KeAcquireSpinLockAtDpcLevel

    mov rcx, rbx
    call KiInvokeDeviceIsr

    mov rcx, [rbx + KINTERRUPT_ActualLock]
    call KeReleaseSpinLockFromDpcLevel

    cli
    movzx rax, byte ptr [rbp + KTRAP_FRAME_PreviousIrql]
    mov cr8, rax

    mov rax, [rbx + KINTERRUPT_InterruptListEntry]
    cmp rax, rsi
    je .Done

    lea rbx, [rax - KINTERRUPT_InterruptListEntry]
    jmp .DoDispatchInterrupt

.Done:

    mov rbx, [rbp + KTRAP_FRAME_Rbx]
    mov rsi, [rbp + KTRAP_FRAME_Rsi]

    /* End it the way this local APIC is reached */
    cmp byte ptr KiHvApicMsrs[rip], 0
    jz KiInterruptDispatchEoiCheckX2Apic
    mov ecx, HEX(40000070)
    xor eax, eax
    xor edx, edx
    wrmsr
    jmp KiInterruptDispatchEoiDone
KiInterruptDispatchEoiCheckX2Apic:
    cmp byte ptr KiX2ApicMsrs[rip], 0
    jz KiInterruptDispatchEoiMmio
    mov ecx, HEX(80B)
    xor eax, eax
    xor edx, edx
    wrmsr
    jmp KiInterruptDispatchEoiDone
KiInterruptDispatchEoiMmio:
    mov dword ptr [APIC_EOI], 0
KiInterruptDispatchEoiDone:
    ExitTrap (TF_SAVE_ALL)
ENDFUNC

EXTERN KiSystemCallHandler:PROC

/* 64-bit SYSCALL: rax = service number, rcx = user RIP, r11 = user RFLAGS, r10/rdx/r8/r9 = parameters 1-4 */
PUBLIC KiSystemCallEntry64
.PROC KiSystemCallEntry64

    .PUSHFRAME
    .ALLOCSTACK (KTRAP_FRAME_LENGTH + MAX_SYSCALL_PARAM_SIZE - MachineFrameLength)
    .SAVEREG rbp, MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rbp
    .ENDPROLOG

    swapgs

    mov gs:[PcUserRsp], rsp

    mov rsp, gs:[PcRspBase]

    sub rsp, (KTRAP_FRAME_LENGTH + MAX_SYSCALL_PARAM_SIZE)

    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rax], rax
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip], rcx
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rdx], rdx
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R8], r8
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R9], r9
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rcx], r10
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_EFlags], r11
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rbp], rbp

    mov rax, gs:[PcUserRsp]
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rsp], rax
    mov word ptr [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_SegCs], (KGDT64_R3_CODE OR RPL_MASK)
    mov word ptr [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_SegSs], (KGDT64_R3_DATA OR RPL_MASK)

    mov ax, ds
    cmp ax, (KGDT64_R3_DATA OR RPL_MASK)
    jne SystemServiceLoadSegments
    mov ax, es
    cmp ax, (KGDT64_R3_DATA OR RPL_MASK)
    jne SystemServiceLoadSegments

SystemServiceSegmentsLoaded:

    stmxcsr [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_MxCsr]
    ldmxcsr gs:[PcMxCsr]

    SAVE_USER_DEBUG_REGISTERS rsp + MAX_SYSCALL_PARAM_SIZE

    mov rax, gs:[PcCurrentThread]
    mov rcx, [rax + ThTrapFrame]

    lea rdx, [rsp + MAX_SYSCALL_PARAM_SIZE]
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_TrapFrame], rcx
    mov [rax + ThTrapFrame], rdx

    test byte ptr [rax + ThDebugActive], DEBUG_ACTIVE_MINIMAL_THREAD
    jnz KiPicoSystemCallEntry

#if DBG

    mov rax, cr8
    test eax, eax
    jz KiSystemCall64Again
    int HEX(2C)
#endif

GLOBAL_LABEL KiSystemCall64Again

    /* Pass TrapFrame pointer in rcx for the C-handler.
     * This is needed because Clang's _AddressOfReturnAddress() intrinsic
     * computes the wrong address when the function uses a non-standard
     * frame pointer setup (lea-based instead of mov rbp,rsp). */
    lea rcx, [rsp + MAX_SYSCALL_PARAM_SIZE]

    call KiSystemCallHandler

    mov rcx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rcx]
    mov rdx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rdx]
    mov r8, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R8]
    mov r9, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R9]
    call rax

GLOBAL_LABEL KiSystemServiceExit

    ASSERT_TRAP_FRAME_INTS_ENABLED rsp + MAX_SYSCALL_PARAM_SIZE

    /* A system service must return to user mode at PASSIVE_LEVEL, in the
     * original APC environment, and with kernel APC delivery enabled. */
    mov r10, cr8
    test r10b, r10b
    jnz SystemServiceIrqlMismatch

    mov r11, gs:[PcCurrentThread]
    movzx r8d, byte ptr [r11 + ThApcStateIndex]
    movsx r9, word ptr [r11 + ThCombinedApcDisable]
    test r8b, r8b
    jnz SystemServiceApcMismatch
    test r9w, r9w
    jnz SystemServiceApcMismatch

    xor r10d, r10d
    mov rcx, gs:[PcCurrentThread]
    cmp byte ptr [rcx + ThApcState + AsUserApcPending], 0
    jz NoUserApcPending

    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rax], rax
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rbp], rbp
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R9], rbp
    mov rax, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rsp]
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R8], rax
    mov rax, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip]
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rcx], rax
    mov rax, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_EFlags]
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R11], rax
    xor rax, rax
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rdx], rax
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R10], rax
    pxor xmm0, xmm0
    movdqa [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm0], xmm0
    movdqa [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm1], xmm0
    movdqa [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm2], xmm0
    movdqa [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm3], xmm0
    movdqa [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm4], xmm0
    movdqa [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm5], xmm0

    lea rcx, [rsp + MAX_SYSCALL_PARAM_SIZE]
    call KiInitiateUserApc
    mov r10, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R10]

NoUserApcPending:

    cli

    ldmxcsr [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_MxCsr]

    mov rcx, gs:[PcCurrentThread]
    mov rdx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_TrapFrame]
    mov [rcx + KTHREAD_TrapFrame], rdx

    RESTORE_USER_DEBUG_REGISTERS rsp + MAX_SYSCALL_PARAM_SIZE

    mov rcx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip]
    mov r11, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_EFlags]

    mov rdx, gs:[PcCurrentThread]
    test byte ptr [rdx + ThDebugActive], DEBUG_ACTIVE_INSTRUMENTED
    jnz SystemServiceInstrumented

SystemServiceInstrumentationDone:
    /* SYSRET to an address outside the user range faults in kernel mode on Intel CPUs */
    mov rdx, rcx
    shr rdx, 47
    jnz SystemServiceIretq

    mov rsp, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rsp]

    mov r8, rsp

    mov r9, rbp

    swapgs

    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    xor rdx, rdx

    sysretq

SystemServiceIretq:
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip], rcx
    mov word ptr [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_SegCs], (KGDT64_R3_CODE OR RPL_MASK)
    mov r8, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rsp]
    mov r9, rbp
    add rsp, MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip
    swapgs
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    xor edx, edx
    iretq

SystemServiceInstrumented:
    mov rdx, [rdx + ThApcState + AsProcess]
    mov rdx, [rdx + PrInstrumentationCallback]
    test rdx, rdx
    jz SystemServiceInstrumentationDone
    mov r10, rcx
    mov rcx, rdx
    jmp SystemServiceInstrumentationDone

SystemServiceLoadSegments:
    /* FIXME: Windows 8.1 keeps user DS and ES across a system call; this resets them to the flat data selector */
    mov ax, (KGDT64_R3_DATA OR RPL_MASK)
    mov ds, ax
    mov es, ax
    jmp SystemServiceSegmentsLoaded

SystemServiceIrqlMismatch:
    /* Preserve the leaked IRQL as bugcheck parameter 2. */
    mov r8, r10
    xor r9d, r9d
    mov ecx, IRQL_GT_ZERO_AT_SYSTEM_SERVICE
    jmp SystemServiceInvariantBugCheck

SystemServiceApcMismatch:
    mov ecx, APC_INDEX_MISMATCH

SystemServiceInvariantBugCheck:
    /* Parameters 1 and 4 identify the user return site and trap frame. */
    mov rdx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip]
    lea r11, [rsp + MAX_SYSCALL_PARAM_SIZE]
    mov [rsp + 4 * 8], r11
    cli
    call KeBugCheckEx
    int HEX(2C)

KiPicoSystemCallEntry:
    /* A Linux system call preserves every register except rax, rcx and r11. */
    mov rcx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip]
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rcx], rcx
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R10], r10
    mov [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R11], r11
    movaps [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm0], xmm0
    movaps [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm1], xmm1
    movaps [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm2], xmm2
    movaps [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm3], xmm3
    movaps [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm4], xmm4
    movaps [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm5], xmm5
    lea rcx, [rsp + MAX_SYSCALL_PARAM_SIZE]
    call KiPicoSystemCall

    ldmxcsr [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_MxCsr]
    mov rcx, gs:[PcCurrentThread]
    mov rdx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_TrapFrame]
    mov [rcx + KTHREAD_TrapFrame], rdx
    RESTORE_PICO_FS_BASE
    RESTORE_USER_DEBUG_REGISTERS rsp + MAX_SYSCALL_PARAM_SIZE
    movaps xmm0, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm0]
    movaps xmm1, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm1]
    movaps xmm2, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm2]
    movaps xmm3, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm3]
    movaps xmm4, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm4]
    movaps xmm5, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Xmm5]
    mov rax, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rax]
    mov rcx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rcx]
    mov rdx, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rdx]
    mov r8, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R8]
    mov r9, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R9]
    mov r10, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R10]
    mov r11, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R11]
    mov rbp, [rsp + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rbp]
    add rsp, MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip
    swapgs
    iretq

.ENDP

/* DECLSPEC_NORETURN KiExceptionExit(PKTRAP_FRAME rcx, PKEXCEPTION_FRAME rdx) */
PUBLIC KiExceptionExit
KiExceptionExit:

    movaps xmm6, [rdx + ExXmm6]
    movaps xmm7, [rdx + ExXmm7]
    movaps xmm8, [rdx + ExXmm8]
    movaps xmm9, [rdx + ExXmm9]
    movaps xmm10, [rdx + ExXmm10]
    movaps xmm11, [rdx + ExXmm11]
    movaps xmm12, [rdx + ExXmm12]
    movaps xmm13, [rdx + ExXmm13]
    movaps xmm14, [rdx + ExXmm14]
    movaps xmm15, [rdx + ExXmm15]
    mov rbx, [rdx + ExRbx]
    mov rdi, [rdx + ExRdi]
    mov rsi, [rdx + ExRsi]
    mov r12, [rdx + ExR12]
    mov r13, [rdx + ExR13]
    mov r14, [rdx + ExR14]
    mov r15, [rdx + ExR15]

    mov rsp, rcx

PUBLIC KiServiceExit3
.PROC KiServiceExit3
    .PUSHFRAME
    .ALLOCSTACK (KTRAP_FRAME_LENGTH - MachineFrameLength)
    .ENDPROLOG

    mov rbp, rsp
    ExitTrap (TF_SEGMENTS OR TF_CHECKUSERAPC)
.ENDP

PUBLIC KiSystemCallEntry32
KiSystemCallEntry32:
    swapgs
    mov gs:[PcUserRsp], rsp
    mov rsp, gs:[PcRspBase]
    push (KGDT64_R3_DATA OR RPL_MASK)
    push qword ptr gs:[PcUserRsp]
    push r11
    push (KGDT64_R3_CMCODE OR RPL_MASK)
    lea rcx, [rcx - 2]
    push rcx
    swapgs
    jmp KiInvalidOpcodeFault

PUBLIC KiZwSystemService
FUNC KiZwSystemService
    push rbp
    .pushreg rbp
    sub rsp, KTRAP_FRAME_LENGTH
    .allocstack KTRAP_FRAME_LENGTH
    mov [rsp + KTRAP_FRAME_Rsi], rsi
    .savereg rsi, KTRAP_FRAME_Rsi
    mov [rsp + KTRAP_FRAME_Rdi], rdi
    .savereg rdi, KTRAP_FRAME_Rdi
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog

    mov r11, gs:[PcCurrentThread]

    mov dil, byte ptr [r11 + KTHREAD_PreviousMode]
    mov byte ptr [rbp + KTRAP_FRAME_PreviousMode], dil

    /* Link the old trap frame and mark this one as a kernel-mode frame */
    mov rdi, [r11 + KTHREAD_TrapFrame]
    mov [rbp + KTRAP_FRAME_TrapFrame], rdi
    mov word ptr [rbp + KTRAP_FRAME_SegCs], KGDT64_R0_CODE

    mov [r11 + ThTrapFrame], rbp
    mov byte ptr [r11 + KTHREAD_PreviousMode], 0

    sub rsp, r10
    and rsp, HEX(0fffffffffffffff0)

    mov [rbp + KTRAP_FRAME_Rcx], rcx

    lea rsi, [rbp + KTRAP_FRAME_LENGTH + 16]
    lea rdi, [rsp]
    mov rcx, r10
    shr rcx, 3
    rep movsq

    mov rcx, [rbp + KTRAP_FRAME_Rcx]

    call rax

    mov r11, gs:[PcCurrentThread]
    mov rsi, [rbp + KTRAP_FRAME_TrapFrame]
    mov [r11 + KTHREAD_TrapFrame], rsi

    mov dil, byte ptr [rbp + KTRAP_FRAME_PreviousMode]
    mov byte ptr [r11 + KTHREAD_PreviousMode], dil

    mov rsi, [rbp + KTRAP_FRAME_Rsi]
    mov rdi, [rbp + KTRAP_FRAME_Rdi]

    lea rsp, [rbp + KTRAP_FRAME_LENGTH]
    pop rbp
    ret

ENDFUNC

PUBLIC KiConvertToGuiThread
FUNC KiConvertToGuiThread

    sub rsp, 40
    .allocstack 40
    .endprolog

    mov rax, gs:[PcCurrentThread]
    cmp byte ptr [rax + KTHREAD_LargeStack], 0
    jnz AlreadyLargeStack

    mov cl, 1
    xor rdx, rdx
    call MmCreateKernelStack

    test rax, rax
    jz KiConvertToGuiThreadFailed

    mov rcx, rax
    mov rdx, rax
    sub rdx, KERNEL_LARGE_STACK_COMMIT
    call KeSwitchKernelStack

    mov rcx, rax
    xor rdx, rdx
    call MmDeleteKernelStack

AlreadyLargeStack:

    call PsConvertToGuiThread

    test eax, eax
    js KiConvertToGuiThreadFailed

    cli

    mov rcx, [rsp + 48 + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rip]
    mov rdx, [rsp + 48 + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_Rdx]
    mov r8, [rsp + 48 + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R8]
    mov r9, [rsp + 48 + MAX_SYSCALL_PARAM_SIZE + KTRAP_FRAME_R9]

    add rsp, 48
    jmp KiSystemCall64Again

KiConvertToGuiThreadFailed:

    add rsp, 40
    jmp KiConvertToGuiThreadFailure

ENDFUNC

EXTERN KiGetTrapContextInternal:PROC

PUBLIC KiGetTrapContext
.PROC KiGetTrapContext

    GENERATE_EXCEPTION_FRAME

    call KiGetTrapContextInternal

    RESTORE_EXCEPTION_STATE

    ret

.ENDP

EXTERN KiSetTrapContextInternal:PROC

PUBLIC KiSetTrapContext
.PROC KiSetTrapContext

    GENERATE_EXCEPTION_FRAME

    call KiSetTrapContextInternal

    RESTORE_EXCEPTION_STATE

    ret

.ENDP

EXTERN KiDeliverApc:PROC
EXTERN KiPicoSystemCallHandler:PROC

PUBLIC KiPicoSystemCall
.PROC KiPicoSystemCall
    GENERATE_EXCEPTION_FRAME
    mov rdx, rsp
    call KiPicoSystemCallHandler
    RESTORE_EXCEPTION_STATE
    ret
.ENDP

PUBLIC KiInitiateUserApc
.PROC KiInitiateUserApc

    GENERATE_EXCEPTION_FRAME

    mov rax, APC_LEVEL
    mov cr8, rax

    mov rbp, gs:[PcCurrentThread]

    mov rsi, rcx

    sti

    mov ecx, 1
    mov rdx, rsp
    mov r8, rsi
    call KiDeliverApc

    cli

    mov rax, PASSIVE_LEVEL
    mov cr8, rax

    RESTORE_EXCEPTION_STATE

    ret

.ENDP

PUBLIC KiInitializeSegments
KiInitializeSegments:
    mov ax, KGDT64_R3_DATA OR RPL_MASK
    mov gs, ax
    swapgs
    mov gs, ax
    ret

PUBLIC KiSwitchKernelStackHelper
KiSwitchKernelStackHelper:

    pop rax

    lea rsp, [rsp + rcx]

    push rax

    mov rax, rdx
    ret

EXTERN KiSwitchKernelStack:PROC

PUBLIC KeSwitchKernelStack
FUNC KeSwitchKernelStack

    mov [rsp + P1Home], rcx
    .savereg rcx, P1Home
    sub rsp, 40
    .allocstack 40
    .endprolog

    /* Keep the live stack stable through the RSP handoff. */
    pushfq
    pop rax
    mov [rsp + 32], rax
    cli

    call KiSwitchKernelStack

    mov rcx, [rsp + 40 + P1Home]

    sub rcx, rax
    add rsp, rcx

    /* Restore the saved flags from the copied stack. */
    mov rdx, [rsp + 32]
    push rdx
    popfq

    add rsp, 40
    ret

ENDFUNC

#ifdef _MSC_VER
#undef lgdt
#undef lidt

PUBLIC __lgdt
__lgdt:
    lgdt fword ptr [rcx]
    ret

PUBLIC __sgdt
__sgdt:
    sgdt fword ptr [rcx]
    ret

PUBLIC __lldt
__lldt:
    lldt cx
    ret

PUBLIC __sldt
__sldt:
    sldt word ptr [rcx]
    ret

PUBLIC __ltr
__ltr:
    ltr cx
    ret

PUBLIC __str
__str:
    str word ptr [rcx]
    ret

PUBLIC __swapgs
__swapgs:
    swapgs
    ret

PUBLIC __readss
__readss:
    mov ax, ss
    ret

PUBLIC __writess
__writess:
    mov ss, cx
    ret

PUBLIC __writefs
__writefs:
    mov fs, cx
    ret

#endif

END
