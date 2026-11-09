/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The security descriptors the system keeps and the ones it assigns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* Everything a descriptor says, read out of whichever form it is kept in */
typedef struct _SEP_SD_PARTS
{
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSID Owner;
    PSID Group;
    PACL Dacl;
    PACL Sacl;
    BOOLEAN OwnerDefaulted;
    BOOLEAN GroupDefaulted;
    BOOLEAN DaclPresent;
    BOOLEAN DaclDefaulted;
    BOOLEAN SaclPresent;
    BOOLEAN SaclDefaulted;
} SEP_SD_PARTS, *PSEP_SD_PARTS;

/* The order the parts are written in, and where each one's offset is kept */
#define SEP_PART_SACL  0
#define SEP_PART_DACL  1
#define SEP_PART_OWNER 2
#define SEP_PART_GROUP 3
#define SEP_PART_COUNT 4

static SECURITY_DESCRIPTOR SepPublicDefaultSd;
static SECURITY_DESCRIPTOR SepPublicDefaultUnrestrictedSd;
static SECURITY_DESCRIPTOR SepPublicOpenSd;
static SECURITY_DESCRIPTOR SepPublicOpenUnrestrictedSd;
static SECURITY_DESCRIPTOR SepSystemDefaultSd;

/* GLOBALS ********************************************************************/

PSECURITY_DESCRIPTOR SeSystemDefaultSd = &SepSystemDefaultSd;
PSECURITY_DESCRIPTOR SePublicDefaultSd = &SepPublicDefaultSd;
PSECURITY_DESCRIPTOR SePublicOpenSd = &SepPublicOpenSd;
PSECURITY_DESCRIPTOR SePublicDefaultUnrestrictedSd = &SepPublicDefaultUnrestrictedSd;
PSECURITY_DESCRIPTOR SePublicOpenUnrestrictedSd = &SepPublicOpenUnrestrictedSd;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Reads out what a descriptor says. One that was handed to us keeps offsets
 * and one we built keeps pointers, and only the published readers know which.
 */
static
VOID
SepReadParts(
    _In_opt_ PSECURITY_DESCRIPTOR Descriptor,
    _Out_ PSEP_SD_PARTS Parts)
{
    ULONG Revision;

    RtlZeroMemory(Parts, sizeof(*Parts));

    if (Descriptor == NULL) return;

    RtlGetControlSecurityDescriptor(Descriptor, &Parts->Control, &Revision);
    RtlGetOwnerSecurityDescriptor(Descriptor, &Parts->Owner, &Parts->OwnerDefaulted);
    RtlGetGroupSecurityDescriptor(Descriptor, &Parts->Group, &Parts->GroupDefaulted);
    RtlGetDaclSecurityDescriptor(Descriptor,
                                 &Parts->DaclPresent,
                                 &Parts->Dacl,
                                 &Parts->DaclDefaulted);
    RtlGetSaclSecurityDescriptor(Descriptor,
                                 &Parts->SaclPresent,
                                 &Parts->Sacl,
                                 &Parts->SaclDefaulted);
}

/* The room one part takes where it is written */
static
VOID
SepMeasureParts(
    _In_ const SEP_SD_PARTS *Parts,
    _Out_writes_(SEP_PART_COUNT) PVOID *Part,
    _Out_writes_(SEP_PART_COUNT) PULONG Size)
{
    Part[SEP_PART_SACL] = Parts->SaclPresent ? Parts->Sacl : NULL;
    Part[SEP_PART_DACL] = Parts->DaclPresent ? Parts->Dacl : NULL;
    Part[SEP_PART_OWNER] = Parts->Owner;
    Part[SEP_PART_GROUP] = Parts->Group;

    Size[SEP_PART_SACL] =
        (Part[SEP_PART_SACL] != NULL) ? ((PACL)Part[SEP_PART_SACL])->AclSize : 0;
    Size[SEP_PART_DACL] =
        (Part[SEP_PART_DACL] != NULL) ? ((PACL)Part[SEP_PART_DACL])->AclSize : 0;
    Size[SEP_PART_OWNER] =
        (Part[SEP_PART_OWNER] != NULL) ? RtlLengthSid(Part[SEP_PART_OWNER]) : 0;
    Size[SEP_PART_GROUP] =
        (Part[SEP_PART_GROUP] != NULL) ? RtlLengthSid(Part[SEP_PART_GROUP]) : 0;
}

