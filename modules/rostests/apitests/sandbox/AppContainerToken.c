/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test kernelbase CreateAppContainerToken, NtCreateLowBoxToken package SIDs and what an AppContainer may open
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/iofuncs.h>

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

typedef struct _TOKEN_CLASS_CASE
{
    TOKEN_INFORMATION_CLASS Class;
    NTSTATUS Status;
    ULONG Length;
    ULONG FirstUlong;
    ULONG AppContainerFirstUlong;
} TOKEN_CLASS_CASE;

static VOID
CheckTokenClasses(
    _In_ const char *Tag,
    _In_ HANDLE Token,
    _In_ BOOL AppContainer)
{
    static const TOKEN_CLASS_CASE Cases[] =
    {
        { TokenUserClaimAttributes, STATUS_SUCCESS, sizeof(CLAIM_SECURITY_ATTRIBUTES_INFORMATION), 1, 1 },
        { TokenDeviceClaimAttributes, STATUS_SUCCESS, sizeof(CLAIM_SECURITY_ATTRIBUTES_INFORMATION), 1, 1 },
        { TokenRestrictedUserClaimAttributes, STATUS_INVALID_INFO_CLASS, 0, 0, 0 },
        { TokenDeviceGroups, STATUS_SUCCESS, sizeof(TOKEN_GROUPS), 0, 0 },
        { TokenRestrictedDeviceGroups, STATUS_INVALID_INFO_CLASS, 0, 0, 0 },
        { TokenProcessTrustLevel, STATUS_SUCCESS, sizeof(PSID), 0, 0 },
        { TokenPrivateNameSpace, STATUS_SUCCESS, sizeof(ULONG), 0, 0 },
        { TokenSingletonAttributes, STATUS_SUCCESS, sizeof(CLAIM_SECURITY_ATTRIBUTES_INFORMATION), 1, 1 },
        { TokenChildProcessFlags, STATUS_INVALID_INFO_CLASS, 0, 0, 0 },
        { TokenIsLessPrivilegedAppContainer, STATUS_INVALID_INFO_CLASS, 0, 0, 0 },
        { TokenIsSandboxed, STATUS_SUCCESS, sizeof(ULONG), 0, 1 },
        { TokenIsAppSilo, STATUS_SUCCESS, sizeof(ULONG), 0, 0 },
    };
    ULONG Buffer[64];
    ULONG Index, Length, Expected;
    NTSTATUS Status;

    for (Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        Length = 0xdeadbeef;
        Status = NtQueryInformationToken(Token, Cases[Index].Class, NULL, 0, &Length);
        if (Cases[Index].Status == STATUS_SUCCESS)
        {
            ok(Status == STATUS_BUFFER_TOO_SMALL, "%s class %d: probe status 0x%lx\n", Tag, Cases[Index].Class, Status);
            ok(Length == Cases[Index].Length, "%s class %d: needs %lu\n", Tag, Cases[Index].Class, Length);
        }
        else
        {
            ok(Status == Cases[Index].Status, "%s class %d: probe status 0x%lx\n", Tag, Cases[Index].Class, Status);
        }

        memset(Buffer, 0xcc, sizeof(Buffer));
        Length = 0xdeadbeef;
        Status = NtQueryInformationToken(Token, Cases[Index].Class, Buffer, sizeof(Buffer), &Length);
        ok(Status == Cases[Index].Status, "%s class %d: status 0x%lx\n", Tag, Cases[Index].Class, Status);
        if (!NT_SUCCESS(Status))
            continue;
        ok(Length == Cases[Index].Length, "%s class %d: returned %lu\n", Tag, Cases[Index].Class, Length);
        Expected = AppContainer ? Cases[Index].AppContainerFirstUlong : Cases[Index].FirstUlong;
        ok(Buffer[0] == Expected, "%s class %d: first value %lu\n", Tag, Cases[Index].Class, Buffer[0]);
    }
}

static BOOL
DaclHasAce(
    _In_ PACL Dacl,
    _In_ PSID Sid,
    _In_ BYTE Flags,
    _In_ ACCESS_MASK Mask)
{
    ACCESS_ALLOWED_ACE *Ace;
    ULONG Index;

    for (Index = 0; Dacl && Index < Dacl->AceCount; Index++)
    {
        if (!GetAce(Dacl, Index, (PVOID *)&Ace) || Ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
            continue;
        if (Ace->Header.AceFlags == Flags && Ace->Mask == Mask && EqualSid((PSID)&Ace->SidStart, Sid))
            return TRUE;
    }
    return FALSE;
}

static VOID
CheckPackageAces(VOID)
{
    static const PCWSTR Packages[] = { L"S-1-15-2-1", L"S-1-15-2-2" };
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KsecDD");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Io;
    PSECURITY_DESCRIPTOR Descriptor;
    PACL WinstaDacl = NULL, DesktopDacl = NULL, KsecDacl = NULL;
    PSECURITY_DESCRIPTOR WinstaSd = NULL, DesktopSd = NULL, KsecSd = NULL;
    HANDLE Ksec = NULL;
    NTSTATUS Status;
    ULONG Index;
    PSID Sid;

    ok(GetSecurityInfo(GetProcessWindowStation(), SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &WinstaDacl, NULL, &WinstaSd) == ERROR_SUCCESS,
       "window station security failed\n");
    ok(GetSecurityInfo(GetThreadDesktop(GetCurrentThreadId()), SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &DesktopDacl, NULL, &DesktopSd) == ERROR_SUCCESS,
       "desktop security failed\n");
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Ksec, READ_CONTROL | SYNCHRONIZE, &Attributes, &Io, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
    ok(Status == STATUS_SUCCESS, "KsecDD open status 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(GetSecurityInfo(Ksec, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &KsecDacl, NULL, &KsecSd) == ERROR_SUCCESS,
           "KsecDD security failed\n");
        NtClose(Ksec);
    }

    for (Index = 0; Index < ARRAYSIZE(Packages); Index++)
    {
        Sid = MakeSid(Packages[Index]);
        if (!Sid)
            continue;
        ok(DaclHasAce(WinstaDacl, Sid, NO_PROPAGATE_INHERIT_ACE, 0x20327), "%ls: no window station ACE\n", Packages[Index]);
        ok(DaclHasAce(WinstaDacl, Sid, INHERIT_ONLY_ACE | OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE, GENERIC_READ | GENERIC_EXECUTE),
           "%ls: no inherited desktop ACE on the window station\n", Packages[Index]);
        ok(DaclHasAce(DesktopDacl, Sid, 0, 0xf00ff), "%ls: no desktop ACE\n", Packages[Index]);
        ok(DaclHasAce(KsecDacl, Sid, 0, 0x1201bf), "%ls: no KsecDD ACE\n", Packages[Index]);
        LocalFree(Sid);
    }

    Descriptor = WinstaSd; if (Descriptor) LocalFree(Descriptor);
    Descriptor = DesktopSd; if (Descriptor) LocalFree(Descriptor);
    Descriptor = KsecSd; if (Descriptor) LocalFree(Descriptor);
}

