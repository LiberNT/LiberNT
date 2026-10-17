/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resource arbiter library shared by the PnP manager and bus drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/rtlfuncs.h>
#include "arblib.h"

#define NDEBUG
#include <debug.h>

#define ARBP_ALTERNATIVE_SHARED  0x00000001
#define ARBP_ALTERNATIVE_FIXED   0x00000002
#define ARBP_ALTERNATIVE_INVALID 0x00000004

#define ARBP_STATE_WINDOW    0x0001
#define ARBP_STATE_NULL_DONE 0x0002

#define ARBP_RANGE_BOOT_ALLOCATED 0x01

#define ARBP_ORDERING_GROWTH 16
#define ARBP_MAX_STEPS       0x10000

static const WCHAR ArbpArbitersKey[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Arbiters\\";

static
UINT64
ArbpAlignDown(
    _In_ UINT64 Value,
    _In_ UINT64 Alignment)
{
    return Alignment > 1 ? Value - (Value % Alignment) : Value;
}

static
BOOLEAN
ArbpRangeIsCompatible(
    _In_ PRTL_RANGE Range,
    _In_ BOOLEAN Shared,
    _In_ UCHAR AvailableAttributes,
    _In_opt_ PARBITER_INSTANCE Arbiter)
{
    if (Shared && (Range->Flags & RTL_RANGE_SHARED))
        return TRUE;
    if (Range->Attributes & AvailableAttributes)
        return TRUE;
    if (Arbiter && Arbiter->ConflictCallback &&
        Arbiter->ConflictCallback(Arbiter->ConflictCallbackContext, Range))
    {
        return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
ArbpFindConflict(
    _In_ PRTL_RANGE_LIST RangeList,
    _In_ UINT64 Start,
    _In_ UINT64 End,
    _In_ BOOLEAN Shared,
    _In_ UCHAR AvailableAttributes,
    _In_opt_ PARBITER_INSTANCE Arbiter,
    _Out_ PUINT64 LowestConflict)
{
    RTL_RANGE_LIST_ITERATOR Iterator;
    PRTL_RANGE Range;
    BOOLEAN Conflict = FALSE;

    *LowestConflict = MAXULONGLONG;
    for (RtlGetFirstRange(RangeList, &Iterator, &Range);
         Range != NULL;
         RtlGetNextRange(&Iterator, &Range, TRUE))
    {
        if (Range->Start > End || Range->End < Start)
            continue;
        if (ArbpRangeIsCompatible(Range, Shared, AvailableAttributes, Arbiter))
            continue;
        Conflict = TRUE;
        if (Range->Start < *LowestConflict)
            *LowestConflict = Range->Start;
    }
    return Conflict;
}

static
BOOLEAN
ArbpSearchRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PRTL_RANGE_LIST RangeList,
    _In_opt_ PARBITER_ALLOCATION_STATE ArbState,
    _In_ UINT64 Minimum,
    _In_ UINT64 Maximum,
    _In_ UINT64 Length,
    _In_ UINT64 Alignment,
    _In_ BOOLEAN Shared,
    _In_ UCHAR AvailableAttributes,
    _Out_ PUINT64 Start)
{
    UINT64 Candidate;
    UINT64 Lowest;
    UINT64 Next;

    if (Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
        return FALSE;

    Candidate = ArbpAlignDown(Maximum - (Length - 1), Alignment);
    while (Candidate >= Minimum)
    {
        if (!ArbpFindConflict(RangeList, Candidate, Candidate + (Length - 1), Shared,
                              AvailableAttributes, Arbiter, &Lowest))
        {
            *Start = Candidate;
            return TRUE;
        }

        if (ArbState)
        {
            ArbState->Start = Candidate;
            ArbState->End = Candidate + (Length - 1);
            if (Arbiter->OverrideConflict(Arbiter, ArbState))
            {
                *Start = Candidate;
                return TRUE;
            }
        }

        if (Lowest == 0 || Lowest - 1 < Minimum || Lowest - 1 - Minimum < Length - 1)
            return FALSE;
        Next = ArbpAlignDown((Lowest - 1) - (Length - 1), Alignment);
        if (Next >= Candidate)
            return FALSE;
        Candidate = Next;
    }

    return FALSE;
}

static
NTSTATUS
ArbpGrowOrderingList(
    _Inout_ PARBITER_ORDERING_LIST OrderList)
{
    PARBITER_ORDERING Orderings;

    if (OrderList->Count < OrderList->Maximum)
        return STATUS_SUCCESS;
    if (OrderList->Maximum > MAXUSHORT - ARBP_ORDERING_GROWTH)
        return STATUS_INSUFFICIENT_RESOURCES;

    Orderings = ExAllocatePoolWithTag(PagedPool,
                                      (OrderList->Maximum + ARBP_ORDERING_GROWTH) * sizeof(ARBITER_ORDERING),
                                      TAG_ARBITER);
    if (!Orderings)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (OrderList->Orderings)
    {
        RtlCopyMemory(Orderings, OrderList->Orderings, OrderList->Count * sizeof(ARBITER_ORDERING));
        ExFreePoolWithTag(OrderList->Orderings, TAG_ARBITER);
    }
    OrderList->Orderings = Orderings;
    OrderList->Maximum += ARBP_ORDERING_GROWTH;
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    OrderList->Count = 0;
    OrderList->Maximum = 0;
    OrderList->Orderings = NULL;
    return ArbpGrowOrderingList(OrderList);
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbFreeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    if (OrderList->Orderings)
        ExFreePoolWithTag(OrderList->Orderings, TAG_ARBITER);
    OrderList->Orderings = NULL;
    OrderList->Count = 0;
    OrderList->Maximum = 0;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (MaximumAddress < MinimumAddress)
        return STATUS_INVALID_PARAMETER;

    Status = ArbpGrowOrderingList(OrderList);
    if (!NT_SUCCESS(Status))
        return Status;

    OrderList->Orderings[OrderList->Count].Start = MinimumAddress;
    OrderList->Orderings[OrderList->Count].End = MaximumAddress;
    OrderList->Count++;
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPruneOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderingList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    ULONG Index = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (MaximumAddress < MinimumAddress)
        return STATUS_INVALID_PARAMETER;

    while (Index < OrderingList->Count)
    {
        PARBITER_ORDERING Ordering = &OrderingList->Orderings[Index];

        if (Ordering->End < MinimumAddress || Ordering->Start > MaximumAddress)
        {
            Index++;
            continue;
        }

        if (Ordering->Start >= MinimumAddress && Ordering->End <= MaximumAddress)
        {
            RtlMoveMemory(Ordering, Ordering + 1,
                          (OrderingList->Count - Index - 1) * sizeof(ARBITER_ORDERING));
            OrderingList->Count--;
            continue;
        }

        if (Ordering->Start < MinimumAddress && Ordering->End > MaximumAddress)
        {
            UINT64 TailEnd = Ordering->End;

            Status = ArbpGrowOrderingList(OrderingList);
            if (!NT_SUCCESS(Status))
                return Status;
            Ordering = &OrderingList->Orderings[Index];
            RtlMoveMemory(Ordering + 2, Ordering + 1,
                          (OrderingList->Count - Index - 1) * sizeof(ARBITER_ORDERING));
            Ordering->End = MinimumAddress - 1;
            Ordering[1].Start = MaximumAddress + 1;
            Ordering[1].End = TailEnd;
            OrderingList->Count++;
            Index += 2;
            continue;
        }

        if (Ordering->Start < MinimumAddress)
            Ordering->End = MinimumAddress - 1;
        else
            Ordering->Start = MaximumAddress + 1;
        Index++;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
ArbpQueryOrderingValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _Out_ PKEY_VALUE_PARTIAL_INFORMATION* Information)
{
    UNICODE_STRING Name;
    ULONG Length = 0;
    NTSTATUS Status;

    *Information = NULL;
    RtlInitUnicodeString(&Name, ValueName);
    Status = ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        return Status;

    *Information = ExAllocatePoolWithTag(PagedPool, Length, TAG_ARBITER);
    if (!*Information)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, *Information, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(*Information, TAG_ARBITER);
        *Information = NULL;
    }
    return Status;
}

static
NTSTATUS
ArbpReadOrdering(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PCWSTR KeyName,
    _In_opt_ PCWSTR ValueName,
    _In_opt_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction,
    _Inout_ PARBITER_ORDERING_LIST OrderList)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyPath;
    PKEY_VALUE_PARTIAL_INFORMATION Information = NULL;
    PIO_RESOURCE_REQUIREMENTS_LIST Requirements;
    PIO_RESOURCE_LIST List;
    PUCHAR DataEnd;
    WCHAR PathBuffer[128];
    HANDLE Key;
    ULONG ListIndex;
    NTSTATUS Status;

    if (!ValueName)
        return STATUS_SUCCESS;

    KeyPath.Buffer = PathBuffer;
    KeyPath.Length = 0;
    KeyPath.MaximumLength = sizeof(PathBuffer);
    RtlAppendUnicodeToString(&KeyPath, ArbpArbitersKey);
    Status = RtlAppendUnicodeToString(&KeyPath, KeyName);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes, &KeyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status == STATUS_OBJECT_NAME_NOT_FOUND ? STATUS_SUCCESS : Status;

    Status = ArbpQueryOrderingValue(Key, ValueName, &Information);
    if (NT_SUCCESS(Status) && Information->Type == REG_SZ)
    {
        WCHAR Redirect[64];
        ULONG Characters = min(Information->DataLength / sizeof(WCHAR), RTL_NUMBER_OF(Redirect) - 1);

        RtlCopyMemory(Redirect, Information->Data, Characters * sizeof(WCHAR));
        Redirect[Characters] = UNICODE_NULL;
        ExFreePoolWithTag(Information, TAG_ARBITER);
        Information = NULL;
        Status = ArbpQueryOrderingValue(Key, Redirect, &Information);
    }
    ZwClose(Key);

    if (!NT_SUCCESS(Status))
        return Status == STATUS_OBJECT_NAME_NOT_FOUND ? STATUS_SUCCESS : Status;

    if (Information->Type != REG_RESOURCE_REQUIREMENTS_LIST ||
        Information->DataLength < FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List))
    {
        ExFreePoolWithTag(Information, TAG_ARBITER);
        return STATUS_SUCCESS;
    }

    Requirements = (PIO_RESOURCE_REQUIREMENTS_LIST)Information->Data;
    DataEnd = Information->Data + Information->DataLength;
    List = &Requirements->List[0];
    for (ListIndex = 0; ListIndex < Requirements->AlternativeLists; ListIndex++)
    {
        ULONG Index;

        if ((PUCHAR)List + FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors) > DataEnd ||
            List->Count > (ULONG_PTR)(DataEnd - (PUCHAR)List->Descriptors) / sizeof(IO_RESOURCE_DESCRIPTOR))
        {
            break;
        }

        for (Index = 0; Index < List->Count; Index++)
        {
            IO_RESOURCE_DESCRIPTOR Descriptor = List->Descriptors[Index];
            UINT64 Minimum, Maximum, Length, Alignment;

            if (TranslateOrderingFunction &&
                !NT_SUCCESS(TranslateOrderingFunction(&Descriptor, &List->Descriptors[Index])))
            {
                continue;
            }
            if (Descriptor.Type != Arbiter->ResourceType)
                continue;
            if (!NT_SUCCESS(Arbiter->UnpackRequirement(&Descriptor, &Minimum, &Maximum, &Length, &Alignment)))
                continue;

            Status = ArbAddOrdering(OrderList, Minimum, Maximum);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Information, TAG_ARBITER);
                return Status;
            }
        }
        List = (PIO_RESOURCE_LIST)&List->Descriptors[List->Count];
    }

    ExFreePoolWithTag(Information, TAG_ARBITER);
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBuildAssignmentOrdering(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PCWSTR OrderName,
    _In_ PCWSTR ReservedOrderName,
    _In_opt_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    ArbFreeOrderingList(&Arbiter->OrderingList);
    ArbFreeOrderingList(&Arbiter->ReservedList);

    Status = ArbInitializeOrderingList(&Arbiter->OrderingList);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ArbInitializeOrderingList(&Arbiter->ReservedList);
    if (!NT_SUCCESS(Status))
        goto Fail;

    Status = ArbpReadOrdering(Arbiter, L"AllocationOrder", OrderName,
                              TranslateOrderingFunction, &Arbiter->OrderingList);
    if (!NT_SUCCESS(Status))
        goto Fail;
    Status = ArbpReadOrdering(Arbiter, L"ReservedResources", ReservedOrderName,
                              TranslateOrderingFunction, &Arbiter->ReservedList);
    if (!NT_SUCCESS(Status))
        goto Fail;

    for (Index = 0; Index < Arbiter->ReservedList.Count; Index++)
    {
        Status = ArbPruneOrdering(&Arbiter->OrderingList,
                                  Arbiter->ReservedList.Orderings[Index].Start,
                                  Arbiter->ReservedList.Orderings[Index].End);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    DPRINT("ArbBuildAssignmentOrdering: '%S' %u orderings, %u reserved\n",
           Arbiter->Name, Arbiter->OrderingList.Count, Arbiter->ReservedList.Count);
    return STATUS_SUCCESS;

Fail:
    ArbFreeOrderingList(&Arbiter->OrderingList);
    ArbFreeOrderingList(&Arbiter->ReservedList);
    return Status;
}

static
INT32
ArbpScoreAlternative(
    _In_ PARBITER_ALTERNATIVE Alternative)
{
    UINT64 Positions;

    if (Alternative->Flags & ARBP_ALTERNATIVE_INVALID)
        return MAXLONG;
    if (Alternative->Length == 0)
        return 0;

    Positions = (Alternative->Maximum - Alternative->Minimum - (Alternative->Length - 1)) / Alternative->Alignment + 1;
    return Positions > MAXLONG ? MAXLONG : (INT32)Positions;
}

static
VOID
ArbpFreeStates(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Arbiter->AllocationStack[Index].Alternatives)
            ExFreePoolWithTag(Arbiter->AllocationStack[Index].Alternatives, TAG_ARB_ALLOCATION);
        Arbiter->AllocationStack[Index].Alternatives = NULL;
    }
}

