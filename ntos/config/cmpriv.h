/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the configuration manager keeps to itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define CMP_HIVE_FILE_NAME_MAX   256
#define CMP_VALUE_NAME_MAX       0x8000
#define CMP_KEY_NAME_MAX         0x8000
#define CMP_CLASS_NAME_MAX       0x8000
#define CMP_HASH_TABLE_DEFAULT   2048
#define CMP_DELAYED_CLOSE_MAX    512
#define CMP_LAZY_FLUSH_SECONDS   5
#define CMP_DELAY_CLOSE_SECONDS  5
#define CMP_DELAY_DEREF_SECONDS  5
#define CMP_LAZY_FLUSH_HIVES     7

#define CMP_CALLBACK_MAJOR       1
#define CMP_CALLBACK_MINOR       3

typedef struct _CM_CALLBACK_ENTRY
{
    LIST_ENTRY ListEntry;
    UNICODE_STRING Altitude;
    ULONGLONG Cookie;
    PEX_CALLBACK_FUNCTION Function;
    PVOID Context;
    PVOID Driver;
    EX_RUNDOWN_REF Rundown;
    PUNICODE_STRING LastName;
    BOOLEAN Legacy;
} CM_CALLBACK_ENTRY, *PCM_CALLBACK_ENTRY;

/*
 * Being told when one of the hives the machine is made of has been read in.
 * INFO_NEEDED: Windows 11 exports CmRegisterMachineHiveLoadedNotification but
 * publishes neither its argument's layout nor its callback, and nothing in the
 * tree calls it, so this is the shape LiberNT uses.
 */
typedef VOID
(NTAPI *PCM_MACHINE_HIVE_LOADED_CALLBACK)(
    _In_opt_ PVOID Context,
    _In_ PCUNICODE_STRING HiveName,
    _In_ PCMHIVE Hive);

typedef struct _CM_MACHINE_HIVE_LOADED_NOTIFICATION
{
    PCM_MACHINE_HIVE_LOADED_CALLBACK Callback;
    PVOID Context;
    LIST_ENTRY ListEntry;
} CM_MACHINE_HIVE_LOADED_NOTIFICATION, *PCM_MACHINE_HIVE_LOADED_NOTIFICATION;

typedef struct _CM_CALLBACK_OBJECT_CONTEXT
{
    LIST_ENTRY ListEntry;
    ULONGLONG Cookie;
    PVOID Context;
} CM_CALLBACK_OBJECT_CONTEXT, *PCM_CALLBACK_OBJECT_CONTEXT;

extern ULONG InitSafeBootMode;

/* cmregistry.c */
extern BOOLEAN CmpRegistryLockCreated;
extern UNICODE_STRING CmpSystemFileName;
extern UNICODE_STRING CmpRegistryMachineHardware;
extern UNICODE_STRING CmpRegistryMachineSystem;
extern UNICODE_STRING CmpRegistryMachineSystemCurrentControlSet;
extern PEPROCESS CmpSystemProcess;

CODE_SEG("INIT") VOID NTAPI CmpInitRegistryLock(VOID);

/* cmkcbpool.c */
extern BOOLEAN CmpAllocInited;
extern KGUARDED_MUTEX CmpAllocBucketLock;
extern KGUARDED_MUTEX CmpDelayAllocBucketLock;
extern LIST_ENTRY CmpFreeKCBListHead;
extern LIST_ENTRY CmpFreeDelayItemsListHead;

/* cmkeycache.c */
VOID NTAPI CmpInsertKeyHash(_In_ PCM_KEY_HASH Hash);
VOID NTAPI CmpRemoveKeyHash(_In_ PCM_KEY_HASH Hash);
PCM_NAME_CONTROL_BLOCK NTAPI CmpGetNameControlBlock(_In_ PUNICODE_STRING Name);
VOID NTAPI CmpUpdateKcbFromNode(_Inout_ PCM_KEY_CONTROL_BLOCK Kcb,
                                _In_ PCM_KEY_NODE Node);
VOID NTAPI InitializeKCBKeyBodyList(_Inout_ PCM_KEY_CONTROL_BLOCK Kcb);

