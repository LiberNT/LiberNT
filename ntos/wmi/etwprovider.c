/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     How a driver says it has events to give and writes them when somebody is listening
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "wmipriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define ETWP_PROVIDER_TAG 'PwtE'

#define ETWP_PROVIDER_SLOTS 512

/* A registration's number is odd while it is live and even while it is not */
#define ETWP_COOKIE_IS_LIVE(c) (((c) & 1) != 0)

/* The most traits a registration will keep on a provider's behalf */
#define ETWP_TRAITS_MAX 1024

typedef struct _ETWP_PROVIDER
{
    volatile ULONG Cookie;
    ULONG Slot;
    GUID ProviderId;
    GUID SourceId;
    PETWENABLECALLBACK EnableCallback;
    PVOID CallbackContext;
    ULONG ClassicType;
    PVOID Traits;
    ULONG TraitsLength;
    BOOLEAN DescriptorTypeInUse;

    /* What the sessions listening to this provider have asked for */
    volatile LONG Sessions;
    volatile ULONG Level;
    volatile ULONGLONG MatchAnyKeyword;
    volatile ULONGLONG MatchAllKeyword;
    volatile ULONG LoggerId;
} ETWP_PROVIDER, *PETWP_PROVIDER;

static PETWP_PROVIDER EtwpProviderSlots[ETWP_PROVIDER_SLOTS];
static EX_PUSH_LOCK EtwpProviderLock;
static ULONG EtwpProviderCookieSeed = 1;

/* How many registrations any session is listening to at all */
static volatile LONG EtwpListeningCount;

static GUID EtwpActivityIdSeed;
static volatile LONGLONG EtwpActivityIdSequence;

/* PRIVATE FUNCTIONS **********************************************************/

static
PETWP_PROVIDER
EtwpProviderFromHandle(
    _In_ REGHANDLE RegHandle)
{
    PETWP_PROVIDER Provider;
    ULONG Cookie = (ULONG)(RegHandle >> 32);
    ULONG Slot = (ULONG)(RegHandle & MAXULONG);

    if (Slot >= ETWP_PROVIDER_SLOTS) return NULL;
    if (!ETWP_COOKIE_IS_LIVE(Cookie)) return NULL;

    Provider = ReadPointerAcquire((PVOID volatile *)&EtwpProviderSlots[Slot]);
    if (Provider == NULL) return NULL;

    if (ReadULongNoFence(&Provider->Cookie) != Cookie) return NULL;

    return Provider;
}

/* The handle that names a registration */
static
REGHANDLE
EtwpHandleFromProvider(
    _In_ PETWP_PROVIDER Provider)
{
    return ((REGHANDLE)ReadULongNoFence(&Provider->Cookie) << 32) |
           (REGHANDLE)Provider->Slot;
}

static
BOOLEAN
EtwpIsEnabled(
    _In_ PETWP_PROVIDER Provider,
    _In_ UCHAR Level,
    _In_ ULONGLONG Keyword)
{
    ULONGLONG MatchAny;
    ULONGLONG MatchAll;
    ULONG Wanted;

    if (ReadNoFence(&Provider->Sessions) == 0) return FALSE;

    /* A session that named no level takes every level */
    Wanted = ReadULongNoFence(&Provider->Level);

    if (Wanted != 0 && Level != 0 && Level > (UCHAR)Wanted) return FALSE;

    /* An event that names no keyword is not filtered by keyword */
    if (Keyword == 0) return TRUE;

    MatchAny = ReadULong64NoFence(&Provider->MatchAnyKeyword);
    MatchAll = ReadULong64NoFence(&Provider->MatchAllKeyword);

    if (MatchAny != 0 && (Keyword & MatchAny) == 0) return FALSE;
    if ((Keyword & MatchAll) != MatchAll) return FALSE;

    return TRUE;
}

