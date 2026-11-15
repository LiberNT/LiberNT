/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 interrupt objects: initialize, connect to an IDT vector, disconnect, synchronize
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef enum _CONNECT_TYPE
{
    NoConnect,
    NormalConnect,
    ChainConnect,
    UnknownConnect
} CONNECT_TYPE, *PCONNECT_TYPE;

typedef struct _DISPATCH_INFO
{
    CONNECT_TYPE Type;
    PKINTERRUPT Interrupt;
    PKINTERRUPT_ROUTINE NoDispatch;
    PKINTERRUPT_ROUTINE InterruptDispatch;
    PKINTERRUPT_ROUTINE FloatingDispatch;
    PKINTERRUPT_ROUTINE ChainedDispatch;
    PKINTERRUPT_ROUTINE *FlatDispatch;
} DISPATCH_INFO, *PDISPATCH_INFO;

ULONG KiISRTimeout = 55;
USHORT KiISROverflow = 30000;
extern ULONG NTAPI KiChainedDispatch2ndLvl(VOID);

VOID
NTAPI
KiGetVectorDispatch(
    _In_ ULONG Vector,
    _In_ PDISPATCH_INFO Dispatch)
{
    PKINTERRUPT_ROUTINE Handler;
    PVOID Current;
    UCHAR Type;
    UCHAR Entry;

    Type = HalSystemVectorDispatchEntry(Vector,
                                        &Dispatch->FlatDispatch,
                                        &Dispatch->NoDispatch);
    ASSERT(Type == 0);

    Entry = HalVectorToIDTEntry(Vector);

    Dispatch->NoDispatch = (PVOID)(((ULONG_PTR)&KiStartUnexpectedRange) +
                                   (Entry - PRIMARY_VECTOR_BASE) *
                                   KiUnexpectedEntrySize);

    Dispatch->InterruptDispatch = (PVOID)KiInterruptDispatch;
    Dispatch->FloatingDispatch = NULL;
    Dispatch->ChainedDispatch = (PVOID)KiChainedDispatch;
    Dispatch->FlatDispatch = NULL;

    Current = KeQueryInterruptHandler(Vector);

    Dispatch->Interrupt = CONTAINING_RECORD(Current,
                                            KINTERRUPT,
                                            DispatchCode);

    if ((PKINTERRUPT_ROUTINE)Current == Dispatch->NoDispatch)
    {
        Dispatch->Type = NoConnect;
    }
    else
    {
        Handler = Dispatch->Interrupt->DispatchAddress;
        if (Handler == Dispatch->ChainedDispatch)
        {
            Dispatch->Type = ChainConnect;
        }
        else if ((Handler == Dispatch->InterruptDispatch) ||
                 (Handler == Dispatch->FloatingDispatch))
        {
            Dispatch->Type = NormalConnect;
        }
        else
        {
            Dispatch->Type = UnknownConnect;
        }
    }
}

VOID
NTAPI
KiConnectVectorToInterrupt(
    _In_ PKINTERRUPT Interrupt,
    _In_ CONNECT_TYPE Type)
{
    DISPATCH_INFO Dispatch;
    PKINTERRUPT_ROUTINE Handler;

    KiGetVectorDispatch(Interrupt->Vector, &Dispatch);

    if (Type == NoConnect)
    {
        Handler = Dispatch.NoDispatch;
    }
    else
    {
        Handler = (Type == NormalConnect) ?
                  Dispatch.InterruptDispatch:
                  Dispatch.ChainedDispatch;
        ASSERT(Interrupt->FloatingSave == FALSE);

        Interrupt->DispatchAddress = Handler;

        ASSERT(Dispatch.FlatDispatch == NULL);
        Handler = (PVOID)&Interrupt->DispatchCode;
    }

    KeRegisterInterruptHandler(Interrupt->Vector, Handler);
}

FORCEINLINE
DECLSPEC_NORETURN
VOID
KiExitInterrupt(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN Spurious)
{
    if (!Spurious)
    {
        _disable();
        HalEndSystemInterrupt(OldIrql, TrapFrame);
    }

    KiEoiHelper(TrapFrame);
}

DECLSPEC_NORETURN
VOID
__cdecl
KiUnexpectedInterrupt(VOID)
{
    KeBugCheck(TRAP_CAUSE_UNKNOWN);
}

VOID
FASTCALL
KiUnexpectedInterruptTailHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;

    KiEnterInterruptTrap(TrapFrame);

    KeGetCurrentPrcb()->InterruptCount++;

    if (HalBeginSystemInterrupt(HIGH_LEVEL, TrapFrame->ErrCode, &OldIrql))
    {
        DPRINT1("\n\x7\x7!!! Unexpected Interrupt 0x%02lx !!!\n", TrapFrame->ErrCode);

        KiExitInterrupt(TrapFrame, OldIrql, FALSE);
    }
    else
    {
        KiExitInterrupt(TrapFrame, OldIrql, TRUE);
    }
}

