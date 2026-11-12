/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the security a window station and a desktop get, and that opening one is checked against it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>
#include <strsafe.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>

static
BOOL
GetSddl(
    _In_ HANDLE Object,
    _In_ SECURITY_INFORMATION Info,
    _Out_writes_(Count) PWSTR Sddl,
    _In_ DWORD Count)
{
    BYTE Buffer[2048];
    DWORD Needed = 0;
    LPWSTR String = NULL;

    Sddl[0] = UNICODE_NULL;
    if (!GetUserObjectSecurity(Object, &Info, Buffer, sizeof(Buffer), &Needed) ||
        !ConvertSecurityDescriptorToStringSecurityDescriptorW(Buffer, SDDL_REVISION_1, Info, &String, NULL))
    {
        return FALSE;
    }
    StringCchCopyW(Sddl, Count, String);
    LocalFree(String);
    return TRUE;
}

static
VOID
TestDirectory(VOID)
{
    WCHAR Path[128], Sddl[512];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Directory;
    NTSTATUS Status;
    DWORD Session = 0;

    ProcessIdToSessionId(GetCurrentProcessId(), &Session);
    if (Session == 0)
        StringCchCopyW(Path, ARRAYSIZE(Path), L"\\Windows\\WindowStations");
    else
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\Windows\\WindowStations", Session);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, 0, NULL, NULL);
    Status = NtOpenDirectoryObject(&Directory, READ_CONTROL, &Attributes);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok(GetSddl(Directory, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, Sddl, ARRAYSIZE(Sddl)), "Cannot read %ls: %lu\n", Path, GetLastError());
    ok(!wcscmp(Sddl, L"O:BAD:(A;CINPIO;0xf037f;;;WD)(A;CINPIO;0xf037f;;;RC)(A;OIIO;GA;;;WD)(A;OIIO;GA;;;RC)(A;;CCLC;;;BA)(A;;DC;;;WD)(A;;DC;;;RC)"),
       "%ls is secured by %ls\n", Path, Sddl);
    NtClose(Directory);
}

START_TEST(WindowStationSecurity)
{
    HWINSTA WinSta, Previous = GetProcessWindowStation();
    WCHAR Sddl[512];
    HDESK Desktop;

    TestDirectory();

    WinSta = CreateWindowStationW(L"LiberNTSecurityTest", 0, WINSTA_ALL_ACCESS | READ_CONTROL, NULL);
    ok(WinSta != NULL, "CreateWindowStationW failed %lu\n", GetLastError());
    if (WinSta != NULL)
    {
        ok(GetSddl(WinSta, DACL_SECURITY_INFORMATION, Sddl, ARRAYSIZE(Sddl)), "Cannot read the window station's DACL: %lu\n", GetLastError());
        ok(!wcscmp(Sddl, L"D:(A;;0xf037f;;;WD)(A;;0xf037f;;;RC)(A;OIIO;GA;;;WD)(A;OIIO;GA;;;RC)"), "A window station created without a descriptor has %ls\n", Sddl);
        ok(SetProcessWindowStation(WinSta), "SetProcessWindowStation failed %lu\n", GetLastError());
        Desktop = CreateDesktopW(L"LiberNTSecurityDesk", NULL, NULL, 0, GENERIC_ALL, NULL);
        ok(Desktop != NULL, "CreateDesktopW failed %lu\n", GetLastError());
        if (Desktop != NULL)
        {
            ok(GetSddl(Desktop, DACL_SECURITY_INFORMATION, Sddl, ARRAYSIZE(Sddl)), "Cannot read the desktop's DACL: %lu\n", GetLastError());
            ok(!wcscmp(Sddl, L"D:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;WD)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;RC)"), "Its desktop has %ls\n", Sddl);
            CloseDesktop(Desktop);
        }
        SetProcessWindowStation(Previous);
        CloseWindowStation(WinSta);
    }

    SetLastError(0xdeadbeef);
    Desktop = OpenDesktopW(L"Winlogon", 0, FALSE, MAXIMUM_ALLOWED);
    ok(Desktop == NULL && GetLastError() == ERROR_ACCESS_DENIED, "The Winlogon desktop opened (%p, %lu)\n", Desktop, GetLastError());
    if (Desktop != NULL)
        CloseDesktop(Desktop);
}
