/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Accept RDP connections in session 0 and hand each one to the server of its session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_NO_STATUS
#include "broker.h"
#include <ws2tcpip.h>
#include <wtsapi32.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <stdio.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#define NTOS_MODE_USER
#include <ndk/ntndk.h>
#include <sm/smmsg.h>

#define MAX_AGENTS 16

typedef struct AGENT
{
    DWORD Session;
    HANDLE Process;
    HSWDEVICE Display;
    WCHAR DisplayInstance[MAX_DEVICE_ID_LEN];
    WCHAR User[257];
    HANDLE Winlogon;
} AGENT;

static BROKER_LOG Log;
static BROKER_START_AGENT StartAgent;
static BROKER_ALLOWED Allowed;
static HANDLE BrokerThread, BrokerStopEvent;
static CRITICAL_SECTION AgentLock;
static HANDLE ConsoleAgent;
static DWORD ConsoleSession;
static AGENT Agents[MAX_AGENTS];
static LONG NextDisplay;

static BOOL ReadCookie(SOCKET client, WCHAR *user, DWORD count, BOOL *routed)
{
    BYTE data[512];
    fd_set readable;
    struct timeval timeout = {10, 0};
    DWORD start = GetTickCount();
    int got;
    for (;;)
    {
        FD_ZERO(&readable);
        FD_SET(client, &readable);
        if (select(0, &readable, NULL, NULL, &timeout) != 1) return FALSE;
        got = recv(client, (char *)data, sizeof(data), MSG_PEEK);
        if (got <= 0) return FALSE;
        if (got >= 4 && ((data[2] << 8) | data[3]) <= got) return BrokerCookieUser(data, got, user, count, routed);
        if (got == sizeof(data) || GetTickCount() - start > 10000) return FALSE;
        Sleep(20);
    }
}

