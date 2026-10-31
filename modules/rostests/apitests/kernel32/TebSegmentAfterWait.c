/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the FS segment of 32-bit threads maps their own TEB after waits that switch threads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#if defined(_M_IX86)

static HANDLE Ping, Pong;
static volatile LONG Mismatches;

static void CheckSegment(PVOID Self, DWORD Id)
{
    PVOID Teb;
    DWORD FsId;

#if defined(_MSC_VER)
    Teb = (PVOID)__readfsdword(0x18);
    FsId = __readfsdword(0x24);
#else
    __asm__ __volatile__("movl %%fs:0x18, %0" : "=r"(Teb));
    __asm__ __volatile__("movl %%fs:0x24, %0" : "=r"(FsId));
#endif
    if (Teb != Self || FsId != Id)
        InterlockedIncrement(&Mismatches);
}

static DWORD WINAPI PingPong(LPVOID Second)
{
    PVOID Self = NtCurrentTeb();
    DWORD Id = GetCurrentThreadId(), Index;

    for (Index = 0; Index < 20000; Index++)
    {
        WaitForSingleObject(Second ? Pong : Ping, INFINITE);
        CheckSegment(Self, Id);
        SetEvent(Second ? Ping : Pong);
        CheckSegment(Self, Id);
        if ((Index & 63) == 0)
        {
            Sleep(0);
            CheckSegment(Self, Id);
        }
    }
    return 0;
}

#endif

START_TEST(TebSegmentAfterWait)
{
#if defined(_M_IX86)
    HANDLE Threads[2];

    Ping = CreateEventW(NULL, FALSE, TRUE, NULL);
    Pong = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Ping != NULL && Pong != NULL, "CreateEventW failed with %lu\n", GetLastError());
    Threads[0] = CreateThread(NULL, 0, PingPong, NULL, 0, NULL);
    Threads[1] = CreateThread(NULL, 0, PingPong, (LPVOID)1, 0, NULL);
    ok(Threads[0] != NULL && Threads[1] != NULL, "CreateThread failed with %lu\n", GetLastError());
    ok(WaitForMultipleObjects(2, Threads, TRUE, 20000) == WAIT_OBJECT_0, "The threads did not finish\n");
    ok_long(Mismatches, 0);
    CloseHandle(Threads[0]);
    CloseHandle(Threads[1]);
    CloseHandle(Ping);
    CloseHandle(Pong);
#else
    skip("The FS segment maps the TEB only for 32-bit code\n");
#endif
}
