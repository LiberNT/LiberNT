/*
 */

#pragma once

#include <cmlib.h>
#include <cmreslist.h>


#define CM_KCB_SIGNATURE                                'bKmC'
#define CM_KCB_INVALID_SIGNATURE                        '4FmC'

#define CM_KCB_NO_SUBKEY                                0x01
#define CM_KCB_SUBKEY_ONE                               0x02
#define CM_KCB_SUBKEY_HINT                              0x04
#define CM_KCB_KEY_NON_EXIST                            0x10
#define CM_KCB_NO_DELAY_CLOSE                           0x20
#define CM_KCB_INVALID_CACHED_INFO                      0x40



#define CMP_HASH_IRRATIONAL                             314159269
#define CMP_HASH_PRIME                                  1000000007

#define CMP_CREATE_FAKE_KCB                             0x1


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












NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTAPI
NTAPI

VOID
NTAPI

VOID
NTAPI

CODE_SEG("INIT")
VOID
NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

CODE_SEG("INIT")
VOID
NTAPI

CODE_SEG("INIT")
VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

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

NTAPI

VOID
NTAPI

NTAPI

VOID
NTAPI

NTAPI

NTAPI

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

PVOID
NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

PULONG
NTAPI
CmpBuildAndLockKcbArray(
    _In_ PCM_HASH_CACHE_STACK HashCacheStack,
    _In_ ULONG KcbLockFlags,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _Inout_ PULONG OuterStackArray,
    _In_ ULONG TotalRemainingSubkeys,

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

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

NTAPI

CODE_SEG("INIT")
VOID
NTAPI

NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI


NTAPI

NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTSTATUS

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS


VOID
CmpInitTransactions(VOID);

NTSTATUS
    _In_ PVOID Transaction);

NTSTATUS
CmpTransCheckActive(
    _In_ PVOID Transaction);



HCELL_INDEX
CmpTransFindSubKeyByNumber(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Parent,
    _In_ ULONG Index,
    _In_opt_ PVOID Transaction);

    _In_ PCM_KEY_CONTROL_BLOCK Kcb,

HSTORAGE_TYPE
CmpTransParentStorage(
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _In_opt_ PVOID Transaction,
    _In_ HSTORAGE_TYPE CellType);

NTSTATUS

VOID

VOID
CmpTransUnbindKeyBody(
    _Inout_ PCM_KEY_BODY KeyBody);

    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Type,
    _In_ ULONG DataSize);


    _In_ ULONG Index,


    _In_ PCM_KEY_BODY KeyBody,


NTAPI

NTAPI

NTSTATUS
NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,

NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI
CODE_SEG("INIT")
BOOLEAN
NTAPI

NTAPI





#include "cm_x.h"
