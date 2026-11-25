/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests CryptStringToBinary modifier flags
 * COPYRIGHT:   Copyright 2026 xor2003
 */

#include <apitest.h>
#include <wincrypt.h>

static void
TestStrictBase64(void)
{
    static const char Plain[] = "QQ==";
    BYTE Buf[16];
    DWORD Size, Skip;

    /* Plain base64 decodes with and without the STRICT modifier */
    Size = sizeof(Buf);
    ok(CryptStringToBinaryA(Plain, 0, CRYPT_STRING_BASE64, Buf, &Size, &Skip, NULL),
       "CryptStringToBinaryA(BASE64) failed, error %lu\n", GetLastError());
    ok(Size == 1 && Buf[0] == 'A', "BASE64 decoded %lu bytes, first 0x%02x\n", Size, Buf[0]);

    Size = sizeof(Buf);
    SetLastError(0xdeadbeef);
    ok(CryptStringToBinaryA(Plain, 0, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                            Buf, &Size, &Skip, NULL),
       "CryptStringToBinaryA(BASE64|STRICT) failed, error %lu\n", GetLastError());
    ok(Size == 1 && Buf[0] == 'A', "BASE64|STRICT decoded %lu bytes, first 0x%02x\n", Size, Buf[0]);

}

static void
TestModifierFlagsAccepted(void)
{
    static const char Plain[] = "QQ==";
    BYTE Buf[16];
    DWORD Size, Skip;

    /* NOCR and NOCRLF are documented CryptStringToBinary modifiers and must
       not be reported as an invalid dwFlags value */
    Size = sizeof(Buf);
    ok(CryptStringToBinaryA(Plain, 0, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCR,
                            Buf, &Size, &Skip, NULL),
       "CryptStringToBinaryA(BASE64|NOCR) failed, error %lu\n", GetLastError());
    Size = sizeof(Buf);
    ok(CryptStringToBinaryA(Plain, 0, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            Buf, &Size, &Skip, NULL),
       "CryptStringToBinaryA(BASE64|NOCRLF) failed, error %lu\n", GetLastError());
}

START_TEST(CryptStringToBinary)
{
    TestStrictBase64();
    TestModifierFlagsAccepted();
}
