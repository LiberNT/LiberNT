/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite NPFS Connect test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>
#include "npfs.h"

#define MAX_INSTANCES   5
#define IN_QUOTA        4096
#define OUT_QUOTA       4096
#define CANCEL_RACE_IRPS        8
#define CANCEL_RACE_ITERATIONS  400

#define CheckServer(ServerHandle, State)                        \
    NpCheckServerPipe(ServerHandle,                             \
                      BYTE_STREAM, QUEUE, BYTE_STREAM, DUPLEX,  \
                      MAX_INSTANCES, 1,                         \
                      IN_QUOTA, 0,                              \
                      OUT_QUOTA, OUT_QUOTA,                     \
                      State)

#define CheckClient(ClientHandle, State)                        \
    NpCheckClientPipe(ClientHandle,                             \
                      BYTE_STREAM, QUEUE, BYTE_STREAM, DUPLEX,  \
                      MAX_INSTANCES, 1,                         \
                      IN_QUOTA, 0,                              \
                      OUT_QUOTA, OUT_QUOTA,                     \
                      State)

static
VOID
ConnectPipe(
    IN OUT PTHREAD_CONTEXT Context)
{
    HANDLE ClientHandle;

    ClientHandle = NULL;
    Context->Connect.Status = NpOpenPipe(&ClientHandle,
                                         Context->Connect.PipePath,
                                         FILE_PIPE_FULL_DUPLEX);
    Context->Connect.ClientHandle = ClientHandle;
}

static
VOID
ListenPipe(
    IN OUT PTHREAD_CONTEXT Context)
{
    Context->Listen.Status = NpListenPipe(Context->Listen.ServerHandle);
}

static
BOOLEAN
CheckConnectPipe(
    IN PTHREAD_CONTEXT Context,
    IN PCWSTR PipePath,
    IN ULONG MilliSeconds)
{
    Context->Work = ConnectPipe;
    Context->Connect.PipePath = PipePath;
    return TriggerWork(Context, MilliSeconds);
}

static
BOOLEAN
CheckListenPipe(
    IN PTHREAD_CONTEXT Context,
    IN HANDLE ServerHandle,
    IN ULONG MilliSeconds)
{
    Context->Work = ListenPipe;
    Context->Listen.ServerHandle = ServerHandle;
    return TriggerWork(Context, MilliSeconds);
}

