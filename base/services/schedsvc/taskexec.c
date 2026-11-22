/*
 * PROJECT:     LiberNT Task Scheduler service
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Start the action of a registered task on demand
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#define COBJMACROS

#include <windows.h>
#include <ole2.h>
#include <xmllite.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <rpc.h>
#include <sddl.h>
#include "taskschd.h"
#include "taskexec.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(schedsvc);

#define TASKEXEC_MAX_DEPTH 8
#define TASKEXEC_MAX_ARGS 33
#define TASKEXEC_MAX_LOGON_TRIGGERS 4

typedef struct _TASKEXEC_LOGON_TRIGGER
{
    BOOL Enabled;
    WCHAR *UserId;
    DWORD DelayMs;
} TASKEXEC_LOGON_TRIGGER;

typedef struct _TASKEXEC_DEFINITION
{
    BOOL Enabled;
    BOOL AllowStartOnDemand;
    BOOL SystemPrincipal;
    BOOL HaveExec;
    WCHAR *Command;
    WCHAR *Arguments;
    WCHAR *WorkingDirectory;
    WCHAR *PrincipalUserId;
    UINT LogonTriggerCount;
    TASKEXEC_LOGON_TRIGGER LogonTriggers[TASKEXEC_MAX_LOGON_TRIGGERS];
} TASKEXEC_DEFINITION;

static BOOL TaskExecIsPath(const WCHAR **Stack, UINT Depth, const WCHAR *Parent, const WCHAR *Leaf)
{
    return Depth >= 2 && !wcscmp(Stack[Depth - 2], Parent) && !wcscmp(Stack[Depth - 1], Leaf);
}

static BOOL TaskExecIsSystemAccount(const WCHAR *Value)
{
    return !_wcsicmp(Value, L"S-1-5-18") || !_wcsicmp(Value, L"SYSTEM") || !_wcsicmp(Value, L"NT AUTHORITY\\SYSTEM");
}

static WCHAR *TaskExecCopy(const WCHAR *Value)
{
    SIZE_T Size = (wcslen(Value) + 1) * sizeof(WCHAR);
    WCHAR *Copy = HeapAlloc(GetProcessHeap(), 0, Size);

    if (Copy != NULL)
        memcpy(Copy, Value, Size);
    return Copy;
}

static DWORD TaskExecParseDuration(const WCHAR *Value)
{
    ULONGLONG Total = 0, Number = 0;
    BOOL Time = FALSE;

    if (*Value++ != 'P')
        return 0;
    for (; *Value; Value++)
    {
        if (iswdigit(*Value))
            Number = Number * 10 + (*Value - '0');
        else if (*Value == 'T')
            Time = TRUE;
        else
        {
            if (*Value == 'D' && !Time)
                Total += Number * 86400000;
            else if (*Value == 'H' && Time)
                Total += Number * 3600000;
            else if (*Value == 'M' && Time)
                Total += Number * 60000;
            else if (*Value == 'S' && Time)
                Total += Number * 1000;
            Number = 0;
        }
    }
    return Total > MAXDWORD - 1 ? MAXDWORD - 1 : (DWORD)Total;
}

static HRESULT TaskExecStoreText(TASKEXEC_DEFINITION *Definition, const WCHAR **Stack, UINT Depth, const WCHAR *Value)
{
    WCHAR **Target = NULL;
    TASKEXEC_LOGON_TRIGGER *Trigger = Definition->LogonTriggerCount != 0 ?
        &Definition->LogonTriggers[Definition->LogonTriggerCount - 1] : NULL;

    if (Trigger != NULL && TaskExecIsPath(Stack, Depth, L"LogonTrigger", L"Enabled"))
        Trigger->Enabled = !wcscmp(Value, L"true");
    else if (Trigger != NULL && TaskExecIsPath(Stack, Depth, L"LogonTrigger", L"Delay"))
        Trigger->DelayMs = TaskExecParseDuration(Value);
    else if (Trigger != NULL && TaskExecIsPath(Stack, Depth, L"LogonTrigger", L"UserId"))
        Target = &Trigger->UserId;
    else if (TaskExecIsPath(Stack, Depth, L"Settings", L"Enabled"))
        Definition->Enabled = !wcscmp(Value, L"true");
    else if (TaskExecIsPath(Stack, Depth, L"Settings", L"AllowStartOnDemand"))
        Definition->AllowStartOnDemand = !wcscmp(Value, L"true");
    else if (TaskExecIsPath(Stack, Depth, L"Principal", L"UserId"))
    {
        Definition->SystemPrincipal = TaskExecIsSystemAccount(Value);
        Target = &Definition->PrincipalUserId;
    }
    else if (!Definition->HaveExec && TaskExecIsPath(Stack, Depth, L"Exec", L"Command"))
        Target = &Definition->Command;
    else if (!Definition->HaveExec && TaskExecIsPath(Stack, Depth, L"Exec", L"Arguments"))
        Target = &Definition->Arguments;
    else if (!Definition->HaveExec && TaskExecIsPath(Stack, Depth, L"Exec", L"WorkingDirectory"))
        Target = &Definition->WorkingDirectory;

    if (Target == NULL)
        return S_OK;
    HeapFree(GetProcessHeap(), 0, *Target);
    *Target = TaskExecCopy(Value);
    return *Target != NULL ? S_OK : E_OUTOFMEMORY;
}

static HRESULT TaskExecWalk(IXmlReader *Reader, TASKEXEC_DEFINITION *Definition)
{
    WCHAR *Stack[TASKEXEC_MAX_DEPTH] = { 0 };
    const WCHAR *Name, *Value;
    XmlNodeType Type;
    UINT Depth = 0, Index;
    HRESULT hr = S_OK;

    while (SUCCEEDED(hr) && IXmlReader_Read(Reader, &Type) == S_OK)
    {
        if (Type == XmlNodeType_Element)
        {
            BOOL Empty = IXmlReader_IsEmptyElement(Reader);

            hr = IXmlReader_GetLocalName(Reader, &Name, NULL);
            if (SUCCEEDED(hr) && Depth != 0 && !wcscmp(Stack[Depth - 1], L"Triggers") && !wcscmp(Name, L"LogonTrigger"))
            {
                /* TODO: more than TASKEXEC_MAX_LOGON_TRIGGERS logon triggers in one task */
                if (Definition->LogonTriggerCount < TASKEXEC_MAX_LOGON_TRIGGERS)
                    Definition->LogonTriggers[Definition->LogonTriggerCount++].Enabled = TRUE;
            }
            if (FAILED(hr) || Empty)
                continue;
            if (Depth == TASKEXEC_MAX_DEPTH)
            {
                hr = SCHED_E_MALFORMEDXML;
                continue;
            }
            Stack[Depth] = TaskExecCopy(Name);
            if (Stack[Depth] == NULL)
                hr = E_OUTOFMEMORY;
            else
                Depth++;
        }
        else if (Type == XmlNodeType_EndElement && Depth != 0)
        {
            if (!wcscmp(Stack[Depth - 1], L"Exec") && Definition->Command != NULL)
                Definition->HaveExec = TRUE;
            HeapFree(GetProcessHeap(), 0, Stack[--Depth]);
            Stack[Depth] = NULL;
        }
        else if (Type == XmlNodeType_Text && Depth != 0)
        {
            hr = IXmlReader_GetValue(Reader, &Value, NULL);
            if (SUCCEEDED(hr))
                hr = TaskExecStoreText(Definition, (const WCHAR **)Stack, Depth, Value);
        }
    }
    for (Index = 0; Index < Depth; Index++)
        HeapFree(GetProcessHeap(), 0, Stack[Index]);
    return hr;
}

