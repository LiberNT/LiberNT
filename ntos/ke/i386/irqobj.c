/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 interrupt dispatch and the connection of interrupt objects to IDT vectors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KI_STRAY_INTERRUPT_REPORTS 32

typedef
VOID
(FASTCALL *PKI_INTERRUPT_DISPATCH)(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKINTERRUPT Interrupt);

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
    static LONG Reports = 0;
    KIRQL OldIrql;
    LONG Count;

    KiEnterInterruptTrap(TrapFrame);
    KeGetCurrentPrcb()->InterruptCount++;

    if (!HalBeginSystemInterrupt(HIGH_LEVEL, TrapFrame->ErrCode, &OldIrql))
        KiExitInterrupt(TrapFrame, OldIrql, TRUE);

    Count = InterlockedIncrement(&Reports);
    if (Count <= KI_STRAY_INTERRUPT_REPORTS)
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "Unexpected interrupt on vector 0x%02lx (%ld so far)\n", TrapFrame->ErrCode, Count);

    KiExitInterrupt(TrapFrame, OldIrql, FALSE);
}

VOID
FASTCALL
KiInterruptDispatch(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;

    KeGetCurrentPrcb()->InterruptCount++;

    if (!HalBeginSystemInterrupt(Interrupt->SynchronizeIrql, Interrupt->Vector, &OldIrql))
        KiExitInterrupt(TrapFrame, OldIrql, TRUE);

    KxAcquireSpinLock(Interrupt->ActualLock);
    Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
    KxReleaseSpinLock(Interrupt->ActualLock);

    KiExitInterrupt(TrapFrame, OldIrql, FALSE);
}

/* A shared vector runs at the lowest IRQL of its chain; each routine is raised to its own synchronize IRQL */
static
BOOLEAN
KiServiceChainedInterrupt(
    _In_ PKINTERRUPT Interrupt)
{
    BOOLEAN Raise = (BOOLEAN)(Interrupt->SynchronizeIrql > Interrupt->Irql);
    KIRQL OldIrql = Interrupt->Irql;
    BOOLEAN Handled;

    if (Raise)
        OldIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);

    KxAcquireSpinLock(Interrupt->ActualLock);
    Handled = Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
    KxReleaseSpinLock(Interrupt->ActualLock);

    if (Raise)
    {
        ASSERT(OldIrql == Interrupt->Irql);
        KfLowerIrql(OldIrql);
    }

    return Handled;
}

