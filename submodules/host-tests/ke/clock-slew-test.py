# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A time adjustment moves the system time by its value per default clock period at any clock step
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


def body(name, fallback=None):
    start = source.find('\n' + name + '(')
    if start < 0:
        if fallback is None:
            raise ValueError(name)
        return fallback
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


fixture = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define NTAPI
#define CLOCK_LEVEL 13
#define IPI_LEVEL 14
#define FIELD_OFFSET(t, f) __builtin_offsetof(t, f)
#define CHECK(x) do { if (!(x)) { printf("%d: %s failed\n", __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
typedef unsigned char BOOLEAN, UCHAR, KIRQL, *PBOOLEAN;
typedef unsigned long ULONG, *PULONG;
typedef uint64_t ULONGLONG, KSPIN_LOCK, *PKSPIN_LOCK;
typedef int64_t LONGLONG;
typedef union { LONGLONG QuadPart; struct { uint32_t LowPart; int32_t HighPart; }; } LARGE_INTEGER;
typedef struct { uint32_t LowPart; int32_t High1Time; int32_t High2Time; } KSYSTEM_TIME;
typedef struct { KSYSTEM_TIME InterruptTime, SystemTime, TickCount; ULONG TickCountLowDeprecated; } KUSER_SHARED_DATA, *PKUSER_SHARED_DATA;
static PKUSER_SHARED_DATA MmWriteableSharedUserData;
static volatile KSYSTEM_TIME KiSystemTimeValue, KiInterruptTimeValue, KeTickCount;
static KSPIN_LOCK KiSystemTimeLock;
static BOOLEAN KiInterruptTimeFrozen, KiTimeAdjustmentEnabled;
static ULONG KeTimeAdjustment, KeMaximumIncrement = 156250;
static ULONGLONG KiTimeAdjustmentRemainder __attribute__((unused));
static long KiTickOffset;
static volatile ULONG KiTickPhase;
static KIRQL Irql;
static int LowHolder, Held;
static LARGE_INTEGER KiReadSystemTime(volatile KSYSTEM_TIME *Time) { LARGE_INTEGER r; r.QuadPart = *(volatile LONGLONG *)Time; return r; }
static void KiWriteSystemTime(volatile KSYSTEM_TIME *Time, LARGE_INTEGER Value) { *(volatile LONGLONG *)Time = Value.QuadPart; }
static ULONGLONG KiGetInterruptTime(void) { return (ULONGLONG)KiReadSystemTime(&KiInterruptTimeValue).QuadPart; }
static ULONGLONG KiGetSystemTime(void) { CHECK(Held); return (ULONGLONG)KiReadSystemTime(&KiSystemTimeValue).QuadPart; }
static void KiWriteInterruptTime(LARGE_INTEGER Interrupt) { KiWriteSystemTime(&KiInterruptTimeValue, Interrupt); }
static void KiPublishSharedTime(ULONG Offset, LARGE_INTEGER Value) { (void)Offset; (void)Value; }
static void KiPublishSystemTimeWithBaseline(LARGE_INTEGER Interrupt, LARGE_INTEGER System) { (void)Interrupt; (void)System; }
static ULONG KiTakeElapsedTicks(long *Offset, ULONG Increment) { (void)Offset; (void)Increment; return 0; }
static KIRQL KfRaiseIrql(KIRQL New) { KIRQL Old = Irql; CHECK(New >= Old); Irql = New; return Old; }
static void KeLowerIrql(KIRQL New) { CHECK(New <= Irql); Irql = New; }
__attribute__((unused)) static KIRQL KeRaiseIrqlToSynchLevel(void) { return KfRaiseIrql(12); }
/* The sleep IPI takes the lock, so every holder has to be at IPI_LEVEL */
static void KxAcquireSpinLock(PKSPIN_LOCK Lock) { if (Irql < IPI_LEVEL) LowHolder = 1; CHECK(!*Lock); *Lock = 1; Held = 1; }
static void KxReleaseSpinLock(PKSPIN_LOCK Lock) { CHECK(*Lock); *Lock = 0; Held = 0; }
@ADJUSTED@
ULONGLONG KiUpdateSharedTime(ULONG Increment, BOOLEAN UpdateSystem) { @TICK@ }
VOID KiSetTimeAdjustment(ULONG Adjustment, BOOLEAN Enabled) { @SET@ }
VOID KiQueryTimeAdjustment(PULONG Adjustment, PBOOLEAN Enabled) { @QUERY@ }
static LONGLONG Advance(ULONG Increment, ULONG Ticks) {
    LARGE_INTEGER Start = {.QuadPart = 0x01DC000000000000LL};
    ULONG i;
    KiWriteSystemTime(&KiSystemTimeValue, Start);
    Irql = CLOCK_LEVEL;
    for (i = 0; i < Ticks; i++)
        (void)KiUpdateSharedTime(Increment, TRUE);
    CHECK(Irql == CLOCK_LEVEL);
    Irql = 0;
    return KiReadSystemTime(&KiSystemTimeValue).QuadPart - Start.QuadPart;
}
int main(void) {
    const ULONG Adjustment = 156260;
    ULONG Queried;
    BOOLEAN Enabled;
    int Failed = 0;
    LONGLONG Moved;

    KeTimeAdjustment = KeMaximumIncrement;

    /* The default clock period: each tick adds the adjustment */
    KiSetTimeAdjustment(Adjustment, TRUE);
    KiQueryTimeAdjustment(&Queried, &Enabled);
    CHECK(Queried == Adjustment && Enabled);
    Moved = Advance(156250, 10);
    if (Moved != 10LL * (LONGLONG)Adjustment) { printf("10 default periods moved the time by %lld, not %lld\n", (long long)Moved, 10LL * (LONGLONG)Adjustment); Failed = 1; }

    /* A 1 ms clock: 125 steps are 8 default periods, worth 8 adjustments */
    KiSetTimeAdjustment(Adjustment, TRUE);
    Moved = Advance(10000, 125);
    if (Moved != 8LL * (LONGLONG)Adjustment) { printf("125 steps of 1 ms moved the time by %lld, not %lld\n", (long long)Moved, 8LL * (LONGLONG)Adjustment); Failed = 1; }

    /* The adjustment that does not divide into a step is carried, not lost */
    KiSetTimeAdjustment(156251, TRUE);
    Moved = Advance(10000, 125);
    if (Moved != 8LL * 156251) { printf("a 1 ms clock lost the slew: %lld, not %lld\n", (long long)Moved, 8LL * 156251); Failed = 1; }

    /* Disabled, the clock moves by the time that passed and the adjustment is back to the period */
    KiSetTimeAdjustment(Adjustment, FALSE);
    KiQueryTimeAdjustment(&Queried, &Enabled);
    CHECK(Queried == KeMaximumIncrement && !Enabled);
    Moved = Advance(10000, 125);
    if (Moved != 125LL * 10000) { printf("a disabled adjustment moved the time by %lld\n", (long long)Moved); Failed = 1; }

    /* Enabled at the period's own value is still enabled */
    KiSetTimeAdjustment(KeMaximumIncrement, TRUE);
    KiQueryTimeAdjustment(&Queried, &Enabled);
    if (!Enabled) { printf("an adjustment equal to the period reads as disabled\n"); Failed = 1; }

    if (LowHolder) { printf("the system time lock was taken below IPI_LEVEL\n"); Failed = 1; }
    return Failed;
}
'''
fixture = fixture.replace('@ADJUSTED@', 'static ULONGLONG KiAdjustedIncrement(ULONG Increment) { ' + body('KiAdjustedIncrement', 'return Increment;') + ' }')
fixture = fixture.replace('@TICK@', body('KiUpdateSharedTime')).replace('@SET@', body('KiSetTimeAdjustment'))
fixture = fixture.replace('@QUERY@', body('KiQueryTimeAdjustment', '*Adjustment = KeTimeAdjustment; *Enabled = (KeTimeAdjustment != KeMaximumIncrement);'))
with tempfile.TemporaryDirectory(prefix='ke-clock-slew-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
