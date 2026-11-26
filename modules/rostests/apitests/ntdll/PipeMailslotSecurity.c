/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for named pipe and mailslot security descriptors
 */

#include "precomp.h"

static
PVOID
QueryTokenInfo(
    _In_ TOKEN_INFORMATION_CLASS InfoClass)
{
    HANDLE Token = NULL;
    DWORD Length = 0;
    PVOID Buffer = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return NULL;

    GetTokenInformation(Token, InfoClass, NULL, 0, &Length);
    if (Length != 0)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
        if (Buffer != NULL && !GetTokenInformation(Token, InfoClass, Buffer, Length, &Length))
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
            Buffer = NULL;
        }
    }

    CloseHandle(Token);
    return Buffer;
}

static
PSECURITY_DESCRIPTOR
QueryKernelSd(
    _In_ HANDLE Handle,
    _In_ SECURITY_INFORMATION Info,
    _Out_ PDWORD OutLength)
{
    PSECURITY_DESCRIPTOR Sd;
    DWORD Needed = 0;
    DWORD Returned = 0;

    *OutLength = 0;
    SetLastError(0xdeadbeef);
    ok(!GetKernelObjectSecurity(Handle, Info, NULL, 0, &Needed),
       "GetKernelObjectSecurity(NULL, 0) unexpectedly succeeded\n");
    ok_dec(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    ok(Needed >= sizeof(SECURITY_DESCRIPTOR_RELATIVE), "Needed %lu\n", Needed);
    if (Needed == 0)
        return NULL;

    Sd = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Needed);
    if (Sd == NULL)
        return NULL;

    SetLastError(0xdeadbeef);
    if (!GetKernelObjectSecurity(Handle, Info, Sd, Needed, &Returned))
    {
        ok(0, "GetKernelObjectSecurity failed (%lu)\n", GetLastError());
        RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);
        return NULL;
    }

    ok_dec(Returned, 0);
    ok(IsValidSecurityDescriptor(Sd), "Security descriptor is invalid\n");
    *OutLength = Needed;
    return Sd;
}

static
BOOL
BuildTwoAceDaclSd(
    _Out_ PSECURITY_DESCRIPTOR AbsoluteSd,
    _Out_writes_bytes_(DaclBufferSize) PACL Dacl,
    _In_ DWORD DaclBufferSize,
    _In_ PSID Sid1,
    _In_ DWORD Mask1,
    _In_ PSID Sid2,
    _In_ DWORD Mask2)
{
    if (!InitializeAcl(Dacl, DaclBufferSize, ACL_REVISION))
        return FALSE;
    if (!AddAccessAllowedAce(Dacl, ACL_REVISION, Mask1, Sid1))
        return FALSE;
    if (!AddAccessAllowedAce(Dacl, ACL_REVISION, Mask2, Sid2))
        return FALSE;
    if (!InitializeSecurityDescriptor(AbsoluteSd, SECURITY_DESCRIPTOR_REVISION))
        return FALSE;
    return SetSecurityDescriptorDacl(AbsoluteSd, TRUE, Dacl, FALSE);
}

static
VOID
CheckAce(
    _In_ PACL Dacl,
    _In_ DWORD Index,
    _In_ PSID ExpectedSid,
    _In_ DWORD ExpectedMask,
    _In_ PCSTR Label)
{
    PACCESS_ALLOWED_ACE Ace = NULL;

    if (!GetAce(Dacl, Index, (PVOID *)&Ace))
    {
        ok(0, "%s: GetAce(%lu) failed (%lu)\n", Label, Index, GetLastError());
        return;
    }

    ok(Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE,
       "%s: ACE %lu type %u\n", Label, Index, Ace->Header.AceType);
    ok(Ace->Mask == ExpectedMask,
       "%s: ACE %lu mask 0x%08lx, expected 0x%08lx\n", Label, Index, Ace->Mask, ExpectedMask);
    ok(EqualSid((PSID)&Ace->SidStart, ExpectedSid),
       "%s: ACE %lu SID mismatch\n", Label, Index);
}

