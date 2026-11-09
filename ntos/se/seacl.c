/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The access control lists the system keeps and the ones it builds
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PACL SeSystemDefaultDacl = NULL;
PACL SePublicDefaultDacl = NULL;
PACL SePublicOpenDacl = NULL;
PACL SePublicDefaultUnrestrictedDacl = NULL;
PACL SePublicOpenUnrestrictedDacl = NULL;
/* TODO: Windows 11 has no list of its own for the anonymous logon; this one
 * stands until the tokens that read it are replaced */
PACL SeSystemAnonymousLogonDacl = NULL;

/* PRIVATE DATA ***************************************************************/

typedef struct _SEP_DACL_ACE
{
    PSID *Sid;
    ACCESS_MASK AccessMask;
} SEP_DACL_ACE;

/*
 * What becomes of one entry when it is copied: whether it is copied at all,
 * the flags and the rights the copy carries, and whose it becomes.
 */
typedef struct _SEP_ACE_PLAN
{
    BOOLEAN Keep;
    BOOLEAN Rewrite;
    UCHAR AceFlags;
    ACCESS_MASK AccessMask;
    PSID Sid;
    ULONG Size;
} SEP_ACE_PLAN;

#define SEP_ACE_INHERIT_FLAGS                                                 \
    (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE |                             \
     NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE)

/* PRIVATE FUNCTIONS **********************************************************/

/* The kinds of entry that are nothing but rights and a holder */
static
BOOLEAN
SepAceIsPlain(
    _In_ UCHAR AceType)
{
    static const UCHAR Plain[] =
    {
        SYSTEM_MANDATORY_LABEL_ACE_TYPE,
        ACCESS_DENIED_ACE_TYPE,
        ACCESS_ALLOWED_ACE_TYPE,
        SYSTEM_ALARM_ACE_TYPE,
        SYSTEM_AUDIT_ACE_TYPE,
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Plain); i++)
    {
        if (Plain[i] == AceType) return TRUE;
    }

    /* TODO: an object or compound entry is copied as it stands */
    return FALSE;
}

/*
 * Decides what one entry of the source list becomes. An entry being inherited
 * is only passed on to the kind of object it says it reaches, and loses the
 * right to go further when it says so.
 */
static
VOID
SepPlanAce(
    _In_ PACE_HEADER Ace,
    _In_opt_ PSID Owner,
    _In_opt_ PSID Group,
    _In_ BOOLEAN IsInherited,
    _In_ BOOLEAN MarkInherited,
    _In_ BOOLEAN IsDirectoryObject,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ SEP_ACE_PLAN *Plan)
{
    PSID Sid;

    RtlZeroMemory(Plan, sizeof(*Plan));

    Plan->Keep = TRUE;
    Plan->AceFlags = Ace->AceFlags;
    Plan->Size = Ace->AceSize;

    if (IsInherited)
    {
        if (IsDirectoryObject)
        {
            if (!(Ace->AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)))
            {
                Plan->Keep = FALSE;
                return;
            }

            if (Ace->AceFlags & CONTAINER_INHERIT_ACE)
            {
                /* It applies to this container, and still reaches past it */
                Plan->AceFlags &= ~INHERIT_ONLY_ACE;
            }
            else
            {
                /* It is only waiting to reach the objects inside */
                Plan->AceFlags |= INHERIT_ONLY_ACE;
            }

            if (Ace->AceFlags & NO_PROPAGATE_INHERIT_ACE)
            {
                if (!(Ace->AceFlags & CONTAINER_INHERIT_ACE))
                {
                    Plan->Keep = FALSE;
                    return;
                }

                Plan->AceFlags &= ~SEP_ACE_INHERIT_FLAGS;
            }
        }
        else
        {
            if (!(Ace->AceFlags & OBJECT_INHERIT_ACE))
            {
                Plan->Keep = FALSE;
                return;
            }

            Plan->AceFlags &= ~SEP_ACE_INHERIT_FLAGS;
        }

        if (MarkInherited) Plan->AceFlags |= INHERITED_ACE;
    }

    if (!SepAceIsPlain(Ace->AceType)) return;

    Sid = (PSID)&((PKNOWN_ACE)Ace)->SidStart;

    Plan->Rewrite = TRUE;
    Plan->AccessMask = ((PKNOWN_ACE)Ace)->Mask;
    Plan->Sid = Sid;

    /*
     * An entry that applies here is read as this object's own rights; one
     * that is only waiting to be inherited keeps the rights it was written
     * with, to be read for whatever object it finally reaches.
     */
    if (!(Plan->AceFlags & INHERIT_ONLY_ACE))
        RtlMapGenericMask(&Plan->AccessMask, GenericMapping);

    if (IsInherited)
    {

        /* What the creator was promised becomes the creator's */
        if (Owner != NULL && RtlEqualSid(Sid, SeCreatorOwnerSid))
            Plan->Sid = Owner;
        else if (Group != NULL && RtlEqualSid(Sid, SeCreatorGroupSid))
            Plan->Sid = Group;
    }

    Plan->Size = (ULONG)(FIELD_OFFSET(KNOWN_ACE, SidStart) +
                         RtlLengthSid(Plan->Sid));
}

