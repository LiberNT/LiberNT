/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Push lock exclusion, wake-ups and spin hand-off under concurrent load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <errno.h>
#include <sched.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pushlock/ntoskrnl.h"

#define PL_MAX_THREADS 16

unsigned long PushLockAsserts;
unsigned long GateWaits;
ULONG KeNumberProcessors;
extern ULONG ExPushLockSpinCount;
static unsigned long Checks, Failures;

#define CHECK(c) do { __atomic_fetch_add(&Checks, 1, __ATOMIC_RELAXED); if (!(c)) { __atomic_fetch_add(&Failures, 1, __ATOMIC_RELAXED); fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint64_t NowNs(void)
{
    struct timespec Time;

    clock_gettime(CLOCK_MONOTONIC, &Time);
    return (uint64_t)Time.tv_sec * 1000000000ull + (uint64_t)Time.tv_nsec;
}

static void WaitSignaled(HOST_WAITABLE *Waitable)
{
    pthread_mutex_lock(&Waitable->Mutex);
    while (!Waitable->Signaled)
        pthread_cond_wait(&Waitable->Cond, &Waitable->Mutex);
    Waitable->Signaled = 0;
    pthread_mutex_unlock(&Waitable->Mutex);
}

static void Signal(HOST_WAITABLE *Waitable)
{
    pthread_mutex_lock(&Waitable->Mutex);
    Waitable->Signaled = 1;
    pthread_cond_signal(&Waitable->Cond);
    pthread_mutex_unlock(&Waitable->Mutex);
}

void KeWaitForGate(PKGATE Gate, int WaitReason, int WaitMode)
{
    (void)WaitReason;
    (void)WaitMode;
    __atomic_fetch_add(&GateWaits, 1, __ATOMIC_RELAXED);
    WaitSignaled(Gate);
}

void KeSignalGateBoostPriority(PKGATE Gate)
{
    Signal(Gate);
}

void KeSetEventBoostPriority(PKEVENT Event, PVOID Thread)
{
    (void)Thread;
    Signal(Event);
}

NTSTATUS KeWaitForSingleObject(PVOID Object, int WaitReason, int WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
    HOST_WAITABLE *Waitable = Object;
    NTSTATUS Status = STATUS_SUCCESS;

    (void)WaitReason;
    (void)WaitMode;
    (void)Alertable;
    pthread_mutex_lock(&Waitable->Mutex);
    if (Timeout == NULL)
    {
        while (!Waitable->Signaled)
            pthread_cond_wait(&Waitable->Cond, &Waitable->Mutex);
    }
    else
    {
        struct timespec Deadline;
        int64_t Hundreds = Timeout->QuadPart < 0 ? -Timeout->QuadPart : 0;

        clock_gettime(CLOCK_REALTIME, &Deadline);
        Deadline.tv_sec += (time_t)(Hundreds / 10000000);
        Deadline.tv_nsec += (long)((Hundreds % 10000000) * 100);
        if (Deadline.tv_nsec >= 1000000000)
        {
            Deadline.tv_sec++;
            Deadline.tv_nsec -= 1000000000;
        }
        while (!Waitable->Signaled)
        {
            if (pthread_cond_timedwait(&Waitable->Cond, &Waitable->Mutex, &Deadline) == ETIMEDOUT)
            {
                Status = STATUS_TIMEOUT;
                break;
            }
        }
    }
    if (Status == STATUS_SUCCESS)
        Waitable->Signaled = 0;
    pthread_mutex_unlock(&Waitable->Mutex);
    return Status;
}

enum
{
    RoleExclusive,
    RoleShared,
    RoleTryShared,
};

typedef struct _PL_SHARED
{
    __attribute__((aligned(128))) EX_PUSH_LOCK Lock;
    __attribute__((aligned(128))) volatile LONG ReadersInside;
    volatile LONG WritersInside;
    volatile uint64_t A;
    volatile uint64_t B;
    volatile int Stop;
    volatile int Go;
} PL_SHARED;

