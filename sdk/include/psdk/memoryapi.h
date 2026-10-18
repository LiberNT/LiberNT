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
UINT WINAPI GetWriteWatch(DWORD,PVOID,SIZE_T,PVOID*,PULONG_PTR,PULONG);
PVOID WINAPI MapViewOfFile(HANDLE,DWORD,DWORD,DWORD,SIZE_T);
PVOID WINAPI MapViewOfFileEx(HANDLE,DWORD,DWORD,DWORD,SIZE_T,PVOID);
HANDLE WINAPI OpenFileMappingW(DWORD,BOOL,LPCWSTR);
BOOL WINAPI ReadProcessMemory(HANDLE,LPCVOID,LPVOID,SIZE_T,PSIZE_T);
UINT WINAPI ResetWriteWatch(LPVOID,SIZE_T);
BOOL WINAPI SetProcessWorkingSetSizeEx(_In_ HANDLE, _In_ SIZE_T, _In_ SIZE_T, _In_ DWORD);
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

#endif
