/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Tests for the NtAdjustGroupsToken API
 * COPYRIGHT:       Copyright 2021 George Bișoc <george.bisoc@reactos.org>
 */

#include "precomp.h"

static
HANDLE
GetProcessToken(
    _In_ DWORD Access)
{
    BOOL Success;
    HANDLE Token;

    Success = OpenProcessToken(GetCurrentProcess(), Access, &Token);
    if (!Success)
    {
        skip("Failed to open the process' token (error code: %lu)!\n", GetLastError());
        return NULL;
    }

    return Token;
}

static
PTOKEN_GROUPS
QueryGroups(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    ULONG Size = 0;
    PTOKEN_GROUPS Groups;

    Status = NtQueryInformationToken(Token, TokenGroups, NULL, 0, &Size);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return NULL;
    Groups = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
    if (Groups && !NT_SUCCESS(NtQueryInformationToken(Token, TokenGroups, Groups, Size, &Size)))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Groups);
        Groups = NULL;
    }
    return Groups;
}

static
HANDLE
DuplicateProcessToken(VOID)
{
    NTSTATUS Status;
    HANDLE ProcessToken, Token = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;

    ProcessToken = GetProcessToken(TOKEN_DUPLICATE);
    if (!ProcessToken)
        return NULL;
    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    Status = NtDuplicateToken(ProcessToken, TOKEN_ADJUST_GROUPS | TOKEN_QUERY, &ObjectAttributes,
                              FALSE, TokenPrimary, &Token);
    CloseHandle(ProcessToken);
    ok_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Token : NULL;
}

static
VOID
CheckGroupsUnchanged(
    _In_ HANDLE Token,
    _In_ PTOKEN_GROUPS Before)
{
    PTOKEN_GROUPS After = QueryGroups(Token);
    ULONG Index;

    ok(After != NULL && After->GroupCount == Before->GroupCount, "The group count changed\n");
    for (Index = 0; After && Index < After->GroupCount && Index < Before->GroupCount; Index++)
    {
        ok(After->Groups[Index].Attributes == Before->Groups[Index].Attributes,
           "Group %lu has attributes 0x%lx, expected 0x%lx\n", Index,
           After->Groups[Index].Attributes, Before->Groups[Index].Attributes);
    }
    if (After) RtlFreeHeap(RtlGetProcessHeap(), 0, After);
}

static
ULONG
FindDefaultOptionalGroup(
    _In_opt_ PTOKEN_GROUPS Groups)
{
    ULONG Index;

    for (Index = 0; Groups && Index < Groups->GroupCount; Index++)
    {
        if ((Groups->Groups[Index].Attributes & (SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_MANDATORY)) ==
            (SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT))
        {
            return Index;
        }
    }
    skip("The token has no optional group that is enabled by default\n");
    return MAXULONG;
}

