/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing a hive file into the namespace and taking it back out
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

PKEVENT CmpLoadWorkerEvent = NULL;
LONG CmpLoadWorkerIncrement = 0;

/* PRIVATE DATA ***************************************************************/

typedef struct _CM_COPY_ITEM
{
    HCELL_INDEX Source;
    HCELL_INDEX DestinationParent;
} CM_COPY_ITEM, *PCM_COPY_ITEM;

typedef struct _CM_COPY_STACK
{
    ULONG Count;
    ULONG Size;
    PCM_COPY_ITEM Items;
} CM_COPY_STACK, *PCM_COPY_STACK;

#define CMP_COPY_STACK_GROW 64

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
CmpCopyStackPush(
    _Inout_ PCM_COPY_STACK Stack,
    _In_ HCELL_INDEX Source,
    _In_ HCELL_INDEX DestinationParent)
{
    PCM_COPY_ITEM Items;
    ULONG Size;

    if (Stack->Count == Stack->Size)
    {
        Size = Stack->Size + CMP_COPY_STACK_GROW;
        Items = ExAllocatePoolZero(PagedPool,
                                   Size * sizeof(CM_COPY_ITEM),
                                   TAG_REGISTRY_STACK);
        if (Items == NULL) return FALSE;

        if (Stack->Items != NULL)
        {
            RtlCopyMemory(Items,
                          Stack->Items,
                          Stack->Count * sizeof(CM_COPY_ITEM));
            ExFreePool(Stack->Items);
        }

        Stack->Items = Items;
        Stack->Size = Size;
    }

    Stack->Items[Stack->Count].Source = Source;
    Stack->Items[Stack->Count].DestinationParent = DestinationParent;
    Stack->Count++;
    return TRUE;
}

static
BOOLEAN
CmpCopyStackPop(
    _Inout_ PCM_COPY_STACK Stack,
    _Out_ PCM_COPY_ITEM Item)
{
    if (Stack->Count == 0) return FALSE;

    Stack->Count--;
    *Item = Stack->Items[Stack->Count];
    return TRUE;
}

static
VOID
CmpCopyStackFree(
    _Inout_ PCM_COPY_STACK Stack)
{
    if (Stack->Items != NULL) ExFreePool(Stack->Items);

    Stack->Items = NULL;
    Stack->Count = 0;
    Stack->Size = 0;
}

/*
 * Copies one key, and only that key: the copy starts out with no children,
 * its own copy of its values and class, and its own security cell.
 */
