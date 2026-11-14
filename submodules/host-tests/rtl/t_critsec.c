/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Critical section exclusion, recursion and wake-ups under concurrent load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include "critsec/rtl.h"

#define CS_MAX_THREADS 16
#define CS_KEYS 8

static unsigned long Checks, Failures;

#define CHECK(c) do { __atomic_fetch_add(&Checks, 1, __ATOMIC_RELAXED); if (!(c)) { __atomic_fetch_add(&Failures, 1, __ATOMIC_RELAXED); fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

BOOLEAN LdrpShutdownInProgress;
HANDLE LdrpShutdownThreadId;
extern BOOLEAN RtlpTimeoutDisable;

static __thread HOST_TEB ThreadTeb;
static HOST_PEB Peb;
static volatile LONG NextThreadId;

typedef struct _KEYED_SLOT
{
    PVOID Key;
    unsigned long Waiting;
    unsigned long Granted;
    unsigned long Offered;
    unsigned long Taken;
} KEYED_SLOT;

static pthread_mutex_t KeyedMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t KeyedCond = PTHREAD_COND_INITIALIZER;
static KEYED_SLOT KeyedSlots[CS_KEYS];
static unsigned long KeyedWaits, KeyedReleases;

HOST_TEB *NtCurrentTeb(void)
{
    if (ThreadTeb.ClientId.UniqueThread == NULL)
    {
        LONG Id = __atomic_add_fetch(&NextThreadId, 1, __ATOMIC_SEQ_CST);

        ThreadTeb.ClientId.UniqueProcess = (HANDLE)(ULONG_PTR)4;
        ThreadTeb.ClientId.UniqueThread = (HANDLE)(ULONG_PTR)(Id * 4);
        ThreadTeb.RealClientId = ThreadTeb.ClientId;
    }
    return &ThreadTeb;
}

HOST_PEB *NtCurrentPeb(void)
{
    return &Peb;
}

static KEYED_SLOT *KeyedSlot(PVOID Key)
{
    int i;

    for (i = 0; i < CS_KEYS; i++)
    {
        if (KeyedSlots[i].Key == Key)
            return &KeyedSlots[i];
    }
    for (i = 0; i < CS_KEYS; i++)
    {
        if (KeyedSlots[i].Key == NULL)
        {
            KeyedSlots[i].Key = Key;
            return &KeyedSlots[i];
        }
    }
    abort();
}

NTSTATUS NtWaitForKeyedEvent(HANDLE Handle, PVOID Key, BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
    KEYED_SLOT *Slot;

    (void)Handle;
    (void)Alertable;
    CHECK(Timeout == NULL);
    pthread_mutex_lock(&KeyedMutex);
    KeyedWaits++;
    Slot = KeyedSlot(Key);
    if (Slot->Offered != Slot->Taken)
    {
        Slot->Taken++;
        pthread_cond_broadcast(&KeyedCond);
    }
    else
    {
        Slot->Waiting++;
        while (Slot->Granted == 0)
            pthread_cond_wait(&KeyedCond, &KeyedMutex);
        Slot->Granted--;
        Slot->Waiting--;
    }
    pthread_mutex_unlock(&KeyedMutex);
    return STATUS_SUCCESS;
}

NTSTATUS NtReleaseKeyedEvent(HANDLE Handle, PVOID Key, BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
    KEYED_SLOT *Slot;
    unsigned long Ticket;

    (void)Handle;
    (void)Alertable;
    (void)Timeout;
    pthread_mutex_lock(&KeyedMutex);
    KeyedReleases++;
    Slot = KeyedSlot(Key);
    if (Slot->Waiting > Slot->Granted)
    {
        Slot->Granted++;
        pthread_cond_broadcast(&KeyedCond);
    }
    else
    {
        Ticket = ++Slot->Offered;
        while (Slot->Taken < Ticket)
            pthread_cond_wait(&KeyedCond, &KeyedMutex);
    }
    pthread_mutex_unlock(&KeyedMutex);
    return STATUS_SUCCESS;
}

NTSTATUS NtWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
    (void)Handle;
    (void)Alertable;
    (void)Timeout;
    abort();
}

NTSTATUS NtSetEvent(HANDLE Event, PLONG PreviousState)
{
    (void)Event;
    (void)PreviousState;
    abort();
}

NTSTATUS NtClose(HANDLE Handle)
{
    (void)Handle;
    return STATUS_SUCCESS;
}

NTSTATUS NtTerminateProcess(HANDLE Process, NTSTATUS ExitStatus)
{
    (void)Process;
    fprintf(stderr, "NtTerminateProcess 0x%x\n", (unsigned)ExitStatus);
    abort();
}

void RtlRaiseException(PEXCEPTION_RECORD ExceptionRecord)
{
    fprintf(stderr, "RtlRaiseException 0x%x\n", (unsigned)ExceptionRecord->ExceptionCode);
    abort();
}

