/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests CreateDispatcherQueueController and Windows.System.DispatcherQueue
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_System
#include <windows.system.h>
#include <dispatcherqueue.h>
#include <weakreference.h>

typedef HRESULT (WINAPI *PCREATE_DISPATCHER_QUEUE_CONTROLLER)(DispatcherQueueOptions, PDISPATCHERQUEUECONTROLLER *);

static PCREATE_DISPATCHER_QUEUE_CONTROLLER pCreateDispatcherQueueController;

#define MAX_EVENTS 16

static struct
{
    LONG Count;
    char Order[MAX_EVENTS];
} Events;

static void
ResetEvents(void)
{
    memset(&Events, 0, sizeof(Events));
}

static void
RecordEvent(char Event)
{
    LONG Index = InterlockedIncrement(&Events.Count) - 1;

    if (Index < MAX_EVENTS - 1)
        Events.Order[Index] = Event;
}

struct handler
{
    union
    {
        IDispatcherQueueHandler Queue;
        ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs Starting;
        ITypedEventHandler_DispatcherQueue_IInspectable Completed;
        IAsyncActionCompletedHandler Action;
    };
    LONG Ref;
    char Event;
    DWORD ThreadId;
    IDispatcherQueue *Queue2;
    BOOL SameQueue;
    boolean HasAccess;
    APTTYPE ApartmentType;
    APTTYPEQUALIFIER Qualifier;
    HRESULT ApartmentResult;
    HANDLE Done;
};

static HRESULT WINAPI
Handler_QueryInterface(IUnknown *Iface, REFIID Iid, void **Out)
{
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IAgileObject) ||
        IsEqualGUID(Iid, &IID_IDispatcherQueueHandler) ||
        IsEqualGUID(Iid, &IID_ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs) ||
        IsEqualGUID(Iid, &IID_ITypedEventHandler_DispatcherQueue_IInspectable) ||
        IsEqualGUID(Iid, &IID_IAsyncActionCompletedHandler))
    {
        *Out = Iface;
        IUnknown_AddRef(Iface);
        return S_OK;
    }
    *Out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
Handler_AddRef(IUnknown *Iface)
{
    struct handler *Handler = CONTAINING_RECORD(Iface, struct handler, Queue);
    return InterlockedIncrement(&Handler->Ref);
}

static ULONG WINAPI
Handler_Release(IUnknown *Iface)
{
    struct handler *Handler = CONTAINING_RECORD(Iface, struct handler, Queue);
    return InterlockedDecrement(&Handler->Ref);
}

static void
Handler_Run(struct handler *Handler)
{
    IDispatcherQueueStatics *Statics;
    IDispatcherQueue2 *Queue2;
    IDispatcherQueue *Current = NULL;

    Handler->ThreadId = GetCurrentThreadId();
    Handler->ApartmentResult = CoGetApartmentType(&Handler->ApartmentType, &Handler->Qualifier);
    if (Handler->Queue2)
    {
        if (SUCCEEDED(GetFactory(RuntimeClass_Windows_System_DispatcherQueue, &IID_IDispatcherQueueStatics,
                                 (void **)&Statics)))
        {
            if (SUCCEEDED(IDispatcherQueueStatics_GetForCurrentThread(Statics, &Current)) && Current)
            {
                Handler->SameQueue = Current == Handler->Queue2;
                IDispatcherQueue_Release(Current);
            }
            IDispatcherQueueStatics_Release(Statics);
        }
        if (SUCCEEDED(IDispatcherQueue_QueryInterface(Handler->Queue2, &IID_IDispatcherQueue2, (void **)&Queue2)))
        {
            IDispatcherQueue2_get_HasThreadAccess(Queue2, &Handler->HasAccess);
            IDispatcherQueue2_Release(Queue2);
        }
    }
    RecordEvent(Handler->Event);
    if (Handler->Done)
        SetEvent(Handler->Done);
}