/* cmdelayclose.c */
extern LIST_ENTRY CmpDelayedCloseListHead;
extern ULONG CmpDelayedCloseElements;
extern ULONG CmpDelayCloseIntervalInSeconds;
extern KTIMER CmpDelayCloseTimer;
extern KDPC CmpDelayCloseDpc;
extern WORK_QUEUE_ITEM CmpDelayCloseWorkItem;
extern BOOLEAN CmpDelayCloseWorkItemActive;
extern LIST_ENTRY CmpDelayDerefKCBListHead;
extern KGUARDED_MUTEX CmpDelayDerefKCBLock;
extern ULONG CmpDelayDerefKCBIntervalInSeconds;
extern KTIMER CmpDelayDerefKCBTimer;
extern KDPC CmpDelayDerefKCBDpc;
extern WORK_QUEUE_ITEM CmpDelayDerefKCBWorkItem;
extern BOOLEAN CmpDelayDerefKCBWorkItemActive;

VOID NTAPI CmpDelayCloseWorker(_In_ PVOID Parameter);
VOID NTAPI CmpDelayDerefKCBWorker(_In_ PVOID Parameter);
KDEFERRED_ROUTINE CmpDelayCloseDpcRoutine;
KDEFERRED_ROUTINE CmpDelayDerefKCBDpcRoutine;

/* cmvaluecache.c */
VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpGetValueDataFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCM_CACHED_VALUE *CachedValue,
    _In_ PCELL_DATA ValueKey,
    _In_ BOOLEAN ValueIsCached,
    _Out_ PVOID *DataPointer,
    _Out_ PBOOLEAN Allocated,
    _Out_ PHCELL_INDEX CellToRelease);

#define CMP_HARDWARE_HIVE 0
#define CMP_SYSTEM_HIVE   1
#define CMP_SOFTWARE_HIVE 2

/* cmapphive.c */
CODE_SEG("INIT") VOID NTAPI CmpInitAppHives(VOID);
NTSTATUS NTAPI CmpRegisterAppHive(_In_ PCMHIVE Hive,
                                  _In_opt_ PEPROCESS Process,
                                  _In_opt_ PCMHIVE TrustClass);
VOID NTAPI CmpUnregisterAppHive(_In_ PCMHIVE Hive);
PEPROCESS NTAPI CmpAppHiveOwner(_In_ PCMHIVE Hive);
NTSTATUS NTAPI CmpAddUnloadEvent(_Inout_ PCMHIVE Hive, _In_ PKEVENT Event);
VOID NTAPI CmpSignalUnloadEvents(_Inout_ PCMHIVE Hive);
VOID NTAPI CmpDiscardUnloadEvents(_Inout_ PCMHIVE Hive);
PCMHIVE NTAPI CmpFindLoadedHive(_In_ PHHIVE Hive);

/* cmcompact.c */
NTSTATUS NTAPI CmpCompactHive(_In_ PCMHIVE Hive);
NTSTATUS NTAPI CmpCompactHivesOfKeys(
    _In_ ULONG Count,
    _In_reads_(Count) PCM_KEY_BODY *KeyBodies);

/* cmselfheal.c */
CM_CHECK_REGISTRY_STATUS NTAPI CmpSelfHealHive(_In_ PCMHIVE Hive,
                                               _In_ ULONG CheckFlags);
VOID NTAPI CmpFlushSelfHealQueue(VOID);
VOID NTAPI CmpRemoveFromSelfHealQueue(_In_ PCMHIVE Hive);

extern LIST_ENTRY CmpSelfHealQueueListHead;
extern KGUARDED_MUTEX CmpSelfHealQueueLock;

/* cmmerge.c */
NTSTATUS NTAPI CmpOverlayKeyTree(_In_ PHHIVE SourceHive,
                                 _In_ HCELL_INDEX SourceCell,
                                 _In_ PHHIVE DestinationHive,
                                 _In_ HCELL_INDEX DestinationCell,
                                 _In_ HSTORAGE_TYPE StorageType);
NTSTATUS NTAPI CmpCloneKeyTree(_In_ PHHIVE Hive,
                               _In_ HCELL_INDEX SourceCell,
                               _In_ HCELL_INDEX ParentCell,
                               _In_ PCUNICODE_STRING NewName,
                               _Out_ PHCELL_INDEX NewCell);

