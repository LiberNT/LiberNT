/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resource arbiter library shared by the PnP manager and bus drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ARBITER_SIGNATURE  'ibrA'
#define TAG_ARBITER        'lbrA'
#define TAG_ARB_ALLOCATION 'sbrA'
#define TAG_ARB_RANGE      'rbrA'

typedef struct _ARBITER_ORDERING
{
    UINT64 Start;
    UINT64 End;
} ARBITER_ORDERING, *PARBITER_ORDERING;

typedef struct _ARBITER_ORDERING_LIST
{
    UINT16 Count;
    UINT16 Maximum;
    PARBITER_ORDERING Orderings;
} ARBITER_ORDERING_LIST, *PARBITER_ORDERING_LIST;

typedef struct _ARBITER_ALTERNATIVE
{
    UINT64 Minimum;
    UINT64 Maximum;
    UINT64 Length;
    UINT64 Alignment;
    INT32 Priority;
    UINT32 Flags;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    UINT32 Reserved[3];
} ARBITER_ALTERNATIVE, *PARBITER_ALTERNATIVE;

typedef struct _ARBITER_ALLOCATION_STATE
{
    UINT64 Start;
    UINT64 End;
    UINT64 CurrentMinimum;
    UINT64 CurrentMaximum;
    PARBITER_LIST_ENTRY Entry;
    PARBITER_ALTERNATIVE CurrentAlternative;
    UINT32 AlternativeCount;
    PARBITER_ALTERNATIVE Alternatives;
    UINT16 Flags;
    UCHAR RangeAttributes;
    UCHAR RangeAvailableAttributes;
    UINT64 WorkSpace;
} ARBITER_ALLOCATION_STATE, *PARBITER_ALLOCATION_STATE;

typedef struct _ARBITER_INSTANCE *PARBITER_INSTANCE;

typedef NTSTATUS
(NTAPI * PARB_UNPACK_REQUIREMENT)(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PUINT64 OutLength,
    _Out_ PUINT64 OutAlignment
);

typedef NTSTATUS
(NTAPI * PARB_PACK_RESOURCE)(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor
);

typedef NTSTATUS
(NTAPI * PARB_UNPACK_RESOURCE)(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PUINT64 OutLength
);

typedef INT32
(NTAPI * PARB_SCORE_REQUIREMENT)(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor
);

typedef NTSTATUS
(NTAPI * PARB_TEST_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_TEST_ALLOCATION_PARAMETERS Parameters
);

typedef NTSTATUS
(NTAPI * PARB_RETEST_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_RETEST_ALLOCATION_PARAMETERS Parameters
);

typedef NTSTATUS
(NTAPI * PARB_COMMIT_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter
);

typedef NTSTATUS
(NTAPI * PARB_ROLLBACK_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter
);

typedef NTSTATUS
(NTAPI * PARB_BOOT_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_BOOT_ALLOCATION_PARAMETERS Parameters
);

typedef NTSTATUS
(NTAPI * PARB_QUERY_ARBITRATE)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_QUERY_ARBITRATE_PARAMETERS Parameters
);

typedef NTSTATUS
(NTAPI * PARB_QUERY_CONFLICT)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_QUERY_CONFLICT_PARAMETERS Parameters
);

typedef NTSTATUS
(NTAPI * PARB_ADD_RESERVED)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ADD_RESERVED_PARAMETERS Parameters
);

typedef NTSTATUS
(NTAPI * PARB_START_ARBITER)(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_opt_ PCM_RESOURCE_LIST StartResources
);

