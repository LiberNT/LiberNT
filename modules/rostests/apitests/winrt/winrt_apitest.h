/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers for the Windows Runtime class tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define COBJMACROS
#include <apitest.h>
#include <winreg.h>
#include <winstring.h>
#include <roapi.h>
#include <stdio.h>
#include <wchar.h>

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include <windows.foundation.h>

BOOL
CreateAppShortcut(
    _Out_writes_(MAX_PATH) WCHAR *Path,
    _In_ const WCHAR *AppId);

static inline HSTRING
CreateHString(
    _In_ const WCHAR *Text)
{
    HSTRING String = NULL;

    WindowsCreateString(Text, (UINT32)wcslen(Text), &String);
    return String;
}

static inline void
TraceActivatableClass(
    _In_ const WCHAR *ClassName)
{
    WCHAR Key[256], Path[MAX_PATH];
    DWORD Size = sizeof(Path), Value, ValueSize = sizeof(Value);
    HKEY Handle;

    swprintf(Key, ARRAYSIZE(Key), L"SOFTWARE\\Microsoft\\WindowsRuntime\\ActivatableClassId\\%ls", ClassName);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Key, 0, KEY_READ | KEY_WOW64_64KEY, &Handle))
    {
        trace("%ls has no activatable class key\n", ClassName);
        return;
    }
    if (!RegQueryValueExW(Handle, L"DllPath", NULL, NULL, (BYTE *)Path, &Size))
        trace("%ls DllPath %ls\n", ClassName, Path);
    if (!RegQueryValueExW(Handle, L"ActivationType", NULL, NULL, (BYTE *)&Value, &ValueSize))
        trace("%ls ActivationType %lu\n", ClassName, Value);
    ValueSize = sizeof(Value);
    if (!RegQueryValueExW(Handle, L"Threading", NULL, NULL, (BYTE *)&Value, &ValueSize))
        trace("%ls Threading %lu\n", ClassName, Value);
    RegCloseKey(Handle);
}

static inline HRESULT
GetFactory(
    _In_ const WCHAR *ClassName,
    _In_ REFIID Iid,
    _Out_ void **Factory)
{
    HSTRING Class = CreateHString(ClassName);
    HRESULT hr = RoGetActivationFactory(Class, Iid, Factory);

    WindowsDeleteString(Class);
    return hr;
}

static inline AsyncStatus
WaitAsync(
    _In_ IUnknown *Operation,
    _In_ DWORD Milliseconds,
    _Out_ HRESULT *ErrorCode)
{
    AsyncStatus Status = Started;
    IAsyncInfo *Info;
    DWORD Start = GetTickCount();

    *ErrorCode = E_FAIL;
    if (FAILED(IUnknown_QueryInterface(Operation, &IID_IAsyncInfo, (void **)&Info)))
        return Error;
    while (SUCCEEDED(IAsyncInfo_get_Status(Info, &Status)) && Status == Started &&
           GetTickCount() - Start < Milliseconds)
    {
        Sleep(50);
    }
    IAsyncInfo_get_ErrorCode(Info, ErrorCode);
    IAsyncInfo_Release(Info);
    return Status;
}
