/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exception dispatch shared by the architectures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KI_DEBUG_DATA_MODULE_LIMIT 1024

static
VOID
KiTouchUserImageDirectory(
    _In_ PVOID ImageBase,
    _In_ USHORT Directory)
{
    ULONG_PTR Page, Last;
    PVOID Data;
    ULONG Size;

    Data = RtlImageDirectoryEntryToData(ImageBase, TRUE, Directory, &Size);
    if ((Data == NULL) || (Size == 0))
        return;

    ProbeForRead(Data, Size, sizeof(UCHAR));

    /* One read per page brings in every page the directory spans, including a partial last one */
    Last = (ULONG_PTR)Data + Size - 1;
    for (Page = (ULONG_PTR)PAGE_ALIGN(Data); Page <= Last; Page += PAGE_SIZE)
        (VOID)*(volatile UCHAR *)Page;
}

/* The kernel debugger cannot take page faults, so the user modules' debug and unwind data are paged in for it first */
VOID
KiPrepareUserDebugData(VOID)
{
    PLDR_DATA_TABLE_ENTRY Module;
    PLIST_ENTRY Head, Entry;
    PPEB_LDR_DATA Ldr;
    PVOID ImageBase;
    ULONG Count;
    PPEB Peb;

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    Peb = PsGetCurrentProcess()->Peb;
    if (!KdDebuggerEnabled || (Peb == NULL))
        return;

    _enable();

    /* The loader list is user memory: every link is probed, and the walk is bounded because the list may loop */
    _SEH2_TRY
    {
        ProbeForRead(Peb, RTL_SIZEOF_THROUGH_FIELD(PEB, Ldr), sizeof(ULONG_PTR));
        Ldr = Peb->Ldr;
        if (Ldr == NULL)
            _SEH2_LEAVE;

        ProbeForRead(Ldr, sizeof(*Ldr), sizeof(ULONG_PTR));
        Head = &Ldr->InLoadOrderModuleList;
        Entry = Head->Flink;

        for (Count = 0; (Entry != Head) && (Count < KI_DEBUG_DATA_MODULE_LIMIT); Count++)
        {
            Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            ProbeForRead(Module, RTL_SIZEOF_THROUGH_FIELD(LDR_DATA_TABLE_ENTRY, DllBase), sizeof(ULONG_PTR));

            ImageBase = Module->DllBase;
            ProbeForRead(ImageBase, sizeof(IMAGE_DOS_HEADER), sizeof(USHORT));

            KiTouchUserImageDirectory(ImageBase, IMAGE_DIRECTORY_ENTRY_DEBUG);
            KiTouchUserImageDirectory(ImageBase, IMAGE_DIRECTORY_ENTRY_EXCEPTION);
            Entry = Module->InLoadOrderLinks.Flink;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;
}

static
VOID
KiCaptureExceptionContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PCONTEXT Context)
{
    /* Zeroed so no kernel stack contents can reach user mode */
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = KiArchExceptionContextFlags(PreviousMode);
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, Context);
    KiArchFinishExceptionContext(TrapFrame, Context);
}

static
VOID
KiNormalizeException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PCONTEXT Context)
{
    switch (ExceptionRecord->ExceptionCode)
    {
        case STATUS_BREAKPOINT:
            /* Report the breakpoint at the instruction itself, wherever the trap left the PC */
            KeSetContextPc(Context, KeGetContextPc(Context) - KI_BREAKPOINT_PC_ADJUST);
            break;

        case KI_EXCEPTION_ACCESS_VIOLATION:
            ExceptionRecord->ExceptionCode = STATUS_ACCESS_VIOLATION;
            break;
    }
}

static
BOOLEAN
KiForwardToDebugPort(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ BOOLEAN SecondChance)
{
    NTSTATUS Code = ExceptionRecord->ExceptionCode;
    BOOLEAN Handled;

    ExceptionRecord->ExceptionCode = KiArchDebugPortExceptionCode(TrapFrame, Code);
    Handled = DbgkForwardException(ExceptionRecord, TRUE, SecondChance);
    ExceptionRecord->ExceptionCode = Code;
    return Handled;
}

static
DECLSPEC_NORETURN
VOID
KiBugCheckUnhandledException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG_PTR Parameter0 = 0;
    ULONG_PTR Parameter1 = 0;

    ASSERT(ExceptionRecord != NULL);
    ASSERT(ExceptionRecord->NumberParameters <= EXCEPTION_MAXIMUM_PARAMETERS);
    if (ExceptionRecord->NumberParameters > 0)
        Parameter0 = ExceptionRecord->ExceptionInformation[0];
    if (ExceptionRecord->NumberParameters > 1)
        Parameter1 = ExceptionRecord->ExceptionInformation[1];
    KeBugCheckWithTf(KMODE_EXCEPTION_NOT_HANDLED,
                     ExceptionRecord->ExceptionCode,
                     (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                     Parameter0,
                     Parameter1,
                     TrapFrame);
}

