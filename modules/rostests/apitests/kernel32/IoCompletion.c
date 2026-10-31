/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test I/O completion port dispatcher-object semantics
 */

#include "precomp.h"
#include "threadwait.h"

typedef BOOL (WINAPI *PWAIT_ON_ADDRESS)(volatile VOID *, PVOID, SIZE_T, DWORD);

typedef enum _WORKER_BLOCK_KIND
{
    BlockSingle,
    BlockMultiple,
    BlockDelay,
    BlockAddress,
    BlockInterruptedAlertable
} WORKER_BLOCK_KIND;

typedef struct _ACTIVE_WORKER_CONTEXT
{
    HANDLE Port;
    HANDLE Event;
    HANDLE Event2;
    HANDLE Packet;
    WORKER_BLOCK_KIND Kind;
    PWAIT_ON_ADDRESS WaitOnAddress;
    volatile LONG *Order;
    volatile LONG Go;
    volatile LONG Release;
    volatile LONG Address;
    BOOL GotPacket;
    ULONG_PTR Key;
    DWORD WaitResult;
    LONG Finished;
} ACTIVE_WORKER_CONTEXT;

typedef struct _BLOCKED_WORKER_CONTEXT
{
    HANDLE Port;
    volatile LONG *Order;
    BOOL Success;
    ULONG_PTR Key;
    LONG Finished;
} BLOCKED_WORKER_CONTEXT;

typedef struct _CROSS_PORT_CONTEXT
{
    HANDLE OwnPort;
    HANDLE OtherPort;
    HANDLE Event;
    HANDLE PrimedEvent;
    HANDLE StartEvent;
    volatile LONG Started;
    DWORD ThreadId;
    BOOL Primed;
    DWORD Result;
} CROSS_PORT_CONTEXT;

static PCSTR BlockNames[] =
{
    "WaitForSingleObject",
    "WaitForMultipleObjects",
    "Sleep",
    "WaitOnAddress",
    "an interrupted alertable SleepEx"
};

typedef struct _DIRECT_WAIT_CONTEXT
{
    HANDLE Port;
    HANDLE Ready;
    DWORD Result;
    DWORD Timeout;
} DIRECT_WAIT_CONTEXT;

typedef struct _DEQUEUE_WAIT_CONTEXT
{
    HANDLE Port;
    HANDLE Ready;
    BOOL Success;
    DWORD Bytes;
    ULONG_PTR Key;
    LPOVERLAPPED Overlapped;
    DWORD Error;
} DEQUEUE_WAIT_CONTEXT;

typedef struct _OWN_QUEUE_WAIT_CONTEXT
{
    HANDLE Event;
    HANDLE Port;
    HANDLE Ready;
    BOOL PrimeSuccess;
    BOOL RemoveSuccess;
    DWORD Result;
    DWORD PrimeBytes;
    DWORD RemoveBytes;
    ULONG_PTR PrimeKey;
    ULONG_PTR RemoveKey;
    LPOVERLAPPED PrimeOverlapped;
    LPOVERLAPPED RemoveOverlapped;
} OWN_QUEUE_WAIT_CONTEXT;

static DWORD WINAPI
DirectWaitThread(_Inout_ PVOID Parameter)
{
    DIRECT_WAIT_CONTEXT *Context = Parameter;

    SetEvent(Context->Ready);
    Context->Result = WaitForSingleObject(Context->Port, Context->Timeout);
    return 0;
}

static DWORD WINAPI
DequeueWaitThread(_Inout_ PVOID Parameter)
{
    DEQUEUE_WAIT_CONTEXT *Context = Parameter;

    SetEvent(Context->Ready);
    Context->Success = GetQueuedCompletionStatus(Context->Port, &Context->Bytes, &Context->Key, &Context->Overlapped, 5000);
    Context->Error = GetLastError();
    return 0;
}