static
HCELL_INDEX
CmpCopyOneKey(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SourceCell,
    _In_ PHHIVE DestinationHive,
    _In_ HSTORAGE_TYPE StorageType,
    _Inout_ PHCELL_INDEX SecurityAnchor)
{
    PCM_KEY_NODE Source;
    PCM_KEY_NODE Target;
    PCELL_DATA SourceClass;
    PCELL_DATA TargetClass;
    HCELL_INDEX NewCell;
    HCELL_INDEX ClassCell;
    HCELL_INDEX SecurityCell;
    NTSTATUS Status;

    NewCell = CmpCopyCell(SourceHive, SourceCell, DestinationHive, StorageType);
    if (NewCell == HCELL_NIL) return HCELL_NIL;

    Source = (PCM_KEY_NODE)HvGetCell(SourceHive, SourceCell);
    if (Source == NULL)
    {
        HvFreeCell(DestinationHive, NewCell);
        return HCELL_NIL;
    }

    Target = (PCM_KEY_NODE)HvGetCell(DestinationHive, NewCell);
    if (Target == NULL)
    {
        HvReleaseCell(SourceHive, SourceCell);
        HvFreeCell(DestinationHive, NewCell);
        return HCELL_NIL;
    }

    Target->SubKeyCounts[Stable] = 0;
    Target->SubKeyCounts[Volatile] = 0;
    Target->SubKeyLists[Stable] = HCELL_NIL;
    Target->SubKeyLists[Volatile] = HCELL_NIL;
    Target->ValueList.Count = 0;
    Target->ValueList.List = HCELL_NIL;
    Target->Security = HCELL_NIL;
    Target->Class = HCELL_NIL;
    Target->Parent = HCELL_NIL;
    Target->Flags &= ~(KEY_HIVE_ENTRY | KEY_HIVE_EXIT);

    if (Source->ValueList.Count != 0)
    {
        Status = CmpCopyKeyValueList(SourceHive,
                                     &Source->ValueList,
                                     DestinationHive,
                                     &Target->ValueList,
                                     StorageType);
        if (!NT_SUCCESS(Status))
        {
            HvReleaseCell(DestinationHive, NewCell);
            HvReleaseCell(SourceHive, SourceCell);
            HvFreeCell(DestinationHive, NewCell);
            return HCELL_NIL;
        }
    }

    if (Source->Class != HCELL_NIL && Source->ClassLength != 0)
    {
        ClassCell = HvAllocateCell(DestinationHive,
                                   Source->ClassLength,
                                   StorageType,
                                   NewCell);
        if (ClassCell != HCELL_NIL)
        {
            SourceClass = (PCELL_DATA)HvGetCell(SourceHive, Source->Class);
            TargetClass = (PCELL_DATA)HvGetCell(DestinationHive, ClassCell);

            if (SourceClass != NULL && TargetClass != NULL)
            {
                RtlCopyMemory(TargetClass->u.KeyString,
                              SourceClass->u.KeyString,
                              Source->ClassLength);
                Target->Class = ClassCell;
                Target->ClassLength = Source->ClassLength;
            }

            if (TargetClass != NULL) HvReleaseCell(DestinationHive, ClassCell);
            if (SourceClass != NULL) HvReleaseCell(SourceHive, Source->Class);
        }
    }

    HvReleaseCell(DestinationHive, NewCell);
    HvReleaseCell(SourceHive, SourceCell);

    Status = CmpCopyKeySecurity(SourceHive,
                                SourceCell,
                                DestinationHive,
                                StorageType,
                                SecurityAnchor,
                                &SecurityCell);
    if (NT_SUCCESS(Status) && SecurityCell != HCELL_NIL)
    {
        Target = (PCM_KEY_NODE)HvGetCell(DestinationHive, NewCell);

        if (Target != NULL)
        {
            Target->Security = SecurityCell;
            HvReleaseCell(DestinationHive, NewCell);
        }
    }

    HvMarkCellDirty(DestinationHive, NewCell, FALSE);
    return NewCell;
}

/* Walks a key and everything under it, deepest first, and frees it all */
static
NTSTATUS
CmpDeleteKeyTree(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_ BOOLEAN KeepRoot)
{
    CM_COPY_STACK Stack = {0, 0, NULL};
    PCM_KEY_NODE Node;
    CM_COPY_ITEM Item;
    HCELL_INDEX Child;
    ULONG Children;
    ULONG i;

    if (!CmpCopyStackPush(&Stack, Cell, HCELL_NIL))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < Stack.Count; i++)
    {
        Item = Stack.Items[i];

        Node = (PCM_KEY_NODE)HvGetCell(Hive, Item.Source);
        if (Node == NULL) continue;

        Children = Node->SubKeyCounts[Stable] + Node->SubKeyCounts[Volatile];

        for (Child = 0; Child < Children; Child++)
        {
            HCELL_INDEX Found = CmpFindSubKeyByNumber(Hive, Node, Child);

            if (Found == HCELL_NIL) continue;
            if (!CmpCopyStackPush(&Stack, Found, Item.Source))
            {
                HvReleaseCell(Hive, Item.Source);
                CmpCopyStackFree(&Stack);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }

        HvReleaseCell(Hive, Item.Source);
    }

    while (CmpCopyStackPop(&Stack, &Item))
    {
        if (Item.Source == Cell && KeepRoot) continue;

        CmpMarkKeyDirty(Hive, Item.Source, FALSE);
        CmpFreeKeyByCell(Hive, Item.Source, Item.DestinationParent != HCELL_NIL);
    }

    CmpCopyStackFree(&Stack);

    if (!KeepRoot) return STATUS_SUCCESS;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (HvMarkCellDirty(Hive, Cell, FALSE))
    {
        Node->SubKeyCounts[Stable] = 0;
        Node->SubKeyCounts[Volatile] = 0;
        Node->SubKeyLists[Stable] = HCELL_NIL;
        Node->SubKeyLists[Volatile] = HCELL_NIL;
    }

    HvReleaseCell(Hive, Cell);
    return STATUS_SUCCESS;
}

