/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Events, event pairs and keyed events as nameable objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ExEventObjectType = NULL;
POBJECT_TYPE ExEventPairObjectType = NULL;
POBJECT_TYPE ExKeyedEventObjectType = NULL;

PVOID ExpCritSecOutOfMemoryEvent = NULL;

GENERIC_MAPPING ExpEventMapping =
{
    STANDARD_RIGHTS_READ | EVENT_QUERY_STATE,
    STANDARD_RIGHTS_WRITE | EVENT_MODIFY_STATE,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    EVENT_ALL_ACCESS
};

GENERIC_MAPPING ExEventPairMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    EVENT_PAIR_ALL_ACCESS
};

/* PRIVATE DATA ***************************************************************/

#define EXP_KEYED_EVENT_HASH_SIZE 23
#define EXP_KEYED_EVENT_TAG 'veyK'

static GENERIC_MAPPING ExpKeyedEventMapping =
{
    STANDARD_RIGHTS_READ | KEYEDEVENT_WAIT,
    STANDARD_RIGHTS_WRITE | KEYEDEVENT_WAKE,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    KEYEDEVENT_ALL_ACCESS
};

/*
 * A keyed event is not one event but a meeting place: a thread arrives naming a
 * key, and leaves as soon as a thread of the same process arrives from the other
 * side naming the same key. Nothing is remembered once a pair has met.
 */
typedef struct _EXP_KEYED_EVENT
{
    EX_PUSH_LOCK Locks[EXP_KEYED_EVENT_HASH_SIZE];
    LIST_ENTRY Waiters[EXP_KEYED_EVENT_HASH_SIZE];
    LIST_ENTRY Releasers[EXP_KEYED_EVENT_HASH_SIZE];
} EXP_KEYED_EVENT, *PEXP_KEYED_EVENT;

typedef struct _EXP_KEYED_WAIT
{
    LIST_ENTRY Link;
    PVOID Key;
    PEPROCESS Process;
    KEVENT Event;
} EXP_KEYED_WAIT, *PEXP_KEYED_WAIT;

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
ExpKeyedEventBucket(
    _In_ PVOID Key)
{
    return (ULONG)(((ULONG_PTR)Key >> 1) % EXP_KEYED_EVENT_HASH_SIZE);
}

static
PEXP_KEYED_WAIT
ExpTakeMatchingWait(
    _In_ PLIST_ENTRY ListHead,
    _In_ PVOID Key,
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Entry;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        PEXP_KEYED_WAIT Wait = CONTAINING_RECORD(Entry, EXP_KEYED_WAIT, Link);

        if ((Wait->Key == Key) && (Wait->Process == Process))
        {
            RemoveEntryList(&Wait->Link);
            return Wait;
        }
    }

    return NULL;
}

/*
 * One half of the meeting. A thread looks for its counterpart on the other
 * side's list; finding none, it puts itself on its own side's list and waits.
 */
static
NTSTATUS
ExpMeetOnKeyedEvent(
    _In_ PEXP_KEYED_EVENT KeyedEvent,
    _In_ PVOID Key,
    _In_ BOOLEAN Releasing,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    ULONG Bucket = ExpKeyedEventBucket(Key);
    PEPROCESS Process = PsGetCurrentProcess();
    PLIST_ENTRY Own, Other;
    PEXP_KEYED_WAIT Partner;
    EXP_KEYED_WAIT Wait;
    NTSTATUS Status;

    if (Releasing)
    {
        Own = &KeyedEvent->Releasers[Bucket];
        Other = &KeyedEvent->Waiters[Bucket];
    }
    else
    {
        Own = &KeyedEvent->Waiters[Bucket];
        Other = &KeyedEvent->Releasers[Bucket];
    }

    ExAcquirePushLockExclusive(&KeyedEvent->Locks[Bucket]);

    Partner = ExpTakeMatchingWait(Other, Key, Process);
    if (Partner != NULL)
    {
        ExReleasePushLockExclusive(&KeyedEvent->Locks[Bucket]);
        KeSetEvent(&Partner->Event, EVENT_INCREMENT, FALSE);
        return STATUS_SUCCESS;
    }

    Wait.Key = Key;
    Wait.Process = Process;
    KeInitializeEvent(&Wait.Event, NotificationEvent, FALSE);
    InsertTailList(Own, &Wait.Link);

    ExReleasePushLockExclusive(&KeyedEvent->Locks[Bucket]);

    Status = KeWaitForSingleObject(&Wait.Event,
                                   WrKeyedEvent,
                                   KernelMode,
                                   Alertable,
                                   Timeout);
    if (Status == STATUS_SUCCESS)
    {
        return STATUS_SUCCESS;
    }

    /*
     * The wait ended on its own terms. The counterpart may have arrived in the
     * meantime, so the block is only abandoned if it is still listed.
     */
    ExAcquirePushLockExclusive(&KeyedEvent->Locks[Bucket]);

    if (ExpTakeMatchingWait(Own, Key, Process) == NULL)
    {
        ExReleasePushLockExclusive(&KeyedEvent->Locks[Bucket]);
        return STATUS_SUCCESS;
    }

    ExReleasePushLockExclusive(&KeyedEvent->Locks[Bucket]);

    return Status;
}

