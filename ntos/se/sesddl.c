/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Security descriptors written as a string
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define SEP_SDDL_REVISION 1

/* One entry a string named, before anything is laid out for it */
typedef struct _SEP_SDDL_ACE
{
    UCHAR Type;
    UCHAR Flags;
    ACCESS_MASK Mask;
    PSID Sid;
    BOOLEAN OwnsSid;
    BOOLEAN IsSacl;
} SEP_SDDL_ACE, *PSEP_SDDL_ACE;

/* A two-letter name and the value it stands for */
typedef struct _SEP_SDDL_NAME
{
    WCHAR Name[2];
    ULONG Value;
} SEP_SDDL_NAME;

typedef struct _SEP_SDDL_SID_NAME
{
    WCHAR Name[2];
    PSID *Sid;
} SEP_SDDL_SID_NAME;

/* The identifiers a string may name instead of spelling out */
static const SEP_SDDL_SID_NAME SepSddlSidNames[] =
{
    {{L'W', L'D'}, &SeWorldSid},
    {{L'S', L'Y'}, &SeLocalSystemSid},
    {{L'B', L'A'}, &SeAliasAdminsSid},
    {{L'B', L'U'}, &SeAliasUsersSid},
    {{L'B', L'G'}, &SeAliasGuestsSid},
    {{L'P', L'U'}, &SeAliasPowerUsersSid},
    {{L'A', L'O'}, &SeAliasAccountOpsSid},
    {{L'S', L'O'}, &SeAliasSystemOpsSid},
    {{L'P', L'O'}, &SeAliasPrintOpsSid},
    {{L'B', L'O'}, &SeAliasBackupOpsSid},
    {{L'I', L'U'}, &SeInteractiveSid},
    {{L'S', L'U'}, &SeServiceSid},
    {{L'A', L'N'}, &SeAnonymousLogonSid},
    {{L'A', L'U'}, &SeAuthenticatedUserSid},
    {{L'R', L'C'}, &SeRestrictedCodeSid},
    {{L'L', L'S'}, &SeLocalServiceSid},
    {{L'N', L'S'}, &SeNetworkServiceSid},
    {{L'N', L'U'}, &SeNetworkSid},
    {{L'C', L'O'}, &SeCreatorOwnerSid},
    {{L'C', L'G'}, &SeCreatorGroupSid},
    {{L'P', L'S'}, &SePrincipalSelfSid},
    {{L'A', L'C'}, &SeAllAppPackagesSid},
    {{L'U', L'D'}, &SeUserModeDriversSid},
    {{L'L', L'W'}, &SeLowMandatorySid},
    {{L'M', L'E'}, &SeMediumMandatorySid},
    {{L'H', L'I'}, &SeHighMandatorySid},
    {{L'S', L'I'}, &SeSystemMandatorySid},
};

/* TODO: the directory service rights (CC, DC, LC, SW, RP, WP, DT, LO, CR) are
 * not named here, so a string using them is refused */
static const SEP_SDDL_NAME SepSddlRights[] =
{
    {{L'G', L'A'}, GENERIC_ALL},
    {{L'G', L'R'}, GENERIC_READ},
    {{L'G', L'W'}, GENERIC_WRITE},
    {{L'G', L'X'}, GENERIC_EXECUTE},
    {{L'R', L'C'}, READ_CONTROL},
    {{L'S', L'D'}, DELETE},
    {{L'W', L'D'}, WRITE_DAC},
    {{L'W', L'O'}, WRITE_OWNER},
    {{L'F', L'A'}, FILE_ALL_ACCESS},
    {{L'F', L'R'}, FILE_GENERIC_READ},
    {{L'F', L'W'}, FILE_GENERIC_WRITE},
    {{L'F', L'X'}, FILE_GENERIC_EXECUTE},
    {{L'K', L'A'}, KEY_ALL_ACCESS},
    {{L'K', L'R'}, KEY_READ},
    {{L'K', L'W'}, KEY_WRITE},
    {{L'K', L'X'}, KEY_EXECUTE},
    {{L'N', L'R'}, SYSTEM_MANDATORY_LABEL_NO_READ_UP},
    {{L'N', L'W'}, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP},
    {{L'N', L'X'}, SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP},
};

