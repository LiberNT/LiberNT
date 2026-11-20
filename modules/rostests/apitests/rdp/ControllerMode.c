/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the RDP console server configured for a test controller
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_LEAN_AND_MEAN
#include <apitest.h>
#include <winsock2.h>
#include <stdio.h>
#include <freerdp/freerdp.h>
#include <freerdp/event.h>
#include <freerdp/input.h>

#define PROTOCOL_RDP    0x00
#define PROTOCOL_SSL    0x01
#define PROTOCOL_HYBRID 0x02

#define TYPE_RDP_NEG_REQ     0x01
#define TYPE_RDP_NEG_RSP     0x02
#define TYPE_RDP_NEG_FAILURE 0x03

#define SSL_REQUIRED_BY_SERVER 0x01

static const char TestCertificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIC/DCCAeSgAwIBAgIUKq2CqZVdIAbIHLfm8p9ScFF0g6QwDQYJKoZIhvcNAQEL\n"
    "BQAwHjEcMBoGA1UEAwwTcmRwLWNvbnRyb2xsZXItdGVzdDAeFw0yNjExMTcxMzI0\n"
    "MzBaFw0zNjExMTQxMzI0MzBaMB4xHDAaBgNVBAMME3JkcC1jb250cm9sbGVyLXRl\n"
    "c3QwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC/2hXVnVCUXqKa8w7n\n"
    "E+p4aXEufDvWoVgPnPBLW56FgwuWTC1oLZbtzI6GFCmaftVHhLQWRGGHAuhip71z\n"
    "NmO9ewTNNSMHDJONwvz8XKBjhrSU0a+s6L/Of+hWRLtW5Qd+CcO86Kd5sTTU4+m0\n"
    "7IKpSFq6xJkFlkBa3ceftefj14W4ahj/xmU1M+l8ElQt8uWr/wNkHQBDpC6vRL30\n"
    "tZZ4V++uDg4kAPb6oytckq4fYSo9qMhHQw3tm32qfZOgHzHvyZDsn5fWnGlKalUt\n"
    "xGrZsbW1vZXgCP8yJnXtQ6bIwlTvfjtS2zJA8U+akhv8w66uA4kUERObultDwlfE\n"
    "8WgDAgMBAAGjMjAwMB0GA1UdDgQWBBRWfddBOwmJ8TCmi9N/um5nOykdqDAPBgNV\n"
    "HRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQBmlpLAbDmOm1K+pyDCDcIF\n"
    "pa67w+kRqBIySM7FfhBHUw58mr42rUsEbIjJ+689HABYWmj5UB/yMXUr1XWzERpe\n"
    "sPQ6Awq5MVJATwpn+1uhU5me5NL8Khb4HqhwyWbotj1xVKRaYwA0rqmoeX8oB87o\n"
    "FWjsZBOcLNfwFfUBJ7JFz0eSyye1KRr8KSWGrdStb03fP6MWRxGLGCPqp2j54/CI\n"
    "CEQe/GPZM/FZZomVVOrclngFe0ax/BjXZ6YmjJ5UcrGCUKNGFTIehHQ6/pguOajg\n"
    "l1fsREy3BcCNR53Qo8MGwNEcB+nafPxI+65tPHOIqADSLtb8avmIv+sQz+PAI+xQ\n"
    "-----END CERTIFICATE-----\n";

