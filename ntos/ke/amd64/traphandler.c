/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 DPC, IPI and NMI interrupt handlers and the system call dispatcher
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>

#define MAX_SYSCALL_PARAMS 16
#define KI_REGISTER_PARAMS 4

VOID
KiRetireDpcListInDpcStack(
    _In_ PKPRCB Prcb,
    _In_ PVOID DpcStack);

NTSTATUS
KiConvertToGuiThread(VOID);

_Requires_lock_not_held_(Prcb->PrcbLock)
VOID
NTAPI
KiDpcInterruptHandler(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KIRQL OldIrql;

    if (SmpDbgEnabled)
        SmpDbgDispatchInterrupt(Prcb->Number);

    OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    KiSendEOI();

    if (KiDpcWorkPending(Prcb))
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);

    _enable();
    KiFinishDispatchInterrupt(Prcb);
    _disable();
    KeLowerIrql(OldIrql);
}

_Requires_lock_not_held_(Prcb->PrcbLock)
VOID
NTAPI
KiIpiInterruptHandler(VOID)
{
    KIRQL OldIrql = KfRaiseIrql(IPI_LEVEL);

    KiSendEOI();

    if (SmpDbgEnabled)
        SmpDbgIpi(KeGetCurrentProcessorNumber());
    KiIpiProcessRequests();

    KeLowerIrql(OldIrql);
}

VOID
KiNmiInterruptHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    /* An NMI can land between a user-mode entry and its swapgs, with the user GS base still loaded */
    BOOLEAN SwapGs = (BOOLEAN)(((TrapFrame->SegCs & MODE_MASK) == 0) && ((LONG64)__readmsr(MSR_GS_BASE) >= 0));

    if (SwapGs)
        __swapgs();

    if (!KiProcessorFreezeHandler(TrapFrame, ExceptionFrame))
        KiHandleNmi();

    if (SwapGs)
        __swapgs();
}

ULONG64
NtSyscallFailure(void)
{
    return (ULONG)KeGetCurrentThread()->TrapFrame->Rax;
}

ULONG64
KiConvertToGuiThreadFailure(VOID)
{
    ULONG ServiceNumber = (ULONG)KeGetCurrentThread()->TrapFrame->Rax & SERVICE_NUMBER_MASK;

    return (ULONG)KiGetGuiServiceFailureStatus(ServiceNumber);
}

VOID
KiInstrumentTrapFrame(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PVOID Callback = Thread->ApcState.Process->InstrumentationCallback;

    if (Callback == NULL)
        return;

    /* The callback finds the interrupted RIP in R10 */
    TrapFrame->R10 = TrapFrame->Rip;
    TrapFrame->Rip = (ULONG64)Callback;
    InterlockedAnd8((PCHAR)&Thread->Header.DebugActive, (CHAR)~DEBUG_ACTIVE_INSTRUMENTED);
}

static
PKTHREAD
KiEnterSystemService(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    __addgsdword(FIELD_OFFSET(KIPCR, Prcb.KeSystemCalls), 1);

    Thread->PreviousMode = TrapFrame->PreviousMode = UserMode;

    /* NtContinue and NtRaiseException leave through KiExceptionExit, which reads this */
    TrapFrame->PreviousIrql = PASSIVE_LEVEL;
    TrapFrame->ExceptionFrame = 0;
    TrapFrame->ExceptionActive = KEXCEPTION_ACTIVE_SERVICE_FRAME;
    return Thread;
}

VOID
KiPicoSystemCallHandler(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKTHREAD Thread = KiEnterSystemService(TrapFrame);

    _enable();
    PspPicoSystemCallDispatch(TrapFrame, ExceptionFrame);

    if (Thread->ApcState.UserApcPending)
    {
        KIRQL OldIrql;

        KeRaiseIrql(APC_LEVEL, &OldIrql);
        KiDeliverApc(UserMode, ExceptionFrame, TrapFrame);
        KeLowerIrql(OldIrql);
    }

    _disable();
}