static
VOID
TestNamedPipeSecurity(
    _In_ PSID TokenOwnerSid,
    _In_ PSID TokenGroupSid,
    _In_ PSID SystemSid,
    _In_ PSID AdminsSid,
    _In_ PSID WorldSid,
    _In_ PSID AnonSid)
{
    WCHAR PipeName[96];
    HANDLE Server, Client;
    PSECURITY_DESCRIPTOR Sd;
    DWORD Length = 0;
    PSID Owner = NULL, Group = NULL;
    PACL Dacl = NULL;
    BOOL Defaulted = FALSE, Present = FALSE;
    SECURITY_DESCRIPTOR CustomSd;
    UCHAR CustomAclBuf[256];
    ACL_SIZE_INFORMATION AclInfo;
    ULONG Needed = 0;
    NTSTATUS Status;

    StringCchPrintfW(PipeName, _countof(PipeName), L"\\\\.\\pipe\\lnt_sec_%lu", GetCurrentProcessId());

    Server = CreateNamedPipeW(PipeName,
                              PIPE_ACCESS_DUPLEX | WRITE_DAC | WRITE_OWNER,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                              2, 4096, 4096, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateNamedPipeW failed (%lu)\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE)
        return;

    Client = CreateFileW(PipeName,
                         GENERIC_READ | GENERIC_WRITE | READ_CONTROL | WRITE_DAC,
                         0, NULL, OPEN_EXISTING, 0, NULL);
    ok(Client != INVALID_HANDLE_VALUE, "CreateFileW(pipe) failed (%lu)\n", GetLastError());
    if (Client == INVALID_HANDLE_VALUE)
    {
        CloseHandle(Server);
        return;
    }

    Sd = QueryKernelSd(Server,
                       OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                       &Length);
    if (Sd != NULL)
    {
        ok(GetSecurityDescriptorOwner(Sd, &Owner, &Defaulted) && Owner != NULL,
           "GetSecurityDescriptorOwner failed\n");
        if (Owner != NULL)
            ok(EqualSid(Owner, TokenOwnerSid), "Pipe default owner does not match token owner\n");

        ok(GetSecurityDescriptorGroup(Sd, &Group, &Defaulted) && Group != NULL,
           "GetSecurityDescriptorGroup failed\n");
        if (Group != NULL)
            ok(EqualSid(Group, TokenGroupSid), "Pipe default group does not match token primary group\n");

        ok(GetSecurityDescriptorDacl(Sd, &Present, &Dacl, &Defaulted) && Present && Dacl != NULL,
           "Pipe has no default DACL\n");
        if (Present && Dacl != NULL)
        {
            ok(GetAclInformation(Dacl, &AclInfo, sizeof(AclInfo), AclSizeInformation),
               "GetAclInformation failed\n");
            ok_dec(AclInfo.AceCount, 5);
            if (AclInfo.AceCount == 5)
            {
                CheckAce(Dacl, 0, SystemSid, FILE_ALL_ACCESS, "pipe default");
                CheckAce(Dacl, 1, AdminsSid, FILE_ALL_ACCESS, "pipe default");
                CheckAce(Dacl, 2, TokenOwnerSid, FILE_ALL_ACCESS, "pipe default");
                CheckAce(Dacl, 3, WorldSid, FILE_GENERIC_READ, "pipe default");
                CheckAce(Dacl, 4, AnonSid, FILE_GENERIC_READ, "pipe default");
            }
        }
        RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);
    }

    ok(BuildTwoAceDaclSd(&CustomSd, (PACL)CustomAclBuf, sizeof(CustomAclBuf),
                         SystemSid, FILE_ALL_ACCESS, TokenOwnerSid, FILE_ALL_ACCESS),
       "BuildTwoAceDaclSd failed (%lu)\n", GetLastError());
    ok(SetKernelObjectSecurity(Server, DACL_SECURITY_INFORMATION, &CustomSd),
       "SetKernelObjectSecurity(pipe server) failed (%lu)\n", GetLastError());

    Sd = QueryKernelSd(Client, DACL_SECURITY_INFORMATION, &Length);
    if (Sd != NULL)
    {
        ok(GetSecurityDescriptorDacl(Sd, &Present, &Dacl, &Defaulted) && Present && Dacl != NULL,
           "Client pipe has no DACL\n");
        if (Present && Dacl != NULL)
        {
            ok(GetAclInformation(Dacl, &AclInfo, sizeof(AclInfo), AclSizeInformation),
               "GetAclInformation(client) failed\n");
            ok_dec(AclInfo.AceCount, 2);
            if (AclInfo.AceCount == 2)
            {
                CheckAce(Dacl, 0, SystemSid, FILE_ALL_ACCESS, "pipe updated");
                CheckAce(Dacl, 1, TokenOwnerSid, FILE_ALL_ACCESS, "pipe updated");
            }
        }
        RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);
    }

    ok(DisconnectNamedPipe(Server), "DisconnectNamedPipe failed (%lu)\n", GetLastError());

    Status = NtQuerySecurityObject(Client, OWNER_SECURITY_INFORMATION, NULL, 0, &Needed);
    ok_hex(Status, STATUS_PIPE_DISCONNECTED);

    Status = NtSetSecurityObject(Client, DACL_SECURITY_INFORMATION, &CustomSd);
    ok_hex(Status, STATUS_PIPE_DISCONNECTED);

    Sd = QueryKernelSd(Server, OWNER_SECURITY_INFORMATION, &Length);
    if (Sd != NULL)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);

    CloseHandle(Client);
    CloseHandle(Server);
}

