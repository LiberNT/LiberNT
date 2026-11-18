/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers of the WMI repository and decoupled provider tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <apitest.h>
#define COBJMACROS
#define CONST_VTABLE
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <oleauto.h>
#include <wbemcli.h>
#include <wbemprov.h>
#include <stdio.h>

#ifndef __IMofCompiler_INTERFACE_DEFINED__
typedef struct tag_CompileStatusInfo
{
    long lPhaseError;
    HRESULT hRes;
    long ObjectNum;
    long FirstLine;
    long LastLine;
    DWORD dwOutFlags;
} WBEM_COMPILE_STATUS_INFO;

typedef struct IMofCompiler IMofCompiler;
typedef struct IMofCompilerVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IMofCompiler *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IMofCompiler *);
    ULONG (STDMETHODCALLTYPE *Release)(IMofCompiler *);
    HRESULT (STDMETHODCALLTYPE *CompileFile)(IMofCompiler *, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LONG, LONG, LONG,
                                             WBEM_COMPILE_STATUS_INFO *);
    HRESULT (STDMETHODCALLTYPE *CompileBuffer)(IMofCompiler *, long, BYTE *, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LONG, LONG,
                                               LONG, WBEM_COMPILE_STATUS_INFO *);
    HRESULT (STDMETHODCALLTYPE *CreateBMOF)(IMofCompiler *, LPWSTR, LPWSTR, LPWSTR, LONG, LONG, LONG,
                                            WBEM_COMPILE_STATUS_INFO *);
} IMofCompilerVtbl;
struct IMofCompiler
{
    const IMofCompilerVtbl *lpVtbl;
};
#define IMofCompiler_CompileBuffer(This, a, b, c, d, e, f, g, h, i, j) \
    (This)->lpVtbl->CompileBuffer(This, a, b, c, d, e, f, g, h, i, j)
#define IMofCompiler_Release(This) (This)->lpVtbl->Release(This)
static const CLSID CLSID_MofCompiler = {0x6daf9757, 0x2e37, 0x11d2, {0xae, 0xc9, 0x00, 0xc0, 0x4f, 0xb6, 0x88, 0x20}};
static const IID IID_IMofCompiler = {0x6daf974e, 0x2e37, 0x11d2, {0xae, 0xc9, 0x00, 0xc0, 0x4f, 0xb6, 0x88, 0x20}};
#endif

#define UNTOUCHED_HR ((HRESULT)0xCCCCCCCC)

HRESULT WmiInitialize(void);
IWbemServices *WmiConnect(const WCHAR *Namespace, HRESULT *Result);
HRESULT WmiCompile(const char *Mof, WBEM_COMPILE_STATUS_INFO *Info);
HRESULT WmiCreateNamespace(IWbemServices *Parent, const WCHAR *Name);
HRESULT WmiDeleteNamespace(IWbemServices *Parent, const WCHAR *Name);
HRESULT WmiGetObject(IWbemServices *Services, const WCHAR *Path, IWbemClassObject **Object);
HRESULT WmiGet(IWbemClassObject *Object, const WCHAR *Name, VARIANT *Value, CIMTYPE *Type, LONG *Flavor);
BOOL WmiGetString(IWbemClassObject *Object, const WCHAR *Name, WCHAR *Buffer, ULONG Count);
LONG WmiGetLong(IWbemClassObject *Object, const WCHAR *Name, LONG Default);
HRESULT WmiPutLong(IWbemClassObject *Object, const WCHAR *Name, LONG Value);
HRESULT WmiPutString(IWbemClassObject *Object, const WCHAR *Name, const WCHAR *Value);
LONG WmiCount(IEnumWbemClassObject *Enum, HRESULT *Last);
HRESULT WmiEnumInstances(IWbemServices *Services, const WCHAR *Class, LONG Flags, LONG *Count);
HRESULT WmiQuery(IWbemServices *Services, const WCHAR *Query, LONG *Count);
DWORD WmiRunSelf(const char *Test, const char *Mode, const char *Argument, HANDLE *Process);
