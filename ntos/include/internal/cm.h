/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Configuration manager internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <cmlib.h>

#include <cmreslist.h>

#include "cmboot.h"

#define CM_KCB_SIGNATURE                                'bKmC'

#define CM_KCB_INVALID_SIGNATURE                        '4FmC'

#define CM_KCB_NO_SUBKEY                                0x01

#define CM_KCB_SUBKEY_ONE                               0x02

#define CM_KCB_SUBKEY_HINT                              0x04

#define CM_KCB_KEY_NON_EXIST                            0x10

#define CM_KCB_NO_DELAY_CLOSE                           0x20

#define CM_KCB_INVALID_CACHED_INFO                      0x40

#define CM_KCB_READ_ONLY_KEY                            0x80

#define CM_KEY_BODY_TYPE                                0x6B793032

#define CMP_HASH_IRRATIONAL                             314159269

#define CMP_HASH_PRIME                                  1000000007

#define CMP_CREATE_FAKE_KCB                             0x1

#define CMP_LOCK_HASHES_FOR_KCB                         0x2

#define CMP_ENLIST_KCB_LOCKED_SHARED                    0x1

#define CMP_ENLIST_KCB_LOCKED_EXCLUSIVE                 0x2

#define CMP_LOCK_KCB_ARRAY_EXCLUSIVE                    0x1

#define CM_NUMBER_OF_MACHINE_HIVES                      6

#define CM_KCBS_PER_PAGE                                \
    ((PAGE_SIZE - FIELD_OFFSET(CM_ALLOC_PAGE, AllocPage)) / sizeof(CM_KEY_CONTROL_BLOCK))

#define CM_DELAYS_PER_PAGE                              \
    ((PAGE_SIZE - FIELD_OFFSET(CM_ALLOC_PAGE, AllocPage)) / sizeof(CM_DELAY_ALLOC))

#define CMP_SUBKEY_LEVELS_DEPTH_LIMIT   32

#define CMP_KCBS_IN_ARRAY_LIMIT         (CMP_SUBKEY_LEVELS_DEPTH_LIMIT + 2)

#if 0
#define CMP_SECURITY_HASH_LISTS                         64
#endif

typedef enum _VALUE_SEARCH_RETURN_TYPE
{
    SearchSuccess,
    SearchNeedExclusiveLock,
    SearchFail
} VALUE_SEARCH_RETURN_TYPE;

typedef struct _CM_KEY_HASH
{
    ULONG ConvKey;
    struct _CM_KEY_HASH *NextHash;
    PHHIVE KeyHive;
    HCELL_INDEX KeyCell;
} CM_KEY_HASH, *PCM_KEY_HASH;

typedef struct _CM_KEY_HASH_TABLE_ENTRY
{
    EX_PUSH_LOCK Lock;
    PKTHREAD Owner;
    PCM_KEY_HASH Entry;
} CM_KEY_HASH_TABLE_ENTRY, *PCM_KEY_HASH_TABLE_ENTRY;

#ifdef _M_ARM64
C_ASSERT(sizeof(CM_KEY_HASH_TABLE_ENTRY) == 24);
C_ASSERT(FIELD_OFFSET(CM_KEY_HASH_TABLE_ENTRY, Lock) == 0);
C_ASSERT(FIELD_OFFSET(CM_KEY_HASH_TABLE_ENTRY, Owner) == 8);
C_ASSERT(FIELD_OFFSET(CM_KEY_HASH_TABLE_ENTRY, Entry) == 16);
#endif

typedef struct _CM_NAME_HASH
{
    ULONG ConvKey;
    struct _CM_NAME_HASH *NextHash;
    USHORT NameLength;
    WCHAR Name[ANYSIZE_ARRAY];
} CM_NAME_HASH, *PCM_NAME_HASH;

typedef struct _CM_NAME_HASH_TABLE_ENTRY
{
    EX_PUSH_LOCK Lock;
    PCM_NAME_HASH Entry;
} CM_NAME_HASH_TABLE_ENTRY, *PCM_NAME_HASH_TABLE_ENTRY;

typedef struct _CM_KEY_SECURITY_CACHE
{
    HCELL_INDEX Cell;
    ULONG ConvKey;
    LIST_ENTRY List;
    ULONG DescriptorLength;
    SECURITY_DESCRIPTOR_RELATIVE Descriptor;
} CM_KEY_SECURITY_CACHE, *PCM_KEY_SECURITY_CACHE;

typedef struct _CM_KEY_SECURITY_CACHE_ENTRY
{
    HCELL_INDEX Cell;
    PCM_KEY_SECURITY_CACHE CachedSecurity;
} CM_KEY_SECURITY_CACHE_ENTRY, *PCM_KEY_SECURITY_CACHE_ENTRY;

typedef struct _CACHED_CHILD_LIST
{
    ULONG Count;
    union
    {
        ULONG_PTR ValueList;
        struct _CM_KEY_CONTROL_BLOCK *RealKcb;
    };
} CACHED_CHILD_LIST, *PCACHED_CHILD_LIST;

typedef struct _CM_INDEX_HINT_BLOCK
{
    ULONG Count;
    ULONG HashKey[ANYSIZE_ARRAY];
} CM_INDEX_HINT_BLOCK, *PCM_INDEX_HINT_BLOCK;

typedef union _CM_TRANS_PTR
{
    ULONG_PTR LightWeight : 1;
    PVOID TransPtr;
} CM_TRANS_PTR, *PCM_TRANS_PTR;

