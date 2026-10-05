/*
 * PROJECT:     LiberNT Restart Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Restart Manager sessions, affected application detection, shutdown and restart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>
#include <winsvc.h>
#include <psapi.h>
#include <rpc.h>
#define NTOS_MODE_USER
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>
#include <restartmanager.h>
#include <strsafe.h>

#define RM_MAX_SESSIONS 64
#define RM_DEFAULT_HUNG_APP_TIMEOUT 5000
#define RM_DEFAULT_WAIT_TO_KILL_APP_TIMEOUT 20000
#define RM_DEFAULT_WAIT_TO_KILL_SERVICE_TIMEOUT 5000
#define RM_SERVICE_POLL_INTERVAL 100

typedef struct _RM_FILTER
{
    RM_FILTER_ACTION Action;
    RM_FILTER_TRIGGER Trigger;
    LPWSTR Name;
    RM_UNIQUE_PROCESS Process;
} RM_FILTER, *PRM_FILTER;

typedef struct _RM_STOPPED_APP
{
    RM_PROCESS_INFO Info;
    WCHAR ImagePath[MAX_PATH];
    LPWSTR CommandLine;
} RM_STOPPED_APP, *PRM_STOPPED_APP;

typedef struct _RM_SESSION
{
    BOOL InUse;
    LONG References;
    volatile LONG Cancel;
    BOOL ShutdownDone;
    WCHAR Key[CCH_RM_SESSION_KEY + 1];
    UINT FileCount;
    LPWSTR *Files;
    UINT ApplicationCount;
    RM_UNIQUE_PROCESS *Applications;
    UINT ServiceCount;
    LPWSTR *Services;
    UINT FilterCount;
    RM_FILTER *Filters;
    UINT StoppedCount;
    RM_STOPPED_APP *Stopped;
} RM_SESSION, *PRM_SESSION;

typedef struct _RM_HANDLE
{
    BOOL InUse;
    BOOL Secondary;
    UINT Session;
} RM_HANDLE;

typedef struct _RM_LIST
{
    UINT Count;
    UINT Capacity;
    RM_PROCESS_INFO *Items;
    DWORD RebootReasons;
} RM_LIST, *PRM_LIST;

typedef struct _RM_WINDOW_QUERY
{
    DWORD ProcessId;
    BOOL MainWindow;
    BOOL OtherWindow;
} RM_WINDOW_QUERY;

typedef struct _RM_CLOSE_QUERY
{
    DWORD ProcessId;
    DWORD Timeout;
    BOOL Refused;
    UINT Count;
    HWND Windows[64];
} RM_CLOSE_QUERY;

static CRITICAL_SECTION RmLock;
static RM_SESSION RmSessions[RM_MAX_SESSIONS];
static RM_HANDLE RmHandles[RM_MAX_SESSIONS];

static PRM_SESSION
RmpGetSession(DWORD SessionHandle, BOOL *Secondary)
{
    if (SessionHandle >= RM_MAX_SESSIONS || !RmHandles[SessionHandle].InUse)
        return NULL;
    if (Secondary)
        *Secondary = RmHandles[SessionHandle].Secondary;
    return &RmSessions[RmHandles[SessionHandle].Session];
}

static DWORD
RmpAllocateHandle(UINT Session, BOOL Secondary, DWORD *Handle)
{
    DWORD Index;

    for (Index = 0; Index < RM_MAX_SESSIONS; Index++)
    {
        if (!RmHandles[Index].InUse)
        {
            RmHandles[Index].InUse = TRUE;
            RmHandles[Index].Secondary = Secondary;
            RmHandles[Index].Session = Session;
            RmSessions[Session].References++;
            *Handle = Index;
            return ERROR_SUCCESS;
        }
    }
    return ERROR_MAX_SESSIONS_REACHED;
}

static VOID
RmpFreeStrings(LPWSTR *Strings, UINT Count)
{
    UINT i;

    for (i = 0; i < Count; i++)
        HeapFree(GetProcessHeap(), 0, Strings[i]);
    HeapFree(GetProcessHeap(), 0, Strings);
}

static VOID
RmpFreeStopped(PRM_SESSION Session)
{
    UINT i;

    for (i = 0; i < Session->StoppedCount; i++)
        HeapFree(GetProcessHeap(), 0, Session->Stopped[i].CommandLine);
    HeapFree(GetProcessHeap(), 0, Session->Stopped);
    Session->Stopped = NULL;
    Session->StoppedCount = 0;
}

static VOID
RmpFreeSession(PRM_SESSION Session)
{
    UINT i;

    RmpFreeStrings(Session->Files, Session->FileCount);
    RmpFreeStrings(Session->Services, Session->ServiceCount);
    HeapFree(GetProcessHeap(), 0, Session->Applications);
    for (i = 0; i < Session->FilterCount; i++)
        HeapFree(GetProcessHeap(), 0, Session->Filters[i].Name);
    HeapFree(GetProcessHeap(), 0, Session->Filters);
    RmpFreeStopped(Session);
    ZeroMemory(Session, sizeof(*Session));
}

static LPWSTR
RmpDuplicateString(LPCWSTR String, BOOL FullPath)
{
    LPWSTR Copy;
    DWORD Length;

    Length = FullPath ? GetFullPathNameW(String, 0, NULL, NULL) : lstrlenW(String) + 1;
    if (!Length)
        return NULL;
    Copy = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (!Copy)
        return NULL;
    if (FullPath)
    {
        if (!GetFullPathNameW(String, Length, Copy, NULL))
        {
            HeapFree(GetProcessHeap(), 0, Copy);
            return NULL;
        }
    }
    else
    {
        CopyMemory(Copy, String, Length * sizeof(WCHAR));
    }
    return Copy;
}

static PVOID
RmpGrowArray(PVOID Array, SIZE_T Count, SIZE_T ElementSize)
{
    if (Array)
        return HeapReAlloc(GetProcessHeap(), 0, Array, Count * ElementSize);
    return HeapAlloc(GetProcessHeap(), 0, Count * ElementSize);
}

static DWORD
RmpAppendStrings(LPWSTR **Strings, UINT *Count, LPCWSTR New[], UINT NewCount, BOOL FullPath)
{
    LPWSTR *Array;
    UINT i;

    if (!NewCount)
        return ERROR_SUCCESS;
    for (i = 0; i < NewCount; i++)
    {
        if (!New[i])
            return ERROR_BAD_ARGUMENTS;
    }
    Array = RmpGrowArray(*Strings, *Count + NewCount, sizeof(LPWSTR));
    if (!Array)
        return ERROR_OUTOFMEMORY;
    *Strings = Array;
    for (i = 0; i < NewCount; i++)
    {
        Array[*Count] = RmpDuplicateString(New[i], FullPath);
        if (!Array[*Count])
            return ERROR_OUTOFMEMORY;
        (*Count)++;
    }
    return ERROR_SUCCESS;
}

static DWORD
RmpReadTimeout(HKEY Root, LPCWSTR SubKey, LPCWSTR ValueName, DWORD Default)
{
    WCHAR Buffer[16];
    DWORD Size = sizeof(Buffer) - sizeof(WCHAR), Type, Value = Default;
    HKEY Key;

    if (RegOpenKeyExW(Root, SubKey, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return Default;
    ZeroMemory(Buffer, sizeof(Buffer));
    if (RegQueryValueExW(Key, ValueName, NULL, &Type, (LPBYTE)Buffer, &Size) == ERROR_SUCCESS)
    {
        if (Type == REG_SZ)
            Value = wcstoul(Buffer, NULL, 10);
        else if (Type == REG_DWORD && Size == sizeof(DWORD))
            Value = *(DWORD *)Buffer;
    }
    RegCloseKey(Key);
    return Value ? Value : Default;
}

DWORD WINAPI
RmStartSession(DWORD *pSessionHandle, DWORD dwSessionFlags, WCHAR strSessionKey[])
{
    UUID Uuid;
    UINT Index, i;
    DWORD Error;
    PRM_SESSION Session;

    if (!pSessionHandle || !strSessionKey || dwSessionFlags)
        return ERROR_BAD_ARGUMENTS;

    if (UuidCreate(&Uuid) != RPC_S_OK)
        return ERROR_OUTOFMEMORY;

    EnterCriticalSection(&RmLock);
    for (Index = 0; Index < RM_MAX_SESSIONS; Index++)
    {
        if (!RmSessions[Index].InUse)
            break;
    }
    if (Index == RM_MAX_SESSIONS)
    {
        LeaveCriticalSection(&RmLock);
        return ERROR_MAX_SESSIONS_REACHED;
    }

    Session = &RmSessions[Index];
    ZeroMemory(Session, sizeof(*Session));
    Session->InUse = TRUE;
    for (i = 0; i < sizeof(Uuid); i++)
        StringCchPrintfW(&Session->Key[i * 2], 3, L"%02x", ((PBYTE)&Uuid)[i]);

    Error = RmpAllocateHandle(Index, FALSE, pSessionHandle);
    if (Error == ERROR_SUCCESS)
        CopyMemory(strSessionKey, Session->Key, sizeof(Session->Key));
    else
        RmpFreeSession(Session);
    LeaveCriticalSection(&RmLock);
    return Error;
}

DWORD WINAPI
RmJoinSession(DWORD *pSessionHandle, const WCHAR strSessionKey[])
{
    UINT Index;
    DWORD Error = ERROR_BAD_ARGUMENTS;

    if (!pSessionHandle || !strSessionKey)
        return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&RmLock);
    for (Index = 0; Index < RM_MAX_SESSIONS; Index++)
    {
        if (RmSessions[Index].InUse && !lstrcmpiW(RmSessions[Index].Key, strSessionKey))
        {
            Error = RmpAllocateHandle(Index, TRUE, pSessionHandle);
            break;
        }
    }
    LeaveCriticalSection(&RmLock);
    return Error;
}

DWORD WINAPI
RmEndSession(DWORD dwSessionHandle)
{
    PRM_SESSION Session;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, NULL);
    if (Session)
    {
        RmHandles[dwSessionHandle].InUse = FALSE;
        if (--Session->References == 0)
            RmpFreeSession(Session);
    }
    LeaveCriticalSection(&RmLock);
    return Session ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

DWORD WINAPI
RmRegisterResources(DWORD dwSessionHandle,
                    UINT nFiles,
                    LPCWSTR rgsFileNames[],
                    UINT nApplications,
                    RM_UNIQUE_PROCESS rgApplications[],
                    UINT nServices,
                    LPCWSTR rgsServiceNames[])
{
    PRM_SESSION Session;
    RM_UNIQUE_PROCESS *Applications;
    DWORD Error;

    if ((nFiles && !rgsFileNames) || (nApplications && !rgApplications) || (nServices && !rgsServiceNames))
        return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, NULL);
    if (!Session)
    {
        LeaveCriticalSection(&RmLock);
        return ERROR_INVALID_HANDLE;
    }

    Error = RmpAppendStrings(&Session->Files, &Session->FileCount, rgsFileNames, nFiles, TRUE);
    if (Error == ERROR_SUCCESS)
        Error = RmpAppendStrings(&Session->Services, &Session->ServiceCount, rgsServiceNames, nServices, FALSE);
    if (Error == ERROR_SUCCESS && nApplications)
    {
        Applications = RmpGrowArray(Session->Applications, Session->ApplicationCount + nApplications, sizeof(*Applications));
        if (Applications)
        {
            CopyMemory(&Applications[Session->ApplicationCount], rgApplications, nApplications * sizeof(*Applications));
            Session->Applications = Applications;
            Session->ApplicationCount += nApplications;
        }
        else
        {
            Error = ERROR_OUTOFMEMORY;
        }
    }
    LeaveCriticalSection(&RmLock);
    return Error;
}

static BOOL CALLBACK
RmpEnumWindowsProc(HWND hWnd, LPARAM lParam)
{
    RM_WINDOW_QUERY *Query = (RM_WINDOW_QUERY *)lParam;
    DWORD ProcessId = 0;

    GetWindowThreadProcessId(hWnd, &ProcessId);
    if (ProcessId != Query->ProcessId)
        return TRUE;
    if (IsWindowVisible(hWnd) && !GetWindow(hWnd, GW_OWNER))
    {
        Query->MainWindow = TRUE;
        return FALSE;
    }
    Query->OtherWindow = TRUE;
    return TRUE;
}

static VOID
RmpSetAppName(RM_PROCESS_INFO *Info, LPCWSTR ImagePath)
{
    struct
    {
        WORD Language;
        WORD CodePage;
    } *Translation;
    WCHAR Query[64];
    LPWSTR Description;
    LPCWSTR Name;
    PVOID Data;
    DWORD Size, Handle;
    UINT Length;

    Name = wcsrchr(ImagePath, L'\\');
    StringCchCopyW(Info->strAppName, ARRAYSIZE(Info->strAppName), Name ? Name + 1 : ImagePath);

    Size = GetFileVersionInfoSizeW(ImagePath, &Handle);
    if (!Size)
        return;
    Data = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Data)
        return;
    if (GetFileVersionInfoW(ImagePath, 0, Size, Data) &&
        VerQueryValueW(Data, L"\\VarFileInfo\\Translation", (LPVOID *)&Translation, &Length) &&
        Length >= sizeof(*Translation))
    {
        StringCchPrintfW(Query, ARRAYSIZE(Query), L"\\StringFileInfo\\%04x%04x\\FileDescription",
                         Translation->Language, Translation->CodePage);
        if (VerQueryValueW(Data, Query, (LPVOID *)&Description, &Length) && Length > 1)
            StringCchCopyW(Info->strAppName, ARRAYSIZE(Info->strAppName), Description);
    }
    HeapFree(GetProcessHeap(), 0, Data);
}

static RM_PROCESS_INFO *
RmpFindProcess(PRM_LIST List, DWORD ProcessId)
{
    UINT i;

    for (i = 0; i < List->Count; i++)
    {
        if (List->Items[i].Process.dwProcessId == ProcessId)
            return &List->Items[i];
    }
    return NULL;
}

static RM_PROCESS_INFO *
RmpAppendItem(PRM_LIST List)
{
    PVOID Items;

    if (List->Count == List->Capacity)
    {
        Items = RmpGrowArray(List->Items, List->Capacity ? List->Capacity * 2 : 16, sizeof(*List->Items));
        if (!Items)
            return NULL;
        List->Items = Items;
        List->Capacity = List->Capacity ? List->Capacity * 2 : 16;
    }
    ZeroMemory(&List->Items[List->Count], sizeof(*List->Items));
    return &List->Items[List->Count++];
}

static DWORD
RmpAddProcess(PRM_LIST List,
              DWORD ProcessId,
              const FILETIME *StartTime,
              LPCWSTR ServiceName,
              LPCWSTR ServiceDisplayName)
{
    RM_PROCESS_INFO *Info;
    RM_WINDOW_QUERY Query;
    PROCESS_BASIC_INFORMATION BasicInformation;
    SECTION_IMAGE_INFORMATION ImageInformation;
    FILETIME CreationTime, ExitTime, KernelTime, UserTime;
    WCHAR ImagePath[MAX_PATH], ExplorerPath[MAX_PATH];
    DWORD Length, SessionId, CurrentSessionId;
    ULONG BreakOnTermination = 0;
    HANDLE Process;

    Info = RmpFindProcess(List, ProcessId);
    if (Info)
    {
        if (ServiceName && Info->ApplicationType != RmService && Info->ApplicationType != RmCritical)
        {
            Info->ApplicationType = RmService;
            Info->bRestartable = TRUE;
            StringCchCopyW(Info->strServiceShortName, ARRAYSIZE(Info->strServiceShortName), ServiceName);
            StringCchCopyW(Info->strAppName, ARRAYSIZE(Info->strAppName), ServiceDisplayName ? ServiceDisplayName : ServiceName);
        }
        return ERROR_SUCCESS;
    }

    Process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ProcessId);
    if (!Process)
        Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
    if (!Process)
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            List->RebootReasons |= RmRebootReasonPermissionDenied;
        return ERROR_SUCCESS;
    }

    if (!GetProcessTimes(Process, &CreationTime, &ExitTime, &KernelTime, &UserTime) ||
        (StartTime && CompareFileTime(StartTime, &CreationTime) != 0) ||
        (NT_SUCCESS(NtQueryInformationProcess(Process, ProcessBasicInformation, &BasicInformation, sizeof(BasicInformation), NULL)) &&
         BasicInformation.ExitStatus != STATUS_PENDING))
    {
        CloseHandle(Process);
        return ERROR_SUCCESS;
    }

    Info = RmpAppendItem(List);
    if (!Info)
    {
        CloseHandle(Process);
        return ERROR_OUTOFMEMORY;
    }
    Info->Process.dwProcessId = ProcessId;
    Info->Process.ProcessStartTime = CreationTime;
    Info->AppStatus = RmStatusRunning;
    Info->TSSessionId = ProcessIdToSessionId(ProcessId, &SessionId) ? SessionId : (DWORD)RM_INVALID_TS_SESSION;

    if (ProcessIdToSessionId(GetCurrentProcessId(), &CurrentSessionId) &&
        Info->TSSessionId != (DWORD)RM_INVALID_TS_SESSION && Info->TSSessionId != CurrentSessionId)
    {
        List->RebootReasons |= RmRebootReasonSessionMismatch;
    }
    if (ProcessId == GetCurrentProcessId())
        List->RebootReasons |= RmRebootReasonDetectedSelf;

    Length = ARRAYSIZE(ImagePath);
    if (QueryFullProcessImageNameW(Process, 0, ImagePath, &Length))
        RmpSetAppName(Info, ImagePath);
    else
        ImagePath[0] = UNICODE_NULL;

    if (ProcessId == 4 ||
        (NT_SUCCESS(NtQueryInformationProcess(Process, ProcessBreakOnTermination, &BreakOnTermination, sizeof(BreakOnTermination), NULL)) &&
         BreakOnTermination))
    {
        Info->ApplicationType = RmCritical;
        List->RebootReasons |= ServiceName ? RmRebootReasonCriticalService : RmRebootReasonCriticalProcess;
    }
    else if (ServiceName)
    {
        Info->ApplicationType = RmService;
        Info->bRestartable = TRUE;
    }
    else
    {
        Query.ProcessId = ProcessId;
        Query.MainWindow = FALSE;
        Query.OtherWindow = FALSE;
        EnumWindows(RmpEnumWindowsProc, (LPARAM)&Query);
        if (ImagePath[0] && GetWindowsDirectoryW(ExplorerPath, ARRAYSIZE(ExplorerPath)) &&
            SUCCEEDED(StringCchCatW(ExplorerPath, ARRAYSIZE(ExplorerPath), L"\\explorer.exe")) &&
            !lstrcmpiW(ImagePath, ExplorerPath))
        {
            Info->ApplicationType = RmExplorer;
        }
        else if (Query.MainWindow)
        {
            Info->ApplicationType = RmMainWindow;
        }
        else if (Query.OtherWindow)
        {
            Info->ApplicationType = RmOtherWindow;
        }
        else if (NT_SUCCESS(NtQueryInformationProcess(Process, ProcessImageInformation, &ImageInformation, sizeof(ImageInformation), NULL)) &&
                 ImageInformation.SubSystemType == IMAGE_SUBSYSTEM_WINDOWS_CUI)
        {
            Info->ApplicationType = RmConsole;
        }
        else
        {
            Info->ApplicationType = RmUnknownApp;
        }
    }

    if (ServiceName)
    {
        StringCchCopyW(Info->strServiceShortName, ARRAYSIZE(Info->strServiceShortName), ServiceName);
        StringCchCopyW(Info->strAppName, ARRAYSIZE(Info->strAppName), ServiceDisplayName ? ServiceDisplayName : ServiceName);
    }

    CloseHandle(Process);
    return ERROR_SUCCESS;
}

static BOOL
RmpProcessUsesFile(HANDLE Process, PRM_SESSION Session)
{
    HMODULE StackModules[256];
    HMODULE *Modules = StackModules;
    WCHAR Path[MAX_PATH];
    DWORD Needed, Count, i, j;
    BOOL Found = FALSE;

    if (!EnumProcessModulesEx(Process, Modules, sizeof(StackModules), &Needed, LIST_MODULES_ALL))
        return FALSE;
    if (Needed > sizeof(StackModules))
    {
        Modules = HeapAlloc(GetProcessHeap(), 0, Needed);
        if (!Modules)
            return FALSE;
        if (!EnumProcessModulesEx(Process, Modules, Needed, &Needed, LIST_MODULES_ALL))
        {
            HeapFree(GetProcessHeap(), 0, Modules);
            return FALSE;
        }
    }

    Count = Needed / sizeof(HMODULE);
    for (i = 0; i < Count && !Found; i++)
    {
        if (!GetModuleFileNameExW(Process, Modules[i], Path, ARRAYSIZE(Path)))
            continue;
        for (j = 0; j < Session->FileCount; j++)
        {
            if (!lstrcmpiW(Path, Session->Files[j]))
            {
                Found = TRUE;
                break;
            }
        }
    }

    if (Modules != StackModules)
        HeapFree(GetProcessHeap(), 0, Modules);
    return Found;
}

static DWORD
RmpCollectFileUsers(PRM_SESSION Session, PRM_LIST List)
{
    DWORD *ProcessIds, Needed = 0, Size = 256 * sizeof(DWORD), i;
    HANDLE Process;
    DWORD Error = ERROR_SUCCESS;

    if (!Session->FileCount)
        return ERROR_SUCCESS;

    for (;;)
    {
        ProcessIds = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!ProcessIds)
            return ERROR_OUTOFMEMORY;
        if (!EnumProcesses(ProcessIds, Size, &Needed))
        {
            Error = GetLastError();
            HeapFree(GetProcessHeap(), 0, ProcessIds);
            return Error;
        }
        if (Needed < Size)
            break;
        HeapFree(GetProcessHeap(), 0, ProcessIds);
        Size *= 2;
    }

    for (i = 0; i < Needed / sizeof(DWORD) && Error == ERROR_SUCCESS; i++)
    {
        if (!ProcessIds[i])
            continue;
        Process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, ProcessIds[i]);
        if (!Process)
            continue;
        if (RmpProcessUsesFile(Process, Session))
            Error = RmpAddProcess(List, ProcessIds[i], NULL, NULL, NULL);
        CloseHandle(Process);
    }

    HeapFree(GetProcessHeap(), 0, ProcessIds);
    return Error;
}

static DWORD
RmpCollectServices(PRM_SESSION Session, PRM_LIST List)
{
    ENUM_SERVICE_STATUS_PROCESSW *Services = NULL;
    SC_HANDLE Manager;
    DWORD Needed = 0, Count = 0, Resume = 0, i, j;
    DWORD Error = ERROR_SUCCESS;
    BOOL Registered;

    if (!List->Count && !Session->ServiceCount)
        return ERROR_SUCCESS;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!Manager)
        return ERROR_SUCCESS;

    if (!EnumServicesStatusExW(Manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, NULL, 0, &Needed, &Count, &Resume, NULL) &&
        GetLastError() == ERROR_MORE_DATA)
    {
        Services = HeapAlloc(GetProcessHeap(), 0, Needed);
        Resume = 0;
        if (!Services ||
            !EnumServicesStatusExW(Manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, (LPBYTE)Services, Needed, &Needed, &Count, &Resume, NULL))
        {
            Count = 0;
        }
    }

    for (i = 0; i < Count && Error == ERROR_SUCCESS; i++)
    {
        if (!Services[i].ServiceStatusProcess.dwProcessId)
            continue;
        Registered = FALSE;
        for (j = 0; j < Session->ServiceCount; j++)
        {
            if (!lstrcmpiW(Services[i].lpServiceName, Session->Services[j]))
            {
                Registered = TRUE;
                break;
            }
        }
        if (Registered || RmpFindProcess(List, Services[i].ServiceStatusProcess.dwProcessId))
        {
            Error = RmpAddProcess(List,
                                  Services[i].ServiceStatusProcess.dwProcessId,
                                  NULL,
                                  Services[i].lpServiceName,
                                  Services[i].lpDisplayName);
        }
    }

    HeapFree(GetProcessHeap(), 0, Services);
    CloseServiceHandle(Manager);
    return Error;
}

static DWORD
RmpBuildList(PRM_SESSION Session, PRM_LIST List)
{
    DWORD Error = ERROR_SUCCESS;
    UINT i;

    for (i = 0; i < Session->ApplicationCount && Error == ERROR_SUCCESS; i++)
    {
        Error = RmpAddProcess(List,
                              Session->Applications[i].dwProcessId,
                              &Session->Applications[i].ProcessStartTime,
                              NULL,
                              NULL);
    }
    if (Error == ERROR_SUCCESS)
        Error = RmpCollectFileUsers(Session, List);
    if (Error == ERROR_SUCCESS)
        Error = RmpCollectServices(Session, List);
    return Error;
}

DWORD WINAPI
RmGetList(DWORD dwSessionHandle,
          UINT *pnProcInfoNeeded,
          UINT *pnProcInfo,
          RM_PROCESS_INFO rgAffectedApps[],
          LPDWORD lpdwRebootReasons)
{
    PRM_SESSION Session;
    RM_PROCESS_INFO *Info;
    RM_LIST List = { 0 };
    DWORD Error;
    UINT i;

    if (!pnProcInfoNeeded || !pnProcInfo || !lpdwRebootReasons || (*pnProcInfo && !rgAffectedApps))
        return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, NULL);
    if (!Session)
    {
        LeaveCriticalSection(&RmLock);
        return ERROR_INVALID_HANDLE;
    }

    Error = RmpBuildList(Session, &List);
    for (i = 0; i < Session->StoppedCount && Error == ERROR_SUCCESS; i++)
    {
        if (RmpFindProcess(&List, Session->Stopped[i].Info.Process.dwProcessId))
            continue;
        Info = RmpAppendItem(&List);
        if (!Info)
            Error = ERROR_OUTOFMEMORY;
        else
            *Info = Session->Stopped[i].Info;
    }
    LeaveCriticalSection(&RmLock);

    if (Error == ERROR_SUCCESS)
    {
        *pnProcInfoNeeded = List.Count;
        *lpdwRebootReasons = List.RebootReasons;
        if (*pnProcInfo < List.Count)
        {
            *pnProcInfo = 0;
            Error = ERROR_MORE_DATA;
        }
        else
        {
            if (List.Count)
                CopyMemory(rgAffectedApps, List.Items, List.Count * sizeof(*List.Items));
            *pnProcInfo = List.Count;
        }
    }

    HeapFree(GetProcessHeap(), 0, List.Items);
    return Error;
}

static PRM_FILTER
RmpFindFilter(PRM_SESSION Session, RM_FILTER_TRIGGER Trigger, LPCWSTR Name, const RM_UNIQUE_PROCESS *Process)
{
    UINT i;

    for (i = 0; i < Session->FilterCount; i++)
    {
        if (Session->Filters[i].Trigger != Trigger)
            continue;
        if (Trigger == RmFilterTriggerProcess)
        {
            if (Session->Filters[i].Process.dwProcessId == Process->dwProcessId &&
                !CompareFileTime(&Session->Filters[i].Process.ProcessStartTime, &Process->ProcessStartTime))
            {
                return &Session->Filters[i];
            }
        }
        else if (!lstrcmpiW(Session->Filters[i].Name, Name))
        {
            return &Session->Filters[i];
        }
    }
    return NULL;
}

static DWORD
RmpFilterKey(LPCWSTR strModuleName,
             RM_UNIQUE_PROCESS *pProcess,
             LPCWSTR strServiceShortName,
             RM_FILTER_TRIGGER *Trigger,
             LPCWSTR *Name)
{
    if ((strModuleName != NULL) + (pProcess != NULL) + (strServiceShortName != NULL) != 1)
        return ERROR_BAD_ARGUMENTS;
    *Trigger = strModuleName ? RmFilterTriggerFile : pProcess ? RmFilterTriggerProcess : RmFilterTriggerService;
    *Name = strModuleName ? strModuleName : strServiceShortName;
    return ERROR_SUCCESS;
}

DWORD WINAPI
RmAddFilter(DWORD dwSessionHandle,
            LPCWSTR strModuleName,
            RM_UNIQUE_PROCESS *pProcess,
            LPCWSTR strServiceShortName,
            RM_FILTER_ACTION FilterAction)
{
    PRM_SESSION Session;
    PRM_FILTER Filter, Filters;
    RM_FILTER_TRIGGER Trigger;
    LPCWSTR Name;
    BOOL Secondary;
    DWORD Error;

    Error = RmpFilterKey(strModuleName, pProcess, strServiceShortName, &Trigger, &Name);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (FilterAction != RmNoRestart && FilterAction != RmNoShutdown)
        return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, &Secondary);
    if (!Session)
        Error = ERROR_INVALID_HANDLE;
    else if (Secondary)
        Error = ERROR_SESSION_CREDENTIAL_CONFLICT;
    if (Error != ERROR_SUCCESS)
    {
        LeaveCriticalSection(&RmLock);
        return Error;
    }

    Filter = RmpFindFilter(Session, Trigger, Name, pProcess);
    if (Filter)
    {
        Filter->Action = FilterAction;
    }
    else
    {
        Filters = RmpGrowArray(Session->Filters, Session->FilterCount + 1, sizeof(*Filters));
        if (!Filters)
        {
            Error = ERROR_OUTOFMEMORY;
        }
        else
        {
            Session->Filters = Filters;
            Filter = &Filters[Session->FilterCount];
            ZeroMemory(Filter, sizeof(*Filter));
            Filter->Action = FilterAction;
            Filter->Trigger = Trigger;
            if (Trigger == RmFilterTriggerProcess)
            {
                Filter->Process = *pProcess;
                Session->FilterCount++;
            }
            else
            {
                Filter->Name = RmpDuplicateString(Name, Trigger == RmFilterTriggerFile);
                if (Filter->Name)
                    Session->FilterCount++;
                else
                    Error = ERROR_OUTOFMEMORY;
            }
        }
    }
    LeaveCriticalSection(&RmLock);
    return Error;
}

DWORD WINAPI
RmRemoveFilter(DWORD dwSessionHandle,
               LPCWSTR strModuleName,
               RM_UNIQUE_PROCESS *pProcess,
               LPCWSTR strServiceShortName)
{
    PRM_SESSION Session;
    PRM_FILTER Filter;
    RM_FILTER_TRIGGER Trigger;
    LPWSTR FullName = NULL;
    LPCWSTR Name;
    BOOL Secondary;
    DWORD Error;

    Error = RmpFilterKey(strModuleName, pProcess, strServiceShortName, &Trigger, &Name);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Trigger == RmFilterTriggerFile)
    {
        FullName = RmpDuplicateString(Name, TRUE);
        if (!FullName)
            return ERROR_OUTOFMEMORY;
        Name = FullName;
    }

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, &Secondary);
    if (!Session)
        Error = ERROR_INVALID_HANDLE;
    else if (Secondary)
        Error = ERROR_SESSION_CREDENTIAL_CONFLICT;
    if (Error == ERROR_SUCCESS)
    {
        Filter = RmpFindFilter(Session, Trigger, Name, pProcess);
        if (!Filter)
        {
            Error = ERROR_FILE_NOT_FOUND;
        }
        else
        {
            HeapFree(GetProcessHeap(), 0, Filter->Name);
            *Filter = Session->Filters[--Session->FilterCount];
        }
    }
    LeaveCriticalSection(&RmLock);
    HeapFree(GetProcessHeap(), 0, FullName);
    return Error;
}

DWORD WINAPI
RmGetFilterList(DWORD dwSessionHandle,
                PBYTE pbFilterBuf,
                DWORD cbFilterBuf,
                LPDWORD cbFilterBufNeeded)
{
    PRM_SESSION Session;
    RM_FILTER_INFO *Info;
    SIZE_T Offset, EntrySize, Total = 0;
    BOOL Secondary;
    DWORD Error = ERROR_SUCCESS;
    UINT i;

    if (!cbFilterBufNeeded || (cbFilterBuf && !pbFilterBuf))
        return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, &Secondary);
    if (!Session)
        Error = ERROR_INVALID_HANDLE;
    else if (Secondary)
        Error = ERROR_SESSION_CREDENTIAL_CONFLICT;
    if (Error != ERROR_SUCCESS)
    {
        LeaveCriticalSection(&RmLock);
        return Error;
    }

    for (i = 0; i < Session->FilterCount; i++)
    {
        EntrySize = sizeof(RM_FILTER_INFO);
        if (Session->Filters[i].Name)
            EntrySize += (lstrlenW(Session->Filters[i].Name) + 1) * sizeof(WCHAR);
        Total += (EntrySize + sizeof(PVOID) - 1) & ~(sizeof(PVOID) - 1);
    }

    *cbFilterBufNeeded = (DWORD)Total;
    if (cbFilterBuf < Total)
    {
        LeaveCriticalSection(&RmLock);
        return ERROR_INSUFFICIENT_BUFFER;
    }

    Offset = 0;
    for (i = 0; i < Session->FilterCount; i++)
    {
        Info = (RM_FILTER_INFO *)(pbFilterBuf + Offset);
        ZeroMemory(Info, sizeof(*Info));
        Info->FilterAction = Session->Filters[i].Action;
        Info->FilterTrigger = Session->Filters[i].Trigger;
        EntrySize = sizeof(RM_FILTER_INFO);
        if (Session->Filters[i].Trigger == RmFilterTriggerProcess)
        {
            Info->Process = Session->Filters[i].Process;
        }
        else
        {
            Info->strFilename = (LPWSTR)(Info + 1);
            StringCchCopyW(Info->strFilename, lstrlenW(Session->Filters[i].Name) + 1, Session->Filters[i].Name);
            EntrySize += (lstrlenW(Session->Filters[i].Name) + 1) * sizeof(WCHAR);
        }
        EntrySize = (EntrySize + sizeof(PVOID) - 1) & ~(sizeof(PVOID) - 1);
        Info->cbNextOffset = (i + 1 < Session->FilterCount) ? (DWORD)EntrySize : 0;
        Offset += EntrySize;
    }
    LeaveCriticalSection(&RmLock);
    return ERROR_SUCCESS;
}

DWORD WINAPI
RmCancelCurrentTask(DWORD dwSessionHandle)
{
    PRM_SESSION Session;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, NULL);
    LeaveCriticalSection(&RmLock);
    if (!Session)
        return ERROR_INVALID_HANDLE;
    InterlockedExchange(&Session->Cancel, TRUE);
    return ERROR_SUCCESS;
}

static RM_FILTER_ACTION
RmpFilterAction(PRM_SESSION Session, const RM_PROCESS_INFO *Info, LPCWSTR ImagePath)
{
    PRM_FILTER Filter;

    Filter = RmpFindFilter(Session, RmFilterTriggerProcess, NULL, &Info->Process);
    if (!Filter && Info->ApplicationType == RmService)
        Filter = RmpFindFilter(Session, RmFilterTriggerService, Info->strServiceShortName, NULL);
    if (!Filter && ImagePath[0])
        Filter = RmpFindFilter(Session, RmFilterTriggerFile, ImagePath, NULL);
    return Filter ? Filter->Action : RmInvalidFilterAction;
}

static BOOL CALLBACK
RmpCollectWindowsProc(HWND hWnd, LPARAM lParam)
{
    RM_CLOSE_QUERY *Query = (RM_CLOSE_QUERY *)lParam;
    DWORD ProcessId = 0;

    GetWindowThreadProcessId(hWnd, &ProcessId);
    if (ProcessId == Query->ProcessId && Query->Count < ARRAYSIZE(Query->Windows))
        Query->Windows[Query->Count++] = hWnd;
    return TRUE;
}

static BOOL
RmpStopWindowApp(DWORD ProcessId, HANDLE Process, BOOL Force)
{
    RM_CLOSE_QUERY Query;
    DWORD_PTR Result;
    DWORD WaitTimeout;
    UINT i;

    ZeroMemory(&Query, sizeof(Query));
    Query.ProcessId = ProcessId;
    Query.Timeout = RmpReadTimeout(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"HungAppTimeout", RM_DEFAULT_HUNG_APP_TIMEOUT);
    WaitTimeout = RmpReadTimeout(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WaitToKillAppTimeout", RM_DEFAULT_WAIT_TO_KILL_APP_TIMEOUT);
    EnumWindows(RmpCollectWindowsProc, (LPARAM)&Query);

    for (i = 0; i < Query.Count; i++)
    {
        if (!SendMessageTimeoutW(Query.Windows[i], WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP,
                                 SMTO_ABORTIFHUNG, Query.Timeout, &Result) || !Result)
        {
            Query.Refused = TRUE;
            break;
        }
    }

    for (i = 0; i < Query.Count; i++)
    {
        SendMessageTimeoutW(Query.Windows[i], WM_ENDSESSION, !Query.Refused, ENDSESSION_CLOSEAPP,
                            SMTO_ABORTIFHUNG, Query.Timeout, &Result);
    }

    if (!Query.Refused && WaitForSingleObject(Process, WaitTimeout) == WAIT_OBJECT_0)
        return TRUE;
    if (Force && TerminateProcess(Process, ERROR_SUCCESS))
        return WaitForSingleObject(Process, WaitTimeout) == WAIT_OBJECT_0;
    return FALSE;
}

static BOOL
RmpStopService(LPCWSTR ServiceName, HANDLE Process, BOOL Force)
{
    SERVICE_STATUS_PROCESS Status;
    SERVICE_STATUS ControlStatus;
    SC_HANDLE Manager, Service;
    DWORD Needed, Timeout, Waited = 0;
    BOOL Stopped = FALSE;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!Manager)
        return FALSE;
    Service = OpenServiceW(Manager, ServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!Service)
    {
        CloseServiceHandle(Manager);
        return FALSE;
    }

    Timeout = RmpReadTimeout(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"WaitToKillServiceTimeout", RM_DEFAULT_WAIT_TO_KILL_SERVICE_TIMEOUT);
    if (ControlService(Service, SERVICE_CONTROL_STOP, &ControlStatus) || GetLastError() == ERROR_SERVICE_NOT_ACTIVE)
    {
        while (QueryServiceStatusEx(Service, SC_STATUS_PROCESS_INFO, (LPBYTE)&Status, sizeof(Status), &Needed))
        {
            if (Status.dwCurrentState == SERVICE_STOPPED)
            {
                Stopped = TRUE;
                break;
            }
            if (Waited >= Timeout)
                break;
            Sleep(RM_SERVICE_POLL_INTERVAL);
            Waited += RM_SERVICE_POLL_INTERVAL;
        }
    }

    if (!Stopped && Force && Process && TerminateProcess(Process, ERROR_SUCCESS))
        Stopped = WaitForSingleObject(Process, Timeout) == WAIT_OBJECT_0;

    CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
    return Stopped;
}

static LPWSTR
RmpGetRestartCommandLine(HANDLE Process, LPCWSTR ImagePath)
{
    WCHAR Arguments[RESTART_MAX_CMD_LINE];
    DWORD Size = ARRAYSIZE(Arguments), Flags = 0;
    SIZE_T Length;
    LPWSTR CommandLine;

    if (FAILED(GetApplicationRestartSettings(Process, Arguments, &Size, &Flags)) || (Flags & RESTART_NO_PATCH))
        return NULL;

    Length = lstrlenW(ImagePath) + lstrlenW(Arguments) + 4;
    CommandLine = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (CommandLine)
        StringCchPrintfW(CommandLine, Length, L"\"%s\" %s", ImagePath, Arguments);
    return CommandLine;
}

DWORD WINAPI
RmShutdown(DWORD dwSessionHandle, ULONG lActionFlags, RM_WRITE_STATUS_CALLBACK fnStatus)
{
    PRM_SESSION Session;
    PRM_STOPPED_APP Stopped;
    RM_PROCESS_INFO *Info;
    RM_LIST List = { 0 };
    HANDLE Process;
    WCHAR ImagePath[MAX_PATH];
    LPWSTR CommandLine;
    DWORD Error, Length;
    BOOL Secondary, Force, Failed = FALSE, Ok;
    UINT i;

    if (lActionFlags & ~(RmForceShutdown | RmShutdownOnlyRegistered))
        return ERROR_BAD_ARGUMENTS;
    Force = (lActionFlags & RmForceShutdown) != 0;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, &Secondary);
    if (!Session || Secondary)
    {
        LeaveCriticalSection(&RmLock);
        return Session ? ERROR_SESSION_CREDENTIAL_CONFLICT : ERROR_INVALID_HANDLE;
    }
    InterlockedExchange(&Session->Cancel, FALSE);

    Error = RmpBuildList(Session, &List);
    if (Error == ERROR_SUCCESS &&
        (List.RebootReasons & (RmRebootReasonCriticalProcess | RmRebootReasonCriticalService | RmRebootReasonDetectedSelf)))
    {
        Error = ERROR_FAIL_NOACTION_REBOOT;
    }

    if (Error == ERROR_SUCCESS && (lActionFlags & RmShutdownOnlyRegistered))
    {
        for (i = 0; i < List.Count && Error == ERROR_SUCCESS; i++)
        {
            if (List.Items[i].ApplicationType == RmService)
                continue;
            Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, List.Items[i].Process.dwProcessId);
            Length = ARRAYSIZE(ImagePath);
            if (!Process || !QueryFullProcessImageNameW(Process, 0, ImagePath, &Length))
                Error = ERROR_FAIL_SHUTDOWN;
            else
            {
                CommandLine = RmpGetRestartCommandLine(Process, ImagePath);
                if (!CommandLine)
                    Error = ERROR_FAIL_SHUTDOWN;
                HeapFree(GetProcessHeap(), 0, CommandLine);
            }
            if (Process)
                CloseHandle(Process);
        }
    }

    if (Error == ERROR_SUCCESS)
    {
        RmpFreeStopped(Session);
        Session->Stopped = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, max(List.Count, 1) * sizeof(*Session->Stopped));
        if (!Session->Stopped)
            Error = ERROR_OUTOFMEMORY;
    }

    for (i = 0; i < List.Count && Error == ERROR_SUCCESS; i++)
    {
        if (Session->Cancel)
        {
            Error = ERROR_CANCELLED;
            break;
        }

        Info = &List.Items[i];
        Process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Info->Process.dwProcessId);
        Length = ARRAYSIZE(ImagePath);
        if (!Process || !QueryFullProcessImageNameW(Process, 0, ImagePath, &Length))
            ImagePath[0] = UNICODE_NULL;

        Stopped = &Session->Stopped[Session->StoppedCount];
        Stopped->Info = *Info;
        StringCchCopyW(Stopped->ImagePath, ARRAYSIZE(Stopped->ImagePath), ImagePath);

        if (RmpFilterAction(Session, Info, ImagePath) == RmNoShutdown)
        {
            Stopped->Info.AppStatus = RmStatusRunning | RmStatusShutdownMasked;
        }
        else
        {
            if (Info->ApplicationType != RmService && Process && ImagePath[0])
                Stopped->CommandLine = RmpGetRestartCommandLine(Process, ImagePath);

            if (Info->ApplicationType == RmService)
                Ok = RmpStopService(Info->strServiceShortName, Process, Force);
            else if (Process && (Info->ApplicationType == RmMainWindow || Info->ApplicationType == RmOtherWindow || Info->ApplicationType == RmExplorer))
                Ok = RmpStopWindowApp(Info->Process.dwProcessId, Process, Force);
            else if (Process && Force)
                Ok = TerminateProcess(Process, ERROR_SUCCESS) &&
                     WaitForSingleObject(Process, RmpReadTimeout(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WaitToKillAppTimeout", RM_DEFAULT_WAIT_TO_KILL_APP_TIMEOUT)) == WAIT_OBJECT_0;
            else
                Ok = FALSE;

            if (Ok)
            {
                Stopped->Info.AppStatus = RmStatusStopped;
            }
            else
            {
                Stopped->Info.AppStatus = RmStatusErrorOnStop;
                Failed = TRUE;
            }
        }
        Session->StoppedCount++;

        if (Process)
            CloseHandle(Process);
        if (fnStatus)
            fnStatus((i + 1) * 100 / List.Count);
    }

    if (Error == ERROR_SUCCESS)
    {
        Session->ShutdownDone = TRUE;
        if (!List.Count && fnStatus)
            fnStatus(100);
        if (Failed)
            Error = ERROR_FAIL_SHUTDOWN;
    }
    LeaveCriticalSection(&RmLock);

    HeapFree(GetProcessHeap(), 0, List.Items);
    return Error;
}

DWORD WINAPI
RmRestart(DWORD dwSessionHandle, DWORD dwRestartFlags, RM_WRITE_STATUS_CALLBACK fnStatus)
{
    PRM_SESSION Session;
    PRM_STOPPED_APP Stopped;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInformation;
    SC_HANDLE Manager = NULL, Service;
    DWORD Error = ERROR_SUCCESS;
    BOOL Secondary, Failed = FALSE, Ok;
    UINT i;

    if (dwRestartFlags)
        return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&RmLock);
    Session = RmpGetSession(dwSessionHandle, &Secondary);
    if (!Session || Secondary)
    {
        LeaveCriticalSection(&RmLock);
        return Session ? ERROR_SESSION_CREDENTIAL_CONFLICT : ERROR_INVALID_HANDLE;
    }
    if (!Session->ShutdownDone)
    {
        LeaveCriticalSection(&RmLock);
        return ERROR_REQUEST_OUT_OF_SEQUENCE;
    }
    InterlockedExchange(&Session->Cancel, FALSE);

    for (i = 0; i < Session->StoppedCount; i++)
    {
        if (Session->Cancel)
        {
            Error = ERROR_CANCELLED;
            break;
        }

        Stopped = &Session->Stopped[i];
        if (Stopped->Info.AppStatus != RmStatusStopped)
            continue;
        if (RmpFilterAction(Session, &Stopped->Info, Stopped->ImagePath) == RmNoRestart)
        {
            Stopped->Info.AppStatus |= RmStatusRestartMasked;
            continue;
        }

        Ok = TRUE;
        if (Stopped->Info.ApplicationType == RmService)
        {
            if (!Manager)
                Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
            Service = Manager ? OpenServiceW(Manager, Stopped->Info.strServiceShortName, SERVICE_START) : NULL;
            Ok = Service && (StartServiceW(Service, 0, NULL) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING);
            if (Service)
                CloseServiceHandle(Service);
        }
        else if (Stopped->CommandLine)
        {
            ZeroMemory(&StartupInfo, sizeof(StartupInfo));
            StartupInfo.cb = sizeof(StartupInfo);
            Ok = CreateProcessW(NULL, Stopped->CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInformation);
            if (Ok)
            {
                CloseHandle(ProcessInformation.hThread);
                CloseHandle(ProcessInformation.hProcess);
            }
        }
        else
        {
            continue;
        }

        if (Ok)
        {
            Stopped->Info.AppStatus = RmStatusRestarted;
        }
        else
        {
            Stopped->Info.AppStatus = RmStatusErrorOnRestart;
            Failed = TRUE;
        }
        if (fnStatus)
            fnStatus((i + 1) * 100 / Session->StoppedCount);
    }

    if (Manager)
        CloseServiceHandle(Manager);
    if (Error == ERROR_SUCCESS)
    {
        Session->ShutdownDone = FALSE;
        if (fnStatus)
            fnStatus(100);
        if (Failed)
            Error = ERROR_FAIL_RESTART;
    }
    LeaveCriticalSection(&RmLock);
    return Error;
}

BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    UINT i;

    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&RmLock);
    }
    else if (fdwReason == DLL_PROCESS_DETACH && !lpvReserved)
    {
        for (i = 0; i < RM_MAX_SESSIONS; i++)
        {
            if (RmSessions[i].InUse)
                RmpFreeSession(&RmSessions[i]);
        }
        DeleteCriticalSection(&RmLock);
    }
    return TRUE;
}
