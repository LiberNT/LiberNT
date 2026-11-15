#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

extern RTL_TIME_ZONE_INFORMATION ExpTimeZoneInfo;
extern LARGE_INTEGER ExpTimeZoneBias;
extern ULONG ExpTimeZoneId;


extern ULONG ExpLastTimeZoneBias;
extern BOOLEAN ExpRealTimeIsUniversal;
extern FAST_MUTEX ExpEnvironmentLock;
extern ERESOURCE ExpFirmwareTableResource;
extern ERESOURCE ExpTimeRefreshLock;
extern LIST_ENTRY ExpFirmwareTableProviderListHead;
extern FIRMWARE_TYPE ExpFirmwareType;
extern SYSTEM_BOOT_ENVIRONMENT_INFORMATION ExpBootEnvironmentInformation;
extern ULONG ExpResourceTimeoutCount;
extern LIST_ENTRY ExpSystemResourcesList;
extern KSPIN_LOCK ExpResourceSpinLock;
extern ULONG ExpAnsiCodePageDataOffset, ExpOemCodePageDataOffset;
extern ULONG ExpUnicodeCaseTableDataOffset;
extern PVOID ExpNlsSectionPointer;
extern EX_WORK_QUEUE ExWorkerQueue[MaximumWorkQueue];
extern ULONG NtGlobalFlag;
extern UNICODE_STRING NtSystemRoot;
extern ULONG ExpInitializationPhase;
extern ULONG ExpAltTimeZoneBias;
extern LIST_ENTRY ExSystemLookasideListHead;
extern PCALLBACK_OBJECT PowerStateCallback;
extern PCALLBACK_OBJECT SetSystemTimeCallback;
extern LIST_ENTRY ExPoolLookasideListHead;
extern LIST_ENTRY ExpNonPagedLookasideListHead;
extern LIST_ENTRY ExpPagedLookasideListHead;
extern KSPIN_LOCK ExpNonPagedLookasideListLock;
extern KSPIN_LOCK ExpPagedLookasideListLock;
extern ULONG ExCriticalWorkerThreads;
extern ULONG ExDelayedWorkerThreads;

extern PVOID ExpDefaultErrorPort;
extern PEPROCESS ExpDefaultErrorPortProcess;
extern BOOLEAN ExReadyForErrors;

extern ULONG NtMajorVersion;
extern ULONG NtMinorVersion;
extern ULONG NtBuildNumber;
extern ULONG CmNtSpBuildNumber;
extern ULONG CmNtCSDVersion;
extern ULONG CmNtCSDReleaseType;
extern WCHAR CmSuiteBuffer[128];
extern ULONG CmSuiteBufferLength;
extern ULONG CmSuiteBufferType;
extern ULONG CmTsAppCompat;
extern ULONG ExSuiteMask;
extern UNICODE_STRING CmVersionString;
extern UNICODE_STRING CmCSDVersionString;
extern CHAR NtBuildLab[];

typedef enum _WINKD_WORKER_STATE
{
    WinKdWorkerReady = 0,
    WinKdWorkerStart,
    WinKdWorkerInitialized
} WINKD_WORKER_STATE;

extern WORK_QUEUE_ITEM ExpDebuggerWorkItem;
extern WINKD_WORKER_STATE ExpDebuggerWork;
extern PEPROCESS ExpDebuggerProcessAttach;
extern PEPROCESS ExpDebuggerProcessKill;


#define SIG_ACPI 0x41435049


typedef struct
{
    PCALLBACK_OBJECT *CallbackObject;
    PWSTR Name;
} SYSTEM_CALLBACKS;

typedef struct _HARDERROR_USER_PARAMETERS
{
    ULONG_PTR Parameters[MAXIMUM_HARDERROR_PARAMETERS];
    UNICODE_STRING Strings[MAXIMUM_HARDERROR_PARAMETERS];
    WCHAR Buffer[ANYSIZE_ARRAY];
} HARDERROR_USER_PARAMETERS, *PHARDERROR_USER_PARAMETERS;

