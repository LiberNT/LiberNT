/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test WinUSB bulk, interrupt and control transfers, pipe policies, stalls and settings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb_test.h"

static PUCHAR Out, In;
#define BUFFER_SIZE (5 * 1024 * 1024)

static VOID Loopback(PTEST_DEVICE Device, WINUSB_INTERFACE_HANDLE Interface, UCHAR OutPipe, UCHAR InPipe,
                     ULONG Size, ULONG Seed)
{
    ULONG Written = 0xdeadbeef, Read = 0;
    DWORD Error;
    BOOL Result;

    FillPattern(Out, Size, Seed);
    memset(In, 0xCC, Size + 1);
    Result = WinUsb_WritePipe(Interface, OutPipe, Out, Size, &Written, NULL);
    ok(Result && Written == Size, "Write of %lu: %d written %lu error %lu\n", Size, Result, Written,
       Result ? 0 : GetLastError());
    Error = ReadWait(Device, Interface, InPipe, In, Size, &Read, 5000);
    ok(Error == ERROR_SUCCESS && Read == Size, "Read of %lu: error %lu read %lu\n", Size, Error, Read);
    ok(CheckPattern(In, Read, Seed), "Data of %lu differs\n", Size);
    ok(In[Size] == 0xCC, "Read of %lu wrote past the buffer\n", Size);
}

static VOID CleanState(PTEST_DEVICE Device)
{
    ok(ResetDevice(Device->Interface), "Device reset failed: %lu\n", GetLastError());
    ok(WinUsb_FlushPipe(Device->Interface, PIPE_LOOP_IN), "FlushPipe failed: %lu\n", GetLastError());
}

static VOID ReadSize(PTEST_DEVICE Device, ULONG Write, ULONG ReadLength, ULONG ExpectedRead, ULONG ExpectedLast,
                     ULONG ExpectedPrevious, ULONG ExpectedTransfers)
{
    DEVICE_STATUS Before, After;
    ULONG Read, Written;
    DWORD Error;

    CleanState(Device);
    FillPattern(Out, Write, Write);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, Write, &Written, NULL) && Written == Write,
       "Write of %lu: %lu %lu\n", Write, Written, GetLastError());
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, ReadLength, &Read, 5000);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(Error == ERROR_SUCCESS && Read == ExpectedRead && CheckPattern(In, Read, Write),
       "Read of %lu after %lu: %lu %lu\n", ReadLength, Write, Error, Read);
    ok(After.InTransfers[1] - Before.InTransfers[1] == ExpectedTransfers && After.InLastSize[1] == ExpectedLast &&
       (ExpectedTransfers < 2 || After.InPrevSize[1] == ExpectedPrevious),
       "Read of %lu after %lu: %lu transfers, last %lu, previous %lu\n", ReadLength, Write,
       After.InTransfers[1] - Before.InTransfers[1], After.InLastSize[1], After.InPrevSize[1]);
    ok(After.InQueuedBytes[1] == Write - ExpectedRead, "Read of %lu after %lu left %lu bytes\n", ReadLength, Write,
       After.InQueuedBytes[1]);
    CleanState(Device);
}

static VOID TestSizes(PTEST_DEVICE Device)
{
    ULONG MaxTransfer = 0, Length = sizeof(MaxTransfer);

    WinUsb_GetPipePolicy(Device->Interface, PIPE_LOOP_IN, MAXIMUM_TRANSFER_SIZE, &Length, &MaxTransfer);
    ReadSize(Device, 100, 100, 100, 512, 0, 1);
    ReadSize(Device, 100, 1000, 100, 512, 0, 1);
    ReadSize(Device, 1636, 1636, 1636, 512, 1536, 2);
    ReadSize(Device, 4096, 4096, 4096, 4096, 0, 1);
    if (MaxTransfer && MaxTransfer + 1024 <= BUFFER_SIZE)
    {
        ReadSize(Device, MaxTransfer + 612, MaxTransfer + 612, MaxTransfer + 512, 512, MaxTransfer, 2);
        ReadSize(Device, MaxTransfer + 100, MaxTransfer + 100, MaxTransfer + 100, 512, MaxTransfer, 2);
        ReadSize(Device, 2 * MaxTransfer + 100, 2 * MaxTransfer + 100, 2 * MaxTransfer + 100, 512, MaxTransfer, 3);
        ReadSize(Device, MaxTransfer + 1024, MaxTransfer + 1024, MaxTransfer + 1024, 1024, MaxTransfer, 2);
    }
}