static const char TestPrivateKey[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQC/2hXVnVCUXqKa\n"
    "8w7nE+p4aXEufDvWoVgPnPBLW56FgwuWTC1oLZbtzI6GFCmaftVHhLQWRGGHAuhi\n"
    "p71zNmO9ewTNNSMHDJONwvz8XKBjhrSU0a+s6L/Of+hWRLtW5Qd+CcO86Kd5sTTU\n"
    "4+m07IKpSFq6xJkFlkBa3ceftefj14W4ahj/xmU1M+l8ElQt8uWr/wNkHQBDpC6v\n"
    "RL30tZZ4V++uDg4kAPb6oytckq4fYSo9qMhHQw3tm32qfZOgHzHvyZDsn5fWnGlK\n"
    "alUtxGrZsbW1vZXgCP8yJnXtQ6bIwlTvfjtS2zJA8U+akhv8w66uA4kUERObultD\n"
    "wlfE8WgDAgMBAAECggEASOUPKMQGKumuNNunTOuHSkseAwCXJ9zT/dKuhqOAHtCE\n"
    "5Y16LzwrPZ1chdtBV39bgDoOxusid08Ghv2fARioWwVT9Lh835/KrLFquGRlWIuF\n"
    "L4pgin1A67Az0ht5ZvHMn/oL0ko/4AARx9X1JZY7qyBfwNkUH0hQmXXs9GUEf8/p\n"
    "pfkIPB+vobbPwdcRh6Yv6wrP2efZKdOtmhmh4952aNNqprdkSyp83e86Oqs2o119\n"
    "QOZxkxrxjCZlvCqDmdnCI2XnpvcWBpp5YHJBNS/dGQwFyDFjvqLB2siybwhZ0MwJ\n"
    "DUx5QBSWlgRZYDNy0YcB2acnEIHUeENMfarwCekzAQKBgQD1jG78/r3NvFuuYGd5\n"
    "9cSzHQ4rUBqWjo+4Pi1LBOxgS7mwW4BPXm0nZf4K/K0x04jiO5E1D4bpbLwonz4e\n"
    "ZGqXhdwqRTtftMOh/pyiBJg9ovx/MubsnHw0MMljcu4zzuEBZJmOE2H0Z1/ZgCtv\n"
    "SyZS/HPU/HsZeCw5EZtYzTNlgwKBgQDIBI7CQxklaemZtvraZapM58y9QjlsnsB5\n"
    "oY1uNnESfKpiS8x/odgIiAzYzSUetxzLvb/AmfY4zo3T8Xi5UZGg2Q+3rXBxcKnz\n"
    "N1QAFjwZN/idcxt12YpXVNua3VQ3jVPix7/q5Ur4t1WH8d20V8ia8av9FrPqLqkn\n"
    "YLLwpfTrgQKBgGjc9HkvATfffPfXS9mVz87+VZLBvtkVQONaClXDYH8vV7mtGfrL\n"
    "RKpSsE9Y4U4qHIchET4rx13gheqDNzAbijnzfLbAoj6ZIAMKjTHT7IBGHrbjsiLS\n"
    "IWjYfOjvqDKP4x+JKtq2BFsv2os0ZVg+K7u8/Gq0+I1D+P7WOhWObKipAoGBAIZN\n"
    "cSIFdcwGasE4+QVOQGR1PKdT4UUK74h8Ln/zdEcKiEmKRdzpaiqIV7/3N1aBc4dm\n"
    "eGu1tXt9TgQi3oHowFT1irU1cwvor2TNIM0SIPZ5+6iVEro2AMsVAoPg21yVu97J\n"
    "axcpMQcB6x9mCwy96MgJQAenh9TV0eTL7Drj5CmBAoGBAI2HwAlKyt3PFlUQhIdW\n"
    "hXbVjuRYEGuwRSzQHRaKeIlyvFAn//7iHexnQ7JupMb6I/uShVe6yWq2JGV3gNBf\n"
    "BxomDN3zs1vccnveP0XDxC9Mx/27/yoxSuqceP8jZUjIyot6tmsCNjTrgCwlJWLD\n"
    "/5/kTwY/BErq5CRK+Ndiotdm\n"
    "-----END PRIVATE KEY-----\n";

static char Directory[MAX_PATH];

static BOOL
WriteTextFile(const char *Name, const char *Text)
{
    char Path[MAX_PATH];
    FILE *File;
    BOOL Written;

    if (_snprintf(Path, sizeof(Path), "%s\\%s", Directory, Name) < 0)
        return FALSE;
    File = fopen(Path, "wb");
    if (!File)
        return FALSE;
    Written = fputs(Text, File) >= 0;
    return fclose(File) == 0 && Written;
}

static SOCKET
ConnectLoopback(void)
{
    SOCKADDR_IN Address = { 0 };
    SOCKET Socket;

    Address.sin_family = AF_INET;
    Address.sin_port = htons(3389);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;
    if (connect(Socket, (SOCKADDR *)&Address, sizeof(Address)) == SOCKET_ERROR)
    {
        closesocket(Socket);
        return INVALID_SOCKET;
    }
    return Socket;
}

static BOOL
NegotiateOn(BYTE Requested, BYTE *Type, DWORD *Value, SOCKET *Kept)
{
    BYTE Request[19] = { 3, 0, 0, 19, 14, 0xE0, 0, 0, 0, 0, 0, TYPE_RDP_NEG_REQ, 0, 8, 0, 0, 0, 0, 0 };
    BYTE Response[64];
    DWORD Timeout = 5000;
    int Received = 0, Result;
    SOCKET Socket;

    Request[15] = Requested;
    Socket = ConnectLoopback();
    if (Socket == INVALID_SOCKET)
        return FALSE;
    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&Timeout, sizeof(Timeout));
    if (send(Socket, (char *)Request, sizeof(Request), 0) != sizeof(Request))
    {
        closesocket(Socket);
        return FALSE;
    }
    while (Received < 4 || Received < ((Response[2] << 8) | Response[3]))
    {
        Result = recv(Socket, (char *)Response + Received, sizeof(Response) - Received, 0);
        if (Result <= 0)
            break;
        Received += Result;
    }
    if (Received != 19 || Response[0] != 3 || Response[5] != 0xD0)
    {
        closesocket(Socket);
        return FALSE;
    }
    *Type = Response[11];
    *Value = Response[15] | (Response[16] << 8) | (Response[17] << 16) | ((DWORD)Response[18] << 24);
    if (Kept)
        *Kept = Socket;
    else
        closesocket(Socket);
    return TRUE;
}