#ifdef _WIN64
#define MAX_FAST_REFS           15
#else
#define MAX_FAST_REFS           7
#endif

C_ASSERT(MAX_FAST_REFS + 1 == MEMORY_ALLOCATION_ALIGNMENT);

#define ExAcquireRundownProtection          _ExAcquireRundownProtection
#define ExReleaseRundownProtection          _ExReleaseRundownProtection
#define ExInitializeRundownProtection       _ExInitializeRundownProtection
#define ExWaitForRundownProtectionRelease   _ExWaitForRundownProtectionRelease
#define ExRundownCompleted                  _ExRundownCompleted


#define EXHANDLE_TABLE_ENTRY_LOCK_BIT    1

#define LOW_LEVEL_ENTRIES   (PAGE_SIZE / sizeof(HANDLE_TABLE_ENTRY))
#define MID_LEVEL_ENTRIES   (PAGE_SIZE / sizeof(PHANDLE_TABLE_ENTRY))
#define HIGH_LEVEL_ENTRIES  (16777216 / (LOW_LEVEL_ENTRIES * MID_LEVEL_ENTRIES))
#define HANDLE_TABLE_ENTRY_INFO_SIZE \
    (LOW_LEVEL_ENTRIES * (sizeof(HANDLE_TABLE_ENTRY_INFO) + sizeof(ULONG)))

#define MAX_MID_INDEX       (MID_LEVEL_ENTRIES * LOW_LEVEL_ENTRIES)
#define MAX_HIGH_INDEX      (MID_LEVEL_ENTRIES * MID_LEVEL_ENTRIES * LOW_LEVEL_ENTRIES)

#define ExpChangeRundown(x, y, z)   (ULONG_PTR)InterlockedCompareExchangePointer(&(x)->Ptr, (PVOID)(y), (PVOID)(z))



typedef struct _UUID_CACHED_VALUES_STRUCT
{
    ULONGLONG Time;
    LONG AllocatedCount;
    union
    {
        struct
        {
            UCHAR ClockSeqHiAndReserved;
            UCHAR ClockSeqLow;
        };
    };
} UUID_CACHED_VALUES_STRUCT, *PUUID_CACHED_VALUES_STRUCT;

C_ASSERT(RTL_FIELD_SIZE(UUID_CACHED_VALUES_STRUCT, GuidInit) == RTL_FIELD_SIZE(UUID, Data4));

typedef struct _EX_RESERVE_OBJECT
{
    volatile LONG InUse;
    KAPC Apc;
} EX_RESERVE_OBJECT, *PEX_RESERVE_OBJECT;

extern POBJECT_TYPE ExUserApcReserveObjectType;
extern POBJECT_TYPE ExIoCompletionReserveObjectType;


NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTAPI

VOID
NTAPI

CODE_SEG("INIT")
NTAPI

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeCallbacks(VOID);

NTAPI

CODE_SEG("INIT")
BOOLEAN
NTAPI

CODE_SEG("INIT")
BOOLEAN
NTAPI

CODE_SEG("INIT")
VOID
NTAPI

NTAPI

NTAPI



NTAPI


NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

CODE_SEG("INIT")
NTAPI

BOOLEAN
NTAPI

NTAPI

CODE_SEG("INIT")
VOID
NTAPI

CODE_SEG("INIT")
VOID
NTAPI


VOID
NTAPI

NTAPI

VOID
NTAPI

BOOLEAN
NTAPI


VOID

NTAPI

NTAPI

VOID
NTAPI


