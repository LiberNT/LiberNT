/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the NtCreateToken API
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include "precomp.h"
#include <tlhelp32.h>

static SID SystemSid = { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_LOCAL_SYSTEM_RID } };
static SID WorldSid = { SID_REVISION, 1, { SECURITY_WORLD_SID_AUTHORITY }, { SECURITY_WORLD_RID } };

/* Windows 11 grants SeCreateTokenPrivilege to lsass.exe only */
static
BOOL
ImpersonateLsass(VOID)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    HANDLE Snapshot, Process, Token, Duplicate;
    BOOL Found, Done = FALSE;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return FALSE;
    for (Found = Process32FirstW(Snapshot, &Entry); Found; Found = Process32NextW(Snapshot, &Entry))
    {
        if (!_wcsicmp(Entry.szExeFile, L"lsass.exe"))
            break;
    }
    CloseHandle(Snapshot);
    if (!Found)
        return FALSE;

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID);
    if (!Process)
        return FALSE;
    if (OpenProcessToken(Process, TOKEN_DUPLICATE | TOKEN_QUERY, &Token))
    {
        if (DuplicateTokenEx(Token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &Duplicate))
        {
            Done = SetThreadToken(NULL, Duplicate);
            CloseHandle(Duplicate);
        }
        CloseHandle(Token);
    }
    CloseHandle(Process);
    return Done;
}

static
NTSTATUS
CreatePrimaryToken(
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE Qos,
    _Out_ PHANDLE Token)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    LUID AuthenticationId = SYSTEM_LUID;
    LARGE_INTEGER Expiration = { .QuadPart = MAXLONGLONG };
    TOKEN_USER User = { { &SystemSid, 0 } };
    TOKEN_GROUPS Groups = { 1, { { &WorldSid, SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED } } };
    TOKEN_PRIVILEGES Privileges = { 0 };
    TOKEN_OWNER Owner = { &SystemSid };
    TOKEN_PRIMARY_GROUP PrimaryGroup = { &WorldSid };
    TOKEN_SOURCE Source = { "apitest" };

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    ObjectAttributes.SecurityQualityOfService = Qos;
    return NtCreateToken(Token, TOKEN_QUERY, &ObjectAttributes, TokenPrimary, &AuthenticationId, &Expiration,
                         &User, &Groups, &Privileges, &Owner, &PrimaryGroup, NULL, &Source);
}

START_TEST(NtCreateToken)
{
    SECURITY_QUALITY_OF_SERVICE Qos = { sizeof(Qos), SecurityImpersonation, SECURITY_STATIC_TRACKING, FALSE };
    TOKEN_TYPE Type;
    ULONG Length;
    NTSTATUS Status;
    BOOLEAN WasEnabled, Impersonating = FALSE;
    HANDLE Token;

    Status = RtlAdjustPrivilege(SE_CREATE_TOKEN_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (!NT_SUCCESS(Status) && ImpersonateLsass())
    {
        Impersonating = TRUE;
        Status = RtlAdjustPrivilege(SE_CREATE_TOKEN_PRIVILEGE, TRUE, TRUE, &WasEnabled);
    }
    if (!NT_SUCCESS(Status))
    {
        skip("Cannot create tokens (Status 0x%lx)\n", Status);
        if (Impersonating)
            RevertToSelf();
        return;
    }

    Status = CreatePrimaryToken(NULL, &Token);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationToken(Token, TokenType, &Type, sizeof(Type), &Length);
        ok_hex(Status, STATUS_SUCCESS);
        ok(Type == TokenPrimary, "Type is %d\n", Type);
        NtClose(Token);
    }

    Status = CreatePrimaryToken(&Qos, &Token);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        NtClose(Token);

    if (Impersonating)
        RevertToSelf();
    else
        RtlAdjustPrivilege(SE_CREATE_TOKEN_PRIVILEGE, WasEnabled, FALSE, &WasEnabled);
}