static
VOID
TestMailslotSecurity(
    _In_ PSID TokenOwnerSid,
    _In_ PSID TokenGroupSid,
    _In_ PSID SystemSid)
{
    WCHAR SlotName[96];
    HANDLE Server, Client;
    PSECURITY_DESCRIPTOR Sd;
    DWORD Length = 0;
    PSID Owner = NULL, Group = NULL;
    PACL Dacl = NULL;
    BOOL Defaulted = FALSE, Present = FALSE;
    SECURITY_DESCRIPTOR CustomSd;
    UCHAR CustomAclBuf[256];
    ACL_SIZE_INFORMATION AclInfo;
    SECURITY_ATTRIBUTES Sa;

    StringCchPrintfW(SlotName, _countof(SlotName), L"\\\\.\\mailslot\\lnt_sec_%lu", GetCurrentProcessId());

    Server = CreateMailslotW(SlotName, 64, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateMailslotW failed (%lu)\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE)
        return;

    Client = CreateFileW(SlotName,
                         GENERIC_WRITE | READ_CONTROL,
                         FILE_SHARE_READ,
                         NULL, OPEN_EXISTING, 0, NULL);
    ok(Client != INVALID_HANDLE_VALUE, "CreateFileW(mailslot) failed (%lu)\n", GetLastError());

    Sd = QueryKernelSd(Server,
                       OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                       &Length);
    if (Sd != NULL)
    {
        ok(GetSecurityDescriptorOwner(Sd, &Owner, &Defaulted) && Owner != NULL,
           "Mailslot has no owner\n");
        if (Owner != NULL)
            ok(EqualSid(Owner, TokenOwnerSid), "Mailslot default owner does not match token owner\n");

        ok(GetSecurityDescriptorGroup(Sd, &Group, &Defaulted) && Group != NULL,
           "Mailslot has no group\n");
        if (Group != NULL)
            ok(EqualSid(Group, TokenGroupSid), "Mailslot default group does not match token primary group\n");

        ok(GetSecurityDescriptorDacl(Sd, &Present, &Dacl, &Defaulted) && Present,
           "Mailslot has no default DACL\n");
        RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);
    }

    ok(BuildTwoAceDaclSd(&CustomSd, (PACL)CustomAclBuf, sizeof(CustomAclBuf),
                         SystemSid, FILE_ALL_ACCESS, TokenOwnerSid, FILE_ALL_ACCESS),
       "BuildTwoAceDaclSd(mailslot) failed (%lu)\n", GetLastError());
    ok(SetKernelObjectSecurity(Server, DACL_SECURITY_INFORMATION, &CustomSd),
       "SetKernelObjectSecurity(mailslot) failed (%lu)\n", GetLastError());

    if (Client != INVALID_HANDLE_VALUE)
    {
        Length = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        ok(!GetKernelObjectSecurity(Client, DACL_SECURITY_INFORMATION, NULL, 0, &Length),
           "GetKernelObjectSecurity(mailslot client) unexpectedly succeeded\n");
        ok_dec(GetLastError(), ERROR_HANDLE_EOF);
        ok_dec(Length, 0);
        CloseHandle(Client);
    }

    Sd = QueryKernelSd(Server, DACL_SECURITY_INFORMATION, &Length);
    if (Sd != NULL)
    {
        ok(GetSecurityDescriptorDacl(Sd, &Present, &Dacl, &Defaulted) && Present && Dacl != NULL,
           "Mailslot server has no DACL\n");
        if (Present && Dacl != NULL)
        {
            ok(GetAclInformation(Dacl, &AclInfo, sizeof(AclInfo), AclSizeInformation),
               "GetAclInformation(mailslot server) failed\n");
            ok_dec(AclInfo.AceCount, 2);
            if (AclInfo.AceCount == 2)
            {
                CheckAce(Dacl, 0, SystemSid, FILE_ALL_ACCESS, "mailslot updated");
                CheckAce(Dacl, 1, TokenOwnerSid, FILE_ALL_ACCESS, "mailslot updated");
            }
        }
        RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);
    }

    CloseHandle(Server);

    StringCchPrintfW(SlotName, _countof(SlotName), L"\\\\.\\mailslot\\lnt_sec_explicit_%lu", GetCurrentProcessId());
    Sa.nLength = sizeof(Sa);
    Sa.lpSecurityDescriptor = &CustomSd;
    Sa.bInheritHandle = FALSE;

    Server = CreateMailslotW(SlotName, 64, 0, &Sa);
    ok(Server != INVALID_HANDLE_VALUE, "CreateMailslotW(explicit SD) failed (%lu)\n", GetLastError());
    if (Server != INVALID_HANDLE_VALUE)
    {
        Sd = QueryKernelSd(Server, DACL_SECURITY_INFORMATION, &Length);
        if (Sd != NULL)
        {
            ok(GetSecurityDescriptorDacl(Sd, &Present, &Dacl, &Defaulted) && Present && Dacl != NULL,
               "Explicit mailslot has no DACL\n");
            if (Present && Dacl != NULL)
            {
                ok(GetAclInformation(Dacl, &AclInfo, sizeof(AclInfo), AclSizeInformation),
                   "GetAclInformation(explicit mailslot) failed\n");
                ok_dec(AclInfo.AceCount, 2);
            }
            RtlFreeHeap(RtlGetProcessHeap(), 0, Sd);
        }
        CloseHandle(Server);
    }
}

