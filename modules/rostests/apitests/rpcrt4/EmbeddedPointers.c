/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test RPC calls passing counted strings embedded in other structures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <wchar.h>
#include "embedded_s.h"

#define ENDPOINT L"LiberNTEmbeddedPointers"
#define SID_SIZE(Count) FIELD_OFFSET(SID_BLOB, SubAuthority[Count])

#define MISMATCH_NAME     0x01
#define MISMATCH_SID      0x02
#define MISMATCH_FULLNAME 0x04
#define MISMATCH_NUMBERS  0x08
#define MISMATCH_COMMENT  0x10
#define MISMATCH_CLASS    0x20

static RPC_BINDING_HANDLE Binding;
static const INFO *ExpectedInfo;

void * __RPC_USER MIDL_user_allocate(SIZE_T Size)
{
    return HeapAlloc(GetProcessHeap(), 0, Size);
}

void __RPC_USER MIDL_user_free(void *Pointer)
{
    HeapFree(GetProcessHeap(), 0, Pointer);
}

static VOID
InitString(COUNTED_STRING *String, PCWSTR Text)
{
    if (!Text)
    {
        String->Length = String->MaximumLength = 0;
        String->Buffer = NULL;
        return;
    }
    String->Length = (unsigned short)(wcslen(Text) * sizeof(WCHAR));
    String->MaximumLength = String->Length + sizeof(WCHAR);
    String->Buffer = (wchar_t *)Text;
}

static VOID
CopyString(COUNTED_STRING *String, PCWSTR Text)
{
    InitString(String, Text);
    if (!Text)
        return;
    String->Buffer = MIDL_user_allocate(String->MaximumLength);
    if (String->Buffer)
        memcpy(String->Buffer, Text, String->MaximumLength);
}

static BOOL
StringEquals(const COUNTED_STRING *String, const COUNTED_STRING *Expected)
{
    if (String->Length != Expected->Length || String->MaximumLength != Expected->MaximumLength)
        return FALSE;
    if (!Expected->Buffer)
        return String->Buffer == NULL;
    return String->Buffer && !memcmp(String->Buffer, Expected->Buffer, Expected->Length);
}

static PSID_BLOB
CreateSid(unsigned long LastSubAuthority, BOOL UseMidlAllocator)
{
    static const SID_AUTHORITY NtAuthority = { { 0, 0, 0, 0, 0, 5 } };
    PSID_BLOB Sid;

    if (UseMidlAllocator)
        Sid = MIDL_user_allocate(SID_SIZE(4));
    else
        Sid = HeapAlloc(GetProcessHeap(), 0, SID_SIZE(4));
    if (!Sid)
        return NULL;
    Sid->Revision = 1;
    Sid->SubAuthorityCount = 4;
    Sid->IdentifierAuthority = NtAuthority;
    Sid->SubAuthority[0] = 21;
    Sid->SubAuthority[1] = 1000;
    Sid->SubAuthority[2] = 2000;
    Sid->SubAuthority[3] = LastSubAuthority;
    return Sid;
}

static BOOL
SidEquals(const SID_BLOB *Sid, const SID_BLOB *Expected)
{
    if (!Expected)
        return Sid == NULL;
    return Sid && Sid->SubAuthorityCount == Expected->SubAuthorityCount &&
           !memcmp(Sid, Expected, SID_SIZE(Expected->SubAuthorityCount));
}

static long
CompareInfo(unsigned long Class, const INFO *Info, const INFO *Expected)
{
    long Mismatch = 0;

    switch (Class)
    {
        case 1:
            if (!StringEquals(&Info->Domain.Name, &Expected->Domain.Name))
                Mismatch |= MISMATCH_NAME;
            if (!SidEquals(Info->Domain.Sid, Expected->Domain.Sid))
                Mismatch |= MISMATCH_SID;
            break;

        case 2:
            if (!StringEquals(&Info->Name.UserName, &Expected->Name.UserName))
                Mismatch |= MISMATCH_NAME;
            if (!StringEquals(&Info->Name.FullName, &Expected->Name.FullName))
                Mismatch |= MISMATCH_FULLNAME;
            break;

        case 3:
            if (Info->Display.Index != Expected->Display.Index ||
                Info->Display.Rid != Expected->Display.Rid ||
                Info->Display.AccountControl != Expected->Display.AccountControl)
            {
                Mismatch |= MISMATCH_NUMBERS;
            }
            if (!StringEquals(&Info->Display.AccountName, &Expected->Display.AccountName))
                Mismatch |= MISMATCH_NAME;
            if (!StringEquals(&Info->Display.AdminComment, &Expected->Display.AdminComment))
                Mismatch |= MISMATCH_COMMENT;
            if (!StringEquals(&Info->Display.FullName, &Expected->Display.FullName))
                Mismatch |= MISMATCH_FULLNAME;
            break;

        default:
            Mismatch |= MISMATCH_CLASS;
            break;
    }
    return Mismatch;
}

