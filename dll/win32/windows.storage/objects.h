/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell storage objects served by windows.storage
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <atlbase.h>
#include <atlcom.h>
#include <undocshell.h>
#include <shlobj_undoc.h>
#include <shlguid_undoc.h>
#include <shlwapi_undoc.h>
#include <shdocvw_undoc.h>
#include <shdeprecated.h>
#include <shellutils.h>
#include <strsafe.h>

#include "resource.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

VOID WSTORAGE_InitObjects(HINSTANCE hInstance);
VOID WSTORAGE_TermObjects(VOID);
HRESULT WSTORAGE_GetObjectsClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv);
BOOL WSTORAGE_ObjectsInUse(VOID);
HRESULT WSTORAGE_UpdateRegistry(BOOL bRegister);
VOID WSTORAGE_LockModule(VOID);
VOID WSTORAGE_UnlockModule(VOID);
HRESULT WINAPI CInternetFolder_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv);

#ifdef __cplusplus
}
#endif
