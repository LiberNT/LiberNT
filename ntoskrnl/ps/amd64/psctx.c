/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ps/amd64/psctx.c
 * PURPOSE:         Process Manager: Set/Get Context for i386
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

typedef struct _PSP_XSTATE_BUFFER
{
    PUCHAR UserXState;
    ULONG Length;
    DECLSPEC_ALIGN(64) UCHAR XState[ANYSIZE_ARRAY];
} PSP_XSTATE_BUFFER, *PPSP_XSTATE_BUFFER;

static
PVOID
PspXStateScratch(
    _In_ PPSP_XSTATE_BUFFER Buffer)
{
    return Buffer->XState + Buffer->Length;
}

NTSTATUS
NTAPI
PspArchCaptureXStateContext(
    _In_ PCONTEXT Context,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SetContext,
    _Out_ PVOID *XState)
{
    PCONTEXT_EX ContextEx;
    PPSP_XSTATE_BUFFER Buffer;
    PXSAVE_AREA_HEADER Header;
    PUCHAR UserXState;
    ULONG Length;
    NTSTATUS Status = STATUS_SUCCESS;

    *XState = NULL;
    if ((ContextFlags & CONTEXT_XSTATE) != CONTEXT_XSTATE)
        return STATUS_SUCCESS;

    ContextEx = (PCONTEXT_EX)(Context + 1);
    if (PreviousMode != KernelMode)
        ProbeForRead(ContextEx, sizeof(*ContextEx), sizeof(ULONG));

    Length = ContextEx->XState.Length;
    UserXState = (PUCHAR)ContextEx + ContextEx->XState.Offset;
    if (Length < sizeof(XSAVE_AREA_HEADER) || Length > KiGetUserXStateMaximumLength())
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        if (SetContext)
            ProbeForRead(UserXState, Length, sizeof(ULONG));
        else
            ProbeForWrite(UserXState, Length, sizeof(ULONG));
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   FIELD_OFFSET(PSP_XSTATE_BUFFER, XState) + Length +
                                   KiGetUserXStateScratchSize(),
                                   TAG_PS_XSTATE);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Buffer->UserXState = UserXState;
    Buffer->Length = Length;
    _SEH2_TRY
    {
        RtlCopyMemory(Buffer->XState, UserXState, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status) && SetContext)
    {
        Header = (PXSAVE_AREA_HEADER)Buffer->XState;
        if (Length < KiGetUserXStateLength(Header->CompactionMask, Header->Mask & KiGetUserXStateSetFeatures(Header)))
            Status = STATUS_BUFFER_OVERFLOW;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_PS_XSTATE);
        return Status;
    }

    *XState = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PspArchCompleteXStateContext(
    _In_opt_ PVOID XState,
    _In_ BOOLEAN CopyOut)
{
    PPSP_XSTATE_BUFFER Buffer = XState;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Buffer)
        return STATUS_SUCCESS;

    if (CopyOut)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Buffer->UserXState, Buffer->XState, Buffer->Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    ExFreePoolWithTag(Buffer, TAG_PS_XSTATE);
    return Status;
}


_IRQL_requires_(APC_LEVEL)
VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE* NormalRoutine,
    _Inout_ PVOID* NormalContext,
    _Inout_ PVOID* SystemArgument1,
    _Inout_ PVOID* SystemArgument2)
{
    PGET_SET_CTX_CONTEXT GetSetContext;
    PPSP_XSTATE_BUFFER XStateBuffer;
    PKTHREAD Thread;
    PKTRAP_FRAME TrapFrame = NULL;
    ULONG ContextFlags;

    PAGED_CODE();

    /* Get the Context Structure */
    GetSetContext = CONTAINING_RECORD(Apc, GET_SET_CTX_CONTEXT, Apc);
    XStateBuffer = GetSetContext->XState;
    Thread = Apc->SystemArgument2;
    NT_ASSERT(KeGetCurrentThread() == Thread);

    /* If this is a kernel-mode request, grab the saved trap frame */
    if (GetSetContext->Mode == KernelMode)
    {
        TrapFrame = Thread->TrapFrame;
    }

    /* If we don't have one, grab it from the stack */
    if (TrapFrame == NULL)
    {
        /* Get the thread's base trap frame */
        TrapFrame = KeGetTrapFrame(KeGetCurrentThread());
    }

    /* Check if it's a set or get */
    if (Apc->SystemArgument1 != 0)
    {
        /* Set the nonvolatiles on the stack, target frame is the trap frame */
        KiSetTrapContext(TrapFrame, &GetSetContext->Context, GetSetContext->Mode);
        if (XStateBuffer)
            KiRestoreUserXState(XStateBuffer->XState, PspXStateScratch(XStateBuffer));
    }
    else
    {
        /* Get the nonvolatiles from the stack */
        KiGetTrapContext(TrapFrame, &GetSetContext->Context);

        ContextFlags = GetSetContext->Context.ContextFlags &
                       ~(CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE | CONTEXT_EXCEPTION_ACTIVE);
        if (ContextFlags & CONTEXT_EXCEPTION_REQUEST)
        {
            ContextFlags |= CONTEXT_EXCEPTION_REPORTING;
            if (TrapFrame->ExceptionActive == KEXCEPTION_ACTIVE_SERVICE_FRAME)
            {
                ContextFlags |= CONTEXT_SERVICE_ACTIVE;
            }
            else if (TrapFrame->ExceptionActive == KEXCEPTION_ACTIVE_EXCEPTION_FRAME)
            {
                ContextFlags |= CONTEXT_EXCEPTION_ACTIVE;
            }
        }
        GetSetContext->Context.ContextFlags = ContextFlags;
        if (XStateBuffer)
            GetSetContext->Status = KiSaveUserXState(XStateBuffer->XState,
                                                     XStateBuffer->Length,
                                                     PspXStateScratch(XStateBuffer));
    }

    /* Notify the Native API that we are done */
    KeSetEvent(&GetSetContext->Event, IO_NO_INCREMENT, FALSE);
}

