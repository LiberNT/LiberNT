/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The debugger's views of processors, ready queues, DPCs, timers, interrupts and waits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_MAXIMUM_LIST_WALK 4096

/* PRIVATE FUNCTIONS **********************************************************/

static
PKPRCB
KdbpProcessorArgument(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_ PULONG Number)
{
    ULONG_PTR Value = KeGetCurrentProcessorNumber();

    if ((Argc > 1) && !KdbpGetHexNumber(Argv[1], &Value))
    {
        KdbpPrint("%s is not a processor number\n", Argv[1]);
        return NULL;
    }

    if ((Value >= (ULONG_PTR)(UCHAR)KeNumberProcessors) || (KiProcessorBlock[Value] == NULL))
    {
        KdbpPrint("There is no processor %Iu\n", Value);
        return NULL;
    }

    *Number = (ULONG)Value;
    return KiProcessorBlock[Value];
}

static
BOOLEAN
KdbpAddressArgumentRequired(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_ PVOID *Address)
{
    ULONG_PTR Value;

    if (Argc < 2)
    {
        KdbPuts("An address is needed\n");
        return FALSE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Value))
        return FALSE;

    *Address = (PVOID)Value;
    return TRUE;
}

static
VOID
KdbpPrintTimer(
    _In_ PKTIMER Address)
{
    KTIMER Timer;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Timer, Address, sizeof(Timer))))
    {
        KdbpPrint("  timer %p cannot be read\n", Address);
        return;
    }

    KdbpPrint("  timer %p due 0x%I64x period %lu signal %ld DPC %p",
              Address,
              Timer.DueTime.QuadPart,
              Timer.Period,
              Timer.Header.SignalState,
              Timer.Dpc);

    if (Timer.Dpc != NULL)
    {
        KDPC Dpc;

        if (NT_SUCCESS(KdbpSafeReadMemory(&Dpc, Timer.Dpc, sizeof(Dpc))))
        {
            KdbPuts(" ");
            KdbSymPrintAddress((PVOID)Dpc.DeferredRoutine, NULL);
        }
    }

    KdbPuts("\n");
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
KdbpCmdPrcb(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG Number;
    PKPRCB Prcb = KdbpProcessorArgument(Argc, Argv, &Number);

    if (Prcb == NULL)
        return TRUE;

    KdbpPrint("KPRCB %p for processor %lu\n", Prcb, Number);
    KdbpPrint("  current thread %p, next %p, idle %p\n",
              Prcb->CurrentThread,
              Prcb->NextThread,
              Prcb->IdleThread);
    KdbpPrint("  ready summary 0x%08lx, DPC routine active %u, DPC queue depth %ld\n",
              Prcb->ReadySummary,
              (ULONG)Prcb->DpcRoutineActive,
              (LONG)Prcb->DpcData[DPC_NORMAL].DpcQueueDepth);
    KdbpPrint("  interrupts %lu, ticks kernel %lu user %lu DPC %lu interrupt %lu\n",
              Prcb->InterruptCount,
              Prcb->KernelTime,
              Prcb->UserTime,
              Prcb->DpcTime,
              Prcb->InterruptTime);

    return TRUE;
}

BOOLEAN
KdbpCmdReady(
    ULONG Argc,
    PCHAR Argv[])
{
    PLIST_ENTRY Head;
    PLIST_ENTRY Entry;
    ULONG Number;
    ULONG Count;
    LONG Priority;
    PKPRCB Prcb = KdbpProcessorArgument(Argc, Argv, &Number);

    if (Prcb == NULL)
        return TRUE;

    KdbpPrint("Ready queues of processor %lu, summary 0x%08lx\n", Number, Prcb->ReadySummary);

    for (Priority = MAXIMUM_PRIORITY - 1; (Priority >= 0) && !KdbpIsOutputAborted(); Priority--)
    {
        Head = &Prcb->DispatcherReadyListHead[Priority];
        if (Head->Flink == Head)
            continue;

        KdbpPrint("  priority %ld:\n", Priority);

        for (Entry = Head->Flink, Count = 0;
             (Entry != Head) && (Count < KDB_MAXIMUM_LIST_WALK);
             Entry = Entry->Flink, Count++)
        {
            PKTHREAD Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);

            KdbpPrint("    thread %p (%p.%p)\n",
                      Thread,
                      CONTAINING_RECORD(Thread, ETHREAD, Tcb)->Cid.UniqueProcess,
                      CONTAINING_RECORD(Thread, ETHREAD, Tcb)->Cid.UniqueThread);
        }
    }

    return TRUE;
}