typedef struct _CM_KEY_BODY
{
    ULONG Type;
    USHORT AccessCheckedLayerHeight;
    struct _CM_KEY_CONTROL_BLOCK *KeyControlBlock;
    struct _CM_NOTIFY_BLOCK *NotifyBlock;
    HANDLE ProcessID;
    LIST_ENTRY KeyBodyList;

    BOOLEAN KcbLocked;
    BOOLEAN NotifyClosed;
    CM_TRANS_PTR Trans;
    GUID *KtmUow;
    LIST_ENTRY ContextListHead;
} CM_KEY_BODY, *PCM_KEY_BODY;

C_ASSERT(FIELD_OFFSET(CM_KEY_BODY, AccessCheckedLayerHeight) == 0x4);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KEY_BODY, KeyControlBlock) == 0x8);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KEY_BODY, NotifyBlock) == 0x10);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KEY_BODY, ProcessID) == 0x18);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KEY_BODY, KeyBodyList) == 0x20);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KEY_BODY, Trans) == 0x38);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KEY_BODY, KtmUow) == 0x40);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KEY_BODY, ContextListHead) == 0x48);

typedef struct _CMP_CALLBACK_ITEM
{
    struct _CM_CALLBACK_ENTRY *Entry;
    PVOID CallContext;
} CMP_CALLBACK_ITEM, *PCMP_CALLBACK_ITEM;

typedef struct _CMP_CALLBACK_CALL
{
    ULONG Count;
    ULONG PostCount;
    REG_NOTIFY_CLASS PostClass;
    PVOID PreInformation;
    PVOID *CallContext;
    PVOID *ObjectContext;
    PCM_KEY_BODY KeyBody;
    PVOID *ResultObject;
    PCMP_CALLBACK_ITEM Items;
    CMP_CALLBACK_ITEM InlineItems[4];
} CMP_CALLBACK_CALL, *PCMP_CALLBACK_CALL;

typedef struct _CM_NAME_CONTROL_BLOCK
{
    BOOLEAN Compressed;
    USHORT RefCount;
    union
    {
        CM_NAME_HASH NameHash;
        struct
        {
            ULONG ConvKey;
            PCM_KEY_HASH NextHash;
            USHORT NameLength;
            WCHAR Name[ANYSIZE_ARRAY];
        };
    };
} CM_NAME_CONTROL_BLOCK, *PCM_NAME_CONTROL_BLOCK;

typedef struct _CM_KEY_CONTROL_BLOCK
{
    ULONG Signature;
    ULONG RefCount;
    struct
    {
        ULONG ExtFlags:8;
        ULONG PrivateAlloc:1;
        ULONG Delete:1;
        ULONG DelayedCloseIndex:12;
        ULONG TotalLevels:10;
    };
    union
    {
        CM_KEY_HASH KeyHash;
        struct
        {
            ULONG ConvKey;
            PCM_KEY_HASH NextHash;
            PHHIVE KeyHive;
            HCELL_INDEX KeyCell;
        };
    };
    struct _CM_KEY_CONTROL_BLOCK *ParentKcb;
    PCM_NAME_CONTROL_BLOCK NameBlock;
    PCM_KEY_SECURITY_CACHE CachedSecurity;
    CACHED_CHILD_LIST ValueCache;
    union
    {
        PCM_INDEX_HINT_BLOCK IndexHint;
        ULONG HashKey;
        ULONG SubKeyCount;
    };
    union
    {
        LIST_ENTRY KeyBodyListHead;
        LIST_ENTRY FreeListEntry;
    };
    PCM_KEY_BODY KeyBodyArray[4];
    PVOID DelayCloseEntry;
    LARGE_INTEGER KcbLastWriteTime;
    USHORT KcbMaxNameLen;
    USHORT KcbMaxValueNameLen;
    ULONG KcbMaxValueDataLen;
    struct
    {
         ULONG KcbUserFlags : 4;
         ULONG KcbVirtControlFlags : 4;
         ULONG KcbDebug : 8;
         ULONG Flags : 16;
    };
    ULONG InDelayClose;
} CM_KEY_CONTROL_BLOCK, *PCM_KEY_CONTROL_BLOCK;

typedef struct _CM_NOTIFY_BLOCK
{
    LIST_ENTRY HiveList;
    LIST_ENTRY PostList;
    PCM_KEY_CONTROL_BLOCK KeyControlBlock;
    PCM_KEY_BODY KeyBody;
    ULONG Filter:29;
    ULONG WatchTree:1;
    ULONG NotifyPending:1;
} CM_NOTIFY_BLOCK, *PCM_NOTIFY_BLOCK;

typedef struct _CM_CELL_REMAP_BLOCK
{
    HCELL_INDEX OldCell;
    HCELL_INDEX NewCell;
} CM_CELL_REMAP_BLOCK, *PCM_CELL_REMAP_BLOCK;

typedef struct _CM_ALLOC_PAGE
{
    ULONG FreeCount;
    ULONG Reserved;
    PVOID AllocPage;
} CM_ALLOC_PAGE, *PCM_ALLOC_PAGE;

typedef struct _CM_DELAY_ALLOC
{
    LIST_ENTRY ListEntry;
    PCM_KEY_CONTROL_BLOCK Kcb;
} CM_DELAY_ALLOC, *PCM_DELAY_ALLOC;

