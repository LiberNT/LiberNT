/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that LsaLogonUser grants each logon type only with its logon right and without its deny right
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <ntsecapi.h>
#include <lm.h>
#include <stdio.h>

#define PASSWORD L"Logon-Rights!2026"
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

static HANDLE Lsa;
static ULONG Package;
static LSA_HANDLE Policy;
static WCHAR User[16];
static WCHAR Group[16];
static PSID UserSid;
static PSID GroupSid;

static NTSTATUS
TryLogOn(SECURITY_LOGON_TYPE Type, PCWSTR Password)
{
    BYTE Buffer[sizeof(MSV1_0_INTERACTIVE_LOGON) + 3 * 64 * sizeof(WCHAR)];
    PMSV1_0_INTERACTIVE_LOGON Logon = (PMSV1_0_INTERACTIVE_LOGON)Buffer;
    PWCHAR Next = (PWCHAR)(Logon + 1);
    LSA_STRING Origin = { 11, 12, "LogonRights" };
    TOKEN_SOURCE Source = { "Test", { 0, 0 } };
    QUOTA_LIMITS Quota;
    PVOID Profile = NULL;
    ULONG ProfileLength = 0;
    LUID LogonId;
    NTSTATUS Status, SubStatus = 0;
    HANDLE Token = NULL;

    ZeroMemory(Buffer, sizeof(Buffer));
    Logon->MessageType = MsV1_0InteractiveLogon;
    Logon->LogonDomainName.Buffer = Next;
    lstrcpyW(Next, L".");
    Logon->LogonDomainName.Length = 2;
    Logon->LogonDomainName.MaximumLength = 4;
    Next += 2;
    Logon->UserName.Buffer = Next;
    lstrcpyW(Next, User);
    Logon->UserName.Length = (USHORT)(wcslen(User) * sizeof(WCHAR));
    Logon->UserName.MaximumLength = Logon->UserName.Length + sizeof(WCHAR);
    Next += wcslen(User) + 1;
    Logon->Password.Buffer = Next;
    lstrcpyW(Next, Password);
    Logon->Password.Length = (USHORT)(wcslen(Password) * sizeof(WCHAR));
    Logon->Password.MaximumLength = Logon->Password.Length + sizeof(WCHAR);
    Next += wcslen(Password) + 1;
    AllocateLocallyUniqueId(&Source.SourceIdentifier);
    Status = LsaLogonUser(Lsa, &Origin, Type, Package, Logon, (ULONG)((PBYTE)Next - Buffer), NULL, &Source, &Profile,
                          &ProfileLength, &LogonId, &Token, &Quota, &SubStatus);
    trace("LsaLogonUser(%d) returned 0x%08lx, substatus 0x%08lx\n", Type, Status, SubStatus);
    if (Profile)
        LsaFreeReturnBuffer(Profile);
    if (NT_SUCCESS(Status))
        CloseHandle(Token);
    return Status;
}

static VOID
ExpectLogOn(SECURITY_LOGON_TYPE Type, NTSTATUS Expected, PCSTR What)
{
    NTSTATUS Status = TryLogOn(Type, PASSWORD);

    ok(Status == Expected, "%s: LsaLogonUser(%d) returned 0x%08lx, expected 0x%08lx\n", What, Type, Status, Expected);
}

static VOID
SetRight(PSID Sid, PCWSTR Right, BOOL Add)
{
    LSA_UNICODE_STRING Name;
    NTSTATUS Status;

    Name.Buffer = (PWSTR)Right;
    Name.Length = (USHORT)(wcslen(Right) * sizeof(WCHAR));
    Name.MaximumLength = Name.Length + sizeof(WCHAR);
    if (Add)
        Status = LsaAddAccountRights(Policy, Sid, &Name, 1);
    else
        Status = LsaRemoveAccountRights(Policy, Sid, FALSE, &Name, 1);
    ok(Status == STATUS_SUCCESS, "%s %ls returned 0x%08lx\n", Add ? "Adding" : "Removing", Right, Status);
}

static PSID
LookupSid(PCWSTR Name)
{
    BYTE Buffer[SECURITY_MAX_SID_SIZE];
    WCHAR Domain[DNLEN + 1];
    DWORD Length = sizeof(Buffer), DomainLength = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    PSID Sid;

    if (!LookupAccountNameW(NULL, Name, Buffer, &Length, Domain, &DomainLength, &Use))
    {
        ok(FALSE, "LookupAccountNameW(%ls) failed: %lu\n", Name, GetLastError());
        return NULL;
    }
    Sid = HeapAlloc(GetProcessHeap(), 0, GetLengthSid(Buffer));
    if (Sid)
        CopySid(GetLengthSid(Buffer), Sid, Buffer);
    return Sid;
}

