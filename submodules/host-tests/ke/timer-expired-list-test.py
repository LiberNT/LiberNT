# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Expired timer list cancellation and rearm regression
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

expire = src.definition('ktimer.c', 'KiTimerListExpire')
needle = '        Timer->Header.Inserted = FALSE;\n        Timer->Header.SignalState = 1;\n'
assert expire.count(needle) == 1
expire = expire.replace(needle, '        TestPause();\n' + needle)

fixture = kt.PRELUDE + r'''
#define MAX_TIMER_DPCS 16
#define MAXIMUM_PROCESSORS 64
#define KI_DPC_TARGET_PROCESSOR_OFFSET MAXIMUM_PROCESSORS
#define KI_MAX_TOLERANCE_LOG2 30
#define DpcObject 19
#define PtrToUlong(p) ((ULONG)(ULONG_PTR)(p))
#define AFFINITY_MASK(n) ((ULONG)1 << (n))
typedef void (*PKDEFERRED_ROUTINE)(PKDPC, PVOID, PVOID, PVOID);
typedef struct { PKDPC Dpc; PKDEFERRED_ROUTINE Routine; PVOID Context; } DPC_QUEUE_ENTRY;
static volatile int PauseExpiry, Paused, Attempted, Done, DoneDuringPause;
static int Routines;
static BOOLEAN CancelResult, ResetResult;
static void TestPause(void) {
    if (!PauseExpiry || Paused) return;
    __atomic_store_n(&Paused, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&Attempted, __ATOMIC_ACQUIRE)) sched_yield();
    DoneDuringPause = AwaitFlag(&Done);
}
static ULONG KeGetCurrentProcessorNumber(void) { return 0; }
static void KiWaitTest(PVOID Object, LONG Increment) { (void)Object; (void)Increment; }
static void KeQuerySystemTimePrecise(PLARGE_INTEGER Time) { *Time = ReadTime(&KiSystemTimeValue); }
static BOOLEAN KeInsertQueueDpc(PKDPC Dpc, PVOID A, PVOID B) { (void)Dpc; (void)A; (void)B; CHECK(!"queued"); return TRUE; }
static void Routine(PKDPC Dpc, PVOID Context, PVOID A, PVOID B) { (void)Dpc; (void)Context; (void)A; (void)B; Routines++; }
'''
for file, name in [('ke_x.h', 'KiRemoveEntryTimer'), ('ke_x.h', 'KiComputeDueTime'), ('ke_x.h', 'KiRoundToDefaultTimerResolution'),
                   ('ke_x.h', 'KxRemoveTreeTimer'), ('ktimer.c', 'KiDecodeTolerableDelay'), ('ktimer.c', 'KiCoalesceDueTime'),
                   ('ktimer.c', 'KiPeriodToInterval'), ('ktimer.c', 'KiInsertTimerTable'), ('ktimer.c', 'KiRearmPeriodicTimer')]:
    fixture += src.definition(file, name)
fixture += expire
for file, name in [('ktimer.c', 'KiTimerExpiration'), ('ktimer.c', 'KeCancelTimer'), ('ktimer.c', 'KiSignalTimer'),
                   ('ktimer.c', 'KiCompleteTimer'), ('ke_x.h', 'KxInsertTimer'), ('ktimer.c', 'KeSetTimerEx')]:
    fixture += src.definition(file, name)
