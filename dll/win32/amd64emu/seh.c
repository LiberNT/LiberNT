/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Dispatch and unwind AMD64 exceptions in processes the native loader owns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "chpeamd64.h"

#define EMU_X64_MAX_FRAMES 1024
#define EMU_RUN_FRAME_SIZE 128

extern UCHAR EmuBoundaryResume[];

static LONG EmuBoundaryEstablisher;

static
BOOLEAN
EmuIsGuestStack(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG64 Address,
    _In_ ULONG Size)
{
    return !(Address & 7) && Address >= (ULONG_PTR)Thread->GuestStack.Limit &&
           Address + Size <= (ULONG_PTR)Thread->GuestStack.Base;
}

static
BOOLEAN
EmuIsSegmentEnd(
    _In_ ULONG64 Rip)
{
    return !Rip || Rip == EmuGuestReturnStub;
}

static
VOID
EmuBoundaryContext(
    _In_ PEMU_BOUNDARY Boundary,
    _Out_ AMD64_CONTEXT *Context)
{
    PEMU_GUEST_CALL Call;

    if (Boundary->Context)
    {
        *Context = *Boundary->Context;
        return;
    }
    for (Call = Boundary->Thread->GuestCall; Call && Call != Boundary->GuestCall; Call = Call->Previous)
    {
        if (Call->Boundary == Boundary)
        {
            *Context = Call->Entry;
            return;
        }
    }
    Context->ContextFlags = CONTEXT_AMD64_FULL;
    EmuCoreGetContext(Boundary->Thread->Cpu, Context);
}

PEMU_AMD64_RUNTIME_FUNCTION
EmuX64LookupFunctionTable(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PULONG Length)
{
    PIMAGE_NT_HEADERS Headers;
    PVOID Base;

    *ImageBase = 0;
    *Length = 0;
    if (!RtlPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, &Base)) return NULL;
    Headers = RtlImageNtHeader(Base);
    if (!Headers || Headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return NULL;
    return ChpepAmd64LookupFunctionTable(ControlPc, ImageBase, Length);
}

PEMU_AMD64_RUNTIME_FUNCTION
EmuX64LookupFunctionEntry(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase)
{
    PIMAGE_NT_HEADERS Headers;
    PVOID Base;

    *ImageBase = 0;
    if (RtlPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, &Base))
    {
        Headers = RtlImageNtHeader(Base);
        if (!Headers || Headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return NULL;
    }
    return ChpepAmd64LookupFunctionEntry(ControlPc, ImageBase, NULL);
}

ULONG64
EmuX64VirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_opt_ PEMU_AMD64_RUNTIME_FUNCTION FunctionEntry,
    _Inout_ AMD64_CONTEXT *Context,
    _Out_opt_ PVOID *HandlerData,
    _Out_opt_ PULONG64 EstablisherFrame,
    _Inout_opt_ PEMU_AMD64_CONTEXT_POINTERS ContextPointers)
{
    PVOID Data = NULL;
    ULONG64 Frame = Context->Rsp;
    ULONG64 Handler = 0;

    if (FunctionEntry)
    {
        Handler = (ULONG64)(ULONG_PTR)ChpepAmd64VirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry,
                                                             Context, &Data, &Frame, ContextPointers);
    }
    else if (Context->Rsp)
    {
        Context->Rip = *(PULONG64)(ULONG_PTR)Context->Rsp;
        Context->Rsp += sizeof(ULONG64);
    }
    if (HandlerData) *HandlerData = Data;
    if (EstablisherFrame) *EstablisherFrame = Frame;
    return Handler;
}

