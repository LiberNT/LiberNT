/*
 *  ReactOS kernel
 *  Copyright (C) 2006 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS hive maker
 * FILE:            tools/mkhive/cmi.c
 * PURPOSE:         Registry file manipulation routines
 * PROGRAMMERS:     Hervé Poussineau
 *                  Hermès Bélusca-Maïto
 */

/* INCLUDES *****************************************************************/

#define NDEBUG
#include "mkhive.h"

#ifdef _WIN32
#include <malloc.h>
#endif

/* FUNCTIONS ****************************************************************/

PVOID
NTAPI
CmpAllocate(
    IN SIZE_T Size,
    IN BOOLEAN Paged,
    IN ULONG Tag)
{
#ifdef _WIN32
    return _aligned_malloc((size_t)Size, 16);
#else
    PVOID Buffer;

    if (posix_memalign(&Buffer, 16, (size_t)Size))
        return NULL;
    return Buffer;
#endif
}

VOID
NTAPI
CmpFree(
    IN PVOID Ptr,
    IN ULONG Quota)
{
#ifdef _WIN32
    _aligned_free(Ptr);
#else
    free(Ptr);
#endif
}

static BOOLEAN
NTAPI
CmpFileRead(
    IN PHHIVE RegistryHive,
    IN ULONG FileType,
    IN PULONG FileOffset,
    OUT PVOID Buffer,
    IN SIZE_T BufferLength)
{
    PCMHIVE CmHive = (PCMHIVE)RegistryHive;
    FILE *File = CmHive->FileHandles[HFILE_TYPE_PRIMARY];
    if (fseek(File, *FileOffset, SEEK_SET) != 0)
        return FALSE;

    return (fread(Buffer, 1, BufferLength, File) == BufferLength);
}

static BOOLEAN
NTAPI
CmpFileWrite(
    IN PHHIVE RegistryHive,
    IN ULONG FileType,
    IN PULONG FileOffset,
    IN PVOID Buffer,
    IN SIZE_T BufferLength)
{
    PCMHIVE CmHive = (PCMHIVE)RegistryHive;
    FILE *File = CmHive->FileHandles[HFILE_TYPE_PRIMARY];
    if (fseek(File, *FileOffset, SEEK_SET) != 0)
        return FALSE;

    return (fwrite(Buffer, 1, BufferLength, File) == BufferLength);
}

BOOLEAN
NTAPI
CmpFileFlush(
    IN PHHIVE RegistryHive,
    IN ULONG FileType,
    PLARGE_INTEGER FileOffset,
    ULONG Length)
{
    PCMHIVE CmHive = (PCMHIVE)RegistryHive;
    FILE *File = CmHive->FileHandles[HFILE_TYPE_PRIMARY];
    return (fflush(File) == 0);
}