static
NTSTATUS
ArbpBuildStates(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList,
    _Out_ PULONG StateCount)
{
    PLIST_ENTRY Link;
    ULONG Count = 0;
    ULONG Size;
    NTSTATUS Status;

    *StateCount = 0;
    for (Link = ArbitrationList->Flink; Link != ArbitrationList; Link = Link->Flink)
        Count++;
    if (Count == 0)
        return STATUS_INVALID_PARAMETER;
    if (Count >= MAXULONG / sizeof(ARBITER_ALLOCATION_STATE) - 1)
        return STATUS_INSUFFICIENT_RESOURCES;

    Size = (Count + 1) * sizeof(ARBITER_ALLOCATION_STATE);
    if (Size > Arbiter->AllocationStackMaxSize)
    {
        PARBITER_ALLOCATION_STATE Stack = ExAllocatePoolWithTag(PagedPool, Size, TAG_ARB_ALLOCATION);

        if (!Stack)
            return STATUS_INSUFFICIENT_RESOURCES;
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        Arbiter->AllocationStack = Stack;
        Arbiter->AllocationStackMaxSize = Size;
    }
    RtlZeroMemory(Arbiter->AllocationStack, Size);

    Count = 0;
    for (Link = ArbitrationList->Flink; Link != ArbitrationList; Link = Link->Flink)
    {
        PARBITER_LIST_ENTRY Entry = CONTAINING_RECORD(Link, ARBITER_LIST_ENTRY, ListEntry);
        PARBITER_ALLOCATION_STATE State = &Arbiter->AllocationStack[Count];
        ULONG Index;

        Count++;
        *StateCount = Count;
        State->Entry = Entry;
        Entry->Result = ArbiterResultUndefined;
        if (Entry->AlternativeCount == 0 || !Entry->Alternatives || !Entry->Assignment ||
            Entry->AlternativeCount >= MAXULONG / sizeof(ARBITER_ALTERNATIVE))
        {
            return STATUS_INVALID_PARAMETER;
        }

        State->Alternatives = ExAllocatePoolWithTag(PagedPool,
                                                    Entry->AlternativeCount * sizeof(ARBITER_ALTERNATIVE),
                                                    TAG_ARB_ALLOCATION);
        if (!State->Alternatives)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(State->Alternatives, Entry->AlternativeCount * sizeof(ARBITER_ALTERNATIVE));
        State->AlternativeCount = Entry->AlternativeCount;

        for (Index = 0; Index < Entry->AlternativeCount; Index++)
        {
            PARBITER_ALTERNATIVE Alternative = &State->Alternatives[Index];
            PIO_RESOURCE_DESCRIPTOR Descriptor = &Entry->Alternatives[Index];

            Alternative->Descriptor = Descriptor;
            if (!NT_SUCCESS(Arbiter->UnpackRequirement(Descriptor,
                                                       &Alternative->Minimum,
                                                       &Alternative->Maximum,
                                                       &Alternative->Length,
                                                       &Alternative->Alignment)))
            {
                Alternative->Flags |= ARBP_ALTERNATIVE_INVALID;
                continue;
            }

            if (Alternative->Alignment == 0)
                Alternative->Alignment = 1;
            if (Alternative->Minimum > Alternative->Maximum ||
                (Alternative->Length && Alternative->Length - 1 > Alternative->Maximum - Alternative->Minimum))
            {
                Alternative->Flags |= ARBP_ALTERNATIVE_INVALID;
                continue;
            }
            if (Descriptor->ShareDisposition == CmResourceShareShared)
                Alternative->Flags |= ARBP_ALTERNATIVE_SHARED;
            if (Alternative->Length && Alternative->Maximum - Alternative->Minimum == Alternative->Length - 1)
                Alternative->Flags |= ARBP_ALTERNATIVE_FIXED;
            Alternative->Priority = Arbiter->ScoreRequirement ? Arbiter->ScoreRequirement(Descriptor)
                                                              : ArbpScoreAlternative(Alternative);
        }

        if (Entry->Flags & ARBITER_FLAG_BOOT_CONFIG)
            State->RangeAvailableAttributes |= ARBP_RANGE_BOOT_ALLOCATED;

        Status = Arbiter->PreprocessEntry(Arbiter, State);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static
INT64
ArbpStateScore(
    _In_ PARBITER_ALLOCATION_STATE State)
{
    INT64 Score = 0;
    ULONG Index;

    for (Index = 0; Index < State->AlternativeCount; Index++)
    {
        if (!(State->Alternatives[Index].Flags & ARBP_ALTERNATIVE_INVALID))
            Score += State->Alternatives[Index].Priority;
    }
    return Score;
}

static
VOID
ArbpSortStates(
    _Inout_ PARBITER_ALLOCATION_STATE States,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 1; Index < Count; Index++)
    {
        ARBITER_ALLOCATION_STATE Key = States[Index];
        INT64 KeyScore = ArbpStateScore(&Key);
        ULONG Slot = Index;

        while (Slot > 0 && ArbpStateScore(&States[Slot - 1]) > KeyScore)
        {
            States[Slot] = States[Slot - 1];
            Slot--;
        }
        States[Slot] = Key;
    }
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPreprocessEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbGetNextAllocationRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PARBITER_ALTERNATIVE End = ArbState->Alternatives + ArbState->AlternativeCount;
    PARBITER_ALTERNATIVE Alternative;
    UINT64 Index;

    PAGED_CODE();

    if (!ArbState->CurrentAlternative)
    {
        Alternative = ArbState->Alternatives;
        Index = 0;
    }
    else
    {
        Alternative = ArbState->CurrentAlternative;
        Index = ArbState->WorkSpace + 1;
    }

    for (; Alternative < End; Alternative++, Index = 0)
    {
        BOOLEAN UseOrdering;
        UINT64 Count;

        if (Alternative->Flags & ARBP_ALTERNATIVE_INVALID)
            continue;

        UseOrdering = !(Alternative->Flags & ARBP_ALTERNATIVE_FIXED) && Alternative->Length &&
                      Arbiter->OrderingList.Count;
        Count = UseOrdering ? Arbiter->OrderingList.Count : 1;

        for (; Index < Count; Index++)
        {
            UINT64 Minimum = Alternative->Minimum;
            UINT64 Maximum = Alternative->Maximum;

            if (UseOrdering)
            {
                PARBITER_ORDERING Ordering = &Arbiter->OrderingList.Orderings[Index];

                Minimum = max(Minimum, Ordering->Start);
                Maximum = min(Maximum, Ordering->End);
                if (Minimum > Maximum || Maximum - Minimum < Alternative->Length - 1)
                    continue;
            }

            ArbState->CurrentAlternative = Alternative;
            ArbState->WorkSpace = Index;
            ArbState->CurrentMinimum = Minimum;
            ArbState->CurrentMaximum = Maximum;
            ArbState->Flags &= ~ARBP_STATE_NULL_DONE;
            return TRUE;
        }
    }

    ArbState->CurrentAlternative = End;
    return FALSE;
}

static
CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbFindSuitableRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PARBITER_ALTERNATIVE Alternative = ArbState->CurrentAlternative;
    UINT64 Start;

    PAGED_CODE();

    if (Alternative->Length == 0)
    {
        if (ArbState->Flags & ARBP_STATE_NULL_DONE)
            return FALSE;
        ArbState->Flags |= ARBP_STATE_NULL_DONE;
        ArbState->Start = ArbState->CurrentMinimum;
        ArbState->End = ArbState->CurrentMinimum;
        return TRUE;
    }

    if (ArbState->CurrentMaximum < ArbState->CurrentMinimum)
        return FALSE;

    if (!ArbpSearchRange(Arbiter,
                         Arbiter->PossibleAllocation,
                         ArbState,
                         ArbState->CurrentMinimum,
                         ArbState->CurrentMaximum,
                         Alternative->Length,
                         Alternative->Alignment,
                         (Alternative->Flags & ARBP_ALTERNATIVE_SHARED) != 0,
                         ArbState->RangeAvailableAttributes,
                         &Start))
    {
        return FALSE;
    }

    ArbState->Start = Start;
    ArbState->End = Start + (Alternative->Length - 1);
    if (Start > ArbState->CurrentMinimum)
    {
        ArbState->CurrentMaximum = (Start - 1) + (Alternative->Length - 1);
    }
    else
    {
        ArbState->CurrentMinimum = 1;
        ArbState->CurrentMaximum = 0;
    }
    return TRUE;
}

