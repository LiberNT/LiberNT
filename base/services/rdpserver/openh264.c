/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Download, check and remove Cisco's OpenH264 binary for the RDP console server
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "openh264.h"
#include <bcrypt.h>
#include <winhttp.h>
#include <bzlib.h>

#define OPENH264_HOST L"ciscobinary.openh264.org"

static const OPENH264_BINARY Binaries[] =
{
    { PROCESSOR_ARCHITECTURE_INTEL, L"openh264-2.6.0-win32.dll.bz2",
      { 0xb0, 0x09, 0x8d, 0xb6, 0xac, 0xbd, 0x29, 0x0a, 0x1f, 0xe1, 0x39, 0x97, 0xd6, 0x1d, 0x46, 0x1e,
        0x73, 0x27, 0xe3, 0x9b, 0x42, 0xbf, 0x86, 0x8d, 0xb4, 0x1f, 0xaf, 0x49, 0x8b, 0x76, 0x21, 0xa2 } },
    { PROCESSOR_ARCHITECTURE_AMD64, L"openh264-2.6.0-win64.dll.bz2",
      { 0x20, 0x76, 0xcb, 0x56, 0x75, 0xec, 0x6c, 0x1a, 0x4c, 0x70, 0xe7, 0xa2, 0xa3, 0x22, 0x55, 0x2f,
        0x54, 0x7b, 0x6e, 0xee, 0xd6, 0x49, 0xd6, 0xdf, 0xcd, 0x9e, 0x02, 0xa5, 0x43, 0xb2, 0x46, 0x91 } },
    { PROCESSOR_ARCHITECTURE_ARM64, L"openh264-2.6.0-win-arm64.dll.bz2",
      { 0xfb, 0x75, 0x10, 0x39, 0x38, 0xf4, 0xf4, 0x7d, 0x11, 0x9b, 0x98, 0x3e, 0x06, 0x33, 0x4d, 0xf4,
        0x1a, 0x80, 0x3b, 0xc7, 0x2f, 0xb5, 0xc4, 0x6e, 0x36, 0x23, 0xf6, 0xfe, 0xa5, 0x78, 0x27, 0x32 } }
};

void bz_internal_error(int errcode)
{
    RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, EXCEPTION_NONCONTINUABLE, 1, (const ULONG_PTR *)&errcode);
}

const OPENH264_BINARY *OpenH264Binary(USHORT architecture)
{
    UINT i;
    for (i = 0; i < ARRAYSIZE(Binaries); ++i)
        if (Binaries[i].Architecture == architecture) return &Binaries[i];
    return NULL;
}

BOOL OpenH264Sha256(const BYTE *data, DWORD size, BYTE hash[32])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE handle = NULL;
    BOOL ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
              BCryptCreateHash(algorithm, &handle, NULL, 0, NULL, 0, 0) == 0 &&
              BCryptHashData(handle, (PUCHAR)data, size, 0) == 0 &&
              BCryptFinishHash(handle, hash, 32, 0) == 0;
    if (handle) BCryptDestroyHash(handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

DWORD OpenH264Unpack(const BYTE *packed, DWORD packedSize, const BYTE sha256[32], BYTE **unpacked, DWORD *unpackedSize)
{
    unsigned int size = OPENH264_MAX_UNPACKED;
    BYTE hash[32];
    BYTE *buffer;
    int status;
    *unpacked = NULL;
    *unpackedSize = 0;
    if (!packedSize || packedSize > OPENH264_MAX_PACKED) return ERROR_INVALID_DATA;
    buffer = malloc(OPENH264_MAX_UNPACKED);
    if (!buffer) return ERROR_OUTOFMEMORY;
    status = BZ2_bzBuffToBuffDecompress((char *)buffer, &size, (char *)packed, packedSize, 0, 0);
    if (status != BZ_OK)
    {
        free(buffer);
        return ERROR_INVALID_DATA;
    }
    assert(size <= OPENH264_MAX_UNPACKED);
    if (!OpenH264Sha256(buffer, size, hash))
    {
        free(buffer);
        return ERROR_INTERNAL_ERROR;
    }
    if (memcmp(hash, sha256, sizeof(hash)))
    {
        free(buffer);
        return ERROR_INVALID_DATA;
    }
    *unpacked = buffer;
    *unpackedSize = size;
    return ERROR_SUCCESS;
}

static DWORD Download(const WCHAR *name, BOOL secure, BYTE **data, DWORD *size)
{
    WCHAR path[64];
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD error = ERROR_SUCCESS, status = 0, length = sizeof(status), read = 0, used = 0;
    BYTE *buffer = malloc(OPENH264_MAX_PACKED);
    *data = NULL;
    *size = 0;
    if (!buffer) return ERROR_OUTOFMEMORY;
    path[0] = L'/';
    lstrcpynW(path + 1, name, ARRAYSIZE(path) - 1);
    session = WinHttpOpen(L"LiberNT", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) connection = WinHttpConnect(session, OPENH264_HOST, secure ? INTERNET_DEFAULT_HTTPS_PORT :
                                             INTERNET_DEFAULT_HTTP_PORT, 0);
    if (connection) request = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &length,
                             NULL))
        error = GetLastError() ? GetLastError() : ERROR_WINHTTP_CONNECTION_ERROR;
    else if (status != 200)
        error = ERROR_FILE_NOT_FOUND;
    while (!error && WinHttpReadData(request, buffer + used, OPENH264_MAX_PACKED - used, &read) && read)
    {
        used += read;
        if (used == OPENH264_MAX_PACKED) error = ERROR_INVALID_DATA;
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    if (!error && !used) error = ERROR_INVALID_DATA;
    if (error)
    {
        free(buffer);
        return error;
    }
    *data = buffer;
    *size = used;
    return ERROR_SUCCESS;
}

