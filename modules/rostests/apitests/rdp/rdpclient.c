/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Remote Desktop clients and accounts shared by the Remote Desktop server tests
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_LEAN_AND_MEAN
#include <apitest.h>
#include <winsock2.h>
#include <stdio.h>
#include <freerdp/freerdp.h>
#include <freerdp/settings.h>
#include <winsvc.h>
#include <lm.h>
#include "rdpclient.h"

#define TERMINAL_SERVER_KEY L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server"

typedef struct _RDP_TEST_CONTEXT
{
    rdpContext Context;
    PRDP_CLIENT Client;
} RDP_TEST_CONTEXT, *PRDP_TEST_CONTEXT;

static BOOL
ServiceRunning(PCWSTR Name)
{
    SERVICE_STATUS Status = { 0 };
    SC_HANDLE Manager, Service;
    BOOL Running = FALSE;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!Manager)
        return FALSE;
    Service = OpenServiceW(Manager, Name, SERVICE_QUERY_STATUS);
    if (Service)
    {
        Running = QueryServiceStatus(Service, &Status) && Status.dwCurrentState == SERVICE_RUNNING;
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);
    return Running;
}

static BOOL
WaitForListener(VOID)
{
    struct sockaddr_in Address = { 0 };
    DWORD Start = GetTickCount();
    SOCKET Socket;
    BOOL Listening;

    Address.sin_family = AF_INET;
    Address.sin_port = htons(3389);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    do
    {
        Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Listening = Socket != INVALID_SOCKET && connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) == 0;
        if (Socket != INVALID_SOCKET)
            closesocket(Socket);
        if (Listening)
            return TRUE;
        Sleep(500);
    } while (GetTickCount() - Start < 60000);
    return FALSE;
}

BOOL
RdpTestEnableDebugPrivilege(VOID)
{
    TOKEN_PRIVILEGES Privileges = { 1 };
    HANDLE Token;
    BOOL Enabled;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
        return FALSE;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Enabled = LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &Privileges.Privileges[0].Luid) &&
              AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Enabled;
}

const char *
RdpTestPassword(VOID)
{
    static char Password[32];

    if (!Password[0])
        _snprintf(Password, sizeof(Password), "Rdp-%08lx!%08lx", GetTickCount(), GetCurrentProcessId() * 2654435761u);
    return Password;
}

BOOL
RdpTestStart(PRDP_TEST_SETUP Setup)
{
    DWORD Size = sizeof(Setup->Original), Type, Value = 0;
    WSADATA Data;

    ZeroMemory(Setup, sizeof(*Setup));
    if (!ServiceRunning(L"LiberNTRDPServer"))
    {
        skip("The LiberNT Remote Desktop server is not running\n");
        return FALSE;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TERMINAL_SERVER_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Setup->Key))
    {
        skip("Changing fDenyTSConnections needs an administrator\n");
        return FALSE;
    }
    if (RegQueryValueExW(Setup->Key, L"fDenyTSConnections", NULL, &Type, (BYTE *)&Setup->Original, &Size) !=
            ERROR_SUCCESS ||
        Type != REG_DWORD)
    {
        Setup->Original = 1;
    }
    RegSetValueExW(Setup->Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Value, sizeof(Value));
    WSAStartup(MAKEWORD(2, 2), &Data);
    Setup->Started = TRUE;
    /* TODO: the clients use FreeRDP's own NTLM; LiberNT's Negotiate client fails CredSSP with SEC_E_INVALID_TOKEN */
    SetEnvironmentVariableA("WINPR_NATIVE_SSPI", "0");
    ok(WaitForListener(), "Port 3389 does not listen with Remote Desktop on\n");
    Sleep(2000);
    return TRUE;
}

VOID
RdpTestEnd(PRDP_TEST_SETUP Setup)
{
    if (!Setup->Started)
        return;
    RegSetValueExW(Setup->Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Setup->Original,
                   sizeof(Setup->Original));
    RegCloseKey(Setup->Key);
    WSACleanup();
}

BOOL
RdpTestAddUser(PCWSTR Name)
{
    WCHAR Password[32];
    LOCALGROUP_MEMBERS_INFO_3 Member;
    USER_INFO_1 User = { 0 };
    SID_IDENTIFIER_AUTHORITY Nt = { SECURITY_NT_AUTHORITY };
    WCHAR Group[GNLEN + 1], Domain[DNLEN + 1];
    DWORD GroupLength = ARRAYSIZE(Group), DomainLength = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    PSID GroupSid;
    NET_API_STATUS Status;
    BOOL Added;

    MultiByteToWideChar(CP_ACP, 0, RdpTestPassword(), -1, Password, ARRAYSIZE(Password));
    User.usri1_name = (LPWSTR)Name;
    User.usri1_password = Password;
    User.usri1_priv = USER_PRIV_USER;
    User.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    Status = NetUserAdd(NULL, 1, (LPBYTE)&User, NULL);
    if (Status == NERR_UserExists)
    {
        NetUserDel(NULL, Name);
        Status = NetUserAdd(NULL, 1, (LPBYTE)&User, NULL);
    }
    ok(Status == NERR_Success, "NetUserAdd(%ls) failed with %lu\n", Name, Status);
    if (Status != NERR_Success)
        return FALSE;
    if (!AllocateAndInitializeSid(&Nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_REMOTE_DESKTOP_USERS,
                                  0, 0, 0, 0, 0, 0, &GroupSid))
        return FALSE;
    Added = LookupAccountSidW(NULL, GroupSid, Group, &GroupLength, Domain, &DomainLength, &Use);
    FreeSid(GroupSid);
    ok(Added, "The Remote Desktop Users group has no name: %lu\n", GetLastError());
    if (!Added)
        return FALSE;
    Member.lgrmi3_domainandname = (LPWSTR)Name;
    Status = NetLocalGroupAddMembers(NULL, Group, 3, (LPBYTE)&Member, 1);
    ok(Status == NERR_Success, "Adding %ls to %ls failed with %lu\n", Name, Group, Status);
    return Status == NERR_Success;
}