static
CODE_SEG("PAGE")
VOID
NTAPI
ArbAddAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    ULONG Flags = RTL_RANGE_LIST_ADD_IF_CONFLICT;
    NTSTATUS Status;

    PAGED_CODE();

    if (ArbState->CurrentAlternative->Flags & ARBP_ALTERNATIVE_SHARED)
        Flags |= RTL_RANGE_LIST_ADD_SHARED;

    Status = RtlAddRange(Arbiter->PossibleAllocation,
                         ArbState->Start,
                         ArbState->End,
                         ArbState->RangeAttributes,
                         Flags,
                         NULL,
                         ArbState->Entry->PhysicalDeviceObject);
    if (!NT_SUCCESS(Status))
        DPRINT1("ArbAddAllocation: '%S' RtlAddRange failed 0x%lx\n", Arbiter->Name, Status);
}

static
CODE_SEG("PAGE")
VOID
NTAPI
ArbBacktrackAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    RtlDeleteRange(Arbiter->PossibleAllocation,
                   ArbState->Start,
                   ArbState->End,
                   ArbState->Entry->PhysicalDeviceObject);
}

static
CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbOverrideConflict(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    return FALSE;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAllocateEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    for (;;)
    {
        if (!(ArbState->Flags & ARBP_STATE_WINDOW))
        {
            if (!Arbiter->GetNextAllocationRange(Arbiter, ArbState))
                return STATUS_UNSUCCESSFUL;
            ArbState->Flags |= ARBP_STATE_WINDOW;
        }

        if (Arbiter->FindSuitableRange(Arbiter, ArbState))
        {
            if (ArbState->CurrentAlternative->Length)
                Arbiter->AddAllocation(Arbiter, ArbState);
            return STATUS_SUCCESS;
        }

        ArbState->Flags &= ~ARBP_STATE_WINDOW;
    }
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeRangeList(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ ULONG ResourceCount,
    _In_reads_(ResourceCount) PCM_PARTIAL_RESOURCE_DESCRIPTOR Resources,
    _Inout_ PRTL_RANGE_LIST RangeList)
{
    RTL_RANGE_LIST Available;
    RTL_RANGE_LIST Unavailable;
    RTL_RANGE_LIST_ITERATOR Iterator;
    PRTL_RANGE Range;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    RtlInitializeRangeList(&Available);
    RtlInitializeRangeList(&Unavailable);

    for (Index = 0; Index < ResourceCount; Index++)
    {
        UINT64 Start, Length;

        if (!NT_SUCCESS(Arbiter->UnpackResource(&Resources[Index], &Start, &Length)) || Length == 0)
            continue;
        Status = RtlAddRange(&Available, Start, Start + (Length - 1), 0,
                             RTL_RANGE_LIST_ADD_IF_CONFLICT, NULL, NULL);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    Status = RtlInvertRangeList(&Unavailable, &Available);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (RtlGetFirstRange(&Unavailable, &Iterator, &Range);
         Range != NULL;
         RtlGetNextRange(&Iterator, &Range, TRUE))
    {
        Status = RtlAddRange(RangeList, Range->Start, Range->End, 0,
                             RTL_RANGE_LIST_ADD_IF_CONFLICT, NULL, Arbiter);
        if (!NT_SUCCESS(Status))
            break;
    }

Done:
    RtlFreeRangeList(&Available);
    RtlFreeRangeList(&Unavailable);
    return Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbDeleteOwnerRanges(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_opt_ PVOID Owner)
{
    PAGED_CODE();

    if (!Owner)
        return STATUS_SUCCESS;
    return RtlDeleteOwnersRanges(Arbiter->PossibleAllocation, Owner);
}

static
NTSTATUS
ArbpPrepareTransaction(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList,
    _In_ ULONG AllocateFromCount,
    _In_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR AllocateFrom)
{
    PLIST_ENTRY Link;
    NTSTATUS Status;

    RtlFreeRangeList(Arbiter->PossibleAllocation);
    Status = RtlCopyRangeList(Arbiter->PossibleAllocation, Arbiter->Allocation);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Link = ArbitrationList->Flink; Link != ArbitrationList; Link = Link->Flink)
    {
        PARBITER_LIST_ENTRY Entry = CONTAINING_RECORD(Link, ARBITER_LIST_ENTRY, ListEntry);

        Status = Arbiter->DeleteOwnerRanges(Arbiter, Entry->PhysicalDeviceObject);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (AllocateFromCount && AllocateFrom)
        Status = Arbiter->InitializeRangeList(Arbiter, AllocateFromCount, AllocateFrom, Arbiter->PossibleAllocation);
    return Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbTestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_TEST_ALLOCATION_PARAMETERS Parameters)
{
    PARBITER_ALLOCATION_STATE Stack;
    ULONG Count = 0;
    ULONG Index = 0;
    ULONG Steps = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Parameters || !Parameters->ArbitrationList || IsListEmpty(Parameters->ArbitrationList))
        return STATUS_INVALID_PARAMETER;

    Status = ArbpPrepareTransaction(Arbiter, Parameters->ArbitrationList,
                                    Parameters->AllocateFromCount, Parameters->AllocateFrom);
    if (NT_SUCCESS(Status))
        Status = ArbpBuildStates(Arbiter, Parameters->ArbitrationList, &Count);
    if (!NT_SUCCESS(Status))
    {
        ArbpFreeStates(Arbiter, Count);
        RtlFreeRangeList(Arbiter->PossibleAllocation);
        return Status;
    }

    Stack = Arbiter->AllocationStack;
    ArbpSortStates(Stack, Count);

    while (Index < Count)
    {
        PARBITER_ALLOCATION_STATE State = &Stack[Index];

        if (++Steps > ARBP_MAX_STEPS)
        {
            Status = STATUS_UNSUCCESSFUL;
            break;
        }

        Status = Arbiter->AllocateEntry(Arbiter, State);
        if (NT_SUCCESS(Status))
        {
            Index++;
            continue;
        }

        State->CurrentAlternative = NULL;
        State->Flags &= ~(ARBP_STATE_WINDOW | ARBP_STATE_NULL_DONE);
        if (Index == 0)
        {
            State->Entry->Result = ArbiterResultExternalConflict;
            break;
        }

        Index--;
        if (Stack[Index].CurrentAlternative->Length)
            Arbiter->BacktrackAllocation(Arbiter, &Stack[Index]);
    }

    if (NT_SUCCESS(Status) && Index == Count)
    {
        for (Index = 0; Index < Count; Index++)
        {
            PARBITER_ALLOCATION_STATE State = &Stack[Index];
            PARBITER_LIST_ENTRY Entry = State->Entry;

            Entry->SelectedAlternative = State->CurrentAlternative->Descriptor;
            Status = Arbiter->PackResource(State->CurrentAlternative->Descriptor, State->Start, Entry->Assignment);
            if (!NT_SUCCESS(Status))
                break;
            Entry->Result = State->CurrentAlternative->Length ? ArbiterResultSuccess : ArbiterResultNullRequest;
        }
    }
    else if (NT_SUCCESS(Status))
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    ArbpFreeStates(Arbiter, Count);
    if (NT_SUCCESS(Status))
    {
        Arbiter->TransactionInProgress = TRUE;
    }
    else
    {
        DPRINT("ArbTestAllocation: '%S' failed 0x%lx after %lu steps\n", Arbiter->Name, Status, Steps);
        RtlFreeRangeList(Arbiter->PossibleAllocation);
    }
    return Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRetestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_RETEST_ALLOCATION_PARAMETERS Parameters)
{
    PLIST_ENTRY Link;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Parameters || !Parameters->ArbitrationList || IsListEmpty(Parameters->ArbitrationList))
        return STATUS_INVALID_PARAMETER;

    Status = ArbpPrepareTransaction(Arbiter, Parameters->ArbitrationList,
                                    Parameters->AllocateFromCount, Parameters->AllocateFrom);

    for (Link = Parameters->ArbitrationList->Flink;
         NT_SUCCESS(Status) && Link != Parameters->ArbitrationList;
         Link = Link->Flink)
    {
        PARBITER_LIST_ENTRY Entry = CONTAINING_RECORD(Link, ARBITER_LIST_ENTRY, ListEntry);
        ULONG Flags = RTL_RANGE_LIST_ADD_IF_CONFLICT;
        UINT64 Start, Length;

        if (!Entry->Assignment || Entry->Result == ArbiterResultNullRequest)
            continue;
        Status = Arbiter->UnpackResource(Entry->Assignment, &Start, &Length);
        if (!NT_SUCCESS(Status) || Length == 0)
            continue;
        if (Entry->Assignment->ShareDisposition == CmResourceShareShared)
            Flags |= RTL_RANGE_LIST_ADD_SHARED;
        Status = RtlAddRange(Arbiter->PossibleAllocation, Start, Start + (Length - 1), 0, Flags,
                             NULL, Entry->PhysicalDeviceObject);
    }

    if (NT_SUCCESS(Status))
        Arbiter->TransactionInProgress = TRUE;
    else
        RtlFreeRangeList(Arbiter->PossibleAllocation);
    return Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbCommitAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PRTL_RANGE_LIST Previous;

    PAGED_CODE();

    RtlDeleteOwnersRanges(Arbiter->PossibleAllocation, Arbiter);
    Previous = Arbiter->Allocation;
    Arbiter->Allocation = Arbiter->PossibleAllocation;
    Arbiter->PossibleAllocation = Previous;
    RtlFreeRangeList(Arbiter->PossibleAllocation);
    Arbiter->TransactionInProgress = FALSE;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRollbackAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    RtlFreeRangeList(Arbiter->PossibleAllocation);
    Arbiter->TransactionInProgress = FALSE;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBootAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_BOOT_ALLOCATION_PARAMETERS Parameters)
{
    PLIST_ENTRY Link;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Parameters || !Parameters->ArbitrationList)
        return STATUS_INVALID_PARAMETER;

    RtlFreeRangeList(Arbiter->PossibleAllocation);
    Status = RtlCopyRangeList(Arbiter->PossibleAllocation, Arbiter->Allocation);

    for (Link = Parameters->ArbitrationList->Flink;
         NT_SUCCESS(Status) && Link != Parameters->ArbitrationList;
         Link = Link->Flink)
    {
        PARBITER_LIST_ENTRY Entry = CONTAINING_RECORD(Link, ARBITER_LIST_ENTRY, ListEntry);
        ULONG Index;

        for (Index = 0; Index < Entry->AlternativeCount && Entry->Alternatives; Index++)
        {
            PIO_RESOURCE_DESCRIPTOR Descriptor = &Entry->Alternatives[Index];
            ULONG Flags = RTL_RANGE_LIST_ADD_IF_CONFLICT;
            UINT64 Minimum, Maximum, Length, Alignment;

            if (!NT_SUCCESS(Arbiter->UnpackRequirement(Descriptor, &Minimum, &Maximum, &Length, &Alignment)))
                continue;
            if (Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
                continue;
            if (Descriptor->ShareDisposition == CmResourceShareShared)
                Flags |= RTL_RANGE_LIST_ADD_SHARED;

            Status = RtlAddRange(Arbiter->PossibleAllocation, Minimum, Minimum + (Length - 1),
                                 ARBP_RANGE_BOOT_ALLOCATED, Flags, NULL, Entry->PhysicalDeviceObject);
            break;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        RtlFreeRangeList(Arbiter->PossibleAllocation);
        return Status;
    }
    return ArbCommitAllocation(Arbiter);
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryConflict(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_QUERY_CONFLICT_PARAMETERS Parameters)
{
    PARBITER_CONFLICT_INFO Conflicts;
    RTL_RANGE_LIST Others;
    RTL_RANGE_LIST_ITERATOR Iterator;
    PRTL_RANGE Range;
    UINT64 Minimum, Maximum, Length, Alignment, Start;
    BOOLEAN Shared;
    ULONG Count = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Parameters || !Parameters->ConflictingResource || !Parameters->ConflictCount || !Parameters->Conflicts)
        return STATUS_INVALID_PARAMETER;

    *Parameters->ConflictCount = 0;
    *Parameters->Conflicts = NULL;

    Status = Arbiter->UnpackRequirement(Parameters->ConflictingResource, &Minimum, &Maximum, &Length, &Alignment);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
        return STATUS_SUCCESS;
    if (Alignment == 0)
        Alignment = 1;
    Shared = Parameters->ConflictingResource->ShareDisposition == CmResourceShareShared;

    RtlInitializeRangeList(&Others);
    Status = RtlCopyRangeList(&Others, Arbiter->Allocation);
    if (NT_SUCCESS(Status) && Parameters->PhysicalDeviceObject)
        Status = RtlDeleteOwnersRanges(&Others, Parameters->PhysicalDeviceObject);
    if (!NT_SUCCESS(Status))
        goto Done;

    if (ArbpSearchRange(Arbiter, &Others, NULL, Minimum, Maximum, Length, Alignment, Shared, 0, &Start))
        goto Done;

    for (RtlGetFirstRange(&Others, &Iterator, &Range); Range != NULL; RtlGetNextRange(&Iterator, &Range, TRUE))
    {
        if (Range->Start <= Maximum && Range->End >= Minimum && !ArbpRangeIsCompatible(Range, Shared, 0, NULL))
            Count++;
    }
    if (Count == 0)
        goto Done;

    Conflicts = ExAllocatePoolWithTag(PagedPool, Count * sizeof(ARBITER_CONFLICT_INFO), TAG_ARBITER);
    if (!Conflicts)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    Count = 0;
    for (RtlGetFirstRange(&Others, &Iterator, &Range); Range != NULL; RtlGetNextRange(&Iterator, &Range, TRUE))
    {
        if (Range->Start > Maximum || Range->End < Minimum || ArbpRangeIsCompatible(Range, Shared, 0, NULL))
            continue;
        Conflicts[Count].OwningObject = Range->Owner;
        Conflicts[Count].Start = Range->Start;
        Conflicts[Count].End = Range->End;
        Count++;
    }
    *Parameters->ConflictCount = Count;
    *Parameters->Conflicts = Conflicts;

Done:
    RtlFreeRangeList(&Others);
    return Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryArbitrate(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_QUERY_ARBITRATE_PARAMETERS Parameters)
{
    PAGED_CODE();

    if (!Parameters || !Parameters->ArbitrationList)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddReserved(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ADD_RESERVED_PARAMETERS Parameters)
{
    PAGED_CODE();

    return STATUS_NOT_SUPPORTED;
}

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbStartArbiter(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_opt_ PCM_RESOURCE_LIST StartResources)
{
    PAGED_CODE();

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbArbiterHandler(
    _Inout_opt_ PVOID Context,
    _In_ ARBITER_ACTION Action,
    _Inout_ PARBITER_PARAMETERS Parameters)
{
    PARBITER_INSTANCE Arbiter = Context;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Arbiter || Arbiter->Signature != ARBITER_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    if (!Parameters && Action != ArbiterActionCommitAllocation && Action != ArbiterActionRollbackAllocation)
        return STATUS_INVALID_PARAMETER;

    KeWaitForSingleObject(Arbiter->MutexEvent, Executive, KernelMode, FALSE, NULL);

    switch (Action)
    {
        case ArbiterActionTestAllocation:
            Status = Arbiter->TestAllocation(Arbiter, &Parameters->Parameters.TestAllocation);
            break;
        case ArbiterActionRetestAllocation:
            Status = Arbiter->RetestAllocation(Arbiter, &Parameters->Parameters.RetestAllocation);
            break;
        case ArbiterActionCommitAllocation:
            Status = Arbiter->TransactionInProgress ? Arbiter->CommitAllocation(Arbiter)
                                                    : STATUS_INVALID_DEVICE_STATE;
            break;
        case ArbiterActionRollbackAllocation:
            Status = Arbiter->RollbackAllocation(Arbiter);
            break;
        case ArbiterActionBootAllocation:
            Status = Arbiter->BootAllocation(Arbiter, &Parameters->Parameters.BootAllocation);
            break;
        case ArbiterActionQueryConflict:
            Status = Arbiter->QueryConflict(Arbiter, &Parameters->Parameters.QueryConflict);
            break;
        case ArbiterActionQueryArbitrate:
            Status = Arbiter->QueryArbitrate(Arbiter, &Parameters->Parameters.QueryArbitrate);
            break;
        case ArbiterActionAddReserved:
            Status = Arbiter->AddReserved(Arbiter, &Parameters->Parameters.AddReserved);
            break;
        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    KeSetEvent(Arbiter->MutexEvent, IO_NO_INCREMENT, FALSE);
    return Status;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbDeleteArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (Arbiter->Allocation)
    {
        RtlFreeRangeList(Arbiter->Allocation);
        ExFreePoolWithTag(Arbiter->Allocation, TAG_ARB_RANGE);
        Arbiter->Allocation = NULL;
    }
    if (Arbiter->PossibleAllocation)
    {
        RtlFreeRangeList(Arbiter->PossibleAllocation);
        ExFreePoolWithTag(Arbiter->PossibleAllocation, TAG_ARB_RANGE);
        Arbiter->PossibleAllocation = NULL;
    }
    if (Arbiter->AllocationStack)
    {
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        Arbiter->AllocationStack = NULL;
        Arbiter->AllocationStackMaxSize = 0;
    }
    if (Arbiter->MutexEvent)
    {
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        Arbiter->MutexEvent = NULL;
    }
    ArbFreeOrderingList(&Arbiter->OrderingList);
    ArbFreeOrderingList(&Arbiter->ReservedList);
    Arbiter->Signature = 0;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PDEVICE_OBJECT BusDeviceObject,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _In_ PCWSTR ArbiterName,
    _In_ PCWSTR OrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    NTSTATUS Status;

    PAGED_CODE();

    DPRINT("ArbInitializeArbiterInstance: '%S'\n", ArbiterName);

    ASSERT(Arbiter->UnpackRequirement != NULL);
    ASSERT(Arbiter->PackResource != NULL);
    ASSERT(Arbiter->UnpackResource != NULL);
    ASSERT(Arbiter->MutexEvent == NULL);
    ASSERT(Arbiter->Allocation == NULL);
    ASSERT(Arbiter->PossibleAllocation == NULL);
    ASSERT(Arbiter->AllocationStack == NULL);

    Arbiter->Signature = ARBITER_SIGNATURE;
    Arbiter->BusDeviceObject = BusDeviceObject;
    Arbiter->OrderingList.Orderings = NULL;
    Arbiter->ReservedList.Orderings = NULL;

    Arbiter->MutexEvent = ExAllocatePoolWithTag(NonPagedPool, sizeof(KEVENT), TAG_ARBITER);
    Arbiter->AllocationStack = ExAllocatePoolWithTag(PagedPool, PAGE_SIZE, TAG_ARB_ALLOCATION);
    Arbiter->Allocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    Arbiter->PossibleAllocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->MutexEvent || !Arbiter->AllocationStack || !Arbiter->Allocation || !Arbiter->PossibleAllocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: '%S' STATUS_INSUFFICIENT_RESOURCES\n", ArbiterName);
        if (Arbiter->Allocation)
            RtlInitializeRangeList(Arbiter->Allocation);
        if (Arbiter->PossibleAllocation)
            RtlInitializeRangeList(Arbiter->PossibleAllocation);
        ArbDeleteArbiterInstance(Arbiter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(Arbiter->MutexEvent, SynchronizationEvent, TRUE);
    Arbiter->AllocationStackMaxSize = PAGE_SIZE;
    RtlInitializeRangeList(Arbiter->Allocation);
    RtlInitializeRangeList(Arbiter->PossibleAllocation);

    Arbiter->Name = ArbiterName;
    Arbiter->OrderingName = OrderName;
    Arbiter->ResourceType = ResourceType;
    Arbiter->TransactionInProgress = FALSE;

    if (!Arbiter->TestAllocation)
        Arbiter->TestAllocation = ArbTestAllocation;
    if (!Arbiter->RetestAllocation)
        Arbiter->RetestAllocation = ArbRetestAllocation;
    if (!Arbiter->CommitAllocation)
        Arbiter->CommitAllocation = ArbCommitAllocation;
    if (!Arbiter->RollbackAllocation)
        Arbiter->RollbackAllocation = ArbRollbackAllocation;
    if (!Arbiter->BootAllocation)
        Arbiter->BootAllocation = ArbBootAllocation;
    if (!Arbiter->QueryArbitrate)
        Arbiter->QueryArbitrate = ArbQueryArbitrate;
    if (!Arbiter->QueryConflict)
        Arbiter->QueryConflict = ArbQueryConflict;
    if (!Arbiter->AddReserved)
        Arbiter->AddReserved = ArbAddReserved;
    if (!Arbiter->StartArbiter)
        Arbiter->StartArbiter = ArbStartArbiter;
    if (!Arbiter->PreprocessEntry)
        Arbiter->PreprocessEntry = ArbPreprocessEntry;
    if (!Arbiter->AllocateEntry)
        Arbiter->AllocateEntry = ArbAllocateEntry;
    if (!Arbiter->GetNextAllocationRange)
        Arbiter->GetNextAllocationRange = ArbGetNextAllocationRange;
    if (!Arbiter->FindSuitableRange)
        Arbiter->FindSuitableRange = ArbFindSuitableRange;
    if (!Arbiter->AddAllocation)
        Arbiter->AddAllocation = ArbAddAllocation;
    if (!Arbiter->BacktrackAllocation)
        Arbiter->BacktrackAllocation = ArbBacktrackAllocation;
    if (!Arbiter->OverrideConflict)
        Arbiter->OverrideConflict = ArbOverrideConflict;
    if (!Arbiter->InitializeRangeList)
        Arbiter->InitializeRangeList = ArbInitializeRangeList;
    if (!Arbiter->DeleteOwnerRanges)
        Arbiter->DeleteOwnerRanges = ArbDeleteOwnerRanges;

    Status = ArbBuildAssignmentOrdering(Arbiter, OrderName, OrderName, TranslateOrderingFunction);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ArbInitializeArbiterInstance: '%S' ordering failed 0x%lx\n", ArbiterName, Status);
        ArbDeleteArbiterInstance(Arbiter);
        return Status;
    }

    return STATUS_SUCCESS;
}
