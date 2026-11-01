/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite error log entry allocation and write
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(IoErrorLogEntry)
{
    static const WCHAR First[] = L"\\Device\\KmtestErrorLog";
    static const WCHAR Second[] = L"kmtest insertion string";
    PIO_ERROR_LOG_PACKET Packet;
    UCHAR Size;

    if (skip(KmtDriverObject != NULL, "no kmtest driver object\n"))
        return;

    ok(IoAllocateErrorLogEntry(KmtDriverObject, ERROR_LOG_MAXIMUM_SIZE + 1) == NULL,
       "an entry above ERROR_LOG_MAXIMUM_SIZE was allocated\n");

    Size = (UCHAR)(FIELD_OFFSET(IO_ERROR_LOG_PACKET, DumpData) + sizeof(ULONG) + sizeof(First) + sizeof(Second));
    Packet = IoAllocateErrorLogEntry(KmtDriverObject, Size);
    ok(Packet != NULL, "IoAllocateErrorLogEntry(%u) failed\n", Size);
    if (Packet == NULL)
        return;

    ok_eq_uint(Packet->DumpDataSize, 0);
    ok_eq_uint(Packet->NumberOfStrings, 0);
    Packet->ErrorCode = STATUS_DEVICE_NOT_READY;
    Packet->FinalStatus = STATUS_UNSUCCESSFUL;
    Packet->UniqueErrorValue = 0x4B4D5445;
    Packet->DumpDataSize = sizeof(ULONG);
    Packet->DumpData[0] = 0x12345678;
    Packet->NumberOfStrings = 2;
    Packet->StringOffset = (USHORT)(FIELD_OFFSET(IO_ERROR_LOG_PACKET, DumpData) + sizeof(ULONG));
    RtlCopyMemory((PUCHAR)Packet + Packet->StringOffset, First, sizeof(First));
    RtlCopyMemory((PUCHAR)Packet + Packet->StringOffset + sizeof(First), Second, sizeof(Second));
    IoWriteErrorLogEntry(Packet);
}