static
BOOLEAN
EmuX64StepFrame(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG HandlerType,
    _Inout_ PEMU_AMD64_DISPATCHER_CONTEXT Dispatcher,
    _Inout_ AMD64_CONTEXT *Context)
{
    ULONG64 ImageBase, Rsp = Context->Rsp;

    if (!EmuIsGuestStack(Thread, Context->Rsp, sizeof(ULONG64))) return FALSE;
    Dispatcher->ControlPc = Context->Rip;
    Dispatcher->FunctionEntry = EmuX64LookupFunctionEntry(Context->Rip, &ImageBase);
    Dispatcher->ImageBase = ImageBase;
    Dispatcher->LanguageHandler = EmuX64VirtualUnwind(HandlerType, ImageBase, Dispatcher->ControlPc,
                                                      Dispatcher->FunctionEntry, Context, &Dispatcher->HandlerData,
                                                      &Dispatcher->EstablisherFrame, NULL);
    if (Context->Rip == Dispatcher->ControlPc && Context->Rsp == Rsp) return FALSE;
    return EmuIsGuestStack(Thread, Dispatcher->EstablisherFrame, 0);
}

static
EXCEPTION_DISPOSITION
EmuX64CallHandler(
    _Inout_ PEXCEPTION_RECORD Record,
    _Inout_ AMD64_CONTEXT *Context,
    _Inout_ PEMU_AMD64_DISPATCHER_CONTEXT Dispatcher)
{
    ULONG64 Arguments[4];

    Arguments[0] = (ULONG_PTR)Record;
    Arguments[1] = Dispatcher->EstablisherFrame;
    Arguments[2] = (ULONG_PTR)Context;
    Arguments[3] = (ULONG_PTR)Dispatcher;
    return (EXCEPTION_DISPOSITION)(LONG)EmuCallGuest(Dispatcher->LanguageHandler, "wqqqq", Arguments);
}

static
EXCEPTION_DISPOSITION
EmuX64SearchFrames(
    _In_ PEMU_BOUNDARY Boundary,
    _Inout_ PEXCEPTION_RECORD Record)
{
    PEMU_THREAD Thread = Boundary->Thread;
    EMU_AMD64_DISPATCHER_CONTEXT Dispatcher;
    AMD64_CONTEXT Local, Unwound, *Context = &Local;
    EXCEPTION_DISPOSITION Disposition;
    ULONG64 NestedFrame = 0;
    ULONG Frames;

    if (Boundary->Unwind && (Boundary->Unwind->Active || Boundary->Unwind->Consolidating))
        Local = Boundary->Unwind->Context;
    else if (Boundary->Context)
        Context = Boundary->Context;
    else
        EmuBoundaryContext(Boundary, &Local);

    Unwound = *Context;
    RtlZeroMemory(&Dispatcher, sizeof(Dispatcher));
    Dispatcher.ContextRecord = &Unwound;
    for (Frames = 0; Frames < EMU_X64_MAX_FRAMES; Frames++)
    {
        if (EmuIsSegmentEnd(Unwound.Rip)) return ExceptionContinueSearch;
        if (!EmuX64StepFrame(Thread, UNW_FLAG_EHANDLER, &Dispatcher, &Unwound)) break;
        if (!Dispatcher.LanguageHandler) continue;

        Dispatcher.ScopeIndex = 0;
        Disposition = EmuX64CallHandler(Record, Context, &Dispatcher);
        if (Dispatcher.EstablisherFrame == NestedFrame)
        {
            Record->ExceptionFlags &= ~EXCEPTION_NESTED_CALL;
            NestedFrame = 0;
        }
        switch (Disposition)
        {
            case ExceptionContinueExecution:
                return ExceptionContinueExecution;

            case ExceptionContinueSearch:
                break;

            case ExceptionNestedException:
                Record->ExceptionFlags |= EXCEPTION_NESTED_CALL;
                if (Dispatcher.EstablisherFrame > NestedFrame) NestedFrame = Dispatcher.EstablisherFrame;
                break;

            default:
                RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
        }
    }
    Record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
    return ExceptionContinueSearch;
}

