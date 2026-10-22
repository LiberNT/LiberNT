/*
 * PROJECT:     LiberNT system tray
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth notification icon
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <dbt.h>
#include <initguid.h>
#include <bthdef.h>

static HICON g_hIconBluetooth = NULL;
static WCHAR g_szBluetoothTip[64];

static BOOL
Bluetooth_RadioPresent(VOID)
{
    SP_DEVICE_INTERFACE_DATA Data = { sizeof(Data) };
    HDEVINFO hSet;
    BOOL bPresent;

    hSet = SetupDiGetClassDevsW(&GUID_BTHPORT_DEVICE_INTERFACE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hSet == INVALID_HANDLE_VALUE)
        return FALSE;
    bPresent = SetupDiEnumDeviceInterfaces(hSet, NULL, &GUID_BTHPORT_DEVICE_INTERFACE, 0, &Data);
    SetupDiDestroyDeviceInfoList(hSet);
    return bPresent;
}

static VOID
Bluetooth_ShowFlyout(VOID)
{
    HWND hwndTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);

    if (!hwndTaskbar || !SendMessageW(hwndTaskbar, WM_USER + 274, 0, 0))
        ShellExecuteW(NULL, NULL, L"ms-settings:bluetooth", NULL, NULL, SW_SHOWNORMAL);
}

static VOID
Bluetooth_ShowContextMenu(CSysTray *pSysTray)
{
    WCHAR szShow[128], szSettings[128];
    HMENU hPopup;
    POINT pt;
    UINT id;

    LoadStringW(g_hInstance, IDS_BT_SHOW, szShow, _countof(szShow));
    LoadStringW(g_hInstance, IDS_BT_SETTINGS, szSettings, _countof(szSettings));
    hPopup = CreatePopupMenu();
    AppendMenuW(hPopup, MF_STRING, IDS_BT_SHOW, szShow);
    AppendMenuW(hPopup, MF_STRING, IDS_BT_SETTINGS, szSettings);
    SetMenuDefaultItem(hPopup, IDS_BT_SHOW, FALSE);
    SetForegroundWindow(pSysTray->GetHWnd());
    GetCursorPos(&pt);
    id = TrackPopupMenuEx(hPopup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                          pt.x, pt.y, pSysTray->GetHWnd(), NULL);
    DestroyMenu(hPopup);

    if (id == IDS_BT_SHOW)
        Bluetooth_ShowFlyout();
    else if (id == IDS_BT_SETTINGS)
        ShellExecuteW(NULL, NULL, L"ms-settings:bluetooth", NULL, NULL, SW_SHOWNORMAL);
}

HRESULT STDMETHODCALLTYPE Bluetooth_Init(_In_ CSysTray * pSysTray)
{
    g_hIconBluetooth = StoLoadTrayIcon(g_hInstance, IDI_BLUETOOTH);
    if (!LoadStringW(g_hInstance, IDS_BT_DEVICES, g_szBluetoothTip, _countof(g_szBluetoothTip)))
        g_szBluetoothTip[0] = UNICODE_NULL;
    return pSysTray->NotifyIcon(NIM_ADD, ID_ICON_BLUETOOTH, g_hIconBluetooth, g_szBluetoothTip,
                                Bluetooth_RadioPresent() ? 0 : NIS_HIDDEN);
}

HRESULT STDMETHODCALLTYPE Bluetooth_Update(_In_ CSysTray * pSysTray)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Bluetooth_Shutdown(_In_ CSysTray * pSysTray)
{
    KillTimer(pSysTray->GetHWnd(), BLUETOOTH_TIMER_ID);
    if (g_hIconBluetooth)
    {
        DestroyIcon(g_hIconBluetooth);
        g_hIconBluetooth = NULL;
    }
    return pSysTray->NotifyIcon(NIM_DELETE, ID_ICON_BLUETOOTH, NULL, NULL);
}

HRESULT STDMETHODCALLTYPE Bluetooth_Message(_In_ CSysTray * pSysTray, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult)
{
    switch (uMsg)
    {
        case WM_DEVICECHANGE:
            if (wParam == DBT_DEVNODES_CHANGED && pSysTray->IsServiceEnabled(BLUETOOTH_SERVICE_FLAG))
                SetTimer(pSysTray->GetHWnd(), BLUETOOTH_TIMER_ID, 200, NULL);
            return S_FALSE;

        case WM_TIMER:
            if (wParam != BLUETOOTH_TIMER_ID)
                return S_FALSE;
            KillTimer(pSysTray->GetHWnd(), BLUETOOTH_TIMER_ID);
            pSysTray->NotifyIcon(NIM_MODIFY, ID_ICON_BLUETOOTH, g_hIconBluetooth, g_szBluetoothTip,
                                 Bluetooth_RadioPresent() ? 0 : NIS_HIDDEN);
            return S_OK;

        case ID_ICON_BLUETOOTH:
            switch (lParam)
            {
                case WM_LBUTTONUP:
                    Bluetooth_ShowFlyout();
                    break;

                case WM_RBUTTONUP:
                    Bluetooth_ShowContextMenu(pSysTray);
                    break;
            }
            return S_OK;
    }

    return S_FALSE;
}
