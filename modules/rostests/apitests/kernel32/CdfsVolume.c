/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that an ISO 9660 disc mounts with the CDFS file system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_LABEL L"LNTCDFS"
#define TEST_TEXT "LiberNT CDFS\n"

START_TEST(CdfsVolume)
{
    WCHAR Root[] = L"C:\\", Label[MAX_PATH + 1], FileSystem[MAX_PATH + 1], File[MAX_PATH];
    DWORD Serial, MaxComponent, Flags, Read;
    char Data[64];
    BOOL Found = FALSE;
    HANDLE hFile;

    for (WCHAR Drive = L'C'; Drive <= L'Z' && !Found; Drive++)
    {
        Root[0] = Drive;
        if (GetDriveTypeW(Root) != DRIVE_CDROM)
            continue;
        if (!GetVolumeInformationW(Root, Label, _countof(Label), &Serial, &MaxComponent, &Flags,
                                   FileSystem, _countof(FileSystem)))
        {
            continue;
        }
        if (wcscmp(Label, TEST_LABEL) != 0)
            continue;

        Found = TRUE;
        ok(wcscmp(FileSystem, L"CDFS") == 0, "%ls mounted with %ls\n", Root, FileSystem);

        StringCchPrintfW(File, _countof(File), L"%sHELLO.TXT", Root);
        hFile = CreateFileW(File, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        ok(hFile != INVALID_HANDLE_VALUE, "Cannot open %ls (%lu)\n", File, GetLastError());
        if (hFile != INVALID_HANDLE_VALUE)
        {
            ZeroMemory(Data, sizeof(Data));
            ok(ReadFile(hFile, Data, sizeof(Data) - 1, &Read, NULL), "ReadFile failed (%lu)\n", GetLastError());
            ok(Read == sizeof(TEST_TEXT) - 1 && memcmp(Data, TEST_TEXT, Read) == 0, "Read %lu bytes: %s\n", Read, Data);
            CloseHandle(hFile);
        }
    }

    if (!Found)
        skip("No disc labelled %ls\n", TEST_LABEL);
}
