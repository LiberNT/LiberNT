/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D12Device::SetStablePowerState
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"
#include <stdio.h>

#define APP_MODEL_UNLOCK L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock"
#define DEVELOPER_MODE L"AllowDevelopmentWithoutDevLicense"

static DWORD DeveloperModeValue(void)
{
    DWORD value = 0, size = sizeof(value);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, APP_MODEL_UNLOCK, DEVELOPER_MODE, RRF_RT_REG_DWORD, NULL, &value, &size) != ERROR_SUCCESS)
        return 0;
    return value;
}

static BOOL WriteDeveloperMode(DWORD value)
{
    HKEY key;
    LONG status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, APP_MODEL_UNLOCK, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);

    if (status != ERROR_SUCCESS)
        return FALSE;
    status = RegSetValueExW(key, DEVELOPER_MODE, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

static void TestDeveloperModeOff(void)
{
    ID3D12Device *device = d3d12_test_create_device();
    HRESULT hr;

    if (!device)
    {
        skip("no device\n");
        return;
    }
    hr = device->SetStablePowerState(TRUE);
    ok(hr == E_FAIL, "Enable without developer mode: %#lx\n", hr);
    hr = device->GetDeviceRemovedReason();
    ok(hr == DXGI_ERROR_INVALID_CALL, "Removal reason without developer mode: %#lx\n", hr);
    hr = device->SetStablePowerState(FALSE);
    ok(hr == E_FAIL, "Disable without developer mode: %#lx\n", hr);
    device->Release();
}

static void TestDeveloperModeOn(void)
{
    ID3D12Device *device = d3d12_test_create_device();
    HRESULT hr;

    if (!device)
    {
        skip("no device\n");
        return;
    }
    hr = device->SetStablePowerState(TRUE);
    ok(hr == S_OK, "Enable: %#lx\n", hr);
    hr = device->SetStablePowerState(TRUE);
    ok(hr == S_OK, "Enable again: %#lx\n", hr);
    hr = device->SetStablePowerState(FALSE);
    ok(hr == S_OK, "Disable: %#lx\n", hr);
    hr = device->SetStablePowerState(FALSE);
    ok(hr == S_OK, "Disable without a request: %#lx\n", hr);
    hr = device->GetDeviceRemovedReason();
    ok(hr == S_OK, "Removal reason: %#lx\n", hr);
    device->Release();
}

static BOOL ClearDeveloperMode(void)
{
    HKEY key;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, APP_MODEL_UNLOCK, 0, KEY_SET_VALUE, &key);

    if (status != ERROR_SUCCESS)
        return FALSE;
    status = RegDeleteValueW(key, DEVELOPER_MODE);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

static DWORD RunDeveloperModeChild(void)
{
    WCHAR path[MAX_PATH], command[MAX_PATH + 32];
    STARTUPINFOW startup = {};
    PROCESS_INFORMATION process = {};
    DWORD code = ~0u;

    startup.cb = sizeof(startup);
    if (!GetModuleFileNameW(NULL, path, MAX_PATH))
        return code;
    _snwprintf(command, ARRAYSIZE(command), L"\"%ls\" stablepower", path);
    if (!CreateProcessW(path, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process))
        return code;
    if (WaitForSingleObject(process.hProcess, D3D12_TEST_BUDGET / 2) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &code);
    else
        TerminateProcess(process.hProcess, code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

static void RunStablePower(void *)
{
    DWORD code;

    if (DeveloperModeValue())
    {
        TestDeveloperModeOn();
        return;
    }
    TestDeveloperModeOff();
    if (!WriteDeveloperMode(1))
    {
        skip("developer mode cannot be changed\n");
        return;
    }
    /* Windows 11 does not honor the change in a process that was already refused. */
    code = RunDeveloperModeChild();
    ok(code == 0, "Developer mode child returned %lu\n", code);
    ok(ClearDeveloperMode(), "Restore developer mode\n");
}

START_TEST(stablepower)
{
    d3d12_test_bounded("stablepower", RunStablePower, NULL, D3D12_TEST_BUDGET);
}
