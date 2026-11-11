/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which waiter a signal reaches when a wait-all is pending
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193.001@gmail.com>
 */

#include "precomp.h"
#include "threadwait.h"

#define WAIT_ALL_TIMEOUT_MS 96

typedef enum _SIGNAL_KIND
{
    SignalAutoEvent,
    SignalMutex,
    SignalSemaphore
} SIGNAL_KIND;

typedef struct _WAIT_CONTEXT
{
    HANDLE Handles[2];
    DWORD Count;
    BOOL WaitAll;
    DWORD Timeout;
    HANDLE Mutex;
    HANDLE Done;
    HANDLE Finish;
    DWORD ThreadId;
    volatile LONG Started;
    DWORD Result;
} WAIT_CONTEXT;

static PCSTR SignalNames[] = { "auto-reset event", "mutex", "semaphore" };

static DWORD WINAPI
WaitThread(_Inout_ PVOID Parameter)
{
    WAIT_CONTEXT *Context = Parameter;

    InterlockedExchange(&Context->Started, 1);
    Context->Result = WaitForMultipleObjects(Context->Count, Context->Handles, Context->WaitAll, Context->Timeout);
    if (Context->Done != NULL)
        SetEvent(Context->Done);
    if (Context->Finish != NULL)
        WaitForSingleObject(Context->Finish, 10000);
    if ((Context->Mutex != NULL) && (Context->Result == WAIT_OBJECT_0))
        ReleaseMutex(Context->Mutex);
    return 0;
}

static VOID
InitWait(
    _Out_ WAIT_CONTEXT *Context,
    _In_ HANDLE First,
    _In_opt_ HANDLE Second,
    _In_ BOOL WaitAll,
    _In_ DWORD Timeout,
    _In_opt_ HANDLE Mutex)
{
    ZeroMemory(Context, sizeof(*Context));
    Context->Handles[0] = First;
    Context->Handles[1] = Second;
    Context->Count = (Second != NULL) ? 2 : 1;
    Context->WaitAll = WaitAll;
    Context->Timeout = Timeout;
    Context->Mutex = Mutex;
    Context->Result = WAIT_FAILED;
}

static HANDLE
StartWait(_Inout_ WAIT_CONTEXT *Context)
{
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, WaitThread, Context, 0, &Context->ThreadId);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL)
        return NULL;

    ok(WaitForThreadWaitState(Context->ThreadId, &Context->Started, UserRequest, 5000),
       "Thread %lu did not block in its wait (state %lu, wait reason %lu)\n",
       Context->ThreadId, LastThreadState, LastWaitReason);
    return Thread;
}

static VOID
JoinWait(_In_opt_ HANDLE Thread)
{
    DWORD Result;

    if (Thread == NULL)
        return;

    Result = WaitForSingleObject(Thread, 10000);
    ok(Result == WAIT_OBJECT_0, "Wait thread did not exit: %#lx\n", Result);
    CloseHandle(Thread);
}

static HANDLE
CreateSignalObject(_In_ SIGNAL_KIND Kind)
{
    switch (Kind)
    {
        case SignalAutoEvent:
            return CreateEventW(NULL, FALSE, FALSE, NULL);
        case SignalMutex:
            return CreateMutexW(NULL, TRUE, NULL);
        default:
            return CreateSemaphoreW(NULL, 0, 1, NULL);
    }
}

static BOOL
Signal(_In_ SIGNAL_KIND Kind, _In_ HANDLE Object)
{
    switch (Kind)
    {
        case SignalAutoEvent:
            return SetEvent(Object);
        case SignalMutex:
            return ReleaseMutex(Object);
        default:
            return ReleaseSemaphore(Object, 1, NULL);
    }
}

