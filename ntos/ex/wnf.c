/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Notification Facility (WNF)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

#define TAG_WNF_INSTANCE 'IfnW'
#define TAG_WNF_DATA     'DfnW'
#define TAG_WNF_SUB      'SfnW'

#define WNF_NAME_BUCKET_COUNT 64

typedef struct _WNF_NAME_BUCKET
{
    EX_PUSH_LOCK Lock;
    LIST_ENTRY ListHead;
} WNF_NAME_BUCKET, *PWNF_NAME_BUCKET;

static WNF_NAME_BUCKET ExpWnfNameBuckets[WNF_NAME_BUCKET_COUNT];
static LONG64 ExpWnfNextUnique;
static LONG64 ExpWnfNextSubscriptionId;
static BOOLEAN ExpWnfInitialized;

/* PRIVATE FUNCTIONS *********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpWnfInitialization(VOID)
{
    ULONG Index;

    for (Index = 0; Index < WNF_NAME_BUCKET_COUNT; Index++)
    {
        ExInitializePushLock(&ExpWnfNameBuckets[Index].Lock);
        InitializeListHead(&ExpWnfNameBuckets[Index].ListHead);
    }
    ExpWnfInitialized = TRUE;
    return TRUE;
}

static
ULONG64
ExpWnfEncodeStateName(ULONG64 InternalName)
{
    return InternalName ^ WNF_STATE_KEY;
}

static
VOID
ExpWnfEncodeToStateName(ULONG64 InternalName,
                        PWNF_STATE_NAME StateName)
{
    ULONG64 Encoded = ExpWnfEncodeStateName(InternalName);

    StateName->Data[0] = (ULONG)Encoded;
    StateName->Data[1] = (ULONG)(Encoded >> 32);
}

static
NTSTATUS
ExpWnfCaptureStateName(PCWNF_STATE_NAME StateName,
                       KPROCESSOR_MODE PreviousMode,
                       PWNF_STATE_NAME_INTERNAL InternalName)
{
    WNF_STATE_NAME CapturedName = {0};

    if (!ExpWnfInitialized)
        return STATUS_UNSUCCESSFUL;

    if (StateName == NULL)
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(StateName, sizeof(*StateName), sizeof(ULONG));
            CapturedName = *StateName;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        CapturedName = *StateName;
    }

    /* The XOR encoding is its own inverse */
    InternalName->Value = ExpWnfEncodeStateName(((ULONG64)CapturedName.Data[1] << 32) |
                                                CapturedName.Data[0]);

    if (InternalName->Version != WNF_STATE_NAME_VERSION)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpWnfCaptureTypeId(PCWNF_TYPE_ID TypeId,
                    KPROCESSOR_MODE PreviousMode,
                    PWNF_TYPE_ID CapturedTypeId,
                    PBOOLEAN HasTypeId)
{
    *HasTypeId = FALSE;

    if (TypeId == NULL)
        return STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(TypeId, sizeof(*TypeId), sizeof(ULONG));
            *CapturedTypeId = *TypeId;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        *CapturedTypeId = *TypeId;
    }

    *HasTypeId = TRUE;
    return STATUS_SUCCESS;
}

static
PWNF_NAME_BUCKET
ExpWnfNameBucket(ULONG64 InternalName)
{
    ULONG Hash = (ULONG)(InternalName ^ (InternalName >> 32)) * 0x9E3779B1;

    return &ExpWnfNameBuckets[Hash >> 26];
}

static
VOID
ExpWnfLockBucketShared(PWNF_NAME_BUCKET Bucket)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Bucket->Lock);
}

static
VOID
ExpWnfUnlockBucketShared(PWNF_NAME_BUCKET Bucket)
{
    ExReleasePushLockShared(&Bucket->Lock);
    KeLeaveCriticalRegion();
}

static
VOID
ExpWnfLockBucketExclusive(PWNF_NAME_BUCKET Bucket)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Bucket->Lock);
}

static
VOID
ExpWnfUnlockBucketExclusive(PWNF_NAME_BUCKET Bucket)
{
    ExReleasePushLockExclusive(&Bucket->Lock);
    KeLeaveCriticalRegion();
}

static
VOID
ExpWnfLockShared(PEX_PUSH_LOCK Lock)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(Lock);
}

static
VOID
ExpWnfUnlockShared(PEX_PUSH_LOCK Lock)
{
    ExReleasePushLockShared(Lock);
    KeLeaveCriticalRegion();
}

static
VOID
ExpWnfLockExclusive(PEX_PUSH_LOCK Lock)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(Lock);
}

static
VOID
ExpWnfUnlockExclusive(PEX_PUSH_LOCK Lock)
{
    ExReleasePushLockExclusive(Lock);
    KeLeaveCriticalRegion();
}