static
USHORT
PspFullTagFromFxsave(PXMM_SAVE_AREA32 Fx)
{
    ULONG Top = (Fx->StatusWord >> 11) & 7;
    USHORT Full = 0;
    ULONG Physical, Stack;
    PUCHAR Reg;
    USHORT Exponent;
    ULONG64 Mantissa;
    USHORT Tag;

    for (Physical = 0; Physical < 8; Physical++)
    {
        if (!(Fx->TagWord & (1 << Physical)))
        {
            Tag = 3;
        }
        else
        {
            Stack = (Physical - Top) & 7;
            Reg = (PUCHAR)&Fx->FloatRegisters[Stack];
            Mantissa = *(PULONG64)Reg;
            Exponent = *(PUSHORT)(Reg + 8) & 0x7FFF;
            if (Exponent == 0x7FFF)
                Tag = 2;
            else if (!Exponent)
                Tag = Mantissa ? 2 : 1;
            else
                Tag = (Mantissa >> 63) ? 0 : 2;
        }
        Full |= Tag << (Physical * 2);
    }
    return Full;
}

static
VOID
PspFxsaveToFloatSave(PXMM_SAVE_AREA32 Fx, WOW64_FLOATING_SAVE_AREA *Fs)
{
    ULONG Index;

    Fs->ControlWord = 0xFFFF0000 | Fx->ControlWord;
    Fs->StatusWord = 0xFFFF0000 | Fx->StatusWord;
    Fs->TagWord = 0xFFFF0000 | PspFullTagFromFxsave(Fx);
    Fs->ErrorOffset = Fx->ErrorOffset;
    Fs->ErrorSelector = Fx->ErrorSelector | ((ULONG)Fx->ErrorOpcode << 16);
    Fs->DataOffset = Fx->DataOffset;
    Fs->DataSelector = 0xFFFF0000 | Fx->DataSelector;
    for (Index = 0; Index < 8; Index++)
        RtlCopyMemory(&Fs->RegisterArea[Index * 10], &Fx->FloatRegisters[Index], 10);
    Fs->Cr0NpxState = 0;
}

static
VOID
PspFloatSaveToFxsave(WOW64_FLOATING_SAVE_AREA *Fs, PXMM_SAVE_AREA32 Fx)
{
    ULONG Index;
    UCHAR Abridged = 0;

    Fx->ControlWord = (USHORT)Fs->ControlWord;
    Fx->StatusWord = (USHORT)Fs->StatusWord;
    for (Index = 0; Index < 8; Index++)
    {
        if (((Fs->TagWord >> (Index * 2)) & 3) != 3)
            Abridged |= (UCHAR)(1 << Index);
    }
    Fx->TagWord = Abridged;
    Fx->ErrorOffset = Fs->ErrorOffset;
    Fx->ErrorSelector = (USHORT)Fs->ErrorSelector;
    Fx->ErrorOpcode = (USHORT)(Fs->ErrorSelector >> 16);
    Fx->DataOffset = Fs->DataOffset;
    Fx->DataSelector = (USHORT)Fs->DataSelector;
    for (Index = 0; Index < 8; Index++)
    {
        RtlZeroMemory(&Fx->FloatRegisters[Index], sizeof(Fx->FloatRegisters[Index]));
        RtlCopyMemory(&Fx->FloatRegisters[Index], &Fs->RegisterArea[Index * 10], 10);
    }
}

