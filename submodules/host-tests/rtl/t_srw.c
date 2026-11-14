/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Slim reader/writer lock exclusion and progress under concurrent load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "srw/rtl_vista.h"

#define SRW_MAX_THREADS 64

unsigned long SrwAsserts;
static unsigned long Checks, Failures;

#define CHECK(c) do { __atomic_fetch_add(&Checks, 1, __ATOMIC_RELAXED); if (!(c)) { __atomic_fetch_add(&Failures, 1, __ATOMIC_RELAXED); fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef struct _HOST_ALERT
{
    pthread_mutex_t Mutex;
    pthread_cond_t Cond;
    int Alerted;
} HOST_ALERT;

static HOST_ALERT Alerts[SRW_MAX_THREADS + 1];
static __thread HOST_TEB ThreadTeb;
static HOST_PEB Peb;
static volatile LONG NextThreadId;

HOST_TEB *NtCurrentTeb(void)
{
    if (ThreadTeb.ClientId.UniqueThread == NULL)
    {
        LONG Id = __atomic_add_fetch(&NextThreadId, 1, __ATOMIC_SEQ_CST);

        if (Id > SRW_MAX_THREADS)
            abort();
        ThreadTeb.ClientId.UniqueThread = (HANDLE)(ULONG_PTR)(Id * 4);
    }
    return &ThreadTeb;
}

HOST_PEB *NtCurrentPeb(void)
{
    return &Peb;
}

NTSTATUS NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER Interval)
{
    int64_t Hundreds = Interval->QuadPart < 0 ? -Interval->QuadPart : 0;
    struct timespec Sleep = { (time_t)(Hundreds / 10000000), (long)((Hundreds % 10000000) * 100) };

    (void)Alertable;
    nanosleep(&Sleep, NULL);
    return 0;
}

static HOST_ALERT *AlertFor(HANDLE ThreadId)
{
    ULONG_PTR Id = (ULONG_PTR)ThreadId / 4;

    if (Id == 0 || Id > SRW_MAX_THREADS)
        abort();
    return &Alerts[Id];
}

NTSTATUS NtWaitForAlertByThreadId(PVOID Address, PLARGE_INTEGER Timeout)
{
    HOST_ALERT *Alert = AlertFor(NtCurrentTeb()->ClientId.UniqueThread);

    (void)Address;
    (void)Timeout;
    pthread_mutex_lock(&Alert->Mutex);
    while (!Alert->Alerted)
        pthread_cond_wait(&Alert->Cond, &Alert->Mutex);
    Alert->Alerted = 0;
    pthread_mutex_unlock(&Alert->Mutex);
    return 0;
}

NTSTATUS NtAlertThreadByThreadId(HANDLE ThreadId)
{
    HOST_ALERT *Alert = AlertFor(ThreadId);

    pthread_mutex_lock(&Alert->Mutex);
    Alert->Alerted = 1;
    pthread_cond_signal(&Alert->Cond);
    pthread_mutex_unlock(&Alert->Mutex);
    return 0;
}

void RtlRaiseStatus(NTSTATUS Status)
{
    fprintf(stderr, "RtlRaiseStatus 0x%x\n", (unsigned)Status);
    abort();
}

enum
{
    RoleReader,
    RoleWriter,
    RoleTryReader,
    RoleTryWriter,
    RoleRecursiveReader,
    RoleBenchReader,
    RoleBenchWriter,
};

typedef struct _SRW_SHARED
{
    __attribute__((aligned(128))) RTL_SRWLOCK Lock;
    __attribute__((aligned(128))) volatile LONG ReadersInside;
    volatile LONG WritersInside;
    volatile uint64_t A;
    volatile uint64_t B;
    volatile int Stop;
    volatile int Go;
} SRW_SHARED;

typedef struct __attribute__((aligned(128))) _SRW_WORKER
{
    SRW_SHARED *Shared;
    int Role;
    uint64_t Ops;
    uint64_t Sink;
} SRW_WORKER;

static void ReaderSection(SRW_SHARED *Shared)
{
    uint64_t A, B;

    __atomic_fetch_add(&Shared->ReadersInside, 1, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&Shared->WritersInside, __ATOMIC_SEQ_CST) == 0);
    A = Shared->A;
    B = Shared->B;
    CHECK(A == B);
    __atomic_fetch_sub(&Shared->ReadersInside, 1, __ATOMIC_SEQ_CST);
}

static void WriterSection(SRW_SHARED *Shared)
{
    CHECK(__atomic_fetch_add(&Shared->WritersInside, 1, __ATOMIC_SEQ_CST) == 0);
    CHECK(__atomic_load_n(&Shared->ReadersInside, __ATOMIC_SEQ_CST) == 0);
    Shared->A = Shared->A + 1;
    Shared->B = Shared->B + 1;
    __atomic_fetch_sub(&Shared->WritersInside, 1, __ATOMIC_SEQ_CST);
}