static const SEP_SDDL_NAME SepSddlAceFlags[] =
{
    {{L'C', L'I'}, CONTAINER_INHERIT_ACE},
    {{L'O', L'I'}, OBJECT_INHERIT_ACE},
    {{L'N', L'P'}, NO_PROPAGATE_INHERIT_ACE},
    {{L'I', L'O'}, INHERIT_ONLY_ACE},
    {{L'I', L'D'}, INHERITED_ACE},
    {{L'S', L'A'}, SUCCESSFUL_ACCESS_ACE_FLAG},
    {{L'F', L'A'}, FAILED_ACCESS_ACE_FLAG},
};

/* PRIVATE FUNCTIONS **********************************************************/

static
WCHAR
SepSddlUpcase(
    _In_ WCHAR Char)
{
    if (Char >= L'a' && Char <= L'z') return Char - L'a' + L'A';

    return Char;
}

static
BOOLEAN
SepSddlNamed(
    _In_ PCWSTR Cursor,
    _In_ const WCHAR *Name)
{
    return (SepSddlUpcase(Cursor[0]) == Name[0] &&
            SepSddlUpcase(Cursor[1]) == Name[1]);
}

/* Reads a number, decimal unless it says otherwise */
static
NTSTATUS
SepSddlReadUlong(
    _Inout_ PCWSTR *Cursor,
    _Out_ PULONG Value)
{
    PCWSTR Current = *Cursor;
    ULONG Result = 0;
    ULONG Base = 10;
    ULONG Digits = 0;

    *Value = 0;

    if (Current[0] == L'0' &&
        (SepSddlUpcase(Current[1]) == L'X'))
    {
        Base = 16;
        Current += 2;
    }

    for (;;)
    {
        WCHAR Char = SepSddlUpcase(*Current);
        ULONG Digit;

        if (Char >= L'0' && Char <= L'9') Digit = Char - L'0';
        else if (Base == 16 && Char >= L'A' && Char <= L'F') Digit = Char - L'A' + 10;
        else break;

        if (Result > (MAXULONG - Digit) / Base) return STATUS_INVALID_PARAMETER;

        Result = Result * Base + Digit;
        Digits++;
        Current++;
    }

    if (Digits == 0) return STATUS_INVALID_PARAMETER;

    *Cursor = Current;
    *Value = Result;

    return STATUS_SUCCESS;
}

/*
 * An identifier is either one of the names above, which stands for one the
 * system already holds, or it is spelled out and has to be built.
 */
