# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The experimental EEVDF scheduler picks the eligible entity with the earliest virtual deadline and serves each priority its weight
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)

header = (root / 'ntos/include/internal/eevdf.h').read_text()
source = (root / 'ntos/ke/eevdf.c').read_text()
types = header[header.index('typedef struct _KI_EEVDF_ENTITY'):header.index('/* Every thread object')]
defines = '\n'.join(re.findall(r'^#define KI_EEVDF_\w+ .*$', source, re.M))
core = source[source.index('/* VIRTUAL TIME ARITHMETIC'):source.index('/* GLOBALS')]

fixture = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOID void
typedef unsigned char UCHAR, BOOLEAN;
typedef int LONG;
typedef unsigned int ULONG;
typedef int64_t LONG64;
typedef uint64_t ULONG64, *PULONG64;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;

#define TRUE 1
#define FALSE 0
#define DBG 1
#define LOW_REALTIME_PRIORITY 16
#define DECLSPEC_CACHEALIGN
#define _In_
#define _In_opt_
#define _Inout_
#define _Out_opt_
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define CONTAINING_RECORD(Address, Type, Field) ((Type *)((char *)(Address) - offsetof(Type, Field)))
#define ASSERT(Expression) \
    do { if (!(Expression)) { printf("ASSERT(%s) at fixture line %d\n", #Expression, __LINE__); abort(); } } while (0)

static void InitializeListHead(PLIST_ENTRY Head) { Head->Flink = Head->Blink = Head; }
static BOOLEAN IsListEmpty(const LIST_ENTRY *Head) { return Head->Flink == Head; }
static void InsertHeadList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head->Flink;
    Entry->Blink = Head;
    Head->Flink->Blink = Entry;
    Head->Flink = Entry;
}
static BOOLEAN RemoveEntryList(PLIST_ENTRY Entry)
{
    Entry->Blink->Flink = Entry->Flink;
    Entry->Flink->Blink = Entry->Blink;
    return Entry->Flink == Entry->Blink;
}

@DEFINES@
@TYPES@
@CORE@

#define SLICE 3000ULL
#define TICK 15625ULL
#define ORIGIN 1000000000000ULL

static int Failures;

#define CHECK(Expression) \
    do { if (!(Expression)) { printf("FAIL line %d: %s\n", __LINE__, #Expression); Failures++; } } while (0)

static void Start(PKI_EEVDF_QUEUE Queue)
{
    memset(Queue, 0, sizeof(*Queue));
    InitializeListHead(&Queue->DeadlineList);
    Queue->BaseVruntime = ORIGIN;
}

static void Make(PKI_EEVDF_ENTITY Entity, UCHAR Priority, LONG64 Vruntime, LONG64 Deadline)
{
    memset(Entity, 0, sizeof(*Entity));
    Entity->Priority = Priority;
    Entity->Vruntime = ORIGIN + (ULONG64)Vruntime;
    Entity->Deadline = ORIGIN + (ULONG64)Deadline;
}

static LONG64 Abs(LONG64 Value) { return Value < 0 ? -Value : Value; }

/* The weighted average of the virtual runtimes, computed apart from the queue's own running sums */
static ULONG64 Average(PKI_EEVDF_ENTITY *Entities, ULONG Count)
{
    __int128 Sum = 0, Load = 0, Quotient;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Sum += (__int128)(LONG64)(Entities[i]->Vruntime - ORIGIN) * KiEevdfWeight(Entities[i]->Priority);
        Load += KiEevdfWeight(Entities[i]->Priority);
    }

    Quotient = Sum / Load;
    if (Sum % Load < 0)
        Quotient--;
    return ORIGIN + (ULONG64)(LONG64)Quotient;
}

static void TestWeights(void)
{
    UCHAR Priority;

    CHECK(!KiEevdfIsFairPriority(0));
    CHECK(KiEevdfIsFairPriority(1));
    CHECK(KiEevdfIsFairPriority(15));
    CHECK(!KiEevdfIsFairPriority(16));
    CHECK(!KiEevdfIsFairPriority(31));
    CHECK(KiEevdfWeight(1) == 1);
    CHECK(KiEevdfWeight(8) == 128);
    CHECK(KiEevdfWeight(15) == 16384);

    for (Priority = 1; Priority <= 15; Priority++)
    {
        CHECK((KiEevdfWeight(Priority) << KiEevdfShift(Priority)) == 16384);
        if (Priority > 1)
            CHECK(KiEevdfWeight(Priority) == 2 * KiEevdfWeight((UCHAR)(Priority - 1)));
    }
}

static void TestPick(void)
{
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY A, B, C, I, R;

    Start(&Queue);
    CHECK(KiEevdfPick(&Queue, NULL, NULL) == NULL);

    Make(&A, 8, 100, 1000);
    Make(&B, 8, -50, 2000);
    Make(&C, 8, 300, 500);
    KiEevdfInsert(&Queue, &A);
    KiEevdfInsert(&Queue, &B);
    KiEevdfInsert(&Queue, &C);
    KiEevdfAssertQueue(&Queue);

    CHECK(KiEevdfSettle(&Queue, NULL, NULL, NULL, NULL) == ORIGIN + 116);
    CHECK(KiEevdfIsEligible(&A, ORIGIN + 116));
    CHECK(KiEevdfIsEligible(&B, ORIGIN + 116));
    CHECK(!KiEevdfIsEligible(&C, ORIGIN + 116));

    /* C has the earliest deadline but has had more than its share; A is the earliest of the eligible */
    CHECK(KiEevdfPick(&Queue, NULL, NULL) == &A);

    /* An eligible incumbent keeps a tie and loses to a strictly earlier deadline */
    Make(&I, 8, 100, 1000);
    CHECK(KiEevdfPick(&Queue, &I, &I) == &I);
    Make(&I, 8, 100, 1001);
    CHECK(KiEevdfPick(&Queue, &I, &I) == &A);

    /* An incumbent ahead of the virtual time gives way to an eligible listed entity */
    Make(&I, 8, 5000, 10);
    CHECK(KiEevdfPick(&Queue, &I, &I) != &I);

    KiEevdfRemove(&Queue, &A);
    KiEevdfRemove(&Queue, &B);
    KiEevdfRemove(&Queue, &C);
    CHECK(Queue.Load == 0 && Queue.WeightedKeySum == 0 && Queue.Count == 0);

    /* Alone, the incumbent is the answer */
    CHECK(KiEevdfPick(&Queue, &I, &I) == &I);

    /* A running entity far behind is averaged in but is no candidate: the listed one is taken though it is not eligible */
    Start(&Queue);
    Make(&R, 15, -100000, 0);
    Make(&A, 1, 0, 100);
    KiEevdfInsert(&Queue, &A);
    CHECK(!KiEevdfIsEligible(&A, KiEevdfSettle(&Queue, &R, NULL, NULL, NULL)));
    CHECK(KiEevdfPick(&Queue, &R, NULL) == &A);
}

static void TestBeats(void)
{
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY Incumbent, Candidate, Listed;

    Start(&Queue);
    Make(&Incumbent, 8, 0, 1000);

    Make(&Candidate, 8, 0, 999);
    CHECK(KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));

    Make(&Candidate, 8, 0, 1000);
    CHECK(!KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));

    /* The candidate has the earlier deadline but is ahead of the virtual time */
    Make(&Candidate, 8, 500, 600);
    CHECK(!KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));

    /* An incumbent that is not eligible gives way to an eligible candidate whatever its deadline */
    Make(&Incumbent, 8, 500, 600);
    Make(&Candidate, 8, 0, 5000);
    CHECK(KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));

    /* A listed entity that would be picked first means the candidate is not the one to run */
    Make(&Incumbent, 8, 0, 1000);
    Make(&Candidate, 8, 0, 900);
    Make(&Listed, 8, 0, 800);
    KiEevdfInsert(&Queue, &Listed);
    CHECK(!KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));
    KiEevdfRemove(&Queue, &Listed);

    Make(&Listed, 8, 0, 950);
    KiEevdfInsert(&Queue, &Listed);
    CHECK(KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));

    /* A higher priority shortens the virtual length of the same request, so a woken thread boosted by two beats its peer */
    Start(&Queue);
    Make(&Incumbent, 8, 0, 0);
    Incumbent.Deadline = Incumbent.Vruntime + (SLICE << KiEevdfShift(8));
    memset(&Candidate, 0, sizeof(Candidate));
    KiEevdfPlace(&Queue, &Incumbent, &Candidate, 10, SLICE, 0);
    CHECK(KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));

    memset(&Candidate, 0, sizeof(Candidate));
    KiEevdfPlace(&Queue, &Incumbent, &Candidate, 8, SLICE, 0);
    CHECK(!KiEevdfBeats(&Queue, &Incumbent, &Candidate, &Incumbent));
}

