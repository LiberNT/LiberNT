/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a MAXIMUM_ALLOWED open is granted what the object's DACL allows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static ACCESS_MASK QueryGrantedAccess(HANDLE Handle)
{
    OBJECT_BASIC_INFORMATION Basic;
    NTSTATUS Status;

    Status = NtQueryObject(Handle, ObjectBasicInformation, &Basic, sizeof(Basic), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Basic.GrantedAccess : 0;
}

START_TEST(NtOpenMaximumAllowed)
{
    static SID_IDENTIFIER_AUTHORITY WorldAuthority = { SECURITY_WORLD_SID_AUTHORITY };
    const ACCESS_MASK Allowed = EVENT_QUERY_STATE | SYNCHRONIZE;
    UCHAR SidBuffer[SECURITY_SID_SIZE(1)];
    UCHAR AclBuffer[sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + SECURITY_SID_SIZE(1)];
    SECURITY_DESCRIPTOR Descriptor;
    SECURITY_ATTRIBUTES Attributes;
    PSID World = (PSID)SidBuffer;
    PACL Acl = (PACL)AclBuffer;
    WCHAR Name[64];
    HANDLE Event, Opened;
    ACCESS_MASK Granted;

    RtlInitializeSid(World, &WorldAuthority, 1);
    *RtlSubAuthoritySid(World, 0) = SECURITY_WORLD_RID;
    ok_hex(RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION), STATUS_SUCCESS);
    ok_hex(RtlAddAccessAllowedAce(Acl, ACL_REVISION, Allowed, World), STATUS_SUCCESS);
    ok_hex(RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION), STATUS_SUCCESS);
    ok_hex(RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE), STATUS_SUCCESS);

    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = &Descriptor;
    Attributes.bInheritHandle = FALSE;

    swprintf(Name, RTL_NUMBER_OF(Name), L"NtOpenMaximumAllowed_%lu", GetCurrentProcessId());
    Event = CreateEventW(&Attributes, TRUE, FALSE, Name);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (Event == NULL)
        return;

    /* The creator keeps every right it asked for, whatever the DACL says */
    ok_hex(QueryGrantedAccess(Event) & EVENT_ALL_ACCESS, EVENT_ALL_ACCESS);

    /* Asking for everything the DACL does not grant is refused */
    SetLastError(0xdeadbeef);
    Opened = OpenEventW(EVENT_ALL_ACCESS, FALSE, Name);
    ok(Opened == NULL, "OpenEventW(EVENT_ALL_ACCESS) succeeded\n");
    ok_long(GetLastError(), ERROR_ACCESS_DENIED);
    if (Opened != NULL)
        CloseHandle(Opened);

    /* MAXIMUM_ALLOWED is granted what the DACL allows and nothing it does not */
    Opened = OpenEventW(MAXIMUM_ALLOWED, FALSE, Name);
    ok(Opened != NULL, "OpenEventW(MAXIMUM_ALLOWED) failed with %lu\n", GetLastError());
    if (Opened != NULL)
    {
        Granted = QueryGrantedAccess(Opened);
        ok_hex(Granted & Allowed, Allowed);
        ok_hex(Granted & EVENT_MODIFY_STATE, 0);
        ok_long(WaitForSingleObject(Opened, 0), WAIT_TIMEOUT);
        CloseHandle(Opened);
    }

    CloseHandle(Event);
}
