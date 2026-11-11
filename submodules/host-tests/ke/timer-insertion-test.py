# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Wait timer insertion and time adjustment regression
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
typedef struct { int SetTimerResolution; } EPROCESS, *PEPROCESS;
struct _KTHREAD;
typedef struct { LIST_ENTRY WaitListEntry; struct _KTHREAD *Thread; PVOID Object; USHORT WaitKey; UCHAR WaitType, BlockState; } KWAIT_BLOCK, *PKWAIT_BLOCK;
typedef struct _KTHREAD { KTIMER Timer; KWAIT_BLOCK WaitBlock[4]; struct { PEPROCESS Process; } ApcState; } KTHREAD, *PKTHREAD;
#define KxPublishTimerWaitBlock() InsertTailList(&Timer->Header.WaitListHead, &TimerBlock->WaitListEntry)
static EPROCESS Process;
static KTHREAD Waiter;
static PKTHREAD KeGetCurrentThread(void) { return &Waiter; }
static volatile int PauseQuery, QueryRead, ClockStarted, ClockDone, ClockDoneDuringPause;
/* The waiter has read the clock and not yet converted its deadline when the clock is moved */
static void KeQuerySystemTimePrecise(PLARGE_INTEGER Time) {
    *Time = ReadTime(&KiSystemTimeValue);
    if (!__atomic_load_n(&PauseQuery, __ATOMIC_ACQUIRE))
        return;
    __atomic_store_n(&PauseQuery, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&QueryRead, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&ClockStarted, __ATOMIC_ACQUIRE)) sched_yield();
    if (!AwaitFlag(&DispatcherContended))
        ClockDoneDuringPause = __atomic_load_n(&ClockDone, __ATOMIC_ACQUIRE);
}
static void KiWaitTest(PVOID Object, LONG Increment) { (void)Object; (void)Increment; }
static void KiRearmPeriodicTimer(PKTIMER Timer) { (void)Timer; CHECK(!"periodic"); }
static BOOLEAN KeInsertQueueDpc(PKDPC Dpc, PVOID A, PVOID B) { (void)Dpc; (void)A; (void)B; return TRUE; }
'''
for file, name in [('ke_x.h', 'KiRemoveEntryTimer'), ('ktimer.c', 'KiInsertTimerTable'), ('ktimer.c', 'KiSignalTimer'),
                   ('ktimer.c', 'KiCompleteTimer'), ('ke_x.h', 'KxInsertTimer'), ('ke_x.h', 'KiRoundToDefaultTimerResolution'),
                   ('ke_x.h', 'KxSetTimerForThreadWait'), ('ke_x.h', 'KxInsertThreadWaitTimer'),
                   ('waitobj.c', 'KiArmThreadWaitTimer'), ('clockint.c', 'KeSetSystemTime')]:
    fixture += src.definition(file, name)
fixture += r'''
static LARGE_INTEGER Interval, Due;
static BOOLEAN Armed;
/* A wait arms its timer with the dispatcher database held, as KeWaitForSingleObject does */
static void *Wait(void *Unused) {
    KIRQL Old;
    (void)Unused;
    Old = KiAcquireDispatcherLock();
    Armed = KiArmThreadWaitTimer(&Waiter, &Interval, KernelMode, &Due);
    KiReleaseDispatcherLock(Old);
    return NULL;
}
static void *MoveClock(void *Unused) {
    LARGE_INTEGER Now, Old;
    (void)Unused;
    while (!__atomic_load_n(&QueryRead, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&ClockStarted, 1, __ATOMIC_RELEASE);
    Now.QuadPart = 400;
    KeSetSystemTime(&Now, &Old, FALSE, NULL);
    __atomic_store_n(&ClockDone, 1, __ATOMIC_RELEASE);
    return NULL;
}
static void Reset(const char *Name) {
    Scenario = Name;
    InitTable();
    InitTimer(&Waiter.Timer);
    Waiter.WaitBlock[TIMER_WAIT_BLOCK] = (KWAIT_BLOCK){0};
    Waiter.ApcState.Process = &Process;
    SetClock(100, 100);
    DispatcherContended = 0;
}
int main(void) {
    pthread_t WaitThread, ClockThread;
    KIRQL Old;

    Reset("the clock moved while an absolute wait arms its timer");
    Interval.QuadPart = 1000;
    __atomic_store_n(&PauseQuery, 1, __ATOMIC_RELEASE);
    CHECK(pthread_create(&WaitThread, NULL, Wait, NULL) == 0);
    CHECK(pthread_create(&ClockThread, NULL, MoveClock, NULL) == 0);
    CHECK(pthread_join(WaitThread, NULL) == 0);
    CHECK(pthread_join(ClockThread, NULL) == 0);
    CHECK(!ClockDoneDuringPause);
    CHECK(Armed && Due.QuadPart == 1000);
    CHECK(Waiter.Timer.Header.Inserted && Waiter.Timer.Header.Absolute);
    CHECK(Waiter.Timer.DueTime.QuadPart == 700);
    CHECK(Waiter.Timer.Header.WaitListHead.Flink == &Waiter.WaitBlock[TIMER_WAIT_BLOCK].WaitListEntry);
    CHECK(Waiter.WaitBlock[TIMER_WAIT_BLOCK].Object == &Waiter.Timer);

    Reset("a relative wait does not move with the clock");
    Interval.QuadPart = -500;
    Old = KiAcquireDispatcherLock();
    Armed = KiArmThreadWaitTimer(&Waiter, &Interval, KernelMode, &Due);
    KiReleaseDispatcherLock(Old);
    CHECK(Armed && !Waiter.Timer.Header.Absolute && Waiter.Timer.DueTime.QuadPart == 600);
    {
        LARGE_INTEGER Now, Previous;
        Now.QuadPart = 400;
        KeSetSystemTime(&Now, &Previous, FALSE, NULL);
    }
    CHECK(Waiter.Timer.Header.Inserted && Waiter.Timer.DueTime.QuadPart == 600);

    Reset("an absolute deadline already passed");
    SetClock(400, 100);
    Interval.QuadPart = 50;
    Old = KiAcquireDispatcherLock();
    Armed = KiArmThreadWaitTimer(&Waiter, &Interval, KernelMode, &Due);
    KiReleaseDispatcherLock(Old);
    CHECK(!Armed && !Waiter.Timer.Header.Inserted);
    CHECK(IsListEmpty(&Waiter.Timer.Header.WaitListHead) && IsListEmpty(&KiTimerTableListHead[0].Entry));
    return 0;
}
'''
kt.run('timer-insertion', fixture, args.cc)
