/*
 * PROJECT:     LiberNT PSDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Restart Manager definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RESTARTMANAGER_H_
#define _RESTARTMANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define RM_SESSION_KEY_LEN sizeof(GUID)
#define CCH_RM_SESSION_KEY RM_SESSION_KEY_LEN*2
#define CCH_RM_MAX_APP_NAME 255
#define CCH_RM_MAX_SVC_NAME 63
#define RM_INVALID_TS_SESSION -1
#define RM_INVALID_PROCESS -1

typedef enum _RM_APP_TYPE {
    RmUnknownApp = 0,
    RmMainWindow = 1,
    RmOtherWindow = 2,
    RmService = 3,
    RmExplorer = 4,
    RmConsole = 5,
    RmCritical = 1000
} RM_APP_TYPE;

typedef enum _RM_SHUTDOWN_TYPE {
    RmForceShutdown = 0x1,
    RmShutdownOnlyRegistered = 0x10
} RM_SHUTDOWN_TYPE;

typedef enum _RM_APP_STATUS {
    RmStatusUnknown = 0x0,
    RmStatusRunning = 0x1,
    RmStatusStopped = 0x2,
    RmStatusStoppedOther = 0x4,
    RmStatusRestarted = 0x8,
    RmStatusErrorOnStop = 0x10,
    RmStatusErrorOnRestart = 0x20,
    RmStatusShutdownMasked = 0x40,
    RmStatusRestartMasked = 0x80
} RM_APP_STATUS;

typedef enum _RM_REBOOT_REASON {
    RmRebootReasonNone = 0x0,
    RmRebootReasonPermissionDenied = 0x1,
    RmRebootReasonSessionMismatch = 0x2,
    RmRebootReasonCriticalProcess = 0x4,
    RmRebootReasonCriticalService = 0x8,
    RmRebootReasonDetectedSelf = 0x10
} RM_REBOOT_REASON;

typedef struct _RM_UNIQUE_PROCESS {
    DWORD dwProcessId;
    FILETIME ProcessStartTime;
} RM_UNIQUE_PROCESS, *PRM_UNIQUE_PROCESS;

typedef struct _RM_PROCESS_INFO {
    RM_UNIQUE_PROCESS Process;
    WCHAR strAppName[CCH_RM_MAX_APP_NAME+1];
    WCHAR strServiceShortName[CCH_RM_MAX_SVC_NAME+1];
    RM_APP_TYPE ApplicationType;
    ULONG AppStatus;
    DWORD TSSessionId;
    BOOL bRestartable;
} RM_PROCESS_INFO, *PRM_PROCESS_INFO;

typedef enum _RM_FILTER_TRIGGER {
    RmFilterTriggerInvalid = 0,
    RmFilterTriggerFile,
    RmFilterTriggerProcess,
    RmFilterTriggerService
} RM_FILTER_TRIGGER;

typedef enum _RM_FILTER_ACTION {
    RmInvalidFilterAction = 0,
    RmNoRestart = 1,
    RmNoShutdown = 2
} RM_FILTER_ACTION;

typedef struct _RM_FILTER_INFO {
    RM_FILTER_ACTION FilterAction;
    RM_FILTER_TRIGGER FilterTrigger;
    DWORD cbNextOffset;
    union {
        LPWSTR strFilename;
        RM_UNIQUE_PROCESS Process;
        LPWSTR strServiceShortName;
    } DUMMYUNIONNAME;
} RM_FILTER_INFO, *PRM_FILTER_INFO;

typedef void (*RM_WRITE_STATUS_CALLBACK)(_In_ UINT nPercentComplete);

DWORD WINAPI RmStartSession(_Out_ DWORD *pSessionHandle, _Reserved_ DWORD dwSessionFlags, _Out_writes_(CCH_RM_SESSION_KEY+1) WCHAR strSessionKey[]);
DWORD WINAPI RmJoinSession(_Out_ DWORD *pSessionHandle, _In_reads_(CCH_RM_SESSION_KEY+1) const WCHAR strSessionKey[]);
DWORD WINAPI RmEndSession(_In_ DWORD dwSessionHandle);
DWORD WINAPI RmRegisterResources(_In_ DWORD dwSessionHandle, _In_ UINT nFiles, _In_reads_opt_(nFiles) LPCWSTR rgsFileNames[], _In_ UINT nApplications, _In_reads_opt_(nApplications) RM_UNIQUE_PROCESS rgApplications[], _In_ UINT nServices, _In_reads_opt_(nServices) LPCWSTR rgsServiceNames[]);
DWORD WINAPI RmGetList(_In_ DWORD dwSessionHandle, _Out_ UINT *pnProcInfoNeeded, _Inout_ UINT *pnProcInfo, _Inout_updates_opt_(*pnProcInfo) RM_PROCESS_INFO rgAffectedApps[], _Out_ LPDWORD lpdwRebootReasons);
DWORD WINAPI RmShutdown(_In_ DWORD dwSessionHandle, _In_ ULONG lActionFlags, _In_opt_ RM_WRITE_STATUS_CALLBACK fnStatus);
DWORD WINAPI RmRestart(_In_ DWORD dwSessionHandle, _Reserved_ DWORD dwRestartFlags, _In_opt_ RM_WRITE_STATUS_CALLBACK fnStatus);
DWORD WINAPI RmCancelCurrentTask(_In_ DWORD dwSessionHandle);
DWORD WINAPI RmAddFilter(_In_ DWORD dwSessionHandle, _In_opt_ LPCWSTR strModuleName, _In_opt_ RM_UNIQUE_PROCESS *pProcess, _In_opt_ LPCWSTR strServiceShortName, _In_ RM_FILTER_ACTION FilterAction);
DWORD WINAPI RmRemoveFilter(_In_ DWORD dwSessionHandle, _In_opt_ LPCWSTR strModuleName, _In_opt_ RM_UNIQUE_PROCESS *pProcess, _In_opt_ LPCWSTR strServiceShortName);
DWORD WINAPI RmGetFilterList(_In_ DWORD dwSessionHandle, _Out_writes_bytes_opt_(cbFilterBuf) PBYTE pbFilterBuf, _In_ DWORD cbFilterBuf, _Out_ LPDWORD cbFilterBufNeeded);

#ifdef __cplusplus
}
#endif

#endif
