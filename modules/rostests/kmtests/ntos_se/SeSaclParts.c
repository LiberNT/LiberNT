/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User mode tests for the label, resource attribute and scoped policy parts of a SACL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

static SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
static SID_IDENTIFIER_AUTHORITY LabelAuthority = {SECURITY_MANDATORY_LABEL_AUTHORITY};
static SID_IDENTIFIER_AUTHORITY ScopeAuthority = {{0, 0, 0, 0, 0, 17}};

/* Appends an entry made of a header, a mask, a SID and an opaque tail */
static
NTSTATUS
AddRawAce(
    _Inout_ PACL Acl,
    _In_ UCHAR Type,
    _In_ UCHAR Flags,
    _In_ ACCESS_MASK Mask,
    _In_ PSID Sid,
    _In_ ULONG TailLength)
{
    UCHAR Buffer[128];
    PACE_HEADER Header = (PACE_HEADER)Buffer;
    ULONG SidLength = RtlLengthSid(Sid);
    ULONG Size = sizeof(ACE_HEADER) + sizeof(ACCESS_MASK) + SidLength + TailLength;

    if (Size > sizeof(Buffer)) return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Header->AceType = Type;
    Header->AceFlags = Flags;
    Header->AceSize = (USHORT)Size;
    *(PACCESS_MASK)(Header + 1) = Mask;
    RtlCopyMemory(Buffer + sizeof(ACE_HEADER) + sizeof(ACCESS_MASK), Sid, SidLength);

    return RtlAddAce(Acl, ACL_REVISION, MAXULONG, Header, Size);
}

