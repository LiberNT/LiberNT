/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test reaching a server restarted on the endpoint of one that died during a call
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <rpc.h>
#include <rpcdcep.h>
#include <stdio.h>

#define ENDPOINT L"LiberNTServerRestart"
#define READY_EVENT L"LiberNTServerRestartReady"

#define PROC_PING 0
#define PROC_DIE  1

static void __RPC_STUB ServerPing(PRPC_MESSAGE Message)
{
    Message->BufferLength = sizeof(DWORD);
    if (I_RpcGetBuffer(Message) == RPC_S_OK)
        *(DWORD *)Message->Buffer = GetCurrentProcessId();
}

static void __RPC_STUB ServerDie(PRPC_MESSAGE Message)
{
    ExitProcess(0);
}

static RPC_DISPATCH_FUNCTION ServerFunctions[] = { ServerPing, ServerDie };
static RPC_DISPATCH_TABLE ServerDispatchTable = { ARRAYSIZE(ServerFunctions), ServerFunctions, 0 };

#define FIRST_INTERFACE_ID {{0x5d2b7a43, 0x91c4, 0x4e0f, {0xa6, 0x18, 0x3b, 0x7e, 0x52, 0xc9, 0x0d, 0x84}}, {1, 0}}
#define SECOND_INTERFACE_ID {{0x5d2b7a44, 0x91c4, 0x4e0f, {0xa6, 0x18, 0x3b, 0x7e, 0x52, 0xc9, 0x0d, 0x84}}, {1, 0}}
#define NDR_SYNTAX {{0x8a885d04, 0x1ceb, 0x11c9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}}

static RPC_SERVER_INTERFACE FirstServerInterface =
{
    sizeof(RPC_SERVER_INTERFACE), FIRST_INTERFACE_ID, NDR_SYNTAX, &ServerDispatchTable, 0, NULL, NULL, NULL, 0
};

static RPC_SERVER_INTERFACE SecondServerInterface =
{
    sizeof(RPC_SERVER_INTERFACE), SECOND_INTERFACE_ID, NDR_SYNTAX, &ServerDispatchTable, 0, NULL, NULL, NULL, 0
};

static RPC_CLIENT_INTERFACE FirstClientInterface =
{
    sizeof(RPC_CLIENT_INTERFACE), FIRST_INTERFACE_ID, NDR_SYNTAX, NULL, 0, NULL, 0, NULL, 0
};

static RPC_CLIENT_INTERFACE SecondClientInterface =
{
    sizeof(RPC_CLIENT_INTERFACE), SECOND_INTERFACE_ID, NDR_SYNTAX, NULL, 0, NULL, 0, NULL, 0
};

static void RunServer(void)
{
    HANDLE Ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, READY_EVENT);

    if (!Ready ||
        RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (RPC_WSTR)ENDPOINT, NULL) != RPC_S_OK ||
        RpcServerRegisterIf(&FirstServerInterface, NULL, NULL) != RPC_S_OK ||
        RpcServerRegisterIf(&SecondServerInterface, NULL, NULL) != RPC_S_OK ||
        RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE) != RPC_S_OK)
    {
        ExitProcess(1);
    }

    SetEvent(Ready);
    RpcMgmtWaitServerListen();
    ExitProcess(0);
}

static HANDLE StartServer(HANDLE Ready)
{
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOA StartupInfo = { sizeof(StartupInfo) };
    char Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    HANDLE Handles[2];
    DWORD Wait;

    ResetEvent(Ready);
    GetModuleFileNameA(NULL, Path, ARRAYSIZE(Path));
    sprintf(CommandLine, "\"%s\" ServerRestart server", Path);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(0, "CreateProcess failed: %lu\n", GetLastError());
        return NULL;
    }
    CloseHandle(ProcessInfo.hThread);

    Handles[0] = Ready;
    Handles[1] = ProcessInfo.hProcess;
    Wait = WaitForMultipleObjects(ARRAYSIZE(Handles), Handles, FALSE, 5000);
    ok(Wait == WAIT_OBJECT_0, "server did not start: %lu\n", Wait);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, 1);
        CloseHandle(ProcessInfo.hProcess);
        return NULL;
    }
    return ProcessInfo.hProcess;
}

