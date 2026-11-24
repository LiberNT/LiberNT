/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Accept RDP connections in session 0 and give each signed-in user the agent of its session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_NO_STATUS
#include "broker.h"
#include "agent.h"
#include <remoteconsole.h>
#include <ws2tcpip.h>
#include <wtsapi32.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <assert.h>
#include <bcrypt.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#define NTOS_MODE_USER
#include <ndk/ntndk.h>
#include <sm/smmsg.h>

#define MAX_AGENTS 16

struct AGENT
{
    LONG References;
    CRITICAL_SECTION Lock;
    DWORD Session;
    BOOL Console;
    HANDLE Process;
    HANDLE Pipe, PipeEvent;
    WCHAR PipeKey[AGENT_PIPE_KEY_CHARS + 1];
    volatile LONG Broken;
    volatile LONG Owner;
    LONG Connections;
    HSWDEVICE Display;
    WCHAR DisplayInstance[MAX_DEVICE_ID_LEN];
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    WCHAR User[257];
    HANDLE Winlogon;
};

static BROKER_LOG Log;
static BROKER_START_AGENT StartAgent;
static BROKER_ALLOWED Allowed;
static BROKER_ACCEPTED Accepted;
static HANDLE BrokerThread, BrokerStopEvent;
static CRITICAL_SECTION TableLock;
static AGENT *Console;
static DWORD ConsoleSession;
static AGENT *Agents[MAX_AGENTS];
static LONG NextDisplay, NextOwner;

static AGENT *NewAgent(DWORD session, BOOL console)
{
    AGENT *agent = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*agent));
    if (!agent) return NULL;
    agent->References = 1;
    agent->Session = session;
    agent->Console = console;
    agent->PipeEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!agent->PipeEvent)
    {
        HeapFree(GetProcessHeap(), 0, agent);
        return NULL;
    }
    InitializeCriticalSection(&agent->Lock);
    return agent;
}

static BOOL ProcessAlive(HANDLE process)
{
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

static void StopAgentProcess(AGENT *agent)
{
    if (agent->Pipe) CloseHandle(agent->Pipe);
    agent->Pipe = NULL;
    if (agent->Process)
    {
        if (WaitForSingleObject(agent->Process, 10000) == WAIT_TIMEOUT) TerminateProcess(agent->Process, 1);
        CloseHandle(agent->Process);
    }
    agent->Process = NULL;
    InterlockedExchange(&agent->Owner, 0);
    InterlockedExchange(&agent->Broken, TRUE);
}

static void ReleaseAgent(AGENT *agent)
{
    if (InterlockedDecrement(&agent->References)) return;
    assert(!agent->Connections);
    StopAgentProcess(agent);
    if (agent->Display) SwDeviceClose(agent->Display);
    if (agent->Winlogon) CloseHandle(agent->Winlogon);
    CloseHandle(agent->PipeEvent);
    DeleteCriticalSection(&agent->Lock);
    HeapFree(GetProcessHeap(), 0, agent);
}

/* winpr links its own WTS functions; the user of a session comes from the system's wtsapi32 */
static BOOL SessionUserSid(DWORD session, BYTE *sid)
{
    typedef BOOL (WINAPI *QUERY_TOKEN)(ULONG, PHANDLE);
    static QUERY_TOKEN Query;
    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    HMODULE wtsapi;
    HANDLE token;
    DWORD length;
    BOOL found;
    if (!Query && (wtsapi = LoadLibraryW(L"wtsapi32.dll")))
        Query = (QUERY_TOKEN)(void *)GetProcAddress(wtsapi, "WTSQueryUserToken");
    if (!Query || !Query(session, &token)) return FALSE;
    found = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &length) &&
            CopySid(SECURITY_MAX_SID_SIZE, sid, ((TOKEN_USER *)buffer)->User.Sid);
    CloseHandle(token);
    return found;
}

