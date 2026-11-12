# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Timer wait-block cleanup regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import sys
sys.dont_write_bytecode = True
import ke_timer_fixture as kt

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
src = kt.Sources(args.root)

fixture = kt.PRELUDE + r'''
#define Waiting 5
#define GateWait 8
struct _KTHREAD;
typedef struct { int Unused; } KPRCB, *PKPRCB;
typedef struct { LIST_ENTRY WaitListEntry; struct _KTHREAD *Thread; PVOID Object; USHORT WaitKey; UCHAR WaitType, BlockState; } KWAIT_BLOCK, *PKWAIT_BLOCK;
typedef struct _KTHREAD {
    UCHAR State; LONG_PTR WaitStatus; ULONG WaitBlockCount; BOOLEAN TimerActive;
    PKWAIT_BLOCK WaitBlockList; KWAIT_BLOCK WaitBlock[4]; KTIMER Timer; PKPRCB WaitPrcb; LIST_ENTRY WaitListEntry;
} KTHREAD, *PKTHREAD;
static KPRCB Prcb;
static LIST_ENTRY PrcbWaitList;
static int WaitLockHeld;
static void KiAcquireWaitLock(PKPRCB P) { CHECK(P == &Prcb && !WaitLockHeld); WaitLockHeld = 1; }
static void KiReleaseWaitLock(PKPRCB P) { CHECK(P == &Prcb && WaitLockHeld); WaitLockHeld = 0; }
'''
for file, name in [('ke_x.h', 'KxRemoveTreeTimer'), ('dispatch.c', 'KiThreadWaitBlock'), ('dispatch.c', 'KiThreadWaitBlockCount'),
                   ('dispatch.c', 'KiUnlinkOneWaitBlock'), ('dispatch.c', 'KiUnlinkWaitBlocks'), ('dispatch.c', 'KiUnlinkThread')]:
    fixture += src.definition(file, name)
fixture += r'''
static KTHREAD Thread;
static DISPATCHER_HEADER Object;
static void Setup(const char *Name, BOOLEAN WithObject, BOOLEAN TimerInTable) {
    PKWAIT_BLOCK TimerBlock = &Thread.WaitBlock[TIMER_WAIT_BLOCK];
    Scenario = Name;
    InitTable();
    Thread = (KTHREAD){0};
    InitTimer(&Thread.Timer);
    Object = (DISPATCHER_HEADER){0};
    InitializeListHead(&Object.WaitListHead);
    InitializeListHead(&PrcbWaitList);
    Thread.State = Waiting;
    Thread.WaitPrcb = &Prcb;
    InsertTailList(&PrcbWaitList, &Thread.WaitListEntry);
    TimerBlock->Thread = &Thread;
    TimerBlock->Object = &Thread.Timer;
    InsertTailList(&Thread.Timer.Header.WaitListHead, &TimerBlock->WaitListEntry);
    if (WithObject) {
        Thread.WaitBlock[0].Thread = &Thread;
        Thread.WaitBlock[0].Object = &Object;
        InsertTailList(&Object.WaitListHead, &Thread.WaitBlock[0].WaitListEntry);
        Thread.WaitBlockList = &Thread.WaitBlock[0];
        Thread.WaitBlockCount = 1;
        Thread.TimerActive = TRUE;
    } else {
        /* A delay wait: the timer block is the array and TimerActive stays clear */
        Thread.WaitBlockList = TimerBlock;
        Thread.WaitBlockCount = 1;
    }
    if (TimerInTable) {
        Thread.Timer.Header.Inserted = TRUE;
        Thread.Timer.DueTime.QuadPart = 1000;
        InsertTailList(&KiTimerTableListHead[0].Entry, &Thread.Timer.TimerListEntry);
        KiTimerTableListHead[0].Time.QuadPart = 1000;
    }
}
static void CheckCleaned(void) {
    CHECK(IsListEmpty(&Object.WaitListHead));
    CHECK(IsListEmpty(&Thread.Timer.Header.WaitListHead));
    CHECK(Thread.WaitBlock[0].WaitListEntry.Flink == NULL);
    CHECK(Thread.WaitBlock[TIMER_WAIT_BLOCK].WaitListEntry.Flink == NULL);
    CHECK(!Thread.TimerActive);
    CHECK(!Thread.Timer.Header.Inserted && IsListEmpty(&KiTimerTableListHead[0].Entry));
    CHECK(Thread.WaitPrcb == NULL && IsListEmpty(&PrcbWaitList) && !WaitLockHeld);
}
/* The wait lists, the timer and the processor's wait list are left under the dispatcher lock the caller holds */
static void Unlink(LONG_PTR Status) {
    KIRQL Old = KiAcquireDispatcherLock();
    KiUnlinkThread(&Thread, Status);
    KiReleaseDispatcherLock(Old);
}
int main(void) {
    KIRQL Old;

    Setup("a wait on an object with a timeout still in the table", TRUE, TRUE);
    Unlink(0x80);
    CHECK(Thread.WaitStatus == 0x80);
    CheckCleaned();

    Setup("a wait whose timer the expiry already took out of the table", TRUE, FALSE);
    Unlink(0);
    CheckCleaned();

    Setup("a delay wait, its timer block the whole array", FALSE, TRUE);
    Unlink(0);
    CheckCleaned();

    /* A second pass over the same thread unlinks nothing twice */
    Scenario = "a second pass";
    Old = KiAcquireDispatcherLock();
    KiUnlinkWaitBlocks(&Thread);
    KiReleaseDispatcherLock(Old);
    CHECK(IsListEmpty(&Thread.Timer.Header.WaitListHead) && IsListEmpty(&Object.WaitListHead));
    return 0;
}
'''
kt.run('wait-timer-cleanup', fixture, args.cc)
