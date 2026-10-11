/*
 * PROJECT:     LiberNT ieframe
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ATL module for the shell objects served by ieframe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "objects.h"

void *operator new(size_t size)
{
    return ::HeapAlloc(::GetProcessHeap(), 0, size);
}

void operator delete(void *ptr)
{
    ::HeapFree(::GetProcessHeap(), 0, ptr);
}

void operator delete(void *ptr, size_t size)
{
    ::HeapFree(::GetProcessHeap(), 0, ptr);
}

class CIEFrameModule : public CComModule
{
};

static CIEFrameModule gModule;

EXTERN_C VOID
IEFRAME_InitObjects(HINSTANCE hInstance)
{
    gModule.Init(NULL, hInstance, NULL);
}

EXTERN_C VOID
IEFRAME_TermObjects(VOID)
{
    gModule.Term();
}

EXTERN_C HRESULT
IEFRAME_GetObjectsClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    return gModule.DllGetClassObject(rclsid, riid, ppv);
}

EXTERN_C BOOL
IEFRAME_ObjectsInUse(VOID)
{
    return gModule.GetLockCount() != 0;
}