static HRESULT WINAPI
QueueHandler_Invoke(IDispatcherQueueHandler *Iface)
{
    Handler_Run(CONTAINING_RECORD(Iface, struct handler, Queue));
    return S_OK;
}

static HRESULT WINAPI
StartingHandler_Invoke(ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs *Iface,
                       IDispatcherQueue *Sender, IDispatcherQueueShutdownStartingEventArgs *Args)
{
    struct handler *Handler = CONTAINING_RECORD(Iface, struct handler, Starting);

    Handler->SameQueue = Sender == Handler->Queue2;
    ok(Args != NULL, "ShutdownStarting args are NULL\n");
    Handler_Run(Handler);
    return S_OK;
}

static HRESULT WINAPI
CompletedHandler_Invoke(ITypedEventHandler_DispatcherQueue_IInspectable *Iface, IDispatcherQueue *Sender,
                        IInspectable *Args)
{
    struct handler *Handler = CONTAINING_RECORD(Iface, struct handler, Completed);

    Handler->SameQueue = Sender == Handler->Queue2;
    ok(Args == NULL, "ShutdownCompleted args %p\n", Args);
    Handler_Run(Handler);
    return S_OK;
}

static HRESULT WINAPI
ActionHandler_Invoke(IAsyncActionCompletedHandler *Iface, IAsyncAction *Action, AsyncStatus Status)
{
    struct handler *Handler = CONTAINING_RECORD(Iface, struct handler, Action);

    ok(Status == Completed, "Shutdown action status %d\n", Status);
    Handler_Run(Handler);
    return S_OK;
}

static const IDispatcherQueueHandlerVtbl QueueHandlerVtbl =
{
    (void *)Handler_QueryInterface, (void *)Handler_AddRef, (void *)Handler_Release, QueueHandler_Invoke,
};

static const ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgsVtbl StartingHandlerVtbl =
{
    (void *)Handler_QueryInterface, (void *)Handler_AddRef, (void *)Handler_Release, StartingHandler_Invoke,
};

static const ITypedEventHandler_DispatcherQueue_IInspectableVtbl CompletedHandlerVtbl =
{
    (void *)Handler_QueryInterface, (void *)Handler_AddRef, (void *)Handler_Release, CompletedHandler_Invoke,
};

static const IAsyncActionCompletedHandlerVtbl ActionHandlerVtbl =
{
    (void *)Handler_QueryInterface, (void *)Handler_AddRef, (void *)Handler_Release, ActionHandler_Invoke,
};

static void
InitHandler(struct handler *Handler, const void *Vtbl, char Event, IDispatcherQueue *Queue)
{
    memset(Handler, 0, sizeof(*Handler));
    Handler->Queue.lpVtbl = (void *)Vtbl;
    Handler->Ref = 1;
    Handler->Event = Event;
    Handler->Queue2 = Queue;
    Handler->ApartmentResult = E_FAIL;
}

static void
PumpUntil(LONG Count, DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Msg;

    while (Events.Count < Count && GetTickCount() - Start < Milliseconds)
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        if (Events.Count < Count)
            MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
    }
}

static void
CheckClassName(IInspectable *Object, const WCHAR *Expected)
{
    HSTRING Name = NULL;
    HRESULT hr = IInspectable_GetRuntimeClassName(Object, &Name);

    ok(hr == S_OK, "GetRuntimeClassName returned %#lx\n", hr);
    ok(!wcscmp(WindowsGetStringRawBuffer(Name, NULL), Expected), "Class name %ls, expected %ls\n",
       WindowsGetStringRawBuffer(Name, NULL), Expected);
    WindowsDeleteString(Name);
}