typedef
VOID
(FASTCALL *PKI_INTERRUPT_DISPATCH)(
    _In_ PKTRAP_FRAME TrapFrame,
    IN PKINTERRUPT Interrupt
);

VOID
FASTCALL
KiInterruptDispatch(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;

    KeGetCurrentPrcb()->InterruptCount++;

    if (HalBeginSystemInterrupt(Interrupt->SynchronizeIrql,
                                Interrupt->Vector,
                                &OldIrql))
    {
        KxAcquireSpinLock(Interrupt->ActualLock);

        Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);

        KxReleaseSpinLock(Interrupt->ActualLock);

        KiExitInterrupt(TrapFrame, OldIrql, FALSE);
    }
    else
    {
        KiExitInterrupt(TrapFrame, OldIrql, TRUE);
    }
}

VOID
FASTCALL
KiChainedDispatch(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql, OldInterruptIrql = 0;
    BOOLEAN Handled;
    PLIST_ENTRY NextEntry, ListHead;

    KeGetCurrentPrcb()->InterruptCount++;

    if (HalBeginSystemInterrupt(Interrupt->Irql,
                                Interrupt->Vector,
                                &OldIrql))
    {
        ListHead = &Interrupt->InterruptListEntry;
        NextEntry = ListHead;
        while (TRUE)
        {
            if (Interrupt->SynchronizeIrql > Interrupt->Irql)
            {
                OldInterruptIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
            }

            KxAcquireSpinLock(Interrupt->ActualLock);

            Handled = Interrupt->ServiceRoutine(Interrupt,
                                                Interrupt->ServiceContext);

            KxReleaseSpinLock(Interrupt->ActualLock);

            if (Interrupt->SynchronizeIrql > Interrupt->Irql)
            {
                ASSERT(OldInterruptIrql == Interrupt->Irql);
                KfLowerIrql(OldInterruptIrql);
            }

            if ((Handled) && (Interrupt->Mode == LevelSensitive)) break;

            NextEntry = NextEntry->Flink;

            if (NextEntry == ListHead)
            {
                if (Interrupt->Mode == LevelSensitive) break;

                if (!Handled) break;
            }

            Interrupt = CONTAINING_RECORD(NextEntry, KINTERRUPT, InterruptListEntry);
        }

        KiExitInterrupt(TrapFrame, OldIrql, FALSE);
    }
    else
    {
        KiExitInterrupt(TrapFrame, OldIrql, TRUE);
    }
}

VOID
FASTCALL
KiInterruptTemplateHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKINTERRUPT Interrupt)
{
    KiEnterInterruptTrap(TrapFrame);

    ((PKI_INTERRUPT_DISPATCH)Interrupt->DispatchAddress)(TrapFrame, Interrupt);
}

VOID
NTAPI
KeInitializeInterrupt(
    _In_ PKINTERRUPT Interrupt,
    _In_ PKSERVICE_ROUTINE ServiceRoutine,
    _In_ PVOID ServiceContext,
    _In_ PKSPIN_LOCK SpinLock,
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KIRQL SynchronizeIrql,
    _In_ KINTERRUPT_MODE InterruptMode,
    _In_ BOOLEAN ShareVector,
    _In_ CHAR ProcessorNumber,
    _In_ BOOLEAN FloatingSave)
{
    ULONG i;
    PULONG DispatchCode = &Interrupt->DispatchCode[0], Patch = DispatchCode;

    Interrupt->Type = InterruptObject;
    Interrupt->Size = sizeof(KINTERRUPT);

    if (SpinLock)
    {
        Interrupt->ActualLock = SpinLock;
    }
    else
    {
        KeInitializeSpinLock(&Interrupt->SpinLock);
        Interrupt->ActualLock = &Interrupt->SpinLock;
    }

    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->Vector = Vector;
    Interrupt->Irql = Irql;
    Interrupt->SynchronizeIrql = SynchronizeIrql;
    Interrupt->Mode = InterruptMode;
    Interrupt->ShareVector = ShareVector;
    Interrupt->Number = ProcessorNumber;
    Interrupt->FloatingSave = FloatingSave;
    Interrupt->TickCount = MAXULONG;
    Interrupt->DispatchCount = MAXULONG;

    for (i = 0; i < DISPATCH_LENGTH; i++)
    {
        *DispatchCode++ = ((PULONG)KiInterruptTemplate)[i];
    }

    Patch = (PULONG)((ULONG_PTR)Patch +
                     ((ULONG_PTR)&KiInterruptTemplateObject -
                      (ULONG_PTR)KiInterruptTemplate) - 4);

    *Patch = PtrToUlong(Interrupt);

    Interrupt->Connected = FALSE;
}

