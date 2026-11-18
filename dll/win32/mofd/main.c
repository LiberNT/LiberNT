/*
 * PROJECT:     LiberNT WMI MOF compiler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     MofCompiler class object and IMofCompiler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "mofd_private.h"
#include <rpcproxy.h>

typedef struct _MOF_COMPILER
{
    IMofCompiler IMofCompiler_iface;
    LONG Refs;
} MOF_COMPILER;

static inline MOF_COMPILER *
MofCompilerFromIface(IMofCompiler *Iface)
{
    return CONTAINING_RECORD(Iface, MOF_COMPILER, IMofCompiler_iface);
}

static HRESULT
MofCheckOptions(LONG Options)
{
    const LONG KnownOptions = WBEM_FLAG_CHECK_ONLY | WBEM_FLAG_AUTORECOVER | WBEM_FLAG_WMI_CHECK |
                              WBEM_FLAG_CONSOLE_PRINT | WBEM_FLAG_DONT_ADD_TO_LIST | WBEM_FLAG_SPLIT_FILES |
                              WBEM_FLAG_STORE_FILE;

    if (Options & ~KnownOptions)
        return WBEM_E_INVALID_PARAMETER;
    if ((Options & WBEM_FLAG_AUTORECOVER) && (Options & WBEM_FLAG_DONT_ADD_TO_LIST))
        return WBEM_E_INVALID_PARAMETER;
    return S_OK;
}

static HRESULT
MofArgumentError(WBEM_COMPILE_STATUS_INFO *Info)
{
    Info->lPhaseError = 1;
    Info->hRes = WBEM_E_INVALID_PARAMETER;
    return WBEM_E_INVALID_PARAMETER;
}

static HRESULT
MofCompileText(WCHAR *Text, const WCHAR *Namespace, LONG Options, LONG ClassFlags, LONG InstanceFlags,
               WBEM_COMPILE_STATUS_INFO *Info)
{
    MOF_UNIT Unit;
    HRESULT Result;

    ZeroMemory(&Unit, sizeof(Unit));
    if (Namespace && !*Namespace)
        Namespace = NULL;
    Result = MofParse(&Unit, Text, Info);
    /* TODO: WBEM_FLAG_CONSOLE_PRINT progress and error messages */
    if (SUCCEEDED(Result) && (Unit.HasAutoRecover || (Options & WBEM_FLAG_AUTORECOVER)) &&
        (Namespace || ClassFlags || InstanceFlags))
        Result = MofArgumentError(Info);
    if (SUCCEEDED(Result) && !(Options & WBEM_FLAG_CHECK_ONLY))
    {
        Result = MofStore(&Unit, Namespace ? Namespace : L"root\\default", ClassFlags, InstanceFlags, Info);
        /* TODO: on success with WBEM_FLAG_AUTORECOVER, add the file to the autorecover list, and return 2 when it has no #pragma autorecover */
    }
    MofFreeUnit(&Unit);
    if (FAILED(Result))
        return WBEM_S_FALSE;
    Info->lPhaseError = 0;
    Info->hRes = S_OK;
    return S_OK;
}

static HRESULT WINAPI
MofCompiler_QueryInterface(IMofCompiler *Iface, REFIID Iid, void **Object)
{
    if (!Object)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IMofCompiler))
    {
        *Object = Iface;
        IMofCompiler_AddRef(Iface);
        return S_OK;
    }
    *Object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
MofCompiler_AddRef(IMofCompiler *Iface)
{
    return InterlockedIncrement(&MofCompilerFromIface(Iface)->Refs);
}

static ULONG WINAPI
MofCompiler_Release(IMofCompiler *Iface)
{
    MOF_COMPILER *Compiler = MofCompilerFromIface(Iface);
    LONG Refs = InterlockedDecrement(&Compiler->Refs);

    if (!Refs)
        HeapFree(GetProcessHeap(), 0, Compiler);
    return Refs;
}

