/*
 * Task Scheduler Service
 *
 * Copyright 2014 Dmitry Timoshkov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "wtsapi32.h"
#include "winsvc.h"
#include "rpc.h"
#include "atsvc.h"
#include "schrpc.h"
#include "wine/debug.h"

#include "schedsvc_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(schedsvc);

static const WCHAR scheduleW[] = {'S','c','h','e','d','u','l','e',0};
static SERVICE_STATUS_HANDLE schedsvc_handle;
static HANDLE done_event, hjob_queue;

void add_process_to_queue(HANDLE process)
{
    if (!AssignProcessToJobObject(hjob_queue, process))
        ERR("AssignProcessToJobObject failed\n");
}

static DWORD WINAPI tasks_monitor_thread(void *arg)
{
    static const WCHAR tasksW[] = { '\\','T','a','s','k','s','\\',0 };
    WCHAR path[MAX_PATH];
    WCHAR name[MAX_PATH];
    FILE_NOTIFY_INFORMATION *entry;
    HANDLE htasks, hport, htimer;
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT job_info;
    OVERLAPPED ov;
    LARGE_INTEGER period;
    struct
    {
        FILE_NOTIFY_INFORMATION data;
        WCHAR name_buffer[MAX_PATH];
    } info;

    /* the buffer must be DWORD aligned */
    C_ASSERT(!(sizeof(info) & 3));

    TRACE("Starting...\n");

    load_at_tasks();
    check_missed_task_time();

    htimer = CreateWaitableTimerW(NULL, FALSE, NULL);
    if (htimer == NULL)
    {
        ERR("CreateWaitableTimer failed\n");
        return -1;
    }

    GetWindowsDirectoryW(path, MAX_PATH);
    lstrcatW(path, tasksW);

    /* Just in case it's an old Wine prefix with missing c:\windows\tasks */
    CreateDirectoryW(path, NULL);

    htasks = CreateFileW(path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (htasks == INVALID_HANDLE_VALUE)
    {
        ERR("Couldn't start monitoring %s for tasks, error %lu\n", debugstr_w(path), GetLastError());
        return -1;
    }

    hjob_queue = CreateJobObjectW(NULL, NULL);
    if (!hjob_queue)
    {
        ERR("CreateJobObject failed\n");
        return -1;
    }

    hport = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!hport)
    {
        ERR("CreateIoCompletionPort failed\n");
        return -1;
    }

    job_info.CompletionKey = hjob_queue;
    job_info.CompletionPort = hport;
    if (!SetInformationJobObject(hjob_queue, JobObjectAssociateCompletionPortInformation, &job_info, sizeof(job_info)))
    {
        ERR("SetInformationJobObject failed\n");
        return -1;
    }

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

    memset(&info, 0, sizeof(info));
    ReadDirectoryChangesW(htasks, &info, sizeof(info), FALSE,
                          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                          NULL, &ov, NULL);

    for (;;)
    {
        HANDLE events[4];
        DWORD ret;

        events[0] = done_event;
        events[1] = htimer;
        events[2] = hport;
        events[3] = ov.hEvent;

        ret = WaitForMultipleObjects(4, events, FALSE, INFINITE);
        /* Done event */
        if (ret == WAIT_OBJECT_0) break;

        /* Next runtime timer */
        if (ret == WAIT_OBJECT_0 + 1)
        {
            check_task_time();
            continue;
        }

        /* Job queue */
        if (ret == WAIT_OBJECT_0 + 2)
        {
            DWORD msg;
            ULONG_PTR dummy, pid;

            if (GetQueuedCompletionStatus(hport, &msg, &dummy, (OVERLAPPED **)&pid, 0))
            {
                if (msg == JOB_OBJECT_MSG_EXIT_PROCESS)
                {
                    TRACE("got message: process %#Ix has terminated\n", pid);
                    update_process_status(pid);
                }
                else
                    FIXME("got message %#lx from the job\n", msg);
            }
            continue;
        }

        /* Directory change notification */
        entry = &info.data;
        for (;;)
        {
            DWORD len = min(entry->FileNameLength / sizeof(WCHAR), MAX_PATH - 1);

            memcpy(name, entry->FileName, len * sizeof(WCHAR));
            name[len] = 0;

            switch (entry->Action)
            {
            case FILE_ACTION_ADDED:
                TRACE("FILE_ACTION_ADDED %s\n", debugstr_w(name));

                GetWindowsDirectoryW(path, MAX_PATH);
                lstrcatW(path, tasksW);
                lstrcatW(path, name);
                add_job(path);
                break;

            case FILE_ACTION_REMOVED:
                TRACE("FILE_ACTION_REMOVED %s\n", debugstr_w(name));
                GetWindowsDirectoryW(path, MAX_PATH);
                lstrcatW(path, tasksW);
                lstrcatW(path, name);
                remove_job(path);
                break;

            case FILE_ACTION_MODIFIED:
                TRACE("FILE_ACTION_MODIFIED %s\n", debugstr_w(name));

                GetWindowsDirectoryW(path, MAX_PATH);
                lstrcatW(path, tasksW);
                lstrcatW(path, name);
                update_job(path);
                break;

            default:
                FIXME("%s: action %#lx not handled\n", debugstr_w(name), entry->Action);
                break;
            }

            if (!entry->NextEntryOffset) break;
            entry = (FILE_NOTIFY_INFORMATION *)((BYTE *)entry + entry->NextEntryOffset);
        }

        check_task_state();

        if (get_next_runtime(&period))
        {
            if (!SetWaitableTimer(htimer, &period, 0, NULL, NULL, FALSE))
                ERR("SetWaitableTimer failed\n");
        }

        memset(&info, 0, sizeof(info));
        if (!ReadDirectoryChangesW(htasks, &info, sizeof(info), FALSE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                   NULL, &ov, NULL)) break;
    }

    CancelWaitableTimer(htimer);
    CloseHandle(htimer);
    CloseHandle(ov.hEvent);
    CloseHandle(hport);
    CloseHandle(hjob_queue);
    CloseHandle(htasks);

    TRACE("Finished.\n");

    return 0;
}

