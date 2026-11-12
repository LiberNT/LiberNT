/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the limits a name lookup puts on chained links and on registry path depth
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

#define LINK_CHAIN_LENGTH 70

static
NTSTATUS
CreateRegistryLink(
    _In_ HANDLE Parent,
    _In_ PCWSTR Name,
    _In_ PCWSTR Target,
    _Out_ PHANDLE Link)
{
    UNICODE_STRING String;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, Parent, NULL);
    Status = NtCreateKey(Link, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&String, L"SymbolicLinkValue");
    Status = NtSetValueKey(*Link, &String, 0, REG_LINK, (PVOID)Target, (ULONG)(wcslen(Target) * sizeof(WCHAR)));
    if (!NT_SUCCESS(Status))
    {
        NtDeleteKey(*Link);
        NtClose(*Link);
        *Link = NULL;
    }
    return Status;
}

static
NTSTATUS
OpenRegistryKey(
    _In_ HANDLE Parent,
    _In_ PCWSTR Name)
{
    UNICODE_STRING String;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;
    HANDLE Key;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Parent, NULL);
    Status = NtOpenKey(&Key, KEY_READ, &Attributes);
    if (NT_SUCCESS(Status))
        NtClose(Key);
    return Status;
}

static
VOID
TestRegistryLinks(VOID)
{
    UNICODE_STRING CurrentUser, String;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Parent = NULL, Target = NULL, Loop = NULL;
    HANDLE Links[LINK_CHAIN_LENGTH] = { NULL };
    WCHAR Path[256], Name[32], LinkTarget[300];
    NTSTATUS Status, FirstFailure = STATUS_SUCCESS;
    ULONG Index, Deepest = 0;

    Status = RtlFormatCurrentUserKeyPath(&CurrentUser);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%wZ\\LinkReparseLimit_%lu", &CurrentUser, GetCurrentProcessId());
    RtlFreeUnicodeString(&CurrentUser);

    RtlInitUnicodeString(&String, Path);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateKey(&Parent, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlInitUnicodeString(&String, L"Target");
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Parent, NULL);
    Status = NtCreateKey(&Target, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    for (Index = 0; Index < LINK_CHAIN_LENGTH; Index++)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"L%lu", Index);
        if (Index + 1 < LINK_CHAIN_LENGTH)
            StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\L%lu", Path, Index + 1);
        else
            StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\Target", Path);
        Status = CreateRegistryLink(Parent, Name, LinkTarget, &Links[Index]);
        ok(Status == STATUS_SUCCESS, "Creating registry link %lu failed: 0x%08lx\n", Index, Status);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    for (Index = LINK_CHAIN_LENGTH; Index > 0; Index--)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"L%lu", Index - 1);
        Status = OpenRegistryKey(Parent, Name);
        if (!NT_SUCCESS(Status))
        {
            FirstFailure = Status;
            break;
        }
        Deepest = LINK_CHAIN_LENGTH - Index + 1;
    }
    ok(Deepest == 63, "A lookup followed %lu chained registry links\n", Deepest);
    ok_ntstatus(FirstFailure, STATUS_OBJECT_NAME_NOT_FOUND);

    StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\Loop", Path);
    Status = CreateRegistryLink(Parent, L"Loop", LinkTarget, &Loop);
    ok(Status == STATUS_SUCCESS, "Creating the registry loop failed: 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
        ok_ntstatus(OpenRegistryKey(Parent, L"Loop"), STATUS_OBJECT_NAME_NOT_FOUND);

Cleanup:
    if (Loop != NULL)
    {
        NtDeleteKey(Loop);
        NtClose(Loop);
    }
    for (Index = 0; Index < LINK_CHAIN_LENGTH; Index++)
    {
        if (Links[Index] == NULL)
            continue;
        NtDeleteKey(Links[Index]);
        NtClose(Links[Index]);
    }
    if (Target != NULL)
    {
        NtDeleteKey(Target);
        NtClose(Target);
    }
    NtDeleteKey(Parent);
    NtClose(Parent);
}

