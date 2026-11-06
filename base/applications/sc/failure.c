/*
 * PROJECT:     ReactOS Services
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/sc/failure.c
 * PURPOSE:     Query/Set the service failure actions
 * COPYRIGHT:   Copyright 2016 Eric Kohl
 */

#include "sc.h"

BOOL QueryFailure(LPCTSTR ServiceName)
{
    SC_HANDLE hManager = NULL;
    SC_HANDLE hService = NULL;
    BOOL bResult = TRUE;
    DWORD cbBytesNeeded = 0;
    LPSERVICE_FAILURE_ACTIONS pServiceFailure = NULL;
    INT i;

#ifdef SCDBG
    _tprintf(_T("service to show failure action - %s\n\n"), ServiceName);
#endif

    hManager = OpenSCManager(NULL,
                             NULL,
                             SC_MANAGER_CONNECT);
    if (hManager == NULL)
    {
        bResult = FALSE;
        goto done;
    }

    hService = OpenService(hManager, ServiceName, SERVICE_QUERY_CONFIG);
    if (hService == NULL)
    {
        bResult = FALSE;
        goto done;
    }

    if (!QueryServiceConfig2(hService,
                             SERVICE_CONFIG_FAILURE_ACTIONS,
                             NULL,
                             0,
                             &cbBytesNeeded))
    {
        if (cbBytesNeeded == 0)
        {
            bResult = FALSE;
            goto done;
        }
    }

    pServiceFailure = HeapAlloc(GetProcessHeap(), 0, cbBytesNeeded);
    if (pServiceFailure == NULL)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        bResult = FALSE;
        goto done;
    }

    if (!QueryServiceConfig2(hService,
                             SERVICE_CONFIG_FAILURE_ACTIONS,
                             (LPBYTE)pServiceFailure,
                             cbBytesNeeded,
                             &cbBytesNeeded))
    {
        bResult = FALSE;
        goto done;
    }

    _tprintf(_T("SERVICE_NAME: %s\n"), ServiceName);
    _tprintf(_T("        RESET_PERIOD       : %lu seconds\n"), pServiceFailure->dwResetPeriod);
    _tprintf(_T("        REBOOT_MESSAGE     : %s\n"), (pServiceFailure->lpRebootMsg) ? pServiceFailure->lpRebootMsg : _T(""));
    _tprintf(_T("        COMMAND_LINE       : %s\n"), (pServiceFailure->lpCommand) ? pServiceFailure->lpCommand : _T(""));
    _tprintf(_T("        FAILURE_ACTIONS    : "));
    for (i = 0; i < pServiceFailure->cActions; i++)
    {
        if (i != 0)
            _tprintf(_T("                             "));
        switch (pServiceFailure->lpsaActions[i].Type)
        {
            case SC_ACTION_NONE:
                continue;

            case SC_ACTION_RESTART:
                _tprintf(_T("RESTART -- Delay = %lu milliseconds.\n"), pServiceFailure->lpsaActions[i].Delay);
                break;

            case SC_ACTION_REBOOT:
                _tprintf(_T("REBOOT -- Delay = %lu milliseconds.\n"), pServiceFailure->lpsaActions[i].Delay);
                break;

            case SC_ACTION_RUN_COMMAND:
                _tprintf(_T("RUN_COMMAND -- Delay = %lu milliseconds.\n"), pServiceFailure->lpsaActions[i].Delay);
                break;

            default:
                _tprintf(_T("\n"));
                break;
        }
    }

done:
    if (bResult == FALSE)
        ReportLastError();

    if (pServiceFailure != NULL)
        HeapFree(GetProcessHeap(), 0, pServiceFailure);

    if (hService)
        CloseServiceHandle(hService);

    if (hManager)
        CloseServiceHandle(hManager);

    return bResult;
}

