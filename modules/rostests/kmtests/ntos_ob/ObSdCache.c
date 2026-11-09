/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for the object manager security descriptor cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_SD_TEST 'TdSK'

static
PACL
BuildWorldDacl(
    _In_ ACCESS_MASK AccessMask)
{
    PACL Dacl;
    ULONG DaclLength;
    NTSTATUS Status;

    DaclLength = sizeof(ACL) +
                 FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) +
                 RtlLengthSid(SeExports->SeWorldSid);

    Dacl = ExAllocatePoolWithTag(PagedPool, DaclLength, TAG_SD_TEST);
    if (Dacl == NULL) return NULL;

    Status = RtlCreateAcl(Dacl, DaclLength, ACL_REVISION);
    if (NT_SUCCESS(Status))
    {
        Status = RtlAddAccessAllowedAce(Dacl,
                                        ACL_REVISION,
                                        AccessMask,
                                        SeExports->SeWorldSid);
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Dacl, TAG_SD_TEST);
        return NULL;
    }

    return Dacl;
}

static
PSECURITY_DESCRIPTOR
MakeSelfRelativeSd(
    _In_ PACL Dacl)
{
    SECURITY_DESCRIPTOR Absolute;
    PSECURITY_DESCRIPTOR SelfRelative;
    NTSTATUS Status;
    ULONG Length;

    Status = RtlCreateSecurityDescriptor(&Absolute, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) return NULL;

    Status = RtlSetDaclSecurityDescriptor(&Absolute, TRUE, Dacl, FALSE);
    if (!NT_SUCCESS(Status)) return NULL;

    Status = RtlSetOwnerSecurityDescriptor(&Absolute,
                                           SeExports->SeLocalSystemSid,
                                           FALSE);
    if (!NT_SUCCESS(Status)) return NULL;

    Length = 0;
    Status = RtlAbsoluteToSelfRelativeSD(&Absolute, NULL, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL) return NULL;

    SelfRelative = ExAllocatePoolWithTag(PagedPool, Length, TAG_SD_TEST);
    if (SelfRelative == NULL) return NULL;

    Status = RtlAbsoluteToSelfRelativeSD(&Absolute, SelfRelative, &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(SelfRelative, TAG_SD_TEST);
        return NULL;
    }

    return SelfRelative;
}

static
PSECURITY_DESCRIPTOR
BuildSelfRelativeSd(
    _In_ ACCESS_MASK AccessMask)
{
    PSECURITY_DESCRIPTOR SelfRelative;
    PACL Dacl;

    Dacl = BuildWorldDacl(AccessMask);
    if (Dacl == NULL) return NULL;

    SelfRelative = MakeSelfRelativeSd(Dacl);
    ExFreePoolWithTag(Dacl, TAG_SD_TEST);

    return SelfRelative;
}

static
VOID
CheckSdCache(
    _In_ PSECURITY_DESCRIPTOR First,
    _In_ PSECURITY_DESCRIPTOR Second,
    _In_ PSECURITY_DESCRIPTOR Other)
{
    PSECURITY_DESCRIPTOR Cached = NULL, CachedAgain = NULL, CachedOther = NULL;
    NTSTATUS Status;
    ULONG Length;

    Length = RtlLengthSecurityDescriptor(First);
    ok_eq_ulong(RtlLengthSecurityDescriptor(Second), Length);
    ok_eq_size(RtlCompareMemory(First, Second, Length), Length);

    /* A descriptor is cached verbatim and stays self-relative */
    Status = ObLogSecurityDescriptor(First, &Cached, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Cached != NULL, "no cached descriptor\n");
    if (!NT_SUCCESS(Status) || Cached == NULL) return;

    ok_bool_true(RtlValidSecurityDescriptor(Cached), "cached SD valid");
    ok_eq_ulong(RtlLengthSecurityDescriptor(Cached), Length);
    ok_eq_size(RtlCompareMemory(Cached, First, Length), Length);
    ok((((PISECURITY_DESCRIPTOR)Cached)->Control & SE_SELF_RELATIVE) != 0,
       "cached SD is not self-relative\n");

    /* The cache is a cache: an equal descriptor resolves to the same entry */
    Status = ObLogSecurityDescriptor(Second, &CachedAgain, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(CachedAgain == Cached,
       "equal descriptors cached separately: %p != %p\n", CachedAgain, Cached);

    /* A different descriptor gets its own entry */
    Status = ObLogSecurityDescriptor(Other, &CachedOther, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(CachedOther != Cached,
       "different descriptors share an entry: %p\n", CachedOther);
    if (NT_SUCCESS(Status) && CachedOther != NULL)
    {
        ok_bool_true(RtlValidSecurityDescriptor(CachedOther), "other cached SD valid");
        ObDereferenceSecurityDescriptor(CachedOther, 1);
    }

    /* Extra references keep the entry alive and addressable */
    ObReferenceSecurityDescriptor(Cached, 2);
    ObDereferenceSecurityDescriptor(Cached, 1);
    ok_bool_true(RtlValidSecurityDescriptor(Cached), "cached SD valid after reference");
    ObDereferenceSecurityDescriptor(Cached, 1);

    if (CachedAgain != NULL)
        ObDereferenceSecurityDescriptor(CachedAgain, 1);
    ObDereferenceSecurityDescriptor(Cached, 1);

    /* That entry is gone now; logging the same descriptor caches it afresh */
    Cached = NULL;
    Status = ObLogSecurityDescriptor(First, &Cached, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Cached != NULL, "no cached descriptor after reuse\n");
    if (NT_SUCCESS(Status) && Cached != NULL)
    {
        ok_eq_size(RtlCompareMemory(Cached, First, Length), Length);
        ObDereferenceSecurityDescriptor(Cached, 1);
    }
}

START_TEST(ObSdCache)
{
    PSECURITY_DESCRIPTOR First, Second, Other;

    First = BuildSelfRelativeSd(GENERIC_READ);
    Second = BuildSelfRelativeSd(GENERIC_READ);
    Other = BuildSelfRelativeSd(GENERIC_WRITE);

    ok(First != NULL, "could not build first descriptor\n");
    ok(Second != NULL, "could not build second descriptor\n");
    ok(Other != NULL, "could not build third descriptor\n");

    if (First != NULL && Second != NULL && Other != NULL)
        CheckSdCache(First, Second, Other);

    if (First != NULL) ExFreePoolWithTag(First, TAG_SD_TEST);
    if (Second != NULL) ExFreePoolWithTag(Second, TAG_SD_TEST);
    if (Other != NULL) ExFreePoolWithTag(Other, TAG_SD_TEST);
}
