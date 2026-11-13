/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     An error log entry a driver writes reaches the System event log with its insertion strings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define KMT_ERROR_LOG_UNIQUE_VALUE 0x4B4D5445

/* The newest System record the kernel test wrote, recognized by the unique value in its packet */
static
PEVENTLOGRECORD
FindRecord(
    _In_ HANDLE Log,
    _Out_writes_bytes_(Size) PBYTE Buffer,
    _In_ DWORD Size)
{
    DWORD Read, Needed, Offset;
    PEVENTLOGRECORD Record;
    PIO_ERROR_LOG_PACKET Packet;

    while (ReadEventLogW(Log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, Buffer, Size, &Read, &Needed))
    {
        for (Offset = 0; Offset < Read; Offset += Record->Length)
        {
            Record = (PEVENTLOGRECORD)(Buffer + Offset);
            if (Record->EventID != (DWORD)STATUS_DEVICE_NOT_READY ||
                Record->DataLength < FIELD_OFFSET(IO_ERROR_LOG_PACKET, DumpData))
            {
                continue;
            }
            Packet = (PIO_ERROR_LOG_PACKET)((PBYTE)Record + Record->DataOffset);
            if (Packet->UniqueErrorValue == KMT_ERROR_LOG_UNIQUE_VALUE)
                return Record;
        }
    }
    return NULL;
}

START_TEST(IoErrorLogRecord)
{
    static BYTE Buffer[0x10000];
    PEVENTLOGRECORD Record = NULL;
    PCWSTR String, Source;
    DWORD Newest = 0, Oldest = 0, Count = 0, Try;
    HANDLE Log;

    Log = OpenEventLogW(NULL, L"System");
    ok(Log != NULL, "OpenEventLogW(System) failed: %lu\n", GetLastError());
    if (Log == NULL)
        return;
    if (GetOldestEventLogRecord(Log, &Oldest) && GetNumberOfEventLogRecords(Log, &Count))
        Newest = Oldest + Count;
    CloseEventLog(Log);

    KmtRunKernelTest("IoErrorLogEntry");

    /* The I/O manager hands the entry to the event log from a work item */
    for (Try = 0; Try < 40 && Record == NULL; Try++)
    {
        Sleep(25);
        Log = OpenEventLogW(NULL, L"System");
        if (Log == NULL)
            continue;
        Record = FindRecord(Log, Buffer, sizeof(Buffer));
        if (Record != NULL && Record->RecordNumber < Newest)
            Record = NULL;
        CloseEventLog(Log);
    }

    ok(Record != NULL, "the error log entry did not reach the System event log\n");
    if (Record == NULL)
        return;

    Source = (PCWSTR)(Record + 1);
    ok(wcslen(Source) >= wcslen(L"Kmtest") && wcscmp(Source + wcslen(Source) - wcslen(L"Kmtest"), L"Kmtest") == 0,
       "source %ls does not name the driver\n", Source);
    ok_eq_uint(Record->NumStrings, 2);
    if (Record->NumStrings != 2)
        return;
    String = (PCWSTR)((PBYTE)Record + Record->StringOffset);
    ok(wcscmp(String, L"\\Device\\KmtestErrorLog") == 0, "first string is %ls\n", String);
    String += wcslen(String) + 1;
    ok(wcscmp(String, L"kmtest insertion string") == 0, "second string is %ls\n", String);
}