static VOID TestBulk(PTEST_DEVICE Device)
{
    static const ULONG Sizes[] = { 1, 2, 63, 64, 65, 511, 512, 513, 1000, 1024, 4095, 4096, 4097, 65535, 65536,
                                   65537, 200000, 1048576 };
    DEVICE_STATUS Before, After;
    ULONG Index, Read, Written, MaxTransfer, Length, Expected;
    ULONGLONG Start;
    DWORD Error;
    BOOL Result;

    for (Index = 0; Index < ARRAYSIZE(Sizes); Index++)
        Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, Sizes[Index], Index);

    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    Result = WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 0, &Written, NULL);
    ok(Result && Written == 0, "Zero-length write: %d %lu %lu\n", Result, Written, Result ? 0 : GetLastError());
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.OutZlps[1] == Before.OutZlps[1] + 1, "Zero-length packets %lu -> %lu\n", Before.OutZlps[1],
       After.OutZlps[1]);
    Read = 0xdeadbeef;
    Result = WinUsb_ReadPipe(Device->Interface, PIPE_LOOP_IN, In, 0, &Read, NULL);
    ok(Result, "Zero-length read: %d %lu\n", Result, Result ? 0 : GetLastError());
    trace("Zero-length read reported %lu bytes\n", Read);
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(Before.InTransfers[1] == After.InTransfers[1], "Zero-length read reached the device: %lu -> %lu\n",
       After.InTransfers[1], Before.InTransfers[1]);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 0, "Read of the zero-length packet: %lu %lu\n", Error, Read);

    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_IN, ALLOW_PARTIAL_READS, FALSE), "ALLOW_PARTIAL_READS off failed\n");
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 0, &Written, NULL), "Zero-length write failed\n");
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 0, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 0, "Zero-length read without partial reads: %lu %lu\n", Error, Read);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.InTransfers[1] == Before.InTransfers[1] + 1 && After.InQueuedPackets[1] == 0,
       "Zero-length read without partial reads: IN %lu -> %lu, %lu packets left\n", Before.InTransfers[1],
       After.InTransfers[1], After.InQueuedPackets[1]);
    SetPolicyBool(Device->Interface, PIPE_LOOP_IN, ALLOW_PARTIAL_READS, TRUE);

    Length = sizeof(MaxTransfer);
    MaxTransfer = 0;
    WinUsb_GetPipePolicy(Device->Interface, PIPE_LOOP_OUT, MAXIMUM_TRANSFER_SIZE, &Length, &MaxTransfer);
    if (MaxTransfer && 2 * MaxTransfer + 512 <= BUFFER_SIZE)
    {
        ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
        Start = NowMs();
        Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 2 * MaxTransfer + 512, 77);
        trace("%lu bytes looped back in %I64u ms\n", 2 * MaxTransfer + 512, NowMs() - Start);
        ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
        Expected = 3;
        ok(After.OutTransfers[1] - Before.OutTransfers[1] == Expected && After.OutLastSize[1] == 512,
           "Write of %lu took %lu OUT transfers, last %lu\n", 2 * MaxTransfer + 512,
           After.OutTransfers[1] - Before.OutTransfers[1], After.OutLastSize[1]);
        ok(After.InTransfers[1] - Before.InTransfers[1] == Expected && After.InLastSize[1] == 512,
           "Read of %lu took %lu IN transfers, last %lu\n", 2 * MaxTransfer + 512,
           After.InTransfers[1] - Before.InTransfers[1], After.InLastSize[1]);
    }

    {
        OVERLAPPED ReadOverlapped, WriteOverlapped;
        ULONG Size = 4 * 1024 * 1024, ReadDone = 0, WriteDone = 0;
        BOOL ReadResult, WriteResult;

        FillPattern(Out, Size, 91);
        memset(In, 0xCC, Size);
        ZeroMemory(&ReadOverlapped, sizeof(ReadOverlapped));
        ZeroMemory(&WriteOverlapped, sizeof(WriteOverlapped));
        ReadOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        WriteOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        ReadResult = WinUsb_ReadPipe(Device->Interface, PIPE_LOOP_IN, In, Size, NULL, &ReadOverlapped);
        Error = GetLastError();
        ok(!ReadResult && Error == ERROR_IO_PENDING, "Pending read of %lu: %d %lu\n", Size, ReadResult, Error);
        WriteResult = WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, Size, NULL, &WriteOverlapped);
        Error = GetLastError();
        ok(WriteResult || Error == ERROR_IO_PENDING, "Write of %lu beside a pending read: %d %lu\n", Size, WriteResult, Error);
        ok(WaitForSingleObject(WriteOverlapped.hEvent, 5000) == WAIT_OBJECT_0, "Write of %lu did not finish\n", Size);
        ok(WaitForSingleObject(ReadOverlapped.hEvent, 5000) == WAIT_OBJECT_0, "Read of %lu did not finish\n", Size);
        if (!WinUsb_GetOverlappedResult(Device->Interface, &WriteOverlapped, &WriteDone, FALSE))
            CancelIoEx(Device->File, &WriteOverlapped);
        if (!WinUsb_GetOverlappedResult(Device->Interface, &ReadOverlapped, &ReadDone, FALSE))
            CancelIoEx(Device->File, &ReadOverlapped);
        ok(WriteDone == Size && ReadDone == Size, "Concurrent %lu-byte write and read moved %lu and %lu bytes\n", Size,
           WriteDone, ReadDone);
        ok(CheckPattern(In, ReadDone, 91), "Concurrent read data differs\n");
        WaitForSingleObject(WriteOverlapped.hEvent, 5000);
        WaitForSingleObject(ReadOverlapped.hEvent, 5000);
        CloseHandle(ReadOverlapped.hEvent);
        CloseHandle(WriteOverlapped.hEvent);
    }
}

