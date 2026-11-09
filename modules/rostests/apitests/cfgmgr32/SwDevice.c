/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for software devices (SwDeviceCreate and related functions)
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include <apitest.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <sddl.h>
#include <swdevice.h>
#include <stdio.h>

#define TEST_ENUMERATOR L"LntSwTest"
#define TEST_HARDWARE_ID L"LntSwTestNoDriver\0"

typedef struct _CREATED
{
    HANDLE Event;
    HRESULT Result;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
} CREATED;

static VOID WINAPI
Created(HSWDEVICE hSwDevice, HRESULT CreateResult, PVOID Context, PCWSTR DeviceInstanceId)
{
    CREATED *State = Context;

    UNREFERENCED_PARAMETER(hSwDevice);
    State->Result = CreateResult;
    if (DeviceInstanceId)
        lstrcpynW(State->InstanceId, DeviceInstanceId, MAX_DEVICE_ID_LEN);
    SetEvent(State->Event);
}

static HRESULT
Create(PCWSTR Instance, ULONG Capabilities, PSECURITY_DESCRIPTOR Security, CREATED *State, HSWDEVICE *Device)
{
    SW_DEVICE_CREATE_INFO Info = { sizeof(Info) };
    HRESULT hr;

    Info.pszInstanceId = Instance;
    Info.pszzHardwareIds = TEST_HARDWARE_ID;
    Info.pszDeviceDescription = L"LiberNT software device test";
    Info.CapabilityFlags = Capabilities;
    Info.pSecurityDescriptor = Security;
    ZeroMemory(State->InstanceId, sizeof(State->InstanceId));
    State->Result = E_PENDING;
    ResetEvent(State->Event);
    hr = SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, Created, State, Device);
    if (SUCCEEDED(hr) && WaitForSingleObject(State->Event, 30000) != WAIT_OBJECT_0)
        hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    return hr;
}

static BOOL
IsPresent(PCWSTR InstanceId)
{
    DEVINST DevInst;

    return CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

static BOOL
WaitGone(PCWSTR InstanceId)
{
    DWORD Start = GetTickCount();

    while (IsPresent(InstanceId) && GetTickCount() - Start < 10000)
        Sleep(200);
    return !IsPresent(InstanceId);
}

static void
test_Lifetime(CREATED *State)
{
    HSWDEVICE Device;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    HRESULT hr;

    hr = Create(L"Lifetime", SWDeviceCapabilitiesSilentInstall, NULL, State, &Device);
    ok(hr == S_OK, "SwDeviceCreate returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    ok(State->Result == S_OK, "The creation reported 0x%08lx\n", State->Result);
    lstrcpyW(InstanceId, State->InstanceId);
    ok(IsPresent(InstanceId), "%S is not present\n", InstanceId);

    hr = SwDeviceSetLifetime(Device, SWDeviceLifetimeParentPresent);
    ok(hr == S_OK, "SwDeviceSetLifetime returned 0x%08lx\n", hr);
    SwDeviceClose(Device);
    ok(IsPresent(InstanceId), "%S left with its handle\n", InstanceId);

    hr = Create(L"Lifetime", SWDeviceCapabilitiesSilentInstall, NULL, State, &Device);
    ok(hr == S_OK, "SwDeviceCreate of the persistent device returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    ok(State->Result == S_OK, "Opening the persistent device reported 0x%08lx\n", State->Result);

    hr = SwDeviceSetLifetime(Device, SWDeviceLifetimeHandle);
    ok(hr == S_OK, "SwDeviceSetLifetime returned 0x%08lx\n", hr);
    SwDeviceClose(Device);
    ok(WaitGone(InstanceId), "%S stayed after its handle closed\n", InstanceId);
}

static void
test_DriverRequired(CREATED *State)
{
    HSWDEVICE Device;
    ULONG Status = 0, Problem = 0;
    DEVINST DevInst;
    HRESULT hr;

    hr = Create(L"DriverRequired", SWDeviceCapabilitiesSilentInstall | SWDeviceCapabilitiesDriverRequired,
                NULL, State, &Device);
    ok(hr == S_OK, "SwDeviceCreate returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    trace("DriverRequired without a driver reported 0x%08lx\n", State->Result);
    ok(State->Result == S_OK, "The creation reported 0x%08lx\n", State->Result);
    if (CM_Locate_DevNodeW(&DevInst, State->InstanceId, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
    {
        CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0);
        trace("Status 0x%lx, problem %lu\n", Status, Problem);
        ok(Status & DN_HAS_PROBLEM, "A device without its required driver has no problem\n");
    }
    else
    {
        ok(0, "The device without its required driver is not present\n");
    }
    SwDeviceClose(Device);
    WaitGone(State->InstanceId);
}

static void
test_SecurityDescriptor(CREATED *State)
{
    PSECURITY_DESCRIPTOR Security = NULL, NoOwner = NULL;
    HSWDEVICE Device;
    HRESULT hr;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GR;;;BA)", SDDL_REVISION_1,
                                                              &Security, NULL) ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GR;;;BA)", SDDL_REVISION_1,
                                                              &NoOwner, NULL))
    {
        skip("No security descriptor (%lu)\n", GetLastError());
        LocalFree(Security);
        return;
    }

    hr = Create(L"SecurityNoOwner", SWDeviceCapabilitiesSilentInstall, NoOwner, State, &Device);
    ok(hr == E_INVALIDARG, "SwDeviceCreate without an owner returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        SwDeviceClose(Device);
        WaitGone(State->InstanceId);
    }

    hr = Create(L"Security", SWDeviceCapabilitiesSilentInstall, Security, State, &Device);
    ok(hr == S_OK, "SwDeviceCreate returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(State->Result == S_OK, "The creation reported 0x%08lx\n", State->Result);
        SwDeviceClose(Device);
        WaitGone(State->InstanceId);
    }
    LocalFree(NoOwner);
    LocalFree(Security);
}

static void
RunChild(void)
{
    CREATED State;
    HSWDEVICE Device;

    State.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (SUCCEEDED(Create(L"ProcessEnd", SWDeviceCapabilitiesSilentInstall, NULL, &State, &Device)) &&
        State.Result == S_OK)
    {
        ExitProcess(0);
    }
    ExitProcess(1);
}

static void
test_ProcessEnd(void)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    WCHAR InstanceId[] = L"SWD\\" TEST_ENUMERATOR L"\\ProcessEnd";
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    DWORD ExitCode = 1;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    _snwprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" SwDevice child", Path);
    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        skip("CreateProcessW failed (%lu)\n", GetLastError());
        return;
    }
    WaitForSingleObject(Process.hProcess, 60000);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    ok(ExitCode == 0, "The child failed to create its device\n");
    if (ExitCode == 0)
        ok(WaitGone(InstanceId), "%S stayed after the process holding its handle ended\n", InstanceId);
}

START_TEST(SwDevice)
{
    CREATED State;
    char **argv;

    if (winetest_get_mainargs(&argv) >= 3 && !strcmp(argv[2], "child"))
        RunChild();

    State.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    test_Lifetime(&State);
    test_DriverRequired(&State);
    test_SecurityDescriptor(&State);
    test_ProcessEnd();
    CloseHandle(State.Event);
}