static void TestPlacement(void)
{
    static const LONG64 Lags[] = {0, 1, 500, -700, 123456, -98765};
    static const UCHAR Priorities[] = {1, 6, 8, 10, 15};
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY A, B, Running, Entity;
    ULONG i, j;

    /* Alone on a timeline nothing is owed to anybody */
    Start(&Queue);
    memset(&Entity, 0, sizeof(Entity));
    Entity.Lag = 777;
    KiEevdfPlace(&Queue, NULL, &Entity, 8, SLICE, 0);
    CHECK(Entity.Vruntime == ORIGIN);
    CHECK(Entity.Deadline == ORIGIN + (SLICE << KiEevdfShift(8)));
    CHECK(Entity.Lag == 0);

    for (i = 0; i < sizeof(Lags) / sizeof(Lags[0]); i++)
    {
        for (j = 0; j < sizeof(Priorities) / sizeof(Priorities[0]); j++)
        {
            Start(&Queue);
            Make(&A, 8, 4000, 9000);
            Make(&B, 11, -2500, 3000);
            Make(&Running, 5, 70000, 90000);
            KiEevdfInsert(&Queue, &A);
            KiEevdfInsert(&Queue, &B);

            memset(&Entity, 0, sizeof(Entity));
            Entity.Lag = Lags[i];
            KiEevdfPlace(&Queue, &Running, &Entity, Priorities[j], SLICE, 0);
            CHECK(Entity.Deadline - Entity.Vruntime == (SLICE << KiEevdfShift(Priorities[j])));
            CHECK(!Entity.SliceEnded);

            /* What it is owed once it has joined is what it was owed when it left, whether it is listed or not */
            KiEevdfTakeLag(&Queue, &Running, &Entity, 1ULL << 40);
            CHECK(Abs(Entity.Lag - Lags[i]) <= 2);

            Entity.Lag = Lags[i];
            KiEevdfPlace(&Queue, &Running, &Entity, Priorities[j], SLICE, 0);
            KiEevdfInsert(&Queue, &Entity);
            KiEevdfAssertQueue(&Queue);
            KiEevdfRemove(&Queue, &Entity);
            KiEevdfTakeLag(&Queue, &Running, &Entity, 1ULL << 40);
            CHECK(Abs(Entity.Lag - Lags[i]) <= 2);
        }
    }

    /* What is owed across a wait is bounded both ways */
    Start(&Queue);
    Make(&A, 8, 0, 100);
    KiEevdfInsert(&Queue, &A);
    Make(&Entity, 8, -100000000, 0);
    KiEevdfTakeLag(&Queue, NULL, &Entity, 2 * SLICE);
    CHECK(Entity.Lag == (LONG64)(2 * SLICE));
    Make(&Entity, 8, 100000000, 0);
    KiEevdfTakeLag(&Queue, NULL, &Entity, 2 * SLICE);
    CHECK(Entity.Lag == -(LONG64)(2 * SLICE));
}