static BOOL
ClientBitmapUpdate(rdpContext *Context, const BITMAP_UPDATE *Bitmap)
{
    UNREFERENCED_PARAMETER(Bitmap);
    InterlockedIncrement(&((PRDP_TEST_CONTEXT)Context)->Client->Updates);
    return TRUE;
}

static BOOL
ClientSurfaceBits(rdpContext *Context, const SURFACE_BITS_COMMAND *Command)
{
    UNREFERENCED_PARAMETER(Command);
    InterlockedIncrement(&((PRDP_TEST_CONTEXT)Context)->Client->Updates);
    return TRUE;
}

static BOOL
ClientPostConnect(freerdp *Instance)
{
    Instance->context->update->BitmapUpdate = ClientBitmapUpdate;
    Instance->context->update->SurfaceBits = ClientSurfaceBits;
    return TRUE;
}

static DWORD WINAPI
ClientThread(PVOID Parameter)
{
    PRDP_CLIENT Client = Parameter;
    HANDLE Events[MAXIMUM_WAIT_OBJECTS];
    char Routing[64];
    freerdp *Instance = Client->Instance;
    rdpSettings *Settings = Instance->context->settings;
    DWORD Count;
    BOOL Set;

    Set = freerdp_settings_set_string(Settings, FreeRDP_ServerHostname, "127.0.0.1") &&
          freerdp_settings_set_uint32(Settings, FreeRDP_ServerPort, 3389) &&
          freerdp_settings_set_string(Settings, FreeRDP_Username, Client->User) &&
          freerdp_settings_set_string(Settings, FreeRDP_Password, RdpTestPassword()) &&
          freerdp_settings_set_bool(Settings, FreeRDP_IgnoreCertificate, TRUE) &&
          freerdp_settings_set_bool(Settings, FreeRDP_NlaSecurity, TRUE) &&
          freerdp_settings_set_bool(Settings, FreeRDP_DeactivateClientDecoding, TRUE);
    if (Set && Client->RoutingUser)
    {
        _snprintf(Routing, sizeof(Routing), "Cookie: msts=user=%s\r\n", Client->RoutingUser);
        Routing[sizeof(Routing) - 1] = 0;
        Set = freerdp_settings_set_pointer_len(Settings, FreeRDP_LoadBalanceInfo, Routing, strlen(Routing));
    }
    if (Set && freerdp_connect(Instance))
    {
        InterlockedExchange(&Client->Alive, TRUE);
        SetEvent(Client->Connected);
        while (WaitForSingleObject(Client->Stop, 0) == WAIT_TIMEOUT)
        {
            Count = freerdp_get_event_handles(Instance->context, Events, ARRAYSIZE(Events) - 1);
            if (!Count)
                break;
            Events[Count++] = Client->Stop;
            if (WaitForMultipleObjects(Count, Events, FALSE, 100) == WAIT_FAILED ||
                !freerdp_check_event_handles(Instance->context) || freerdp_shall_disconnect_context(Instance->context))
            {
                Client->Error = freerdp_get_last_error(Instance->context);
                InterlockedExchange(&Client->Disconnected, TRUE);
                break;
            }
        }
        freerdp_disconnect(Instance);
    }
    else
    {
        Client->Error = freerdp_get_last_error(Instance->context);
        SetEvent(Client->Connected);
    }
    InterlockedExchange(&Client->Alive, FALSE);
    return 0;
}

BOOL
RdpClientStart(PRDP_CLIENT Client, const char *User, const char *RoutingUser)
{
    freerdp *Instance;

    ZeroMemory(Client, sizeof(*Client));
    Client->User = User;
    Client->RoutingUser = RoutingUser;
    Client->Stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    Client->Connected = CreateEventW(NULL, TRUE, FALSE, NULL);
    Instance = freerdp_new();
    if (Instance)
    {
        Instance->ContextSize = sizeof(RDP_TEST_CONTEXT);
        Instance->PostConnect = ClientPostConnect;
    }
    if (Instance && !freerdp_context_new(Instance))
    {
        freerdp_free(Instance);
        Instance = NULL;
    }
    if (Instance)
        ((PRDP_TEST_CONTEXT)Instance->context)->Client = Client;
    ok(Instance != NULL, "Cannot make a client for %s\n", User);
    Client->Instance = Instance;
    if (Instance)
    {
        Client->Thread = CreateThread(NULL, 0, ClientThread, Client, 0, NULL);
        ok(Client->Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    return Client->Thread != NULL;
}

BOOL
RdpClientWaitConnected(PRDP_CLIENT Client)
{
    BOOL Finished = WaitForSingleObject(Client->Connected, 120000) == WAIT_OBJECT_0;

    ok(Finished, "%s never finished connecting\n", Client->User);
    return Finished && Client->Alive;
}

VOID
RdpClientStop(PRDP_CLIENT Client)
{
    freerdp *Instance = Client->Instance;

    SetEvent(Client->Stop);
    if (Client->Thread)
    {
        freerdp_abort_connect_context(Instance->context);
        ok(WaitForSingleObject(Client->Thread, 60000) == WAIT_OBJECT_0, "the client of %s did not end\n",
           Client->User);
        CloseHandle(Client->Thread);
    }
    if (Instance)
    {
        freerdp_context_free(Instance);
        freerdp_free(Instance);
    }
    CloseHandle(Client->Stop);
    CloseHandle(Client->Connected);
}