void schedsvc_auto_start(void)
{
    static DWORD start_type;
    SC_HANDLE scm, service;
    QUERY_SERVICE_CONFIGW *cfg;
    DWORD cfg_size;

    if (start_type == SERVICE_AUTO_START) return;

    TRACE("changing service start type to SERVICE_AUTO_START\n");

    scm = OpenSCManagerW(NULL, NULL, 0);
    if (!scm)
    {
        WARN("failed to open SCM (%lu)\n", GetLastError());
        return;
    }

    service = OpenServiceW(scm, scheduleW, SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
    if (service)
    {
        if (!QueryServiceConfigW(service, NULL, 0, &cfg_size) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            cfg = malloc(cfg_size);
            if (cfg)
            {
                if (QueryServiceConfigW(service, cfg, cfg_size, &cfg_size))
                {
                    start_type = cfg->dwStartType;
                    if (start_type != SERVICE_AUTO_START)
                    {
                        if (ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE,
                                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL))
                            start_type = SERVICE_AUTO_START;
                    }
                }
                free(cfg);
            }
        }
        else
            WARN("failed to query service config (%lu)\n", GetLastError());

        CloseServiceHandle(service);
    }
    else
        WARN("failed to open service (%lu)\n", GetLastError());

    CloseServiceHandle(scm);
}

static void schedsvc_update_status(DWORD state)
{
    SERVICE_STATUS status;

    status.dwServiceType = SERVICE_WIN32;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
    status.dwWin32ExitCode = 0;
    status.dwServiceSpecificExitCode = 0;
    status.dwCheckPoint = 0;
    status.dwWaitHint = 0;
    status.dwCurrentState = state;

    SetServiceStatus(schedsvc_handle, &status);
}

#define MAX_LOGON_SESSIONS 64

static CRITICAL_SECTION logon_sessions_cs;
static DWORD logon_sessions[MAX_LOGON_SESSIONS];
static UINT logon_session_count;

static DWORD WINAPI logon_tasks_worker(void *context)
{
    schedsvc_run_logon_tasks((DWORD)(ULONG_PTR)context);
    return 0;
}

/* Returns TRUE the first time a session's logon is seen, until that session logs off. */
static BOOL claim_logon_session(DWORD session_id)
{
    BOOL claimed = TRUE;
    UINT i;

    EnterCriticalSection(&logon_sessions_cs);
    for (i = 0; i < logon_session_count; i++)
        if (logon_sessions[i] == session_id) claimed = FALSE;
    if (claimed && logon_session_count < MAX_LOGON_SESSIONS)
        logon_sessions[logon_session_count++] = session_id;
    LeaveCriticalSection(&logon_sessions_cs);
    return claimed;
}

static void release_logon_session(DWORD session_id)
{
    UINT i;

    EnterCriticalSection(&logon_sessions_cs);
    for (i = 0; i < logon_session_count; i++)
    {
        if (logon_sessions[i] == session_id)
        {
            logon_sessions[i] = logon_sessions[--logon_session_count];
            break;
        }
    }
    LeaveCriticalSection(&logon_sessions_cs);
}

static void queue_logon_tasks(DWORD session_id)
{
    if (!claim_logon_session(session_id)) return;
    if (!QueueUserWorkItem(logon_tasks_worker, (void *)(ULONG_PTR)session_id, WT_EXECUTELONGFUNCTION))
        WARN("cannot queue the logon tasks of session %lu\n", session_id);
}

/* Users who logged on before the service started still get their logon tasks. */
static void queue_logged_on_sessions(void)
{
    WTS_SESSION_INFOW *sessions;
    DWORD count, i, bytes;
    WCHAR *user;

    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) return;
    for (i = 0; i < count; i++)
    {
        if (!sessions[i].SessionId) continue;
        if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessions[i].SessionId, WTSUserName, &user, &bytes))
            continue;
        if (user[0]) queue_logon_tasks(sessions[i].SessionId);
        WTSFreeMemory(user);
    }
    WTSFreeMemory(sessions);
}