static HRESULT WINAPI
MofCompiler_CompileFile(IMofCompiler *Iface, LPWSTR FileName, LPWSTR Namespace, LPWSTR User, LPWSTR Authority,
                        LPWSTR Password, LONG Options, LONG ClassFlags, LONG InstanceFlags,
                        WBEM_COMPILE_STATUS_INFO *Info)
{
    WBEM_COMPILE_STATUS_INFO Local;
    LARGE_INTEGER FileSize;
    HANDLE File;
    BYTE *Data;
    WCHAR *Text = NULL;
    DWORD Read = 0;
    HRESULT Result;

    /* TODO: remote namespaces with User, Authority and Password */
    if (!FileName)
        return WBEM_E_INVALID_PARAMETER;
    if (!Info)
        Info = &Local;
    ZeroMemory(Info, sizeof(*Info));
    if (FAILED(MofCheckOptions(Options)))
    {
        MofArgumentError(Info);
        return WBEM_S_FALSE;
    }
    Info->lPhaseError = 2;
    Info->hRes = WBEMMOF_E_INVALID_FILE;
    File = CreateFileW(FileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return WBEM_S_FALSE;
    if (!GetFileSizeEx(File, &FileSize) || FileSize.QuadPart > MAXLONG)
    {
        CloseHandle(File);
        return WBEM_S_FALSE;
    }
    Data = HeapAlloc(GetProcessHeap(), 0, FileSize.LowPart ? FileSize.LowPart : 1);
    if (!Data)
    {
        CloseHandle(File);
        Info->hRes = WBEM_E_OUT_OF_MEMORY;
        return WBEM_S_FALSE;
    }
    if (!ReadFile(File, Data, FileSize.LowPart, &Read, NULL) || Read != FileSize.LowPart)
    {
        HeapFree(GetProcessHeap(), 0, Data);
        CloseHandle(File);
        return WBEM_S_FALSE;
    }
    CloseHandle(File);
    Result = MofDecodeText(Data, Read, TRUE, &Text);
    HeapFree(GetProcessHeap(), 0, Data);
    if (FAILED(Result))
    {
        Info->hRes = Result;
        return WBEM_S_FALSE;
    }
    Result = MofCompileText(Text, Namespace, Options, ClassFlags, InstanceFlags, Info);
    HeapFree(GetProcessHeap(), 0, Text);
    return Result;
}

static HRESULT WINAPI
MofCompiler_CompileBuffer(IMofCompiler *Iface, LONG Size, BYTE *Buffer, LPWSTR Namespace, LPWSTR User,
                          LPWSTR Authority, LPWSTR Password, LONG Options, LONG ClassFlags, LONG InstanceFlags,
                          WBEM_COMPILE_STATUS_INFO *Info)
{
    WBEM_COMPILE_STATUS_INFO Local;
    WCHAR *Text;
    HRESULT Result;

    if (Size <= 0 || !Buffer)
        return WBEM_E_INVALID_PARAMETER;
    if (!Info)
        Info = &Local;
    if (FAILED(MofCheckOptions(Options)))
    {
        MofArgumentError(Info);
        return WBEM_S_FALSE;
    }
    Result = MofDecodeText(Buffer, Size, FALSE, &Text);
    if (FAILED(Result))
    {
        Info->lPhaseError = 2;
        Info->hRes = Result;
        return WBEM_S_FALSE;
    }
    Result = MofCompileText(Text, Namespace, Options, ClassFlags, InstanceFlags, Info);
    HeapFree(GetProcessHeap(), 0, Text);
    return Result;
}

static HRESULT WINAPI
MofCompiler_CreateBMOF(IMofCompiler *Iface, LPWSTR TextFileName, LPWSTR BmofFileName, LPWSTR Namespace,
                       LONG Options, LONG ClassFlags, LONG InstanceFlags, WBEM_COMPILE_STATUS_INFO *Info)
{
    if (!TextFileName || !BmofFileName)
        return WBEM_E_INVALID_PARAMETER;
    /* TODO: binary MOF output */
    return WBEM_E_NOT_SUPPORTED;
}

static const IMofCompilerVtbl MofCompilerVtbl =
{
    MofCompiler_QueryInterface,
    MofCompiler_AddRef,
    MofCompiler_Release,
    MofCompiler_CompileFile,
    MofCompiler_CompileBuffer,
    MofCompiler_CreateBMOF
};

static HRESULT WINAPI
MofFactory_QueryInterface(IClassFactory *Iface, REFIID Iid, void **Object)
{
    if (!Object)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IClassFactory))
    {
        *Object = Iface;
        return S_OK;
    }
    *Object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
MofFactory_AddRef(IClassFactory *Iface)
{
    return 2;
}

static ULONG WINAPI
MofFactory_Release(IClassFactory *Iface)
{
    return 1;
}

static HRESULT WINAPI
MofFactory_CreateInstance(IClassFactory *Iface, IUnknown *Outer, REFIID Iid, void **Object)
{
    MOF_COMPILER *Compiler;
    HRESULT Result;

    if (!Object)
        return E_POINTER;
    *Object = NULL;
    if (Outer)
        return CLASS_E_NOAGGREGATION;
    Compiler = HeapAlloc(GetProcessHeap(), 0, sizeof(*Compiler));
    if (!Compiler)
        return E_OUTOFMEMORY;
    Compiler->IMofCompiler_iface.lpVtbl = &MofCompilerVtbl;
    Compiler->Refs = 1;
    Result = IMofCompiler_QueryInterface(&Compiler->IMofCompiler_iface, Iid, Object);
    IMofCompiler_Release(&Compiler->IMofCompiler_iface);
    return Result;
}

static HRESULT WINAPI
MofFactory_LockServer(IClassFactory *Iface, BOOL Lock)
{
    return S_OK;
}

static const IClassFactoryVtbl MofFactoryVtbl =
{
    MofFactory_QueryInterface,
    MofFactory_AddRef,
    MofFactory_Release,
    MofFactory_CreateInstance,
    MofFactory_LockServer
};

static IClassFactory MofFactory = { &MofFactoryVtbl };

HRESULT WINAPI
DllGetClassObject(REFCLSID Clsid, REFIID Iid, void **Object)
{
    if (!Object)
        return E_POINTER;
    if (IsEqualGUID(Clsid, &CLSID_MofCompiler))
        return IClassFactory_QueryInterface(&MofFactory, Iid, Object);
    *Object = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}
