/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bootstrap allocator host definitions
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
typedef int32_t LONG;
typedef uint32_t ULONG, NTSTATUS;
typedef uint64_t ULONG64;
typedef unsigned char UCHAR, BOOLEAN, *PUCHAR;
typedef size_t SIZE_T;
typedef void *PVOID;
typedef void VOID;
typedef ULONG *PULONG;
typedef struct { int Pfn; } MI_SYSTEM;
typedef struct { ULONG64 AvailableLow; } MI_PROCESS_MANAGER;
typedef struct { int unused; } KEVENT;
typedef struct { int MemoryMaker; } THREAD;
typedef struct { long long QuadPart; } LARGE_INTEGER;
extern MI_SYSTEM MiSystem;
extern MI_PROCESS_MANAGER MiProcessManager;
#define TRUE 1
#define FALSE 0
#define NTAPI
#define _In_
#define _Inout_
#define ROUND_UP(v,a) (((v)+((a)-1))&~((a)-1))
#define NonPagedPool 0
#define MI_CORE_TAG 0
#define STATUS_SUCCESS 0
#define STATUS_NO_MEMORY 1
#define APC_LEVEL 1
#define MI_FRAME_INVALID UINT32_MAX
#define MiPageStandby 0
#define NotificationEvent 0
#define WrFreePage 0
#define KernelMode 0
#define NT_SUCCESS(s) ((s)==0)
#define ASSERT(x) assert(x)
static inline LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG o) { return __sync_val_compare_and_swap(p,o,n); }
static inline PVOID ExAllocatePoolWithTag(int t,SIZE_T n,int tag) { (void)t;(void)tag;return malloc(n); }
static inline void ExFreePoolWithTag(PVOID p,int tag) { (void)tag;free(p); }
static inline void KeInitializeEvent(KEVENT *e,int t,int s) { (void)e;(void)t;(void)s; }
static inline void KePulseEvent(KEVENT *e,int p,int w) { (void)e;(void)p;(void)w; }
static inline int KeGetCurrentIrql(void) { return 0; }
static inline THREAD *PsGetCurrentThread(void) { static THREAD t; return &t; }
static inline ULONG MiSystemPtesRefillTables(MI_SYSTEM *s) { (void)s;return 0; }
static inline ULONG64 MiPfnAvailablePages(int *p) { (void)p;return 0; }
static inline ULONG64 MiPfnListCount(int *p,int x) { (void)p;(void)x;return 0; }
static inline ULONG MiPfnAllocatePage(int *p,int x) { (void)p;(void)x;return MI_FRAME_INVALID; }
static inline void MiPfnFreePage(int *p,ULONG x) { (void)p;(void)x; }
static inline ULONG MiBalanceMemory(MI_SYSTEM *s,MI_PROCESS_MANAGER *m) { (void)s;(void)m;return 0; }
static inline void MiWakeBalanceSetManager(void) {}
static inline void KeWaitForSingleObject(KEVENT *e,int r,int m,int a,LARGE_INTEGER *t) { (void)e;(void)r;(void)m;(void)a;(void)t; }
