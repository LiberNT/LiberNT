/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Test for Read/Write operations
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>
#include "IoReadWrite.h"

static
VOID
TestRead(
    _In_ HANDLE FileHandle,
    _In_ BOOLEAN Cached,
    _In_ BOOLEAN UseFastIo,
    _In_ BOOLEAN ReturnPending)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    HANDLE EventHandle;
    UCHAR Buffer[32];
    LARGE_INTEGER Offset;
    ULONG BaseKey, StatusKey, Key;
    DWORD WaitStatus;
    BOOLEAN Win11GeOrLater;

    BaseKey = (UseFastIo ? KEY_USE_FASTIO : 0) |
              (ReturnPending ? KEY_RETURN_PENDING : 0);
    Win11GeOrLater = GetNTDDIVersion() >= NTDDI_WIN11_GE;

    EventHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(EventHandle != NULL, "CreateEvent failed with %lu\n", GetLastError());

    for (StatusKey = KEY_SUCCEED ; StatusKey != 0xff; StatusKey = KEY_NEXT(StatusKey))
    {
        //trace("\tSTATUS KEY: %lx\n", StatusKey);
        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        Key = BaseKey | StatusKey | KEY_DATA(0x11);
        Offset.QuadPart = 0;
        Status = NtReadFile(FileHandle,
                            EventHandle,
                            NULL,
                            NULL,
                            &IoStatus,
                            NULL,
                            0,
                            &Offset,
                            &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        if (ReturnPending && !Win11GeOrLater)
            ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        else
            ok_eq_hex(Status, STATUS_BUFFER_OVERFLOW);
        if (Win11GeOrLater || (Cached && UseFastIo && !ReturnPending))
        {
            ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
            ok_eq_hex(IoStatus.Status, STATUS_BUFFER_OVERFLOW);
            ok_eq_ulongptr(IoStatus.Information, TEST_FILE_SIZE);
        }
        else
        {
            ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
            ok_eq_hex(IoStatus.Status, 0x55555555);
            ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        }

        KmtStartSeh()
        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        Key = BaseKey | StatusKey | KEY_DATA(0x22);
        Offset.QuadPart = 0;
        Status = NtReadFile(FileHandle,
                            EventHandle,
                            NULL,
                            NULL,
                            &IoStatus,
                            NULL,
                            sizeof(Buffer),
                            &Offset,
                            &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        ok_eq_hex(IoStatus.Status, 0x55555555);
        ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        KmtEndSeh(STATUS_SUCCESS);

        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        Key = BaseKey | StatusKey | KEY_DATA(0x33);
        Offset.QuadPart = 0;
        Status = NtReadFile(FileHandle,
                            EventHandle,
                            NULL,
                            NULL,
                            &IoStatus,
                            Buffer,
                            0,
                            &Offset,
                            &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        if (ReturnPending && !Win11GeOrLater)
            ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        else
            ok_eq_hex(Status, STATUS_BUFFER_OVERFLOW);
        if (Win11GeOrLater || (Cached && UseFastIo && !ReturnPending))
        {
            ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
            ok_eq_hex(IoStatus.Status, STATUS_BUFFER_OVERFLOW);
            ok_eq_ulongptr(IoStatus.Information, TEST_FILE_SIZE);
        }
        else
        {
            ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
            ok_eq_hex(IoStatus.Status, 0x55555555);
            ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        }
        ok_eq_uint(Buffer[0], 0x55);

        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        Key = BaseKey | StatusKey | KEY_DATA(0x44);
        Offset.QuadPart = 0;
        Status = NtReadFile(FileHandle,
                            EventHandle,
                            NULL,
                            NULL,
                            &IoStatus,
                            Buffer,
                            sizeof(Buffer),
                            &Offset,
                            &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_hex(Status, TestGetReturnStatus(StatusKey));
        if ((Win11GeOrLater && ReturnPending) ||
            (Cached && UseFastIo && !ReturnPending &&
             (StatusKey == KEY_SUCCEED || StatusKey == KEY_FAIL_OVERFLOW || StatusKey == KEY_FAIL_EOF)) ||
            !KEY_ERROR(StatusKey))
        {
            ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
            ok_eq_hex(IoStatus.Status, TestGetReturnStatus(StatusKey));
            ok_eq_ulongptr(IoStatus.Information, TEST_FILE_SIZE);
        }
        else
        {
            ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
            ok_eq_hex(IoStatus.Status, 0x55555555);
            ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        }
        if ((StatusKey != KEY_FAIL_VERIFY_REQUIRED && !KEY_ERROR(StatusKey)) ||
            Cached)
        {
            ok_eq_uint(Buffer[0], 0x44);
            ok_eq_uint(Buffer[TEST_FILE_SIZE - 1], 0x44);
            ok_eq_uint(Buffer[TEST_FILE_SIZE], 0x55);
        }
        else
        {
            ok_eq_uint(Buffer[0], 0x55);
        }
    }
}

static
VOID
TestWrite(
    _In_ HANDLE FileHandle,
    _In_ BOOLEAN Cached,
    _In_ BOOLEAN UseFastIo,
    _In_ BOOLEAN ReturnPending)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    HANDLE EventHandle;
    UCHAR Buffer[32];
    LARGE_INTEGER Offset;
    ULONG BaseKey, StatusKey, Key;
    DWORD WaitStatus;
    BOOLEAN Win11GeOrLater;

    BaseKey = (UseFastIo ? KEY_USE_FASTIO : 0) |
              (ReturnPending ? KEY_RETURN_PENDING : 0);
    Win11GeOrLater = GetNTDDIVersion() >= NTDDI_WIN11_GE;

    EventHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(EventHandle != NULL, "CreateEvent failed with %lu\n", GetLastError());

    for (StatusKey = KEY_SUCCEED ; StatusKey != 0xff; StatusKey = KEY_NEXT(StatusKey))
    {
        //trace("\tSTATUS KEY: %lx\n", StatusKey);
        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        Key = BaseKey | StatusKey | KEY_DATA(0x11);
        Offset.QuadPart = 0;
        Status = NtWriteFile(FileHandle,
                             EventHandle,
                             NULL,
                             NULL,
                             &IoStatus,
                             NULL,
                             0,
                             &Offset,
                             &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_hex(Status, TestGetReturnStatus(StatusKey));
        if (!KEY_ERROR(StatusKey) || (Win11GeOrLater && ReturnPending))
        {
            ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
            ok_eq_hex(IoStatus.Status, TestGetReturnStatus(StatusKey));
            ok_eq_ulongptr(IoStatus.Information, 0);
        }
        else
        {
            ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
            ok_eq_hex(IoStatus.Status, 0x55555555);
            ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        }

        KmtStartSeh()
        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        Key = BaseKey | StatusKey | KEY_DATA(0x22);
        Offset.QuadPart = 0;
        Status = NtWriteFile(FileHandle,
                             EventHandle,
                             NULL,
                             NULL,
                             &IoStatus,
                             NULL,
                             sizeof(Buffer),
                             &Offset,
                             &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        ok_eq_hex(IoStatus.Status, 0x55555555);
        ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        KmtEndSeh(STATUS_SUCCESS);

        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        Key = BaseKey | StatusKey | KEY_DATA(0x33);
        Offset.QuadPart = 0;
        Status = NtWriteFile(FileHandle,
                             EventHandle,
                             NULL,
                             NULL,
                             &IoStatus,
                             Buffer,
                             0,
                             &Offset,
                             &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_hex(Status, TestGetReturnStatus(StatusKey));
        if (!KEY_ERROR(StatusKey) || (Win11GeOrLater && ReturnPending))
        {
            ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
            ok_eq_hex(IoStatus.Status, TestGetReturnStatus(StatusKey));
            ok_eq_ulongptr(IoStatus.Information, 0);
        }
        else
        {
            ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
            ok_eq_hex(IoStatus.Status, 0x55555555);
            ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        }

        ResetEvent(EventHandle);
        RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
        RtlFillMemory(Buffer, sizeof(Buffer), 0x44);
        Key = BaseKey | StatusKey | KEY_DATA(0x44);
        Offset.QuadPart = 0;
        Status = NtWriteFile(FileHandle,
                             EventHandle,
                             NULL,
                             NULL,
                             &IoStatus,
                             Buffer,
                             sizeof(Buffer),
                             &Offset,
                             &Key);
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_hex(Status, TestGetReturnStatus(StatusKey));
        if (!KEY_ERROR(StatusKey) || (Win11GeOrLater && ReturnPending))
        {
            ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
            ok_eq_hex(IoStatus.Status, TestGetReturnStatus(StatusKey));
            ok_eq_ulongptr(IoStatus.Information, sizeof(Buffer));
        }
        else
        {
            ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
            ok_eq_hex(IoStatus.Status, 0x55555555);
            ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0x5555555555555555);
        }
    }
}

static HANDLE HeldReadFile;
static HANDLE HeldReadReady;
static IO_STATUS_BLOCK HeldReadIoStatus;
static UCHAR HeldReadBuffer[TEST_FILE_SIZE];
static NTSTATUS HeldReadStatus;
static IO_STATUS_BLOCK HeldReadIoStatusAtReturn;
static UCHAR HeldReadByteAtReturn;
static LONG HeldReadApcs;
static LONG HeldReadApcsAtReturn;

static
VOID
NTAPI
HeldReadApc(
    _In_ ULONG_PTR Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    InterlockedIncrement(&HeldReadApcs);
}

static
DWORD
WINAPI
HeldReadThread(
    _In_ PVOID Context)
{
    LARGE_INTEGER Offset;
    ULONG Key = KEY_HOLD_PENDING | KEY_SUCCEED | KEY_DATA(0x22);

    UNREFERENCED_PARAMETER(Context);

    Offset.QuadPart = 0;
    SetEvent(HeldReadReady);
    HeldReadStatus = NtReadFile(HeldReadFile,
                                NULL,
                                NULL,
                                NULL,
                                &HeldReadIoStatus,
                                HeldReadBuffer,
                                sizeof(HeldReadBuffer),
                                &Offset,
                                &Key);
    HeldReadIoStatusAtReturn = HeldReadIoStatus;
    HeldReadByteAtReturn = HeldReadBuffer[TEST_FILE_SIZE - 1];
    HeldReadApcsAtReturn = HeldReadApcs;
    return 0;
}

/* A user APC interrupts an alertable synchronous read whose request the driver cannot cancel */
static
VOID
TestAlertedHeldRead(
    _In_ HANDLE FileHandle)
{
    HANDLE Thread;
    DWORD Result;

    HeldReadFile = FileHandle;
    HeldReadApcs = 0;
    HeldReadApcsAtReturn = 0;
    HeldReadByteAtReturn = 0x55;
    HeldReadStatus = 0x55555555;
    RtlFillMemory(&HeldReadIoStatus, sizeof(HeldReadIoStatus), 0x55);
    RtlFillMemory(&HeldReadIoStatusAtReturn, sizeof(HeldReadIoStatusAtReturn), 0x55);
    RtlFillMemory(HeldReadBuffer, sizeof(HeldReadBuffer), 0x55);
    HeldReadReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(HeldReadReady != NULL, "CreateEvent failed with %lu\n", GetLastError());
    if (!HeldReadReady)
        return;

    Thread = CreateThread(NULL, 0, HeldReadThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Thread)
    {
        CloseHandle(HeldReadReady);
        return;
    }

    ok_eq_ulong(WaitForSingleObject(HeldReadReady, 5000), WAIT_OBJECT_0);
    ok(QueueUserAPC(HeldReadApc, Thread, 0), "QueueUserAPC failed with %lu\n", GetLastError());
    Result = WaitForSingleObject(Thread, 5000);
    ok_eq_ulong(Result, WAIT_OBJECT_0);

    /* The service may only return once the driver completed the request */
    ok_eq_hex(HeldReadStatus, STATUS_SUCCESS);
    ok_eq_hex(HeldReadIoStatusAtReturn.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(HeldReadIoStatusAtReturn.Information, TEST_FILE_SIZE);
    ok_eq_hex(HeldReadByteAtReturn, 0x22);
    ok_eq_long(HeldReadApcsAtReturn, 1L);

    CloseHandle(Thread);
    CloseHandle(HeldReadReady);
}

START_TEST(IoReadWrite)
{
    HANDLE FileHandle;
    UNICODE_STRING CachedFileName = RTL_CONSTANT_STRING(L"\\Device\\Kmtest-IoReadWrite\\Cached");
    UNICODE_STRING NonCachedFileName = RTL_CONSTANT_STRING(L"\\Device\\Kmtest-IoReadWrite\\NonCached");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"IoReadWrite", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
    InitializeObjectAttributes(&ObjectAttributes,
                               &NonCachedFileName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatus,
                        0,
                        FILE_NON_DIRECTORY_FILE |
                        FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!skip(NT_SUCCESS(Status), "No file\n"))
    {
        ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
        ok_eq_ulongptr(IoStatus.Information, FILE_OPENED);
        trace("Non-Cached read, no FastIo, direct return\n");
        TestRead(FileHandle, FALSE, FALSE, FALSE);
        trace("Non-Cached read, allow FastIo, direct return\n");
        TestRead(FileHandle, FALSE, TRUE, FALSE);
        trace("Non-Cached read, no FastIo, pending return\n");
        TestRead(FileHandle, FALSE, FALSE, TRUE);
        trace("Non-Cached read, allow FastIo, pending return\n");
        TestRead(FileHandle, FALSE, TRUE, TRUE);

        trace("Non-Cached write, no FastIo, direct return\n");
        TestWrite(FileHandle, FALSE, FALSE, FALSE);
        trace("Non-Cached write, allow FastIo, direct return\n");
        TestWrite(FileHandle, FALSE, TRUE, FALSE);
        trace("Non-Cached write, no FastIo, pending return\n");
        TestWrite(FileHandle, FALSE, FALSE, TRUE);
        trace("Non-Cached write, allow FastIo, pending return\n");
        TestWrite(FileHandle, FALSE, TRUE, TRUE);
        NtClose(FileHandle);
    }

    RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
    InitializeObjectAttributes(&ObjectAttributes,
                               &NonCachedFileName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatus,
                        0,
                        FILE_NON_DIRECTORY_FILE |
                        FILE_SYNCHRONOUS_IO_ALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!skip(NT_SUCCESS(Status), "No file\n"))
    {
        trace("Non-Cached alertable read held by the driver\n");
        TestAlertedHeldRead(FileHandle);
        NtClose(FileHandle);
    }

    RtlFillMemory(&IoStatus, sizeof(IoStatus), 0x55);
    InitializeObjectAttributes(&ObjectAttributes,
                               &CachedFileName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatus,
                        0,
                        FILE_NON_DIRECTORY_FILE |
                        FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!skip(NT_SUCCESS(Status), "No file\n"))
    {
        ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
        ok_eq_ulongptr(IoStatus.Information, FILE_OPENED);
        trace("Cached read, no FastIo, direct return\n");
        TestRead(FileHandle, TRUE, FALSE, FALSE);
        trace("Cached read, allow FastIo, direct return\n");
        TestRead(FileHandle, TRUE, TRUE, FALSE);
        trace("Cached read, no FastIo, pending return\n");
        TestRead(FileHandle, TRUE, FALSE, TRUE);
        trace("Cached read, allow FastIo, pending return\n");
        TestRead(FileHandle, TRUE, TRUE, TRUE);

        trace("Cached write, no FastIo, direct return\n");
        TestWrite(FileHandle, TRUE, FALSE, FALSE);
        trace("Cached write, allow FastIo, direct return\n");
        TestWrite(FileHandle, TRUE, TRUE, FALSE);
        trace("Cached write, no FastIo, pending return\n");
        TestWrite(FileHandle, TRUE, FALSE, TRUE);
        trace("Cached write, allow FastIo, pending return\n");
        TestWrite(FileHandle, TRUE, TRUE, TRUE);
        NtClose(FileHandle);
    }

    KmtCloseDriver();
    KmtUnloadDriver();
}