static VOID
TestPulse(_In_ BOOL ManualReset)
{
    WAIT_CONTEXT Context;
    HANDLE Event, Mutex, Thread;
    DWORD Result;

    Event = CreateEventW(NULL, ManualReset, FALSE, NULL);
    Mutex = CreateMutexW(NULL, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Mutex != NULL, "CreateMutexW failed with %lu\n", GetLastError());
    if ((Event == NULL) || (Mutex == NULL))
        return;

    InitWait(&Context, Event, Mutex, TRUE, WAIT_ALL_TIMEOUT_MS, Mutex);
    Thread = StartWait(&Context);
    ok(PulseEvent(Event), "PulseEvent failed with %lu\n", GetLastError());
    JoinWait(Thread);

    ok(Context.Result == WAIT_TIMEOUT,
       "Pulsing a %s event satisfied a wait-all whose other object was signaled: %#lx\n",
       ManualReset ? "manual-reset" : "auto-reset", Context.Result);
    Result = WaitForSingleObject(Event, 0);
    ok(Result == WAIT_TIMEOUT, "The pulsed event stayed signaled: %#lx\n", Result);
    Result = WaitForSingleObject(Mutex, 0);
    ok(Result == WAIT_OBJECT_0, "The mutex was not free after the wait-all: %#lx\n", Result);
    if (Result == WAIT_OBJECT_0)
        ReleaseMutex(Mutex);

    CloseHandle(Mutex);
    CloseHandle(Event);
}

static VOID
TestPulseUnsatisfied(VOID)
{
    WAIT_CONTEXT Context;
    HANDLE Event, Mutex, Thread;
    DWORD Result;

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Mutex = CreateMutexW(NULL, TRUE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Mutex != NULL, "CreateMutexW failed with %lu\n", GetLastError());
    if ((Event == NULL) || (Mutex == NULL))
        return;

    InitWait(&Context, Event, Mutex, TRUE, WAIT_ALL_TIMEOUT_MS, Mutex);
    Thread = StartWait(&Context);
    ok(PulseEvent(Event), "PulseEvent failed with %lu\n", GetLastError());
    ok(ReleaseMutex(Mutex), "ReleaseMutex failed with %lu\n", GetLastError());
    JoinWait(Thread);

    ok(Context.Result == WAIT_TIMEOUT,
       "A pulse with an owned mutex satisfied the wait-all: %#lx\n", Context.Result);
    Result = WaitForSingleObject(Mutex, 0);
    ok(Result == WAIT_OBJECT_0, "The unsatisfied wait-all took the mutex: %#lx\n", Result);
    if (Result == WAIT_OBJECT_0)
        ReleaseMutex(Mutex);

    CloseHandle(Mutex);
    CloseHandle(Event);
}

static VOID
TestWaitAllQueuedFirst(_In_ SIGNAL_KIND Kind)
{
    WAIT_CONTEXT AllContext, AnyContext;
    HANDLE Object, Ready, Finish, AllThread, AnyThread;
    DWORD Result;

    Object = CreateSignalObject(Kind);
    Ready = CreateEventW(NULL, TRUE, TRUE, NULL);
    Finish = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Object != NULL, "Creating the %s failed with %lu\n", SignalNames[Kind], GetLastError());
    ok(Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Finish != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if ((Object == NULL) || (Ready == NULL) || (Finish == NULL))
        return;

    InitWait(&AllContext, Object, Ready, TRUE, WAIT_ALL_TIMEOUT_MS, (Kind == SignalMutex) ? Object : NULL);
    AllContext.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    AllContext.Finish = Finish;
    InitWait(&AnyContext, Object, NULL, FALSE, 2000, (Kind == SignalMutex) ? Object : NULL);
    AnyContext.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    AnyContext.Finish = Finish;

    AllThread = StartWait(&AllContext);
    AnyThread = StartWait(&AnyContext);
    ok(Signal(Kind, Object), "Signaling the %s failed with %lu\n", SignalNames[Kind], GetLastError());

    Result = WaitForSingleObject(AnyContext.Done, 5000);
    ok(Result == WAIT_OBJECT_0, "The wait-any waiter did not finish: %#lx\n", Result);
    Result = WaitForSingleObject(AllContext.Done, 5000);
    ok(Result == WAIT_OBJECT_0, "The wait-all waiter did not finish: %#lx\n", Result);
    ok(AnyContext.Result == WAIT_OBJECT_0,
       "The wait-any queued second did not get the %s: wait-all %#lx, wait-any %#lx\n",
       SignalNames[Kind], AllContext.Result, AnyContext.Result);
    ok(AllContext.Result == WAIT_TIMEOUT,
       "The wait-all queued first was satisfied by the %s: %#lx\n", SignalNames[Kind], AllContext.Result);

    SetEvent(Finish);
    JoinWait(AllThread);
    JoinWait(AnyThread);

    if (Kind != SignalMutex)
    {
        Result = WaitForSingleObject(Object, 0);
        ok(Result == WAIT_TIMEOUT, "The %s was signaled after one waiter took it: %#lx\n",
           SignalNames[Kind], Result);
    }

    CloseHandle(AnyContext.Done);
    CloseHandle(AllContext.Done);
    CloseHandle(Finish);
    CloseHandle(Ready);
    CloseHandle(Object);
}

