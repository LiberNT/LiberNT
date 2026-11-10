/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host definitions that let lxcore memory code build on the NVS harness
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "ntprotectshim.h"
#include <setjmp.h>

typedef void *HANDLE, **PHANDLE;
typedef short SHORT;
typedef int INT;
typedef char *PSTR;
typedef LONG *PLONG;
typedef LONG64 *PLONG64;
typedef ULONG_PTR *PULONG_PTR;
typedef ULONG ACCESS_MASK;
typedef UCHAR KPROCESSOR_MODE;
typedef void *PKTHREAD;
typedef struct _EPROCESS_HOST EPROCESS_HOST, *PKPROCESS;
typedef struct _OBJECT_TYPE_HOST *POBJECT_TYPE;

typedef union _LARGE_INTEGER
{
    LONG64 QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _MEMORY_BASIC_INFORMATION
{
    PVOID BaseAddress;
    PVOID AllocationBase;
    ULONG AllocationProtect;
    USHORT PartitionId;
    SIZE_T RegionSize;
    ULONG State;
    ULONG Protect;
    ULONG Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

typedef struct _IO_STATUS_BLOCK
{
    NTSTATUS Status;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _FILE_POSITION_INFORMATION
{
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION
{
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

typedef struct _SECTION_BASIC_INFORMATION
{
    PVOID BaseAddress;
    ULONG Attributes;
    LARGE_INTEGER Size;
} SECTION_BASIC_INFORMATION, *PSECTION_BASIC_INFORMATION;

typedef struct _OBJECT_ATTRIBUTES
{
    ULONG Length;
    HANDLE RootDirectory;
    PVOID ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _LX_HOST_OBJECT
{
    volatile LONG RefCount;
    ULONG Kind;
} LX_HOST_OBJECT, *PLX_HOST_OBJECT;

typedef struct _FILE_OBJECT
{
    LX_HOST_OBJECT Header;
    PVOID SectionObjectPointer;
    PVOID FsContext;
} FILE_OBJECT, *PFILE_OBJECT;

typedef struct _KEVENT
{
    pthread_mutex_t Mutex;
    pthread_cond_t Condition;
    LONG Signaled;
} KEVENT, *PKEVENT;

typedef struct _KAPC_STATE
{
    PVOID SavedSpace;
} KAPC_STATE, *PKAPC_STATE;

typedef enum _SECTION_INHERIT { ViewShare = 1, ViewUnmap = 2 } SECTION_INHERIT;
typedef enum _EVENT_TYPE { NotificationEvent, SynchronizationEvent } EVENT_TYPE;
typedef enum _KWAIT_REASON { Executive = 0, UserRequest = 6 } KWAIT_REASON;
typedef enum _MEMORY_INFORMATION_CLASS { MemoryBasicInformation = 0, MemoryWorkingSetExList = 4 } MEMORY_INFORMATION_CLASS;
typedef enum _FILE_INFORMATION_CLASS
{
    FileStandardInformation = 5,
    FilePositionInformation = 14,
    FileEndOfFileInformation = 20
} FILE_INFORMATION_CLASS;
typedef enum _SECTION_INFORMATION_CLASS { SectionBasicInformation = 0 } SECTION_INFORMATION_CLASS;
typedef enum _PROCESSINFOCLASS { ProcessQuotaLimits = 1 } PROCESSINFOCLASS;

typedef struct _QUOTA_LIMITS
{
    SIZE_T PagedPoolLimit;
    SIZE_T NonPagedPoolLimit;
    SIZE_T MinimumWorkingSetSize;
    SIZE_T MaximumWorkingSetSize;
    SIZE_T PagefileLimit;
    LARGE_INTEGER TimeLimit;
} QUOTA_LIMITS, *PQUOTA_LIMITS;

#define MAP_PROCESS 1
#define STATUS_NOT_LOCKED ((NTSTATUS)0xC000002A)

#define KernelMode 0
#define IO_NO_INCREMENT 0
#define PASSIVE_LEVEL 0
#define OBJ_KERNEL_HANDLE 0x00000200
#define MEM_COMMIT 0x00001000
#define MAXULONG64 (~(ULONG64)0)
#define InterlockedExchangeAdd(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define ReadNoFence(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define HandleToUlong(h) ((ULONG)(ULONG_PTR)(h))
#define ULongToHandle(u) ((HANDLE)(ULONG_PTR)(u))
typedef struct _FAST_MUTEX { LONG Unused; } FAST_MUTEX, *PFAST_MUTEX;
#define ExAcquireFastMutex(m) ((void)(m))
#define ExReleaseFastMutex(m) ((void)(m))
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define MEM_MAPPED 0x00040000
#define MEM_TOP_DOWN 0x00100000
#define SEC_COMMIT 0x08000000
#define SECTION_QUERY 0x0001
#define SECTION_MAP_WRITE 0x0002
#define SECTION_MAP_READ 0x0004
#define SECTION_MAP_EXECUTE 0x0008
#define SECTION_EXTEND_SIZE 0x0010
#define SECTION_ALL_ACCESS 0x000F001F
#define STATUS_PROCESS_IS_TERMINATING ((NTSTATUS)0xC000010A)
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#define STATUS_OBJECT_TYPE_MISMATCH ((NTSTATUS)0xC0000024)
#define STATUS_MAPPED_FILE_SIZE_ZERO ((NTSTATUS)0xC000011E)

#define _In_reads_bytes_(x)
#define _Out_writes_bytes_(x)
#define _Inout_updates_bytes_(x)
#define _Inout_opt_
#define NTAPI

#define InitializeObjectAttributes(p, n, a, r, s) \
    do { (p)->Length = sizeof(OBJECT_ATTRIBUTES); (p)->RootDirectory = (r); (p)->ObjectName = (n); \
         (p)->Attributes = (a); (p)->SecurityDescriptor = (s); (p)->SecurityQualityOfService = NULL; } while (0)
#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
typedef long LONG_PTR;

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

static inline PLIST_ENTRY RemoveHeadList(PLIST_ENTRY Head)
{
    PLIST_ENTRY Entry = Head->Flink;

    RemoveEntryList(Entry);
    return Entry;
}

extern _Thread_local jmp_buf *LxHostSehFrame;
extern _Thread_local NTSTATUS LxHostSehCode;
void LxHostCopy(void *Destination, const void *Source, SIZE_T Length);

#undef RtlCopyMemory
#define RtlCopyMemory(d, s, l) LxHostCopy((d), (s), (l))
#define _SEH2_TRY { jmp_buf *LxSehPrevious = LxHostSehFrame; jmp_buf LxSehFrame; \
    LxHostSehFrame = &LxSehFrame; if (setjmp(LxSehFrame) == 0) {
#define _SEH2_EXCEPT(f) LxHostSehFrame = LxSehPrevious; } else { LxHostSehFrame = LxSehPrevious;
#define _SEH2_END } }
#define _SEH2_GetExceptionCode() LxHostSehCode

#include <lxabi.h>

#define LX_TAG              'xcxL'
#define LX_PAGE_SIZE        0x1000ULL
#define LX_ALLOC_GRANULARITY LX_PAGE_SIZE
#define LX_FAULT_CLUSTER    0x10000ULL
#define LX_IO_CHUNK         (64 * 1024)
#define LX_BRK_RESERVE      (1024ULL * 1024 * 1024)
#define LX_MMAP_MIN_ADDR    0x10000ULL
#define LX_MMAP_GAP         (128ULL * 1024 * 1024)
#define LX_STACK_SIZE       (8 * 1024 * 1024)
#define LX_MS_NOEXEC        0x00000008
#define LX_VMA_HUGETLB      0x00020000
#define LX_VMA_DONTDUMP     0x00040000
#define LX_VMA_TOUCHED      0x00080000
#define LX_VMA_WIPEONFORK   0x00100000
#define LX_VMA_DONTFORK     0x00200000
#define LX_VMA_POISON       0x00400000
#define LX_VMA_FORKED       0x00800000
#define LX_VMA_NOMAYWRITE   0x01000000
#define LX_VMA_LOCKONFAULT  0x02000000
#define LX_VMA_LOCKED       0x04000000
#define LX_VMA_LOCK_MASK    (LX_VMA_LOCKED | LX_VMA_LOCKONFAULT)
#define LX_VMA_DIRTY        0x08000000
#define LX_VMA_SHM          0x10000000
#define LX_VMA_VIEW         0x20000000
#define LX_VMA_HEAP         0x40000000
#define LX_VMA_STACK        0x80000000
#define LX_ALIGN_DOWN(v, a) ((v) & ~((ULONG64)(a) - 1))
#define LX_ALIGN_UP(v, a)   (((v) + (ULONG64)(a) - 1) & ~((ULONG64)(a) - 1))

#include "../rtl/include/rtltree.h"

typedef struct _LX_FILE LX_FILE, *PLX_FILE;
typedef struct _LX_MOUNT { ULONG Flags; } LX_MOUNT, *PLX_MOUNT;
typedef struct _LX_PROCESS LX_PROCESS, *PLX_PROCESS;
typedef struct _LX_THREAD LX_THREAD, *PLX_THREAD;
typedef struct _LX_TMPFS_INODE LX_TMPFS_INODE, *PLX_TMPFS_INODE;

#define LX_HOST_USER_LIMIT      0x100000000ULL
#define MM_HIGHEST_USER_ADDRESS ((PVOID)(ULONG_PTR)(LX_HOST_USER_LIMIT - 1))

typedef enum MEM_EXTENDED_PARAMETER_TYPE
{
    MemExtendedParameterInvalidType,
    MemExtendedParameterAddressRequirements,
    MemExtendedParameterNumaNode,
    MemExtendedParameterPartitionHandle,
    MemExtendedParameterUserPhysicalHandle,
    MemExtendedParameterAttributeFlags,
    MemExtendedParameterImageMachine,
    MemExtendedParameterMax
} MEM_EXTENDED_PARAMETER_TYPE, *PMEM_EXTENDED_PARAMETER_TYPE;

typedef struct _MEM_ADDRESS_REQUIREMENTS
{
    PVOID LowestStartingAddress;
    PVOID HighestEndingAddress;
    SIZE_T Alignment;
} MEM_ADDRESS_REQUIREMENTS, *PMEM_ADDRESS_REQUIREMENTS;

#define MEM_EXTENDED_PARAMETER_TYPE_BITS 8

typedef struct MEM_EXTENDED_PARAMETER
{
    struct
    {
        ULONG64 Type : MEM_EXTENDED_PARAMETER_TYPE_BITS;
        ULONG64 Reserved : 64 - MEM_EXTENDED_PARAMETER_TYPE_BITS;
    };
    union
    {
        ULONG64 ULong64;
        PVOID Pointer;
        SIZE_T Size;
        HANDLE Handle;
        ULONG ULong;
    };
} MEM_EXTENDED_PARAMETER, *PMEM_EXTENDED_PARAMETER;

typedef struct _LX_VMA
{
    LIST_ENTRY Link;
    RTL_BALANCED_NODE TreeNode;
    ULONG64 Start;
    ULONG64 End;
    ULONG Prot;
    ULONG Flags;
    PLX_FILE File;
    ULONG64 Offset;
    PVOID Section;
    ULONG64 ViewBase;
    ULONG64 ViewOffset;
    ULONG64 MapEnd;
} LX_VMA, *PLX_VMA;

typedef struct _FILE_END_OF_FILE_INFORMATION
{
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION, *PFILE_END_OF_FILE_INFORMATION;

typedef struct _LX_HUGE_STORE
{
    ULONG Count;
    PVOID *Pages;
    PUCHAR Faulted;
} LX_HUGE_STORE, *PLX_HUGE_STORE;

typedef struct _LX_MEMFD
{
    volatile LONG RefCount;
    volatile LONG Seals;
    volatile LONG WritableMaps;
    HANDLE Owner;
    PLX_HUGE_STORE Huge;
} LX_MEMFD, *PLX_MEMFD;

struct _LX_FILE
{
    volatile LONG RefCount;
    ULONG Flags;
    ULONG Type;
    PSTR Path;
    HANDLE Handle;
    PUCHAR Data;
    BOOLEAN Directory;
    BOOLEAN NtPipe;
    ULONG64 Rdev;
    PLX_MEMFD Memfd;
    PLX_MOUNT Mount;
};

struct _LX_PROCESS
{
    struct _LX_PROCESS *Mm;
    ULONG Pid;
    ULONG NtPid;
    BOOLEAN Exited;
    LIST_ENTRY GlobalLink;
    KEVENT MmLock;
    PKTHREAD MmLockOwner;
    ULONG MmLockDepth;
    LIST_ENTRY Vmas;
    RTL_RB_TREE VmaTree;
    LONG VmaCount;
    LIST_ENTRY Guards;
    ULONG64 BrkBase;
    ULONG64 BrkCurrent;
    ULONG64 BrkCommitted;
    ULONG64 BrkLimit;
    BOOLEAN BrkReserved;
    ULONG MmLockDefault;
    ULONG64 CapEffective;
    LX_RLIMIT Limits[16];
    ULONG Personality;
    ULONG64 MmapTop;
    ULONG64 StackTop;
};

struct _LX_THREAD
{
    PLX_PROCESS Process;
    ULONG64 FaultAddress;
    ULONG FaultRetries;
    UCHAR FaultBus;
};

typedef struct _LX_CALL
{
    PLX_THREAD Thread;
    PLX_PROCESS Process;
    ULONG Number;
    ULONG64 Args[6];
} LX_CALL, *PLX_CALL;

#define LX_SYSCALL(Name) LONG64 Name(_Inout_ PLX_CALL Call)

#define KeGetCurrentThread LxHostKeGetCurrentThread
#define KeEnterCriticalRegion LxHostKeEnterCriticalRegion
#define KeLeaveCriticalRegion LxHostKeLeaveCriticalRegion
#define KeGetCurrentIrql LxHostKeGetCurrentIrql
#define KeAreAllApcsDisabled LxHostKeAreAllApcsDisabled
#define KeInitializeEvent LxHostKeInitializeEvent
#define KeWaitForSingleObject LxHostKeWaitForSingleObject
#define KeSetEvent LxHostKeSetEvent
#define KeStackAttachProcess LxHostKeStackAttachProcess
#define KeUnstackDetachProcess LxHostKeUnstackDetachProcess
#define IoFileObjectType LxHostIoFileObjectType
#define ObReferenceObject LxHostObReferenceObject
#define ObDereferenceObject LxHostObDereferenceObject
#define ObOpenObjectByPointer LxHostObOpenObjectByPointer
#define ObReferenceObjectByHandle LxHostObReferenceObjectByHandle
#define ZwAllocateVirtualMemory LxHostZwAllocateVirtualMemory
#define ZwAllocateVirtualMemoryEx LxHostZwAllocateVirtualMemoryEx
#define ZwFreeVirtualMemory LxHostZwFreeVirtualMemory
#define ZwProtectVirtualMemory LxHostZwProtectVirtualMemory
#define ZwQueryVirtualMemory LxHostZwQueryVirtualMemory
#define ZwFlushInstructionCache LxHostZwFlushInstructionCache
#define ZwFlushVirtualMemory LxHostZwFlushVirtualMemory
#define ZwLockVirtualMemory LxHostZwLockVirtualMemory
#define ZwUnlockVirtualMemory LxHostZwUnlockVirtualMemory
#define ZwQueryInformationProcess LxHostZwQueryInformationProcess
#define ZwSetInformationProcess LxHostZwSetInformationProcess
#define ZwCreateSection LxHostZwCreateSection
#define ZwMapViewOfSection LxHostZwMapViewOfSection
#define ZwUnmapViewOfSection LxHostZwUnmapViewOfSection
#define ZwQuerySection LxHostZwQuerySection
#define ZwQueryInformationFile LxHostZwQueryInformationFile
#define ZwClose LxHostZwClose
#define ZwWriteFile LxHostZwWriteFile
#define ZwSetInformationFile LxHostZwSetInformationFile
#define MmGetPhysicalAddress LxHostMmGetPhysicalAddress

PKTHREAD KeGetCurrentThread(void);
void KeEnterCriticalRegion(void);
void KeLeaveCriticalRegion(void);
KIRQL KeGetCurrentIrql(void);
BOOLEAN KeAreAllApcsDisabled(void);
void KeInitializeEvent(PKEVENT Event, EVENT_TYPE Type, BOOLEAN State);
NTSTATUS KeWaitForSingleObject(PVOID Object, KWAIT_REASON Reason, KPROCESSOR_MODE Mode, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
LONG KeSetEvent(PKEVENT Event, LONG Increment, BOOLEAN Wait);
void KeStackAttachProcess(PKPROCESS Process, PKAPC_STATE ApcState);
void KeUnstackDetachProcess(PKAPC_STATE ApcState);

extern POBJECT_TYPE *IoFileObjectType;
void ObReferenceObject(PVOID Object);
void ObDereferenceObject(PVOID Object);
NTSTATUS ObOpenObjectByPointer(PVOID Object, ULONG Attributes, PVOID AccessState, ACCESS_MASK Access, POBJECT_TYPE Type,
                               KPROCESSOR_MODE Mode, PHANDLE Handle);
NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, ACCESS_MASK Access, POBJECT_TYPE Type, KPROCESSOR_MODE Mode,
                                   PVOID *Object, PVOID Information);

NTSTATUS ZwAllocateVirtualMemory(HANDLE Process, PVOID *BaseAddress, ULONG_PTR ZeroBits, PSIZE_T RegionSize,
                                 ULONG AllocationType, ULONG Protect);
NTSTATUS ZwAllocateVirtualMemoryEx(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG AllocationType,
                                   ULONG PageProtection, PMEM_EXTENDED_PARAMETER ExtendedParameters,
                                   ULONG ExtendedParameterCount);
NTSTATUS ZwFreeVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG FreeType);
NTSTATUS ZwProtectVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG NewProtect,
                                PULONG OldProtect);
NTSTATUS ZwQueryVirtualMemory(HANDLE Process, PVOID BaseAddress, MEMORY_INFORMATION_CLASS Class, PVOID Information,
                              SIZE_T Length, PSIZE_T ReturnLength);
NTSTATUS ZwFlushInstructionCache(HANDLE Process, PVOID BaseAddress, SIZE_T Length);
NTSTATUS ZwFlushVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, PIO_STATUS_BLOCK Iosb);
NTSTATUS ZwLockVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG MapType);
NTSTATUS ZwUnlockVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG MapType);
NTSTATUS ZwQueryInformationProcess(HANDLE Process, PROCESSINFOCLASS Class, PVOID Information, ULONG Length,
                                   PULONG ReturnLength);
NTSTATUS ZwSetInformationProcess(HANDLE Process, PROCESSINFOCLASS Class, PVOID Information, ULONG Length);
extern SIZE_T LxHostWsMinimum;
extern ULONG LxHostQuotaRaises;
NTSTATUS ZwSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK Iosb, PVOID Information, ULONG Length,
                              FILE_INFORMATION_CLASS Class);
LARGE_INTEGER MmGetPhysicalAddress(PVOID BaseAddress);
NTSTATUS ZwWriteFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK Iosb,
                     PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);
NTSTATUS ZwCreateSection(PHANDLE Section, ACCESS_MASK Access, POBJECT_ATTRIBUTES Attributes, PLARGE_INTEGER MaximumSize,
                         ULONG PageProtection, ULONG AllocationAttributes, HANDLE File);
NTSTATUS ZwMapViewOfSection(HANDLE Section, HANDLE Process, PVOID *BaseAddress, ULONG_PTR ZeroBits, SIZE_T CommitSize,
                            PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, SECTION_INHERIT Inherit,
                            ULONG AllocationType, ULONG Protect);
NTSTATUS ZwUnmapViewOfSection(HANDLE Process, PVOID BaseAddress);
NTSTATUS ZwQuerySection(HANDLE Section, SECTION_INFORMATION_CLASS Class, PVOID Information, SIZE_T Length,
                        PSIZE_T ReturnLength);
NTSTATUS ZwQueryInformationFile(HANDLE File, PIO_STATUS_BLOCK Iosb, PVOID Information, ULONG Length,
                                FILE_INFORMATION_CLASS Class);
NTSTATUS ZwClose(HANDLE Handle);

PVOID LxAlloc(_In_ SIZE_T Size);
VOID LxFree(_In_opt_ PVOID Buffer);
PVOID LxAllocPaged(_In_ SIZE_T Size);
VOID LxFreePaged(_In_opt_ PVOID Buffer);
LONG LxStatusToErrno(_In_ NTSTATUS Status);
LONG64 LxCopyToUser(_In_ ULONG64 Destination, _In_reads_bytes_(Length) const VOID *Source, _In_ SIZE_T Length);
extern LIST_ENTRY LxProcessList;
extern FAST_MUTEX LxProcessListLock;
VOID LxProcessReference(_In_ PLX_PROCESS Process);
VOID LxProcessDereference(_In_ PLX_PROCESS Process);
HANDLE PsGetCurrentProcessId(VOID);
NTSTATUS PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS *Process);
LONG64 LxCopyFromUser(_Out_writes_bytes_(Length) PVOID Destination, _In_ ULONG64 Source, _In_ SIZE_T Length);
BOOLEAN LxCapable(_In_ PLX_PROCESS Process, _In_ ULONG Capability);
LONG64 LxZeroUser(_In_ ULONG64 Destination, _In_ SIZE_T Length);
PLX_THREAD LxCurrentThread(VOID);
VOID LxFileReference(_In_ PLX_FILE File);
VOID LxFileDereference(_In_ PLX_FILE File);
PLX_FILE LxFdGet(_In_ PLX_PROCESS Process, _In_ LONG Fd);
LONG64 LxFileReadKernel(_In_ PLX_FILE File, _Out_writes_bytes_(Length) PVOID Buffer, _In_ ULONG Length, _In_ ULONG64 Offset);
VOID LxIpcShmVmaOpen(_In_ PVOID Section);
VOID LxIpcShmVmaClose(_In_ PVOID Section);
BOOLEAN LxDrmIsFile(_In_ PLX_FILE File);
LONG64 LxDrmMmap(_In_ PLX_PROCESS Process, _In_ PLX_FILE File, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot,
                 _In_ ULONG Flags, _In_ ULONG64 Offset);

