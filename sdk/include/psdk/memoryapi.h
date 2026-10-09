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