/* cmsubkey.c */
VOID NTAPI CmpBuildSubKeyCache(_Inout_ PCM_KEY_CONTROL_BLOCK Kcb,
                               _In_ PCM_KEY_NODE Node);
BOOLEAN NTAPI CmpSubKeyPossible(_In_ PCM_KEY_CONTROL_BLOCK Kcb,
                                _In_ PCUNICODE_STRING Name);
VOID NTAPI CmpReleaseSubKeyCache(_Inout_ PCM_KEY_CONTROL_BLOCK Kcb);
ULONG NTAPI CmpCachedSubKeyCount(_In_ PCM_KEY_CONTROL_BLOCK Kcb);

/* cmsecurecache.c */
PCM_KEY_SECURITY_CACHE NTAPI CmpGetSecurityCacheEntry(
    _In_ PCMHIVE Hive,
    _In_ HCELL_INDEX Cell);
VOID NTAPI CmpInvalidateSecurityCacheEntry(_In_ PCMHIVE Hive,
                                           _In_ HCELL_INDEX Cell);
HCELL_INDEX NTAPI CmpFindSecurityCellByDescriptor(
    _In_ PCMHIVE Hive,
    _In_reads_bytes_(Length) PVOID Descriptor,
    _In_ ULONG Length);
VOID NTAPI CmpPopulateSecurityCache(_In_ PCMHIVE Hive,
                                    _In_ HCELL_INDEX Anchor);
VOID NTAPI CmpForgetCachedSecurity(_In_ PCM_KEY_SECURITY_CACHE Cached);

/* cmbigdata.c */
BOOLEAN NTAPI CmpGetBigValueData(_In_ PHHIVE Hive,
                                 _In_ PCM_KEY_VALUE Value,
                                 _In_ ULONG Length,
                                 _Out_ PVOID *Buffer,
                                 _Out_ PBOOLEAN Allocated);
VOID NTAPI CmpFreeBigValueData(_In_ PHHIVE Hive, _In_ HCELL_INDEX BigCell);
BOOLEAN NTAPI CmpMarkBigValueDataDirty(_In_ PHHIVE Hive,
                                       _In_ HCELL_INDEX BigCell);
NTSTATUS NTAPI CmpSetBigValueData(_In_ PHHIVE Hive,
                                  _In_ HSTORAGE_TYPE StorageType,
                                  _In_ HCELL_INDEX ValueCell,
                                  _In_reads_bytes_(DataSize) PVOID Data,
                                  _In_ ULONG DataSize,
                                  _Out_ PHCELL_INDEX BigCell);
VOID NTAPI CmpReleaseValueData(_In_ PHHIVE Hive, _In_ HCELL_INDEX ValueCell);
NTSTATUS NTAPI CmpWriteValueData(_In_ PHHIVE Hive,
                                 _In_ HSTORAGE_TYPE StorageType,
                                 _In_ HCELL_INDEX ValueCell,
                                 _In_ ULONG Type,
                                 _In_reads_bytes_opt_(DataSize) PVOID Data,
                                 _In_ ULONG DataSize,
                                 _In_ ULONG SmallData);
BOOLEAN NTAPI CmpReadValueData(_In_ PHHIVE Hive,
                               _In_ PCM_KEY_VALUE Value,
                               _Out_ PULONG Length,
                               _Out_ PVOID *Buffer,
                               _Out_ PBOOLEAN Allocated,
                               _Out_ PHCELL_INDEX CellToRelease);

/* cmkeyapi.c */
NTSTATUS
NTAPI
CmpQueryKeyData(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Node,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength);

NTSTATUS
NTAPI
CmpSetValueKeyNew(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Parent,
    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Index,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize,
    _In_ HSTORAGE_TYPE StorageType,
    _In_ ULONG SmallData);

NTSTATUS
NTAPI
CmpSetValueKeyExisting(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX OldChild,
    _In_ PCM_KEY_VALUE Value,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize,
    _In_ HSTORAGE_TYPE StorageType,
    _In_ ULONG TempData);

VOID NTAPI CmpTouchKey(_In_ PHHIVE Hive, _In_ HCELL_INDEX Cell);

/* cmnamespace.c */
NTSTATUS
NTAPI
CmpCreateKeyBody(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PCM_PARSE_CONTEXT Context,
    _Out_ PVOID *Object);

