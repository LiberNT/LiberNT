/*
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern UCHAR KiInterruptDispatchTemplate[16];
extern KI_INTERRUPT_DISPATCH_ENTRY KiUnexpectedRange[256];
extern KI_INTERRUPT_DISPATCH_ENTRY KiUnexpectedRangeEnd[];
void KiInterruptDispatch(void);

VOID
NTAPI
{
    ULONG Vector = (ULONG)((ErrorCode + 128) & 0xFF);
}

VOID
NTAPI
KeInitializeInterrupt(
{

    Interrupt->Type = InterruptObject;
    Interrupt->Size = sizeof(KINTERRUPT);
    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->Vector = Vector;
    Interrupt->Irql = Irql;
    Interrupt->SynchronizeIrql = SynchronizeIrql;
    Interrupt->Mode = InterruptMode;
    Interrupt->ShareVector = ShareVector;
    Interrupt->Number = ProcessorNumber;
    Interrupt->FloatingSave = FloatingSave;
    Interrupt->TickCount = 0;
    Interrupt->Connected = FALSE;
    Interrupt->ServiceCount = 0;
    Interrupt->DispatchCount = 0;
    Interrupt->TrapFrame = NULL;
    Interrupt->Reserved = 0;


}

BOOLEAN
NTAPI
{
    BOOLEAN Connected;

    {
        DPRINT1("Invalid interrupt vector: %lu\n", Interrupt->Vector);
        return FALSE;
    }

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiConnectPassiveInterrupt(Interrupt);

    ASSERT(Interrupt->Number < KeNumberProcessors);
    ASSERT(Interrupt->Irql <= HIGH_LEVEL);
    ASSERT(Interrupt->SynchronizeIrql >= Interrupt->Irql);
    ASSERT(Interrupt->Irql == (Interrupt->Vector >> 4));




    {
    }
    else
    {
    }
}

BOOLEAN
NTAPI
{
    KIRQL OldIrql;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiDisconnectPassiveInterrupt(Interrupt);


    if (Interrupt->Connected)
    {
        Interrupt->Connected = FALSE;
    }

    return TRUE;
}

BOOLEAN
NTAPI
{
    KIRQL OldIrql;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiSynchronizePassiveInterrupt(Interrupt, SynchronizeRoutine, SynchronizeContext);

    OldIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
    KeLowerIrql(OldIrql);

}

BOOLEAN
NTAPI
KeDispatchSecondaryInterrupt(
    _In_ ULONG Vector,
    _In_ ULONG_PTR Flags,
    _In_opt_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Reserved);
    return KiDispatchPassiveInterrupt(Vector, NULL);
}

BOOLEAN
NTAPI
KiSetClockDeadline(
    _In_ ULONGLONG InterruptTime)
{
    UNREFERENCED_PARAMETER(InterruptTime);
    return FALSE;
}
