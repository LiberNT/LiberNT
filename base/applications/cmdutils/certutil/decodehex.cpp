/*
 * PROJECT:     LiberNT certutil
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CertUtil decodehex implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <wincrypt.h>
#include <stdlib.h>

BOOL decode_hex(LPCWSTR InFile, LPCWSTR OutFile)
{
    LARGE_INTEGER Size;
    HANDLE hIn, hOut;
    char *Text = NULL;
    BYTE *Data = NULL;
    DWORD Read = 0, Length = 0, Written;
    BOOL bSuccess = FALSE;

    if (!OutFile)
    {
        ConPrintf(StdOut, L"CertUtil: -decodehex expected 2 arguments, got 1\n");
        return FALSE;
    }

    hIn = CreateFileW(InFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hIn == INVALID_HANDLE_VALUE)
    {
        ConPrintf(StdOut, L"CertUtil: -decodehex command failed: %d\n", GetLastError());
        return FALSE;
    }

    if (GetFileSizeEx(hIn, &Size) && Size.QuadPart < MAXDWORD)
    {
        Text = (char *)malloc(Size.LowPart + 1);
        if (Text && ReadFile(hIn, Text, Size.LowPart, &Read, NULL) &&
            CryptStringToBinaryA(Text, Read, CRYPT_STRING_HEX, NULL, &Length, NULL, NULL))
        {
            Data = (BYTE *)malloc(Length ? Length : 1);
            bSuccess = Data && CryptStringToBinaryA(Text, Read, CRYPT_STRING_HEX, Data, &Length, NULL, NULL);
        }
    }
    CloseHandle(hIn);

    if (bSuccess)
    {
        hOut = CreateFileW(OutFile, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        bSuccess = (hOut != INVALID_HANDLE_VALUE);
        if (bSuccess)
        {
            bSuccess = WriteFile(hOut, Data, Length, &Written, NULL) && Written == Length;
            CloseHandle(hOut);
        }
    }

    if (bSuccess)
    {
        ConPrintf(StdOut, L"Input Length = %lu\n", Read);
        ConPrintf(StdOut, L"Output Length = %lu\n", Length);
    }
    else
    {
        ConPrintf(StdOut, L"CertUtil: -decodehex command failed: %d\n", GetLastError());
    }

    free(Data);
    free(Text);
    return bSuccess;
}