NTSTATUS
NTAPI
PspArchCopyLiveWow64Context(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    ULONG Flags = Context->ContextFlags & ~WOW64_CONTEXT_i386;
    PCONTEXT Native;
    NTSTATUS Status;

    Native = ExAllocatePoolWithTag(PagedPool, sizeof(*Native), 'xtCP');
    if (!Native)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Native, sizeof(*Native));
    Native->ContextFlags = CONTEXT_ALL;
    Status = PspGetOrSetUserContext(Thread, Native, FALSE);
    if (!NT_SUCCESS(Status) || Native->SegCs != (KGDT64_R3_CMCODE | RPL_MASK))
    {
        ExFreePoolWithTag(Native, 'xtCP');
        return STATUS_NOT_FOUND;
    }

    if (!SetContext)
    {
        RtlZeroMemory(Context, sizeof(*Context));
        Context->ContextFlags = WOW64_CONTEXT_i386;
        if (Flags & WOW64_CONTEXT_INTEGER)
        {
            Context->Eax = (ULONG)Native->Rax;
            Context->Ebx = (ULONG)Native->Rbx;
            Context->Ecx = (ULONG)Native->Rcx;
            Context->Edx = (ULONG)Native->Rdx;
            Context->Esi = (ULONG)Native->Rsi;
            Context->Edi = (ULONG)Native->Rdi;
            Context->ContextFlags |= WOW64_CONTEXT_INTEGER;
        }
        if (Flags & WOW64_CONTEXT_CONTROL)
        {
            Context->Ebp = (ULONG)Native->Rbp;
            Context->Eip = (ULONG)Native->Rip;
            Context->Esp = (ULONG)Native->Rsp;
            Context->EFlags = Native->EFlags;
            Context->SegCs = Native->SegCs;
            Context->SegSs = Native->SegSs;
            Context->ContextFlags |= WOW64_CONTEXT_CONTROL;
        }
        if (Flags & WOW64_CONTEXT_SEGMENTS)
        {
            Context->SegDs = Native->SegDs;
            Context->SegEs = Native->SegEs;
            Context->SegFs = Native->SegFs;
            Context->SegGs = Native->SegGs;
            Context->ContextFlags |= WOW64_CONTEXT_SEGMENTS;
        }
        if (Flags & WOW64_CONTEXT_DEBUG_REGISTERS)
        {
            Context->Dr0 = (ULONG)Native->Dr0;
            Context->Dr1 = (ULONG)Native->Dr1;
            Context->Dr2 = (ULONG)Native->Dr2;
            Context->Dr3 = (ULONG)Native->Dr3;
            Context->Dr6 = (ULONG)Native->Dr6;
            Context->Dr7 = (ULONG)Native->Dr7;
            Context->ContextFlags |= WOW64_CONTEXT_DEBUG_REGISTERS;
        }
        if (Flags & WOW64_CONTEXT_FLOATING_POINT)
        {
            PspFxsaveToFloatSave(&Native->FltSave, &Context->FloatSave);
            Context->ContextFlags |= WOW64_CONTEXT_FLOATING_POINT;
        }
        if (Flags & WOW64_CONTEXT_EXTENDED_REGISTERS)
        {
            C_ASSERT(sizeof(Context->ExtendedRegisters) == sizeof(Native->FltSave));
            RtlCopyMemory(Context->ExtendedRegisters, &Native->FltSave, sizeof(Context->ExtendedRegisters));
            Context->ContextFlags |= WOW64_CONTEXT_EXTENDED_REGISTERS;
        }
    }
    else
    {
        if (Flags & WOW64_CONTEXT_INTEGER)
        {
            Native->Rax = Context->Eax;
            Native->Rbx = Context->Ebx;
            Native->Rcx = Context->Ecx;
            Native->Rdx = Context->Edx;
            Native->Rsi = Context->Esi;
            Native->Rdi = Context->Edi;
        }
        if (Flags & WOW64_CONTEXT_CONTROL)
        {
            Native->Rbp = Context->Ebp;
            Native->Rip = Context->Eip;
            Native->Rsp = Context->Esp;
            Native->EFlags = Context->EFlags;
        }
        if (Flags & WOW64_CONTEXT_DEBUG_REGISTERS)
        {
            Native->Dr0 = Context->Dr0;
            Native->Dr1 = Context->Dr1;
            Native->Dr2 = Context->Dr2;
            Native->Dr3 = Context->Dr3;
            Native->Dr6 = Context->Dr6;
            Native->Dr7 = Context->Dr7;
        }
        if (Flags & WOW64_CONTEXT_EXTENDED_REGISTERS)
            RtlCopyMemory(&Native->FltSave, Context->ExtendedRegisters, sizeof(Context->ExtendedRegisters));
        if (Flags & WOW64_CONTEXT_FLOATING_POINT)
            PspFloatSaveToFxsave(&Context->FloatSave, &Native->FltSave);
        Status = PspGetOrSetUserContext(Thread, Native, TRUE);
    }

    ExFreePoolWithTag(Native, 'xtCP');
    return Status;
}

/* EOF */

VOID
NTAPI
PsArchInitializeUserThreadContext(
    _Out_ PCONTEXT Context,
    _In_ PVOID ThreadStart,
    _In_ PVOID StartRoutine,
    _In_opt_ PVOID Argument,
    _In_ PVOID StackBase)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_FULL;
    Context->Rip = (ULONG64)ThreadStart;
    Context->Rcx = (ULONG64)StartRoutine;
    Context->Rdx = (ULONG64)Argument;
    Context->Rsp = (((ULONG64)StackBase - 6 * sizeof(PVOID)) & ~15ULL) - 8;
    Context->EFlags = EFLAGS_INTERRUPT_MASK;
    Context->SegCs = KGDT64_R3_CODE | RPL_MASK;
    Context->SegDs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegEs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
    Context->SegGs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegSs = KGDT64_R3_DATA | RPL_MASK;
    Context->MxCsr = INITIAL_MXCSR;
}
