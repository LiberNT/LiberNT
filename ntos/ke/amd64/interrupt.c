/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 interrupt objects: connection to an IDT vector and disconnection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KI_STRAY_INTERRUPT_REPORTS 32

extern UCHAR KiInterruptDispatchTemplate[16];
extern KI_INTERRUPT_DISPATCH_ENTRY KiUnexpectedRange[256];
extern KI_INTERRUPT_DISPATCH_ENTRY KiUnexpectedRangeEnd[];
void KiInterruptDispatch(void);

/* trap.S passes the stub's error code, vector - 128, and sends the EOI itself */
VOID
NTAPI
KiUnexpectedInterruptHandler(
    _In_ ULONG64 ErrorCode)
{
    static LONG Reports = 0;
    ULONG Vector = (ULONG)((ErrorCode + 128) & 0xFF);
    LONG Count = InterlockedIncrement(&Reports);

    if (Count <= KI_STRAY_INTERRUPT_REPORTS)
        DbgPrint("Unexpected interrupt on vector 0x%02lx, processor %u (%ld so far)\n", Vector, KeGetCurrentPrcb()->Number, Count);
}

/* The dispatch stub is position independent, so a plain copy is enough */
VOID
KiAmd64PrepareInterruptDispatch(
    _Inout_ PKINTERRUPT Interrupt)
{
    RtlCopyMemory(Interrupt->DispatchCode, KiInterruptDispatchTemplate, sizeof(Interrupt->DispatchCode));
    Interrupt->DispatchAddress = KiInterruptDispatch;
}

/* The IDT is per processor: run on the target and hold the dispatcher lock while its vector changes */
static
KIRQL
KiLockVectorOf(
    _In_ PKINTERRUPT Interrupt)
{
    KeSetSystemAffinityThread(1ULL << Interrupt->Number);
    return KiAcquireDispatcherLock();
}

static
VOID
KiUnlockVector(
    _In_ KIRQL OldIrql)
{
    KiReleaseDispatcherLock(OldIrql);
    KeRevertToUserAffinityThread();
}

static
BOOLEAN
KiIsUnexpectedHandler(
    _In_ PVOID Handler)
{
    return (BOOLEAN)((Handler >= (PVOID)KiUnexpectedRange) && (Handler <= (PVOID)KiUnexpectedRangeEnd));
}

/* A connected vector points at the DispatchCode of the interrupt that heads its chain */
static
PKINTERRUPT
KiChainHeadOf(
    _In_ PVOID Handler,
    _In_ PKINTERRUPT Interrupt)
{
    PKINTERRUPT Head = CONTAINING_RECORD(Handler, KINTERRUPT, DispatchCode);

    ASSERT(Head->Connected);
    ASSERT(Head->DispatchAddress == KiInterruptDispatch);
    ASSERT(Head->Vector == Interrupt->Vector);
    ASSERT(Head->Number == Interrupt->Number);
    return Head;
}

static
BOOLEAN
KiClaimFreeVector(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ PVOID UnexpectedHandler)
{
    ASSERT(Interrupt->DispatchAddress == KiInterruptDispatch);

    InitializeListHead(&Interrupt->InterruptListEntry);
    KeRegisterInterruptHandler(Interrupt->Vector, Interrupt->DispatchCode);

    if (HalEnableSystemInterrupt(Interrupt->Vector, Interrupt->Irql, Interrupt->Mode))
        return TRUE;

    DPRINT1("HalEnableSystemInterrupt failed\n");
    KeRegisterInterruptHandler(Interrupt->Vector, UnexpectedHandler);
    return FALSE;
}

static
BOOLEAN
KiJoinChain(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ PKINTERRUPT Head)
{
    ASSERT(Head->InterruptListEntry.Flink->Blink == &Head->InterruptListEntry);
    ASSERT(Head->InterruptListEntry.Blink->Flink == &Head->InterruptListEntry);

    /* Both sides must agree to share, and with the same trigger mode */
    if (!Interrupt->ShareVector || !Head->ShareVector || (Interrupt->Mode != Head->Mode))
        return FALSE;

    InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
    return TRUE;
}

BOOLEAN
NTAPI
KeConnectInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    PVOID Handler;
    BOOLEAN Connected;
    KIRQL OldIrql;

    if ((Interrupt->Vector < PRIMARY_VECTOR_BASE) || (Interrupt->Vector > MAXIMUM_IDTVECTOR))
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

    OldIrql = KiLockVectorOf(Interrupt);

    if (!Interrupt->Connected)
    {
        Handler = KeQueryInterruptHandler(Interrupt->Vector);

        if (KiIsUnexpectedHandler(Handler))
            Interrupt->Connected = KiClaimFreeVector(Interrupt, Handler);
        else
            Interrupt->Connected = KiJoinChain(Interrupt, KiChainHeadOf(Handler, Interrupt));
    }

    Connected = Interrupt->Connected;
    KiUnlockVector(OldIrql);
    return Connected;
}

static
VOID
KiLeaveChain(
    _Inout_ PKINTERRUPT Interrupt)
{
    PKINTERRUPT Head = KiChainHeadOf(KeQueryInterruptHandler(Interrupt->Vector), Interrupt);
    PLIST_ENTRY Others = &Head->InterruptListEntry;

    ASSERT(Interrupt->InterruptListEntry.Flink->Blink == &Interrupt->InterruptListEntry);
    ASSERT(Interrupt->InterruptListEntry.Blink->Flink == &Interrupt->InterruptListEntry);

    if (IsListEmpty(Others))
    {
        /* Last one on the vector: give it back to the unexpected-interrupt stub */
        ASSERT(Head == Interrupt);
        HalDisableSystemInterrupt(Interrupt->Vector, Interrupt->Irql);
        KeRegisterInterruptHandler(Interrupt->Vector, &KiUnexpectedRange[Interrupt->Vector]._Op_push);
    }
    else if (Head == Interrupt)
    {
        /* The next interrupt becomes the head: the list moves to it and the IDT points at its stub */
        PLIST_ENTRY NextEntry = Others->Flink;
        PKINTERRUPT Next = CONTAINING_RECORD(NextEntry, KINTERRUPT, InterruptListEntry);

        ASSERT(Next->Connected);
        ASSERT(Next->DispatchAddress == KiInterruptDispatch);

        RemoveTailList(NextEntry);
        KeRegisterInterruptHandler(Interrupt->Vector, Next->DispatchCode);
    }
    else
    {
        RemoveEntryList(&Interrupt->InterruptListEntry);
    }
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiDisconnectPassiveInterrupt(Interrupt);

    OldIrql = KiLockVectorOf(Interrupt);

    if (Interrupt->Connected)
    {
        KiLeaveChain(Interrupt);
        Interrupt->Connected = FALSE;
    }

    KiUnlockVector(OldIrql);
    return TRUE;
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