fixture += r'''
static KTIMER Timer;
static KDPC Dpc;
static void *Expire(void *Unused) {
    (void)Unused;
    Irql = DISPATCH_LEVEL;
    KiTimerExpiration(NULL, NULL, UlongToPtr(0), UlongToPtr(0));
    CHECK(Irql == DISPATCH_LEVEL && !DispatcherHeld);
    return NULL;
}
static void *Cancel(void *Unused) {
    (void)Unused;
    while (!__atomic_load_n(&Paused, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&Attempted, 1, __ATOMIC_RELEASE);
    CancelResult = KeCancelTimer(&Timer);
    __atomic_store_n(&Done, 1, __ATOMIC_RELEASE);
    return NULL;
}
static void *ResetTimer(void *Unused) {
    LARGE_INTEGER Due;
    (void)Unused;
    while (!__atomic_load_n(&Paused, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&Attempted, 1, __ATOMIC_RELEASE);
    Due.QuadPart = -500;
    ResetResult = KeSetTimerEx(&Timer, Due, 0, &Dpc);
    __atomic_store_n(&Done, 1, __ATOMIC_RELEASE);
    return NULL;
}
/* A timer that came due at 50, with the clock at 100 */
static void Arm(const char *Name, LONG Period, int Pause) {
    Scenario = Name;
    InitTable();
    InitTimer(&Timer);
    SetClock(100, 100);
    Dpc = (KDPC){0};
    Dpc.Type = DpcObject;
    Dpc.DeferredRoutine = Routine;
    Timer.Dpc = &Dpc;
    Timer.Period = Period;
    Timer.Header.Inserted = TRUE;
    Timer.DueTime.QuadPart = 50;
    InsertTailList(&KiTimerTableListHead[0].Entry, &Timer.TimerListEntry);
    KiTimerTableListHead[0].Time.QuadPart = 50;
    Routines = 0;
    PauseExpiry = Pause;
    Paused = Attempted = Done = DoneDuringPause = 0;
    CancelResult = 2;
}
static void ExpireWithCancel(void) {
    pthread_t ExpireThread, CancelThread;
    CHECK(pthread_create(&ExpireThread, NULL, Expire, NULL) == 0);
    CHECK(pthread_create(&CancelThread, NULL, Cancel, NULL) == 0);
    CHECK(pthread_join(ExpireThread, NULL) == 0);
    CHECK(pthread_join(CancelThread, NULL) == 0);
    /* The cancel waited for the expiry to finish with the timer */
    CHECK(Paused && !DoneDuringPause && Done);
}
int main(void) {
    pthread_t ExpireThread;

    Arm("a periodic timer rearmed when it expires", 10, 0);
    CHECK(pthread_create(&ExpireThread, NULL, Expire, NULL) == 0);
    CHECK(pthread_join(ExpireThread, NULL) == 0);
    CHECK(Timer.Header.SignalState == 1 && Routines == 1);
    CHECK(Timer.Header.Inserted && Timer.DueTime.QuadPart == 100 + 10 * 10000);
    CHECK(KiTimerTableListHead[0].Entry.Flink == &Timer.TimerListEntry);

    Arm("a cancel arriving while a one-shot timer expires", 0, 1);
    ExpireWithCancel();
    CHECK(CancelResult == FALSE && Timer.Header.SignalState == 1 && Routines == 1);
    CHECK(!Timer.Header.Inserted && IsListEmpty(&KiTimerTableListHead[0].Entry));

    Arm("a cancel arriving while a periodic timer expires", 10, 1);
    ExpireWithCancel();
    CHECK(CancelResult == TRUE && Timer.Header.SignalState == 1 && Routines == 1);
    CHECK(!Timer.Header.Inserted && IsListEmpty(&KiTimerTableListHead[0].Entry));

    /* A reset waits for the expiry, then arms the timer afresh, unsignalled */
    Arm("a reset arriving while a one-shot timer expires", 0, 1);
    {
        pthread_t ResetThread;
        CHECK(pthread_create(&ExpireThread, NULL, Expire, NULL) == 0);
        CHECK(pthread_create(&ResetThread, NULL, ResetTimer, NULL) == 0);
        CHECK(pthread_join(ExpireThread, NULL) == 0);
        CHECK(pthread_join(ResetThread, NULL) == 0);
    }
    CHECK(Paused && !DoneDuringPause && Done);
    CHECK(ResetResult == FALSE && Routines == 1);
    CHECK(Timer.Header.Inserted && Timer.Header.SignalState == 0 && Timer.DueTime.QuadPart == 600);
    CHECK(KiTimerTableListHead[0].Entry.Flink == &Timer.TimerListEntry);
    return 0;
}
'''
kt.run('timer-expired-list', fixture, args.cc)
