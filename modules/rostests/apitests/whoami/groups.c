/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the integrity label row of whoami /groups
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

static BOOL
RunWhoamiGroups(char *Output, DWORD Size)
{
    WCHAR Command[MAX_PATH + 32];
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HANDLE Read, Write;
    DWORD Total = 0, Count;
    UINT Length;

    Length = GetSystemDirectoryW(Command, MAX_PATH);
    if (!Length || Length >= MAX_PATH)
        return FALSE;
    wcscat(Command, L"\\whoami.exe /groups");

    if (!CreatePipe(&Read, &Write, &Attributes, 0))
        return FALSE;
    SetHandleInformation(Read, HANDLE_FLAG_INHERIT, 0);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    Startup.hStdOutput = Write;
    Startup.hStdError = Write;
    if (!CreateProcessW(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Process))
    {
        CloseHandle(Read);
        CloseHandle(Write);
        return FALSE;
    }
    CloseHandle(Write);
    while (Total + 1 < Size && ReadFile(Read, Output + Total, Size - 1 - Total, &Count, NULL) && Count)
        Total += Count;
    Output[Total] = 0;
    CloseHandle(Read);
    WaitForSingleObject(Process.hProcess, 30000);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return Total != 0;
}

START_TEST(groups)
{
    static char Output[64 * 1024];
    UCHAR LabelBuffer[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    PTOKEN_MANDATORY_LABEL Label = (PTOKEN_MANDATORY_LABEL)LabelBuffer;
    PTOKEN_GROUPS Groups;
    char Name[256], Domain[256], Expected[520], *SidText, *Line, *End, *Rest;
    DWORD cchName = sizeof(Name), cchDomain = sizeof(Domain), Length, Index;
    SID_NAME_USE Use;
    HANDLE Token;
    BOOL Found = FALSE;
    ULONG Unnamed = 0;

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token), "OpenProcessToken failed: %lu\n", GetLastError());
    ok(GetTokenInformation(Token, TokenIntegrityLevel, Label, sizeof(LabelBuffer), &Length),
       "TokenIntegrityLevel failed: %lu\n", GetLastError());
    ok_long(Label->Label.Attributes, SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED);

    GetTokenInformation(Token, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    ok(Groups && GetTokenInformation(Token, TokenGroups, Groups, Length, &Length), "TokenGroups failed: %lu\n", GetLastError());
    for (Index = 0; Groups && Index < Groups->GroupCount; Index++)
    {
        if (EqualSid(Groups->Groups[Index].Sid, Label->Label.Sid))
        {
            Found = TRUE;
            ok_long(Groups->Groups[Index].Attributes, SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED);
        }
    }
    ok(Found, "the integrity label is not in TokenGroups\n");
    HeapFree(GetProcessHeap(), 0, Groups);
    CloseHandle(Token);

    ok(LookupAccountSidA(NULL, Label->Label.Sid, Name, &cchName, Domain, &cchDomain, &Use),
       "LookupAccountSidA failed: %lu\n", GetLastError());
    ok_int(Use, SidTypeLabel);
    sprintf(Expected, "%s\\%s ", Domain, Name);

    ok(ConvertSidToStringSidA(Label->Label.Sid, &SidText), "ConvertSidToStringSidA failed: %lu\n", GetLastError());
    ok(RunWhoamiGroups(Output, sizeof(Output)), "whoami /groups failed: %lu\n", GetLastError());

    Found = FALSE;
    for (Line = Output; *Line; Line = End)
    {
        End = strchr(Line, '\n');
        End = End ? End + 1 : Line + strlen(Line);
        if (Line[0] == '\\')
            Unnamed++;
        Rest = strstr(Line, SidText);
        if (!Rest || Rest > End)
            continue;
        Rest += strlen(SidText);
        if (*Rest != ' ' && *Rest != '\r' && *Rest != '\n')
            continue;
        Found = TRUE;
        ok(!strncmp(Line, Expected, strlen(Expected)), "label row is %.*s\n", (int)(End - Line), Line);
        ok(strstr(Line, " Label ") && strstr(Line, " Label ") < Rest, "label row has no Label type: %.*s\n", (int)(End - Line), Line);
        while (Rest < End && (*Rest == ' ' || *Rest == '\r' || *Rest == '\n'))
            Rest++;
        ok(Rest == End, "label row has attributes: %.*s\n", (int)(End - Line), Line);
    }
    ok(Found, "no row for %s in:\n%s\n", SidText, Output);
    ok(!Unnamed, "%lu rows without a name in:\n%s\n", Unnamed, Output);
    LocalFree(SidText);
}