void RtlRaiseStatus(NTSTATUS Status)
{
    fprintf(stderr, "RtlRaiseStatus 0x%x\n", (unsigned)Status);
    abort();
}

PVOID RtlAllocateHeap(PVOID Heap, ULONG Flags, SIZE_T Size)
{
    (void)Heap;
    (void)Flags;
    return malloc(Size);
}

BOOLEAN RtlFreeHeap(PVOID Heap, ULONG Flags, PVOID Pointer)
{
    (void)Heap;
    (void)Flags;
    free(Pointer);
    return TRUE;
}

enum
{
    RoleEnter,
    RoleRecursive,
    RoleTry,
    RoleBench,
};

typedef struct _CS_SHARED
{
    __attribute__((aligned(128))) RTL_CRITICAL_SECTION Lock;
    __attribute__((aligned(128))) volatile LONG Inside;
    volatile uint64_t A;
    volatile uint64_t B;
    volatile int Stop;
    volatile int Go;
} CS_SHARED;

typedef struct __attribute__((aligned(128))) _CS_WORKER
{
    CS_SHARED *Shared;
    int Role;
    uint64_t Ops;
    ULONG OwnedAtExit;
} CS_WORKER;

static void Section(CS_SHARED *Shared)
{
    CHECK(__atomic_fetch_add(&Shared->Inside, 1, __ATOMIC_SEQ_CST) == 0);
    CHECK(Shared->Lock.OwningThread == NtCurrentTeb()->ClientId.UniqueThread);
    Shared->A = Shared->A + 1;
    Shared->B = Shared->B + 1;
    CHECK(Shared->A == Shared->B);
    __atomic_fetch_sub(&Shared->Inside, 1, __ATOMIC_SEQ_CST);
}

static void *Worker(void *Argument)
{
    CS_WORKER *Worker = Argument;
    CS_SHARED *Shared = Worker->Shared;

    NtCurrentTeb();
    while (!__atomic_load_n(&Shared->Go, __ATOMIC_ACQUIRE))
        sched_yield();

    while (!__atomic_load_n(&Shared->Stop, __ATOMIC_ACQUIRE))
    {
        switch (Worker->Role)
        {
            case RoleEnter:
                RtlEnterCriticalSection(&Shared->Lock);
                CHECK(RtlGetCriticalSectionRecursionCount(&Shared->Lock) == 1);
                Section(Shared);
                RtlLeaveCriticalSection(&Shared->Lock);
                break;
            case RoleRecursive:
                RtlEnterCriticalSection(&Shared->Lock);
                Section(Shared);
                RtlEnterCriticalSection(&Shared->Lock);
                CHECK(RtlGetCriticalSectionRecursionCount(&Shared->Lock) == 2);
                CHECK(RtlTryEnterCriticalSection(&Shared->Lock));
                CHECK(RtlGetCriticalSectionRecursionCount(&Shared->Lock) == 3);
                RtlLeaveCriticalSection(&Shared->Lock);
                Section(Shared);
                RtlLeaveCriticalSection(&Shared->Lock);
                Section(Shared);
                RtlLeaveCriticalSection(&Shared->Lock);
                break;
            case RoleTry:
                if (RtlTryEnterCriticalSection(&Shared->Lock))
                {
                    Section(Shared);
                    RtlLeaveCriticalSection(&Shared->Lock);
                }
                break;
            case RoleBench:
                RtlEnterCriticalSection(&Shared->Lock);
                Shared->A = Shared->A + 1;
                Shared->B = Shared->B + 1;
                RtlLeaveCriticalSection(&Shared->Lock);
                break;
        }
        Worker->Ops++;
    }
    Worker->OwnedAtExit = NtCurrentTeb()->CountOfOwnedCriticalSections;
    return NULL;
}

