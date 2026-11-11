# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     System-time adjustment and timer cancellation regression
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

set_time = src.definition('clockint.c', 'KeSetSystemTime')
needle = '        Entry = RemoveHeadList(&AbsoluteList);\n'
assert set_time.count(needle) == 1
set_time = set_time.replace(needle, '        TestPause();\n' + needle)

fixture = kt.PRELUDE + r'''
static volatile int Paused, Attempted, Done, DoneDuringPause;
static BOOLEAN CancelResult;
static void TestPause(void) {
    if (Paused) return;
    __atomic_store_n(&Paused, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&Attempted, __ATOMIC_ACQUIRE)) sched_yield();
    DoneDuringPause = AwaitFlag(&Done);
}
static void KiWaitTest(PVOID Object, LONG Increment) { (void)Object; (void)Increment; }
static void KiRearmPeriodicTimer(PKTIMER Timer) { (void)Timer; CHECK(!"periodic"); }
static void KeQuerySystemTimePrecise(PLARGE_INTEGER Time) { *Time = ReadTime(&KiSystemTimeValue); }
static BOOLEAN KeInsertQueueDpc(PKDPC Dpc, PVOID A, PVOID B) { (void)Dpc; (void)A; (void)B; return TRUE; }
'''
for file, name in [('ke_x.h', 'KiRemoveEntryTimer'), ('ktimer.c', 'KiInsertTimerTable'), ('ktimer.c', 'KiSignalTimer'),
                   ('ktimer.c', 'KiCompleteTimer'), ('ke_x.h', 'KxInsertTimer'), ('ke_x.h', 'KxRemoveTreeTimer'),
                   ('ktimer.c', 'KeCancelTimer')]:
    fixture += src.definition(file, name)
fixture += set_time + r'''
static void *Cancel(void *Timer) {
    while (!__atomic_load_n(&Paused, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&Attempted, 1, __ATOMIC_RELEASE);
    CancelResult = KeCancelTimer(Timer);
    __atomic_store_n(&Done, 1, __ATOMIC_RELEASE);
    return NULL;
}
int main(void) {
    KTIMER Timer;
    LARGE_INTEGER Now, Old;
    pthread_t Thread;

    Scenario = "a cancel arriving while the clock is moved";
    InitTable();
    InitTimer(&Timer);
    SetClock(100, 100);
    KeBootTime.QuadPart = 50;
    Timer.Header.Absolute = TRUE;
    Timer.Header.Inserted = TRUE;
    Timer.DueTime.QuadPart = 1000;
    InsertTailList(&KiTimerTableListHead[0].Entry, &Timer.TimerListEntry);
    KiTimerTableListHead[0].Time.QuadPart = 1000;

    CHECK(pthread_create(&Thread, NULL, Cancel, &Timer) == 0);
    Now.QuadPart = 200;
    KeSetSystemTime(&Now, &Old, FALSE, NULL);
    CHECK(pthread_join(Thread, NULL) == 0);

    /* The cancel waited for the clock change, which moved the timer first */
    CHECK(Paused && !DoneDuringPause && Done);
    CHECK(Old.QuadPart == 100 && KeBootTime.QuadPart == 150);
    CHECK(Timer.DueTime.QuadPart == 900);
    CHECK(CancelResult == TRUE);
    CHECK(!Timer.Header.Inserted && IsListEmpty(&KiTimerTableListHead[0].Entry));
    return 0;
}
'''
kt.run('time-adjustment', fixture, args.cc)