static
BOOLEAN
KiDispatchKernelException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN FirstChance)
{
    if (FirstChance)
    {
        if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, Context, KernelMode, FALSE))
            return TRUE;

        if (RtlDispatchException(ExceptionRecord, Context))
            return TRUE;
    }

    KiArchReportUnhandledKernelFault(TrapFrame);

    if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, Context, KernelMode, TRUE))
        return TRUE;

    if (KiArchLastChanceKernelDebugger(ExceptionRecord, Context))
        return TRUE;

    /* A breakpoint met while already bugchecking would only recurse */
    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) && KeBugCheckActive)
        HalHaltSystem();

    KiBugCheckUnhandledException(ExceptionRecord, TrapFrame);
}

static
BOOLEAN
KiOfferUserExceptionFirstChance(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PBOOLEAN Resumed)
{
    *Resumed = FALSE;

    /* Kernel debugger first unless a user debugger is attached or user exceptions are ignored; debug services always go to it */
    if ((!PsGetCurrentProcess()->DebugPort && !KdIgnoreUmExceptions) ||
        KdIsThisAKdTrap(ExceptionRecord, Context, PreviousMode))
    {
        ULONG_PTR Pc = KeGetContextPc(Context);

        KiPrepareUserDebugData();

        if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, Context, PreviousMode, FALSE))
            return TRUE;

        /* The debugger did not take it, so its changes are dropped */
        KeSetContextPc(Context, Pc);
    }

    if (ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT)
        KiArchPrepareUserBreakpoint(ExceptionRecord, TrapFrame);

    if (KiForwardToDebugPort(ExceptionRecord, TrapFrame, FALSE) ||
        KiDispatchExceptionToUser(TrapFrame, Context, ExceptionRecord))
    {
        *Resumed = TRUE;
    }

    return FALSE;
}

static
VOID
KiDispatchUserException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN FirstChance,
    _Out_ PBOOLEAN Handled)
{
    BOOLEAN FastFail = (BOOLEAN)(ExceptionRecord->ExceptionCode == STATUS_STACK_BUFFER_OVERRUN);
    BOOLEAN Resumed;

    *Handled = FALSE;

    if (PspPicoDispatchException(ExceptionRecord, ExceptionFrame, TrapFrame))
        return;

    KiArchBeginUserExceptionDispatch(TrapFrame);

    /* A fast fail never reaches user-mode handlers */
    if (FirstChance && !FastFail)
    {
        *Handled = KiOfferUserExceptionFirstChance(ExceptionRecord, ExceptionFrame, TrapFrame, Context, PreviousMode, &Resumed);
        if (*Handled || Resumed)
            return;
    }

    if (KiForwardToDebugPort(ExceptionRecord, TrapFrame, TRUE))
        return;

    if (!FastFail && DbgkForwardException(ExceptionRecord, FALSE, TRUE))
        return;

    /* Third chance: nothing handled it, so the process ends */
    DPRINT1("Kill %.16s, ExceptionCode: %lx, ExceptionAddress: %p, BaseAddress: %p\n",
            PsGetCurrentProcess()->ImageFileName,
            ExceptionRecord->ExceptionCode,
            ExceptionRecord->ExceptionAddress,
            PsGetCurrentProcess()->SectionBaseAddress);

    ZwTerminateProcess(NtCurrentProcess(), ExceptionRecord->ExceptionCode);

    /* Self-termination does not return; failing to end the process must not resume the fault */
    KiBugCheckUnhandledException(ExceptionRecord, TrapFrame);
}

VOID
NTAPI
KiDispatchException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN FirstChance)
{
    CONTEXT Context;
    BOOLEAN Handled;

    ASSERT(ExceptionRecord != NULL);
    ASSERT(TrapFrame != NULL);

    if ((PreviousMode == KernelMode) && (ExceptionRecord->ExceptionCode != STATUS_BREAKPOINT))
    {
        KiArchBootReadout(BootReadoutException,
                          ExceptionRecord->ExceptionCode,
                          (ULONG64)ExceptionRecord->ExceptionAddress,
                          ExceptionRecord->ExceptionInformation[0],
                          ExceptionRecord->ExceptionInformation[1],
                          KeGetTrapFrameStackRegister(TrapFrame));
    }

    KeGetCurrentPrcb()->KeExceptionDispatchCount++;

    KiCaptureExceptionContext(TrapFrame, ExceptionFrame, PreviousMode, &Context);
    KiNormalizeException(ExceptionRecord, &Context);

    if (PreviousMode == KernelMode)
        Handled = KiDispatchKernelException(ExceptionRecord, ExceptionFrame, TrapFrame, &Context, FirstChance);
    else
        KiDispatchUserException(ExceptionRecord, ExceptionFrame, TrapFrame, &Context, PreviousMode, FirstChance, &Handled);

    /* A debugger or handler that took the exception may have changed the context */
    if (Handled)
        KeContextToTrapFrame(&Context, ExceptionFrame, TrapFrame, Context.ContextFlags, PreviousMode);
}
