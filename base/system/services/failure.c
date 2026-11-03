/*
 * PROJECT:     LiberNT Service Control Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Recovery actions taken when a service fails
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "services.h"

#include <userenv.h>
#include <strsafe.h>

#define NDEBUG
#include <debug.h>

typedef struct _SCM_RECOVERY
{
    SC_ACTION_TYPE Type;
    DWORD dwDelay;
    LPWSTR lpCommandLine;
    LPWSTR lpRebootMessage;
    WCHAR szServiceName[ANYSIZE_ARRAY];
} SCM_RECOVERY, *PSCM_RECOVERY;

static LPWSTR
ScmExpandFailureCount(LPCWSTR lpCommand, DWORD dwFailureCount)
{
    WCHAR szCount[16];
    SIZE_T cchCount, cchResult = 1;
    LPCWSTR p;
    LPWSTR lpResult, q;

    StringCchPrintfW(szCount, ARRAYSIZE(szCount), L"%lu", dwFailureCount);
    cchCount = wcslen(szCount);
    for (p = lpCommand; *p != UNICODE_NULL; )
    {
        if (wcsncmp(p, L"%1%", 3) == 0)
        {
            cchResult += cchCount;
            p += 3;
        }
        else
        {
            cchResult++;
            p++;
        }
    }

    lpResult = HeapAlloc(GetProcessHeap(), 0, cchResult * sizeof(WCHAR));
    if (lpResult == NULL)
        return NULL;
    for (p = lpCommand, q = lpResult; *p != UNICODE_NULL; )
    {
        if (wcsncmp(p, L"%1%", 3) == 0)
        {
            RtlCopyMemory(q, szCount, cchCount * sizeof(WCHAR));
            q += cchCount;
            p += 3;
        }
        else
        {
            *q++ = *p++;
        }
    }
    *q = UNICODE_NULL;
    return lpResult;
}

static VOID
ScmRebootForRecovery(LPWSTR lpRebootMessage)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE hToken;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        Privileges.PrivilegeCount = 1;
        Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &Privileges.Privileges[0].Luid))
            AdjustTokenPrivileges(hToken, FALSE, &Privileges, 0, NULL, NULL);
        CloseHandle(hToken);
    }

    /* FIXME: the shutdown reason Windows records for a recovery reboot is not verified */
    if (!InitiateSystemShutdownExW(NULL, lpRebootMessage, 0, TRUE, TRUE,
                                   SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER))
    {
        DPRINT1("Recovery reboot failed (Error %lu)\n", GetLastError());
    }
}

static VOID
ScmFreeRecovery(PSCM_RECOVERY pRecovery)
{
    if (pRecovery->lpCommandLine != NULL)
        HeapFree(GetProcessHeap(), 0, pRecovery->lpCommandLine);
    if (pRecovery->lpRebootMessage != NULL)
        HeapFree(GetProcessHeap(), 0, pRecovery->lpRebootMessage);
    HeapFree(GetProcessHeap(), 0, pRecovery);
}

static DWORD
WINAPI
ScmRecoveryThread(PVOID pParam)
{
    PSCM_RECOVERY pRecovery = (PSCM_RECOVERY)pParam;
    PSERVICE lpService;
    DWORD dwError;

    Sleep(pRecovery->dwDelay);

    ScmLockDatabaseExclusive();
    lpService = ScmShutdown ? NULL : ScmGetServiceEntryByName(pRecovery->szServiceName);
    if (lpService != NULL && lpService->bDeleted)
        lpService = NULL;
    if (lpService != NULL)
        ScmReferenceService(lpService);
    ScmUnlockDatabase();

    if (lpService != NULL)
    {
        switch (pRecovery->Type)
        {
            case SC_ACTION_RESTART:
                if (lpService->dwStartType != SERVICE_DISABLED)
                {
                    dwError = ScmStartService(lpService, 0, NULL);
                    if (dwError != ERROR_SUCCESS && dwError != ERROR_SERVICE_ALREADY_RUNNING)
                        DPRINT1("Recovery restart of %S failed (Error %lu)\n", lpService->lpServiceName, dwError);
                }
                break;

            case SC_ACTION_RUN_COMMAND:
                dwError = ScmRunServiceCommand(lpService, pRecovery->lpCommandLine);
                if (dwError != ERROR_SUCCESS)
                    DPRINT1("Recovery command of %S failed (Error %lu)\n", lpService->lpServiceName, dwError);
                break;

            case SC_ACTION_REBOOT:
                ScmRebootForRecovery(pRecovery->lpRebootMessage);
                break;

            default:
                break;
        }

        ScmLockDatabaseExclusive();
        ScmDereferenceService(lpService);
        ScmUnlockDatabase();
    }

    ScmFreeRecovery(pRecovery);
    return 0;
}

