/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Connection recovery after a failed roam attempt and a stalled 4-way handshake
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <winreg.h>
#include <winuser.h>
#include <wlanapi.h>
#include <setupapi.h>

#include "vwifi.h"

#define MAX_EVENTS 128

typedef struct _TEST_EVENT
{
    DWORD Source;
    DWORD Code;
    WLAN_REASON_CODE Reason;
} TEST_EVENT;

static const UCHAR RoamBssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x77};

static const WCHAR Wpa2ProfileXml[] =
    L"<?xml version=\"1.0\"?>"
    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
    L"<name>ReactOS-WPA2</name>"
    L"<SSIDConfig><SSID><name>ReactOS-WPA2</name></SSID></SSIDConfig>"
    L"<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>"
    L"<MSM><security><authEncryption><authentication>WPA2PSK</authentication>"
    L"<encryption>AES</encryption><useOneX>false</useOneX></authEncryption>"
    L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
    L"<keyMaterial>vwifi-test-passphrase</keyMaterial></sharedKey></security></MSM>"
    L"</WLANProfile>";

static const WCHAR RoamProfileXml[] =
    L"<?xml version=\"1.0\"?>"
    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
    L"<name>ReactOS-Roam</name>"
    L"<SSIDConfig><SSID><name>ReactOS-Roam</name></SSID></SSIDConfig>"
    L"<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>"
    L"<MSM><security><authEncryption><authentication>open</authentication>"
    L"<encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>"
    L"</WLANProfile>";

static CRITICAL_SECTION EventLock;
static HANDLE EventSignal;
static TEST_EVENT Events[MAX_EVENTS];
static LONG EventCount;

static VOID WINAPI
NotificationCallback(PWLAN_NOTIFICATION_DATA Data, PVOID Context)
{
    TEST_EVENT Event;

    UNREFERENCED_PARAMETER(Context);

    Event.Source = Data->NotificationSource;
    Event.Code = Data->NotificationCode;
    Event.Reason = WLAN_REASON_CODE_SUCCESS;
    if (Data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM &&
        Data->pData != NULL &&
        Data->dwDataSize >= sizeof(WLAN_CONNECTION_NOTIFICATION_DATA))
    {
        Event.Reason = ((PWLAN_CONNECTION_NOTIFICATION_DATA)Data->pData)->wlanReasonCode;
    }
    else if (Data->NotificationSource == WLAN_NOTIFICATION_SOURCE_MSM &&
             Data->pData != NULL &&
             Data->dwDataSize >= sizeof(WLAN_MSM_NOTIFICATION_DATA))
    {
        Event.Reason = ((PWLAN_MSM_NOTIFICATION_DATA)Data->pData)->wlanReasonCode;
    }

    EnterCriticalSection(&EventLock);
    if (EventCount < MAX_EVENTS)
        Events[EventCount++] = Event;
    LeaveCriticalSection(&EventLock);
    SetEvent(EventSignal);
}

static LONG
EventMark(VOID)
{
    LONG Mark;

    EnterCriticalSection(&EventLock);
    Mark = EventCount;
    LeaveCriticalSection(&EventLock);
    return Mark;
}

static BOOL
FindEvent(LONG From, DWORD Source, DWORD Code, WLAN_REASON_CODE *Reason)
{
    BOOL Found = FALSE;
    LONG i;

    EnterCriticalSection(&EventLock);
    for (i = From; i < EventCount && !Found; i++)
    {
        if (Events[i].Source == Source && Events[i].Code == Code)
        {
            Found = TRUE;
            if (Reason != NULL)
                *Reason = Events[i].Reason;
        }
    }
    LeaveCriticalSection(&EventLock);
    return Found;
}

static BOOL
WaitEvent(LONG From, DWORD Source, DWORD Code, DWORD TimeoutMs, WLAN_REASON_CODE *Reason)
{
    ULONGLONG Deadline = GetTickCount64() + TimeoutMs;
    ULONGLONG Now;

    for (Now = GetTickCount64(); Now < Deadline; Now = GetTickCount64())
    {
        if (FindEvent(From, Source, Code, Reason))
            return TRUE;
        WaitForSingleObject(EventSignal, (DWORD)(Deadline - Now));
    }
    return FindEvent(From, Source, Code, Reason);
}

static WLAN_INTERFACE_STATE
QueryState(HANDLE Client, const GUID *Interface)
{
    WLAN_INTERFACE_STATE State = wlan_interface_state_not_ready;
    WLAN_INTERFACE_STATE *Data = NULL;
    DWORD Size = 0;
    DWORD Error;

    Error = WlanQueryInterface(Client, Interface, wlan_intf_opcode_interface_state, NULL,
                               &Size, (PVOID *)&Data, NULL);
    if (Error == ERROR_SUCCESS && Data != NULL && Size >= sizeof(*Data))
        State = *Data;
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
        Sleep(100);
    }
    return TRUE;
}

static DWORD
ConnectProfile(HANDLE Client, const GUID *Interface, LPCWSTR Xml, LPCWSTR Name)
{
    WLAN_CONNECTION_PARAMETERS Params;
    WLAN_REASON_CODE Reason = 0;
    DWORD Error;

    Error = WlanSetProfile(Client, Interface, 0, Xml, NULL, TRUE, NULL, &Reason);
    ok(Error == ERROR_SUCCESS, "WlanSetProfile(%ls) failed: %lu, reason %lu\n", Name, Error, Reason);
    if (Error != ERROR_SUCCESS)
        return Error;

    ZeroMemory(&Params, sizeof(Params));
    Params.wlanConnectionMode = wlan_connection_mode_profile;
    Params.strProfile = Name;
    Params.dot11BssType = dot11_BSS_type_infrastructure;
    Error = WlanConnect(Client, Interface, &Params, NULL);
    ok(Error == ERROR_SUCCESS, "WlanConnect(%ls) failed: %lu\n", Name, Error);
    return Error;
}