static VOID TestShortPackets(PTEST_DEVICE Device)
{
    DEVICE_STATUS Before, After;
    ULONG Read, Written;
    ULONGLONG Start, Elapsed;
    DWORD Error;
    BOOL Result;

    CleanState(Device);
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_OUT, SHORT_PACKET_TERMINATE, TRUE), "SHORT_PACKET_TERMINATE failed\n");
    FillPattern(Out, 1024, 3);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 512, &Written, NULL) && Written == 512,
       "Write of 512 with ZLP: %lu\n", Written);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.OutZlps[1] == Before.OutZlps[1] + 1 && After.OutTransfers[1] == Before.OutTransfers[1] + 2,
       "Write of 512 with ZLP: transfers %lu zlps %lu\n", After.OutTransfers[1] - Before.OutTransfers[1],
       After.OutZlps[1] - Before.OutZlps[1]);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 1024, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 512 && CheckPattern(In, 512, 3), "Read after ZLP: %lu %lu\n", Error, Read);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 1000, &Written, NULL) && Written == 1000,
       "Write of 1000 with ZLP policy: %lu\n", Written);
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(Before.OutZlps[1] == After.OutZlps[1], "Short write got a ZLP\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 1000, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 1000, "Read of 1000: %lu %lu\n", Error, Read);
    SetPolicyBool(Device->Interface, PIPE_LOOP_OUT, SHORT_PACKET_TERMINATE, FALSE);

    ok(SetPolicyUlong(Device->Interface, PIPE_LOOP_IN, PIPE_TRANSFER_TIMEOUT, 300), "Timeout policy failed\n");
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 512, &Written, NULL), "Write of 512 failed\n");
    Start = NowMs();
    Read = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Result = WinUsb_ReadPipe(Device->Interface, PIPE_LOOP_IN, In, 1024, &Read, NULL);
    Error = GetLastError();
    Elapsed = NowMs() - Start;
    ok(!Result && Error == ERROR_SEM_TIMEOUT, "Read of 1024 after 512 without ZLP: %d %lu\n", Result, Error);
    ok(Read == 512, "Timed out read transferred %lu\n", Read);
    ok(Elapsed >= 250 && Elapsed < 1500, "Timed out after %I64u ms\n", Elapsed);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.InQueuedBytes[1] == 0, "%lu bytes left in the device\n", After.InQueuedBytes[1]);
    ok(After.HaltMask == 0, "Halt mask %08lx\n", After.HaltMask);
    Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 100, 5);
    SetPolicyUlong(Device->Interface, PIPE_LOOP_IN, PIPE_TRANSFER_TIMEOUT, 0);

    FillPattern(Out, 300, 9);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, NULL), "Write of 100 failed\n");
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out + 100, 200, &Written, NULL), "Write of 200 failed\n");
    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_IN, IGNORE_SHORT_PACKETS, TRUE), "IGNORE_SHORT_PACKETS failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 300, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 300 && CheckPattern(In, 300, 9), "Read across short packets: %lu %lu\n",
       Error, Read);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, NULL), "Write of 100 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 1024, &Read, 500);
    ok(Error == TEST_TIMEOUT_ERROR, "Read of 1024 after 100 with IGNORE_SHORT_PACKETS: %lu %lu\n", Error, Read);
    trace("Cancelled IGNORE_SHORT_PACKETS read transferred %lu\n", Read);
    SetPolicyBool(Device->Interface, PIPE_LOOP_IN, IGNORE_SHORT_PACKETS, FALSE);
    WinUsb_FlushPipe(Device->Interface, PIPE_LOOP_IN);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    if (After.InQueuedBytes[1])
    {
        trace("%lu bytes left in the device after the cancelled read\n", After.InQueuedBytes[1]);
        ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 1024, &Read, 500);
    }
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, NULL), "Write of 100 failed\n");
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out + 100, 200, &Written, NULL), "Write of 200 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 300, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100, "Read up to the first short packet: %lu %lu\n", Error, Read);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 300, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 200 && !memcmp(In, Out + 100, 200), "Read of the second packet: %lu %lu\n",
       Error, Read);
}