static VOID
TestDefaultRights(VOID)
{
    NTSTATUS Status;

    ExpectLogOn(Interactive, STATUS_SUCCESS, "User");
    ExpectLogOn(Network, STATUS_SUCCESS, "User");
    ExpectLogOn(NetworkCleartext, STATUS_SUCCESS, "User");
    ExpectLogOn(Unlock, STATUS_SUCCESS, "User");
    ExpectLogOn(Batch, STATUS_LOGON_TYPE_NOT_GRANTED, "User");
    todo_if(is_reactos()) ExpectLogOn(Service, STATUS_LOGON_TYPE_NOT_GRANTED, "User");
    ExpectLogOn(RemoteInteractive, STATUS_LOGON_TYPE_NOT_GRANTED, "User");

    Status = TryLogOn(Batch, L"Wrong-Password!1");
    ok(Status == STATUS_LOGON_FAILURE, "A wrong password for a logon type not granted returned 0x%08lx\n", Status);
}

static VOID
TestGrantedRights(VOID)
{
    SetRight(UserSid, L"SeBatchLogonRight", TRUE);
    SetRight(UserSid, L"SeServiceLogonRight", TRUE);
    ExpectLogOn(Batch, STATUS_SUCCESS, "Batch right");
    ExpectLogOn(Service, STATUS_SUCCESS, "Service right");
    SetRight(UserSid, L"SeBatchLogonRight", FALSE);
    SetRight(UserSid, L"SeServiceLogonRight", FALSE);
    ExpectLogOn(Batch, STATUS_LOGON_TYPE_NOT_GRANTED, "Batch right removed");
    todo_if(is_reactos()) ExpectLogOn(Service, STATUS_LOGON_TYPE_NOT_GRANTED, "Service right removed");

    SetRight(GroupSid, L"SeBatchLogonRight", TRUE);
    ExpectLogOn(Batch, STATUS_SUCCESS, "Batch right of a group");
    SetRight(UserSid, L"SeDenyBatchLogonRight", TRUE);
    ExpectLogOn(Batch, STATUS_LOGON_TYPE_NOT_GRANTED, "Batch right of a group, denied to the user");
    SetRight(UserSid, L"SeDenyBatchLogonRight", FALSE);
    SetRight(GroupSid, L"SeBatchLogonRight", FALSE);
}

static BOOL
SetAdministrator(BOOL Add)
{
    SID_IDENTIFIER_AUTHORITY Nt = { SECURITY_NT_AUTHORITY };
    LOCALGROUP_MEMBERS_INFO_3 Member;
    WCHAR Name[GNLEN + 1], Domain[DNLEN + 1];
    DWORD NameLength = ARRAYSIZE(Name), DomainLength = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    NET_API_STATUS Error;
    PSID Sid;
    BOOL Found;

    if (!AllocateAndInitializeSid(&Nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &Sid))
        return FALSE;
    Found = LookupAccountSidW(NULL, Sid, Name, &NameLength, Domain, &DomainLength, &Use);
    FreeSid(Sid);
    ok(Found, "The Administrators group has no name: %lu\n", GetLastError());
    if (!Found)
        return FALSE;
    Member.lgrmi3_domainandname = User;
    Error = Add ? NetLocalGroupAddMembers(NULL, Name, 3, (LPBYTE)&Member, 1)
                : NetLocalGroupDelMembers(NULL, Name, 3, (LPBYTE)&Member, 1);
    ok(Error == NERR_Success, "Changing the membership of %ls failed with %lu\n", Name, Error);
    return Error == NERR_Success;
}

static VOID
TestAdministratorRights(VOID)
{
    if (!SetAdministrator(TRUE))
        return;
    ExpectLogOn(Batch, STATUS_SUCCESS, "Administrator");
    ExpectLogOn(RemoteInteractive, STATUS_SUCCESS, "Administrator");
    todo_if(is_reactos()) ExpectLogOn(Service, STATUS_LOGON_TYPE_NOT_GRANTED, "Administrator");
    SetAdministrator(FALSE);
}