static HRESULT TaskExecParse(const WCHAR *Xml, TASKEXEC_DEFINITION *Definition)
{
    SIZE_T Bytes = wcslen(Xml) * sizeof(WCHAR);
    IXmlReader *Reader = NULL;
    IStream *Stream = NULL;
    HGLOBAL Memory;
    HRESULT hr;

    Definition->Enabled = TRUE;
    Definition->AllowStartOnDemand = TRUE;
    Memory = GlobalAlloc(GMEM_MOVEABLE, Bytes ? Bytes : 1);
    if (Memory == NULL)
        return E_OUTOFMEMORY;
    memcpy(GlobalLock(Memory), Xml, Bytes);
    GlobalUnlock(Memory);
    hr = CreateStreamOnHGlobal(Memory, TRUE, &Stream);
    if (FAILED(hr))
    {
        GlobalFree(Memory);
        return hr;
    }
    hr = CreateXmlReader(&IID_IXmlReader, (void **)&Reader, NULL);
    if (SUCCEEDED(hr))
        hr = IXmlReader_SetInput(Reader, (IUnknown *)Stream);
    if (SUCCEEDED(hr))
        hr = TaskExecWalk(Reader, Definition);
    if (Reader != NULL)
        IXmlReader_Release(Reader);
    IStream_Release(Stream);
    return FAILED(hr) ? SCHED_E_MALFORMEDXML : S_OK;
}