static BOOL AgentAlive(HANDLE process)
{
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

/* winpr links its own WTS functions; the session's user comes from the system's wtsapi32 */
static BOOL SessionUser(DWORD session, WCHAR *user, DWORD count)
{
    typedef BOOL (WINAPI *QUERY)(HANDLE, DWORD, WTS_INFO_CLASS, WCHAR **, DWORD *);
    typedef void (WINAPI *FREE)(void *);
    static QUERY Query;
    static FREE Free;
    HMODULE wtsapi;
    WCHAR *name = NULL;
    DWORD size = 0;
    *user = 0;
    if (!Query && (wtsapi = LoadLibraryW(L"wtsapi32.dll")))
    {
        Free = (FREE)(void *)GetProcAddress(wtsapi, "WTSFreeMemory");
        Query = Free ? (QUERY)(void *)GetProcAddress(wtsapi, "WTSQuerySessionInformationW") : NULL;
    }
    if (!Query || !Query(WTS_CURRENT_SERVER_HANDLE, session, WTSUserName, &name, &size) || !name) return FALSE;
    lstrcpynW(user, name, count);
    Free(name);
    return TRUE;
}

typedef struct DISPLAY_CREATION
{
    LONG References;
    HANDLE Ready;
    HRESULT Result;
    WCHAR Instance[MAX_DEVICE_ID_LEN];
} DISPLAY_CREATION;

static void ReleaseCreation(DISPLAY_CREATION *creation)
{
    if (InterlockedDecrement(&creation->References)) return;
    if (creation->Ready) CloseHandle(creation->Ready);
    HeapFree(GetProcessHeap(), 0, creation);
}

static VOID WINAPI DisplayCreated(HSWDEVICE device, HRESULT result, PVOID context, PCWSTR instance)
{
    DISPLAY_CREATION *creation = context;
    (void)device;
    if (SUCCEEDED(result) && instance) lstrcpynW(creation->Instance, instance, ARRAYSIZE(creation->Instance));
    creation->Result = SUCCEEDED(result) && !instance ? E_UNEXPECTED : result;
    SetEvent(creation->Ready);
    ReleaseCreation(creation);
}

static BOOL DisplayStarted(const WCHAR *instance)
{
    ULONG status = 0, problem = 0;
    DEVINST node;
    return CM_Locate_DevNodeW(&node, (DEVINSTID_W)instance, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
           CM_Get_DevNode_Status(&status, &problem, node, 0) == CR_SUCCESS && (status & DN_STARTED);
}

static void CloseAgent(AGENT *agent)
{
    if (agent->Process)
    {
        if (AgentAlive(agent->Process)) TerminateProcess(agent->Process, 1);
        CloseHandle(agent->Process);
    }
    if (agent->Display) SwDeviceClose(agent->Display);
    if (agent->Winlogon) CloseHandle(agent->Winlogon);
    ZeroMemory(agent, sizeof(*agent));
}

/* A new session's win32k needs its display before csrss initializes, so the adapter is made first */
static AGENT *StartNewSession(const WCHAR *user)
{
    static const WCHAR hardwareIds[] = L"RdpVdd_RemoteDisplay\0";
    SW_DEVICE_CREATE_INFO info = {sizeof(info)};
    WCHAR name[64];
    HANDLE port = NULL, csrss = NULL, initial = NULL;
    AGENT *agent = NULL;
    DISPLAY_CREATION *creation;
    NTSTATUS status;
    ULONG session = 0;
    DWORD i, wait;
    BOOL created;
    for (i = 0; i < MAX_AGENTS && !agent; ++i)
        if (!Agents[i].Process && !Agents[i].Display) agent = &Agents[i];
    if (!agent)
    {
        Log("No room for another session");
        return NULL;
    }
    _snwprintf(name, ARRAYSIZE(name), BROKER_DISPLAY_PREFIX L"%ld", InterlockedIncrement(&NextDisplay));
    name[ARRAYSIZE(name) - 1] = 0;
    info.pszInstanceId = name;
    info.pszzHardwareIds = hardwareIds;
    info.pszzCompatibleIds = hardwareIds;
    info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                           SWDeviceCapabilitiesDriverRequired;
    info.pszDeviceDescription = L"LiberNT Remote Display Adapter";
    creation = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*creation));
    if (!creation || !(creation->Ready = CreateEventW(NULL, TRUE, FALSE, NULL)))
    {
        if (creation) HeapFree(GetProcessHeap(), 0, creation);
        Log("Cannot create the display of a new session: out of memory");
        goto failed;
    }
    creation->References = 2;
    creation->Result = E_FAIL;
    if (FAILED(SwDeviceCreate(L"RemoteDisplayEnum", L"HTREE\\ROOT\\0", &info, 0, NULL, DisplayCreated, creation,
                              &agent->Display)))
    {
        agent->Display = NULL;
        ReleaseCreation(creation);
        ReleaseCreation(creation);
        Log("Cannot create the display of a new session");
        goto failed;
    }
    created = WaitForSingleObject(creation->Ready, 15000) == WAIT_OBJECT_0 && SUCCEEDED(creation->Result);
    if (created) lstrcpynW(agent->DisplayInstance, creation->Instance, ARRAYSIZE(agent->DisplayInstance));
    else Log("Cannot create the display of a new session: 0x%08lx", creation->Result);
    ReleaseCreation(creation);
    if (!created) goto failed;
    for (wait = 0; wait < 150 && !DisplayStarted(agent->DisplayInstance); ++wait) Sleep(100);
    if (!DisplayStarted(agent->DisplayInstance))
    {
        Log("The display of a new session did not start");
        goto failed;
    }
    status = SmConnectToSm(NULL, NULL, 0, &port);
    if (NT_SUCCESS(status)) status = SmStartCsr(port, &session, NULL, &csrss, &initial);
    if (port) NtClose(port);
    if (!NT_SUCCESS(status))
    {
        Log("Cannot start a new session: 0x%08lx", status);
        goto failed;
    }
    agent->Session = session;
    agent->Winlogon = OpenProcess(SYNCHRONIZE, FALSE, HandleToUlong(initial));
    lstrcpynW(agent->User, user, ARRAYSIZE(agent->User));
    Log("Session %lu started for %ls on %ls", session, user, agent->DisplayInstance);
    return agent;
failed:
    CloseAgent(agent);
    return NULL;
}