static VOID
TestDeniedRights(VOID)
{
    SetRight(UserSid, L"SeDenyInteractiveLogonRight", TRUE);
    ExpectLogOn(Interactive, STATUS_LOGON_TYPE_NOT_GRANTED, "Interactive denied");
    ExpectLogOn(Unlock, STATUS_LOGON_TYPE_NOT_GRANTED, "Interactive denied");
    ExpectLogOn(Network, STATUS_SUCCESS, "Interactive denied");
    SetRight(UserSid, L"SeDenyInteractiveLogonRight", FALSE);

    SetRight(GroupSid, L"SeDenyNetworkLogonRight", TRUE);
    ExpectLogOn(Network, STATUS_LOGON_TYPE_NOT_GRANTED, "Network denied to a group");
    ExpectLogOn(NetworkCleartext, STATUS_LOGON_TYPE_NOT_GRANTED, "Network denied to a group");
    ExpectLogOn(Interactive, STATUS_SUCCESS, "Network denied to a group");
    SetRight(GroupSid, L"SeDenyNetworkLogonRight", FALSE);

    ExpectLogOn(Interactive, STATUS_SUCCESS, "Rights restored");
    ExpectLogOn(Network, STATUS_SUCCESS, "Rights restored");
}

START_TEST(LogonRights)
{
    static WCHAR Password[] = PASSWORD;
    LSA_STRING PackageName = { sizeof(MSV1_0_PACKAGE_NAME) - 1, sizeof(MSV1_0_PACKAGE_NAME), MSV1_0_PACKAGE_NAME };
    LSA_OBJECT_ATTRIBUTES Attributes = { sizeof(Attributes) };
    LOCALGROUP_MEMBERS_INFO_3 Member;
    LOCALGROUP_INFO_0 GroupInfo;
    USER_INFO_1 Info = { 0 };
    NET_API_STATUS Error;
    NTSTATUS Status;

    _snwprintf(User, ARRAYSIZE(User), L"lr%06lx", GetTickCount() & 0xffffff);
    _snwprintf(Group, ARRAYSIZE(Group), L"lg%06lx", GetTickCount() & 0xffffff);
    Info.usri1_name = User;
    Info.usri1_password = Password;
    Info.usri1_priv = USER_PRIV_USER;
    Info.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    Error = NetUserAdd(NULL, 1, (LPBYTE)&Info, NULL);
    if (Error != NERR_Success)
    {
        skip("NetUserAdd failed with %lu\n", Error);
        return;
    }
    GroupInfo.lgrpi0_name = Group;
    Error = NetLocalGroupAdd(NULL, 0, (LPBYTE)&GroupInfo, NULL);
    ok(Error == NERR_Success, "NetLocalGroupAdd failed with %lu\n", Error);
    Member.lgrmi3_domainandname = User;
    if (Error == NERR_Success)
    {
        Error = NetLocalGroupAddMembers(NULL, Group, 3, (LPBYTE)&Member, 1);
        ok(Error == NERR_Success, "NetLocalGroupAddMembers failed with %lu\n", Error);
    }
    UserSid = LookupSid(User);
    GroupSid = LookupSid(Group);

    Status = LsaOpenPolicy(NULL, &Attributes, POLICY_LOOKUP_NAMES | POLICY_CREATE_ACCOUNT, &Policy);
    ok(Status == STATUS_SUCCESS, "LsaOpenPolicy returned 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = LsaConnectUntrusted(&Lsa);
        ok(Status == STATUS_SUCCESS, "LsaConnectUntrusted returned 0x%08lx\n", Status);
    }
    if (NT_SUCCESS(Status))
    {
        Status = LsaLookupAuthenticationPackage(Lsa, &PackageName, &Package);
        ok(Status == STATUS_SUCCESS, "LsaLookupAuthenticationPackage returned 0x%08lx\n", Status);
        if (NT_SUCCESS(Status) && UserSid && GroupSid && Error == NERR_Success)
        {
            TestDefaultRights();
            TestGrantedRights();
            TestDeniedRights();
            TestAdministratorRights();
        }
        LsaDeregisterLogonProcess(Lsa);
    }
    if (Policy)
    {
        if (UserSid)
            LsaRemoveAccountRights(Policy, UserSid, TRUE, NULL, 0);
        if (GroupSid)
            LsaRemoveAccountRights(Policy, GroupSid, TRUE, NULL, 0);
        LsaClose(Policy);
    }
    NetLocalGroupDel(NULL, Group);
    NetUserDel(NULL, User);
    HeapFree(GetProcessHeap(), 0, UserSid);
    HeapFree(GetProcessHeap(), 0, GroupSid);
}