BOOL
SetFailure(
    LPCTSTR *ServiceArgs,
    INT ArgCount)
{
    SC_HANDLE hManager = NULL;
    SC_HANDLE hService = NULL;
    BOOL bResult = TRUE;
    SERVICE_FAILURE_ACTIONS FailureActions;
    LPCTSTR lpServiceName = NULL;
    BOOLEAN Old = FALSE;

    ZeroMemory(&FailureActions, sizeof(SERVICE_FAILURE_ACTIONS));

    if (!ParseFailureArguments(ServiceArgs, ArgCount, &lpServiceName, &FailureActions))
    {
        SetFailureUsage();
        return FALSE;
    }

    hManager = OpenSCManager(NULL,
                             NULL,
                             SC_MANAGER_CONNECT);
    if (hManager == NULL)
    {
        _tprintf(_T("[SC] OpenSCManager FAILED %lu:\n\n"), GetLastError());
        bResult = FALSE;
        goto done;
    }

    hService = OpenService(hManager,
                           lpServiceName,
                           SERVICE_CHANGE_CONFIG | SERVICE_START);
    if (hService == NULL)
    {
        _tprintf(_T("[SC] OpenService FAILED %lu:\n\n"), GetLastError());
        bResult = FALSE;
        goto done;
    }

    RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Old);

    if (!ChangeServiceConfig2(hService,
                              SERVICE_CONFIG_FAILURE_ACTIONS,
                              (LPBYTE)&FailureActions))
    {
        _tprintf(_T("[SC] ChangeServiceConfig2 FAILED %lu:\n\n"), GetLastError());
        bResult = FALSE;
        goto done;
    }

    _tprintf(_T("[SC] ChangeServiceConfig2 SUCCESS\n\n"));

done:
    RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, Old, FALSE, &Old);

    if (bResult == FALSE)
        ReportLastError();

    if (FailureActions.lpsaActions != NULL)
        HeapFree(GetProcessHeap(), 0, FailureActions.lpsaActions);

    if (hService)
        CloseServiceHandle(hService);

    if (hManager)
        CloseServiceHandle(hManager);

    return bResult;
}


static SC_HANDLE
OpenFailureFlagService(LPCTSTR ServiceName, DWORD dwDesiredAccess, SC_HANDLE *phManager)
{
    SC_HANDLE hService;

    *phManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (*phManager == NULL)
    {
        _tprintf(_T("[SC] OpenSCManager FAILED %lu:\n\n"), GetLastError());
        return NULL;
    }
    hService = OpenService(*phManager, ServiceName, dwDesiredAccess);
    if (hService == NULL)
    {
        _tprintf(_T("[SC] OpenService FAILED %lu:\n\n"), GetLastError());
        CloseServiceHandle(*phManager);
        *phManager = NULL;
    }
    return hService;
}

BOOL QueryFailureFlag(LPCTSTR ServiceName)
{
    SERVICE_FAILURE_ACTIONS_FLAG Flag = { FALSE };
    SC_HANDLE hManager, hService;
    DWORD cbBytesNeeded = 0;
    BOOL bResult;

    hService = OpenFailureFlagService(ServiceName, SERVICE_QUERY_CONFIG, &hManager);
    if (hService == NULL)
    {
        ReportLastError();
        return FALSE;
    }

    bResult = QueryServiceConfig2(hService, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,
                                  (LPBYTE)&Flag, sizeof(Flag), &cbBytesNeeded);
    if (bResult)
    {
        _tprintf(_T("[SC] QueryServiceConfig2 SUCCESS\n\n"));
        _tprintf(_T("SERVICE_NAME: %s\n"), ServiceName);
        _tprintf(_T("        FAILURE_ACTIONS_ON_NONCRASH_FAILURES: %s\n\n"),
                 Flag.fFailureActionsOnNonCrashFailures ? _T("TRUE") : _T("FALSE"));
    }
    else
    {
        _tprintf(_T("[SC] QueryServiceConfig2 FAILED %lu:\n\n"), GetLastError());
        ReportLastError();
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hManager);
    return bResult;
}

BOOL SetFailureFlag(LPCTSTR *ServiceArgs, INT ArgCount)
{
    SERVICE_FAILURE_ACTIONS_FLAG Flag;
    SC_HANDLE hManager, hService;
    BOOL bResult;

    if (ArgCount != 2)
    {
        SetFailureFlagUsage();
        return FALSE;
    }
    Flag.fFailureActionsOnNonCrashFailures = _ttoi(ServiceArgs[1]) != 0;

    hService = OpenFailureFlagService(ServiceArgs[0], SERVICE_CHANGE_CONFIG, &hManager);
    if (hService == NULL)
    {
        ReportLastError();
        return FALSE;
    }

    bResult = ChangeServiceConfig2(hService, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, (LPBYTE)&Flag);
    if (bResult)
    {
        _tprintf(_T("[SC] ChangeServiceConfig2 SUCCESS\n\n"));
    }
    else
    {
        _tprintf(_T("[SC] ChangeServiceConfig2 FAILED %lu:\n\n"), GetLastError());
        ReportLastError();
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hManager);
    return bResult;
}
