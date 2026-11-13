/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 system-service dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * System-service convention (see sdk/include/asm/syscalls.inc and syscall.S):
 * $a7 carries the service id, $a0-$a7 the first eight arguments and the rest
 * travel on the user stack. The routine pointer reaches the callee in $t0,
 * which KiLa64InvokeSystemService fills in from its first parameter.
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

/* Keep in sync with the assembly invocation frame; reject, never truncate. */
#define KI_LA64_MAX_SERVICE_ARGUMENTS 32
C_ASSERT(KI_LA64_MAX_SERVICE_ARGUMENTS * sizeof(ULONG_PTR) == 256);
ULONG_PTR NTAPI KiLa64InvokeSystemService(PVOID Routine, PULONG_PTR Arguments);
typedef ULONG_PTR (NTAPI *PKI_LA64_REGISTER_SERVICE)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                                     ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);
NTSTATUS NTAPI PsConvertToGuiThread(VOID);

/* The private debugger service id: 0xfffffffd, far above any SSDT entry. */
#define KI_LA64_DEBUG_SERVICE_ID 0xFFFFFFFDULL

static
ULONG_PTR
KiLa64UserDebugService(_Inout_ PKTRAP_FRAME Frame)
{
    BOOLEAN Handled;

    /* The common workers probe and capture user buffers under native SEH. */
    switch (Frame->Context.A0)
    {
        case BREAKPOINT_PRINT:
            return (LONG_PTR)KdpPrint((ULONG)Frame->Context.A3,
                                      (ULONG)Frame->Context.A4,
                                      (PCHAR)Frame->Context.A1,
                                      (USHORT)Frame->Context.A2,
                                      UserMode, Frame, NULL, &Handled);
        case BREAKPOINT_PROMPT:
            return KdpPrompt((PCHAR)Frame->Context.A1,
                             (USHORT)Frame->Context.A2,
                             (PCHAR)Frame->Context.A3,
                             (USHORT)Frame->Context.A4,
                             UserMode, Frame, NULL);
        case BREAKPOINT_LOAD_SYMBOLS:
        case BREAKPOINT_UNLOAD_SYMBOLS:
        case BREAKPOINT_COMMAND_STRING:
            /* Like the common KD trap, notifications from user mode cannot
             * change kernel debugger state. */
            return STATUS_SUCCESS;
        default:
            return (LONG_PTR)STATUS_INVALID_PARAMETER;
    }
}

/* A win32k service from a thread that is not a GUI thread yet. */
static
BOOLEAN
KiLa64NeedsGuiConversion(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME Frame)
{
    return !Thread->Header.Minimal && ((Frame->Context.A7 >> TABLE_OFFSET_BITS) == WIN32K_SERVICE_INDEX) &&
           !Thread->GuiThread && KeServiceDescriptorTableShadow[WIN32K_SERVICE_INDEX].Base &&
           PspW32ProcessCallout && PspW32ThreadCallout;
}

/*
 * GUI conversion needs a large kernel stack. The dispatching frame lives on
 * the thread's own stack, so KiLa64PrepareKernelStack (through
 * KeSwitchKernelStack) relocates the live frame chain and the trap frame
 * before PsConvertToGuiThread runs. Allocation and release may block and so
 * run with interrupts enabled inside a guarded region, with the service frame
 * linked exactly as the service itself will see it.
 */
static
BOOLEAN
KiLa64CreateGuiStack(
    _Inout_ PKTRAP_FRAME Frame,
    _Out_ PVOID *OldStack)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PVOID Stack;

    if (Thread->LargeStack || !KiLa64NeedsGuiConversion(Thread, Frame))
        return FALSE;

    Frame->PreviousTrapFrame = Thread->TrapFrame;
    Thread->TrapFrame = Frame;
    _enable();
    KeEnterGuardedRegion();
    Stack = MmCreateKernelStack(TRUE, 0);
    if (!Stack)
    {
        /* PsConvertToGuiThread reports the failure to the caller. */
        KeLeaveGuardedRegion();
        _disable();
        Thread->TrapFrame = Frame->PreviousTrapFrame;
        Frame->PreviousTrapFrame = NULL;
        return FALSE;
    }

    /* KeSwitchKernelStack requires the switch to be non-preemptible. */
    _disable();
    *OldStack = KeSwitchKernelStack(Stack, (PVOID)((ULONG_PTR)Stack - KERNEL_LARGE_STACK_COMMIT));
    _enable();
    return TRUE;
}

static
VOID
KiLa64DeleteKernelStack(
    _In_ PVOID OldStack,
    _Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(Thread->LargeStack && (Thread->TrapFrame == Frame));
    KeLeaveGuardedRegion();
    MmDeleteKernelStack(OldStack, FALSE);
    _disable();
    Thread->TrapFrame = Frame->PreviousTrapFrame;
    Frame->PreviousTrapFrame = NULL;
}

static
DECLSPEC_NOINLINE
ULONG_PTR
KiLa64DispatchStackService(
    _In_ PKTRAP_FRAME Frame,
    _In_ PVOID Routine,
    _In_ ULONG Count)
{
    ULONG_PTR Arguments[KI_LA64_MAX_SERVICE_ARGUMENTS] = {0};
    ULONG Argument;
    NTSTATUS Status;

    for (Argument = 0; Argument < 8; ++Argument)
        Arguments[Argument] = Frame->Context.R[4 + Argument];
    if (Frame->Context.Sp & 15) return (LONG_PTR)STATUS_DATATYPE_MISALIGNMENT;
    Status = KiLa64CopyFromUser(&Arguments[8], (PVOID)Frame->Context.Sp, (Count - 8) * sizeof(ULONG_PTR));
    if (!NT_SUCCESS(Status)) return (LONG_PTR)Status;

    return KiLa64InvokeSystemService(Routine, Arguments);
}

