/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The security reference monitor and its logon sessions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <srmp.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* A session tells whoever asked once it ends */
#define SEP_LOGON_SESSION_NOTIFY_TERMINATION 0x00000001

/* One routine that asked to be told */
typedef struct _SEP_TERMINATION_NOTIFY
{
    LIST_ENTRY Entry;
    PSE_LOGON_SESSION_TERMINATED_ROUTINE Routine;
} SEP_TERMINATION_NOTIFY, *PSEP_TERMINATION_NOTIFY;

static PSEP_LOGON_SESSION_REFERENCES SepLogonSessions = NULL;
static KGUARDED_MUTEX SepLogonSessionLock;

static LIST_ENTRY SepTerminationNotifyList;
static KGUARDED_MUTEX SepTerminationNotifyLock;

static HANDLE SepRmCommandPort = NULL;
static HANDLE SepRmCommandMessagePort = NULL;
static HANDLE SepLsaInitEvent = NULL;

/* PRIVATE FUNCTIONS **********************************************************/

/* The list is walked under its own lock, so the caller holds it */
static
PSEP_LOGON_SESSION_REFERENCES
SepFindLogonSession(
    _In_ PLUID LogonId)
{
    PSEP_LOGON_SESSION_REFERENCES Session;

    for (Session = SepLogonSessions;
         Session != NULL;
         Session = Session->Next)
    {
        if (RtlEqualLuid(&Session->LogonId, LogonId)) return Session;
    }

    return NULL;
}

/*
 * Starts a session off with one reference, the one the authority that created
 * it holds, so it lives until that authority says it is over.
 */
static
NTSTATUS
SepCreateLogonSession(
    _In_ PLUID LogonId)
{
    PSEP_LOGON_SESSION_REFERENCES Session;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    Session = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(SEP_LOGON_SESSION_REFERENCES),
                                    TAG_SE);
    if (Session == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Session, sizeof(*Session));
    Session->LogonId = *LogonId;
    Session->ReferenceCount = 1;

    /* TODO: the tokens of a session are not threaded onto this list, because
     * the published token has nowhere to hold a link */
    InitializeListHead(&Session->TokenList);

    KeAcquireGuardedMutex(&SepLogonSessionLock);

    if (SepFindLogonSession(LogonId) != NULL)
    {
        Status = STATUS_LOGON_SESSION_EXISTS;
    }
    else
    {
        Session->Next = SepLogonSessions;
        SepLogonSessions = Session;
    }

    KeReleaseGuardedMutex(&SepLogonSessionLock);

    if (!NT_SUCCESS(Status)) ExFreePoolWithTag(Session, TAG_SE);

    return Status;
}

/* Tells everyone who asked, outside the list's lock */
static
VOID
SepNotifyLogonSessionTerminated(
    _In_ PLUID LogonId)
{
    PSEP_TERMINATION_NOTIFY Notify;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    KeAcquireGuardedMutex(&SepTerminationNotifyLock);

    for (Entry = SepTerminationNotifyList.Flink;
         Entry != &SepTerminationNotifyList;
         Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, SEP_TERMINATION_NOTIFY, Entry);
        (VOID)Notify->Routine(LogonId);
    }

    KeReleaseGuardedMutex(&SepTerminationNotifyLock);
}

/*
 * Takes a session out once the last thing holding it has let go. What it
 * carried is given back here, and whoever asked is told.
 */
static
NTSTATUS
SepDeleteLogonSession(
    _In_ PLUID LogonId)
{
    PSEP_LOGON_SESSION_REFERENCES Session;
    PSEP_LOGON_SESSION_REFERENCES *Link;
    PDEVICE_MAP DeviceMap = NULL;
    BOOLEAN Notify = FALSE;
    LUID Ended;

    PAGED_CODE();

    KeAcquireGuardedMutex(&SepLogonSessionLock);

    Session = SepFindLogonSession(LogonId);
    if (Session == NULL)
    {
        KeReleaseGuardedMutex(&SepLogonSessionLock);
        return STATUS_NOT_FOUND;
    }

    /* The authority's own reference is what is being given back */
    if (Session->ReferenceCount != 0) Session->ReferenceCount--;

    if (Session->ReferenceCount != 0)
    {
        KeReleaseGuardedMutex(&SepLogonSessionLock);
        return STATUS_SUCCESS;
    }

    for (Link = &SepLogonSessions; *Link != NULL; Link = &(*Link)->Next)
    {
        if (*Link != Session) continue;

        *Link = Session->Next;
        break;
    }

    Ended = Session->LogonId;
    DeviceMap = Session->pDeviceMap;
    Notify = (Session->Flags & SEP_LOGON_SESSION_NOTIFY_TERMINATION) != 0;

    KeReleaseGuardedMutex(&SepLogonSessionLock);

    if (Notify) SepNotifyLogonSessionTerminated(&Ended);

    if (DeviceMap != NULL) ObfDereferenceDeviceMap(DeviceMap);

    ExFreePoolWithTag(Session, TAG_SE);

    return STATUS_SUCCESS;
}