static VOID
InitDisplayEntry(DISPLAY_ENTRY *Entry, unsigned long Index, BOOL Copy)
{
    static const PCWSTR Names[] = { L"Administrator", L"Guest", L"DefaultAccount", L"WDAGUtilityAccount" };
    static const PCWSTR Comments[] = { L"Built-in account for administering the computer/domain",
                                       L"Built-in account for guest access to the computer/domain",
                                       L"A user account managed by the system.", L"" };
    static const PCWSTR FullNames[] = { L"Local Administrator", NULL, L"Default", NULL };
    unsigned long Slot = Index % ARRAYSIZE(Names);

    Entry->Index = Index + 7;
    Entry->Rid = 500 + Index;
    Entry->AccountControl = 0x210 | Index;
    if (Copy)
    {
        CopyString(&Entry->AccountName, Names[Slot]);
        CopyString(&Entry->AdminComment, Comments[Slot]);
        CopyString(&Entry->FullName, FullNames[Slot]);
    }
    else
    {
        InitString(&Entry->AccountName, Names[Slot]);
        InitString(&Entry->AdminComment, Comments[Slot]);
        InitString(&Entry->FullName, FullNames[Slot]);
    }
}

static VOID
InitDomainInfo(DOMAIN_INFO *Domain, unsigned long Index, BOOL Copy)
{
    static const PCWSTR Names[] = { L"LIBERNT-PC", L"BUILTIN", L"NT AUTHORITY" };
    PCWSTR Name = Names[Index % ARRAYSIZE(Names)];

    if (Copy)
        CopyString(&Domain->Name, Name);
    else
        InitString(&Domain->Name, Name);
    Domain->Sid = (Index % 2) ? NULL : CreateSid(Index, Copy);
}

static VOID
InitInfo(unsigned long Class, INFO *Info, BOOL Copy)
{
    switch (Class)
    {
        case 1:
            InitDomainInfo(&Info->Domain, 0, Copy);
            break;
        case 2:
            if (Copy)
            {
                CopyString(&Info->Name.UserName, L"Administrator");
                CopyString(&Info->Name.FullName, L"Local Administrator");
            }
            else
            {
                InitString(&Info->Name.UserName, L"Administrator");
                InitString(&Info->Name.FullName, L"Local Administrator");
            }
            break;
        case 3:
            InitDisplayEntry(&Info->Display, 0, Copy);
            break;
    }
}

static VOID
FreeDisplayEntry(DISPLAY_ENTRY *Entry)
{
    MIDL_user_free(Entry->AccountName.Buffer);
    MIDL_user_free(Entry->AdminComment.Buffer);
    MIDL_user_free(Entry->FullName.Buffer);
}

static VOID
FreeDomainInfo(DOMAIN_INFO *Domain)
{
    MIDL_user_free(Domain->Name.Buffer);
    MIDL_user_free(Domain->Sid);
}

static VOID
FreeInfo(unsigned long Class, INFO *Info)
{
    switch (Class)
    {
        case 1:
            FreeDomainInfo(&Info->Domain);
            break;
        case 2:
            MIDL_user_free(Info->Name.UserName.Buffer);
            MIDL_user_free(Info->Name.FullName.Buffer);
            break;
        case 3:
            FreeDisplayEntry(&Info->Display);
            break;
    }
}

long s_SetInfo(handle_t BindingHandle, unsigned long Class, INFO *Info)
{
    if (!ExpectedInfo)
        return MISMATCH_CLASS;
    return CompareInfo(Class, Info, ExpectedInfo);
}

long s_QueryInfo(handle_t BindingHandle, unsigned long Class, INFO **Info)
{
    *Info = MIDL_user_allocate(sizeof(**Info));
    if (!*Info)
        return -1;
    InitInfo(Class, *Info, TRUE);
    return 0;
}

