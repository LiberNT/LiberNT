/*
 */

#include <ntoskrnl.h>

VOID
NTAPI
KiInitializeUserApc(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{

    ASSERT((TrapFrame->SegCs & MODE_MASK) != KernelMode);

    _SEH2_TRY
    {

        Context->ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
        KeTrapFrameToContext(TrapFrame, ExceptionFrame, Context);

        Context->P1Home = (ULONG64)NormalContext;
        Context->P2Home = (ULONG64)SystemArgument1;
        Context->P3Home = (ULONG64)SystemArgument2;
        Context->P4Home = (ULONG64)NormalRoutine;

    }
    {
    }
    _SEH2_END;

}

NTSTATUS
FASTCALL
KiUserModeCallout(
    _Out_ PKCALLOUT_FRAME CalloutFrame)
{
    PKIPCR Pcr;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    {

    }


    _disable();


    Pcr = (PKIPCR)KeGetPcr();

    _enable();

}

VOID
KiSetupUserCalloutFrame(
    _Out_ PUCALLOUT_FRAME UserCalloutFrame,
    _In_ ULONG ApiNumber,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    UserCalloutFrame->Buffer = (PVOID)(UserCalloutFrame + 1);
    UserCalloutFrame->Length = BufferLength;
    UserCalloutFrame->ApiNumber = ApiNumber;
    UserCalloutFrame->MachineFrame.Rip = TrapFrame->Rip;
    UserCalloutFrame->MachineFrame.Rsp = TrapFrame->Rsp;
}

    _In_ NTSTATUS CallbackStatus)
{

    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)

    {
    }

    Pcr->TssBase->Rsp0 = CalloutFrame->InitialStack;
    Pcr->Prcb.RspBase = CalloutFrame->InitialStack;
}

