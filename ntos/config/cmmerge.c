/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Copying one tree of keys onto another, and cloning one in place
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

typedef struct _CM_MERGE_ITEM
{
    HCELL_INDEX Source;
    HCELL_INDEX Destination;
} CM_MERGE_ITEM, *PCM_MERGE_ITEM;

typedef struct _CM_MERGE_STACK
{
    ULONG Count;
    ULONG Size;
    PCM_MERGE_ITEM Items;
} CM_MERGE_STACK, *PCM_MERGE_STACK;

#define CMP_MERGE_STACK_GROW 64
#define CMP_MERGE_NAME_MAX   512

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
CmpMergePush(
    _Inout_ PCM_MERGE_STACK Stack,
    _In_ HCELL_INDEX Source,
    _In_ HCELL_INDEX Destination)
{
    PCM_MERGE_ITEM Items;
    ULONG Size;

    if (Stack->Count == Stack->Size)
    {
        Size = Stack->Size + CMP_MERGE_STACK_GROW;

        Items = ExAllocatePoolZero(PagedPool,
                                   Size * sizeof(CM_MERGE_ITEM),
                                   TAG_REGISTRY_STACK);
        if (Items == NULL) return FALSE;

        if (Stack->Items != NULL)
        {
            RtlCopyMemory(Items,
                          Stack->Items,
                          Stack->Count * sizeof(CM_MERGE_ITEM));
            ExFreePool(Stack->Items);
        }

        Stack->Items = Items;
        Stack->Size = Size;
    }

    Stack->Items[Stack->Count].Source = Source;
    Stack->Items[Stack->Count].Destination = Destination;
    Stack->Count++;
    return TRUE;
}

static
BOOLEAN
CmpMergePop(
    _Inout_ PCM_MERGE_STACK Stack,
    _Out_ PCM_MERGE_ITEM Item)
{
    if (Stack->Count == 0) return FALSE;

    Stack->Count--;
    *Item = Stack->Items[Stack->Count];
    return TRUE;
}

static
VOID
CmpMergeStackFree(
    _Inout_ PCM_MERGE_STACK Stack)
{
    if (Stack->Items != NULL) ExFreePool(Stack->Items);

    Stack->Items = NULL;
    Stack->Count = 0;
    Stack->Size = 0;
}

/* The name of one key or value as characters a lookup can be made with */
static
BOOLEAN
CmpNodeNameToString(
    _In_ PCM_KEY_NODE Node,
    _Out_writes_(CMP_MERGE_NAME_MAX) PWCHAR Buffer,
    _Out_ PUNICODE_STRING Name)
{
    USHORT Count;

    Name->Buffer = Buffer;
    Name->Length = 0;
    Name->MaximumLength = CMP_MERGE_NAME_MAX * sizeof(WCHAR);

    if (Node->Flags & KEY_COMP_NAME)
    {
        Count = Node->NameLength;
        if (Count > CMP_MERGE_NAME_MAX) return FALSE;

        CmpCopyCompressedName(Buffer,
                              (ULONG)(Count * sizeof(WCHAR)),
                              Node->Name,
                              Node->NameLength);
    }
    else
    {
        Count = Node->NameLength / sizeof(WCHAR);
        if (Count > CMP_MERGE_NAME_MAX) return FALSE;

        RtlCopyMemory(Buffer, Node->Name, Node->NameLength);
    }

    Name->Length = (USHORT)(Count * sizeof(WCHAR));
    return TRUE;
}

