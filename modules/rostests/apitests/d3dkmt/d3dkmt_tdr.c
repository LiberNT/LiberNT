/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Forced GPU timeout recovery through D3DKMT_ESCAPE_TDRDBGCTRL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <winreg.h>

static const WCHAR graphics_drivers_key[] = L"System\\CurrentControlSet\\Control\\GraphicsDrivers";

static NTSTATUS TdrControl(PFN_D3DKMTEscape pfnEscape, D3DKMT_HANDLE hAdapter, UINT Control)
{
    D3DKMT_ESCAPE Escape;

    memset(&Escape, 0, sizeof(Escape));
    Escape.hAdapter = hAdapter;
    Escape.Type = D3DKMT_ESCAPE_TDRDBGCTRL;
    Escape.pPrivateDriverData = &Control;
    Escape.PrivateDriverDataSize = sizeof(Control);
    return pfnEscape(&Escape);
}

static D3DKMT_DEVICEEXECUTION_STATE ExecutionState(PFN_D3DKMTGetDeviceState pfnGetDeviceState, D3DKMT_HANDLE hDevice)
{
    D3DKMT_GETDEVICESTATE State;

    memset(&State, 0, sizeof(State));
    State.hDevice = hDevice;
    State.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    if (!NT_SUCCESS(pfnGetDeviceState(&State)))
        return (D3DKMT_DEVICEEXECUTION_STATE)0;
    return State.ExecutionState;
}

static BOOL SetTestMode(HKEY Key, DWORD Value, BOOL Delete)
{
    if (Delete)
    {
        LONG Error = RegDeleteValueW(Key, L"TdrTestMode");
        return Error == ERROR_SUCCESS || Error == ERROR_FILE_NOT_FOUND;
    }
    return RegSetValueExW(Key, L"TdrTestMode", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value)) == ERROR_SUCCESS;
}

START_TEST(D3dkmtTdr)
{
    D3DKMT_DEVICEEXECUTION_STATE State = D3DKMT_DEVICEEXECUTION_ACTIVE;
    D3DKMT_HANDLE hAdapter, hDevice, hNewDevice;
    DWORD OldValue = 0, Size = sizeof(OldValue), Type = 0;
    BOOL HadValue;
    HKEY Key;
    NTSTATUS Status;
    ULONG Waited;

    LOAD_D3DKMT(D3DKMTEscape);
    LOAD_D3DKMT(D3DKMTGetDeviceState);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open the display adapter\n");
        return;
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, graphics_drivers_key, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL,
            &Key, NULL) != ERROR_SUCCESS)
    {
        skip("GraphicsDrivers key not writable (not an administrator)\n");
        CloseAdapter(hAdapter);
        return;
    }
    HadValue = RegQueryValueExW(Key, L"TdrTestMode", NULL, &Type, (BYTE *)&OldValue, &Size) == ERROR_SUCCESS
            && Type == REG_DWORD;

    ok(SetTestMode(Key, 0, TRUE), "could not clear TdrTestMode\n");
    Status = TdrControl(pfnD3DKMTEscape, hAdapter, D3DKMT_TDRDBGCTRLTYPE_FORCETDR);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);

    if (!SetTestMode(Key, 1, FALSE))
    {
        skip("could not set TdrTestMode\n");
        RegCloseKey(Key);
        CloseAdapter(hAdapter);
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    ok(hDevice != 0, "CreateDevice failed\n");
    if (hDevice)
        ok_eq_long(ExecutionState(pfnD3DKMTGetDeviceState, hDevice), D3DKMT_DEVICEEXECUTION_ACTIVE);

    Status = TdrControl(pfnD3DKMTEscape, hAdapter, D3DKMT_TDRDBGCTRLTYPE_FORCETDR);
    ok_eq_hex(Status, STATUS_SUCCESS);
    for (Waited = 0; hDevice && NT_SUCCESS(Status) && Waited < 10000; Waited += 100)
    {
        State = ExecutionState(pfnD3DKMTGetDeviceState, hDevice);
        if (State != D3DKMT_DEVICEEXECUTION_ACTIVE)
            break;
        Sleep(100);
    }
    trace("device state %d after %lu ms\n", State, Waited);
    ok(State == D3DKMT_DEVICEEXECUTION_RESET || State == D3DKMT_DEVICEEXECUTION_HUNG,
       "the device survived the forced timeout in state %d\n", State);

    hNewDevice = CreateTestDevice(hAdapter);
    ok(hNewDevice != 0, "CreateDevice after the recovery failed\n");
    if (hNewDevice)
    {
        ok_eq_long(ExecutionState(pfnD3DKMTGetDeviceState, hNewDevice), D3DKMT_DEVICEEXECUTION_ACTIVE);
        DestroyTestDevice(hNewDevice);
    }
    if (hDevice)
        DestroyTestDevice(hDevice);

    ok(HadValue ? SetTestMode(Key, OldValue, FALSE) : SetTestMode(Key, 0, TRUE), "could not restore TdrTestMode\n");
    RegCloseKey(Key);
    CloseAdapter(hAdapter);
}
