/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WPA2-PSK key material: spaces kept end to end, short passphrase and wrong key reason codes
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
#define PSK_NETWORK L"ReactOS-PSK"
#define PSK_NETWORK_KEY L"    cccccccc    "
#define PSK_TRIMMED_KEY L"cccccccc"

typedef struct _CONNECT_EVENTS
{
    volatile LONG ScanComplete;
    volatile LONG Completed;
    volatile LONG CompleteReason;
    volatile LONG Failed;
    volatile LONG FailReason;
} CONNECT_EVENTS;

static HANDLE ConnectSignal;
static CONNECT_EVENTS Connect;

static DWORD
SetNamedPskProfile(HANDLE Client, const GUID *Interface, LPCWSTR Name, LPCWSTR Ssid, LPCWSTR Key,
                   WLAN_REASON_CODE *Reason)
{
    WCHAR Xml[1024] = {0};

    _snwprintf(Xml, ARRAYSIZE(Xml) - 1,
             L"<?xml version=\"1.0\"?>"
             L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
             L"<name>%ls</name>"
             L"<SSIDConfig><SSID><name>%ls</name></SSID></SSIDConfig>"
             L"<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>"
             L"<MSM><security><authEncryption><authentication>WPA2PSK</authentication>"
             L"<encryption>AES</encryption><useOneX>false</useOneX></authEncryption>"
             L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
             L"<keyMaterial>%ls</keyMaterial></sharedKey></security></MSM>"
             L"</WLANProfile>",
             Name, Ssid, Key);
    *Reason = 0xdeadbeef;
    return WlanSetProfile(Client, Interface, 0, Xml, NULL, TRUE, NULL, Reason);
}

static DWORD
SetPskProfile(HANDLE Client, const GUID *Interface, LPCWSTR Key, WLAN_REASON_CODE *Reason)
{
    return SetNamedPskProfile(Client, Interface, KEY_PROFILE_NAME, L"ReactOS-WPA2", Key, Reason);
}

static VOID WINAPI
ConnectNotification(PWLAN_NOTIFICATION_DATA Data, PVOID Context)
{
    PWLAN_CONNECTION_NOTIFICATION_DATA Connection = Data->pData;

    UNREFERENCED_PARAMETER(Context);

    if (Data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM)
        return;
    if (Data->NotificationCode == wlan_notification_acm_scan_complete)
    {
        InterlockedExchange(&Connect.ScanComplete, TRUE);
        SetEvent(ConnectSignal);
        return;
    }
    if ((Data->NotificationCode != wlan_notification_acm_connection_complete &&
         Data->NotificationCode != wlan_notification_acm_connection_attempt_fail) ||
        Connection == NULL || Data->dwDataSize < sizeof(*Connection) ||
        wcscmp(Connection->strProfileName, PSK_NETWORK) != 0)
    {
        return;
    }
    if (Data->NotificationCode == wlan_notification_acm_connection_complete)
    {
        InterlockedExchange(&Connect.CompleteReason, (LONG)Connection->wlanReasonCode);
        InterlockedExchange(&Connect.Completed, TRUE);
    }
    else
    {
        InterlockedExchange(&Connect.FailReason, (LONG)Connection->wlanReasonCode);
        InterlockedExchange(&Connect.Failed, TRUE);
    }
    SetEvent(ConnectSignal);
}

static BOOL
WaitFlag(volatile LONG *Flag, DWORD TimeoutMs)
{
    ULONGLONG Deadline = GetTickCount64() + TimeoutMs;
    ULONGLONG Now;

    for (Now = GetTickCount64(); Now < Deadline; Now = GetTickCount64())
    {
        if (InterlockedCompareExchange(Flag, 0, 0))
            return TRUE;
        WaitForSingleObject(ConnectSignal, (DWORD)(Deadline - Now));
    }
    return InterlockedCompareExchange(Flag, 0, 0) != 0;
}

static WLAN_INTERFACE_STATE
QueryState(HANDLE Client, const GUID *Interface)
{
    WLAN_INTERFACE_STATE State = wlan_interface_state_not_ready;
    WLAN_INTERFACE_STATE *Data = NULL;
    DWORD Size = 0;

    if (WlanQueryInterface(Client, Interface, wlan_intf_opcode_interface_state, NULL, &Size, (PVOID *)&Data,
                           NULL) == ERROR_SUCCESS && Data != NULL && Size >= sizeof(*Data))
    {
        State = *Data;
    }
    if (Data != NULL)
        WlanFreeMemory(Data);
    return State;
}

static BOOL
WaitState(HANDLE Client, const GUID *Interface, WLAN_INTERFACE_STATE Expected, DWORD TimeoutMs)
{
    ULONGLONG Deadline = GetTickCount64() + TimeoutMs;

    while (QueryState(Client, Interface) != Expected)
    {
        if (GetTickCount64() >= Deadline)
            return FALSE;
        Sleep(50);
    }
    return TRUE;
}

