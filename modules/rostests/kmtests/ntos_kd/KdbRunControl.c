/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Lets the debugger drive the machine with breakpoint commands, display, g, gu, p and watches
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define KDB_TEST_TRACE_MAX 16

/*
 * The debugger's start-up file sets the breakpoints at the first stop. Every
 * command list counts its hits here with eq; the stops that have no list of
 * their own are recorded by display, which writes where the machine stopped.
 */
ULONG64 KdbTestTrace[KDB_TEST_TRACE_MAX];
ULONG64 KdbTestTraceCount;
ULONG64 KdbTestLeafHits;
ULONG64 KdbTestOnceHits;
ULONG64 KdbTestIgnoredHits;
ULONG64 KdbTestCondHits;
ULONG64 KdbTestCondFlag;
ULONG64 KdbTestWatchHits;
ULONG64 KdbTestExecHits;
volatile ULONG64 KdbTestWatched;
PVOID KdbTestGuReturn;

static volatile LONG KdbTestWork[8];

DECLSPEC_NOINLINE VOID NTAPI KdbTestLeaf(VOID) { InterlockedIncrement(&KdbTestWork[0]); }
DECLSPEC_NOINLINE VOID NTAPI KdbTestOnce(VOID) { InterlockedIncrement(&KdbTestWork[1]); }
DECLSPEC_NOINLINE VOID NTAPI KdbTestIgnored(VOID) { InterlockedIncrement(&KdbTestWork[2]); }
DECLSPEC_NOINLINE VOID NTAPI KdbTestConditional(VOID) { InterlockedIncrement(&KdbTestWork[3]); }
DECLSPEC_NOINLINE VOID NTAPI KdbTestExecWatched(VOID) { InterlockedIncrement(&KdbTestWork[4]); }
DECLSPEC_NOINLINE VOID NTAPI KdbTestGDestination(VOID) { InterlockedIncrement(&KdbTestWork[5]); }
DECLSPEC_NOINLINE VOID NTAPI KdbTestGTarget(VOID) { InterlockedIncrement(&KdbTestWork[6]); }

/* Cleans up after the test: its start-up file gives every breakpoint and display up here */
DECLSPEC_NOINLINE VOID NTAPI KdbTestFinish(VOID) { InterlockedIncrement(&KdbTestWork[7]); }

DECLSPEC_NOINLINE
VOID
NTAPI
KdbTestGuTarget(VOID)
{
    KdbTestGuReturn = _ReturnAddress();
}

DECLSPEC_NOINLINE
VOID
NTAPI
KdbTestGuCaller(VOID)
{
    KdbTestGuTarget();
    InterlockedIncrement(&KdbTestWork[0]);
}

DECLSPEC_NOINLINE
VOID
NTAPI
KdbTestGCaller(VOID)
{
    KdbTestGTarget();
    KdbTestGDestination();
}

/* p 3 from the start of this function stops three instructions into it */
DECLSPEC_NOINLINE
VOID
NTAPI
KdbTestStepHere(VOID)
{
    KdbTestWork[1] += 1;
    KdbTestWork[2] += 2;
    KdbTestWork[3] += 3;
}

START_TEST(KdbRunControl)
{
    ULONG64 Value;
    ULONG Index;

    RtlZeroMemory(KdbTestTrace, sizeof(KdbTestTrace));
    KdbTestTraceCount = 0;
    KdbTestLeafHits = 0;
    KdbTestOnceHits = 0;
    KdbTestIgnoredHits = 0;
    KdbTestCondHits = 0;
    KdbTestCondFlag = 0;
    KdbTestWatchHits = 0;
    KdbTestExecHits = 0;
    KdbTestGuReturn = NULL;

    /* The debugger's start-up file sets everything up here */
    DbgBreakPoint();

    for (Index = 0; Index < 3; Index++)
        KdbTestLeaf();

    KdbTestOnce();
    KdbTestOnce();

    for (Index = 0; Index < 4; Index++)
        KdbTestIgnored();

    KdbTestConditional();
    KdbTestCondFlag = 1;
    KdbTestConditional();

    KdbTestGuCaller();
    KdbTestGCaller();

    KdbTestWatched = 1;
    KdbTestWatched = 2;
    KdbTestExecWatched();
    KdbTestExecWatched();

    KdbTestStepHere();
    KdbTestFinish();

    Value = KdbTestLeafHits;
    ok_eq_ulonglong(Value, 3ULL);
    Value = KdbTestOnceHits;
    ok_eq_ulonglong(Value, 1ULL);
    Value = KdbTestIgnoredHits;
    ok_eq_ulonglong(Value, 2ULL);
    Value = KdbTestCondHits;
    ok_eq_ulonglong(Value, 1ULL);

#if defined(_M_AMD64) || defined(_M_IX86)
    Value = KdbTestWatchHits;
    ok_eq_ulonglong(Value, 2ULL);
    Value = KdbTestExecHits;
    ok_eq_ulonglong(Value, 2ULL);
#endif

    /* display recorded the stops that had no commands of their own: gu, g <address> and p 3 */
    Value = KdbTestTraceCount;
    ok_eq_ulonglong(Value, 3ULL);
    ok(KdbTestTrace[0] == (ULONG64)(ULONG_PTR)KdbTestGuReturn,
       "gu stopped at %I64x, not at the return address %p\n", KdbTestTrace[0], KdbTestGuReturn);
    ok(KdbTestTrace[1] == (ULONG64)(ULONG_PTR)KdbTestGDestination,
       "g stopped at %I64x, not at %p\n", KdbTestTrace[1], (PVOID)KdbTestGDestination);
    ok((KdbTestTrace[2] > (ULONG64)(ULONG_PTR)KdbTestStepHere) &&
       (KdbTestTrace[2] < (ULONG64)(ULONG_PTR)KdbTestStepHere + 0x40),
       "p 3 stopped at %I64x, not a few instructions into %p\n", KdbTestTrace[2], (PVOID)KdbTestStepHere);
}
