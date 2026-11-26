/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Instances of one pipe listening again at the same time as clients come and go
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define INSTANCES 4
#define RUN_MS 2000
#define JOIN_MS 30000

static WCHAR PipeName[64];
static volatile LONG Stop;

typedef struct _SERVER
{
    HANDLE Pipe;
    LONG Connections;
    DWORD Error;
} SERVER;

static DWORD WINAPI
ServerThread(PVOID Parameter)
{
    SERVER *Server = Parameter;
    CHAR Byte;
    DWORD Got;

    while (!Stop)
    {
        /* A client may have come and gone before the server listened: that ends as ERROR_NO_DATA */
        if (!ConnectNamedPipe(Server->Pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED &&
            GetLastError() != ERROR_NO_DATA)
        {
            Server->Error = GetLastError();
            break;
        }
        InterlockedIncrement(&Server->Connections);
        while (ReadFile(Server->Pipe, &Byte, 1, &Got, NULL))
            ;
        if (!DisconnectNamedPipe(Server->Pipe))
        {
            Server->Error = GetLastError();
            break;
        }
    }
    return 0;
}

static DWORD WINAPI
ClientThread(PVOID Parameter)
{
    HANDLE Pipe;

    UNREFERENCED_PARAMETER(Parameter);
    while (!Stop)
    {
        if (!WaitNamedPipeW(PipeName, 100))
            continue;
        Pipe = CreateFileW(PipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (Pipe != INVALID_HANDLE_VALUE)
            CloseHandle(Pipe);
    }
    return 0;
}

START_TEST(PipeRelisten)
{
    SERVER Servers[INSTANCES];
    HANDLE Threads[INSTANCES * 2];
    HANDLE Pipe;
    LONG Total = 0;
    ULONG Index, Started = 0, Waited;
    DWORD Result = WAIT_FAILED;

    StringCchPrintfW(PipeName, ARRAYSIZE(PipeName), L"\\\\.\\pipe\\PipeRelisten_%lu", GetCurrentProcessId());
    ZeroMemory(Servers, sizeof(Servers));
    for (Index = 0; Index < INSTANCES; ++Index)
    {
        Servers[Index].Pipe = CreateNamedPipeW(PipeName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, INSTANCES,
                                               64, 64, 0, NULL);
        ok(Servers[Index].Pipe != INVALID_HANDLE_VALUE, "CreateNamedPipeW %lu failed with %lu\n", Index, GetLastError());
        if (Servers[Index].Pipe == INVALID_HANDLE_VALUE)
            goto Cleanup;
    }

    for (Index = 0; Index < INSTANCES; ++Index)
    {
        Threads[Started] = CreateThread(NULL, 0, ServerThread, &Servers[Index], 0, NULL);
        ok(Threads[Started] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Threads[Started] != NULL) Started++;
        Threads[Started] = CreateThread(NULL, 0, ClientThread, NULL, 0, NULL);
        ok(Threads[Started] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Threads[Started] != NULL) Started++;
    }

    Sleep(RUN_MS);
    InterlockedExchange(&Stop, 1);

    /* A server still blocked in ConnectNamedPipe after the clients stopped is let go by one more client */
    for (Waited = 0; Waited < JOIN_MS; Waited += 50)
    {
        Result = WaitForMultipleObjects(Started, Threads, TRUE, 50);
        if (Result < WAIT_OBJECT_0 + Started)
            break;
        Pipe = CreateFileW(PipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (Pipe != INVALID_HANDLE_VALUE)
            CloseHandle(Pipe);
    }
    ok(Result < WAIT_OBJECT_0 + Started, "The pipe threads did not finish: %#lx\n", Result);
    for (Index = 0; Index < Started; ++Index)
        CloseHandle(Threads[Index]);

    for (Index = 0; Index < INSTANCES; ++Index)
    {
        ok(Servers[Index].Error == 0, "Instance %lu failed with %lu\n", Index, Servers[Index].Error);
        Total += Servers[Index].Connections;
    }
    ok(Total >= INSTANCES, "Only %ld connections were made\n", Total);

Cleanup:
    for (Index = 0; Index < INSTANCES; ++Index)
    {
        if (Servers[Index].Pipe != NULL && Servers[Index].Pipe != INVALID_HANDLE_VALUE)
            CloseHandle(Servers[Index].Pipe);
    }
}
