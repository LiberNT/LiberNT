/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User mode tests for the token an AppContainer runs with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/sefuncs.h>

static SID_IDENTIFIER_AUTHORITY PackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};

/* Reads an answer whose size depends on the token into a block of its own */
static
PVOID
QueryToken(
    _In_ HANDLE Token,
    _In_ TOKEN_INFORMATION_CLASS Class)
{
    ULONG Length = 0;
    NTSTATUS Status;
    PVOID Buffer;

    Status = NtQueryInformationToken(Token, Class, NULL, 0, &Length);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "class %d: 0x%08lx\n", Class, Status);
    if (Length == 0) return NULL;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    if (Buffer == NULL) return NULL;

    Status = NtQueryInformationToken(Token, Class, Buffer, Length, &Length);
    ok(Status == STATUS_SUCCESS, "class %d: 0x%08lx\n", Class, Status);
    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return NULL;
    }

    return Buffer;
}

/* What a package's token says about the package it stands for */
static
VOID
CheckPackage(
    _In_ HANDLE Token,
    _In_ PSID Package,
    _In_ PSID Capability,
    _In_ PCSTR Which)
{
    PTOKEN_APPCONTAINER_INFORMATION Container;
    PTOKEN_GROUPS Capabilities;
    ULONG IsAppContainer = 0;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = NtQueryInformationToken(Token,
                                     TokenIsAppContainer,
                                     &IsAppContainer,
                                     sizeof(IsAppContainer),
                                     &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(IsAppContainer != 0, "%s: not an AppContainer token\n", Which);

    Container = QueryToken(Token, TokenAppContainerSid);
    ok(Container != NULL, "%s: no package answered\n", Which);
    if (Container != NULL)
    {
        ok(Container->TokenAppContainer != NULL &&
           RtlEqualSid(Container->TokenAppContainer, Package),
           "%s: the package is not the one the token was made for\n", Which);
        HeapFree(GetProcessHeap(), 0, Container);
    }

    Capabilities = QueryToken(Token, TokenCapabilities);
    ok(Capabilities != NULL, "%s: no capabilities answered\n", Which);
    if (Capabilities != NULL)
    {
        ok(Capabilities->GroupCount == 1,
           "%s: %lu capabilities, expected 1\n", Which, Capabilities->GroupCount);
        if (Capabilities->GroupCount == 1)
        {
            ok(RtlEqualSid(Capabilities->Groups[0].Sid, Capability),
               "%s: the capability is another one\n", Which);
            ok(Capabilities->Groups[0].Attributes == SE_GROUP_ENABLED,
               "%s: capability attributes 0x%lx\n",
               Which, Capabilities->Groups[0].Attributes);
        }
        HeapFree(GetProcessHeap(), 0, Capabilities);
    }
}

/* A package's token, and the copies made of it, all stand for the package */
static
VOID
TestPackageSurvivesCopies(
    _In_ HANDLE Process,
    _In_ PSID Package,
    _In_ PSID Capability)
{
    SID_AND_ATTRIBUTES Capabilities[1];
    HANDLE LowBox = NULL;
    HANDLE Copy = NULL;
    NTSTATUS Status;

    Capabilities[0].Sid = Capability;
    Capabilities[0].Attributes = SE_GROUP_ENABLED;

    Status = NtCreateLowBoxToken(&LowBox,
                                 Process,
                                 TOKEN_ALL_ACCESS,
                                 NULL,
                                 Package,
                                 RTL_NUMBER_OF(Capabilities),
                                 Capabilities,
                                 0,
                                 NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    CheckPackage(LowBox, Package, Capability, "created");

    /* A process started with the token runs with a copy of it */
    Status = NtDuplicateToken(LowBox,
                              TOKEN_ALL_ACCESS,
                              NULL,
                              FALSE,
                              TokenPrimary,
                              &Copy);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckPackage(Copy, Package, Capability, "duplicated");
        NtClose(Copy);
    }

    NtClose(LowBox);
}

/*
 * The handles a package's token is given keep their objects as long as a
 * token of the package lives, even after whoever made it let go of its own.
 */
static
VOID
TestHandlesKept(
    _In_ HANDLE Process,
    _In_ PSID Package)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\KmtestLowBoxKept");
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Event = NULL;
    HANDLE Again = NULL;
    HANDLE LowBox = NULL;
    HANDLE Copy = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, &Attributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = NtCreateLowBoxToken(&LowBox,
                                 Process,
                                 TOKEN_ALL_ACCESS,
                                 NULL,
                                 Package,
                                 0,
                                 NULL,
                                 1,
                                 &Event);
    ok_eq_hex(Status, STATUS_SUCCESS);
    NtClose(Event);
    if (!NT_SUCCESS(Status)) return;

    Status = NtOpenEvent(&Again, SYNCHRONIZE, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) NtClose(Again);

    Status = NtDuplicateToken(LowBox, TOKEN_ALL_ACCESS, NULL, FALSE, TokenPrimary, &Copy);
    ok_eq_hex(Status, STATUS_SUCCESS);
    NtClose(LowBox);

    if (NT_SUCCESS(Status))
    {
        Status = NtOpenEvent(&Again, SYNCHRONIZE, &Attributes);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) NtClose(Again);

        NtClose(Copy);
    }

    /* With no token of the package left, nothing holds the object any more */
    Status = NtOpenEvent(&Again, SYNCHRONIZE, &Attributes);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status)) NtClose(Again);
}

START_TEST(SeLowBoxToken)
{
    PSID Package = NULL;
    PSID Capability = NULL;
    HANDLE Process = NULL;
    NTSTATUS Status;

    Status = RtlAllocateAndInitializeSid(&PackageAuthority,
                                         SECURITY_APP_PACKAGE_RID_COUNT,
                                         SECURITY_APP_PACKAGE_BASE_RID,
                                         1, 2, 3, 4, 5, 6, 7,
                                         &Package);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = RtlAllocateAndInitializeSid(&PackageAuthority,
                                         2,
                                         SECURITY_CAPABILITY_BASE_RID,
                                         SECURITY_CAPABILITY_INTERNET_CLIENT,
                                         0, 0, 0, 0, 0, 0,
                                         &Capability);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_ALL_ACCESS, &Process);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (Package != NULL && Capability != NULL && Process != NULL)
    {
        TestPackageSurvivesCopies(Process, Package, Capability);
        TestHandlesKept(Process, Package);
    }

    if (Process != NULL) NtClose(Process);
    if (Capability != NULL) RtlFreeSid(Capability);
    if (Package != NULL) RtlFreeSid(Package);
}