/*
 * Writes a descriptor as one block that keeps offsets, the only form that can
 * be stored or handed on. With no block to write into it only measures.
 */
static
NTSTATUS
SepWriteParts(
    _Out_writes_bytes_opt_(*Length) PSECURITY_DESCRIPTOR Buffer,
    _Inout_ PULONG Length,
    _In_ SECURITY_DESCRIPTOR_CONTROL Control,
    _In_ const SEP_SD_PARTS *Parts)
{
    PISECURITY_DESCRIPTOR_RELATIVE Flat;
    PVOID Part[SEP_PART_COUNT];
    ULONG Size[SEP_PART_COUNT];
    ULONG Total = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    PULONG Field;
    ULONG Where;
    ULONG i;

    SepMeasureParts(Parts, Part, Size);

    for (i = 0; i < SEP_PART_COUNT; i++) Total += Size[i];

    if (Buffer == NULL)
    {
        *Length = Total;
        return STATUS_SUCCESS;
    }

    if (*Length < Total)
    {
        *Length = Total;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Flat = Buffer;
    RtlZeroMemory(Flat, sizeof(*Flat));
    Flat->Revision = SECURITY_DESCRIPTOR_REVISION;
    Flat->Control = Control | SE_SELF_RELATIVE;

    Where = sizeof(SECURITY_DESCRIPTOR_RELATIVE);

    for (i = 0; i < SEP_PART_COUNT; i++)
    {
        if (Size[i] == 0) continue;

        switch (i)
        {
            case SEP_PART_SACL: Field = &Flat->Sacl; break;
            case SEP_PART_DACL: Field = &Flat->Dacl; break;
            case SEP_PART_OWNER: Field = &Flat->Owner; break;
            default: Field = &Flat->Group; break;
        }

        RtlCopyMemory((PUCHAR)Flat + Where, Part[i], Size[i]);
        *Field = Where;
        Where += Size[i];
    }

    *Length = Total;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SepNewDescriptor(
    _In_ SECURITY_DESCRIPTOR_CONTROL Control,
    _In_ const SEP_SD_PARTS *Parts,
    _In_ POOL_TYPE PoolType,
    _Outptr_ PSECURITY_DESCRIPTOR *Descriptor)
{
    ULONG Length = 0;
    PVOID Block;
    NTSTATUS Status;

    *Descriptor = NULL;

    SepWriteParts(NULL, &Length, Control, Parts);

    Block = ExAllocatePoolWithTag(PoolType, Length, TAG_SD);
    if (Block == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = SepWriteParts(Block, &Length, Control, Parts);
    if (NT_SUCCESS(Status))
    {
        *Descriptor = Block;
        return STATUS_SUCCESS;
    }

    ExFreePoolWithTag(Block, TAG_SD);
    return Status;
}

/* Every part a descriptor names has to be a part of its kind */
static
BOOLEAN
SepPartsAreWellFormed(
    _In_ const SEP_SD_PARTS *Parts)
{
    if (Parts->Owner != NULL && !RtlValidSid(Parts->Owner)) return FALSE;
    if (Parts->Group != NULL && !RtlValidSid(Parts->Group)) return FALSE;
    if (Parts->DaclPresent && Parts->Dacl != NULL && !RtlValidAcl(Parts->Dacl))
        return FALSE;
    if (Parts->SaclPresent && Parts->Sacl != NULL && !RtlValidAcl(Parts->Sacl))
        return FALSE;

    return TRUE;
}

/* A part that lives in the caller's own memory is read where it stands */
static
VOID
SepProbeParts(
    _In_ const SEP_SD_PARTS *Parts)
{
    if (Parts->Owner != NULL)
    {
        ProbeForRead(Parts->Owner, sizeof(SID), sizeof(ULONG));
        ProbeForRead(Parts->Owner, RtlLengthSid(Parts->Owner), sizeof(ULONG));
    }

    if (Parts->Group != NULL)
    {
        ProbeForRead(Parts->Group, sizeof(SID), sizeof(ULONG));
        ProbeForRead(Parts->Group, RtlLengthSid(Parts->Group), sizeof(ULONG));
    }

    if (Parts->Dacl != NULL)
    {
        ProbeForRead(Parts->Dacl, sizeof(ACL), sizeof(ULONG));
        ProbeForRead(Parts->Dacl, Parts->Dacl->AclSize, sizeof(ULONG));
    }

    if (Parts->Sacl != NULL)
    {
        ProbeForRead(Parts->Sacl, sizeof(ACL), sizeof(ULONG));
        ProbeForRead(Parts->Sacl, Parts->Sacl->AclSize, sizeof(ULONG));
    }
}

/*
 * One list of a new object, settled and copied. The caller says where the
 * candidates come from; what comes back is the caller's to free.
 */
static
NTSTATUS
SepInheritAcl(
    _In_ BOOLEAN IsDirectoryObject,
    _In_ PGENERIC_MAPPING Mapping,
    _In_opt_ GUID *ObjectType,
    _In_ PSID Holder,
    _In_opt_ PSID Team,
    _In_opt_ PACL Named,
    _In_ BOOLEAN NamedPresent,
    _In_ BOOLEAN NamedDefaulted,
    _In_opt_ PACL FromAbove,
    _In_opt_ PACL Carried,
    _Out_ PACL *Result,
    _Out_ PBOOLEAN Present,
    _Out_ PBOOLEAN Inherited,
    _Out_ PBOOLEAN Defaulted)
{
    PACL Chosen = NULL;
    PACL Built = NULL;
    ULONG Length = 0;
    NTSTATUS Status;

    *Result = NULL;
    *Defaulted = FALSE;

    Status = SepSelectAcl(Named,
                          NamedPresent,
                          NamedDefaulted,
                          FromAbove,
                          Carried,
                          &Length,
                          Holder,
                          Team,
                          Present,
                          Inherited,
                          IsDirectoryObject,
                          ObjectType,
                          Mapping,
                          &Chosen);
    if (!NT_SUCCESS(Status) || !*Present || Chosen == NULL) return Status;

    /* Only the caller can say its own list was never really chosen */
    *Defaulted = (BOOLEAN)(NamedDefaulted && Chosen == Named);

    if (Length <= sizeof(ACL)) Length = sizeof(ACL);

    Built = ExAllocatePoolWithTag(PagedPool, Length, TAG_ACL);
    if (Built == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = SepPropagateAcl(Built,
                             &Length,
                             Chosen,
                             Holder,
                             Team,
                             *Inherited,
                             *Inherited,
                             IsDirectoryObject,
                             ObjectType,
                             Mapping);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Built, TAG_ACL);
        return Status;
    }

    *Result = Built;
    return STATUS_SUCCESS;
}

/* Whoever the caller speaks for may be handed an object */
static
NTSTATUS
SepCheckNewOwner(
    _In_opt_ PTOKEN Token,
    _In_ PSID Owner,
    _In_ ULONG AutoInheritFlags)
{
    LUID_AND_ATTRIBUTES TakeOwnership;

    if (AutoInheritFlags & SEF_AVOID_PRIVILEGE_CHECK) return STATUS_SUCCESS;
    if (Token == NULL) return STATUS_INVALID_OWNER;
    if (SepSidInToken((PACCESS_TOKEN)Token, Owner)) return STATUS_SUCCESS;

    TakeOwnership.Luid = SeTakeOwnershipPrivilege;
    TakeOwnership.Attributes = 0;

    if (SepPrivilegeCheck(Token,
                          &TakeOwnership,
                          1,
                          PRIVILEGE_SET_ALL_NECESSARY,
                          KernelMode))
    {
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_OWNER;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The descriptors the system starts its own objects from. Each one says
 * nothing but the list that protects it, so whoever creates an object from it
 * becomes its owner, which is what a descriptor without an owner means.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitSystemSds(VOID)
{
    PSECURITY_DESCRIPTOR Descriptor[] =
    {
        &SepSystemDefaultSd,
        &SepPublicDefaultSd,
        &SepPublicOpenSd,
        &SepPublicDefaultUnrestrictedSd,
        &SepPublicOpenUnrestrictedSd,
    };
    PACL *List[] =
    {
        &SeSystemDefaultDacl,
        &SePublicDefaultDacl,
        &SePublicOpenDacl,
        &SePublicDefaultUnrestrictedDacl,
        &SePublicOpenUnrestrictedDacl,
    };
    ULONG i;

    PAGED_CODE();

    for (i = 0; i < RTL_NUMBER_OF(Descriptor); i++)
    {
        if (!NT_SUCCESS(RtlCreateSecurityDescriptor(Descriptor[i],
                                                    SECURITY_DESCRIPTOR_REVISION)) ||
            !NT_SUCCESS(RtlSetDaclSecurityDescriptor(Descriptor[i],
                                                     TRUE,
                                                     *List[i],
                                                     FALSE)))
        {
            DPRINT1("SE: could not settle the descriptors the system starts from\n");
            return FALSE;
        }
    }

    return TRUE;
}

/*
 * Takes a copy of a descriptor the caller handed in, so what it says cannot
 * change once it has been read, and in the one form that stands on its own.
 */
NTSTATUS
NTAPI
SeCaptureSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR OriginalSecurityDescriptor,
    _In_ KPROCESSOR_MODE CurrentMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PSECURITY_DESCRIPTOR *CapturedSecurityDescriptor)
{
    SEP_SD_PARTS Parts;
    NTSTATUS Status = STATUS_SUCCESS;
    PISECURITY_DESCRIPTOR Given;
    BOOLEAN FromUser;
    ULONG Control;

    PAGED_CODE();

    *CapturedSecurityDescriptor = NULL;

    if (OriginalSecurityDescriptor == NULL) return STATUS_SUCCESS;

    if (CurrentMode == KernelMode && !CaptureIfKernel)
    {
        *CapturedSecurityDescriptor = OriginalSecurityDescriptor;
        return STATUS_SUCCESS;
    }

    Given = OriginalSecurityDescriptor;
    FromUser = (BOOLEAN)(CurrentMode != KernelMode);

    _SEH2_TRY
    {
        if (FromUser)
        {
            ProbeForRead(Given,
                         sizeof(SECURITY_DESCRIPTOR_RELATIVE),
                         sizeof(ULONG));
        }

        if (Given->Revision != SECURITY_DESCRIPTOR_REVISION)
            _SEH2_YIELD(return STATUS_UNKNOWN_REVISION);

        if (FromUser)
        {
            if (Given->Control & SE_SELF_RELATIVE)
            {
                ProbeForRead(Given,
                             RtlLengthSecurityDescriptor(Given),
                             sizeof(ULONG));
            }
            else
            {
                ProbeForRead(Given, sizeof(SECURITY_DESCRIPTOR), sizeof(ULONG));
            }
        }

        SepReadParts(Given, &Parts);

        /* A descriptor of pointers points outside itself, so each part is
         * reached in turn */
        if (FromUser && !(Parts.Control & SE_SELF_RELATIVE))
            SepProbeParts(&Parts);

        if (!SepPartsAreWellFormed(&Parts))
            _SEH2_YIELD(return STATUS_INVALID_SECURITY_DESCR);

        Control = Parts.Control & ~SE_SELF_RELATIVE;
        if (Parts.DaclPresent) Control |= SE_DACL_PRESENT;
        if (Parts.SaclPresent) Control |= SE_SACL_PRESENT;

        Status = SepNewDescriptor((SECURITY_DESCRIPTOR_CONTROL)Control,
                                  &Parts,
                                  PoolType,
                                  CapturedSecurityDescriptor);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
SeReleaseSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR CapturedSecurityDescriptor,
    _In_ KPROCESSOR_MODE CurrentMode,
    _In_ BOOLEAN CaptureIfKernelMode)
{
    PAGED_CODE();

    /* Only a copy is ours to let go of */
    if ((CapturedSecurityDescriptor != NULL) &&
        (CurrentMode != KernelMode || CaptureIfKernelMode))
    {
        ExFreePoolWithTag(CapturedSecurityDescriptor, TAG_SD);
    }

    return STATUS_SUCCESS;
}

/*
 * Says whether a block of the given size holds a descriptor that stands on
 * its own, with every part it names lying inside it.
 */
BOOLEAN
NTAPI
SeValidSecurityDescriptor(
    _In_ ULONG Length,
    _In_ PSECURITY_DESCRIPTOR _SecurityDescriptor)
{
    PISECURITY_DESCRIPTOR_RELATIVE Flat = _SecurityDescriptor;
    ULONG Offset[SEP_PART_COUNT];
    ULONG Least[SEP_PART_COUNT];
    ULONG Measured;
    PVOID Part;
    ULONG i;

    PAGED_CODE();

    if (Length < sizeof(SECURITY_DESCRIPTOR_RELATIVE)) return FALSE;
    if (Flat->Revision != SECURITY_DESCRIPTOR_REVISION) return FALSE;
    if (!(Flat->Control & SE_SELF_RELATIVE)) return FALSE;

    Offset[SEP_PART_SACL] = (Flat->Control & SE_SACL_PRESENT) ? Flat->Sacl : 0;
    Offset[SEP_PART_DACL] = (Flat->Control & SE_DACL_PRESENT) ? Flat->Dacl : 0;
    Offset[SEP_PART_OWNER] = Flat->Owner;
    Offset[SEP_PART_GROUP] = Flat->Group;

    Least[SEP_PART_SACL] = sizeof(ACL);
    Least[SEP_PART_DACL] = sizeof(ACL);
    Least[SEP_PART_OWNER] = sizeof(SID);
    Least[SEP_PART_GROUP] = sizeof(SID);

    for (i = 0; i < SEP_PART_COUNT; i++)
    {
        if (Offset[i] == 0) continue;

        if (Offset[i] < sizeof(SECURITY_DESCRIPTOR_RELATIVE)) return FALSE;
        if (Offset[i] > Length - Least[i]) return FALSE;

        Part = (PUCHAR)Flat + Offset[i];

        if (i == SEP_PART_SACL || i == SEP_PART_DACL)
        {
            Measured = ((PACL)Part)->AclSize;
            if (Offset[i] + Measured > Length) return FALSE;
            if (!RtlValidAcl(Part)) return FALSE;
            continue;
        }

        if (!RtlValidSid(Part)) return FALSE;

        Measured = RtlLengthSid(Part);
        if (Offset[i] + Measured > Length) return FALSE;
    }

    return TRUE;
}

/*
 * Settles what protects a new object: what the caller asked for, then what
 * the container it is made in passes down, then what its token carries. The
 * answer stands on its own and is the caller's to give back.
 */
NTSTATUS
NTAPI
SeAssignSecurityEx(
    _In_opt_ PSECURITY_DESCRIPTOR ParentDescriptor,
    _In_opt_ PSECURITY_DESCRIPTOR ExplicitDescriptor,
    _Out_ PSECURITY_DESCRIPTOR *NewDescriptor,
    _In_opt_ GUID *ObjectType,
    _In_ BOOLEAN IsDirectoryObject,
    _In_ ULONG AutoInheritFlags,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ POOL_TYPE PoolType)
{
    SEP_SD_PARTS Asked;
    SEP_SD_PARTS Above;
    SEP_SD_PARTS Settled;
    BOOLEAN DaclInherited = FALSE;
    BOOLEAN SaclInherited = FALSE;
    BOOLEAN DaclDefaulted = FALSE;
    BOOLEAN SaclDefaulted = FALSE;
    ULONG Control = 0;
    PTOKEN Token;
    NTSTATUS Status;

    PAGED_CODE();

    *NewDescriptor = NULL;

    SepReadParts(ExplicitDescriptor, &Asked);
    SepReadParts(ParentDescriptor, &Above);
    RtlZeroMemory(&Settled, sizeof(Settled));

    SeLockSubjectContext(SubjectContext);

    Token = (PTOKEN)SeQuerySubjectContextToken(SubjectContext);

    /* Whoever was named, else whoever holds the container, else the caller */
    Settled.Owner = Asked.Owner;

    if (Settled.Owner == NULL && (AutoInheritFlags & SEF_DEFAULT_OWNER_FROM_PARENT))
        Settled.Owner = Above.Owner;

    if (Settled.Owner != NULL)
    {
        Status = SepCheckNewOwner(Token, Settled.Owner, AutoInheritFlags);
        if (!NT_SUCCESS(Status))
        {
            SeUnlockSubjectContext(SubjectContext);
            return Status;
        }
    }
    else
    {
        if (Token == NULL)
        {
            SeUnlockSubjectContext(SubjectContext);
            return STATUS_INVALID_OWNER;
        }

        Settled.Owner = Token->UserAndGroups[Token->DefaultOwnerIndex].Sid;
        Control |= SE_OWNER_DEFAULTED;
    }

    Settled.Group = Asked.Group;

    if (Settled.Group == NULL && (AutoInheritFlags & SEF_DEFAULT_GROUP_FROM_PARENT))
        Settled.Group = Above.Group;

    if (Settled.Group == NULL && Token != NULL)
    {
        Settled.Group = Token->PrimaryGroup;
        Control |= SE_GROUP_DEFAULTED;
    }

    Status = SepInheritAcl(IsDirectoryObject,
                           GenericMapping,
                           ObjectType,
                           Settled.Owner,
                           Settled.Group,
                           Asked.Dacl,
                           Asked.DaclPresent,
                           Asked.DaclDefaulted,
                           Above.DaclPresent ? Above.Dacl : NULL,
                           (Token != NULL) ? Token->DefaultDacl : NULL,
                           &Settled.Dacl,
                           &Settled.DaclPresent,
                           &DaclInherited,
                           &DaclDefaulted);

    if (NT_SUCCESS(Status))
    {
        Status = SepInheritAcl(IsDirectoryObject,
                               GenericMapping,
                               ObjectType,
                               Settled.Owner,
                               Settled.Group,
                               Asked.Sacl,
                               Asked.SaclPresent,
                               Asked.SaclDefaulted,
                               Above.SaclPresent ? Above.Sacl : NULL,
                               NULL,
                               &Settled.Sacl,
                               &Settled.SaclPresent,
                               &SaclInherited,
                               &SaclDefaulted);
    }

    if (NT_SUCCESS(Status))
    {
        if (Settled.DaclPresent) Control |= SE_DACL_PRESENT;
        if (Settled.SaclPresent) Control |= SE_SACL_PRESENT;
        if (DaclDefaulted) Control |= SE_DACL_DEFAULTED;
        if (SaclDefaulted) Control |= SE_SACL_DEFAULTED;
        if (DaclInherited && (AutoInheritFlags & SEF_DACL_AUTO_INHERIT))
            Control |= SE_DACL_AUTO_INHERITED;
        if (SaclInherited && (AutoInheritFlags & SEF_SACL_AUTO_INHERIT))
            Control |= SE_SACL_AUTO_INHERITED;

        /* TODO: an object nobody labelled is given no label of its own */
        Status = SepNewDescriptor((SECURITY_DESCRIPTOR_CONTROL)Control,
                                  &Settled,
                                  PoolType,
                                  NewDescriptor);
    }

    SeUnlockSubjectContext(SubjectContext);

    if (Settled.Dacl != NULL) ExFreePoolWithTag(Settled.Dacl, TAG_ACL);
    if (Settled.Sacl != NULL) ExFreePoolWithTag(Settled.Sacl, TAG_ACL);

    return Status;
}

NTSTATUS
NTAPI
SeAssignSecurity(
    _In_opt_ PSECURITY_DESCRIPTOR ParentDescriptor,
    _In_opt_ PSECURITY_DESCRIPTOR ExplicitDescriptor,
    _Out_ PSECURITY_DESCRIPTOR *NewDescriptor,
    _In_ BOOLEAN IsDirectoryObject,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ POOL_TYPE PoolType)
{
    /* Nothing is said about object types, and nothing is inherited by itself */
    return SeAssignSecurityEx(ParentDescriptor,
                              ExplicitDescriptor,
                              NewDescriptor,
                              NULL,
                              IsDirectoryObject,
                              0,
                              SubjectContext,
                              GenericMapping,
                              PoolType);
}

NTSTATUS
NTAPI
SeDeassignSecurity(
    _Inout_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Done = *SecurityDescriptor;

    PAGED_CODE();

    *SecurityDescriptor = NULL;

    if (Done != NULL) ExFreePoolWithTag(Done, TAG_SD);

    return STATUS_SUCCESS;
}

/*
 * Hands back the parts of an object's descriptor that were asked for, in the
 * form that stands on its own, and says how much room they take.
 */
NTSTATUS
NTAPI
SeQuerySecurityDescriptorInfo(
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Out_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG Length,
    _Inout_ PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor)
{
    SECURITY_INFORMATION Wanted = *SecurityInformation;
    SEP_SD_PARTS Held;
    SEP_SD_PARTS Told;
    ULONG Control = 0;
    ULONG Needed = 0;

    PAGED_CODE();

    SepReadParts(*ObjectsSecurityDescriptor, &Held);
    RtlZeroMemory(&Told, sizeof(Told));

    if (Wanted & OWNER_SECURITY_INFORMATION) Told.Owner = Held.Owner;
    if (Wanted & GROUP_SECURITY_INFORMATION) Told.Group = Held.Group;

    if (Wanted & DACL_SECURITY_INFORMATION)
    {
        Told.Dacl = Held.Dacl;
        Told.DaclPresent = Held.DaclPresent;

        if (Held.DaclPresent) Control |= SE_DACL_PRESENT;
        Control |= Held.Control & (SE_DACL_DEFAULTED | SE_DACL_AUTO_INHERITED);
    }

    if (Wanted & SACL_SECURITY_INFORMATION)
    {
        Told.Sacl = Held.Sacl;
        Told.SaclPresent = Held.SaclPresent;

        if (Held.SaclPresent) Control |= SE_SACL_PRESENT;
        Control |= Held.Control & (SE_SACL_DEFAULTED | SE_SACL_AUTO_INHERITED);
    }

    /* Asking with no room to write in is asking how much room it takes */
    SepWriteParts(NULL, &Needed, (SECURITY_DESCRIPTOR_CONTROL)Control, &Told);

    if (SecurityDescriptor == NULL || *Length < Needed)
    {
        *Length = Needed;
        return STATUS_BUFFER_TOO_SMALL;
    }

    return SepWriteParts(SecurityDescriptor,
                         Length,
                         (SECURITY_DESCRIPTOR_CONTROL)Control,
                         &Told);
}

/*
 * Changes the parts of an object's descriptor the caller named and leaves the
 * rest as they stand. What comes back is a new block; what the object held is
 * the caller's to let go of.
 */
NTSTATUS
NTAPI
SeSetSecurityDescriptorInfoEx(
    _In_opt_ PVOID Object,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR ModificationDescriptor,
    _Inout_ PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor,
    _In_ ULONG AutoInheritFlags,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    SECURITY_INFORMATION Wanted = *SecurityInformation;
    SEP_SD_PARTS Held;
    SEP_SD_PARTS Given;
    SEP_SD_PARTS Next;
    PACL ReadDacl = NULL;
    PACL ReadSacl = NULL;
    ULONG Control;
    ULONG Length;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(AutoInheritFlags);

    SepReadParts(*ObjectsSecurityDescriptor, &Held);
    SepReadParts(ModificationDescriptor, &Given);

    Next = Held;
    Control = Held.Control &
              (SE_DACL_DEFAULTED | SE_SACL_DEFAULTED |
               SE_DACL_AUTO_INHERITED | SE_SACL_AUTO_INHERITED);

    if (Wanted & OWNER_SECURITY_INFORMATION)
    {
        if (Given.Owner == NULL) return STATUS_INVALID_OWNER;
        Next.Owner = Given.Owner;
    }

    if (Wanted & GROUP_SECURITY_INFORMATION)
    {
        if (Given.Group == NULL) return STATUS_INVALID_PRIMARY_GROUP;
        Next.Group = Given.Group;
    }

    /* A list that is set is read as this object's own rights from here on */
    if (Wanted & DACL_SECURITY_INFORMATION)
    {
        Next.Dacl = NULL;
        Next.DaclPresent = Given.DaclPresent;
        Control &= ~(SE_DACL_DEFAULTED | SE_DACL_AUTO_INHERITED);

        if (Given.DaclPresent && Given.Dacl != NULL)
        {
            Length = 0;
            Status = SepPropagateAcl(NULL, &Length, Given.Dacl, Next.Owner,
                                     Next.Group, FALSE, FALSE, FALSE, NULL,
                                     GenericMapping);
            if (NT_SUCCESS(Status))
            {
                ReadDacl = ExAllocatePoolWithTag(PagedPool, Length, TAG_ACL);
                if (ReadDacl == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;
            }

            if (NT_SUCCESS(Status))
            {
                Status = SepPropagateAcl(ReadDacl, &Length, Given.Dacl,
                                         Next.Owner, Next.Group, FALSE, FALSE,
                                         FALSE, NULL, GenericMapping);
            }

            Next.Dacl = ReadDacl;
        }
    }

    if (NT_SUCCESS(Status) && (Wanted & SACL_SECURITY_INFORMATION))
    {
        Next.Sacl = NULL;
        Next.SaclPresent = Given.SaclPresent;
        Control &= ~(SE_SACL_DEFAULTED | SE_SACL_AUTO_INHERITED);

        if (Given.SaclPresent && Given.Sacl != NULL)
        {
            Length = 0;
            Status = SepPropagateAcl(NULL, &Length, Given.Sacl, Next.Owner,
                                     Next.Group, FALSE, FALSE, FALSE, NULL,
                                     GenericMapping);
            if (NT_SUCCESS(Status))
            {
                ReadSacl = ExAllocatePoolWithTag(PagedPool, Length, TAG_ACL);
                if (ReadSacl == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;
            }

            if (NT_SUCCESS(Status))
            {
                Status = SepPropagateAcl(ReadSacl, &Length, Given.Sacl,
                                         Next.Owner, Next.Group, FALSE, FALSE,
                                         FALSE, NULL, GenericMapping);
            }

            Next.Sacl = ReadSacl;
        }
    }

    if (NT_SUCCESS(Status))
    {
        if (Next.DaclPresent) Control |= SE_DACL_PRESENT;
        if (Next.SaclPresent) Control |= SE_SACL_PRESENT;

        Status = SepNewDescriptor((SECURITY_DESCRIPTOR_CONTROL)Control,
                                  &Next,
                                  PoolType,
                                  ObjectsSecurityDescriptor);
    }

    if (ReadDacl != NULL) ExFreePoolWithTag(ReadDacl, TAG_ACL);
    if (ReadSacl != NULL) ExFreePoolWithTag(ReadSacl, TAG_ACL);

    return Status;
}

NTSTATUS
NTAPI
SeSetSecurityDescriptorInfo(
    _In_opt_ PVOID Object,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    /* Nothing is inherited by itself when the caller says nothing about it */
    return SeSetSecurityDescriptorInfoEx(Object,
                                         SecurityInformation,
                                         SecurityDescriptor,
                                         ObjectsSecurityDescriptor,
                                         0,
                                         PoolType,
                                         GenericMapping);
}

/* Writes a descriptor that keeps nothing back from anyone */
NTSTATUS
NTAPI
SeSetWorldSecurityDescriptor(
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PISECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PULONG BufferLength)
{
    SEP_SD_PARTS Parts;
    ULONG Control = 0;
    ULONG DaclLength;
    NTSTATUS Status;
    ULONG Length;

    PAGED_CODE();

    RtlZeroMemory(&Parts, sizeof(Parts));

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
    {
        DaclLength = (ULONG)(sizeof(ACL) + FIELD_OFFSET(KNOWN_ACE, SidStart) +
                             RtlLengthSid(SeWorldSid));

        Parts.Dacl = ExAllocatePoolWithTag(PagedPool, DaclLength, TAG_ACL);
        if (Parts.Dacl == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = RtlCreateAcl(Parts.Dacl, DaclLength, ACL_REVISION);
        if (NT_SUCCESS(Status))
        {
            Status = RtlAddAccessAllowedAce(Parts.Dacl,
                                            ACL_REVISION,
                                            GENERIC_ALL,
                                            SeWorldSid);
        }

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Parts.Dacl, TAG_ACL);
            return Status;
        }

        Parts.DaclPresent = TRUE;
        Control |= SE_DACL_PRESENT;
    }

    Length = *BufferLength;

    Status = SepWriteParts(SecurityDescriptor,
                           &Length,
                           (SECURITY_DESCRIPTOR_CONTROL)Control,
                           &Parts);

    *BufferLength = Length;

    if (Parts.Dacl != NULL) ExFreePoolWithTag(Parts.Dacl, TAG_ACL);

    return Status;
}

/* What a descriptor costs the quota it is charged against */
NTSTATUS
NTAPI
SeComputeQuotaInformationSize(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Out_ PULONG QuotaInfoSize)
{
    SEP_SD_PARTS Parts;
    PVOID Part[SEP_PART_COUNT];
    ULONG Size[SEP_PART_COUNT];
    ULONG Charged = 0;
    ULONG i;

    PAGED_CODE();

    SepReadParts(SecurityDescriptor, &Parts);
    SepMeasureParts(&Parts, Part, Size);

    for (i = 0; i < SEP_PART_COUNT; i++)
        Charged += ALIGN_UP(Size[i], ULONG);

    *QuotaInfoSize = Charged;
    return STATUS_SUCCESS;
}
