/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the activation, deactivation and closing of RPC interface groups
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <rpc.h>
#include <rpcdcep.h>

#define ENDPOINT_W L"LiberNTInterfaceGroup"
#define ENDPOINT_A "LiberNTInterfaceGroupA"

#define PROC_PING  0
#define PROC_BLOCK 1

static HANDLE BlockEntered;
static HANDLE BlockRelease;

static void __RPC_STUB ServerPing(PRPC_MESSAGE Message)
{
    Message->BufferLength = sizeof(DWORD);
    if (I_RpcGetBuffer(Message) == RPC_S_OK)
        *(DWORD *)Message->Buffer = GetCurrentProcessId();
}

static void __RPC_STUB ServerBlock(PRPC_MESSAGE Message)
{
    SetEvent(BlockEntered);
    WaitForSingleObject(BlockRelease, 10000);
    ServerPing(Message);
}

static RPC_DISPATCH_FUNCTION ServerFunctions[] = { ServerPing, ServerBlock };
static RPC_DISPATCH_TABLE ServerDispatchTable = { ARRAYSIZE(ServerFunctions), ServerFunctions, 0 };

#define GROUP_INTERFACE_ID {{0x7c1e5a90, 0x3b2d, 0x4f61, {0x9e, 0x07, 0x5a, 0x4c, 0x81, 0x2f, 0x6d, 0x13}}, {1, 0}}
#define NDR_SYNTAX {{0x8a885d04, 0x1ceb, 0x11c9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}}

static RPC_SERVER_INTERFACE GroupServerInterface =
{
    sizeof(RPC_SERVER_INTERFACE), GROUP_INTERFACE_ID, NDR_SYNTAX, &ServerDispatchTable, 0, NULL, NULL, NULL, 0
};

static RPC_CLIENT_INTERFACE GroupClientInterface =
{
    sizeof(RPC_CLIENT_INTERFACE), GROUP_INTERFACE_ID, NDR_SYNTAX, NULL, 0, NULL, 0, NULL, 0
};

static RPC_BINDING_HANDLE Bind(PCWSTR Endpoint)
{
    RPC_BINDING_HANDLE Binding = NULL;
    RPC_WSTR StringBinding;
    RPC_STATUS Status;

    Status = RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL, (RPC_WSTR)Endpoint, NULL, &StringBinding);
    ok_long(Status, RPC_S_OK);
    if (Status != RPC_S_OK)
        return NULL;
    Status = RpcBindingFromStringBindingW(StringBinding, &Binding);
    ok_long(Status, RPC_S_OK);
    RpcStringFreeW(&StringBinding);
    return Status == RPC_S_OK ? Binding : NULL;
}

static RPC_STATUS CallOn(RPC_BINDING_HANDLE Binding, UINT ProcNum, DWORD *ServerPid)
{
    RPC_MESSAGE Message;
    RPC_STATUS Status;

    memset(&Message, 0, sizeof(Message));
    Message.Handle = Binding;
    Message.DataRepresentation = NDR_LOCAL_DATA_REPRESENTATION;
    Message.RpcInterfaceInformation = &GroupClientInterface;
    Message.ProcNum = ProcNum;
    Message.BufferLength = sizeof(DWORD);

    Status = I_RpcGetBuffer(&Message);
    if (Status != RPC_S_OK)
        return Status;

    *(DWORD *)Message.Buffer = 0;
    Status = I_RpcSendReceive(&Message);
    if (Status != RPC_S_OK)
        return Status;

    if (ServerPid)
        *ServerPid = Message.BufferLength >= sizeof(DWORD) ? *(DWORD *)Message.Buffer : 0;
    I_RpcFreeBuffer(&Message);
    return RPC_S_OK;
}

static RPC_STATUS Call(PCWSTR Endpoint, UINT ProcNum, DWORD *ServerPid)
{
    RPC_BINDING_HANDLE Binding = Bind(Endpoint);
    RPC_STATUS Status;

    if (!Binding)
        return RPC_S_INVALID_BINDING;
    Status = CallOn(Binding, ProcNum, ServerPid);
    RpcBindingFree(&Binding);
    return Status;
}

static DWORD WINAPI BlockingCall(PVOID Parameter)
{
    return Call(ENDPOINT_W, PROC_BLOCK, NULL);
}

static BOOL BindingsName(RPC_BINDING_VECTOR *Vector, PCWSTR Endpoint)
{
    RPC_WSTR StringBinding;
    BOOL Found = FALSE;
    ULONG i;

    for (i = 0; i < Vector->Count && !Found; i++)
    {
        if (RpcBindingToStringBindingW(Vector->BindingH[i], &StringBinding) != RPC_S_OK)
            continue;
        Found = wcsstr((PCWSTR)StringBinding, Endpoint) != NULL;
        RpcStringFreeW(&StringBinding);
    }
    return Found;
}