static BOOL
Negotiate(BYTE Requested, BYTE *Type, DWORD *Value)
{
    return NegotiateOn(Requested, Type, Value, NULL);
}

static BOOL
NegotiateWhenFree(BYTE Requested, BYTE *Type, DWORD *Value, SOCKET *Kept)
{
    DWORD Start = GetTickCount();

    do
    {
        if (NegotiateOn(Requested, Type, Value, Kept))
            return TRUE;
        Sleep(100);
    } while (GetTickCount() - Start < 10000);

    return FALSE;
}

static BOOL
WaitForNegotiation(BOOL Expected)
{
    DWORD Start = GetTickCount(), Value;
    BYTE Type;

    do
    {
        if (Negotiate(PROTOCOL_SSL, &Type, &Value) == Expected)
            return TRUE;
        Sleep(200);
    } while (GetTickCount() - Start < 20000);

    return FALSE;
}

static HANDLE
StartServer(const char *Server, const char *Allow, const char *Password)
{
    char Ini[1024], Command[2 * MAX_PATH + 8];
    STARTUPINFOA Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;

    _snprintf(Ini, sizeof(Ini),
              "[server]\r\nenabled=1\r\nbind=127.0.0.1\r\nallow=%s\r\nusername=controller\r\n"
              "password=%s\r\ncertificate=test.crt\r\nprivatekey=test.key\r\n", Allow, Password);
    Ini[sizeof(Ini) - 1] = 0;
    if (!WriteTextFile("rdpserver.ini", Ini))
        return NULL;
    _snprintf(Command, sizeof(Command), "\"%s\" \"%s\\rdpserver.ini\"", Server, Directory);
    Command[sizeof(Command) - 1] = 0;
    if (!CreateProcessA(Server, Command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process))
        return NULL;
    CloseHandle(Process.hThread);
    return Process.hProcess;
}

static void
StopServer(HANDLE Process)
{
    TerminateProcess(Process, 0);
    WaitForSingleObject(Process, 10000);
    CloseHandle(Process);
}

static BOOL EarlyInputSent;
static int EarlyInputState;
static UINT32 EarlyInputError;

static void
SendInputBeforeFontList(void *Context, const StateChangedEventArgs *Event)
{
    rdpInput *Input = ((rdpContext *)Context)->input;

    if ((int)Event->newState > EarlyInputState)
        EarlyInputState = (int)Event->newState;
    if (Event->newState == CONNECTION_STATE_FINALIZATION_FONT_LIST && Input->SynchronizeEvent)
        EarlyInputSent = Input->SynchronizeEvent(Input, 0);
}