static
DECLSPEC_NOINLINE
ULONG_PTR
KiLa64DispatchSystemService(_Inout_ PKTRAP_FRAME Frame)
{
    ULONG64 Service = Frame->Context.A7;
    ULONG64 Table = Service >> TABLE_OFFSET_BITS;
    ULONG Index = Service & SERVICE_NUMBER_MASK;
    PKTHREAD Thread = KeGetCurrentThread();
    PKSERVICE_TABLE_DESCRIPTOR Descriptor;
    ULONG Count;

    if (Service == KI_LA64_DEBUG_SERVICE_ID)
        return KiLa64UserDebugService(Frame);

    /* The outer entry completes GUI conversion before argument capture. */
    if ((Table >= SSDT_MAX_ENTRIES) || (Table && !Thread->GuiThread))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Descriptor = &(Thread->GuiThread ? KeServiceDescriptorTableShadow : KeServiceDescriptorTable)[Table];
    if (!Descriptor->Base || !Descriptor->Number || (Index >= Descriptor->Limit))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Count = Descriptor->Number[Index];
    if ((Count % sizeof(ULONG_PTR)) || (Count / sizeof(ULONG_PTR) > KI_LA64_MAX_SERVICE_ARGUMENTS) ||
        (Descriptor->Base[Index] < (ULONG_PTR)MmSystemRangeStart))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Count /= sizeof(ULONG_PTR);

    if (Count <= 8)
    {
        return ((PKI_LA64_REGISTER_SERVICE)Descriptor->Base[Index])(
            Frame->Context.A0, Frame->Context.A1, Frame->Context.A2, Frame->Context.A3,
            Frame->Context.A4, Frame->Context.A5, Frame->Context.A6, Frame->Context.A7);
    }

    return KiLa64DispatchStackService(Frame, (PVOID)Descriptor->Base[Index], Count);
}

/* The image has no LoongArch64 service stub yet: KiLa64InvokeSystemService
 * already places the routine pointer in $t0, as the psABI indirect call does,
 * so the callee sees the documented convention. */
DECLSPEC_NORETURN
VOID
NTAPI
KiLa64SystemService(_Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR Result;
    NTSTATUS Status;
    PVOID OldStack = NULL;

    ASSERT(KiLa64UserTrap(Frame));
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Thread->PreviousMode = UserMode;
    KeGetCurrentPrcb()->KeSystemCalls++;
    /* SYSCALL is always four bytes and ERA holds its address [V1-7.5.4]. */
    Frame->Context.Pc += 4;
    _enable();
    if (Thread->Header.Minimal)
    {
        PspPicoSystemCallDispatch(Frame, NULL);
        goto CheckExit;
    }
    if (KiLa64CreateGuiStack(Frame, &OldStack))
    {
        /* The switch relocated the frame; the old local one is stale. */
        Frame = ((PKTRAP_FRAME)Thread->InitialStack) - 1;
        Status = PsConvertToGuiThread();
        if (!NT_SUCCESS(Status) && (Status != STATUS_ALREADY_WIN32))
        {
            Result = (LONG_PTR)KiGetGuiServiceFailureStatus((ULONG)Frame->Context.A7 & SERVICE_NUMBER_MASK);
            goto Exit;
        }
    }
    /* GDI32 queues drawing commands in the TEB. The win32k call is the
     * synchronization point: without this flush, NtGdiFlush returns while
     * the batch remains full and later commands overwrite Win32ClientInfo. */
    if (((Frame->Context.A7 >> TABLE_OFFSET_BITS) == WIN32K_SERVICE_INDEX) &&
        Thread->GuiThread && Thread->Teb && KeGdiFlushUserBatch)
    {
        ULONG GdiBatchCount = 0;
        Status = KiLa64CopyFromUser(&GdiBatchCount,
                                    (PUCHAR)Thread->Teb + FIELD_OFFSET(TEB, GdiBatchCount),
                                    sizeof(GdiBatchCount));
        if (NT_SUCCESS(Status) && GdiBatchCount)
            KeGdiFlushUserBatch();
    }
    Result = KiLa64DispatchSystemService(Frame);

Exit:
    /* The service frame sits right below the initial stack. Thread->TrapFrame
     * is not used here: a failed NtRaiseException or NtContinue returns with
     * it already unlinked. */
    Frame = ((PKTRAP_FRAME)Thread->InitialStack) - 1;
    Frame->Context.A0 = Result;

CheckExit:
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE, Frame->Context.A7, KeGetCurrentIrql(), 0, 0);
    if ((Thread->ApcStateIndex != OriginalApcEnvironment) || Thread->CombinedApcDisable)
        KeBugCheckEx(APC_INDEX_MISMATCH, Frame->Context.A7, Thread->ApcStateIndex, Thread->CombinedApcDisable, 0);

    if (OldStack)
        KiLa64DeleteKernelStack(OldStack, Frame);
    KiLa64ReturnToUser(Frame);
}

/* EOF */