static
VOID
EtwpRetireProvider(
    _In_ PETWP_PROVIDER Provider)
{
    PVOID Traits;

    if (ReadNoFence(&Provider->Sessions) != 0)
    {
        InterlockedDecrement(&EtwpListeningCount);
    }

    WriteULongNoFence(&Provider->Cookie, Provider->Cookie + 1);

    Provider->EnableCallback = NULL;
    Provider->CallbackContext = NULL;
    Provider->ClassicType = 0;
    Provider->DescriptorTypeInUse = FALSE;
    Provider->Sessions = 0;
    Provider->Level = 0;
    Provider->MatchAnyKeyword = 0;
    Provider->MatchAllKeyword = 0;
    Provider->LoggerId = 0;

    Traits = Provider->Traits;
    Provider->Traits = NULL;
    Provider->TraitsLength = 0;

    RtlZeroMemory(&Provider->ProviderId, sizeof(Provider->ProviderId));
    RtlZeroMemory(&Provider->SourceId, sizeof(Provider->SourceId));

    if (Traits != NULL) ExFreePoolWithTag(Traits, ETWP_PROVIDER_TAG);
}

static
NTSTATUS
EtwpClaimProvider(
    _Outptr_ PETWP_PROVIDER *Claimed)
{
    PETWP_PROVIDER Provider;
    ULONG Slot;

    *Claimed = NULL;

    for (Slot = 0; Slot < ETWP_PROVIDER_SLOTS; Slot++)
    {
        Provider = EtwpProviderSlots[Slot];

        if (Provider == NULL)
        {
            Provider = ExAllocatePoolZero(NonPagedPoolNx,
                                          sizeof(ETWP_PROVIDER),
                                          ETWP_PROVIDER_TAG);
            if (Provider == NULL) return STATUS_INSUFFICIENT_RESOURCES;

            Provider->Slot = Slot;
            Provider->Cookie = EtwpProviderCookieSeed++ * 2;

            WritePointerRelease((PVOID volatile *)&EtwpProviderSlots[Slot],
                                Provider);
        }
        else if (ETWP_COOKIE_IS_LIVE(Provider->Cookie))
        {
            continue;
        }

        WriteULongNoFence(&Provider->Cookie, Provider->Cookie + 1);

        *Claimed = Provider;
        return STATUS_SUCCESS;
    }

    /* TODO: grow past ETWP_PROVIDER_SLOTS registrations at once */
    return STATUS_INSUFFICIENT_RESOURCES;
}

/* Does the work both ways of registering, which differ only in the type */
static
NTSTATUS
EtwpRegisterProvider(
    _In_ LPCGUID ProviderId,
    _In_ ULONG ClassicType,
    _In_opt_ PETWENABLECALLBACK EnableCallback,
    _In_opt_ PVOID CallbackContext,
    _Out_ PREGHANDLE RegHandle)
{
    PETWP_PROVIDER Provider;
    NTSTATUS Status;

    PAGED_CODE();

    if (ProviderId == NULL || RegHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *RegHandle = 0;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&EtwpProviderLock);

    Status = EtwpClaimProvider(&Provider);

    if (NT_SUCCESS(Status))
    {
        Provider->ProviderId = *ProviderId;
        Provider->ClassicType = ClassicType;
        Provider->EnableCallback = EnableCallback;
        Provider->CallbackContext = CallbackContext;

        *RegHandle = EtwpHandleFromProvider(Provider);
    }

    ExReleasePushLockExclusive(&EtwpProviderLock);
    KeLeaveCriticalRegion();

    /*
     * TODO: tell the session-control path about the new registration, so a
     * session already collecting this provider turns it on straight away
     */
    return Status;
}