static NTSTATUS StopSession(DWORD session)
{
    HANDLE port = NULL;
    NTSTATUS status = SmConnectToSm(NULL, NULL, 0, &port);
    if (NT_SUCCESS(status)) status = SmStopCsr(port, session);
    if (port) NtClose(port);
    return status;
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

/* A new session's win32k needs its display before csrss initializes, so the adapter is made first */
static BOOL WaitForWinlogon(AGENT *agent)
{
    WCHAR pipe[64];
    DWORD wait;
    _snwprintf(pipe, ARRAYSIZE(pipe), REMOTE_CONSOLE_PIPE_FORMAT, agent->Session);
    pipe[ARRAYSIZE(pipe) - 1] = 0;
    for (wait = 0; wait < WINLOGON_START_TIMEOUT / 100; ++wait)
    {
        if (WaitNamedPipeW(pipe, 1) || GetLastError() != ERROR_FILE_NOT_FOUND) return TRUE;
        if (WaitForSingleObject(agent->Winlogon, 100) == WAIT_OBJECT_0) return FALSE;
    }
    return FALSE;
}

static BOOL CreateDisplay(AGENT *agent)
{
    static const WCHAR hardwareIds[] = L"RdpVdd_RemoteDisplay\0";
    SW_DEVICE_CREATE_INFO info = {sizeof(info)};
    WCHAR name[64];
    DISPLAY_CREATION *creation;
    DWORD wait;
    BOOL created;
    assert(!agent->Console && !agent->Display);
    /* dxgkrnl gives a NewSession adapter only to a session without a desktop yet */
    if (agent->Session)
        _snwprintf(name, ARRAYSIZE(name), L"RdpVdd_RemoteDisplay&SessionId_%04lu", agent->Session);
    else
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
        Log("Cannot create a session display: out of memory");
        return FALSE;
    }
    creation->References = 2;
    creation->Result = E_FAIL;
    if (FAILED(SwDeviceCreate(L"RemoteDisplayEnum", L"HTREE\\ROOT\\0", &info, 0, NULL, DisplayCreated, creation,
                              &agent->Display)))
    {
        agent->Display = NULL;
        ReleaseCreation(creation);
        ReleaseCreation(creation);
        Log("Cannot create a session display");
        return FALSE;
    }
    created = WaitForSingleObject(creation->Ready, 15000) == WAIT_OBJECT_0 && SUCCEEDED(creation->Result);
    if (created) lstrcpynW(agent->DisplayInstance, creation->Instance, ARRAYSIZE(agent->DisplayInstance));
    else Log("Cannot create a session display: 0x%08lx", creation->Result);
    ReleaseCreation(creation);
    for (wait = 0; created && wait < 150 && !DisplayStarted(agent->DisplayInstance); ++wait) Sleep(100);
    if (created && DisplayStarted(agent->DisplayInstance)) return TRUE;
    if (created) Log("The session display %ls did not start", agent->DisplayInstance);
    SwDeviceClose(agent->Display);
    agent->Display = NULL;
    agent->DisplayInstance[0] = 0;
    return FALSE;
}

/* The sessions the service started are kept in a volatile key, so a restarted service can give them back */
static void RememberSession(AGENT *agent, BOOL keep)
{
    WCHAR name[16];
    HKEY key;
    LSTATUS error;
    _snwprintf(name, ARRAYSIZE(name), L"%lu", agent->Session);
    name[ARRAYSIZE(name) - 1] = 0;
    error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, BROKER_SESSIONS_KEY, 0, NULL, REG_OPTION_VOLATILE, KEY_SET_VALUE, NULL,
                            &key, NULL);
    if (error == ERROR_SUCCESS)
    {
        error = keep ? RegSetValueExW(key, name, 0, REG_BINARY, agent->Sid, GetLengthSid(agent->Sid))
                     : RegDeleteValueW(key, name);
        RegCloseKey(key);
    }
    if (error != ERROR_SUCCESS && (keep || error != ERROR_FILE_NOT_FOUND))
        Log("Cannot %s session %lu in the session list: %ld", keep ? "add" : "remove", agent->Session, error);
}

static AGENT *StartNewSession(const BYTE *sid, const WCHAR *user)
{
    HANDLE port = NULL, csrss = NULL, initial = NULL;
    AGENT *agent;
    AGENT **slot = NULL;
    NTSTATUS status;
    ULONG session = 0;
    DWORD i;
    for (i = 0; i < MAX_AGENTS && !slot; ++i)
        if (!Agents[i]) slot = &Agents[i];
    if (!slot)
    {
        Log("No room for another session");
        return NULL;
    }
    agent = NewAgent(0, FALSE);
    if (!agent)
    {
        Log("Cannot start a new session: out of memory");
        return NULL;
    }
    if (!CreateDisplay(agent)) goto failed;
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
    if (!agent->Winlogon)
    {
        Log("Cannot watch Winlogon of session %lu error=%lu: 0x%08lx", session, GetLastError(), StopSession(session));
        goto failed;
    }
    if (!WaitForWinlogon(agent))
    {
        Log("Winlogon of session %lu did not start: 0x%08lx", session, StopSession(session));
        goto failed;
    }
    CopySid(sizeof(agent->Sid), agent->Sid, (PSID)sid);
    lstrcpynW(agent->User, user, ARRAYSIZE(agent->User));
    *slot = agent;
    RememberSession(agent, TRUE);
    Log("Session %lu started for %ls on %ls", session, user, agent->DisplayInstance);
    return agent;
failed:
    ReleaseAgent(agent);
    return NULL;
}