static LONG64 OwedAfter(LONG64 Lag, UCHAR Priority, ULONG64 Away)
{
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY Peer, Entity;

    Start(&Queue);
    Make(&Peer, 8, 0, 100);
    KiEevdfInsert(&Queue, &Peer);

    memset(&Entity, 0, sizeof(Entity));
    Entity.Lag = Lag;
    KiEevdfPlace(&Queue, NULL, &Entity, Priority, SLICE, Away);
    KiEevdfTakeLag(&Queue, NULL, &Entity, 1ULL << 40);
    return Entity.Lag;
}

/* Service taken ahead of the share is paid back while away, at the share waiting on the timeline would have earned */
static void TestRepayment(void)
{
    CHECK(Abs(OwedAfter(-1000, 8, 0) + 1000) <= 2);
    CHECK(Abs(OwedAfter(-1000, 8, 1000) + 500) <= 2);
    CHECK(Abs(OwedAfter(-1000, 8, 2000)) <= 2);
    CHECK(Abs(OwedAfter(-1000, 8, 1ULL << 62)) <= 2);
    CHECK(Abs(OwedAfter(-1000, 9, 1500)) <= 2);
    CHECK(Abs(OwedAfter(-1000, 9, 750) + 500) <= 2);

    /* Service that is owed is neither lost nor grown by staying away */
    CHECK(Abs(OwedAfter(1000, 8, 0) - 1000) <= 2);
    CHECK(Abs(OwedAfter(1000, 8, 1ULL << 62) - 1000) <= 2);
}

