/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Test for ReadFile
 * COPYRIGHT:   Copyright 2026 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include "precomp.h"

static void Test_ReadFile_sync(HANDLE hFile)
{
    UCHAR Buffer[128];
    DWORD dwBytesRead;
    BOOL bResult;

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    /* Test NULL File */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    bResult = ReadFile(NULL, Buffer, sizeof(Buffer), &dwBytesRead, NULL);
    ok_eq_bool(bResult, FALSE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_HANDLE);

    /* Test INVALID_HANDLE_VALUE */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    bResult = ReadFile(INVALID_HANDLE_VALUE, Buffer, sizeof(Buffer), &dwBytesRead, NULL);
    ok_eq_bool(bResult, FALSE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_HANDLE);

    /* Read some bytes */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    bResult = ReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead, NULL);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(dwBytesRead, sizeof(Buffer));
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    /* Try to read at end of file */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    ok(SetFilePointer(hFile, 0, NULL, FILE_END) > 0, "SetFilePointer failed\n");
    bResult = ReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead, NULL);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    /* Try to read 0 bytes at end of file */
    SetLastError(0xdeadbeef);
    bResult = ReadFile(hFile, Buffer, 0, &dwBytesRead, NULL);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    /* Test with NULL lpBuffer */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    StartSeh()
        bResult = ReadFile(hFile, NULL, sizeof(Buffer), &dwBytesRead, NULL);
        ok_eq_bool(bResult, FALSE);
        ok_eq_ulong(dwBytesRead, 0);
        ok_eq_ulong(GetLastError(), ERROR_NOACCESS);
    EndSeh(STATUS_SUCCESS);

    /* Test with NULL lpBuffer and 0 nNumberOfBytesToRead */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    StartSeh()
        bResult = ReadFile(hFile, NULL, 0, &dwBytesRead, NULL);
        ok_eq_bool(bResult, TRUE);
        ok_eq_ulong(dwBytesRead, 0);
        ok_eq_ulong(GetLastError(), 0xdeadbeef);
    EndSeh(STATUS_SUCCESS);

    /* Test with NULL lpNumberOfBytesRead */
    SetLastError(0xdeadbeef);
    StartSeh()
        bResult = ReadFile(hFile, Buffer, sizeof(Buffer), NULL, NULL);
        ok_eq_bool(bResult, TRUE);
        ok_eq_ulong(GetLastError(), 0xdeadbeef);
    EndSeh(is_reactos() || (GetNTVersion() >= _WIN32_WINNT_WIN8) ? STATUS_SUCCESS : EXCEPTION_ACCESS_VIOLATION);

    /* Test with NULL lpNumberOfBytesRead at end of file */
    ok(SetFilePointer(hFile, 0, NULL, FILE_END) > 0, "SetFilePointer failed\n");
    SetLastError(0xdeadbeef);
    StartSeh()
        bResult = ReadFile(hFile, Buffer, sizeof(Buffer), NULL, NULL);
        ok_eq_bool(bResult, TRUE);
        ok_eq_ulong(GetLastError(), 0xdeadbeef);
    EndSeh(is_reactos() || (GetNTVersion() >= _WIN32_WINNT_WIN8) ? STATUS_SUCCESS : EXCEPTION_ACCESS_VIOLATION);
}