static
NTSTATUS
SepSddlParseSid(
    _Inout_ PCWSTR *Cursor,
    _Out_ PSID *Sid,
    _Out_ PBOOLEAN Owned)
{
    PCWSTR Current = *Cursor;
    ULONG SubAuthority[SID_MAX_SUB_AUTHORITIES];
    SID_IDENTIFIER_AUTHORITY Authority;
    NTSTATUS Status;
    ULONG Revision;
    ULONG Value;
    ULONG Count = 0;
    UCHAR Index;
    PSID Built;

    *Sid = NULL;
    *Owned = FALSE;

    if (SepSddlUpcase(Current[0]) != L'S' || Current[1] != L'-')
    {
        for (Index = 0; Index < RTL_NUMBER_OF(SepSddlSidNames); Index++)
        {
            if (!SepSddlNamed(Current, SepSddlSidNames[Index].Name)) continue;

            *Sid = *SepSddlSidNames[Index].Sid;
            if (*Sid == NULL) return STATUS_INVALID_PARAMETER;

            *Cursor = Current + 2;
            return STATUS_SUCCESS;
        }

        return STATUS_INVALID_PARAMETER;
    }

    Current += 2;

    Status = SepSddlReadUlong(&Current, &Revision);
    if (!NT_SUCCESS(Status)) return Status;
    if (Revision != SID_REVISION) return STATUS_INVALID_PARAMETER;
    if (*Current != L'-') return STATUS_INVALID_PARAMETER;
    Current++;

    Status = SepSddlReadUlong(&Current, &Value);
    if (!NT_SUCCESS(Status)) return Status;

    /* The authority is the last six bytes of the identifier, most significant
     * first, and only its lowest four are ever written as a number */
    RtlZeroMemory(&Authority, sizeof(Authority));
    Authority.Value[2] = (UCHAR)(Value >> 24);
    Authority.Value[3] = (UCHAR)(Value >> 16);
    Authority.Value[4] = (UCHAR)(Value >> 8);
    Authority.Value[5] = (UCHAR)Value;

    while (*Current == L'-')
    {
        Current++;

        if (Count >= SID_MAX_SUB_AUTHORITIES) return STATUS_INVALID_PARAMETER;

        Status = SepSddlReadUlong(&Current, &SubAuthority[Count]);
        if (!NT_SUCCESS(Status)) return Status;

        Count++;
    }

    Built = ExAllocatePoolWithTag(PagedPool,
                                  RtlLengthRequiredSid(Count),
                                  TAG_SID);
    if (Built == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlInitializeSid(Built, &Authority, (UCHAR)Count);

    for (Index = 0; Index < Count; Index++)
        *RtlSubAuthoritySid(Built, Index) = SubAuthority[Index];

    *Sid = Built;
    *Owned = TRUE;
    *Cursor = Current;

    return STATUS_SUCCESS;
}

/* Rights are either named two letters at a time or written as one number */
static
NTSTATUS
SepSddlParseMask(
    _Inout_ PCWSTR *Cursor,
    _Out_ PACCESS_MASK Mask)
{
    PCWSTR Current = *Cursor;
    ACCESS_MASK Result = 0;
    NTSTATUS Status;
    ULONG Value;
    UCHAR Index;

    *Mask = 0;

    if (*Current >= L'0' && *Current <= L'9')
    {
        Status = SepSddlReadUlong(&Current, &Value);
        if (!NT_SUCCESS(Status)) return Status;

        *Cursor = Current;
        *Mask = Value;

        return STATUS_SUCCESS;
    }

    while (*Current != L';' && *Current != L')' && *Current != UNICODE_NULL)
    {
        BOOLEAN Known = FALSE;

        for (Index = 0; Index < RTL_NUMBER_OF(SepSddlRights); Index++)
        {
            if (!SepSddlNamed(Current, SepSddlRights[Index].Name)) continue;

            Result |= SepSddlRights[Index].Value;
            Current += 2;
            Known = TRUE;
            break;
        }

        if (!Known) return STATUS_INVALID_PARAMETER;
    }

    *Cursor = Current;
    *Mask = Result;

    return STATUS_SUCCESS;
}

/* One entry, as its six fields between brackets */
static
NTSTATUS
SepSddlParseAce(
    _Inout_ PCWSTR *Cursor,
    _In_ BOOLEAN IsSacl,
    _Out_ PSEP_SDDL_ACE Ace)
{
    PCWSTR Current = *Cursor;
    NTSTATUS Status;
    UCHAR Index;

    RtlZeroMemory(Ace, sizeof(*Ace));
    Ace->IsSacl = IsSacl;

    if (*Current != L'(') return STATUS_INVALID_PARAMETER;
    Current++;

    /* What the entry does */
    if (SepSddlNamed(Current, L"AU")) { Ace->Type = SYSTEM_AUDIT_ACE_TYPE; Current += 2; }
    else if (SepSddlNamed(Current, L"AL")) { Ace->Type = SYSTEM_ALARM_ACE_TYPE; Current += 2; }
    else if (SepSddlNamed(Current, L"ML")) { Ace->Type = SYSTEM_MANDATORY_LABEL_ACE_TYPE; Current += 2; }
    else if (SepSddlUpcase(Current[0]) == L'A' && Current[1] == L';') { Ace->Type = ACCESS_ALLOWED_ACE_TYPE; Current++; }
    else if (SepSddlUpcase(Current[0]) == L'D' && Current[1] == L';') { Ace->Type = ACCESS_DENIED_ACE_TYPE; Current++; }
    else
    {
        /* TODO: the entries that name an object type (OA, OD, OU, OL) are not
         * built here, so a string using them is refused */
        return STATUS_NOT_IMPLEMENTED;
    }

    if (*Current != L';') return STATUS_INVALID_PARAMETER;
    Current++;

    /* How it is handed down */
    while (*Current != L';')
    {
        BOOLEAN Known = FALSE;

        if (*Current == UNICODE_NULL) return STATUS_INVALID_PARAMETER;

        for (Index = 0; Index < RTL_NUMBER_OF(SepSddlAceFlags); Index++)
        {
            if (!SepSddlNamed(Current, SepSddlAceFlags[Index].Name)) continue;

            Ace->Flags |= (UCHAR)SepSddlAceFlags[Index].Value;
            Current += 2;
            Known = TRUE;
            break;
        }

        if (!Known) return STATUS_INVALID_PARAMETER;
    }
    Current++;

    Status = SepSddlParseMask(&Current, &Ace->Mask);
    if (!NT_SUCCESS(Status)) return Status;

    if (*Current != L';') return STATUS_INVALID_PARAMETER;
    Current++;

    /* The two object type names, which have to be absent here */
    for (Index = 0; Index < 2; Index++)
    {
        if (*Current != L';') return STATUS_NOT_IMPLEMENTED;
        Current++;
    }

    Status = SepSddlParseSid(&Current, &Ace->Sid, &Ace->OwnsSid);
    if (!NT_SUCCESS(Status)) return Status;

    if (*Current != L')') return STATUS_INVALID_PARAMETER;
    Current++;

    *Cursor = Current;

    return STATUS_SUCCESS;
}

/*
 * A list is a few flags and then its entries. "NO_ACCESS_CONTROL" names no
 * list at all, which is not the same as an empty one: it allows everything.
 */
static
NTSTATUS
SepSddlParseAcl(
    _Inout_ PCWSTR *Cursor,
    _In_ BOOLEAN IsSacl,
    _Out_writes_to_(MaxAces, *Count) PSEP_SDDL_ACE Aces,
    _In_ ULONG MaxAces,
    _Out_ PULONG Count,
    _Out_ PUSHORT Control,
    _Out_ PBOOLEAN Present)
{
    PCWSTR Current = *Cursor;
    NTSTATUS Status;

    *Count = 0;
    *Control = 0;
    *Present = TRUE;

    if (_wcsnicmp(Current, L"NO_ACCESS_CONTROL", 17) == 0)
    {
        *Cursor = Current + 17;
        *Present = FALSE;
        return STATUS_SUCCESS;
    }

    for (;;)
    {
        if (SepSddlNamed(Current, L"AR"))
        {
            *Control |= IsSacl ? SE_SACL_AUTO_INHERIT_REQ : SE_DACL_AUTO_INHERIT_REQ;
            Current += 2;
        }
        else if (SepSddlNamed(Current, L"AI"))
        {
            *Control |= IsSacl ? SE_SACL_AUTO_INHERITED : SE_DACL_AUTO_INHERITED;
            Current += 2;
        }
        else if (SepSddlUpcase(Current[0]) == L'P')
        {
            *Control |= IsSacl ? SE_SACL_PROTECTED : SE_DACL_PROTECTED;
            Current++;
        }
        else
        {
            break;
        }
    }

    while (*Current == L'(')
    {
        if (*Count >= MaxAces) return STATUS_INVALID_PARAMETER;

        Status = SepSddlParseAce(&Current, IsSacl, &Aces[*Count]);
        if (!NT_SUCCESS(Status)) return Status;

        (*Count)++;
    }

    *Cursor = Current;

    return STATUS_SUCCESS;
}

/* What one list takes once it is laid out */
static
ULONG
SepSddlAclLength(
    _In_ PSEP_SDDL_ACE Aces,
    _In_ ULONG Count,
    _In_ BOOLEAN IsSacl)
{
    ULONG Length = sizeof(ACL);
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Aces[Index].IsSacl != IsSacl) continue;

        Length += (ULONG)(FIELD_OFFSET(KNOWN_ACE, SidStart) +
                          RtlLengthSid(Aces[Index].Sid));
    }

    return Length;
}