static
BOOLEAN
EmuX64UnwindFrames(
    _In_ PEMU_BOUNDARY Boundary,
    _Inout_ PEMU_X64_UNWIND Unwind)
{
    PEMU_THREAD Thread = Boundary->Thread;
    PEMU_AMD64_DISPATCHER_CONTEXT Dispatcher = &Unwind->Dispatcher;
    PEXCEPTION_RECORD Record = Unwind->Record;
    EXCEPTION_DISPOSITION Disposition;
    BOOLEAN Collided = Unwind->Active;
    ULONG Frames;

    if (Unwind->Consolidating)
        Unwind->Unwound = Unwind->Context;
    else if (!Collided)
    {
        EmuBoundaryContext(Boundary, &Unwind->Context);
        Unwind->Unwound = Unwind->Context;
    }
    Unwind->Consolidating = FALSE;
    Record->ExceptionFlags |= EXCEPTION_UNWINDING;
    Dispatcher->ContextRecord = &Unwind->Context;
    Dispatcher->HistoryTable = NULL;
    Dispatcher->TargetIp = Unwind->TargetIp;

    for (Frames = 0; Frames < EMU_X64_MAX_FRAMES; Frames++)
    {
        if (Collided)
        {
            Unwind->Unwound = Unwind->Context;
            Dispatcher->LanguageHandler = EmuX64VirtualUnwind(UNW_FLAG_UHANDLER, Dispatcher->ImageBase,
                                                              Dispatcher->ControlPc, Dispatcher->FunctionEntry,
                                                              &Unwind->Unwound, &Dispatcher->HandlerData,
                                                              &Dispatcher->EstablisherFrame, NULL);
            Record->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
        }
        else
        {
            if (EmuIsSegmentEnd(Unwind->Unwound.Rip)) return FALSE;
            if (!EmuX64StepFrame(Thread, UNW_FLAG_UHANDLER, Dispatcher, &Unwind->Unwound))
                RtlRaiseStatus(STATUS_BAD_STACK);
            Dispatcher->ScopeIndex = 0;
        }
        Collided = FALSE;

        if (Dispatcher->LanguageHandler)
        {
            if (Unwind->TargetFrame && Dispatcher->EstablisherFrame > Unwind->TargetFrame)
                RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
            if (Dispatcher->EstablisherFrame == Unwind->TargetFrame) Record->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
            Unwind->Unwound.Rax = Unwind->ReturnValue;
            Unwind->Active = TRUE;
            Disposition = EmuX64CallHandler(Record, &Unwind->Context, Dispatcher);
            Unwind->Active = FALSE;
            Record->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND | EXCEPTION_COLLIDED_UNWIND);
            if (Disposition != ExceptionContinueSearch) RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
        }
        if (Dispatcher->FunctionEntry && Dispatcher->EstablisherFrame == Unwind->TargetFrame) return TRUE;
        Unwind->Context = Unwind->Unwound;
    }
    RtlRaiseStatus(STATUS_BAD_STACK);
}

static
PEMU_X64_UNWIND
EmuX64AcquireUnwind(
    _Inout_ PEMU_BOUNDARY Boundary)
{
    if (!Boundary->Unwind)
        Boundary->Unwind = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EMU_X64_UNWIND));
    if (!Boundary->Unwind) RtlRaiseStatus(STATUS_NO_MEMORY);
    return Boundary->Unwind;
}

static
VOID
EmuX64ReleaseUnwind(
    _Inout_ PEMU_BOUNDARY Boundary)
{
    if (Boundary->Unwind) RtlFreeHeap(RtlGetProcessHeap(), 0, Boundary->Unwind);
    Boundary->Unwind = NULL;
}

static
VOID
EmuX64LeaveSegment(
    _Inout_ PEMU_BOUNDARY Boundary)
{
    PEMU_THREAD Thread = Boundary->Thread;
    PEMU_GUEST_CALL Call = Boundary->GuestCall;
    PEMU_REGISTERS Registers = EmuCoreGetRegisters(Thread->Cpu);

    EmuX64ReleaseUnwind(Boundary);
    Thread->Boundary = Boundary->Previous;
    Thread->GuestCall = Call->Previous;
    if (Call->Boundary)
    {
        EmuCoreSetContext(Thread->Cpu, &Call->Entry);
    }
    else
    {
        Registers->Gpr[EmuRsp] = Call->Rsp;
        Registers->Rip = Call->Rip;
    }
    EmuCoreSetFrame(Thread->Cpu, Call->Frame);
    Thread->HostDepth = Call->HostDepth;
}