typedef struct _CM_DELAYED_CLOSE_ENTRY
{
    LIST_ENTRY DelayedLRUList;
    PCM_KEY_CONTROL_BLOCK KeyControlBlock;
} CM_DELAYED_CLOSE_ENTRY, *PCM_DELAYED_CLOSE_ENTRY;

typedef struct _CM_DELAY_DEREF_KCB_ITEM
{
    LIST_ENTRY ListEntry;
    PCM_KEY_CONTROL_BLOCK Kcb;
} CM_DELAY_DEREF_KCB_ITEM, *PCM_DELAY_DEREF_KCB_ITEM;

typedef struct _CM_CACHED_VALUE_INDEX
{
    HCELL_INDEX CellIndex;
    union
    {
        CELL_DATA CellData;
        ULONG_PTR List[ANYSIZE_ARRAY];
    } Data;
} CM_CACHED_VALUE_INDEX, *PCM_CACHED_VALUE_INDEX;

typedef struct _CM_CACHED_VALUE
{
    USHORT DataCacheType;
    USHORT ValueKeySize;
    ULONG HashKey;
    CM_KEY_VALUE KeyValue;
} CM_CACHED_VALUE, *PCM_CACHED_VALUE;

typedef struct _HIVE_LIST_ENTRY
{
    PWSTR Name;
    PWSTR BaseName;
    PCMHIVE CmHive;
    ULONG HHiveFlags;
    ULONG CmHiveFlags;
    PCMHIVE CmHive2;
    BOOLEAN ThreadFinished;
    BOOLEAN ThreadStarted;
    BOOLEAN Allocate;
} HIVE_LIST_ENTRY, *PHIVE_LIST_ENTRY;

typedef struct _CM_HASH_CACHE_STACK
{
    UNICODE_STRING NameOfKey;
    ULONG ConvKey;
} CM_HASH_CACHE_STACK, *PCM_HASH_CACHE_STACK;

typedef struct _CM_PARSE_CONTEXT
{
    ULONG TitleIndex;
    UNICODE_STRING Class;
    ULONG CreateOptions;
    ULONG Disposition;
    CM_KEY_REFERENCE ChildHive;
    HANDLE PredefinedHandle;
    BOOLEAN CreateLink;
    BOOLEAN CreateOperation;
    PCMHIVE OriginatingPoint;
    ULONG Wow64Flags;
    PVOID Transaction;
    PVOID EffectiveTransaction;
    PVOID ConflictEnlistment;
} CM_PARSE_CONTEXT, *PCM_PARSE_CONTEXT;

typedef enum _UoWActionType
{
    UoWAddThisKey = 0,
    UoWAddChildKey = 1,
    UoWDeleteThisKey = 2,
    UoWDeleteChildKey = 3,
    UoWSetValueNew = 4,
    UoWSetValueExisting = 5,
    UoWDeleteValue = 6,
    UoWSetKeyUserFlags = 7,
    UoWSetLastWriteTime = 8,
    UoWSetSecurityDescriptor = 9,
    UoWRenameSubKeyObsolete = 10,
    UoWRenameOldSubKeyObsolete = 11,
    UoWRenameNewSubKeyObsolete = 12,
    UoWIsolation = 13,
    UoWTestFail = 14,
    UoWRecreateKey = 15,
    UoWInvalid = 16
} UoWActionType;

typedef struct _CM_INTENT_LOCK
{
    ULONG OwnerCount;
    struct _CM_KCB_UOW **OwnerTable;
} CM_INTENT_LOCK, *PCM_INTENT_LOCK;

typedef struct _CM_UOW_SET_VALUE_KEY_DATA
{
    HCELL_INDEX PreparedCell;
    HCELL_INDEX OldValueCell;
    USHORT NameLength;
    ULONG DataSize;
} CM_UOW_SET_VALUE_KEY_DATA, *PCM_UOW_SET_VALUE_KEY_DATA;

typedef struct _CM_TRANS
{
    LIST_ENTRY TransactionListEntry;
    LIST_ENTRY KCBUoWListHead;
    LIST_ENTRY LazyCommitListEntry;
    union
    {
        struct
        {
            ULONG Prepared : 1;
            ULONG Aborted : 1;
            ULONG Committed : 1;
            ULONG Initializing : 1;
            ULONG Invalid : 1;
            ULONG UseReservation : 1;
            ULONG TmCallbacksActive : 1;
            ULONG LightWeight : 1;
            ULONG Freed1 : 1;
            ULONG Freed2 : 1;
            ULONG Spare1 : 2;
            ULONG Freed : 1;
            ULONG Spare : 19;
        };
        ULONG TransState;
    };
    CM_TRANS_PTR Trans;
    struct _CM_RM *CmRm;
    struct _KENLISTMENT *KtmEnlistmentObject;
    HANDLE KtmEnlistmentHandle;
    GUID KtmUow;
    ULONGLONG StartLsn;
    ULONG HiveCount;
    PCMHIVE HiveArray[8];
} CM_TRANS, *PCM_TRANS;