typedef NTSTATUS
(NTAPI * PARB_PREPROCESS_ENTRY)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef NTSTATUS
(NTAPI * PARB_ALLOCATE_ENTRY)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef BOOLEAN
(NTAPI * PARB_GET_NEXT_ALLOCATION_RANGE)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef BOOLEAN
(NTAPI * PARB_FIND_SUITABLE_RANGE)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef VOID
(NTAPI * PARB_ADD_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef VOID
(NTAPI * PARB_BACKTRACK_ALLOCATION)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef BOOLEAN
(NTAPI * PARB_OVERRIDE_CONFLICT)(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState
);

typedef NTSTATUS
(NTAPI * PARB_INITIALIZE_RANGE_LIST)(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ ULONG ResourceCount,
    _In_reads_(ResourceCount) PCM_PARTIAL_RESOURCE_DESCRIPTOR Resources,
    _Inout_ PRTL_RANGE_LIST RangeList
);

typedef NTSTATUS
(NTAPI * PARB_DELETE_OWNER_RANGES)(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_opt_ PVOID Owner
);

typedef struct _ARBITER_INSTANCE
{
    UINT32 Signature;
    PKEVENT MutexEvent;
    PCWSTR Name;
    PCWSTR OrderingName;
    CM_RESOURCE_TYPE ResourceType;
    PRTL_RANGE_LIST Allocation;
    PRTL_RANGE_LIST PossibleAllocation;
    ARBITER_ORDERING_LIST OrderingList;
    ARBITER_ORDERING_LIST ReservedList;
    INT32 ReferenceCount;
    PARBITER_INTERFACE Interface;
    UINT32 AllocationStackMaxSize;
    PARBITER_ALLOCATION_STATE AllocationStack;
    PARB_UNPACK_REQUIREMENT UnpackRequirement;
    PARB_PACK_RESOURCE PackResource;
    PARB_UNPACK_RESOURCE UnpackResource;
    PARB_SCORE_REQUIREMENT ScoreRequirement;
    PARB_TEST_ALLOCATION TestAllocation;
    PARB_RETEST_ALLOCATION RetestAllocation;
    PARB_COMMIT_ALLOCATION CommitAllocation;
    PARB_ROLLBACK_ALLOCATION RollbackAllocation;
    PARB_BOOT_ALLOCATION BootAllocation;
    PARB_QUERY_ARBITRATE QueryArbitrate;
    PARB_QUERY_CONFLICT QueryConflict;
    PARB_ADD_RESERVED AddReserved;
    PARB_START_ARBITER StartArbiter;
    PARB_PREPROCESS_ENTRY PreprocessEntry;
    PARB_ALLOCATE_ENTRY AllocateEntry;
    PARB_GET_NEXT_ALLOCATION_RANGE GetNextAllocationRange;
    PARB_FIND_SUITABLE_RANGE FindSuitableRange;
    PARB_ADD_ALLOCATION AddAllocation;
    PARB_BACKTRACK_ALLOCATION BacktrackAllocation;
    PARB_OVERRIDE_CONFLICT OverrideConflict;
    PARB_INITIALIZE_RANGE_LIST InitializeRangeList;
    PARB_DELETE_OWNER_RANGES DeleteOwnerRanges;
    BOOLEAN TransactionInProgress;
    PKEVENT TransactionEvent;
    PVOID Extension;
    PDEVICE_OBJECT BusDeviceObject;
    PVOID ConflictCallbackContext;
    PRTL_CONFLICT_RANGE_CALLBACK ConflictCallback;
} ARBITER_INSTANCE, *PARBITER_INSTANCE;

C_ASSERT(sizeof(PVOID) != 8 || sizeof(ARBITER_ALTERNATIVE) == 64);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(ARBITER_ALLOCATION_STATE) == 80);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(ARBITER_ORDERING_LIST) == 16);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(ARBITER_INSTANCE, OrderingName) == 24);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(ARBITER_INSTANCE, OrderingList) == 56);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(ARBITER_INSTANCE, AllocationStack) == 112);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(ARBITER_INSTANCE, OverrideConflict) == 272);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(ARBITER_INSTANCE, DeleteOwnerRanges) == 288);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(ARBITER_INSTANCE, TransactionEvent) == 304);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(ARBITER_INSTANCE, ConflictCallback) == 336);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(ARBITER_INSTANCE) == 344);

typedef NTSTATUS
(NTAPI * PARB_TRANSLATE_ORDERING)(
    _Out_ PIO_RESOURCE_DESCRIPTOR OutIoDescriptor,
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor
);

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PDEVICE_OBJECT BusDeviceObject,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _In_ PCWSTR ArbiterName,
    _In_ PCWSTR OrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction
);

CODE_SEG("PAGE")
VOID
NTAPI
ArbDeleteArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter
);

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbArbiterHandler(
    _Inout_opt_ PVOID Context,
    _In_ ARBITER_ACTION Action,
    _Inout_ PARBITER_PARAMETERS Parameters
);

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBuildAssignmentOrdering(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PCWSTR OrderName,
    _In_ PCWSTR ReservedOrderName,
    _In_opt_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction
);

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList
);

CODE_SEG("PAGE")
VOID
NTAPI
ArbFreeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList
);

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress
);

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPruneOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderingList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress
);
