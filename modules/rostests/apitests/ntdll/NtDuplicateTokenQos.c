/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the security quality of service an object attribute carries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
HANDLE
OpenOwnToken(VOID)
{
    HANDLE Token = NULL;
    NTSTATUS Status;

    Status = NtOpenProcessToken(NtCurrentProcess(),
                                TOKEN_QUERY | TOKEN_DUPLICATE,
                                &Token);
    if (!NT_SUCCESS(Status))
    {
        skip("NtOpenProcessToken failed (Status 0x%08lx)\n", Status);
        return NULL;
    }

    return Token;
}

/* The level the captured service asks for is the level the duplicate gets */
static
VOID
TestLevelIsTaken(
    _In_ HANDLE Token)
{
    static const SECURITY_IMPERSONATION_LEVEL Levels[] =
    {
        SecurityAnonymous,
        SecurityIdentification,
        SecurityImpersonation,
        SecurityDelegation
    };
    SECURITY_QUALITY_OF_SERVICE Qos;
    OBJECT_ATTRIBUTES ObjectAttributes;
    SECURITY_IMPERSONATION_LEVEL Read;
    HANDLE Duplicate;
    NTSTATUS Status;
    ULONG Returned;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Levels); ++Index)
    {
        Qos.Length = sizeof(Qos);
        Qos.ImpersonationLevel = Levels[Index];
        Qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
        Qos.EffectiveOnly = FALSE;

        InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
        ObjectAttributes.SecurityQualityOfService = &Qos;

        Duplicate = NULL;
        Status = NtDuplicateToken(Token,
                                  TOKEN_QUERY,
                                  &ObjectAttributes,
                                  FALSE,
                                  TokenImpersonation,
                                  &Duplicate);
        ok(Status == STATUS_SUCCESS, "level %d: Status 0x%08lx\n",
           Levels[Index], Status);
        if (!NT_SUCCESS(Status)) continue;

        Read = SecurityAnonymous;
        Returned = 0;
        Status = NtQueryInformationToken(Duplicate,
                                         TokenImpersonationLevel,
                                         &Read,
                                         sizeof(Read),
                                         &Returned);
        ok(Status == STATUS_SUCCESS, "level %d: query Status 0x%08lx\n",
           Levels[Index], Status);
        if (NT_SUCCESS(Status))
        {
            ok(Read == Levels[Index], "level %d: read back %d\n",
               Levels[Index], Read);
        }

        NtClose(Duplicate);
    }
}

/* Object attributes that carry nothing leave the duplicate to the parameters */
static
VOID
TestAbsentServiceIsAllowed(
    _In_ HANDLE Token)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Duplicate = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);

    Status = NtDuplicateToken(Token,
                              TOKEN_QUERY,
                              &ObjectAttributes,
                              FALSE,
                              TokenPrimary,
                              &Duplicate);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) NtClose(Duplicate);

    /* No attributes at all is the same answer */
    Duplicate = NULL;
    Status = NtDuplicateToken(Token,
                              TOKEN_QUERY,
                              NULL,
                              FALSE,
                              TokenPrimary,
                              &Duplicate);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) NtClose(Duplicate);
}

/*
 * A service whose length is not its own size, or which cannot be read at all,
 * is refused rather than acted on.
 */
/* TODO: the exact status of each refusal is the documented capture contract,
 * not yet compared against Windows 11 */
static
VOID
TestRefusedService(
    _In_ HANDLE Token)
{
    static const ULONG Lengths[] = { 0, sizeof(SECURITY_QUALITY_OF_SERVICE) - 1,
                                     sizeof(SECURITY_QUALITY_OF_SERVICE) + 1 };
    SECURITY_QUALITY_OF_SERVICE Qos;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Duplicate;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Lengths); ++Index)
    {
        Qos.Length = Lengths[Index];
        Qos.ImpersonationLevel = SecurityImpersonation;
        Qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
        Qos.EffectiveOnly = FALSE;

        InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
        ObjectAttributes.SecurityQualityOfService = &Qos;

        Duplicate = NULL;
        Status = NtDuplicateToken(Token,
                                  TOKEN_QUERY,
                                  &ObjectAttributes,
                                  FALSE,
                                  TokenImpersonation,
                                  &Duplicate);
        ok(!NT_SUCCESS(Status), "length %lu: Status 0x%08lx\n",
           Lengths[Index], Status);
        trace("length %lu refused with 0x%08lx\n", Lengths[Index], Status);
        if (NT_SUCCESS(Status) && Duplicate != NULL) NtClose(Duplicate);
    }

    /* A pointer into nothing is read, so the attempt is what fails */
    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    ObjectAttributes.SecurityQualityOfService = (PVOID)(ULONG_PTR)0x10;

    Duplicate = NULL;
    Status = NtDuplicateToken(Token,
                              TOKEN_QUERY,
                              &ObjectAttributes,
                              FALSE,
                              TokenImpersonation,
                              &Duplicate);
    ok(!NT_SUCCESS(Status), "unreadable service: Status 0x%08lx\n", Status);
    trace("unreadable service refused with 0x%08lx\n", Status);
    if (NT_SUCCESS(Status) && Duplicate != NULL) NtClose(Duplicate);
}

START_TEST(NtDuplicateTokenQos)
{
    HANDLE Token = OpenOwnToken();

    if (Token == NULL) return;

    TestAbsentServiceIsAllowed(Token);
    TestLevelIsTaken(Token);
    TestRefusedService(Token);

    NtClose(Token);
}
