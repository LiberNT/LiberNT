/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt object initialization and synchronization shared by the architectures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

VOID
NTAPI
KeInitializeInterrupt(
    _Out_ PKINTERRUPT Interrupt,
    _In_ PKSERVICE_ROUTINE ServiceRoutine,
    _In_opt_ PVOID ServiceContext,
    _In_opt_ PKSPIN_LOCK SpinLock,
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KIRQL SynchronizeIrql,
    _In_ KINTERRUPT_MODE InterruptMode,
    _In_ BOOLEAN ShareVector,
    _In_ CHAR ProcessorNumber,
    _In_ BOOLEAN FloatingSave)
{
    RtlZeroMemory(Interrupt, sizeof(*Interrupt));

    Interrupt->Type = InterruptObject;
    Interrupt->Size = sizeof(KINTERRUPT);
    KeInitializeSpinLock(&Interrupt->SpinLock);
    Interrupt->ActualLock = (SpinLock != NULL) ? SpinLock : &Interrupt->SpinLock;
    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->Vector = Vector;
    Interrupt->Irql = Irql;
    Interrupt->SynchronizeIrql = SynchronizeIrql;
    Interrupt->Mode = InterruptMode;
    Interrupt->ShareVector = ShareVector;
    Interrupt->Number = ProcessorNumber;
    Interrupt->FloatingSave = FloatingSave;
    InitializeListHead(&Interrupt->InterruptListEntry);

    KiArchPrepareInterruptDispatch(Interrupt);
}

BOOLEAN
NTAPI
KeSynchronizeExecution(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    _In_opt_ PVOID SynchronizeContext)
{
    BOOLEAN Result;
    KIRQL OldIrql;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiSynchronizePassiveInterrupt(Interrupt, SynchronizeRoutine, SynchronizeContext);

    OldIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
    Result = SynchronizeRoutine(SynchronizeContext);
    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
    KeLowerIrql(OldIrql);

    return Result;
}
