/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host synchronization for the NDIS reset source fixture
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "thread_host.h"

static CRITICAL_SECTION Lock;
static HANDLE Events[3], Thread;
static void (*ThreadCallback)(void);

int ResetTestHostInitialize(void)
{
    unsigned Index;
    InitializeCriticalSection(&Lock);
    for (Index = 0; Index < 3; ++Index)
    {
        Events[Index] = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!Events[Index])
        {
            ResetTestHostDestroy();
            return 0;
        }
    }
    return 1;
}

void ResetTestHostDestroy(void)
{
    unsigned Index;
    for (Index = 0; Index < 3; ++Index)
        if (Events[Index]) CloseHandle(Events[Index]);
    DeleteCriticalSection(&Lock);
}

void ResetTestHostEnter(void)
{
    EnterCriticalSection(&Lock);
}

void ResetTestHostLeave(void)
{
    LeaveCriticalSection(&Lock);
}

int ResetTestHostWait(unsigned Event)
{
    return Event < 3 && WaitForSingleObject(Events[Event], 10000) == WAIT_OBJECT_0;
}

void ResetTestHostSignal(unsigned Event)
{
    if (Event < 3) SetEvent(Events[Event]);
}

static DWORD WINAPI RunThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    ThreadCallback();
    return 0;
}

int ResetTestHostRun(void (*Callback)(void))
{
    ThreadCallback = Callback;
    Thread = CreateThread(NULL, 0, RunThread, NULL, 0, NULL);
    return Thread != NULL;
}

int ResetTestHostJoin(void)
{
    if (WaitForSingleObject(Thread, 10000) != WAIT_OBJECT_0)
        return 0;
    CloseHandle(Thread);
    Thread = NULL;
    return 1;
}
