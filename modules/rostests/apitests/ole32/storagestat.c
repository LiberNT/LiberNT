/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the state bits and times that structured storage reports
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <objbase.h>
#include <stdio.h>

static WCHAR FilePath[MAX_PATH];

static
BOOL
IsZeroTime(const FILETIME *Time)
{
    return !Time->dwLowDateTime && !Time->dwHighDateTime;
}

static
VOID
CheckElements(IStorage *Root, const STATSTG *SubStat)
{
    IEnumSTATSTG *Enum;
    STATSTG Stat;
    ULONG Fetched;
    BOOL SeenSub = FALSE, SeenStream = FALSE;

    ok_hex(IStorage_EnumElements(Root, 0, NULL, 0, &Enum), S_OK);
    while (IEnumSTATSTG_Next(Enum, 1, &Stat, &Fetched) == S_OK && Fetched == 1)
    {
        if (!wcscmp(Stat.pwcsName, L"Sub"))
        {
            SeenSub = TRUE;
            ok_long(Stat.type, STGTY_STORAGE);
            ok_hex(Stat.grfStateBits, 0xf3);
            ok(!memcmp(&Stat.ctime, &SubStat->ctime, sizeof(FILETIME)), "enumerated creation time differs\n");
            ok(!memcmp(&Stat.mtime, &SubStat->mtime, sizeof(FILETIME)), "enumerated modification time differs\n");
            ok(!memcmp(&Stat.atime, &Stat.mtime, sizeof(FILETIME)), "access time %08lx%08lx, modification time %08lx%08lx\n",
               Stat.atime.dwHighDateTime, Stat.atime.dwLowDateTime, Stat.mtime.dwHighDateTime, Stat.mtime.dwLowDateTime);
        }
        else if (!wcscmp(Stat.pwcsName, L"Stream"))
        {
            SeenStream = TRUE;
            ok_long(Stat.type, STGTY_STREAM);
            ok_hex(Stat.grfStateBits, 0);
            ok(IsZeroTime(&Stat.ctime), "stream creation time %08lx%08lx\n", Stat.ctime.dwHighDateTime,
               Stat.ctime.dwLowDateTime);
            ok(IsZeroTime(&Stat.mtime), "stream modification time %08lx%08lx\n", Stat.mtime.dwHighDateTime,
               Stat.mtime.dwLowDateTime);
            ok(IsZeroTime(&Stat.atime), "stream access time %08lx%08lx\n", Stat.atime.dwHighDateTime,
               Stat.atime.dwLowDateTime);
        }
        CoTaskMemFree(Stat.pwcsName);
    }
    IEnumSTATSTG_Release(Enum);
    ok(SeenSub, "Sub not enumerated\n");
    ok(SeenStream, "Stream not enumerated\n");
}

static
VOID
TestStateBits(DWORD Mode)
{
    IStorage *Root, *Sub;
    IStream *Stream;
    STATSTG Stat, SubStat;
    ULONG Written;

    ok_hex(StgCreateDocfile(FilePath, STGM_CREATE | Mode | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &Root), S_OK);
    if (!Root)
        return;
    ok_hex(IStorage_SetStateBits(Root, 0x11, ~0u), S_OK);
    ok_hex(IStorage_Stat(Root, &Stat, STATFLAG_NONAME), S_OK);
    ok_hex(Stat.grfStateBits, 0x11);
    ok_hex(IStorage_CreateStorage(Root, L"Sub", STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &Sub), S_OK);
    if (Sub)
    {
        ok_hex(IStorage_SetStateBits(Sub, 0xf0, ~0u), S_OK);
        ok_hex(IStorage_SetStateBits(Sub, 0x0f, 0x03), S_OK);
        ok_hex(IStorage_Stat(Sub, &Stat, STATFLAG_NONAME), S_OK);
        ok_hex(Stat.grfStateBits, 0xf3);
        if (Mode == STGM_TRANSACTED)
            ok_hex(IStorage_Commit(Sub, STGC_DEFAULT), S_OK);
        IStorage_Release(Sub);
    }
    ok_hex(IStorage_CreateStream(Root, L"Stream", STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &Stream), S_OK);
    if (Stream)
    {
        ok_hex(IStream_Write(Stream, "abc", 3, &Written), S_OK);
        IStream_Release(Stream);
    }
    if (Mode == STGM_TRANSACTED)
        ok_hex(IStorage_Commit(Root, STGC_DEFAULT), S_OK);
    IStorage_Release(Root);

    ok_hex(StgOpenStorage(FilePath, NULL, STGM_READ | STGM_SHARE_DENY_WRITE, NULL, 0, &Root), S_OK);
    if (!Root)
        return;
    ok_hex(IStorage_Stat(Root, &Stat, STATFLAG_NONAME), S_OK);
    ok_hex(Stat.grfStateBits, 0x11);
    ok_hex(IStorage_OpenStorage(Root, L"Sub", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0, &Sub), S_OK);
    if (Sub)
    {
        ok_hex(IStorage_Stat(Sub, &SubStat, STATFLAG_NONAME), S_OK);
        ok_hex(SubStat.grfStateBits, 0xf3);
        IStorage_Release(Sub);
        CheckElements(Root, &SubStat);
    }
    IStorage_Release(Root);
}

START_TEST(storagestat)
{
    WCHAR TempDir[MAX_PATH];

    GetTempPathW(RTL_NUMBER_OF(TempDir), TempDir);
    swprintf(FilePath, RTL_NUMBER_OF(FilePath), L"%lslntstgstat.stg", TempDir);

    winetest_push_context("direct");
    TestStateBits(STGM_DIRECT);
    winetest_pop_context();
    winetest_push_context("transacted");
    TestStateBits(STGM_TRANSACTED);
    winetest_pop_context();

    DeleteFileW(FilePath);
}
