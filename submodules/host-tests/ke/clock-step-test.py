# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The clock step follows the resolution requests and the high-resolution deadlines; only the requests are reported
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ke/clockint.c').read_text()


def body(name):
    start = source.index('\n' + name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


fixture = r'''
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define NTAPI
#define DISPATCH_LEVEL 2
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
typedef unsigned char BOOLEAN, KIRQL;
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef ULONG KSPIN_LOCK;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct _KI_HIGH_RESOLUTION_DEADLINE { LIST_ENTRY Link; ULONGLONG DueTime; BOOLEAN Armed; } KI_HIGH_RESOLUTION_DEADLINE, *PKI_HIGH_RESOLUTION_DEADLINE;
static const char *Scenario;
static KIRQL Irql;
static ULONG KeMaximumIncrement = 156250, KeMinimumIncrement = 10000, KeTimeIncrement = 156250;
static ULONG HalNext;
static int HalCalls;
static BOOLEAN OneShot;
static ULONGLONG ArchDeadline;
static KSPIN_LOCK KiClockStepLock;
static LIST_ENTRY KiHighResolutionDeadlines = { &KiHighResolutionDeadlines, &KiHighResolutionDeadlines };
static ULONG KiRequestedIncrement, KiGrantedIncrement, KiAppliedRequest;
static BOOLEAN KiClockStepBoosted;
static KIRQL KeGetCurrentIrql(void) { return Irql; }
static void KeAcquireSpinLock(KSPIN_LOCK *Lock, KIRQL *Old) { CHECK(*Lock == 0); *Lock = 1; *Old = Irql; Irql = DISPATCH_LEVEL; }
static void KeReleaseSpinLock(KSPIN_LOCK *Lock, KIRQL Old) { CHECK(*Lock == 1); *Lock = 0; Irql = Old; }
static void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry) { Entry->Flink = Head; Entry->Blink = Head->Blink; Head->Blink->Flink = Entry; Head->Blink = Entry; }
static void RemoveEntryList(PLIST_ENTRY Entry) { Entry->Blink->Flink = Entry->Flink; Entry->Flink->Blink = Entry->Blink; }
/* The platform clamps the step to what it offers and applies the last one it was given */
static ULONG HalSetTimeIncrement(ULONG Increment) {
    HalCalls++;
    if (Increment < KeMinimumIncrement) Increment = KeMinimumIncrement;
    if (Increment > KeMaximumIncrement) Increment = KeMaximumIncrement;
    HalNext = Increment;
    return Increment;
}
static BOOLEAN KiSetClockDeadline(ULONGLONG InterruptTime) { ArchDeadline = InterruptTime; return OneShot; }
static int Armed(void) { int Count = 0; PLIST_ENTRY Entry; for (Entry = KiHighResolutionDeadlines.Flink; Entry != &KiHighResolutionDeadlines; Entry = Entry->Flink) Count++; return Count; }
static void KiApplyClockStep(void) { @APPLY@ }
static void KiArmHighResolutionDeadline(PKI_HIGH_RESOLUTION_DEADLINE Deadline, ULONGLONG DueTime) { @ARM@ }
static void KiDisarmHighResolutionDeadline(PKI_HIGH_RESOLUTION_DEADLINE Deadline) { @DISARM@ }
static void KiSetRequestedTimeIncrement(ULONG Increment) { @REQUEST@ }
static ULONG KiQueryRequestedTimeIncrement(void) { @QUERY@ }
int main(void) {
    KI_HIGH_RESOLUTION_DEADLINE First = {0}, Second = {0};
    int Calls;

    Scenario = "nothing asked for";
    CHECK(KiQueryRequestedTimeIncrement() == KeMaximumIncrement && KeTimeIncrement == KeMaximumIncrement);

    Scenario = "a process asks for a millisecond, then goes";
    KiSetRequestedTimeIncrement(10000);
    CHECK(KeTimeIncrement == 10000 && HalNext == 10000 && KiQueryRequestedTimeIncrement() == 10000);
    KiSetRequestedTimeIncrement(KeMaximumIncrement);
    CHECK(KeTimeIncrement == KeMaximumIncrement && HalNext == KeMaximumIncrement);
    CHECK(KiQueryRequestedTimeIncrement() == KeMaximumIncrement && Irql == 0);

    Scenario = "high-resolution deadlines on a clock without one-shot";
    KiArmHighResolutionDeadline(&First, 1000);
    CHECK(First.Armed && Armed() == 1 && ArchDeadline == 1000);
    CHECK(KeTimeIncrement == KeMinimumIncrement && HalNext == KeMinimumIncrement);
    CHECK(KiQueryRequestedTimeIncrement() == KeMaximumIncrement);
    Calls = HalCalls;
    KiArmHighResolutionDeadline(&Second, 700);
    CHECK(Armed() == 2 && ArchDeadline == 700 && HalCalls == Calls);
    KiDisarmHighResolutionDeadline(&First);
    CHECK(!First.Armed && Armed() == 1 && ArchDeadline == 700 && KeTimeIncrement == KeMinimumIncrement);
    KiDisarmHighResolutionDeadline(&First);
    CHECK(Armed() == 1);
    KiDisarmHighResolutionDeadline(&Second);
    CHECK(Armed() == 0 && ArchDeadline == 0 && KeTimeIncrement == KeMaximumIncrement && HalNext == KeMaximumIncrement);

    Scenario = "a request changing while the finest step is held";
    KiArmHighResolutionDeadline(&First, 2000);
    KiSetRequestedTimeIncrement(50000);
    CHECK(KiQueryRequestedTimeIncrement() == 50000 && KeTimeIncrement == KeMinimumIncrement && HalNext == KeMinimumIncrement);
    KiDisarmHighResolutionDeadline(&First);
    CHECK(KeTimeIncrement == 50000 && HalNext == 50000 && KiQueryRequestedTimeIncrement() == 50000);
    KiSetRequestedTimeIncrement(KeMaximumIncrement);
    CHECK(KeTimeIncrement == KeMaximumIncrement && KiQueryRequestedTimeIncrement() == KeMaximumIncrement);

    Scenario = "a clock that interrupts at the deadline";
    OneShot = TRUE;
    KiArmHighResolutionDeadline(&First, 500);
    KiArmHighResolutionDeadline(&Second, 300);
    CHECK(ArchDeadline == 300 && KeTimeIncrement == KeMaximumIncrement);
    KiArmHighResolutionDeadline(&Second, 800);
    CHECK(Armed() == 2 && ArchDeadline == 500);
    KiDisarmHighResolutionDeadline(&First);
    CHECK(ArchDeadline == 800);
    KiDisarmHighResolutionDeadline(&Second);
    CHECK(ArchDeadline == 0 && Armed() == 0 && KeTimeIncrement == KeMaximumIncrement && KiClockStepLock == 0);
    return 0;
}
'''
for marker, name in [('APPLY', 'KiApplyClockStep'), ('ARM', 'KiArmHighResolutionDeadline'),
                     ('DISARM', 'KiDisarmHighResolutionDeadline'), ('REQUEST', 'KiSetRequestedTimeIncrement'),
                     ('QUERY', 'KiQueryRequestedTimeIncrement')]:
    fixture = fixture.replace('@' + marker + '@', body(name))
with tempfile.TemporaryDirectory(prefix='ke-clock-step-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