typedef struct _CM_KCB_UOW
{
    LIST_ENTRY TransactionListEntry;
    PCM_INTENT_LOCK KCBLock;
    PCM_INTENT_LOCK KeyLock;
    LIST_ENTRY KCBListEntry;
    struct _CM_KEY_CONTROL_BLOCK *KeyControlBlock;
    PCM_TRANS Transaction;
    ULONG UoWState;
    UoWActionType ActionType;
    HSTORAGE_TYPE StorageType;
    struct _CM_KCB_UOW *ParentUoW;
    union
    {
        struct _CM_KEY_CONTROL_BLOCK *ChildKCB;
        HCELL_INDEX VolatileKeyCell;
        struct
        {
            HCELL_INDEX OldValueCell;
            HCELL_INDEX NewValueCell;
        };
        ULONG UserFlags;
        LARGE_INTEGER LastWriteTime;
        struct
        {
            PVOID TxCachedSecurity;
            HCELL_INDEX TxSecurityCell;
            BOOLEAN UpdateEntireSecurity;
        };
    };
    union
    {
        PVOID PrepareDataPointer;
        PVOID SecurityData;
        PVOID ModifyKeysData;
        PVOID SetValueData;
    };
    union
    {
        PCM_UOW_SET_VALUE_KEY_DATA ValueData;
        PVOID DiscardReplaceContext;
    };
} CM_KCB_UOW, *PCM_KCB_UOW;

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_TRANS, TransState) == 0x30);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_TRANS, Trans) == 0x38);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_TRANS, KtmEnlistmentObject) == 0x48);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_TRANS, KtmUow) == 0x58);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_TRANS, HiveArray) == 0x78);

C_ASSERT(sizeof(PVOID) != 8 || sizeof(CM_TRANS) == 0xb8);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KCB_UOW, KCBListEntry) == 0x20);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KCB_UOW, KeyControlBlock) == 0x30);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KCB_UOW, ActionType) == 0x44);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KCB_UOW, ParentUoW) == 0x50);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KCB_UOW, ChildKCB) == 0x58);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KCB_UOW, PrepareDataPointer) == 0x68);

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CM_KCB_UOW, ValueData) == 0x70);

C_ASSERT(sizeof(PVOID) != 8 || sizeof(CM_KCB_UOW) == 0x78);

typedef struct _CMP_MF_TYPE
{
    PCHAR Identifier;
    USHORT InterfaceType;
    USHORT Count;
} CMP_MF_TYPE, *PCMP_MF_TYPE;

typedef struct _CM_SYSTEM_CONTROL_VECTOR
{
    PWCHAR KeyPath;
    PWCHAR ValueName;
    PVOID Buffer;
    PULONG BufferLength;
    PULONG Type;
} CM_SYSTEM_CONTROL_VECTOR, *PCM_SYSTEM_CONTROL_VECTOR;

extern ULONG CmpCallBackCount;

extern ULONG CmpTransUoWCount;

extern ULONG CmpTraceLevel;

extern BOOLEAN CmpSpecialBootCondition;

extern BOOLEAN CmpFlushOnLockRelease;

extern ULONG CmpVolatileBoot;

extern BOOLEAN CmpShareSystemHives;

extern BOOLEAN CmpMiniNTBoot;

extern BOOLEAN CmpNoVolatileCreates;

extern EX_PUSH_LOCK CmpHiveListHeadLock, CmpLoadHiveLock;

extern LIST_ENTRY CmpHiveListHead;

extern POBJECT_TYPE CmpKeyObjectType;

extern PCM_KEY_HASH_TABLE_ENTRY CmpCacheTable;

extern PCM_NAME_HASH_TABLE_ENTRY CmpNameCacheTable;

extern CMHIVE CmControlHive;

extern WCHAR CmDefaultLanguageId[];

extern ULONG CmDefaultLanguageIdLength;

extern ULONG CmDefaultLanguageIdType;

extern WCHAR CmInstallUILanguageId[];

extern ULONG CmInstallUILanguageIdLength;

extern ULONG CmInstallUILanguageIdType;

extern ULONG CmNtGlobalFlag;

extern LANGID PsInstallUILanguageId;

extern LANGID PsDefaultUILanguageId;

extern CM_SYSTEM_CONTROL_VECTOR CmControlVector[];

extern ULONG CmpConfigurationAreaSize;

extern PCM_FULL_RESOURCE_DESCRIPTOR CmpConfigurationData;

extern UNICODE_STRING CmTypeName[];

extern UNICODE_STRING CmClassName[];

extern CMP_MF_TYPE CmpMultifunctionTypes[];

extern USHORT CmpUnknownBusCount;

extern ULONG CmpTypeCount[MaximumType + 1];

extern HIVE_LIST_ENTRY CmpMachineHiveList[];

extern UNICODE_STRING CmSymbolicLinkValueName;

extern UNICODE_STRING CmpSystemStartOptions;

extern UNICODE_STRING CmpLoadOptions;

extern ULONG CmpBootType;

extern ULONG CmSelfHeal;

extern BOOLEAN CmpSelfHeal;

extern HANDLE CmpRegistryRootHandle;

extern PCM_KEY_BODY CmpRegistryRootObject;

extern PCM_KEY_BODY CmpRegistryNamespaceRoot;

extern BOOLEAN ExpInTextModeSetup;

extern BOOLEAN InitIsWinPEMode;

extern ULONG CmpHashTableSize;

extern ULONG CmpDelayedCloseSize;

extern BOOLEAN CmpNoWrite;

extern BOOLEAN CmpForceForceFlush;

extern BOOLEAN CmpWasSetupBoot;

extern BOOLEAN CmpProfileLoaded;

extern PCMHIVE CmiVolatileHive;

extern BOOLEAN CmpHoldLazyFlush;

extern ULONG CmpLazyFlushIntervalInSeconds;

extern ULONG CmpLazyFlushHiveCount;

extern BOOLEAN HvShutdownComplete;