static RPC_BINDING_HANDLE Bind(void)
{
    RPC_BINDING_HANDLE Binding = NULL;
    RPC_WSTR StringBinding;
    RPC_STATUS Status;

    Status = RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL, (RPC_WSTR)ENDPOINT, NULL, &StringBinding);
    ok_long(Status, RPC_S_OK);
    if (Status != RPC_S_OK)
        return NULL;
    Status = RpcBindingFromStringBindingW(StringBinding, &Binding);
    ok_long(Status, RPC_S_OK);
    RpcStringFreeW(&StringBinding);
    return Status == RPC_S_OK ? Binding : NULL;
}

static RPC_STATUS Call(RPC_BINDING_HANDLE Binding, RPC_CLIENT_INTERFACE *Interface, UINT ProcNum, DWORD *ServerPid)
{
    RPC_MESSAGE Message;
    RPC_STATUS Status;

    memset(&Message, 0, sizeof(Message));
    Message.Handle = Binding;
    Message.DataRepresentation = NDR_LOCAL_DATA_REPRESENTATION;
    Message.RpcInterfaceInformation = Interface;
    Message.ProcNum = ProcNum;
    Message.BufferLength = sizeof(DWORD);

    Status = I_RpcGetBuffer(&Message);
    if (Status != RPC_S_OK)
        return Status;

    *(DWORD *)Message.Buffer = 0;
    Status = I_RpcSendReceive(&Message);
    if (Status == RPC_S_OK && ServerPid)
        *ServerPid = Message.BufferLength >= sizeof(DWORD) ? *(DWORD *)Message.Buffer : 0;
    I_RpcFreeBuffer(&Message);
    return Status;
}

START_TEST(ServerRestart)
{
    RPC_BINDING_HANDLE Binding, DyingBinding;
    HANDLE Ready, Server;
    RPC_STATUS Status;
    DWORD Pid;
    char **Arguments;

    if (winetest_get_mainargs(&Arguments) >= 3 && !strcmp(Arguments[2], "server"))
    {
        RunServer();
        return;
    }

    Ready = CreateEventW(NULL, TRUE, FALSE, READY_EVENT);
    ok(Ready != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (!Ready)
        return;

    Server = StartServer(Ready);
    Binding = Server ? Bind() : NULL;
    DyingBinding = Binding ? Bind() : NULL;
    if (!DyingBinding)
    {
        if (Binding)
            RpcBindingFree(&Binding);
        if (Server)
        {
            TerminateProcess(Server, 1);
            CloseHandle(Server);
        }
        CloseHandle(Ready);
        return;
    }

    /* A completed call leaves its connection idle in the association both bindings share */
    Pid = 0;
    Status = Call(Binding, &FirstClientInterface, PROC_PING, &Pid);
    ok_long(Status, RPC_S_OK);
    ok_long(Pid, GetProcessId(Server));

    Status = Call(DyingBinding, &SecondClientInterface, PROC_DIE, NULL);
    ok(Status != RPC_S_OK, "a call to a server that exits before replying succeeded\n");
    ok_long(WaitForSingleObject(Server, 5000), WAIT_OBJECT_0);
    CloseHandle(Server);
    ok_long(RpcBindingFree(&Binding), RPC_S_OK);
    ok_long(RpcBindingFree(&DyingBinding), RPC_S_OK);

    Server = StartServer(Ready);
    Binding = Server ? Bind() : NULL;
    if (Binding)
    {
        Pid = 0;
        Status = Call(Binding, &SecondClientInterface, PROC_PING, &Pid);
        ok_long(Status, RPC_S_OK);
        ok_long(Pid, GetProcessId(Server));
        ok_long(RpcBindingFree(&Binding), RPC_S_OK);
    }
    if (Server)
    {
        TerminateProcess(Server, 0);
        WaitForSingleObject(Server, 5000);
        CloseHandle(Server);
    }
    CloseHandle(Ready);
}
