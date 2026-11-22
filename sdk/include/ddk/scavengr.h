#ifndef _SCAVENGR_H_
#define _SCAVENGR_H_

extern KMUTEX RxScavengerMutex;

#define RX_SCAVENGER_FINALIZATION_TIME_INTERVAL (10 * 1000 * 1000 * 10)

typedef struct _RX_SCAVENGER_ENTRY
{
    LIST_ENTRY List;
    UCHAR Type;
    UCHAR Operation;
    UCHAR State;
    UCHAR Flags;
    struct _RX_SCAVENGER_ENTRY *pContinuationEntry;
} RX_SCAVENGER_ENTRY, *PRX_SCAVENGER_ENTRY;

#define RxInitializeScavengerEntry(ScavengerEntry) \
    (ScavengerEntry)->State  = 0;                  \
    (ScavengerEntry)->Flags  = 0;                  \
    (ScavengerEntry)->Type   = 0;                  \
    (ScavengerEntry)->Operation = 0;               \
    InitializeListHead(&(ScavengerEntry)->List);   \
    (ScavengerEntry)->pContinuationEntry = NULL

#define RxAcquireScavengerMutex() KeWaitForSingleObject(&RxScavengerMutex, Executive, KernelMode, FALSE, NULL)
#define RxReleaseScavengerMutex() KeReleaseMutex(&RxScavengerMutex, FALSE)

VOID
RxMarkFobxOnCleanup(
    _In_ PFOBX pFobx,
    _Out_ PBOOLEAN NeedPurge);

VOID
RxMarkFobxOnClose(
    _In_ PFOBX Fobx);

NTSTATUS
RxPurgeRelatedFobxs(
    PNET_ROOT NetRoot,
    PRX_CONTEXT RxContext,
    BOOLEAN AttemptFinalization,
    PFCB PurgingFcb);

#define DONT_ATTEMPT_FINALIZE_ON_PURGE FALSE
#define ATTEMPT_FINALIZE_ON_PURGE TRUE

typedef enum _RDBSS_SCAVENGER_STATE
{
    RDBSS_SCAVENGER_INACTIVE,
    RDBSS_SCAVENGER_DORMANT,
    RDBSS_SCAVENGER_ACTIVE,
    RDBSS_SCAVENGER_SUSPENDED
} RDBSS_SCAVENGER_STATE, *PRDBSS_SCAVENGER_STATE;

typedef struct _RDBSS_SCAVENGER
{
    RDBSS_SCAVENGER_STATE State;
    LONG MaximumNumberOfDormantFiles;
    volatile LONG NumberOfDormantFiles;
    LARGE_INTEGER TimeLimit;
    ULONG SrvCallsToBeFinalized;
    ULONG NetRootsToBeFinalized;
    ULONG VNetRootsToBeFinalized;
    ULONG FcbsToBeFinalized;
    ULONG SrvOpensToBeFinalized;
    ULONG FobxsToBeFinalized;
    LIST_ENTRY SrvCallFinalizationList;
    LIST_ENTRY NetRootFinalizationList;
    LIST_ENTRY VNetRootFinalizationList;
    LIST_ENTRY FcbFinalizationList;
    LIST_ENTRY SrvOpenFinalizationList;
    LIST_ENTRY FobxFinalizationList;
    LIST_ENTRY ClosePendingFobxsList;
    RX_WORK_ITEM WorkItem;
    KEVENT SyncEvent;
    KEVENT ScavengeEvent;
    PETHREAD CurrentScavengerThread;
    PNET_ROOT CurrentNetRootForClosePendingProcessing;
    PFCB CurrentFcbForClosePendingProcessing;
    KEVENT ClosePendingProcessingSyncEvent;
} RDBSS_SCAVENGER, *PRDBSS_SCAVENGER;