static
VOID
KiTrackInstrumentation(
    _Inout_ PKTHREAD Thread)
{
    BOOLEAN Wanted = (BOOLEAN)(Thread->ApcState.Process->InstrumentationCallback != NULL);
    BOOLEAN Marked = (BOOLEAN)((Thread->Header.DebugActive & DEBUG_ACTIVE_INSTRUMENTED) != 0);

    if (Wanted && !Marked)
        Thread->Header.DebugActive |= DEBUG_ACTIVE_INSTRUMENTED;
    else if (!Wanted && Marked)
        Thread->Header.DebugActive &= ~DEBUG_ACTIVE_INSTRUMENTED;
}

static
VOID
KiFlushGdiBatch(VOID)
{
    ULONG Pending = 0;

    _SEH2_TRY
    {
        Pending = NtCurrentTeb()->GdiBatchCount;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Pending = 0;
    }
    _SEH2_END;

    if (Pending != 0)
        KeGdiFlushUserBatch();
}

static
PKSERVICE_TABLE_DESCRIPTOR
KiServiceTableOf(
    _In_ PKTHREAD Thread,
    _In_ ULONG TableIndex)
{
    /* TODO: the table follows GuiThread; not yet checked against Windows 11 */
    PKSERVICE_TABLE_DESCRIPTOR Tables = Thread->GuiThread ? KeServiceDescriptorTableShadow : KeServiceDescriptorTable;

    return &Tables[TableIndex];
}

PVOID
KiSystemCallHandler(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KiEnterSystemService(TrapFrame);
    PKSERVICE_TABLE_DESCRIPTOR Table;
    PULONG64 KernelParams, UserParams;
    ULONG ServiceNumber, TableIndex, Count, Index;
    ULONG64 UserRsp;

    KiTrackInstrumentation(Thread);

    UserRsp = TrapFrame->Rsp;
    _enable();

    /* A kernel-range stack pointer is clamped so the copy below faults instead */
    if (UserRsp > MmUserProbeAddress)
        UserRsp = MmUserProbeAddress;

    UserParams = (PULONG64)UserRsp + 1;
    KernelParams = (PULONG64)TrapFrame - MAX_SYSCALL_PARAMS;

    ServiceNumber = (ULONG)TrapFrame->Rax;
    TableIndex = (ServiceNumber >> TABLE_OFFSET_BITS) & ((1 << TABLE_NUMBER_BITS) - 1);
    ServiceNumber &= SERVICE_NUMBER_MASK;

    if (TableIndex == WIN32K_SERVICE_INDEX)
        KiFlushGdiBatch();

    Table = KiServiceTableOf(Thread, TableIndex);

    if ((TableIndex == WIN32K_SERVICE_INDEX) &&
        PsGetCurrentProcess()->MitigationFlagsValues.DisallowWin32kSystemCalls)
    {
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    if (ServiceNumber >= Table->Limit)
    {
        /* A first win32k call converts the thread; the stack switch has to happen in the entry stub */
        if ((TableIndex == WIN32K_SERVICE_INDEX) && !Thread->GuiThread)
            return (PVOID)KiConvertToGuiThread;

        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    /* The first four parameters arrive in registers; only the rest are copied from the user stack */
    Count = Table->Number[ServiceNumber] / sizeof(ULONG64);
    ASSERT(Count <= MAX_SYSCALL_PARAMS);

    _SEH2_TRY
    {
        if (Count <= MAX_SYSCALL_PARAMS)
        {
            for (Index = KI_REGISTER_PARAMS; Index < Count; Index++)
                KernelParams[Index] = UserParams[Index];
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        TrapFrame->Rax = _SEH2_GetExceptionCode();
        return (PVOID)NtSyscallFailure;
    }
    _SEH2_END;

    return (PVOID)Table->Base[ServiceNumber];
}