static void TaskExecFree(TASKEXEC_DEFINITION *Definition)
{
    UINT Index;

    for (Index = 0; Index < Definition->LogonTriggerCount; Index++)
        HeapFree(GetProcessHeap(), 0, Definition->LogonTriggers[Index].UserId);
    HeapFree(GetProcessHeap(), 0, Definition->PrincipalUserId);
    HeapFree(GetProcessHeap(), 0, Definition->Command);
    HeapFree(GetProcessHeap(), 0, Definition->Arguments);
    HeapFree(GetProcessHeap(), 0, Definition->WorkingDirectory);
}

static WCHAR *TaskExecExpand(const WCHAR *Value)
{
    DWORD Needed = ExpandEnvironmentStringsW(Value, NULL, 0);
    WCHAR *Expanded;

    if (Needed == 0)
        return NULL;
    Expanded = HeapAlloc(GetProcessHeap(), 0, Needed * sizeof(WCHAR));
    if (Expanded != NULL && ExpandEnvironmentStringsW(Value, Expanded, Needed) == 0)
    {
        HeapFree(GetProcessHeap(), 0, Expanded);
        Expanded = NULL;
    }
    return Expanded;
}

static SIZE_T TaskExecArgumentsLength(const WCHAR *Arguments, DWORD ArgCount, const WCHAR **Args)
{
    SIZE_T Length = 0;
    const WCHAR *Scan = Arguments;
    DWORD Index;

    while (*Scan)
    {
        if (Scan[0] == '$' && Scan[1] == '(' && Scan[2] == 'A' && Scan[3] == 'r' && Scan[4] == 'g' &&
            iswdigit(Scan[5]))
        {
            const WCHAR *End = Scan + 5;

            Index = 0;
            while (iswdigit(*End))
                Index = Index * 10 + (*End++ - '0');
            if (*End == ')')
            {
                if (Index < ArgCount && Index < TASKEXEC_MAX_ARGS && Args[Index] != NULL)
                    Length += wcslen(Args[Index]);
                Scan = End + 1;
                continue;
            }
        }
        Length++;
        Scan++;
    }
    return Length;
}

static void TaskExecSubstitute(WCHAR *Out, const WCHAR *Arguments, DWORD ArgCount, const WCHAR **Args)
{
    const WCHAR *Scan = Arguments;
    DWORD Index;

    while (*Scan)
    {
        if (Scan[0] == '$' && Scan[1] == '(' && Scan[2] == 'A' && Scan[3] == 'r' && Scan[4] == 'g' &&
            iswdigit(Scan[5]))
        {
            const WCHAR *End = Scan + 5;

            Index = 0;
            while (iswdigit(*End))
                Index = Index * 10 + (*End++ - '0');
            if (*End == ')')
            {
                if (Index < ArgCount && Index < TASKEXEC_MAX_ARGS && Args[Index] != NULL)
                {
                    wcscpy(Out, Args[Index]);
                    Out += wcslen(Args[Index]);
                }
                Scan = End + 1;
                continue;
            }
        }
        *Out++ = *Scan++;
    }
    *Out = 0;
}

static WCHAR *TaskExecCommandLine(const TASKEXEC_DEFINITION *Definition, DWORD ArgCount, const WCHAR **Args)
{
    WCHAR *Command = TaskExecExpand(Definition->Command);
    WCHAR *Arguments = Definition->Arguments != NULL ? TaskExecExpand(Definition->Arguments) : NULL;
    BOOL Quote;
    SIZE_T Length;
    WCHAR *Line = NULL, *Out;

    if (Command == NULL || (Definition->Arguments != NULL && Arguments == NULL))
    {
        HeapFree(GetProcessHeap(), 0, Command);
        HeapFree(GetProcessHeap(), 0, Arguments);
        return NULL;
    }
    Quote = Command[0] != '"' && wcschr(Command, ' ') != NULL;
    Length = wcslen(Command) + (Quote ? 2 : 0) + 1;
    if (Arguments != NULL)
        Length += 1 + TaskExecArgumentsLength(Arguments, ArgCount, Args);
    Line = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (Line != NULL)
    {
        Out = Line;
        if (Quote)
            *Out++ = '"';
        wcscpy(Out, Command);
        Out += wcslen(Command);
        if (Quote)
            *Out++ = '"';
        *Out = 0;
        if (Arguments != NULL)
        {
            *Out++ = ' ';
            TaskExecSubstitute(Out, Arguments, ArgCount, Args);
        }
    }
    HeapFree(GetProcessHeap(), 0, Command);
    HeapFree(GetProcessHeap(), 0, Arguments);
    return Line;
}

