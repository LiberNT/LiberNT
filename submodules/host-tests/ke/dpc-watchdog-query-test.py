# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Verify DPC watchdog query reports remaining budgets
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)


def function_body(source, name):
    start = source.index('{', source.index('\n' + name + '('))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


clock = (root / 'ntos/ke/clockint.c').read_text()
query = (root / 'ntos/ke/dpcobj.c').read_text()
fixture = r"""
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int NTSTATUS;
typedef struct {
    ULONG DpcTimeLimit, DpcTimeCount;
    LONG DpcWatchdogPeriod, DpcWatchdogCount;
    unsigned Number, DpcRoutineActive;
    void *CurrentThread, *IdleThread;
    struct { void *ActiveDpc; } DpcData[1];
} KPRCB, *PKPRCB;
typedef struct { ULONG DpcTimeLimit, DpcTimeCount, DpcWatchdogLimit, DpcWatchdogCount, Reserved; } KDPC_WATCHDOG_INFORMATION, *PKDPC_WATCHDOG_INFORMATION;
#define KiPrcbDpcTimeLimit(p) ((p)->DpcTimeLimit)
#define KiPrcbDpcTimeCount(p) ((p)->DpcTimeCount)
#define KiPrcbDpcWatchdogPeriod(p) ((p)->DpcWatchdogPeriod)
#define KiPrcbDpcWatchdogCount(p) ((p)->DpcWatchdogCount)
#define DISPATCH_LEVEL 2
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL -1
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef int BOOLEAN;
static int irql, masked, querying = 1;
static BOOLEAN KeDisableInterrupts(void) { int prior = masked; masked = 1; return prior; }
static void KeRestoreInterrupts(BOOLEAN prior) { masked = prior; }
static KPRCB prcb;
static int KeGetCurrentIrql(void) { return irql; }
static PKPRCB KeGetCurrentPrcb(void) { if (querying && !masked) abort(); return &prcb; }
typedef ULONG *PULONG;
typedef void *PVOID;
typedef struct interrupt {
    BOOLEAN (*ServiceRoutine)(struct interrupt *, void *);
    void *ServiceContext;
} *PKINTERRUPT;
#define MAXULONG UINT32_MAX
#define MAXLONG INT32_MAX
#define min(x, y) ((x) < (y) ? (x) : (y))
#define DPC_NORMAL 0
#define DPC_WATCHDOG_VIOLATION 0x133
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof(*(a)))
#define ASSERT(x) do { if (!(x)) abort(); } while (0)
static int KdEnteredDebugger, KeBugCheckActive, KiInterruptTimeFrozen;
static ULONG reports, args[4];
static PULONG volatile KiIsrWatchdogCount[1];
static void *InterlockedExchangePointer(void *volatile *p, void *value) {
    return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST);
}
static void KeBugCheckEx(ULONG code, ULONG a, ULONG b, ULONG c, ULONG d) {
    ASSERT(code == 0x133);
    ++reports;
    args[0] = a; args[1] = b; args[2] = c; args[3] = d;
}
static void charge(PKPRCB Prcb, ULONG Irql, ULONG Ticks) { @CHARGE@ }
static BOOLEAN invoke(PKINTERRUPT Interrupt) { @ISR@ }
static BOOLEAN nested(PKINTERRUPT irq, void *context) {
    (void)irq; (void)context;
    charge(&prcb, 5, 2);
    ASSERT(*KiIsrWatchdogCount[0] == 2 && prcb.DpcTimeCount == 3);
    return 1;
}
static BOOLEAN outer(PKINTERRUPT irq, void *context) {
    (void)irq;
    charge(&prcb, 4, 1);
    ASSERT(invoke(context));
    ASSERT(*KiIsrWatchdogCount[0] == 1);
    charge(&prcb, 4, 2);
    return 1;
}
static NTSTATUS query(PKDPC_WATCHDOG_INFORMATION WatchdogInformation) { @BODY@ }
int main(void) {
    KDPC_WATCHDOG_INFORMATION info = { 9, 9, 9, 9, 9 };
    irql = 0;
    CHECK(query(&info) == STATUS_UNSUCCESSFUL && info.DpcTimeLimit == 9);
    irql = DISPATCH_LEVEL;
    prcb = (KPRCB){ .DpcTimeLimit = 20, .DpcWatchdogPeriod = 120 };
    CHECK(query(&info) == STATUS_SUCCESS);
    CHECK(info.DpcTimeLimit == 20 && info.DpcTimeCount == 20);
    CHECK(info.DpcWatchdogLimit == 120 && info.DpcWatchdogCount == 120 && info.Reserved == 0);
    prcb.DpcTimeCount = 3; prcb.DpcWatchdogCount = 5;
    CHECK(query(&info) == STATUS_SUCCESS);
    CHECK(info.DpcTimeCount == 17 && info.DpcWatchdogCount == 115);
    prcb.DpcTimeCount = 21; prcb.DpcWatchdogCount = 121;
    CHECK(query(&info) == STATUS_SUCCESS);
    CHECK(info.DpcTimeCount == 0 && info.DpcWatchdogCount == 0);
    prcb = (KPRCB){ .DpcTimeCount = 3, .DpcWatchdogCount = 5 };
    CHECK(query(&info) == STATUS_SUCCESS);
    CHECK(info.DpcTimeCount == 0 && info.DpcWatchdogCount == 0);
    CHECK(!masked);
    querying = 0;
    prcb.CurrentThread = &prcb;
    prcb.IdleThread = &info;
    prcb.DpcTimeCount = 0;
    prcb.DpcWatchdogCount = 0;
    prcb.DpcTimeLimit = 5;
    prcb.DpcWatchdogPeriod = 12;
    prcb.DpcRoutineActive = 1;
    prcb.DpcData[0].ActiveDpc = &info;
    charge(&prcb, 2, 3);
    struct interrupt inner = { nested, NULL }, first = { outer, &inner };
    CHECK(invoke(&first) && KiIsrWatchdogCount[0] == NULL);
    CHECK(prcb.DpcWatchdogCount == 8 && prcb.DpcTimeCount == 3 && !reports);
    charge(&prcb, 2, 2);
    CHECK(reports == 1 && args[0] == 0 && args[1] == 5 && args[2] == 5 && !args[3]);
    prcb.DpcRoutineActive = 0;
    prcb.DpcData[0].ActiveDpc = NULL;
    charge(&prcb, 4, 2);
    CHECK(reports == 2 && args[0] == 1 && args[1] == 12 && !args[2] && !args[3]);
    charge(&prcb, 0, 1);
    CHECK(!prcb.DpcWatchdogCount);
    prcb.CurrentThread = prcb.IdleThread;
    charge(&prcb, DISPATCH_LEVEL, 1000);
    CHECK(reports == 2 && !prcb.DpcWatchdogCount);
    charge(&prcb, 5, 1);
    CHECK(prcb.DpcWatchdogCount == 1);
    KiInterruptTimeFrozen = 1;
    charge(&prcb, 5, 500);
    CHECK(prcb.DpcWatchdogCount == 1);
    KiInterruptTimeFrozen = 0;
    prcb.DpcWatchdogPeriod = 0;
    charge(&prcb, 5, UINT32_MAX);
    CHECK(prcb.DpcWatchdogCount == INT32_MAX && reports == 2);
    return 0;
}
""".replace('@BODY@', function_body(query, 'KeQueryDpcWatchdogInformation')).replace(
    '@CHARGE@', function_body(clock, 'KiChargeDpcWatchdog')).replace(
    '@ISR@', function_body(clock, 'KiInvokeDeviceIsr'))
with tempfile.TemporaryDirectory(prefix='ke-dpc-watchdog-query-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