static
VOID
AdjustOneGroup(VOID)
{
    NTSTATUS Status;
    HANDLE Token;
    PTOKEN_GROUPS Before = NULL, After = NULL, Reset = NULL;
    TOKEN_GROUPS Disable;
    ULONG Index, Target = MAXULONG;

    Token = DuplicateProcessToken();
    if (!Token)
        return;

    Before = QueryGroups(Token);
    Target = FindDefaultOptionalGroup(Before);
    if (Target == MAXULONG)
        goto Cleanup;

    Disable.GroupCount = 1;
    Disable.Groups[0].Sid = Before->Groups[Target].Sid;
    Disable.Groups[0].Attributes = 0;
    Status = NtAdjustGroupsToken(Token, FALSE, &Disable, 0, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);

    After = QueryGroups(Token);
    ok(After != NULL && After->GroupCount == Before->GroupCount, "The group count changed\n");
    for (Index = 0; After && Index < After->GroupCount && Index < Before->GroupCount; Index++)
    {
        if (Index == Target)
            ok(After->Groups[Index].Attributes == (Before->Groups[Index].Attributes & ~SE_GROUP_ENABLED),
               "Group %lu has attributes 0x%lx, expected 0x%lx\n", Index, After->Groups[Index].Attributes,
               Before->Groups[Index].Attributes & ~SE_GROUP_ENABLED);
        else
            ok(After->Groups[Index].Attributes == Before->Groups[Index].Attributes,
               "Group %lu has attributes 0x%lx, expected 0x%lx\n", Index, After->Groups[Index].Attributes,
               Before->Groups[Index].Attributes);
    }

    Status = NtAdjustGroupsToken(Token, TRUE, NULL, 0, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    Reset = QueryGroups(Token);
    ok(Reset != NULL && Reset->GroupCount == Before->GroupCount, "The group count changed\n");
    for (Index = 0; Reset && Index < Reset->GroupCount && Index < Before->GroupCount; Index++)
    {
        ok(Reset->Groups[Index].Attributes == Before->Groups[Index].Attributes,
           "Group %lu has attributes 0x%lx after the reset, expected 0x%lx\n", Index,
           Reset->Groups[Index].Attributes, Before->Groups[Index].Attributes);
    }

Cleanup:
    if (Before) RtlFreeHeap(RtlGetProcessHeap(), 0, Before);
    if (After) RtlFreeHeap(RtlGetProcessHeap(), 0, After);
    if (Reset) RtlFreeHeap(RtlGetProcessHeap(), 0, Reset);
    CloseHandle(Token);
}

static
VOID
AdjustMandatoryGroup(VOID)
{
    NTSTATUS Status;
    HANDLE Token;
    PTOKEN_GROUPS Before;
    struct
    {
        ULONG GroupCount;
        SID_AND_ATTRIBUTES Groups[2];
    } Disable;
    ULONG Index, Optional = MAXULONG, Mandatory = MAXULONG;

    Token = DuplicateProcessToken();
    if (!Token)
        return;
    Before = QueryGroups(Token);
    for (Index = 0; Before && Index < Before->GroupCount; Index++)
    {
        ULONG Attributes = Before->Groups[Index].Attributes;

        if (Optional == MAXULONG &&
            (Attributes & (SE_GROUP_ENABLED | SE_GROUP_MANDATORY)) == SE_GROUP_ENABLED)
        {
            Optional = Index;
        }
        else if (Optional != MAXULONG && (Attributes & SE_GROUP_MANDATORY))
        {
            Mandatory = Index;
            break;
        }
    }
    if (Mandatory == MAXULONG)
    {
        skip("The token has no mandatory group after an optional enabled one\n");
        goto Cleanup;
    }

    /* The optional group comes first in the token, so it would change first */
    Disable.GroupCount = 2;
    Disable.Groups[0].Sid = Before->Groups[Optional].Sid;
    Disable.Groups[0].Attributes = 0;
    Disable.Groups[1].Sid = Before->Groups[Mandatory].Sid;
    Disable.Groups[1].Attributes = 0;
    Status = NtAdjustGroupsToken(Token, FALSE, (PTOKEN_GROUPS)&Disable, 0, NULL, NULL);
    ok_hex(Status, STATUS_CANT_DISABLE_MANDATORY);
    CheckGroupsUnchanged(Token, Before);

Cleanup:
    if (Before) RtlFreeHeap(RtlGetProcessHeap(), 0, Before);
    CloseHandle(Token);
}

static
VOID
AdjustWithPreviousState(VOID)
{
    NTSTATUS Status;
    HANDLE Token;
    PTOKEN_GROUPS Before, Previous = NULL;
    TOKEN_GROUPS Disable, Small;
    ULONG Target, Length, Expected;

    Token = DuplicateProcessToken();
    if (!Token)
        return;
    Before = QueryGroups(Token);
    Target = FindDefaultOptionalGroup(Before);
    if (Target == MAXULONG)
        goto Cleanup;

    Disable.GroupCount = 1;
    Disable.Groups[0].Sid = Before->Groups[Target].Sid;
    Disable.Groups[0].Attributes = 0;
    Expected = FIELD_OFFSET(TOKEN_GROUPS, Groups[1]) + RtlLengthSid(Before->Groups[Target].Sid);

    Length = 0;
    Status = NtAdjustGroupsToken(Token, FALSE, &Disable, 0, &Small, &Length);
    ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok(Length == Expected, "ReturnLength is %lu, expected %lu\n", Length, Expected);
    CheckGroupsUnchanged(Token, Before);

    Previous = RtlAllocateHeap(RtlGetProcessHeap(), 0, Expected);
    if (!Previous)
    {
        skip("Out of memory\n");
        goto Cleanup;
    }
    Status = NtAdjustGroupsToken(Token, FALSE, &Disable, Expected, Previous, &Length);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Length == Expected, "ReturnLength is %lu, expected %lu\n", Length, Expected);
    ok(Previous->GroupCount == 1, "GroupCount is %lu\n", Previous->GroupCount);
    if (Previous->GroupCount != 1)
        goto Cleanup;
    ok((PUCHAR)Previous->Groups[0].Sid == (PUCHAR)&Previous->Groups[1],
       "The SID is at %p, expected after the array at %p\n", Previous->Groups[0].Sid, &Previous->Groups[1]);
    ok(RtlEqualSid(Previous->Groups[0].Sid, Before->Groups[Target].Sid), "The previous SID differs\n");
    ok(Previous->Groups[0].Attributes == Before->Groups[Target].Attributes,
       "The previous attributes are 0x%lx, expected 0x%lx\n",
       Previous->Groups[0].Attributes, Before->Groups[Target].Attributes);

    Status = NtAdjustGroupsToken(Token, FALSE, Previous, 0, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    CheckGroupsUnchanged(Token, Before);

Cleanup:
    if (Previous) RtlFreeHeap(RtlGetProcessHeap(), 0, Previous);
    if (Before) RtlFreeHeap(RtlGetProcessHeap(), 0, Before);
    CloseHandle(Token);
}

START_TEST(NtAdjustGroupsToken)
{
    HANDLE TokenHandle;
    NTSTATUS Status;

    /* Get the token from current process but with incorrect rights */
    TokenHandle = GetProcessToken(TOKEN_DUPLICATE);

    /* We give an invalid handle */
    Status = NtAdjustGroupsToken(NULL,
                                 TRUE,
                                 NULL,
                                 0,
                                 NULL,
                                 NULL);
    ok_hex(Status, STATUS_INVALID_HANDLE);

    /* We're trying to adjust the token's groups with wrong rights */
    Status = NtAdjustGroupsToken(TokenHandle,
                                 TRUE,
                                 NULL,
                                 0,
                                 NULL,
                                 NULL);
    ok_hex(Status, STATUS_ACCESS_DENIED);

    /* Close our handle and open a new one with right access right */
    CloseHandle(TokenHandle);
    TokenHandle = GetProcessToken(TOKEN_ADJUST_GROUPS);

    /* We don't give a list of groups to be adjusted in token */
    Status = NtAdjustGroupsToken(TokenHandle,
                                 FALSE,
                                 NULL,
                                 0,
                                 NULL,
                                 NULL);
    ok_hex(Status, STATUS_INVALID_PARAMETER);

    /* Reset the groups of an access token to default */
    Status = NtAdjustGroupsToken(TokenHandle,
                                 TRUE,
                                 NULL,
                                 0,
                                 NULL,
                                 NULL);
    ok_hex(Status, STATUS_SUCCESS);

    CloseHandle(TokenHandle);

    AdjustOneGroup();
    AdjustMandatoryGroup();
    AdjustWithPreviousState();
}
