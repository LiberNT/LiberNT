/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WPA2-PSK key material of WlanSetProfile: spaces kept, short passphrase reason code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <winreg.h>
#include <winuser.h>
#include <wlanapi.h>
#include <setupapi.h>
#include <stdio.h>

#include "vwifi.h"

#define KEY_PROFILE_NAME L"ReactOS-WPA2-Key"

static DWORD
SetPskProfile(HANDLE Client, const GUID *Interface, LPCWSTR Key, WLAN_REASON_CODE *Reason)
{
    WCHAR Xml[1024] = {0};

    _snwprintf(Xml, ARRAYSIZE(Xml) - 1,
             L"<?xml version=\"1.0\"?>"
             L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
             L"<name>" KEY_PROFILE_NAME L"</name>"
             L"<SSIDConfig><SSID><name>ReactOS-WPA2</name></SSID></SSIDConfig>"
             L"<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>"
             L"<MSM><security><authEncryption><authentication>WPA2PSK</authentication>"
             L"<encryption>AES</encryption><useOneX>false</useOneX></authEncryption>"
             L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
             L"<keyMaterial>%ls</keyMaterial></sharedKey></security></MSM>"
             L"</WLANProfile>",
             Key);
    *Reason = 0xdeadbeef;
    return WlanSetProfile(Client, Interface, 0, Xml, NULL, TRUE, NULL, Reason);
}

static VOID
TestKeyMaterial(HANDLE Client, const GUID *Interface)
{
    WLAN_REASON_CODE Reason;
    DWORD Error;

    Error = SetPskProfile(Client, Interface, L"    cccc", &Reason);
    ok(Error == ERROR_SUCCESS, "8-character passphrase with leading spaces: error %lu, reason 0x%lx\n", Error, Reason);
    ok(Reason == WLAN_REASON_CODE_SUCCESS, "8-character passphrase with leading spaces: reason 0x%lx\n", Reason);
    if (Error == ERROR_SUCCESS)
        WlanDeleteProfile(Client, Interface, KEY_PROFILE_NAME, NULL);

    Error = SetPskProfile(Client, Interface, L"cccc    ", &Reason);
    ok(Error == ERROR_SUCCESS, "8-character passphrase with trailing spaces: error %lu, reason 0x%lx\n", Error, Reason);
    if (Error == ERROR_SUCCESS)
        WlanDeleteProfile(Client, Interface, KEY_PROFILE_NAME, NULL);

    Error = SetPskProfile(Client, Interface, L"    cc", &Reason);
    ok(Error == ERROR_BAD_PROFILE, "6-character passphrase: error %lu\n", Error);
    ok(Reason == WLAN_REASON_CODE_MSMSEC_PROFILE_PSK_LENGTH, "6-character passphrase: reason 0x%lx\n", Reason);
    if (Error == ERROR_SUCCESS)
        WlanDeleteProfile(Client, Interface, KEY_PROFILE_NAME, NULL);
}

START_TEST(WlanProfileKeyMaterial)
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

    if (InstallVwifi(&Devices, &Device))
    {
        if (WaitInterface(Client, Devices, &Device, &Interface))
            TestKeyMaterial(Client, &Interface);
        else
            ok(FALSE, "No wireless interface appeared for the vwifi adapter\n");
        RemoveVwifi(Devices, &Device);
    }
    WlanCloseHandle(Client, NULL);
}
