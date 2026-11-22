/*
 * Copyright (C) 2009 Andrey Turkin
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

#ifndef __WINE_LOADPERF_H
#define __WINE_LOADPERF_H

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI InstallPerfDllA(LPCSTR, LPCSTR, ULONG_PTR);
DWORD WINAPI InstallPerfDllW(LPCWSTR, LPCWSTR, ULONG_PTR);
#define      InstallPerfDll WINELIB_NAME_AW(InstallPerfDll);

DWORD WINAPI LoadPerfCounterTextStringsA(LPCSTR, BOOL);
DWORD WINAPI LoadPerfCounterTextStringsW(LPCWSTR, BOOL);
#define      LoadPerfCounterTextStrings WINELIB_NAME_AW(LoadPerfCounterTextStrings)

DWORD WINAPI UnloadPerfCounterTextStringsA(LPCSTR, BOOL);
DWORD WINAPI UnloadPerfCounterTextStringsW(LPCWSTR, BOOL);
#define      UnloadPerfCounterTextStrings WINELIB_NAME_AW(UnloadPerfCounterTextStrings)

#ifdef __cplusplus
}
#endif

#define LOADPERF_FLAGS_CREATE_MOF_ONLY ((ULONG_PTR)4)
#define LOADPERF_FLAGS_DELETE_MOF_ON_EXIT ((ULONG_PTR) 1)
#define LOADPERF_FLAGS_DISPLAY_USER_MSGS ((ULONG_PTR)8)
#define LOADPERF_FLAGS_LOAD_REGISTRY_ONLY ((ULONG_PTR)2)
#define LOADPERF_FUNCTION 0
#ifdef UNICODE
#define SetServiceAsTrusted SetServiceAsTrustedW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define SetServiceAsTrusted SetServiceAsTrustedW
#else
#define SetServiceAsTrusted SetServiceAsTrustedA
#endif
#endif
#endif
#ifdef UNICODE
#define UpdatePerfNameFiles UpdatePerfNameFilesW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define UpdatePerfNameFiles UpdatePerfNameFilesW
#else
#define UpdatePerfNameFiles UpdatePerfNameFilesA
#endif
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
DWORD WINAPI BackupPerfRegistryToFileW(LPCWSTR szFileName, LPCWSTR szCommentString);
DWORD WINAPI RestorePerfRegistryFromFileW(LPCWSTR szFileName, LPCWSTR szLangId);
DWORD WINAPI SetServiceAsTrustedA(LPCSTR szReserved, LPCSTR szServiceName);
DWORD WINAPI SetServiceAsTrustedW(LPCWSTR szReserved, LPCWSTR szServiceName);
DWORD WINAPI UpdatePerfNameFilesA(LPCSTR szNewCtrFilePath, LPCSTR szNewHlpFilePath, LPSTR szLanguageID, ULONG_PTR dwModes);
DWORD WINAPI UpdatePerfNameFilesW(LPCWSTR szNewCtrFilePath, LPCWSTR szNewHlpFilePath, LPWSTR szLanguageID, ULONG_PTR dwModes);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_LOADPERF_H */