PVOID
NTAPI
CmpAllocate(
    _In_ SIZE_T Size,
    _In_ BOOLEAN Paged,
    _In_ ULONG Tag);

BOOLEAN
NTAPI
CmpFileFlush(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _Inout_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length);

VOID
NTAPI
CmpFree(
    _In_ PVOID Ptr,
    _In_ ULONG Quota);

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

NTSTATUS
NTAPI
CmpCreateEvent(
    _In_ EVENT_TYPE EventType,
    _Out_ PHANDLE EventHandle,
    _Out_ PKEVENT *Event);

BOOLEAN
NTAPI
CmpFileRead(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _Inout_ PULONG FileOffset,
    _Out_ PVOID Buffer,
    _In_ SIZE_T BufferLength);

BOOLEAN
NTAPI
CmpFileSetSize(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _In_ ULONG FileSize,
    _In_ ULONG OldFileSize);

BOOLEAN
NTAPI
CmpFileWrite(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _Inout_ PULONG FileOffset,
    _In_ PVOID Buffer,
    _In_ SIZE_T BufferLength);

VOID
NTAPI
CmpLockHiveFlusherExclusive(
    _In_ PCMHIVE Hive);

VOID
NTAPI
CmpLockHiveFlusherShared(
    _In_ PCMHIVE Hive);

VOID
NTAPI
CmpUnlockHiveFlusher(
    _In_ PCMHIVE Hive);

CODE_SEG("INIT")
VOID
NTAPI
CmGetSystemControlValues(
    _In_ PVOID SystemHiveData,
    _In_ PCM_SYSTEM_CONTROL_VECTOR ControlVector);

NTSTATUS
NTAPI
CmpSaveBootControlSet(
    _In_ USHORT ControlSet);

VOID
NTAPI
CmpReadLastKnownGoodPolicy(VOID);

NTSTATUS
NTAPI
CmpAcceptBoot(VOID);

VOID
NTAPI
CmpAddToDelayedClose(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeldExclusively);

VOID
NTAPI
CmpArmDelayedCloseTimer(VOID);

VOID
NTAPI
CmpDelayDerefKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitDelayDerefKCBEngine(VOID);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeDelayedCloseTable(VOID);

