/*
 * PROJECT:     LiberNT Driver Installer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a package which would replace a protected system file is recognized before it is installed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include "devices.hpp"
#include "text.hpp"

#include <setupapi.h>
#include <stdio.h>

using namespace lntdrv;

static bool WriteInf(const std::wstring &path, const char *file, int dirid)
{
    char text[2048];
    HANDLE handle;
    DWORD written = 0;
    int length;

    length = _snprintf(text, sizeof(text) - 1,
                       "[Version]\r\n"
                       "Signature=\"$WINDOWS NT$\"\r\n"
                       "Class=System\r\n"
                       "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
                       "Provider=%%Provider%%\r\n"
                       "DriverVer=11/20/2026,1.0.0.0\r\n"
                       "\r\n"
                       "[Install]\r\n"
                       "CopyFiles=Files\r\n"
                       "\r\n"
                       "[Files]\r\n"
                       "%s\r\n"
                       "\r\n"
                       "[DestinationDirs]\r\n"
                       "Files=%d\r\n"
                       "\r\n"
                       "[SourceDisksNames]\r\n"
                       "1=%%Disk%%\r\n"
                       "\r\n"
                       "[SourceDisksFiles]\r\n"
                       "%s=1\r\n"
                       "\r\n"
                       "[Strings]\r\n"
                       "Provider=\"LiberNT Protected File Test\"\r\n"
                       "Disk=\"LiberNT protected file test files\"\r\n",
                       file, dirid, file);
    if (length <= 0)
        return false;
    handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    WriteFile(handle, text, (DWORD)length, &written, NULL);
    CloseHandle(handle);
    return written == (DWORD)length;
}

static void CheckPackage(const std::wstring &directory, const char *file, int dirid, DWORD expected)
{
    PackageDriver driver;
    std::wstring found;
    DWORD error;

    driver.Inf = directory + L"\\lntprotected.inf";
    driver.Section = L"Install";
    ok(WriteInf(driver.Inf, file, dirid), "Writing the INF for %s failed with %lu\n", file, GetLastError());

    found = L"stale";
    error = FindProtectedFile(driver, found);
    ok(error == expected, "%s: FindProtectedFile returned 0x%lx, expected 0x%lx\n", file, error, expected);
    if (expected == ERROR_LNTDRV_PROTECTED_FILE)
    {
        size_t length = strlen(file);
        ok(found.size() > length && _wcsicmp(found.c_str() + found.size() - length,
                                             std::wstring(file, file + length).c_str()) == 0,
           "%s: the protected file found was %ls\n", file, found.c_str());
    }
    else
    {
        ok(found.empty(), "%s: a protected file was reported: %ls\n", file, found.c_str());
    }
    DeleteFileW(driver.Inf.c_str());
}

START_TEST(ProtectedFile)
{
    WCHAR temp[MAX_PATH];
    std::wstring directory;
    PackageDriver driver;
    std::wstring found;

    GetTempPathW(_countof(temp), temp);
    directory = std::wstring(temp) + L"lntprotectedpkg";
    CreateDirectoryW(directory.c_str(), NULL);

    CheckPackage(directory, "ntoskrnl.exe", 11, ERROR_LNTDRV_PROTECTED_FILE);
    CheckPackage(directory, "lntprotectedtest.sys", 12, ERROR_SUCCESS);

    driver.Inf = directory + L"\\lntprotected.inf";
    ok(FindProtectedFile(driver, found) == ERROR_SECTION_NOT_FOUND, "A driver without a section was scanned\n");

    RemoveDirectoryW(directory.c_str());
}
