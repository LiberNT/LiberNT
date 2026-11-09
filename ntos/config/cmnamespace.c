/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Turning a registry path into a key, and making one that is missing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE FUNCTIONS **********************************************************/

/* Follows the reference an exit node holds into the hive mounted under it */
static
BOOLEAN
CmpCrossHiveBoundary(
    _Inout_ PHHIVE *Hive,
    _Inout_ PHCELL_INDEX Cell,
    _Inout_ PCM_KEY_NODE *Node)
{
    PCM_KEY_REFERENCE Reference = &(*Node)->ChildHiveReference;
    PHHIVE ChildHive = Reference->KeyHive;
    HCELL_INDEX ChildCell = Reference->KeyCell;
    PCM_KEY_NODE ChildNode;

    if (ChildHive == NULL || ChildCell == HCELL_NIL) return FALSE;

    ChildNode = (PCM_KEY_NODE)HvGetCell(ChildHive, ChildCell);
    if (ChildNode == NULL) return FALSE;

    HvReleaseCell(*Hive, *Cell);

    *Hive = ChildHive;
    *Cell = ChildCell;
    *Node = ChildNode;
    return TRUE;
}

/*
 * Reads the path a link key stands for and writes the caller a whole new name
 * made of that path and whatever was still unresolved behind the link.
 */
