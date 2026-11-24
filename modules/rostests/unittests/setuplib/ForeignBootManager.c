/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which system installed the PC/AT boot manager of a root
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <strsafe.h>

static
VOID
WriteBootManager(
    _In_ PCWSTR Dir,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ DWORD Size)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    DWORD Written;

    StringCchPrintfW(Path, MAX_PATH, L"%s\\bootmgr", Dir);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(File, Data, Size, &Written, NULL) && Written == Size, "WriteFile failed with %lu\n", GetLastError());
    CloseHandle(File);
}

START_TEST(ForeignBootManager)
{
    /* The head of LiberNT's PC/AT BOOTMGR: a jump, "BOOTMGR", then the startup block 'BSTP' at 0x10 */
    static const UCHAR LiberNT[0x20] = { 0xE9, 0xF7, 0x01, 0x8D, 0xB4, 0x00, 0x00, 0x90, 'B', 'O', 'O', 'T', 'M', 'G', 'R', 0,
                                         'B', 'S', 'T', 'P', 1, 0, 0, 0 };
    static const UCHAR Other[0x20] = { 0xEB, 0x22, 0x90, 'B', 'O', 'O', 'T', 'M', 'G', 'R', 0, 0, 0, 0, 0, 0,
                                       'O', 'T', 'H', 'R' };
    WCHAR Dir[MAX_PATH], Root[MAX_PATH], Path[MAX_PATH];

    GetTempPathW(MAX_PATH, Dir);
    wcscat(Dir, L"LiberNTForeignBootManager");
    CreateDirectoryW(Dir, NULL);
    StringCchPrintfW(Root, MAX_PATH, L"\\??\\%s", Dir);
    StringCchPrintfW(Path, MAX_PATH, L"%s\\bootmgr", Dir);
    DeleteFileW(Path);

    ok_int(GetPcatBootManagerOwner(Root), BootManagerNone);

    WriteBootManager(Dir, LiberNT, sizeof(LiberNT));
    ok_int(GetPcatBootManagerOwner(Root), BootManagerLiberNT);

    WriteBootManager(Dir, Other, sizeof(Other));
    ok_int(GetPcatBootManagerOwner(Root), BootManagerForeign);

    WriteBootManager(Dir, LiberNT, 8);
    ok_int(GetPcatBootManagerOwner(Root), BootManagerForeign);

    DeleteFileW(Path);
    RemoveDirectoryW(Dir);
}
