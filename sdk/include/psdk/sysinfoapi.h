/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     API definitions for api-ms-win-core-sysinfo-l1
 * COPYRIGHT:   Copyright 2024 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#pragma once

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SYSTEM_INFO {
	_ANONYMOUS_UNION union {
		DWORD dwOemId;
		_ANONYMOUS_STRUCT struct {
			WORD wProcessorArchitecture;
			WORD wReserved;
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME;
	DWORD dwPageSize;
	PVOID lpMinimumApplicationAddress;
	PVOID lpMaximumApplicationAddress;
	DWORD_PTR dwActiveProcessorMask;
	DWORD dwNumberOfProcessors;
	DWORD dwProcessorType;
	DWORD dwAllocationGranularity;
	WORD wProcessorLevel;
	WORD wProcessorRevision;
} SYSTEM_INFO,*LPSYSTEM_INFO;

#if (_WIN32_WINNT >= 0x0500)
typedef struct _MEMORYSTATUSEX {
	DWORD dwLength;
	DWORD dwMemoryLoad;
	DWORDLONG ullTotalPhys;
	DWORDLONG ullAvailPhys;
	DWORDLONG ullTotalPageFile;
	DWORDLONG ullAvailPageFile;
	DWORDLONG ullTotalVirtual;
	DWORDLONG ullAvailVirtual;
	DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;
typedef enum _COMPUTER_NAME_FORMAT {
	ComputerNameNetBIOS,
	ComputerNameDnsHostname,
	ComputerNameDnsDomain,
	ComputerNameDnsFullyQualified,
	ComputerNamePhysicalNetBIOS,
	ComputerNamePhysicalDnsHostname,
	ComputerNamePhysicalDnsDomain,
	ComputerNamePhysicalDnsFullyQualified,
	ComputerNameMax
} COMPUTER_NAME_FORMAT;
BOOL WINAPI GetComputerNameExA(COMPUTER_NAME_FORMAT,LPSTR,LPDWORD);
BOOL WINAPI GetComputerNameExW(COMPUTER_NAME_FORMAT,LPWSTR,LPDWORD);
#endif

#if (_WIN32_WINNT >= 0x0601)
BOOL
WINAPI
GetLogicalProcessorInformationEx(
  _In_ LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
  _Out_writes_bytes_to_opt_(*ReturnedLength, *ReturnedLength) PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer,
  _Inout_ PDWORD ReturnedLength);
#endif

void WINAPI GetLocalTime(LPSYSTEMTIME);

#if (_WIN32_WINNT >= 0x0501)
VOID WINAPI GetNativeSystemInfo(LPSYSTEM_INFO);
#endif

#if (_WIN32_WINNT >= 0x0501)
#if (_WIN32_WINNT >= 0x0602)
BOOL WINAPI GetOsSafeBootMode(_Out_ PDWORD);
#endif
#endif

UINT WINAPI GetSystemDirectoryA(LPSTR,UINT);
UINT WINAPI GetSystemDirectoryW(LPWSTR,UINT);
VOID WINAPI GetSystemInfo(LPSYSTEM_INFO);
VOID WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime);
BOOL WINAPI GetSystemTimeAdjustment(PDWORD,PDWORD,PBOOL);
void WINAPI GetSystemTimeAsFileTime(LPFILETIME);

#if (_WIN32_WINNT >= 0x0500)
UINT WINAPI GetSystemWindowsDirectoryA(LPSTR,UINT);
UINT WINAPI GetSystemWindowsDirectoryW(LPWSTR,UINT);
#endif

DWORD WINAPI GetTickCount(VOID);

#if (_WIN32_WINNT >= 0x0600)
ULONGLONG WINAPI GetTickCount64(VOID);
#endif

DWORD WINAPI GetVersion(void);
BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA);
BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW);
UINT WINAPI GetWindowsDirectoryA(LPSTR,UINT);
UINT WINAPI GetWindowsDirectoryW(LPWSTR,UINT);

#if (_WIN32_WINNT >= 0x0500)
BOOL WINAPI GlobalMemoryStatusEx(LPMEMORYSTATUSEX);
#endif

BOOL WINAPI SetComputerNameA(_In_ LPCSTR);
BOOL WINAPI SetComputerNameW(_In_ LPCWSTR);

#if (_WIN32_WINNT >= 0x0500)
BOOL WINAPI SetComputerNameExA(_In_ COMPUTER_NAME_FORMAT, _In_ LPCSTR);
BOOL WINAPI SetComputerNameExW(COMPUTER_NAME_FORMAT,LPCWSTR);
#endif

#if (_WIN32_WINNT >= 0x0502)
WINBASEAPI
UINT
WINAPI
EnumSystemFirmwareTables(
    _In_ DWORD FirmwareTableProviderSignature,
    _Out_writes_bytes_to_opt_(BufferSize, return) PVOID pFirmwareTableEnumBuffer,
    _In_ DWORD BufferSize);
WINBASEAPI
UINT
WINAPI
GetSystemFirmwareTable(
    _In_ DWORD FirmwareTableProviderSignature,
    _In_ DWORD FirmwareTableID,
    _Out_writes_bytes_to_opt_(BufferSize, return) PVOID pFirmwareTableBuffer,
    _In_ DWORD BufferSize);
#endif

BOOL WINAPI SetLocalTime(const SYSTEMTIME*);
BOOL WINAPI SetSystemTime(const SYSTEMTIME*);
BOOL WINAPI SetSystemTimeAdjustment(_In_ DWORD, _In_ BOOL);

#ifdef UNICODE
#if (_WIN32_WINNT >= 0x0500)
#define GetComputerNameEx GetComputerNameExW
#endif
#define GetSystemDirectory GetSystemDirectoryW
#if (_WIN32_WINNT >= 0x0500)
#define GetSystemWindowsDirectory GetSystemWindowsDirectoryW
#endif
#define GetVersionEx GetVersionExW
#define GetWindowsDirectory GetWindowsDirectoryW
#define SetComputerName SetComputerNameW
#define SetComputerNameEx SetComputerNameExW
#else
#if (_WIN32_WINNT >= 0x0500)
#define GetComputerNameEx GetComputerNameExA
#endif
#define GetSystemDirectory GetSystemDirectoryA
#if (_WIN32_WINNT >= 0x0500)
#define GetSystemWindowsDirectory GetSystemWindowsDirectoryA
#endif
#define GetVersionEx GetVersionExA
#define GetWindowsDirectory GetWindowsDirectoryA
#define SetComputerName SetComputerNameA
#define SetComputerNameEx SetComputerNameExA
#endif

WINBASEAPI
BOOL
WINAPI
GetProductInfo(
    _In_ DWORD dwOSMajorVersion,
    _In_ DWORD dwOSMinorVersion,
    _In_ DWORD dwSpMajorVersion,
    _In_ DWORD dwSpMinorVersion,
    _Out_ PDWORD pdwReturnedProductType);

WINBASEAPI
VOID
WINAPI
GetSystemTimePreciseAsFileTime(
    _Out_ LPFILETIME lpSystemTimeAsFileTime);

#ifdef __cplusplus
} // extern "C"
#endif