static
VOID
TestRegistryLinkShapes(VOID)
{
    UNICODE_STRING CurrentUser, String;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Parent = NULL, Target = NULL, Sub = NULL, Mid = NULL, Nul = NULL, Grow = NULL, GrowNul = NULL;
    WCHAR Path[256], LinkTarget[300];
    NTSTATUS Status;
    ULONG Length;

    Status = RtlFormatCurrentUserKeyPath(&CurrentUser);
    if (!NT_SUCCESS(Status))
        return;
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%wZ\\LinkReparseShape_%lu", &CurrentUser, GetCurrentProcessId());
    RtlFreeUnicodeString(&CurrentUser);

    RtlInitUnicodeString(&String, Path);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateKey(&Parent, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    RtlInitUnicodeString(&String, L"Target");
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Parent, NULL);
    Status = NtCreateKey(&Target, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    RtlInitUnicodeString(&String, L"Sub");
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Target, NULL);
    Status = NtCreateKey(&Sub, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);

    StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\Target", Path);
    Status = CreateRegistryLink(Parent, L"Mid", LinkTarget, &Mid);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_ntstatus(OpenRegistryKey(Parent, L"Mid"), STATUS_SUCCESS);
    ok_ntstatus(OpenRegistryKey(Parent, L"Mid\\Sub"), STATUS_SUCCESS);

    RtlInitUnicodeString(&String, L"Nul");
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, Parent, NULL);
    Status = NtCreateKey(&Nul, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK, NULL);
    if (NT_SUCCESS(Status))
    {
        Length = (ULONG)((wcslen(LinkTarget) + 1) * sizeof(WCHAR));
        RtlInitUnicodeString(&String, L"SymbolicLinkValue");
        Status = NtSetValueKey(Nul, &String, 0, REG_LINK, LinkTarget, Length);
    }
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_ntstatus(OpenRegistryKey(Parent, L"Nul"), STATUS_OBJECT_NAME_NOT_FOUND);
    ok_ntstatus(OpenRegistryKey(Parent, L"Nul\\Sub"), STATUS_OBJECT_NAME_NOT_FOUND);

    StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\Grow\\Grow", Path);
    Status = CreateRegistryLink(Parent, L"Grow", LinkTarget, &Grow);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_ntstatus(OpenRegistryKey(Parent, L"Grow"), STATUS_INVALID_PARAMETER);

    RtlInitUnicodeString(&String, L"GrowNul");
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, Parent, NULL);
    Status = NtCreateKey(&GrowNul, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK, NULL);
    if (NT_SUCCESS(Status))
    {
        StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\GrowNul\\GrowNul", Path);
        Length = (ULONG)((wcslen(LinkTarget) + 1) * sizeof(WCHAR));
        RtlInitUnicodeString(&String, L"SymbolicLinkValue");
        Status = NtSetValueKey(GrowNul, &String, 0, REG_LINK, LinkTarget, Length);
    }
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_ntstatus(OpenRegistryKey(Parent, L"GrowNul"), STATUS_INVALID_PARAMETER);

    if (GrowNul != NULL) { NtDeleteKey(GrowNul); NtClose(GrowNul); }
    if (Grow != NULL) { NtDeleteKey(Grow); NtClose(Grow); }
    if (Nul != NULL) { NtDeleteKey(Nul); NtClose(Nul); }
    if (Mid != NULL) { NtDeleteKey(Mid); NtClose(Mid); }
    if (Sub != NULL) { NtDeleteKey(Sub); NtClose(Sub); }
    if (Target != NULL) { NtDeleteKey(Target); NtClose(Target); }
    NtDeleteKey(Parent);
    NtClose(Parent);
}

#define DEPTH_LEVELS 520

static HANDLE DepthKeys[DEPTH_LEVELS];
static WCHAR DepthName[DEPTH_LEVELS * 2 + 8];

