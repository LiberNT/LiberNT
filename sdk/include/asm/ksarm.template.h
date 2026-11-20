/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) assembler offsets, named as in the SDK 10.0.16299 shared/ksarm.h
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

HEADER("Context frame offsets"),
OFFSET(CxContextFlags, CONTEXT, ContextFlags),
OFFSET(CxR0, CONTEXT, R0),
OFFSET(CxR1, CONTEXT, R1),
OFFSET(CxR2, CONTEXT, R2),
OFFSET(CxR3, CONTEXT, R3),
OFFSET(CxR4, CONTEXT, R4),
OFFSET(CxR11, CONTEXT, R11),
OFFSET(CxR12, CONTEXT, R12),
OFFSET(CxSp, CONTEXT, Sp),
OFFSET(CxLr, CONTEXT, Lr),
OFFSET(CxPc, CONTEXT, Pc),
OFFSET(CxCpsr, CONTEXT, Cpsr),
OFFSET(CxFpscr, CONTEXT, Fpscr),
OFFSET(CxD, CONTEXT, D),
SIZE(CONTEXT_FRAME_LENGTH, CONTEXT),
CONSTANT(CONTEXT_FULL),
CONSTANT(CONTEXT_CONTROL),
CONSTANT(CONTEXT_INTEGER),
CONSTANT(CONTEXT_FLOATING_POINT),

HEADER("Exception record"),
SIZE(ExceptionRecordLength, EXCEPTION_RECORD),

HEADER("Usermode callout frame"),
OFFSET(CkBuffer, UCALLOUT_FRAME, Buffer),
OFFSET(CkLength, UCALLOUT_FRAME, Length),
OFFSET(CkApiNumber, UCALLOUT_FRAME, ApiNumber),
OFFSET(CkSp, UCALLOUT_FRAME, MachineFrame.Sp),
OFFSET(CkPc, UCALLOUT_FRAME, MachineFrame.Pc),
SIZE(CalloutFrameLength, UCALLOUT_FRAME),

HEADER("Thread environment block"),
OFFSET(TeStackBase, NT_TIB, StackBase),
OFFSET(TeStackLimit, NT_TIB, StackLimit),
OFFSET(TePeb, TEB, ProcessEnvironmentBlock),
OFFSET(TeDeallocationStack, TEB, DeallocationStack),
OFFSET(TeFlsData, TEB, FlsData),
OFFSET(PeKernelCallbackTable, PEB, KernelCallbackTable),