/* Reads the pieces of data a provider handed over into trace fields */
static
NTSTATUS
EtwpCollectEventFields(
    _Out_writes_to_(MAX_MOF_FIELDS, *FieldCount) PMOF_FIELD Fields,
    _Out_ PULONG FieldCount,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    ULONG Count = 0;
    ULONG i;

    *FieldCount = 0;

    if (UserDataCount == 0) return STATUS_SUCCESS;

    if (UserData == NULL) return STATUS_INVALID_PARAMETER;

    /*
     * TODO: carry more than MAX_MOF_FIELDS pieces, which needs the record
     * form a manifest event is consumed in rather than the classic one
     */
    if (UserDataCount > MAX_MOF_FIELDS) return STATUS_BUFFER_OVERFLOW;

    for (i = 0; i < UserDataCount; i++)
    {
        if (UserData[i].Size == 0) continue;
        if (UserData[i].Ptr == 0) return STATUS_INVALID_PARAMETER;
        if (UserData[i].Size > MAXUSHORT) return STATUS_BUFFER_OVERFLOW;

        Fields[Count].DataPtr = UserData[i].Ptr;
        Fields[Count].Length = UserData[i].Size;
        Fields[Count].DataType = 0;
        Count++;
    }

    *FieldCount = Count;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
EtwpInitializeProviders(VOID)
{
    PAGED_CODE();

    ExInitializePushLock(&EtwpProviderLock);

    if (!NT_SUCCESS(ExUuidCreate((UUID *)&EtwpActivityIdSeed)))
    {
        /* Without a drawn identifier the counter alone keeps them apart */
        RtlZeroMemory(&EtwpActivityIdSeed, sizeof(EtwpActivityIdSeed));
    }
}

VOID
NTAPI
EtwpEnableProvider(
    _In_ LPCGUID ProviderId,
    _In_opt_ LPCGUID SourceId,
    _In_ ULONG ControlCode,
    _In_ ULONG LoggerId,
    _In_ UCHAR Level,
    _In_ ULONGLONG MatchAnyKeyword,
    _In_ ULONGLONG MatchAllKeyword,
    _In_opt_ PEVENT_FILTER_DESCRIPTOR FilterData)
{
    PETWENABLECALLBACK Callback;
    PETWP_PROVIDER Provider;
    PVOID Context;
    GUID Source;
    ULONG Slot;

    PAGED_CODE();

    if (ProviderId == NULL) return;

    for (Slot = 0; Slot < ETWP_PROVIDER_SLOTS; Slot++)
    {
        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&EtwpProviderLock);

        Provider = EtwpProviderSlots[Slot];

        if (Provider == NULL ||
            !ETWP_COOKIE_IS_LIVE(Provider->Cookie) ||
            !IsEqualGUID(&Provider->ProviderId, ProviderId))
        {
            ExReleasePushLockExclusive(&EtwpProviderLock);
            KeLeaveCriticalRegion();
            continue;
        }

        if (SourceId != NULL) Provider->SourceId = *SourceId;

        if (ControlCode == EVENT_CONTROL_CODE_ENABLE_PROVIDER)
        {
            WriteULongNoFence(&Provider->Level, Level);
            WriteULong64NoFence(&Provider->MatchAnyKeyword, MatchAnyKeyword);
            WriteULong64NoFence(&Provider->MatchAllKeyword, MatchAllKeyword);
            WriteULongNoFence(&Provider->LoggerId, LoggerId);

            if (InterlockedExchange(&Provider->Sessions, 1) == 0)
            {
                InterlockedIncrement(&EtwpListeningCount);
            }
        }
        else if (ControlCode == EVENT_CONTROL_CODE_DISABLE_PROVIDER)
        {
            if (InterlockedExchange(&Provider->Sessions, 0) != 0)
            {
                InterlockedDecrement(&EtwpListeningCount);
            }

            WriteULongNoFence(&Provider->Level, 0);
            WriteULong64NoFence(&Provider->MatchAnyKeyword, 0);
            WriteULong64NoFence(&Provider->MatchAllKeyword, 0);
            WriteULongNoFence(&Provider->LoggerId, 0);
        }

        Callback = Provider->EnableCallback;
        Context = Provider->CallbackContext;
        Source = Provider->SourceId;

        ExReleasePushLockExclusive(&EtwpProviderLock);
        KeLeaveCriticalRegion();

        if (Callback != NULL)
        {
            Callback(&Source,
                     ControlCode,
                     Level,
                     MatchAnyKeyword,
                     MatchAllKeyword,
                     FilterData,
                     Context);
        }
    }
}

NTSTATUS
NTAPI
EtwRegister(
    _In_ LPCGUID ProviderId,
    _In_opt_ PETWENABLECALLBACK EnableCallback,
    _In_opt_ PVOID CallbackContext,
    _Out_ PREGHANDLE RegHandle)
{
    PAGED_CODE();

    return EtwpRegisterProvider(ProviderId,
                                0,
                                EnableCallback,
                                CallbackContext,
                                RegHandle);
}

NTSTATUS
NTAPI
EtwRegisterClassicProvider(
    _In_ LPCGUID ProviderId,
    _In_ ULONG Type,
    _In_opt_ PETWENABLECALLBACK EnableCallback,
    _In_opt_ PVOID CallbackContext,
    _Out_ PREGHANDLE RegHandle)
{
    PAGED_CODE();

    /* TODO: the pre-enable type, which is told about a session before it starts */
    return EtwpRegisterProvider(ProviderId,
                                Type,
                                EnableCallback,
                                CallbackContext,
                                RegHandle);
}