static WCHAR *TaskExecDirectory(const WCHAR *Directory)
{
    WCHAR *Expanded, *Start;
    SIZE_T Length;

    if (Directory == NULL)
        return NULL;
    Expanded = TaskExecExpand(Directory);
    if (Expanded == NULL)
        return NULL;
    Start = Expanded;
    Length = wcslen(Start);
    if (Length >= 2 && Start[0] == '"' && Start[Length - 1] == '"')
    {
        Start[Length - 1] = 0;
        memmove(Expanded, Start + 1, (Length - 1) * sizeof(WCHAR));
    }
    return Expanded;
}

static HRESULT TaskExecStart(BOOL SystemPrincipal, WCHAR *CommandLine, const WCHAR *Directory, DWORD Flags,
                             DWORD SessionId)
{
    STARTUPINFOW Startup;
    PROCESS_INFORMATION Process;
    HANDLE Token = NULL;
    void *Environment = NULL;
    BOOL Started;
    DWORD Error;

    memset(&Startup, 0, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    if (SystemPrincipal)
    {
        Started = CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, Directory, &Startup, &Process);
        if (!Started)
            return HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
        return S_OK;
    }

    /* FIXME: RunLevel is not applied; the action gets the session user's own token */
    if (!(Flags & TASK_RUN_USE_SESSION_ID))
        SessionId = WTSGetActiveConsoleSessionId();
    if (SessionId == 0xFFFFFFFF || !WTSQueryUserToken(SessionId, &Token))
        return HRESULT_FROM_WIN32(SessionId == 0xFFFFFFFF ? ERROR_NO_TOKEN : GetLastError());
    if (!CreateEnvironmentBlock(&Environment, Token, FALSE))
        Environment = NULL;
    Startup.lpDesktop = (WCHAR *)L"winsta0\\default";
    Started = CreateProcessAsUserW(Token, NULL, CommandLine, NULL, NULL, FALSE,
                                   Environment != NULL ? CREATE_UNICODE_ENVIRONMENT : 0, Environment, Directory,
                                   &Startup, &Process);
    Error = GetLastError();
    if (Environment != NULL)
        DestroyEnvironmentBlock(Environment);
    CloseHandle(Token);
    if (!Started)
        return HRESULT_FROM_WIN32(Error);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return S_OK;
}

HRESULT TaskExecRun(const WCHAR *Xml, DWORD ArgCount, const WCHAR **Args, DWORD Flags, DWORD SessionId, GUID *Guid)
{
    TASKEXEC_DEFINITION Definition;
    WCHAR *CommandLine, *Directory;
    HRESULT hr;

    memset(&Definition, 0, sizeof(Definition));
    hr = TaskExecParse(Xml, &Definition);
    if (FAILED(hr))
    {
        TaskExecFree(&Definition);
        return hr;
    }
    if (!Definition.Enabled)
        hr = SCHED_E_TASK_DISABLED;
    else if (!Definition.AllowStartOnDemand)
        hr = SCHED_E_START_ON_DEMAND;
    else if (Definition.Command == NULL)
        /* TODO: COM handler, e-mail and message actions */
        hr = E_NOTIMPL;
    if (FAILED(hr))
    {
        TaskExecFree(&Definition);
        return hr;
    }

    CommandLine = TaskExecCommandLine(&Definition, ArgCount, Args);
    Directory = TaskExecDirectory(Definition.WorkingDirectory);
    if (CommandLine == NULL || (Definition.WorkingDirectory != NULL && Directory == NULL))
        hr = E_OUTOFMEMORY;
    else
        hr = TaskExecStart(Definition.SystemPrincipal, CommandLine, Directory, Flags, SessionId);
    TRACE("%s -> %#lx\n", debugstr_w(CommandLine), hr);
    if (SUCCEEDED(hr) && Guid != NULL && UuidCreate(Guid) != RPC_S_OK)
        memset(Guid, 0, sizeof(*Guid));
    HeapFree(GetProcessHeap(), 0, CommandLine);
    HeapFree(GetProcessHeap(), 0, Directory);
    TaskExecFree(&Definition);
    return hr;
}

typedef struct _TASKEXEC_DELAYED_START
{
    BOOL SystemPrincipal;
    DWORD SessionId;
    DWORD DelayMs;
    WCHAR *CommandLine;
    WCHAR *Directory;
} TASKEXEC_DELAYED_START;