static VOID TestPartialReads(PTEST_DEVICE Device)
{
    DEVICE_STATUS Before, After;
    ULONG Read, Written;
    DWORD Error;
    BOOL Result;

    CleanState(Device);
    FillPattern(Out, 300, 11);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 300, &Written, NULL), "Write of 300 failed\n");
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 100, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100 && CheckPattern(In, 100, 11), "Partial read of 100: %lu %lu\n", Error,
       Read);
    memset(In, 0xCC, 512);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 200 && !memcmp(In, Out + 100, 200), "Read of the saved bytes: %lu %lu\n",
       Error, Read);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.InTransfers[1] == Before.InTransfers[1] + 1, "Saved bytes took %lu IN transfers\n",
       After.InTransfers[1] - Before.InTransfers[1]);

    FillPattern(Out, 300, 12);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 300, &Written, NULL), "Write of 300 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 100, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100, "Partial read before a short read: %lu %lu\n", Error, Read);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 50, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 50 && !memcmp(In, Out + 100, 50), "Read of 50 saved bytes: %lu %lu\n",
       Error, Read);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 150 && !memcmp(In, Out + 150, 150), "Read of the last saved bytes: %lu %lu\n",
       Error, Read);

    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_IN, AUTO_FLUSH, TRUE), "AUTO_FLUSH failed\n");
    FillPattern(Out, 300, 13);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 300, &Written, NULL), "Write of 300 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 100, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100 && CheckPattern(In, 100, 13), "Flushed partial read: %lu %lu\n", Error,
       Read);
    FillPattern(Out, 50, 14);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 50, &Written, NULL), "Write of 50 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 50 && CheckPattern(In, 50, 14), "Read after AUTO_FLUSH: %lu %lu\n", Error,
       Read);
    SetPolicyBool(Device->Interface, PIPE_LOOP_IN, AUTO_FLUSH, FALSE);

    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_IN, ALLOW_PARTIAL_READS, FALSE), "ALLOW_PARTIAL_READS failed\n");
    FillPattern(Out, 300, 15);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 300, &Written, NULL), "Write of 300 failed\n");
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 100, &Read, 2000);
    ok(Error == ERROR_CRC && Read == 100 && CheckPattern(In, 100, 15), "Read of 100 without partial reads: %lu %lu\n",
       Error, Read);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.InQueuedBytes[1] == 0, "Bytes left in the device: %lu\n", After.InQueuedBytes[1]);
    ok(After.InTransfers[1] - Before.InTransfers[1] == 1 && After.InLastSize[1] == 512 && After.HaltMask == 0 &&
       After.ClearHalts == Before.ClearHalts, "Overflowing read: %lu transfers of %lu, halt %08lx, clear halts %lu\n",
       After.InTransfers[1] - Before.InTransfers[1], After.InLastSize[1], After.HaltMask,
       After.ClearHalts - Before.ClearHalts);
    Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 100, 20);
    SetLastError(0xdeadbeef);
    Result = WinUsb_ResetPipe(Device->Interface, PIPE_LOOP_IN);
    ok(Result, "ResetPipe after the overflow: %lu\n", GetLastError());
    Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 512 * 3, 16);
    Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 100, 17);
    SetPolicyBool(Device->Interface, PIPE_LOOP_IN, ALLOW_PARTIAL_READS, TRUE);

    FillPattern(Out, 300, 18);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 300, &Written, NULL), "Write of 300 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 100, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100, "Partial read before flush: %lu %lu\n", Error, Read);
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(WinUsb_FlushPipe(Device->Interface, PIPE_LOOP_IN), "FlushPipe failed: %lu\n", GetLastError());
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.InTransfers[1] == Before.InTransfers[1] && After.ClearHalts == Before.ClearHalts,
       "FlushPipe reached the device\n");
    FillPattern(Out, 50, 19);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 50, &Written, NULL), "Write of 50 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 50 && CheckPattern(In, 50, 19), "Read after FlushPipe: %lu %lu\n", Error,
       Read);
    SetLastError(0xdeadbeef);
    Result = WinUsb_FlushPipe(Device->Interface, 0x8F);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "FlushPipe(8f): %d %lu\n", Result, Error);
}

static VOID TestRawIo(PTEST_DEVICE Device)
{
    ULONG Read, Written, MaxTransfer, Length;
    DWORD Error;

    CleanState(Device);
    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_IN, RAW_IO, TRUE), "RAW_IO failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 100, &Read, 1000);
    ok(Error == ERROR_INVALID_FUNCTION, "RAW_IO read of 100: %lu %lu\n", Error, Read);
    Length = sizeof(MaxTransfer);
    MaxTransfer = 0;
    WinUsb_GetPipePolicy(Device->Interface, PIPE_LOOP_IN, MAXIMUM_TRANSFER_SIZE, &Length, &MaxTransfer);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, MaxTransfer + 512, &Read, 1000);
    ok(Error == ERROR_INVALID_FUNCTION, "RAW_IO read of %lu: %lu\n", MaxTransfer + 512, Error);
    FillPattern(Out, 2048, 21);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 1000, &Written, NULL), "Write of 1000 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 1024, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 1000 && CheckPattern(In, 1000, 21), "RAW_IO read of 1024: %lu %lu\n", Error,
       Read);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 0, &Written, NULL), "Zero-length write failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 0, &Read, 1000);
    ok(Error == ERROR_INVALID_FUNCTION, "RAW_IO zero-length read: %lu %lu\n", Error, Read);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 500);
    ok(Error == ERROR_SUCCESS && Read == 0, "RAW_IO read of the ZLP: %lu %lu\n", Error, Read);
    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_OUT, RAW_IO, TRUE), "RAW_IO on OUT failed\n");
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, NULL) && Written == 100,
       "Write of 100 with RAW_IO on OUT: %lu %lu\n", Written, GetLastError());
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100, "RAW_IO read of 512: %lu %lu\n", Error, Read);
    SetPolicyBool(Device->Interface, PIPE_LOOP_OUT, RAW_IO, FALSE);
    SetPolicyBool(Device->Interface, PIPE_LOOP_IN, RAW_IO, FALSE);
}