NTSTATUS
CmiInitializeHive(
    IN OUT PCMHIVE Hive,
    IN PCWSTR Name)
{
    NTSTATUS Status;

    RtlZeroMemory(Hive, sizeof(*Hive));

    DPRINT("Hive 0x%p\n", Hive);

    Status = HvInitialize(&Hive->Hive,
                          HINIT_CREATE,
                          HIVE_NOLAZYFLUSH,
                          HFILE_TYPE_PRIMARY,
                          0,
                          CmpAllocate,
                          CmpFree,
                          CmpFileWrite,
                          CmpFileRead,
                          1,
                          NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // HACK: See the HACK from r31253
    if (!CmCreateRootNode(&Hive->Hive, Name))
    {
        HvFree(&Hive->Hive);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Add the new hive to the hive list */
    InsertTailList(&CmiHiveListHead,
                   &Hive->HiveList);

    return STATUS_SUCCESS;
}

NTSTATUS
CmiCreateSecurityKey(
    IN PHHIVE Hive,
    IN HCELL_INDEX Cell,
    IN PUCHAR Descriptor,
    IN ULONG DescriptorLength)
{
    HCELL_INDEX SecurityCell;
    PCM_KEY_NODE Node;
    PCM_KEY_SECURITY Security;

    if (!Descriptor || DescriptorLength < sizeof(SECURITY_DESCRIPTOR_RELATIVE))
        return STATUS_INVALID_PARAMETER;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    SecurityCell = HvAllocateCell(Hive,
                                  FIELD_OFFSET(CM_KEY_SECURITY, Descriptor) +
                                  DescriptorLength,
                                  Stable,
                                  HCELL_NIL);
    if (SecurityCell == HCELL_NIL)
    {
        HvReleaseCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Node->Security = SecurityCell;
    Security = (PCM_KEY_SECURITY)HvGetCell(Hive, SecurityCell);
    Security->Signature = CM_KEY_SECURITY_SIGNATURE;
    Security->ReferenceCount = 1;
    Security->DescriptorLength = DescriptorLength;

    RtlMoveMemory(&Security->Descriptor,
                  Descriptor,
                  DescriptorLength);

    Security->Flink = Security->Blink = SecurityCell;

    HvReleaseCell(Hive, SecurityCell);
    HvReleaseCell(Hive, Cell);

    return STATUS_SUCCESS;
}

#define MKHIVE_SE_DACL_PRESENT          0x0004
#define MKHIVE_SE_SACL_PRESENT          0x0010
#define MKHIVE_SE_DACL_AUTO_INHERITED   0x0400
#define MKHIVE_SE_SACL_AUTO_INHERITED   0x0800
#define MKHIVE_SE_DACL_PROTECTED        0x1000
#define MKHIVE_SE_SACL_PROTECTED        0x2000
#define MKHIVE_SE_SELF_RELATIVE         0x8000
#define MKHIVE_OBJECT_INHERIT_ACE       0x01
#define MKHIVE_CONTAINER_INHERIT_ACE    0x02
#define MKHIVE_NO_PROPAGATE_INHERIT_ACE 0x04
#define MKHIVE_INHERIT_ONLY_ACE         0x08
#define MKHIVE_INHERITED_ACE            0x10

static ULONG
CmiSidLength(IN const UCHAR* Sid)
{
    return 8 + 4 * (ULONG)Sid[1];
}

static BOOLEAN
CmiIsCreatorSid(IN const UCHAR* Sid, IN UCHAR Rid)
{
    static const UCHAR CreatorAuthority[6] = {0, 0, 0, 0, 0, 3};

    return Sid[0] == 1 && Sid[1] == 1 && !memcmp(&Sid[2], CreatorAuthority, 6) &&
           Sid[8] == Rid && Sid[9] == 0 && Sid[10] == 0 && Sid[11] == 0;
}

static ULONG
CmiPutAce(IN PUCHAR Out, IN UCHAR Type, IN UCHAR Flags, IN ULONG Mask, IN const UCHAR* Sid)
{
    ULONG SidLength = CmiSidLength(Sid);
    USHORT Size = (USHORT)(8 + SidLength);

    if (Out)
    {
        Out[0] = Type;
        Out[1] = Flags;
        memcpy(&Out[2], &Size, sizeof(Size));
        memcpy(&Out[4], &Mask, sizeof(Mask));
        memcpy(&Out[8], Sid, SidLength);
    }
    return Size;
}

static BOOLEAN
CmiAclGrants(IN const UCHAR* Acl, IN USHORT Before, IN UCHAR Type, IN ULONG Mask, IN const UCHAR* Sid)
{
    ULONG Offset = 8;
    USHORT Index;

    for (Index = 0; Index < Before; Index++)
    {
        const UCHAR* Ace = &Acl[Offset];
        USHORT Size;
        ULONG AceMask;

        memcpy(&Size, &Ace[2], sizeof(Size));
        memcpy(&AceMask, &Ace[4], sizeof(AceMask));
        Offset += Size;

        if (Ace[0] == Type && (Ace[1] & MKHIVE_CONTAINER_INHERIT_ACE) && (AceMask & Mask) == Mask &&
            CmiSidLength(&Ace[8]) == CmiSidLength(Sid) && !memcmp(&Ace[8], Sid, CmiSidLength(Sid)))
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* Builds, or only measures with Out == NULL, the list a subkey inherits from its parent's list */
static ULONG
CmiInheritAcl(IN const UCHAR* Acl, IN const UCHAR* Owner, IN const UCHAR* Group, OUT PUCHAR Out)
{
    USHORT AceCount, Index, Count = 0;
    ULONG Offset = 8, Written = 8;
    USHORT AclSize;

    memcpy(&AceCount, &Acl[4], sizeof(AceCount));

    for (Index = 0; Index < AceCount; Index++)
    {
        const UCHAR* Ace = &Acl[Offset];
        UCHAR Type = Ace[0], Flags = Ace[1];
        USHORT Size;
        ULONG Mask;
        const UCHAR* Sid = &Ace[8];

        memcpy(&Size, &Ace[2], sizeof(Size));
        memcpy(&Mask, &Ace[4], sizeof(Mask));
        Offset += Size;

        /* Only entries for containers reach a subkey, and only the plain allow and deny ones are understood here */
        if (!(Flags & MKHIVE_CONTAINER_INHERIT_ACE) || Type > 1) continue;

        if (CmiIsCreatorSid(Sid, 0) || CmiIsCreatorSid(Sid, 1))
        {
            const UCHAR* Creator = CmiIsCreatorSid(Sid, 0) ? Owner : Group;

            /* The creator's own entry is left out when an earlier one already gives it that access */
            if (!CmiAclGrants(Acl, Index, Type, Mask, Creator))
            {
                Written += CmiPutAce(Out ? &Out[Written] : NULL, Type, MKHIVE_INHERITED_ACE, Mask, Creator);
                Count++;
            }
            if (Flags & MKHIVE_NO_PROPAGATE_INHERIT_ACE) continue;

            Written += CmiPutAce(Out ? &Out[Written] : NULL, Type,
                                 (UCHAR)((Flags & (MKHIVE_OBJECT_INHERIT_ACE | MKHIVE_CONTAINER_INHERIT_ACE)) |
                                         MKHIVE_INHERIT_ONLY_ACE | MKHIVE_INHERITED_ACE),
                                 Mask, Sid);
            Count++;
            continue;
        }

        if (Flags & MKHIVE_NO_PROPAGATE_INHERIT_ACE)
            Flags = MKHIVE_INHERITED_ACE;
        else
            Flags = (UCHAR)((Flags & (MKHIVE_OBJECT_INHERIT_ACE | MKHIVE_CONTAINER_INHERIT_ACE)) | MKHIVE_INHERITED_ACE);

        Written += CmiPutAce(Out ? &Out[Written] : NULL, Type, Flags, Mask, Sid);
        Count++;
    }

    if (Out)
    {
        AclSize = (USHORT)Written;
        Out[0] = Acl[0];
        Out[1] = 0;
        memcpy(&Out[2], &AclSize, sizeof(AclSize));
        memcpy(&Out[4], &Count, sizeof(Count));
        Out[6] = Out[7] = 0;
    }
    return Written;
}

/* A subkey gets what its parent passes down as inherited entries, without the parent's protection */
static HCELL_INDEX
CmiInheritedSecurityCell(IN PHHIVE Hive, IN HCELL_INDEX ParentCell)
{
    PCM_KEY_SECURITY Parent, Security, Candidate;
    const UCHAR* Base;
    const UCHAR *Owner, *Group;
    SECURITY_DESCRIPTOR_RELATIVE Header;
    PUCHAR Descriptor;
    ULONG Length, Offset;
    HCELL_INDEX Cell, Next;

    if (ParentCell == HCELL_NIL) return HCELL_NIL;

    Parent = (PCM_KEY_SECURITY)HvGetCell(Hive, ParentCell);
    Base = (const UCHAR*)&Parent->Descriptor;
    Header = Parent->Descriptor;

    if (!(Header.Control & (MKHIVE_SE_DACL_PROTECTED | MKHIVE_SE_SACL_PROTECTED)) ||
        !Header.Owner || !Header.Group)
    {
        Parent->ReferenceCount++;
        HvReleaseCell(Hive, ParentCell);
        return ParentCell;
    }

    Owner = Base + Header.Owner;
    Group = Base + Header.Group;

    Length = sizeof(SECURITY_DESCRIPTOR_RELATIVE) + CmiSidLength(Owner) + CmiSidLength(Group);
    if ((Header.Control & MKHIVE_SE_DACL_PRESENT) && Header.Dacl)
        Length += CmiInheritAcl(Base + Header.Dacl, Owner, Group, NULL);
    if ((Header.Control & MKHIVE_SE_SACL_PRESENT) && Header.Sacl)
        Length += CmiInheritAcl(Base + Header.Sacl, Owner, Group, NULL);

    Descriptor = malloc(Length);
    if (!Descriptor)
    {
        Parent->ReferenceCount++;
        HvReleaseCell(Hive, ParentCell);
        return ParentCell;
    }

    memset(Descriptor, 0, Length);
    ((PISECURITY_DESCRIPTOR_RELATIVE)Descriptor)->Revision = Header.Revision;
    ((PISECURITY_DESCRIPTOR_RELATIVE)Descriptor)->Control =
        (SECURITY_DESCRIPTOR_CONTROL)((Header.Control & (MKHIVE_SE_DACL_PRESENT | MKHIVE_SE_SACL_PRESENT)) |
                                      MKHIVE_SE_SELF_RELATIVE | MKHIVE_SE_DACL_AUTO_INHERITED |
                                      MKHIVE_SE_SACL_AUTO_INHERITED);

    Offset = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    if ((Header.Control & MKHIVE_SE_SACL_PRESENT) && Header.Sacl)
    {
        ((PISECURITY_DESCRIPTOR_RELATIVE)Descriptor)->Sacl = Offset;
        Offset += CmiInheritAcl(Base + Header.Sacl, Owner, Group, Descriptor + Offset);
    }
    if ((Header.Control & MKHIVE_SE_DACL_PRESENT) && Header.Dacl)
    {
        ((PISECURITY_DESCRIPTOR_RELATIVE)Descriptor)->Dacl = Offset;
        Offset += CmiInheritAcl(Base + Header.Dacl, Owner, Group, Descriptor + Offset);
    }
    ((PISECURITY_DESCRIPTOR_RELATIVE)Descriptor)->Owner = Offset;
    memcpy(Descriptor + Offset, Owner, CmiSidLength(Owner));
    Offset += CmiSidLength(Owner);
    ((PISECURITY_DESCRIPTOR_RELATIVE)Descriptor)->Group = Offset;
    memcpy(Descriptor + Offset, Group, CmiSidLength(Group));

    /* The same descriptor is kept once per hive */
    Cell = Parent->Flink;
    while (Cell != ParentCell)
    {
        Candidate = (PCM_KEY_SECURITY)HvGetCell(Hive, Cell);
        Next = Candidate->Flink;
        if (Candidate->DescriptorLength == Length && !memcmp(&Candidate->Descriptor, Descriptor, Length))
        {
            Candidate->ReferenceCount++;
            HvReleaseCell(Hive, Cell);
            HvReleaseCell(Hive, ParentCell);
            free(Descriptor);
            return Cell;
        }
        HvReleaseCell(Hive, Cell);
        Cell = Next;
    }

    Cell = HvAllocateCell(Hive, FIELD_OFFSET(CM_KEY_SECURITY, Descriptor) + Length, Stable, HCELL_NIL);
    if (Cell == HCELL_NIL)
    {
        Parent->ReferenceCount++;
        HvReleaseCell(Hive, ParentCell);
        free(Descriptor);
        return ParentCell;
    }

    Parent = (PCM_KEY_SECURITY)HvGetCell(Hive, ParentCell);
    Security = (PCM_KEY_SECURITY)HvGetCell(Hive, Cell);
    Security->Signature = CM_KEY_SECURITY_SIGNATURE;
    Security->ReferenceCount = 1;
    Security->DescriptorLength = Length;
    memcpy(&Security->Descriptor, Descriptor, Length);

    /* Link it into the hive's list of security cells, right after its parent's */
    Next = Parent->Flink;
    Security->Flink = Next;
    Security->Blink = ParentCell;
    Parent->Flink = Cell;
    if (Next == ParentCell)
    {
        Parent->Blink = Cell;
    }
    else
    {
        Candidate = (PCM_KEY_SECURITY)HvGetCell(Hive, Next);
        Candidate->Blink = Cell;
        HvReleaseCell(Hive, Next);
    }

    HvReleaseCell(Hive, Cell);
    HvReleaseCell(Hive, ParentCell);
    free(Descriptor);
    return Cell;
}

static NTSTATUS
CmiCreateSubKey(
    IN PCMHIVE RegistryHive,
    IN HCELL_INDEX ParentKeyCellOffset,
    IN PCUNICODE_STRING SubKeyName,
    IN BOOLEAN VolatileKey,
    OUT HCELL_INDEX* pNKBOffset)
{
    HCELL_INDEX NKBOffset;
    PCM_KEY_NODE NewKeyCell;
    UNICODE_STRING KeyName;
    HSTORAGE_TYPE Storage;

    /* Skip leading path separator if present */
    if (SubKeyName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        KeyName.Buffer = &SubKeyName->Buffer[1];
        KeyName.Length = KeyName.MaximumLength = SubKeyName->Length - sizeof(WCHAR);
    }
    else
    {
        KeyName = *SubKeyName;
    }

    Storage = (VolatileKey ? Volatile : Stable);

    NKBOffset = HvAllocateCell(&RegistryHive->Hive,
                               FIELD_OFFSET(CM_KEY_NODE, Name) +
                               CmpNameSize(&RegistryHive->Hive, &KeyName),
                               Storage,
                               HCELL_NIL);
    if (NKBOffset == HCELL_NIL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewKeyCell = (PCM_KEY_NODE)HvGetCell(&RegistryHive->Hive, NKBOffset);
    if (NewKeyCell == NULL)
    {
        HvFreeCell(&RegistryHive->Hive, NKBOffset);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewKeyCell->Signature = CM_KEY_NODE_SIGNATURE;
    NewKeyCell->Flags = (VolatileKey ? KEY_IS_VOLATILE : 0);
    KeQuerySystemTime(&NewKeyCell->LastWriteTime);
    NewKeyCell->Parent = ParentKeyCellOffset;
    NewKeyCell->SubKeyCounts[Stable] = 0;
    NewKeyCell->SubKeyCounts[Volatile] = 0;
    NewKeyCell->SubKeyLists[Stable] = HCELL_NIL;
    NewKeyCell->SubKeyLists[Volatile] = HCELL_NIL;
    NewKeyCell->ValueList.Count = 0;
    NewKeyCell->ValueList.List = HCELL_NIL;
    NewKeyCell->Security = HCELL_NIL;
    NewKeyCell->Class = HCELL_NIL;
    NewKeyCell->ClassLength = 0;
    NewKeyCell->MaxNameLen = 0;
    NewKeyCell->MaxClassLen = 0;
    NewKeyCell->MaxValueNameLen = 0;
    NewKeyCell->MaxValueDataLen = 0;
    NewKeyCell->NameLength = CmpCopyName(&RegistryHive->Hive, NewKeyCell->Name, &KeyName);
    if (NewKeyCell->NameLength < KeyName.Length) NewKeyCell->Flags |= KEY_COMP_NAME;

    /* Inherit the security from the parent */
    if (ParentKeyCellOffset == HCELL_NIL)
    {
        // We are in fact creating a root key.
        // This is not handled there, but when we
        // call CmCreateRootNode instead.
        ASSERT(FALSE);
    }
    else
    {
        /* Get the parent node */
        PCM_KEY_NODE ParentKeyCell;
        ParentKeyCell = (PCM_KEY_NODE)HvGetCell(&RegistryHive->Hive, ParentKeyCellOffset);

        if (ParentKeyCell)
        {
            NewKeyCell->Security = CmiInheritedSecurityCell(&RegistryHive->Hive, ParentKeyCell->Security);

            HvReleaseCell(&RegistryHive->Hive, ParentKeyCellOffset);
        }
    }

    HvReleaseCell(&RegistryHive->Hive, NKBOffset);

    *pNKBOffset = NKBOffset;
    return STATUS_SUCCESS;
}

NTSTATUS
CmiAddSubKey(
    IN PCMHIVE RegistryHive,
    IN HCELL_INDEX ParentKeyCellOffset,
    IN PCUNICODE_STRING SubKeyName,
    IN BOOLEAN VolatileKey,
    OUT HCELL_INDEX *pBlockOffset)
{
    PCM_KEY_NODE ParentKeyCell;
    HCELL_INDEX NKBOffset;
    NTSTATUS Status;

    /* Create the new key */
    Status = CmiCreateSubKey(RegistryHive, ParentKeyCellOffset, SubKeyName, VolatileKey, &NKBOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Mark the parent cell as dirty */
    HvMarkCellDirty(&RegistryHive->Hive, ParentKeyCellOffset, FALSE);

    if (!CmpAddSubKey(&RegistryHive->Hive, ParentKeyCellOffset, NKBOffset))
    {
        /* FIXME: delete newly created cell */
        // CmpFreeKeyByCell(&RegistryHive->Hive, NewCell /*NKBOffset*/, FALSE);
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    /* Get the parent node */
    ParentKeyCell = (PCM_KEY_NODE)HvGetCell(&RegistryHive->Hive, ParentKeyCellOffset);
    if (!ParentKeyCell)
    {
        /* FIXME: delete newly created cell */
        return STATUS_UNSUCCESSFUL;
    }
    VERIFY_KEY_CELL(ParentKeyCell);

    /* Update the timestamp */
    KeQuerySystemTime(&ParentKeyCell->LastWriteTime);

    /* Check if we need to update name maximum, update it if so */
    if (ParentKeyCell->MaxNameLen < SubKeyName->Length)
        ParentKeyCell->MaxNameLen = SubKeyName->Length;

    /* Release the cell */
    HvReleaseCell(&RegistryHive->Hive, ParentKeyCellOffset);

    *pBlockOffset = NKBOffset;
    return STATUS_SUCCESS;
}

NTSTATUS
CmiAddValueKey(
    IN PCMHIVE RegistryHive,
    IN PCM_KEY_NODE Parent,
    IN ULONG ChildIndex,
    IN PCUNICODE_STRING ValueName,
    OUT PCM_KEY_VALUE *pValueCell,
    OUT HCELL_INDEX *pValueCellOffset)
{
    NTSTATUS Status;
    HSTORAGE_TYPE Storage;
    PCM_KEY_VALUE NewValueCell;
    HCELL_INDEX NewValueCellOffset;

    Storage = (Parent->Flags & KEY_IS_VOLATILE) ? Volatile : Stable;

    NewValueCellOffset = HvAllocateCell(&RegistryHive->Hive,
                               FIELD_OFFSET(CM_KEY_VALUE, Name) +
                               CmpNameSize(&RegistryHive->Hive, (PUNICODE_STRING)ValueName),
                               Storage,
                               HCELL_NIL);
    if (NewValueCellOffset == HCELL_NIL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewValueCell = (PCM_KEY_VALUE)HvGetCell(&RegistryHive->Hive, NewValueCellOffset);
    if (NewValueCell == NULL)
    {
        HvFreeCell(&RegistryHive->Hive, NewValueCellOffset);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewValueCell->Signature = CM_KEY_VALUE_SIGNATURE;
    NewValueCell->NameLength = CmpCopyName(&RegistryHive->Hive,
                                           NewValueCell->Name,
                                           (PUNICODE_STRING)ValueName);

    /* Check for compressed name */
    if (NewValueCell->NameLength < ValueName->Length)
    {
        /* This is a compressed name */
        NewValueCell->Flags = VALUE_COMP_NAME;
    }
    else
    {
        /* No flags to set */
        NewValueCell->Flags = 0;
    }

    NewValueCell->Type = 0;
    NewValueCell->DataLength = 0;
    NewValueCell->Data = HCELL_NIL;

    HvMarkCellDirty(&RegistryHive->Hive, NewValueCellOffset, FALSE);

    /* Check if we already have a value list */
    if (Parent->ValueList.Count)
    {
        /* Then make sure it's valid and dirty it */
        ASSERT(Parent->ValueList.List != HCELL_NIL);
        HvMarkCellDirty(&RegistryHive->Hive, Parent->ValueList.List, FALSE);
    }

    /* Add this value cell to the child list */
    Status = CmpAddValueToList(&RegistryHive->Hive,
                               NewValueCellOffset,
                               ChildIndex,
                               Storage,
                               &Parent->ValueList);

    /* If we failed, free the entire cell, including the data */
    if (!NT_SUCCESS(Status))
    {
        /* Overwrite the status with a known one */
        CmpFreeValue(&RegistryHive->Hive, NewValueCellOffset);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        *pValueCell = NewValueCell;
        *pValueCellOffset = NewValueCellOffset;
        Status = STATUS_SUCCESS;
    }

    return Status;
}