/* Writes one list out where it belongs */
static
VOID
SepSddlWriteAcl(
    _Out_ PACL Acl,
    _In_ ULONG Length,
    _In_ PSEP_SDDL_ACE Aces,
    _In_ ULONG Count,
    _In_ BOOLEAN IsSacl)
{
    PUCHAR Cursor = (PUCHAR)Acl + sizeof(ACL);
    ULONG Index;

    Acl->AclRevision = ACL_REVISION;
    Acl->Sbz1 = 0;
    Acl->AclSize = (USHORT)Length;
    Acl->AceCount = 0;
    Acl->Sbz2 = 0;

    for (Index = 0; Index < Count; Index++)
    {
        PKNOWN_ACE Ace = (PKNOWN_ACE)Cursor;
        ULONG SidLength;

        if (Aces[Index].IsSacl != IsSacl) continue;

        SidLength = (ULONG)RtlLengthSid(Aces[Index].Sid);

        Ace->Header.AceType = Aces[Index].Type;
        Ace->Header.AceFlags = Aces[Index].Flags;
        Ace->Header.AceSize =
            (USHORT)(FIELD_OFFSET(KNOWN_ACE, SidStart) + SidLength);
        Ace->Mask = Aces[Index].Mask;

        RtlCopyMemory(&Ace->SidStart, Aces[Index].Sid, SidLength);

        Cursor += Ace->Header.AceSize;
        Acl->AceCount++;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Reads a descriptor written as a string and lays it out as one, in the
 * self-relative form, so what comes back is a single block the caller frees.
 */
NTSTATUS
NTAPI
SeConvertStringSecurityDescriptorToSecurityDescriptor(
    _In_ PCWSTR StringSecurityDescriptor,
    _In_ ULONG StringSecurityDescriptorRevision,
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_opt_ PULONG SecurityDescriptorSize)
{
    PISECURITY_DESCRIPTOR_RELATIVE Relative = NULL;
    PCWSTR Cursor = StringSecurityDescriptor;
    PSEP_SDDL_ACE Aces = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN DaclPresent = FALSE;
    BOOLEAN SaclPresent = FALSE;
    BOOLEAN OwnerOwned = FALSE;
    BOOLEAN GroupOwned = FALSE;
    PSID Owner = NULL;
    PSID Group = NULL;
    USHORT Control = SE_SELF_RELATIVE;
    USHORT AclControl;
    ULONG DaclCount = 0;
    ULONG SaclCount = 0;
    ULONG MaxAces = 1;
    ULONG DaclLength = 0;
    ULONG SaclLength = 0;
    ULONG OwnerLength = 0;
    ULONG GroupLength = 0;
    ULONG Offset;
    ULONG Total;
    ULONG Index;
    BOOLEAN Present;

    PAGED_CODE();

    *SecurityDescriptor = NULL;
    if (SecurityDescriptorSize != NULL) *SecurityDescriptorSize = 0;

    if (StringSecurityDescriptorRevision != SEP_SDDL_REVISION)
        return STATUS_UNKNOWN_REVISION;

    /* Every entry opens a bracket, so that is how many there can be */
    for (Index = 0; StringSecurityDescriptor[Index] != UNICODE_NULL; Index++)
    {
        if (StringSecurityDescriptor[Index] == L'(') MaxAces++;
    }

    Aces = ExAllocatePoolWithTag(PagedPool,
                                 (SIZE_T)MaxAces * sizeof(SEP_SDDL_ACE),
                                 TAG_SD);
    if (Aces == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Aces, (SIZE_T)MaxAces * sizeof(SEP_SDDL_ACE));

    while (NT_SUCCESS(Status) && *Cursor != UNICODE_NULL)
    {
        WCHAR Part = SepSddlUpcase(Cursor[0]);

        if (Cursor[1] != L':')
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Cursor += 2;

        switch (Part)
        {
            case L'O':
                Status = SepSddlParseSid(&Cursor, &Owner, &OwnerOwned);
                break;

            case L'G':
                Status = SepSddlParseSid(&Cursor, &Group, &GroupOwned);
                break;

            case L'D':
                Status = SepSddlParseAcl(&Cursor, FALSE, Aces, MaxAces,
                                         &DaclCount, &AclControl, &Present);
                if (!NT_SUCCESS(Status)) break;
                Control |= AclControl;
                DaclPresent = Present;
                break;

            case L'S':
                Status = SepSddlParseAcl(&Cursor, TRUE, &Aces[DaclCount],
                                         MaxAces - DaclCount,
                                         &SaclCount, &AclControl, &Present);
                if (!NT_SUCCESS(Status)) break;
                Control |= AclControl;
                SaclPresent = Present;
                break;

            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        Total = sizeof(SECURITY_DESCRIPTOR_RELATIVE);

        if (Owner != NULL)
        {
            OwnerLength = (ULONG)RtlLengthSid(Owner);
            Total += OwnerLength;
        }

        if (Group != NULL)
        {
            GroupLength = (ULONG)RtlLengthSid(Group);
            Total += GroupLength;
        }

        if (DaclPresent)
        {
            Control |= SE_DACL_PRESENT;
            DaclLength = SepSddlAclLength(Aces, DaclCount + SaclCount, FALSE);
            Total += DaclLength;
        }

        if (SaclPresent)
        {
            Control |= SE_SACL_PRESENT;
            SaclLength = SepSddlAclLength(Aces, DaclCount + SaclCount, TRUE);
            Total += SaclLength;
        }

        Relative = ExAllocatePoolWithTag(PagedPool, Total, TAG_SD);
        if (Relative == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(Relative, Total);

        Relative->Revision = SECURITY_DESCRIPTOR_REVISION;
        Relative->Sbz1 = 0;
        Relative->Control = Control;

        Offset = sizeof(SECURITY_DESCRIPTOR_RELATIVE);

        if (SaclPresent)
        {
            SepSddlWriteAcl((PACL)((PUCHAR)Relative + Offset),
                            SaclLength,
                            Aces,
                            DaclCount + SaclCount,
                            TRUE);
            Relative->Sacl = Offset;
            Offset += SaclLength;
        }

        if (DaclPresent)
        {
            SepSddlWriteAcl((PACL)((PUCHAR)Relative + Offset),
                            DaclLength,
                            Aces,
                            DaclCount + SaclCount,
                            FALSE);
            Relative->Dacl = Offset;
            Offset += DaclLength;
        }

        if (Owner != NULL)
        {
            RtlCopyMemory((PUCHAR)Relative + Offset, Owner, OwnerLength);
            Relative->Owner = Offset;
            Offset += OwnerLength;
        }

        if (Group != NULL)
        {
            RtlCopyMemory((PUCHAR)Relative + Offset, Group, GroupLength);
            Relative->Group = Offset;
        }

        *SecurityDescriptor = Relative;
        if (SecurityDescriptorSize != NULL) *SecurityDescriptorSize = Total;
    }

    /* Only what was spelled out was allocated; a name points at what the
     * system already holds */
    for (Index = 0; Index < DaclCount + SaclCount; Index++)
    {
        if (Aces[Index].OwnsSid) ExFreePoolWithTag(Aces[Index].Sid, TAG_SID);
    }

    if (OwnerOwned) ExFreePoolWithTag(Owner, TAG_SID);
    if (GroupOwned) ExFreePoolWithTag(Group, TAG_SID);

    ExFreePoolWithTag(Aces, TAG_SD);

    return Status;
}

/* EOF */