typedef struct __attribute__((aligned(128))) _PL_WORKER
{
    PL_SHARED *Shared;
    int Role;
    uint64_t Ops;
} PL_WORKER;

static void ReaderSection(PL_SHARED *Shared)
{
    uint64_t A, B;

    __atomic_fetch_add(&Shared->ReadersInside, 1, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&Shared->WritersInside, __ATOMIC_SEQ_CST) == 0);
    A = Shared->A;
    B = Shared->B;
    CHECK(A == B);
    __atomic_fetch_sub(&Shared->ReadersInside, 1, __ATOMIC_SEQ_CST);
}

static void WriterSection(PL_SHARED *Shared)
{
    CHECK(__atomic_fetch_add(&Shared->WritersInside, 1, __ATOMIC_SEQ_CST) == 0);
    CHECK(__atomic_load_n(&Shared->ReadersInside, __ATOMIC_SEQ_CST) == 0);
    Shared->A = Shared->A + 1;
    Shared->B = Shared->B + 1;
    __atomic_fetch_sub(&Shared->WritersInside, 1, __ATOMIC_SEQ_CST);
}

static void *Worker(void *Argument)
{
    PL_WORKER *Worker = Argument;
    PL_SHARED *Shared = Worker->Shared;

    while (!__atomic_load_n(&Shared->Go, __ATOMIC_ACQUIRE))
        sched_yield();

    while (!__atomic_load_n(&Shared->Stop, __ATOMIC_ACQUIRE))
    {
        switch (Worker->Role)
        {
            case RoleExclusive:
                ExAcquirePushLockExclusive(&Shared->Lock);
                WriterSection(Shared);
                ExReleasePushLockExclusive(&Shared->Lock);
                break;
            case RoleShared:
                ExAcquirePushLockShared(&Shared->Lock);
                ReaderSection(Shared);
                ExReleasePushLockShared(&Shared->Lock);
                break;
            case RoleTryShared:
                if (ExfTryAcquirePushLockShared(&Shared->Lock))
                {
                    ReaderSection(Shared);
                    ExfReleasePushLock(&Shared->Lock);
                }
                break;
        }
        Worker->Ops++;
    }
    return NULL;
}

static void RunMix(const char *Name, const int *Roles, int Count, double Seconds)
{
    static PL_SHARED Shared;
    PL_WORKER Workers[PL_MAX_THREADS];
    pthread_t Threads[PL_MAX_THREADS];
    unsigned long Waits = __atomic_load_n(&GateWaits, __ATOMIC_RELAXED);
    uint64_t Total = 0;
    int i;

    memset(&Shared, 0, sizeof(Shared));
    ExInitializePushLock((PULONG_PTR)&Shared.Lock);
    for (i = 0; i < Count; i++)
    {
        Workers[i] = (PL_WORKER){ &Shared, Roles[i], 0 };
        if (pthread_create(&Threads[i], NULL, Worker, &Workers[i]) != 0)
            abort();
    }
    __atomic_store_n(&Shared.Go, 1, __ATOMIC_RELEASE);
    usleep((useconds_t)(Seconds * 1e6));
    __atomic_store_n(&Shared.Stop, 1, __ATOMIC_RELEASE);
    for (i = 0; i < Count; i++)
    {
        pthread_join(Threads[i], NULL);
        Total += Workers[i].Ops;
        CHECK(Workers[i].Ops != 0 || Roles[i] == RoleTryShared);
    }

    CHECK(Shared.ReadersInside == 0 && Shared.WritersInside == 0);
    CHECK(Shared.A == Shared.B);
    CHECK(Shared.Lock.Value == 0);
    Waits = __atomic_load_n(&GateWaits, __ATOMIC_RELAXED) - Waits;
    printf("  %-28s %10.1f Mops/s total (%lu sleeps)\n", Name, Total / Seconds / 1e6, Waits);
}