static BOOL
ConnectWithEarlyInput(const char *Password)
{
    freerdp *Instance = freerdp_new();
    rdpSettings *Settings;
    HANDLE Events[MAXIMUM_WAIT_OBJECTS];
    DWORD Count, Start;
    BOOL Alive = FALSE;

    EarlyInputSent = FALSE;
    EarlyInputState = 0;
    EarlyInputError = 0;
    if (!Instance)
        return FALSE;
    if (!freerdp_context_new(Instance))
    {
        freerdp_free(Instance);
        return FALSE;
    }
    Settings = Instance->context->settings;
    if (freerdp_settings_set_string(Settings, FreeRDP_ServerHostname, "127.0.0.1") &&
        freerdp_settings_set_uint32(Settings, FreeRDP_ServerPort, 3389) &&
        freerdp_settings_set_string(Settings, FreeRDP_Username, "controller") &&
        freerdp_settings_set_string(Settings, FreeRDP_Password, Password) &&
        freerdp_settings_set_bool(Settings, FreeRDP_IgnoreCertificate, TRUE) &&
        freerdp_settings_set_bool(Settings, FreeRDP_RdpSecurity, FALSE) &&
        freerdp_settings_set_bool(Settings, FreeRDP_TlsSecurity, TRUE) &&
        freerdp_settings_set_bool(Settings, FreeRDP_NlaSecurity, FALSE) &&
        freerdp_settings_set_bool(Settings, FreeRDP_ExtSecurity, FALSE) &&
        freerdp_settings_set_bool(Settings, FreeRDP_DeactivateClientDecoding, TRUE) &&
        PubSub_SubscribeStateChanged(Instance->context->pubSub, SendInputBeforeFontList) >= 0 &&
        freerdp_connect(Instance))
    {
        Alive = TRUE;
        Start = GetTickCount();
        while (Alive && GetTickCount() - Start < 3000)
        {
            Count = freerdp_get_event_handles(Instance->context, Events, ARRAYSIZE(Events));
            if (!Count || WaitForMultipleObjects(Count, Events, FALSE, 100) == WAIT_FAILED ||
                !freerdp_check_event_handles(Instance->context) || freerdp_shall_disconnect_context(Instance->context))
                Alive = FALSE;
        }
        freerdp_disconnect(Instance);
    }
    else
        EarlyInputError = freerdp_get_last_error(Instance->context);
    freerdp_context_free(Instance);
    freerdp_free(Instance);
    return Alive;
}

static void
TestNegotiation(BYTE Requested, BYTE ExpectedType, DWORD ExpectedValue)
{
    BYTE Type = 0;
    DWORD Value = 0;

    ok(NegotiateWhenFree(Requested, &Type, &Value, NULL), "No negotiation response for protocols 0x%x\n", Requested);
    ok(Type == ExpectedType, "Protocols 0x%x: type %u, expected %u\n", Requested, Type, ExpectedType);
    ok(Value == ExpectedValue, "Protocols 0x%x: value 0x%lx, expected 0x%lx\n", Requested, Value, ExpectedValue);
}