static DWORD WINAPI
OwnQueueMultipleWaitThread(_Inout_ PVOID Parameter)
{
    OWN_QUEUE_WAIT_CONTEXT *Context = Parameter;
    HANDLE Handles[2];

    Context->PrimeSuccess = GetQueuedCompletionStatus(Context->Port, &Context->PrimeBytes, &Context->PrimeKey, &Context->PrimeOverlapped, 5000);
    SetEvent(Context->Ready);
    Handles[0] = Context->Event;
    Handles[1] = Context->Port;
    Context->Result = WaitForMultipleObjects(2, Handles, FALSE, 5000);
    Context->RemoveSuccess = GetQueuedCompletionStatus(Context->Port, &Context->RemoveBytes, &Context->RemoveKey, &Context->RemoveOverlapped, 0);
    return 0;
}

static VOID CALLBACK
NopApc(_In_ ULONG_PTR Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
}

static VOID
SpinUntilSet(_In_ volatile LONG *Flag)
{
    DWORD Start = GetTickCount();

    while ((InterlockedCompareExchange(Flag, 0, 0) == 0) && ((GetTickCount() - Start) < 10000))
        SwitchToThread();
}

static DWORD WINAPI
ActiveWorkerThread(_Inout_ PVOID Parameter)
{
    ACTIVE_WORKER_CONTEXT *Context = Parameter;
    LPOVERLAPPED Overlapped;
    HANDLE Handles[2];
    LONG Compare = 0;
    DWORD Bytes;

    Context->GotPacket = GetQueuedCompletionStatus(Context->Port, &Bytes, &Context->Key, &Overlapped, 5000);
    SetEvent(Context->Packet);
    SpinUntilSet(&Context->Go);

    switch (Context->Kind)
    {
        case BlockSingle:
            Context->WaitResult = WaitForSingleObject(Context->Event, 600);
            break;
        case BlockMultiple:
            Handles[0] = Context->Event;
            Handles[1] = Context->Event2;
            Context->WaitResult = WaitForMultipleObjects(2, Handles, FALSE, 600);
            break;
        case BlockDelay:
            Sleep(600);
            Context->WaitResult = WAIT_TIMEOUT;
            break;
        case BlockAddress:
            Context->WaitResult = Context->WaitOnAddress(&Context->Address, &Compare, sizeof(Compare), 600) ?
                                  WAIT_OBJECT_0 : GetLastError();
            break;
        default:
            Context->WaitResult = SleepEx(5000, TRUE);
            break;
    }

    Context->Finished = InterlockedIncrement(Context->Order);
    SpinUntilSet(&Context->Release);
    return 0;
}

static DWORD WINAPI
BlockedWorkerThread(_Inout_ PVOID Parameter)
{
    BLOCKED_WORKER_CONTEXT *Context = Parameter;
    LPOVERLAPPED Overlapped;
    DWORD Bytes;

    Context->Success = GetQueuedCompletionStatus(Context->Port, &Bytes, &Context->Key, &Overlapped, 5000);
    Context->Finished = InterlockedIncrement(Context->Order);
    return 0;
}

static DWORD WINAPI
CrossPortWaitThread(_Inout_ PVOID Parameter)
{
    CROSS_PORT_CONTEXT *Context = Parameter;
    LPOVERLAPPED Overlapped;
    HANDLE Handles[2];
    ULONG_PTR Key;
    DWORD Bytes;

    Context->Primed = GetQueuedCompletionStatus(Context->OwnPort, &Bytes, &Key, &Overlapped, 5000);
    if (Context->PrimedEvent != NULL)
    {
        SetEvent(Context->PrimedEvent);
        WaitForSingleObject(Context->StartEvent, 5000);
    }
    InterlockedExchange(&Context->Started, 1);
    if (Context->Event == NULL)
    {
        Context->Result = WaitForSingleObject(Context->OtherPort, 5000);
    }
    else
    {
        Handles[0] = Context->Event;
        Handles[1] = Context->OtherPort;
        Context->Result = WaitForMultipleObjects(2, Handles, FALSE, 5000);
    }
    return 0;
}

