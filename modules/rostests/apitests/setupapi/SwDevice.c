/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the software device API
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <stdio.h>

#define TEST_ENUMERATOR L"LiberNTSwDevTest"
#define TEST_INSTANCE   L"Inst0001"
#define CHILD_INSTANCE  L"Child0001"
#define TEST_DEVICE_ID  L"SWD\\" TEST_ENUMERATOR L"\\" TEST_INSTANCE
#define CHILD_DEVICE_ID L"SWD\\" TEST_ENUMERATOR L"\\" CHILD_INSTANCE

static const WCHAR TestHardwareIds[] = L"LiberNTSwDevTestHw1\0LiberNTSwDevTestHw2\0";
static const WCHAR TestCompatibleIds[] = L"LiberNTSwDevTestCompat\0";

typedef struct _CREATE_RESULT
{
    HANDLE Event;
    HSWDEVICE Device;
    HRESULT Result;
    PVOID Context;
    LONG Calls;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
} CREATE_RESULT, *PCREATE_RESULT;

static VOID WINAPI
CreateCallback(
    _In_ HSWDEVICE hSwDevice,
    _In_ HRESULT CreateResult,
    _In_opt_ PVOID pContext,
    _In_opt_ PCWSTR pszDeviceInstanceId)
{
    PCREATE_RESULT Result = pContext;

    Result->Device = hSwDevice;
    Result->Result = CreateResult;
    Result->Context = pContext;
    if (pszDeviceInstanceId)
        lstrcpynW(Result->InstanceId, pszDeviceInstanceId, ARRAYSIZE(Result->InstanceId));
    else
        Result->InstanceId[0] = UNICODE_NULL;
    InterlockedIncrement(&Result->Calls);
    SetEvent(Result->Event);
}

static VOID
InitCreateInfo(PSW_DEVICE_CREATE_INFO Info, PCWSTR InstanceId)
{
    ZeroMemory(Info, sizeof(*Info));
    Info->cbSize = sizeof(*Info);
    Info->pszInstanceId = InstanceId;
    Info->pszzHardwareIds = TestHardwareIds;
    Info->pszzCompatibleIds = TestCompatibleIds;
    Info->CapabilityFlags = SWDeviceCapabilitiesSilentInstall | SWDeviceCapabilitiesNoDisplayInUI;
    Info->pszDeviceDescription = L"LiberNT software device test";
}

static HRESULT
CreateTestDevice(PCWSTR InstanceId, PCREATE_RESULT Result, HSWDEVICE *Device)
{
    SW_DEVICE_CREATE_INFO Info;
    HRESULT hr;

    InitCreateInfo(&Info, InstanceId);
    ResetEvent(Result->Event);
    Result->Calls = 0;
    Result->Result = E_PENDING;
    Result->Device = NULL;
    Result->InstanceId[0] = UNICODE_NULL;
    *Device = NULL;
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL,
                        CreateCallback, Result, Device);
    if (SUCCEEDED(hr) && WaitForSingleObject(Result->Event, 15000) != WAIT_OBJECT_0)
        Result->Result = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    return hr;
}

static CONFIGRET
LocateDevice(PCWSTR DeviceId, ULONG Flags, DEVINST *DevInst)
{
    return CM_Locate_DevNodeW(DevInst, (DEVINSTID_W)DeviceId, Flags);
}

static BOOL
WaitForPresence(PCWSTR DeviceId, BOOL Present)
{
    DEVINST DevInst;
    DWORD Start = GetTickCount();

    for (;;)
    {
        BOOL Found = LocateDevice(DeviceId, CM_LOCATE_DEVNODE_NORMAL, &DevInst) == CR_SUCCESS;
        if (Found == Present)
            return TRUE;
        if (GetTickCount() - Start > 10000)
            return FALSE;
        Sleep(50);
    }
}

static BOOL
MultiSzEqual(PCWSTR Left, PCWSTR Right)
{
    for (;;)
    {
        if (_wcsicmp(Left, Right))
            return FALSE;
        if (!*Left)
            return TRUE;
        Left += wcslen(Left) + 1;
        Right += wcslen(Right) + 1;
    }
}

static CONFIGRET
GetProperty(DEVINST DevInst, ULONG Property, PWSTR Buffer, ULONG Count)
{
    ULONG Size = Count * sizeof(WCHAR);
    ULONG Type;

    ZeroMemory(Buffer, Size);
    return CM_Get_DevNode_Registry_PropertyW(DevInst, Property, &Type, Buffer, &Size, 0);
}