static VOID TestInterrupt(PTEST_DEVICE Device)
{
    ULONG Read, Value;
    ULONGLONG Start, Elapsed;
    DWORD Error;
    BOOL Result;

    CleanState(Device);
    ok(VendorOut(Device->Interface, REQ_NOTIFY, 0x1234, 0x5678, NULL, 0), "NOTIFY failed: %lu\n", GetLastError());
    Value = 0;
    Error = ReadWait(Device, Device->Interface, PIPE_INTR_IN, &Value, 16, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 4 && Value == 0x56781234, "Interrupt read: %lu %lu %08lx\n", Error, Read,
       Value);
    ok(VendorOut(Device->Interface, REQ_NOTIFY, 0xAABB, 0xCCDD, NULL, 0), "NOTIFY failed: %lu\n", GetLastError());
    Value = 0;
    Error = ReadWait(Device, Device->Interface, PIPE_INTR_IN, &Value, 2, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 2 && Value == 0xAABB, "Interrupt read of 2: %lu %lu %08lx\n", Error, Read,
       Value);
    Value = 0;
    Error = ReadWait(Device, Device->Interface, PIPE_INTR_IN, &Value, 16, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 2 && Value == 0xCCDD, "Interrupt read of the saved 2: %lu %lu %08lx\n",
       Error, Read, Value);

    ok(SetPolicyUlong(Device->Interface, PIPE_INTR_IN, PIPE_TRANSFER_TIMEOUT, 200), "Timeout policy failed\n");
    Start = NowMs();
    SetLastError(0xdeadbeef);
    Read = 0xdeadbeef;
    Result = WinUsb_ReadPipe(Device->Interface, PIPE_INTR_IN, (PUCHAR)&Value, 16, &Read, NULL);
    Error = GetLastError();
    Elapsed = NowMs() - Start;
    ok(!Result && Error == ERROR_SEM_TIMEOUT && Read == 0, "Interrupt read timeout: %d %lu %lu\n", Result, Error, Read);
    ok(Elapsed >= 150 && Elapsed < 1000, "Interrupt timeout after %I64u ms\n", Elapsed);
    SetPolicyUlong(Device->Interface, PIPE_INTR_IN, PIPE_TRANSFER_TIMEOUT, 0);
    ok(VendorOut(Device->Interface, REQ_NOTIFY, 7, 0, NULL, 0), "NOTIFY failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_INTR_IN, &Value, 16, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 4 && Value == 7, "Interrupt read after the timeout: %lu %lu %08lx\n", Error,
       Read, Value);
}

static VOID TestControl(PTEST_DEVICE Device)
{
    WINUSB_SETUP_PACKET Setup;
    ULONG Transferred, Index;
    ULONGLONG Start, Elapsed;
    OVERLAPPED Overlapped;
    UCHAR Data[8];
    DWORD Error;
    BOOL Result;

    memset(In, 0, 4097);
    ok(VendorIn(Device->Interface, REQ_ECHO, 0x40, 0, In, 16, &Transferred) && Transferred == 16,
       "ECHO 16: %lu %lu\n", Transferred, GetLastError());
    for (Index = 0; Index < 16 && In[Index] == (UCHAR)(0x40 + Index); Index++)
        ;
    ok(Index == 16, "ECHO data differs at %lu\n", Index);
    ok(VendorIn(Device->Interface, REQ_ECHO, 0, 0, In, 4096, &Transferred) && Transferred == 4096, "ECHO 4096: %lu %lu\n",
       Transferred, GetLastError());
    Setup.RequestType = 0xC0;
    Setup.Request = REQ_ECHO;
    Setup.Value = 0;
    Setup.Index = 0;
    Setup.Length = 4097;
    SetLastError(0xdeadbeef);
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, In, 4097, &Transferred, NULL);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "ECHO 4097: %d %lu %lu\n", Result, Error, Transferred);
    Setup.Length = 16;
    SetLastError(0xdeadbeef);
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, In, 8, &Transferred, NULL);
    Error = GetLastError();
    ok(Result && Transferred == 8, "ECHO setup 16 buffer 8: %d %lu %lu\n", Result, Error, Transferred);

    FillPattern(Out, 1000, 31);
    ok(VendorOut(Device->Interface, REQ_STORE, 0, 0, Out, 1000), "STORE failed: %lu\n", GetLastError());
    memset(In, 0, 2000);
    ok(VendorIn(Device->Interface, REQ_LOAD, 0, 0, In, 2000, &Transferred) && Transferred == 1000,
       "LOAD: %lu %lu\n", Transferred, GetLastError());
    ok(CheckPattern(In, 1000, 31), "LOAD data differs\n");

    SetLastError(0xdeadbeef);
    Result = VendorIn(Device->Interface, REQ_STALL, 0, 0, In, 8, &Transferred);
    Error = GetLastError();
    ok(!Result && Error == ERROR_GEN_FAILURE, "Stalled request: %d %lu %lu\n", Result, Error, Transferred);
    SetLastError(0xdeadbeef);
    Result = VendorOut(Device->Interface, REQ_STALL, 0, 0, NULL, 0);
    Error = GetLastError();
    ok(!Result && Error == ERROR_GEN_FAILURE, "Stalled OUT request: %d %lu\n", Result, Error);
    ok(VendorIn(Device->Interface, REQ_ECHO, 1, 0, In, 4, &Transferred) && Transferred == 4,
       "Request after a stall: %lu %lu\n", Transferred, GetLastError());

    Setup.RequestType = 0xC1;
    Setup.Request = REQ_IFACE;
    Setup.Value = 0x1234;
    Setup.Index = 0x0105;
    Setup.Length = 4;
    memset(Data, 0xCC, sizeof(Data));
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, Data, 4, &Transferred, NULL);
    ok(Result && Transferred == 4, "Interface request: %d %lu %lu\n", Result, Transferred, GetLastError());
    ok(Data[0] == 0x00 && Data[1] == 0x01 && Data[2] == 0x34 && Data[3] == 0x12,
       "Interface request reached the device as index %02x%02x value %02x%02x\n", Data[1], Data[0], Data[3], Data[2]);
    Setup.RequestType = 0xC2;
    Setup.Request = REQ_ENDPOINT;
    Setup.Index = 0x0081;
    memset(Data, 0xCC, sizeof(Data));
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, Data, 4, &Transferred, NULL);
    ok(Result && Transferred == 4 && Data[0] == 0x81 && Data[1] == 0x00, "Endpoint request: %d %lu index %02x%02x\n",
       Result, Transferred, Data[1], Data[0]);

    Setup.RequestType = 0x82;
    Setup.Request = 0x00;
    Setup.Value = 0;
    Setup.Index = 0x81;
    Setup.Length = 2;
    memset(Data, 0xCC, sizeof(Data));
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, Data, 2, &Transferred, NULL);
    ok(Result && Transferred == 2 && Data[0] == 0 && Data[1] == 0, "GET_STATUS(81): %d %lu %02x %02x\n", Result,
       Transferred, Data[0], Data[1]);
    Setup.RequestType = 0x00;
    Setup.Request = 0x09;
    Setup.Value = 1;
    Setup.Index = 0;
    Setup.Length = 0;
    SetLastError(0xdeadbeef);
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, NULL, 0, NULL, NULL);
    Error = GetLastError();
    ok(Result, "SET_CONFIGURATION: %d %lu\n", Result, Error);
    Setup.RequestType = 0x01;
    Setup.Request = 0x0B;
    Setup.Value = 0;
    Setup.Index = 0;
    SetLastError(0xdeadbeef);
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, NULL, 0, NULL, NULL);
    Error = GetLastError();
    ok(Result, "SET_INTERFACE: %d %lu\n", Result, Error);

    Setup.RequestType = 0xC0;
    Setup.Request = REQ_ECHO;
    Setup.Value = 0x10;
    Setup.Index = 0;
    Setup.Length = 64;
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    Result = WinUsb_ControlTransfer(Device->Interface, Setup, In, 64, NULL, &Overlapped);
    Error = GetLastError();
    ok(!Result && Error == ERROR_IO_PENDING, "Overlapped control transfer: %d %lu\n", Result, Error);
    ok(WaitForSingleObject(Overlapped.hEvent, 2000) == WAIT_OBJECT_0, "Overlapped control transfer did not finish\n");
    Transferred = 0;
    ok(WinUsb_GetOverlappedResult(Device->Interface, &Overlapped, &Transferred, FALSE) && Transferred == 64,
       "Overlapped control result %lu\n", Transferred);
    CloseHandle(Overlapped.hEvent);
    SetLastError(0xdeadbeef);
    Result = WinUsb_ControlTransfer(NULL, Setup, In, 64, &Transferred, NULL);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "ControlTransfer(NULL): %d %lu\n", Result, Error);

    Start = NowMs();
    for (Index = 0; Index < 50; Index++)
    {
        if (!VendorIn(Device->Interface, REQ_ECHO, (USHORT)Index, 0, In, 16, &Transferred) || Transferred != 16)
            break;
    }
    Elapsed = NowMs() - Start;
    ok(Index == 50, "ECHO loop stopped at %lu: %lu\n", Index, GetLastError());
    ok(Elapsed < 1000, "50 synchronous control transfers took %I64u ms\n", Elapsed);
}