START_TEST(ControllerMode)
{
    static const char Password[] = "Controller-Test-2026!";
    char Server[MAX_PATH], Temp[MAX_PATH];
    HANDLE Process;
    SOCKET Waiting;
    DWORD Code = 0, Value;
    BYTE Type;
    WSADATA Data;

    if (!GetSystemDirectoryA(Server, MAX_PATH) || strlen(Server) + sizeof("\\rdpserver\\rdpserver.exe") > MAX_PATH)
    {
        skip("No system directory\n");
        return;
    }
    strcat(Server, "\\rdpserver\\rdpserver.exe");
    if (GetFileAttributesA(Server) == INVALID_FILE_ATTRIBUTES)
    {
        skip("No LiberNT RDP console server\n");
        return;
    }
    ok_int(WSAStartup(MAKEWORD(2, 2), &Data), 0);
    Waiting = ConnectLoopback();
    if (Waiting != INVALID_SOCKET)
    {
        closesocket(Waiting);
        skip("Port 3389 is in use\n");
        WSACleanup();
        return;
    }
    GetTempPathA(MAX_PATH, Temp);
    _snprintf(Directory, sizeof(Directory), "%srdpcontroller%lu", Temp, GetCurrentProcessId());
    Directory[sizeof(Directory) - 1] = 0;
    ok(CreateDirectoryA(Directory, NULL), "CreateDirectory failed with %lu\n", GetLastError());
    ok(WriteTextFile("test.crt", TestCertificate), "Cannot write the certificate\n");
    ok(WriteTextFile("test.key", TestPrivateKey), "Cannot write the private key\n");

    Process = StartServer(Server, "127.0.0.1", "short");
    ok(Process != NULL, "Cannot start the server: %lu\n", GetLastError());
    if (Process)
    {
        ok_int(WaitForSingleObject(Process, 20000), WAIT_OBJECT_0);
        GetExitCodeProcess(Process, &Code);
        ok_int(Code, 2);
        StopServer(Process);
    }

    Process = StartServer(Server, "127.0.0.1", Password);
    ok(Process != NULL, "Cannot start the server: %lu\n", GetLastError());
    if (Process)
    {
        ok(WaitForNegotiation(TRUE), "The server does not negotiate on 127.0.0.1:3389\n");
        TestNegotiation(PROTOCOL_SSL, TYPE_RDP_NEG_RSP, PROTOCOL_SSL);
        TestNegotiation(PROTOCOL_SSL | PROTOCOL_HYBRID, TYPE_RDP_NEG_RSP, PROTOCOL_SSL);
        TestNegotiation(PROTOCOL_HYBRID, TYPE_RDP_NEG_FAILURE, SSL_REQUIRED_BY_SERVER);
        TestNegotiation(PROTOCOL_RDP, TYPE_RDP_NEG_FAILURE, SSL_REQUIRED_BY_SERVER);

        Waiting = INVALID_SOCKET;
        ok(NegotiateWhenFree(PROTOCOL_SSL, &Type, &Value, &Waiting), "Cannot open the first connection\n");
        ok(!Negotiate(PROTOCOL_SSL, &Type, &Value), "A second client was served while the first one is connected\n");
        if (Waiting != INVALID_SOCKET)
            closesocket(Waiting);
        ok(WaitForNegotiation(TRUE), "The server does not serve a client after the first one left\n");
        ok(ConnectWithEarlyInput(Password), "The server dropped a client that sent input before the connection was active: state %d, error 0x%08x\n",
           EarlyInputState, EarlyInputError);
        ok(EarlyInputSent, "The client could not send input before the connection was active: state %d\n", EarlyInputState);
        StopServer(Process);
    }

    Process = StartServer(Server, "127.0.0.2", Password);
    ok(Process != NULL, "Cannot start the server: %lu\n", GetLastError());
    if (Process)
    {
        DWORD Start = GetTickCount();
        while ((Waiting = ConnectLoopback()) == INVALID_SOCKET && GetTickCount() - Start < 20000)
            Sleep(200);
        ok(Waiting != INVALID_SOCKET, "The server does not listen on 127.0.0.1:3389\n");
        if (Waiting != INVALID_SOCKET)
            closesocket(Waiting);
        ok(!Negotiate(PROTOCOL_SSL, &Type, &Value), "A client from an address that is not allowed was served\n");
        ok(WaitForSingleObject(Process, 0) == WAIT_TIMEOUT, "The server exited\n");
        StopServer(Process);
    }

    {
        char Path[MAX_PATH];
        static const char *Names[] = { "rdpserver.ini", "test.crt", "test.key" };
        UINT i;
        for (i = 0; i < ARRAYSIZE(Names); ++i)
        {
            _snprintf(Path, sizeof(Path), "%s\\%s", Directory, Names[i]);
            Path[sizeof(Path) - 1] = 0;
            DeleteFileA(Path);
        }
    }
    RemoveDirectoryA(Directory);
    WSACleanup();
}