static void Test_ReadFile_async(HANDLE hFile)
{
    BOOL bResult;
    UCHAR Buffer[128];
    DWORD dwBytesRead;
    OVERLAPPED ol = { 0 };

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    /* Test NULL File */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    bResult = ReadFile(NULL, Buffer, sizeof(Buffer), &dwBytesRead, &ol);
    ok_eq_bool(bResult, FALSE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_HANDLE);

    /* Test INVALID_HANDLE_VALUE */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    bResult = ReadFile(INVALID_HANDLE_VALUE, Buffer, sizeof(Buffer), &dwBytesRead, &ol);
    ok_eq_bool(bResult, FALSE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_HANDLE);

    /* Read some bytes */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    bResult = ReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead, &ol);
    ok_eq_bool(bResult, TRUE);
    dwBytesRead = 0xdeadbeef;
    bResult = GetOverlappedResult(hFile, &ol, &dwBytesRead, TRUE);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(dwBytesRead, sizeof(Buffer));
    ok_eq_ulong(GetLastError(), (GetNTVersion() >= _WIN32_WINNT_WIN10) ? 0 : 0xdeadbeef);

    /* Try to read at end of file */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    ol.Offset = GetFileSize(hFile, NULL);
    ok(ol.Offset > 0, "GetFileSize failed\n");
    bResult = ReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead, &ol);
    ok_eq_bool(bResult, FALSE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), ERROR_HANDLE_EOF);

    /* Try to read 0 bytes at end of file */
    SetLastError(0xdeadbeef);
    bResult = ReadFile(hFile, Buffer, 0, &dwBytesRead, &ol);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);
    ol.Offset = 0;

    /* Test with NULL lpBuffer */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    StartSeh()
        bResult = ReadFile(hFile, NULL, sizeof(Buffer), &dwBytesRead, &ol);
        ok_eq_bool(bResult, FALSE);
        ok_eq_ulong(GetLastError(), ERROR_NOACCESS);
    EndSeh(STATUS_SUCCESS);

    /* Test with NULL lpBuffer and 0 nNumberOfBytesToRead */
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    StartSeh()
        bResult = ReadFile(hFile, NULL, 0, &dwBytesRead, &ol);
        ok_eq_bool(bResult, TRUE);
        ok_eq_ulong(GetLastError(), 0xdeadbeef);
        bResult = GetOverlappedResult(hFile, &ol, &dwBytesRead, TRUE);
        ok_eq_bool(bResult, TRUE);
        ok_eq_ulong(dwBytesRead, 0);
        ok_eq_ulong(GetLastError(), (GetNTVersion() >= _WIN32_WINNT_WIN10) ? 0 : 0xdeadbeef);
    EndSeh(STATUS_SUCCESS);

    /* Test with NULL lpNumberOfBytesRead */
    SetLastError(0xdeadbeef);
    bResult = ReadFile(hFile, Buffer, sizeof(Buffer), NULL, &ol);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);
    bResult = GetOverlappedResult(hFile, &ol, &dwBytesRead, TRUE);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(dwBytesRead, sizeof(Buffer));
    ok_eq_ulong(GetLastError(), (GetNTVersion() >= _WIN32_WINNT_WIN10) ? 0 : 0xdeadbeef);

    /* Test with NULL lpNumberOfBytesRead at end of file */
    SetLastError(0xdeadbeef);
    ol.Offset = GetFileSize(hFile, NULL);
    ok(ol.Offset > 0, "GetFileSize failed\n");
    bResult = ReadFile(hFile, Buffer, sizeof(Buffer), NULL, &ol);
    ok_eq_bool(bResult, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_HANDLE_EOF);
    SetLastError(0xdeadbeef);
    dwBytesRead = 0xdeadbeef;
    bResult = GetOverlappedResult(hFile, &ol, &dwBytesRead, TRUE);
    ok_eq_bool(bResult, FALSE);
    ok_eq_ulong(dwBytesRead, 0);
    ok_eq_ulong(GetLastError(), ERROR_HANDLE_EOF);
    ol.Offset = 0;
}

static ULONG ReadFileExCalls;
static DWORD ReadFileExError;
static DWORD ReadFileExBytes;
static LPOVERLAPPED ReadFileExOverlapped;

static VOID WINAPI ReadFileExCompletion(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
    ReadFileExCalls++;
    ReadFileExError = dwErrorCode;
    ReadFileExBytes = dwNumberOfBytesTransfered;
    ReadFileExOverlapped = lpOverlapped;
}

static void Test_ReadFileEx_pending(void)
{
    static const CHAR Data[] = "pending";
    WCHAR PipeName[64];
    UCHAR Buffer[16] = { 0 };
    OVERLAPPED ol = { 0 };
    HANDLE hServer, hClient;
    DWORD dwWritten, dwWait;
    BOOL bResult;

    StringCchPrintfW(PipeName, ARRAYSIZE(PipeName), L"\\\\.\\pipe\\ReadFileEx_%lu", GetCurrentProcessId());
    hServer = CreateNamedPipeW(PipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT,
                               1, 0, sizeof(Buffer), 0, NULL);
    ok(hServer != INVALID_HANDLE_VALUE, "CreateNamedPipeW failed: %lu\n", GetLastError());
    if (hServer == INVALID_HANDLE_VALUE)
        return;
    hClient = CreateFileW(PipeName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(hClient != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (hClient == INVALID_HANDLE_VALUE)
    {
        CloseHandle(hServer);
        return;
    }

    bResult = ReadFileEx(hServer, Buffer, sizeof(Buffer), &ol, ReadFileExCompletion);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(ReadFileExCalls, 0);

    bResult = WriteFile(hClient, Data, sizeof(Data) - 1, &dwWritten, NULL);
    ok_eq_bool(bResult, TRUE);
    ok_eq_ulong(dwWritten, sizeof(Data) - 1);

    dwWait = SleepEx(10000, TRUE);
    ok_eq_ulong(dwWait, WAIT_IO_COMPLETION);
    ok_eq_ulong(ReadFileExCalls, 1);
    ok_eq_ulong(ReadFileExError, ERROR_SUCCESS);
    ok_eq_ulong(ReadFileExBytes, sizeof(Data) - 1);
    ok_eq_pointer(ReadFileExOverlapped, &ol);
    ok(memcmp(Buffer, Data, sizeof(Data) - 1) == 0, "Unexpected data '%.16s'\n", (PCSTR)Buffer);

    CloseHandle(hClient);
    CloseHandle(hServer);
}

START_TEST(ReadFile)
{
    CHAR FileName[MAX_PATH];
    HANDLE hFile;

    /* Open the executable file */
    GetModuleFileNameA(NULL, FileName, ARRAYSIZE(FileName));
    hFile = CreateFileA(FileName,
                        FILE_READ_ACCESS,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        skip("CreateFileA failed with error %lu\n", GetLastError());
        return;
    }

    Test_ReadFile_sync(hFile);
    Test_ReadFile_async(hFile);

    CloseHandle(hFile);

    Test_ReadFileEx_pending();
}