START_TEST(PipeMailslotSecurity)
{
    PTOKEN_OWNER TokenOwnerInfo;
    PTOKEN_PRIMARY_GROUP TokenGroupInfo;
    UCHAR SystemSid[SECURITY_MAX_SID_SIZE];
    UCHAR AdminsSid[SECURITY_MAX_SID_SIZE];
    UCHAR WorldSid[SECURITY_MAX_SID_SIZE];
    UCHAR AnonSid[SECURITY_MAX_SID_SIZE];
    DWORD Size;

    TokenOwnerInfo = QueryTokenInfo(TokenOwner);
    TokenGroupInfo = QueryTokenInfo(TokenPrimaryGroup);
    ok(TokenOwnerInfo != NULL && TokenGroupInfo != NULL, "Failed to query token owner/group\n");
    if (TokenOwnerInfo == NULL || TokenGroupInfo == NULL)
        goto Cleanup;

    Size = sizeof(SystemSid);
    ok(CreateWellKnownSid(WinLocalSystemSid, NULL, SystemSid, &Size), "WinLocalSystemSid failed\n");
    Size = sizeof(AdminsSid);
    ok(CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, AdminsSid, &Size), "WinBuiltinAdministratorsSid failed\n");
    Size = sizeof(WorldSid);
    ok(CreateWellKnownSid(WinWorldSid, NULL, WorldSid, &Size), "WinWorldSid failed\n");
    Size = sizeof(AnonSid);
    ok(CreateWellKnownSid(WinAnonymousSid, NULL, AnonSid, &Size), "WinAnonymousSid failed\n");

    TestNamedPipeSecurity(TokenOwnerInfo->Owner, TokenGroupInfo->PrimaryGroup,
                          SystemSid, AdminsSid, WorldSid, AnonSid);
    TestMailslotSecurity(TokenOwnerInfo->Owner, TokenGroupInfo->PrimaryGroup, SystemSid);

Cleanup:
    if (TokenGroupInfo != NULL)
        RtlFreeHeap(RtlGetProcessHeap(), 0, TokenGroupInfo);
    if (TokenOwnerInfo != NULL)
        RtlFreeHeap(RtlGetProcessHeap(), 0, TokenOwnerInfo);
}
