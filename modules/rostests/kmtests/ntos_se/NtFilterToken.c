/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User mode tests for cutting a token down through the system call
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/psfuncs.h>
#include <ndk/sefuncs.h>

static SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};

/* Whether the token holds the identifier only to be denied by it */
static
VOID
CheckDenyOnly(
    _In_ HANDLE Token,
    _In_ PSID Sid)
{
    PTOKEN_GROUPS Groups;
    ULONG Length = 0;
    NTSTATUS Status;
    ULONG Index;

    Status = NtQueryInformationToken(Token, TokenGroups, NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    if (Length == 0) return;

    Groups = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    if (Groups == NULL) return;

    Status = NtQueryInformationToken(Token, TokenGroups, Groups, Length, &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Groups->GroupCount; Index++)
        {
            if (RtlEqualSid(Groups->Groups[Index].Sid, Sid)) break;
        }

        ok(Index < Groups->GroupCount, "the group disappeared\n");
        if (Index < Groups->GroupCount)
        {
            ok(Groups->Groups[Index].Attributes & SE_GROUP_USE_FOR_DENY_ONLY,
               "attributes 0x%lx: not deny-only\n", Groups->Groups[Index].Attributes);
            ok(!(Groups->Groups[Index].Attributes & SE_GROUP_ENABLED),
               "attributes 0x%lx: still enabled\n", Groups->Groups[Index].Attributes);
        }
    }

    HeapFree(GetProcessHeap(), 0, Groups);
}

/* A token says whether it was ever filtered, whatever the filtering removed */
static
VOID
CheckHasRestrictions(
    _In_ HANDLE Token,
    _In_ ULONG Expected)
{
    ULONG Value = 0xdeadbeef;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = NtQueryInformationToken(Token, TokenHasRestrictions, &Value, sizeof(Value), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(!!Value == !!Expected, "TokenHasRestrictions %lu, expected %lu\n", Value, Expected);
}

/* Whether the current thread can wear the token at the level it is acted on */
static
VOID
CheckImpersonation(
    _In_ HANDLE Token)
{
    OBJECT_ATTRIBUTES Attributes;
    SECURITY_QUALITY_OF_SERVICE Qos;
    HANDLE Impersonation = NULL;
    HANDLE None = NULL;
    NTSTATUS Status;

    Qos.Length = sizeof(Qos);
    Qos.ImpersonationLevel = SecurityImpersonation;
    Qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    Qos.EffectiveOnly = FALSE;

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    Attributes.SecurityQualityOfService = &Qos;

    Status = NtDuplicateToken(Token,
                              TOKEN_IMPERSONATE | TOKEN_QUERY,
                              &Attributes,
                              FALSE,
                              TokenImpersonation,
                              &Impersonation);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = NtSetInformationThread(NtCurrentThread(),
                                    ThreadImpersonationToken,
                                    &Impersonation,
                                    sizeof(Impersonation));
    ok_eq_hex(Status, STATUS_SUCCESS);

    NtSetInformationThread(NtCurrentThread(),
                           ThreadImpersonationToken,
                           &None,
                           sizeof(None));
    NtClose(Impersonation);
}

static
BOOLEAN
OpenOwnToken(
    _Out_ PHANDLE Process,
    _Out_ PSID *World)
{
    NTSTATUS Status;

    *Process = NULL;
    *World = NULL;

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, Process);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = RtlAllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID,
                                         0, 0, 0, 0, 0, 0, 0, World);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) return TRUE;

    NtClose(*Process);
    *Process = NULL;
    return FALSE;
}

START_TEST(NtFilterToken)
{
    TOKEN_GROUPS Groups;
    HANDLE Process;
    HANDLE Filtered;
    PSID World;
    NTSTATUS Status;

    if (!OpenOwnToken(&Process, &World)) return;

    CheckHasRestrictions(Process, 0);

    Groups.GroupCount = 1;
    Groups.Groups[0].Sid = World;
    Groups.Groups[0].Attributes = 0;

    Status = NtFilterToken(Process, 0, &Groups, NULL, NULL, &Filtered);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckDenyOnly(Filtered, World);
        CheckHasRestrictions(Filtered, 1);
        NtClose(Filtered);
    }

    /* A caller may act as a restricted copy of itself */
    Status = NtFilterToken(Process, 0, NULL, NULL, &Groups, &Filtered);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckHasRestrictions(Filtered, 1);
        CheckImpersonation(Filtered);
        NtClose(Filtered);
    }

    RtlFreeSid(World);
    NtClose(Process);
}

/* What the caller names but cannot read is the caller's fault, and said so */
START_TEST(NtFilterTokenCapture)
{
    TOKEN_GROUPS Groups;
    HANDLE Process;
    HANDLE Filtered;
    PVOID Unreadable = NULL;
    SIZE_T Size = PAGE_SIZE;
    PSID World;
    NTSTATUS Status;

    if (!OpenOwnToken(&Process, &World)) return;

    /* Memory that is only reserved faults on every read */
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Unreadable, 0, &Size,
                                     MEM_RESERVE, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        Groups.GroupCount = 1;
        Groups.Groups[0].Sid = Unreadable;
        Groups.Groups[0].Attributes = 0;

        Status = NtFilterToken(Process, 0, &Groups, NULL, NULL, &Filtered);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        if (NT_SUCCESS(Status)) NtClose(Filtered);

        Status = NtFilterToken(Process, 0, NULL, NULL, &Groups, &Filtered);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        if (NT_SUCCESS(Status)) NtClose(Filtered);

        Status = NtFilterToken(Process, 0, Unreadable, NULL, NULL, &Filtered);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        if (NT_SUCCESS(Status)) NtClose(Filtered);

        Status = NtFilterToken(Process, 0, NULL, Unreadable, NULL, &Filtered);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        if (NT_SUCCESS(Status)) NtClose(Filtered);

        Size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Unreadable, &Size, MEM_RELEASE);
    }

    /* A good list still works once the bad ones were refused */
    Groups.GroupCount = 1;
    Groups.Groups[0].Sid = World;
    Groups.Groups[0].Attributes = 0;

    Status = NtFilterToken(Process, 0, &Groups, NULL, NULL, &Filtered);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) NtClose(Filtered);

    RtlFreeSid(World);
    NtClose(Process);
}
