/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Global\TermSrvReadyEvent, set once session notifications can be registered
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <sddl.h>

#ifndef EVENT_QUERY_STATE
#define EVENT_QUERY_STATE 0x0001
#endif

#define READY_EVENT L"Global\\TermSrvReadyEvent"
#define READY_SDDL L"O:BAG:SYD:(A;;0x100000;;;WD)(A;;0x100000;;;AC)" \
                   L"(A;;0x100000;;;S-1-15-3-1024-1864111754-776273317-3666925027-2523908081-3792458206-3582472437-4114419977-1582884857)" \
                   L"(A;;DC;;;SY)"

static BOOL IsMember(WELL_KNOWN_SID_TYPE Type)
{
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    DWORD Size = sizeof(Sid);
    BOOL Member = FALSE;

    return CreateWellKnownSid(Type, NULL, Sid, &Size) && CheckTokenMembership(NULL, Sid, &Member) && Member;
}

static void CheckOpen(ACCESS_MASK Access, BOOL Granted)
{
    HANDLE Event;

    SetLastError(0xdeadbeef);
    Event = OpenEventW(Access, FALSE, READY_EVENT);
    ok(Granted ? Event != NULL : (Event == NULL && GetLastError() == ERROR_ACCESS_DENIED),
       "access %#lx: handle %p error %lu, expected %s\n", Access, Event, Event ? 0 : GetLastError(),
       Granted ? "granted" : "refused");
    if (Event)
        CloseHandle(Event);
}

static void CheckSecurity(void)
{
    BYTE Buffer[1024];
    DWORD Needed = 0;
    WCHAR *Text = NULL;
    HANDLE Event;

    Event = OpenEventW(READ_CONTROL, FALSE, READY_EVENT);
    ok(Event != NULL, "OpenEventW(READ_CONTROL) failed: %lu\n", GetLastError());
    if (!Event)
        return;
    if (GetKernelObjectSecurity(Event, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                (PSECURITY_DESCRIPTOR)Buffer, sizeof(Buffer), &Needed) &&
        ConvertSecurityDescriptorToStringSecurityDescriptorW((PSECURITY_DESCRIPTOR)Buffer, SDDL_REVISION_1,
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &Text, NULL))
    {
        ok(!wcscmp(Text, READY_SDDL), "security %ls\n", Text);
        LocalFree(Text);
    }
    else
    {
        ok(0, "reading the security failed: %lu\n", GetLastError());
    }
    CloseHandle(Event);
}

START_TEST(TermSrvReady)
{
    BOOL System = IsMember(WinLocalSystemSid), Owner = System || IsMember(WinBuiltinAdministratorsSid);
    HANDLE Event;

    Event = OpenEventW(SYNCHRONIZE, FALSE, READY_EVENT);
    ok(Event != NULL, "OpenEventW(%ls, SYNCHRONIZE) failed: %lu\n", READY_EVENT, GetLastError());
    if (!Event)
        return;
    ok(WaitForSingleObject(Event, 0) == WAIT_OBJECT_0, "the event is not set\n");
    ok(WaitForSingleObject(Event, 0) == WAIT_OBJECT_0, "a wait reset the event\n");
    CloseHandle(Event);

    CheckOpen(EVENT_QUERY_STATE, FALSE);
    CheckOpen(EVENT_MODIFY_STATE, System);
    CheckOpen(WRITE_OWNER, FALSE);
    CheckOpen(EVENT_ALL_ACCESS, FALSE);
    if (!Owner)
    {
        skip("the caller is not a member of the owner group\n");
        return;
    }
    CheckOpen(WRITE_DAC, TRUE);
    CheckSecurity();
}
