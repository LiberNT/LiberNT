/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the console alias functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static WCHAR ExeName[] = L"aliastest.exe";

static
VOID
PrintList(
    _In_ PCSTR What,
    _In_ DWORD Result,
    _In_reads_bytes_(Bytes) const WCHAR* Text,
    _In_ DWORD Bytes)
{
    CHAR Line[512];
    DWORD Index, Out = 0;

    for (Index = 0; Index < Bytes / sizeof(WCHAR) && Out < sizeof(Line) - 1; Index++)
        Line[Out++] = Text[Index] == 0 ? '|' : (Text[Index] < 0x80 ? (CHAR)Text[Index] : '?');
    Line[Out] = 0;
    printf("%s: ret=%lu gle=%lu text=[%s]\n", What, Result, GetLastError(), Line);
}

static
VOID
TestWideAliases(VOID)
{
    WCHAR Buffer[256];
    DWORD Result;

    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasW(L"zz", Buffer, sizeof(Buffer), ExeName);
    ok_long(Result, 0);
    ok_long(GetLastError(), ERROR_GEN_FAILURE);
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasesLengthW(ExeName), 0);
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(GetConsoleAliasExesLengthW(), 0);

    SetLastError(0xdeadbeef);
    ok_long(AddConsoleAliasW(L"dir1", L"dir /w $*", ExeName), TRUE);
    ok_long(GetLastError(), 0xdeadbeef);

    memset(Buffer, 0x55, sizeof(Buffer));
    Result = GetConsoleAliasW(L"dir1", Buffer, sizeof(Buffer), ExeName);
    ok_long(Result, 20);
    ok(wcscmp(Buffer, L"dir /w $*") == 0, "Got %S\n", Buffer);
    memset(Buffer, 0x55, sizeof(Buffer));
    Result = GetConsoleAliasW(L"DIR1", Buffer, sizeof(Buffer), L"ALIASTEST.EXE");
    ok_long(Result, 20);
    ok(wcscmp(Buffer, L"dir /w $*") == 0, "Got %S\n", Buffer);

    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasW(L"dir1", Buffer, 4, ExeName);
    ok_long(Result, 4);
    ok_long(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    ok(Buffer[0] == 0x5555, "Buffer was written: %x\n", Buffer[0]);
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasW(L"dir1", Buffer, 9 * sizeof(WCHAR), ExeName);
    ok_long(Result, 18);
    ok_long(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    Result = GetConsoleAliasW(L"dir1", Buffer, 10 * sizeof(WCHAR), ExeName);
    ok_long(Result, 20);
    Result = GetConsoleAliasW(L"dir1", Buffer, 21, ExeName);
    ok_long(Result, 20);

    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasW(L"dir1", NULL, 0, ExeName), 0);
    ok_long(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasW(NULL, Buffer, sizeof(Buffer), ExeName), 0);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasW(L"dir1", Buffer, sizeof(Buffer), NULL), 0);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasW(L"dir1", Buffer, sizeof(Buffer), L"other.exe"), 0);
    ok_long(GetLastError(), ERROR_GEN_FAILURE);

    ok_long(AddConsoleAliasW(L"x", L"y", ExeName), TRUE);
    ok_long(GetConsoleAliasesLengthW(ExeName), 38);
    memset(Buffer, 0x55, sizeof(Buffer));
    Result = GetConsoleAliasesW(Buffer, sizeof(Buffer), ExeName);
    ok_long(Result, 38);
    ok(memcmp(Buffer, L"dir1=dir /w $*\0x=y\0", 38) == 0, "Wrong alias list\n");
    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasesW(Buffer, 10, ExeName), 0);
    ok_long(GetLastError(), ERROR_BUFFER_OVERFLOW);
    ok(Buffer[0] == 0x5555, "Buffer was written: %x\n", Buffer[0]);

    ok_long(AddConsoleAliasW(L"X", L"zz", ExeName), TRUE);
    ok_long(GetConsoleAliasW(L"x", Buffer, sizeof(Buffer), ExeName), 6);
    ok(wcscmp(Buffer, L"zz") == 0, "Got %S\n", Buffer);
    memset(Buffer, 0x55, sizeof(Buffer));
    ok_long(GetConsoleAliasesW(Buffer, sizeof(Buffer), ExeName), 40);
    ok(memcmp(Buffer, L"dir1=dir /w $*\0x=zz\0", 40) == 0, "Wrong alias list\n");

    ok_long(AddConsoleAliasW(L"q", L"r", L"Other.exe"), TRUE);
    ok_long(GetConsoleAliasExesLengthW(), 48);
    memset(Buffer, 0x55, sizeof(Buffer));
    Result = GetConsoleAliasExesW(Buffer, sizeof(Buffer));
    ok_long(Result, 48);
    PrintList("exes", Result, Buffer, 64);
    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasExesW(Buffer, 6), 0);
    ok_long(GetLastError(), ERROR_BUFFER_OVERFLOW);

    ok_long(AddConsoleAliasW(L"a b", L"t", ExeName), TRUE);
    SetLastError(0xdeadbeef);
    ok_long(AddConsoleAliasW(L"", L"t", ExeName), FALSE);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_long(AddConsoleAliasW(NULL, L"t", ExeName), FALSE);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_long(AddConsoleAliasW(L"s", L"t", NULL), FALSE);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_long(AddConsoleAliasW(L"s", L"t", L""), FALSE);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
}

