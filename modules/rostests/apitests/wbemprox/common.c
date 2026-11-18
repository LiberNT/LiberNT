/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers of the WMI repository and decoupled provider tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "wmitest.h"

HRESULT
WmiInitialize(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (FAILED(hr))
        return hr;
    hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL,
                              EOAC_NONE, NULL);
    return hr == RPC_E_TOO_LATE ? S_OK : hr;
}

IWbemServices *
WmiConnect(const WCHAR *Namespace, HRESULT *Result)
{
    IWbemLocator *Locator = NULL;
    IWbemServices *Services = NULL;
    BSTR Name;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&Locator);
    if (SUCCEEDED(hr))
    {
        Name = SysAllocString(Namespace);
        hr = IWbemLocator_ConnectServer(Locator, Name, NULL, NULL, NULL, 0, NULL, NULL, &Services);
        SysFreeString(Name);
        IWbemLocator_Release(Locator);
    }
    if (SUCCEEDED(hr))
        CoSetProxyBlanket((IUnknown *)Services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (Result)
        *Result = hr;
    return SUCCEEDED(hr) ? Services : NULL;
}

HRESULT
WmiCompile(const char *Mof, WBEM_COMPILE_STATUS_INFO *Info)
{
    IMofCompiler *Compiler;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MofCompiler, NULL, CLSCTX_INPROC_SERVER, &IID_IMofCompiler, (void **)&Compiler);
    if (FAILED(hr))
        return hr;
    memset(Info, 0, sizeof(*Info));
    hr = IMofCompiler_CompileBuffer(Compiler, (LONG)strlen(Mof), (BYTE *)Mof, NULL, NULL, NULL, NULL, 0, 0, 0, Info);
    IMofCompiler_Release(Compiler);
    return hr;
}

HRESULT
WmiGetObject(IWbemServices *Services, const WCHAR *Path, IWbemClassObject **Object)
{
    BSTR Name = Path ? SysAllocString(Path) : NULL;
    HRESULT hr;

    *Object = NULL;
    hr = IWbemServices_GetObject(Services, Name, 0, NULL, Object, NULL);
    SysFreeString(Name);
    return hr;
}

HRESULT
WmiCreateNamespace(IWbemServices *Parent, const WCHAR *Name)
{
    IWbemClassObject *Class, *Instance = NULL;
    HRESULT hr;

    hr = WmiGetObject(Parent, L"__Namespace", &Class);
    if (FAILED(hr))
        return hr;
    hr = IWbemClassObject_SpawnInstance(Class, 0, &Instance);
    IWbemClassObject_Release(Class);
    if (SUCCEEDED(hr))
        hr = WmiPutString(Instance, L"Name", Name);
    if (SUCCEEDED(hr))
        hr = IWbemServices_PutInstance(Parent, Instance, WBEM_FLAG_CREATE_ONLY, NULL, NULL);
    if (Instance)
        IWbemClassObject_Release(Instance);
    return hr;
}

HRESULT
WmiDeleteNamespace(IWbemServices *Parent, const WCHAR *Name)
{
    WCHAR Path[128];
    BSTR Text;
    HRESULT hr;

    swprintf(Path, ARRAYSIZE(Path), L"__Namespace.Name=\"%ls\"", Name);
    Text = SysAllocString(Path);
    hr = IWbemServices_DeleteInstance(Parent, Text, 0, NULL, NULL);
    SysFreeString(Text);
    return hr;
}

HRESULT
WmiGet(IWbemClassObject *Object, const WCHAR *Name, VARIANT *Value, CIMTYPE *Type, LONG *Flavor)
{
    VariantInit(Value);
    if (Type)
        *Type = (CIMTYPE)0xCCCC;
    if (Flavor)
        *Flavor = (LONG)0xCCCC;
    return IWbemClassObject_Get(Object, Name, 0, Value, Type, Flavor);
}

BOOL
WmiGetString(IWbemClassObject *Object, const WCHAR *Name, WCHAR *Buffer, ULONG Count)
{
    VARIANT Value;
    BOOL Result = FALSE;

    Buffer[0] = 0;
    if (SUCCEEDED(WmiGet(Object, Name, &Value, NULL, NULL)) && V_VT(&Value) == VT_BSTR)
    {
        lstrcpynW(Buffer, V_BSTR(&Value), Count);
        Result = TRUE;
    }
    VariantClear(&Value);
    return Result;
}

