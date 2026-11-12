/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel mode tests for descriptors written as a string
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntifs.h>

NTSTATUS
NTAPI
SeConvertStringSecurityDescriptorToSecurityDescriptor(
    _In_ PCWSTR StringSecurityDescriptor,
    _In_ ULONG StringSecurityDescriptorRevision,
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_opt_ PULONG SecurityDescriptorSize);

static
VOID
CheckAce(
    _In_ PACL Acl,
    _In_ ULONG Index,
    _In_ UCHAR ExpectedType,
    _In_ UCHAR ExpectedFlags,
    _In_ ACCESS_MASK ExpectedMask,
    _In_ PSID ExpectedSid)
{
    PACCESS_ALLOWED_ACE Ace = NULL;
    NTSTATUS Status;

    Status = RtlGetAce(Acl, Index, (PVOID *)&Ace);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || Ace == NULL) return;

    ok_eq_uint(Ace->Header.AceType, ExpectedType);
    ok_eq_uint(Ace->Header.AceFlags, ExpectedFlags);
    ok_eq_hex(Ace->Mask, ExpectedMask);
    ok(RtlEqualSid((PSID)&Ace->SidStart, ExpectedSid),
       "ace %lu names another identifier\n", Index);
}

/* The shape a driver's own string takes */
static
VOID
TestDeviceString(VOID)
{
    PISECURITY_DESCRIPTOR_RELATIVE Sd = NULL;
    BOOLEAN Present, Defaulted;
    NTSTATUS Status;
    ULONG Size = 0;
    PACL Dacl;

    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"D:P(A;;GA;;;SY)(A;;GRGX;;;BA)", 1, (PVOID *)&Sd, &Size);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    ok(Size != 0, "no size reported\n");

    /* What comes back is one block, so it says so */
    ok(BooleanFlagOn(Sd->Control, SE_SELF_RELATIVE), "not self-relative\n");
    ok(BooleanFlagOn(Sd->Control, SE_DACL_PRESENT), "no dacl present\n");
    ok(BooleanFlagOn(Sd->Control, SE_DACL_PROTECTED), "dacl not protected\n");
    ok(!BooleanFlagOn(Sd->Control, SE_SACL_PRESENT), "a sacl appeared\n");
    ok_eq_uint(Sd->Revision, SECURITY_DESCRIPTOR_REVISION);
    ok_eq_ulong(Sd->Owner, 0UL);
    ok_eq_ulong(Sd->Group, 0UL);

    Dacl = NULL;
    Status = RtlGetDaclSecurityDescriptor(Sd, &Present, &Dacl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Present, "dacl not reported\n");

    if (Dacl != NULL)
    {
        ok_eq_uint(Dacl->AceCount, 2);
        CheckAce(Dacl, 0, ACCESS_ALLOWED_ACE_TYPE, 0, GENERIC_ALL,
                 SeExports->SeLocalSystemSid);
        CheckAce(Dacl, 1, ACCESS_ALLOWED_ACE_TYPE, 0,
                 GENERIC_READ | GENERIC_EXECUTE,
                 SeExports->SeAliasAdminsSid);
    }

    ExFreePool(Sd);
}

/* An owner, a group, how an entry is handed down, and a spelled-out identifier */
static
VOID
TestFullString(VOID)
{
    PISECURITY_DESCRIPTOR_RELATIVE Sd = NULL;
    BOOLEAN Present, Defaulted;
    NTSTATUS Status;
    PSID Owner, Group;
    PACL Acl;

    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"O:BAG:SYD:(A;CIOI;0x1f01ff;;;S-1-5-32-544)"
                 L"S:(AU;SAFA;GR;;;WD)", 1, (PVOID *)&Sd, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    ok(BooleanFlagOn(Sd->Control, SE_DACL_PRESENT), "no dacl\n");
    ok(BooleanFlagOn(Sd->Control, SE_SACL_PRESENT), "no sacl\n");
    ok(!BooleanFlagOn(Sd->Control, SE_DACL_PROTECTED), "dacl protected\n");

    Owner = NULL;
    Status = RtlGetOwnerSecurityDescriptor(Sd, &Owner, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Owner != NULL)
    {
        ok(RtlEqualSid(Owner, SeExports->SeAliasAdminsSid),
           "the owner is somebody else\n");
    }

    Group = NULL;
    Status = RtlGetGroupSecurityDescriptor(Sd, &Group, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Group != NULL)
    {
        ok(RtlEqualSid(Group, SeExports->SeLocalSystemSid),
           "the group is somebody else\n");
    }

    Acl = NULL;
    Status = RtlGetDaclSecurityDescriptor(Sd, &Present, &Acl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Acl != NULL)
    {
        ok_eq_uint(Acl->AceCount, 1);

        /* The identifier was spelled out, and names what BA does */
        CheckAce(Acl, 0, ACCESS_ALLOWED_ACE_TYPE,
                 CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE,
                 0x1f01ff, SeExports->SeAliasAdminsSid);
    }

    Acl = NULL;
    Status = RtlGetSaclSecurityDescriptor(Sd, &Present, &Acl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Present, "sacl not reported\n");
    if (Acl != NULL)
    {
        ok_eq_uint(Acl->AceCount, 1);
        CheckAce(Acl, 0, SYSTEM_AUDIT_ACE_TYPE,
                 SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG,
                 GENERIC_READ, SeExports->SeWorldSid);
    }

    ExFreePool(Sd);
}