static
BOOLEAN
CmpValueNameToString(
    _In_ PCM_KEY_VALUE Value,
    _Out_writes_(CMP_MERGE_NAME_MAX) PWCHAR Buffer,
    _Out_ PUNICODE_STRING Name)
{
    USHORT Count;

    Name->Buffer = Buffer;
    Name->Length = 0;
    Name->MaximumLength = CMP_MERGE_NAME_MAX * sizeof(WCHAR);

    if (Value->Flags & VALUE_COMP_NAME)
    {
        Count = Value->NameLength;
        if (Count > CMP_MERGE_NAME_MAX) return FALSE;

        CmpCopyCompressedName(Buffer,
                              (ULONG)(Count * sizeof(WCHAR)),
                              Value->Name,
                              Value->NameLength);
    }
    else
    {
        Count = Value->NameLength / sizeof(WCHAR);
        if (Count > CMP_MERGE_NAME_MAX) return FALSE;

        RtlCopyMemory(Buffer, Value->Name, Value->NameLength);
    }

    Name->Length = (USHORT)(Count * sizeof(WCHAR));
    return TRUE;
}

/*
 * Writes one value of the source key onto the destination key, replacing
 * whatever the destination said under that name.
 */
static
NTSTATUS
CmpOverlayOneValue(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SourceValueCell,
    _In_ PHHIVE DestinationHive,
    _In_ HCELL_INDEX DestinationKeyCell,
    _In_ HSTORAGE_TYPE StorageType)
{
    HCELL_INDEX DataToRelease = HCELL_NIL;
    UNICODE_STRING Name;
    WCHAR Buffer[CMP_MERGE_NAME_MAX];
    PCM_KEY_VALUE SourceValue;
    PCM_KEY_VALUE TargetValue;
    PCM_KEY_NODE TargetNode;
    HCELL_INDEX Existing;
    BOOLEAN Allocated = FALSE;
    PVOID Data = NULL;
    NTSTATUS Status;
    ULONG SmallData = 0;
    ULONG Length = 0;
    ULONG Type;

    SourceValue = (PCM_KEY_VALUE)HvGetCell(SourceHive, SourceValueCell);
    if (SourceValue == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (!CmpValueNameToString(SourceValue, Buffer, &Name))
    {
        HvReleaseCell(SourceHive, SourceValueCell);
        return STATUS_INVALID_PARAMETER;
    }

    Type = SourceValue->Type;

    if (!CmpReadValueData(SourceHive,
                          SourceValue,
                          &Length,
                          &Data,
                          &Allocated,
                          &DataToRelease))
    {
        HvReleaseCell(SourceHive, SourceValueCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Length != 0 && Length <= CM_KEY_VALUE_SMALL && Data != NULL)
    {
        RtlCopyMemory(&SmallData, Data, Length);
    }

    TargetNode = (PCM_KEY_NODE)HvGetCell(DestinationHive, DestinationKeyCell);
    if (TargetNode == NULL)
    {
        if (Allocated) ExFreePool(Data);
        if (DataToRelease != HCELL_NIL)
        {
            HvReleaseCell(SourceHive, DataToRelease);
        }

        HvReleaseCell(SourceHive, SourceValueCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!HvMarkCellDirty(DestinationHive, DestinationKeyCell, FALSE))
    {
        HvReleaseCell(DestinationHive, DestinationKeyCell);

        if (Allocated) ExFreePool(Data);
        if (DataToRelease != HCELL_NIL)
        {
            HvReleaseCell(SourceHive, DataToRelease);
        }

        HvReleaseCell(SourceHive, SourceValueCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Existing = CmpFindValueByName(DestinationHive, TargetNode, &Name);

    if (Existing != HCELL_NIL)
    {
        TargetValue = (PCM_KEY_VALUE)HvGetCell(DestinationHive, Existing);

        if (TargetValue == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            Status = CmpSetValueKeyExisting(DestinationHive,
                                            Existing,
                                            TargetValue,
                                            Type,
                                            Data,
                                            Length,
                                            StorageType,
                                            SmallData);
            HvReleaseCell(DestinationHive, Existing);
        }
    }
    else
    {
        Status = CmpSetValueKeyNew(DestinationHive,
                                   TargetNode,
                                   &Name,
                                   TargetNode->ValueList.Count,
                                   Type,
                                   Data,
                                   Length,
                                   StorageType,
                                   SmallData);
    }

    if (NT_SUCCESS(Status))
    {
        if (Name.Length > TargetNode->MaxValueNameLen)
        {
            TargetNode->MaxValueNameLen = Name.Length;
        }

        if (Length > TargetNode->MaxValueDataLen)
        {
            TargetNode->MaxValueDataLen = Length;
        }
    }

    HvReleaseCell(DestinationHive, DestinationKeyCell);

    if (Allocated) ExFreePool(Data);
    if (DataToRelease != HCELL_NIL) HvReleaseCell(SourceHive, DataToRelease);

    HvReleaseCell(SourceHive, SourceValueCell);
    return Status;
}

static
NTSTATUS
CmpOverlayValues(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SourceKeyCell,
    _In_ PHHIVE DestinationHive,
    _In_ HCELL_INDEX DestinationKeyCell,
    _In_ HSTORAGE_TYPE StorageType)
{
    PCM_KEY_NODE SourceNode;
    PCELL_DATA SourceList;
    HCELL_INDEX ListCell;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Count;
    ULONG i;

    SourceNode = (PCM_KEY_NODE)HvGetCell(SourceHive, SourceKeyCell);
    if (SourceNode == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Count = SourceNode->ValueList.Count;
    ListCell = SourceNode->ValueList.List;
    HvReleaseCell(SourceHive, SourceKeyCell);

    if (Count == 0 || ListCell == HCELL_NIL) return STATUS_SUCCESS;

    SourceList = (PCELL_DATA)HvGetCell(SourceHive, ListCell);
    if (SourceList == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < Count; i++)
    {
        HCELL_INDEX ValueCell = SourceList->u.KeyList[i];

        if (ValueCell == HCELL_NIL) continue;

        Status = CmpOverlayOneValue(SourceHive,
                                    ValueCell,
                                    DestinationHive,
                                    DestinationKeyCell,
                                    StorageType);
        if (!NT_SUCCESS(Status)) break;
    }

    HvReleaseCell(SourceHive, ListCell);
    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Lays one tree of keys over another. Where both say something about the same
 * name, the source wins; where only the destination says something, it is
 * left alone; and what only the source has is copied in whole.
 */
NTSTATUS
NTAPI
CmpOverlayKeyTree(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SourceCell,
    _In_ PHHIVE DestinationHive,
    _In_ HCELL_INDEX DestinationCell,
    _In_ HSTORAGE_TYPE StorageType)
{
    CM_MERGE_STACK Stack = {0, 0, NULL};
    UNICODE_STRING Name;
    WCHAR Buffer[CMP_MERGE_NAME_MAX];
    PCM_KEY_NODE SourceNode;
    PCM_KEY_NODE ChildNode;
    PCM_KEY_NODE TargetNode;
    HCELL_INDEX SourceChild;
    HCELL_INDEX TargetChild;
    HCELL_INDEX CopiedChild;
    CM_MERGE_ITEM Item;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Children;
    ULONG i;

    if (!CmpMergePush(&Stack, SourceCell, DestinationCell))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while (CmpMergePop(&Stack, &Item))
    {
        Status = CmpOverlayValues(SourceHive,
                                  Item.Source,
                                  DestinationHive,
                                  Item.Destination,
                                  StorageType);
        if (!NT_SUCCESS(Status)) break;

        SourceNode = (PCM_KEY_NODE)HvGetCell(SourceHive, Item.Source);
        if (SourceNode == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Children = SourceNode->SubKeyCounts[Stable] +
                   SourceNode->SubKeyCounts[Volatile];

        for (i = 0; i < Children; i++)
        {
            SourceChild = CmpFindSubKeyByNumber(SourceHive, SourceNode, i);
            if (SourceChild == HCELL_NIL) continue;

            ChildNode = (PCM_KEY_NODE)HvGetCell(SourceHive, SourceChild);
            if (ChildNode == NULL) continue;

            if (!CmpNodeNameToString(ChildNode, Buffer, &Name))
            {
                HvReleaseCell(SourceHive, SourceChild);
                continue;
            }

            HvReleaseCell(SourceHive, SourceChild);

            TargetNode = (PCM_KEY_NODE)HvGetCell(DestinationHive,
                                                 Item.Destination);
            if (TargetNode == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            TargetChild = CmpFindSubKeyByName(DestinationHive,
                                              TargetNode,
                                              &Name);
            HvReleaseCell(DestinationHive, Item.Destination);

            if (TargetChild != HCELL_NIL)
            {
                if (!CmpMergePush(&Stack, SourceChild, TargetChild))
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                continue;
            }

            Status = CmpDeepCopyKey(SourceHive,
                                    SourceChild,
                                    DestinationHive,
                                    StorageType,
                                    &CopiedChild);
            if (!NT_SUCCESS(Status)) break;

            if (!CmpAddSubKey(DestinationHive,
                              Item.Destination,
                              CopiedChild))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
        }

        HvReleaseCell(SourceHive, Item.Source);

        if (!NT_SUCCESS(Status)) break;
    }

    CmpMergeStackFree(&Stack);
    return Status;
}

/* Makes one empty key of a chosen name under a parent, at cell level */
static
NTSTATUS
CmpCreateNamedKeyCell(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX ParentCell,
    _In_ PCUNICODE_STRING Name,
    _In_ HSTORAGE_TYPE StorageType,
    _Out_ PHCELL_INDEX NewCell)
{
    PCM_KEY_NODE ParentNode;
    PCM_KEY_NODE Node;
    HCELL_INDEX Cell;
    USHORT NameSize;

    *NewCell = HCELL_NIL;

    NameSize = CmpNameSize(Hive, Name);

    Cell = HvAllocateCell(Hive,
                          FIELD_OFFSET(CM_KEY_NODE, Name) + NameSize,
                          StorageType,
                          ParentCell);
    if (Cell == HCELL_NIL) return STATUS_INSUFFICIENT_RESOURCES;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (Node == NULL)
    {
        HvFreeCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Node, FIELD_OFFSET(CM_KEY_NODE, Name));

    Node->Signature = CM_KEY_NODE_SIGNATURE;
    Node->Parent = ParentCell;
    Node->Security = HCELL_NIL;
    Node->Class = HCELL_NIL;
    Node->SubKeyLists[Stable] = HCELL_NIL;
    Node->SubKeyLists[Volatile] = HCELL_NIL;
    Node->ValueList.List = HCELL_NIL;
    KeQuerySystemTime(&Node->LastWriteTime);

    Node->NameLength = CmpCopyName(Hive, Node->Name, Name);
    if (Node->NameLength < Name->Length) Node->Flags |= KEY_COMP_NAME;

    HvMarkCellDirty(Hive, Cell, FALSE);
    HvReleaseCell(Hive, Cell);

    if (!CmpAddSubKey(Hive, ParentCell, Cell))
    {
        HvFreeCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ParentNode = (PCM_KEY_NODE)HvGetCell(Hive, ParentCell);

    if (ParentNode != NULL)
    {
        if (NameSize > ParentNode->MaxNameLen)
        {
            ParentNode->MaxNameLen = NameSize;
        }

        KeQuerySystemTime(&ParentNode->LastWriteTime);
        HvMarkCellDirty(Hive, ParentCell, FALSE);
        HvReleaseCell(Hive, ParentCell);
    }

    *NewCell = Cell;
    return STATUS_SUCCESS;
}

/* Carries the class and the access of one key onto a copy of it */
static
VOID
CmpCopyKeyAttributes(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX SourceCell,
    _In_ HCELL_INDEX TargetCell,
    _In_ HSTORAGE_TYPE StorageType)
{
    HCELL_INDEX SecurityAnchor = HCELL_NIL;
    HCELL_INDEX SecurityCell;
    PCM_KEY_NODE Source;
    PCM_KEY_NODE Target;
    PCELL_DATA SourceClass;
    PCELL_DATA TargetClass;
    HCELL_INDEX ClassCell;
    ULONG ClassLength;

    Source = (PCM_KEY_NODE)HvGetCell(Hive, SourceCell);
    if (Source == NULL) return;

    ClassLength = Source->ClassLength;
    ClassCell = Source->Class;
    HvReleaseCell(Hive, SourceCell);

    if (ClassLength != 0 && ClassCell != HCELL_NIL)
    {
        HCELL_INDEX Copy = HvAllocateCell(Hive,
                                          ClassLength,
                                          StorageType,
                                          TargetCell);

        if (Copy != HCELL_NIL)
        {
            SourceClass = (PCELL_DATA)HvGetCell(Hive, ClassCell);
            TargetClass = (PCELL_DATA)HvGetCell(Hive, Copy);

            if (SourceClass != NULL && TargetClass != NULL)
            {
                RtlCopyMemory(TargetClass->u.KeyString,
                              SourceClass->u.KeyString,
                              ClassLength);

                Target = (PCM_KEY_NODE)HvGetCell(Hive, TargetCell);

                if (Target != NULL)
                {
                    Target->Class = Copy;
                    Target->ClassLength = ClassLength;
                    HvMarkCellDirty(Hive, TargetCell, FALSE);
                    HvReleaseCell(Hive, TargetCell);
                }
            }

            if (TargetClass != NULL) HvReleaseCell(Hive, Copy);
            if (SourceClass != NULL) HvReleaseCell(Hive, ClassCell);
        }
    }

    if (!NT_SUCCESS(CmpCopyKeySecurity(Hive,
                                       SourceCell,
                                       Hive,
                                       StorageType,
                                       &SecurityAnchor,
                                       &SecurityCell)))
    {
        return;
    }

    if (SecurityCell == HCELL_NIL) return;

    Target = (PCM_KEY_NODE)HvGetCell(Hive, TargetCell);
    if (Target == NULL) return;

    Target->Security = SecurityCell;
    HvMarkCellDirty(Hive, TargetCell, FALSE);
    HvReleaseCell(Hive, TargetCell);
}

/*
 * Makes a second copy of a whole tree inside the same hive, under a parent
 * and a name the caller chooses. This is how a control set that has proved
 * itself becomes the one to fall back on.
 */
NTSTATUS
NTAPI
CmpCloneKeyTree(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX SourceCell,
    _In_ HCELL_INDEX ParentCell,
    _In_ PCUNICODE_STRING NewName,
    _Out_ PHCELL_INDEX NewCell)
{
    HSTORAGE_TYPE StorageType;
    PCM_KEY_NODE ParentNode;
    HCELL_INDEX Existing;
    HCELL_INDEX Clone;
    NTSTATUS Status;

    *NewCell = HCELL_NIL;

    StorageType = HvGetCellType(ParentCell);

    ParentNode = (PCM_KEY_NODE)HvGetCell(Hive, ParentCell);
    if (ParentNode == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Existing = CmpFindSubKeyByName(Hive, ParentNode, NewName);
    HvReleaseCell(Hive, ParentCell);

    if (Existing != HCELL_NIL)
    {
        Clone = Existing;
    }
    else
    {
        Status = CmpCreateNamedKeyCell(Hive,
                                       ParentCell,
                                       NewName,
                                       StorageType,
                                       &Clone);
        if (!NT_SUCCESS(Status)) return Status;

        CmpCopyKeyAttributes(Hive, SourceCell, Clone, StorageType);
    }

    Status = CmpOverlayKeyTree(Hive, SourceCell, Hive, Clone, StorageType);
    if (!NT_SUCCESS(Status)) return Status;

    *NewCell = Clone;
    return STATUS_SUCCESS;
}