static BOOL HandOff(SOCKET client, HANDLE *process, DWORD session, const WCHAR *display, BOOL restart, BOOL named)
{
    WCHAR pipe[64];
    BROKER_HANDOFF message;
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD written = 0, tries, server = 0;
    _snwprintf(pipe, ARRAYSIZE(pipe), BROKER_PIPE_FORMAT, session);
    pipe[ARRAYSIZE(pipe) - 1] = 0;
    for (tries = 0; tries < 300 && handle == INVALID_HANDLE_VALUE; ++tries)
    {
        if (!AgentAlive(*process))
        {
            if (!restart) return FALSE;
            if (*process) CloseHandle(*process);
            *process = tries < 200 ? StartAgent(session, display) : NULL;
            if (!*process) return FALSE;
        }
        handle = CreateFileW(pipe, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                             SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
        if (handle == INVALID_HANDLE_VALUE) Sleep(100);
    }
    if (handle == INVALID_HANDLE_VALUE)
    {
        Log("The server of session %lu does not take connections", session);
        return FALSE;
    }
    if (!GetNamedPipeServerProcessId(handle, &server) || server != GetProcessId(*process))
    {
        Log("The pipe of session %lu belongs to process %lu, not to its server", session, server);
        CloseHandle(handle);
        return FALSE;
    }
    message.Named = named;
    if (WSADuplicateSocketW(client, GetProcessId(*process), &message.Socket) ||
        !WriteFile(handle, &message, sizeof(message), &written, NULL) || written != sizeof(message))
    {
        Log("Cannot hand the connection to session %lu error=%lu", session, GetLastError());
        CloseHandle(handle);
        return FALSE;
    }
    CloseHandle(handle);
    return TRUE;
}

static void Route(SOCKET client)
{
    WCHAR user[257], consoleUser[257];
    AGENT *agent = NULL;
    BOOL handed, routed, ambiguous = FALSE;
    DWORD i;
    if (!ReadCookie(client, user, ARRAYSIZE(user), &routed))
    {
        Log("Dropped a connection without a connection request");
        return;
    }
    EnterCriticalSection(&AgentLock);
    if (*user && SessionUser(ConsoleSession, consoleUser, ARRAYSIZE(consoleUser)) && *consoleUser &&
        !BrokerSameUser(consoleUser, user, routed))
    {
        for (i = 0; i < MAX_AGENTS; ++i)
        {
            if (!Agents[i].Display || !BrokerSameUser(Agents[i].User, user, routed)) continue;
            if (agent) ambiguous = TRUE;
            agent = &Agents[i];
        }
        if (ambiguous) agent = NULL;
        if (!agent && (routed || wcslen(user) < BROKER_COOKIE_CHARS))
        {
            agent = StartNewSession(user);
            if (!agent)
            {
                LeaveCriticalSection(&AgentLock);
                return;
            }
        }
    }
    if (agent)
    {
        handed = HandOff(client, &agent->Process, agent->Session, agent->DisplayInstance, TRUE, routed);
    }
    else if (ConsoleAgent)
    {
        handed = HandOff(client, &ConsoleAgent, ConsoleSession, NULL, FALSE, routed);
    }
    else
    {
        handed = FALSE;
        Log("No server runs in the console session");
    }
    LeaveCriticalSection(&AgentLock);
    if (handed) Log("Connection of %ls handed to session %lu", *user ? user : L"an unnamed user", agent ? agent->Session : ConsoleSession);
    if (!handed) Log("Dropped a connection for %ls", *user ? user : L"an unnamed user");
}

static NTSTATUS StopSession(DWORD session)
{
    HANDLE port = NULL;
    NTSTATUS status = SmConnectToSm(NULL, NULL, 0, &port);
    if (NT_SUCCESS(status)) status = SmStopCsr(port, session);
    if (port) NtClose(port);
    return status;
}

/* Winlogon of a session started for a user leaves when the user signs out, and the session ends with it */
static void EndSignedOutSessions(void)
{
    NTSTATUS status;
    DWORD i;
    EnterCriticalSection(&AgentLock);
    for (i = 0; i < MAX_AGENTS; ++i)
    {
        if (!Agents[i].Winlogon || WaitForSingleObject(Agents[i].Winlogon, 0) != WAIT_OBJECT_0) continue;
        status = StopSession(Agents[i].Session);
        Log("Session %lu of %ls ended after its user signed out: 0x%08lx", Agents[i].Session, Agents[i].User, status);
        CloseAgent(&Agents[i]);
    }
    LeaveCriticalSection(&AgentLock);
}

static SOCKET Listen(void)
{
    struct sockaddr_in6 address = {0};
    DWORD off = 0;
    SOCKET listener = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listener != INVALID_SOCKET)
    {
        setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof(off));
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(3389);
        if (!bind(listener, (struct sockaddr *)&address, sizeof(address)) && !listen(listener, SOMAXCONN))
            return listener;
        closesocket(listener);
    }
    {
        struct sockaddr_in address4 = {0};
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) return INVALID_SOCKET;
        address4.sin_family = AF_INET;
        address4.sin_port = htons(3389);
        if (!bind(listener, (struct sockaddr *)&address4, sizeof(address4)) && !listen(listener, SOMAXCONN))
            return listener;
        closesocket(listener);
    }
    return INVALID_SOCKET;
}

