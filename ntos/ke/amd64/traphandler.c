/*
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>


VOID
KiRetireDpcListInDpcStack(

NTSTATUS

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

        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);

    _enable();

    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
    }
    {
    }

    _disable();
    KeLowerIrql(OldIrql);
}

_Requires_lock_not_held_(Prcb->PrcbLock)
VOID
NTAPI
KiIpiInterruptHandler(VOID)
{

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

    TrapFrame->R10 = TrapFrame->Rip;
    TrapFrame->Rip = (ULONG64)Callback;
    InterlockedAnd8((PCHAR)&Thread->Header.DebugActive, (CHAR)~DEBUG_ACTIVE_INSTRUMENTED);
}

{
    PKTHREAD Thread = KeGetCurrentThread();

    __addgsdword(FIELD_OFFSET(KIPCR, Prcb.KeSystemCalls), 1);
    Thread->PreviousMode = TrapFrame->PreviousMode = UserMode;
    TrapFrame->PreviousIrql = PASSIVE_LEVEL;
    TrapFrame->ExceptionFrame = 0;
    TrapFrame->ExceptionActive = KEXCEPTION_ACTIVE_SERVICE_FRAME;
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

{



    {
    }
    {
    }


    _enable();


    UserParams = (PULONG64)UserRsp + 1;
    KernelParams = (PULONG64)TrapFrame - MAX_SYSCALL_PARAMS;

    ServiceNumber = (ULONG)TrapFrame->Rax;
    TableIndex = (ServiceNumber >> TABLE_OFFSET_BITS) & ((1 << TABLE_NUMBER_BITS) - 1);
    ServiceNumber &= SERVICE_NUMBER_MASK;

    if (TableIndex == WIN32K_SERVICE_INDEX)


    if ((TableIndex == WIN32K_SERVICE_INDEX) &&
        PsGetCurrentProcess()->MitigationFlagsValues.DisallowWin32kSystemCalls)
    {
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    {
            return (PVOID)KiConvertToGuiThread;

        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }


    _SEH2_TRY
    {
        {
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        TrapFrame->Rax = _SEH2_GetExceptionCode();
        return (PVOID)NtSyscallFailure;
    }
    _SEH2_END;

}
