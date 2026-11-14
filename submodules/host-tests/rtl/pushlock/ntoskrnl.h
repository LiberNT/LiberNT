/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host environment for compiling ntos/ex/expushlock.c
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

typedef int32_t LONG, *PLONG;
typedef uint32_t ULONG, *PULONG;
typedef uint16_t USHORT, *PUSHORT;
typedef uint64_t ULONG64, *PULONG64;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR, *PULONG_PTR;
typedef size_t SIZE_T, *PSIZE_T;
typedef ssize_t SSIZE_T;
typedef unsigned char BOOLEAN, UCHAR, *PUCHAR, KIRQL;
typedef void VOID, *PVOID;
typedef LONG NTSTATUS;

#define TRUE 1
#define FALSE 0
#define NTAPI
#define FASTCALL
#define CODE_SEG(x)
#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define _In_reads_bytes_(x)
#define __ALIGNED(x) __attribute__((aligned(x)))
#define UNREFERENCED_PARAMETER(x) ((void)(x))

#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_TIMEOUT ((NTSTATUS)0x00000102)

#define APC_LEVEL 1
#define KernelMode 0
#define WrPushLock 28
#define SynchronizationEvent 1

typedef union _LARGE_INTEGER
{
    int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _HOST_WAITABLE
{
    pthread_mutex_t Mutex;
    pthread_cond_t Cond;
    int Signaled;
} HOST_WAITABLE;

typedef HOST_WAITABLE KGATE, *PKGATE;
typedef HOST_WAITABLE KEVENT, *PKEVENT;

typedef struct _EX_PUSH_LOCK
{
    union
    {
        struct
        {
            ULONG_PTR Locked:1;
            ULONG_PTR Waiting:1;
            ULONG_PTR Waking:1;
            ULONG_PTR MultipleShared:1;
            ULONG_PTR Shared:sizeof(ULONG_PTR) * 8 - 4;
        };
        ULONG_PTR Value;
        PVOID Ptr;
    };
} EX_PUSH_LOCK, *PEX_PUSH_LOCK;

#define EX_PUSH_LOCK_LOCK_V                 ((ULONG_PTR)0x0)
#define EX_PUSH_LOCK_LOCK                   ((ULONG_PTR)0x1)
#define EX_PUSH_LOCK_WAITING                ((ULONG_PTR)0x2)
#define EX_PUSH_LOCK_WAKING                 ((ULONG_PTR)0x4)
#define EX_PUSH_LOCK_MULTIPLE_SHARED        ((ULONG_PTR)0x8)
#define EX_PUSH_LOCK_SHARE_INC              ((ULONG_PTR)0x10)
#define EX_PUSH_LOCK_PTR_BITS               ((ULONG_PTR)0xf)
#define EX_PUSH_LOCK_FLAGS_EXCLUSIVE        1
#define EX_PUSH_LOCK_FLAGS_WAIT_V           1
#define EX_PUSH_LOCK_FLAGS_WAIT             2

typedef __ALIGNED(16) struct _EX_PUSH_LOCK_WAIT_BLOCK
{
    union
    {
        KGATE WakeGate;
        KEVENT WakeEvent;
    };
    struct _EX_PUSH_LOCK_WAIT_BLOCK *Next;
    struct _EX_PUSH_LOCK_WAIT_BLOCK *Last;
    struct _EX_PUSH_LOCK_WAIT_BLOCK *Previous;
    LONG ShareCount;
    LONG Flags;
} EX_PUSH_LOCK_WAIT_BLOCK, *PEX_PUSH_LOCK_WAIT_BLOCK;

extern unsigned long PushLockAsserts;
extern unsigned long GateWaits;
#define ASSERT(e) do { if (!(e)) { __atomic_fetch_add(&PushLockAsserts, 1, __ATOMIC_RELAXED); fprintf(stderr, "ASSERT %s:%d %s\n", __FILE__, __LINE__, #e); } } while (0)

extern ULONG KeNumberProcessors;

static inline KIRQL KeGetCurrentIrql(void)
{
    return 0;
}

static inline void HostWaitableInit(HOST_WAITABLE *Waitable)
{
    pthread_mutex_init(&Waitable->Mutex, NULL);
    pthread_cond_init(&Waitable->Cond, NULL);
    Waitable->Signaled = 0;
}

static inline void KeInitializeGate(PKGATE Gate)
{
    HostWaitableInit(Gate);
}

static inline void KeInitializeEvent(PKEVENT Event, int Type, BOOLEAN State)
{
    (void)Type;
    HostWaitableInit(Event);
    Event->Signaled = State;
}

void KeWaitForGate(PKGATE Gate, int WaitReason, int WaitMode);
void KeSignalGateBoostPriority(PKGATE Gate);
void KeSetEventBoostPriority(PKEVENT Event, PVOID Thread);
NTSTATUS KeWaitForSingleObject(PVOID Object, int WaitReason, int WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout);

static inline PVOID InterlockedCompareExchangePointer(PVOID volatile *Target, PVOID New, PVOID Old)
{
    __atomic_compare_exchange_n(Target, &Old, New, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Old;
}

static inline PVOID InterlockedExchangePointer(PVOID volatile *Target, PVOID New)
{
    return __atomic_exchange_n(Target, New, __ATOMIC_SEQ_CST);
}

static inline SIZE_T InterlockedExchangeAddSizeT(PSIZE_T Target, SSIZE_T Value)
{
    return __atomic_fetch_add((SIZE_T volatile *)Target, (SIZE_T)Value, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedDecrement(LONG volatile *Target)
{
    return __atomic_sub_fetch(Target, 1, __ATOMIC_SEQ_CST);
}

static inline BOOLEAN InterlockedBitTestAndReset(LONG volatile *Base, LONG Bit)
{
    return (__atomic_fetch_and(Base, ~(1 << Bit), __ATOMIC_SEQ_CST) >> Bit) & 1;
}

static inline PVOID ReadPointerNoFence(PVOID const volatile *Source)
{
    return __atomic_load_n(Source, __ATOMIC_RELAXED);
}

static inline PVOID ReadPointerAcquire(PVOID const volatile *Source)
{
    return __atomic_load_n(Source, __ATOMIC_ACQUIRE);
}

static inline void WritePointerNoFence(PVOID volatile *Destination, PVOID Value)
{
    __atomic_store_n(Destination, Value, __ATOMIC_RELAXED);
}

static inline LONG ReadNoFence(LONG const volatile *Source)
{
    return __atomic_load_n(Source, __ATOMIC_RELAXED);
}

#if defined(__aarch64__)
#define YieldProcessor() __asm__ __volatile__("yield")
#else
#define YieldProcessor() __asm__ __volatile__("pause")
#endif

#define ExpChangePushlock(x, y, z) InterlockedCompareExchangePointer((PVOID *)(x), (PVOID)(y), (PVOID)(z))

void FASTCALL ExfAcquirePushLockExclusive(PEX_PUSH_LOCK PushLock);
void FASTCALL ExfAcquirePushLockShared(PEX_PUSH_LOCK PushLock);
BOOLEAN FASTCALL ExfTryAcquirePushLockShared(PEX_PUSH_LOCK PushLock);
void FASTCALL ExfReleasePushLockExclusive(PEX_PUSH_LOCK PushLock);
void FASTCALL ExfReleasePushLockShared(PEX_PUSH_LOCK PushLock);
void FASTCALL ExfReleasePushLock(PEX_PUSH_LOCK PushLock);
void FASTCALL ExfTryToWakePushLock(PEX_PUSH_LOCK PushLock);
void NTAPI ExInitializePushLock(PULONG_PTR PushLock);
void NTAPI ExpInitializePushLocks(void);

static inline void ExAcquirePushLockExclusive(PEX_PUSH_LOCK PushLock)
{
    if (__atomic_fetch_or((ULONG_PTR volatile *)&PushLock->Value, EX_PUSH_LOCK_LOCK, __ATOMIC_SEQ_CST) & EX_PUSH_LOCK_LOCK)
        ExfAcquirePushLockExclusive(PushLock);
}

static inline BOOLEAN ExTryToAcquirePushLockExclusive(PEX_PUSH_LOCK PushLock)
{
    if (__atomic_fetch_or((ULONG_PTR volatile *)&PushLock->Value, EX_PUSH_LOCK_LOCK, __ATOMIC_SEQ_CST) & EX_PUSH_LOCK_LOCK)
        return FALSE;
    return TRUE;
}

static inline void ExAcquirePushLockShared(PEX_PUSH_LOCK PushLock)
{
    if (ExpChangePushlock(PushLock, EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC, 0))
        ExfAcquirePushLockShared(PushLock);
}

static inline void ExReleasePushLockShared(PEX_PUSH_LOCK PushLock)
{
    if (ExpChangePushlock(PushLock, 0, EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC) !=
        (PVOID)(EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC))
    {
        ExfReleasePushLockShared(PushLock);
    }
}

static inline void ExReleasePushLockExclusive(PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue;

    OldValue.Value = InterlockedExchangeAddSizeT((PSIZE_T)PushLock, -(SSIZE_T)EX_PUSH_LOCK_LOCK);
    ASSERT(OldValue.Locked);
    ASSERT(OldValue.Waiting || OldValue.Shared == 0);
    if (OldValue.Waiting && !OldValue.Waking)
        ExfTryToWakePushLock(PushLock);
}