static
VOID
TestRegistryDepth(VOID)
{
    UNICODE_STRING CurrentUser, String;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Parent = NULL, Previous;
    WCHAR Path[256];
    NTSTATUS Status, CreateFailure = STATUS_SUCCESS, OpenFailure = STATUS_SUCCESS, MultiFailure = STATUS_SUCCESS;
    ULONG Index, Created = 0, Opened = 0, MultiCreated = 0;
    HANDLE Key;

    Status = RtlFormatCurrentUserKeyPath(&CurrentUser);
    if (!NT_SUCCESS(Status))
        return;
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%wZ\\LinkReparseDepth_%lu", &CurrentUser, GetCurrentProcessId());
    RtlFreeUnicodeString(&CurrentUser);

    RtlInitUnicodeString(&String, Path);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateKey(&Parent, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Previous = Parent;
    RtlInitUnicodeString(&String, L"x");
    for (Index = 0; Index < DEPTH_LEVELS; Index++)
    {
        InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Previous, NULL);
        Status = NtCreateKey(&DepthKeys[Index], KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
        if (!NT_SUCCESS(Status))
        {
            DepthKeys[Index] = NULL;
            CreateFailure = Status;
            break;
        }
        Previous = DepthKeys[Index];
        Created = Index + 1;
    }
    ok(Created == 508, "%lu nested keys were created under a key 3 levels below \\Registry\n", Created);
    ok_ntstatus(CreateFailure, STATUS_INVALID_PARAMETER);

    DepthName[0] = L'x';
    DepthName[1] = 0;
    for (Index = 1; Index <= Created; Index++)
    {
        if (Index > 1)
            StringCchCatW(DepthName, ARRAYSIZE(DepthName), L"\\x");
        RtlInitUnicodeString(&String, DepthName);
        InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Parent, NULL);
        Status = NtOpenKey(&Key, KEY_READ, &Attributes);
        if (!NT_SUCCESS(Status))
        {
            OpenFailure = Status;
            break;
        }
        NtClose(Key);
        Opened = Index;
    }
    ok(Opened == 32, "One open walked %lu components\n", Opened);
    ok_ntstatus(OpenFailure, STATUS_INVALID_PARAMETER);

    DepthName[0] = L'y';
    DepthName[1] = 0;
    for (Index = 1; Index <= 64; Index++)
    {
        if (Index > 1)
            StringCchCatW(DepthName, ARRAYSIZE(DepthName), L"\\y");
        RtlInitUnicodeString(&String, DepthName);
        InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Parent, NULL);
        Status = NtCreateKey(&Key, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
        if (!NT_SUCCESS(Status))
        {
            MultiFailure = Status;
            break;
        }
        NtClose(Key);
        MultiCreated = Index;
    }
    ok(MultiCreated == 32, "One create made a %lu-component path\n", MultiCreated);
    ok_ntstatus(MultiFailure, STATUS_INVALID_PARAMETER);

    for (Index = Created; Index > 0; Index--)
    {
        NtDeleteKey(DepthKeys[Index - 1]);
        NtClose(DepthKeys[Index - 1]);
    }
    for (Index = MultiCreated; Index > 0; Index--)
    {
        ULONG Level;

        DepthName[0] = L'y';
        DepthName[1] = 0;
        for (Level = 1; Level < Index; Level++)
            StringCchCatW(DepthName, ARRAYSIZE(DepthName), L"\\y");
        RtlInitUnicodeString(&String, DepthName);
        InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Parent, NULL);
        if (NT_SUCCESS(NtOpenKey(&Key, DELETE, &Attributes)))
        {
            NtDeleteKey(Key);
            NtClose(Key);
        }
    }
    NtDeleteKey(Parent);
    NtClose(Parent);
}