long s_QueryDomains(handle_t BindingHandle, unsigned long Count, DOMAIN_LIST **List)
{
    unsigned long Index;

    *List = MIDL_user_allocate(sizeof(**List));
    if (!*List)
        return -1;
    (*List)->Entries = Count;
    (*List)->MaxEntries = Count + 5;
    (*List)->Domains = MIDL_user_allocate(Count * sizeof(DOMAIN_INFO));
    for (Index = 0; Index < Count; Index++)
        InitDomainInfo(&(*List)->Domains[Index], Index, TRUE);
    return 0;
}

long s_QueryDisplay(handle_t BindingHandle, unsigned long Count, DISPLAY_BUFFER *Buffer)
{
    unsigned long Index;

    Buffer->EntriesRead = Count;
    Buffer->Buffer = MIDL_user_allocate(Count * sizeof(DISPLAY_ENTRY));
    for (Index = 0; Index < Count; Index++)
        InitDisplayEntry(&Buffer->Buffer[Index], Index, TRUE);
    return 0;
}

static VOID
TestSetInfo(unsigned long Class, const INFO *Info, PCSTR What)
{
    unsigned long Code = 0;
    long Ret = -1;

    ExpectedInfo = Info;
    RpcTryExcept
    {
        Ret = SetInfo(Binding, Class, (INFO *)Info);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = RpcExceptionCode();
    }
    RpcEndExcept
    ExpectedInfo = NULL;

    ok(Code == 0, "SetInfo %s raised %lu\n", What, Code);
    ok(Ret == 0, "SetInfo %s: server saw mismatches 0x%lx\n", What, Ret);
}

static VOID
TestSetInfos(VOID)
{
    INFO Info;

    InitInfo(1, &Info, FALSE);
    TestSetInfo(1, &Info, "domain");
    HeapFree(GetProcessHeap(), 0, Info.Domain.Sid);

    Info.Domain.Sid = NULL;
    TestSetInfo(1, &Info, "domain without SID");

    InitInfo(2, &Info, FALSE);
    TestSetInfo(2, &Info, "name");

    InitString(&Info.Name.FullName, NULL);
    TestSetInfo(2, &Info, "name without full name");

    InitInfo(3, &Info, FALSE);
    TestSetInfo(3, &Info, "display entry");

    InitString(&Info.Display.AdminComment, NULL);
    InitString(&Info.Display.FullName, L"X");
    TestSetInfo(3, &Info, "display entry without comment");
}

static VOID
TestQueryInfo(unsigned long Class, PCSTR What)
{
    INFO *Info = NULL;
    INFO Expected;
    unsigned long Code = 0;
    long Ret = -1;

    RpcTryExcept
    {
        Ret = QueryInfo(Binding, Class, &Info);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = RpcExceptionCode();
    }
    RpcEndExcept

    ok(Code == 0, "QueryInfo %s raised %lu\n", What, Code);
    ok(Ret == 0, "QueryInfo %s returned %ld\n", What, Ret);
    ok(Info != NULL, "QueryInfo %s returned no data\n", What);
    if (Code || !Info)
        return;

    InitInfo(Class, &Expected, FALSE);
    ok(CompareInfo(Class, Info, &Expected) == 0, "QueryInfo %s: mismatches 0x%lx\n", What,
       CompareInfo(Class, Info, &Expected));
    if (Class == 1)
        HeapFree(GetProcessHeap(), 0, Expected.Domain.Sid);
    FreeInfo(Class, Info);
    MIDL_user_free(Info);
}

static VOID
TestQueryDomains(unsigned long Count)
{
    DOMAIN_LIST *List = NULL;
    DOMAIN_INFO Expected;
    unsigned long Code = 0, Index;
    long Ret = -1;

    RpcTryExcept
    {
        Ret = QueryDomains(Binding, Count, &List);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = RpcExceptionCode();
    }
    RpcEndExcept

    ok(Code == 0, "QueryDomains(%lu) raised %lu\n", Count, Code);
    ok(Ret == 0, "QueryDomains(%lu) returned %ld\n", Count, Ret);
    if (Code || !List)
    {
        ok(List != NULL, "QueryDomains(%lu) returned no list\n", Count);
        return;
    }

    ok(List->Entries == Count, "QueryDomains(%lu): %lu entries\n", Count, List->Entries);
    ok(List->MaxEntries == Count + 5, "QueryDomains(%lu): MaxEntries %lu\n", Count, List->MaxEntries);
    for (Index = 0; Index < List->Entries && Index < Count; Index++)
    {
        InitDomainInfo(&Expected, Index, FALSE);
        ok(StringEquals(&List->Domains[Index].Name, &Expected.Name),
           "QueryDomains(%lu): domain %lu name differs (length %u/%u)\n", Count, Index,
           List->Domains[Index].Name.Length, List->Domains[Index].Name.MaximumLength);
        ok(SidEquals(List->Domains[Index].Sid, Expected.Sid), "QueryDomains(%lu): domain %lu SID differs\n",
           Count, Index);
        HeapFree(GetProcessHeap(), 0, Expected.Sid);
        FreeDomainInfo(&List->Domains[Index]);
    }
    MIDL_user_free(List->Domains);
    MIDL_user_free(List);
}