/* A SACL with an audit entry, a label, a resource attribute and a scoped policy, in that order */
static
VOID
BuildFullSacl(
    _Out_writes_bytes_(Size) PACL Acl,
    _In_ ULONG Size,
    _In_ PSID World,
    _In_ PSID Low,
    _In_ PSID Scope,
    _In_ ACCESS_MASK AttributeMask)
{
    NTSTATUS Status;

    Status = RtlCreateAcl(Acl, Size, ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAuditAccessAceEx(Acl, ACL_REVISION, SUCCESSFUL_ACCESS_ACE_FLAG, GENERIC_ALL, World, TRUE, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddMandatoryAce(Acl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP,
                                SYSTEM_MANDATORY_LABEL_ACE_TYPE, Low);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddRawAce(Acl, SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE, 0, AttributeMask, World, 8);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddRawAce(Acl, SYSTEM_SCOPED_POLICY_ID_ACE_TYPE, 0, 0, Scope, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

/* Reads the SACL part asked for and tells which entry types came back, in order */
static
ULONG
QueryTypes(
    _In_ HANDLE Event,
    _In_ SECURITY_INFORMATION Information,
    _Out_writes_(4) PUCHAR Types,
    _Out_opt_ PACCESS_MASK AttributeMask)
{
    UCHAR Buffer[512];
    PSECURITY_DESCRIPTOR Descriptor = (PSECURITY_DESCRIPTOR)Buffer;
    BOOLEAN Present = FALSE, Defaulted = FALSE;
    PACE_HEADER Ace;
    PACL Sacl = NULL;
    ULONG Length;
    ULONG Index;
    NTSTATUS Status;

    RtlZeroMemory(Types, 4);
    if (AttributeMask) *AttributeMask = 0;

    Status = NtQuerySecurityObject(Event, Information, Descriptor, sizeof(Buffer), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return 0;

    Status = RtlGetSaclSecurityDescriptor(Descriptor, &Present, &Sacl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || !Present || Sacl == NULL) return 0;

    for (Index = 0; Index < Sacl->AceCount && Index < 4; Index++)
    {
        if (!NT_SUCCESS(RtlGetAce(Sacl, Index, (PVOID *)&Ace))) break;
        Types[Index] = Ace->AceType;
        if (AttributeMask && Ace->AceType == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE)
            *AttributeMask = *(PACCESS_MASK)(Ace + 1);
    }

    return Sacl->AceCount;
}

/* Sets the given SACL as the named part or parts */
static
NTSTATUS
SetPart(
    _In_ HANDLE Event,
    _In_ SECURITY_INFORMATION Information,
    _In_ PACL Sacl)
{
    SECURITY_DESCRIPTOR Descriptor;

    RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    RtlSetSaclSecurityDescriptor(&Descriptor, TRUE, Sacl, FALSE);
    return NtSetSecurityObject(Event, Information, &Descriptor);
}

/* Reads and sets each part of the event's SACL and checks what the rest keeps */
static
VOID
CheckParts(
    _In_ HANDLE Event,
    _In_ PSID World)
{
    ULONG OtherBuffer[64];
    PACL Other = (PACL)OtherBuffer;
    HANDLE Limited = NULL;
    ACCESS_MASK Mask;
    UCHAR Types[4];
    ULONG Count;
    NTSTATUS Status;

    /* Each part is read on its own, and the SACL as a whole holds them all */
    Count = QueryTypes(Event, ATTRIBUTE_SECURITY_INFORMATION, Types, &Mask);
    ok_eq_ulong(Count, 1UL);
    ok_eq_uint(Types[0], SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE);
    ok_eq_hex(Mask, 1UL);
    Count = QueryTypes(Event, SCOPE_SECURITY_INFORMATION, Types, NULL);
    ok_eq_ulong(Count, 1UL);
    ok_eq_uint(Types[0], SYSTEM_SCOPED_POLICY_ID_ACE_TYPE);
    Count = QueryTypes(Event, LABEL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION, Types, NULL);
    ok_eq_ulong(Count, 2UL);
    ok_eq_uint(Types[0], SYSTEM_MANDATORY_LABEL_ACE_TYPE);
    ok_eq_uint(Types[1], SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE);
    Count = QueryTypes(Event, SACL_SECURITY_INFORMATION, Types, NULL);
    ok_eq_ulong(Count, 4UL);

    /* Setting the resource attributes replaces them and keeps the rest */
    Status = RtlCreateAcl(Other, sizeof(OtherBuffer), ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddRawAce(Other, SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE, 0, 2, World, 8);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = SetPart(Event, ATTRIBUTE_SECURITY_INFORMATION, Other);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Count = QueryTypes(Event, SACL_SECURITY_INFORMATION, Types, &Mask);
    ok_eq_ulong(Count, 4UL);
    ok_eq_hex(Mask, 2UL);

    /* Setting the audit entries keeps the label, the resource attributes and the scoped policies */
    Status = RtlCreateAcl(Other, sizeof(OtherBuffer), ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAuditAccessAceEx(Other, ACL_REVISION, FAILED_ACCESS_ACE_FLAG, GENERIC_READ, World, FALSE, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = SetPart(Event, SACL_SECURITY_INFORMATION, Other);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Count = QueryTypes(Event, SACL_SECURITY_INFORMATION, Types, &Mask);
    ok_eq_ulong(Count, 4UL);
    ok_eq_hex(Mask, 2UL);
    Count = QueryTypes(Event, SCOPE_SECURITY_INFORMATION, Types, NULL);
    ok_eq_ulong(Count, 1UL);

    /* Setting an empty list of scoped policies drops them alone */
    Status = RtlCreateAcl(Other, sizeof(OtherBuffer), ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = SetPart(Event, SCOPE_SECURITY_INFORMATION, Other);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Count = QueryTypes(Event, SACL_SECURITY_INFORMATION, Types, NULL);
    ok_eq_ulong(Count, 3UL);
    Count = QueryTypes(Event, SCOPE_SECURITY_INFORMATION, Types, NULL);
    ok_eq_ulong(Count, 0UL);

    /* Resource attributes are set with WRITE_DAC, scoped policies only with ACCESS_SYSTEM_SECURITY */
    Status = NtDuplicateObject(NtCurrentProcess(), Event, NtCurrentProcess(), &Limited,
                               WRITE_DAC | READ_CONTROL, 0, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = SetPart(Limited, ATTRIBUTE_SECURITY_INFORMATION, Other);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = SetPart(Limited, SCOPE_SECURITY_INFORMATION, Other);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    NtClose(Limited);
}

START_TEST(SeSaclParts)
{
    ULONG WorldBuffer[4], LowBuffer[4], ScopeBuffer[4];
    ULONG AclBuffer[96];
    PSID World = (PSID)WorldBuffer, Low = (PSID)LowBuffer, Scope = (PSID)ScopeBuffer;
    PACL Full = (PACL)AclBuffer;
    SECURITY_DESCRIPTOR Descriptor;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Event = NULL;
    BOOLEAN WasEnabled;
    NTSTATUS Status;

    RtlInitializeSid(World, &WorldAuthority, 1);
    *RtlSubAuthoritySid(World, 0) = SECURITY_WORLD_RID;
    RtlInitializeSid(Low, &LabelAuthority, 1);
    *RtlSubAuthoritySid(Low, 0) = SECURITY_MANDATORY_LOW_RID;
    RtlInitializeSid(Scope, &ScopeAuthority, 1);
    *RtlSubAuthoritySid(Scope, 0) = 1;

    Status = RtlAdjustPrivilege(SE_SECURITY_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (skip(NT_SUCCESS(Status), "no security privilege (0x%lx)\n", Status)) return;

    BuildFullSacl(Full, sizeof(AclBuffer), World, Low, Scope, 1);
    RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    RtlSetDaclSecurityDescriptor(&Descriptor, FALSE, NULL, FALSE);
    RtlSetSaclSecurityDescriptor(&Descriptor, TRUE, Full, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, &Descriptor);
    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS | ACCESS_SYSTEM_SECURITY, &Attributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        CheckParts(Event, World);
        NtClose(Event);
    }

    if (!WasEnabled) RtlAdjustPrivilege(SE_SECURITY_PRIVILEGE, FALSE, FALSE, &WasEnabled);
}