static DWORD WINAPI schedsvc_handler(DWORD control, DWORD event_type, void *event_data, void *context)
{
    TRACE("%#lx %#lx\n", control, event_type);

    switch (control)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        schedsvc_update_status(SERVICE_STOP_PENDING);
        SetEvent(done_event);
        break;

    case SERVICE_CONTROL_SESSIONCHANGE:
        /* TODO: SessionStateChangeTrigger (lock, unlock, connect, disconnect) */
        if (event_type == WTS_SESSION_LOGON && event_data)
            queue_logon_tasks(((const WTSSESSION_NOTIFICATION *)event_data)->dwSessionId);
        else if (event_type == WTS_SESSION_LOGOFF && event_data)
            release_logon_session(((const WTSSESSION_NOTIFICATION *)event_data)->dwSessionId);
        break;

    default:
        schedsvc_update_status(SERVICE_RUNNING);
        break;
    }
    return NO_ERROR;
}

static RPC_BINDING_VECTOR *sched_bindings;

static RPC_STATUS RPC_init(void)
{
    static WCHAR ncacn_npW[] = { 'n','c','a','c','n','_','n','p',0 };
    static WCHAR endpoint_npW[] = { '\\','p','i','p','e','\\','a','t','s','v','c',0 };
    static WCHAR ncalrpcW[] = { 'n','c','a','l','r','p','c',0 };
    static WCHAR endpoint_lrpcW[] = { 'a','t','s','v','c',0 };
    RPC_STATUS status;

    status = RpcServerRegisterIf(ITaskSchedulerService_v1_0_s_ifspec, NULL, NULL);
    if (status != RPC_S_OK)
    {
        ERR("RpcServerRegisterIf error %#lx\n", status);
        return status;
    }

    status = RpcServerRegisterIf(atsvc_v1_0_s_ifspec, NULL, NULL);
    if (status != RPC_S_OK)
    {
        ERR("RpcServerRegisterIf error %#lx\n", status);
        RpcServerUnregisterIf(ITaskSchedulerService_v1_0_s_ifspec, NULL, FALSE);
        return status;
    }

    status = RpcServerUseProtseqEpW(ncacn_npW, RPC_C_PROTSEQ_MAX_REQS_DEFAULT, endpoint_npW, NULL);
    if (status != RPC_S_OK)
    {
        ERR("RpcServerUseProtseqEp error %#lx\n", status);
        return status;
    }

    status = RpcServerUseProtseqEpW(ncalrpcW, RPC_C_PROTSEQ_MAX_REQS_DEFAULT, endpoint_lrpcW, NULL);
    if (status != RPC_S_OK)
    {
        ERR("RpcServerUseProtseqEp error %#lx\n", status);
        return status;
    }

    status = RpcServerInqBindings(&sched_bindings);
    if (status != RPC_S_OK)
    {
        ERR("RpcServerInqBindings error %#lx\n", status);
        return status;
    }

    status = RpcEpRegisterW(ITaskSchedulerService_v1_0_s_ifspec, sched_bindings, NULL, NULL);
    if (status != RPC_S_OK)
    {
        ERR("RpcEpRegister error %#lx\n", status);
        return status;
    }

    status = RpcEpRegisterW(atsvc_v1_0_s_ifspec, sched_bindings, NULL, NULL);
    if (status != RPC_S_OK)
    {
        ERR("RpcEpRegister error %#lx\n", status);
        return status;
    }

    status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE);
    if (status != RPC_S_OK)
    {
        ERR("RpcServerListen error %#lx\n", status);
        return status;
    }
    return RPC_S_OK;
}

static void RPC_finish(void)
{
    RpcMgmtStopServerListening(NULL);
    RpcEpUnregister(ITaskSchedulerService_v1_0_s_ifspec, sched_bindings, NULL);
    RpcEpUnregister(atsvc_v1_0_s_ifspec, sched_bindings, NULL);
    RpcBindingVectorFree(&sched_bindings);
    RpcServerUnregisterIf(ITaskSchedulerService_v1_0_s_ifspec, NULL, FALSE);
    RpcServerUnregisterIf(atsvc_v1_0_s_ifspec, NULL, FALSE);
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    HANDLE thread;
    DWORD tid;

    TRACE("starting Task Scheduler Service\n");

    InitializeCriticalSection(&logon_sessions_cs);
    schedsvc_handle = RegisterServiceCtrlHandlerExW(scheduleW, schedsvc_handler, NULL);
    if (!schedsvc_handle)
    {
        ERR("RegisterServiceCtrlHandler error %ld\n", GetLastError());
        return;
    }

    schedsvc_update_status(SERVICE_START_PENDING);

    done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, tasks_monitor_thread, NULL, 0, &tid);

    if (thread && RPC_init() == RPC_S_OK)
    {
        schedsvc_update_status(SERVICE_RUNNING);
        queue_logged_on_sessions();
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        RPC_finish();
    }

    schedsvc_update_status(SERVICE_STOPPED);

    TRACE("exiting Task Scheduler Service\n");
}

void  __RPC_FAR * __RPC_USER MIDL_user_allocate(SIZE_T len)
{
    return malloc(len);
}

void __RPC_USER MIDL_user_free(void __RPC_FAR * ptr)
{
    free(ptr);
}