static VOID TestStall(PTEST_DEVICE Device)
{
    DEVICE_STATUS Before, After;
    ULONG Read, Written;
    DWORD Error;
    BOOL Result;

    CleanState(Device);
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(VendorOut(Device->Interface, REQ_HALT, 1, PIPE_LOOP_IN, NULL, 0), "HALT failed\n");
    FillPattern(Out, 100, 41);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, NULL), "Write of 100 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_GEN_FAILURE, "Read from a halted pipe: %lu %lu\n", Error, Read);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.HaltMask == (1u << 17) && After.ClearHalts == Before.ClearHalts,
       "After the stall: halt mask %08lx clear halts %lu\n", After.HaltMask, After.ClearHalts - Before.ClearHalts);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == TEST_TIMEOUT_ERROR, "Second read from a halted pipe: %lu %lu\n", Error, Read);
    ok(WinUsb_ResetPipe(Device->Interface, PIPE_LOOP_IN), "ResetPipe failed: %lu\n", GetLastError());
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.HaltMask == 0 && After.ClearHalts == Before.ClearHalts + 1,
       "After ResetPipe: halt mask %08lx clear halts %lu\n", After.HaltMask, After.ClearHalts - Before.ClearHalts);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100 && CheckPattern(In, 100, 41), "Read after ResetPipe: %lu %lu\n", Error,
       Read);

    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_IN, AUTO_CLEAR_STALL, TRUE), "AUTO_CLEAR_STALL failed\n");
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(VendorOut(Device->Interface, REQ_HALT, 1, PIPE_LOOP_IN, NULL, 0), "HALT failed\n");
    FillPattern(Out, 100, 42);
    ok(WinUsb_WritePipe(Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, NULL), "Write of 100 failed\n");
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_GEN_FAILURE, "Read from a halted pipe with AUTO_CLEAR_STALL: %lu %lu\n", Error, Read);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.HaltMask == 0 && After.ClearHalts == Before.ClearHalts + 1,
       "After an automatic clear: halt mask %08lx clear halts %lu\n", After.HaltMask,
       After.ClearHalts - Before.ClearHalts);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_IN, In, 512, &Read, 2000);
    ok(Error == ERROR_SUCCESS && Read == 100 && CheckPattern(In, 100, 42), "Read after the automatic clear: %lu %lu\n",
       Error, Read);
    SetPolicyBool(Device->Interface, PIPE_LOOP_IN, AUTO_CLEAR_STALL, FALSE);

    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(WinUsb_ResetPipe(Device->Interface, PIPE_LOOP_IN), "ResetPipe of a running pipe failed\n");
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.ClearHalts == Before.ClearHalts + 1, "ResetPipe of a running pipe sent %lu CLEAR_FEATURE\n",
       After.ClearHalts - Before.ClearHalts);

    ok(VendorOut(Device->Interface, REQ_HALT, 1, PIPE_LOOP_OUT, NULL, 0), "HALT OUT failed\n");
    Error = WriteWait(Device, Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, 2000);
    ok(Error == ERROR_GEN_FAILURE, "Write to a halted pipe: %lu %lu\n", Error, Written);
    Error = WriteWait(Device, Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, 1000);
    ok(Error == TEST_TIMEOUT_ERROR, "Second write to a halted pipe: %lu %lu\n", Error, Written);
    ok(WinUsb_ResetPipe(Device->Interface, PIPE_LOOP_OUT), "ResetPipe OUT failed: %lu\n", GetLastError());
    Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 100, 44);
    ok(SetPolicyBool(Device->Interface, PIPE_LOOP_OUT, AUTO_CLEAR_STALL, TRUE), "AUTO_CLEAR_STALL on OUT: %lu\n",
       GetLastError());
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(VendorOut(Device->Interface, REQ_HALT, 1, PIPE_LOOP_OUT, NULL, 0), "HALT OUT failed\n");
    Error = WriteWait(Device, Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, 2000);
    ok(Error == ERROR_GEN_FAILURE, "Write to a halted pipe with AUTO_CLEAR_STALL: %lu %lu\n", Error, Written);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.ClearHalts == Before.ClearHalts && After.HaltMask == (1u << 1),
       "AUTO_CLEAR_STALL on OUT: clear halts %lu, halt mask %08lx\n", After.ClearHalts - Before.ClearHalts,
       After.HaltMask);
    Error = WriteWait(Device, Device->Interface, PIPE_LOOP_OUT, Out, 100, &Written, 1000);
    ok(Error == TEST_TIMEOUT_ERROR, "Second write with AUTO_CLEAR_STALL on OUT: %lu %lu\n", Error, Written);
    SetPolicyBool(Device->Interface, PIPE_LOOP_OUT, AUTO_CLEAR_STALL, FALSE);
    ok(WinUsb_ResetPipe(Device->Interface, PIPE_LOOP_OUT), "ResetPipe OUT failed: %lu\n", GetLastError());
    CleanState(Device);
    Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 100, 43);

    SetLastError(0xdeadbeef);
    Result = WinUsb_ResetPipe(Device->Interface, 0x8F);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "ResetPipe(8f): %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = WinUsb_ResetPipe(Device->Interface, 0);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "ResetPipe(0): %d %lu\n", Result, Error);
    SetLastError(0xdeadbeef);
    Result = WinUsb_ResetPipe(NULL, PIPE_LOOP_IN);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "ResetPipe(NULL): %d %lu\n", Result, Error);
    ok(WinUsb_AbortPipe(Device->Interface, PIPE_LOOP_IN), "AbortPipe of an idle pipe failed: %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Result = WinUsb_AbortPipe(Device->Interface, 0x8F);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "AbortPipe(8f): %d %lu\n", Result, Error);

    Error = WriteWait(Device, Device->Interface, PIPE_LOOP_IN, Out, 10, &Written, 1000);
    ok(Error == ERROR_INVALID_PARAMETER, "Write to an IN pipe: %lu\n", Error);
    Error = ReadWait(Device, Device->Interface, PIPE_LOOP_OUT, In, 10, &Read, 1000);
    ok(Error == ERROR_INVALID_PARAMETER, "Read from an OUT pipe: %lu\n", Error);
    Error = ReadWait(Device, Device->Interface, 0x8F, In, 10, &Read, 1000);
    ok(Error == ERROR_INVALID_PARAMETER, "Read from pipe 8f: %lu\n", Error);
    SetLastError(0xdeadbeef);
    Result = WinUsb_ReadPipe(NULL, PIPE_LOOP_IN, In, 10, &Read, NULL);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_HANDLE, "ReadPipe(NULL): %d %lu\n", Result, Error);
}

