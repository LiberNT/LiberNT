/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     hdgfx Gen 5 user batches are limited to the active console session
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"
#include <winsvc.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>
#include <drivers/directx/hdgfx_umd.h>
#include "../advapi32/svchlp.h"

static SERVICE_STATUS_HANDLE SessionStatusHandle;
static LUID SessionLuid;

static void SessionReportStatus(DWORD State)
{
    SERVICE_STATUS Status = {0};

    Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    Status.dwCurrentState = State;
    SetServiceStatus(SessionStatusHandle, &Status);
}

static VOID WINAPI SessionHandler(DWORD Control)
{
    UNREFERENCED_PARAMETER(Control);
}

static BOOL SessionIsIronlake(D3DKMT_HANDLE hAdapter)
{
    PFND3DKMT_QUERYADAPTERINFO QueryAdapterInfo;
    D3DKMT_QUERY_DEVICE_IDS Ids = {0};
    D3DKMT_QUERYADAPTERINFO Query = {0};

    QueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    if (!QueryAdapterInfo)
        return FALSE;
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
    Query.pPrivateDriverData = &Ids;
    Query.PrivateDriverDataSize = sizeof(Ids);
    if (!NT_SUCCESS(QueryAdapterInfo(&Query)) || Ids.DeviceIds.VendorID != 0x8086)
        return FALSE;
    return Ids.DeviceIds.DeviceID == 0x0042 || Ids.DeviceIds.DeviceID == 0x0046;
}

static NTSTATUS SessionCreateContext(PFND3DKMT_CREATECONTEXT CreateContext, PFND3DKMT_DESTROYCONTEXT DestroyContext,
                                     D3DKMT_HANDLE hDevice, BOOL Gen)
{
    HDGFX_GEN_CONTEXT_DATA Data = { HDGFX_GEN_CONTEXT_MAGIC, HDGFX_GEN_CONTEXT_VERSION };
    D3DKMT_CREATECONTEXT Create = {0};
    D3DKMT_DESTROYCONTEXT Destroy = {0};
    NTSTATUS Status;

    Create.hDevice = hDevice;
    if (Gen)
    {
        Create.pPrivateDriverData = &Data;
        Create.PrivateDriverDataSize = sizeof(Data);
    }
    Status = CreateContext(&Create);
    if (NT_SUCCESS(Status))
    {
        Destroy.hContext = Create.hContext;
        DestroyContext(&Destroy);
    }
    return Status;
}

