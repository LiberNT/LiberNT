/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Traverse checking when an object below a directory is opened
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
NTSTATUS
SetChangeNotifyPrivilege(
    _In_ HANDLE Token,
    _In_ BOOLEAN Enable)
{
    TOKEN_PRIVILEGES Privileges;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Luid.LowPart = SE_CHANGE_NOTIFY_PRIVILEGE;
    Privileges.Privileges[0].Luid.HighPart = 0;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    return NtAdjustPrivilegesToken(Token, FALSE, &Privileges, 0, NULL, NULL);
}

static
NTSTATUS
OpenWithImpersonation(
    _In_opt_ HANDLE Token,
    _In_ PUNICODE_STRING Name)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE NoToken = NULL;
    HANDLE Event;
    NTSTATUS Status;

    if (Token != NULL)
    {
        Status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &Token, sizeof(Token));
        ok_hex(Status, STATUS_SUCCESS);
    }

    InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenEvent(&Event, EVENT_QUERY_STATE, &ObjectAttributes);
    if (NT_SUCCESS(Status))
        NtClose(Event);

    if (Token != NULL)
        NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &NoToken, sizeof(NoToken));

    return Status;
}

static
VOID
CheckTraverse(
    _In_ PUNICODE_STRING EventName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    SECURITY_QUALITY_OF_SERVICE Qos;
    HANDLE ProcessToken, Token;
    NTSTATUS Status;

    /* With SeChangeNotifyPrivilege enabled the directory is passed without a check */
    ok_hex(OpenWithImpersonation(NULL, EventName), STATUS_SUCCESS);

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_DUPLICATE, &ProcessToken);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Qos.Length = sizeof(Qos);
    Qos.ImpersonationLevel = SecurityImpersonation;
    Qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    Qos.EffectiveOnly = FALSE;
    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    ObjectAttributes.SecurityQualityOfService = &Qos;
    Status = NtDuplicateToken(ProcessToken, TOKEN_ALL_ACCESS, &ObjectAttributes, FALSE, TokenImpersonation, &Token);
    ok_hex(Status, STATUS_SUCCESS);
    NtClose(ProcessToken);
    if (!NT_SUCCESS(Status))
        return;

    /* The same subject without the privilege is held to the directory's DACL */
    ok_hex(SetChangeNotifyPrivilege(Token, FALSE), STATUS_SUCCESS);
    ok_hex(OpenWithImpersonation(Token, EventName), STATUS_ACCESS_DENIED);

    ok_hex(SetChangeNotifyPrivilege(Token, TRUE), STATUS_SUCCESS);
    ok_hex(OpenWithImpersonation(Token, EventName), STATUS_SUCCESS);

    NtClose(Token);
}

START_TEST(ObTraverse)
{
    SID_IDENTIFIER_AUTHORITY WorldAuthority = SECURITY_WORLD_SID_AUTHORITY;
    UCHAR AclBuffer[64];
    PACL Acl = (PACL)AclBuffer;
    SECURITY_DESCRIPTOR Descriptor;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DirectoryName, EventName;
    WCHAR DirectoryBuffer[96], EventBuffer[112];
    PSID World = NULL;
    HANDLE Directory, Event;
    NTSTATUS Status;

    Status = RtlAllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &World);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    /* Everyone may create and list, but nobody is granted traversal */
    RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    RtlAddAccessAllowedAce(Acl, ACL_REVISION,
                           DIRECTORY_CREATE_OBJECT | DIRECTORY_QUERY | READ_CONTROL | WRITE_DAC | DELETE,
                           World);
    RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE);

    StringCbPrintfW(DirectoryBuffer, sizeof(DirectoryBuffer), L"\\BaseNamedObjects\\ObTraverse%lu",
                    HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess));
    StringCbPrintfW(EventBuffer, sizeof(EventBuffer), L"%s\\Event", DirectoryBuffer);
    RtlInitUnicodeString(&DirectoryName, DirectoryBuffer);
    RtlInitUnicodeString(&EventName, EventBuffer);

    InitializeObjectAttributes(&ObjectAttributes, &DirectoryName, OBJ_CASE_INSENSITIVE, NULL, &Descriptor);
    Status = NtCreateDirectoryObject(&Directory, DIRECTORY_ALL_ACCESS, &ObjectAttributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, &EventName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            CheckTraverse(&EventName);
            NtClose(Event);
        }
        NtClose(Directory);
    }

    RtlFreeSid(World);
}
