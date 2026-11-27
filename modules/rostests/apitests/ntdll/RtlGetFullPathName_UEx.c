/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for RtlGetFullPathName_UEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define UNTOUCHED_PART ((PWSTR)(ULONG_PTR)0x1111)

static
VOID
CheckFullPath(
    _In_ PCWSTR FileName,
    _In_ ULONG Size,
    _In_ NTSTATUS ExpectedStatus,
    _In_ ULONG ExpectedBytes,
    _In_opt_ PCWSTR ExpectedPath,
    _In_ LONG ExpectedPart)
{
    WCHAR Buffer[MAX_PATH];
    PWSTR Part = UNTOUCHED_PART;
    ULONG Bytes = 0xDEADBEEF;
    ULONG Length;
    NTSTATUS Status;

    ok(Size <= sizeof(Buffer), "Size %lu too big\n", Size);
    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Status = RtlGetFullPathName_UEx(FileName, Size, Buffer, &Part, &Bytes);
    ok(Status == ExpectedStatus, "'%S' %lu: status 0x%lx, expected 0x%lx\n", FileName, Size, Status, ExpectedStatus);
    ok(Bytes == ExpectedBytes, "'%S' %lu: %lu bytes, expected %lu\n", FileName, Size, Bytes, ExpectedBytes);
    if (ExpectedPart < 0)
        ok(Part == NULL, "'%S' %lu: file part %p, expected NULL\n", FileName, Size, Part);
    else
        ok(Part == Buffer + ExpectedPart, "'%S' %lu: file part %p, expected %p\n", FileName, Size, Part, Buffer + ExpectedPart);
    if (ExpectedPath)
        ok(wcscmp(Buffer, ExpectedPath) == 0, "'%S' %lu: '%S', expected '%S'\n", FileName, Size, Buffer, ExpectedPath);
    else if (Size == 0 || !NT_SUCCESS(ExpectedStatus))
        ok(Buffer[0] == 0x5555, "'%S' %lu: buffer written (0x%x)\n", FileName, Size, Buffer[0]);
    else
        ok(Buffer[0] == UNICODE_NULL, "'%S' %lu: buffer starts with 0x%x\n", FileName, Size, Buffer[0]);

    Length = RtlGetFullPathName_U(FileName, Size, Buffer, NULL);
    ok(Length == ExpectedBytes, "'%S' %lu: RtlGetFullPathName_U returned %lu, expected %lu\n", FileName, Size, Length, ExpectedBytes);
}

START_TEST(RtlGetFullPathName_UEx)
{
    WCHAR Buffer[MAX_PATH];
    PWSTR Part = UNTOUCHED_PART;
    ULONG Bytes = 0xDEADBEEF;
    NTSTATUS Status;

    CheckFullPath(L"C:\\dir\\file.txt", sizeof(Buffer), STATUS_SUCCESS, 30, L"C:\\dir\\file.txt", 7);
    CheckFullPath(L"C:\\dir\\file.txt", 32, STATUS_SUCCESS, 30, L"C:\\dir\\file.txt", 7);
    CheckFullPath(L"C:\\dir\\file.txt", 30, STATUS_SUCCESS, 32, NULL, -1);
    CheckFullPath(L"C:\\dir\\file.txt", 10, STATUS_SUCCESS, 32, NULL, -1);
    CheckFullPath(L"C:\\dir\\file.txt", 0, STATUS_SUCCESS, 32, NULL, -1);
    CheckFullPath(L"C:\\a\\b\\..\\c.txt", sizeof(Buffer), STATUS_SUCCESS, 20, L"C:\\a\\c.txt", 5);
    CheckFullPath(L"C:\\dir\\", sizeof(Buffer), STATUS_SUCCESS, 14, L"C:\\dir\\", -1);
    CheckFullPath(L"", sizeof(Buffer), STATUS_OBJECT_NAME_INVALID, 0, NULL, -1);
    CheckFullPath(L"CON", sizeof(Buffer), STATUS_SUCCESS, 14, L"\\\\.\\CON", -1);
    CheckFullPath(L"C:\\dir\\nul", sizeof(Buffer), STATUS_SUCCESS, 14, L"\\\\.\\nul", -1);
    CheckFullPath(L"CON", 8, STATUS_SUCCESS, 16, NULL, -1);
    CheckFullPath(L"\\\\server\\share\\x.txt", sizeof(Buffer), STATUS_SUCCESS, 40, L"\\\\server\\share\\x.txt", 15);
    CheckFullPath(L"\\\\?\\C:\\x.txt", sizeof(Buffer), STATUS_SUCCESS, 24, L"\\\\?\\C:\\x.txt", 7);

    Status = RtlGetFullPathName_UEx(L"C:\\dir\\file.txt", sizeof(Buffer), Buffer, NULL, &Bytes);
    ok(Status == STATUS_SUCCESS, "Status 0x%lx\n", Status);
    ok(Bytes == 30, "%lu bytes\n", Bytes);

    Status = RtlGetFullPathName_UEx(L"C:\\dir\\file.txt", sizeof(Buffer), Buffer, &Part, NULL);
    ok(Status == STATUS_SUCCESS, "Status 0x%lx\n", Status);
    ok(Part == Buffer + 7, "File part %p, expected %p\n", Part, Buffer + 7);
}
