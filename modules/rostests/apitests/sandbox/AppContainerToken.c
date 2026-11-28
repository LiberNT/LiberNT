/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test kernelbase CreateAppContainerToken and the package SID checks of NtCreateLowBoxToken
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_CREATE_APP_CONTAINER_TOKEN)(HANDLE, PSECURITY_CAPABILITIES, PHANDLE);
typedef NTSTATUS (NTAPI *PFN_NT_CREATE_LOW_BOX)(PHANDLE, HANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PSID, ULONG, PSID_AND_ATTRIBUTES, ULONG, HANDLE *);

#define TEST_PACKAGE L"S-1-15-2-11-22-33-44-55-66-77"

static PFN_CREATE_APP_CONTAINER_TOKEN CreateToken;
static WCHAR ChildPath[MAX_PATH];

static PSID
MakeSid(
    _In_ PCWSTR Text)
{
    PSID Sid = NULL;

    ok(ConvertStringSidToSidW(Text, &Sid), "ConvertStringSidToSidW(%ls) failed with %lu\n", Text, GetLastError());
    return Sid;
}

static VOID
CheckToken(
    _In_ const char *Tag,
    _In_ HANDLE Token,
    _In_ PSID Package)
{
    BYTE Buffer[1024];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Info;
    WCHAR CommandLine[] = L"cmd /c exit 7";
    TOKEN_TYPE Type = 0;
    DWORD Value = 0, Length = 0, ExitCode = 0;
    PSID Low = MakeSid(L"S-1-16-4096");

    ok(GetTokenInformation(Token, TokenType, &Type, sizeof(Type), &Length), "%s: TokenType failed\n", Tag);
    ok(Type == TokenPrimary, "%s: type %d\n", Tag, Type);
    ok(GetTokenInformation(Token, TokenIsAppContainer, &Value, sizeof(Value), &Length), "%s: TokenIsAppContainer failed\n", Tag);
    ok(Value == 1, "%s: TokenIsAppContainer %lu\n", Tag, Value);
    ok(GetTokenInformation(Token, TokenAppContainerSid, Buffer, sizeof(Buffer), &Length), "%s: TokenAppContainerSid failed\n", Tag);
    ok(EqualSid(((PTOKEN_APPCONTAINER_INFORMATION)Buffer)->TokenAppContainer, Package), "%s: wrong package SID\n", Tag);
    ok(GetTokenInformation(Token, TokenCapabilities, Buffer, sizeof(Buffer), &Length), "%s: TokenCapabilities failed\n", Tag);
    ok(((PTOKEN_GROUPS)Buffer)->GroupCount == 1, "%s: %lu capabilities\n", Tag, ((PTOKEN_GROUPS)Buffer)->GroupCount);
    ok(((PTOKEN_GROUPS)Buffer)->Groups[0].Attributes == SE_GROUP_ENABLED, "%s: capability attributes 0x%lx\n", Tag, ((PTOKEN_GROUPS)Buffer)->Groups[0].Attributes);
    ok(GetTokenInformation(Token, TokenIntegrityLevel, Buffer, sizeof(Buffer), &Length), "%s: TokenIntegrityLevel failed\n", Tag);
    ok(EqualSid(((PTOKEN_MANDATORY_LABEL)Buffer)->Label.Sid, Low), "%s: integrity is not low\n", Tag);

    ok(CreateProcessAsUserW(Token, ChildPath, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Info),
       "%s: CreateProcessAsUserW failed with %lu\n", Tag, GetLastError());
    if (Info.hProcess)
    {
        WaitForSingleObject(Info.hProcess, 5000);
        GetExitCodeProcess(Info.hProcess, &ExitCode);
        ok(ExitCode == 7, "%s: exit code %lu\n", Tag, ExitCode);
        CloseHandle(Info.hProcess);
        CloseHandle(Info.hThread);
    }
    LocalFree(Low);
}

static VOID
CheckNamedObjectDirectory(VOID)
{
    static const PCWSTR Children[] = { L"Global", L"Local", L"Session", L"RPC Control" };
    WCHAR Path[256];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Directory = NULL, Child;
    DWORD Session = 0;
    NTSTATUS Status;
    ULONG Index;

    ProcessIdToSessionId(GetCurrentProcessId(), &Session);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\AppContainerNamedObjects\\" TEST_PACKAGE, Session);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenDirectoryObject(&Directory, DIRECTORY_QUERY, &Attributes);
    ok(Status == STATUS_SUCCESS, "AppContainer directory: status 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    for (Index = 0; Index < ARRAYSIZE(Children); Index++)
    {
        RtlInitUnicodeString(&Name, Children[Index]);
        InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, Directory, NULL);
        Status = NtOpenDirectoryObject(&Child, DIRECTORY_QUERY, &Attributes);
        ok(Status == STATUS_SUCCESS, "%ls: status 0x%lx\n", Children[Index], Status);
        if (NT_SUCCESS(Status))
            NtClose(Child);
    }
    NtClose(Directory);
}