/* Strings that name nothing a descriptor can be made of */
static
VOID
TestRefusedStrings(VOID)
{
    static PCWSTR Bad[] =
    {
        L"",                       /* nothing at all is nothing to build */
        L"Z:(A;;GA;;;SY)",         /* no such part */
        L"D(A;;GA;;;SY)",          /* a part has to be followed by a colon */
        L"D:(A;;GA;;;ZZ)",         /* no such identifier */
        L"D:(A;;ZZ;;;SY)",         /* no such right */
        L"D:(A;ZZ;GA;;;SY)",       /* no such way of handing it down */
        L"D:(A;;GA;;;SY",          /* the entry never closes */
        L"D:(A;;GA;;;S-2-5-18)",   /* only one revision is written this way */
    };
    PSECURITY_DESCRIPTOR Sd;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Bad); Index++)
    {
        Sd = NULL;
        Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                     Bad[Index], 1, &Sd, NULL);

        /* An empty string names no part, so it builds an empty descriptor */
        if (Index == 0)
        {
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status)) ExFreePool(Sd);
            continue;
        }

        ok(!NT_SUCCESS(Status), "string %lu was accepted (0x%08lx)\n",
           Index, Status);
        if (NT_SUCCESS(Status) && Sd != NULL) ExFreePool(Sd);
    }

    /* Only the one revision is understood */
    Sd = NULL;
    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"D:(A;;GA;;;SY)", 2, &Sd, NULL);
    ok_eq_hex(Status, NTSTATUS_FROM_WIN32(1305));
    if (NT_SUCCESS(Status) && Sd != NULL) ExFreePool(Sd);
}

/* A list that is absent allows everything, which an empty one does not */
static
VOID
TestNoAccessControl(VOID)
{
    PISECURITY_DESCRIPTOR_RELATIVE Sd = NULL;
    NTSTATUS Status;

    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"D:NO_ACCESS_CONTROL", 1, (PVOID *)&Sd, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    ok(!BooleanFlagOn(Sd->Control, SE_DACL_PRESENT),
       "a dacl was built for a string that names none\n");
    ok_eq_ulong(Sd->Dacl, 0UL);

    ExFreePool(Sd);
}

