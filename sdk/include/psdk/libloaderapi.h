/*
 * Copyright (C) 2017 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _APISETLIBLOADER_
#define _APISETLIBLOADER_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *DLL_DIRECTORY_COOKIE, **PDLL_DIRECTORY_COOKIE;

WINBASEAPI DLL_DIRECTORY_COOKIE WINAPI AddDllDirectory(const WCHAR *);
WINBASEAPI BOOL WINAPI RemoveDllDirectory(DLL_DIRECTORY_COOKIE);
WINBASEAPI BOOL WINAPI SetDefaultDllDirectories(DWORD);
WINBASEAPI INT  WINAPI FindStringOrdinal(DWORD, const WCHAR *, INT, const WCHAR *, INT, BOOL);

#define DONT_RESOLVE_DLL_REFERENCES                 0x00000001
#define LOAD_LIBRARY_AS_DATAFILE                    0x00000002
#define LOAD_WITH_ALTERED_SEARCH_PATH               0x00000008
#define LOAD_IGNORE_CODE_AUTHZ_LEVEL                0x00000010
#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
#define LOAD_LIBRARY_AS_IMAGE_RESOURCE              0x00000020
#define LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE          0x00000040
#define LOAD_LIBRARY_REQUIRE_SIGNED_TARGET          0x00000080
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR            0x00000100
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR         0x00000200
#define LOAD_LIBRARY_SEARCH_USER_DIRS               0x00000400
#define LOAD_LIBRARY_SEARCH_SYSTEM32                0x00000800
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS            0x00001000
#endif
#if (NTDDI_VERSION >= NTDDI_WIN10_RS1)
#define LOAD_LIBRARY_SAFE_CURRENT_DIRS              0x00002000
#define LOAD_LIBRARY_SEARCH_SYSTEM32_NO_FORWARDER   0x00004000
#else
#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
#define LOAD_LIBRARY_SEARCH_SYSTEM32_NO_FORWARDER   LOAD_LIBRARY_SEARCH_SYSTEM32
#endif
#endif
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
#define LOAD_LIBRARY_OS_INTEGRITY_CONTINUITY        0x00008000
#endif

#if (_WIN32_WINNT >= 0x0500)
#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x1
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x2
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4
#endif

typedef BOOL(CALLBACK *ENUMRESLANGPROCA)(HMODULE,LPCSTR,LPCSTR,WORD,LONG_PTR);
typedef BOOL(CALLBACK *ENUMRESLANGPROCW)(HMODULE,LPCWSTR,LPCWSTR,WORD,LONG_PTR);
typedef BOOL(CALLBACK *ENUMRESNAMEPROCA)(HMODULE,LPCSTR,LPSTR,LONG_PTR);
typedef BOOL(CALLBACK *ENUMRESNAMEPROCW)(HMODULE,LPCWSTR,LPWSTR,LONG_PTR);
typedef BOOL(CALLBACK *ENUMRESTYPEPROCA)(HMODULE,LPSTR,LONG_PTR);
typedef BOOL(CALLBACK *ENUMRESTYPEPROCW)(HMODULE,LPWSTR,LONG_PTR);

BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
BOOL WINAPI EnumResourceNamesW(_In_opt_ HMODULE, _In_ LPCWSTR, _In_ ENUMRESNAMEPROCW, _In_ LONG_PTR);
_Ret_maybenull_ HRSRC WINAPI FindResourceW(_In_opt_ HMODULE,_In_ LPCWSTR, _In_ LPCWSTR);
HRSRC WINAPI FindResourceExW(HINSTANCE,LPCWSTR,LPCWSTR,WORD);
BOOL WINAPI FreeLibrary(HMODULE);
DECLSPEC_NORETURN void WINAPI FreeLibraryAndExitThread(HMODULE,DWORD);
BOOL WINAPI FreeResource(HGLOBAL);
DWORD WINAPI GetModuleFileNameA(HINSTANCE hModule,LPSTR lpFilename,DWORD nSize);
DWORD WINAPI GetModuleFileNameW(HINSTANCE hModule,LPWSTR lpFilename,DWORD nSize);
HMODULE WINAPI GetModuleHandleA(LPCSTR);
HMODULE WINAPI GetModuleHandleW(LPCWSTR);
FARPROC WINAPI GetProcAddress(HINSTANCE,LPCSTR);
_Ret_maybenull_ HINSTANCE WINAPI LoadLibraryA(_In_ LPCSTR);
_Ret_maybenull_ HINSTANCE WINAPI LoadLibraryW(_In_ LPCWSTR);
HINSTANCE WINAPI LoadLibraryExA(LPCSTR,HANDLE,DWORD);
HINSTANCE WINAPI LoadLibraryExW(LPCWSTR,HANDLE,DWORD);
HGLOBAL WINAPI LoadResource(HINSTANCE,HRSRC);
PVOID WINAPI LockResource(HGLOBAL);
DWORD WINAPI SizeofResource(HINSTANCE,HRSRC);
#if (_WIN32_WINNT >= 0x0500)
BOOL WINAPI GetModuleHandleExA(DWORD,LPCSTR,HMODULE*);
BOOL WINAPI GetModuleHandleExW(DWORD,LPCWSTR,HMODULE*);
#endif

#ifdef UNICODE
#define GetModuleFileName GetModuleFileNameW
#define GetModuleHandle GetModuleHandleW
#if (_WIN32_WINNT >= 0x0500)
#define GetModuleHandleEx GetModuleHandleExW
#endif
#define LoadLibrary LoadLibraryW
#define LoadLibraryEx LoadLibraryExW
#else
#define GetModuleFileName GetModuleFileNameA
#define GetModuleHandle GetModuleHandleA
#if (_WIN32_WINNT >= 0x0500)
#define GetModuleHandleEx GetModuleHandleExA
#endif
#define LoadLibrary LoadLibraryA
#define LoadLibraryEx LoadLibraryExA
#endif

#ifdef __cplusplus
}
#endif

#endif  /* _APISETLIBLOADER_ */
