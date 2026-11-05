/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a write whose buffer cannot be read leaves the file size and data unchanged
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

static BOOL QueryStandard(HANDLE File, PFILE_STANDARD_INFO Info)
{
    ZeroMemory(Info, sizeof(*Info));
    return GetFileInformationByHandleEx(File, FileStandardInfo, Info, sizeof(*Info));
}

static HANDLE CreateFilled(PCWSTR Path, DWORD Size)
{
    BYTE Fill[4096];
    DWORD Done = 0, Chunk, Written;
    HANDLE File;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return File;
    FillMemory(Fill, sizeof(Fill), 'A');
    while (Done < Size)
    {
        Chunk = min(Size - Done, (DWORD)sizeof(Fill));
        Written = 0;
        if (!WriteFile(File, Fill, Chunk, &Written, NULL) || Written != Chunk)
            break;
        Done += Written;
    }
    ok(Done == Size, "filling %lu bytes wrote %lu, error %lu\n", Size, Done, GetLastError());
    return File;
}

static void CheckFailedWrite(PCWSTR Path, DWORD Size, const BYTE *Buffer, DWORD Length, DWORD ExpectedError, PCSTR Name)
{
    FILE_STANDARD_INFO Before, After;
    BYTE Tail[16], Check[16];
    LARGE_INTEGER Position;
    DWORD Written, Read, Error;
    HANDLE File;
    BOOL Ret;

    File = CreateFilled(Path, Size);
    if (File == INVALID_HANDLE_VALUE)
    {
        skip("%s: CreateFileW failed, error %lu\n", Name, GetLastError());
        return;
    }
    ok(QueryStandard(File, &Before), "%s: query before failed, error %lu\n", Name, GetLastError());
    ok(Before.EndOfFile.QuadPart == Size, "%s: size before %I64d\n", Name, Before.EndOfFile.QuadPart);

    SetLastError(0xdeadbeef);
    Written = 0xdeadbeef;
    Ret = WriteFile(File, Buffer, Length, &Written, NULL);
    Error = GetLastError();
    ok(!Ret, "%s: WriteFile of %#lx bytes succeeded\n", Name, Length);
    ok(Written == 0, "%s: %lu bytes reported written\n", Name, Written);
    if (ExpectedError)
        ok(Error == ExpectedError, "%s: error %lu, expected %lu\n", Name, Error, ExpectedError);

    ok(QueryStandard(File, &After), "%s: query after failed, error %lu\n", Name, GetLastError());
    ok(After.EndOfFile.QuadPart == Before.EndOfFile.QuadPart, "%s: size %#I64x after the failed write, expected %#I64x\n",
       Name, After.EndOfFile.QuadPart, Before.EndOfFile.QuadPart);
    ok(After.AllocationSize.QuadPart == Before.AllocationSize.QuadPart ||
       After.AllocationSize.QuadPart >= (LONGLONG)Size + Length,
       "%s: allocation %#I64x after the failed write, expected %#I64x or at least %#I64x\n", Name,
       After.AllocationSize.QuadPart, Before.AllocationSize.QuadPart, (LONGLONG)Size + Length);
    Position.QuadPart = 0;
    ok(SetFilePointerEx(File, Position, &Position, FILE_CURRENT), "%s: SetFilePointerEx failed, error %lu\n", Name, GetLastError());
    ok(Position.QuadPart == Size, "%s: file pointer %#I64x after the failed write, expected %#lx\n", Name, Position.QuadPart, Size);

    Position.QuadPart = 0;
    ok(SetFilePointerEx(File, Position, NULL, FILE_END), "%s: seek to end failed, error %lu\n", Name, GetLastError());
    FillMemory(Tail, sizeof(Tail), 'Z');
    Written = 0;
    ok(WriteFile(File, Tail, sizeof(Tail), &Written, NULL) && Written == sizeof(Tail), "%s: append failed, error %lu\n",
       Name, GetLastError());
    ok(QueryStandard(File, &After), "%s: query after append failed, error %lu\n", Name, GetLastError());
    ok(After.EndOfFile.QuadPart == Size + sizeof(Tail), "%s: size %#I64x after the append, expected %#lx\n", Name,
       After.EndOfFile.QuadPart, Size + (DWORD)sizeof(Tail));

    Position.QuadPart = Size - sizeof(Check);
    ok(SetFilePointerEx(File, Position, NULL, FILE_BEGIN), "%s: seek back failed, error %lu\n", Name, GetLastError());
    Read = 0;
    ok(ReadFile(File, Check, sizeof(Check), &Read, NULL) && Read == sizeof(Check), "%s: read old tail failed, error %lu\n",
       Name, GetLastError());
    ok(Check[0] == 'A' && Check[sizeof(Check) - 1] == 'A', "%s: old data changed to %02x..%02x\n", Name, Check[0],
       Check[sizeof(Check) - 1]);
    Read = 0;
    ok(ReadFile(File, Check, sizeof(Check), &Read, NULL) && Read == sizeof(Check), "%s: read appended data failed, error %lu\n",
       Name, GetLastError());
    ok(!memcmp(Check, Tail, sizeof(Tail)), "%s: appended data starts with %02x\n", Name, Check[0]);
    CloseHandle(File);
}

START_TEST(WriteFileBadBuffer)
{
    WCHAR Path[MAX_PATH];
    SYSTEM_INFO System;
    PBYTE Pages;
    DWORD OldProtect;
    BYTE Small[64];

    GetSystemInfo(&System);
    ok(GetTempPathW(ARRAYSIZE(Path), Path) != 0, "GetTempPathW failed, error %lu\n", GetLastError());
    StringCchCatW(Path, ARRAYSIZE(Path), L"WriteFileBadBuffer.bin");

    Pages = VirtualAlloc(NULL, 2 * System.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Pages != NULL, "VirtualAlloc failed, error %lu\n", GetLastError());
    if (!Pages)
        return;
    FillMemory(Pages, System.dwPageSize, 'X');
    ok(VirtualProtect(Pages + System.dwPageSize, System.dwPageSize, PAGE_NOACCESS, &OldProtect),
       "VirtualProtect failed, error %lu\n", GetLastError());

    CheckFailedWrite(Path, 100, Pages + System.dwPageSize - 16, 32, ERROR_INVALID_USER_BUFFER, "small file");
    CheckFailedWrite(Path, 65536, Pages, 2 * System.dwPageSize, ERROR_INVALID_USER_BUFFER, "large file");
    FillMemory(Small, sizeof(Small), 'Y');
    CheckFailedWrite(Path, 65536, Small, 0xFFFFFFFC, 0, "negative length");

    VirtualFree(Pages, 0, MEM_RELEASE);
}
