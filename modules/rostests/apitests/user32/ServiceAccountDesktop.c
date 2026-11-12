/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The interactive window station and its default desktop grant nothing to the service accounts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <aclapi.h>

static
BOOL
DaclNamesSid(
    _In_ HANDLE hObject,
    _In_ PSID Sid,
    _Out_ PBOOL Named)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    ACL_SIZE_INFORMATION Info;
    PACL pDacl = NULL;
    DWORD Index;

    *Named = FALSE;
    if (GetSecurityInfo(hObject, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pDacl, NULL, &pSD) !=
        ERROR_SUCCESS)
    {
        return FALSE;
    }

    if (pDacl != NULL && GetAclInformation(pDacl, &Info, sizeof(Info), AclSizeInformation))
    {
        for (Index = 0; Index < Info.AceCount && !*Named; Index++)
        {
            PACCESS_ALLOWED_ACE Ace;

            if (GetAce(pDacl, Index, (PVOID *)&Ace) && Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE)
                *Named = EqualSid((PSID)&Ace->SidStart, Sid);
        }
    }

    LocalFree(pSD);
    return TRUE;
}

static
VOID
CheckObject(
    _In_ HANDLE hObject,
    _In_ PCWSTR ObjectName,
    _In_ PSID NetworkService,
    _In_ PSID LocalService)
{
    BOOL Read, Named;

    Read = DaclNamesSid(hObject, NetworkService, &Named);
    ok(Read, "Reading the DACL of %ls failed %lu\n", ObjectName, GetLastError());
    ok(!Named, "%ls grants NetworkService\n", ObjectName);

    Read = DaclNamesSid(hObject, LocalService, &Named);
    ok(Read, "Reading the DACL of %ls failed %lu\n", ObjectName, GetLastError());
    ok(!Named, "%ls grants LocalService\n", ObjectName);
}

START_TEST(ServiceAccountDesktop)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PSID NetworkService = NULL, LocalService = NULL;
    HWINSTA hWinSta0, hOwnWinSta;
    HDESK hDefault;

    if (!AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_NETWORK_SERVICE_RID, 0, 0, 0, 0, 0, 0, 0, &NetworkService) ||
        !AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SERVICE_RID, 0, 0, 0, 0, 0, 0, 0, &LocalService))
    {
        ok(FALSE, "AllocateAndInitializeSid failed %lu\n", GetLastError());
        if (NetworkService != NULL)
            FreeSid(NetworkService);
        return;
    }

    hWinSta0 = OpenWindowStationW(L"WinSta0", FALSE, READ_CONTROL | WINSTA_ENUMDESKTOPS);
    ok(hWinSta0 != NULL, "OpenWindowStationW(WinSta0) failed %lu\n", GetLastError());
    if (hWinSta0 != NULL)
    {
        CheckObject(hWinSta0, L"WinSta0", NetworkService, LocalService);

        /* A desktop is opened in the process's own window station */
        hOwnWinSta = GetProcessWindowStation();
        ok(SetProcessWindowStation(hWinSta0), "SetProcessWindowStation failed %lu\n", GetLastError());
        hDefault = OpenDesktopW(L"Default", 0, FALSE, READ_CONTROL);
        ok(hDefault != NULL, "OpenDesktopW(Default) failed %lu\n", GetLastError());
        ok(SetProcessWindowStation(hOwnWinSta), "SetProcessWindowStation back failed %lu\n", GetLastError());

        if (hDefault != NULL)
        {
            CheckObject(hDefault, L"WinSta0\\Default", NetworkService, LocalService);
            CloseDesktop(hDefault);
        }

        CloseWindowStation(hWinSta0);
    }

    FreeSid(LocalService);
    FreeSid(NetworkService);
}