VOID
NTAPI
CmpRemoveFromDelayedClose(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

VOID
NTAPI
CmpRunDownDelayDerefKCBEngine(VOID);

VOID
NTAPI
CmpCleanupKeyBodyContexts(
    _In_ PCM_KEY_BODY KeyBody);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCallback(VOID);

NTSTATUS
NTAPI
CmpPostCallbacks(
    _Inout_ PCMP_CALLBACK_CALL Call,
    _In_ NTSTATUS Status);

BOOLEAN
NTAPI
CmpPreCallbacks(
    _Out_ PCMP_CALLBACK_CALL Call,
    _In_ REG_NOTIFY_CLASS PreClass,
    _In_ REG_NOTIFY_CLASS PostClass,
    _In_ PVOID PreInformation,
    _In_opt_ PVOID *CallContext,
    _In_opt_ PVOID *ObjectContext,
    _In_opt_ PCM_KEY_BODY KeyBody,
    _In_opt_ PVOID *ResultObject,
    _Out_ PNTSTATUS Status);

VOID
NTAPI
CmSetLazyFlushState(
    _In_ BOOLEAN Enable);

VOID
NTAPI
CmShutdownSystem(VOID);

VOID
NTAPI
CmpCmdInit(
    _In_ BOOLEAN SetupBoot);

BOOLEAN
NTAPI
CmpDoFlushAll(
    _In_ BOOLEAN ForceFlush);

VOID
NTAPI
CmpLazyFlush(VOID);

VOID
NTAPI
CmpShutdownWorkers(VOID);

NTSTATUS
NTAPI
CmLoadKey(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ POBJECT_ATTRIBUTES SourceFile,
    _In_ ULONG Flags,
    _In_ PCM_KEY_BODY KeyBody);

NTSTATUS
NTAPI
CmSaveKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ HANDLE FileHandle,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
CmSaveMergedKeys(
    _In_ PCM_KEY_CONTROL_BLOCK HighKcb,
    _In_ PCM_KEY_CONTROL_BLOCK LowKcb,
    _In_ HANDLE FileHandle);

NTSTATUS
NTAPI
CmUnloadKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
CmpCmdHiveOpen(
    _In_ POBJECT_ATTRIBUTES FileAttributes,
    _In_ PSECURITY_CLIENT_CONTEXT ImpersonationContext,
    _Inout_ PBOOLEAN Allocate,
    _Out_ PCMHIVE *NewHive,
    _In_ ULONG CheckFlags);

VOID
NTAPI
CmpCompleteAppHiveLoad(
    PCMHIVE Hive);

NTSTATUS
NTAPI
CmpDeepCopyKey(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SrcKeyCell,
    _In_ PHHIVE DestinationHive,
    _In_ HSTORAGE_TYPE StorageType,
    _Out_opt_ PHCELL_INDEX DestKeyCell);

NTSTATUS
NTAPI
CmpLoadAppHive(
    POBJECT_ATTRIBUTES TargetKey,
    POBJECT_ATTRIBUTES SourceFile,
    ULONG Flags,
    KPROCESSOR_MODE AccessMode,
    PCM_KEY_BODY *RootBody,
    PCMHIVE *LoadedHive);

VOID
NTAPI
CmpUnloadAppHiveIfUnused(
    PCMHIVE Hive);

NTSTATUS
NTAPI
CmpAddToHiveFileList(
    _Inout_ PCMHIVE Hive);

VOID
NTAPI
CmpCloseHiveFiles(
    _In_ PCMHIVE Hive);

BOOLEAN
NTAPI
CmpCreateRootNode(
    PHHIVE Hive,
    PCUNICODE_STRING Name,
    PHCELL_INDEX Index);

VOID
NTAPI
CmpDereferenceHive(
    PCMHIVE Hive);

NTSTATUS
NTAPI
CmpDestroyHive(
    _In_ PCMHIVE CmHive);

VOID
NTAPI
CmpDestroyHiveViewList(
    _In_ PCMHIVE Hive);

NTSTATUS
NTAPI
CmpInitHiveFromFile(
    _In_ PCUNICODE_STRING HiveName,
    _In_opt_ PCUNICODE_STRING LinkName,
    _In_ ULONG HiveFlags,
    _Out_ PCMHIVE *Hive,
    _Inout_ PBOOLEAN New,
    _In_ ULONG CheckFlags);

VOID
NTAPI
CmpInitHiveViewList(
    _In_ PCMHIVE Hive);

NTSTATUS
NTAPI
CmpInitializeHive(
    _Out_ PCMHIVE *CmHive,
    _In_ ULONG OperationType,
    _In_ ULONG HiveFlags,
    _In_ ULONG FileType,
    _In_opt_ PVOID HiveData,
    _In_ HANDLE Primary,
    _In_ HANDLE Log,
    _In_ HANDLE External,
    _In_ HANDLE Alternate,
    _In_opt_ PCUNICODE_STRING FileName,
    _In_ ULONG CheckFlags);

NTSTATUS
NTAPI
CmpInitializeScratchHive(
    _Out_ PCMHIVE *CmHive);

NTSTATUS
NTAPI
CmpLinkHiveToMaster(
    _In_ PUNICODE_STRING LinkName,
    _In_ HANDLE RootDirectory,
    _In_ PCMHIVE CmHive,
    _In_ BOOLEAN Allocate,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor);

NTSTATUS
NTAPI
CmpOpenHiveFiles(
    _In_ PCUNICODE_STRING BaseName,
    _In_opt_ PCWSTR Extension,
    _Out_ PHANDLE Primary,
    _Out_ PHANDLE Log,
    _Out_ PULONG PrimaryDisposition,
    _Out_ PULONG LogDisposition,
    _In_ BOOLEAN CreateAllowed,
    _In_ BOOLEAN MarkAsSystemHive,
    _In_ BOOLEAN NoBuffering,
    _Out_opt_ PULONG ClusterSize);

VOID
NTAPI
CmpReferenceHive(
    PCMHIVE Hive);

VOID
NTAPI
CmpRemoveFromHiveFileList(
    _Inout_ PCMHIVE Hive);

CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpInitializeHardwareConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeProcessorClockFromFirmware(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpInitializeRegistryNode(
    _In_ PCONFIGURATION_COMPONENT_DATA CurrentEntry,
    _In_ HANDLE NodeHandle,
    _Out_ PHANDLE NewHandle,
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PUSHORT DeviceIndexTable);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeSystemBiosInformation(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

PVOID
NTAPI
CmpAllocateDelayItem(VOID);

PCM_KEY_CONTROL_BLOCK
NTAPI
CmpAllocateKeyControlBlock(VOID);

VOID
NTAPI
CmpFreeDelayItem(
    PVOID Entry);

VOID
NTAPI
CmpFreeKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCmPrivateAlloc(VOID);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCmPrivateDelayAlloc(VOID);

NTSTATUS
NTAPI
CmDeleteKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

NTSTATUS
NTAPI
CmDeleteValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ UNICODE_STRING ValueName);

NTSTATUS
NTAPI
CmFlushKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN EclusiveLock);

NTSTATUS
NTAPI
CmRenameKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING NewName);

ULONG
NTAPI
CmpEnumerateOpenSubKeys(
    _In_ PCM_KEY_CONTROL_BLOCK RootKcb,
    _In_ BOOLEAN RemoveEmptyCacheEntries,
    _In_ BOOLEAN DereferenceOpenedEntries);

VOID
NTAPI
CmpAcquireTwoKcbLocksExclusiveByKey(
    _In_ ULONG ConvKey1,
    _In_ ULONG ConvKey2);

PULONG
NTAPI
CmpBuildAndLockKcbArray(
    _In_ PCM_HASH_CACHE_STACK HashCacheStack,
    _In_ ULONG KcbLockFlags,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _Inout_ PULONG OuterStackArray,
    _In_ ULONG TotalRemainingSubkeys,
    _In_ ULONG MatchRemainSubkeyLevel);

VOID
NTAPI
CmpCancelKeyControlBlockRename(
    _In_ PVOID RenameContext);

VOID
NTAPI
CmpCleanUpKcbCacheWithLock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeldExclusively);

VOID
NTAPI
CmpCleanUpKcbValueCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