static VOID TestSettings(PTEST_DEVICE Device)
{
    WINUSB_INTERFACE_HANDLE Associated = NULL;
    DEVICE_STATUS Before, After;
    WINUSB_SETUP_PACKET Setup;
    OVERLAPPED Overlapped;
    ULONG Read, Transferred;
    UCHAR Alt, Data[4];
    DWORD Error;
    BOOL Result;

    CleanState(Device);
    ok(GetDeviceStatus(Device->Interface, &Before), "No device status\n");
    ok(WinUsb_SetCurrentAlternateSetting(Device->Interface, 1), "SetCurrentAlternateSetting(1) failed: %lu\n",
       GetLastError());
    Alt = 0xCC;
    ok(WinUsb_GetCurrentAlternateSetting(Device->Interface, &Alt) && Alt == 1, "Current setting %u\n", Alt);
    ok(GetDeviceStatus(Device->Interface, &After), "No device status\n");
    ok(After.Alt0 == 1 && After.SetInterfaces == Before.SetInterfaces + 1, "Device setting %lu, SET_INTERFACE %lu\n",
       After.Alt0, After.SetInterfaces - Before.SetInterfaces);
    Loopback(Device, Device->Interface, PIPE_LOOP_OUT, PIPE_LOOP_IN, 777, 51);
    Error = ReadWait(Device, Device->Interface, PIPE_HELD_IN, In, 512, &Read, 1000);
    ok(Error == ERROR_INVALID_PARAMETER, "Read from a pipe of setting 0: %lu\n", Error);
    SetLastError(0xdeadbeef);
    Result = WinUsb_SetCurrentAlternateSetting(Device->Interface, 2);
    Error = GetLastError();
    ok(!Result && Error == ERROR_NOT_FOUND, "SetCurrentAlternateSetting(2): %d %lu\n", Result, Error);
    Alt = 0xCC;
    ok(WinUsb_GetCurrentAlternateSetting(Device->Interface, &Alt) && Alt == 1, "Setting after a failure %u\n", Alt);
    ok(WinUsb_SetCurrentAlternateSetting(Device->Interface, 0), "SetCurrentAlternateSetting(0) failed: %lu\n",
       GetLastError());

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Result = WinUsb_ReadPipe(Device->Interface, PIPE_HELD_IN, In, 512, NULL, &Overlapped);
    ok(!Result && GetLastError() == ERROR_IO_PENDING, "Held read: %d %lu\n", Result, GetLastError());
    SetLastError(0xdeadbeef);
    Result = WinUsb_SetCurrentAlternateSetting(Device->Interface, 1);
    Error = GetLastError();
    ok(!Result && Error == ERROR_BUSY, "SetCurrentAlternateSetting with a pending read: %d %lu\n", Result, Error);
    ok(WaitForSingleObject(Overlapped.hEvent, 0) == WAIT_TIMEOUT, "Pending read finished\n");
    if (Result)
        WinUsb_SetCurrentAlternateSetting(Device->Interface, 0);
    CancelIoEx(Device->File, &Overlapped);
    WaitForSingleObject(Overlapped.hEvent, 2000);
    Result = WinUsb_GetOverlappedResult(Device->Interface, &Overlapped, &Read, FALSE);
    Error = GetLastError();
    ok(!Result && Error == ERROR_OPERATION_ABORTED, "Cancelled held read: %d %lu\n", Result, Error);
    CloseHandle(Overlapped.hEvent);

    ok(WinUsb_GetAssociatedInterface(Device->Interface, 0, &Associated), "No associated interface: %lu\n",
       GetLastError());
    if (Associated)
    {
        Loopback(Device, Associated, PIPE_LOOP2_OUT, PIPE_LOOP2_IN, 4000, 52);
        Error = ReadWait(Device, Device->Interface, PIPE_LOOP2_IN, In, 512, &Read, 1000);
        ok(Error == ERROR_INVALID_PARAMETER, "Read of interface 1's pipe through interface 0: %lu\n", Error);
        Setup.RequestType = 0xC1;
        Setup.Request = REQ_IFACE;
        Setup.Value = 0;
        Setup.Index = 0x0000;
        Setup.Length = 4;
        memset(Data, 0xCC, sizeof(Data));
        Result = WinUsb_ControlTransfer(Associated, Setup, Data, 4, &Transferred, NULL);
        ok(Result && Data[0] == 1 && Data[1] == 0, "Interface request through interface 1: %d index %02x%02x\n",
           Result, Data[1], Data[0]);
        Alt = 0xCC;
        ok(WinUsb_GetCurrentAlternateSetting(Associated, &Alt) && Alt == 0, "Associated setting %u\n", Alt);
        SetLastError(0xdeadbeef);
        Result = WinUsb_SetCurrentAlternateSetting(Associated, 1);
        Error = GetLastError();
        ok(!Result && Error == ERROR_NOT_FOUND, "Associated SetCurrentAlternateSetting(1): %d %lu\n", Result, Error);
        WinUsb_Free(Associated);
    }
}