VOID
FASTCALL
ExfInitializeRundownProtection(

VOID
FASTCALL
ExfReInitializeRundownProtection(

VOID
FASTCALL
ExfReleaseRundownProtection(

VOID
FASTCALL
ExfReleaseRundownProtectionEx(

VOID
FASTCALL
ExfRundownCompleted(

VOID
FASTCALL
ExfWaitForRundownProtectionRelease(






NTAPI

NTAPI

VOID
NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTAPI

NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI

NTAPI

BOOLEAN
NTAPI

VOID
NTAPI

NTAPI

NTAPI


NTAPI


FORCEINLINE
VOID
ExDoCallBack(IN OUT PEX_CALLBACK Callback,
             IN PVOID Context,
             IN PVOID Argument1,
             IN PVOID Argument2)
{
    PEX_CALLBACK_ROUTINE_BLOCK CallbackBlock;
    PEX_CALLBACK_FUNCTION Function;

    CallbackBlock = ExReferenceCallBackBlock(Callback);
    if (CallbackBlock)
    {
        Function = ExGetCallBackBlockRoutine(CallbackBlock);

        Function(Context, Argument1, Argument2);

        ExDereferenceCallBackBlock(Callback, CallbackBlock);
    }
}

FORCEINLINE
PVOID
ExGetObjectFastReference(IN EX_FAST_REF FastRef)
{
    return (PVOID)(FastRef.Value & ~MAX_FAST_REFS);
}

FORCEINLINE
ULONG
ExGetCountFastReference(IN EX_FAST_REF FastRef)
{
    return (ULONG)FastRef.RefCnt;
}

FORCEINLINE
VOID
ExInitializeFastReference(OUT PEX_FAST_REF FastRef,
                          IN OPTIONAL PVOID Object)
{
    ASSERT((((ULONG_PTR)Object) & MAX_FAST_REFS) == 0);

    if (!Object)
    {
        FastRef->Object = NULL;
    }
    else
    {
        FastRef->Value = (ULONG_PTR)Object | MAX_FAST_REFS;
    }
}

FORCEINLINE
EX_FAST_REF
ExAcquireFastReference(IN OUT PEX_FAST_REF FastRef)
{
    EX_FAST_REF OldValue, NewValue;

    for (;;)
    {
        OldValue = *FastRef;
        if (OldValue.RefCnt)
        {
            NewValue.Value = OldValue.Value - 1;
            NewValue.Object = ExpChangePushlock(&FastRef->Object,
                                                NewValue.Object,
                                                OldValue.Object);
            if (NewValue.Object != OldValue.Object) continue;
        }

        break;
    }

    return OldValue;
}

FORCEINLINE
BOOLEAN
ExInsertFastReference(IN OUT PEX_FAST_REF FastRef,
                      IN PVOID Object)
{
    EX_FAST_REF OldValue, NewValue;

    ASSERT(!(((ULONG_PTR)Object) & MAX_FAST_REFS));

    for (;;)
    {
        OldValue = *FastRef;

        if (((OldValue.RefCnt + MAX_FAST_REFS) > MAX_FAST_REFS) ||
            ((OldValue.Value &~ MAX_FAST_REFS) != (ULONG_PTR)Object))
        {
            return FALSE;
        }

        NewValue.Value = OldValue.Value + MAX_FAST_REFS;
        NewValue.Object = ExpChangePushlock(&FastRef->Object,
                                            NewValue.Object,
                                            OldValue.Object);
        if (NewValue.Object != OldValue.Object) continue;

        break;
    }

    return TRUE;
}

FORCEINLINE
BOOLEAN
ExReleaseFastReference(IN PEX_FAST_REF FastRef,
                       IN PVOID Object)
{
    EX_FAST_REF OldValue, NewValue;

    ASSERT(Object != NULL);
    ASSERT(!(((ULONG_PTR)Object) & MAX_FAST_REFS));

    for (;;)
    {
        OldValue = *FastRef;

        if ((OldValue.Value ^ (ULONG_PTR)Object) >= MAX_FAST_REFS) return FALSE;

        NewValue.Value = OldValue.Value + 1;
        NewValue.Object = ExpChangePushlock(&FastRef->Object,
                                            NewValue.Object,
                                            OldValue.Object);
        if (NewValue.Object != OldValue.Object) continue;

        break;
    }

    return TRUE;
}

FORCEINLINE
EX_FAST_REF
ExSwapFastReference(IN PEX_FAST_REF FastRef,
                    IN PVOID Object)
{
    EX_FAST_REF NewValue, OldValue;

    ASSERT((((ULONG_PTR)Object) & MAX_FAST_REFS) == 0);

    if (!Object)
    {
        NewValue.Object = NULL;
    }
    else
    {
        NewValue.Value = (ULONG_PTR)Object | MAX_FAST_REFS;
    }

    OldValue.Object = InterlockedExchangePointer(&FastRef->Object, NewValue.Object);
    return OldValue;
}

FORCEINLINE
EX_FAST_REF
ExCompareSwapFastReference(IN PEX_FAST_REF FastRef,
                           IN PVOID Object,
                           IN PVOID OldObject)
{
    EX_FAST_REF OldValue, NewValue;

    ASSERT(!(((ULONG_PTR)Object) & MAX_FAST_REFS));
    for (;;)
    {
        OldValue = *FastRef;

        if (!((OldValue.Value ^ (ULONG_PTR)OldObject) <= MAX_FAST_REFS)) break;

        if (Object)
        {
            NewValue.Value = (ULONG_PTR)Object | MAX_FAST_REFS;
        }
        else
        {
            NewValue.Value = (ULONG_PTR)Object;
        }

        NewValue.Object = ExpChangePushlock(&FastRef->Object,
                                            NewValue.Object,
                                            OldValue.Object);
        if (NewValue.Object != OldValue.Object) continue;

        break;
    }

    return OldValue;
}

FORCEINLINE
PEX_RUNDOWN_REF
ExGetRunRefForGivenProcessor(IN PEX_RUNDOWN_REF_CACHE_AWARE RunRefCacheAware,
                             IN ULONG ProcNumber)
{
    return (PEX_RUNDOWN_REF)((ULONG_PTR)RunRefCacheAware->RunRefs +
                             RunRefCacheAware->RunRefSize *
                             (ProcNumber % RunRefCacheAware->Number));
}

FORCEINLINE
BOOLEAN
_ExAcquireRundownProtection(IN PEX_RUNDOWN_REF RunRef)
{
    ULONG_PTR Value, NewValue;

    Value = RunRef->Count &~ EX_RUNDOWN_ACTIVE;

    NewValue = Value + EX_RUNDOWN_COUNT_INC;

    NewValue = ExpChangeRundown(RunRef, NewValue, Value);
    if (NewValue != Value)
    {
        return ExfAcquireRundownProtection(RunRef);
    }

    return TRUE;
}

FORCEINLINE
VOID
_ExReleaseRundownProtection(IN PEX_RUNDOWN_REF RunRef)
{
    ULONG_PTR Value, NewValue;

    Value = RunRef->Count &~ EX_RUNDOWN_ACTIVE;

    NewValue = Value - EX_RUNDOWN_COUNT_INC;

    NewValue = ExpChangeRundown(RunRef, NewValue, Value);

    if (NewValue != Value)
    {
        ExfReleaseRundownProtection(RunRef);
    }
    else
    {
        ASSERT((Value >= EX_RUNDOWN_COUNT_INC) || (KeNumberProcessors > 1));
    }
}

FORCEINLINE
VOID
_ExInitializeRundownProtection(IN PEX_RUNDOWN_REF RunRef)
{
    RunRef->Count = 0;
}

FORCEINLINE
VOID
_ExWaitForRundownProtectionRelease(IN PEX_RUNDOWN_REF RunRef)
{
    ULONG_PTR Value;

    Value = ExpChangeRundown(RunRef, EX_RUNDOWN_ACTIVE, 0);
    if ((Value) && (Value != EX_RUNDOWN_ACTIVE))
    {
        ExfWaitForRundownProtectionRelease(RunRef);
    }
}

FORCEINLINE
VOID
_ExRundownCompleted(IN PEX_RUNDOWN_REF RunRef)
{
    ASSERT((RunRef->Count & EX_RUNDOWN_ACTIVE) != 0);

    ExpSetRundown(RunRef, EX_RUNDOWN_ACTIVE);
}

FORCEINLINE
VOID
_ExInitializePushLock(OUT PEX_PUSH_LOCK PushLock)
{
    PushLock->Ptr = 0;
}

FORCEINLINE
VOID
ExAcquirePushLockExclusive(PEX_PUSH_LOCK PushLock)
{
#ifdef _WIN64
    if (InterlockedBitTestAndSet64((PLONG64)PushLock, EX_PUSH_LOCK_LOCK_V))
#else
    if (InterlockedBitTestAndSet((PLONG)PushLock, EX_PUSH_LOCK_LOCK_V))
#endif
    {
        ExfAcquirePushLockExclusive(PushLock);
    }

    ASSERT(PushLock->Locked);
}

FORCEINLINE
BOOLEAN
ExTryToAcquirePushLockExclusive(PEX_PUSH_LOCK PushLock)
{
#ifdef _WIN64
    if (InterlockedBitTestAndSet64((PLONG64)PushLock, EX_PUSH_LOCK_LOCK_V))
#else
    if (InterlockedBitTestAndSet((PLONG)PushLock, EX_PUSH_LOCK_LOCK_V))
#endif
    {
        return FALSE;
    }

    ASSERT (PushLock->Locked);
    return TRUE;
}

FORCEINLINE
VOID
ExAcquirePushLockShared(PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK NewValue;

    NewValue.Value = EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC;
    if (ExpChangePushlock(PushLock, NewValue.Ptr, 0))
    {
        ExfAcquirePushLockShared(PushLock);
    }

    ASSERT(PushLock->Locked);
}

FORCEINLINE
BOOLEAN
ExConvertPushLockSharedToExclusive(IN PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue;

    OldValue.Value = EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC;

    if (ExpChangePushlock(PushLock, EX_PUSH_LOCK_LOCK, OldValue.Value) !=
        OldValue.Ptr)
    {
        return FALSE;
    }

    ASSERT(PushLock->Locked);
    return TRUE;
}

FORCEINLINE
VOID
ExReleasePushLockShared(PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue;

    ASSERT(PushLock->Locked);

    OldValue.Value = EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC;
    if (ExpChangePushlock(PushLock, 0, OldValue.Ptr) != OldValue.Ptr)
    {
        ExfReleasePushLockShared(PushLock);
    }
}

FORCEINLINE
VOID
ExReleasePushLockExclusive(PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue;

    ASSERT(PushLock->Locked);

    OldValue.Value = InterlockedExchangeAddSizeT((PSIZE_T)PushLock,
                                                 -(SSIZE_T)EX_PUSH_LOCK_LOCK);

    ASSERT(OldValue.Locked);
    ASSERT(OldValue.Waiting || OldValue.Shared == 0);

    if ((OldValue.Waiting) && !(OldValue.Waking))
    {
        ExfTryToWakePushLock(PushLock);
    }
}

FORCEINLINE
VOID
ExReleasePushLock(PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue = *PushLock;
    EX_PUSH_LOCK NewValue;

    ASSERT(OldValue.Locked);

    if (OldValue.Shared > 1)
    {
        NewValue.Value = OldValue.Value - EX_PUSH_LOCK_SHARE_INC;
    }
    else
    {
        NewValue.Value = 0;
    }

    if ((OldValue.Waiting) ||
        (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) !=
         OldValue.Ptr))
    {
        ExfReleasePushLock(PushLock);
    }
}

FORCEINLINE
VOID
ExpReleaseFastMutexState(IN OUT PFAST_MUTEX FastMutex)
{
#if (NTDDI_VERSION >= NTDDI_VISTA)
    LONG OldValue, NewValue;

    OldValue = InterlockedExchangeAdd(&FastMutex->Count, FM_LOCK_BIT);
    ASSERT((OldValue & FM_LOCK_BIT) == 0);
    if (OldValue & FM_LOCK_BIT)
        return;

    if ((OldValue != 0) && !(OldValue & FM_LOCK_WAITER_WOKEN))
    {
        OldValue += FM_LOCK_BIT;
        NewValue = OldValue + FM_LOCK_WAITER_WOKEN - FM_LOCK_WAITER_INC;
        if (InterlockedCompareExchange(&FastMutex->Count, NewValue, OldValue) == OldValue)
        {
            KeSetEventBoostPriority(&FastMutex->Event, NULL);
        }
    }
#else
    if (InterlockedIncrement(&FastMutex->Count) <= 0)
    {
        KeSetEventBoostPriority(&FastMutex->Event, NULL);
    }
#endif
}

FORCEINLINE
VOID
_ExAcquireFastMutexUnsafe(IN PFAST_MUTEX FastMutex)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT((KeGetCurrentIrql() == APC_LEVEL) ||
           (Thread->CombinedApcDisable != 0) ||
           (Thread->Teb == NULL) ||
           (Thread->Teb >= (PTEB)MM_SYSTEM_RANGE_START));
    ASSERT(FastMutex->Owner != Thread);

#if (NTDDI_VERSION >= NTDDI_VISTA)
    if (!InterlockedBitTestAndReset(&FastMutex->Count, FM_LOCK_BIT_V))
#else
    if (InterlockedDecrement(&FastMutex->Count))
#endif
    {
        KiAcquireFastMutex(FastMutex);
    }

    FastMutex->Owner = Thread;
}

FORCEINLINE
VOID
_ExReleaseFastMutexUnsafe(IN OUT PFAST_MUTEX FastMutex)
{
    ASSERT((KeGetCurrentIrql() == APC_LEVEL) ||
           (KeGetCurrentThread()->CombinedApcDisable != 0) ||
           (KeGetCurrentThread()->Teb == NULL) ||
           (KeGetCurrentThread()->Teb >= (PTEB)MM_SYSTEM_RANGE_START));
    ASSERT(FastMutex->Owner == KeGetCurrentThread());

    FastMutex->Owner = NULL;

    ExpReleaseFastMutexState(FastMutex);
}

FORCEINLINE
VOID
_ExAcquireFastMutex(IN PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    KeRaiseIrql(APC_LEVEL, &OldIrql);

#if (NTDDI_VERSION >= NTDDI_VISTA)
    if (!InterlockedBitTestAndReset(&FastMutex->Count, FM_LOCK_BIT_V))
#else
    if (InterlockedDecrement(&FastMutex->Count))
#endif
    {
        KiAcquireFastMutex(FastMutex);
    }

    FastMutex->Owner = KeGetCurrentThread();
    FastMutex->OldIrql = OldIrql;
}

FORCEINLINE
VOID
_ExReleaseFastMutex(IN OUT PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;
    ASSERT(KeGetCurrentIrql() == APC_LEVEL);

    FastMutex->Owner = NULL;
    OldIrql = (KIRQL)FastMutex->OldIrql;

    ExpReleaseFastMutexState(FastMutex);

    KeLowerIrql(OldIrql);
}

FORCEINLINE
BOOLEAN
_ExTryToAcquireFastMutex(IN OUT PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    KeRaiseIrql(APC_LEVEL, &OldIrql);

    if (InterlockedCompareExchange(&FastMutex->Count, 0, 1) == 1)
    {
        FastMutex->Owner = KeGetCurrentThread();
        FastMutex->OldIrql = OldIrql;
        return TRUE;
    }
    else
    {
        KeLowerIrql(OldIrql);
        YieldProcessor();
        return FALSE;
    }
}

FORCEINLINE
VOID
_ExEnterCriticalRegionAndAcquireFastMutexUnsafe(IN OUT PFAST_MUTEX FastMutex)
{
    KeEnterCriticalRegion();

    _ExAcquireFastMutexUnsafe(FastMutex);
}

FORCEINLINE
VOID
_ExReleaseFastMutexUnsafeAndLeaveCriticalRegion(IN OUT PFAST_MUTEX FastMutex)
{
    _ExReleaseFastMutexUnsafe(FastMutex);

    KeLeaveCriticalRegion();
}
