/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Automatic network switching: autoconnect must pick a network by
 *              itself, survive the network dropping the station, and switch to
 *              a newly available preferred network without any WlanConnect call
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */
#include <apitest.h>
#include <winreg.h>
#include <winuser.h>
#include <wlanapi.h>
#include <setupapi.h>
#include "vwifi.h"

static const WCHAR OpenAutoXml[] =
    L"<?xml version=\"1.0\"?>"
    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
    L"<name>ReactOS-Open</name>"
    L"<SSIDConfig><SSID><name>ReactOS-Open</name></SSID></SSIDConfig>"
    L"<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>"
    L"<MSM><security><authEncryption><authentication>open</authentication>"
    L"<encryption>none</encryption><useOneX>false</useOneX></authEncryption>"
    L"</security></MSM></WLANProfile>";

static const WCHAR RoamAutoXml[] =
    L"<?xml version=\"1.0\"?>"
    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
    L"<name>ReactOS-Roam</name>"
    L"<SSIDConfig><SSID><name>ReactOS-Roam</name></SSID></SSIDConfig>"
    L"<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>"
    L"<MSM><security><authEncryption><authentication>open</authentication>"
    L"<encryption>none</encryption><useOneX>false</useOneX></authEncryption>"
    L"</security></MSM></WLANProfile>";

static CRITICAL_SECTION Lock;
static volatile LONG ConnComplete;
static volatile LONG Disconnected;

static VOID WINAPI NotificationCallback(PWLAN_NOTIFICATION_DATA Data, PVOID Context)
{
    (void)Context;
    if (Data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM)
        return;
    if (Data->NotificationCode == wlan_notification_acm_connection_complete)
        InterlockedIncrement(&ConnComplete);
    if (Data->NotificationCode == wlan_notification_acm_disconnected)
        InterlockedIncrement(&Disconnected);
}

static DWORD QueryState(HANDLE Client, const GUID *Interface)
{
    DWORD Size = 0;
    PWLAN_CONNECTION_ATTRIBUTES Attr = NULL;
    DWORD State = wlan_interface_state_not_ready;
    if (WlanQueryInterface(Client, Interface, wlan_intf_opcode_current_connection, NULL,
                           &Size, (PVOID *)&Attr, NULL) == ERROR_SUCCESS && Attr != NULL)
    {
        State = Attr->isState;
        WlanFreeMemory(Attr);
    }
    return (DWORD)State;
}

static void ProfileNameOf(HANDLE Client, const GUID *Interface, WCHAR *Out, DWORD OutLen)
{
    DWORD Size = 0;
    PWLAN_CONNECTION_ATTRIBUTES Attr = NULL;
    Out[0] = 0;
    if (WlanQueryInterface(Client, Interface, wlan_intf_opcode_current_connection, NULL,
                           &Size, (PVOID *)&Attr, NULL) == ERROR_SUCCESS && Attr != NULL)
    {
        if (Attr->isState == wlan_interface_state_connected)
            lstrcpynW(Out, Attr->strProfileName, OutLen);
        WlanFreeMemory(Attr);
    }
}

static BOOL WaitConnectedTo(HANDLE Client, const GUID *Interface, const WCHAR *Ssid, DWORD Ms)
{
    DWORD elapsed = 0;
    WCHAR name[64];
    while (elapsed < Ms)
    {
        if (QueryState(Client, Interface) == wlan_interface_state_connected)
        {
            ProfileNameOf(Client, Interface, name, ARRAYSIZE(name));
            if (Ssid == NULL || lstrcmpW(name, Ssid) == 0)
                return TRUE;
        }
        Sleep(250);
        elapsed += 250;
    }
    return FALSE;
}

static void TestAutoConnectAndSwitch(HANDLE Client, const GUID *Interface)
{
    DWORD Error;
    DWORD Reason = 0;
    LONG connBefore;
    WCHAR name[64];

    /* Phase 1: a single auto profile must connect with no WlanConnect call. */
    connBefore = ConnComplete;
    Error = WlanSetProfile(Client, Interface, 0, RoamAutoXml, NULL, TRUE, NULL, &Reason);
    ok(Error == ERROR_SUCCESS, "Setting the Roam profile failed: %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    ok(WaitConnectedTo(Client, Interface, L"ReactOS-Roam", 20000),
       "No automatic connection to ReactOS-Roam within 20s (state %lu)\n",
       QueryState(Client, Interface));
    ok(ConnComplete > connBefore, "No connection-complete notification for the autoconnect\n");

    /* Phase 2: with the Open profile also present, an explicit disconnect must
     * let autoconnect fire by itself and pick the strongest network: Open sits
     * at -40 dBm against Roam's -50, so the service must switch networks. */
    Error = WlanSetProfile(Client, Interface, 0, OpenAutoXml, NULL, TRUE, NULL, &Reason);
    ok(Error == ERROR_SUCCESS, "Setting the Open profile failed: %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        goto Cleanup;
    {
        LONG connBefore2 = ConnComplete;
        Error = WlanDisconnect(Client, Interface, NULL);
        ok(Error == ERROR_SUCCESS, "WlanDisconnect failed: %lu\n", Error);
        ok(WaitConnectedTo(Client, Interface, L"ReactOS-Open", 25000),
           "No automatic switch to ReactOS-Open after disconnect within 25s (state %lu)\n",
           QueryState(Client, Interface));
        ProfileNameOf(Client, Interface, name, ARRAYSIZE(name));
        ok(lstrcmpW(name, L"ReactOS-Open") == 0, "Reconnected to %ls, expected the stronger ReactOS-Open\n", name);
        ok(ConnComplete > connBefore2, "No connection-complete notification for the automatic switch\n");
    }

Cleanup:
    WlanDeleteProfile(Client, Interface, L"ReactOS-Roam", NULL);
    WlanDeleteProfile(Client, Interface, L"ReactOS-Open", NULL);
}

START_TEST(WlanAutoSwitch)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices = INVALID_HANDLE_VALUE;
    DWORD NegotiatedVersion;
    HANDLE Client;
    GUID Interface;
    DWORD Error;

    Error = WlanOpenHandle(WLAN_API_VERSION_2_0, NULL, &NegotiatedVersion, &Client);
    if (Error == ERROR_SERVICE_NOT_ACTIVE)
    {
        skip("WlanSvc is not running\n");
        return;
    }
    ok(Error == ERROR_SUCCESS, "WlanOpenHandle failed: %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    InitializeCriticalSection(&Lock);
    if (InstallVwifi(&Devices, &Device))
    {
        if (WaitInterface(Client, &Interface))
        {
            Error = WlanRegisterNotification(Client, WLAN_NOTIFICATION_SOURCE_ACM,
                                             FALSE, NotificationCallback, NULL, NULL, NULL);
            ok(Error == ERROR_SUCCESS, "WlanRegisterNotification failed: %lu\n", Error);
            TestAutoConnectAndSwitch(Client, &Interface);
        }
        else
            skip("Wireless interface never appeared\n");
        RemoveVwifi(Devices, &Device);
    }
    else
        skip("Could not install the vwifi adapter\n");
    WlanCloseHandle(Client, NULL);
}