NTSTATUS
NTAPI
EtwUnregister(
    _In_ REGHANDLE RegHandle)
{
    PETWP_PROVIDER Provider;
    NTSTATUS Status;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&EtwpProviderLock);

    Provider = EtwpProviderFromHandle(RegHandle);

    if (Provider == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else
    {
        EtwpRetireProvider(Provider);
        Status = STATUS_SUCCESS;
    }

    ExReleasePushLockExclusive(&EtwpProviderLock);
    KeLeaveCriticalRegion();

    return Status;
}

BOOLEAN
NTAPI
EtwEventEnabled(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor)
{
    PETWP_PROVIDER Provider;

    if (ReadNoFence(&EtwpListeningCount) == 0) return FALSE;

    if (EventDescriptor == NULL) return FALSE;

    Provider = EtwpProviderFromHandle(RegHandle);
    if (Provider == NULL) return FALSE;

    return EtwpIsEnabled(Provider,
                         EventDescriptor->Level,
                         EventDescriptor->Keyword);
}

/* Whether anything of this importance carrying these keywords would reach anybody */
BOOLEAN
NTAPI
EtwProviderEnabled(
    _In_ REGHANDLE RegHandle,
    _In_ UCHAR Level,
    _In_ ULONGLONG Keyword)
{
    PETWP_PROVIDER Provider;

    if (ReadNoFence(&EtwpListeningCount) == 0) return FALSE;

    Provider = EtwpProviderFromHandle(RegHandle);
    if (Provider == NULL) return FALSE;

    return EtwpIsEnabled(Provider, Level, Keyword);
}

NTSTATUS
NTAPI
EtwWriteTransfer(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor,
    _In_opt_ LPCGUID ActivityId,
    _In_opt_ LPCGUID RelatedActivityId,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    struct
    {
        EVENT_TRACE_HEADER Header;
        MOF_FIELD Fields[MAX_MOF_FIELDS];
    } Event;
    PETWP_PROVIDER Provider;
    NTSTATUS Status;
    ULONG FieldCount;
    ULONG LoggerId;
    ULONG Size;

    UNREFERENCED_PARAMETER(ActivityId);
    UNREFERENCED_PARAMETER(RelatedActivityId);

    if (EventDescriptor == NULL) return STATUS_INVALID_PARAMETER;

    if (UserDataCount > MAX_EVENT_DATA_DESCRIPTORS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ReadNoFence(&EtwpListeningCount) == 0) return STATUS_SUCCESS;

    Provider = EtwpProviderFromHandle(RegHandle);
    if (Provider == NULL) return STATUS_INVALID_HANDLE;

    if (!EtwpIsEnabled(Provider,
                       EventDescriptor->Level,
                       EventDescriptor->Keyword))
    {
        return STATUS_SUCCESS;
    }

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        /* FIXME: Windows writes from any level; this drops the event instead */
        return STATUS_SUCCESS;
    }

    Status = EtwpCollectEventFields(Event.Fields,
                                    &FieldCount,
                                    UserDataCount,
                                    UserData);
    if (!NT_SUCCESS(Status)) return Status;

    Size = sizeof(EVENT_TRACE_HEADER) + FieldCount * sizeof(MOF_FIELD);
    LoggerId = ReadULongNoFence(&Provider->LoggerId);

    RtlZeroMemory(&Event.Header, sizeof(Event.Header));

    Event.Header.Size = (USHORT)Size;
    Event.Header.Flags = WNODE_FLAG_TRACED_GUID | WNODE_FLAG_USE_MOF_PTR;
    Event.Header.Guid = Provider->ProviderId;

    /*
     * FIXME: a manifest event is consumed in its own record form, which
     * carries the identifier, the channel and the activity; said in the
     * classic form only the importance, the operation and the version survive
     */
    Event.Header.Class.Type = EventDescriptor->Opcode;
    Event.Header.Class.Level = EventDescriptor->Level;
    Event.Header.Class.Version = EventDescriptor->Id;

    return NtTraceEvent((HANDLE)(ULONG_PTR)LoggerId,
                        ETW_NT_FLAGS_TRACE_HEADER,
                        Size,
                        &Event.Header);
}