static void TestAdvance(void)
{
    KI_EEVDF_ENTITY Entity;

    Make(&Entity, 8, 0, 0);
    Entity.Deadline = Entity.Vruntime + (SLICE << KiEevdfShift(8));

    KiEevdfAdvance(&Entity, SLICE - 1, SLICE);
    CHECK(Entity.Vruntime == ORIGIN + ((SLICE - 1) << KiEevdfShift(8)));
    CHECK(!Entity.SliceEnded);
    CHECK(Entity.Deadline == ORIGIN + (SLICE << KiEevdfShift(8)));

    /* The request is served in full: the next one starts where this one ended */
    KiEevdfAdvance(&Entity, 1, SLICE);
    CHECK(Entity.SliceEnded);
    CHECK(Entity.Deadline == Entity.Vruntime + (SLICE << KiEevdfShift(8)));

    /* A cycle costs a priority 1 thread 2^14 units and a priority 15 thread one */
    Make(&Entity, 1, 0, 1 << 30);
    KiEevdfAdvance(&Entity, 3, SLICE);
    CHECK(Entity.Vruntime == ORIGIN + (3 << 14));
    Make(&Entity, 15, 0, 1 << 30);
    KiEevdfAdvance(&Entity, 3, SLICE);
    CHECK(Entity.Vruntime == ORIGIN + 3);

    /* Offering the turn puts the request one slice further away and charges nothing */
    Make(&Entity, 8, 10, 500);
    KiEevdfForfeit(&Entity, SLICE);
    CHECK(Entity.Vruntime == ORIGIN + 10);
    CHECK(Entity.Deadline == ORIGIN + 500 + (SLICE << KiEevdfShift(8)));
}

static void TestForfeit(void)
{
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY Yielder, Other;
    ULONG64 Request = SLICE << KiEevdfShift(8);
    ULONG i;

    /* Turns offered one after the other do not add up: the deadline stays within two requests of what was served */
    Make(&Yielder, 8, 0, 0);
    Yielder.Deadline = Yielder.Vruntime + Request;
    for (i = 0; i < 100000; i++)
        KiEevdfForfeit(&Yielder, SLICE);
    CHECK(Yielder.Vruntime == ORIGIN);
    CHECK(Yielder.Deadline == ORIGIN + 2 * Request);

    /* So the turn of a thread that offered it many times still ends, and the one waiting behind it is picked */
    Start(&Queue);
    Make(&Other, 8, 0, 0);
    Other.Deadline = Other.Vruntime + Request;
    KiEevdfInsert(&Queue, &Other);

    KiEevdfAdvance(&Yielder, 2 * SLICE, SLICE);
    CHECK(Yielder.SliceEnded);
    CHECK(KiEevdfPick(&Queue, &Yielder, &Yielder) == &Other);
}

