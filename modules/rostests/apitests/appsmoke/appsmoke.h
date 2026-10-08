/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers of the fast application smoke tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _APPSMOKE_H_
#define _APPSMOKE_H_

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL AppSmokeIsRole(const char *Role);
BOOL AppSmokeRunSelf(const char *Test, const char *Role, DWORD BudgetMs, DWORD *ExitCode, DWORD *ElapsedMs);
HWND AppSmokeCreateWindow(int Width, int Height);
void AppSmokePumpMessages(void);
BOOL AppSmokeCycleDisplayMode(void);
BOOL AppSmokeD3D11DriverName(WCHAR *Name, UINT Count);
BOOL AppSmokeD3D12DriverName(WCHAR *Name, UINT Count);
BOOL AppSmokeOpenGlIcdName(WCHAR *Name, UINT Count);
BOOL AppSmokeOpenClDriverName(WCHAR *Name, UINT Count);
BOOL AppSmokeModuleLoaded(const WCHAR *Path);

#ifdef __cplusplus
}
#endif

#endif
