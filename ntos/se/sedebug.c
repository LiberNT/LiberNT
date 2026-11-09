/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Security manager debug dumps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Writes out what a descriptor holds. A self-relative descriptor keeps
 * offsets where an absolute one keeps pointers, so the published accessors
 * are what resolve them; reaching into the fields directly faults on one.
 */
VOID
SepDumpSdDebugInfo(
    _In_opt_ PISECURITY_DESCRIPTOR SecurityDescriptor)
{
    BOOLEAN Present, Defaulted;
    PSID Owner, Group;
    PACL Acl;

    if (SecurityDescriptor == NULL)
    {
        DPRINT1("SE: no security descriptor\n");
        return;
    }

    DPRINT1("SE: descriptor %p revision %u control 0x%04x\n",
            SecurityDescriptor,
            SecurityDescriptor->Revision,
            SecurityDescriptor->Control);

    Owner = NULL;
    if (NT_SUCCESS(RtlGetOwnerSecurityDescriptor(SecurityDescriptor,
                                                 &Owner,
                                                 &Defaulted)))
    {
        DPRINT1("SE:   owner %p%s\n", Owner, Defaulted ? " (defaulted)" : "");
    }

    Group = NULL;
    if (NT_SUCCESS(RtlGetGroupSecurityDescriptor(SecurityDescriptor,
                                                 &Group,
                                                 &Defaulted)))
    {
        DPRINT1("SE:   group %p%s\n", Group, Defaulted ? " (defaulted)" : "");
    }

    Acl = NULL;
    Present = FALSE;
    if (NT_SUCCESS(RtlGetDaclSecurityDescriptor(SecurityDescriptor,
                                                &Present,
                                                &Acl,
                                                &Defaulted)))
    {
        if (!Present || Acl == NULL)
        {
            DPRINT1("SE:   no dacl, so every access is allowed\n");
        }
        else
        {
            DPRINT1("SE:   dacl %p size %u aces %u revision %u%s\n",
                    Acl, Acl->AclSize, Acl->AceCount, Acl->AclRevision,
                    Defaulted ? " (defaulted)" : "");
        }
    }

    Acl = NULL;
    Present = FALSE;
    if (NT_SUCCESS(RtlGetSaclSecurityDescriptor(SecurityDescriptor,
                                                &Present,
                                                &Acl,
                                                &Defaulted)) &&
        Present && Acl != NULL)
    {
        DPRINT1("SE:   sacl %p size %u aces %u revision %u%s\n",
                Acl, Acl->AclSize, Acl->AceCount, Acl->AclRevision,
                Defaulted ? " (defaulted)" : "");
    }
}

/* Writes out who a check is being made for */
VOID
SepDumpTokenDebugInfo(
    _In_opt_ PTOKEN Token)
{
    if (Token == NULL)
    {
        DPRINT1("SE: no token\n");
        return;
    }

    DPRINT1("SE: token %p id %08x-%08x authentication %08x-%08x\n",
            Token,
            Token->TokenId.HighPart, Token->TokenId.LowPart,
            Token->AuthenticationId.HighPart,
            Token->AuthenticationId.LowPart);

    DPRINT1("SE:   type %u level %u flags 0x%08lx session %lu\n",
            Token->TokenType,
            Token->ImpersonationLevel,
            Token->TokenFlags,
            Token->SessionId);

    DPRINT1("SE:   %lu users and groups, %lu restricted, %lu privileges\n",
            Token->UserAndGroupCount,
            Token->RestrictedSidCount,
            Token->PrivilegeCount);
}

/* Writes out where a check has got to: what is granted, denied and left */
VOID
SepDumpAccessRightsStats(
    _In_ PACCESS_CHECK_RIGHTS AccessRights)
{
    DPRINT1("SE: remaining 0x%08lx granted 0x%08lx denied 0x%08lx\n",
            AccessRights->RemainingAccessRights,
            AccessRights->GrantedAccessRights,
            AccessRights->DeniedAccessRights);
}

/*
 * Writes out what a check answered. A result list answers once per entry of
 * the object type list; anything else answers once for the object.
 */
VOID
SepDumpAccessAndStatusList(
    _In_ PACCESS_MASK GrantedAccessList,
    _In_ PNTSTATUS AccessStatusList,
    _In_ BOOLEAN IsResultList,
    _In_ POBJECT_TYPE_LIST_INTERNAL ObjectTypeList,
    _In_ ULONG ObjectTypeListLength)
{
    ULONG Count = IsResultList ? ObjectTypeListLength : 1;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        DPRINT1("SE: result %lu granted 0x%08lx status 0x%08lx\n",
                Index,
                GrantedAccessList[Index],
                AccessStatusList[Index]);

        if (IsResultList && ObjectTypeList != NULL)
        {
            DPRINT1("SE:   type level %u remaining 0x%08lx denied 0x%08lx\n",
                    ObjectTypeList[Index].Level,
                    ObjectTypeList[Index].ObjectAccessRights.RemainingAccessRights,
                    ObjectTypeList[Index].ObjectAccessRights.DeniedAccessRights);
        }
    }
}

/* EOF */
