/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What can be asked of a key and what can be done to one
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE FUNCTIONS **********************************************************/

static
USHORT
CmpKeyNodeNameSize(
    _In_ PCM_KEY_NODE Node)
{
    if (Node->Flags & KEY_COMP_NAME)
    {
        return (USHORT)(Node->NameLength * sizeof(WCHAR));
    }

    return Node->NameLength;
}

static
VOID
CmpCopyKeyNodeName(
    _In_ PCM_KEY_NODE Node,
    _Out_writes_bytes_(Size) PWCHAR Buffer,
    _In_ USHORT Size)
{
    if (Node->Flags & KEY_COMP_NAME)
    {
        CmpCopyCompressedName(Buffer, Size, Node->Name, Node->NameLength);
        return;
    }

    RtlCopyMemory(Buffer, Node->Name, Size);
}

/* Copies the class name of a key out of its own cell */
static
ULONG
CmpCopyKeyClass(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Node,
    _Out_writes_bytes_(Size) PWCHAR Buffer,
    _In_ ULONG Size)
{
    PCELL_DATA Class;
    ULONG Copied;

    if (Node->Class == HCELL_NIL || Node->ClassLength == 0) return 0;

    Class = (PCELL_DATA)HvGetCell(Hive, Node->Class);
    if (Class == NULL) return 0;

    Copied = min(Size, Node->ClassLength);
    RtlCopyMemory(Buffer, Class->u.KeyString, Copied);

    HvReleaseCell(Hive, Node->Class);
    return Copied;
}