static VOID
UninstallTestDevice(PCWSTR DeviceId)
{
    DEVINST DevInst;

    if (LocateDevice(DeviceId, CM_LOCATE_DEVNODE_PHANTOM, &DevInst) == CR_SUCCESS)
        CM_Uninstall_DevNode(DevInst, 0);
}

static VOID
RunChild(VOID)
{
    CREATE_RESULT Result = { 0 };
    HSWDEVICE Device;
    HRESULT hr;

    Result.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    hr = CreateTestDevice(CHILD_INSTANCE, &Result, &Device);
    ExitProcess(SUCCEEDED(hr) && SUCCEEDED(Result.Result) &&
                WaitForPresence(CHILD_DEVICE_ID, TRUE) ? 0 : 1);
}

static VOID
TestParameters(PCREATE_RESULT Result)
{
    SW_DEVICE_CREATE_INFO Info;
    DEVPROPERTY Property;
    HSWDEVICE Device;
    HRESULT hr;

    InitCreateInfo(&Info, TEST_INSTANCE);

    Device = (HSWDEVICE)(ULONG_PTR)0xdeadbeef;
    hr = SwDeviceCreate(NULL, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "NULL enumerator: 0x%08lx\n", hr);
    ok(Device == NULL, "NULL enumerator: handle %p\n", Device);

    Device = (HSWDEVICE)(ULONG_PTR)0xdeadbeef;
    hr = SwDeviceCreate(L"", L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "empty enumerator: 0x%08lx\n", hr);

    hr = SwDeviceCreate(L"LiberNT\\Bad", L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "enumerator with a backslash: 0x%08lx\n", hr);

    hr = SwDeviceCreate(TEST_ENUMERATOR, NULL, &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "NULL parent: 0x%08lx\n", hr);

    Info.cbSize = sizeof(Info) - 1;
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "short cbSize: 0x%08lx\n", hr);
    Info.cbSize = 0;
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "zero cbSize: 0x%08lx\n", hr);
    Info.cbSize = sizeof(Info);

    Info.pszInstanceId = NULL;
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "NULL instance ID: 0x%08lx\n", hr);
    Info.pszInstanceId = L"";
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "empty instance ID: 0x%08lx\n", hr);
    Info.pszInstanceId = L"Bad\\Instance";
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "instance ID with a backslash: 0x%08lx\n", hr);
    Info.pszInstanceId = TEST_INSTANCE;

    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, NULL, Result, &Device);
    ok(hr == E_INVALIDARG, "NULL callback: 0x%08lx\n", hr);

    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 1, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "properties count without properties: 0x%08lx\n", hr);

    ZeroMemory(&Property, sizeof(Property));
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, &Property, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "properties without a count: 0x%08lx\n", hr);

    Info.CapabilityFlags = 0x10;
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "unknown capability: 0x%08lx\n", hr);

    Info.CapabilityFlags = SWDeviceCapabilitiesDriverRequired;
    Info.pszzHardwareIds = NULL;
    Info.pszzCompatibleIds = NULL;
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, &Device);
    ok(hr == E_INVALIDARG, "driver required without IDs: 0x%08lx\n", hr);

    ok(Result->Calls == 0, "callback called %ld times for rejected calls\n", Result->Calls);
}