static VOID
TestWorkerBlockActivatesQueue(_In_ WORKER_BLOCK_KIND Kind)
{
    ACTIVE_WORKER_CONTEXT Active;
    BLOCKED_WORKER_CONTEXT Blocked;
    HANDLE Threads[2] = { NULL, NULL };
    volatile LONG Order = 0;
    DWORD BlockedId = 0;
    DWORD Result;
    BOOL Interrupted = (Kind == BlockInterruptedAlertable);

    ZeroMemory(&Active, sizeof(Active));
    ZeroMemory(&Blocked, sizeof(Blocked));
    Active.Kind = Kind;
    Active.Order = &Order;
    Active.WaitResult = WAIT_FAILED;
    Active.WaitOnAddress = (PWAIT_ON_ADDRESS)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "WaitOnAddress");
    Blocked.Order = &Order;
    if ((Kind == BlockAddress) && (Active.WaitOnAddress == NULL))
    {
        skip("WaitOnAddress is not available\n");
        return;
    }

    Active.Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    Active.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Active.Event2 = CreateEventW(NULL, TRUE, FALSE, NULL);
    Active.Packet = CreateEventW(NULL, TRUE, FALSE, NULL);
    Blocked.Port = Active.Port;
    ok(Active.Port != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    ok(Active.Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Active.Event2 != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Active.Packet != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if ((Active.Port == NULL) || (Active.Event == NULL) || (Active.Event2 == NULL) || (Active.Packet == NULL))
        return;

    ok(PostQueuedCompletionStatus(Active.Port, 1, 0x5101, NULL), "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
    ok(PostQueuedCompletionStatus(Active.Port, 2, 0x5102, NULL), "PostQueuedCompletionStatus failed with %lu\n", GetLastError());

    Threads[0] = CreateThread(NULL, 0, ActiveWorkerThread, &Active, 0, NULL);
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Threads[0] != NULL)
    {
        Result = WaitForSingleObject(Active.Packet, 5000);
        ok(Result == WAIT_OBJECT_0, "The active worker did not dequeue its packet: %#lx\n", Result);
        Threads[1] = CreateThread(NULL, 0, BlockedWorkerThread, &Blocked, 0, &BlockedId);
        ok(Threads[1] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    if (Threads[1] != NULL)
    {
        ok(Active.GotPacket && (Active.Key == 0x5101), "The active worker got key %p (success %d)\n", (PVOID)Active.Key, Active.GotPacket);
        ok(WaitForThreadWaitState(BlockedId, NULL, WrQueue, 5000),
           "A second worker was not held back by a concurrency limit of 1 (state %lu, wait reason %lu)\n",
           LastThreadState, LastWaitReason);
        if (Interrupted)
            ok(QueueUserAPC(NopApc, Threads[0], 0), "QueueUserAPC failed with %lu\n", GetLastError());
        InterlockedExchange(&Active.Go, 1);

        if (Interrupted)
        {
            Result = WaitForSingleObject(Threads[1], 300);
            ok(Result == WAIT_TIMEOUT,
               "A worker that never blocked released another worker: %#lx\n", Result);
        }
        else
        {
            Result = WaitForSingleObject(Threads[1], 5000);
            ok(Result == WAIT_OBJECT_0, "Blocking in %s did not release the waiting worker\n", BlockNames[Kind]);
        }
    }

    InterlockedExchange(&Active.Go, 1);
    InterlockedExchange(&Active.Release, 1);
    if (Threads[0] != NULL)
    {
        Result = WaitForSingleObject(Threads[0], 10000);
        ok(Result == WAIT_OBJECT_0, "The active worker did not exit: %#lx\n", Result);
    }
    if (Threads[1] != NULL)
    {
        Result = WaitForSingleObject(Threads[1], 10000);
        ok(Result == WAIT_OBJECT_0, "The blocked worker did not exit: %#lx\n", Result);
        ok(Blocked.Success && (Blocked.Key == 0x5102), "The blocked worker got key %p (success %d)\n", (PVOID)Blocked.Key, Blocked.Success);
        if (Interrupted)
        {
            ok(Active.WaitResult == WAIT_IO_COMPLETION, "SleepEx returned %#lx instead of WAIT_IO_COMPLETION\n", Active.WaitResult);
            ok(Active.Finished < Blocked.Finished, "The second worker ran before the first one stopped (%ld, %ld)\n",
               Active.Finished, Blocked.Finished);
        }
        else
        {
            ok(Blocked.Finished < Active.Finished, "The second worker waited until %s returned (%ld, %ld)\n",
               BlockNames[Kind], Blocked.Finished, Active.Finished);
            ok(Active.WaitResult == ((Kind == BlockAddress) ? ERROR_TIMEOUT : WAIT_TIMEOUT),
               "%s returned %#lx\n", BlockNames[Kind], Active.WaitResult);
        }
    }

    if (Threads[1] != NULL) CloseHandle(Threads[1]);
    if (Threads[0] != NULL) CloseHandle(Threads[0]);
    CloseHandle(Active.Packet);
    CloseHandle(Active.Event2);
    CloseHandle(Active.Event);
    CloseHandle(Active.Port);
}

static VOID
TestCrossPortWaits(_In_ BOOL Multiple)
{
    CROSS_PORT_CONTEXT Contexts[2];
    HANDLE Ports[2] = { NULL, NULL };
    HANDLE Threads[2] = { NULL, NULL };
    HANDLE Primed[2] = { NULL, NULL };
    HANDLE StartEvent;
    HANDLE Event = NULL;
    LPOVERLAPPED Overlapped;
    ULONG_PTR Key;
    DWORD Result;
    DWORD Bytes;
    ULONG Index;

    if (Multiple)
    {
        Event = CreateEventW(NULL, TRUE, FALSE, NULL);
        ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    }
    StartEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(StartEvent != NULL, "CreateEventW failed with %lu\n", GetLastError());

    for (Index = 0; Index != 2; ++Index)
    {
        Primed[Index] = CreateEventW(NULL, TRUE, FALSE, NULL);
        ok(Primed[Index] != NULL, "CreateEventW failed with %lu\n", GetLastError());
        Ports[Index] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        ok(Ports[Index] != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
        if (Ports[Index] != NULL)
            ok(PostQueuedCompletionStatus(Ports[Index], 0, 0x6100 + Index, NULL), "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
    }

    for (Index = 0; (Index != 2) && (Ports[0] != NULL) && (Ports[1] != NULL); ++Index)
    {
        ZeroMemory(&Contexts[Index], sizeof(Contexts[Index]));
        Contexts[Index].OwnPort = Ports[Index];
        Contexts[Index].OtherPort = Ports[Index ^ 1];
        Contexts[Index].Event = Event;
        Contexts[Index].PrimedEvent = Primed[Index];
        Contexts[Index].StartEvent = StartEvent;
        Contexts[Index].Result = WAIT_FAILED;
        Threads[Index] = CreateThread(NULL, 0, CrossPortWaitThread, &Contexts[Index], 0, &Contexts[Index].ThreadId);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    if ((Threads[0] != NULL) && (Threads[1] != NULL))
    {
        Result = WaitForMultipleObjects(2, Primed, TRUE, 5000);
        ok(Result == WAIT_OBJECT_0, "Workers did not take their priming packets: %#lx\n", Result);
        SetEvent(StartEvent);
        for (Index = 0; Index != 2; ++Index)
        {
            ok(WaitForThreadWaitState(Contexts[Index].ThreadId, &Contexts[Index].Started, UserRequest, 5000),
               "Worker %lu did not block on the other port (state %lu, wait reason %lu)\n",
               Index, LastThreadState, LastWaitReason);
        }
        for (Index = 0; Index != 2; ++Index)
            ok(PostQueuedCompletionStatus(Ports[Index], 0, 0x6200 + Index, NULL), "PostQueuedCompletionStatus failed with %lu\n", GetLastError());

        Result = WaitForMultipleObjects(2, Threads, TRUE, 10000);
        ok(Result == WAIT_OBJECT_0, "Workers waiting on each other's port did not finish: %#lx\n", Result);
        for (Index = 0; Index != 2; ++Index)
        {
            ok(Contexts[Index].Primed, "Worker %lu did not get its priming packet\n", Index);
            ok(Contexts[Index].Result == (Multiple ? WAIT_OBJECT_0 + 1 : WAIT_OBJECT_0),
               "Worker %lu waiting on the other port returned %#lx\n", Index, Contexts[Index].Result);
            ok(GetQueuedCompletionStatus(Ports[Index], &Bytes, &Key, &Overlapped, 0) && (Key == 0x6200 + Index),
               "Port %lu lost the packet its observer saw (key %p)\n", Index, (PVOID)Key);
        }
    }

    for (Index = 0; Index != 2; ++Index)
    {
        if (Threads[Index] != NULL) CloseHandle(Threads[Index]);
        if (Ports[Index] != NULL) CloseHandle(Ports[Index]);
        if (Primed[Index] != NULL) CloseHandle(Primed[Index]);
    }
    if (StartEvent != NULL) CloseHandle(StartEvent);
    if (Event != NULL) CloseHandle(Event);
}

static VOID
TestPortClosedUnderBlockedWorker(VOID)
{
    CROSS_PORT_CONTEXT Context;
    HANDLE Thread;
    HANDLE Event;
    DWORD Result;

    ZeroMemory(&Context, sizeof(Context));
    Context.OwnPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Context.OtherPort = Event;
    Context.Result = WAIT_FAILED;
    ok(Context.OwnPort != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if ((Context.OwnPort == NULL) || (Event == NULL))
        return;

    ok(PostQueuedCompletionStatus(Context.OwnPort, 0, 0x6300, NULL), "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
    Thread = CreateThread(NULL, 0, CrossPortWaitThread, &Context, 0, &Context.ThreadId);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread != NULL)
    {
        ok(WaitForThreadWaitState(Context.ThreadId, &Context.Started, UserRequest, 5000),
           "The associated worker did not block on its event (state %lu, wait reason %lu)\n",
           LastThreadState, LastWaitReason);
        CloseHandle(Context.OwnPort);
        Context.OwnPort = NULL;
        ok(SetEvent(Event), "SetEvent failed with %lu\n", GetLastError());
        Result = WaitForSingleObject(Thread, 5000);
        ok(Result == WAIT_OBJECT_0, "The worker of a closed port did not finish: %#lx\n", Result);
        ok(Context.Primed, "The worker did not get its priming packet\n");
        ok(Context.Result == WAIT_OBJECT_0, "The worker of a closed port returned %#lx\n", Context.Result);
        CloseHandle(Thread);
    }

    if (Context.OwnPort != NULL) CloseHandle(Context.OwnPort);
    CloseHandle(Event);
}

START_TEST(IoCompletion)
{
    DEQUEUE_WAIT_CONTEXT DequeueContext;
    DIRECT_WAIT_CONTEXT DirectContexts[2];
    OWN_QUEUE_WAIT_CONTEXT OwnContext;
    HANDLE ReadyHandles[2];
    HANDLE Threads[2];
    LPOVERLAPPED Overlapped;
    HANDLE Handles[2];
    HANDLE MixedPort;
    HANDLE OwnPort;
    ULONG_PTR Key;
    HANDLE Event;
    HANDLE Port;
    DWORD Bytes;
    DWORD Result;
    ULONG Index;
    BOOL Success;

    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(Port != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    if (!Port)
        return;

    Result = WaitForSingleObject(Port, 0);
    ok(Result == WAIT_TIMEOUT, "An empty completion port returned %#lx instead of WAIT_TIMEOUT\n", Result);

    Success = PostQueuedCompletionStatus(Port, 7, 0x1234, (LPOVERLAPPED)(ULONG_PTR)0x5678);
    ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
    Result = WaitForSingleObject(Port, 0);
    ok(Result == WAIT_OBJECT_0, "A queued completion port returned %#lx instead of WAIT_OBJECT_0\n", Result);

    Bytes = 0;
    Key = 0;
    Overlapped = NULL;
    Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
    ok(Success, "GetQueuedCompletionStatus failed with %lu\n", GetLastError());
    ok(Bytes == 7, "Expected 7 transferred bytes, got %lu\n", Bytes);
    ok(Key == 0x1234, "Expected completion key 0x1234, got %p\n", (PVOID)Key);
    ok(Overlapped == (LPOVERLAPPED)(ULONG_PTR)0x5678, "Expected OVERLAPPED 0x5678, got %p\n", Overlapped);
    Result = WaitForSingleObject(Port, 0);
    ok(Result == WAIT_TIMEOUT, "A drained completion port returned %#lx instead of WAIT_TIMEOUT\n", Result);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (Event)
    {
        Handles[0] = Event;
        Handles[1] = Port;
        Result = WaitForMultipleObjects(2, Handles, FALSE, 0);
        ok(Result == WAIT_TIMEOUT, "An empty completion port in a multiple wait returned %#lx instead of WAIT_TIMEOUT\n", Result);
        Success = PostQueuedCompletionStatus(Port, 11, 0x4321, (LPOVERLAPPED)(ULONG_PTR)0x8765);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Handles, FALSE, 0);
        ok(Result == WAIT_OBJECT_0 + 1, "A queued completion port in a multiple wait returned %#lx instead of %#x\n", Result, WAIT_OBJECT_0 + 1);
        Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
        ok(Success, "GetQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Handles, FALSE, 0);
        ok(Result == WAIT_TIMEOUT, "A drained completion port in a multiple wait returned %#lx instead of WAIT_TIMEOUT\n", Result);
        CloseHandle(Event);
    }

    for (Index = 0; Index != 2; ++Index)
    {
        DirectContexts[Index].Port = Port;
        DirectContexts[Index].Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
        DirectContexts[Index].Result = WAIT_FAILED;
        DirectContexts[Index].Timeout = 5000;
        ReadyHandles[Index] = DirectContexts[Index].Ready;
        Threads[Index] = CreateThread(NULL, 0, DirectWaitThread, &DirectContexts[Index], 0, NULL);
        ok(ReadyHandles[Index] != NULL, "CreateEventW failed with %lu\n", GetLastError());
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }
    if (ReadyHandles[0] && ReadyHandles[1] && Threads[0] && Threads[1])
    {
        Result = WaitForMultipleObjects(2, ReadyHandles, TRUE, 5000);
        ok(Result == WAIT_OBJECT_0, "Direct waiters did not become ready: %#lx\n", Result);
        Sleep(100);
        Success = PostQueuedCompletionStatus(Port, 21, 0x2001, (LPOVERLAPPED)(ULONG_PTR)0x2002);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Threads, TRUE, 5000);
        ok(Result == WAIT_OBJECT_0, "Direct waiters did not exit: %#lx\n", Result);
        ok(DirectContexts[0].Result == WAIT_OBJECT_0, "First direct waiter returned %#lx\n", DirectContexts[0].Result);
        ok(DirectContexts[1].Result == WAIT_OBJECT_0, "Second direct waiter returned %#lx\n", DirectContexts[1].Result);
        Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
        ok(Success, "GetQueuedCompletionStatus failed with %lu\n", GetLastError());
        ok(Key == 0x2001, "Expected completion key 0x2001, got %p\n", (PVOID)Key);
    }
    for (Index = 0; Index != 2; ++Index)
    {
        if (Threads[Index]) CloseHandle(Threads[Index]);
        if (ReadyHandles[Index]) CloseHandle(ReadyHandles[Index]);
    }

    MixedPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    ok(MixedPort != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    DequeueContext.Port = MixedPort;
    DequeueContext.Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    DequeueContext.Success = FALSE;
    DequeueContext.Bytes = 0;
    DequeueContext.Key = 0;
    DequeueContext.Overlapped = NULL;
    Threads[0] = CreateThread(NULL, 0, DequeueWaitThread, &DequeueContext, 0, NULL);
    DirectContexts[0].Port = MixedPort;
    DirectContexts[0].Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    DirectContexts[0].Result = WAIT_FAILED;
    DirectContexts[0].Timeout = 250;
    Threads[1] = NULL;
    if (MixedPort && DequeueContext.Ready && Threads[0])
    {
        Result = WaitForSingleObject(DequeueContext.Ready, 5000);
        ok(Result == WAIT_OBJECT_0, "Dequeue waiter did not become ready: %#lx\n", Result);
        Sleep(100);
        Threads[1] = CreateThread(NULL, 0, DirectWaitThread, &DirectContexts[0], 0, NULL);
    }
    ok(DequeueContext.Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(DirectContexts[0].Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    ok(Threads[1] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (DirectContexts[0].Ready && Threads[1])
    {
        Result = WaitForSingleObject(DirectContexts[0].Ready, 5000);
        ok(Result == WAIT_OBJECT_0, "Direct waiter did not become ready: %#lx\n", Result);
        Sleep(100);
        Success = PostQueuedCompletionStatus(MixedPort, 31, 0x3001, (LPOVERLAPPED)(ULONG_PTR)0x3002);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Threads, TRUE, 5000);
        ok(Result == WAIT_OBJECT_0, "Mixed waiters did not exit: %#lx\n", Result);
        ok(DequeueContext.Success, "GetQueuedCompletionStatus failed in dequeue waiter\n");
        ok(DequeueContext.Key == 0x3001, "Expected completion key 0x3001, got %p\n", (PVOID)DequeueContext.Key);
        ok(DirectContexts[0].Result == WAIT_TIMEOUT, "Direct waiter returned %#lx instead of WAIT_TIMEOUT\n", DirectContexts[0].Result);
    }
    if (Threads[0]) CloseHandle(Threads[0]);
    if (Threads[1]) CloseHandle(Threads[1]);
    if (DequeueContext.Ready) CloseHandle(DequeueContext.Ready);
    if (DirectContexts[0].Ready) CloseHandle(DirectContexts[0].Ready);
    if (MixedPort) CloseHandle(MixedPort);

    MixedPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(MixedPort != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    DequeueContext.Port = MixedPort;
    DequeueContext.Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    DequeueContext.Success = TRUE;
    DequeueContext.Bytes = 0x55555555;
    DequeueContext.Key = 0x55555555;
    DequeueContext.Overlapped = (LPOVERLAPPED)(ULONG_PTR)0x55555555;
    DequeueContext.Error = ERROR_SUCCESS;
    Threads[0] = CreateThread(NULL, 0, DequeueWaitThread, &DequeueContext, 0, NULL);
    ok(DequeueContext.Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (MixedPort && DequeueContext.Ready && Threads[0])
    {
        Result = WaitForSingleObject(DequeueContext.Ready, 5000);
        ok(Result == WAIT_OBJECT_0, "Close waiter did not become ready: %#lx\n", Result);
        Sleep(100);
        CloseHandle(MixedPort);
        MixedPort = NULL;
        Result = WaitForSingleObject(Threads[0], 5000);
        ok(Result == WAIT_OBJECT_0, "Close waiter did not exit: %#lx\n", Result);
        ok(!DequeueContext.Success, "GetQueuedCompletionStatus succeeded after the port closed\n");
        ok(DequeueContext.Error == ERROR_ABANDONED_WAIT_0, "Expected ERROR_ABANDONED_WAIT_0, got %lu\n", DequeueContext.Error);
        ok(DequeueContext.Bytes == 0x55555555, "Bytes changed to %#lx\n", DequeueContext.Bytes);
        ok(DequeueContext.Key == 0x55555555, "Key changed to %#Ix\n", (size_t)DequeueContext.Key);
        ok(DequeueContext.Overlapped == NULL, "Overlapped changed to %p\n", DequeueContext.Overlapped);
    }
    if (Threads[0]) CloseHandle(Threads[0]);
    if (DequeueContext.Ready) CloseHandle(DequeueContext.Ready);
    if (MixedPort) CloseHandle(MixedPort);

    OwnPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    OwnContext.Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    OwnContext.Event = Event;
    OwnContext.Port = OwnPort;
    OwnContext.PrimeSuccess = FALSE;
    OwnContext.RemoveSuccess = FALSE;
    OwnContext.Result = WAIT_FAILED;
    OwnContext.PrimeBytes = 0;
    OwnContext.RemoveBytes = 0;
    OwnContext.PrimeKey = 0;
    OwnContext.RemoveKey = 0;
    OwnContext.PrimeOverlapped = NULL;
    OwnContext.RemoveOverlapped = NULL;
    Threads[0] = NULL;
    ok(OwnPort != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(OwnContext.Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (OwnPort && Event && OwnContext.Ready)
    {
        Success = PostQueuedCompletionStatus(OwnPort, 41, 0x4001, (LPOVERLAPPED)(ULONG_PTR)0x4002);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Threads[0] = CreateThread(NULL, 0, OwnQueueMultipleWaitThread, &OwnContext, 0, NULL);
    }
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Threads[0])
    {
        Result = WaitForSingleObject(OwnContext.Ready, 5000);
        ok(Result == WAIT_OBJECT_0, "Associated waiter did not become ready: %#lx\n", Result);
        ok(OwnContext.PrimeSuccess, "GetQueuedCompletionStatus failed for priming packet\n");
        ok(OwnContext.PrimeKey == 0x4001, "Expected priming key 0x4001, got %p\n", (PVOID)OwnContext.PrimeKey);
        Sleep(100);
        Success = PostQueuedCompletionStatus(OwnPort, 42, 0x4003, (LPOVERLAPPED)(ULONG_PTR)0x4004);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForSingleObject(Threads[0], 5000);
        ok(Result == WAIT_OBJECT_0, "Associated waiter did not exit: %#lx\n", Result);
        ok(OwnContext.Result == WAIT_OBJECT_0 + 1, "Associated multiple wait returned %#lx\n", OwnContext.Result);
        ok(OwnContext.RemoveSuccess, "GetQueuedCompletionStatus failed for observed packet\n");
        ok(OwnContext.RemoveKey == 0x4003, "Expected completion key 0x4003, got %p\n", (PVOID)OwnContext.RemoveKey);
    }
    if (Threads[0]) CloseHandle(Threads[0]);
    if (OwnContext.Ready) CloseHandle(OwnContext.Ready);
    if (Event) CloseHandle(Event);
    if (OwnPort) CloseHandle(OwnPort);

    for (Index = 0; Index != 256; ++Index)
    {
        Result = WaitForSingleObject(Port, 0);
        if (Result != WAIT_TIMEOUT)
            break;
        Success = PostQueuedCompletionStatus(Port, Index, Index, (LPOVERLAPPED)(ULONG_PTR)(Index + 1));
        if (!Success)
            break;
        Result = WaitForSingleObject(Port, 0);
        if (Result != WAIT_OBJECT_0)
            break;
        Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
        if (!Success || Bytes != Index || Key != Index || Overlapped != (LPOVERLAPPED)(ULONG_PTR)(Index + 1))
            break;
    }
    ok(Index == 256, "Completion port state diverged at stress iteration %lu\n", Index);

    CloseHandle(Port);

    TestCrossPortWaits(FALSE);
    TestCrossPortWaits(TRUE);
    TestPortClosedUnderBlockedWorker();
    TestWorkerBlockActivatesQueue(BlockSingle);
    TestWorkerBlockActivatesQueue(BlockMultiple);
    TestWorkerBlockActivatesQueue(BlockDelay);
    TestWorkerBlockActivatesQueue(BlockAddress);
    TestWorkerBlockActivatesQueue(BlockInterruptedAlertable);
}