/* Writes an event that belongs to no activity in particular */
NTSTATUS
NTAPI
EtwWrite(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor,
    _In_opt_ LPCGUID ActivityId,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    return EtwWriteTransfer(RegHandle,
                            EventDescriptor,
                            ActivityId,
                            NULL,
                            UserDataCount,
                            UserData);
}

NTSTATUS
NTAPI
EtwActivityIdControl(
    _In_ ULONG ControlCode,
    _Inout_updates_bytes_(sizeof(GUID)) LPGUID ActivityId)
{
    PETHREAD Thread = PsGetCurrentThread();
    const GUID *Current;
    LONGLONG Sequence;
    GUID Fresh;

    if (ActivityId == NULL) return STATUS_INVALID_PARAMETER;

    if (ControlCode == EVENT_ACTIVITY_CTRL_CREATE_ID)
    {
        Sequence = InterlockedExchangeAdd64(&EtwpActivityIdSequence, 1) + 1;

        Fresh = EtwpActivityIdSeed;
        RtlCopyMemory(&Fresh.Data4[0], &Sequence, sizeof(Fresh.Data4));

        *ActivityId = Fresh;
        return STATUS_SUCCESS;
    }

    if (ControlCode == EVENT_ACTIVITY_CTRL_GET_ID)
    {
        Current = Thread->ActivityId;

        if (Current != NULL)
        {
            *ActivityId = *Current;
        }
        else
        {
            RtlZeroMemory(ActivityId, sizeof(*ActivityId));
        }

        return STATUS_SUCCESS;
    }

    if (ControlCode != EVENT_ACTIVITY_CTRL_SET_ID &&
        ControlCode != EVENT_ACTIVITY_CTRL_GET_SET_ID &&
        ControlCode != EVENT_ACTIVITY_CTRL_CREATE_SET_ID)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * TODO: room on the thread for the identifier it is told to carry, which
     * the thread's own teardown lets go of again
     */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
EtwSetInformation(
    _In_ REGHANDLE RegHandle,
    _In_ EVENT_INFO_CLASS InformationClass,
    _In_opt_ PVOID EventInformation,
    _In_ ULONG InformationLength)
{
    PETWP_PROVIDER Provider;
    NTSTATUS Status;
    PVOID Traits = NULL;
    PVOID Replaced;

    PAGED_CODE();

    if (InformationClass == EventProviderSetTraits)
    {
        if (EventInformation == NULL || InformationLength == 0 ||
            InformationLength > ETWP_TRAITS_MAX)
        {
            return STATUS_INVALID_PARAMETER;
        }

        Traits = ExAllocatePoolZero(NonPagedPoolNx,
                                    InformationLength,
                                    ETWP_PROVIDER_TAG);
        if (Traits == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(Traits, EventInformation, InformationLength);
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&EtwpProviderLock);

    Provider = EtwpProviderFromHandle(RegHandle);

    if (Provider == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else if (InformationClass == EventProviderSetTraits)
    {
        Replaced = Provider->Traits;
        Provider->Traits = Traits;
        Provider->TraitsLength = InformationLength;
        Traits = Replaced;
        Status = STATUS_SUCCESS;
    }
    else if (InformationClass == EventProviderUseDescriptorType)
    {
        if (EventInformation == NULL || InformationLength != sizeof(BOOLEAN))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            Provider->DescriptorTypeInUse = *(PBOOLEAN)EventInformation;
            Status = STATUS_SUCCESS;
        }
    }
    else if (InformationClass == EventProviderBinaryTrackInfo)
    {
        /* TODO: record which image a registration's events came out of */
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }

    ExReleasePushLockExclusive(&EtwpProviderLock);
    KeLeaveCriticalRegion();

    if (Traits != NULL) ExFreePoolWithTag(Traits, ETWP_PROVIDER_TAG);

    return Status;
}

NTSTATUS
NTAPI
EtwTelemetryCoverageReport(
    _In_opt_ PVOID CoverageReport)
{
    UNREFERENCED_PARAMETER(CoverageReport);

    /* TODO: the shape of a coverage report and who it is handed to */
    return STATUS_NOT_SUPPORTED;
}

/* EOF */
