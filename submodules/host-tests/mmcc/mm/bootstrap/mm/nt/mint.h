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
#include <wchar.h>
typedef KEVENT *PKEVENT;
typedef void *HANDLE;
typedef const wchar_t *PCWSTR;
typedef struct { unsigned short Length, MaximumLength; const wchar_t *Buffer; } UNICODE_STRING;
typedef struct { ULONG Attributes; UNICODE_STRING *ObjectName; PVOID SecurityDescriptor; } OBJECT_ATTRIBUTES;
typedef struct { UCHAR Revision; } SECURITY_DESCRIPTOR, *PSECURITY_DESCRIPTOR;
typedef struct { UCHAR AclRevision; } ACL, *PACL;
typedef struct { UCHAR AceType; } ACE;
#define _Outptr_
#define PagedPool 1
#define PAGE_SHIFT 12
#define ACL_REVISION 2
#define SECURITY_DESCRIPTOR_REVISION 1
#define EVENT_QUERY_STATE 0x0001
#define SYNCHRONIZE 0x00100000
#define READ_CONTROL 0x00020000
#define EVENT_ALL_ACCESS 0x001F0003
#define OBJ_CASE_INSENSITIVE 0x00000040
#define OBJ_PERMANENT 0x00000010
#define OBJ_KERNEL_HANDLE 0x00000200
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002)
#define SeWorldSid ((PVOID)0)
#define SeAliasAdminsSid ((PVOID)0)
#define SeLocalSystemSid ((PVOID)0)
#define ExEventObjectType ((PVOID)0)
#define InitializeObjectAttributes(o,n,a,r,s) ((o)->Attributes=(a),(o)->ObjectName=(n),(o)->SecurityDescriptor=(s),(void)(r))
static ULONG64 MmNumberOfPhysicalPages;
static inline ULONG RtlLengthSid(PVOID Sid) { (void)Sid;return 0; }
static inline NTSTATUS RtlCreateSecurityDescriptor(PSECURITY_DESCRIPTOR Sd,ULONG r) { (void)Sd;(void)r;return STATUS_NOT_IMPLEMENTED; }
static inline NTSTATUS RtlCreateAcl(PACL Acl,ULONG n,ULONG r) { (void)Acl;(void)n;(void)r;return STATUS_NOT_IMPLEMENTED; }
static inline NTSTATUS RtlAddAccessAllowedAce(PACL Acl,ULONG r,ULONG m,PVOID Sid) { (void)Acl;(void)r;(void)m;(void)Sid;return STATUS_NOT_IMPLEMENTED; }
static inline NTSTATUS RtlSetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR Sd,BOOLEAN p,PACL Acl,BOOLEAN d) { (void)Sd;(void)p;(void)Acl;(void)d;return STATUS_NOT_IMPLEMENTED; }
static inline void RtlInitUnicodeString(UNICODE_STRING *u,PCWSTR s) { u->Buffer=s;u->Length=u->MaximumLength=0; }
static inline NTSTATUS ZwCreateEvent(HANDLE *h,ULONG a,OBJECT_ATTRIBUTES *o,int t,BOOLEAN s) { (void)a;(void)o;(void)t;(void)s;*h=0;return STATUS_NOT_IMPLEMENTED; }
static inline NTSTATUS ObReferenceObjectByHandle(HANDLE h,ULONG a,PVOID t,int m,PVOID *o,PVOID i) { (void)h;(void)a;(void)t;(void)m;(void)i;*o=0;return STATUS_NOT_IMPLEMENTED; }
static inline NTSTATUS ZwClose(HANDLE h) { (void)h;return STATUS_SUCCESS; }
static inline void KeClearEvent(KEVENT *e) { (void)e; }
static inline LONG KeSetEvent(KEVENT *e,LONG i,BOOLEAN w) { (void)e;(void)i;(void)w;return 0; }