EXCEPTION_DISPOSITION
NTAPI
EmuBoundaryHandler(
    _Inout_ PEXCEPTION_RECORD Record,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT NativeContext,
    _Inout_ PVOID NativeDispatcherContext)
{
    PDISPATCHER_CONTEXT Dispatcher = NativeDispatcherContext;
    PEMU_BOUNDARY Boundary = *(PEMU_BOUNDARY *)(ULONG_PTR)Dispatcher->ContextRecord->Sp;
    PEMU_X64_UNWIND Unwind;

    UNREFERENCED_PARAMETER(EstablisherFrame);
    UNREFERENCED_PARAMETER(NativeContext);

    if (!(Record->ExceptionFlags & EXCEPTION_UNWIND)) return EmuX64SearchFrames(Boundary, Record);
    if (Record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) return ExceptionContinueSearch;

    Unwind = EmuX64AcquireUnwind(Boundary);
    Unwind->TargetFrame = 0;
    Unwind->TargetIp = 0;
    Unwind->ReturnValue = 0;
    Unwind->Record = Record;
    EmuX64UnwindFrames(Boundary, Unwind);
    EmuX64LeaveSegment(Boundary);
    return ExceptionContinueSearch;
}

VOID
EmuBoundaryContinue(
    _In_ PEMU_BOUNDARY Boundary)
{
    PEMU_X64_UNWIND Unwind = Boundary->Unwind;

    if (!EmuX64UnwindFrames(Boundary, Unwind)) RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
    if (Unwind->Record->ExceptionCode != STATUS_UNWIND_CONSOLIDATE) Unwind->Context.Rip = Unwind->TargetIp;
    Unwind->Context.Rax = Unwind->ReturnValue;
    Unwind->Consolidating = TRUE;
    EmuX64RestoreContext(Boundary->Thread, &Unwind->Context, Unwind->Record);
    EmuX64ReleaseUnwind(Boundary);
}

static
PEMU_BOUNDARY
EmuX64TargetBoundary(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG64 TargetFrame)
{
    PEMU_BOUNDARY Boundary;

    for (Boundary = Thread->Boundary; Boundary; Boundary = Boundary->Previous)
    {
        if (TargetFrame < Boundary->GuestCall->Rsp) return Boundary;
    }
    return NULL;
}

VOID
EmuX64UnwindEx(
    _In_ PEMU_THREAD Thread,
    _In_opt_ ULONG64 TargetFrame,
    _In_opt_ ULONG64 TargetIp,
    _In_opt_ PEXCEPTION_RECORD Record,
    _In_ ULONG64 ReturnValue)
{
    PEMU_BOUNDARY Boundary;
    PEMU_X64_UNWIND Unwind;
    CONTEXT Native;

    if (!TargetFrame)
    {
        RtlUnwindEx(NULL, NULL, Record, (PVOID)(ULONG_PTR)ReturnValue, &Native, NULL);
        RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
    }

    Boundary = EmuX64TargetBoundary(Thread, TargetFrame);
    if (!Boundary) RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
    Unwind = EmuX64AcquireUnwind(Boundary);
    if (!Record)
    {
        RtlZeroMemory(&Unwind->RecordCopy, sizeof(Unwind->RecordCopy));
        Unwind->RecordCopy.ExceptionCode = STATUS_UNWIND;
        Unwind->RecordCopy.ExceptionAddress = (PVOID)(ULONG_PTR)EmuCoreGetRegisters(Thread->Cpu)->Rip;
    }
    else if (Record != &Unwind->RecordCopy)
    {
        Unwind->RecordCopy = *Record;
    }
    Unwind->Record = &Unwind->RecordCopy;
    Unwind->TargetFrame = TargetFrame;
    Unwind->TargetIp = TargetIp;
    Unwind->ReturnValue = ReturnValue;
    RtlUnwindEx((PVOID)(ULONG_PTR)(Boundary->RunFrame + EmuBoundaryEstablisher), EmuBoundaryResume, Unwind->Record,
                Boundary, &Native, NULL);
    RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
}