/* The port is open only while Remote Desktop is allowed */
static DWORD WINAPI BrokerMain(void *unused)
{
    SOCKET listener = INVALID_SOCKET, client;
    WSAEVENT accepted = WSACreateEvent();
    HANDLE waits[2];
    DWORD wait;
    (void)unused;
    waits[0] = BrokerStopEvent;
    waits[1] = accepted;
    for (;;)
    {
        if (!Allowed())
        {
            if (listener != INVALID_SOCKET)
            {
                closesocket(listener);
                listener = INVALID_SOCKET;
                Log("Stopped listening on port 3389");
            }
        }
        else if (listener == INVALID_SOCKET && (listener = Listen()) != INVALID_SOCKET)
        {
            if (WSAEventSelect(listener, accepted, FD_ACCEPT))
            {
                closesocket(listener);
                listener = INVALID_SOCKET;
            }
            else
            {
                Log("Listening on port 3389 with NLA");
            }
        }
        EndSignedOutSessions();
        wait = WaitForMultipleObjects(2, waits, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        if (wait != WAIT_OBJECT_0 + 1 || listener == INVALID_SOCKET) continue;
        WSAResetEvent(accepted);
        while ((client = accept(listener, NULL, NULL)) != INVALID_SOCKET)
        {
            if (Allowed()) Route(client);
            closesocket(client);
        }
    }
    if (listener != INVALID_SOCKET) closesocket(listener);
    WSACloseEvent(accepted);
    return 0;
}

BOOL BrokerStart(BROKER_LOG log, BROKER_START_AGENT start, BROKER_ALLOWED allowed)
{
    WSADATA winsock;
    Log = log;
    StartAgent = start;
    Allowed = allowed;
    InitializeCriticalSection(&AgentLock);
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) return FALSE;
    BrokerStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!BrokerStopEvent) return FALSE;
    BrokerThread = CreateThread(NULL, 0, BrokerMain, NULL, 0, NULL);
    return BrokerThread != NULL;
}

void BrokerStop(void)
{
    DWORD i;
    if (!BrokerThread) return;
    SetEvent(BrokerStopEvent);
    WaitForSingleObject(BrokerThread, INFINITE);
    CloseHandle(BrokerThread);
    BrokerThread = NULL;
    EnterCriticalSection(&AgentLock);
    for (i = 0; i < MAX_AGENTS; ++i)
    {
        /* The display of a session goes away with this process, so a session left behind could never be shown again */
        if (Agents[i].Session)
            Log("Session %lu of %ls ended with the service: 0x%08lx", Agents[i].Session, Agents[i].User,
                StopSession(Agents[i].Session));
        CloseAgent(&Agents[i]);
    }
    if (ConsoleAgent) CloseHandle(ConsoleAgent);
    ConsoleAgent = NULL;
    LeaveCriticalSection(&AgentLock);
    WSACleanup();
}

void BrokerSetConsoleAgent(HANDLE process, DWORD session)
{
    if (!BrokerThread) return;
    EnterCriticalSection(&AgentLock);
    if (ConsoleAgent) CloseHandle(ConsoleAgent);
    ConsoleAgent = NULL;
    if (process && !DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &ConsoleAgent, 0, FALSE,
                                    DUPLICATE_SAME_ACCESS))
        ConsoleAgent = NULL;
    ConsoleSession = session;
    LeaveCriticalSection(&AgentLock);
}