static
VOID
TestAnsiAliases(VOID)
{
    CHAR Buffer[256];

    ok_long(AddConsoleAliasA("ansi", "target text", "aliastest.exe"), TRUE);
    memset(Buffer, 0x55, sizeof(Buffer));
    ok_long(GetConsoleAliasA("ansi", Buffer, sizeof(Buffer), "aliastest.exe"), 12);
    ok(strcmp(Buffer, "target text") == 0, "Got %s\n", Buffer);
    ok_long(GetConsoleAliasesLengthA("aliastest.exe"), 43);
    ok_long(GetConsoleAliasExesLengthA(), 24);
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasA("ansi", Buffer, 4, "aliastest.exe"), 8);
    ok_long(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    ok_long(GetConsoleAliasesLengthW(ExeName), 86);
}

static
VOID
TestDeleteAliases(VOID)
{
    WCHAR Buffer[256];
    DWORD Result;

    ok_long(AddConsoleAliasW(L"dir1", NULL, ExeName), TRUE);
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasW(L"dir1", Buffer, sizeof(Buffer), ExeName), 0);
    ok_long(GetLastError(), ERROR_GEN_FAILURE);
    ok_long(AddConsoleAliasW(L"x", L"", ExeName), TRUE);
    SetLastError(0xdeadbeef);
    ok_long(GetConsoleAliasW(L"x", Buffer, sizeof(Buffer), ExeName), 0);
    ok_long(GetLastError(), ERROR_GEN_FAILURE);
    ok_long(AddConsoleAliasW(L"nonexistent", NULL, ExeName), TRUE);

    Result = AddConsoleAliasW(L"ansi", NULL, ExeName);
    printf("delete-ansi: ret=%lu\n", Result);
    Result = AddConsoleAliasW(L"a b", NULL, ExeName);
    printf("delete-space: ret=%lu\n", Result);
    printf("len-after-delete: %lu\n", GetConsoleAliasesLengthW(ExeName));
    printf("exes-len-after-delete: %lu\n", GetConsoleAliasExesLengthW());
    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasExesW(Buffer, sizeof(Buffer));
    PrintList("exes-after-delete", Result, Buffer, 64);
    Result = AddConsoleAliasW(L"q", NULL, L"other.exe");
    printf("delete-other: ret=%lu\n", Result);
    printf("exes-len-end: %lu\n", GetConsoleAliasExesLengthW());

    ok_long(AddConsoleAliasW(L"m", L"n", L"MiXeD.EXE"), TRUE);
    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasExesW(Buffer, sizeof(Buffer));
    PrintList("exes-mixed-case", Result, Buffer, 64);
    ok_long(AddConsoleAliasW(L"Bee", L"1", L"order.exe"), TRUE);
    ok_long(AddConsoleAliasW(L"apple", L"2", L"order.exe"), TRUE);
    ok_long(AddConsoleAliasW(L"Cat", L"3", L"order.exe"), TRUE);
    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasesW(Buffer, sizeof(Buffer), L"order.exe");
    PrintList("order", Result, Buffer, 64);
    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasesW(Buffer, sizeof(Buffer), L"unknown.exe");
    PrintList("list-unknown-exe", Result, Buffer, 4);
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasesLengthW(NULL);
    printf("len-null-exe: ret=%lu gle=%lu\n", Result, GetLastError());
    SetLastError(0xdeadbeef);
    Result = GetConsoleAliasW(L"m", Buffer, sizeof(Buffer), L"mixed.exe");
    printf("get-mixed-lower: ret=%lu gle=%lu\n", Result, GetLastError());
}

START_TEST(ConsoleAlias)
{
    TestWideAliases();
    TestAnsiAliases();
    TestDeleteAliases();
}