/* A level line is serviced until one routine claims it; an edge line is walked again until a full pass finds no work */
VOID
FASTCALL
KiChainedDispatch(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKINTERRUPT Head)
{
    PLIST_ENTRY Entry;
    BOOLEAN AnyHandled;
    KIRQL OldIrql;

    KeGetCurrentPrcb()->InterruptCount++;

    if (!HalBeginSystemInterrupt(Head->Irql, Head->Vector, &OldIrql))
        KiExitInterrupt(TrapFrame, OldIrql, TRUE);

    do
    {
        PKINTERRUPT Interrupt = Head;

        AnyHandled = FALSE;
        Entry = &Head->InterruptListEntry;
        do
        {
            if (KiServiceChainedInterrupt(Interrupt))
            {
                if (Head->Mode == LevelSensitive)
                    KiExitInterrupt(TrapFrame, OldIrql, FALSE);
                AnyHandled = TRUE;
            }

            Entry = Entry->Flink;
            Interrupt = CONTAINING_RECORD(Entry, KINTERRUPT, InterruptListEntry);
        } while (Entry != &Head->InterruptListEntry);
    } while (AnyHandled && (Head->Mode == Latched));

    KiExitInterrupt(TrapFrame, OldIrql, FALSE);
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

/* The dispatch code is the template with the object's own address patched in */
VOID
KiI386PrepareInterruptDispatch(
    _Inout_ PKINTERRUPT Interrupt)
{
    ULONG_PTR ObjectSlot = (ULONG_PTR)&KiInterruptTemplateObject - (ULONG_PTR)KiInterruptTemplate - sizeof(ULONG);

    RtlCopyMemory(Interrupt->DispatchCode, (PVOID)KiInterruptTemplate, sizeof(Interrupt->DispatchCode));
    *(PULONG)((ULONG_PTR)Interrupt->DispatchCode + ObjectSlot) = PtrToUlong(Interrupt);
}

static
PVOID
KiUnexpectedStubOf(
    _In_ ULONG Vector)
{
    return (PVOID)((ULONG_PTR)&KiStartUnexpectedRange +
                   (HalVectorToIDTEntry(Vector) - PRIMARY_VECTOR_BASE) * KiUnexpectedEntrySize);
}

/* A connected vector points at the DispatchCode of the interrupt that heads its chain */
static
PKINTERRUPT
KiVectorHead(
    _In_ ULONG Vector)
{
    PVOID Handler = KeQueryInterruptHandler(Vector);
    PKINTERRUPT Head;

    if (Handler == KiUnexpectedStubOf(Vector))
        return NULL;

    Head = CONTAINING_RECORD(Handler, KINTERRUPT, DispatchCode);
    ASSERT(Head->Connected && (Head->Vector == Vector));
    ASSERT((Head->DispatchAddress == (PVOID)KiInterruptDispatch) ||
           (Head->DispatchAddress == (PVOID)KiChainedDispatch));
    return Head;
}

/* A vector with a single interrupt skips the chain walk */
static
VOID
KiRouteVector(
    _Inout_ PKINTERRUPT Head)
{
    Head->DispatchAddress = IsListEmpty(&Head->InterruptListEntry) ? (PVOID)KiInterruptDispatch
                                                                  : (PVOID)KiChainedDispatch;
    KeRegisterInterruptHandler(Head->Vector, Head->DispatchCode);
}

static KSPIN_LOCK KiVectorLock;

/* The IDT is per processor: run on the target and keep every other change of a vector out while this one is made */
static
KIRQL
KiLockVectorOf(
    _In_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;

    KeSetSystemAffinityThread((KAFFINITY)1 << Interrupt->Number);
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireFlagLock(&KiVectorLock);
    return OldIrql;
}

static
VOID
KiUnlockVector(
    _In_ KIRQL OldIrql)
{
    KiReleaseFlagLock(&KiVectorLock);
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
}

BOOLEAN
NTAPI
KeConnectInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    BOOLEAN Connected = FALSE;
    BOOLEAN Enabled = TRUE;
    PKINTERRUPT Head;
    KIRQL OldIrql;

    if ((Interrupt->Irql > HIGH_LEVEL) ||
        (Interrupt->Number >= KeNumberProcessors) ||
        (Interrupt->SynchronizeIrql < Interrupt->Irql) ||
        Interrupt->FloatingSave)
    {
        return FALSE;
    }

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiConnectPassiveInterrupt(Interrupt);

    OldIrql = KiLockVectorOf(Interrupt);

    if (!Interrupt->Connected)
    {
        Head = KiVectorHead(Interrupt->Vector);
        if (Head == NULL)
        {
            InitializeListHead(&Interrupt->InterruptListEntry);
            KiRouteVector(Interrupt);
            Interrupt->Connected = Connected = TRUE;
            Enabled = HalEnableSystemInterrupt(Interrupt->Vector, Interrupt->Irql, Interrupt->Mode);
        }
        else if (Interrupt->ShareVector && Head->ShareVector && (Interrupt->Mode == Head->Mode))
        {
            ASSERT(Interrupt->Irql <= SYNCH_LEVEL);
            ASSERT(!IsListEmpty(&Head->InterruptListEntry) || (Head->Mode != Latched));

            InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
            KiRouteVector(Head);
            Interrupt->Connected = Connected = TRUE;
        }
    }

    KiUnlockVector(OldIrql);

    if (Connected && !Enabled)
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
    _Inout_ PKINTERRUPT Interrupt)
{
    BOOLEAN WasConnected;
    PKINTERRUPT Head, Next;
    KIRQL OldIrql;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiDisconnectPassiveInterrupt(Interrupt);

    OldIrql = KiLockVectorOf(Interrupt);

    WasConnected = Interrupt->Connected;
    if (WasConnected)
    {
        Head = KiVectorHead(Interrupt->Vector);
        ASSERT(Head != NULL);

        if (IsListEmpty(&Interrupt->InterruptListEntry))
        {
            /* Last one on the vector: give it back to the unexpected-interrupt stub */
            ASSERT(Head == Interrupt);
            HalDisableSystemInterrupt(Interrupt->Vector, Interrupt->Irql);
            KeRegisterInterruptHandler(Interrupt->Vector, KiUnexpectedStubOf(Interrupt->Vector));
        }
        else
        {
            ASSERT(Interrupt->Irql <= SYNCH_LEVEL);
            Next = CONTAINING_RECORD(Interrupt->InterruptListEntry.Flink, KINTERRUPT, InterruptListEntry);
            RemoveEntryList(&Interrupt->InterruptListEntry);
            KiRouteVector((Head == Interrupt) ? Next : Head);
        }

        Interrupt->Connected = FALSE;
    }

    KiUnlockVector(OldIrql);
    return WasConnected;
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