static void
CheckInterfaces(IUnknown *Object, const char *Name, BOOL Closable)
{
    static const struct
    {
        const IID *Iid;
        const char *Name;
    } Interfaces[] =
    {
        { &IID_IInspectable, "IInspectable" },
        { &IID_IAgileObject, "IAgileObject" },
        { &IID_IMarshal, "IMarshal" },
        { &IID_IWeakReferenceSource, "IWeakReferenceSource" },
    };
    IUnknown *Unknown;
    HRESULT hr;
    UINT i;

    for (i = 0; i < ARRAYSIZE(Interfaces); i++)
    {
        hr = IUnknown_QueryInterface(Object, Interfaces[i].Iid, (void **)&Unknown);
        ok(hr == S_OK, "%s %s returned %#lx\n", Name, Interfaces[i].Name, hr);
        if (SUCCEEDED(hr))
            IUnknown_Release(Unknown);
    }
    hr = IUnknown_QueryInterface(Object, &IID_IClosable, (void **)&Unknown);
    ok(hr == (Closable ? S_OK : E_NOINTERFACE), "%s IClosable returned %#lx\n", Name, hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(Unknown);
}

static IDispatcherQueue *
GetCurrentQueue(void)
{
    IDispatcherQueueStatics *Statics;
    IDispatcherQueue *Queue = (void *)0xdeadbeef;
    HRESULT hr;

    hr = GetFactory(RuntimeClass_Windows_System_DispatcherQueue, &IID_IDispatcherQueueStatics, (void **)&Statics);
    ok(hr == S_OK, "DispatcherQueue statics returned %#lx\n", hr);
    if (FAILED(hr))
        return NULL;
    hr = IDispatcherQueueStatics_GetForCurrentThread(Statics, &Queue);
    ok(hr == S_OK, "GetForCurrentThread returned %#lx\n", hr);
    IDispatcherQueueStatics_Release(Statics);
    return SUCCEEDED(hr) ? Queue : NULL;
}

static HRESULT
CreateController(DWORD Size, DWORD ThreadType, DWORD ApartmentType, IDispatcherQueueController **Controller)
{
    DispatcherQueueOptions Options;

    Options.dwSize = Size;
    Options.threadType = ThreadType;
    Options.apartmentType = ApartmentType;
    *Controller = (void *)0xdeadbeef;
    return pCreateDispatcherQueueController(Options, Controller);
}

static void
TestOptions(void)
{
    static const struct
    {
        DWORD Size;
        DWORD ThreadType;
        DWORD ApartmentType;
        HRESULT Expected;
    } Tests[] =
    {
        { 0, DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE, E_INVALIDARG },
        { sizeof(DispatcherQueueOptions) - 1, DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE, E_INVALIDARG },
        { sizeof(DispatcherQueueOptions) + 4, DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE, E_INVALIDARG },
        { sizeof(DispatcherQueueOptions), 0, DQTAT_COM_NONE, E_INVALIDARG },
        { sizeof(DispatcherQueueOptions), 3, DQTAT_COM_NONE, E_INVALIDARG },
        { sizeof(DispatcherQueueOptions), DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE, E_INVALIDARG },
        { sizeof(DispatcherQueueOptions), DQTYPE_THREAD_DEDICATED, 3, E_INVALIDARG },
    };
    IDispatcherQueueController *Controller;
    HRESULT hr;
    UINT i;

    for (i = 0; i < ARRAYSIZE(Tests); i++)
    {
        hr = CreateController(Tests[i].Size, Tests[i].ThreadType, Tests[i].ApartmentType, &Controller);
        ok(hr == Tests[i].Expected, "%u: size %lu type %lu apartment %lu returned %#lx\n", i, Tests[i].Size,
           Tests[i].ThreadType, Tests[i].ApartmentType, hr);
        ok(Controller == (void *)0xdeadbeef || Controller == NULL, "%u: controller %p\n", i, Controller);
    }
}

static IDispatcherQueue *
GetQueue(IDispatcherQueueController *Controller)
{
    IDispatcherQueue *Queue = NULL;
    HRESULT hr = IDispatcherQueueController_get_DispatcherQueue(Controller, &Queue);

    ok(hr == S_OK, "get_DispatcherQueue returned %#lx\n", hr);
    ok(Queue != NULL, "DispatcherQueue is NULL\n");
    return Queue;
}

static boolean
HasThreadAccess(IDispatcherQueue *Queue)
{
    IDispatcherQueue2 *Queue2;
    boolean Access = 2;
    HRESULT hr;

    hr = IDispatcherQueue_QueryInterface(Queue, &IID_IDispatcherQueue2, (void **)&Queue2);
    ok(hr == S_OK, "IDispatcherQueue2 returned %#lx\n", hr);
    if (FAILED(hr))
        return Access;
    hr = IDispatcherQueue2_get_HasThreadAccess(Queue2, &Access);
    ok(hr == S_OK, "get_HasThreadAccess returned %#lx\n", hr);
    IDispatcherQueue2_Release(Queue2);
    return Access;
}

static DWORD WINAPI
AccessThread(void *Param)
{
    IDispatcherQueue *Queue = Param;
    IDispatcherQueue *Current;

    RoInitialize(RO_INIT_MULTITHREADED);
    ok(HasThreadAccess(Queue) == FALSE, "Other thread has access\n");
    Current = GetCurrentQueue();
    ok(Current == NULL, "Other thread queue %p\n", Current);
    if (Current)
        IDispatcherQueue_Release(Current);
    RoUninitialize();
    return 0;
}

static void
Shutdown(IDispatcherQueueController *Controller, IDispatcherQueue *Queue, BOOL Pump)
{
    struct handler Starting, Finished, Action, After;
    EventRegistrationToken StartingToken, CompletedToken;
    IAsyncAction *Operation = NULL, *Second = (void *)0xdeadbeef;
    IAsyncInfo *Info;
    AsyncStatus Status;
    boolean Queued = 2;
    HRESULT hr, Error;

    InitHandler(&Starting, &StartingHandlerVtbl, 'S', Queue);
    InitHandler(&Finished, &CompletedHandlerVtbl, 'C', Queue);
    InitHandler(&Action, &ActionHandlerVtbl, 'A', NULL);
    InitHandler(&After, &QueueHandlerVtbl, 'X', NULL);
    Action.Done = CreateEventW(NULL, TRUE, FALSE, NULL);

    hr = IDispatcherQueue_add_ShutdownStarting(Queue, &Starting.Starting, &StartingToken);
    ok(hr == S_OK, "add_ShutdownStarting returned %#lx\n", hr);
    hr = IDispatcherQueue_add_ShutdownCompleted(Queue, &Finished.Completed, &CompletedToken);
    ok(hr == S_OK, "add_ShutdownCompleted returned %#lx\n", hr);

    ResetEvents();
    hr = IDispatcherQueueController_ShutdownQueueAsync(Controller, &Operation);
    ok(hr == S_OK, "ShutdownQueueAsync returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    CheckInterfaces((IUnknown *)Operation, "Shutdown action", FALSE);
    hr = IAsyncAction_put_Completed(Operation, &Action.Action);
    ok(hr == S_OK, "put_Completed returned %#lx\n", hr);

    if (Pump)
    {
        ok(Events.Count == 0, "%ld events before pumping: %s\n", Events.Count, Events.Order);
        PumpUntil(3, 5000);
    }
    else
    {
        ok(WaitForSingleObject(Action.Done, 5000) == WAIT_OBJECT_0, "Shutdown did not complete\n");
    }
    ok(!strcmp(Events.Order, "SCA"), "Shutdown order %s\n", Events.Order);
    ok(Starting.SameQueue, "ShutdownStarting sender is not the queue\n");
    ok(Finished.SameQueue, "ShutdownCompleted sender is not the queue\n");
    if (Pump)
    {
        ok(Starting.ThreadId == GetCurrentThreadId(), "ShutdownStarting ran on thread %lx\n", Starting.ThreadId);
        ok(Action.ThreadId == GetCurrentThreadId(), "Shutdown action ran on thread %lx\n", Action.ThreadId);
    }
    else
    {
        ok(Starting.ThreadId != GetCurrentThreadId(), "ShutdownStarting ran on the caller\n");
        ok(Finished.ThreadId == Starting.ThreadId, "ShutdownCompleted ran on thread %lx, starting on %lx\n",
           Finished.ThreadId, Starting.ThreadId);
    }

    hr = IAsyncAction_QueryInterface(Operation, &IID_IAsyncInfo, (void **)&Info);
    ok(hr == S_OK, "IAsyncInfo returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Status = Started;
        IAsyncInfo_get_Status(Info, &Status);
        ok(Status == Completed, "Shutdown status %d\n", Status);
        Error = E_FAIL;
        IAsyncInfo_get_ErrorCode(Info, &Error);
        ok(Error == S_OK, "Shutdown error %#lx\n", Error);
        IAsyncInfo_Release(Info);
    }

    hr = IDispatcherQueue_TryEnqueue(Queue, &After.Queue, &Queued);
    ok(hr == S_OK, "TryEnqueue after shutdown returned %#lx\n", hr);
    ok(Queued == FALSE, "TryEnqueue after shutdown queued %d\n", Queued);

    hr = IDispatcherQueueController_ShutdownQueueAsync(Controller, &Second);
    ok(hr == E_UNEXPECTED, "Second ShutdownQueueAsync returned %#lx\n", hr);
    ok(Second == (void *)0xdeadbeef || Second == NULL, "Second shutdown action %p\n", Second);
    if (SUCCEEDED(hr) && Second)
        IAsyncAction_Release(Second);
    IAsyncAction_Release(Operation);

    hr = IDispatcherQueue_remove_ShutdownStarting(Queue, StartingToken);
    ok(hr == S_OK, "remove_ShutdownStarting returned %#lx\n", hr);
    hr = IDispatcherQueue_remove_ShutdownCompleted(Queue, CompletedToken);
    ok(hr == S_OK, "remove_ShutdownCompleted returned %#lx\n", hr);
done:
    CloseHandle(Action.Done);
}

static DWORD WINAPI
CurrentThread(void *Param)
{
    IDispatcherQueueController *Controller, *Second;
    IDispatcherQueue *Queue, *Current;
    struct handler Low, Normal, High, Null;
    boolean Queued;
    HANDLE Thread;
    HRESULT hr;

    RoInitialize(RO_INIT_MULTITHREADED);

    Current = GetCurrentQueue();
    ok(Current == NULL, "Queue %p before creation\n", Current);

    hr = CreateController(sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, 3, &Controller);
    ok(hr == S_OK, "Current thread controller returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    CheckClassName((IInspectable *)Controller, RuntimeClass_Windows_System_DispatcherQueueController);
    CheckInterfaces((IUnknown *)Controller, "Controller", FALSE);

    hr = CreateController(sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE, &Second);
    ok(hr == RPC_E_WRONG_THREAD, "Second current thread controller returned %#lx\n", hr);
    ok(Second == (void *)0xdeadbeef || Second == NULL, "Second controller %p\n", Second);
    if (SUCCEEDED(hr) && Second)
        IDispatcherQueueController_Release(Second);

    Queue = GetQueue(Controller);
    if (!Queue)
        goto release;
    CheckClassName((IInspectable *)Queue, RuntimeClass_Windows_System_DispatcherQueue);
    CheckInterfaces((IUnknown *)Queue, "Queue", FALSE);

    Current = GetCurrentQueue();
    ok(Current == Queue, "Current queue %p, expected %p\n", Current, Queue);
    if (Current)
        IDispatcherQueue_Release(Current);

    ok(HasThreadAccess(Queue) == TRUE, "Creating thread has no access\n");
    Thread = CreateThread(NULL, 0, AccessThread, Queue, 0, NULL);
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);

    hr = IDispatcherQueue_TryEnqueue(Queue, NULL, &Queued);
    ok(hr == E_POINTER, "TryEnqueue NULL returned %#lx\n", hr);
    InitHandler(&Null, &QueueHandlerVtbl, 'N', NULL);
    hr = IDispatcherQueue_TryEnqueueWithPriority(Queue, 3, &Null.Queue, &Queued);
    ok(hr == E_INVALIDARG, "TryEnqueueWithPriority 3 returned %#lx\n", hr);

    ResetEvents();
    InitHandler(&Low, &QueueHandlerVtbl, 'L', Queue);
    InitHandler(&Normal, &QueueHandlerVtbl, 'N', Queue);
    InitHandler(&High, &QueueHandlerVtbl, 'H', Queue);
    Queued = 2;
    hr = IDispatcherQueue_TryEnqueueWithPriority(Queue, DispatcherQueuePriority_Low, &Low.Queue, &Queued);
    ok(hr == S_OK && Queued == TRUE, "Low TryEnqueue returned %#lx, %d\n", hr, Queued);
    Queued = 2;
    hr = IDispatcherQueue_TryEnqueue(Queue, &Normal.Queue, &Queued);
    ok(hr == S_OK && Queued == TRUE, "TryEnqueue returned %#lx, %d\n", hr, Queued);
    Queued = 2;
    hr = IDispatcherQueue_TryEnqueueWithPriority(Queue, DispatcherQueuePriority_High, &High.Queue, &Queued);
    ok(hr == S_OK && Queued == TRUE, "High TryEnqueue returned %#lx, %d\n", hr, Queued);
    ok(Low.Ref > 1 && Normal.Ref > 1 && High.Ref > 1, "Handler refs %ld %ld %ld\n", Low.Ref, Normal.Ref, High.Ref);
    ok(Events.Count == 0, "%ld handlers ran before pumping\n", Events.Count);

    PumpUntil(3, 5000);
    ok(!strcmp(Events.Order, "HNL"), "Handler order %s\n", Events.Order);
    ok(Normal.ThreadId == GetCurrentThreadId(), "Handler ran on thread %lx\n", Normal.ThreadId);
    ok(Normal.SameQueue, "GetForCurrentThread in the handler is not the queue\n");
    ok(Normal.HasAccess == TRUE, "Handler has no access\n");
    ok(Low.Ref == 1 && Normal.Ref == 1 && High.Ref == 1, "Handler refs after run %ld %ld %ld\n", Low.Ref,
       Normal.Ref, High.Ref);

    Shutdown(Controller, Queue, TRUE);

    Current = GetCurrentQueue();
    ok(Current == NULL, "Current queue after shutdown %p\n", Current);
    if (Current)
        IDispatcherQueue_Release(Current);

    IDispatcherQueue_Release(Queue);
release:
    IDispatcherQueueController_Release(Controller);
done:
    RoUninitialize();
    return 0;
}

static void
TestCurrentThread(void)
{
    HANDLE Thread = CreateThread(NULL, 0, CurrentThread, NULL, 0, NULL);

    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
}

static void
TestDedicatedThread(DWORD ApartmentType)
{
    IDispatcherQueueController *Controller;
    IDispatcherQueue *Queue;
    struct handler Handler;
    boolean Queued = 2;
    HRESULT hr;

    hr = CreateController(sizeof(DispatcherQueueOptions), DQTYPE_THREAD_DEDICATED, ApartmentType, &Controller);
    ok(hr == S_OK, "Dedicated controller %lu returned %#lx\n", ApartmentType, hr);
    if (FAILED(hr))
        return;
    Queue = GetQueue(Controller);
    if (!Queue)
    {
        IDispatcherQueueController_Release(Controller);
        return;
    }
    ok(HasThreadAccess(Queue) == FALSE, "Caller has access to the dedicated queue\n");

    ResetEvents();
    InitHandler(&Handler, &QueueHandlerVtbl, 'D', Queue);
    Handler.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    hr = IDispatcherQueue_TryEnqueue(Queue, &Handler.Queue, &Queued);
    ok(hr == S_OK && Queued == TRUE, "Dedicated TryEnqueue returned %#lx, %d\n", hr, Queued);
    ok(WaitForSingleObject(Handler.Done, 5000) == WAIT_OBJECT_0, "Dedicated handler did not run\n");
    ok(Handler.ThreadId && Handler.ThreadId != GetCurrentThreadId(), "Dedicated handler ran on thread %lx\n",
       Handler.ThreadId);
    ok(Handler.SameQueue, "GetForCurrentThread on the dedicated thread is not the queue\n");
    ok(Handler.HasAccess == TRUE, "Dedicated thread has no access\n");
    ok(Handler.ApartmentResult == S_OK, "%lu: CoGetApartmentType returned %#lx\n", ApartmentType,
       Handler.ApartmentResult);
    ok(Handler.ApartmentType == APTTYPE_MAINSTA, "%lu: apartment type %d\n", ApartmentType, Handler.ApartmentType);
    ok(Handler.Qualifier == APTTYPEQUALIFIER_NONE, "%lu: apartment qualifier %d\n", ApartmentType,
       Handler.Qualifier);
    CloseHandle(Handler.Done);

    Shutdown(Controller, Queue, FALSE);

    IDispatcherQueue_Release(Queue);
    IDispatcherQueueController_Release(Controller);
}

static void
TestStatics(void)
{
    IDispatcherQueueControllerStatics *Statics;
    IDispatcherQueueController *Controller = NULL;
    IDispatcherQueue *Queue;
    struct handler Handler;
    boolean Queued = 2;
    HRESULT hr;

    TraceActivatableClass(RuntimeClass_Windows_System_DispatcherQueue);
    TraceActivatableClass(RuntimeClass_Windows_System_DispatcherQueueController);

    hr = GetFactory(RuntimeClass_Windows_System_DispatcherQueueController, &IID_IDispatcherQueueControllerStatics,
                    (void **)&Statics);
    ok(hr == S_OK, "DispatcherQueueController statics returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IDispatcherQueueControllerStatics_CreateOnDedicatedThread(Statics, &Controller);
    ok(hr == S_OK, "CreateOnDedicatedThread returned %#lx\n", hr);
    IDispatcherQueueControllerStatics_Release(Statics);
    if (FAILED(hr))
        return;

    Queue = GetQueue(Controller);
    if (Queue)
    {
        InitHandler(&Handler, &QueueHandlerVtbl, 'D', Queue);
        Handler.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
        hr = IDispatcherQueue_TryEnqueue(Queue, &Handler.Queue, &Queued);
        ok(hr == S_OK && Queued == TRUE, "TryEnqueue returned %#lx, %d\n", hr, Queued);
        ok(WaitForSingleObject(Handler.Done, 5000) == WAIT_OBJECT_0, "Handler did not run\n");
        ok(Handler.ApartmentType == APTTYPE_MAINSTA, "Apartment type %d\n", Handler.ApartmentType);
        ok(Handler.Qualifier == APTTYPEQUALIFIER_NONE, "Apartment qualifier %d\n", Handler.Qualifier);
        CloseHandle(Handler.Done);
        Shutdown(Controller, Queue, FALSE);
        IDispatcherQueue_Release(Queue);
    }
    IDispatcherQueueController_Release(Controller);
}

START_TEST(DispatcherQueue)
{
    HMODULE Module = LoadLibraryW(L"coremessaging.dll");

    ok(Module != NULL, "coremessaging.dll failed to load: %lu\n", GetLastError());
    if (!Module)
        return;
    pCreateDispatcherQueueController = (void *)GetProcAddress(Module, "CreateDispatcherQueueController");
    ok(pCreateDispatcherQueueController != NULL, "CreateDispatcherQueueController is not exported\n");
    if (!pCreateDispatcherQueueController)
        return;

    RoInitialize(RO_INIT_MULTITHREADED);
    TestOptions();
    TestCurrentThread();
    TestDedicatedThread(DQTAT_COM_STA);
    TestDedicatedThread(DQTAT_COM_ASTA);
    TestStatics();
    RoUninitialize();
}