static
NTSTATUS
CmpReparseThroughLink(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_NODE Node,
    _In_ PCUNICODE_STRING Remaining,
    _Inout_ PUNICODE_STRING CompleteName)
{
    PCM_KEY_VALUE Value;
    HCELL_INDEX Cell;
    PCELL_DATA Data;
    UNICODE_STRING Target;
    PWCHAR Buffer;
    ULONG Length;
    USHORT Size;

    Cell = CmpFindValueByName(Hive, Node, &CmSymbolicLinkValueName);
    if (Cell == HCELL_NIL) return STATUS_OBJECT_NAME_NOT_FOUND;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
    if (Value == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Value->Type != REG_LINK)
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Data = CmpValueToData(Hive, Value, &Length);
    if (Data == NULL || Length == 0 || Length > MAXUSHORT)
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Target.Buffer = (PWCHAR)Data;
    Target.Length = (USHORT)Length;
    Target.MaximumLength = (USHORT)Length;

    Size = Target.Length + Remaining->Length;
    if (Remaining->Length != 0 &&
        Remaining->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
    {
        Size += sizeof(WCHAR);
    }

    Buffer = ExAllocatePoolZero(PagedPool, Size + sizeof(WCHAR), TAG_CM);
    if (Buffer == NULL)
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, Target.Buffer, Target.Length);
    Size = Target.Length;

    if (Remaining->Length != 0)
    {
        if (Remaining->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
        {
            Buffer[Size / sizeof(WCHAR)] = OBJ_NAME_PATH_SEPARATOR;
            Size += sizeof(WCHAR);
        }

        RtlCopyMemory((PUCHAR)Buffer + Size,
                      Remaining->Buffer,
                      Remaining->Length);
        Size += Remaining->Length;
    }

    HvReleaseCell(Hive, Cell);

    if (CompleteName->Buffer != NULL) ExFreePool(CompleteName->Buffer);

    CompleteName->Buffer = Buffer;
    CompleteName->Length = Size;
    CompleteName->MaximumLength = Size + sizeof(WCHAR);
    return STATUS_REPARSE;
}

/* The parent has to allow a subkey before one may be put under it */
static
NTSTATUS
CmpCheckCreateAccess(
    _In_ PCM_KEY_CONTROL_BLOCK ParentKcb,
    _In_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CreateLink)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    ACCESS_MASK Wanted = KEY_CREATE_SUB_KEY;
    ACCESS_MASK Granted = 0;
    NTSTATUS AccessStatus;
    NTSTATUS Status;
    BOOLEAN Allowed;

    if (AccessMode == KernelMode) return STATUS_SUCCESS;

    if (CreateLink) Wanted |= KEY_CREATE_LINK;

    Status = CmpGetKeySecurityDescriptor(ParentKcb, &Descriptor);
    if (!NT_SUCCESS(Status)) return Status;
    if (Descriptor == NULL) return STATUS_SUCCESS;

    SeLockSubjectContext(&AccessState->SubjectSecurityContext);

    Allowed = SeAccessCheck(Descriptor,
                            &AccessState->SubjectSecurityContext,
                            TRUE,
                            Wanted,
                            0,
                            NULL,
                            &CmpKeyObjectType->TypeInfo.GenericMapping,
                            AccessMode,
                            &Granted,
                            &AccessStatus);

    SeUnlockSubjectContext(&AccessState->SubjectSecurityContext);

    return Allowed ? STATUS_SUCCESS : AccessStatus;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Cuts the next path component off the front of a name. An empty component
 * with LastName set means the whole name has been consumed; FALSE says the
 * path itself is malformed.
 */
BOOLEAN
NTAPI
CmpGetNextName(
    _Inout_ PUNICODE_STRING RemainingName,
    _Out_ PUNICODE_STRING NextName,
    _Out_ PBOOLEAN LastName)
{
    ULONG Count;
    ULONG i;

    NextName->Buffer = NULL;
    NextName->Length = 0;
    NextName->MaximumLength = 0;
    *LastName = FALSE;

    if (RemainingName->Length == 0)
    {
        *LastName = TRUE;
        return TRUE;
    }

    if (RemainingName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        RemainingName->Buffer++;
        RemainingName->Length -= sizeof(WCHAR);

        if (RemainingName->Length == 0)
        {
            *LastName = TRUE;
            return TRUE;
        }

        if (RemainingName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR) return FALSE;
    }

    Count = RemainingName->Length / sizeof(WCHAR);

    for (i = 0; i < Count; i++)
    {
        if (RemainingName->Buffer[i] == OBJ_NAME_PATH_SEPARATOR) break;
    }

    NextName->Buffer = RemainingName->Buffer;
    NextName->Length = (USHORT)(i * sizeof(WCHAR));
    NextName->MaximumLength = NextName->Length;

    RemainingName->Buffer += i;
    RemainingName->Length -= NextName->Length;

    if (RemainingName->Length == 0) *LastName = TRUE;
    return TRUE;
}

NTSTATUS
NTAPI
CmpCreateKeyBody(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PCM_PARSE_CONTEXT Context,
    _Out_ PVOID *Object)
{
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    if (!CmpReferenceKeyControlBlock(Kcb)) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObCreateObject(AccessMode,
                            CmpKeyObjectType,
                            NULL,
                            AccessMode,
                            NULL,
                            sizeof(CM_KEY_BODY),
                            0,
                            0,
                            (PVOID*)&KeyBody);
    if (!NT_SUCCESS(Status))
    {
        CmpDereferenceKeyControlBlock(Kcb);
        return Status;
    }

    RtlZeroMemory(KeyBody, sizeof(CM_KEY_BODY));

    KeyBody->Type = CM_KEY_BODY_TYPE;
    KeyBody->KeyControlBlock = Kcb;
    KeyBody->NotifyBlock = NULL;
    KeyBody->ProcessID = PsGetCurrentProcessId();
    InitializeListHead(&KeyBody->ContextListHead);
    InitializeListHead(&KeyBody->KeyBodyList);

    EnlistKeyBodyWithKCB(KeyBody, 0);

    if (Context != NULL && Context->EffectiveTransaction != NULL)
    {
        CmpTransBindKeyBody(KeyBody, Context->EffectiveTransaction);
    }

    *Object = KeyBody;
    return STATUS_SUCCESS;
}

/*
 * Builds one new key under a parent that already exists. A volatile parent
 * can only hold volatile children, which is the one shape of create the
 * caller cannot ask its way out of.
 */
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
    _Out_ PVOID *Object)
{
    PCM_KEY_CONTROL_BLOCK Kcb;
    HSTORAGE_TYPE StorageType;
    PCM_KEY_NODE ParentNode;
    PCM_KEY_NODE Node;
    HCELL_INDEX NewCell;
    HCELL_INDEX ClassCell = HCELL_NIL;
    PCELL_DATA ClassData;
    NTSTATUS Status;
    USHORT NameSize;
    USHORT Flags = 0;

    *Object = NULL;

    if (Hive->ReadOnly) return STATUS_ACCESS_DENIED;

    StorageType = (Context->CreateOptions & REG_OPTION_VOLATILE) ? Volatile
                                                                 : Stable;

    if (HvGetCellType(Cell) == Volatile && StorageType == Stable)
    {
        return STATUS_CHILD_MUST_BE_VOLATILE;
    }

    if (Context->CreateOptions & REG_OPTION_CREATE_LINK)
    {
        Flags |= KEY_SYM_LINK;
    }

    if (StorageType == Volatile) Flags |= KEY_IS_VOLATILE;

    Status = CmpCheckCreateAccess(ParentKcb,
                                  AccessState,
                                  AccessMode,
                                  (Flags & KEY_SYM_LINK) != 0);
    if (!NT_SUCCESS(Status)) return Status;

    ParentNode = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (ParentNode == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (!HvMarkCellDirty(Hive, Cell, FALSE))
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NameSize = CmpNameSize(Hive, Name);

    NewCell = HvAllocateCell(Hive,
                             FIELD_OFFSET(CM_KEY_NODE, Name) + NameSize,
                             StorageType,
                             Cell);
    if (NewCell == HCELL_NIL)
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Node = (PCM_KEY_NODE)HvGetCell(Hive, NewCell);
    if (Node == NULL)
    {
        HvFreeCell(Hive, NewCell);
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Node, FIELD_OFFSET(CM_KEY_NODE, Name));

    Node->Signature = (Flags & KEY_SYM_LINK) ? CM_LINK_NODE_SIGNATURE
                                             : CM_KEY_NODE_SIGNATURE;
    Node->Flags = Flags;
    Node->Parent = Cell;
    Node->Security = HCELL_NIL;
    Node->Class = HCELL_NIL;
    Node->SubKeyLists[Stable] = HCELL_NIL;
    Node->SubKeyLists[Volatile] = HCELL_NIL;
    Node->ValueList.List = HCELL_NIL;
    KeQuerySystemTime(&Node->LastWriteTime);

    Node->NameLength = CmpCopyName(Hive, Node->Name, Name);
    if (Node->NameLength < Name->Length) Node->Flags |= KEY_COMP_NAME;

    if (Context->Class.Length != 0)
    {
        ClassCell = HvAllocateCell(Hive,
                                   Context->Class.Length,
                                   StorageType,
                                   NewCell);
        if (ClassCell == HCELL_NIL)
        {
            HvReleaseCell(Hive, NewCell);
            HvFreeCell(Hive, NewCell);
            HvReleaseCell(Hive, Cell);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        ClassData = (PCELL_DATA)HvGetCell(Hive, ClassCell);
        if (ClassData == NULL)
        {
            HvFreeCell(Hive, ClassCell);
            HvReleaseCell(Hive, NewCell);
            HvFreeCell(Hive, NewCell);
            HvReleaseCell(Hive, Cell);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(ClassData->u.KeyString,
                      Context->Class.Buffer,
                      Context->Class.Length);
        HvReleaseCell(Hive, ClassCell);

        Node->Class = ClassCell;
        Node->ClassLength = Context->Class.Length;
    }

    HvReleaseCell(Hive, NewCell);

    if (!CmpAddSubKey(Hive, Cell, NewCell))
    {
        if (ClassCell != HCELL_NIL) HvFreeCell(Hive, ClassCell);
        HvFreeCell(Hive, NewCell);
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (NameSize > ParentNode->MaxNameLen) ParentNode->MaxNameLen = NameSize;
    if (Context->Class.Length > ParentNode->MaxClassLen)
    {
        ParentNode->MaxClassLen = Context->Class.Length;
    }

    KeQuerySystemTime(&ParentNode->LastWriteTime);
    HvReleaseCell(Hive, Cell);

    Node = (PCM_KEY_NODE)HvGetCell(Hive, NewCell);
    if (Node == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Kcb = CmpCreateKeyControlBlock(Hive, NewCell, Node, ParentKcb, 0, Name);
    HvReleaseCell(Hive, NewCell);

    if (Kcb == NULL)
    {
        CmpRemoveSubKey(Hive, Cell, NewCell);
        if (ClassCell != HCELL_NIL) HvFreeCell(Hive, ClassCell);
        HvFreeCell(Hive, NewCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = CmpAssignSecurityDescriptor(Kcb, AccessState->SecurityDescriptor);
    if (!NT_SUCCESS(Status))
    {
        CmpDereferenceKeyControlBlock(Kcb);
        CmpRemoveSubKey(Hive, Cell, NewCell);
        if (ClassCell != HCELL_NIL) HvFreeCell(Hive, ClassCell);
        HvFreeCell(Hive, NewCell);
        return Status;
    }

    Status = CmpCreateKeyBody(Kcb, AccessMode, Context, Object);
    CmpDereferenceKeyControlBlock(Kcb);

    if (!NT_SUCCESS(Status)) return Status;

    CmpCleanUpSubKeyInfo(ParentKcb);
    Context->Disposition = REG_CREATED_NEW_KEY;

    CmpReportNotify(ParentKcb, Hive, Cell, REG_NOTIFY_CHANGE_NAME);
    return STATUS_SUCCESS;
}

/*
 * A link node is the key that stands in for the root of a hive mounted
 * somewhere else in the namespace: it holds the hive and cell to continue at
 * instead of children of its own.
 */
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
    _In_opt_ PULONG KcbsLocked,
    _Out_ PVOID *Object)
{
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_KEY_NODE ParentNode;
    PCM_KEY_NODE Node;
    PCM_KEY_NODE RootNode;
    HCELL_INDEX LinkCell;
    NTSTATUS Status;
    USHORT NameSize;

    UNREFERENCED_PARAMETER(CreateOptions);
    UNREFERENCED_PARAMETER(KcbsLocked);

    *Object = NULL;

    if (Context->ChildHive.KeyHive == NULL ||
        Context->ChildHive.KeyCell == HCELL_NIL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Hive != &CmiVolatileHive->Hive) return STATUS_INVALID_PARAMETER;

    ParentNode = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (ParentNode == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (CmpFindSubKeyByName(Hive, ParentNode, &Name) != HCELL_NIL)
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    if (!HvMarkCellDirty(Hive, Cell, FALSE))
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NameSize = CmpNameSize(Hive, &Name);

    LinkCell = HvAllocateCell(Hive,
                              FIELD_OFFSET(CM_KEY_NODE, Name) + NameSize,
                              Volatile,
                              Cell);
    if (LinkCell == HCELL_NIL)
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Node = (PCM_KEY_NODE)HvGetCell(Hive, LinkCell);
    if (Node == NULL)
    {
        HvFreeCell(Hive, LinkCell);
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Node, FIELD_OFFSET(CM_KEY_NODE, Name));

    Node->Signature = CM_KEY_NODE_SIGNATURE;
    Node->Flags = KEY_HIVE_EXIT | KEY_NO_DELETE;
    Node->Parent = Cell;
    Node->Security = HCELL_NIL;
    Node->Class = HCELL_NIL;
    Node->ChildHiveReference = Context->ChildHive;
    KeQuerySystemTime(&Node->LastWriteTime);

    Node->NameLength = CmpCopyName(Hive, Node->Name, &Name);
    if (Node->NameLength < Name.Length) Node->Flags |= KEY_COMP_NAME;

    HvReleaseCell(Hive, LinkCell);

    if (!CmpAddSubKey(Hive, Cell, LinkCell))
    {
        HvFreeCell(Hive, LinkCell);
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (NameSize > ParentNode->MaxNameLen) ParentNode->MaxNameLen = NameSize;
    KeQuerySystemTime(&ParentNode->LastWriteTime);
    HvReleaseCell(Hive, Cell);

    RootNode = (PCM_KEY_NODE)HvGetCell(Context->ChildHive.KeyHive,
                                       Context->ChildHive.KeyCell);
    if (RootNode == NULL)
    {
        CmpRemoveSubKey(Hive, Cell, LinkCell);
        HvFreeCell(Hive, LinkCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (HvMarkCellDirty(Context->ChildHive.KeyHive,
                        Context->ChildHive.KeyCell,
                        FALSE))
    {
        RootNode->Flags |= KEY_HIVE_ENTRY | KEY_NO_DELETE;
        RootNode->Parent = LinkCell;
    }

    Kcb = CmpCreateKeyControlBlock(Context->ChildHive.KeyHive,
                                   Context->ChildHive.KeyCell,
                                   RootNode,
                                   ParentKcb,
                                   0,
                                   &Name);
    HvReleaseCell(Context->ChildHive.KeyHive, Context->ChildHive.KeyCell);

    if (Kcb == NULL)
    {
        CmpRemoveSubKey(Hive, Cell, LinkCell);
        HvFreeCell(Hive, LinkCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = CmpCreateKeyBody(Kcb, AccessMode, Context, Object);
    CmpDereferenceKeyControlBlock(Kcb);

    if (!NT_SUCCESS(Status)) return Status;

    if (AccessState != NULL && AccessState->SecurityDescriptor != NULL)
    {
        CmpAssignSecurityDescriptor(Kcb, AccessState->SecurityDescriptor);
    }

    CmpCleanUpSubKeyInfo(ParentKcb);
    Context->Disposition = REG_CREATED_NEW_KEY;
    return STATUS_SUCCESS;
}

/*
 * The object manager hands a path to the key it got this far on and asks the
 * configuration manager to finish it. Every component is looked up in the
 * hive, every key passed through gets a control block, and a link met on the
 * way sends the whole lookup back to the object manager with a new name.
 */
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
    _In_opt_ POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    _Out_ PVOID *Object)
{
    PCM_PARSE_CONTEXT ParseContext = Context;
    PCM_KEY_BODY ParseKey = ParseObject;
    PCM_KEY_CONTROL_BLOCK Current;
    PCM_KEY_CONTROL_BLOCK Next;
    UNICODE_STRING Remaining;
    UNICODE_STRING Component;
    PCM_KEY_NODE Node;
    PCM_KEY_NODE Child;
    HCELL_INDEX ChildCell;
    HCELL_INDEX Cell;
    PHHIVE Hive;
    BOOLEAN Create;
    BOOLEAN OpenLink;
    BOOLEAN Last;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(SecurityQos);
    UNREFERENCED_PARAMETER(ExtendedParameters);

    *Object = NULL;

    if (ObjectType != CmpKeyObjectType) return STATUS_OBJECT_TYPE_MISMATCH;
    if (ParseKey->Type != CM_KEY_BODY_TYPE) return STATUS_OBJECT_TYPE_MISMATCH;

    Create = (ParseContext != NULL) && ParseContext->CreateOperation;

    if (Create)
    {
        CmpLockRegistryExclusive();
    }
    else
    {
        CmpLockRegistry();
    }

    Current = ParseKey->KeyControlBlock;

    if (Current->Delete)
    {
        CmpUnlockRegistry();
        return STATUS_KEY_DELETED;
    }

    if (!CmpReferenceKeyControlBlock(Current))
    {
        CmpUnlockRegistry();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Hive = Current->KeyHive;
    Cell = Current->KeyCell;
    Remaining = *RemainingName;
    Status = STATUS_SUCCESS;

    for (;;)
    {
        if (!CmpGetNextName(&Remaining, &Component, &Last))
        {
            Status = STATUS_OBJECT_NAME_INVALID;
            break;
        }

        if (Component.Length == 0)
        {
            if (!Last)
            {
                Status = STATUS_OBJECT_NAME_INVALID;
                break;
            }

            if (Create) ParseContext->Disposition = REG_OPENED_EXISTING_KEY;

            Status = CmpCreateKeyBody(Current, AccessMode, ParseContext, Object);
            break;
        }

        if (Component.Length > CMP_KEY_NAME_MAX)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Node = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
        if (Node == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        if (CmpSubKeyPossible(Current, &Component))
        {
            ChildCell = CmpFindSubKeyByName(Hive, Node, &Component);
        }
        else
        {
            ChildCell = HCELL_NIL;
        }

        if (ChildCell == HCELL_NIL)
        {
            HvReleaseCell(Hive, Cell);

            if (!Last || !Create)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }

            if (ParseContext->CreateLink)
            {
                Status = CmpCreateLinkNode(Hive,
                                           Cell,
                                           AccessState,
                                           Component,
                                           AccessMode,
                                           ParseContext->CreateOptions,
                                           ParseContext,
                                           Current,
                                           NULL,
                                           Object);
                break;
            }

            Status = CmpDoCreate(Hive,
                                 Cell,
                                 AccessState,
                                 &Component,
                                 AccessMode,
                                 ParseContext,
                                 Current,
                                 Object);
            break;
        }

        HvReleaseCell(Hive, Cell);

        Child = (PCM_KEY_NODE)HvGetCell(Hive, ChildCell);
        if (Child == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        if (Child->Flags & KEY_HIVE_EXIT)
        {
            PHHIVE NextHive = Hive;
            HCELL_INDEX NextCell = ChildCell;

            if (!CmpCrossHiveBoundary(&NextHive, &NextCell, &Child))
            {
                HvReleaseCell(Hive, ChildCell);
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }

            Hive = NextHive;
            ChildCell = NextCell;
        }

        OpenLink = (Attributes & OBJ_OPENLINK) != 0;

        if (ParseContext != NULL &&
            (ParseContext->CreateOptions & REG_OPTION_OPEN_LINK))
        {
            OpenLink = TRUE;
        }

        if ((Child->Flags & KEY_SYM_LINK) && (!Last || !OpenLink))
        {
            Status = CmpReparseThroughLink(Hive, Child, &Remaining, CompleteName);
            HvReleaseCell(Hive, ChildCell);
            break;
        }

        if ((Child->Flags & KEY_PREDEF_HANDLE) && Last)
        {
            if (ParseContext != NULL)
            {
                ParseContext->PredefinedHandle =
                    (HANDLE)(ULONG_PTR)Child->ValueList.Count;
            }

            HvReleaseCell(Hive, ChildCell);
            /* TODO: the object manager must not make a handle for this status */
            Status = STATUS_PREDEFINED_HANDLE;
            break;
        }

        Next = CmpCreateKeyControlBlock(Hive,
                                        ChildCell,
                                        Child,
                                        Current,
                                        0,
                                        &Component);
        HvReleaseCell(Hive, ChildCell);

        if (Next == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        CmpDereferenceKeyControlBlock(Current);
        Current = Next;
        Hive = Current->KeyHive;
        Cell = Current->KeyCell;

        if (!Last) continue;

        if (Create) ParseContext->Disposition = REG_OPENED_EXISTING_KEY;

        Status = CmpCreateKeyBody(Current, AccessMode, ParseContext, Object);
        break;
    }

    CmpDereferenceKeyControlBlock(Current);
    CmpUnlockRegistry();

    if (!NT_SUCCESS(Status) && Status != STATUS_REPARSE) *Object = NULL;
    return Status;
}

NTSTATUS
NTAPI
CmpParseKey(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *Object)
{
    return CmpParseKeyEx(ParseObject,
                         ObjectType,
                         AccessState,
                         AccessMode,
                         Attributes,
                         CompleteName,
                         RemainingName,
                         Context,
                         SecurityQos,
                         NULL,
                         Object);
}