LONG64 LxMmMap(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot, _In_ ULONG Flags, _In_opt_ PLX_FILE File, _In_ ULONG64 Offset);
LONG64 LxMmUnmap(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 Length);
LONG64 LxMmProtect(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ ULONG Prot);
VOID LxMmReset(_In_ PLX_PROCESS Process);
VOID LxMmLock(_In_ PLX_PROCESS Process);
VOID LxMmUnlock(_In_ PLX_PROCESS Process);
LONG LxMmFork(_In_ PLX_PROCESS Child, _In_ PLX_PROCESS Parent);
VOID LxMmRelease(_In_ PLX_PROCESS Process);
ULONG LxMmReleaseKeyedViews(_In_ PVOID Key, _In_opt_ VOID (*Visit)(_In_ PVOID Context, _In_ PLX_PROCESS Process),
                            _In_opt_ PVOID Context);
ULONG64 LxTmpfsInodeSize(_In_ PLX_TMPFS_INODE Inode);
LONG LxTmpfsInodeRemoveRange(_In_ PLX_TMPFS_INODE Inode, _In_ ULONG64 Start, _In_ ULONG64 End);
PLX_TMPFS_INODE LxTmpfsFileInode(_In_opt_ PLX_FILE File);
NTSTATUS LxFillRandom(_Out_writes_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length);
VOID LxArchMmapRndBits(_Out_ PULONG Minimum, _Out_ PULONG Maximum);
ULONG64 LxArchStackRndMask(VOID);
NTSTATUS LxTmpfsMapSection(_In_ PLX_TMPFS_INODE Inode, _In_ BOOLEAN Shared, _Out_ PHANDLE Section, _Out_opt_ PULONG64 Size);
BOOLEAN LxMmHandleFault(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG Access);
BOOLEAN LxMmFaultIn(_In_ ULONG64 Address, _In_ SIZE_T Length, _In_ BOOLEAN Write);
LONG LxMmZero(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 End);
ULONG LxProtToNt(_In_ ULONG Prot);
BOOLEAN LxMmSharedKey(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _Out_ PVOID *Object, _Out_ PULONG64 Offset);
LONG LxMmPrepareRemote(_In_ PLX_PROCESS Process, _In_ PEPROCESS EProcess, _In_ ULONG64 Address, _In_ ULONG64 Length, _In_ BOOLEAN Write);
LX_SYSCALL(LxSysMmap);
LX_SYSCALL(LxSysMunmap);
LX_SYSCALL(LxSysMprotect);
LX_SYSCALL(LxSysMadvise);
LX_SYSCALL(LxSysMremap);
LX_SYSCALL(LxSysMsync);
LX_SYSCALL(LxSysMlock);
LX_SYSCALL(LxSysMlock2);
LX_SYSCALL(LxSysMunlock);
LX_SYSCALL(LxSysMlockall);
LX_SYSCALL(LxSysMunlockall);
ULONG64 LxMmLockedBytes(_In_ PLX_PROCESS Process);
VOID LxMmForkChild(_In_ PLX_PROCESS Parent, _In_ PLX_PROCESS Child, _In_ HANDLE ProcessHandle);
ULONG64 LxHugePageSize(VOID);
LONG LxHugeStoreReserve(_Inout_ PLX_HUGE_STORE Store, _In_ ULONG64 First, _In_ ULONG64 Count, _In_ BOOLEAN Surplus);
PVOID LxHugeStorePage(_In_ PLX_HUGE_STORE Store, _In_ ULONG64 Index);
VOID LxHugeStoreFault(_In_ PLX_HUGE_STORE Store, _In_ ULONG64 Index);
NTSTATUS LxHugeMapPage(_In_ PVOID Section, _In_ HANDLE ProcessHandle, _Inout_ PVOID *Base, _In_ ULONG Protect);
NTSTATUS LxHugeCopyPage(_In_ PVOID From, _In_ PVOID To);
PLX_FILE LxHugeFileCreate(_In_ PCSTR Path);
extern volatile LONG LxMaxMapCount;
LONG64 LxMmMincore(_In_ PLX_PROCESS Process, _In_ ULONG64 Address, _In_ ULONG64 End, _In_ ULONG64 Output);
ULONG64 LxMmVmaResident(_In_ HANDLE ProcessHandle, _In_ PLX_VMA Vma);
LX_SYSCALL(LxSysBrk);