/* The rights a directory object answers for, and the entries naming its parts */
static
VOID
TestObjectEntries(VOID)
{
    static const GUID Property =
        {0xbf967aba, 0x0de6, 0x11d0, {0xa2, 0x85, 0x00, 0xaa, 0x00, 0x30, 0x49, 0xe2}};
    static const GUID Class =
        {0xbf967a86, 0x0de6, 0x11d0, {0xa2, 0x85, 0x00, 0xaa, 0x00, 0x30, 0x49, 0xe2}};
    PISECURITY_DESCRIPTOR_RELATIVE Sd = NULL;
    PACCESS_ALLOWED_OBJECT_ACE Ace;
    BOOLEAN Present, Defaulted;
    NTSTATUS Status;
    PACL Acl;

    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"D:(A;;CCDCLCSWRPWPDTLOCR;;;SY)"
                 L"(OA;CI;RP;bf967aba-0de6-11d0-a285-00aa003049e2;"
                 L"bf967a86-0de6-11d0-a285-00aa003049e2;BA)"
                 L"(OD;;WP;;bf967a86-0de6-11d0-a285-00aa003049e2;WD)"
                 L"S:(OU;SA;CR;bf967aba-0de6-11d0-a285-00aa003049e2;;WD)",
                 1, (PVOID *)&Sd, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Acl = NULL;
    Status = RtlGetDaclSecurityDescriptor(Sd, &Present, &Acl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Acl != NULL)
    {
        ok_eq_uint(Acl->AceCount, 3);

        /* The nine directory rights, CC through CR, are the low nine bits */
        CheckAce(Acl, 0, ACCESS_ALLOWED_ACE_TYPE, 0, 0x1ff,
                 SeExports->SeLocalSystemSid);

        Ace = NULL;
        Status = RtlGetAce(Acl, 1, (PVOID *)&Ace);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Ace != NULL)
        {
            ok_eq_uint(Ace->Header.AceType, ACCESS_ALLOWED_OBJECT_ACE_TYPE);
            ok_eq_uint(Ace->Header.AceFlags, CONTAINER_INHERIT_ACE);
            ok_eq_hex(Ace->Mask, 0x10UL);
            ok_eq_hex(Ace->Flags, (ULONG)(ACE_OBJECT_TYPE_PRESENT |
                                          ACE_INHERITED_OBJECT_TYPE_PRESENT));
            ok(RtlEqualMemory(&Ace->ObjectType, &Property, sizeof(GUID)),
               "the object type is another one\n");
            ok(RtlEqualMemory(&Ace->InheritedObjectType, &Class, sizeof(GUID)),
               "the inherited object type is another one\n");
            ok(RtlEqualSid((PSID)&Ace->SidStart, SeExports->SeAliasAdminsSid),
               "the entry names another identifier\n");
            ok_eq_uint(Ace->Header.AceSize,
                       FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, SidStart) +
                       RtlLengthSid(SeExports->SeAliasAdminsSid));
        }

        /* A type that is left out leaves no room behind it */
        Ace = NULL;
        Status = RtlGetAce(Acl, 2, (PVOID *)&Ace);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Ace != NULL)
        {
            ok_eq_uint(Ace->Header.AceType, ACCESS_DENIED_OBJECT_ACE_TYPE);
            ok_eq_uint(Ace->Header.AceFlags, 0);
            ok_eq_hex(Ace->Mask, 0x20UL);
            ok_eq_hex(Ace->Flags, (ULONG)ACE_INHERITED_OBJECT_TYPE_PRESENT);
            ok(RtlEqualMemory(&Ace->ObjectType, &Class, sizeof(GUID)),
               "the inherited object type is not where the first type goes\n");
            ok(RtlEqualSid((PSID)&Ace->InheritedObjectType, SeExports->SeWorldSid),
               "the identifier does not follow the one type given\n");
            ok_eq_uint(Ace->Header.AceSize,
                       FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, InheritedObjectType) +
                       RtlLengthSid(SeExports->SeWorldSid));
        }
    }

    Acl = NULL;
    Status = RtlGetSaclSecurityDescriptor(Sd, &Present, &Acl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Acl != NULL)
    {
        ok_eq_uint(Acl->AceCount, 1);

        Ace = NULL;
        Status = RtlGetAce(Acl, 0, (PVOID *)&Ace);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Ace != NULL)
        {
            ok_eq_uint(Ace->Header.AceType, SYSTEM_AUDIT_OBJECT_ACE_TYPE);
            ok_eq_uint(Ace->Header.AceFlags, SUCCESSFUL_ACCESS_ACE_FLAG);
            ok_eq_hex(Ace->Mask, 0x100UL);
            ok_eq_hex(Ace->Flags, (ULONG)ACE_OBJECT_TYPE_PRESENT);
            ok(RtlEqualMemory(&Ace->ObjectType, &Property, sizeof(GUID)),
               "the object type is another one\n");
            ok(RtlEqualSid((PSID)&Ace->InheritedObjectType, SeExports->SeWorldSid),
               "the identifier does not follow the one type given\n");
        }
    }

    ExFreePool(Sd);

    /* Only an entry that names an object type may carry one, and only whole */
    Sd = NULL;
    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"D:(A;;GA;bf967aba-0de6-11d0-a285-00aa003049e2;;SY)",
                 1, (PVOID *)&Sd, NULL);
    ok(!NT_SUCCESS(Status), "a plain entry took an object type (0x%08lx)\n", Status);
    if (NT_SUCCESS(Status) && Sd != NULL) ExFreePool(Sd);

    Sd = NULL;
    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"D:(OA;;RP;bf967aba-0de6-11d0-a285;;SY)", 1, (PVOID *)&Sd, NULL);
    ok(!NT_SUCCESS(Status), "a broken object type was taken (0x%08lx)\n", Status);
    if (NT_SUCCESS(Status) && Sd != NULL) ExFreePool(Sd);
}

/* A string may name its lists in either order */
static
VOID
TestListOrder(VOID)
{
    PISECURITY_DESCRIPTOR_RELATIVE Sd = NULL;
    BOOLEAN Present, Defaulted;
    NTSTATUS Status;
    PACL Acl;

    Status = SeConvertStringSecurityDescriptorToSecurityDescriptor(
                 L"S:(AU;FA;GW;;;WD)D:(A;;GA;;;SY)(D;;WD;;;BG)",
                 1, (PVOID *)&Sd, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Acl = NULL;
    Status = RtlGetSaclSecurityDescriptor(Sd, &Present, &Acl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Present, "sacl not reported\n");
    if (Acl != NULL)
    {
        ok_eq_uint(Acl->AceCount, 1);
        CheckAce(Acl, 0, SYSTEM_AUDIT_ACE_TYPE, FAILED_ACCESS_ACE_FLAG,
                 GENERIC_WRITE, SeExports->SeWorldSid);
    }

    Acl = NULL;
    Status = RtlGetDaclSecurityDescriptor(Sd, &Present, &Acl, &Defaulted);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Present, "dacl not reported\n");
    if (Acl != NULL)
    {
        ok_eq_uint(Acl->AceCount, 2);
        CheckAce(Acl, 0, ACCESS_ALLOWED_ACE_TYPE, 0, GENERIC_ALL,
                 SeExports->SeLocalSystemSid);
        CheckAce(Acl, 1, ACCESS_DENIED_ACE_TYPE, 0, WRITE_DAC,
                 SeExports->SeAliasGuestsSid);
    }

    ExFreePool(Sd);
}

START_TEST(SeSddl)
{
    TestDeviceString();
    TestFullString();
    TestNoAccessControl();
    TestRefusedStrings();
    TestObjectEntries();
}

START_TEST(SeSddlListOrder)
{
    TestListOrder();
}