VOID
EmuX64RestoreContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ AMD64_CONTEXT *Context,
    _In_opt_ PEXCEPTION_RECORD Record)
{
    PEMU_AMD64_JUMP_BUFFER JumpBuffer;
    ULONG64 Argument;

    if (Record && Record->ExceptionCode == STATUS_UNWIND_CONSOLIDATE && Record->NumberParameters >= 1)
    {
        EmuCoreGetRegisters(Thread->Cpu)->Gpr[EmuRsp] = Context->Rsp;
        Argument = (ULONG_PTR)Record;
        Context->Rip = EmuCallGuest(Record->ExceptionInformation[0], "qq", &Argument);
    }
    else if (Record && Record->ExceptionCode == STATUS_LONGJUMP && Record->NumberParameters >= 1)
    {
        JumpBuffer = (PEMU_AMD64_JUMP_BUFFER)Record->ExceptionInformation[0];
        Context->Rbx = JumpBuffer->Rbx;
        Context->Rsp = JumpBuffer->Rsp;
        Context->Rbp = JumpBuffer->Rbp;
        Context->Rsi = JumpBuffer->Rsi;
        Context->Rdi = JumpBuffer->Rdi;
        Context->R12 = JumpBuffer->R12;
        Context->R13 = JumpBuffer->R13;
        Context->R14 = JumpBuffer->R14;
        Context->R15 = JumpBuffer->R15;
        Context->Rip = JumpBuffer->Rip;
        Context->MxCsr = JumpBuffer->MxCsr;
        Context->FltSave.MxCsr = JumpBuffer->MxCsr;
        Context->FltSave.ControlWord = JumpBuffer->FpCsr;
        RtlCopyMemory(&Context->Xmm6, JumpBuffer->Xmm, sizeof(JumpBuffer->Xmm));
    }
    EmuCoreSetContext(Thread->Cpu, Context);
}

VOID
EmuX64DispatchService(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    PEXCEPTION_RECORD Record = (PEXCEPTION_RECORD)Registers->Gpr[EmuR10];
    AMD64_CONTEXT *Context = (AMD64_CONTEXT *)Registers->Gpr[EmuRdx];
    volatile BOOLEAN Raised = FALSE;
    CONTEXT Native;

    Thread->Boundary->Context = Context;
    RtlCaptureContext(&Native);
    if (!Raised)
    {
        Raised = TRUE;
        RtlRaiseStatus(NtRaiseException(Record, &Native, TRUE));
    }
    EmuCoreSetContext(Thread->Cpu, Context);
}

typedef struct _EMU_VECTORED_HANDLER
{
    LIST_ENTRY Link;
    ULONG64 Handler;
    ULONG References;
    BOOLEAN Removed;
} EMU_VECTORED_HANDLER, *PEMU_VECTORED_HANDLER;

static RTL_SRWLOCK EmuVectoredLock;
static LIST_ENTRY EmuVectoredHandlers[2] =
{
    { &EmuVectoredHandlers[0], &EmuVectoredHandlers[0] },
    { &EmuVectoredHandlers[1], &EmuVectoredHandlers[1] },
};

PVOID
EmuX64AddVectoredHandler(
    _In_ BOOLEAN ContinueHandler,
    _In_ ULONG First,
    _In_ ULONG64 Handler)
{
    PEMU_VECTORED_HANDLER Entry;

    if (!Handler) return NULL;
    Entry = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Entry));
    if (!Entry) return NULL;
    Entry->Handler = Handler;
    RtlAcquireSRWLockExclusive(&EmuVectoredLock);
    if (First)
        InsertHeadList(&EmuVectoredHandlers[ContinueHandler != FALSE], &Entry->Link);
    else
        InsertTailList(&EmuVectoredHandlers[ContinueHandler != FALSE], &Entry->Link);
    RtlReleaseSRWLockExclusive(&EmuVectoredLock);
    return Entry;
}