static void TestUnicodeGroup(void)
{
    RPC_INTERFACE_TEMPLATEW Interface = { 0 };
    RPC_ENDPOINT_TEMPLATEW Endpoint = { 0 };
    RPC_INTERFACE_GROUP Group = NULL;
    RPC_BINDING_HANDLE Held;
    RPC_BINDING_VECTOR *Vector;
    RPC_STATUS Status;
    HANDLE Thread;
    DWORD Pid, Result;

    Interface.IfSpec = &GroupServerInterface;
    Interface.MaxCalls = RPC_C_LISTEN_MAX_CALLS_DEFAULT;
    Interface.MaxRpcSize = (unsigned int)-1;
    Interface.Flags = RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH;
    Endpoint.ProtSeq = (RPC_WSTR)L"ncalrpc";
    Endpoint.Endpoint = (RPC_WSTR)ENDPOINT_W;
    Endpoint.Backlog = RPC_C_PROTSEQ_MAX_REQS_DEFAULT;

    Status = RpcServerInterfaceGroupCreateW(&Interface, 1, &Endpoint, 1, INFINITE, NULL, NULL, &Group);
    ok_long(Status, RPC_S_OK);
    ok(Group != NULL, "no interface group was returned\n");
    if (Status != RPC_S_OK)
        return;

    Vector = NULL;
    Status = RpcServerInterfaceGroupInqBindings(Group, &Vector);
    ok_long(Status, RPC_S_NO_BINDINGS);

    Status = RpcServerInterfaceGroupActivate(Group);
    ok_long(Status, RPC_S_OK);
    Status = RpcServerInterfaceGroupActivate(Group);
    ok_long(Status, RPC_S_ALREADY_LISTENING);

    Vector = NULL;
    Status = RpcServerInterfaceGroupInqBindings(Group, &Vector);
    ok_long(Status, RPC_S_OK);
    ok(Vector != NULL && BindingsName(Vector, ENDPOINT_W), "the bindings of the active group do not name its endpoint\n");
    if (Vector)
        ok_long(RpcBindingVectorFree(&Vector), RPC_S_OK);

    Pid = 0;
    Status = Call(ENDPOINT_W, PROC_PING, &Pid);
    ok_long(Status, RPC_S_OK);
    ok_long(Pid, GetCurrentProcessId());

    Thread = CreateThread(NULL, 0, BlockingCall, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread)
    {
        ok_long(WaitForSingleObject(BlockEntered, 5000), WAIT_OBJECT_0);
        Status = RpcServerInterfaceGroupDeactivate(Group, FALSE);
        ok_long(Status, RPC_S_SERVER_TOO_BUSY);

        Pid = 0;
        Status = Call(ENDPOINT_W, PROC_PING, &Pid);
        ok_long(Status, RPC_S_OK);
        ok_long(Pid, GetCurrentProcessId());

        SetEvent(BlockRelease);
        ok_long(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
        Result = (DWORD)-1;
        GetExitCodeThread(Thread, &Result);
        ok_long(Result, RPC_S_OK);
        CloseHandle(Thread);
        ResetEvent(BlockRelease);
    }

    Held = Bind(ENDPOINT_W);
    if (Held)
    {
        ok_long(CallOn(Held, PROC_PING, NULL), RPC_S_OK);
        Status = RpcServerInterfaceGroupDeactivate(Group, FALSE);
        ok_long(Status, RPC_S_SERVER_TOO_BUSY);
        ok_long(CallOn(Held, PROC_PING, NULL), RPC_S_OK);
        RpcBindingFree(&Held);
    }

    Status = RpcServerInterfaceGroupDeactivate(Group, TRUE);
    ok_long(Status, RPC_S_OK);
    Status = Call(ENDPOINT_W, PROC_PING, NULL);
    ok(Status != RPC_S_OK, "a deactivated group dispatched a call\n");

    Status = RpcServerInterfaceGroupActivate(Group);
    ok_long(Status, RPC_S_OK);
    Pid = 0;
    Status = Call(ENDPOINT_W, PROC_PING, &Pid);
    ok_long(Status, RPC_S_OK);
    ok_long(Pid, GetCurrentProcessId());

    Status = RpcServerInterfaceGroupClose(Group);
    ok_long(Status, RPC_S_OK);
    Status = Call(ENDPOINT_W, PROC_PING, NULL);
    ok(Status != RPC_S_OK, "a closed group dispatched a call\n");
}

static void TestAnsiGroup(void)
{
    RPC_INTERFACE_TEMPLATEA Interface = { 0 };
    RPC_ENDPOINT_TEMPLATEA Endpoint = { 0 };
    RPC_INTERFACE_GROUP Group = NULL;
    RPC_STATUS Status;
    DWORD Pid;

    Interface.IfSpec = &GroupServerInterface;
    Interface.MaxCalls = RPC_C_LISTEN_MAX_CALLS_DEFAULT;
    Interface.MaxRpcSize = (unsigned int)-1;
    Interface.Flags = RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH;
    Interface.Annotation = (RPC_CSTR)"LiberNT interface group";
    Endpoint.ProtSeq = (RPC_CSTR)"ncalrpc";
    Endpoint.Endpoint = (RPC_CSTR)ENDPOINT_A;
    Endpoint.Backlog = RPC_C_PROTSEQ_MAX_REQS_DEFAULT;

    Status = RpcServerInterfaceGroupCreateA(&Interface, 1, &Endpoint, 1, INFINITE, NULL, NULL, &Group);
    ok_long(Status, RPC_S_OK);
    if (Status != RPC_S_OK)
        return;

    Status = RpcServerInterfaceGroupActivate(Group);
    ok_long(Status, RPC_S_OK);
    Pid = 0;
    Status = Call(L"" ENDPOINT_A, PROC_PING, &Pid);
    ok_long(Status, RPC_S_OK);
    ok_long(Pid, GetCurrentProcessId());

    Status = RpcServerInterfaceGroupClose(Group);
    ok_long(Status, RPC_S_OK);
}

START_TEST(InterfaceGroup)
{
    BlockEntered = CreateEventW(NULL, FALSE, FALSE, NULL);
    BlockRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(BlockEntered && BlockRelease, "CreateEvent failed: %lu\n", GetLastError());
    if (!BlockEntered || !BlockRelease)
        return;

    TestUnicodeGroup();
    TestAnsiGroup();

    CloseHandle(BlockEntered);
    CloseHandle(BlockRelease);
}