static
CODE_SEG("INIT")
PACL
SepBuildSystemDacl(
    _In_ const SEP_DACL_ACE *Aces,
    _In_ ULONG AceCount)
{
    ULONG Size = sizeof(ACL);
    PACL Dacl;
    ULONG i;

    for (i = 0; i < AceCount; i++)
    {
        Size += (ULONG)(FIELD_OFFSET(KNOWN_ACE, SidStart) +
                        RtlLengthSid(*Aces[i].Sid));
    }

    Dacl = ExAllocatePoolWithTag(PagedPool, Size, TAG_ACL);
    if (Dacl == NULL) return NULL;

    if (!NT_SUCCESS(RtlCreateAcl(Dacl, Size, ACL_REVISION)))
    {
        ExFreePoolWithTag(Dacl, TAG_ACL);
        return NULL;
    }

    for (i = 0; i < AceCount; i++)
    {
        if (NT_SUCCESS(RtlAddAccessAllowedAce(Dacl,
                                              ACL_REVISION,
                                              Aces[i].AccessMask,
                                              *Aces[i].Sid)))
        {
            continue;
        }

        ExFreePoolWithTag(Dacl, TAG_ACL);
        return NULL;
    }

    return Dacl;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The lists every object the system makes for itself starts from. What they
 * say was read back from the namespace they protect: the root of the
 * namespace, a callback object and a driver object.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitSystemDacls(VOID)
{
    static const SEP_DACL_ACE PublicDefault[] =
    {
        {&SeWorldSid, GENERIC_READ},
        {&SeLocalSystemSid, GENERIC_ALL},
        {&SeAliasAdminsSid, GENERIC_ALL},
    };
    static const SEP_DACL_ACE PublicDefaultUnrestricted[] =
    {
        {&SeWorldSid, GENERIC_READ},
        {&SeLocalSystemSid, GENERIC_ALL},
        {&SeAliasAdminsSid, GENERIC_ALL},
        {&SeRestrictedCodeSid, GENERIC_READ},
    };
    static const SEP_DACL_ACE PublicOpen[] =
    {
        {&SeWorldSid, GENERIC_READ | GENERIC_WRITE},
        {&SeLocalSystemSid, GENERIC_ALL},
        {&SeAliasAdminsSid, GENERIC_ALL},
    };
    static const SEP_DACL_ACE PublicOpenUnrestricted[] =
    {
        {&SeWorldSid, GENERIC_READ | GENERIC_WRITE},
        {&SeLocalSystemSid, GENERIC_ALL},
        {&SeAliasAdminsSid, GENERIC_ALL},
        {&SeRestrictedCodeSid, GENERIC_READ},
    };
    static const SEP_DACL_ACE SystemDefault[] =
    {
        {&SeLocalSystemSid, GENERIC_ALL},
        {&SeAliasAdminsSid, GENERIC_READ},
    };
    /* The holder of a token and the system may do as they like with it */
    static const SEP_DACL_ACE SystemAnonymousLogon[] =
    {
        {&SeAnonymousLogonSid, GENERIC_ALL},
        {&SeLocalSystemSid, GENERIC_ALL},
    };

    PAGED_CODE();

    SePublicDefaultDacl =
        SepBuildSystemDacl(PublicDefault, RTL_NUMBER_OF(PublicDefault));
    SePublicDefaultUnrestrictedDacl =
        SepBuildSystemDacl(PublicDefaultUnrestricted,
                           RTL_NUMBER_OF(PublicDefaultUnrestricted));
    SePublicOpenDacl =
        SepBuildSystemDacl(PublicOpen, RTL_NUMBER_OF(PublicOpen));
    SePublicOpenUnrestrictedDacl =
        SepBuildSystemDacl(PublicOpenUnrestricted,
                           RTL_NUMBER_OF(PublicOpenUnrestricted));
    SeSystemDefaultDacl =
        SepBuildSystemDacl(SystemDefault, RTL_NUMBER_OF(SystemDefault));
    SeSystemAnonymousLogonDacl =
        SepBuildSystemDacl(SystemAnonymousLogon,
                           RTL_NUMBER_OF(SystemAnonymousLogon));

    if (SePublicDefaultDacl == NULL ||
        SePublicDefaultUnrestrictedDacl == NULL ||
        SePublicOpenDacl == NULL ||
        SePublicOpenUnrestrictedDacl == NULL ||
        SeSystemDefaultDacl == NULL ||
        SeSystemAnonymousLogonDacl == NULL)
    {
        DPRINT1("SE: could not build the lists the system starts from\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * Takes a copy of a list the caller handed in, so what it says cannot change
 * once it has been read.
 */
NTSTATUS
NTAPI
SepCaptureAcl(
    _In_ PACL InputAcl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PACL *CapturedAcl)
{
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN FromUser;
    PACL Copy = NULL;
    USHORT Room;

    PAGED_CODE();

    /* A list the kernel already holds is read where it stands */
    if (AccessMode == KernelMode && !CaptureIfKernel)
    {
        *CapturedAcl = InputAcl;
        return STATUS_SUCCESS;
    }

    FromUser = (BOOLEAN)(AccessMode != KernelMode);

    _SEH2_TRY
    {
        if (FromUser) ProbeForRead(InputAcl, sizeof(ACL), sizeof(ULONG));

        Room = InputAcl->AclSize;

        if (Room < sizeof(ACL)) _SEH2_YIELD(return STATUS_INVALID_ACL);

        if (FromUser) ProbeForRead(InputAcl, Room, sizeof(ULONG));

        Copy = ExAllocatePoolWithTag(PoolType, Room, TAG_ACL);
        if (Copy == NULL) _SEH2_YIELD(return STATUS_INSUFFICIENT_RESOURCES);

        RtlCopyMemory(Copy, InputAcl, Room);

        /* What was copied is judged, never what the caller still holds */
        if (Copy->AclSize != Room || !RtlValidAcl(Copy))
            Status = STATUS_INVALID_ACL;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        if (Copy != NULL) ExFreePoolWithTag(Copy, TAG_ACL);
        return Status;
    }

    *CapturedAcl = Copy;
    return STATUS_SUCCESS;
}

VOID
NTAPI
SepReleaseAcl(
    _In_ PACL CapturedAcl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel)
{
    PAGED_CODE();

    if (CapturedAcl == NULL) return;
    if (AccessMode == KernelMode && !CaptureIfKernel) return;

    ExFreePoolWithTag(CapturedAcl, TAG_ACL);
}

/*
 * The list that decides who may wear a thread's token: whoever the token
 * speaks for, whoever the process that holds it speaks for, and the system.
 */
NTSTATUS
NTAPI
SepCreateImpersonationTokenDacl(
    _In_ PTOKEN Token,
    _In_ PTOKEN PrimaryToken,
    _Out_ PACL *Dacl)
{
    PSID Holders[3];
    ULONG HolderCount = 0;
    ULONG Size = sizeof(ACL);
    PACL New;
    ULONG i;

    PAGED_CODE();

    *Dacl = NULL;

    Holders[HolderCount++] = Token->UserAndGroups[0].Sid;

    if (!RtlEqualSid(PrimaryToken->UserAndGroups[0].Sid, Holders[0]))
        Holders[HolderCount++] = PrimaryToken->UserAndGroups[0].Sid;

    for (i = 0; i < HolderCount; i++)
    {
        if (!RtlEqualSid(Holders[i], SeLocalSystemSid)) continue;
        break;
    }

    if (i == HolderCount) Holders[HolderCount++] = SeLocalSystemSid;

    for (i = 0; i < HolderCount; i++)
    {
        Size += (ULONG)(FIELD_OFFSET(KNOWN_ACE, SidStart) +
                        RtlLengthSid(Holders[i]));
    }

    New = ExAllocatePoolWithTag(PagedPool, Size, TAG_ACL);
    if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (!NT_SUCCESS(RtlCreateAcl(New, Size, ACL_REVISION)))
    {
        ExFreePoolWithTag(New, TAG_ACL);
        return STATUS_INVALID_ACL;
    }

    for (i = 0; i < HolderCount; i++)
    {
        if (NT_SUCCESS(RtlAddAccessAllowedAce(New,
                                              ACL_REVISION,
                                              TOKEN_ALL_ACCESS,
                                              Holders[i])))
        {
            continue;
        }

        ExFreePoolWithTag(New, TAG_ACL);
        return STATUS_INVALID_ACL;
    }

    *Dacl = New;
    return STATUS_SUCCESS;
}

/*
 * Copies a list onto a new object. The rights it speaks of are the new
 * object's own, what the creator was promised becomes the creator's, and an
 * entry being inherited only reaches the kind of object it says it does.
 *
 * Called with no destination it only says how much room the copy needs.
 */
NTSTATUS
SepPropagateAcl(
    _Out_writes_bytes_opt_(AclLength) PACL AclDest,
    _Inout_ PULONG AclLength,
    _In_reads_bytes_(AclSource->AclSize) PACL AclSource,
    _In_ PSID Owner,
    _In_ PSID Group,
    _In_ BOOLEAN IsInherited,
    _In_ BOOLEAN MarkInherited,
    _In_ BOOLEAN IsDirectoryObject,
    _In_opt_ GUID *ObjectType,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    SEP_ACE_PLAN Plan;
    PACE_HEADER Ace;
    PKNOWN_ACE Copy;
    ULONG Required = sizeof(ACL);
    ULONG Offset;
    USHORT AceCount = 0;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(ObjectType);

    if (AclSource == NULL)
    {
        *AclLength = 0;
        return STATUS_SUCCESS;
    }

    for (i = 0; i < AclSource->AceCount; i++)
    {
        Status = RtlGetAce(AclSource, i, (PVOID *)&Ace);
        if (!NT_SUCCESS(Status)) return Status;

        SepPlanAce(Ace, Owner, Group, IsInherited, MarkInherited,
                   IsDirectoryObject, GenericMapping, &Plan);
        if (!Plan.Keep) continue;

        Required += Plan.Size;
        AceCount++;
    }

    if (Required > MAXUSHORT) return STATUS_ALLOTTED_SPACE_EXCEEDED;

    if (AclDest == NULL)
    {
        *AclLength = Required;
        return STATUS_SUCCESS;
    }

    if (*AclLength < Required)
    {
        *AclLength = Required;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = RtlCreateAcl(AclDest, *AclLength, ACL_REVISION);
    if (!NT_SUCCESS(Status)) return Status;

    Offset = sizeof(ACL);

    for (i = 0; i < AclSource->AceCount; i++)
    {
        Status = RtlGetAce(AclSource, i, (PVOID *)&Ace);
        if (!NT_SUCCESS(Status)) return Status;

        SepPlanAce(Ace, Owner, Group, IsInherited, MarkInherited,
                   IsDirectoryObject, GenericMapping, &Plan);
        if (!Plan.Keep) continue;

        Copy = (PKNOWN_ACE)((PUCHAR)AclDest + Offset);

        if (Plan.Rewrite)
        {
            Copy->Header = *Ace;
            Copy->Mask = Plan.AccessMask;
            RtlCopyMemory(&Copy->SidStart, Plan.Sid, RtlLengthSid(Plan.Sid));
        }
        else
        {
            RtlCopyMemory(Copy, Ace, Ace->AceSize);
        }

        Copy->Header.AceFlags = Plan.AceFlags;
        Copy->Header.AceSize = (USHORT)Plan.Size;

        Offset += Plan.Size;
    }

    AclDest->AclSize = (USHORT)Required;
    AclDest->AceCount = AceCount;

    return STATUS_SUCCESS;
}

/*
 * Picks the list a new object starts from: one the caller named itself, then
 * what the container it is made in passes down, then the one the caller only
 * let stand, then the one its token carries.
 */
NTSTATUS
SepSelectAcl(
    _In_opt_ PACL ExplicitAcl,
    _In_ BOOLEAN ExplicitPresent,
    _In_ BOOLEAN ExplicitDefaulted,
    _In_opt_ PACL ParentAcl,
    _In_opt_ PACL DefaultAcl,
    _Out_ PULONG AclLength,
    _In_ PSID Owner,
    _In_ PSID Group,
    _Out_ PBOOLEAN AclPresent,
    _Out_ PBOOLEAN IsInherited,
    _In_ BOOLEAN IsDirectoryObject,
    _In_opt_ GUID *ObjectType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PACL *SelectedAcl)
{
    PAGED_CODE();

    *AclLength = 0;
    *AclPresent = TRUE;
    *IsInherited = FALSE;
    *SelectedAcl = NULL;

    if (ExplicitPresent && !ExplicitDefaulted)
    {
        *SelectedAcl = ExplicitAcl;
    }
    else
    {
        if (ParentAcl != NULL)
        {
            ULONG Length = 0;
            NTSTATUS Status;

            Status = SepPropagateAcl(NULL, &Length, ParentAcl, Owner, Group,
                                     TRUE, TRUE, IsDirectoryObject, ObjectType,
                                     GenericMapping);

            /* A container passes down only what says it reaches this far */
            if (NT_SUCCESS(Status) && Length > sizeof(ACL))
            {
                *SelectedAcl = ParentAcl;
                *IsInherited = TRUE;
                *AclLength = Length;
                return STATUS_SUCCESS;
            }
        }

        if (ExplicitPresent)
        {
            *SelectedAcl = ExplicitAcl;
        }
        else if (DefaultAcl != NULL)
        {
            *SelectedAcl = DefaultAcl;
        }
        else
        {
            *AclPresent = FALSE;
            return STATUS_SUCCESS;
        }
    }

    /* A list that is there and says nothing is a list all the same */
    if (*SelectedAcl == NULL) return STATUS_SUCCESS;

    return SepPropagateAcl(NULL,
                           AclLength,
                           *SelectedAcl,
                           Owner,
                           Group,
                           *IsInherited,
                           *IsInherited,
                           IsDirectoryObject,
                           ObjectType,
                           GenericMapping);
}