static DWORD WINAPI TaskExecDelayedStart(void *Context)
{
    TASKEXEC_DELAYED_START *Start = Context;
    HRESULT hr;

    Sleep(Start->DelayMs);
    hr = TaskExecStart(Start->SystemPrincipal, Start->CommandLine, Start->Directory, TASK_RUN_USE_SESSION_ID,
                       Start->SessionId);
    TRACE("%s -> %#lx\n", debugstr_w(Start->CommandLine), hr);
    HeapFree(GetProcessHeap(), 0, Start->CommandLine);
    HeapFree(GetProcessHeap(), 0, Start->Directory);
    HeapFree(GetProcessHeap(), 0, Start);
    return 0;
}

static BOOL TaskExecUserMatches(const WCHAR *UserId, DWORD SessionId)
{
    WCHAR *User = NULL, *Domain = NULL, *SidText = NULL, Qualified[512];
    BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD Bytes;
    HANDLE Token;
    BOOL Match = FALSE;

    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, SessionId, WTSUserName, &User, &Bytes) &&
        WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, SessionId, WTSDomainName, &Domain, &Bytes) &&
        User[0] != 0)
    {
        _snwprintf(Qualified, ARRAYSIZE(Qualified), L"%s\\%s", Domain, User);
        Qualified[ARRAYSIZE(Qualified) - 1] = 0;
        Match = !_wcsicmp(UserId, User) || !_wcsicmp(UserId, Qualified);
    }
    if (User != NULL)
        WTSFreeMemory(User);
    if (Domain != NULL)
        WTSFreeMemory(Domain);
    if (Match || _wcsnicmp(UserId, L"S-1-", 4) || !WTSQueryUserToken(SessionId, &Token))
        return Match;
    if (GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Bytes) &&
        ConvertSidToStringSidW(((TOKEN_USER *)Buffer)->User.Sid, &SidText))
    {
        Match = !_wcsicmp(UserId, SidText);
        LocalFree(SidText);
    }
    CloseHandle(Token);
    return Match;
}

HRESULT TaskExecLogon(const WCHAR *Xml, DWORD SessionId)
{
    TASKEXEC_DEFINITION Definition;
    TASKEXEC_DELAYED_START *Start;
    const TASKEXEC_LOGON_TRIGGER *Trigger = NULL;
    HANDLE Thread;
    UINT Index;
    HRESULT hr;

    memset(&Definition, 0, sizeof(Definition));
    hr = TaskExecParse(Xml, &Definition);
    for (Index = 0; SUCCEEDED(hr) && Trigger == NULL && Index < Definition.LogonTriggerCount; Index++)
    {
        const TASKEXEC_LOGON_TRIGGER *Candidate = &Definition.LogonTriggers[Index];

        if (Candidate->Enabled && (Candidate->UserId == NULL || TaskExecUserMatches(Candidate->UserId, SessionId)))
            Trigger = Candidate;
    }
    /* FIXME: a task whose principal is another user runs only when that user logs on; stored credentials are not used */
    if (FAILED(hr) || Trigger == NULL || !Definition.Enabled || Definition.Command == NULL ||
        (Definition.PrincipalUserId != NULL && !Definition.SystemPrincipal &&
         !TaskExecUserMatches(Definition.PrincipalUserId, SessionId)))
    {
        TaskExecFree(&Definition);
        return FAILED(hr) ? hr : S_FALSE;
    }

    Start = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Start));
    if (Start != NULL)
    {
        Start->SystemPrincipal = Definition.SystemPrincipal;
        Start->SessionId = SessionId;
        Start->DelayMs = Trigger->DelayMs;
        Start->CommandLine = TaskExecCommandLine(&Definition, 0, NULL);
        Start->Directory = TaskExecDirectory(Definition.WorkingDirectory);
    }
    if (Start == NULL || Start->CommandLine == NULL || (Definition.WorkingDirectory != NULL && Start->Directory == NULL))
        hr = E_OUTOFMEMORY;
    TaskExecFree(&Definition);
    if (FAILED(hr))
    {
        if (Start != NULL)
        {
            HeapFree(GetProcessHeap(), 0, Start->CommandLine);
            HeapFree(GetProcessHeap(), 0, Start->Directory);
            HeapFree(GetProcessHeap(), 0, Start);
        }
        return hr;
    }
    Thread = CreateThread(NULL, 0, TaskExecDelayedStart, Start, 0, NULL);
    if (Thread == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        HeapFree(GetProcessHeap(), 0, Start->CommandLine);
        HeapFree(GetProcessHeap(), 0, Start->Directory);
        HeapFree(GetProcessHeap(), 0, Start);
        return hr;
    }
    CloseHandle(Thread);
    return S_OK;
}
