/*
 * PROJECT:     LiberNT PSDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Process environment API set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _PROCESSENV_
#define _PROCESSENV_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI ExpandEnvironmentStringsA(LPCSTR,LPSTR,DWORD);
DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR,LPWSTR,DWORD);
BOOL WINAPI FreeEnvironmentStringsA(LPSTR);
BOOL WINAPI FreeEnvironmentStringsW(LPWSTR);
BOOL WINAPI SetEnvironmentStringsW(LPWCH);
LPSTR WINAPI GetCommandLineA(VOID);
LPWSTR WINAPI GetCommandLineW(VOID);
DWORD WINAPI GetCurrentDirectoryA(DWORD,LPSTR);
DWORD WINAPI GetCurrentDirectoryW(DWORD,LPWSTR);
LPSTR WINAPI GetEnvironmentStrings(void);
LPWSTR WINAPI GetEnvironmentStringsW(void);
DWORD WINAPI GetEnvironmentVariableA(LPCSTR,LPSTR,DWORD);
DWORD WINAPI GetEnvironmentVariableW(LPCWSTR,LPWSTR,DWORD);
HANDLE WINAPI GetStdHandle(_In_ DWORD);
BOOL WINAPI SetCurrentDirectoryA(LPCSTR);
BOOL WINAPI SetCurrentDirectoryW(LPCWSTR);
BOOL WINAPI SetEnvironmentVariableA(LPCSTR,LPCSTR);
BOOL WINAPI SetEnvironmentVariableW(LPCWSTR,LPCWSTR);
BOOL WINAPI SetStdHandle(_In_ DWORD, _In_ HANDLE);

#if _WIN32_WINNT >= 0x0502
WINBASEAPI BOOL WINAPI NeedCurrentDirectoryForExePathA(LPCSTR ExeName);
WINBASEAPI BOOL WINAPI NeedCurrentDirectoryForExePathW(LPCWSTR ExeName);
#endif

_Success_(return != 0 && return < nBufferLength)
DWORD
WINAPI
SearchPathA(
  _In_opt_ LPCSTR lpPath,
  _In_ LPCSTR lpFileName,
  _In_opt_ LPCSTR lpExtension,
  _In_ DWORD nBufferLength,
  _Out_writes_to_opt_(nBufferLength, return + 1) LPSTR lpBuffer,
  _Out_opt_ LPSTR *lpFilePart);

DWORD WINAPI
SearchPathW(
    _In_opt_ LPCWSTR lpPath,
    _In_ LPCWSTR lpFileName,
    _In_opt_ LPCWSTR lpExtension,
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return +1) LPWSTR lpBuffer,
    _Out_opt_ LPWSTR *lpFilePart);

#ifdef UNICODE
#define ExpandEnvironmentStrings ExpandEnvironmentStringsW
#define FreeEnvironmentStrings FreeEnvironmentStringsW
#define GetCommandLine GetCommandLineW
#define GetCurrentDirectory GetCurrentDirectoryW
#define GetEnvironmentStrings GetEnvironmentStringsW
#define GetEnvironmentVariable GetEnvironmentVariableW
#define SearchPath SearchPathW
#define SetCurrentDirectory SetCurrentDirectoryW
#define SetEnvironmentVariable SetEnvironmentVariableW
#else
#define ExpandEnvironmentStrings ExpandEnvironmentStringsA
#define FreeEnvironmentStrings FreeEnvironmentStringsA
#define GetCommandLine GetCommandLineA
#define GetCurrentDirectory GetCurrentDirectoryA
#define GetEnvironmentStringsA GetEnvironmentStrings
#define GetEnvironmentVariable GetEnvironmentVariableA
#define SearchPath SearchPathA
#define SetCurrentDirectory SetCurrentDirectoryA
#define SetEnvironmentVariable SetEnvironmentVariableA
#endif

#ifdef __cplusplus
}
#endif

#endif