static
NTSTATUS
ExpOpenKeyedEventHandle(
    _In_opt_ HANDLE Handle,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PEXP_KEYED_EVENT *KeyedEvent,
    _Out_ PVOID *Referenced)
{
    NTSTATUS Status;

    *Referenced = NULL;

    if (Handle == NULL)
    {
        *KeyedEvent = ExpCritSecOutOfMemoryEvent;
        return (*KeyedEvent != NULL) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
    }

    Status = ObReferenceObjectByHandle(Handle,
                                       DesiredAccess,
                                       ExKeyedEventObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)KeyedEvent,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *Referenced = *KeyedEvent;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeEventImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Event");

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = EVENT_ALL_ACCESS;
    Initializer.GenericMapping = ExpEventMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(KEVENT);

    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
                                       NULL,
                                       &ExEventObjectType)))
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
NTAPI
NtCreateEvent(
    _Out_ PHANDLE EventHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ EVENT_TYPE EventType,
    _In_ BOOLEAN InitialState)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKEVENT Event;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if ((EventType != NotificationEvent) && (EventType != SynchronizationEvent))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(EventHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            ExEventObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KEVENT),
                            0,
                            0,
                            (PVOID *)&Event);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeInitializeEvent(Event, EventType, InitialState);

    Status = ObInsertObject(Event,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *EventHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *EventHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtOpenEvent(
    _Out_ PHANDLE EventHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(EventHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ExEventObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *EventHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *EventHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtSetEvent(
    _In_ HANDLE EventHandle,
    _Out_opt_ PLONG PreviousState)
{
    PKEVENT Event;
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EventHandle,
                                       EVENT_MODIFY_STATE,
                                       ExEventObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    State = KeSetEvent(Event, EVENT_INCREMENT, FALSE);

    ObDereferenceObject(Event);

    if (PreviousState == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (ExGetPreviousMode() != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteLong(PreviousState);
            *PreviousState = State;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *PreviousState = State;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtSetEventBoostPriority(
    _In_ HANDLE EventHandle)
{
    PKEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EventHandle,
                                       EVENT_MODIFY_STATE,
                                       ExEventObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeSetEventBoostPriority(Event, NULL);

    ObDereferenceObject(Event);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtClearEvent(
    _In_ HANDLE EventHandle)
{
    PKEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EventHandle,
                                       EVENT_MODIFY_STATE,
                                       ExEventObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeClearEvent(Event);

    ObDereferenceObject(Event);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtResetEvent(
    _In_ HANDLE EventHandle,
    _Out_opt_ PLONG PreviousState)
{
    PKEVENT Event;
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EventHandle,
                                       EVENT_MODIFY_STATE,
                                       ExEventObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    State = KeResetEvent(Event);

    ObDereferenceObject(Event);

    if (PreviousState == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (ExGetPreviousMode() != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteLong(PreviousState);
            *PreviousState = State;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *PreviousState = State;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtPulseEvent(
    _In_ HANDLE EventHandle,
    _Out_opt_ PLONG PreviousState)
{
    PKEVENT Event;
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EventHandle,
                                       EVENT_MODIFY_STATE,
                                       ExEventObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    State = KePulseEvent(Event, EVENT_INCREMENT, FALSE);

    ObDereferenceObject(Event);

    if (PreviousState == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (ExGetPreviousMode() != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteLong(PreviousState);
            *PreviousState = State;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *PreviousState = State;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryEvent(
    _In_ HANDLE EventHandle,
    _In_ EVENT_INFORMATION_CLASS EventInformationClass,
    _Out_ PVOID EventInformation,
    _In_ ULONG EventInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKEVENT Event;
    EVENT_BASIC_INFORMATION Basic;
    NTSTATUS Status;

    PAGED_CODE();

    if (EventInformationClass != EventBasicInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (EventInformationLength < sizeof(EVENT_BASIC_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(EventInformation,
                          sizeof(EVENT_BASIC_INFORMATION),
                          sizeof(ULONG));
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(EventHandle,
                                       EVENT_QUERY_STATE,
                                       ExEventObjectType,
                                       PreviousMode,
                                       (PVOID *)&Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Basic.EventType = (Event->Header.Type == EventNotificationObject) ?
                      NotificationEvent : SynchronizationEvent;
    Basic.EventState = KeReadStateEvent(Event);

    ObDereferenceObject(Event);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *(PEVENT_BASIC_INFORMATION)EventInformation = Basic;
            if (ReturnLength != NULL)
            {
                *ReturnLength = sizeof(EVENT_BASIC_INFORMATION);
            }
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *(PEVENT_BASIC_INFORMATION)EventInformation = Basic;
    if (ReturnLength != NULL)
    {
        *ReturnLength = sizeof(EVENT_BASIC_INFORMATION);
    }

    return STATUS_SUCCESS;
}

/* EVENT PAIRS ****************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeEventPairImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"EventPair");

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = EVENT_PAIR_ALL_ACCESS;
    Initializer.GenericMapping = ExEventPairMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(KEVENT_PAIR);

    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
                                       NULL,
                                       &ExEventPairObjectType)))
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
NTAPI
NtCreateEventPair(
    _Out_ PHANDLE EventPairHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKEVENT_PAIR EventPair;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(EventPairHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            ExEventPairObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KEVENT_PAIR),
                            0,
                            0,
                            (PVOID *)&EventPair);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeInitializeEvent(&EventPair->LowEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&EventPair->HighEvent, SynchronizationEvent, FALSE);

    Status = ObInsertObject(EventPair,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *EventPairHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *EventPairHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtOpenEventPair(
    _Out_ PHANDLE EventPairHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(EventPairHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ExEventPairObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *EventPairHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *EventPairHandle = Handle;
    return Status;
}

static
NTSTATUS
ExpOperateOnEventPair(
    _In_ HANDLE EventPairHandle,
    _In_ BOOLEAN SetHigh,
    _In_ BOOLEAN Wait)
{
    PKEVENT_PAIR EventPair;
    PKEVENT Set, WaitOn;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EventPairHandle,
                                       SYNCHRONIZE,
                                       ExEventPairObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&EventPair,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Set = SetHigh ? &EventPair->HighEvent : &EventPair->LowEvent;
    WaitOn = SetHigh ? &EventPair->LowEvent : &EventPair->HighEvent;

    if (!Wait)
    {
        KeSetEvent(Set, EVENT_INCREMENT, FALSE);
        ObDereferenceObject(EventPair);
        return STATUS_SUCCESS;
    }

    Status = KeSetEvent(Set, EVENT_INCREMENT, TRUE);
    Status = KeWaitForSingleObject(WaitOn,
                                   WrEventPair,
                                   ExGetPreviousMode(),
                                   FALSE,
                                   NULL);

    ObDereferenceObject(EventPair);

    return Status;
}

static
NTSTATUS
ExpWaitOnEventPair(
    _In_ HANDLE EventPairHandle,
    _In_ BOOLEAN High)
{
    PKEVENT_PAIR EventPair;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EventPairHandle,
                                       SYNCHRONIZE,
                                       ExEventPairObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&EventPair,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = KeWaitForSingleObject(High ? &EventPair->HighEvent :
                                          &EventPair->LowEvent,
                                   WrEventPair,
                                   ExGetPreviousMode(),
                                   FALSE,
                                   NULL);

    ObDereferenceObject(EventPair);

    return Status;
}

NTSTATUS
NTAPI
NtSetHighEventPair(
    _In_ HANDLE EventPairHandle)
{
    return ExpOperateOnEventPair(EventPairHandle, TRUE, FALSE);
}

NTSTATUS
NTAPI
NtSetLowEventPair(
    _In_ HANDLE EventPairHandle)
{
    return ExpOperateOnEventPair(EventPairHandle, FALSE, FALSE);
}

NTSTATUS
NTAPI
NtSetHighWaitLowEventPair(
    _In_ HANDLE EventPairHandle)
{
    return ExpOperateOnEventPair(EventPairHandle, TRUE, TRUE);
}

NTSTATUS
NTAPI
NtSetLowWaitHighEventPair(
    _In_ HANDLE EventPairHandle)
{
    return ExpOperateOnEventPair(EventPairHandle, FALSE, TRUE);
}

NTSTATUS
NTAPI
NtWaitHighEventPair(
    _In_ HANDLE EventPairHandle)
{
    return ExpWaitOnEventPair(EventPairHandle, TRUE);
}

NTSTATUS
NTAPI
NtWaitLowEventPair(
    _In_ HANDLE EventPairHandle)
{
    return ExpWaitOnEventPair(EventPairHandle, FALSE);
}

/* KEYED EVENTS ***************************************************************/

VOID
NTAPI
ExpInitializeKeyedEvent(
    _Out_ PVOID KeyedEvent)
{
    PEXP_KEYED_EVENT Event = KeyedEvent;
    ULONG Index;

    for (Index = 0; Index < EXP_KEYED_EVENT_HASH_SIZE; Index++)
    {
        ExInitializePushLock(&Event->Locks[Index]);
        InitializeListHead(&Event->Waiters[Index]);
        InitializeListHead(&Event->Releasers[Index]);
    }
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeKeyedEventImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"KeyedEvent");
    NTSTATUS Status;

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = KEYEDEVENT_ALL_ACCESS;
    Initializer.GenericMapping = ExpKeyedEventMapping;
    Initializer.UseDefaultObject = TRUE;
    Initializer.PoolType = PagedPool;
    Initializer.DefaultPagedPoolCharge = sizeof(EXP_KEYED_EVENT);

    Status = ObCreateObjectType(&Name,
                                &Initializer,
                                NULL,
                                &ExKeyedEventObjectType);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    /*
     * One keyed event belongs to the system itself, for a lock in user mode
     * that could not get the memory to make its own.
     */
    Status = ObCreateObject(KernelMode,
                            ExKeyedEventObjectType,
                            NULL,
                            KernelMode,
                            NULL,
                            sizeof(EXP_KEYED_EVENT),
                            0,
                            0,
                            &ExpCritSecOutOfMemoryEvent);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    ExpInitializeKeyedEvent(ExpCritSecOutOfMemoryEvent);

    return TRUE;
}

NTSTATUS
NTAPI
ExpWaitForKeyedEvent(
    _In_ PVOID KeyedEvent,
    _In_ PVOID Key,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    return ExpMeetOnKeyedEvent(KeyedEvent, Key, FALSE, Alertable, Timeout);
}

NTSTATUS
NTAPI
ExpReleaseKeyedEvent(
    _In_ PVOID KeyedEvent,
    _In_ PVOID Key,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    return ExpMeetOnKeyedEvent(KeyedEvent, Key, TRUE, Alertable, Timeout);
}

NTSTATUS
NTAPI
ExpReleaseOrWaitForKeyedEvent(
    _In_opt_ HANDLE Handle,
    _In_ PVOID Key,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ BOOLEAN Release)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEXP_KEYED_EVENT KeyedEvent;
    PVOID Referenced;
    LARGE_INTEGER SafeTimeout;
    NTSTATUS Status;

    PAGED_CODE();

    /* The lowest bit of a key is not the caller's to use. */
    if ((ULONG_PTR)Key & 1)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    if ((Timeout != NULL) && (PreviousMode != KernelMode))
    {
        _SEH2_TRY
        {
            ProbeForRead(Timeout, sizeof(LARGE_INTEGER), sizeof(ULONG));
            SafeTimeout = *Timeout;
            Timeout = &SafeTimeout;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ExpOpenKeyedEventHandle(Handle,
                                     Release ? KEYEDEVENT_WAKE :
                                               KEYEDEVENT_WAIT,
                                     &KeyedEvent,
                                     &Referenced);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExpMeetOnKeyedEvent(KeyedEvent,
                                 Key,
                                 Release,
                                 Alertable,
                                 Timeout);

    if (Referenced != NULL)
    {
        ObDereferenceObject(Referenced);
    }

    return Status;
}

NTSTATUS
NTAPI
NtCreateKeyedEvent(
    _Out_ PHANDLE KeyedEventHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID KeyedEvent;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (Flags != 0)
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(KeyedEventHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            ExKeyedEventObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(EXP_KEYED_EVENT),
                            0,
                            0,
                            &KeyedEvent);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ExpInitializeKeyedEvent(KeyedEvent);

    Status = ObInsertObject(KeyedEvent,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *KeyedEventHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *KeyedEventHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtOpenKeyedEvent(
    _Out_ PHANDLE KeyedEventHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(KeyedEventHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ExKeyedEventObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *KeyedEventHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *KeyedEventHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtWaitForKeyedEvent(
    _In_opt_ HANDLE KeyedEventHandle,
    _In_ PVOID Key,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    return ExpReleaseOrWaitForKeyedEvent(KeyedEventHandle,
                                         Key,
                                         Alertable,
                                         Timeout,
                                         FALSE);
}

NTSTATUS
NTAPI
NtReleaseKeyedEvent(
    _In_opt_ HANDLE KeyedEventHandle,
    _In_ PVOID Key,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    return ExpReleaseOrWaitForKeyedEvent(KeyedEventHandle,
                                         Key,
                                         Alertable,
                                         Timeout,
                                         TRUE);
}

/* EOF */
