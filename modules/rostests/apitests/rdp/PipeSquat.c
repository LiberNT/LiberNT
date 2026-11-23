/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the Remote Desktop server talks only to the pipes its own servers made
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <lm.h>
#include <stdio.h>
#include "rdpclient.h"

#define AGENT_PIPE_FORMAT   L"\\\\.\\pipe\\LiberNT-RDPServer-%lu"
#define CONSOLE_PIPE_FORMAT L"\\\\.\\pipe\\LiberNT-RemoteConsole-%lu"
#define LAST_SESSION 32

typedef struct _SQUAT
{
    HANDLE Pipe[LAST_SESSION];
    HANDLE Event[LAST_SESSION];
    OVERLAPPED Overlapped[LAST_SESSION];
    DWORD Session[LAST_SESSION];
    DWORD Count;
} SQUAT, *PSQUAT;

static VOID
SquatPipes(PSQUAT Squat, PCWSTR Format)
{
    WCHAR Name[64];
    DWORD Session, i;
    HANDLE Pipe;

    ZeroMemory(Squat, sizeof(*Squat));
    for (Session = 1; Session <= LAST_SESSION; Session++)
    {
        _snwprintf(Name, ARRAYSIZE(Name), Format, Session);
        Name[ARRAYSIZE(Name) - 1] = 0;
        Pipe = CreateNamedPipeW(Name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
        if (Pipe == INVALID_HANDLE_VALUE)
            continue;
        i = Squat->Count++;
        Squat->Pipe[i] = Pipe;
        Squat->Session[i] = Session;
        Squat->Event[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        Squat->Overlapped[i].hEvent = Squat->Event[i];
        if (!ConnectNamedPipe(Pipe, &Squat->Overlapped[i]) && GetLastError() == ERROR_PIPE_CONNECTED)
            SetEvent(Squat->Event[i]);
    }
}

static VOID
ReleasePipes(PSQUAT Squat)
{
    DWORD i;

    for (i = 0; i < Squat->Count; i++)
    {
        CancelIo(Squat->Pipe[i]);
        CloseHandle(Squat->Pipe[i]);
        CloseHandle(Squat->Event[i]);
    }
    Squat->Count = 0;
}

static SECURITY_IMPERSONATION_LEVEL
ClientLevel(HANDLE Pipe)
{
    SECURITY_IMPERSONATION_LEVEL Level = SecurityAnonymous;
    HANDLE Token;
    DWORD Size;

    if (!ImpersonateNamedPipeClient(Pipe))
        return SecurityAnonymous;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &Token))
    {
        if (!GetTokenInformation(Token, TokenImpersonationLevel, &Level, sizeof(Level), &Size))
            Level = SecurityAnonymous;
        CloseHandle(Token);
    }
    RevertToSelf();
    return Level;
}

static VOID
TestSquattedPipes(PCWSTR Format, PCWSTR Kind, const char *User)
{
    SECURITY_IMPERSONATION_LEVEL Level;
    OVERLAPPED Overlapped = { 0 };
    BYTE Message[4096];
    RDP_CLIENT Client;
    SQUAT Squat;
    DWORD Index, Read = 0;
    HANDLE Pipe;

    SquatPipes(&Squat, Format);
    ok(Squat.Count != 0, "No %ls pipe name was free\n", Kind);
    if (!Squat.Count)
        return;
    RdpClientStart(&Client, User, User);
    Index = WaitForMultipleObjects(Squat.Count, Squat.Event, FALSE, 120000);
    ok(Index < Squat.Count, "Nothing opened a squatted %ls pipe: %lu\n", Kind, Index);
    if (Index < Squat.Count)
    {
        Pipe = Squat.Pipe[Index];
        Level = ClientLevel(Pipe);
        ok(Level < SecurityImpersonation, "The %ls pipe of session %lu let its maker act as the client (level %d)\n",
           Kind, Squat.Session[Index], Level);
        Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (ReadFile(Pipe, Message, sizeof(Message), &Read, &Overlapped) ||
            (GetLastError() == ERROR_IO_PENDING &&
             WaitForSingleObject(Overlapped.hEvent, 15000) == WAIT_OBJECT_0 &&
             GetOverlappedResult(Pipe, &Overlapped, &Read, FALSE)))
        {
            Level = ClientLevel(Pipe);
            ok(Level < SecurityImpersonation, "After a message, the %ls pipe let its maker act as the client (level %d)\n",
               Kind, Level);
        }
        else
        {
            CancelIo(Pipe);
            Read = 0;
        }
        CloseHandle(Overlapped.hEvent);
        ok(Read == 0, "The %ls pipe of session %lu, made by another process, got a %lu byte message\n",
           Kind, Squat.Session[Index], Read);
    }
    ReleasePipes(&Squat);
    RdpClientStop(&Client);
}

START_TEST(PipeSquat)
{
    RDP_TEST_SETUP Setup;
    WCHAR First[16], Second[16];
    char FirstA[16], SecondA[16];
    DWORD Run = GetTickCount() & 0xffffff;

    /* TODO: end the sessions this test starts with WTSLogoffSession once it is implemented; until then each run needs new users */
    _snwprintf(First, ARRAYSIZE(First), L"rdpsq%06lxa", Run);
    _snwprintf(Second, ARRAYSIZE(Second), L"rdpsq%06lxb", Run);
    _snprintf(FirstA, sizeof(FirstA), "%ls", First);
    _snprintf(SecondA, sizeof(SecondA), "%ls", Second);
    if (!RdpTestStart(&Setup))
        return;
    if (RdpTestAddUser(First) && RdpTestAddUser(Second))
    {
        TestSquattedPipes(AGENT_PIPE_FORMAT, L"session server", FirstA);
        TestSquattedPipes(CONSOLE_PIPE_FORMAT, L"Winlogon", SecondA);
    }
    NetUserDel(NULL, First);
    NetUserDel(NULL, Second);
    RdpTestEnd(&Setup);
}
