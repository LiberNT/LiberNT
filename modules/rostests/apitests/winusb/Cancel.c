/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test cancellation, abort and timeouts of pending WinUSB transfers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb_test.h"

#define PENDING_READS 4

typedef struct _PENDING_READ
{
    OVERLAPPED Overlapped;
    UCHAR Buffer[1024];
    ULONGLONG Done;
} PENDING_READ, *PPENDING_READ;

static BOOL PostReads(PTEST_DEVICE Device, PPENDING_READ Reads, ULONG Count, ULONG Length)
{
    ULONG Index;
    BOOL Posted = TRUE;

    for (Index = 0; Index < Count; Index++)
    {
        ZeroMemory(&Reads[Index].Overlapped, sizeof(Reads[Index].Overlapped));
        Reads[Index].Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        memset(Reads[Index].Buffer, 0xCC, sizeof(Reads[Index].Buffer));
        SetLastError(0xdeadbeef);
        if (WinUsb_ReadPipe(Device->Interface, PIPE_HELD_IN, Reads[Index].Buffer, Length, NULL,
                            &Reads[Index].Overlapped) || GetLastError() != ERROR_IO_PENDING)
        {
            ok(0, "Read %lu did not pend: %lu\n", Index, GetLastError());
            Posted = FALSE;
        }
    }
    return Posted;
}

static DWORD ReadResult(PTEST_DEVICE Device, PPENDING_READ Read, PULONG Transferred, DWORD WaitMs)
{
    *Transferred = 0;
    if (WaitForSingleObject(Read->Overlapped.hEvent, WaitMs) != WAIT_OBJECT_0)
        return TEST_TIMEOUT_ERROR;
    return WinUsb_GetOverlappedResult(Device->Interface, &Read->Overlapped, Transferred, FALSE) ? ERROR_SUCCESS
                                                                                               : GetLastError();
}

static VOID DrainReads(PTEST_DEVICE Device, PPENDING_READ Reads, ULONG Count)
{
    ULONG Index, Transferred;

    CancelIoEx(Device->File, NULL);
    for (Index = 0; Index < Count; Index++)
    {
        if (!Reads[Index].Overlapped.hEvent)
            continue;
        WaitForSingleObject(Reads[Index].Overlapped.hEvent, 5000);
        WinUsb_GetOverlappedResult(Device->Interface, &Reads[Index].Overlapped, &Transferred, FALSE);
        CloseHandle(Reads[Index].Overlapped.hEvent);
        Reads[Index].Overlapped.hEvent = NULL;
    }
}

static VOID TestCancelOne(PTEST_DEVICE Device, BOOL RawIo, ULONG Cancel)
{
    PENDING_READ Reads[PENDING_READS];
    DEVICE_STATUS Before, After;
    ULONG Index, Transferred, Expected = 0;
    DWORD Error;

    ZeroMemory(Reads, sizeof(Reads));
    ok(ResetDevice(Device->Interface), "Device reset failed\n");
    ok(SetPolicyBool(Device->Interface, PIPE_HELD_IN, RAW_IO, (UCHAR)RawIo), "RAW_IO policy failed\n");
    if (!PostReads(Device, Reads, PENDING_READS, 512))
    {
        DrainReads(Device, Reads, PENDING_READS);
        return;
    }
    Sleep(100);
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    for (Index = 0; Index < PENDING_READS; Index++)
        ok(WaitForSingleObject(Reads[Index].Overlapped.hEvent, 0) == WAIT_TIMEOUT, "raw %d: read %lu finished early\n",
           RawIo, Index);
    ok(Before.InPendingMask & (1 << 3), "raw %d: no transfer reached the device\n", RawIo);

    ok(CancelIoEx(Device->File, &Reads[Cancel].Overlapped), "raw %d: CancelIoEx(%lu) failed: %lu\n", RawIo, Cancel,
       GetLastError());
    Error = ReadResult(Device, &Reads[Cancel], &Transferred, 2000);
    ok(Error == ERROR_OPERATION_ABORTED && Transferred == 0, "raw %d: cancelled read %lu: %lu %lu\n", RawIo, Cancel,
       Error, Transferred);
    Sleep(100);
    for (Index = 0; Index < PENDING_READS; Index++)
    {
        if (Index != Cancel)
            ok(WaitForSingleObject(Reads[Index].Overlapped.hEvent, 0) == WAIT_TIMEOUT,
               "raw %d, cancel %lu: read %lu finished after the cancel\n", RawIo, Cancel, Index);
    }
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    trace("raw %d, cancel %lu: device cancels %lu, pending mask %08lx -> %08lx\n", RawIo, Cancel,
          After.InCancelled[3] - Before.InCancelled[3], Before.InPendingMask, After.InPendingMask);
    ok(After.InPendingMask & (1 << 3), "raw %d, cancel %lu: no transfer left at the device\n", RawIo, Cancel);

    ok(VendorOut(Device->Interface, REQ_RELEASE, PENDING_READS - 1, 100, NULL, 0), "RELEASE failed\n");
    for (Index = 0; Index < PENDING_READS; Index++)
    {
        if (Index == Cancel)
            continue;
        Error = ReadResult(Device, &Reads[Index], &Transferred, 2000);
        ok(Error == ERROR_SUCCESS && Transferred == 100, "raw %d, cancel %lu: read %lu: %lu %lu\n", RawIo, Cancel,
           Index, Error, Transferred);
        ok(*(PULONG)Reads[Index].Buffer == Expected, "raw %d, cancel %lu: read %lu got message %lu, expected %lu\n",
           RawIo, Cancel, Index, *(PULONG)Reads[Index].Buffer, Expected);
        Expected++;
    }
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.InQueuedPackets[3] == 0, "raw %d, cancel %lu: %lu packets left\n", RawIo, Cancel,
       After.InQueuedPackets[3]);
    DrainReads(Device, Reads, PENDING_READS);
    SetPolicyBool(Device->Interface, PIPE_HELD_IN, RAW_IO, FALSE);
}