static BOOL SessionBelongsTo(AGENT *agent, const BYTE *sid)
{
    BYTE user[SECURITY_MAX_SID_SIZE];
    if (SessionUserSid(agent->Session, user)) return EqualSid(user, (PSID)sid);
    return EqualSid(agent->Sid, (PSID)sid);
}

/* A user with a session of its own gets it back; the console's user, or anyone while nobody is signed in there, gets the console session */
static AGENT *FindSession(const BYTE *sid, const WCHAR *user)
{
    BYTE consoleUser[SECURITY_MAX_SID_SIZE];
    DWORD i;
    for (i = 0; i < MAX_AGENTS; ++i)
        if (Agents[i] && SessionBelongsTo(Agents[i], sid)) return Agents[i];
    if (ConsoleSession != 0 &&
        (!SessionUserSid(ConsoleSession, consoleUser) || EqualSid(consoleUser, (PSID)sid)))
    {
        if (!Console) Console = NewAgent(ConsoleSession, TRUE);
        return Console;
    }
    return StartNewSession(sid, user);
}

/* Any user can make a pipe, so the name of each agent's pipe cannot be guessed */
static BOOL NewPipeKey(AGENT *agent)
{
    BYTE key[AGENT_PIPE_KEY_CHARS / 2];
    DWORD i;
    if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, key, sizeof(key), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return FALSE;
    for (i = 0; i < sizeof(key); ++i)
        _snwprintf(agent->PipeKey + i * 2, 3, L"%02x", key[i]);
    agent->PipeKey[AGENT_PIPE_KEY_CHARS] = 0;
    return TRUE;
}

static BOOL RunAgent(AGENT *agent)
{
    WCHAR pipe[128];
    DWORD tries, server = 0, mode = PIPE_READMODE_MESSAGE;
    if (agent->Pipe && !agent->Broken && ProcessAlive(agent->Process)) return TRUE;
    StopAgentProcess(agent);
    if (!agent->Console && !agent->Display && !CreateDisplay(agent)) return FALSE;
    if (!NewPipeKey(agent)) return FALSE;
    agent->Process = StartAgent(agent->Session, agent->PipeKey, agent->Console ? NULL : agent->DisplayInstance);
    if (!agent->Process) return FALSE;
    _snwprintf(pipe, ARRAYSIZE(pipe), AGENT_PIPE_FORMAT, agent->Session, agent->PipeKey);
    pipe[ARRAYSIZE(pipe) - 1] = 0;
    for (tries = 0; tries < 300 && ProcessAlive(agent->Process); ++tries)
    {
        agent->Pipe = CreateFileW(pipe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
        if (agent->Pipe != INVALID_HANDLE_VALUE) break;
        agent->Pipe = NULL;
        Sleep(100);
    }
    if (!agent->Pipe)
    {
        Log("The agent of session %lu does not take connections", agent->Session);
        StopAgentProcess(agent);
        return FALSE;
    }
    if (!GetNamedPipeServerProcessId(agent->Pipe, &server) || server != GetProcessId(agent->Process) ||
        !SetNamedPipeHandleState(agent->Pipe, &mode, NULL, NULL))
    {
        Log("The pipe of session %lu belongs to process %lu, not to its agent", agent->Session, server);
        StopAgentProcess(agent);
        return FALSE;
    }
    InterlockedExchange(&agent->Broken, FALSE);
    return TRUE;
}

AGENT *BrokerConnect(const BYTE *sid, const WCHAR *user)
{
    AGENT *agent;
    BOOL running;
    EnterCriticalSection(&TableLock);
    agent = FindSession(sid, user);
    if (agent) InterlockedIncrement(&agent->References);
    LeaveCriticalSection(&TableLock);
    if (!agent) return NULL;
    EnterCriticalSection(&agent->Lock);
    ++agent->Connections;
    running = RunAgent(agent);
    LeaveCriticalSection(&agent->Lock);
    if (running)
    {
        Log("Connection of %ls goes to session %lu", user, agent->Session);
        return agent;
    }
    BrokerDisconnect(agent);
    return NULL;
}

void BrokerDisconnect(AGENT *agent)
{
    EnterCriticalSection(&agent->Lock);
    assert(agent->Connections > 0);
    if (!--agent->Connections) StopAgentProcess(agent);
    LeaveCriticalSection(&agent->Lock);
    ReleaseAgent(agent);
}

BOOL AgentEnter(AGENT *agent, LONG owner)
{
    EnterCriticalSection(&agent->Lock);
    if (agent->Pipe && !agent->Broken && (!owner || agent->Owner == owner)) return TRUE;
    LeaveCriticalSection(&agent->Lock);
    return FALSE;
}

void AgentLeave(AGENT *agent)
{
    LeaveCriticalSection(&agent->Lock);
}

LONG AgentTakeOver(AGENT *agent)
{
    LONG owner = InterlockedIncrement(&NextOwner);
    InterlockedExchange(&agent->Owner, owner);
    return owner;
}

BOOL AgentOwnedBy(AGENT *agent, LONG owner)
{
    return owner && InterlockedCompareExchange(&agent->Owner, 0, 0) == owner;
}

BOOL AgentBroken(AGENT *agent)
{
    return InterlockedCompareExchange(&agent->Broken, 0, 0) != 0;
}

DWORD AgentSession(AGENT *agent)
{
    return agent->Session;
}

void AgentAbandon(AGENT *agent)
{
    InterlockedExchange(&agent->Broken, TRUE);
}

/* An agent that does not answer in time is given up, so no connection or service stop waits on it forever */
static BOOL Transfer(AGENT *agent, BOOL write, void *buffer, DWORD size, DWORD *done, DWORD timeout)
{
    OVERLAPPED overlapped = {0};
    BOOL finished;
    *done = 0;
    overlapped.hEvent = agent->PipeEvent;
    ResetEvent(agent->PipeEvent);
    finished = write ? WriteFile(agent->Pipe, buffer, size, done, &overlapped)
                     : ReadFile(agent->Pipe, buffer, size, done, &overlapped);
    if (!finished && GetLastError() == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(agent->PipeEvent, timeout) != WAIT_OBJECT_0)
        {
            Log("The agent of session %lu did not answer in %lu ms", agent->Session, timeout);
            CancelIo(agent->Pipe);
        }
        finished = GetOverlappedResult(agent->Pipe, &overlapped, done, TRUE);
    }
    if (!finished) AgentAbandon(agent);
    return finished;
}