static
VOID
TestConnect(
    IN HANDLE ServerHandle,
    IN PCWSTR PipePath)
{
    NTSTATUS Status;
    THREAD_CONTEXT ConnectContext;
    THREAD_CONTEXT ListenContext;
    BOOLEAN Okay;
    HANDLE ClientHandle;

    StartWorkerThread(&ConnectContext);
    StartWorkerThread(&ListenContext);

    /* Server should start out listening */
    CheckServer(ServerHandle, FILE_PIPE_LISTENING_STATE);

    /* Connect a client */
    ClientHandle = NULL;
    Okay = CheckConnectPipe(&ConnectContext, PipePath, 100);
    ok_bool_true(Okay, "CheckConnectPipe returned");
    ok_eq_hex(ConnectContext.Connect.Status, STATUS_SUCCESS);
    if (NT_SUCCESS(ConnectContext.Connect.Status))
    {
        ClientHandle = ConnectContext.Connect.ClientHandle;
        CheckClient(ClientHandle, FILE_PIPE_CONNECTED_STATE);
    }
    CheckServer(ServerHandle, FILE_PIPE_CONNECTED_STATE);

    /* Connect another client */
    Okay = CheckConnectPipe(&ConnectContext, PipePath, 100);
    ok_bool_true(Okay, "CheckConnectPipe returned");
    ok_eq_hex(ConnectContext.Connect.Status, STATUS_PIPE_NOT_AVAILABLE);
    if (NT_SUCCESS(ConnectContext.Connect.Status))
        ObCloseHandle(ConnectContext.Connect.ClientHandle, KernelMode);
    CheckClient(ClientHandle, FILE_PIPE_CONNECTED_STATE);
    CheckServer(ServerHandle, FILE_PIPE_CONNECTED_STATE);

    /* Disconnecting the client should fail */
    Status = NpDisconnectPipe(ClientHandle);
    ok_eq_hex(Status, STATUS_ILLEGAL_FUNCTION);
    CheckClient(ClientHandle, FILE_PIPE_CONNECTED_STATE);
    CheckServer(ServerHandle, FILE_PIPE_CONNECTED_STATE);

    /* Listening on the client should fail */
    Status = NpListenPipe(ClientHandle);
    ok_eq_hex(Status, STATUS_ILLEGAL_FUNCTION);
    CheckClient(ClientHandle, FILE_PIPE_CONNECTED_STATE);
    CheckServer(ServerHandle, FILE_PIPE_CONNECTED_STATE);

    /* Close client */
    if (ClientHandle)
        ObCloseHandle(ClientHandle, KernelMode);
    CheckServer(ServerHandle, FILE_PIPE_CLOSING_STATE);

    /* Connecting a client now should fail */
    Okay = CheckConnectPipe(&ConnectContext, PipePath, 100);
    ok_bool_true(Okay, "CheckConnectPipe returned");
    ok_eq_hex(ConnectContext.Connect.Status, STATUS_PIPE_NOT_AVAILABLE);
    if (NT_SUCCESS(ConnectContext.Connect.Status))
        ObCloseHandle(ConnectContext.Connect.ClientHandle, KernelMode);
    CheckServer(ServerHandle, FILE_PIPE_CLOSING_STATE);

    /* Listening should fail */
    Okay = CheckListenPipe(&ListenContext, ServerHandle, 100);
    ok_bool_true(Okay, "CheckListenPipe returned");
    if (!skip(Okay, "Listen succeeded unexpectedly\n"))
        CheckServer(ServerHandle, FILE_PIPE_CLOSING_STATE);

    /* Disconnect server */
    Status = NpDisconnectPipe(ServerHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckServer(ServerHandle, FILE_PIPE_DISCONNECTED_STATE);

    /* Disconnecting again should fail */
    Status = NpDisconnectPipe(ServerHandle);
    ok_eq_hex(Status, STATUS_PIPE_DISCONNECTED);
    CheckServer(ServerHandle, FILE_PIPE_DISCONNECTED_STATE);

    /* Connecting a client now should fail */
    Okay = CheckConnectPipe(&ConnectContext, PipePath, 100);
    ok_bool_true(Okay, "CheckConnectPipe returned");
    ok_eq_hex(ConnectContext.Connect.Status, STATUS_PIPE_NOT_AVAILABLE);
    if (NT_SUCCESS(ConnectContext.Connect.Status))
        ObCloseHandle(ConnectContext.Connect.ClientHandle, KernelMode);
    CheckServer(ServerHandle, FILE_PIPE_DISCONNECTED_STATE);

    /**************************************************************************/
    /* Now listen again */
    Okay = CheckListenPipe(&ListenContext, ServerHandle, 100);
    ok_bool_false(Okay, "CheckListenPipe returned");
    //blocks: CheckServer(ServerHandle, FILE_PIPE_LISTENING_STATE);

    /* Connect client */
    ClientHandle = NULL;
    Okay = CheckConnectPipe(&ConnectContext, PipePath, 100);
    ok_bool_true(Okay, "CheckConnectPipe returned");
    ok_eq_hex(ConnectContext.Connect.Status, STATUS_SUCCESS);
    if (NT_SUCCESS(ConnectContext.Connect.Status))
    {
        ClientHandle = ConnectContext.Connect.ClientHandle;
        CheckClient(ClientHandle, FILE_PIPE_CONNECTED_STATE);
    }
    Okay = WaitForWork(&ListenContext, 100);
    ok_bool_true(Okay, "WaitForWork returned");
    ok_eq_hex(ListenContext.Listen.Status, STATUS_SUCCESS);
    CheckServer(ServerHandle, FILE_PIPE_CONNECTED_STATE);

    /* Listening again should fail */
    Okay = CheckListenPipe(&ListenContext, ServerHandle, 100);
    ok_bool_true(Okay, "CheckListenPipe returned");
    ok_eq_hex(ListenContext.Listen.Status, STATUS_PIPE_CONNECTED);
    CheckClient(ClientHandle, FILE_PIPE_CONNECTED_STATE);
    CheckServer(ServerHandle, FILE_PIPE_CONNECTED_STATE);

    /* Disconnect server */
    Status = NpDisconnectPipe(ServerHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    NpQueryPipe(ClientHandle, STATUS_PIPE_DISCONNECTED);
    CheckServer(ServerHandle, FILE_PIPE_DISCONNECTED_STATE);

    /* Close client */
    if (ClientHandle)
        ObCloseHandle(ClientHandle, KernelMode);
    CheckServer(ServerHandle, FILE_PIPE_DISCONNECTED_STATE);

    /**************************************************************************/
    /* Listen once more */
    Okay = CheckListenPipe(&ListenContext, ServerHandle, 100);
    ok_bool_false(Okay, "CheckListenPipe returned");
    //blocks: CheckServer(ServerHandle, FILE_PIPE_LISTENING_STATE);

    /* Connect client */
    ClientHandle = NULL;
    Okay = CheckConnectPipe(&ConnectContext, PipePath, 100);
    ok_bool_true(Okay, "CheckConnectPipe returned");
    ok_eq_hex(ConnectContext.Connect.Status, STATUS_SUCCESS);
    if (NT_SUCCESS(ConnectContext.Connect.Status))
    {
        ClientHandle = ConnectContext.Connect.ClientHandle;
        CheckClient(ClientHandle, FILE_PIPE_CONNECTED_STATE);
    }
    Okay = WaitForWork(&ListenContext, 100);
    ok_bool_true(Okay, "WaitForWork returned");
    ok_eq_hex(ListenContext.Listen.Status, STATUS_SUCCESS);
    CheckServer(ServerHandle, FILE_PIPE_CONNECTED_STATE);

    /* Close server */
    Status = ObCloseHandle(ServerHandle, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);
    NpCheckClientPipe(ClientHandle,
                      BYTE_STREAM, QUEUE, BYTE_STREAM, DUPLEX,
                      MAX_INSTANCES,
                      GetNTVersion() >= _WIN32_WINNT_VISTA ? 0 : 1,
                      IN_QUOTA, 0,
                      OUT_QUOTA, OUT_QUOTA,
                      FILE_PIPE_CLOSING_STATE);

    /* Close client */
    if (ClientHandle)
        ObCloseHandle(ClientHandle, KernelMode);

    FinishWorkerThread(&ListenContext);
    FinishWorkerThread(&ConnectContext);
}

typedef struct _LISTEN_CANCEL_RACE_CONTEXT
{
    HANDLE ServerHandle;
    HANDLE DisconnectEvent;
    KEVENT StartEvent;
    KEVENT DoneEvent;
    volatile BOOLEAN Stop;
} LISTEN_CANCEL_RACE_CONTEXT, *PLISTEN_CANCEL_RACE_CONTEXT;

static KSTART_ROUTINE DisconnectRaceWorker;
static
VOID
NTAPI
DisconnectRaceWorker(
    IN PVOID Parameter)
{
    PLISTEN_CANCEL_RACE_CONTEXT Context = Parameter;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    for (;;)
    {
        KeWaitForSingleObject(&Context->StartEvent, Executive, KernelMode, FALSE, NULL);
        if (Context->Stop)
            break;

        Status = ZwFsControlFile(Context->ServerHandle,
                                 Context->DisconnectEvent,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_PIPE_DISCONNECT,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
        if (Status == STATUS_PENDING)
            ZwWaitForSingleObject(Context->DisconnectEvent, FALSE, NULL);

        KeSetEvent(&Context->DoneEvent, IO_NO_INCREMENT, FALSE);
    }
}

static
VOID
TestListenCancelRace(VOID)
{
    NTSTATUS Status;
    HANDLE ServerHandle = NULL;
    HANDLE Events[CANCEL_RACE_IRPS];
    IO_STATUS_BLOCK IoStatusBlocks[CANCEL_RACE_IRPS];
    NTSTATUS ListenStatuses[CANCEL_RACE_IRPS];
    IO_STATUS_BLOCK CancelIosb;
    OBJECT_ATTRIBUTES ObjectAttributes;
    LARGE_INTEGER DefaultTimeout;
    LISTEN_CANCEL_RACE_CONTEXT Context;
    PKTHREAD WorkerThread;
    ULONG Iteration, Index;
    ULONG UnexpectedStatusCount = 0;

    RtlZeroMemory(Events, sizeof(Events));
    RtlZeroMemory(&Context, sizeof(Context));
    DefaultTimeout.QuadPart = -50 * 1000 * 10;

    Status = NpCreatePipeEx(&ServerHandle,
                            DEVICE_NAMED_PIPE L"\\KmtestNpfsListenCancelRacePipe",
                            BYTE_STREAM,
                            QUEUE,
                            BYTE_STREAM,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            MAX_INSTANCES,
                            IN_QUOTA,
                            OUT_QUOTA,
                            SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE,
                            FILE_OPEN_IF,
                            0,
                            &DefaultTimeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    for (Index = 0; Index < CANCEL_RACE_IRPS; Index++)
    {
        Status = ZwCreateEvent(&Events[Index],
                               EVENT_ALL_ACCESS,
                               &ObjectAttributes,
                               NotificationEvent,
                               FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Status = ZwCreateEvent(&Context.DisconnectEvent,
                           EVENT_ALL_ACCESS,
                           &ObjectAttributes,
                           NotificationEvent,
                           FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Context.ServerHandle = ServerHandle;
    KeInitializeEvent(&Context.StartEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.DoneEvent, SynchronizationEvent, FALSE);

    WorkerThread = KmtStartThread(DisconnectRaceWorker, &Context);

    for (Iteration = 0; Iteration < CANCEL_RACE_ITERATIONS; Iteration++)
    {
        for (Index = 0; Index < CANCEL_RACE_IRPS; Index++)
        {
            ZwResetEvent(Events[Index], NULL);
            IoStatusBlocks[Index].Status = STATUS_PENDING;
            IoStatusBlocks[Index].Information = 0;
            ListenStatuses[Index] = ZwFsControlFile(ServerHandle,
                                                    Events[Index],
                                                    NULL,
                                                    NULL,
                                                    &IoStatusBlocks[Index],
                                                    FSCTL_PIPE_LISTEN,
                                                    NULL,
                                                    0,
                                                    NULL,
                                                    0);
        }

        KeSetEvent(&Context.StartEvent, IO_NO_INCREMENT, FALSE);
        ZwCancelIoFile(ServerHandle, &CancelIosb);

        for (Index = 0; Index < CANCEL_RACE_IRPS; Index++)
        {
            NTSTATUS FinalStatus = ListenStatuses[Index];

            if (FinalStatus == STATUS_PENDING)
            {
                ZwWaitForSingleObject(Events[Index], FALSE, NULL);
                FinalStatus = IoStatusBlocks[Index].Status;
            }
            if (FinalStatus != STATUS_CANCELLED &&
                FinalStatus != STATUS_PIPE_DISCONNECTED &&
                FinalStatus != STATUS_SUCCESS)
            {
                UnexpectedStatusCount++;
            }
        }

        KeWaitForSingleObject(&Context.DoneEvent, Executive, KernelMode, FALSE, NULL);
    }

    Context.Stop = TRUE;
    KeSetEvent(&Context.StartEvent, IO_NO_INCREMENT, FALSE);
    KmtFinishThread(WorkerThread, NULL);

    ok_eq_ulong(UnexpectedStatusCount, 0UL);

Cleanup:
    if (Context.DisconnectEvent)
        ObCloseHandle(Context.DisconnectEvent, KernelMode);
    for (Index = 0; Index < CANCEL_RACE_IRPS; Index++)
    {
        if (Events[Index])
            ObCloseHandle(Events[Index], KernelMode);
    }
    if (ServerHandle)
        ObCloseHandle(ServerHandle, KernelMode);
}

static KSTART_ROUTINE RunTest;
static
VOID
NTAPI
RunTest(
    IN PVOID Context)
{
    NTSTATUS Status;
    HANDLE ServerHandle;

    UNREFERENCED_PARAMETER(Context);

    ServerHandle = INVALID_HANDLE_VALUE;
    Status = NpCreatePipe(&ServerHandle,
                          DEVICE_NAMED_PIPE L"\\KmtestNpfsConnectTestPipe",
                          BYTE_STREAM, QUEUE, BYTE_STREAM, DUPLEX,
                          MAX_INSTANCES,
                          IN_QUOTA,
                          OUT_QUOTA);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(ServerHandle != NULL && ServerHandle != INVALID_HANDLE_VALUE, "ServerHandle = %p\n", ServerHandle);
    if (!skip(NT_SUCCESS(Status) && ServerHandle != NULL && ServerHandle != INVALID_HANDLE_VALUE, "No pipe\n"))
    {
        CheckServer(ServerHandle, FILE_PIPE_LISTENING_STATE);
        TestConnect(ServerHandle, DEVICE_NAMED_PIPE L"\\KmtestNpfsConnectTestPipe");
    }

    TestListenCancelRace();
}

START_TEST(NpfsConnect)
{
    PKTHREAD Thread;

    Thread = KmtStartThread(RunTest, NULL);
    KmtFinishThread(Thread, NULL);
}
