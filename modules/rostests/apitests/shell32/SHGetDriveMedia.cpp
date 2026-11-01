/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SHGetDriveMedia
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shobjidl.h>

static void test_drive(PCWSTR drive, HRESULT expected_hr, DWORD expected_content)
{
    DWORD content = 0xdeadbeef;
    HRESULT hr;

    hr = SHGetDriveMedia(drive, &content);
    ok(hr == expected_hr, "%ls: hr 0x%lx, expected 0x%lx\n", drive, hr, expected_hr);
    ok(content == expected_content, "%ls: content 0x%lx, expected 0x%lx\n", drive, content, expected_content);
}

START_TEST(SHGetDriveMedia)
{
    WCHAR windir[MAX_PATH], root[4];
    WCHAR letter;
    DWORD drives;

    CoInitialize(NULL);

    GetWindowsDirectoryW(windir, MAX_PATH);
    root[0] = windir[0];
    root[1] = L':';
    root[2] = L'\\';
    root[3] = 0;

    test_drive(root, S_OK, ARCONTENT_UNKNOWNCONTENT);
    root[2] = 0;
    test_drive(root, S_OK, ARCONTENT_UNKNOWNCONTENT);
    test_drive(windir, S_OK, ARCONTENT_UNKNOWNCONTENT);

    drives = GetLogicalDrives();
    for (letter = L'Z'; letter > L'D'; letter--)
    {
        if (!(drives & (1 << (letter - L'A'))))
        {
            WCHAR missing[4] = { letter, L':', L'\\', 0 };

            test_drive(missing, E_INVALIDARG, 0);
            break;
        }
    }

    for (letter = L'C'; letter <= L'Z'; letter++)
    {
        WCHAR path[4] = { letter, L':', L'\\', 0 };
        DWORD content = 0xdeadbeef;
        HRESULT hr;

        if (!(drives & (1 << (letter - L'A'))))
            continue;
        hr = SHGetDriveMedia(path, &content);
        trace("%ls type %u: hr 0x%lx content 0x%lx\n", path, GetDriveTypeW(path), hr, content);
    }

    test_drive(L"", E_INVALIDARG, 0);
    test_drive(L"\\\\localhost\\c$", E_INVALIDARG, 0);

    CoUninitialize();
}
