/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GNU GPLv2 only as published by the Free Software Foundation
 * PURPOSE:         Test for mailslot (CORE-10188)
 * PROGRAMMER:      Nikita Pechenkin (n.pechenkin@mail.ru)
 */

#include "precomp.h"

#define LMS TEXT("\\\\.\\mailslot\\rostest_slot")
#define MSG (0x50DA)

static DWORD dInMsg  = MSG;
static DWORD dOutMsg = 0x0;

static
VOID
WaitIoPending(
        HANDLE hThread)
{
    BOOL Pending = FALSE;
    ULONG i;

    for (i = 0; i < 100000 && GetThreadIOPendingFlag(hThread, &Pending) && !Pending; i++)
        SwitchToThread();
}

DWORD
WINAPI
MailSlotWriter(
        LPVOID lpParam)
{
    DWORD cbWritten;
    HANDLE hMailslot;

    hMailslot = CreateFile(LMS, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(hMailslot != INVALID_HANDLE_VALUE, "CreateFile failed, results might not be accurate\n");
    if (hMailslot != INVALID_HANDLE_VALUE)
    {
        WaitIoPending((HANDLE)lpParam);
        ok(WriteFile(hMailslot, &dInMsg, sizeof(dInMsg), &cbWritten, (LPOVERLAPPED) NULL), "Slot write failed\n");
        CloseHandle(hMailslot);
    }
    return 0;
}

DWORD
WINAPI
MailSlotReader(
        LPVOID lpParam)
{
    HANDLE hMailslotClient;
    DWORD cbRead;
    HANDLE hThread;
    HANDLE hReader;

    hMailslotClient = CreateMailslot(LMS, 0L, MAILSLOT_WAIT_FOREVER, (LPSECURITY_ATTRIBUTES) NULL);
    ok(hMailslotClient != INVALID_HANDLE_VALUE, "CreateMailslot failed\n");
    if (hMailslotClient != INVALID_HANDLE_VALUE)
    {
        hReader = OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
        hThread = CreateThread(NULL,0, MailSlotWriter, hReader, 0, NULL);
        ok(hThread != INVALID_HANDLE_VALUE, "CreateThread failed\n");
        if (hThread != INVALID_HANDLE_VALUE)
        {
            ok(ReadFile(hMailslotClient, &dOutMsg, sizeof(dOutMsg), &cbRead, NULL), "Slot read failed\n");
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
        CloseHandle(hReader);
        CloseHandle(hMailslotClient);
    }
    return 0;
}

VOID
StartTestCORE10188(VOID)
{
    HANDLE  hThread;

    hThread = CreateThread(NULL,0, MailSlotReader, NULL, 0, NULL);
    ok(hThread != INVALID_HANDLE_VALUE, "CreateThread failed\n");
    if (hThread != INVALID_HANDLE_VALUE)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
    ok(dInMsg == dOutMsg, "Transfer data failed\n");
}

START_TEST(MailslotRead)
{
    StartTestCORE10188();
}