BOOLEAN
KdbpCmdDpc(
    ULONG Argc,
    PCHAR Argv[])
{
    PSINGLE_LIST_ENTRY Entry;
    ULONG Number;
    ULONG Queue;
    ULONG Count;
    KDPC Dpc;
    PKPRCB Prcb = KdbpProcessorArgument(Argc, Argv, &Number);

    if (Prcb == NULL)
        return TRUE;

    KdbpPrint("DPC queues of processor %lu\n", Number);

    for (Queue = DPC_NORMAL; Queue <= DPC_THREADED; Queue++)
    {
        PKDPC_DATA Data = &Prcb->DpcData[Queue];

        KdbpPrint("  %s: depth %ld, %lu queued in all, active %p\n",
                  (Queue == DPC_NORMAL) ? "normal" : "threaded",
                  (LONG)Data->DpcQueueDepth,
                  Data->DpcCount,
                  Data->ActiveDpc);

        for (Entry = Data->DpcList.ListHead.Next, Count = 0;
             (Entry != NULL) && (Count < KDB_MAXIMUM_LIST_WALK) && !KdbpIsOutputAborted();
             Entry = Dpc.DpcListEntry.Next, Count++)
        {
            PKDPC Address = CONTAINING_RECORD(Entry, KDPC, DpcListEntry);

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Dpc, Address, sizeof(Dpc))))
            {
                KdbpPrint("    DPC %p cannot be read\n", Address);
                break;
            }

            KdbpPrint("    DPC %p importance %u target %u ", Address, Dpc.Importance, Dpc.Number);
            KdbSymPrintAddress((PVOID)Dpc.DeferredRoutine, NULL);
            KdbPuts("\n");
        }
    }

    return TRUE;
}

BOOLEAN
KdbpCmdTimer(
    ULONG Argc,
    PCHAR Argv[])
{
    PLIST_ENTRY Head;
    PLIST_ENTRY Entry;
    ULONG Bucket;
    ULONG Count = 0;
    PVOID Address;

    if (Argc > 1)
    {
        if (KdbpAddressArgumentRequired(Argc, Argv, &Address))
            KdbpPrintTimer(Address);

        return TRUE;
    }

    for (Bucket = 0; (Bucket < TIMER_TABLE_SIZE) && !KdbpIsOutputAborted(); Bucket++)
    {
        Head = &KiTimerTableListHead[Bucket].Entry;

        for (Entry = Head->Flink;
             (Entry != Head) && (Count < KDB_MAXIMUM_LIST_WALK) && !KdbpIsOutputAborted();
             Entry = Entry->Flink, Count++)
        {
            KdbpPrintTimer(CONTAINING_RECORD(Entry, KTIMER, TimerListEntry));
        }
    }

    KdbpPrint("%lu timers queued\n", Count);
    return TRUE;
}

BOOLEAN
KdbpCmdInterrupt(
    ULONG Argc,
    PCHAR Argv[])
{
    KINTERRUPT Interrupt;
    PVOID Address;

    if (!KdbpAddressArgumentRequired(Argc, Argv, &Address))
        return TRUE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Interrupt, Address, sizeof(Interrupt))))
    {
        KdbpPrint("The interrupt object at %p cannot be read\n", Address);
        return TRUE;
    }

    KdbpPrint("KINTERRUPT %p vector 0x%lx IRQL %u synchronize %u %s%s, connected %u\n",
              Address,
              Interrupt.Vector,
              Interrupt.Irql,
              Interrupt.SynchronizeIrql,
              (Interrupt.Mode == Latched) ? "latched" : "level",
              Interrupt.ShareVector ? " shared" : "",
              Interrupt.Connected);
    KdbpPrint("  service %p ", (PVOID)Interrupt.ServiceRoutine);
    KdbSymPrintAddress((PVOID)Interrupt.ServiceRoutine, NULL);
    KdbpPrint(", context %p\n", Interrupt.ServiceContext);

    return TRUE;
}

BOOLEAN
KdbpCmdDispatcher(
    ULONG Argc,
    PCHAR Argv[])
{
    DISPATCHER_HEADER Header;
    KWAIT_BLOCK WaitBlock;
    PLIST_ENTRY Head;
    PLIST_ENTRY Entry;
    PVOID Address;
    ULONG Count;

    if (!KdbpAddressArgumentRequired(Argc, Argv, &Address))
        return TRUE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Header, Address, sizeof(Header))))
    {
        KdbpPrint("The dispatcher object at %p cannot be read\n", Address);
        return TRUE;
    }

    KdbpPrint("Dispatcher object %p type %u signal %ld\n", Address, Header.Type, Header.SignalState);

    Head = &((PDISPATCHER_HEADER)Address)->WaitListHead;
    for (Entry = Header.WaitListHead.Flink, Count = 0;
         (Entry != Head) && (Count < KDB_MAXIMUM_LIST_WALK) && !KdbpIsOutputAborted();
         Entry = WaitBlock.WaitListEntry.Flink, Count++)
    {
        PKWAIT_BLOCK Block = CONTAINING_RECORD(Entry, KWAIT_BLOCK, WaitListEntry);

        if (!NT_SUCCESS(KdbpSafeReadMemory(&WaitBlock, Block, sizeof(WaitBlock))))
        {
            KdbpPrint("  wait block %p cannot be read\n", Block);
            break;
        }

        KdbpPrint("  waiter %p, wait type %u, key %u\n",
                  WaitBlock.Thread,
                  WaitBlock.WaitType,
                  WaitBlock.WaitKey);
    }

    if (Count == 0)
        KdbPuts("  no waiters\n");

    return TRUE;
}

BOOLEAN
KdbpCmdInterruptTable(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address = 0;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    KdbpArchPrintInterruptTable(Address);
    return TRUE;
}

/* EOF */