typedef struct _HANDOFF
{
    EX_PUSH_LOCK Lock;
    volatile int Spinning;
    volatile uint64_t ReleasedAt;
    volatile uint64_t AcquiredAt;
} HANDOFF;

static void *HandoffWaiter(void *Argument)
{
    HANDOFF *Handoff = Argument;

    __atomic_store_n(&Handoff->Spinning, 1, __ATOMIC_RELEASE);
    ExAcquirePushLockExclusive(&Handoff->Lock);
    __atomic_store_n(&Handoff->AcquiredAt, NowNs(), __ATOMIC_RELEASE);
    ExReleasePushLockExclusive(&Handoff->Lock);
    return NULL;
}

static void TestSpinSeesRelease(void)
{
    static HANDOFF Handoff;
    unsigned long Waits = __atomic_load_n(&GateWaits, __ATOMIC_RELAXED);
    ULONG SpinCount = ExPushLockSpinCount;
    pthread_t Thread;
    uint64_t Latency;

    memset(&Handoff, 0, sizeof(Handoff));
    ExPushLockSpinCount = 2000000000u;
    ExAcquirePushLockExclusive(&Handoff.Lock);
    if (pthread_create(&Thread, NULL, HandoffWaiter, &Handoff) != 0)
        abort();
    while (!__atomic_load_n(&Handoff.Spinning, __ATOMIC_ACQUIRE))
        sched_yield();
    usleep(20000);
    __atomic_store_n(&Handoff.ReleasedAt, NowNs(), __ATOMIC_RELEASE);
    ExReleasePushLockExclusive(&Handoff.Lock);
    pthread_join(Thread, NULL);
    ExPushLockSpinCount = SpinCount;

    Latency = Handoff.AcquiredAt - Handoff.ReleasedAt;
    CHECK(Handoff.AcquiredAt >= Handoff.ReleasedAt);
    CHECK(Latency < 100000000ull);
    CHECK(__atomic_load_n(&GateWaits, __ATOMIC_RELAXED) == Waits);
    CHECK(Handoff.Lock.Value == 0);
    printf("  spinning waiter took the released lock after %.3f ms\n", Latency / 1e6);
}

int main(int argc, char **argv)
{
    static const int Exclusive4[] = { RoleExclusive, RoleExclusive, RoleExclusive, RoleExclusive };
    static const int Shared4[] = { RoleShared, RoleShared, RoleShared, RoleShared };
    static const int Mixed[] = { RoleShared, RoleShared, RoleShared, RoleExclusive };
    static const int MixedHeavy[] = { RoleShared, RoleExclusive, RoleShared, RoleExclusive, RoleShared, RoleShared };
    static const int Tries[] = { RoleShared, RoleTryShared, RoleExclusive, RoleTryShared, RoleShared };
    static const int Mixed8[] = { RoleShared, RoleShared, RoleShared, RoleShared,
                                  RoleShared, RoleExclusive, RoleTryShared, RoleExclusive };
    double Seconds = argc > 1 ? atof(argv[1]) : 0.3;

    KeNumberProcessors = (ULONG)sysconf(_SC_NPROCESSORS_ONLN);
    ExpInitializePushLocks();

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("push lock host test (%u CPUs, spin %u, %.2f s per mix)\n", KeNumberProcessors, ExPushLockSpinCount, Seconds);
    TestSpinSeesRelease();
    RunMix("4 exclusive", Exclusive4, 4, Seconds);
    RunMix("4 shared", Shared4, 4, Seconds);
    RunMix("3 shared + exclusive", Mixed, 4, Seconds);
    RunMix("4 shared + 2 exclusive", MixedHeavy, 6, Seconds);
    RunMix("try shared", Tries, 5, Seconds);
    RunMix("5 shared + 2 exclusive + try", Mixed8, 8, Seconds);

    CHECK(PushLockAsserts == 0);
    printf("pushlock: %lu checks, %lu failures\n", Checks, Failures);
    return Failures != 0;
}
