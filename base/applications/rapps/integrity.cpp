/*
 * PROJECT:     ReactOS Applications Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Various integrity check mechanisms
 * COPYRIGHT:   Copyright Ismael Ferreras Morezuelas (swyterzone+ros@gmail.com)
 *              Copyright 2016 Mark Jansen <mark.jansen@reactos.org>
 */

#include "rapps.h"

#include "download.h"

BOOL
VerifyIntegDigest(LPCWSTR lpSHA1Hash, PSHA_CTX Context)
{
    ULONG sha[5];
    WCHAR buf[(sizeof(sha) * 2) + 1];

    A_SHAFinal(Context, sha);
    for (UINT i = 0; i < sizeof(sha); i++)
        _swprintf(buf + 2 * i, L"%02x", ((unsigned char *)sha)[i]);
    return !_wcsicmp(buf, lpSHA1Hash);
}

BOOL
VerifyInteg(LPCWSTR lpSHA1Hash, LPCWSTR lpFileName)
{
    BOOL ret = FALSE;

    /* first off, does it exist at all? */
    HANDLE file = CreateFileW(lpFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);

    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    /* let's grab the actual file size to organize the mmap'ing rounds */
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);

    /* retrieve a handle to map the file contents to memory */
    HANDLE map = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (map)
    {
        /* map that thing in address space */
        const unsigned char *file_map = static_cast<const unsigned char *>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
        if (file_map)
        {
            SHA_CTX ctx;
            /* initialize the SHA-1 context */
            A_SHAInit(&ctx);

            /* feed the data to the cookie monster */
            A_SHAUpdate(&ctx, file_map, size.LowPart);

            /* cool, we don't need this anymore */
            UnmapViewOfFile(file_map);

            /* does the resulting SHA1 match with the provided one? */
            ret = VerifyIntegDigest(lpSHA1Hash, &ctx);
        }
        CloseHandle(map);
    }
    CloseHandle(file);
    return ret;
}