static VOID TestAbortAndCancelAll(PTEST_DEVICE Device)
{
    PENDING_READ Reads[PENDING_READS];
    ULONG Index, Transferred;
    DWORD Error;
    BOOL Result;

    ZeroMemory(Reads, sizeof(Reads));
    ok(ResetDevice(Device->Interface), "Device reset failed\n");
    if (PostReads(Device, Reads, 3, 512))
    {
        Sleep(50);
        SetLastError(0xdeadbeef);
        Result = WinUsb_GetOverlappedResult(Device->Interface, &Reads[0].Overlapped, &Transferred, FALSE);
        Error = GetLastError();
        ok(!Result && Error == ERROR_IO_INCOMPLETE, "Result of a pending read: %d %lu\n", Result, Error);
        ok(WinUsb_AbortPipe(Device->Interface, PIPE_HELD_IN), "AbortPipe failed: %lu\n", GetLastError());
        for (Index = 0; Index < 3; Index++)
        {
            Error = ReadResult(Device, &Reads[Index], &Transferred, 0);
            ok(Error == ERROR_OPERATION_ABORTED, "Read %lu after AbortPipe: %lu\n", Index, Error);
        }
    }
    DrainReads(Device, Reads, 3);
    ok(VendorOut(Device->Interface, REQ_RELEASE, 1, 100, NULL, 0), "RELEASE failed\n");
    if (PostReads(Device, Reads, 1, 512))
    {
        Error = ReadResult(Device, &Reads[0], &Transferred, 2000);
        ok(Error == ERROR_SUCCESS && Transferred == 100 && *(PULONG)Reads[0].Buffer == 0, "Read after abort: %lu %lu %lu\n",
           Error, Transferred, *(PULONG)Reads[0].Buffer);
    }
    DrainReads(Device, Reads, 1);

    if (PostReads(Device, Reads, 3, 512))
    {
        ok(CancelIo(Device->File), "CancelIo failed: %lu\n", GetLastError());
        for (Index = 0; Index < 3; Index++)
        {
            Error = ReadResult(Device, &Reads[Index], &Transferred, 2000);
            ok(Error == ERROR_OPERATION_ABORTED, "Read %lu after CancelIo: %lu\n", Index, Error);
        }
    }
    DrainReads(Device, Reads, 3);

    if (PostReads(Device, Reads, 3, 512))
    {
        ok(CancelIoEx(Device->File, NULL), "CancelIoEx(NULL) failed: %lu\n", GetLastError());
        for (Index = 0; Index < 3; Index++)
        {
            Error = ReadResult(Device, &Reads[Index], &Transferred, 2000);
            ok(Error == ERROR_OPERATION_ABORTED, "Read %lu after CancelIoEx(NULL): %lu\n", Index, Error);
        }
    }
    DrainReads(Device, Reads, 3);
}

static VOID TestQueuedTimeouts(PTEST_DEVICE Device, BOOL RawIo)
{
    PENDING_READ Reads[3];
    ULONGLONG Start;
    ULONG Index, Transferred;
    DWORD Error;

    ZeroMemory(Reads, sizeof(Reads));
    ok(ResetDevice(Device->Interface), "Device reset failed\n");
    ok(SetPolicyBool(Device->Interface, PIPE_HELD_IN, RAW_IO, (UCHAR)RawIo), "RAW_IO policy failed\n");
    ok(SetPolicyUlong(Device->Interface, PIPE_HELD_IN, PIPE_TRANSFER_TIMEOUT, 300), "Timeout policy failed\n");
    Start = NowMs();
    if (PostReads(Device, Reads, 3, 512))
    {
        for (Index = 0; Index < 3; Index++)
        {
            Error = ReadResult(Device, &Reads[Index], &Transferred, 3000);
            Reads[Index].Done = NowMs() - Start;
            ok(Error == ERROR_SEM_TIMEOUT, "raw %d: read %lu: %lu\n", RawIo, Index, Error);
        }
        trace("raw %d: reads timed out after %I64u, %I64u and %I64u ms\n", RawIo, Reads[0].Done, Reads[1].Done,
              Reads[2].Done);
        for (Index = 0; Index < 3; Index++)
        {
            ULONGLONG Low = RawIo ? 250 : 250 * (Index + 1), High = RawIo ? 900 : 300 * (Index + 1) + 600;

            ok(Reads[Index].Done >= Low && Reads[Index].Done < High, "raw %d: read %lu timed out after %I64u ms\n",
               RawIo, Index, Reads[Index].Done);
        }
    }
    DrainReads(Device, Reads, 3);
    SetPolicyUlong(Device->Interface, PIPE_HELD_IN, PIPE_TRANSFER_TIMEOUT, 0);
    SetPolicyBool(Device->Interface, PIPE_HELD_IN, RAW_IO, FALSE);
    ok(VendorOut(Device->Interface, REQ_RELEASE, 1, 64, NULL, 0), "RELEASE failed\n");
    if (PostReads(Device, Reads, 1, 512))
    {
        Error = ReadResult(Device, &Reads[0], &Transferred, 2000);
        ok(Error == ERROR_SUCCESS && Transferred == 64, "raw %d: read after the timeouts: %lu %lu\n", RawIo, Error,
           Transferred);
    }
    DrainReads(Device, Reads, 1);
}

