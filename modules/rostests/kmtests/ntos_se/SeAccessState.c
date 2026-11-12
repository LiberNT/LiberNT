/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel mode tests for what an access state says about its subject
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntifs.h>

static GENERIC_MAPPING TestMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_ALL
};

/* Whether an access state made now marks its subject as restricted */
static
VOID
CheckRestrictedFlag(
    _In_ PVOID AuxData,
    _In_ BOOLEAN Expected)
{
    ACCESS_STATE AccessState;
    NTSTATUS Status;

    RtlZeroMemory(&AccessState, sizeof(AccessState));

    Status = SeCreateAccessState(&AccessState, AuxData, READ_CONTROL, &TestMapping);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    ok_eq_bool(BooleanFlagOn(AccessState.Flags, TOKEN_IS_RESTRICTED), Expected);

    SeDeleteAccessState(&AccessState);
}

START_TEST(SeAccessState)
{
    SECURITY_SUBJECT_CONTEXT Subject;
    TOKEN_GROUPS Restricted;
    PACCESS_TOKEN Filtered = NULL;
    PVOID AuxData;
    NTSTATUS Status;

    /* The auxiliary data is opaque and has grown with each release */
    AuxData = ExAllocatePoolZero(PagedPool, 0xE0, 'SAmK');
    if (skip(AuxData != NULL, "no memory for the auxiliary data\n")) return;

    CheckRestrictedFlag(AuxData, FALSE);

    SeCaptureSubjectContext(&Subject);

    Restricted.GroupCount = 1;
    Restricted.Groups[0].Sid = SeExports->SeWorldSid;
    Restricted.Groups[0].Attributes = 0;

    Status = SeFilterToken(SeQuerySubjectContextToken(&Subject),
                           0,
                           NULL,
                           NULL,
                           &Restricted,
                           &Filtered);
    ok_eq_hex(Status, STATUS_SUCCESS);

    SeReleaseSubjectContext(&Subject);

    if (NT_SUCCESS(Status))
    {
        ok(SeTokenIsRestricted(Filtered), "the filtered token is not restricted\n");

        Status = PsImpersonateClient(PsGetCurrentThread(),
                                     Filtered,
                                     FALSE,
                                     FALSE,
                                     SecurityImpersonation);
        ok_eq_hex(Status, STATUS_SUCCESS);

        if (NT_SUCCESS(Status))
        {
            CheckRestrictedFlag(AuxData, TRUE);
            PsRevertToSelf();
        }

        ObDereferenceObject(Filtered);
    }

    ExFreePoolWithTag(AuxData, 'SAmK');
}
