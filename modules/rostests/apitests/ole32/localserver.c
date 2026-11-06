/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests local server activation after the server process ends
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <wine/test.h>
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <stdio.h>

static const CLSID CLSID_TestLocalServer = { 0x5c1f2a90, 0x6b1e, 0x4d1a, { 0x9e, 0x51, 0x3a, 0x7d, 0x21, 0x40, 0x88, 0x01 } };

static HRESULT WINAPI Factory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI Factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI Factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI Factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    *ppv = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI Factory_LockServer(IClassFactory *iface, BOOL lock)
{
    return S_OK;
}

static IClassFactoryVtbl FactoryVtbl =
{
    Factory_QueryInterface,
    Factory_AddRef,
    Factory_Release,
    Factory_CreateInstance,
    Factory_LockServer
};

static IClassFactory Factory = { &FactoryVtbl };

static void RunServer(const char *EventString)
{
    HANDLE Event;
    DWORD Cookie;
    HRESULT hr;

    if (sscanf(EventString, "%p", &Event) != 1)
        return;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoRegisterClassObject(&CLSID_TestLocalServer, (IUnknown *)&Factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &Cookie);
    ok(hr == S_OK, "CoRegisterClassObject failed, hr %#lx\n", hr);
    SetEvent(Event);
    Sleep(INFINITE);
}

START_TEST(localserver)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    STARTUPINFOA StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    char CommandLine[MAX_PATH + 64];
    IClassFactory *ClassFactory;
    HANDLE Event;
    HRESULT hr;
    char **Argv;
    int Argc;

    Argc = winetest_get_mainargs(&Argv);
    if (Argc >= 4 && !strcmp(Argv[2], "server"))
    {
        RunServer(Argv[3]);
        return;
    }

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Event = CreateEventA(&Attributes, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEventA failed with error %lu\n", GetLastError());
    snprintf(CommandLine, sizeof(CommandLine), "\"%s\" localserver server %p", Argv[0], Event);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessA failed with error %lu\n", GetLastError());
        CloseHandle(Event);
        CoUninitialize();
        return;
    }
    ok(WaitForSingleObject(Event, 10000) == WAIT_OBJECT_0, "The server did not register its class\n");

    hr = CoGetClassObject(&CLSID_TestLocalServer, CLSCTX_LOCAL_SERVER, NULL, &IID_IClassFactory, (void **)&ClassFactory);
    ok(hr == S_OK, "CoGetClassObject from the running server failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
        IClassFactory_Release(ClassFactory);

    TerminateProcess(ProcessInfo.hProcess, 1);
    ok(WaitForSingleObject(ProcessInfo.hProcess, 10000) == WAIT_OBJECT_0, "The server did not end\n");

    ClassFactory = (IClassFactory *)(ULONG_PTR)0xdeadbeef;
    hr = CoGetClassObject(&CLSID_TestLocalServer, CLSCTX_LOCAL_SERVER, NULL, &IID_IClassFactory, (void **)&ClassFactory);
    ok(hr == REGDB_E_CLASSNOTREG, "CoGetClassObject after the server ended returned %#lx\n", hr);
    ok(ClassFactory == NULL, "ClassFactory is %p\n", ClassFactory);

    CloseHandle(ProcessInfo.hProcess);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(Event);
    CoUninitialize();
}
