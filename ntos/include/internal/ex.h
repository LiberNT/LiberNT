/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Executive internals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef ExArchPostHalInitSystemPhase0
#define ExArchPostHalInitSystemPhase0() ((VOID)0)
#endif

#ifndef ExArchPostHalInitSystemPhase1
#define ExArchPostHalInitSystemPhase1() ((VOID)0)
#endif

extern RTL_TIME_ZONE_INFORMATION ExpTimeZoneInfo;

extern LARGE_INTEGER ExpTimeZoneBias;

extern ULONG ExpTimeZoneId;

extern ULONG ExpTickCountMultiplier;

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

extern ULONG_PTR ExpDebuggerPageIn;

#define SIG_ACPI 0x41435049

#define SIG_RSMB 0x52534D42

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

#define ExGetPreviousMode                   KeGetPreviousMode

#define EXHANDLE_TABLE_ENTRY_LOCK_BIT    1

#define LOW_LEVEL_ENTRIES   (PAGE_SIZE / sizeof(HANDLE_TABLE_ENTRY))

#define MID_LEVEL_ENTRIES   (PAGE_SIZE / sizeof(PHANDLE_TABLE_ENTRY))

#define HIGH_LEVEL_ENTRIES  (16777216 / (LOW_LEVEL_ENTRIES * MID_LEVEL_ENTRIES))

#define HANDLE_TABLE_ENTRY_INFO_SIZE \
    (LOW_LEVEL_ENTRIES * (sizeof(HANDLE_TABLE_ENTRY_INFO) + sizeof(ULONG)))

#define MAX_MID_INDEX       (MID_LEVEL_ENTRIES * LOW_LEVEL_ENTRIES)

#define MAX_HIGH_INDEX      (MID_LEVEL_ENTRIES * MID_LEVEL_ENTRIES * LOW_LEVEL_ENTRIES)

#define ExpChangeRundown(x, y, z)   (ULONG_PTR)InterlockedCompareExchangePointer(&(x)->Ptr, (PVOID)(y), (PVOID)(z))

#define ExpChangePushlock(x, y, z)  InterlockedCompareExchangePointer((PVOID*)(x), (PVOID)(y), (PVOID)(z))

#define ExpSetRundown(x, y)         InterlockedExchangePointer(&(x)->Ptr, (PVOID)(y))

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
            UCHAR NodeId[6 ];
        };
        UCHAR GuidInit[8];
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

typedef NTSTATUS
(NTAPI *PEXP_SHARE_DEVICE_ADDRESS_SPACE)(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG ReturnedAsid);

typedef struct _EXP_DEVICE_ADDRESS_SPACE_PROVIDER
{
    PEXP_SHARE_DEVICE_ADDRESS_SPACE ShareAddressSpace;
} EXP_DEVICE_ADDRESS_SPACE_PROVIDER, *PEXP_DEVICE_ADDRESS_SPACE_PROVIDER;

typedef NTSTATUS
(NTAPI *PEXP_MANAGE_MEMORY_PARTITION)(
    _In_opt_ PVOID ProviderContext,
    _In_opt_ HANDLE SourceHandle,
    _In_ ULONG PartitionInformationClass,
    _Inout_updates_bytes_opt_(PartitionInformationLength) PVOID PartitionInformation,
    _In_ ULONG PartitionInformationLength);

typedef BOOLEAN
(NTAPI *PEX_SWEEP_HANDLE_CALLBACK)(
    PHANDLE_TABLE_ENTRY HandleTableEntry,
    HANDLE Handle,
    PVOID Context
);

typedef BOOLEAN
(NTAPI *PEX_DUPLICATE_HANDLE_CALLBACK)(
    IN PEPROCESS Process,
    IN PHANDLE_TABLE HandleTable,
    IN PHANDLE_TABLE_ENTRY HandleTableEntry,
    IN PHANDLE_TABLE_ENTRY NewEntry
);

typedef BOOLEAN
(NTAPI *PEX_CHANGE_HANDLE_CALLBACK)(
    PHANDLE_TABLE_ENTRY HandleTableEntry,
    ULONG_PTR Context
);

#define ExInitializePushLock _ExInitializePushLock

#define InterlockedDecrementUL(Addend) \
   (ULONG)InterlockedDecrement((PLONG)(Addend))

#define InterlockedIncrementUL(Addend) \
   (ULONG)InterlockedIncrement((PLONG)(Addend))

#define InterlockedCompareExchangeSizeT(Destination, Exchange, Comperand) \
   (SIZE_T)InterlockedCompareExchangePointer((PVOID*)(Destination), (PVOID)(SIZE_T)(Exchange), (PVOID)(SIZE_T)(Comperand))

#ifdef __cplusplus
}
#endif

extern PKWIN32_GLOBALATOMTABLE_CALLOUT ExpGlobalAtomTableCallout;

PEX_CALLBACK_ROUTINE_BLOCK
NTAPI
ExAllocateCallBack(
    _In_ PEX_CALLBACK_FUNCTION Function,
    _In_ PVOID Context);

