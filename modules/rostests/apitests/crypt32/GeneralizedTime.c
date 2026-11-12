/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests decoding GeneralizedTime values with fractional seconds
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>

static void CheckTime(const char *text, BOOL expected, WORD second, WORD milliseconds)
{
    BYTE encoded[32];
    FILETIME time;
    SYSTEMTIME st;
    DWORD size = sizeof(time), length = (DWORD)strlen(text);
    BOOL ret;

    encoded[0] = 0x18;
    encoded[1] = (BYTE)length;
    memcpy(encoded + 2, text, length);
    SetLastError(0xdeadbeef);
    ret = CryptDecodeObjectEx(X509_ASN_ENCODING, X509_CHOICE_OF_TIME, encoded, length + 2, 0, NULL, &time, &size);
    ok(ret == expected, "%s: decode returned %d, error 0x%08lx\n", text, ret, GetLastError());
    if (!ret)
    {
        ok(GetLastError() == CRYPT_E_ASN1_CORRUPT, "%s: error 0x%08lx\n", text, GetLastError());
        return;
    }
    if (!expected)
        return;
    ok(FileTimeToSystemTime(&time, &st), "%s: FileTimeToSystemTime failed\n", text);
    ok(st.wYear == 2026 && st.wMonth == 9 && st.wDay == 29 && st.wHour == 15 && st.wMinute == 1,
       "%s: decoded %04u-%02u-%02u %02u:%02u\n", text, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    ok(st.wSecond == second, "%s: second %u, expected %u\n", text, st.wSecond, second);
    ok(st.wMilliseconds == milliseconds, "%s: milliseconds %u, expected %u\n", text, st.wMilliseconds, milliseconds);
}

START_TEST(GeneralizedTime)
{
    CheckTime("20260929150142Z", TRUE, 42, 0);
    CheckTime("20260929150142.5Z", TRUE, 42, 500);
    CheckTime("20260929150142.56Z", TRUE, 42, 560);
    CheckTime("20260929150142.567Z", TRUE, 42, 567);
    CheckTime("20260929150142.5678Z", TRUE, 42, 567);
    CheckTime("20260929150142,25Z", TRUE, 42, 250);
    CheckTime("20260929150142.Z", FALSE, 0, 0);
}