static VOID
TestLifetime(PCREATE_RESULT Result)
{
    SW_DEVICE_LIFETIME Lifetime;
    HSWDEVICE Device;
    HRESULT hr;

    hr = CreateTestDevice(TEST_INSTANCE, Result, &Device);
    ok(hr == S_OK, "SwDeviceCreate: 0x%08lx\n", hr);
    ok(Result->Result == S_OK, "create result: 0x%08lx\n", Result->Result);
    if (FAILED(hr))
        return;

    hr = SwDeviceSetLifetime(Device, SWDeviceLifetimeMax);
    ok(hr == E_INVALIDARG, "SwDeviceSetLifetime(Max): 0x%08lx\n", hr);
    hr = SwDeviceSetLifetime(Device, (SW_DEVICE_LIFETIME)-1);
    ok(hr == E_INVALIDARG, "SwDeviceSetLifetime(-1): 0x%08lx\n", hr);
    hr = SwDeviceGetLifetime(Device, NULL);
    ok(hr == E_INVALIDARG, "SwDeviceGetLifetime(NULL): 0x%08lx\n", hr);

    hr = SwDeviceSetLifetime(Device, SWDeviceLifetimeParentPresent);
    ok(hr == S_OK, "SwDeviceSetLifetime(ParentPresent): 0x%08lx\n", hr);
    Lifetime = SWDeviceLifetimeMax;
    hr = SwDeviceGetLifetime(Device, &Lifetime);
    ok(hr == S_OK && Lifetime == SWDeviceLifetimeParentPresent,
       "SwDeviceGetLifetime: 0x%08lx, %d\n", hr, Lifetime);

    SwDeviceClose(Device);
    Sleep(1000);
    ok(WaitForPresence(TEST_DEVICE_ID, TRUE), "ParentPresent device gone after close\n");

    hr = CreateTestDevice(TEST_INSTANCE, Result, &Device);
    ok(hr == S_OK, "SwDeviceCreate on a persistent device: 0x%08lx\n", hr);
    ok(Result->Result == S_OK, "create result on a persistent device: 0x%08lx\n", Result->Result);
    if (FAILED(hr))
        return;
    Lifetime = SWDeviceLifetimeMax;
    hr = SwDeviceGetLifetime(Device, &Lifetime);
    ok(hr == S_OK && Lifetime == SWDeviceLifetimeParentPresent,
       "SwDeviceGetLifetime on a persistent device: 0x%08lx, %d\n", hr, Lifetime);

    hr = SwDeviceSetLifetime(Device, SWDeviceLifetimeHandle);
    ok(hr == S_OK, "SwDeviceSetLifetime(Handle): 0x%08lx\n", hr);
    SwDeviceClose(Device);
    ok(WaitForPresence(TEST_DEVICE_ID, FALSE), "Handle device still present after close\n");
}

static VOID
TestProcessExit(VOID)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    DWORD ExitCode = 1;
    BOOL Created;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    swprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%ls\" SwDevice child", Path);
    Created = CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo);
    ok(Created, "CreateProcessW failed: %lu\n", GetLastError());
    if (!Created)
        return;
    WaitForSingleObject(ProcessInfo.hProcess, 30000);
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    CloseHandle(ProcessInfo.hProcess);
    CloseHandle(ProcessInfo.hThread);
    ok(ExitCode == 0, "child exit code %lu\n", ExitCode);
    ok(WaitForPresence(CHILD_DEVICE_ID, FALSE), "device still present after its creator exited\n");
}

