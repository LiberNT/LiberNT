/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how the OpenH264 binary is chosen, unpacked and checked before use
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <stdlib.h>
#include <stdio.h>
#include <bzlib.h>
#include "openh264.h"

#define DATA_SIZE 100000

static BYTE Data[DATA_SIZE];
static BYTE Packed[DATA_SIZE * 2];

static void TestBinaries(void)
{
    static const USHORT architectures[] =
        { PROCESSOR_ARCHITECTURE_INTEL, PROCESSOR_ARCHITECTURE_AMD64, PROCESSOR_ARCHITECTURE_ARM64 };
    static const WCHAR *names[] =
        { L"openh264-2.6.0-win32.dll.bz2", L"openh264-2.6.0-win64.dll.bz2", L"openh264-2.6.0-win-arm64.dll.bz2" };
    static const BYTE zero[32] = {0};
    SYSTEM_INFO system;
    UINT i;
    for (i = 0; i < ARRAYSIZE(architectures); ++i)
    {
        const OPENH264_BINARY *binary = OpenH264Binary(architectures[i]);
        ok(binary != NULL, "no binary for architecture %u\n", architectures[i]);
        if (!binary) continue;
        ok(!lstrcmpW(binary->Name, names[i]), "architecture %u downloads %ls\n", architectures[i], binary->Name);
        ok(memcmp(binary->Sha256, zero, sizeof(zero)) != 0, "architecture %u has no hash\n", architectures[i]);
    }
    ok(OpenH264Binary(PROCESSOR_ARCHITECTURE_IA64) == NULL, "a binary was offered for IA64\n");
    GetNativeSystemInfo(&system);
    ok(OpenH264Binary(system.wProcessorArchitecture) != NULL, "no binary for this machine (%u)\n",
       system.wProcessorArchitecture);
}

static void TestSha256(void)
{
    static const BYTE abc[32] =
    {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    BYTE hash[32];
    ok(OpenH264Sha256((const BYTE *)"abc", 3, hash), "OpenH264Sha256 failed\n");
    ok(!memcmp(hash, abc, sizeof(hash)), "the SHA-256 of \"abc\" is wrong\n");
}

static void TestUnpack(void)
{
    unsigned int packedSize = sizeof(Packed);
    static BYTE sentinel;
    BYTE hash[32], wrong[32], *unpacked = &sentinel;
    DWORD unpackedSize = 1, error;
    UINT32 i, random = 0x2468ACE1;
    for (i = 0; i < DATA_SIZE; ++i)
    {
        random = random * 1103515245u + 12345u;
        Data[i] = (i % 7) ? (BYTE)(random >> 24) : (BYTE)i;
    }
    ok(BZ2_bzBuffToBuffCompress((char *)Packed, &packedSize, (char *)Data, DATA_SIZE, 9, 0, 0) == BZ_OK,
       "BZ2_bzBuffToBuffCompress failed\n");
    ok(OpenH264Sha256(Data, DATA_SIZE, hash), "OpenH264Sha256 failed\n");

    error = OpenH264Unpack(Packed, packedSize, hash, &unpacked, &unpackedSize);
    ok(error == ERROR_SUCCESS, "unpacking with the right hash failed with %lu\n", error);
    ok(unpacked && unpackedSize == DATA_SIZE && !memcmp(unpacked, Data, DATA_SIZE), "the unpacked data differ\n");
    free(unpacked);

    memcpy(wrong, hash, sizeof(wrong));
    wrong[31] ^= 1;
    unpacked = &sentinel;
    error = OpenH264Unpack(Packed, packedSize, wrong, &unpacked, &unpackedSize);
    ok(error == ERROR_INVALID_DATA, "unpacking with a wrong hash returned %lu\n", error);
    ok(unpacked == NULL && unpackedSize == 0, "a rejected binary was returned\n");

    Packed[packedSize / 2] ^= 0x55;
    error = OpenH264Unpack(Packed, packedSize, hash, &unpacked, &unpackedSize);
    ok(error == ERROR_INVALID_DATA, "unpacking damaged data returned %lu\n", error);
    ok(unpacked == NULL, "damaged data were returned\n");

    error = OpenH264Unpack(Packed, 0, hash, &unpacked, &unpackedSize);
    ok(error == ERROR_INVALID_DATA, "unpacking nothing returned %lu\n", error);
    error = OpenH264Unpack(Packed, OPENH264_MAX_PACKED + 1, hash, &unpacked, &unpackedSize);
    ok(error == ERROR_INVALID_DATA, "unpacking too much data returned %lu\n", error);
}

static void TestLoad(void)
{
    WCHAR system[MAX_PATH], library[MAX_PATH], saved[MAX_PATH], planted[MAX_PATH], other[MAX_PATH];
    BOOL moved;
    HMODULE module;
    GetSystemDirectoryW(system, ARRAYSIZE(system));
    _snwprintf(library, ARRAYSIZE(library), L"%ls\\rdpserver\\openh264.dll", system);
    _snwprintf(saved, ARRAYSIZE(saved), L"%ls.test-saved", library);
    _snwprintf(other, ARRAYSIZE(other), L"%ls\\version.dll", system);
    GetModuleFileNameW(NULL, planted, ARRAYSIZE(planted));
    lstrcpyW(wcsrchr(planted, L'\\') + 1, L"openh264.dll");
    moved = MoveFileExW(library, saved, MOVEFILE_REPLACE_EXISTING);
    if (!moved && GetLastError() != ERROR_FILE_NOT_FOUND)
    {
        skip("Cannot move %ls aside: %lu\n", library, GetLastError());
        return;
    }

    ok(CopyFileW(other, planted, FALSE), "Cannot plant openh264.dll next to the test: %lu\n", GetLastError());
    module = OpenH264Load();
    ok(module == NULL, "With no library in rdpserver, OpenH264Load loaded %p\n", module);
    ok(GetModuleHandleW(L"openh264.dll") == NULL, "A planted openh264.dll was loaded\n");
    DeleteFileW(planted);

    if (CopyFileW(other, library, FALSE))
    {
        module = OpenH264Load();
        ok(module == NULL, "OpenH264Load loaded a library that is not Cisco's: %p\n", module);
        ok(GetModuleHandleW(L"openh264.dll") == NULL, "A library that is not Cisco's was loaded\n");
        DeleteFileW(library);
    }
    else
    {
        skip("Cannot write %ls: %lu\n", library, GetLastError());
    }

    if (moved)
    {
        ok(MoveFileExW(saved, library, 0), "Cannot put %ls back: %lu\n", library, GetLastError());
        module = OpenH264Load();
        ok(module != NULL, "Cisco's library was not loaded\n");
        if (module) FreeLibrary(module);
    }
}

START_TEST(OpenH264)
{
    TestBinaries();
    TestSha256();
    TestUnpack();
    TestLoad();
}
