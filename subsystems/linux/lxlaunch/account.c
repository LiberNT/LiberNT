/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux accounts and login sessions of a Linux root
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

#include "../include/lxlaunch.h"

#define LX_ROOT_FILE_MAX        (1024 * 1024)
#define LX_COMPOSITOR_PATH      L"\\usr\\local\\lib\\libernt\\lxwayland\\lxwayland"
#define LX_WAYLAND_DISPLAY      "/run/lxwayland/wayland-0"

static
PCHAR
LxReadRootFile(
    _In_ PCWSTR Root,
    _In_ PCWSTR Relative)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    PCHAR Data;
    DWORD Size, Read;

    if (!GetFullPathNameW(Root, MAX_PATH, Path, NULL) || (wcslen(Path) + wcslen(Relative) >= MAX_PATH))
        return NULL;
    wcscat(Path, Relative);
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return NULL;
    Size = GetFileSize(File, NULL);
    Data = ((Size != INVALID_FILE_SIZE) && (Size < LX_ROOT_FILE_MAX)) ? malloc(Size + 1) : NULL;
    if ((Data != NULL) && !ReadFile(File, Data, Size, &Read, NULL))
    {
        free(Data);
        Data = NULL;
    }
    CloseHandle(File);
    if (Data != NULL)
        Data[Read] = '\0';
    return Data;
}

static
PCHAR
LxNextLine(
    _Inout_ PCHAR *Cursor)
{
    PCHAR Line = *Cursor, End;

    if ((Line == NULL) || (*Line == '\0'))
        return NULL;
    End = strchr(Line, '\n');
    if (End != NULL)
    {
        *End = '\0';
        *Cursor = End + 1;
    }
    else
    {
        *Cursor = Line + strlen(Line);
    }
    Line[strcspn(Line, "\r")] = '\0';
    return Line;
}

static
ULONG
LxSplitFields(
    _Inout_ PCHAR Line,
    _Out_writes_(Count) PCHAR *Fields,
    _In_ ULONG Count)
{
    ULONG Found = 0;

    while (Found < Count)
    {
        Fields[Found++] = Line;
        Line = strchr(Line, ':');
        if (Line == NULL)
            break;
        *Line++ = '\0';
    }
    return Found;
}

static
BOOL
LxParseId(
    _In_ PCSTR Text,
    _Out_ PULONG Id)
{
    PCHAR End;
    unsigned long Value;

    if ((*Text < '0') || (*Text > '9'))
        return FALSE;
    Value = strtoul(Text, &End, 10);
    if ((*End != '\0') || (Value >= 0xFFFFFFFFUL))
        return FALSE;
    *Id = (ULONG)Value;
    return TRUE;
}