/*
 * Serves the authority that decides who is logged on. It says when a session
 * begins and when it is over, and every command is answered.
 */
static
VOID
NTAPI
SepRmCommandServerThread(
    _In_ PVOID Context)
{
    SEP_RM_API_MESSAGE Message;
    SEP_RM_API_MESSAGE Reply;
    PPORT_MESSAGE Answer;
    SIZE_T Length;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Context);

    for (;;)
    {
        Answer = NULL;
        Length = sizeof(Message);

        Status = NtAlpcSendWaitReceivePort(SepRmCommandPort,
                                           0,
                                           NULL,
                                           NULL,
                                           &Message.Header,
                                           &Length,
                                           NULL,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SE: the command port stopped answering (0x%08lx)\n", Status);
            break;
        }

        switch (Message.Header.u2.s2.Type & 0xFF)
        {
            case LPC_CONNECTION_REQUEST:
                /* Only one authority speaks here, and only once */
                Status = NtAlpcAcceptConnectPort(&SepRmCommandMessagePort,
                                                 SepRmCommandPort,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 &Message.Header,
                                                 NULL,
                                                 (SepRmCommandMessagePort == NULL));
                if (!NT_SUCCESS(Status))
                    DPRINT1("SE: refused a command port caller (0x%08lx)\n", Status);
                continue;

            case LPC_REQUEST:
                break;

            default:
                /* Anything else needs no answer */
                continue;
        }

        RtlZeroMemory(&Reply, sizeof(Reply));
        Reply.Header.u1.s1.TotalLength =
            (CSHORT)(sizeof(PORT_MESSAGE) + sizeof(ULONG) + sizeof(NTSTATUS));
        Reply.Header.u1.s1.DataLength =
            Reply.Header.u1.s1.TotalLength - (CSHORT)sizeof(PORT_MESSAGE);
        Reply.Header.MessageId = Message.Header.MessageId;
        Reply.Header.ClientId = Message.Header.ClientId;
        Reply.ApiNumber = Message.ApiNumber;

        switch (Message.ApiNumber)
        {
            case RmCreateLogonSession:
                Reply.u.ResultStatus =
                    SepCreateLogonSession(&Message.u.LogonLuid);
                break;

            case RmDeleteLogonSession:
                Reply.u.ResultStatus =
                    SepDeleteLogonSession(&Message.u.LogonLuid);
                break;

            case RmAuditSetCommand:
                /* TODO: what is to be audited is accepted and not acted on */
                Reply.u.ResultStatus = STATUS_SUCCESS;
                break;

            default:
                Reply.u.ResultStatus = STATUS_INVALID_PARAMETER;
                break;
        }

        Answer = &Reply.Header;
        Length = 0;

        Status = NtAlpcSendWaitReceivePort(SepRmCommandPort,
                                           0,
                                           Answer,
                                           NULL,
                                           NULL,
                                           &Length,
                                           NULL,
                                           NULL);
        if (!NT_SUCCESS(Status))
            DPRINT1("SE: could not answer a command (0x%08lx)\n", Status);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepRmInitializeSessions(VOID)
{
    PAGED_CODE();

    KeInitializeGuardedMutex(&SepLogonSessionLock);
    KeInitializeGuardedMutex(&SepTerminationNotifyLock);
    InitializeListHead(&SepTerminationNotifyList);

    /*
     * The system and the anonymous logon are there before anything could log
     * anyone on, because the tokens built during start-up belong to them.
     */
    if (!NT_SUCCESS(SepCreateLogonSession(&SeSystemAuthenticationId)))
        return FALSE;

    if (!NT_SUCCESS(SepCreateLogonSession(&SeAnonymousAuthenticationId)))
        return FALSE;

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
SeRmInitPhase1(VOID)
{
    ALPC_PORT_ATTRIBUTES PortAttributes;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\SeRmCommandPort");
    UNICODE_STRING EventName = RTL_CONSTANT_STRING(L"\\SeLsaInitEvent");
    HANDLE ThreadHandle;
    NTSTATUS Status;

    PAGED_CODE();

    /*
     * The authority announces itself with this event, so it has to be there
     * before the authority starts; the authority only opens it.
     */
    InitializeObjectAttributes(&ObjectAttributes,
                               &EventName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateEvent(&SepLsaInitEvent,
                           EVENT_ALL_ACCESS,
                           &ObjectAttributes,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SE: could not publish the authority event (0x%08lx)\n", Status);
        return FALSE;
    }

    /* The message carries a union sized to the largest a port allows, so the
     * structure itself is bigger than that and cannot be the limit */
    RtlZeroMemory(&PortAttributes, sizeof(PortAttributes));
    PortAttributes.MaxMessageLength = PORT_MAXIMUM_MESSAGE_LENGTH;

    /* The quality of service is read whether or not it is asked for, so it
     * has to say how long it is */
    PortAttributes.SecurityQos.Length = sizeof(SECURITY_QUALITY_OF_SERVICE);
    PortAttributes.SecurityQos.ImpersonationLevel = SecurityIdentification;
    PortAttributes.SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    PortAttributes.SecurityQos.EffectiveOnly = TRUE;

    InitializeObjectAttributes(&ObjectAttributes, &Name, 0, NULL, NULL);

    Status = NtAlpcCreatePort(&SepRmCommandPort,
                              &ObjectAttributes,
                              &PortAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SE: could not open the command port (0x%08lx)\n", Status);
        return FALSE;
    }

    /*
     * Nothing waits for the authority to arrive: it starts later, and the
     * system has to come up whether or not it ever does.
     */
    /* TODO: the authority's own command port is never connected to, so no
     * audit record is handed to it; the records are dropped */
    Status = PsCreateSystemThread(&ThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  SepRmCommandServerThread,
                                  NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SE: could not serve the command port (0x%08lx)\n", Status);
        return FALSE;
    }

    ObCloseHandle(ThreadHandle, KernelMode);

    return TRUE;
}

NTSTATUS
SepRmReferenceLogonSession(
    _Inout_ PLUID LogonLuid)
{
    PSEP_LOGON_SESSION_REFERENCES Session;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    KeAcquireGuardedMutex(&SepLogonSessionLock);

    Session = SepFindLogonSession(LogonLuid);
    if (Session == NULL) Status = STATUS_NO_SUCH_LOGON_SESSION;
    else Session->ReferenceCount++;

    KeReleaseGuardedMutex(&SepLogonSessionLock);

    return Status;
}

NTSTATUS
SepRmDereferenceLogonSession(
    _Inout_ PLUID LogonLuid)
{
    PSEP_LOGON_SESSION_REFERENCES Session;
    PSEP_LOGON_SESSION_REFERENCES *Link;
    PDEVICE_MAP DeviceMap = NULL;
    BOOLEAN Notify = FALSE;
    LUID Ended;

    PAGED_CODE();

    KeAcquireGuardedMutex(&SepLogonSessionLock);

    Session = SepFindLogonSession(LogonLuid);
    if (Session == NULL)
    {
        KeReleaseGuardedMutex(&SepLogonSessionLock);
        return STATUS_NO_SUCH_LOGON_SESSION;
    }

    if (Session->ReferenceCount != 0) Session->ReferenceCount--;

    if (Session->ReferenceCount != 0)
    {
        KeReleaseGuardedMutex(&SepLogonSessionLock);
        return STATUS_SUCCESS;
    }

    for (Link = &SepLogonSessions; *Link != NULL; Link = &(*Link)->Next)
    {
        if (*Link != Session) continue;

        *Link = Session->Next;
        break;
    }

    Ended = Session->LogonId;
    DeviceMap = Session->pDeviceMap;
    Notify = (Session->Flags & SEP_LOGON_SESSION_NOTIFY_TERMINATION) != 0;

    KeReleaseGuardedMutex(&SepLogonSessionLock);

    if (Notify) SepNotifyLogonSessionTerminated(&Ended);
    if (DeviceMap != NULL) ObfDereferenceDeviceMap(DeviceMap);

    ExFreePoolWithTag(Session, TAG_SE);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SepRmInsertLogonSessionIntoToken(
    _Inout_ PTOKEN Token)
{
    PSEP_LOGON_SESSION_REFERENCES Session;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    KeAcquireGuardedMutex(&SepLogonSessionLock);

    Session = SepFindLogonSession(&Token->AuthenticationId);
    if (Session == NULL)
    {
        Status = STATUS_NO_SUCH_LOGON_SESSION;
    }
    else
    {
        Session->ReferenceCount++;
        Token->LogonSession = Session;
    }

    KeReleaseGuardedMutex(&SepLogonSessionLock);

    return Status;
}

NTSTATUS
NTAPI
SepRmRemoveLogonSessionFromToken(
    _Inout_ PTOKEN Token)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (Token->LogonSession == NULL) return STATUS_NO_SUCH_LOGON_SESSION;

    /* The token lets go of the session before it forgets which one it was */
    Status = SepRmDereferenceLogonSession(&Token->LogonSession->LogonId);

    Token->LogonSession = NULL;

    return Status;
}

NTSTATUS
NTAPI
SeMarkLogonSessionForTerminationNotification(
    _In_ PLUID LogonId)
{
    PSEP_LOGON_SESSION_REFERENCES Session;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    KeAcquireGuardedMutex(&SepLogonSessionLock);

    Session = SepFindLogonSession(LogonId);
    if (Session == NULL)
        Status = STATUS_NOT_FOUND;
    else
        Session->Flags |= SEP_LOGON_SESSION_NOTIFY_TERMINATION;

    KeReleaseGuardedMutex(&SepLogonSessionLock);

    return Status;
}

NTSTATUS
NTAPI
SeRegisterLogonSessionTerminatedRoutine(
    _In_ PSE_LOGON_SESSION_TERMINATED_ROUTINE CallbackRoutine)
{
    PSEP_TERMINATION_NOTIFY Notify;

    PAGED_CODE();

    if (CallbackRoutine == NULL) return STATUS_INVALID_PARAMETER;

    Notify = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Notify), TAG_SE);
    if (Notify == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Notify->Routine = CallbackRoutine;

    KeAcquireGuardedMutex(&SepTerminationNotifyLock);
    InsertTailList(&SepTerminationNotifyList, &Notify->Entry);
    KeReleaseGuardedMutex(&SepTerminationNotifyLock);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SeUnregisterLogonSessionTerminatedRoutine(
    _In_ PSE_LOGON_SESSION_TERMINATED_ROUTINE CallbackRoutine)
{
    PSEP_TERMINATION_NOTIFY Found = NULL;
    PSEP_TERMINATION_NOTIFY Notify;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (CallbackRoutine == NULL) return STATUS_INVALID_PARAMETER;

    KeAcquireGuardedMutex(&SepTerminationNotifyLock);

    for (Entry = SepTerminationNotifyList.Flink;
         Entry != &SepTerminationNotifyList;
         Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, SEP_TERMINATION_NOTIFY, Entry);

        if (Notify->Routine != CallbackRoutine) continue;

        RemoveEntryList(&Notify->Entry);
        Found = Notify;
        break;
    }

    KeReleaseGuardedMutex(&SepTerminationNotifyLock);

    /* The documentation gives a missing entry this status, not NOT_FOUND */
    if (Found == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    ExFreePoolWithTag(Found, TAG_SE);

    return STATUS_SUCCESS;
}

/*
 * A session may be given a map of its own, so the drive letters one logon
 * sees are not the ones another does.
 */
NTSTATUS
NTAPI
SeGetLogonIdDeviceMap(
    _In_ PLUID LogonId,
    _Out_ PDEVICE_MAP *DeviceMap)
{
    PSEP_LOGON_SESSION_REFERENCES Session;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    *DeviceMap = NULL;

    KeAcquireGuardedMutex(&SepLogonSessionLock);

    Session = SepFindLogonSession(LogonId);
    if (Session == NULL)
    {
        Status = STATUS_NO_SUCH_LOGON_SESSION;
    }
    else if (Session->pDeviceMap == NULL)
    {
        /* TODO: a session is not given a map of its own on demand */
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    else
    {
        *DeviceMap = Session->pDeviceMap;
    }

    KeReleaseGuardedMutex(&SepLogonSessionLock);

    return Status;
}

/* Reads one value the security manager keeps in the registry */
NTSTATUS
NTAPI
SepRegQueryHelper(
    _In_ PCWSTR KeyName,
    _In_ PCWSTR ValueName,
    _In_ ULONG ValueType,
    _In_ ULONG DataLength,
    _Out_ PVOID ValueData)
{
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE KeyHandle;
    NTSTATUS Status;
    ULONG Length;
    ULONG Size;

    PAGED_CODE();

    RtlInitUnicodeString(&Name, KeyName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    Size = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + DataLength;

    Information = ExAllocatePoolWithTag(PagedPool, Size, TAG_SE);
    if (Information == NULL)
    {
        ZwClose(KeyHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlInitUnicodeString(&Name, ValueName);

    Status = ZwQueryValueKey(KeyHandle,
                             &Name,
                             KeyValuePartialInformation,
                             Information,
                             Size,
                             &Length);
    if (NT_SUCCESS(Status))
    {
        /* Only the kind and the size that were asked for will do */
        if (Information->Type != ValueType ||
            Information->DataLength != DataLength)
        {
            Status = STATUS_OBJECT_TYPE_MISMATCH;
        }
        else
        {
            RtlCopyMemory(ValueData, Information->Data, DataLength);
        }
    }

    ExFreePoolWithTag(Information, TAG_SE);
    ZwClose(KeyHandle);

    return Status;
}

/* EOF */