static void RunMix(const char *Name, const int *Roles, int Count, LONG SpinCount, double Seconds)
{
    static CS_SHARED Shared;
    CS_WORKER Workers[CS_MAX_THREADS];
    pthread_t Threads[CS_MAX_THREADS];
    unsigned long Waits = KeyedWaits;
    uint64_t Total = 0;
    int i;

    memset(&Shared, 0, sizeof(Shared));
    CHECK(RtlInitializeCriticalSection(&Shared.Lock) == STATUS_SUCCESS);
    if (SpinCount >= 0)
        RtlSetCriticalSectionSpinCount(&Shared.Lock, (ULONG)SpinCount);
    for (i = 0; i < Count; i++)
    {
        Workers[i] = (CS_WORKER){ &Shared, Roles[i], 0, 0 };
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
        CHECK(Workers[i].Ops != 0 || Roles[i] == RoleTry);
        CHECK(Workers[i].OwnedAtExit == 0);
    }

    CHECK(Shared.Inside == 0);
    CHECK(Shared.A == Shared.B);
    CHECK(Shared.Lock.LockCount == -1);
    CHECK(Shared.Lock.RecursionCount == 0);
    CHECK(Shared.Lock.OwningThread == NULL);
    CHECK(RtlTryEnterCriticalSection(&Shared.Lock));
    RtlLeaveCriticalSection(&Shared.Lock);
    CHECK(Shared.Lock.LockCount == -1);
    CHECK(RtlDeleteCriticalSection(&Shared.Lock) == STATUS_SUCCESS);
    pthread_mutex_lock(&KeyedMutex);
    for (i = 0; i < CS_KEYS; i++)
    {
        if (KeyedSlots[i].Key == &Shared.Lock)
        {
            CHECK(KeyedSlots[i].Waiting == 0 && KeyedSlots[i].Granted == 0);
            CHECK(KeyedSlots[i].Offered == KeyedSlots[i].Taken);
            memset(&KeyedSlots[i], 0, sizeof(KeyedSlots[i]));
        }
    }
    Waits = KeyedWaits - Waits;
    pthread_mutex_unlock(&KeyedMutex);
    printf("  %-30s %10.1f Mops/s total (%lu waits)\n", Name, Total / Seconds / 1e6, Waits);
}

static void TestSingleThread(void)
{
    RTL_CRITICAL_SECTION Lock;

    CHECK(RtlInitializeCriticalSectionEx(&Lock, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO) == STATUS_SUCCESS);
    CHECK(Lock.DebugInfo != NULL && Lock.DebugInfo != LongToPtr(-1));
    CHECK(Lock.LockCount == -1);
    CHECK(RtlEnterCriticalSection(&Lock) == STATUS_SUCCESS);
    CHECK(Lock.LockCount == -2 && Lock.RecursionCount == 1);
    CHECK(Lock.OwningThread == NtCurrentTeb()->ClientId.UniqueThread);
    CHECK(NtCurrentTeb()->CountOfOwnedCriticalSections == 0);
    CHECK(RtlEnterCriticalSection(&Lock) == STATUS_SUCCESS);
    CHECK(Lock.LockCount == -2 && Lock.RecursionCount == 2);
    CHECK(RtlLeaveCriticalSection(&Lock) == STATUS_SUCCESS);
    CHECK(Lock.LockCount == -2 && Lock.RecursionCount == 1);
    CHECK(RtlLeaveCriticalSection(&Lock) == STATUS_SUCCESS);
    CHECK(Lock.LockCount == -1 && Lock.RecursionCount == 0 && Lock.OwningThread == NULL);
    CHECK(NtCurrentTeb()->CountOfOwnedCriticalSections == 0);
    CHECK(RtlTryEnterCriticalSection(&Lock));
    CHECK(Lock.LockCount == -2);
    CHECK(RtlLeaveCriticalSection(&Lock) == STATUS_SUCCESS);
    CHECK(Lock.LockCount == -1);
    CHECK(RtlDeleteCriticalSection(&Lock) == STATUS_SUCCESS);
}

int main(int argc, char **argv)
{
    static const int Enter4[] = { RoleEnter, RoleEnter, RoleEnter, RoleEnter };
    static const int Bench1[] = { RoleBench };
    static const int Bench4[] = { RoleBench, RoleBench, RoleBench, RoleBench };
    static const int Mixed[] = { RoleEnter, RoleRecursive, RoleTry, RoleEnter, RoleRecursive };
    static const int Enter8[] = { RoleEnter, RoleEnter, RoleEnter, RoleEnter,
                                  RoleRecursive, RoleRecursive, RoleTry, RoleTry };
    double Seconds = argc > 1 ? atof(argv[1]) : 0.3;

    Peb.NumberOfProcessors = (ULONG)sysconf(_SC_NPROCESSORS_ONLN);
    Peb.OSMajorVersion = 10;
    RtlpTimeoutDisable = TRUE;
    NtCurrentTeb();
    RtlpInitDeferredCriticalSection();

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("critical section host test (%u CPUs, %.2f s per mix)\n", Peb.NumberOfProcessors, Seconds);
    TestSingleThread();
    RunMix("4 enter, default spin", Enter4, 4, -1, Seconds);
    RunMix("4 enter, no spin", Enter4, 4, 0, Seconds);
    RunMix("mixed recursive + try", Mixed, 5, -1, Seconds);
    RunMix("mixed recursive + try, no spin", Mixed, 5, 0, Seconds);
    RunMix("8 mixed, spin 64", Enter8, 8, 64, Seconds);
    RunMix("bench 1", Bench1, 1, -1, Seconds);
    RunMix("bench 4", Bench4, 4, -1, Seconds);

    printf("critsec: %lu checks, %lu failures\n", Checks, Failures);
    return Failures != 0;
}
