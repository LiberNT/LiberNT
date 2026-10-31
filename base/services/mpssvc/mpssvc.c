/*
 * PROJECT:     LiberNT Windows Firewall service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Firewall service lifecycle and policy reload orchestration
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <windows.h>
#include <winsvc.h>
#include <rpc.h>
#include <fwpmu.h>

#include "mpssvc_policy.h"

#define MPS_SERVICE_NAME L"MpsSvc"
#define MPS_RULES_KEY L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\FirewallRules"
#define MPS_MAX_RULES 4096

static SERVICE_STATUS_HANDLE MpsStatusHandle;
static SERVICE_STATUS MpsStatus;
static HANDLE MpsStopEvent;

static VOID
MpsReportStatus(
    _In_ DWORD State,
    _In_ DWORD Error)
{
    MpsStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    MpsStatus.dwCurrentState = State;
    MpsStatus.dwWin32ExitCode = Error;
    MpsStatus.dwServiceSpecificExitCode = 0;
    MpsStatus.dwControlsAccepted = State == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    MpsStatus.dwCheckPoint = 0;
    MpsStatus.dwWaitHint = 0;
    SetServiceStatus(MpsStatusHandle, &MpsStatus);
}

static DWORD WINAPI
MpsControlHandler(
    _In_ DWORD Control,
    _In_ DWORD EventType,
    _In_opt_ LPVOID EventData,
    _In_opt_ LPVOID Context)
{
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventData);
    UNREFERENCED_PARAMETER(Context);

    if (Control == SERVICE_CONTROL_STOP || Control == SERVICE_CONTROL_SHUTDOWN)
    {
        MpsReportStatus(SERVICE_STOP_PENDING, NO_ERROR);
        if (!SetEvent(MpsStopEvent))
            return GetLastError();
        return NO_ERROR;
    }

    if (Control == SERVICE_CONTROL_INTERROGATE)
    {
        SetServiceStatus(MpsStatusHandle, &MpsStatus);
        return NO_ERROR;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static DWORD
MpsOpenEngine(
    _In_ UINT32 Flags,
    _Out_ HANDLE *Engine)
{
    FWPM_SESSION0 Session;

    ZeroMemory(&Session, sizeof(Session));
    Session.flags = Flags;
    return FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &Session, Engine);
}

static DWORD
MpsAddSubLayer(VOID)
{
    HANDLE Engine;
    DWORD Error;

    Error = MpsOpenEngine(0, &Engine);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = MpsAddFirewallSubLayer(Engine, &FWPM_SUBLAYER_MPSSVC_WF);
    FwpmEngineClose0(Engine);
    return Error;
}

static DWORD
MpsOpenAndLoadEngine(
    _In_opt_ HKEY RulesKey,
    _Out_ HANDLE *Engine)
{
    HANDLE NewEngine = NULL;
    DWORD Error;

    *Engine = NULL;

    Error = MpsOpenEngine(FWPM_SESSION_FLAG_DYNAMIC, &NewEngine);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = MpsAddLoopbackPermit(NewEngine, &FWPM_SUBLAYER_MPSSVC_WF);
    if (Error == ERROR_SUCCESS && RulesKey != NULL)
        Error = MpsLoadFirewallRulesFromKey(NewEngine, &FWPM_SUBLAYER_MPSSVC_WF, RulesKey);

    if (Error != ERROR_SUCCESS)
    {
        FwpmEngineClose0(NewEngine);
        return Error;
    }

    *Engine = NewEngine;
    return ERROR_SUCCESS;
}

static VOID WINAPI
MpsServiceMain(
    _In_ DWORD ArgumentCount,
    _In_reads_(ArgumentCount) LPWSTR *Arguments)
{
    HANDLE Engine = NULL;
    HANDLE ChangeEvent = NULL;
    HANDLE WaitHandles[2];
    HKEY RulesKey = NULL;
    DWORD Error;
    DWORD WaitResult;
    BOOL StopRequested = FALSE;
    BOOL WatchRules;

    UNREFERENCED_PARAMETER(ArgumentCount);
    UNREFERENCED_PARAMETER(Arguments);

    MpsStatusHandle = RegisterServiceCtrlHandlerExW(MPS_SERVICE_NAME,
                                                    MpsControlHandler,
                                                    NULL);
    if (MpsStatusHandle == NULL)
        return;

    MpsReportStatus(SERVICE_START_PENDING, NO_ERROR);
    MpsStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (MpsStopEvent == NULL)
    {
        MpsReportStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    ChangeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (ChangeEvent == NULL)
    {
        Error = GetLastError();
        CloseHandle(MpsStopEvent);
        MpsStopEvent = NULL;
        MpsReportStatus(SERVICE_STOPPED, Error);
        return;
    }

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          MPS_RULES_KEY,
                          0,
                          KEY_QUERY_VALUE | KEY_NOTIFY,
                          &RulesKey);
    if (Error == ERROR_FILE_NOT_FOUND)
        Error = ERROR_SUCCESS;

    if (Error == ERROR_SUCCESS)
        Error = MpsAddSubLayer();

    if (Error == ERROR_SUCCESS)
        Error = MpsOpenAndLoadEngine(RulesKey, &Engine);

    if (Error == ERROR_SUCCESS)
    {
        MpsReportStatus(SERVICE_RUNNING, NO_ERROR);

        WaitHandles[0] = MpsStopEvent;
        WaitHandles[1] = ChangeEvent;

        while (!StopRequested)
        {
            WatchRules = RulesKey != NULL &&
                         RegNotifyChangeKeyValue(RulesKey,
                                                 FALSE,
                                                 REG_NOTIFY_CHANGE_NAME |
                                                 REG_NOTIFY_CHANGE_LAST_SET,
                                                 ChangeEvent,
                                                 TRUE) == ERROR_SUCCESS;

            WaitResult = WaitForMultipleObjects(WatchRules ? 2 : 1,
                                                WaitHandles,
                                                FALSE,
                                                INFINITE);
            if (WaitResult == WAIT_OBJECT_0)
            {
                StopRequested = TRUE;
            }
            else if (WaitResult == WAIT_OBJECT_0 + 1)
            {
                HANDLE NewEngine = NULL;

                if (MpsOpenAndLoadEngine(RulesKey, &NewEngine) == ERROR_SUCCESS)
                {
                    if (Engine != NULL)
                        FwpmEngineClose0(Engine);
                    Engine = NewEngine;
                }
            }
            else
            {
                Error = WaitResult == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
                break;
            }
        }
    }

    if (RulesKey != NULL)
        RegCloseKey(RulesKey);

    if (Engine != NULL)
    {
        DWORD CloseError = FwpmEngineClose0(Engine);
        if (Error == ERROR_SUCCESS)
            Error = CloseError;
    }

    CloseHandle(ChangeEvent);
    CloseHandle(MpsStopEvent);
    MpsStopEvent = NULL;
    MpsReportStatus(SERVICE_STOPPED, Error);
}

int
wmain(
    int ArgumentCount,
    wchar_t *Arguments[])
{
    SERVICE_TABLE_ENTRYW DispatchTable[] =
    {
        { (LPWSTR)MPS_SERVICE_NAME, MpsServiceMain },
        { NULL, NULL }
    };

    UNREFERENCED_PARAMETER(ArgumentCount);
    UNREFERENCED_PARAMETER(Arguments);

    if (!StartServiceCtrlDispatcherW(DispatchTable))
        return (int)GetLastError();

    return 0;
}