BOOL AgentWrite(AGENT *agent, const void *message, DWORD size)
{
    DWORD written;
    if (Transfer(agent, TRUE, (void *)message, size, &written, AGENT_TIMEOUT) && written == size) return TRUE;
    AgentAbandon(agent);
    return FALSE;
}

BOOL AgentRead(AGENT *agent, void *buffer, DWORD capacity, DWORD *size, DWORD timeout)
{
    if (Transfer(agent, FALSE, buffer, capacity, size, timeout) && *size >= sizeof(ULONG)) return TRUE;
    AgentAbandon(agent);
    return FALSE;
}

HANDLE AgentDuplicate(AGENT *agent, ULONG64 handle, DWORD access)
{
    HANDLE duplicate = NULL;
    if (!DuplicateHandle(agent->Process, (HANDLE)(ULONG_PTR)handle, GetCurrentProcess(), &duplicate, access, FALSE, 0))
        return NULL;
    return duplicate;
}

/* Winlogon of a session started for a user leaves when the user signs out, and the session ends with it */
static void EndSignedOutSessions(void)
{
    NTSTATUS status;
    DWORD i;
    EnterCriticalSection(&TableLock);
    for (i = 0; i < MAX_AGENTS; ++i)
    {
        AGENT *agent = Agents[i];
        if (!agent || !agent->Winlogon || WaitForSingleObject(agent->Winlogon, 0) != WAIT_OBJECT_0) continue;
        status = StopSession(agent->Session);
        Log("Session %lu of %ls ended after its user signed out: 0x%08lx", agent->Session, agent->User, status);
        RememberSession(agent, FALSE);
        Agents[i] = NULL;
        ReleaseAgent(agent);
    }
    LeaveCriticalSection(&TableLock);
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
            if (!Allowed() || !Accepted(client)) closesocket(client);
        }
    }
    if (listener != INVALID_SOCKET) closesocket(listener);
    WSACloseEvent(accepted);
    return 0;
}

