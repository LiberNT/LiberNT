/*
 * PROJECT:     LiberNT Driver Installer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the package hash and signature checks of the driver installer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include "trust.hpp"
#include "text.hpp"

#include <bcrypt.h>

using namespace lntdrv;

static bool WriteBytes(const std::wstring &path, const void *data, DWORD size)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD written = 0;

    if (file == INVALID_HANDLE_VALUE)
        return false;
    WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
    return written == size;
}

START_TEST(TrustRules)
{
    static const BYTE AbcSha1[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                      0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
    static const char Inf[] = "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\nCatalogFile=test.cat\r\n";
    static const char NotACatalog[] = "this is not a PKCS #7 catalog";
    WCHAR temp[MAX_PATH];
    std::wstring directory, file;
    std::vector<BYTE> digest;
    DWORD error;

    if (!GetTempPathW(ARRAYSIZE(temp), temp))
    {
        skip("no temporary directory\n");
        return;
    }
    directory = std::wstring(temp) + L"lntdrvtrust";
    CreateDirectoryW(directory.c_str(), NULL);

    ok(WriteBytes(directory + L"\\abc.bin", "abc", 3), "writing abc.bin failed\n");
    error = HashFile(directory + L"\\abc.bin", BCRYPT_SHA1_ALGORITHM, digest);
    ok(error == ERROR_SUCCESS && digest.size() == sizeof(AbcSha1) && !memcmp(digest.data(), AbcSha1, sizeof(AbcSha1)),
       "SHA-1 of \"abc\": error %lu, %u bytes\n", error, (unsigned)digest.size());
    error = HashFile(directory + L"\\missing.bin", BCRYPT_SHA1_ALGORITHM, digest);
    ok(error == ERROR_FILE_NOT_FOUND && digest.empty(), "missing file: error %lu, %u bytes\n", error,
       (unsigned)digest.size());
    DeleteFileW((directory + L"\\abc.bin").c_str());

    ok(WriteBytes(directory + L"\\test.inf", Inf, sizeof(Inf) - 1), "writing test.inf failed\n");
    error = VerifyPackage(directory, file);
    ok(error == ERROR_LNTDRV_NO_SIGNATURE && file.empty(), "package without a catalog: error 0x%lx, file '%ls'\n", error,
       file.c_str());

    ok(WriteBytes(directory + L"\\test.cat", NotACatalog, sizeof(NotACatalog) - 1), "writing test.cat failed\n");
    error = VerifyPackage(directory, file);
    ok(error != ERROR_SUCCESS && error != ERROR_LNTDRV_NO_SIGNATURE && file == L"test.cat",
       "package with a corrupt catalog: error 0x%lx, file '%ls'\n", error, file.c_str());

    DeleteFileW((directory + L"\\test.cat").c_str());
    DeleteFileW((directory + L"\\test.inf").c_str());
    RemoveDirectoryW(directory.c_str());
}