/* cmkeysecurity.c */
NTSTATUS
NTAPI
CmpQuerySecurityDescriptor(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_writes_bytes_opt_(*Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG Length);

NTSTATUS
NTAPI
CmpSetSecurityDescriptor(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping);

VOID
NTAPI
CmpAssignSecurityToKcb(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ HCELL_INDEX SecurityCell,
    _In_ BOOLEAN KeyLockHeld);

/* cmhivemgr.c */
NTSTATUS NTAPI CmpFlushHiveToDisk(_In_ PCMHIVE Hive, _In_ BOOLEAN Force);
HCELL_INDEX NTAPI CmpHiveSecurityAnchor(_In_ PHHIVE Hive);

/* cmhiveload.c */
extern PKEVENT CmpLoadWorkerEvent;
extern LONG CmpLoadWorkerIncrement;

NTSTATUS NTAPI CmpLoadHiveAtName(_In_ POBJECT_ATTRIBUTES TargetKey,
                                 _In_ POBJECT_ATTRIBUTES SourceFile,
                                 _In_ ULONG Flags,
                                 _In_ KPROCESSOR_MODE AccessMode,
                                 _In_opt_ PCMHIVE TrustClass,
                                 _Out_opt_ PCMHIVE *LoadedHive,
                                 _Out_opt_ PHANDLE RootHandle,
                                 _Out_opt_ PCM_KEY_BODY *RootBody);
NTSTATUS NTAPI CmpRestoreKeyFromFile(_In_ PCM_KEY_BODY KeyBody,
                                     _In_ HANDLE FileHandle,
                                     _In_ ULONG Flags);
NTSTATUS NTAPI CmpReplaceHiveFile(_In_ PCMHIVE Hive,
                                  _In_ PCUNICODE_STRING NewFileName,
                                  _In_ PCUNICODE_STRING OldFileName);
CODE_SEG("INIT") VOID NTAPI CmpLoadHiveThread(_In_ PVOID StartContext);

/* cmflusher.c */
extern BOOLEAN CmpLazyFlushPending;
extern ULONG CmpLazyFlushCount;
extern KTIMER CmpLazyFlushTimer;
extern KDPC CmpLazyFlushDpc;
extern WORK_QUEUE_ITEM CmpLazyWorkItem;
extern KTIMER CmpEnableLazyFlushTimer;
extern KDPC CmpEnableLazyFlushDpc;

BOOLEAN NTAPI CmpDoFlushNextHive(_In_ BOOLEAN ForceFlush,
                                 _Out_opt_ PBOOLEAN Error,
                                 _Out_opt_ PULONG DirtyCount);
VOID NTAPI CmpLazyFlushWorker(_In_ PVOID Parameter);
KDEFERRED_ROUTINE CmpLazyFlushDpcRoutine;
KDEFERRED_ROUTINE CmpEnableLazyFlushDpcRoutine;

/* cmwatch.c */
VOID NTAPI CmpPostNotifyBlock(_In_ PCM_NOTIFY_BLOCK NotifyBlock,
                              _In_ NTSTATUS Status);

/* cmfilter.c */
NTSTATUS NTAPI CmpCallbackObjectContextCleanup(_In_ PCM_KEY_BODY KeyBody);
VOID NTAPI CmpNotifyMachineHiveLoaded(_In_ PCUNICODE_STRING HiveName,
                                      _In_ PCMHIVE Hive);

/* cmhwtree.c */
CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpSetupConfigurationTree(
    _In_ PCONFIGURATION_COMPONENT_DATA CurrentEntry,
    _In_ HANDLE NodeHandle,
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PUSHORT DeviceIndexTable);

/* cmstartup.c */
CODE_SEG("INIT") NTSTATUS NTAPI CmpCreateObjectTypes(VOID);
CODE_SEG("INIT") NTSTATUS NTAPI CmpCreateRegistryRoot(VOID);
CODE_SEG("INIT") BOOLEAN NTAPI CmpInitializeSystemHive(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
CODE_SEG("INIT") NTSTATUS NTAPI CmpCreateControlSet(_In_ PHHIVE Hive);
CODE_SEG("INIT") VOID NTAPI CmpSetSystemValues(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