static HANDLE OpenWinlogon(DWORD session)
{
    PROCESSENTRY32W entry = {sizeof(entry)};
    HANDLE snapshot, winlogon = NULL;
    DWORD processSession;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return NULL;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (!_wcsicmp(entry.szExeFile, L"winlogon.exe") && ProcessIdToSessionId(entry.th32ProcessID, &processSession) &&
                processSession == session)
                winlogon = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
        } while (!winlogon && Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return winlogon;
}

/* A session outlives a service that ended abnormally; it waits, disconnected, for its user as on Windows */
static BOOL AdoptSession(DWORD session, const BYTE *sid)
{
    WCHAR name[128], domain[128];
    DWORD nameLength = ARRAYSIZE(name), domainLength = ARRAYSIZE(domain), i;
    SID_NAME_USE use;
    AGENT *agent;
    HANDLE winlogon;
    for (i = 0; i < MAX_AGENTS && Agents[i]; ++i)
        ;
    if (i == MAX_AGENTS) return FALSE;
    winlogon = OpenWinlogon(session);
    if (!winlogon) return FALSE;
    agent = NewAgent(session, FALSE);
    if (!agent)
    {
        CloseHandle(winlogon);
        return FALSE;
    }
    agent->Winlogon = winlogon;
    CopySid(sizeof(agent->Sid), agent->Sid, (PSID)sid);
    if (LookupAccountSidW(NULL, (PSID)sid, name, &nameLength, domain, &domainLength, &use))
        _snwprintf(agent->User, ARRAYSIZE(agent->User), L"%ls\\%ls", domain, name);
    agent->User[ARRAYSIZE(agent->User) - 1] = 0;
    Agents[i] = agent;
    Log("Session %lu of %ls waits for its user after the service restarted", session, agent->User);
    return TRUE;
}

static void AdoptSessions(void)
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    WCHAR name[16];
    DWORD index = 0, nameLength, sidLength, type;
    ULONG session;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BROKER_SESSIONS_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    for (;;)
    {
        nameLength = ARRAYSIZE(name);
        sidLength = sizeof(sid);
        if (RegEnumValueW(key, index, name, &nameLength, NULL, &type, sid, &sidLength) != ERROR_SUCCESS) break;
        session = wcstoul(name, NULL, 10);
        if (type == REG_BINARY && session != 0 && IsValidSid(sid) && GetLengthSid(sid) == sidLength &&
            AdoptSession(session, sid))
        {
            ++index;
            continue;
        }
        Log("Session %ls is gone", name);
        RegDeleteValueW(key, name);
    }
    RegCloseKey(key);
}

BOOL BrokerStart(BROKER_LOG log, BROKER_START_AGENT start, BROKER_ALLOWED allowed, BROKER_ACCEPTED accepted)
{
    Log = log;
    StartAgent = start;
    Allowed = allowed;
    Accepted = accepted;
    InitializeCriticalSection(&TableLock);
    AdoptSessions();
    BrokerStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!BrokerStopEvent) return FALSE;
    BrokerThread = CreateThread(NULL, 0, BrokerMain, NULL, 0, NULL);
    return BrokerThread != NULL;
}

void BrokerClose(void)
{
    if (!BrokerThread) return;
    SetEvent(BrokerStopEvent);
    WaitForSingleObject(BrokerThread, INFINITE);
    CloseHandle(BrokerThread);
    BrokerThread = NULL;
}

void BrokerStop(void)
{
    DWORD i;
    BrokerClose();
    EnterCriticalSection(&TableLock);
    for (i = 0; i < MAX_AGENTS; ++i)
    {
        AGENT *agent = Agents[i];
        if (!agent) continue;
        /* The display of a session goes away with this process, so a session left behind could never be shown again */
        Log("Session %lu of %ls ended with the service: 0x%08lx", agent->Session, agent->User,
            StopSession(agent->Session));
        RememberSession(agent, FALSE);
        Agents[i] = NULL;
        ReleaseAgent(agent);
    }
    if (Console) ReleaseAgent(Console);
    Console = NULL;
    LeaveCriticalSection(&TableLock);
}

void BrokerSetConsoleSession(DWORD session)
{
    AGENT *old = NULL;
    /* The console has no session while its session is shown remotely */
    if (session == 0xFFFFFFFF) return;
    EnterCriticalSection(&TableLock);
    if (session != ConsoleSession)
    {
        old = Console;
        Console = NULL;
        ConsoleSession = session;
    }
    LeaveCriticalSection(&TableLock);
    if (!old) return;
    EnterCriticalSection(&old->Lock);
    StopAgentProcess(old);
    LeaveCriticalSection(&old->Lock);
    ReleaseAgent(old);
}