ULONG
EmuX64RemoveVectoredHandler(
    _In_ BOOLEAN ContinueHandler,
    _In_ PVOID Handle)
{
    PLIST_ENTRY Head = &EmuVectoredHandlers[ContinueHandler != FALSE], Link;
    PEMU_VECTORED_HANDLER Entry = NULL;
    BOOLEAN Free = FALSE;

    RtlAcquireSRWLockExclusive(&EmuVectoredLock);
    for (Link = Head->Flink; Link != Head; Link = Link->Flink)
    {
        if (Link != &((PEMU_VECTORED_HANDLER)Handle)->Link) continue;
        Entry = Handle;
        RemoveEntryList(&Entry->Link);
        Entry->Removed = TRUE;
        Free = !Entry->References;
        break;
    }
    RtlReleaseSRWLockExclusive(&EmuVectoredLock);
    if (Free) RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
    return Entry != NULL;
}

static
VOID
EmuX64ReleaseVectoredHandler(
    _Inout_ PEMU_VECTORED_HANDLER Entry)
{
    BOOLEAN Free;

    RtlAcquireSRWLockExclusive(&EmuVectoredLock);
    Free = !--Entry->References && Entry->Removed;
    RtlReleaseSRWLockExclusive(&EmuVectoredLock);
    if (Free) RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
}

LONG
EmuX64CallVectoredHandlers(
    _In_ PEMU_THREAD Thread,
    _In_ PEXCEPTION_RECORD Record,
    _In_ BOOLEAN ContinueHandlers)
{
    PLIST_ENTRY Head = &EmuVectoredHandlers[ContinueHandlers != FALSE], Link;
    PEMU_VECTORED_HANDLER *Snapshot;
    EXCEPTION_POINTERS Pointers;
    AMD64_CONTEXT Local;
    LONG Result = EXCEPTION_CONTINUE_SEARCH;
    ULONG64 Argument;
    ULONG Count = 0, Index;

    if (!Thread->Boundary) return EXCEPTION_CONTINUE_SEARCH;

    RtlAcquireSRWLockExclusive(&EmuVectoredLock);
    for (Link = Head->Flink; Link != Head; Link = Link->Flink) Count++;
    Snapshot = Count ? RtlAllocateHeap(RtlGetProcessHeap(), 0, Count * sizeof(*Snapshot)) : NULL;
    for (Link = Head->Flink, Index = 0; Snapshot && Link != Head; Link = Link->Flink, Index++)
    {
        Snapshot[Index] = CONTAINING_RECORD(Link, EMU_VECTORED_HANDLER, Link);
        Snapshot[Index]->References++;
    }
    RtlReleaseSRWLockExclusive(&EmuVectoredLock);
    if (!Snapshot) return EXCEPTION_CONTINUE_SEARCH;

    Pointers.ExceptionRecord = Record;
    if (Thread->Boundary->Context)
    {
        Pointers.ContextRecord = (PCONTEXT)Thread->Boundary->Context;
    }
    else
    {
        EmuBoundaryContext(Thread->Boundary, &Local);
        Pointers.ContextRecord = (PCONTEXT)&Local;
    }
    Argument = (ULONG_PTR)&Pointers;
    for (Index = 0; Index < Count; Index++)
    {
        Result = (LONG)EmuCallGuest(Snapshot[Index]->Handler, "wq", &Argument);
        if (Result == EXCEPTION_CONTINUE_EXECUTION) break;
    }
    for (Index = 0; Index < Count; Index++) EmuX64ReleaseVectoredHandler(Snapshot[Index]);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Snapshot);
    return Result;
}

static
LONG
NTAPI
EmuVectoredContinueHandler(
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    PEMU_THREAD Thread = EmuCurrentThread();

    if (!Thread || !Thread->Cpu) return EXCEPTION_CONTINUE_SEARCH;
    return EmuX64CallVectoredHandlers(Thread, ExceptionInfo->ExceptionRecord, TRUE);
}