VOID
NTAPI
CmpCleanUpSubKeyInfo(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

VOID
NTAPI
CmpCommitKeyControlBlockRename(
    _In_ PVOID RenameContext,
    _In_ HCELL_INDEX NewCell);

BOOLEAN
CmpIsHardwareTree(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

PUNICODE_STRING
NTAPI
CmpConstructName(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

PCM_KEY_CONTROL_BLOCK
NTAPI
CmpCreateKeyControlBlock(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Index,
    _In_ PCM_KEY_NODE Node,
    _In_ PCM_KEY_CONTROL_BLOCK Parent,
    _In_ ULONG Flags,
    _In_ PUNICODE_STRING KeyName);

VOID
NTAPI
CmpDereferenceKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

VOID
NTAPI
CmpDereferenceKeyControlBlockWithLock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeldExclusively);

VOID
NTAPI
CmpDereferenceNameControlBlockWithLock(
    _In_ PCM_NAME_CONTROL_BLOCK Ncb);

VOID
NTAPI
CmpFlushNotifiesOnKeyBodyList(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeld);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeCache(VOID);

PVOID
NTAPI
CmpPrepareKeyControlBlockRename(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING NewName);

BOOLEAN
NTAPI
CmpReferenceKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

VOID
NTAPI
CmpReleaseTwoKcbLockByKey(
    _In_ ULONG ConvKey1,
    _In_ ULONG ConvKey2);

VOID
NTAPI
CmpRemoveKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

VOID
CmpUnLockKcbArray(
    _In_ PULONG LockedKcbs);

VOID
NTAPI
DelistKeyBodyFromKCB(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ BOOLEAN LockHeld);

VOID
NTAPI
EnlistKeyBodyWithKCB(
    _In_ PCM_KEY_BODY KeyObject,
    _In_ ULONG Flags);

VOID
NTAPI
CmpCloseKeyObject(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

VOID
NTAPI
CmpDeleteKeyObject(
    _In_ PVOID Object);

NTSTATUS
NTAPI
CmpQueryKeyName(
    _In_ PVOID Object,
    _In_ BOOLEAN HasObjectName,
    _Out_ POBJECT_NAME_INFORMATION ObjectNameInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE AccessMode);

NTSTATUS
NTAPI
CmpAppHiveAccessMode(
    PCMHIVE Hive,
    ACCESS_MASK DesiredAccess,
    KPROCESSOR_MODE AccessMode,
    KPROCESSOR_MODE *EffectiveMode);

NTSTATUS
CmpAssignSecurityDescriptor(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor);

NTSTATUS
CmpAssignSecurityDescriptorLocked(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor);

NTSTATUS
CmpCopyKeySecurity(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SourceCell,
    _In_ PHHIVE DestinationHive,
    _In_ HSTORAGE_TYPE StorageType,
    _Inout_ PHCELL_INDEX AnchorCell,
    _Out_ PHCELL_INDEX NewCell);

NTSTATUS
CmpGetKeySecurityDescriptor(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _Out_ PSECURITY_DESCRIPTOR *Descriptor);

PSECURITY_DESCRIPTOR
NTAPI
CmpHiveRootSecurityDescriptor(VOID);

NTSTATUS
NTAPI
CmpSecurityMethod(
    _In_ PVOID Object,
    _In_ SECURITY_OPERATION_CODE OperationType,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG CapturedLength,
    _Inout_ PSECURITY_DESCRIPTOR *ObjectSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode);

NTSTATUS
NTAPI
CmpCreateLinkNode(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_ PACCESS_STATE AccessState,
    _In_ UNICODE_STRING Name,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG CreateOptions,
    _In_ PCM_PARSE_CONTEXT Context,
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _In_ PULONG KcbsLocked,
    _Out_ PVOID *Object);

NTSTATUS
NTAPI
CmpDoCreate(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_ PACCESS_STATE AccessState,
    _In_ PUNICODE_STRING Name,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ PCM_PARSE_CONTEXT Context,
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _Out_ PVOID *Object);

BOOLEAN
NTAPI
CmpGetNextName(
    _Inout_ PUNICODE_STRING RemainingName,
    _Out_ PUNICODE_STRING NextName,
    _Out_ PBOOLEAN LastName);

NTSTATUS
NTAPI
CmpParseKeyEx(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_ POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    _Out_ PVOID *Object);

VOID
NTAPI
CmpBeginStarveWriters(VOID);

VOID
NTAPI
CmpEndStarveWriters(VOID);

VOID
NTAPI
CmpLockRegistry(VOID);

VOID
NTAPI
CmpLockRegistryExclusive(VOID);

VOID
NTAPI
CmpSetGlobalQuotaAllowed(VOID);

BOOLEAN
NTAPI
CmpTestRegistryLock(VOID);

BOOLEAN
NTAPI
CmpTestRegistryLockExclusive(VOID);

VOID
NTAPI
CmpUnlockRegistry(VOID);

VOID
NTAPI
CmpDestroySecurityCache(
    _In_ PCMHIVE Hive);

VOID
NTAPI
CmpInitSecurityCache(
    _In_ PCMHIVE Hive);

CODE_SEG("INIT")
PUNICODE_STRING*
NTAPI
CmGetSystemDriverList(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
CmInitSystem1(VOID);

VOID
NTAPI
CmpInitializeHiveList(VOID);

VOID
NTAPI
CmpSetVersionData(VOID);

VOID
CmpInitTransactions(VOID);

NTSTATUS
CmpTransAddKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PVOID Transaction,
    _In_ HSTORAGE_TYPE StorageType);

VOID
CmpTransAdjustKeyCounts(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PVOID Transaction,
    _Inout_ PULONG SubKeys,
    _Inout_ PULONG Values);

VOID
CmpTransBindKeyBody(
    _Inout_ PCM_KEY_BODY KeyBody,
    _In_ PVOID Transaction);

NTSTATUS
CmpTransCheckActive(
    _In_ PVOID Transaction);

NTSTATUS
CmpTransCheckCreate(
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _In_opt_ PVOID Transaction,
    _Out_ PVOID *ConflictEnlistment);

NTSTATUS
CmpTransDeleteKey(
    _In_ PCM_KEY_BODY KeyBody);

NTSTATUS
CmpTransDeleteValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PUNICODE_STRING ValueName);

NTSTATUS
CmpTransEnlist(
    _In_ PVOID Transaction);

HCELL_INDEX
CmpTransFindSubKeyByNumber(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Parent,
    _In_ ULONG Index,
    _In_opt_ PVOID Transaction);

BOOLEAN
CmpTransIsCellVisible(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_opt_ PVOID Transaction);

BOOLEAN
CmpTransIsKcbVisible(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PVOID Transaction);

HSTORAGE_TYPE
CmpTransParentStorage(
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _In_opt_ PVOID Transaction,
    _In_ HSTORAGE_TYPE CellType);

NTSTATUS
CmpTransPrepareWrite(
    _In_ PCM_KEY_BODY KeyBody);

VOID
CmpTransResolveConflict(
    _In_ PVOID Enlistment);

VOID
CmpTransUnbindKeyBody(
    _Inout_ PCM_KEY_BODY KeyBody);

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpCompareNewValueDataAgainstKCBCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Type,
    _In_ PVOID Data,
    _In_ ULONG DataSize);

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpFindValueByNameFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCUNICODE_STRING Name,
    _Out_ PCM_CACHED_VALUE **CachedValue,
    _Out_ ULONG *Index,
    _Out_ PCM_KEY_VALUE *Value,
    _Out_ BOOLEAN *ValueIsCached,
    _Out_ PHCELL_INDEX CellToRelease);

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpGetValueKeyFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCELL_DATA CellData,
    _In_ ULONG Index,
    _Out_ PCM_CACHED_VALUE **CachedValue,
    _Out_ PCM_KEY_VALUE *Value,
    _In_ BOOLEAN IndexIsCached,
    _Out_ BOOLEAN *ValueIsCached,
    _Out_ PHCELL_INDEX CellToRelease);

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpGetValueListFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _Out_ PCELL_DATA *CellData,
    _Out_ BOOLEAN *IndexIsCached,
    _Out_ PHCELL_INDEX ValueListToRelease);