#define RxInitializeRdbssScavenger(Scavenger, ScavengerTimeLimit)                                 \
    (Scavenger)->State = RDBSS_SCAVENGER_INACTIVE;                                                \
    (Scavenger)->SrvCallsToBeFinalized = 0;                                                       \
    (Scavenger)->NetRootsToBeFinalized = 0;                                                       \
    (Scavenger)->VNetRootsToBeFinalized = 0;                                                      \
    (Scavenger)->FcbsToBeFinalized = 0;                                                           \
    (Scavenger)->SrvOpensToBeFinalized = 0;                                                       \
    (Scavenger)->FobxsToBeFinalized = 0;                                                          \
    (Scavenger)->NumberOfDormantFiles = 0;                                                        \
    (Scavenger)->MaximumNumberOfDormantFiles = 50;                                                \
    (Scavenger)->CurrentFcbForClosePendingProcessing = NULL;                                      \
    (Scavenger)->CurrentNetRootForClosePendingProcessing = NULL;                                  \
    if ((ScavengerTimeLimit).QuadPart == 0)                                                       \
    {                                                                                             \
          (Scavenger)->TimeLimit.QuadPart = RX_SCAVENGER_FINALIZATION_TIME_INTERVAL;              \
    }                                                                                             \
    else                                                                                          \
    {                                                                                             \
          (Scavenger)->TimeLimit.QuadPart = (ScavengerTimeLimit).QuadPart;                        \
    }                                                                                             \
    KeInitializeEvent(&((Scavenger)->SyncEvent), NotificationEvent, FALSE);                       \
    KeInitializeEvent(&((Scavenger)->ScavengeEvent), SynchronizationEvent, TRUE);                 \
    KeInitializeEvent(&((Scavenger)->ClosePendingProcessingSyncEvent), NotificationEvent, FALSE); \
    InitializeListHead(&(Scavenger)->SrvCallFinalizationList);                                    \
    InitializeListHead(&(Scavenger)->NetRootFinalizationList);                                    \
    InitializeListHead(&(Scavenger)->VNetRootFinalizationList);                                   \
    InitializeListHead(&(Scavenger)->SrvOpenFinalizationList);                                    \
    InitializeListHead(&(Scavenger)->FcbFinalizationList);                                        \
    InitializeListHead(&(Scavenger)->FobxFinalizationList);                                       \
    InitializeListHead(&(Scavenger)->ClosePendingFobxsList)

typedef struct _PURGE_SYNCHRONIZATION_CONTEXT
{
    LIST_ENTRY ContextsAwaitingPurgeCompletion;
    BOOLEAN PurgeInProgress;
} PURGE_SYNCHRONIZATION_CONTEXT, *PPURGE_SYNCHRONIZATION_CONTEXT;

VOID
RxInitializePurgeSyncronizationContext(
    _In_ PPURGE_SYNCHRONIZATION_CONTEXT PurgeSyncronizationContext);

BOOLEAN
RxScavengeRelatedFobxs(
    _In_ PFCB Fcb);

VOID
RxScavengeFobxsForNetRoot(
    PNET_ROOT NetRoot,
    PFCB PurgingFcb,
    BOOLEAN SynchronizeWithScavenger);

VOID
RxpMarkInstanceForScavengedFinalization(
   PVOID Instance);

VOID
RxpUndoScavengerFinalizationMarking(
   _In_ PVOID Instance);

BOOLEAN
RxScavengeVNetRoots(
    _In_ PRDBSS_DEVICE_OBJECT RxDeviceObject);

#if (_WIN32_WINNT >= 0x0600)
VOID
RxSynchronizeWithScavenger(
    _In_ PRX_CONTEXT RxContext,
    _In_ PFCB Fcb);
#else
VOID
RxSynchronizeWithScavenger(
    _In_ PRX_CONTEXT RxContext);
#endif

#ifndef RX_MAX_SCAVENGER_TIME_INTERVAL
#define RX_MAX_SCAVENGER_TIME_INTERVAL 120
#endif
#ifndef RX_SCAVENGER_ENTRY_TYPE_FCB
#define RX_SCAVENGER_ENTRY_TYPE_FCB 0x0002
#endif
#ifndef RX_SCAVENGER_ENTRY_TYPE_MARKER
#define RX_SCAVENGER_ENTRY_TYPE_MARKER 0x0001
#endif
#ifndef RX_SCAVENGER_INITIATED_OPERATION
#define RX_SCAVENGER_INITIATED_OPERATION 0x0001
#endif
#ifndef RX_SCAVENGER_MUTEX_ACQUIRED
#define RX_SCAVENGER_MUTEX_ACQUIRED 1
#endif
#ifndef RX_SCAVENGER_OP_CLOSE
#define RX_SCAVENGER_OP_CLOSE 0x0002
#endif
#ifndef RX_SCAVENGER_OP_PURGE
#define RX_SCAVENGER_OP_PURGE 0x0001
#endif

typedef enum _RX_SCAVENGER_ENTRY_STATE {
    RX_SCAVENGING_INACTIVE = 0,
    RX_SCAVENGING_PENDING = 1,
    RX_SCAVENGING_IN_PROGRESS = 2,
    RX_SCAVENGING_AWAITING_RESPONSE = 3
} RX_SCAVENGER_ENTRY_STATE, *PRX_SCAVENGER_ENTRY_STATE;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
void NTAPI RxTerminateScavenging(PRX_CONTEXT RxContext);

#ifdef __cplusplus
}
#endif

#endif