EXCEPTION_DISPOSITION
EmuX64CSpecificHandler(
    _Inout_ PEXCEPTION_RECORD Record,
    _In_ ULONG64 EstablisherFrame,
    _Inout_ AMD64_CONTEXT *Context,
    _Inout_ PEMU_AMD64_DISPATCHER_CONTEXT DispatcherContext)
{
    PEMU_AMD64_SCOPE_TABLE ScopeTable = DispatcherContext->HandlerData;
    ULONG64 ImageBase = DispatcherContext->ImageBase;
    ULONG64 ControlPc = DispatcherContext->ControlPc;
    EXCEPTION_POINTERS Pointers;
    ULONG64 Arguments[2];
    ULONG Index;
    LONG Filter;

    if (!ScopeTable) return ExceptionContinueSearch;

    if (Record->ExceptionFlags & EXCEPTION_UNWIND)
    {
        for (Index = DispatcherContext->ScopeIndex; Index < ScopeTable->Count; Index++)
        {
            if (ControlPc < ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress ||
                ControlPc >= ImageBase + ScopeTable->ScopeRecord[Index].EndAddress ||
                ScopeTable->ScopeRecord[Index].JumpTarget)
            {
                continue;
            }
            if ((Record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                DispatcherContext->TargetIp >= ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress &&
                DispatcherContext->TargetIp < ImageBase + ScopeTable->ScopeRecord[Index].EndAddress)
            {
                break;
            }
            DispatcherContext->ScopeIndex = Index + 1;
            Arguments[0] = TRUE;
            Arguments[1] = EstablisherFrame;
            EmuCallGuest(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress, "vBq", Arguments);
        }
        return ExceptionContinueSearch;
    }

    Pointers.ExceptionRecord = Record;
    Pointers.ContextRecord = (PCONTEXT)Context;
    for (Index = DispatcherContext->ScopeIndex; Index < ScopeTable->Count; Index++)
    {
        if (ControlPc < ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress ||
            ControlPc >= ImageBase + ScopeTable->ScopeRecord[Index].EndAddress ||
            !ScopeTable->ScopeRecord[Index].JumpTarget)
        {
            continue;
        }
        Filter = EXCEPTION_EXECUTE_HANDLER;
        if (ScopeTable->ScopeRecord[Index].HandlerAddress != EXCEPTION_EXECUTE_HANDLER)
        {
            Arguments[0] = (ULONG_PTR)&Pointers;
            Arguments[1] = EstablisherFrame;
            Filter = (LONG)EmuCallGuest(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress, "wqq", Arguments);
        }
        if (Filter == EXCEPTION_CONTINUE_SEARCH) continue;
        if (Filter == EXCEPTION_CONTINUE_EXECUTION) return ExceptionContinueExecution;
        EmuX64UnwindEx(EmuCurrentThread(), EstablisherFrame, ImageBase + ScopeTable->ScopeRecord[Index].JumpTarget,
                       Record, (ULONG)Record->ExceptionCode);
    }
    return ExceptionContinueSearch;
}

NTSTATUS
EmuX64InitializeDispatch(VOID)
{
    UCHAR Code[] = { 0x4C, 0x8B, 0xD1, 0xB8, 0, 0, 0, 0, 0x0F, 0x05, 0xCC };
    ULONG Service = EMU_DISPATCH_SERVICE;
    PRISCV64_UNWIND_INFO_V1 UnwindInfo;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase;

    FunctionEntry = RtlLookupFunctionEntry((ULONG_PTR)EmuBoundaryResume, &ImageBase, NULL);
    if (!FunctionEntry) return STATUS_BAD_FUNCTION_TABLE;
    UnwindInfo = (PRISCV64_UNWIND_INFO_V1)(ULONG_PTR)(ImageBase + FunctionEntry->UnwindData);
    EmuBoundaryEstablisher = EMU_RUN_FRAME_SIZE + UnwindInfo->EstablisherFrameOffset;

    RtlCopyMemory(&Code[4], &Service, sizeof(Service));
    EmuProcess.KiUserExceptionDispatcher = EmuCreateStub(Code, sizeof(Code));
    if (!EmuProcess.KiUserExceptionDispatcher) return STATUS_NO_MEMORY;
    return RtlAddVectoredContinueHandler(1, EmuVectoredContinueHandler) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}
