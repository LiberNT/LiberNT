/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that one user's Remote Desktop connection cannot take over another user's session
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

#define TERMINAL_SERVER_KEY L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server"
#define FIRST_USER  "rdpowner"
#define SECOND_USER "rdpother"
#define PASSWORD    "Rdp-Owner!Test2026"

typedef struct _CLIENT
{
    const char *User;
    const char *RoutingUser;
    HANDLE Stop;
    HANDLE Connected;
    volatile LONG Alive;
    volatile LONG Disconnected;
    UINT32 Error;
} CLIENT, *PCLIENT;

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
AddRemoteUser(PCWSTR Name)
{
    static WCHAR Password[] = L"" PASSWORD;
    LOCALGROUP_MEMBERS_INFO_3 Member;
    USER_INFO_1 User = { 0 };
    SID_IDENTIFIER_AUTHORITY Nt = { SECURITY_NT_AUTHORITY };
    WCHAR Group[GNLEN + 1], Domain[DNLEN + 1];
    DWORD GroupLength = ARRAYSIZE(Group), DomainLength = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    PSID GroupSid;
    NET_API_STATUS Status;
    BOOL Added;

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

static DWORD WINAPI
ClientThread(PVOID Parameter)
{
    PCLIENT Client = Parameter;
    HANDLE Events[MAXIMUM_WAIT_OBJECTS];
    char Routing[64];
    freerdp *Instance;
    rdpSettings *Settings;
    DWORD Count;
    BOOL Set;

    Instance = freerdp_new();
    if (!Instance || !freerdp_context_new(Instance))
    {
        if (Instance)
            freerdp_free(Instance);
        SetEvent(Client->Connected);
        return 1;
    }
    Settings = Instance->context->settings;
    Set = freerdp_settings_set_string(Settings, FreeRDP_ServerHostname, "127.0.0.1") &&
          freerdp_settings_set_uint32(Settings, FreeRDP_ServerPort, 3389) &&
          freerdp_settings_set_string(Settings, FreeRDP_Username, Client->User) &&
          freerdp_settings_set_string(Settings, FreeRDP_Password, PASSWORD) &&
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
    freerdp_context_free(Instance);
    freerdp_free(Instance);
    return 0;
}

static HANDLE
StartClient(PCLIENT Client, const char *User, const char *RoutingUser)
{
    HANDLE Thread;

    ZeroMemory(Client, sizeof(*Client));
    Client->User = User;
    Client->RoutingUser = RoutingUser;
    Client->Stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    Client->Connected = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, ClientThread, Client, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread)
        ok(WaitForSingleObject(Client->Connected, 120000) == WAIT_OBJECT_0, "%s never finished connecting\n", User);
    return Thread;
}

static VOID
StopClient(PCLIENT Client, HANDLE Thread)
{
    SetEvent(Client->Stop);
    if (Thread)
    {
        ok(WaitForSingleObject(Thread, 60000) == WAIT_OBJECT_0, "the client of %s did not end\n", Client->User);
        CloseHandle(Thread);
    }
    CloseHandle(Client->Stop);
    CloseHandle(Client->Connected);
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

START_TEST(OtherUser)
{
    DWORD Original = 1, Size = sizeof(Original), Type, Value = 0, Wait;
    CLIENT Owner, Other;
    HANDLE OwnerThread, OtherThread;
    WSADATA Data;
    HKEY Key;

    if (!ServiceRunning(L"LiberNTRDPServer"))
    {
        skip("The LiberNT Remote Desktop server is not running\n");
        return;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TERMINAL_SERVER_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key))
    {
        skip("Changing fDenyTSConnections needs an administrator\n");
        return;
    }
    if (RegQueryValueExW(Key, L"fDenyTSConnections", NULL, &Type, (BYTE *)&Original, &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD)
    {
        Original = 1;
    }
    RegSetValueExW(Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Value, sizeof(Value));
    WSAStartup(MAKEWORD(2, 2), &Data);
    /* TODO: the clients use FreeRDP's own NTLM; LiberNT's Negotiate client fails CredSSP with SEC_E_INVALID_TOKEN */
    SetEnvironmentVariableA("WINPR_NATIVE_SSPI", "0");

    ok(WaitForListener(), "Port 3389 does not listen with Remote Desktop on\n");
    Sleep(2000);
    if (AddRemoteUser(L"" FIRST_USER) && AddRemoteUser(L"" SECOND_USER))
    {
        OwnerThread = StartClient(&Owner, FIRST_USER, NULL);
        ok(Owner.Alive, "%s could not connect: 0x%08x\n", FIRST_USER, Owner.Error);
        if (Owner.Alive)
        {
            Sleep(3000);
            OtherThread = StartClient(&Other, SECOND_USER, FIRST_USER);
            for (Wait = 0; Wait < 50 && !Owner.Disconnected; Wait++)
                Sleep(100);
            ok(!Owner.Disconnected, "%s was disconnected by %s: 0x%08x\n", FIRST_USER, SECOND_USER, Owner.Error);
            ok(Owner.Alive, "%s is no longer connected\n", FIRST_USER);
            StopClient(&Other, OtherThread);
        }
        StopClient(&Owner, OwnerThread);
    }

    NetUserDel(NULL, L"" FIRST_USER);
    NetUserDel(NULL, L"" SECOND_USER);
    RegSetValueExW(Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Original, sizeof(Original));
    RegCloseKey(Key);
    WSACleanup();
}