static BOOL LibraryPath(WCHAR *path, WCHAR *temporary)
{
    UINT length = GetSystemDirectoryW(path, MAX_PATH - 32);
    if (!length || length >= MAX_PATH - 32) return FALSE;
    lstrcatW(path, L"\\rdpserver\\openh264.dll");
    lstrcpyW(temporary, path);
    lstrcatW(temporary, L".new");
    return TRUE;
}

static DWORD SetEnabled(DWORD value)
{
    HKEY key;
    LONG error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, OPENH264_PARAMETERS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (error) return (DWORD)error;
    error = RegSetValueExW(key, OPENH264_VALUE, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return (DWORD)error;
}

BOOL OpenH264Enabled(void)
{
    DWORD value = 0, size = sizeof(value), type;
    WCHAR path[MAX_PATH], temporary[MAX_PATH];
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, OPENH264_PARAMETERS_KEY, 0, KEY_QUERY_VALUE, &key)) return FALSE;
    if (RegQueryValueExW(key, OPENH264_VALUE, NULL, &type, (BYTE *)&value, &size) || type != REG_DWORD) value = 0;
    RegCloseKey(key);
    return value && LibraryPath(path, temporary) && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

DWORD OpenH264Install(void)
{
    SYSTEM_INFO system;
    const OPENH264_BINARY *binary;
    WCHAR path[MAX_PATH], temporary[MAX_PATH];
    BYTE *packed = NULL, *library = NULL;
    DWORD packedSize = 0, librarySize = 0, written = 0, error;
    HANDLE file;
    GetNativeSystemInfo(&system);
    binary = OpenH264Binary(system.wProcessorArchitecture);
    if (!binary) return ERROR_NOT_SUPPORTED;
    if (!LibraryPath(path, temporary)) return ERROR_PATH_NOT_FOUND;
    error = Download(binary->Name, TRUE, &packed, &packedSize);
    if (error) error = Download(binary->Name, FALSE, &packed, &packedSize);
    if (!error) error = OpenH264Unpack(packed, packedSize, binary->Sha256, &library, &librarySize);
    free(packed);
    if (error) return error;
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        error = GetLastError();
    else
    {
        if (!WriteFile(file, library, librarySize, &written, NULL) || written != librarySize) error = GetLastError();
        if (!FlushFileBuffers(file) && !error) error = GetLastError();
        CloseHandle(file);
        if (!error && !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            error = GetLastError();
        if (error) DeleteFileW(temporary);
    }
    free(library);
    return error ? error : SetEnabled(1);
}

DWORD OpenH264Remove(void)
{
    WCHAR path[MAX_PATH], temporary[MAX_PATH];
    DWORD error = SetEnabled(0);
    if (error) return error;
    if (!LibraryPath(path, temporary)) return ERROR_PATH_NOT_FOUND;
    if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND &&
        !MoveFileExW(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
        return GetLastError();
    return ERROR_SUCCESS;
}