static void *Worker(void *Argument)
{
    SRW_WORKER *Worker = Argument;
    SRW_SHARED *Shared = Worker->Shared;

    NtCurrentTeb();
    while (!__atomic_load_n(&Shared->Go, __ATOMIC_ACQUIRE))
        sched_yield();

    while (!__atomic_load_n(&Shared->Stop, __ATOMIC_ACQUIRE))
    {
        switch (Worker->Role)
        {
            case RoleReader:
                RtlAcquireSRWLockShared(&Shared->Lock);
                ReaderSection(Shared);
                RtlReleaseSRWLockShared(&Shared->Lock);
                break;
            case RoleWriter:
                RtlAcquireSRWLockExclusive(&Shared->Lock);
                WriterSection(Shared);
                RtlReleaseSRWLockExclusive(&Shared->Lock);
                break;
            case RoleTryReader:
                if (RtlTryAcquireSRWLockShared(&Shared->Lock))
                {
                    ReaderSection(Shared);
                    RtlReleaseSRWLockShared(&Shared->Lock);
                }
                break;
            case RoleTryWriter:
                if (RtlTryAcquireSRWLockExclusive(&Shared->Lock))
                {
                    WriterSection(Shared);
                    RtlReleaseSRWLockExclusive(&Shared->Lock);
                }
                break;
            case RoleBenchReader:
                RtlAcquireSRWLockShared(&Shared->Lock);
                Worker->Sink += Shared->A;
                RtlReleaseSRWLockShared(&Shared->Lock);
                break;
            case RoleBenchWriter:
                RtlAcquireSRWLockExclusive(&Shared->Lock);
                Shared->A = Shared->A + 1;
                Shared->B = Shared->B + 1;
                RtlReleaseSRWLockExclusive(&Shared->Lock);
                break;
            case RoleRecursiveReader:
                RtlAcquireSRWLockShared(&Shared->Lock);
                ReaderSection(Shared);
                RtlAcquireSRWLockShared(&Shared->Lock);
                ReaderSection(Shared);
                RtlReleaseSRWLockShared(&Shared->Lock);
                ReaderSection(Shared);
                RtlReleaseSRWLockShared(&Shared->Lock);
                break;
        }
        Worker->Ops++;
    }
    return NULL;
}

static uint64_t RunMix(const char *Name, const int *Roles, int Count, double Seconds)
{
    static SRW_SHARED Shared;
    SRW_WORKER Workers[SRW_MAX_THREADS];
    pthread_t Threads[SRW_MAX_THREADS];
    uint64_t Total = 0;
    int i;

    memset(&Shared, 0, sizeof(Shared));
    RtlInitializeSRWLock(&Shared.Lock);
    for (i = 0; i < Count; i++)
    {
        Workers[i] = (SRW_WORKER){ &Shared, Roles[i], 0, 0 };
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
        CHECK(Workers[i].Ops != 0 || Roles[i] == RoleTryWriter || Roles[i] == RoleTryReader);
    }

    CHECK(Shared.ReadersInside == 0 && Shared.WritersInside == 0);
    CHECK(Shared.A == Shared.B);
    CHECK(RtlTryAcquireSRWLockExclusive(&Shared.Lock));
    RtlReleaseSRWLockExclusive(&Shared.Lock);
    CHECK(Shared.Lock.Ptr == NULL);
    printf("  %-28s %10.1f Mops/s total (writes %llu)\n", Name, Total / Seconds / 1e6, (unsigned long long)Shared.A);
    return Total;
}

int main(int argc, char **argv)
{
    static const int Readers4[] = { RoleReader, RoleReader, RoleReader, RoleReader };
    static const int Bench1[] = { RoleBenchReader };
    static const int BenchWriter1[] = { RoleBenchWriter };
    static const int BenchReaders4[] = { RoleBenchReader, RoleBenchReader, RoleBenchReader, RoleBenchReader };
    static const int BenchWriters4[] = { RoleBenchWriter, RoleBenchWriter, RoleBenchWriter, RoleBenchWriter };
    static const int BenchMixed[] = { RoleBenchReader, RoleBenchReader, RoleBenchReader, RoleBenchWriter };
    static const int Writers4[] = { RoleWriter, RoleWriter, RoleWriter, RoleWriter };
    static const int Mixed[] = { RoleReader, RoleReader, RoleReader, RoleWriter };
    static const int MixedHeavy[] = { RoleReader, RoleWriter, RoleReader, RoleWriter, RoleReader, RoleReader };
    static const int Tries[] = { RoleReader, RoleTryWriter, RoleTryReader, RoleWriter, RoleReader };
    static const int Recursive[] = { RoleRecursiveReader, RoleRecursiveReader, RoleReader, RoleTryWriter };
    static const int Readers8[] = { RoleReader, RoleReader, RoleReader, RoleReader,
                                    RoleReader, RoleReader, RoleReader, RoleWriter };
    double Seconds = argc > 1 ? atof(argv[1]) : 0.3;
    int i;

    Peb.NumberOfProcessors = (ULONG)sysconf(_SC_NPROCESSORS_ONLN);
    for (i = 0; i <= SRW_MAX_THREADS; i++)
    {
        pthread_mutex_init(&Alerts[i].Mutex, NULL);
        pthread_cond_init(&Alerts[i].Cond, NULL);
    }
    NtCurrentTeb();

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("srw host test (%u CPUs, %.2f s per mix)\n", Peb.NumberOfProcessors, Seconds);
    RunMix("4 readers", Readers4, 4, Seconds);
    RunMix("4 writers", Writers4, 4, Seconds);
    RunMix("3 readers + writer", Mixed, 4, Seconds);
    RunMix("4 readers + 2 writers", MixedHeavy, 6, Seconds);
    RunMix("try variants", Tries, 5, Seconds);
    RunMix("recursive readers + try", Recursive, 4, Seconds);
    RunMix("7 readers + writer", Readers8, 8, Seconds);
    RunMix("4 readers again", Readers4, 4, Seconds);
    RunMix("bench 1 reader", Bench1, 1, Seconds);
    RunMix("bench 1 writer", BenchWriter1, 1, Seconds);
    RunMix("bench 4 readers", BenchReaders4, 4, Seconds);
    RunMix("bench 4 writers", BenchWriters4, 4, Seconds);
    RunMix("bench 3 readers + writer", BenchMixed, 4, Seconds);

    CHECK(SrwAsserts == 0);
    printf("srw: %lu checks, %lu failures\n", Checks, Failures);
    return Failures != 0;
}