static VOID RunSection(VOID (*Section)(PTEST_DEVICE))
{
    TEST_DEVICE Device;

    if (!OpenTestDevice(&GUID_WINUSB_TEST, &Device))
    {
        skip("No usb-winusb device: %lu\n", GetLastError());
        return;
    }
    Out = HeapAlloc(GetProcessHeap(), 0, BUFFER_SIZE + 16);
    In = HeapAlloc(GetProcessHeap(), 0, BUFFER_SIZE + 16);
    ok(ResetDevice(Device.Interface), "Device reset failed: %lu\n", GetLastError());
    Section(&Device);
    HeapFree(GetProcessHeap(), 0, Out);
    HeapFree(GetProcessHeap(), 0, In);
    CloseTestDevice(&Device);
}

START_TEST(Bulk)
{
    RunSection(TestBulk);
}

START_TEST(Sizes)
{
    RunSection(TestSizes);
}

START_TEST(ShortPackets)
{
    RunSection(TestShortPackets);
}

START_TEST(PartialReads)
{
    RunSection(TestPartialReads);
}

START_TEST(RawIo)
{
    RunSection(TestRawIo);
}

START_TEST(Interrupt)
{
    RunSection(TestInterrupt);
}

START_TEST(Control)
{
    RunSection(TestControl);
}

START_TEST(Stall)
{
    RunSection(TestStall);
}

START_TEST(Settings)
{
    RunSection(TestSettings);
}