PCMHIVE
NTAPI
CmpFindLoadedHive(
    _In_ PHHIVE Hive)
{
    PCMHIVE Result = NULL;
    PLIST_ENTRY Entry;
    PCMHIVE Candidate;

    ExAcquirePushLockShared(&CmpHiveListHeadLock);

    for (Entry = CmpHiveListHead.Flink;
         Entry != &CmpHiveListHead;
         Entry = Entry->Flink)
    {
        Candidate = CONTAINING_RECORD(Entry, CMHIVE, HiveList);

        if (&Candidate->Hive != Hive) continue;

        Result = Candidate;
        break;
    }

    ExReleasePushLockShared(&CmpHiveListHeadLock);
    return Result;
}

static
NTSTATUS
CmpRememberHiveFileNames(
    _Inout_ PCMHIVE Hive,
    _In_ PCUNICODE_STRING FilePath,
    _In_opt_ PCUNICODE_STRING RootPath)
{
    PWCHAR Buffer;

    Buffer = ExAllocatePoolZero(PagedPool,
                                FilePath->Length + sizeof(WCHAR),
                                TAG_CM);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, FilePath->Buffer, FilePath->Length);
    Hive->FileFullPath.Buffer = Buffer;
    Hive->FileFullPath.Length = FilePath->Length;
    Hive->FileFullPath.MaximumLength = FilePath->Length + sizeof(WCHAR);

    if (RootPath == NULL) return STATUS_SUCCESS;

    Buffer = ExAllocatePoolZero(PagedPool,
                                RootPath->Length + sizeof(WCHAR),
                                TAG_CM);
    if (Buffer == NULL) return STATUS_SUCCESS;

    RtlCopyMemory(Buffer, RootPath->Buffer, RootPath->Length);
    Hive->HiveRootPath.Buffer = Buffer;
    Hive->HiveRootPath.Length = RootPath->Length;
    Hive->HiveRootPath.MaximumLength = RootPath->Length + sizeof(WCHAR);
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
CmpCmdHiveOpen(
    _In_ POBJECT_ATTRIBUTES FileAttributes,
    _In_opt_ PSECURITY_CLIENT_CONTEXT ImpersonationContext,
    _Inout_ PBOOLEAN Allocate,
    _Out_ PCMHIVE *NewHive,
    _In_ ULONG CheckFlags)
{
    NTSTATUS Status;

    *NewHive = NULL;

    if (FileAttributes->ObjectName == NULL) return STATUS_INVALID_PARAMETER;

    if (ImpersonationContext != NULL)
    {
        Status = SeImpersonateClientEx(ImpersonationContext, NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = CmpInitHiveFromFile(FileAttributes->ObjectName,
                                 0,
                                 NewHive,
                                 Allocate,
                                 CheckFlags);

    if (ImpersonationContext != NULL) PsRevertToSelf();

    return Status;
}

NTSTATUS
NTAPI
CmpDeepCopyKey(
    _In_ PHHIVE SourceHive,
    _In_ HCELL_INDEX SrcKeyCell,
    _In_ PHHIVE DestinationHive,
    _In_ HSTORAGE_TYPE StorageType,
    _Out_opt_ PHCELL_INDEX DestKeyCell)
{
    CM_COPY_STACK Stack = {0, 0, NULL};
    HCELL_INDEX SecurityAnchor = HCELL_NIL;
    HCELL_INDEX Root = HCELL_NIL;
    HCELL_INDEX NewCell;
    PCM_KEY_NODE Node;
    CM_COPY_ITEM Item;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Children;
    ULONG i;

    if (DestKeyCell != NULL) *DestKeyCell = HCELL_NIL;

    if (!CmpCopyStackPush(&Stack, SrcKeyCell, HCELL_NIL))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while (CmpCopyStackPop(&Stack, &Item))
    {
        NewCell = CmpCopyOneKey(SourceHive,
                                Item.Source,
                                DestinationHive,
                                StorageType,
                                &SecurityAnchor);
        if (NewCell == HCELL_NIL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        if (Item.DestinationParent == HCELL_NIL)
        {
            Root = NewCell;
        }
        else if (!CmpAddSubKey(DestinationHive,
                               Item.DestinationParent,
                               NewCell))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Node = (PCM_KEY_NODE)HvGetCell(SourceHive, Item.Source);
        if (Node == NULL) continue;

        Children = Node->SubKeyCounts[Stable] + Node->SubKeyCounts[Volatile];

        for (i = 0; i < Children; i++)
        {
            HCELL_INDEX Child = CmpFindSubKeyByNumber(SourceHive, Node, i);

            if (Child == HCELL_NIL) continue;
            if (!CmpCopyStackPush(&Stack, Child, NewCell))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
        }

        HvReleaseCell(SourceHive, Item.Source);

        if (!NT_SUCCESS(Status)) break;
    }

    CmpCopyStackFree(&Stack);

    if (!NT_SUCCESS(Status)) return Status;

    if (DestKeyCell != NULL) *DestKeyCell = Root;
    return STATUS_SUCCESS;
}

/*
 * Reads a hive file and hangs it under a name the caller chooses. Doing this
 * needs the restore privilege, because the file decides what the keys under
 * that name say about who may read them. An application hive is exempt: it
 * belongs to the process that asked for it and nothing else can reach it.
 */
NTSTATUS
NTAPI
CmpLoadHiveAtName(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ POBJECT_ATTRIBUTES SourceFile,
    _In_ ULONG Flags,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PCMHIVE TrustClass,
    _Out_opt_ PCMHIVE *LoadedHive,
    _Out_opt_ PHANDLE RootHandle,
    _Out_opt_ PCM_KEY_BODY *RootBody)
{
    OBJECT_ATTRIBUTES RootAttributes;
    BOOLEAN Allocate = TRUE;
    PCMHIVE CmHive = NULL;
    PCM_KEY_BODY KeyBody;
    HANDLE KeyHandle = NULL;
    NTSTATUS Status;
    ULONG Attributes;

    if (LoadedHive != NULL) *LoadedHive = NULL;
    if (RootHandle != NULL) *RootHandle = NULL;
    if (RootBody != NULL) *RootBody = NULL;

    if (TargetKey->ObjectName == NULL || SourceFile->ObjectName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Flags & REG_APP_HIVE) == 0 &&
        !SeSinglePrivilegeCheck(SeRestorePrivilege, AccessMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    ExAcquirePushLockExclusive(&CmpLoadHiveLock);

    Status = CmpInitHiveFromFile(SourceFile->ObjectName,
                                 (Flags & REG_NO_LAZY_FLUSH) ? HIVE_NOLAZYFLUSH
                                                             : 0,
                                 &CmHive,
                                 &Allocate,
                                 CM_CHECK_REGISTRY_VALIDATE_HIVE);
    if (!NT_SUCCESS(Status))
    {
        ExReleasePushLockExclusive(&CmpLoadHiveLock);
        return Status;
    }

    if (Flags & REG_APP_HIVE) CmHive->Flags |= CMHIVE_FLAG_APPLICATION_HIVE;

    if ((Flags & REG_PROCESS_PRIVATE) || TrustClass != NULL)
    {
        if (Flags & REG_PROCESS_PRIVATE)
        {
            CmHive->Flags |= CMHIVE_FLAG_PROCESS_PRIVATE;
        }

        Status = CmpRegisterAppHive(
            CmHive,
            (Flags & REG_PROCESS_PRIVATE) ? PsGetCurrentProcess() : NULL,
            TrustClass);
        if (!NT_SUCCESS(Status))
        {
            CmpDestroyHive(CmHive);
            ExReleasePushLockExclusive(&CmpLoadHiveLock);
            return Status;
        }
    }

    if (Flags & REG_WHOLE_HIVE_VOLATILE)
    {
        CmHive->Hive.HiveFlags |= HIVE_VOLATILE;
    }

    CmpRememberHiveFileNames(CmHive,
                             SourceFile->ObjectName,
                             TargetKey->ObjectName);

    CmpLockRegistryExclusive();

    Status = CmpLinkHiveToMaster(TargetKey->ObjectName,
                                 TargetKey->RootDirectory,
                                 CmHive,
                                 (BOOLEAN)Allocate,
                                 TargetKey->SecurityDescriptor);

    CmpUnlockRegistry();

    if (!NT_SUCCESS(Status))
    {
        CmpUnregisterAppHive(CmHive);
        CmpDestroyHive(CmHive);
        ExReleasePushLockExclusive(&CmpLoadHiveLock);
        return Status;
    }

    if ((Flags & REG_APP_HIVE) == 0) CmpAddToHiveFileList(CmHive);

    if (RootHandle != NULL || RootBody != NULL)
    {
        Attributes = OBJ_CASE_INSENSITIVE;
        if (AccessMode == KernelMode) Attributes |= OBJ_KERNEL_HANDLE;

        InitializeObjectAttributes(&RootAttributes,
                                   TargetKey->ObjectName,
                                   Attributes,
                                   TargetKey->RootDirectory,
                                   NULL);

        Status = ObOpenObjectByName(&RootAttributes,
                                    CmpKeyObjectType,
                                    AccessMode,
                                    NULL,
                                    KEY_ALL_ACCESS,
                                    NULL,
                                    &KeyHandle);

        if (NT_SUCCESS(Status) && RootBody != NULL)
        {
            Status = ObReferenceObjectByHandle(KeyHandle,
                                               0,
                                               CmpKeyObjectType,
                                               KernelMode,
                                               (PVOID*)&KeyBody,
                                               NULL);
            if (NT_SUCCESS(Status)) *RootBody = KeyBody;
        }

        if (NT_SUCCESS(Status) && RootHandle != NULL)
        {
            *RootHandle = KeyHandle;
        }
        else if (KeyHandle != NULL)
        {
            ZwClose(KeyHandle);
        }
    }

    if (LoadedHive != NULL) *LoadedHive = CmHive;

    ExReleasePushLockExclusive(&CmpLoadHiveLock);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmLoadKey(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ POBJECT_ATTRIBUTES SourceFile,
    _In_ ULONG Flags,
    _In_ PCM_KEY_BODY KeyBody)
{
    UNREFERENCED_PARAMETER(KeyBody);

    return CmpLoadHiveAtName(TargetKey,
                             SourceFile,
                             Flags,
                             ExGetPreviousMode(),
                             NULL,
                             NULL,
                             NULL,
                             NULL);
}

/*
 * Takes a loaded hive back out. A hive with keys still open cannot go unless
 * the caller asks for it to be forced, in which case those keys are made to
 * answer that they are gone.
 */
NTSTATUS
NTAPI
CmUnloadKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ ULONG Flags)
{
    PCM_KEY_NODE Node;
    HCELL_INDEX LinkCell;
    PHHIVE MasterHive;
    PCMHIVE CmHive;
    ULONG OpenCount;

    if ((Kcb->Flags & KEY_HIVE_ENTRY) == 0) return STATUS_INVALID_PARAMETER;

    if (!SeSinglePrivilegeCheck(SeRestorePrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    CmHive = CmpFindLoadedHive(Kcb->KeyHive);
    if (CmHive == NULL) return STATUS_INVALID_PARAMETER;

    OpenCount = CmpEnumerateOpenSubKeys(Kcb, TRUE, FALSE);

    if (OpenCount != 0)
    {
        if ((Flags & REG_FORCE_UNLOAD) == 0) return STATUS_CANNOT_DELETE;

        CmpEnumerateOpenSubKeys(Kcb, TRUE, TRUE);
    }

    if (Kcb->RefCount > 1 && (Flags & REG_FORCE_UNLOAD) == 0)
    {
        return STATUS_CANNOT_DELETE;
    }

    CmpFlushHiveToDisk(CmHive, TRUE);

    Node = (PCM_KEY_NODE)HvGetCell(&CmHive->Hive,
                                   CmHive->Hive.BaseBlock->RootCell);
    LinkCell = (Node != NULL) ? Node->Parent : HCELL_NIL;
    if (Node != NULL)
    {
        HvReleaseCell(&CmHive->Hive, CmHive->Hive.BaseBlock->RootCell);
    }

    if (LinkCell != HCELL_NIL && CmiVolatileHive != NULL)
    {
        MasterHive = &CmiVolatileHive->Hive;

        Node = (PCM_KEY_NODE)HvGetCell(MasterHive, LinkCell);
        if (Node != NULL)
        {
            HCELL_INDEX Parent = Node->Parent;

            HvReleaseCell(MasterHive, LinkCell);

            CmpRemoveSubKey(MasterHive, Parent, LinkCell);
            HvFreeCell(MasterHive, LinkCell);
        }
    }

    Kcb->Delete = TRUE;
    CmpRemoveKeyControlBlock(Kcb);

    if (CmHive->RootKcb != NULL)
    {
        CmHive->RootKcb = NULL;
        CmpDereferenceKeyControlBlock(Kcb);
    }

    CmHive->Hive.HiveFlags |= HIVE_IS_UNLOADING;

    CmpUnregisterAppHive(CmHive);
    CmpSignalUnloadEvents(CmHive);
    CmpDereferenceHive(CmHive);
    return STATUS_SUCCESS;
}

/*
 * An application hive belongs to one process and is not announced in the hive
 * list, so nothing outside that process has any business finding it.
 */
NTSTATUS
NTAPI
CmpLoadAppHive(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ POBJECT_ATTRIBUTES SourceFile,
    _In_ ULONG Flags,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_opt_ PCM_KEY_BODY *RootBody,
    _Out_opt_ PCMHIVE *LoadedHive)
{
    PCMHIVE Hive = NULL;
    NTSTATUS Status;

    if (RootBody != NULL) *RootBody = NULL;
    if (LoadedHive != NULL) *LoadedHive = NULL;

    Status = CmpLoadHiveAtName(TargetKey,
                               SourceFile,
                               Flags | REG_APP_HIVE,
                               AccessMode,
                               NULL,
                               &Hive,
                               NULL,
                               RootBody);
    if (!NT_SUCCESS(Status)) return Status;

    CmpCompleteAppHiveLoad(Hive);

    if (LoadedHive != NULL) *LoadedHive = Hive;
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmpCompleteAppHiveLoad(
    _In_ PCMHIVE Hive)
{
    Hive->Hive.HiveFlags |= HIVE_NOLAZYFLUSH;
    Hive->CreatorOwner = NULL;
}

VOID
NTAPI
CmpUnloadAppHiveIfUnused(
    _In_ PCMHIVE Hive)
{
    if ((Hive->Flags & CMHIVE_FLAG_APPLICATION_HIVE) == 0) return;
    if (Hive->RootKcb != NULL && Hive->RootKcb->RefCount != 0) return;

    CmpDereferenceHive(Hive);
}

/*
 * Writes a key and everything under it out as a hive file of its own, which
 * is what a backup of part of the registry is.
 */
NTSTATUS
NTAPI
CmSaveKey(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ HANDLE FileHandle,
    _In_ ULONG Flags)
{
    HCELL_INDEX RootCell = HCELL_NIL;
    PCM_KEY_NODE Node;
    PCMHIVE Target;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Flags);

    if (!SeSinglePrivilegeCheck(SeBackupPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Kcb->Delete) return STATUS_KEY_DELETED;

    Status = CmpInitializeHive(&Target,
                               HINIT_CREATE,
                               HIVE_VOLATILE,
                               HFILE_TYPE_PRIMARY,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               0);
    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpDeepCopyKey(Kcb->KeyHive,
                            Kcb->KeyCell,
                            &Target->Hive,
                            Stable,
                            &RootCell);
    if (!NT_SUCCESS(Status) || RootCell == HCELL_NIL)
    {
        CmpDestroyHive(Target);
        return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
    }

    Node = (PCM_KEY_NODE)HvGetCell(&Target->Hive, RootCell);
    if (Node != NULL)
    {
        Node->Flags |= KEY_HIVE_ENTRY | KEY_NO_DELETE;
        Node->Parent = HCELL_NIL;
        HvMarkCellDirty(&Target->Hive, RootCell, FALSE);
        HvReleaseCell(&Target->Hive, RootCell);
    }

    Target->Hive.BaseBlock->RootCell = RootCell;
    Target->Hive.HiveFlags &= ~HIVE_VOLATILE;
    Target->FileHandles[HFILE_TYPE_PRIMARY] = FileHandle;

    Status = HvWriteHive(&Target->Hive) ? STATUS_SUCCESS
                                        : STATUS_REGISTRY_IO_FAILED;

    Target->FileHandles[HFILE_TYPE_PRIMARY] = NULL;
    Target->Hive.HiveFlags |= HIVE_VOLATILE;
    CmpDestroyHive(Target);
    return Status;
}

/*
 * Writes out one tree laid over another: everything the low key says, with
 * everything the high key says on top of it.
 */
NTSTATUS
NTAPI
CmSaveMergedKeys(
    _In_ PCM_KEY_CONTROL_BLOCK HighKcb,
    _In_ PCM_KEY_CONTROL_BLOCK LowKcb,
    _In_ HANDLE FileHandle)
{
    HCELL_INDEX RootCell = HCELL_NIL;
    PCM_KEY_NODE Node;
    PCMHIVE Target;
    NTSTATUS Status;

    if (!SeSinglePrivilegeCheck(SeBackupPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = CmpInitializeHive(&Target,
                               HINIT_CREATE,
                               HIVE_VOLATILE,
                               HFILE_TYPE_PRIMARY,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               0);
    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpDeepCopyKey(LowKcb->KeyHive,
                            LowKcb->KeyCell,
                            &Target->Hive,
                            Stable,
                            &RootCell);

    if (NT_SUCCESS(Status) && RootCell != HCELL_NIL)
    {
        Status = CmpOverlayKeyTree(HighKcb->KeyHive,
                                   HighKcb->KeyCell,
                                   &Target->Hive,
                                   RootCell,
                                   Stable);
    }

    if (!NT_SUCCESS(Status) || RootCell == HCELL_NIL)
    {
        CmpDestroyHive(Target);
        return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
    }

    Node = (PCM_KEY_NODE)HvGetCell(&Target->Hive, RootCell);
    if (Node != NULL)
    {
        Node->Flags |= KEY_HIVE_ENTRY | KEY_NO_DELETE;
        Node->Parent = HCELL_NIL;
        HvMarkCellDirty(&Target->Hive, RootCell, FALSE);
        HvReleaseCell(&Target->Hive, RootCell);
    }

    Target->Hive.BaseBlock->RootCell = RootCell;
    Target->Hive.HiveFlags &= ~HIVE_VOLATILE;
    Target->FileHandles[HFILE_TYPE_PRIMARY] = FileHandle;

    Status = HvWriteHive(&Target->Hive) ? STATUS_SUCCESS
                                        : STATUS_REGISTRY_IO_FAILED;

    Target->FileHandles[HFILE_TYPE_PRIMARY] = NULL;
    Target->Hive.HiveFlags |= HIVE_VOLATILE;
    CmpDestroyHive(Target);
    return Status;
}

/*
 * Throws away what a key holds and puts the contents of a hive file there
 * instead, which is how a backup is read back in.
 */
NTSTATUS
NTAPI
CmpRestoreKeyFromFile(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ HANDLE FileHandle,
    _In_ ULONG Flags)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    HCELL_INDEX SourceRoot;
    HCELL_INDEX NewChild;
    PCM_KEY_NODE Source;
    PCMHIVE Loaded;
    NTSTATUS Status;
    ULONG Children;
    ULONG i;

    if (!SeSinglePrivilegeCheck(SeRestorePrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Kcb == NULL || Kcb->Delete) return STATUS_KEY_DELETED;
    if (Kcb->KeyHive->ReadOnly) return STATUS_ACCESS_DENIED;

    Status = CmpInitializeHive(&Loaded,
                               HINIT_FILE,
                               HIVE_VOLATILE,
                               HFILE_TYPE_PRIMARY,
                               NULL,
                               FileHandle,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               CM_CHECK_REGISTRY_VALIDATE_HIVE);
    if (!NT_SUCCESS(Status)) return Status;

    SourceRoot = Loaded->Hive.BaseBlock->RootCell;

    Status = CmpDeleteKeyTree(Kcb->KeyHive, Kcb->KeyCell, TRUE);
    if (!NT_SUCCESS(Status))
    {
        Loaded->FileHandles[HFILE_TYPE_PRIMARY] = NULL;
        CmpDestroyHive(Loaded);
        return Status;
    }

    Source = (PCM_KEY_NODE)HvGetCell(&Loaded->Hive, SourceRoot);
    Children = (Source != NULL)
                   ? Source->SubKeyCounts[Stable] +
                         Source->SubKeyCounts[Volatile]
                   : 0;

    for (i = 0; i < Children; i++)
    {
        HCELL_INDEX Child = CmpFindSubKeyByNumber(&Loaded->Hive, Source, i);

        if (Child == HCELL_NIL) continue;

        Status = CmpDeepCopyKey(&Loaded->Hive,
                                Child,
                                Kcb->KeyHive,
                                HvGetCellType(Kcb->KeyCell),
                                &NewChild);
        if (!NT_SUCCESS(Status)) break;

        if (!CmpAddSubKey(Kcb->KeyHive, Kcb->KeyCell, NewChild))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
    }

    if (Source != NULL) HvReleaseCell(&Loaded->Hive, SourceRoot);

    Loaded->FileHandles[HFILE_TYPE_PRIMARY] = NULL;
    CmpDestroyHive(Loaded);

    if (!NT_SUCCESS(Status)) return Status;

    CmpCleanUpSubKeyInfo(Kcb);
    CmpCleanUpKcbValueCache(Kcb);
    CmpTouchKey(Kcb->KeyHive, Kcb->KeyCell);

    if (Flags & REG_NO_LAZY_FLUSH) return STATUS_SUCCESS;

    CmpReportNotify(Kcb, Kcb->KeyHive, Kcb->KeyCell, REG_NOTIFY_CHANGE_NAME);
    return STATUS_SUCCESS;
}

/*
 * Puts a different file behind a loaded hive. The hive goes on running from
 * what it has in memory; the new file is what the next boot will read.
 */
NTSTATUS
NTAPI
CmpReplaceHiveFile(
    _In_ PCMHIVE Hive,
    _In_ PCUNICODE_STRING NewFileName,
    _In_ PCUNICODE_STRING OldFileName)
{
    PFILE_RENAME_INFORMATION Rename;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Size;

    if (!SeSinglePrivilegeCheck(SeRestorePrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Hive->FileFullPath.Length == 0) return STATUS_INVALID_PARAMETER;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&Hive->FileFullPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        DELETE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatus,
                        0,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return Status;

    Size = (ULONG)FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) +
           OldFileName->Length;

    Rename = ExAllocatePoolZero(PagedPool, Size, TAG_CM);
    if (Rename == NULL)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Rename->ReplaceIfExists = TRUE;
    Rename->RootDirectory = NULL;
    Rename->FileNameLength = OldFileName->Length;
    RtlCopyMemory(Rename->FileName, OldFileName->Buffer, OldFileName->Length);

    Status = ZwSetInformationFile(Handle,
                                  &IoStatus,
                                  Rename,
                                  Size,
                                  FileRenameInformation);
    ExFreePool(Rename);
    ZwClose(Handle);

    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)NewFileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        DELETE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatus,
                        0,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return Status;

    Size = (ULONG)FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) +
           Hive->FileFullPath.Length;

    Rename = ExAllocatePoolZero(PagedPool, Size, TAG_CM);
    if (Rename == NULL)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Rename->ReplaceIfExists = TRUE;
    Rename->RootDirectory = NULL;
    Rename->FileNameLength = Hive->FileFullPath.Length;
    RtlCopyMemory(Rename->FileName,
                  Hive->FileFullPath.Buffer,
                  Hive->FileFullPath.Length);

    Status = ZwSetInformationFile(Handle,
                                  &IoStatus,
                                  Rename,
                                  Size,
                                  FileRenameInformation);
    ExFreePool(Rename);
    ZwClose(Handle);

    /* The replacement is what the next boot reads; this one goes on from memory */
    return Status;
}

/*
 * One boot-time thread per hive, so a slow disk only holds up the hive that
 * lives on it.
 */
CODE_SEG("INIT")
VOID
NTAPI
CmpLoadHiveThread(
    _In_ PVOID StartContext)
{
    PHIVE_LIST_ENTRY ListEntry = StartContext;
    UNICODE_STRING FileName;
    UNICODE_STRING RootName;
    BOOLEAN Allocate = TRUE;
    PCMHIVE CmHive = NULL;
    NTSTATUS Status;
    PWCHAR Buffer;
    SIZE_T Size;

    ListEntry->ThreadStarted = TRUE;

    Size = (wcslen(ListEntry->BaseName) + 1) * sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Size, TAG_CM);
    if (Buffer == NULL)
    {
        ListEntry->ThreadFinished = TRUE;
        if (CmpLoadWorkerEvent != NULL)
        {
            KeSetEvent(CmpLoadWorkerEvent, 0, FALSE);
        }

        PsTerminateSystemThread(STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    RtlCopyMemory(Buffer, ListEntry->BaseName, Size);
    RtlInitUnicodeString(&FileName, Buffer);
    RtlInitUnicodeString(&RootName, ListEntry->Name);

    Status = CmpInitHiveFromFile(&FileName,
                                 ListEntry->HHiveFlags,
                                 &CmHive,
                                 &Allocate,
                                 CM_CHECK_REGISTRY_VALIDATE_HIVE);
    if (NT_SUCCESS(Status))
    {
        CmHive->Flags |= ListEntry->CmHiveFlags;
        CmpRememberHiveFileNames(CmHive, &FileName, &RootName);
        ListEntry->CmHive = CmHive;
        ListEntry->Allocate = Allocate;
    }

    ExFreePool(Buffer);

    ListEntry->ThreadFinished = TRUE;

    if (CmpLoadWorkerEvent != NULL)
    {
        KeSetEvent(CmpLoadWorkerEvent, 0, FALSE);
    }

    PsTerminateSystemThread(Status);
}