static VOID
TestQueryDisplay(unsigned long Count)
{
    DISPLAY_BUFFER Buffer = { 0, NULL };
    DISPLAY_ENTRY Expected;
    INFO Received, ExpectedEntry;
    unsigned long Code = 0, Index;
    long Ret = -1;

    RpcTryExcept
    {
        Ret = QueryDisplay(Binding, Count, &Buffer);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = RpcExceptionCode();
    }
    RpcEndExcept

    ok(Code == 0, "QueryDisplay(%lu) raised %lu\n", Count, Code);
    ok(Ret == 0, "QueryDisplay(%lu) returned %ld\n", Count, Ret);
    if (Code)
        return;

    ok(Buffer.EntriesRead == Count, "QueryDisplay(%lu): %lu entries\n", Count, Buffer.EntriesRead);
    for (Index = 0; Index < Buffer.EntriesRead && Index < Count && Buffer.Buffer; Index++)
    {
        InitDisplayEntry(&Expected, Index, FALSE);
        Received.Display = Buffer.Buffer[Index];
        ExpectedEntry.Display = Expected;
        ok(CompareInfo(3, &Received, &ExpectedEntry) == 0, "QueryDisplay(%lu): entry %lu mismatches 0x%lx\n",
           Count, Index, CompareInfo(3, &Received, &ExpectedEntry));
        FreeDisplayEntry(&Buffer.Buffer[Index]);
    }
    MIDL_user_free(Buffer.Buffer);
}

START_TEST(EmbeddedPointers)
{
    RPC_WSTR StringBinding = NULL;
    RPC_STATUS Status;

    Status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (RPC_WSTR)ENDPOINT, NULL);
    ok(Status == RPC_S_OK, "RpcServerUseProtseqEpW returned %ld\n", Status);
    Status = RpcServerRegisterIf(s_EmbeddedPointers_v1_0_s_ifspec, NULL, NULL);
    ok(Status == RPC_S_OK, "RpcServerRegisterIf returned %ld\n", Status);
    Status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE);
    ok(Status == RPC_S_OK, "RpcServerListen returned %ld\n", Status);
    if (Status != RPC_S_OK)
        return;

    Status = RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL, (RPC_WSTR)ENDPOINT, NULL, &StringBinding);
    ok(Status == RPC_S_OK, "RpcStringBindingComposeW returned %ld\n", Status);
    Status = RpcBindingFromStringBindingW(StringBinding, &Binding);
    ok(Status == RPC_S_OK, "RpcBindingFromStringBindingW returned %ld\n", Status);
    RpcStringFreeW(&StringBinding);

    if (Status == RPC_S_OK)
    {
        TestSetInfos();
        TestQueryInfo(1, "domain");
        TestQueryInfo(2, "name");
        TestQueryInfo(3, "display entry");
        TestQueryDomains(0);
        TestQueryDomains(1);
        TestQueryDomains(5);
        TestQueryDisplay(0);
        TestQueryDisplay(1);
        TestQueryDisplay(6);
        RpcBindingFree(&Binding);
    }

    Status = RpcMgmtStopServerListening(NULL);
    ok(Status == RPC_S_OK, "RpcMgmtStopServerListening returned %ld\n", Status);
    Status = RpcMgmtWaitServerListen();
    ok(Status == RPC_S_OK, "RpcMgmtWaitServerListen returned %ld\n", Status);
    Status = RpcServerUnregisterIf(NULL, NULL, FALSE);
    ok(Status == RPC_S_OK, "RpcServerUnregisterIf returned %ld\n", Status);
}