static
NTSTATUS
OpenEventByName(
    _In_ PCWSTR Path)
{
    UNICODE_STRING String;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;
    HANDLE Event;

    RtlInitUnicodeString(&String, Path);
    InitializeObjectAttributes(&Attributes, &String, 0, NULL, NULL);
    Status = NtOpenEvent(&Event, EVENT_QUERY_STATE, &Attributes);
    if (NT_SUCCESS(Status))
        NtClose(Event);
    return Status;
}

static
VOID
TestObjectLinks(VOID)
{
    UNICODE_STRING String, TargetString;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Directory = NULL, Event = NULL, Loop = NULL;
    HANDLE Links[LINK_CHAIN_LENGTH] = { NULL };
    WCHAR Path[128], Name[160], LinkTarget[160];
    NTSTATUS Status, FirstFailure = STATUS_SUCCESS;
    ULONG Index, Deepest = 0, Session = 0;

    ProcessIdToSessionId(GetCurrentProcessId(), &Session);
    if (Session == 0)
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\BaseNamedObjects\\LinkReparseLimit_%lu", GetCurrentProcessId());
    else
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\BaseNamedObjects\\LinkReparseLimit_%lu", Session, GetCurrentProcessId());

    RtlInitUnicodeString(&String, Path);
    InitializeObjectAttributes(&Attributes, &String, 0, NULL, NULL);
    Status = NtCreateDirectoryObject(&Directory, DIRECTORY_ALL_ACCESS, &Attributes);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlInitUnicodeString(&String, L"Event");
    InitializeObjectAttributes(&Attributes, &String, 0, Directory, NULL);
    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, &Attributes, NotificationEvent, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    for (Index = 0; Index < LINK_CHAIN_LENGTH; Index++)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"S%lu", Index);
        if (Index + 1 < LINK_CHAIN_LENGTH)
            StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\S%lu", Path, Index + 1);
        else
            StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\Event", Path);
        RtlInitUnicodeString(&String, Name);
        RtlInitUnicodeString(&TargetString, LinkTarget);
        InitializeObjectAttributes(&Attributes, &String, 0, Directory, NULL);
        Status = NtCreateSymbolicLinkObject(&Links[Index], SYMBOLIC_LINK_ALL_ACCESS, &Attributes, &TargetString);
        ok(Status == STATUS_SUCCESS, "Creating object link %lu failed: 0x%08lx\n", Index, Status);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    for (Index = LINK_CHAIN_LENGTH; Index > 0; Index--)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"%s\\S%lu", Path, Index - 1);
        Status = OpenEventByName(Name);
        if (!NT_SUCCESS(Status))
        {
            FirstFailure = Status;
            break;
        }
        Deepest = LINK_CHAIN_LENGTH - Index + 1;
    }
    ok(Deepest == 63, "A lookup followed %lu chained object links\n", Deepest);
    ok_ntstatus(FirstFailure, STATUS_OBJECT_NAME_NOT_FOUND);

    StringCchPrintfW(LinkTarget, ARRAYSIZE(LinkTarget), L"%s\\Loop", Path);
    RtlInitUnicodeString(&String, L"Loop");
    RtlInitUnicodeString(&TargetString, LinkTarget);
    InitializeObjectAttributes(&Attributes, &String, 0, Directory, NULL);
    Status = NtCreateSymbolicLinkObject(&Loop, SYMBOLIC_LINK_ALL_ACCESS, &Attributes, &TargetString);
    ok(Status == STATUS_SUCCESS, "Creating the object loop failed: 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
        ok_ntstatus(OpenEventByName(LinkTarget), STATUS_OBJECT_NAME_NOT_FOUND);

Cleanup:
    if (Loop != NULL)
        NtClose(Loop);
    for (Index = 0; Index < LINK_CHAIN_LENGTH; Index++)
    {
        if (Links[Index] != NULL)
            NtClose(Links[Index]);
    }
    if (Event != NULL)
        NtClose(Event);
    NtClose(Directory);
}

START_TEST(LinkReparseLimit)
{
    TestRegistryLinks();
    TestRegistryLinkShapes();
    TestRegistryDepth();
    TestObjectLinks();
}