BOOLEAN
NTAPI
ExCompareExchangeCallBack(
    _Inout_ PEX_CALLBACK CallBack,
    _In_ PEX_CALLBACK_ROUTINE_BLOCK NewBlock,
    _In_ PEX_CALLBACK_ROUTINE_BLOCK OldBlock);

VOID
NTAPI
ExDereferenceCallBackBlock(
    _Inout_ PEX_CALLBACK CallBack,
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock);

VOID
NTAPI
ExFreeCallBack(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock);

PVOID
NTAPI
ExGetCallBackBlockContext(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock);

PEX_CALLBACK_FUNCTION
NTAPI
ExGetCallBackBlockRoutine(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock);

VOID
NTAPI
ExInitializeCallBack(
    _Inout_ PEX_CALLBACK Callback);

PEX_CALLBACK_ROUTINE_BLOCK
NTAPI
ExReferenceCallBackBlock(
    _Inout_ PEX_CALLBACK CallBack);

VOID
NTAPI
ExWaitForCallBacks(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeCallbackObjects(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeCallbacks(VOID);

VOID
NTAPI
ExpDebuggerWorker(
    _In_ PVOID Context);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeEventImplementation(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeKeyedEventImplementation(VOID);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeSecureBoot(VOID);

BOOLEAN
NTAPI
ExChangeHandle(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle,
    _In_ PEX_CHANGE_HANDLE_CALLBACK ChangeRoutine,
    _In_ ULONG_PTR Context);

HANDLE
NTAPI
ExCreateHandle(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry);

HANDLE
NTAPI
ExCreateLockedHandle(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _Outptr_ PHANDLE_TABLE_ENTRY *LockedEntry);

PHANDLE_TABLE
NTAPI
ExCreateHandleTable(
    _In_opt_ PEPROCESS Process,
    _In_ BOOLEAN InsertInList);

BOOLEAN
NTAPI
ExDestroyHandle(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle,
    _In_opt_ PHANDLE_TABLE_ENTRY HandleTableEntry);

VOID
NTAPI
ExDestroyHandleTable(
    _In_ PHANDLE_TABLE HandleTable,
    _In_opt_ PVOID DestroyHandleProcedure);

PHANDLE_TABLE
NTAPI
ExDupHandleTable(
    _In_ PEPROCESS Process,
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PEX_DUPLICATE_HANDLE_CALLBACK DupHandleProcedure,
    _In_ ULONG_PTR Mask);

PULONG
NTAPI
ExGetHandleCachedReferenceCount(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry);

PHANDLE_TABLE_ENTRY
NTAPI
ExMapHandleToPointer(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle);

VOID
NTAPI
ExSweepHandleTable(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PEX_SWEEP_HANDLE_CALLBACK EnumHandleProcedure,
    _In_ PVOID Context);

VOID
NTAPI
ExUnlockHandleTableEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry);

PLIST_ENTRY
NTAPI
ExpFirstHandleTableEntry(
    _Out_ PULONG Bucket);

LONG
NTAPI
ExpGetHandleCount(
    _In_ PHANDLE_TABLE HandleTable);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeHandleTables(VOID);

BOOLEAN
NTAPI
ExpLockHandleTableEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry);

PLIST_ENTRY
NTAPI
ExpNextHandleTableEntry(
    _Inout_ PULONG Bucket,
    _In_ PLIST_ENTRY Entry);

LONG
NTAPI
ExSystemExceptionFilter(VOID);

CODE_SEG("INIT")
VOID
NTAPI
HeadlessInit(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeExecutive(
    _In_ ULONG Cpu,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
Phase1Initialization(
    _In_ PVOID Context);

CODE_SEG("INIT")
VOID
NTAPI
ExInitPoolLookasidePointers(VOID);

CODE_SEG("INIT")
VOID
NTAPI
ExInitializeSystemLookasideList(
    _In_ PGENERAL_LOOKASIDE List,
    _In_ POOL_TYPE Type,
    _In_ ULONG Size,
    _In_ ULONG Tag,
    _In_ USHORT MaximumDepth,
    _In_ PLIST_ENTRY ListHead);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitLookasideLists(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeProfileImplementation(VOID);

VOID
FASTCALL
ExBlockPushLock(
    _In_ PEX_PUSH_LOCK PushLock,
    _In_ PVOID WaitBlock);

VOID
FASTCALL
ExWaitForUnblockPushLock(
    _In_ PEX_PUSH_LOCK PushLock,
    _In_ PVOID WaitBlock);

VOID
FASTCALL
ExfUnblockPushLock(
    _In_ PEX_PUSH_LOCK PushLock,
    _In_ PVOID CurrentWaitBlock);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializePushLocks(VOID);

BOOLEAN
NTAPI
ExTryToAcquireResourceExclusiveLite(
    _In_ PERESOURCE Resource);

CODE_SEG("INIT")
VOID
NTAPI
ExpResourceInitialization(VOID);

BOOLEAN
FASTCALL
ExfAcquireRundownProtection(
    _Inout_ PEX_RUNDOWN_REF RunRef);

BOOLEAN
FASTCALL
ExfAcquireRundownProtectionEx(
    _Inout_ PEX_RUNDOWN_REF RunRef,
    _In_ ULONG Count);

VOID
FASTCALL
ExfInitializeRundownProtection(
    _Out_ PEX_RUNDOWN_REF RunRef);

VOID
FASTCALL
ExfReInitializeRundownProtection(
    _Out_ PEX_RUNDOWN_REF RunRef);

VOID
FASTCALL
ExfReleaseRundownProtection(
    _Inout_ PEX_RUNDOWN_REF RunRef);

VOID
FASTCALL
ExfReleaseRundownProtectionEx(
    _Inout_ PEX_RUNDOWN_REF RunRef,
    _In_ ULONG Count);

VOID
FASTCALL
ExfRundownCompleted(
    _Out_ PEX_RUNDOWN_REF RunRef);

VOID
FASTCALL
ExfWaitForRundownProtectionRelease(
    _Inout_ PEX_RUNDOWN_REF RunRef);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeMutantImplementation(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeSemaphoreImplementation(VOID);

VOID
NTAPI
ExShutdownSystem(VOID);

NTSTATUS
NTAPI
ExLockUserBuffer(
    PVOID BaseAddress,
    ULONG Length,
    KPROCESSOR_MODE AccessMode,
    LOCK_OPERATION Operation,
    PVOID *MappedSystemVa,
    PMDL *OutMdl);

VOID
NTAPI
ExUnlockUserBuffer(
    PMDL Mdl);

NTSTATUS
NTAPI
ExpGetRawSMBiosTable(
    _Out_opt_ PVOID Buffer,
    _Out_ ULONG *OutSize,
    _In_ ULONG BufferSize);

BOOLEAN
NTAPI
ExAcquireTimeRefreshLock(
    _In_ BOOLEAN Wait);

ULONG
NTAPI
ExComputeTickCountMultiplier(
    _In_ ULONG TimeIncrement);

BOOLEAN
NTAPI
ExRefreshTimeZoneInformation(
    _In_ PLARGE_INTEGER SystemBootTime);

VOID
NTAPI
ExReleaseProcessTimerResolution(
    _Inout_ PEPROCESS Process);

VOID
NTAPI
ExReleaseTimeRefreshLock(VOID);

VOID
NTAPI
ExUpdateSystemTimeFromCmos(
    _In_ BOOLEAN UpdateInterruptTime,
    _In_ ULONG MaxSepInSeconds);

VOID
NTAPI
ExpSetHighResolutionClock(
    _In_ ULONG Resolution);

NTSTATUS
ExpSetTimeZoneInformation(
    _In_ PRTL_TIME_ZONE_INFORMATION TimeZoneInformation);

VOID
NTAPI
ExTimerRundown(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeTimerImplementation(VOID);

VOID
NTAPI
ExAllocateLocallyUniqueId(
    _Out_ LUID *LocallyUniqueId);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExLuidInitialization(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpUuidInitialization(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpWin32kInit(VOID);

VOID
NTAPI
ExSwapinWorkerThreads(
    _In_ BOOLEAN AllowSwap);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeWorkerThreads(VOID);

CODE_SEG("INIT")
VOID
NTAPI
XIPInit(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

NTSTATUS
NTAPI
ExpCreateMemoryPartition(
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PEXP_MANAGE_MEMORY_PARTITION ManagePartition,
    _In_opt_ PVOID ProviderContext,
    _Out_ PHANDLE PartitionHandle);

BOOLEAN
NTAPI
ExpInitializePartitionImplementation(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeReserveObjectTypes(VOID);

NTSTATUS
NTAPI
ExpRegisterDeviceAddressSpaceProvider(
    _In_ const EXP_DEVICE_ADDRESS_SPACE_PROVIDER *Provider);

VOID
NTAPI
ExTrimPoolHeaps(VOID);

NTSTATUS
NTAPI
ExGetPoolTagInfo(
    _In_ PSYSTEM_POOLTAG_INFORMATION SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Inout_opt_ PULONG ReturnLength);

VOID
NTAPI
EtwpShutdownLoggers(VOID);

NTSTATUS
NTAPI
WmipQueryRawSMBiosTables(
    _Inout_ ULONG *InOutBufferSize,
    _Out_opt_ PVOID OutBuffer);

NTSTATUS
NTAPI
ExGetSystemFirmwareTable(
    _In_ ULONG FirmwareTableProviderSignature,
    _In_ ULONG FirmwareTableID,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PVOID FirmwareTableBuffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength);

NTSTATUS
NTAPI
ExpEnumerateAcpiTables(
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnLength);

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


    if (!InterlockedBitTestAndReset(&FastMutex->Count, FM_LOCK_BIT_V))
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


    if (!InterlockedBitTestAndReset(&FastMutex->Count, FM_LOCK_BIT_V))
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