START_TEST(SwDevice)
{
    CREATE_RESULT Result = { 0 };
    WCHAR Buffer[512];
    DEVINST DevInst, Parent;
    HSWDEVICE Device, Second;
    SW_DEVICE_LIFETIME Lifetime;
    CREATE_RESULT SecondResult = { 0 };
    ULONG Status, Problem, Capabilities = 0, Size;
    PCWSTR Id;
    CONFIGRET cr;
    HRESULT hr;
    char **Arguments;

    if (winetest_get_mainargs(&Arguments) >= 3 && !strcmp(Arguments[2], "child"))
    {
        RunChild();
        return;
    }

    Result.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    SecondResult.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    UninstallTestDevice(TEST_DEVICE_ID);
    UninstallTestDevice(CHILD_DEVICE_ID);

    TestParameters(&Result);

    hr = CreateTestDevice(TEST_INSTANCE, &Result, &Device);
    if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
    {
        skip("SwDeviceCreate needs an administrator: 0x%08lx\n", hr);
        return;
    }
    ok(hr == S_OK, "SwDeviceCreate: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    ok(Device != NULL, "NULL handle\n");
    ok(Result.Calls == 1, "callback called %ld times\n", Result.Calls);
    ok(Result.Result == S_OK, "create result: 0x%08lx\n", Result.Result);
    ok(Result.Device == Device, "callback handle %p, expected %p\n", Result.Device, Device);
    ok(Result.Context == &Result, "callback context %p\n", Result.Context);
    ok(!_wcsicmp(Result.InstanceId, TEST_DEVICE_ID), "instance ID '%ls'\n", Result.InstanceId);
    ok(!wcscmp(Result.InstanceId, TEST_DEVICE_ID),
       "instance ID case '%ls'\n", Result.InstanceId);

    cr = LocateDevice(TEST_DEVICE_ID, CM_LOCATE_DEVNODE_NORMAL, &DevInst);
    ok(cr == CR_SUCCESS, "CM_Locate_DevNodeW: %lu\n", cr);
    if (cr == CR_SUCCESS)
    {
        cr = CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0);
        ok(cr == CR_SUCCESS, "CM_Get_DevNode_Status: %lu\n", cr);
        ok((Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM), "status 0x%08lx problem %lu\n", Status, Problem);
        trace("status 0x%08lx problem %lu\n", Status, Problem);

        cr = GetProperty(DevInst, CM_DRP_HARDWAREID, Buffer, ARRAYSIZE(Buffer));
        ok(cr == CR_SUCCESS && MultiSzEqual(Buffer, TestHardwareIds), "hardware IDs: %lu '%ls'\n", cr, Buffer);

        cr = GetProperty(DevInst, CM_DRP_COMPATIBLEIDS, Buffer, ARRAYSIZE(Buffer));
        ok(cr == CR_SUCCESS, "compatible IDs: %lu\n", cr);
        for (Id = Buffer; *Id; Id += wcslen(Id) + 1)
            trace("compatible ID '%ls'\n", Id);
        ok(MultiSzEqual(Buffer, L"LiberNTSwDevTestCompat\0SWD\\GenericRaw\0SWD\\Generic\0"),
           "compatible IDs '%ls'\n", Buffer);

        cr = GetProperty(DevInst, CM_DRP_DEVICEDESC, Buffer, ARRAYSIZE(Buffer));
        trace("device description %lu '%ls'\n", cr, Buffer);
        cr = GetProperty(DevInst, CM_DRP_CLASS, Buffer, ARRAYSIZE(Buffer));
        trace("class %lu '%ls'\n", cr, Buffer);
        Size = sizeof(Capabilities);
        cr = CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_CAPABILITIES, NULL, &Capabilities, &Size, 0);
        ok(cr == CR_SUCCESS && Capabilities == (CM_DEVCAP_UNIQUEID | CM_DEVCAP_SILENTINSTALL |
                                                CM_DEVCAP_RAWDEVICEOK | CM_DEVCAP_SURPRISEREMOVALOK),
           "capabilities %lu 0x%08lx\n", cr, Capabilities);

        cr = CM_Get_Parent(&Parent, DevInst, 0);
        ok(cr == CR_SUCCESS, "CM_Get_Parent: %lu\n", cr);
        if (cr == CR_SUCCESS)
        {
            cr = CM_Get_Device_IDW(Parent, Buffer, ARRAYSIZE(Buffer), 0);
            ok(cr == CR_SUCCESS && !_wcsicmp(Buffer, L"HTREE\\ROOT\\0"), "parent '%ls'\n", Buffer);
        }
    }

    Lifetime = SWDeviceLifetimeMax;
    hr = SwDeviceGetLifetime(Device, &Lifetime);
    ok(hr == S_OK && Lifetime == SWDeviceLifetimeHandle, "SwDeviceGetLifetime: 0x%08lx, %d\n", hr, Lifetime);

    hr = CreateTestDevice(TEST_INSTANCE, &SecondResult, &Second);
    ok(hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), "second create of an open device: 0x%08lx\n", hr);
    ok(SecondResult.Calls == 0, "second create called the callback %ld times\n", SecondResult.Calls);
    ok(Second == NULL, "second create returned handle %p\n", Second);
    if (SUCCEEDED(hr))
        SwDeviceClose(Second);

    SwDeviceClose(Device);
    trace("present right after SwDeviceClose: %d\n",
          LocateDevice(TEST_DEVICE_ID, CM_LOCATE_DEVNODE_NORMAL, &DevInst) == CR_SUCCESS);
    ok(WaitForPresence(TEST_DEVICE_ID, FALSE), "device still present after SwDeviceClose\n");
    cr = LocateDevice(TEST_DEVICE_ID, CM_LOCATE_DEVNODE_PHANTOM, &DevInst);
    ok(cr == CR_SUCCESS, "phantom device not found: %lu\n", cr);

    TestLifetime(&Result);
    TestProcessExit();

    UninstallTestDevice(TEST_DEVICE_ID);
    UninstallTestDevice(CHILD_DEVICE_ID);
    CloseHandle(Result.Event);
    CloseHandle(SecondResult.Event);
}
