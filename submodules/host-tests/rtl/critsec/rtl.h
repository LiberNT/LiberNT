/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host environment for compiling sdk/lib/rtl/critical.c
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef int32_t LONG, *PLONG;
typedef uint32_t ULONG, *PULONG;
typedef uint16_t USHORT;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef unsigned char BOOLEAN, UCHAR;
typedef LONG LOGICAL;
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
#define _At_(a, b)
#define _Post_notnull_
#define DECLSPEC_NOINLINE __attribute__((noinline))
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_TIMEOUT ((NTSTATUS)0x00000102)
#define STATUS_NO_MEMORY ((NTSTATUS)0xC0000017)
#define STATUS_THREAD_IS_TERMINATING ((NTSTATUS)0xC000004B)
#define STATUS_INVALID_PARAMETER_2 ((NTSTATUS)0xC00000F0)
#define STATUS_INVALID_PARAMETER_3 ((NTSTATUS)0xC00000F1)
#define STATUS_POSSIBLE_DEADLOCK ((NTSTATUS)0xC0000194)
#define STATUS_RESOURCE_NOT_OWNED ((NTSTATUS)0xC0000264)

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define LongToPtr(l) ((PVOID)(LONG_PTR)(l))
#define _WIN32_WINNT_WIN7 0x0601
#define EXCEPTION_EXECUTE_HANDLER 1
#define RTL_CRITSECT_TYPE 0

#define RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO    0x01000000
#define RTL_CRITICAL_SECTION_FLAG_DYNAMIC_SPIN     0x02000000
#define RTL_CRITICAL_SECTION_FLAG_STATIC_INIT      0x04000000
#define RTL_CRITICAL_SECTION_FLAG_RESOURCE_TYPE    0x08000000
#define RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO 0x10000000
#define RTL_CRITICAL_SECTION_ALL_FLAG_BITS         0xFF000000

#define _SEH2_TRY {
#define _SEH2_EXCEPT(Filter) } if (0) {
#define _SEH2_END }
#define ERROR_DBGBREAK(...) fprintf(stderr, __VA_ARGS__)
#define UNIMPLEMENTED fprintf(stderr, "%s is unimplemented\n", __func__)
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))

typedef struct _LIST_ENTRY
{
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef union _LARGE_INTEGER
{
    int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _EXCEPTION_RECORD
{
    NTSTATUS ExceptionCode;
    ULONG ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    ULONG NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

struct _RTL_CRITICAL_SECTION;

typedef struct _RTL_CRITICAL_SECTION_DEBUG
{
    USHORT Type;
    USHORT CreatorBackTraceIndex;
    struct _RTL_CRITICAL_SECTION *CriticalSection;
    LIST_ENTRY ProcessLocksList;
    ULONG EntryCount;
    ULONG ContentionCount;
    ULONG Flags;
    USHORT CreatorBackTraceIndexHigh;
    USHORT SpareUSHORT;
} RTL_CRITICAL_SECTION_DEBUG, *PRTL_CRITICAL_SECTION_DEBUG;

typedef struct _RTL_CRITICAL_SECTION
{
    PRTL_CRITICAL_SECTION_DEBUG DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION;

typedef struct _HOST_CLIENT_ID
{
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} HOST_CLIENT_ID;

typedef struct _HOST_TEB
{
    HOST_CLIENT_ID ClientId;
    HOST_CLIENT_ID RealClientId;
    ULONG CountOfOwnedCriticalSections;
} HOST_TEB;

typedef struct _HOST_PEB
{
    ULONG NumberOfProcessors;
    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    PVOID ProcessHeap;
} HOST_PEB;

HOST_TEB *NtCurrentTeb(void);
HOST_PEB *NtCurrentPeb(void);
#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
#define RtlGetProcessHeap() (NtCurrentPeb()->ProcessHeap)

NTSTATUS NtTerminateProcess(HANDLE Process, NTSTATUS ExitStatus);
NTSTATUS NtWaitForKeyedEvent(HANDLE Handle, PVOID Key, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
NTSTATUS NtReleaseKeyedEvent(HANDLE Handle, PVOID Key, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
NTSTATUS NtWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
NTSTATUS NtSetEvent(HANDLE Event, PLONG PreviousState);
NTSTATUS NtClose(HANDLE Handle);
void RtlRaiseException(PEXCEPTION_RECORD ExceptionRecord);
void RtlRaiseStatus(NTSTATUS Status);
PVOID RtlAllocateHeap(PVOID Heap, ULONG Flags, SIZE_T Size);
BOOLEAN RtlFreeHeap(PVOID Heap, ULONG Flags, PVOID Pointer);

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

static inline LONG InterlockedCompareExchange(LONG volatile *Target, LONG New, LONG Old)
{
    __atomic_compare_exchange_n(Target, &Old, New, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Old;
}

static inline PVOID InterlockedCompareExchangePointer(PVOID volatile *Target, PVOID New, PVOID Old)
{
    __atomic_compare_exchange_n(Target, &Old, New, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Old;
}

static inline LONG ReadNoFence(LONG const volatile *Source)
{
    return __atomic_load_n(Source, __ATOMIC_RELAXED);
}

static inline PVOID ReadPointerNoFence(PVOID const volatile *Source)
{
    return __atomic_load_n(Source, __ATOMIC_RELAXED);
}

static inline void WritePointerNoFence(PVOID volatile *Destination, PVOID Value)
{
    __atomic_store_n(Destination, Value, __ATOMIC_RELAXED);
}

static inline ULONG_PTR ReadULongPtrNoFence(ULONG_PTR const volatile *Source)
{
    return __atomic_load_n(Source, __ATOMIC_RELAXED);
}

static inline void WriteULongPtrNoFence(ULONG_PTR volatile *Destination, ULONG_PTR Value)
{
    __atomic_store_n(Destination, Value, __ATOMIC_RELAXED);
}

#if defined(__aarch64__)
#define YieldProcessor() __asm__ __volatile__("yield")
#else
#define YieldProcessor() __asm__ __volatile__("pause")
#endif

NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
NTSTATUS NTAPI RtlInitializeCriticalSectionEx(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount, ULONG Flags);
NTSTATUS NTAPI RtlInitializeCriticalSectionAndSpinCount(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount);
NTSTATUS NTAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
ULONG NTAPI RtlSetCriticalSectionSpinCount(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount);
NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
NTSTATUS NTAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
LOGICAL NTAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
LONG NTAPI RtlGetCriticalSectionRecursionCount(PRTL_CRITICAL_SECTION CriticalSection);
VOID NTAPI RtlpInitDeferredCriticalSection(VOID);
PRTL_CRITICAL_SECTION_DEBUG NTAPI RtlpAllocateDebugInfo(VOID);
VOID NTAPI RtlpFreeDebugInfo(PRTL_CRITICAL_SECTION_DEBUG DebugInfo);
NTSTATUS NTAPI RtlpWaitForCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
VOID NTAPI RtlpUnWaitCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
VOID NTAPI RtlpCreateCriticalSectionSem(PRTL_CRITICAL_SECTION CriticalSection);
