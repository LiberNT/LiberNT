/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host environment for compiling sdk/lib/rtl/srw.c
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

typedef int32_t LONG, *PLONG;
typedef uint32_t ULONG, *PULONG;
typedef int64_t LONG64;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef unsigned char BOOLEAN, UCHAR;
typedef char CHAR;
typedef void VOID, *PVOID;
typedef void *HANDLE;
typedef LONG NTSTATUS;

#define TRUE 1
#define FALSE 0
#define NTAPI
#define IN
#define OUT
#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define DECLSPEC_NOINLINE __attribute__((noinline))
#define DECLSPEC_ALIGN(x) __attribute__((aligned(x)))
#define C_ASSERT(e) _Static_assert(e, #e)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#define STATUS_RESOURCE_NOT_OWNED ((NTSTATUS)0xC0000264)

typedef struct _LIST_ENTRY
{
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _RTL_SRWLOCK
{
    PVOID Ptr;
} RTL_SRWLOCK, *PRTL_SRWLOCK;

typedef union _LARGE_INTEGER
{
    int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _HOST_CLIENT_ID
{
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} HOST_CLIENT_ID;

typedef struct _HOST_TEB
{
    HOST_CLIENT_ID ClientId;
} HOST_TEB;

typedef struct _HOST_PEB
{
    ULONG NumberOfProcessors;
} HOST_PEB;

HOST_TEB *NtCurrentTeb(void);
HOST_PEB *NtCurrentPeb(void);
NTSTATUS NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER Interval);
NTSTATUS NtWaitForAlertByThreadId(PVOID Address, PLARGE_INTEGER Timeout);
NTSTATUS NtAlertThreadByThreadId(HANDLE ThreadId);
void RtlRaiseStatus(NTSTATUS Status);

static inline void InitializeListHead(PLIST_ENTRY Head) { Head->Flink = Head->Blink = Head; }
static inline void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}
static inline BOOLEAN RemoveEntryList(PLIST_ENTRY Entry)
{
    Entry->Blink->Flink = Entry->Flink;
    Entry->Flink->Blink = Entry->Blink;
    return Entry->Flink == Entry->Blink;
}

static inline PVOID InterlockedCompareExchangePointer(PVOID volatile *Target, PVOID New, PVOID Old)
{
    __atomic_compare_exchange_n(Target, &Old, New, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Old;
}
static inline PVOID InterlockedExchangePointer(PVOID volatile *Target, PVOID New)
{
    return __atomic_exchange_n(Target, New, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedCompareExchange(LONG volatile *Target, LONG New, LONG Old)
{
    __atomic_compare_exchange_n(Target, &Old, New, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Old;
}
static inline LONG InterlockedExchange(LONG volatile *Target, LONG New)
{
    return __atomic_exchange_n(Target, New, __ATOMIC_SEQ_CST);
}
static inline SIZE_T InterlockedExchangeAddSizeT(PVOID volatile *Target, SIZE_T Value)
{
    return __atomic_fetch_add((SIZE_T volatile *)Target, Value, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedDecrement(LONG volatile *Target)
{
    return __atomic_sub_fetch(Target, 1, __ATOMIC_SEQ_CST);
}
static inline LONG ReadNoFence(LONG const volatile *Source)
{
    return __atomic_load_n(Source, __ATOMIC_RELAXED);
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
static inline void WritePointerRelease(PVOID volatile *Destination, PVOID Value)
{
    __atomic_store_n(Destination, Value, __ATOMIC_RELEASE);
}
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#if defined(__aarch64__)
#define YieldProcessor() __asm__ __volatile__("yield")
#else
#define YieldProcessor() __asm__ __volatile__("pause")
#endif

extern unsigned long SrwAsserts;
#define ASSERT(e) do { if (!(e)) { __atomic_fetch_add(&SrwAsserts, 1, __ATOMIC_RELAXED); fprintf(stderr, "ASSERT %s:%d %s\n", __FILE__, __LINE__, #e); } } while (0)

VOID NTAPI RtlInitializeSRWLock(PRTL_SRWLOCK SRWLock);
VOID NTAPI RtlAcquireSRWLockShared(PRTL_SRWLOCK SRWLock);
VOID NTAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK SRWLock);
VOID NTAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK SRWLock);
VOID NTAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK SRWLock);
BOOLEAN NTAPI RtlTryAcquireSRWLockShared(PRTL_SRWLOCK SRWLock);
BOOLEAN NTAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK SRWLock);
