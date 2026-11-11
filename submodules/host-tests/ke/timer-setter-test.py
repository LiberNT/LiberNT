# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Absolute timer setter and system-time adjustment regression
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
static volatile int PauseQuery, QueryRead, ClockStarted, ClockDone, ClockDoneDuringPause;
static int Signals;
/* The setter has read the clock and not yet converted its due time when the clock is moved */
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
static void KiWaitTest(PVOID Object, LONG Increment) { (void)Object; (void)Increment; Signals++; }
static void KiRearmPeriodicTimer(PKTIMER Timer) { (void)Timer; CHECK(!"periodic"); }
static BOOLEAN KeInsertQueueDpc(PKDPC Dpc, PVOID A, PVOID B) { (void)Dpc; (void)A; (void)B; return TRUE; }
'''
for file, name in [('ke_x.h', 'KiRemoveEntryTimer'), ('ktimer.c', 'KiInsertTimerTable'), ('ktimer.c', 'KiSignalTimer'),
                   ('ktimer.c', 'KiCompleteTimer'), ('ke_x.h', 'KxInsertTimer'), ('ke_x.h', 'KiComputeDueTime'),
                   ('ke_x.h', 'KxRemoveTreeTimer'), ('ktimer.c', 'KeSetTimerEx'), ('ktimer.c', 'KeCancelTimer'),
                   ('clockint.c', 'KeSetSystemTime')]:
    fixture += src.definition(file, name)
fixture += r'''
static void *SetTimer(void *Timer) {
    LARGE_INTEGER Due;
    Due.QuadPart = 1000;
    CHECK(!KeSetTimerEx(Timer, Due, 0, NULL));
    return NULL;
}
static void *MoveClock(void *Unused) {
    LARGE_INTEGER Now, Old;
    (void)Unused;
    while (!__atomic_load_n(&QueryRead, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&ClockStarted, 1, __ATOMIC_RELEASE);
    Now.QuadPart = 400;
    KeSetSystemTime(&Now, &Old, FALSE, NULL);
    CHECK(Old.QuadPart == 100);
    __atomic_store_n(&ClockDone, 1, __ATOMIC_RELEASE);
    return NULL;
}
int main(void) {
    KTIMER Timer;
    LARGE_INTEGER Expired;
    pthread_t Setter, Clock;

    Scenario = "the clock moved while an absolute timer is set";
    InitTable();
    InitTimer(&Timer);
    SetClock(100, 100);
    __atomic_store_n(&PauseQuery, 1, __ATOMIC_RELEASE);
    CHECK(pthread_create(&Setter, NULL, SetTimer, &Timer) == 0);
    CHECK(pthread_create(&Clock, NULL, MoveClock, NULL) == 0);
    CHECK(pthread_join(Setter, NULL) == 0);
    CHECK(pthread_join(Clock, NULL) == 0);

    /* Whichever way the two were ordered, the timer is due 600 after the new clock's 400 */
    CHECK(!ClockDoneDuringPause);
    CHECK(Timer.Header.Inserted && Timer.Header.Absolute);
    CHECK(Timer.DueTime.QuadPart == 700);

    Scenario = "cancelling the moved timer";
    CHECK(KeCancelTimer(&Timer));
    CHECK(!Timer.Header.Inserted && IsListEmpty(&KiTimerTableListHead[0].Entry));

    Scenario = "an absolute time already passed";
    Expired.QuadPart = 50;
    CHECK(!KeSetTimerEx(&Timer, Expired, 0, NULL));
    CHECK(Timer.Header.SignalState == 1 && !Timer.Header.Inserted);
    CHECK(IsListEmpty(&KiTimerTableListHead[0].Entry));
    return 0;
}
'''
kt.run('timer-setter', fixture, args.cc)