BOOL
LxRootHasFile(
    _In_ PCWSTR Root,
    _In_ PCWSTR Relative)
{
    WCHAR Path[MAX_PATH];
    DWORD Attributes;

    if (!GetFullPathNameW(Root, MAX_PATH, Path, NULL) || (wcslen(Path) + wcslen(Relative) >= MAX_PATH))
        return FALSE;
    wcscat(Path, Relative);
    Attributes = GetFileAttributesW(Path);
    return (Attributes != INVALID_FILE_ATTRIBUTES) && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL
LxRootDefaultUser(
    _In_ PCWSTR Root,
    _Out_writes_z_(Size) PCHAR Name,
    _In_ SIZE_T Size)
{
    PCHAR Data = LxReadRootFile(Root, L"\\etc\\wsl.conf"), Cursor = Data, Line;
    BOOL InUser = FALSE, Found = FALSE;

    Name[0] = '\0';
    while ((Line = LxNextLine(&Cursor)) != NULL)
    {
        PCHAR Value;

        Line += strspn(Line, " \t");
        if (Line[0] == '[')
        {
            InUser = (strncmp(Line, "[user]", 6) == 0);
            continue;
        }
        if (!InUser || (strncmp(Line, "default", 7) != 0))
            continue;
        Value = Line + 7 + strspn(Line + 7, " \t");
        if (*Value != '=')
            continue;
        Value++;
        Value += strspn(Value, " \t");
        Value[strcspn(Value, " \t#")] = '\0';
        if ((Value[0] != '\0') && (strlen(Value) < Size) &&
            (Value[strspn(Value, "abcdefghijklmnopqrstuvwxyz0123456789_-")] == '\0'))
        {
            strcpy(Name, Value);
            Found = TRUE;
        }
        break;
    }
    free(Data);
    return Found;
}

static
BOOL
LxNameInList(
    _In_ PCSTR List,
    _In_ PCSTR Name)
{
    SIZE_T Length = strlen(Name);

    while (*List != '\0')
    {
        SIZE_T Item = strcspn(List, ",");

        if ((Item == Length) && (strncmp(List, Name, Length) == 0))
            return TRUE;
        List += Item;
        if (*List == ',')
            List++;
    }
    return FALSE;
}

static
VOID
LxAddGroup(
    _Inout_ PLX_ACCOUNT Account,
    _In_ ULONG Gid)
{
    ULONG Index;

    for (Index = 0; Index < Account->GroupCount; Index++)
    {
        if (Account->Groups[Index] == Gid)
            return;
    }
    if (Account->GroupCount < ARRAYSIZE(Account->Groups))
        Account->Groups[Account->GroupCount++] = Gid;
}

BOOL
LxRootGroupId(
    _In_ PCWSTR Root,
    _In_ PCSTR Name,
    _Out_ PULONG Gid)
{
    PCHAR Data = LxReadRootFile(Root, L"\\etc\\group"), Cursor = Data, Line;
    BOOL Found = FALSE;

    while (!Found && ((Line = LxNextLine(&Cursor)) != NULL))
    {
        PCHAR Fields[4];

        if ((LxSplitFields(Line, Fields, 4) >= 3) && (strcmp(Fields[0], Name) == 0))
            Found = LxParseId(Fields[2], Gid);
    }
    free(Data);
    return Found;
}

BOOL
LxRootAccount(
    _In_ PCWSTR Root,
    _In_opt_ PCSTR Name,
    _In_ ULONG Uid,
    _Out_ PLX_ACCOUNT Account)
{
    PCHAR Data = LxReadRootFile(Root, L"\\etc\\passwd"), Cursor = Data, Line;
    BOOL Found = FALSE;

    ZeroMemory(Account, sizeof(*Account));
    while (!Found && ((Line = LxNextLine(&Cursor)) != NULL))
    {
        PCHAR Fields[7];
        ULONG EntryUid, EntryGid;

        if ((LxSplitFields(Line, Fields, 7) != 7) || !LxParseId(Fields[2], &EntryUid) ||
            !LxParseId(Fields[3], &EntryGid))
        {
            continue;
        }
        if ((Name != NULL) ? (strcmp(Fields[0], Name) != 0) : (EntryUid != Uid))
            continue;
        if ((strlen(Fields[0]) >= sizeof(Account->Name)) || (strlen(Fields[5]) >= sizeof(Account->Home)) ||
            (strlen(Fields[6]) >= sizeof(Account->Shell)))
        {
            break;
        }
        strcpy(Account->Name, Fields[0]);
        strcpy(Account->Home, (Fields[5][0] == '/') ? Fields[5] : "/");
        strcpy(Account->Shell, (Fields[6][0] == '/') ? Fields[6] : "/bin/sh");
        Account->Uid = EntryUid;
        Account->Gid = EntryGid;
        Found = TRUE;
    }
    free(Data);
    if (!Found)
        return FALSE;

    LxAddGroup(Account, Account->Gid);
    Data = LxReadRootFile(Root, L"\\etc\\group");
    Cursor = Data;
    while ((Line = LxNextLine(&Cursor)) != NULL)
    {
        PCHAR Fields[4];
        ULONG Gid;

        if ((LxSplitFields(Line, Fields, 4) == 4) && LxParseId(Fields[2], &Gid) &&
            LxNameInList(Fields[3], Account->Name))
        {
            LxAddGroup(Account, Gid);
        }
    }
    free(Data);
    return TRUE;
}

static
BOOL
LxRootSetting(
    _In_ PCWSTR Root,
    _In_ PCWSTR Relative,
    _In_ PCSTR Key,
    _In_ CHAR Separator,
    _Out_writes_z_(Size) PCHAR Value,
    _In_ SIZE_T Size)
{
    PCHAR Data = LxReadRootFile(Root, Relative), Cursor = Data, Line;
    SIZE_T KeyLength = strlen(Key);
    BOOL Found = FALSE;

    Value[0] = '\0';
    while (!Found && ((Line = LxNextLine(&Cursor)) != NULL))
    {
        PCHAR Text;

        Line += strspn(Line, " \t");
        if ((strncmp(Line, Key, KeyLength) != 0) ||
            ((Line[KeyLength] != Separator) && !((Separator == ' ') && (Line[KeyLength] == '\t'))))
        {
            continue;
        }
        Text = Line + KeyLength + 1;
        Text += strspn(Text, " \t");
        if (*Text == '"')
        {
            Text++;
            Text[strcspn(Text, "\"")] = '\0';
        }
        else
        {
            Text[strcspn(Text, " \t#")] = '\0';
        }
        if ((Text[0] != '\0') && (strlen(Text) < Size))
        {
            strcpy(Value, Text);
            Found = TRUE;
        }
    }
    free(Data);
    return Found;
}

static
BOOL
LxEnvPair(
    _Inout_ PLX_STRINGS Env,
    _In_ PCSTR Name,
    _In_ PCSTR Value)
{
    SIZE_T Length = strlen(Name) + strlen(Value) + 2;
    PCHAR Assignment = malloc(Length);
    BOOL Result;

    if (Assignment == NULL)
        return FALSE;
    _snprintf(Assignment, Length, "%s=%s", Name, Value);
    Result = LxEnvSet(Env, Assignment);
    free(Assignment);
    return Result;
}

static
BOOL
LxApplyEnvironmentFile(
    _Inout_ PLX_LAUNCH Launch,
    _In_ PCWSTR Root)
{
    PCHAR Data = LxReadRootFile(Root, L"\\etc\\environment"), Cursor = Data, Line;
    BOOL Success = TRUE;

    while (Success && ((Line = LxNextLine(&Cursor)) != NULL))
    {
        PCHAR Equals, Value;
        SIZE_T Length;

        Line += strspn(Line, " \t");
        Equals = strchr(Line, '=');
        if ((Line[0] == '#') || (Equals == NULL) || (Equals == Line) ||
            (Line[strspn(Line, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")] != '='))
        {
            continue;
        }
        Value = Equals + 1;
        Length = strlen(Value);
        if ((Length >= 2) && ((Value[0] == '"') || (Value[0] == '\'')) && (Value[Length - 1] == Value[0]))
        {
            Value[Length - 1] = '\0';
            memmove(Value, Value + 1, Length - 1);
        }
        Success = LxEnvSet(&Launch->Env, Line);
    }
    free(Data);
    return Success;
}

BOOL
LxSetSession(
    _Inout_ PLX_LAUNCH Launch,
    _In_ PCWSTR Root,
    _In_ PLX_ACCOUNT Account)
{
    CHAR Path[1024], Lang[128], Runtime[64];

    Launch->User = TRUE;
    Launch->Uid = Account->Uid;
    Launch->Gid = Account->Gid;
    Launch->GroupCount = Account->GroupCount;
    memcpy(Launch->Groups, Account->Groups, Account->GroupCount * sizeof(ULONG));
    Launch->RuntimeDir = TRUE;
    Launch->Cwd = Account->Home;

    if (!LxRootSetting(Root, L"\\etc\\login.defs", (Account->Uid == 0) ? "ENV_SUPATH" : "ENV_PATH", ' ',
                       Path, sizeof(Path)) ||
        (strncmp(Path, "PATH=", 5) != 0))
    {
        strcpy(Path, (Account->Uid == 0) ? "PATH=/sbin:/bin:/usr/sbin:/usr/bin" : "PATH=/bin:/usr/bin");
    }
    if (!LxRootSetting(Root, L"\\etc\\default\\locale", "LANG", '=', Lang, sizeof(Lang)))
        strcpy(Lang, "C.UTF-8");
    _snprintf(Runtime, sizeof(Runtime), "/run/user/%lu", Account->Uid);

    if (!LxEnvSet(&Launch->Env, Path) ||
        !LxEnvPair(&Launch->Env, "HOME", Account->Home) ||
        !LxEnvPair(&Launch->Env, "USER", Account->Name) ||
        !LxEnvPair(&Launch->Env, "LOGNAME", Account->Name) ||
        !LxEnvPair(&Launch->Env, "SHELL", Account->Shell) ||
        !LxEnvPair(&Launch->Env, "LANG", Lang) ||
        !LxEnvPair(&Launch->Env, "XDG_RUNTIME_DIR", Runtime))
    {
        return FALSE;
    }
    if (!LxApplyEnvironmentFile(Launch, Root))
        return FALSE;
    if (LxRootHasFile(Root, LX_COMPOSITOR_PATH) &&
        (!LxEnvPair(&Launch->Env, "WAYLAND_DISPLAY", LX_WAYLAND_DISPLAY) ||
         !LxEnvPair(&Launch->Env, "XDG_SESSION_TYPE", "wayland")))
    {
        return FALSE;
    }
    return TRUE;
}