BOOLEAN
NTAPI
KeConnectInterrupt(
    _In_ PKINTERRUPT Interrupt)
{
    BOOLEAN Connected, Error, Status;
    KIRQL Irql, OldIrql;
    UCHAR Number;
    ULONG Vector;
    DISPATCH_INFO Dispatch;

    Number = Interrupt->Number;
    Vector = Interrupt->Vector;
    Irql = Interrupt->Irql;

    if ((Irql > HIGH_LEVEL) ||
        (Number >= KeNumberProcessors) ||
        (Interrupt->SynchronizeIrql < Irql) ||
        (Interrupt->FloatingSave))
    {
        return FALSE;
    }

    if (Irql == PASSIVE_LEVEL)
        return KiConnectPassiveInterrupt(Interrupt);

    Connected = FALSE;
    Error = FALSE;

    KeSetSystemAffinityThread(1 << Number);
    OldIrql = KiAcquireDispatcherLock();

    if (!Interrupt->Connected)
    {
        KiGetVectorDispatch(Vector, &Dispatch);

        if (Dispatch.Type == NoConnect)
        {
            Interrupt->Connected = Connected = TRUE;

            InitializeListHead(&Interrupt->InterruptListEntry);

            KiConnectVectorToInterrupt(Interrupt, NormalConnect);
            Status = HalEnableSystemInterrupt(Vector, Irql, Interrupt->Mode);
            if (!Status) Error = TRUE;
        }
        else if ((Dispatch.Type != UnknownConnect) &&
                (Interrupt->ShareVector) &&
                (Dispatch.Interrupt->ShareVector) &&
                (Dispatch.Interrupt->Mode == Interrupt->Mode))
        {
            Interrupt->Connected = Connected = TRUE;

            ASSERT(Irql <= SYNCH_LEVEL);

            if (Dispatch.Type != ChainConnect)
            {
                ASSERT(Dispatch.Interrupt->Mode != Latched);

                KiConnectVectorToInterrupt(Dispatch.Interrupt, ChainConnect);
            }

            InsertTailList(&Dispatch.Interrupt->InterruptListEntry,
                           &Interrupt->InterruptListEntry);
        }
    }

    KiReleaseDispatcherLock(OldIrql);
    KeRevertToUserAffinityThread();

    if ((Connected) && (Error))
    {
        DPRINT1("HalEnableSystemInterrupt failed\n");
        KeDisconnectInterrupt(Interrupt);
        Connected = FALSE;
    }

    return Connected;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(
    _In_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql, Irql;
    ULONG Vector;
    DISPATCH_INFO Dispatch;
    PKINTERRUPT NextInterrupt;
    BOOLEAN State;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiDisconnectPassiveInterrupt(Interrupt);

    KeSetSystemAffinityThread(1 << Interrupt->Number);

    OldIrql = KiAcquireDispatcherLock();

    State = Interrupt->Connected;
    if (State)
    {
        Irql = Interrupt->Irql;
        Vector = Interrupt->Vector;

        KiGetVectorDispatch(Vector, &Dispatch);

        if (Dispatch.Type == ChainConnect)
        {
            ASSERT(Irql <= SYNCH_LEVEL);
            if (Interrupt == Dispatch.Interrupt)
            {
                Dispatch.Interrupt = CONTAINING_RECORD(Dispatch.Interrupt->
                                                       InterruptListEntry.Flink,
                                                       KINTERRUPT,
                                                       InterruptListEntry);

                KiConnectVectorToInterrupt(Dispatch.Interrupt, ChainConnect);
            }

            RemoveEntryList(&Interrupt->InterruptListEntry);

            NextInterrupt = CONTAINING_RECORD(Dispatch.Interrupt->
                                              InterruptListEntry.Flink,
                                              KINTERRUPT,
                                              InterruptListEntry);

            if (Dispatch.Interrupt == NextInterrupt)
            {
                KiConnectVectorToInterrupt(Dispatch.Interrupt, NormalConnect);
            }
        }
        else
        {
            HalDisableSystemInterrupt(Interrupt->Vector, Irql);
            KiConnectVectorToInterrupt(Interrupt, NoConnect);
        }

        Interrupt->Connected = FALSE;
    }

    KiReleaseDispatcherLock(OldIrql);
    KeRevertToUserAffinityThread();

    return State;
}

BOOLEAN
NTAPI
KeSynchronizeExecution(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    _In_opt_ PVOID SynchronizeContext)
{
    BOOLEAN Success;
    KIRQL OldIrql;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiSynchronizePassiveInterrupt(Interrupt, SynchronizeRoutine, SynchronizeContext);

    KeRaiseIrql(Interrupt->SynchronizeIrql,
                &OldIrql);

    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);

    Success = SynchronizeRoutine(SynchronizeContext);

    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);

    KeLowerIrql(OldIrql);

    return Success;
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