#define AC_CHILD_DESKTOP        0x01
#define AC_CHILD_INPUT_DESKTOP  0x02
#define AC_CHILD_KSECDD         0x04
#define AC_CHILD_RANDOM         0x08
#define AC_CHILD_PIPE_CREATE    0x10
#define AC_CHILD_PIPE_OPEN      0x20

typedef BOOLEAN (WINAPI *PFN_RTL_GEN_RANDOM)(PVOID, ULONG);

static DWORD
RunObjectsChild(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KsecDD");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Io;
    PFN_RTL_GEN_RANDOM GenRandom;
    WCHAR PipeName[96];
    HANDLE Handle, Server, Client;
    HDESK Input;
    DWORD Failures = 0;
    BYTE Random[16];
    NTSTATUS Status;

    if (!GetProcessWindowStation() || !GetThreadDesktop(GetCurrentThreadId()) || !GetDesktopWindow())
        SbxChildFail(&Failures, AC_CHILD_DESKTOP, "window station or desktop", GetLastError());
    Input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (Input)
        CloseDesktop(Input);
    else
        SbxChildFail(&Failures, AC_CHILD_INPUT_DESKTOP, "OpenInputDesktop", GetLastError());

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_READ_DATA | SYNCHRONIZE, &Attributes, &Io, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (NT_SUCCESS(Status))
        NtClose(Handle);
    else
        SbxChildFail(&Failures, AC_CHILD_KSECDD, "KsecDD open", Status);
    GenRandom = (PFN_RTL_GEN_RANDOM)GetProcAddress(LoadLibraryW(L"advapi32.dll"), "SystemFunction036");
    if (!GenRandom || !GenRandom(Random, sizeof(Random)))
        SbxChildFail(&Failures, AC_CHILD_RANDOM, "RtlGenRandom", GetLastError());

    StringCchPrintfW(PipeName, ARRAYSIZE(PipeName), L"\\\\.\\pipe\\LOCAL\\sbx_ac_pipe.%lu", GetCurrentProcessId());
    Server = CreateNamedPipeW(PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 4096, 4096, 5000, NULL);
    if (Server == INVALID_HANDLE_VALUE)
    {
        SbxChildFail(&Failures, AC_CHILD_PIPE_CREATE | AC_CHILD_PIPE_OPEN, "CreateNamedPipe", GetLastError());
        return Failures;
    }
    Client = CreateFileW(PipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS | FILE_FLAG_OVERLAPPED, NULL);
    if (Client != INVALID_HANDLE_VALUE)
        CloseHandle(Client);
    else
        SbxChildFail(&Failures, AC_CHILD_PIPE_OPEN, "open own pipe", GetLastError());
    CloseHandle(Server);
    return Failures;
}

START_TEST(AppContainerToken)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);
    PROCESS_INFORMATION Child;
    SECURITY_CAPABILITIES Capabilities = { 0 };
    SID_AND_ATTRIBUTES Capability;
    TOKEN_PRIVILEGES Privileges;
    HANDLE Own = NULL, Restricted = NULL, QueryOnly = NULL, Token;
    PSID Package, InternetClient;
    BOOL Result;

    if (SbxIsChild(Arguments, Count, "objects"))
        ExitProcess(RunObjectsChild());

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
    CheckTokenClasses("own token", Own, FALSE);
    CheckPackageAces();

    Token = NULL;
    ok(CreateToken(NULL, &Capabilities, &Token), "objects token: error %lu\n", GetLastError());
    if (Token)
    {
        CheckTokenClasses("AppContainer token", Token, TRUE);
        ok(SbxSpawnChild("AppContainerToken", "objects", NULL, Token, NULL, 0, &Child), "objects child: error %lu\n", GetLastError());
        if (Child.hProcess)
            ok(SbxWaitChild(&Child) == 0, "objects child failed\n");
        CloseHandle(Token);
    }

    CloseHandle(QueryOnly);
    CloseHandle(Restricted);
    CloseHandle(Own);
    LocalFree(InternetClient);
    LocalFree(Package);
}