static
NTSTATUS
CmpKeyIsUsable(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    if (Kcb->Delete) return STATUS_KEY_DELETED;
    if (Kcb->ExtFlags & CM_KCB_KEY_NON_EXIST) return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/* Stamps a key as just written and lets its cached copy know */
VOID
NTAPI
CmpTouchKey(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell)
{
    PCM_KEY_NODE Node;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (Node == NULL) return;

    KeQuerySystemTime(&Node->LastWriteTime);
    HvMarkCellDirty(Hive, Cell, FALSE);
    HvReleaseCell(Hive, Cell);
}

NTSTATUS
NTAPI
CmpQueryKeyData(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Node,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG NameSize;
    ULONG HeaderSize;
    ULONG Required;
    ULONG Copied;

    NameSize = CmpKeyNodeNameSize(Node);
    *ResultLength = 0;

    switch (KeyInformationClass)
    {
        case KeyBasicInformation:
        {
            PKEY_BASIC_INFORMATION Answer = KeyInformation;

            HeaderSize = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name);
            Required = HeaderSize + NameSize;
            *ResultLength = Required;

            if (Length < HeaderSize) return STATUS_BUFFER_TOO_SMALL;

            Answer->LastWriteTime = Node->LastWriteTime;
            Answer->TitleIndex = 0;
            Answer->NameLength = NameSize;

            Copied = min(NameSize, Length - HeaderSize);
            if (Copied != 0) CmpCopyKeyNodeName(Node, Answer->Name, (USHORT)Copied);

            if (Length < Required) Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        case KeyNodeInformation:
        {
            PKEY_NODE_INFORMATION Answer = KeyInformation;
            ULONG ClassOffset;

            HeaderSize = FIELD_OFFSET(KEY_NODE_INFORMATION, Name);
            ClassOffset = ALIGN_UP_BY(HeaderSize + NameSize, sizeof(ULONG));
            Required = ClassOffset + Node->ClassLength;
            *ResultLength = Required;

            if (Length < HeaderSize) return STATUS_BUFFER_TOO_SMALL;

            Answer->LastWriteTime = Node->LastWriteTime;
            Answer->TitleIndex = 0;
            Answer->ClassOffset = ClassOffset;
            Answer->ClassLength = Node->ClassLength;
            Answer->NameLength = NameSize;

            Copied = min(NameSize, Length - HeaderSize);
            if (Copied != 0) CmpCopyKeyNodeName(Node, Answer->Name, (USHORT)Copied);

            if (Length < Required) Status = STATUS_BUFFER_OVERFLOW;

            if (Length > ClassOffset)
            {
                CmpCopyKeyClass(Hive,
                                Node,
                                (PWCHAR)((PUCHAR)KeyInformation + ClassOffset),
                                Length - ClassOffset);
            }

            break;
        }

        case KeyFullInformation:
        {
            PKEY_FULL_INFORMATION Answer = KeyInformation;
            ULONG ClassOffset;

            HeaderSize = FIELD_OFFSET(KEY_FULL_INFORMATION, Class);
            ClassOffset = HeaderSize;
            Required = ClassOffset + Node->ClassLength;
            *ResultLength = Required;

            if (Length < HeaderSize) return STATUS_BUFFER_TOO_SMALL;

            Answer->LastWriteTime = Node->LastWriteTime;
            Answer->TitleIndex = 0;
            Answer->ClassOffset = ClassOffset;
            Answer->ClassLength = Node->ClassLength;
            Answer->SubKeys = Node->SubKeyCounts[Stable] +
                              Node->SubKeyCounts[Volatile];
            Answer->MaxNameLen = Node->MaxNameLen;
            Answer->MaxClassLen = Node->MaxClassLen;
            Answer->Values = Node->ValueList.Count;
            Answer->MaxValueNameLen = Node->MaxValueNameLen;
            Answer->MaxValueDataLen = Node->MaxValueDataLen;

            if (Length < Required) Status = STATUS_BUFFER_OVERFLOW;

            if (Length > ClassOffset)
            {
                CmpCopyKeyClass(Hive,
                                Node,
                                Answer->Class,
                                Length - ClassOffset);
            }

            break;
        }

        default:
            return STATUS_INVALID_PARAMETER;
    }

    return Status;
}

NTSTATUS
NTAPI
CmQueryKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PVOID Transaction,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    PHHIVE Hive = Kcb->KeyHive;
    PUNICODE_STRING FullName;
    PCM_KEY_NODE Node;
    NTSTATUS Status;
    ULONG HeaderSize;
    ULONG Required;
    ULONG Copied;

    UNREFERENCED_PARAMETER(Transaction);

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    switch (KeyInformationClass)
    {
        case KeyNameInformation:
        {
            PKEY_NAME_INFORMATION Answer = KeyInformation;

            FullName = CmpConstructName(Kcb);
            if (FullName == NULL) return STATUS_INSUFFICIENT_RESOURCES;

            HeaderSize = FIELD_OFFSET(KEY_NAME_INFORMATION, Name);
            Required = HeaderSize + FullName->Length;
            *ResultLength = Required;

            if (Length < HeaderSize)
            {
                ExFreePool(FullName);
                return STATUS_BUFFER_TOO_SMALL;
            }

            Answer->NameLength = FullName->Length;
            Copied = min(FullName->Length, Length - HeaderSize);
            RtlCopyMemory(Answer->Name, FullName->Buffer, Copied);

            Status = (Length < Required) ? STATUS_BUFFER_OVERFLOW
                                         : STATUS_SUCCESS;
            ExFreePool(FullName);
            return Status;
        }

        case KeyCachedInformation:
        {
            PKEY_CACHED_INFORMATION Answer = KeyInformation;

            Required = sizeof(KEY_CACHED_INFORMATION);
            *ResultLength = Required;
            if (Length < Required) return STATUS_BUFFER_TOO_SMALL;

            Node = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
            if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

            Answer->LastWriteTime = Node->LastWriteTime;
            Answer->TitleIndex = 0;
            Answer->SubKeys = Node->SubKeyCounts[Stable] +
                              Node->SubKeyCounts[Volatile];
            Answer->MaxNameLen = Node->MaxNameLen;
            Answer->Values = Node->ValueList.Count;
            Answer->MaxValueNameLen = Node->MaxValueNameLen;
            Answer->MaxValueDataLen = Node->MaxValueDataLen;
            Answer->NameLength = CmpKeyNodeNameSize(Node);

            HvReleaseCell(Hive, Kcb->KeyCell);
            return STATUS_SUCCESS;
        }

        case KeyFlagsInformation:
        {
            PKEY_CONTROL_FLAGS_INFORMATION Answer = KeyInformation;

            Required = sizeof(KEY_CONTROL_FLAGS_INFORMATION);
            *ResultLength = Required;
            if (Length < Required) return STATUS_BUFFER_TOO_SMALL;

            Answer->ControlFlags = Kcb->KcbUserFlags;
            return STATUS_SUCCESS;
        }

        case KeyVirtualizationInformation:
        {
            PKEY_VIRTUALIZATION_INFORMATION Answer = KeyInformation;

            Required = sizeof(KEY_VIRTUALIZATION_INFORMATION);
            *ResultLength = Required;
            if (Length < Required) return STATUS_BUFFER_TOO_SMALL;

            RtlZeroMemory(Answer, Required);
            Answer->VirtualTarget = (Kcb->Flags & KEY_VIRT_TARGET) ? 1 : 0;
            Answer->VirtualStore = (Kcb->Flags & KEY_VIRTUAL_STORE) ? 1 : 0;
            Answer->VirtualSource = (Kcb->Flags & KEY_VIRT_MIRRORED) ? 1 : 0;
            return STATUS_SUCCESS;
        }

        case KeyHandleTagsInformation:
        {
            PKEY_HANDLE_TAGS_INFORMATION Answer = KeyInformation;

            Required = sizeof(KEY_HANDLE_TAGS_INFORMATION);
            *ResultLength = Required;
            if (Length < Required) return STATUS_BUFFER_TOO_SMALL;

            Answer->HandleTags = Kcb->KcbVirtControlFlags;
            return STATUS_SUCCESS;
        }

        case KeyBasicInformation:
        case KeyNodeInformation:
        case KeyFullInformation:
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = CmpQueryKeyData(Hive,
                             Node,
                             KeyInformationClass,
                             KeyInformation,
                             Length,
                             ResultLength);

    HvReleaseCell(Hive, Kcb->KeyCell);
    return Status;
}

NTSTATUS
NTAPI
CmEnumerateKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_opt_ PVOID Transaction,
    _In_ ULONG Index,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    PHHIVE Hive = Kcb->KeyHive;
    PCM_KEY_NODE Parent;
    PCM_KEY_NODE Child;
    HCELL_INDEX Cell;
    NTSTATUS Status;

    if (KeyInformationClass != KeyBasicInformation &&
        KeyInformationClass != KeyNodeInformation &&
        KeyInformationClass != KeyFullInformation)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    Parent = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (Parent == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Cell = CmpTransFindSubKeyByNumber(Hive, Parent, Index, Transaction);
    HvReleaseCell(Hive, Kcb->KeyCell);

    if (Cell == HCELL_NIL) return STATUS_NO_MORE_ENTRIES;

    Child = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (Child == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = CmpQueryKeyData(Hive,
                             Child,
                             KeyInformationClass,
                             KeyInformation,
                             Length,
                             ResultLength);

    HvReleaseCell(Hive, Cell);
    return Status;
}

NTSTATUS
NTAPI
CmQueryValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ UNICODE_STRING ValueName,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    HCELL_INDEX CellToRelease = HCELL_NIL;
    PCM_CACHED_VALUE *CachedValue;
    BOOLEAN ValueIsCached;
    PCM_KEY_VALUE Value;
    NTSTATUS Status;
    ULONG Index;

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (CmpFindValueByNameFromCache(Kcb,
                                    &ValueName,
                                    &CachedValue,
                                    &Index,
                                    &Value,
                                    &ValueIsCached,
                                    &CellToRelease) != SearchSuccess)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    CmpQueryKeyValueData(Kcb,
                         CachedValue,
                         Value,
                         ValueIsCached,
                         KeyValueInformationClass,
                         KeyValueInformation,
                         Length,
                         ResultLength,
                         &Status);

    if (CellToRelease != HCELL_NIL) HvReleaseCell(Kcb->KeyHive, CellToRelease);
    return Status;
}

NTSTATUS
NTAPI
CmEnumerateValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ ULONG Index,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    HCELL_INDEX ListToRelease = HCELL_NIL;
    HCELL_INDEX CellToRelease = HCELL_NIL;
    PCM_CACHED_VALUE *CachedValue;
    BOOLEAN ValueIsCached;
    BOOLEAN ListIsCached;
    PCM_KEY_VALUE Value;
    PCELL_DATA List;
    NTSTATUS Status;

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (Index >= Kcb->ValueCache.Count) return STATUS_NO_MORE_ENTRIES;

    if (CmpGetValueListFromCache(Kcb,
                                 &List,
                                 &ListIsCached,
                                 &ListToRelease) != SearchSuccess)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (CmpGetValueKeyFromCache(Kcb,
                                List,
                                Index,
                                &CachedValue,
                                &Value,
                                ListIsCached,
                                &ValueIsCached,
                                &CellToRelease) != SearchSuccess)
    {
        if (ListToRelease != HCELL_NIL) HvReleaseCell(Kcb->KeyHive, ListToRelease);
        return STATUS_NO_MORE_ENTRIES;
    }

    CmpQueryKeyValueData(Kcb,
                         CachedValue,
                         Value,
                         ValueIsCached,
                         KeyValueInformationClass,
                         KeyValueInformation,
                         Length,
                         ResultLength,
                         &Status);

    if (CellToRelease != HCELL_NIL) HvReleaseCell(Kcb->KeyHive, CellToRelease);
    if (ListToRelease != HCELL_NIL) HvReleaseCell(Kcb->KeyHive, ListToRelease);
    return Status;
}