static VOID
CheckLowBoxPackageSids(
    _In_ HANDLE Base)
{
    static const struct { PCWSTR Sid; NTSTATUS Status; } Cases[] =
    {
        { L"S-1-15-3-1", STATUS_INVALID_PARAMETER },
        { L"S-1-5-32-544", STATUS_INVALID_PARAMETER },
        { L"S-1-15-2-1-2-3-4-5-6", STATUS_INVALID_PACKAGE_SID_LENGTH },
        { L"S-1-15-2-1-2-3-4-5-6-7-8-9", STATUS_INVALID_PACKAGE_SID_LENGTH },
        { L"S-1-15-2-1-2-3-4-5-6-7", STATUS_SUCCESS },
        { L"S-1-15-2-1-2-3-4-5-6-7-8-9-10-11", STATUS_SUCCESS },
    };
    PFN_NT_CREATE_LOW_BOX CreateLowBox = (PFN_NT_CREATE_LOW_BOX)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateLowBoxToken");
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Token;
    NTSTATUS Status;
    PSID Sid;
    ULONG Index;

    ok(CreateLowBox != NULL, "NtCreateLowBoxToken is not exported\n");
    if (!CreateLowBox)
        return;
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    for (Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        Sid = MakeSid(Cases[Index].Sid);
        if (!Sid)
            continue;
        Token = NULL;
        Status = CreateLowBox(&Token, Base, TOKEN_ALL_ACCESS, &Attributes, Sid, 0, NULL, 0, NULL);
        ok(Status == Cases[Index].Status, "%ls: status 0x%lx, expected 0x%lx\n", Cases[Index].Sid, Status, Cases[Index].Status);
        if (NT_SUCCESS(Status))
            NtClose(Token);
        LocalFree(Sid);
    }
}

START_TEST(AppContainerToken)
{
    SECURITY_CAPABILITIES Capabilities = { 0 };
    SID_AND_ATTRIBUTES Capability;
    TOKEN_PRIVILEGES Privileges;
    HANDLE Own = NULL, Restricted = NULL, QueryOnly = NULL, Token;
    PSID Package, InternetClient;
    BOOL Result;

    CreateToken = (PFN_CREATE_APP_CONTAINER_TOKEN)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CreateAppContainerToken");
    ok(CreateToken != NULL, "kernelbase does not export CreateAppContainerToken\n");
    ok(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateAppContainerToken") == NULL, "kernel32 exports CreateAppContainerToken\n");
    if (!CreateToken)
        return;

    GetSystemDirectoryW(ChildPath, ARRAYSIZE(ChildPath));
    StringCchCatW(ChildPath, ARRAYSIZE(ChildPath), L"\\cmd.exe");
    Package = MakeSid(TEST_PACKAGE);
    InternetClient = MakeSid(L"S-1-15-3-1");
    if (!Package || !InternetClient)
        return;
    Capability.Sid = InternetClient;
    Capability.Attributes = SE_GROUP_ENABLED;
    Capabilities.AppContainerSid = Package;
    Capabilities.Capabilities = &Capability;
    Capabilities.CapabilityCount = 1;

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Own), "OpenProcessToken failed with %lu\n", GetLastError());
    Privileges.PrivilegeCount = 1;
    LookupPrivilegeValueW(NULL, SE_ASSIGNPRIMARYTOKEN_NAME, &Privileges.Privileges[0].Luid);
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
    AdjustTokenPrivileges(Own, FALSE, &Privileges, 0, NULL, NULL);

    Capabilities.AppContainerSid = InternetClient;
    SetLastError(0xdeadbeef);
    Token = (HANDLE)(ULONG_PTR)0x1234;
    Result = CreateToken(Own, &Capabilities, &Token);
    ok(!Result && GetLastError() == ERROR_NOT_APPCONTAINER, "capability SID as package: %d, error %lu\n", Result, GetLastError());
    Capabilities.AppContainerSid = Package;

    Token = NULL;
    ok(CreateToken(Own, &Capabilities, &Token), "own token: error %lu\n", GetLastError());
    if (Token) { CheckToken("own token", Token, Package); CloseHandle(Token); }

    Token = NULL;
    ok(CreateToken(NULL, &Capabilities, &Token), "process token: error %lu\n", GetLastError());
    if (Token) { CheckToken("process token", Token, Package); CloseHandle(Token); }

    ok(CreateRestrictedToken(Own, 0, 0, NULL, 0, NULL, 0, NULL, &Restricted), "CreateRestrictedToken failed with %lu\n", GetLastError());
    Token = NULL;
    ok(CreateToken(Restricted, &Capabilities, &Token), "restricted token: error %lu\n", GetLastError());
    if (Token) { CheckToken("restricted token", Token, Package); CloseHandle(Token); }

    ok(DuplicateHandle(GetCurrentProcess(), Own, GetCurrentProcess(), &QueryOnly, TOKEN_QUERY, FALSE, 0), "DuplicateHandle failed\n");
    SetLastError(0xdeadbeef);
    Token = NULL;
    Result = CreateToken(QueryOnly, &Capabilities, &Token);
    ok(!Result && GetLastError() == ERROR_ACCESS_DENIED, "query-only token: %d, error %lu\n", Result, GetLastError());

    CheckNamedObjectDirectory();
    CheckLowBoxPackageSids(Own);

    CloseHandle(QueryOnly);
    CloseHandle(Restricted);
    CloseHandle(Own);
    LocalFree(InternetClient);
    LocalFree(Package);
}