static DWORD WINAPI FreeThread(PVOID Interface)
{
    return WinUsb_Free((WINUSB_INTERFACE_HANDLE)Interface);
}

static VOID TestFreeWithPendingReads(VOID)
{
    PENDING_READ Reads[2];
    TEST_DEVICE Device;
    ULONG Index, Transferred;
    HANDLE Thread;
    DWORD Error, Wait, ExitCode = 0;

    if (!OpenTestDevice(&GUID_WINUSB_TEST, &Device))
        return;
    ZeroMemory(Reads, sizeof(Reads));
    if (PostReads(&Device, Reads, 2, 512))
    {
        Thread = CreateThread(NULL, 0, FreeThread, Device.Interface, 0, NULL);
        Wait = WaitForSingleObject(Thread, 2000);
        ok(Wait == WAIT_OBJECT_0, "WinUsb_Free with pending reads blocked: %lu\n", Wait);
        if (Wait != WAIT_OBJECT_0)
        {
            CancelIoEx(Device.File, NULL);
            WaitForSingleObject(Thread, 5000);
        }
        GetExitCodeThread(Thread, &ExitCode);
        ok(ExitCode == TRUE, "WinUsb_Free with pending reads returned %lu\n", ExitCode);
        CloseHandle(Thread);
        Sleep(100);
        for (Index = 0; Index < 2; Index++)
        {
            Error = WaitForSingleObject(Reads[Index].Overlapped.hEvent, 0);
            ok(Error == WAIT_TIMEOUT, "Read %lu after WinUsb_Free: wait %lu\n", Index, Error);
            Error = GetOverlappedResult(Device.File, &Reads[Index].Overlapped, &Transferred, FALSE) ? ERROR_SUCCESS
                                                                                                  : GetLastError();
            ok(Error == ERROR_IO_INCOMPLETE, "Read %lu after WinUsb_Free: %lu\n", Index, Error);
        }
        Device.Interface = NULL;
    }
    ok(CancelIoEx(Device.File, NULL), "CancelIoEx after WinUsb_Free failed: %lu\n", GetLastError());
    for (Index = 0; Index < 2; Index++)
    {
        if (!Reads[Index].Overlapped.hEvent)
            continue;
        Error = WaitForSingleObject(Reads[Index].Overlapped.hEvent, 2000);
        ok(Error == WAIT_OBJECT_0, "Read %lu not cancelled after WinUsb_Free: %lu\n", Index, Error);
        Error = GetOverlappedResult(Device.File, &Reads[Index].Overlapped, &Transferred, FALSE) ? ERROR_SUCCESS
                                                                                              : GetLastError();
        ok(Error == ERROR_OPERATION_ABORTED, "Read %lu cancelled after WinUsb_Free: %lu\n", Index, Error);
    }
    for (Index = 0; Index < 2; Index++)
    {
        if (Reads[Index].Overlapped.hEvent)
        {
            WaitForSingleObject(Reads[Index].Overlapped.hEvent, 2000);
            CloseHandle(Reads[Index].Overlapped.hEvent);
        }
    }
    CloseTestDevice(&Device);
}

START_TEST(Cancel)
{
    TEST_DEVICE Device;

    if (!OpenTestDevice(&GUID_WINUSB_TEST, &Device))
    {
        skip("No usb-winusb device: %lu\n", GetLastError());
        return;
    }

    TestCancelOne(&Device, FALSE, 1);
    TestCancelOne(&Device, FALSE, 0);
    TestCancelOne(&Device, FALSE, 3);
    TestCancelOne(&Device, TRUE, 1);
    TestCancelOne(&Device, TRUE, 0);
    TestCancelOne(&Device, TRUE, 3);
    TestAbortAndCancelAll(&Device);
    TestQueuedTimeouts(&Device, FALSE);
    TestQueuedTimeouts(&Device, TRUE);
    CloseTestDevice(&Device);
    TestFreeWithPendingReads();
}