/*
 * A value of four bytes or fewer lives inside its own cell instead of getting
 * a data cell of its own, which is what the special size bit marks.
 */
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
    _In_ ULONG SmallData)
{
    PCM_KEY_VALUE Value;
    HCELL_INDEX ValueCell;
    NTSTATUS Status;
    USHORT NameSize;

    NameSize = CmpNameSize(Hive, ValueName);

    ValueCell = HvAllocateCell(Hive,
                               FIELD_OFFSET(CM_KEY_VALUE, Name) + NameSize,
                               StorageType,
                               HCELL_NIL);
    if (ValueCell == HCELL_NIL) return STATUS_INSUFFICIENT_RESOURCES;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, ValueCell);
    if (Value == NULL)
    {
        HvFreeCell(Hive, ValueCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Value->Signature = CM_KEY_VALUE_SIGNATURE;
    Value->Type = Type;
    Value->Flags = 0;
    Value->Spare = 0;
    Value->NameLength = CmpCopyName(Hive, Value->Name, ValueName);
    if (Value->NameLength < ValueName->Length) Value->Flags |= VALUE_COMP_NAME;

    Value->DataLength = CM_KEY_VALUE_SPECIAL_SIZE;
    Value->Data = HCELL_NIL;
    HvReleaseCell(Hive, ValueCell);

    Status = CmpWriteValueData(Hive,
                               StorageType,
                               ValueCell,
                               Type,
                               Data,
                               DataSize,
                               SmallData);
    if (!NT_SUCCESS(Status))
    {
        HvFreeCell(Hive, ValueCell);
        return Status;
    }

    Status = CmpAddValueToList(Hive,
                               ValueCell,
                               Index,
                               StorageType,
                               &Parent->ValueList);
    if (!NT_SUCCESS(Status))
    {
        CmpFreeValue(Hive, ValueCell);
        return Status;
    }

    return STATUS_SUCCESS;
}

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
    _In_ ULONG TempData)
{
    if (!HvMarkCellDirty(Hive, OldChild, FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (CmpIsKeyValueBig(Hive, Value->DataLength))
    {
        if (!CmpMarkBigValueDataDirty(Hive, Value->Data))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else if (!CmpMarkValueDataDirty(Hive, Value))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    CmpReleaseValueData(Hive, OldChild);

    return CmpWriteValueData(Hive,
                             StorageType,
                             OldChild,
                             Type,
                             Data,
                             DataSize,
                             TempData);
}

NTSTATUS
NTAPI
CmSetValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize)
{
    HCELL_INDEX CellToRelease = HCELL_NIL;
    PHHIVE Hive = Kcb->KeyHive;
    PCM_CACHED_VALUE *CachedValue;
    HSTORAGE_TYPE StorageType;
    BOOLEAN ValueIsCached;
    BOOLEAN Changed = TRUE;
    PCM_KEY_VALUE Value;
    PCM_KEY_NODE Node;
    NTSTATUS Status;
    ULONG SmallData = 0;
    ULONG Index;

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (Hive->ReadOnly) return STATUS_ACCESS_DENIED;

    if (DataSize <= CM_KEY_VALUE_SMALL && DataSize != 0)
    {
        RtlCopyMemory(&SmallData, Data, DataSize);
    }

    if (CmpCompareNewValueDataAgainstKCBCache(Kcb,
                                              ValueName,
                                              Type,
                                              Data,
                                              DataSize) == SearchSuccess)
    {
        Changed = FALSE;
    }

    StorageType = HvGetCellType(Kcb->KeyCell);

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (!HvMarkCellDirty(Hive, Kcb->KeyCell, FALSE))
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = STATUS_SUCCESS;

    if (CmpFindValueByNameFromCache(Kcb,
                                    ValueName,
                                    &CachedValue,
                                    &Index,
                                    &Value,
                                    &ValueIsCached,
                                    &CellToRelease) == SearchSuccess)
    {
        Status = CmpSetValueKeyExisting(Hive,
                                        CellToRelease,
                                        Value,
                                        Type,
                                        Data,
                                        DataSize,
                                        StorageType,
                                        SmallData);

        if (CellToRelease != HCELL_NIL) HvReleaseCell(Hive, CellToRelease);
    }
    else
    {
        Index = Node->ValueList.Count;
        Status = CmpSetValueKeyNew(Hive,
                                   Node,
                                   ValueName,
                                   Index,
                                   Type,
                                   Data,
                                   DataSize,
                                   StorageType,
                                   SmallData);
    }

    if (!NT_SUCCESS(Status))
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return Status;
    }

    if (ValueName->Length > Node->MaxValueNameLen)
    {
        Node->MaxValueNameLen = ValueName->Length;
    }

    if (DataSize > Node->MaxValueDataLen) Node->MaxValueDataLen = DataSize;

    KeQuerySystemTime(&Node->LastWriteTime);

    CmpCleanUpKcbValueCache(Kcb);
    CmpUpdateKcbFromNode(Kcb, Node);

    HvReleaseCell(Hive, Kcb->KeyCell);

    if (Changed)
    {
        CmpReportNotify(Kcb, Hive, Kcb->KeyCell, REG_NOTIFY_CHANGE_LAST_SET);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmDeleteValueKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ UNICODE_STRING ValueName)
{
    HCELL_INDEX CellToRelease = HCELL_NIL;
    PHHIVE Hive = Kcb->KeyHive;
    PCM_CACHED_VALUE *CachedValue;
    BOOLEAN ValueIsCached;
    PCM_KEY_VALUE Value;
    PCM_KEY_NODE Node;
    NTSTATUS Status;
    ULONG Index;

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (Hive->ReadOnly) return STATUS_ACCESS_DENIED;

    if (CmpFindValueByNameFromCache(Kcb,
                                    &ValueName,
                                    &CachedValue,
                                    &Index,
                                    &Value,
                                    &ValueIsCached,
                                    &CellToRelease) != SearchSuccess)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (CellToRelease != HCELL_NIL) HvReleaseCell(Hive, CellToRelease);

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (!HvMarkCellDirty(Hive, Kcb->KeyCell, FALSE))
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = CmpRemoveValueFromList(Hive, Index, &Node->ValueList);
    if (!NT_SUCCESS(Status))
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return Status;
    }

    CmpReleaseValueData(Hive, CellToRelease);

    if (!CmpFreeValue(Hive, CellToRelease))
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeQuerySystemTime(&Node->LastWriteTime);

    CmpCleanUpKcbValueCache(Kcb);
    CmpUpdateKcbFromNode(Kcb, Node);

    HvReleaseCell(Hive, Kcb->KeyCell);

    CmpReportNotify(Kcb, Hive, Kcb->KeyCell, REG_NOTIFY_CHANGE_LAST_SET);
    return STATUS_SUCCESS;
}

/*
 * A key can only go once nothing lives under it. Every handle still open on
 * it keeps working but answers STATUS_KEY_DELETED from here on.
 */
NTSTATUS
NTAPI
CmDeleteKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    PHHIVE Hive = Kcb->KeyHive;
    PCM_KEY_CONTROL_BLOCK Parent;
    PCM_KEY_NODE Node;
    HCELL_INDEX Cell;
    NTSTATUS Status;

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (Hive->ReadOnly) return STATUS_ACCESS_DENIED;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Node->Flags & (KEY_NO_DELETE | KEY_HIVE_ENTRY | KEY_HIVE_EXIT))
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_CANNOT_DELETE;
    }

    if (Node->SubKeyCounts[Stable] + Node->SubKeyCounts[Volatile] != 0)
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_CANNOT_DELETE;
    }

    Cell = Kcb->KeyCell;
    Parent = Kcb->ParentKcb;

    if (!CmpMarkKeyDirty(Hive, Cell, TRUE))
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    HvReleaseCell(Hive, Cell);

    Status = CmpFreeKeyByCell(Hive, Cell, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Kcb->Delete = TRUE;
    CmpRemoveKeyControlBlock(Kcb);
    CmpCleanUpKcbValueCache(Kcb);

    if (Parent != NULL)
    {
        CmpCleanUpSubKeyInfo(Parent);
        CmpTouchKey(Parent->KeyHive, Parent->KeyCell);
        CmpReportNotify(Parent,
                        Parent->KeyHive,
                        Parent->KeyCell,
                        REG_NOTIFY_CHANGE_NAME);
    }

    CmpFlushNotifiesOnKeyBodyList(Kcb, TRUE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmFlushKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN EclusiveLock)
{
    PCMHIVE CmHive;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(EclusiveLock);

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (CmpNoWrite) return STATUS_SUCCESS;
    if (Kcb->KeyHive->HiveFlags & HIVE_VOLATILE) return STATUS_SUCCESS;

    CmHive = CONTAINING_RECORD(Kcb->KeyHive, CMHIVE, Hive);
    return CmpFlushHiveToDisk(CmHive, TRUE);
}

/*
 * The name of a key is written inside its own cell, so renaming it means
 * building a new cell and moving everything that pointed at the old one.
 */
NTSTATUS
NTAPI
CmRenameKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING NewName)
{
    PHHIVE Hive = Kcb->KeyHive;
    PCM_KEY_CONTROL_BLOCK Parent = Kcb->ParentKcb;
    PCM_KEY_NODE ParentNode;
    PCM_KEY_NODE OldNode;
    PCM_KEY_NODE NewNode;
    PCM_KEY_NODE ChildNode;
    HCELL_INDEX ChildCell;
    HCELL_INDEX NewCell;
    HSTORAGE_TYPE StorageType;
    PVOID RenameContext;
    NTSTATUS Status;
    USHORT NameSize;
    ULONG Children;
    ULONG i;

    Status = CmpKeyIsUsable(Kcb);
    if (!NT_SUCCESS(Status)) return Status;

    if (Hive->ReadOnly) return STATUS_ACCESS_DENIED;
    if (Parent == NULL) return STATUS_ACCESS_DENIED;
    if (NewName->Length == 0 || NewName->Length > CMP_KEY_NAME_MAX)
    {
        return STATUS_INVALID_PARAMETER;
    }

    OldNode = (PCM_KEY_NODE)HvGetCell(Hive, Kcb->KeyCell);
    if (OldNode == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (OldNode->Flags & (KEY_HIVE_ENTRY | KEY_HIVE_EXIT | KEY_NO_DELETE))
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_ACCESS_DENIED;
    }

    ParentNode = (PCM_KEY_NODE)HvGetCell(Hive, Parent->KeyCell);
    if (ParentNode == NULL)
    {
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (CmpFindSubKeyByName(Hive, ParentNode, NewName) != HCELL_NIL)
    {
        HvReleaseCell(Hive, Parent->KeyCell);
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    StorageType = HvGetCellType(Kcb->KeyCell);
    NameSize = CmpNameSize(Hive, NewName);

    NewCell = HvAllocateCell(Hive,
                             FIELD_OFFSET(CM_KEY_NODE, Name) + NameSize,
                             StorageType,
                             Kcb->KeyCell);
    if (NewCell == HCELL_NIL)
    {
        HvReleaseCell(Hive, Parent->KeyCell);
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewNode = (PCM_KEY_NODE)HvGetCell(Hive, NewCell);
    if (NewNode == NULL)
    {
        HvFreeCell(Hive, NewCell);
        HvReleaseCell(Hive, Parent->KeyCell);
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RenameContext = CmpPrepareKeyControlBlockRename(Kcb, NewName);
    if (RenameContext == NULL)
    {
        HvReleaseCell(Hive, NewCell);
        HvFreeCell(Hive, NewCell);
        HvReleaseCell(Hive, Parent->KeyCell);
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(NewNode, OldNode, FIELD_OFFSET(CM_KEY_NODE, Name));
    NewNode->Flags &= ~KEY_COMP_NAME;
    NewNode->NameLength = CmpCopyName(Hive, NewNode->Name, NewName);
    if (NewNode->NameLength < NewName->Length) NewNode->Flags |= KEY_COMP_NAME;

    Children = OldNode->SubKeyCounts[Stable] + OldNode->SubKeyCounts[Volatile];

    for (i = 0; i < Children; i++)
    {
        ChildCell = CmpFindSubKeyByNumber(Hive, NewNode, i);
        if (ChildCell == HCELL_NIL) continue;

        ChildNode = (PCM_KEY_NODE)HvGetCell(Hive, ChildCell);
        if (ChildNode == NULL) continue;

        if (HvMarkCellDirty(Hive, ChildCell, FALSE))
        {
            ChildNode->Parent = NewCell;
        }

        HvReleaseCell(Hive, ChildCell);
    }

    HvReleaseCell(Hive, NewCell);

    if (!CmpAddSubKey(Hive, Parent->KeyCell, NewCell) ||
        !CmpRemoveSubKey(Hive, Parent->KeyCell, Kcb->KeyCell))
    {
        CmpCancelKeyControlBlockRename(RenameContext);
        HvFreeCell(Hive, NewCell);
        HvReleaseCell(Hive, Parent->KeyCell);
        HvReleaseCell(Hive, Kcb->KeyCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (NameSize > ParentNode->MaxNameLen) ParentNode->MaxNameLen = NameSize;

    HvReleaseCell(Hive, Kcb->KeyCell);
    HvFreeCell(Hive, Kcb->KeyCell);

    CmpCommitKeyControlBlockRename(RenameContext, NewCell);

    KeQuerySystemTime(&ParentNode->LastWriteTime);
    HvMarkCellDirty(Hive, Parent->KeyCell, FALSE);
    HvReleaseCell(Hive, Parent->KeyCell);

    CmpCleanUpSubKeyInfo(Parent);
    CmpReportNotify(Parent,
                    Hive,
                    Parent->KeyCell,
                    REG_NOTIFY_CHANGE_NAME);

    return STATUS_SUCCESS;
}

/*
 * Counts the keys still open under one key, and on request lets go of what it
 * finds, which is how a forced hive unload clears the way for itself.
 */
ULONG
NTAPI
CmpEnumerateOpenSubKeys(
    _In_ PCM_KEY_CONTROL_BLOCK RootKcb,
    _In_ BOOLEAN RemoveEmptyCacheEntries,
    _In_ BOOLEAN DereferenceOpenedEntries)
{
    PCM_KEY_CONTROL_BLOCK Ancestor;
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_KEY_HASH Hash;
    PCM_KEY_HASH Next;
    ULONG Levels;
    ULONG Count = 0;
    ULONG i;

    CMP_ASSERT_EXCLUSIVE_REGISTRY_LOCK();

    for (Levels = CMP_SUBKEY_LEVELS_DEPTH_LIMIT; Levels > RootKcb->TotalLevels; Levels--)
    {
        for (i = 0; i < CmpHashTableSize; i++)
        {
            for (Hash = CmpCacheTable[i].Entry; Hash != NULL; Hash = Next)
            {
                Next = Hash->NextHash;
                Kcb = CONTAINING_RECORD(Hash, CM_KEY_CONTROL_BLOCK, KeyHash);

                if (Kcb->TotalLevels != Levels) continue;

                for (Ancestor = Kcb->ParentKcb;
                     Ancestor != NULL && Ancestor != RootKcb;
                     Ancestor = Ancestor->ParentKcb)
                {
                    NOTHING;
                }

                if (Ancestor != RootKcb) continue;

                if (Kcb->RefCount == 0)
                {
                    if (RemoveEmptyCacheEntries)
                    {
                        CmpRemoveFromDelayedClose(Kcb);
                        CmpCleanUpKcbCacheWithLock(Kcb, TRUE);
                    }

                    continue;
                }

                Count++;

                if (DereferenceOpenedEntries)
                {
                    CmpFlushNotifiesOnKeyBodyList(Kcb, TRUE);
                    Kcb->Delete = TRUE;
                    CmpRemoveKeyControlBlock(Kcb);
                }
            }
        }
    }

    return Count;
}