static void TestReweight(void)
{
    static const UCHAR To[] = {1, 4, 9, 12, 15};
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY A, B, C;
    ULONG64 Before, After;
    LONG64 Owed, Left;
    ULONG i, Listed;

    for (Listed = 0; Listed < 2; Listed++)
    {
        for (i = 0; i < sizeof(To) / sizeof(To[0]); i++)
        {
            Start(&Queue);
            Make(&A, 8, 40960, 90000);
            Make(&B, 10, -8192, 30000);
            Make(&C, 6, 1 << 20, (1 << 20) + (2000 << KiEevdfShift(6)));
            KiEevdfInsert(&Queue, &A);
            KiEevdfInsert(&Queue, &B);
            if (Listed)
                KiEevdfInsert(&Queue, &C);

            Before = KiEevdfSettle(&Queue, &C, NULL, NULL, NULL);
            Owed = (LONG64)(Before - C.Vruntime) / (1LL << KiEevdfShift(6));
            Left = (LONG64)(C.Deadline - C.Vruntime) / (1LL << KiEevdfShift(6));

            KiEevdfReweight(&Queue, Listed ? NULL : &C, &C, To[i]);
            KiEevdfAssertQueue(&Queue);
            CHECK(C.Priority == To[i]);
            CHECK(C.Listed == (BOOLEAN)Listed);

            /* The others are owed what they were, so the virtual time stays where it was */
            After = KiEevdfSettle(&Queue, &C, NULL, NULL, NULL);
            CHECK(Abs((LONG64)(After - Before)) <= 1);

            /* In cycles, the entity is owed the same and has the same left of its request */
            CHECK(Abs((LONG64)(After - C.Vruntime) / (1LL << KiEevdfShift(To[i])) - Owed) <= 1);
            CHECK(Abs((LONG64)(C.Deadline - C.Vruntime) / (1LL << KiEevdfShift(To[i])) - Left) <= 1);
        }
    }
}

/* One processor and a clock: the running entity is charged a tick, then whoever is picked runs; a request is one tick long, as the kernel makes it */
static void Simulate(const UCHAR *Priorities, ULONG Count, ULONG Ticks)
{
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY Entities[8];
    PKI_EEVDF_ENTITY Running, Next;
    ULONG64 Service[8] = {0};
    LONG64 Load = 0, Worst = 0, Lag;
    ULONG i, Tick;

    Start(&Queue);
    for (i = 0; i < Count; i++)
    {
        memset(&Entities[i], 0, sizeof(Entities[i]));
        KiEevdfPlace(&Queue, NULL, &Entities[i], Priorities[i], TICK, 0);
        KiEevdfInsert(&Queue, &Entities[i]);
        Load += KiEevdfWeight(Priorities[i]);
    }

    Running = KiEevdfPick(&Queue, NULL, NULL);
    KiEevdfRemove(&Queue, Running);

    for (Tick = 1; Tick <= Ticks; Tick++)
    {
        KiEevdfAdvance(Running, TICK, TICK);
        Service[Running - Entities] += TICK;

        Next = KiEevdfPick(&Queue, Running, Running);
        if (Next != Running)
        {
            KiEevdfInsert(&Queue, Running);
            KiEevdfRemove(&Queue, Next);
            Running = Next;
        }
        KiEevdfAssertQueue(&Queue);

        /* Nobody is ever further from its share than one tick of the processor */
        for (i = 0; i < Count; i++)
        {
            Lag = (LONG64)(((__int128)Tick * TICK * KiEevdfWeight(Priorities[i])) / Load) - (LONG64)Service[i];
            if (Abs(Lag) > Worst)
                Worst = Abs(Lag);
        }
    }

    if (Worst > (LONG64)TICK)
    {
        printf("FAIL: %lu entities were up to %lld cycles from their share, more than a tick of %llu\n",
               (unsigned long)Count, (long long)Worst, (unsigned long long)TICK);
        Failures++;
    }
}