static
PWNF_NAME_INSTANCE
ExpWnfFindNameInstance(PWNF_NAME_BUCKET Bucket,
                       ULONG64 InternalName)
{
    PLIST_ENTRY Entry;
    PWNF_NAME_INSTANCE Instance;

    for (Entry = Bucket->ListHead.Flink;
         Entry != &Bucket->ListHead;
         Entry = Entry->Flink)
    {
        Instance = CONTAINING_RECORD(Entry, WNF_NAME_INSTANCE, NameListEntry);
        if (Instance->StateName.Value == InternalName)
            return Instance;
    }

    return NULL;
}

static
PWNF_NAME_INSTANCE
ExpWnfReferenceNameInstance(ULONG64 InternalName)
{
    PWNF_NAME_BUCKET Bucket = ExpWnfNameBucket(InternalName);
    PWNF_NAME_INSTANCE Instance;

    ExpWnfLockBucketShared(Bucket);
    Instance = ExpWnfFindNameInstance(Bucket, InternalName);
    if ((Instance != NULL) && !ExAcquireRundownProtection(&Instance->RunRef))
        Instance = NULL;
    ExpWnfUnlockBucketShared(Bucket);

    return Instance;
}

static
VOID
ExpWnfDereferenceNameInstance(PWNF_NAME_INSTANCE Instance)
{
    ExReleaseRundownProtection(&Instance->RunRef);
}

static
PWNF_NAME_INSTANCE
ExpWnfAllocateNameInstance(PWNF_STATE_NAME_INTERNAL InternalName,
                           PCWNF_TYPE_ID TypeId,
                           ULONG MaximumStateSize,
                           PSECURITY_DESCRIPTOR CapturedSd)
{
    PWNF_NAME_INSTANCE Instance;

    Instance = ExAllocatePoolWithTag(PagedPool, sizeof(*Instance), TAG_WNF_INSTANCE);
    if (Instance == NULL)
        return NULL;

    RtlZeroMemory(Instance, sizeof(*Instance));
    ExInitializeRundownProtection(&Instance->RunRef);
    ExInitializePushLock(&Instance->StateDataLock);
    ExInitializePushLock(&Instance->SubscriptionListLock);
    Instance->StateName.Value = InternalName->Value;
    Instance->Lifetime = (WNF_STATE_NAME_LIFETIME)InternalName->NameLifetime;
    Instance->DataScope = (WNF_DATA_SCOPE)InternalName->DataScope;
    Instance->PersistData = (BOOLEAN)InternalName->PermanentData;
    if (TypeId != NULL)
    {
        Instance->HasTypeId = TRUE;
        Instance->TypeId = *TypeId;
    }
    Instance->MaximumStateSize = MaximumStateSize ? MaximumStateSize
                                                  : WNF_MAX_STATE_SIZE;
    Instance->SecurityDescriptor = CapturedSd;
    Instance->CreatorProcess = PsGetCurrentProcess();
    InitializeListHead(&Instance->SubscriptionListHead);
    return Instance;
}

static
PWNF_NAME_INSTANCE
ExpWnfInsertNameInstance(PWNF_NAME_INSTANCE Instance)
{
    PWNF_NAME_BUCKET Bucket = ExpWnfNameBucket(Instance->StateName.Value);
    PWNF_NAME_INSTANCE Existing;

    ExpWnfLockBucketExclusive(Bucket);
    Existing = ExpWnfFindNameInstance(Bucket, Instance->StateName.Value);
    if (Existing == NULL)
    {
        InsertTailList(&Bucket->ListHead, &Instance->NameListEntry);
        Existing = Instance;
    }
    if (!ExAcquireRundownProtection(&Existing->RunRef))
        Existing = NULL;
    ExpWnfUnlockBucketExclusive(Bucket);

    return Existing;
}