static VOID
ScmLogServiceFailure(PSERVICE lpService, const SC_ACTION *lpAction)
{
    WCHAR szCount[16], szDelay[16], szType[16], szAction[80];
    LPCWSTR lpLogStrings[5];
    UINT uActionId;

    StringCchPrintfW(szCount, ARRAYSIZE(szCount), L"%lu", lpService->dwFailureCount);
    lpLogStrings[0] = lpService->lpDisplayName;
    lpLogStrings[1] = szCount;
    if (lpAction == NULL || lpAction->Type == SC_ACTION_NONE)
    {
        ScmLogEvent(EVENT_SERVICE_CRASH_NO_ACTION, EVENTLOG_ERROR_TYPE, 2, lpLogStrings);
        return;
    }

    switch (lpAction->Type)
    {
        case SC_ACTION_RESTART: uActionId = IDS_RECOVERY_RESTART; break;
        case SC_ACTION_REBOOT: uActionId = IDS_RECOVERY_REBOOT; break;
        case SC_ACTION_RUN_COMMAND: uActionId = IDS_RECOVERY_RUN_COMMAND; break;
        default: uActionId = IDS_RECOVERY_NONE; break;
    }
    if (!LoadStringW(GetModuleHandleW(NULL), uActionId, szAction, ARRAYSIZE(szAction)))
        szAction[0] = UNICODE_NULL;
    StringCchPrintfW(szDelay, ARRAYSIZE(szDelay), L"%lu", lpAction->Delay);
    StringCchPrintfW(szType, ARRAYSIZE(szType), L"%lu", (DWORD)lpAction->Type);
    lpLogStrings[2] = szDelay;
    lpLogStrings[3] = szType;
    lpLogStrings[4] = szAction;
    ScmLogEvent(EVENT_SERVICE_CRASH, EVENTLOG_ERROR_TYPE, 5, lpLogStrings);
}

static PSCM_RECOVERY
ScmPrepareRecovery(PSERVICE lpService, HKEY hServiceKey, const SC_ACTION *lpAction)
{
    PSCM_RECOVERY pRecovery;
    LPWSTR lpCommand = NULL;
    SIZE_T cbRecovery;

    cbRecovery = FIELD_OFFSET(SCM_RECOVERY, szServiceName) + (wcslen(lpService->lpServiceName) + 1) * sizeof(WCHAR);
    pRecovery = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbRecovery);
    if (pRecovery == NULL)
        return NULL;
    pRecovery->Type = lpAction->Type;
    pRecovery->dwDelay = lpAction->Delay;
    wcscpy(pRecovery->szServiceName, lpService->lpServiceName);

    if (lpAction->Type == SC_ACTION_REBOOT)
        ScmReadString(hServiceKey, L"RebootMessage", &pRecovery->lpRebootMessage);
    if (lpAction->Type != SC_ACTION_RUN_COMMAND)
        return pRecovery;

    if (ScmReadString(hServiceKey, L"FailureCommand", &lpCommand) == ERROR_SUCCESS)
    {
        pRecovery->lpCommandLine = ScmExpandFailureCount(lpCommand, lpService->dwFailureCount);
        HeapFree(GetProcessHeap(), 0, lpCommand);
    }
    if (pRecovery->lpCommandLine == NULL)
    {
        ScmFreeRecovery(pRecovery);
        return NULL;
    }
    return pRecovery;
}

static VOID
ScmHandleServiceFailure(PSERVICE lpService, BOOL bCrashed, HKEY hServiceKey,
                        const SERVICE_FAILURE_ACTIONS_VALUE *lpValue, DWORD dwActions)
{
    const SC_ACTION *lpAction = NULL;
    PSCM_RECOVERY pRecovery;
    ULONGLONG ullNow;
    HANDLE hThread;

    ullNow = GetTickCount64();
    if (lpService->dwFailureCount != 0 && lpValue != NULL && lpValue->dwResetPeriod != INFINITE &&
        ullNow - lpService->ullLastFailureTime >= (ULONGLONG)lpValue->dwResetPeriod * 1000)
    {
        lpService->dwFailureCount = 0;
    }
    lpService->dwFailureCount++;
    lpService->ullLastFailureTime = ullNow;

    if (dwActions != 0)
        lpAction = &((const SC_ACTION *)(lpValue + 1))[min(lpService->dwFailureCount, dwActions) - 1];
    if (bCrashed)
        ScmLogServiceFailure(lpService, lpAction);
    if (lpAction == NULL || lpAction->Type == SC_ACTION_NONE)
        return;

    pRecovery = ScmPrepareRecovery(lpService, hServiceKey, lpAction);
    if (pRecovery == NULL)
        return;
    hThread = CreateThread(NULL, 0, ScmRecoveryThread, pRecovery, 0, NULL);
    if (hThread == NULL)
    {
        DPRINT1("Cannot start the recovery of %S (Error %lu)\n", lpService->lpServiceName, GetLastError());
        ScmFreeRecovery(pRecovery);
        return;
    }
    CloseHandle(hThread);
}

/*
 * A service failed: its process terminated without reporting SERVICE_STOPPED (bCrashed), or it
 * stopped with a non-zero exit code, which counts only with FailureActionsOnNonCrashFailures.
 * The database lock must be held exclusively.
 */
VOID
ScmServiceFailure(PSERVICE lpService, BOOL bCrashed)
{
    PSERVICE_FAILURE_ACTIONS_VALUE lpValue = NULL;
    HKEY hServiceKey = NULL;
    DWORD dwActions = 0;
    DWORD dwNonCrash = 0;
    DWORD dwSize = sizeof(dwNonCrash);

    if (ScmShutdown)
        return;

    if (ScmOpenServiceKey(lpService->lpServiceName, KEY_READ, &hServiceKey) == ERROR_SUCCESS)
    {
        ScmReadFailureActions(hServiceKey, &lpValue, &dwActions);
        if (RegQueryValueExW(hServiceKey, L"FailureActionsOnNonCrashFailures", NULL, NULL,
                             (LPBYTE)&dwNonCrash, &dwSize) != ERROR_SUCCESS)
            dwNonCrash = 0;
    }
    if (bCrashed || (dwNonCrash != 0 && dwActions != 0))
        ScmHandleServiceFailure(lpService, bCrashed, hServiceKey, lpValue, dwActions);

    if (lpValue != NULL)
        HeapFree(GetProcessHeap(), 0, lpValue);
    if (hServiceKey != NULL)
        RegCloseKey(hServiceKey);
}