static DWORD
ConnectPsk(HANDLE Client, const GUID *Interface, LPCWSTR Key)
{
    WLAN_CONNECTION_PARAMETERS Params;
    WLAN_REASON_CODE Reason;
    DWORD Error;

    Error = SetNamedPskProfile(Client, Interface, PSK_NETWORK, PSK_NETWORK, Key, &Reason);
    ok(Error == ERROR_SUCCESS, "WlanSetProfile failed: %lu, reason 0x%lx\n", Error, Reason);
    if (Error != ERROR_SUCCESS)
        return Error;

    ZeroMemory((PVOID)&Connect, sizeof(Connect));
    ResetEvent(ConnectSignal);
    ZeroMemory(&Params, sizeof(Params));
    Params.wlanConnectionMode = wlan_connection_mode_profile;
    Params.strProfile = PSK_NETWORK;
    Params.dot11BssType = dot11_BSS_type_infrastructure;
    Error = WlanConnect(Client, Interface, &Params, NULL);
    ok(Error == ERROR_SUCCESS, "WlanConnect failed: %lu\n", Error);
    return Error;
}

static VOID
TestHandshake(HANDLE Client, const GUID *Interface)
{
    PWLAN_CONNECTION_ATTRIBUTES Attributes = NULL;
    ULONGLONG Start;
    DWORD Size = 0;
    DWORD Error;

    Start = GetTickCount64();
    if (ConnectPsk(Client, Interface, PSK_TRIMMED_KEY) == ERROR_SUCCESS)
    {
        ok(WaitFlag(&Connect.Failed, 5000), "No connection failure with the key stripped of its spaces\n");
        trace("Wrong key reported after %I64u ms\n", GetTickCount64() - Start);
        ok(Connect.FailReason == WLAN_REASON_CODE_MSMSEC_PSK_MISMATCH_SUSPECTED,
           "Wrong key: reason 0x%lx, expected 0x%x\n", Connect.FailReason,
           WLAN_REASON_CODE_MSMSEC_PSK_MISMATCH_SUSPECTED);
        ok(!Connect.Completed || Connect.CompleteReason != WLAN_REASON_CODE_SUCCESS,
           "The connection succeeded with a wrong key\n");
        ok(WaitState(Client, Interface, wlan_interface_state_disconnected, 2000),
           "Interface state is %d after a wrong key, expected disconnected\n", QueryState(Client, Interface));
    }

    Start = GetTickCount64();
    if (ConnectPsk(Client, Interface, PSK_NETWORK_KEY) == ERROR_SUCCESS)
    {
        ok(WaitFlag(&Connect.Completed, 5000), "The key with leading and trailing spaces did not connect\n");
        trace("Connected after %I64u ms\n", GetTickCount64() - Start);
        ok(Connect.CompleteReason == WLAN_REASON_CODE_SUCCESS, "Connection completed with reason 0x%lx\n",
           Connect.CompleteReason);
        ok(!Connect.Failed, "The connection with the right key failed with reason 0x%lx\n", Connect.FailReason);
        ok(WaitState(Client, Interface, wlan_interface_state_connected, 2000),
           "Interface state is %d, expected connected\n", QueryState(Client, Interface));

        Error = WlanQueryInterface(Client, Interface, wlan_intf_opcode_current_connection, NULL, &Size,
                                   (PVOID *)&Attributes, NULL);
        ok(Error == ERROR_SUCCESS, "Querying the current connection failed: %lu\n", Error);
        if (Error == ERROR_SUCCESS && Attributes != NULL)
        {
            ok(Attributes->wlanSecurityAttributes.bSecurityEnabled, "Security is not enabled\n");
            ok(Attributes->wlanSecurityAttributes.dot11AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK,
               "Authentication algorithm %d\n", Attributes->wlanSecurityAttributes.dot11AuthAlgorithm);
            ok(Attributes->wlanSecurityAttributes.dot11CipherAlgorithm == DOT11_CIPHER_ALGO_CCMP,
               "Cipher algorithm %d\n", Attributes->wlanSecurityAttributes.dot11CipherAlgorithm);
        }
        if (Attributes != NULL)
            WlanFreeMemory(Attributes);

        ok(WlanDisconnect(Client, Interface, NULL) == ERROR_SUCCESS, "WlanDisconnect failed\n");
        ok(WaitState(Client, Interface, wlan_interface_state_disconnected, 2000),
           "Interface state is %d after the disconnect\n", QueryState(Client, Interface));
    }
    WlanDeleteProfile(Client, Interface, PSK_NETWORK, NULL);
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

    ConnectSignal = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (InstallVwifi(&Devices, &Device))
    {
        if (WaitInterface(Client, Devices, &Device, &Interface))
        {
            TestKeyMaterial(Client, &Interface);

            Error = WlanRegisterNotification(Client, WLAN_NOTIFICATION_SOURCE_ACM, FALSE, ConnectNotification,
                                             NULL, NULL, NULL);
            ok(Error == ERROR_SUCCESS, "WlanRegisterNotification failed: %lu\n", Error);
            Error = WlanScan(Client, &Interface, NULL, NULL, NULL);
            ok(Error == ERROR_SUCCESS, "WlanScan failed: %lu\n", Error);
            ok(WaitFlag(&Connect.ScanComplete, 5000), "The scan did not complete\n");
            TestHandshake(Client, &Interface);
            WlanRegisterNotification(Client, WLAN_NOTIFICATION_SOURCE_NONE, FALSE, NULL, NULL, NULL, NULL);
        }
        else
        {
            ok(FALSE, "No wireless interface appeared for the vwifi adapter\n");
        }
        RemoveVwifi(Devices, &Device);
    }
    WlanCloseHandle(Client, NULL);
    CloseHandle(ConnectSignal);
}