static void TestShares(void)
{
    static const UCHAR Equal[] = {8, 8, 8};
    static const UCHAR Step[] = {8, 9};
    static const UCHAR Classes[] = {4, 6, 8, 10, 13};
    static const UCHAR Extremes[] = {1, 15};
    static const UCHAR Crowd[] = {8, 8, 8, 8, 8, 8, 8, 9};

    Simulate(Equal, 3, 30000);
    Simulate(Step, 2, 30000);
    Simulate(Classes, 5, 200000);
    Simulate(Extremes, 2, 200000);
    Simulate(Crowd, 8, 100000);
}

static ULONG Seed = 12345;
static ULONG Random(ULONG Range)
{
    Seed = Seed * 1103515245 + 12345;
    return (Seed >> 8) % Range;
}

/* Whatever is done to a timeline, its running sums give the weighted average of what is on it */
static void TestBookkeeping(void)
{
    KI_EEVDF_QUEUE Queue;
    KI_EEVDF_ENTITY Entities[24];
    PKI_EEVDF_ENTITY Listed[24];
    ULONG i, Step, Count;

    Start(&Queue);
    memset(Entities, 0, sizeof(Entities));

    for (Step = 0; Step < 200000; Step++)
    {
        PKI_EEVDF_ENTITY Entity = &Entities[Random(24)];

        switch (Random(5))
        {
            case 0:
                if (!Entity->Listed)
                {
                    Entity->Lag = (LONG64)Random(20000) - 10000;
                    KiEevdfPlace(&Queue, NULL, Entity, (UCHAR)(1 + Random(15)), SLICE, Random(5000));
                    KiEevdfInsert(&Queue, Entity);
                }
                break;
            case 1:
                if (Entity->Listed)
                    KiEevdfRemove(&Queue, Entity);
                break;
            case 2:
                if (Entity->Listed)
                {
                    KiEevdfRemove(&Queue, Entity);
                    KiEevdfAdvance(Entity, Random(40000), SLICE);
                    KiEevdfInsert(&Queue, Entity);
                }
                break;
            case 3:
                if (Entity->Listed)
                    KiEevdfReweight(&Queue, NULL, Entity, (UCHAR)(1 + Random(15)));
                break;
            default:
                if (Entity->Listed)
                {
                    KiEevdfRemove(&Queue, Entity);
                    KiEevdfTakeLag(&Queue, NULL, Entity, 2 * SLICE);
                    CHECK(Abs(Entity->Lag) <= (LONG64)(2 * SLICE));
                }
                break;
        }

        KiEevdfAssertQueue(&Queue);

        Count = 0;
        for (i = 0; i < 24; i++)
        {
            if (Entities[i].Listed)
                Listed[Count++] = &Entities[i];
        }

        CHECK(Count == Queue.Count);
        if (Count != 0)
            CHECK(KiEevdfSettle(&Queue, NULL, NULL, NULL, NULL) == Average(Listed, Count));
        if (Failures)
            return;
    }
}

int main(void)
{
    TestWeights();
    TestPick();
    TestBeats();
    TestPlacement();
    TestRepayment();
    TestAdvance();
    TestForfeit();
    TestReweight();
    TestShares();
    TestBookkeeping();

    if (!Failures)
        printf("eevdf: eligibility, deadlines, placement, repayment, reweighting and shares hold\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    text = fixture.replace('@DEFINES@', defines).replace('@TYPES@', types).replace('@CORE@', core)
    (path / 'eevdf.c').write_text(text)
    subprocess.run([args.cc, '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror', str(path / 'eevdf.c'), '-o', str(path / 'eevdf')], check=True)
    subprocess.run([str(path / 'eevdf')], check=True)