static void SessionRunInService(void)
{
    PFND3DKMT_OPENADAPTERFROMLUID OpenAdapterFromLuid;
    PFND3DKMT_CREATEDEVICE CreateDevice;
    PFND3DKMT_DESTROYDEVICE DestroyDevice;
    PFND3DKMT_CREATECONTEXT CreateContext;
    PFND3DKMT_DESTROYCONTEXT DestroyContext;
    D3DKMT_OPENADAPTERFROMLUID Open = {0};
    D3DKMT_CREATEDEVICE Device = {0};
    D3DKMT_DESTROYDEVICE Destroy = {0};
    DWORD SessionId = 0;
    NTSTATUS Status;
    BOOL Ironlake;

    service_ok(ProcessIdToSessionId(GetCurrentProcessId(), &SessionId), "ProcessIdToSessionId: %lu\n", GetLastError());
    service_ok(SessionId != WTSGetActiveConsoleSessionId(), "service runs in the console session %lu\n", SessionId);

    OpenAdapterFromLuid = (PFND3DKMT_OPENADAPTERFROMLUID)LoadD3DKMTProc("D3DKMTOpenAdapterFromLuid");
    CreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    DestroyDevice = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    CreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    DestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    if (!OpenAdapterFromLuid || !CreateDevice || !DestroyDevice || !CreateContext || !DestroyContext)
    {
        service_ok(FALSE, "D3DKMT entry points missing\n");
        return;
    }
    Open.AdapterLuid = SessionLuid;
    Status = OpenAdapterFromLuid(&Open);
    service_ok(NT_SUCCESS(Status), "OpenAdapterFromLuid: 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Ironlake = SessionIsIronlake(Open.hAdapter);
    Device.hAdapter = Open.hAdapter;
    Status = CreateDevice(&Device);
    service_ok(NT_SUCCESS(Status), "CreateDevice: 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = SessionCreateContext(CreateContext, DestroyContext, Device.hDevice, FALSE);
        service_ok(NT_SUCCESS(Status), "2D context outside the console session: 0x%08lx\n", Status);
        Status = SessionCreateContext(CreateContext, DestroyContext, Device.hDevice, TRUE);
        if (Ironlake)
            service_ok(Status == STATUS_ACCESS_DENIED, "Ironlake Gen context outside the console session: 0x%08lx\n", Status);
        else
            service_ok(NT_SUCCESS(Status), "Gen 6/7 context outside the console session: 0x%08lx\n", Status);
        Destroy.hDevice = Device.hDevice;
        DestroyDevice(&Destroy);
    }
    CloseAdapter(Open.hAdapter);
}

static VOID WINAPI SessionServiceMain(DWORD Argc, LPWSTR *Argv)
{
    UNREFERENCED_PARAMETER(Argc);

    SessionStatusHandle = RegisterServiceCtrlHandlerW(Argv[0], SessionHandler);
    service_ok(SessionStatusHandle != NULL, "RegisterServiceCtrlHandler: %lu\n", GetLastError());
    if (!SessionStatusHandle)
        return;
    SessionReportStatus(SERVICE_RUNNING);
    SessionRunInService();
    SessionReportStatus(SERVICE_STOPPED);
}

static BOOL SessionStartService(PCSTR ServiceNameA, PCWSTR ServiceNameW)
{
    SERVICE_TABLE_ENTRYW Table[] = { { (PWSTR)ServiceNameW, SessionServiceMain }, { NULL, NULL } };
    BOOL Result;

    UNREFERENCED_PARAMETER(ServiceNameA);
    Result = StartServiceCtrlDispatcherW(Table);
    service_ok(Result, "StartServiceCtrlDispatcherW: %lu\n", GetLastError());
    return Result;
}

static void SessionServer(PCSTR ServiceNameA, PCWSTR ServiceNameW, void *Param)
{
    CHAR Args[64];
    SC_HANDLE hScm;
    SC_HANDLE hService;
    SERVICE_STATUS Status = {0};
    UINT Waited;

    UNREFERENCED_PARAMETER(ServiceNameW);
    UNREFERENCED_PARAMETER(Param);
    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hScm)
    {
        skip("OpenSCManager: %lu\n", GetLastError());
        return;
    }
    StringCbPrintfA(Args, sizeof(Args), "%lx %lx", SessionLuid.LowPart, (ULONG)SessionLuid.HighPart);
    hService = register_serviceA(hScm, "hdgfx_gen_session", ServiceNameA, Args);
    if (hService)
    {
        ok(StartServiceW(hService, 0, NULL), "StartService: %lu\n", GetLastError());
        for (Waited = 0; Waited < 100; Waited++)
        {
            if (!QueryServiceStatus(hService, &Status) || Status.dwCurrentState == SERVICE_STOPPED)
                break;
            Sleep(100);
        }
        ok(Status.dwCurrentState == SERVICE_STOPPED, "service state %lu\n", Status.dwCurrentState);
        DeleteService(hService);
        CloseServiceHandle(hService);
    }
    CloseServiceHandle(hScm);
}

START_TEST(hdgfx_gen_session)
{
    PFN_D3DKMTOpenAdapterFromGdiDisplayName OpenAdapterFromGdiDisplayName;
    PFND3DKMT_CREATEDEVICE CreateDevice;
    PFND3DKMT_DESTROYDEVICE DestroyDevice;
    PFND3DKMT_CREATECONTEXT CreateContext;
    PFND3DKMT_DESTROYCONTEXT DestroyContext;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open = {0};
    D3DKMT_CREATEDEVICE Device = {0};
    D3DKMT_DESTROYDEVICE Destroy = {0};
    NTSTATUS Status;
    char **Argv;
    int Argc;

    Argc = winetest_get_mainargs(&Argv);
    if (Argc >= 5)
    {
        SessionLuid.LowPart = strtoul(Argv[3], NULL, 16);
        SessionLuid.HighPart = (LONG)strtoul(Argv[4], NULL, 16);
        service_process(SessionStartService, Argc, Argv);
        return;
    }

    OpenAdapterFromGdiDisplayName =
        (PFN_D3DKMTOpenAdapterFromGdiDisplayName)LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
    CreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    DestroyDevice = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    CreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    DestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    if (!OpenAdapterFromGdiDisplayName || !CreateDevice || !DestroyDevice || !CreateContext || !DestroyContext)
    {
        skip("D3DKMT entry points not exported\n");
        return;
    }
    wcscpy(Open.DeviceName, L"\\\\.\\DISPLAY1");
    if (!NT_SUCCESS(OpenAdapterFromGdiDisplayName(&Open)))
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    SessionLuid = Open.AdapterLuid;
    Device.hAdapter = Open.hAdapter;
    Status = CreateDevice(&Device);
    ok_succeeded(Status, "CreateDevice: 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = SessionCreateContext(CreateContext, DestroyContext, Device.hDevice, TRUE);
        Destroy.hDevice = Device.hDevice;
        DestroyDevice(&Destroy);
    }
    CloseAdapter(Open.hAdapter);
    if (!NT_SUCCESS(Status))
    {
        skip("no hdgfx Gen context in the console session (0x%08lx)\n", Status);
        return;
    }
    test_runner(SessionServer, NULL);
}