static VOID
TestStalledHandshake(HANDLE Client, const GUID *Interface)
{
    WLAN_REASON_CODE Reason = WLAN_REASON_CODE_SUCCESS;
    ULONGLONG Start;
    LONG Mark;
    BOOL Failed;

    Mark = EventMark();
    Start = GetTickCount64();
    if (ConnectProfile(Client, Interface, Wpa2ProfileXml, L"ReactOS-WPA2") != ERROR_SUCCESS)
        return;

    Failed = WaitEvent(Mark, WLAN_NOTIFICATION_SOURCE_ACM, wlan_notification_acm_connection_attempt_fail,
                       15000, &Reason);
    ok(Failed, "No connection failure 15 s after an association whose 4-way handshake never started\n");
    if (Failed)
    {
        trace("Handshake gave up after %I64u ms\n", GetTickCount64() - Start);
        ok(Reason == WLAN_REASON_CODE_MSMSEC_KEY_START_TIMEOUT,
           "Expected reason 0x%x, got 0x%lx\n", WLAN_REASON_CODE_MSMSEC_KEY_START_TIMEOUT, Reason);
        ok(WaitState(Client, Interface, wlan_interface_state_disconnected, 3000),
           "Interface state is %d, expected disconnected\n", QueryState(Client, Interface));
    }
    else
    {
        WlanDisconnect(Client, Interface, NULL);
    }
    WlanDeleteProfile(Client, Interface, L"ReactOS-WPA2", NULL);
}

static VOID
TestRoamAfterFailedAttempt(HANDLE Client, const GUID *Interface)
{
    WLAN_REASON_CODE Reason = WLAN_REASON_CODE_UNKNOWN;
    PWLAN_CONNECTION_ATTRIBUTES Attributes = NULL;
    DWORD Size = 0;
    DWORD Error;
    LONG Mark;
    LONG RoamMark;

    Mark = EventMark();
    if (ConnectProfile(Client, Interface, RoamProfileXml, L"ReactOS-Roam") != ERROR_SUCCESS)
        return;

    ok(WaitEvent(Mark, WLAN_NOTIFICATION_SOURCE_ACM, wlan_notification_acm_connection_complete, 5000, &Reason),
       "ReactOS-Roam did not connect\n");
    ok(Reason == WLAN_REASON_CODE_SUCCESS, "Connection completed with reason 0x%lx\n", Reason);

    RoamMark = EventMark();
    ok(WaitEvent(Mark, WLAN_NOTIFICATION_SOURCE_MSM, wlan_notification_msm_roaming_end, 5000, NULL),
       "No roaming end notification\n");
    ok(!FindEvent(RoamMark, WLAN_NOTIFICATION_SOURCE_ACM, wlan_notification_acm_connection_attempt_fail, NULL),
       "A failed association attempt during the roam was reported as a failed connection\n");
    ok(WaitState(Client, Interface, wlan_interface_state_connected, 3000),
       "Interface state is %d after the roam, expected connected\n", QueryState(Client, Interface));

    Error = WlanQueryInterface(Client, Interface, wlan_intf_opcode_current_connection, NULL,
                               &Size, (PVOID *)&Attributes, NULL);
    ok(Error == ERROR_SUCCESS, "Querying the current connection failed: %lu\n", Error);
    if (Error == ERROR_SUCCESS && Attributes != NULL)
    {
        ok(Attributes->isState == wlan_interface_state_connected, "isState %d\n", Attributes->isState);
        ok(!memcmp(Attributes->wlanAssociationAttributes.dot11Bssid, RoamBssid, sizeof(RoamBssid)),
           "Connected to the wrong BSSID\n");
    }
    if (Attributes != NULL)
        WlanFreeMemory(Attributes);

    WlanDisconnect(Client, Interface, NULL);
    WlanDeleteProfile(Client, Interface, L"ReactOS-Roam", NULL);
}

START_TEST(WlanConnectionRecovery)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices = INVALID_HANDLE_VALUE;
    DWORD NegotiatedVersion;
    HANDLE Client;
    GUID Interface;
    LONG Mark;
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

    InitializeCriticalSection(&EventLock);
    EventSignal = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (InstallVwifi(&Devices, &Device))
    {
        if (WaitInterface(Client, &Interface))
        {
            Error = WlanRegisterNotification(Client, WLAN_NOTIFICATION_SOURCE_ACM | WLAN_NOTIFICATION_SOURCE_MSM,
                                             FALSE, NotificationCallback, NULL, NULL, NULL);
            ok(Error == ERROR_SUCCESS, "WlanRegisterNotification failed: %lu\n", Error);

            Mark = EventMark();
            Error = WlanScan(Client, &Interface, NULL, NULL, NULL);
            ok(Error == ERROR_SUCCESS, "WlanScan failed: %lu\n", Error);
            ok(WaitEvent(Mark, WLAN_NOTIFICATION_SOURCE_ACM, wlan_notification_acm_scan_complete, 5000, NULL),
               "The scan did not complete\n");

            TestRoamAfterFailedAttempt(Client, &Interface);
            TestStalledHandshake(Client, &Interface);

            WlanRegisterNotification(Client, WLAN_NOTIFICATION_SOURCE_NONE, FALSE, NULL, NULL, NULL, NULL);
        }
        else
        {
            ok(FALSE, "No wireless interface appeared for the vwifi adapter\n");
        }
        RemoveVwifi(Devices, &Device);
    }
    WlanCloseHandle(Client, NULL);
    CloseHandle(EventSignal);
    DeleteCriticalSection(&EventLock);
}
