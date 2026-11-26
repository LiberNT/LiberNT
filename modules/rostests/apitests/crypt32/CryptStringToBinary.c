/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests CryptStringToBinary modifier flags
 * COPYRIGHT:   Copyright 2026 xor2003
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winnls.h>
#include <wincrypt.h>

static void
TestDecode(PCSTR String, DWORD Flags, BOOL Expected, DWORD ExpectedError)
{
    WCHAR WideString[32];
    BYTE Buffer[16];
    DWORD Size;
    BOOL Result;

    Size = sizeof(Buffer);
    SetLastError(0xdeadbeef);
    Result = CryptStringToBinaryA(String, 0, Flags, Buffer, &Size, NULL, NULL);
    ok(Result == Expected, "A '%s' %08lx returned %d, error %lu\n", String, Flags, Result, GetLastError());
    if (Result)
        ok(Size == 1 && Buffer[0] == 'A', "A '%s' %08lx decoded %lu bytes, first 0x%02x\n", String, Flags, Size, Buffer[0]);
    else
        ok(GetLastError() == ExpectedError, "A '%s' %08lx error %lu\n", String, Flags, GetLastError());

    MultiByteToWideChar(CP_ACP, 0, String, -1, WideString, _countof(WideString));
    Size = sizeof(Buffer);
    SetLastError(0xdeadbeef);
    Result = CryptStringToBinaryW(WideString, 0, Flags, Buffer, &Size, NULL, NULL);
    ok(Result == Expected, "W '%s' %08lx returned %d, error %lu\n", String, Flags, Result, GetLastError());
    if (Result)
        ok(Size == 1 && Buffer[0] == 'A', "W '%s' %08lx decoded %lu bytes, first 0x%02x\n", String, Flags, Size, Buffer[0]);
    else
        ok(GetLastError() == ExpectedError, "W '%s' %08lx error %lu\n", String, Flags, GetLastError());
}

START_TEST(CryptStringToBinary)
{
    TestDecode("QQ==", CRYPT_STRING_BASE64, TRUE, 0);
    TestDecode("QQ==", CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, TRUE, 0);
    TestDecode("QQ", CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, TRUE, 0);
    TestDecode("Q Q==", CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, TRUE, 0);
    TestDecode("QQ==\r\n", CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, TRUE, 0);
    TestDecode("QQ==x", CRYPT_STRING_BASE64, TRUE, 0);
    TestDecode("QQ==x", CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, FALSE, ERROR_INVALID_DATA);

    /* NOCR and NOCRLF only apply to encoding */
    TestDecode("QQ==", CRYPT_STRING_BASE64 | CRYPT_STRING_NOCR, FALSE, ERROR_INVALID_DATA);
    TestDecode("QQ==", CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, FALSE, ERROR_INVALID_DATA);
}