static
NTSTATUS
ExpWnfReferenceOrCreateNameInstance(PWNF_STATE_NAME_INTERNAL InternalName,
                                    PWNF_NAME_INSTANCE *OutInstance)
{
    PWNF_NAME_INSTANCE Instance;
    PWNF_NAME_INSTANCE Created;

    Instance = ExpWnfReferenceNameInstance(InternalName->Value);
    if (Instance != NULL)
    {
        *OutInstance = Instance;
        return STATUS_SUCCESS;
    }

    /* Well-known names spring into existence on first use */
    if (InternalName->NameLifetime != WnfWellKnownStateName)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    Created = ExpWnfAllocateNameInstance(InternalName, NULL, 0, NULL);
    if (Created == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Instance = ExpWnfInsertNameInstance(Created);
    if (Instance != Created)
        ExFreePoolWithTag(Created, TAG_WNF_INSTANCE);
    if (Instance == NULL)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    *OutInstance = Instance;
    return STATUS_SUCCESS;
}

static
BOOLEAN
ExpWnfMatchTypeId(PWNF_NAME_INSTANCE Instance,
                  BOOLEAN HasTypeId,
                  PCWNF_TYPE_ID TypeId)
{
    if (!Instance->HasTypeId || !HasTypeId)
        return TRUE;

    return RtlCompareMemory(&Instance->TypeId, TypeId, sizeof(WNF_TYPE_ID)) ==
           sizeof(WNF_TYPE_ID);
}

static
VOID
ExpWnfDereferenceSubscription(PWNF_SUBSCRIPTION Subscription)
{
    if (InterlockedDecrement(&Subscription->RefCount) == 0)
        KeSetEvent(&Subscription->RundownEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
ExpWnfReapSubscription(PWNF_SUBSCRIPTION Subscription)
{
    ExpWnfDereferenceSubscription(Subscription);
    KeWaitForSingleObject(&Subscription->RundownEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    ExFreePoolWithTag(Subscription, TAG_WNF_SUB);
}

static
VOID
ExpWnfReapSubscriptionList(PLIST_ENTRY ReapList)
{
    PLIST_ENTRY Entry;

    while (!IsListEmpty(ReapList))
    {
        Entry = RemoveHeadList(ReapList);
        ExpWnfReapSubscription(CONTAINING_RECORD(Entry,
                                                 WNF_SUBSCRIPTION,
                                                 SubscriptionListEntry));
    }
}

static
VOID
NTAPI
ExpWnfCallbackWorker(PVOID Parameter)
{
    PWNF_SUBSCRIPTION Subscription = Parameter;
    PWNF_NAME_INSTANCE Instance = Subscription->NameInstance;
    WNF_STATE_NAME EncodedName;
    WNF_TYPE_ID TypeId = { { 0 } };
    PVOID Data = NULL;
    ULONG DataSize = 0;
    WNF_CHANGE_STAMP ChangeStamp = 0;
    BOOLEAN HasTypeId = FALSE;
    BOOLEAN Deliver = FALSE;

    InterlockedExchange(&Subscription->WorkQueued, 0);
    if (!InterlockedCompareExchange(&Subscription->Removed, 0, 0))
    {
        ExpWnfLockShared(&Instance->StateDataLock);
        DataSize = Instance->DataSize;
        if (DataSize != 0)
        {
            Data = ExAllocatePoolWithTag(PagedPool, DataSize, TAG_WNF_DATA);
            if (Data != NULL)
                RtlCopyMemory(Data, Instance->DataBuffer, DataSize);
            else
                DataSize = 0;
        }
        ChangeStamp = Instance->ChangeStamp;
        HasTypeId = Instance->HasTypeId;
        TypeId = Instance->TypeId;
        ExpWnfUnlockShared(&Instance->StateDataLock);
        ExpWnfEncodeToStateName(Instance->StateName.Value, &EncodedName);
        Deliver = TRUE;
    }

    if (Deliver)
    {
        Subscription->Callback(&EncodedName,
                               ChangeStamp,
                               HasTypeId ? &TypeId : NULL,
                               Subscription->CallbackContext,
                               Data,
                               DataSize);
        Subscription->ChangeStamp = ChangeStamp;
    }

    if (Data != NULL)
        ExFreePoolWithTag(Data, TAG_WNF_DATA);

    InterlockedDecrement(&Instance->DeliveriesInFlight);
    ExpWnfDereferenceNameInstance(Instance);
    ExpWnfDereferenceSubscription(Subscription);
}

static
VOID
ExpWnfSignalSubscription(PWNF_SUBSCRIPTION Subscription)
{
    PWNF_NAME_INSTANCE Instance = Subscription->NameInstance;

    KeSetEvent(&Subscription->NotificationEvent, IO_NO_INCREMENT, FALSE);

    if ((Subscription->Callback != NULL) &&
        (InterlockedCompareExchange(&Subscription->WorkQueued, 1, 0) == 0))
    {
        if (!ExAcquireRundownProtection(&Instance->RunRef))
        {
            InterlockedExchange(&Subscription->WorkQueued, 0);
            return;
        }
        InterlockedIncrement(&Subscription->RefCount);
        InterlockedIncrement(&Instance->DeliveriesInFlight);
        ExQueueWorkItem(&Subscription->WorkItem, DelayedWorkQueue);
    }
}

static
VOID
ExpWnfSignalSubscriptions(PWNF_NAME_INSTANCE Instance)
{
    PLIST_ENTRY Entry;

    ExpWnfLockShared(&Instance->SubscriptionListLock);
    for (Entry = Instance->SubscriptionListHead.Flink;
         Entry != &Instance->SubscriptionListHead;
         Entry = Entry->Flink)
    {
        ExpWnfSignalSubscription(CONTAINING_RECORD(Entry,
                                                   WNF_SUBSCRIPTION,
                                                   SubscriptionListEntry));
    }
    ExpWnfUnlockShared(&Instance->SubscriptionListLock);
}

static
PWNF_SUBSCRIPTION
ExpWnfAllocateSubscription(WNF_CHANGE_STAMP ChangeStamp,
                           ULONG EventMask,
                           PWNF_KERNEL_CALLBACK Callback,
                           PVOID CallbackContext)
{
    PWNF_SUBSCRIPTION Subscription;

    Subscription = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(*Subscription),
                                         TAG_WNF_SUB);
    if (Subscription == NULL)
        return NULL;

    RtlZeroMemory(Subscription, sizeof(*Subscription));
    KeInitializeEvent(&Subscription->NotificationEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Subscription->RundownEvent, NotificationEvent, FALSE);
    Subscription->RefCount = 1;
    Subscription->ChangeStamp = ChangeStamp;
    Subscription->EventMask = EventMask;
    Subscription->Callback = Callback;
    Subscription->CallbackContext = CallbackContext;
    ExInitializeWorkItem(&Subscription->WorkItem,
                         ExpWnfCallbackWorker,
                         Subscription);
    return Subscription;
}

static
VOID
ExpWnfDetachSubscription(PWNF_SUBSCRIPTION Subscription,
                         PLIST_ENTRY ReapList)
{
    RemoveEntryList(&Subscription->SubscriptionListEntry);
    InterlockedExchange(&Subscription->Removed, TRUE);
    InterlockedDecrement(&Subscription->NameInstance->SubscriptionCount);
    if (ReapList != NULL)
        InsertTailList(ReapList, &Subscription->SubscriptionListEntry);
    else
        InitializeListHead(&Subscription->SubscriptionListEntry);
    ExpWnfDereferenceSubscription(Subscription);
}

static
VOID
ExpWnfAttachSubscription(PWNF_NAME_INSTANCE Instance,
                         PWNF_SUBSCRIPTION Subscription,
                         PWNF_STATE_NAME_INTERNAL InternalName)
{
    Subscription->NameInstance = Instance;
    Subscription->StateName.Value = InternalName->Value;
    Subscription->SubscriptionId = InterlockedIncrement64(&ExpWnfNextSubscriptionId);
    InterlockedIncrement(&Subscription->RefCount);

    ExpWnfLockExclusive(&Instance->SubscriptionListLock);
    InsertTailList(&Instance->SubscriptionListHead,
                   &Subscription->SubscriptionListEntry);
    InterlockedIncrement(&Instance->SubscriptionCount);
    ExpWnfUnlockExclusive(&Instance->SubscriptionListLock);
}

static
VOID
ExpWnfFreeNameInstance(PWNF_NAME_INSTANCE Instance)
{
    ASSERT(IsListEmpty(&Instance->SubscriptionListHead));

    if (Instance->DataBuffer != NULL)
        ExFreePoolWithTag(Instance->DataBuffer, TAG_WNF_DATA);

    if (Instance->SecurityDescriptor != NULL)
        SeReleaseSecurityDescriptor(Instance->SecurityDescriptor, KernelMode, TRUE);

    ExFreePoolWithTag(Instance, TAG_WNF_INSTANCE);
}

static
VOID
ExpWnfDestroyNameInstance(PWNF_NAME_INSTANCE Instance)
{
    PWNF_SUBSCRIPTION Subscription;
    LIST_ENTRY ReapList;

    InitializeListHead(&ReapList);
    ExWaitForRundownProtectionRelease(&Instance->RunRef);

    ExpWnfLockExclusive(&Instance->SubscriptionListLock);
    while (!IsListEmpty(&Instance->SubscriptionListHead))
    {
        Subscription = CONTAINING_RECORD(Instance->SubscriptionListHead.Flink,
                                         WNF_SUBSCRIPTION,
                                         SubscriptionListEntry);
        KeSetEvent(&Subscription->NotificationEvent, IO_NO_INCREMENT, FALSE);
        ExpWnfDetachSubscription(Subscription,
                                 (Subscription->Callback == NULL) ? &ReapList : NULL);
    }
    ExpWnfUnlockExclusive(&Instance->SubscriptionListLock);

    ExpWnfReapSubscriptionList(&ReapList);
    ExpWnfFreeNameInstance(Instance);
}

static
NTSTATUS
ExpWnfQuerySnapshot(ULONG64 InternalName,
                    BOOLEAN HasTypeId,
                    PCWNF_TYPE_ID TypeId,
                    PWNF_CHANGE_STAMP ChangeStamp,
                    PVOID *Data,
                    PULONG DataSize)
{
    PWNF_NAME_INSTANCE Instance;
    PVOID Snapshot = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    Instance = ExpWnfReferenceNameInstance(InternalName);
    if (Instance == NULL)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    else if (!ExpWnfMatchTypeId(Instance, HasTypeId, TypeId))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        ExpWnfLockShared(&Instance->StateDataLock);
        *ChangeStamp = Instance->ChangeStamp;
        *DataSize = Instance->DataSize;
        if (Instance->DataSize != 0)
        {
            Snapshot = ExAllocatePoolWithTag(PagedPool,
                                             Instance->DataSize,
                                             TAG_WNF_DATA);
            if (Snapshot != NULL)
                RtlCopyMemory(Snapshot, Instance->DataBuffer, Instance->DataSize);
            else
                Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        ExpWnfUnlockShared(&Instance->StateDataLock);
    }
    if (Instance != NULL)
        ExpWnfDereferenceNameInstance(Instance);

    *Data = Snapshot;
    return Status;
}

static
BOOLEAN
ExpWnfRemoveNameInstance(ULONG64 InternalName,
                         PWNF_NAME_INSTANCE Instance)
{
    PWNF_NAME_BUCKET Bucket = ExpWnfNameBucket(InternalName);
    BOOLEAN Removed = FALSE;

    ExpWnfLockBucketExclusive(Bucket);
    if (ExpWnfFindNameInstance(Bucket, InternalName) == Instance)
    {
        RemoveEntryList(&Instance->NameListEntry);
        Removed = TRUE;
    }
    ExpWnfUnlockBucketExclusive(Bucket);

    return Removed;
}

static
NTSTATUS
ExpWnfStoreStateData(PWNF_NAME_INSTANCE Instance,
                     PVOID *Data,
                     ULONG Length,
                     BOOLEAN HasTypeId,
                     PCWNF_TYPE_ID TypeId,
                     WNF_CHANGE_STAMP MatchingChangeStamp,
                     LOGICAL CheckStamp)
{
    PVOID OldData;

    if (Length > Instance->MaximumStateSize)
        return STATUS_BUFFER_OVERFLOW;

    if (!ExpWnfMatchTypeId(Instance, HasTypeId, TypeId))
        return STATUS_INVALID_PARAMETER;

    ExpWnfLockExclusive(&Instance->StateDataLock);
    if (CheckStamp && (Instance->ChangeStamp != MatchingChangeStamp))
    {
        ExpWnfUnlockExclusive(&Instance->StateDataLock);
        return STATUS_UNSUCCESSFUL;
    }

    OldData = Instance->DataBuffer;
    Instance->DataBuffer = *Data;
    Instance->DataSize = Length;
    Instance->ChangeStamp++;
    ExpWnfUnlockExclusive(&Instance->StateDataLock);

    *Data = OldData;
    ExpWnfSignalSubscriptions(Instance);
    return STATUS_SUCCESS;
}

static
WNF_CHANGE_STAMP
ExpWnfReadChangeStamp(PWNF_NAME_INSTANCE Instance)
{
    WNF_CHANGE_STAMP ChangeStamp;

    ExpWnfLockShared(&Instance->StateDataLock);
    ChangeStamp = Instance->ChangeStamp;
    ExpWnfUnlockShared(&Instance->StateDataLock);

    return ChangeStamp;
}

/* SYSTEM CALLS **************************************************************/

NTSTATUS
NTAPI
NtCreateWnfStateName(PWNF_STATE_NAME StateName,
                     WNF_STATE_NAME_LIFETIME NameLifetime,
                     WNF_DATA_SCOPE DataScope,
                     BOOLEAN PersistData,
                     PCWNF_TYPE_ID TypeId,
                     ULONG MaximumStateSize,
                     PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_TYPE_ID CapturedTypeId;
    BOOLEAN HasTypeId;
    PSECURITY_DESCRIPTOR CapturedSd = NULL;
    PWNF_NAME_INSTANCE Instance;
    PWNF_NAME_INSTANCE Created;
    NTSTATUS Status;

    PAGED_CODE();

    if (!ExpWnfInitialized)
        return STATUS_UNSUCCESSFUL;

    if (StateName == NULL)
        return STATUS_INVALID_PARAMETER;

    if ((NameLifetime != WnfPermanentStateName) &&
        (NameLifetime != WnfPersistentStateName) &&
        (NameLifetime != WnfTemporaryStateName))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DataScope > WnfDataScopePhysicalMachine)
        return STATUS_INVALID_PARAMETER;

    if (MaximumStateSize > WNF_MAX_STATE_SIZE)
        return STATUS_INVALID_PARAMETER;

    if (PersistData && (NameLifetime == WnfTemporaryStateName))
        return STATUS_INVALID_PARAMETER;

    if ((NameLifetime != WnfTemporaryStateName) &&
        !SeSinglePrivilegeCheck(SeCreatePermanentPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(StateName, sizeof(*StateName), sizeof(ULONG));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ExpWnfCaptureTypeId(TypeId, PreviousMode, &CapturedTypeId, &HasTypeId);
    if (!NT_SUCCESS(Status))
        return Status;

    if (SecurityDescriptor != NULL)
    {
        Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                             PreviousMode,
                                             PagedPool,
                                             TRUE,
                                             &CapturedSd);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    InternalName.Value = 0;
    InternalName.Version = WNF_STATE_NAME_VERSION;
    InternalName.NameLifetime = NameLifetime;
    InternalName.DataScope = DataScope;
    InternalName.PermanentData = PersistData ? 1 : 0;

    InternalName.Unique = InterlockedIncrement64(&ExpWnfNextUnique);
    Instance = ExpWnfAllocateNameInstance(&InternalName,
                                          HasTypeId ? &CapturedTypeId : NULL,
                                          MaximumStateSize,
                                          CapturedSd);
    if (Instance == NULL)
    {
        if (CapturedSd != NULL)
            SeReleaseSecurityDescriptor(CapturedSd, KernelMode, TRUE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Created = ExpWnfInsertNameInstance(Instance);
    ASSERT(Created == Instance);
    ExpWnfDereferenceNameInstance(Created);

    _SEH2_TRY
    {
        ExpWnfEncodeToStateName(InternalName.Value, StateName);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
        if (ExpWnfRemoveNameInstance(InternalName.Value, Instance))
            ExpWnfDestroyNameInstance(Instance);
        _SEH2_YIELD(return Status);
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtDeleteWnfStateName(PCWNF_STATE_NAME StateName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    Instance = ExpWnfReferenceNameInstance(InternalName.Value);
    if (Instance == NULL)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    if ((Instance->CreatorProcess != PsGetCurrentProcess()) &&
        !SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
    {
        ExpWnfDereferenceNameInstance(Instance);
        return STATUS_ACCESS_DENIED;
    }

    ExpWnfDereferenceNameInstance(Instance);
    if (!ExpWnfRemoveNameInstance(InternalName.Value, Instance))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    ExpWnfDestroyNameInstance(Instance);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtUpdateWnfStateData(PCWNF_STATE_NAME StateName,
                     const VOID *Buffer,
                     ULONG Length,
                     PCWNF_TYPE_ID TypeId,
                     const VOID *ExplicitScope,
                     WNF_CHANGE_STAMP MatchingChangeStamp,
                     LOGICAL CheckStamp)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_TYPE_ID CapturedTypeId;
    BOOLEAN HasTypeId;
    PWNF_NAME_INSTANCE Instance;
    PVOID NewData = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((Length != 0) && (Buffer == NULL))
        return STATUS_INVALID_PARAMETER;

    if (Length > WNF_MAX_STATE_SIZE)
        return STATUS_BUFFER_OVERFLOW;

    Status = ExpWnfCaptureTypeId(TypeId, PreviousMode, &CapturedTypeId, &HasTypeId);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length != 0)
    {
        NewData = ExAllocatePoolWithTag(PagedPool, Length, TAG_WNF_DATA);
        if (NewData == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            if (PreviousMode != KernelMode)
                ProbeForRead(Buffer, Length, 1);
            RtlCopyMemory(NewData, Buffer, Length);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            ExFreePoolWithTag(NewData, TAG_WNF_DATA);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ExpWnfReferenceOrCreateNameInstance(&InternalName, &Instance);
    if (NT_SUCCESS(Status))
    {
        Status = ExpWnfStoreStateData(Instance,
                                      &NewData,
                                      Length,
                                      HasTypeId,
                                      &CapturedTypeId,
                                      MatchingChangeStamp,
                                      CheckStamp);
        ExpWnfDereferenceNameInstance(Instance);
    }

    if (NewData != NULL)
        ExFreePoolWithTag(NewData, TAG_WNF_DATA);

    return Status;
}

NTSTATUS
NTAPI
NtDeleteWnfStateData(PCWNF_STATE_NAME StateName,
                     const VOID *ExplicitScope)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PVOID OldData;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    Instance = ExpWnfReferenceNameInstance(InternalName.Value);
    if (Instance == NULL)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    ExpWnfLockExclusive(&Instance->StateDataLock);
    OldData = Instance->DataBuffer;
    Instance->DataBuffer = NULL;
    Instance->DataSize = 0;
    Instance->ChangeStamp = 0;
    ExpWnfUnlockExclusive(&Instance->StateDataLock);

    ExpWnfSignalSubscriptions(Instance);
    ExpWnfDereferenceNameInstance(Instance);

    if (OldData != NULL)
        ExFreePoolWithTag(OldData, TAG_WNF_DATA);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryWnfStateData(PCWNF_STATE_NAME StateName,
                    PCWNF_TYPE_ID TypeId,
                    const VOID *ExplicitScope,
                    PWNF_CHANGE_STAMP ChangeStamp,
                    PVOID Buffer,
                    PULONG BufferSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_TYPE_ID CapturedTypeId;
    BOOLEAN HasTypeId;
    WNF_CHANGE_STAMP Stamp = 0;
    PVOID Snapshot = NULL;
    ULONG CallerSize = 0;
    ULONG Required = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ChangeStamp == NULL) || (BufferSize == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = ExpWnfCaptureTypeId(TypeId, PreviousMode, &CapturedTypeId, &HasTypeId);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWriteUlong((PULONG)ChangeStamp);
            ProbeForWriteUlong(BufferSize);
        }
        CallerSize = *BufferSize;
        if ((PreviousMode != KernelMode) && (Buffer != NULL) && (CallerSize != 0))
            ProbeForWrite(Buffer, CallerSize, 1);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Buffer == NULL)
        CallerSize = 0;

    Status = ExpWnfQuerySnapshot(InternalName.Value,
                                 HasTypeId,
                                 &CapturedTypeId,
                                 &Stamp,
                                 &Snapshot,
                                 &Required);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *ChangeStamp = Stamp;
        *BufferSize = Required;
        if (CallerSize >= Required)
        {
            if (Required != 0)
                RtlCopyMemory(Buffer, Snapshot, Required);
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, TAG_WNF_DATA);

    return Status;
}

NTSTATUS
NTAPI
NtQueryWnfStateNameInformation(PCWNF_STATE_NAME StateName,
                               ULONG NameInfoClass,
                               const VOID *ExplicitScope,
                               PVOID InfoBuffer,
                               ULONG InfoBufferSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    ULONG Value = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ExplicitScope);

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if (NameInfoClass > WnfInfoIsQuiescent)
        return STATUS_INVALID_INFO_CLASS;

    if ((InfoBuffer == NULL) || (InfoBufferSize != sizeof(ULONG)))
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(InfoBuffer);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Instance = ExpWnfReferenceNameInstance(InternalName.Value);
    switch (NameInfoClass)
    {
        case WnfInfoStateNameExist:
            Value = (Instance != NULL);
            Status = STATUS_SUCCESS;
            break;

        case WnfInfoSubscribersPresent:
            if (Instance == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            else
            {
                Value = (InterlockedCompareExchange(&Instance->SubscriptionCount, 0, 0) != 0);
                Status = STATUS_SUCCESS;
            }
            break;

        case WnfInfoIsQuiescent:
            if (Instance == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            else
            {
                Value = (InterlockedCompareExchange(&Instance->DeliveriesInFlight, 0, 0) == 0);
                Status = STATUS_SUCCESS;
            }
            break;
    }

    if (Instance != NULL)
        ExpWnfDereferenceNameInstance(Instance);

    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *(PULONG)InfoBuffer = Value;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtSubscribeWnfStateChange(PCWNF_STATE_NAME StateName,
                          WNF_CHANGE_STAMP ChangeStamp,
                          ULONG EventMask,
                          PULONG64 SubscriptionId)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    LIST_ENTRY ReapList;
    ULONG64 Id;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((PreviousMode != KernelMode) && (SubscriptionId != NULL))
    {
        _SEH2_TRY
        {
            ProbeForWrite(SubscriptionId, sizeof(*SubscriptionId), sizeof(ULONG64));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Subscription = ExpWnfAllocateSubscription(ChangeStamp, EventMask, NULL, NULL);
    if (Subscription == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Subscription->Process = PsGetCurrentProcess();
    Subscription->Thread = PsGetCurrentThread();

    Status = ExpWnfReferenceOrCreateNameInstance(&InternalName, &Instance);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Subscription, TAG_WNF_SUB);
        return Status;
    }

    ExpWnfAttachSubscription(Instance, Subscription, &InternalName);
    Id = Subscription->SubscriptionId;

    if (ExpWnfReadChangeStamp(Instance) > ChangeStamp)
        KeSetEvent(&Subscription->NotificationEvent, IO_NO_INCREMENT, FALSE);

    if (SubscriptionId != NULL)
    {
        _SEH2_TRY
        {
            *SubscriptionId = Id;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    if (!NT_SUCCESS(Status))
    {
        InitializeListHead(&ReapList);
        ExpWnfLockExclusive(&Instance->SubscriptionListLock);
        ExpWnfDetachSubscription(Subscription, &ReapList);
        ExpWnfUnlockExclusive(&Instance->SubscriptionListLock);
        ExpWnfReapSubscriptionList(&ReapList);
    }

    ExpWnfDereferenceNameInstance(Instance);
    return Status;
}

NTSTATUS
NTAPI
NtUnsubscribeWnfStateChange(PCWNF_STATE_NAME StateName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    PEPROCESS Process = PsGetCurrentProcess();
    LIST_ENTRY ReapList;
    PLIST_ENTRY Entry, NextEntry;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpWnfCaptureStateName(StateName, PreviousMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeListHead(&ReapList);

    Instance = ExpWnfReferenceNameInstance(InternalName.Value);
    if (Instance == NULL)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    ExpWnfLockExclusive(&Instance->SubscriptionListLock);
    for (Entry = Instance->SubscriptionListHead.Flink;
         Entry != &Instance->SubscriptionListHead;
         Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Subscription = CONTAINING_RECORD(Entry,
                                         WNF_SUBSCRIPTION,
                                         SubscriptionListEntry);
        if ((Subscription->Process == Process) &&
            (Subscription->Callback == NULL))
        {
            ExpWnfDetachSubscription(Subscription, &ReapList);
            Found = TRUE;
        }
    }
    ExpWnfUnlockExclusive(&Instance->SubscriptionListLock);
    ExpWnfDereferenceNameInstance(Instance);

    ExpWnfReapSubscriptionList(&ReapList);

    return Found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/* KERNEL EXPORTS ************************************************************/

NTSTATUS
NTAPI
ExQueryWnfStateData(PCWNF_STATE_NAME StateName,
                    PWNF_CHANGE_STAMP ChangeStamp,
                    PVOID Buffer,
                    PULONG BufferSize)
{
    WNF_STATE_NAME_INTERNAL InternalName;
    WNF_CHANGE_STAMP Stamp = 0;
    PVOID Snapshot = NULL;
    ULONG Required = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if ((ChangeStamp == NULL) || (BufferSize == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = ExpWnfCaptureStateName(StateName, KernelMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ExpWnfQuerySnapshot(InternalName.Value,
                                 FALSE,
                                 NULL,
                                 &Stamp,
                                 &Snapshot,
                                 &Required);
    if (!NT_SUCCESS(Status))
        return Status;

    *ChangeStamp = Stamp;
    if (Required == 0)
    {
        Status = STATUS_SUCCESS;
    }
    else if ((Buffer != NULL) && (*BufferSize >= Required))
    {
        RtlCopyMemory(Buffer, Snapshot, Required);
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    *BufferSize = Required;

    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, TAG_WNF_DATA);

    return Status;
}

NTSTATUS
NTAPI
ExSubscribeWnfStateChange(PVOID *SubscriptionHandle,
                          PCWNF_STATE_NAME StateName,
                          WNF_CHANGE_STAMP ChangeStamp,
                          ULONG Flags,
                          PWNF_KERNEL_CALLBACK Callback,
                          PVOID CallbackContext)
{
    WNF_STATE_NAME_INTERNAL InternalName;
    PWNF_NAME_INSTANCE Instance;
    PWNF_SUBSCRIPTION Subscription;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Flags);

    if ((SubscriptionHandle == NULL) || (Callback == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = ExpWnfCaptureStateName(StateName, KernelMode, &InternalName);
    if (!NT_SUCCESS(Status))
        return Status;

    Subscription = ExpWnfAllocateSubscription(ChangeStamp,
                                              0,
                                              Callback,
                                              CallbackContext);
    if (Subscription == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ExpWnfReferenceOrCreateNameInstance(&InternalName, &Instance);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Subscription, TAG_WNF_SUB);
        return Status;
    }

    ExpWnfAttachSubscription(Instance, Subscription, &InternalName);

    if (ExpWnfReadChangeStamp(Instance) > ChangeStamp)
    {
        ExpWnfLockShared(&Instance->SubscriptionListLock);
        if (!InterlockedCompareExchange(&Subscription->Removed, 0, 0))
            ExpWnfSignalSubscription(Subscription);
        ExpWnfUnlockShared(&Instance->SubscriptionListLock);
    }

    ExpWnfDereferenceNameInstance(Instance);
    *SubscriptionHandle = Subscription;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ExUnsubscribeWnfStateChange(PVOID SubscriptionHandle)
{
    PWNF_SUBSCRIPTION Subscription = SubscriptionHandle;
    PWNF_NAME_INSTANCE Instance;

    PAGED_CODE();

    if ((Subscription == NULL) || !ExpWnfInitialized)
        return;

    if (!InterlockedCompareExchange(&Subscription->Removed, 0, 0))
    {
        Instance = ExpWnfReferenceNameInstance(Subscription->StateName.Value);
        if (Instance != NULL)
        {
            ExpWnfLockExclusive(&Instance->SubscriptionListLock);
            if ((Subscription->NameInstance == Instance) &&
                !InterlockedCompareExchange(&Subscription->Removed, 0, 0))
            {
                ExpWnfDetachSubscription(Subscription, NULL);
            }
            ExpWnfUnlockExclusive(&Instance->SubscriptionListLock);
            ExpWnfDereferenceNameInstance(Instance);
        }
    }

    ExpWnfReapSubscription(Subscription);
}