static VOID
TestNotAllSignaled(_In_ SIGNAL_KIND Kind)
{
    WAIT_CONTEXT AllContext, AnyContext;
    HANDLE Object, Blocker, Finish, AllThread, AnyThread;
    DWORD Result;

    Object = CreateSignalObject(Kind);
    Blocker = CreateEventW(NULL, TRUE, FALSE, NULL);
    Finish = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Object != NULL, "Creating the %s failed with %lu\n", SignalNames[Kind], GetLastError());
    ok(Blocker != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Finish != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if ((Object == NULL) || (Blocker == NULL) || (Finish == NULL))
        return;

    InitWait(&AllContext, Object, Blocker, TRUE, WAIT_ALL_TIMEOUT_MS, (Kind == SignalMutex) ? Object : NULL);
    AllContext.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    AllContext.Finish = Finish;
    InitWait(&AnyContext, Object, NULL, FALSE, 2000, (Kind == SignalMutex) ? Object : NULL);
    AnyContext.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    AnyContext.Finish = Finish;

    AllThread = StartWait(&AllContext);
    AnyThread = StartWait(&AnyContext);
    ok(Signal(Kind, Object), "Signaling the %s failed with %lu\n", SignalNames[Kind], GetLastError());

    Result = WaitForSingleObject(AnyContext.Done, 5000);
    ok(Result == WAIT_OBJECT_0, "The wait-any waiter did not finish: %#lx\n", Result);
    Result = WaitForSingleObject(AllContext.Done, 5000);
    ok(Result == WAIT_OBJECT_0, "The wait-all waiter did not finish: %#lx\n", Result);
    ok(AnyContext.Result == WAIT_OBJECT_0,
       "The wait-any did not get the %s the unsatisfiable wait-all could not take: %#lx\n",
       SignalNames[Kind], AnyContext.Result);
    ok(AllContext.Result == WAIT_TIMEOUT,
       "A wait-all with an unsignaled object returned %#lx\n", AllContext.Result);

    SetEvent(Finish);
    JoinWait(AllThread);
    JoinWait(AnyThread);

    CloseHandle(AnyContext.Done);
    CloseHandle(AllContext.Done);
    CloseHandle(Finish);
    CloseHandle(Blocker);
    CloseHandle(Object);
}

START_TEST(WaitForMultipleObjects)
{
    SIGNAL_KIND Kind;

    TestPulse(TRUE);
    TestPulse(FALSE);
    TestPulseUnsatisfied();

    for (Kind = SignalAutoEvent; Kind <= SignalSemaphore; Kind++)
    {
        TestWaitAllQueuedFirst(Kind);
        TestNotAllSignaled(Kind);
    }
}
