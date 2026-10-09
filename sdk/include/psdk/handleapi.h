/*
 * PROJECT:     LiberNT PSDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Handle API set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _APISETHANDLE_
#define _APISETHANDLE_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INVALID_HANDLE_VALUE (HANDLE)(-1)

BOOL WINAPI CloseHandle(HANDLE);
BOOL WINAPI DuplicateHandle(HANDLE,HANDLE,HANDLE,PHANDLE,DWORD,BOOL,DWORD);
BOOL WINAPI GetHandleInformation(HANDLE,PDWORD);
BOOL WINAPI SetHandleInformation(HANDLE,DWORD,DWORD);

#ifdef __cplusplus
}
#endif

#endif
