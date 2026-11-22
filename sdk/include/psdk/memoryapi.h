/*
 * PROJECT:     LiberNT PSDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Memory management API set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _MEMORYAPI_H_
#define _MEMORYAPI_H_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_MAP_COPY SECTION_QUERY
#define FILE_MAP_WRITE SECTION_MAP_WRITE
#define FILE_MAP_READ SECTION_MAP_READ
#define FILE_MAP_ALL_ACCESS SECTION_ALL_ACCESS
#define FILE_MAP_EXECUTE SECTION_MAP_EXECUTE_EXPLICIT

HANDLE WINAPI CreateFileMappingW(HANDLE,LPSECURITY_ATTRIBUTES,DWORD,DWORD,DWORD,LPCWSTR);
BOOL WINAPI FlushViewOfFile(LPCVOID,SIZE_T);
SIZE_T WINAPI GetLargePageMinimum(void);
#if (_WIN32_WINNT >= 0x0502)
#define FILE_CACHE_FLAGS_DEFINED
#define FILE_CACHE_MAX_HARD_ENABLE 0x00000001
#define FILE_CACHE_MAX_HARD_DISABLE 0x00000002
#define FILE_CACHE_MIN_HARD_ENABLE 0x00000004
#define FILE_CACHE_MIN_HARD_DISABLE 0x00000008
BOOL WINAPI GetSystemFileCacheSize(_Out_ PSIZE_T, _Out_ PSIZE_T, _Out_ PDWORD);
#endif
UINT WINAPI GetWriteWatch(DWORD,PVOID,SIZE_T,PVOID*,PULONG_PTR,PULONG);
PVOID WINAPI MapViewOfFile(HANDLE,DWORD,DWORD,DWORD,SIZE_T);
PVOID WINAPI MapViewOfFileEx(HANDLE,DWORD,DWORD,DWORD,SIZE_T,PVOID);
HANDLE WINAPI OpenFileMappingW(DWORD,BOOL,LPCWSTR);
BOOL WINAPI ReadProcessMemory(HANDLE,LPCVOID,LPVOID,SIZE_T,PSIZE_T);
UINT WINAPI ResetWriteWatch(LPVOID,SIZE_T);
BOOL WINAPI SetProcessWorkingSetSizeEx(_In_ HANDLE, _In_ SIZE_T, _In_ SIZE_T, _In_ DWORD);
#if (_WIN32_WINNT >= 0x0502)
BOOL WINAPI SetSystemFileCacheSize(_In_ SIZE_T, _In_ SIZE_T, _In_ DWORD);
#endif
BOOL WINAPI UnmapViewOfFile(LPCVOID);
PVOID WINAPI VirtualAlloc(PVOID,SIZE_T,DWORD,DWORD);
PVOID WINAPI VirtualAllocEx(HANDLE,PVOID,SIZE_T,DWORD,DWORD);
BOOL WINAPI VirtualFree(PVOID,SIZE_T,DWORD);
BOOL WINAPI VirtualFreeEx(HANDLE,PVOID,SIZE_T,DWORD);
BOOL WINAPI VirtualLock(PVOID,SIZE_T);
BOOL WINAPI VirtualProtect(PVOID,SIZE_T,DWORD,PDWORD);
BOOL WINAPI VirtualProtectEx(HANDLE,PVOID,SIZE_T,DWORD,PDWORD);
SIZE_T WINAPI VirtualQuery(LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T);
SIZE_T WINAPI VirtualQueryEx(HANDLE,LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T);
BOOL WINAPI VirtualUnlock(PVOID,SIZE_T);
BOOL WINAPI WriteProcessMemory(HANDLE,LPVOID,LPCVOID,SIZE_T,SIZE_T*);

#if (NTDDI_VERSION >= NTDDI_WIN10_RS4)
WINBASEAPI
PVOID
WINAPI
VirtualAlloc2(
  _In_opt_ HANDLE Process,
  _In_opt_ PVOID BaseAddress,
  _In_ SIZE_T Size,
  _In_ ULONG AllocationType,
  _In_ ULONG PageProtection,
  _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER *ExtendedParameters,
  _In_ ULONG ParameterCount);

WINBASEAPI
PVOID
WINAPI
MapViewOfFile3(
  _In_ HANDLE FileMapping,
  _In_opt_ HANDLE Process,
  _In_opt_ PVOID BaseAddress,
  _In_ ULONG64 Offset,
  _In_ SIZE_T ViewSize,
  _In_ ULONG AllocationType,
  _In_ ULONG PageProtection,
  _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER *ExtendedParameters,
  _In_ ULONG ParameterCount);

WINBASEAPI
PVOID
WINAPI
VirtualAlloc2FromApp(
  _In_opt_ HANDLE Process,
  _In_opt_ PVOID BaseAddress,
  _In_ SIZE_T Size,
  _In_ ULONG AllocationType,
  _In_ ULONG PageProtection,
  _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER *ExtendedParameters,
  _In_ ULONG ParameterCount);
#endif

#if (NTDDI_VERSION >= NTDDI_WIN10_RS1)
typedef enum WIN32_MEMORY_INFORMATION_CLASS {
  MemoryRegionInfo
} WIN32_MEMORY_INFORMATION_CLASS;

WINBASEAPI
BOOL
WINAPI
QueryVirtualMemoryInformation(
  _In_ HANDLE Process,
  _In_ const VOID *VirtualAddress,
  _In_ WIN32_MEMORY_INFORMATION_CLASS MemoryInformationClass,
  _Out_writes_bytes_(MemoryInformationSize) PVOID MemoryInformation,
  _In_ SIZE_T MemoryInformationSize,
  _Out_opt_ PSIZE_T ReturnSize);
#endif

WINBASEAPI
BOOL
WINAPI
UnmapViewOfFile2(
  _In_ HANDLE Process,
  _In_ PVOID BaseAddress,
  _In_ ULONG UnmapFlags);

#if (_WIN32_WINNT >= 0x0602)
WINBASEAPI
HANDLE
WINAPI
CreateFileMappingFromApp(
  _In_ HANDLE hFile,
  _In_opt_ PSECURITY_ATTRIBUTES SecurityAttributes,
  _In_ ULONG PageProtection,
  _In_ ULONG64 MaximumSize,
  _In_opt_ PCWSTR Name);

WINBASEAPI
PVOID
WINAPI
MapViewOfFileFromApp(
  _In_ HANDLE hFileMappingObject,
  _In_ ULONG DesiredAccess,
  _In_ ULONG64 FileOffset,
  _In_ SIZE_T NumberOfBytesToMap);
#endif

#if (_WIN32_WINNT >= 0x0A00)
WINBASEAPI
PVOID
WINAPI
VirtualAllocFromApp(
  _In_opt_ PVOID BaseAddress,
  _In_ SIZE_T Size,
  _In_ ULONG AllocationType,
  _In_ ULONG Protection);

WINBASEAPI
HANDLE
WINAPI
OpenFileMappingFromApp(
  _In_ ULONG DesiredAccess,
  _In_ BOOL InheritHandle,
  _In_ PCWSTR Name);
#endif

#if (_WIN32_WINNT >= 0x0501)
typedef enum {
	LowMemoryResourceNotification ,
	HighMemoryResourceNotification
} MEMORY_RESOURCE_NOTIFICATION_TYPE;

HANDLE WINAPI CreateMemoryResourceNotification(MEMORY_RESOURCE_NOTIFICATION_TYPE);
BOOL WINAPI QueryMemoryResourceNotification(HANDLE,PBOOL);
#endif

#if (_WIN32_WINNT >= 0x0602)
BOOL WINAPI UnmapViewOfFileEx(_In_ PVOID, _In_ ULONG);

typedef struct _WIN32_MEMORY_RANGE_ENTRY {
  PVOID VirtualAddress;
  SIZE_T NumberOfBytes;
} WIN32_MEMORY_RANGE_ENTRY, *PWIN32_MEMORY_RANGE_ENTRY;

WINBASEAPI
BOOL
WINAPI
PrefetchVirtualMemory(
  _In_ HANDLE hProcess,
  _In_ ULONG_PTR NumberOfEntries,
  _In_reads_(NumberOfEntries) PWIN32_MEMORY_RANGE_ENTRY VirtualAddresses,
  _In_ ULONG Flags);
#endif

#if (_WIN32_WINNT >= 0x0500)

BOOL
WINAPI
AllocateUserPhysicalPages(
  _In_ HANDLE hProcess,
  _Inout_ PULONG_PTR NumberOfPages,
  _Out_writes_to_(*NumberOfPages, *NumberOfPages) PULONG_PTR PageArray);

BOOL
WINAPI
FreeUserPhysicalPages(
  _In_ HANDLE hProcess,
  _Inout_ PULONG_PTR NumberOfPages,
  _In_reads_(*NumberOfPages) PULONG_PTR PageArray);

BOOL
WINAPI
MapUserPhysicalPages(
  _In_ PVOID VirtualAddress,
  _In_ ULONG_PTR NumberOfPages,
  _In_reads_opt_(NumberOfPages) PULONG_PTR PageArray);
#endif

#ifdef __cplusplus
}
#endif

#ifndef FILE_MAP_LARGE_PAGES
#define FILE_MAP_LARGE_PAGES 0x20000000
#endif
#ifndef FILE_MAP_RESERVE
#define FILE_MAP_RESERVE 0x80000000
#endif
#ifndef FILE_MAP_TARGETS_INVALID
#define FILE_MAP_TARGETS_INVALID 0x40000000
#endif
#ifndef MEHC_PATROL_SCRUBBER_PRESENT
#define MEHC_PATROL_SCRUBBER_PRESENT 0x1
#endif

typedef enum OFFER_PRIORITY {
    VmOfferPriorityVeryLow = 1,
    VmOfferPriorityLow = 2,
    VmOfferPriorityBelowNormal = 3,
    VmOfferPriorityNormal = 4
} OFFER_PRIORITY;

typedef enum WIN32_MEMORY_PARTITION_INFORMATION_CLASS {
    MemoryPartitionInfo = 0,
    MemoryPartitionDedicatedMemoryInfo = 1
} WIN32_MEMORY_PARTITION_INFORMATION_CLASS;

#ifdef __cplusplus
extern "C" {
#endif
HANDLE WINAPI CreateFileMapping2(HANDLE File, SECURITY_ATTRIBUTES * SecurityAttributes, ULONG DesiredAccess, ULONG PageProtection, ULONG AllocationAttributes, ULONG64 MaximumSize, PCWSTR Name, MEM_EXTENDED_PARAMETER * ExtendedParameters, ULONG ParameterCount);
DWORD WINAPI DiscardVirtualMemory(PVOID VirtualAddress, SIZE_T Size);
#ifdef __cplusplus
}
#endif

typedef struct WIN32_MEMORY_PARTITION_INFORMATION {
  ULONG Flags;
  ULONG NumaNode;
  ULONG Channel;
  ULONG NumberOfNumaNodes;
  ULONG64 ResidentAvailablePages;
  ULONG64 CommittedPages;
  ULONG64 CommitLimit;
  ULONG64 PeakCommitment;
  ULONG64 TotalNumberOfPages;
  ULONG64 AvailablePages;
  ULONG64 ZeroPages;
  ULONG64 FreePages;
  ULONG64 StandbyPages;
  ULONG64 Reserved[16];
  ULONG64 MaximumCommitLimit;
  ULONG64 Reserved2;
  ULONG PartitionId;
} WIN32_MEMORY_PARTITION_INFORMATION;

typedef void WINAPI BAD_MEMORY_CALLBACK_ROUTINE(void);

struct NUMA_NODE_MEMORY_PERFORMANCE_MEASUREMENT_CONDITION
{
    struct
    {
        UCHAR MinTransferSizeToAchieveValues : 1;
        UCHAR NonSequentialTransfers : 1;
        UCHAR Reserved : 6;
    } Flags;
    ULONGLONG MinTransferSizeInBytes;
};

struct NUMA_NODE_MEMORY_PERFORMANCE_BANDWIDTH
{
    NUMA_NODE_MEMORY_PERFORMANCE_MEASUREMENT_CONDITION Condition;
    ULONGLONG Bandwidth;
};

struct NUMA_NODE_MEMORY_PERFORMANCE_LATENCY
{
    NUMA_NODE_MEMORY_PERFORMANCE_MEASUREMENT_CONDITION Condition;
    ULONGLONG Latency;
};

typedef BAD_MEMORY_CALLBACK_ROUTINE *PBAD_MEMORY_CALLBACK_ROUTINE;

struct WIN32_MEMORY_REGION_INFORMATION
{
    PVOID AllocationBase;
    ULONG AllocationProtect;
    union
    {
        ULONG Flags;
        struct
        {
            ULONG Private : 1;
            ULONG MappedDataFile : 1;
            ULONG MappedImage : 1;
            ULONG MappedPageFile : 1;
            ULONG MappedPhysical : 1;
            ULONG DirectMapped : 1;
            ULONG Reserved : 26;
        };
    };
    SIZE_T RegionSize;
    SIZE_T CommitSize;
};

#ifdef __cplusplus
extern "C" {
#endif

BOOL WINAPI AllocateUserPhysicalPagesNuma(HANDLE hProcess, PULONG_PTR NumberOfPages, PULONG_PTR PageArray, DWORD nndPreferred);
HANDLE WINAPI CreateFileMappingNumaW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName, DWORD nndPreferred);
BOOL WINAPI GetProcessWorkingSetSizeEx(HANDLE hProcess, PSIZE_T lpMinimumWorkingSetSize, PSIZE_T lpMaximumWorkingSetSize, PDWORD Flags);
BOOL WINAPI SetProcessValidCallTargets(HANDLE hProcess, PVOID VirtualAddress, SIZE_T RegionSize, ULONG NumberOfOffsets, PCFG_CALL_TARGET_INFO OffsetInformation);
LPVOID WINAPI VirtualAllocExNuma(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect, DWORD nndPreferred);
BOOL WINAPI VirtualProtectFromApp(PVOID Address, SIZE_T Size, ULONG NewProtection, PULONG OldProtection);

/* TODO: LiberNT does not implement the functions below yet */
BOOL AllocateUserPhysicalPages2(HANDLE ObjectHandle, PULONG_PTR NumberOfPages, PULONG_PTR PageArray, PMEM_EXTENDED_PARAMETER ExtendedParameters, ULONG ExtendedParameterCount);
BOOL GetMemoryErrorHandlingCapabilities(PULONG Capabilities);
BOOL GetNumaNodeMemoryClosestInitiatorNode(USHORT TargetNodeNumber, USHORT *InitiatorNodeNumber);
BOOL WINAPI GetNumaNodeMemoryReadBandwidth(USHORT TargetNodeNumber, USHORT InitiatorNodeNumber, NUMA_NODE_MEMORY_PERFORMANCE_BANDWIDTH *Bandwidth);
BOOL WINAPI GetNumaNodeMemoryReadLatency(USHORT TargetNodeNumber, USHORT InitiatorNodeNumber, NUMA_NODE_MEMORY_PERFORMANCE_LATENCY *Latency);
BOOL WINAPI GetNumaNodeMemoryWriteBandwidth(USHORT TargetNodeNumber, USHORT InitiatorNodeNumber, NUMA_NODE_MEMORY_PERFORMANCE_BANDWIDTH *Bandwidth);
BOOL WINAPI GetNumaNodeMemoryWriteLatency(USHORT TargetNodeNumber, USHORT InitiatorNodeNumber, NUMA_NODE_MEMORY_PERFORMANCE_LATENCY *Latency);
#if (!defined(MIDL_PASS))
PVOID MapViewOfFile2(HANDLE FileMappingHandle, HANDLE ProcessHandle, ULONG64 Offset, PVOID BaseAddress, SIZE_T ViewSize, ULONG AllocationType, ULONG PageProtection);
#endif
PVOID MapViewOfFile3FromApp(HANDLE FileMapping, HANDLE Process, PVOID BaseAddress, ULONG64 Offset, SIZE_T ViewSize, ULONG AllocationType, ULONG PageProtection, MEM_EXTENDED_PARAMETER *ExtendedParameters, ULONG ParameterCount);
PVOID MapViewOfFileNuma2(HANDLE FileMappingHandle, HANDLE ProcessHandle, ULONG64 Offset, PVOID BaseAddress, SIZE_T ViewSize, ULONG AllocationType, ULONG PageProtection, ULONG PreferredNode);
DWORD WINAPI OfferVirtualMemory(PVOID VirtualAddress, SIZE_T Size, OFFER_PRIORITY Priority);
HANDLE OpenDedicatedMemoryPartition(HANDLE Partition, ULONG64 DedicatedMemoryTypeId, ACCESS_MASK DesiredAccess, BOOL InheritHandle);
BOOL QueryPartitionInformation(HANDLE Partition, WIN32_MEMORY_PARTITION_INFORMATION_CLASS PartitionInformationClass, PVOID PartitionInformation, ULONG PartitionInformationLength);
DWORD WINAPI ReclaimVirtualMemory(const void *VirtualAddress, SIZE_T Size);
PVOID RegisterBadMemoryNotification(PBAD_MEMORY_CALLBACK_ROUTINE Callback);
BOOL SetProcessValidCallTargetsForMappedView(HANDLE Process, PVOID VirtualAddress, SIZE_T RegionSize, ULONG NumberOfOffsets, PCFG_CALL_TARGET_INFO OffsetInformation, HANDLE Section, ULONG64 ExpectedFileOffset);
BOOL UnregisterBadMemoryNotification(PVOID RegistrationHandle);
BOOL VirtualUnlockEx(HANDLE Process, LPVOID Address, SIZE_T Size);

#ifdef __cplusplus
}
#endif

enum _MEMORY_RESOURCE_NOTIFICATION_TYPE
{

};

#endif
