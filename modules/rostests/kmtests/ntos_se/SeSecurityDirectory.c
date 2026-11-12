/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User mode tests for the objects the security manager publishes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/sefuncs.h>

static SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};

/* A copy of the caller's token that may only do what everybody may do */
static
HANDLE
MakeEverybodyToken(VOID)
{
    SECURITY_QUALITY_OF_SERVICE Qos;
    OBJECT_ATTRIBUTES Attributes;
    TOKEN_GROUPS Restrict;
    HANDLE Process = NULL;
    HANDLE Filtered = NULL;
    HANDLE Impersonation = NULL;
    PSID World = NULL;
    NTSTATUS Status;

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &Process);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return NULL;

    Status = RtlAllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID,
                                         0, 0, 0, 0, 0, 0, 0, &World);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        Restrict.GroupCount = 1;
        Restrict.Groups[0].Sid = World;
        Restrict.Groups[0].Attributes = 0;
        Status = NtFilterToken(Process, 0, NULL, NULL, &Restrict, &Filtered);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    if (NT_SUCCESS(Status))
    {
        Qos.Length = sizeof(Qos);
        Qos.ImpersonationLevel = SecurityImpersonation;
        Qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
        Qos.EffectiveOnly = FALSE;
        InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
        Attributes.SecurityQualityOfService = &Qos;

        Status = NtDuplicateToken(Filtered, TOKEN_IMPERSONATE | TOKEN_QUERY, &Attributes,
                                  FALSE, TokenImpersonation, &Impersonation);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    if (Filtered != NULL) NtClose(Filtered);
    if (World != NULL) RtlFreeSid(World);
    NtClose(Process);

    return NT_SUCCESS(Status) ? Impersonation : NULL;
}

START_TEST(SeSecurityDirectory)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Security\\LSA_AUTHENTICATION_INITIALIZED");
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Token;
    HANDLE Event = NULL;
    HANDLE None = NULL;
    NTSTATUS Status;

    Token = MakeEverybodyToken();
    if (skip(Token != NULL, "no token restricted to everybody\n")) return;

    Status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &Token, sizeof(Token));
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* Any account may wait for the authority to be ready, as every LSA client does */
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenEvent(&Event, SYNCHRONIZE, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) NtClose(Event);

    NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &None, sizeof(None));
    NtClose(Token);
}