LONG
WmiGetLong(IWbemClassObject *Object, const WCHAR *Name, LONG Default)
{
    VARIANT Value;
    LONG Result = Default;

    if (SUCCEEDED(WmiGet(Object, Name, &Value, NULL, NULL)) &&
        SUCCEEDED(VariantChangeType(&Value, &Value, 0, VT_I4)))
        Result = V_I4(&Value);
    VariantClear(&Value);
    return Result;
}

HRESULT
WmiPutLong(IWbemClassObject *Object, const WCHAR *Name, LONG Number)
{
    VARIANT Value;

    V_VT(&Value) = VT_I4;
    V_I4(&Value) = Number;
    return IWbemClassObject_Put(Object, Name, 0, &Value, 0);
}

HRESULT
WmiPutString(IWbemClassObject *Object, const WCHAR *Name, const WCHAR *Text)
{
    VARIANT Value;
    HRESULT hr;

    V_VT(&Value) = VT_BSTR;
    V_BSTR(&Value) = SysAllocString(Text);
    hr = IWbemClassObject_Put(Object, Name, 0, &Value, 0);
    VariantClear(&Value);
    return hr;
}

LONG
WmiCount(IEnumWbemClassObject *Enum, HRESULT *Last)
{
    IWbemClassObject *Object;
    LONG Count = 0;
    ULONG Returned;
    HRESULT hr;

    for (;;)
    {
        Object = NULL;
        Returned = 0;
        hr = IEnumWbemClassObject_Next(Enum, WBEM_INFINITE, 1, &Object, &Returned);
        if (Returned == 0)
            break;
        IWbemClassObject_Release(Object);
        Count++;
    }
    if (Last)
        *Last = hr;
    return Count;
}

HRESULT
WmiEnumInstances(IWbemServices *Services, const WCHAR *Class, LONG Flags, LONG *Count)
{
    IEnumWbemClassObject *Enum = NULL;
    BSTR Name = SysAllocString(Class);
    HRESULT hr, Last = S_OK;

    *Count = -1;
    hr = IWbemServices_CreateInstanceEnum(Services, Name, Flags, NULL, &Enum);
    SysFreeString(Name);
    if (SUCCEEDED(hr))
    {
        *Count = WmiCount(Enum, &Last);
        IEnumWbemClassObject_Release(Enum);
        if (FAILED(Last))
            hr = Last;
    }
    return hr;
}

HRESULT
WmiQuery(IWbemServices *Services, const WCHAR *Query, LONG *Count)
{
    IEnumWbemClassObject *Enum = NULL;
    BSTR Language = SysAllocString(L"WQL"), Text = SysAllocString(Query);
    HRESULT hr, Last = S_OK;

    *Count = -1;
    hr = IWbemServices_ExecQuery(Services, Language, Text, WBEM_FLAG_FORWARD_ONLY, NULL, &Enum);
    SysFreeString(Language);
    SysFreeString(Text);
    if (SUCCEEDED(hr))
    {
        *Count = WmiCount(Enum, &Last);
        IEnumWbemClassObject_Release(Enum);
        if (FAILED(Last))
            hr = Last;
    }
    return hr;
}

DWORD
WmiRunSelf(const char *Test, const char *Mode, const char *Argument, HANDLE *Process)
{
    char **Argv, CommandLine[MAX_PATH * 2];
    STARTUPINFOA Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Info;
    DWORD ExitCode = 0xFFFFFFFF;

    winetest_get_mainargs(&Argv);
    snprintf(CommandLine, sizeof(CommandLine), "\"%s\" %s %s %s", Argv[0], Test, Mode, Argument ? Argument : "");
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Info))
        return GetLastError() | 0x80000000;
    CloseHandle(Info.hThread);
    if (Process)
    {
        *Process = Info.hProcess;
        return 0;
    }
    WaitForSingleObject(Info.hProcess, 60000);
    GetExitCodeProcess(Info.hProcess, &ExitCode);
    CloseHandle(Info.hProcess);
    return ExitCode;
}