VOID
NTAPI
CmpCloseNotify(
    PCM_KEY_BODY KeyBody);

VOID
NTAPI
CmpFlushNotify(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ BOOLEAN LockHeld);

VOID
NTAPI
CmpFlushNotifyOnKcb(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb);

VOID
NTAPI
CmpFlushNotifyThread(
    PETHREAD Thread);

CODE_SEG("INIT")
VOID
NTAPI
CmpInitNotify(VOID);

NTSTATUS
NTAPI
CmpNotifyChangeKey(
    PCM_KEY_BODY KeyBody,
    PCM_KEY_BODY SlaveKeyBody,
    PKEVENT Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    BOOLEAN IoStatus32,
    ULONG Filter,
    BOOLEAN WatchTree,
    BOOLEAN Asynchronous,
    KPROCESSOR_MODE PreviousMode);

VOID
NTAPI
CmpReportNotify(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_ ULONG Filter);

HCELL_INDEX
NTAPI
CmpCopyCell(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SourceCell,
    _In_ PHHIVE DestinationHive,
    _In_ HSTORAGE_TYPE StorageType);

NTSTATUS
NTAPI
CmEnumerateKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PVOID Transaction,
    _In_ ULONG Index,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _In_ PVOID KeyInformation,
    _In_ ULONG Length,
    _In_ PULONG ResultLength);

NTSTATUS
NTAPI
CmEnumerateValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ ULONG Index,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _In_ PVOID KeyValueInformation,
    _In_ ULONG Length,
    _In_ PULONG ResultLength);

NTSTATUS
NTAPI
CmQueryKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PVOID Transaction,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _In_ PVOID KeyInformation,
    _In_ ULONG Length,
    _In_ PULONG ResultLength);

NTSTATUS
NTAPI
CmQueryValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ UNICODE_STRING ValueName,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _In_ PVOID KeyValueInformation,
    _In_ ULONG Length,
    _In_ PULONG ResultLength);

NTSTATUS
NTAPI
CmSetValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Type,
    _In_ PVOID Data,
    _In_ ULONG DataSize);

CODE_SEG("INIT")
BOOLEAN
NTAPI
CmpGetSmbiosProcessorStrings(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_writes_(ManufacturerSize) PCHAR Manufacturer,
    _In_ SIZE_T ManufacturerSize,
    _Out_writes_(VersionSize) PCHAR Version,
    _In_ SIZE_T VersionSize);

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpQueryKeyValueData(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCM_CACHED_VALUE *CachedValue,
    _In_ PCM_KEY_VALUE ValueKey,
    _In_ BOOLEAN ValueIsCached,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _In_ PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength,
    _Out_ PNTSTATUS Status);

NTSTATUS
CmpTransEnumerateKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ ULONG Index,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength);

NTSTATUS
CmpTransEnumerateValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ ULONG Index,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength);

NTSTATUS
CmpTransQueryKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength);

NTSTATUS
CmpTransQueryValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PUNICODE_STRING ValueName,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength);

NTSTATUS
CmpTransSetValueKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize);

#include "cm_x.h"
