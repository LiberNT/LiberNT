/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests decoding an otherName entry of an alternative name
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>

static const BYTE PermanentIdentifierValue[] =
{
    0x30, 0x16, 0x0c, 0x14, 'F', 'R', '-', '4', '9', '3', ' ', '5', '9', '0', ' ', '2', '0', '2', ' ',
    '0', '0', '0', '4', '7'
};

static const BYTE AltName[] =
{
    0x30, 0x35,
    0xa0, 0x24, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x08, 0x03,
    0xa0, 0x18, 0x30, 0x16, 0x0c, 0x14, 'F', 'R', '-', '4', '9', '3', ' ', '5', '9', '0', ' ', '2', '0', '2', ' ',
    '0', '0', '0', '4', '7',
    0x81, 0x0d, 'a', '@', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm'
};

START_TEST(OtherName)
{
    CERT_ALT_NAME_INFO *info = NULL;
    DWORD size = 0;
    BOOL ret;

    ret = CryptDecodeObjectEx(X509_ASN_ENCODING, X509_ALTERNATE_NAME, AltName, sizeof(AltName),
                              CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &size);
    ok(ret, "CryptDecodeObjectEx failed 0x%08lx\n", GetLastError());
    if (!ret)
        return;

    ok(info->cAltEntry == 2, "%lu entries\n", info->cAltEntry);
    if (info->cAltEntry == 2)
    {
        CERT_ALT_NAME_ENTRY *other = &info->rgAltEntry[0];

        ok(other->dwAltNameChoice == CERT_ALT_NAME_OTHER_NAME, "choice %lu\n", other->dwAltNameChoice);
        if (other->dwAltNameChoice == CERT_ALT_NAME_OTHER_NAME)
        {
            ok(!strcmp(other->pOtherName->pszObjId, "1.3.6.1.5.5.7.8.3"), "OID %s\n", other->pOtherName->pszObjId);
            ok(other->pOtherName->Value.cbData == sizeof(PermanentIdentifierValue), "value is %lu bytes\n",
               other->pOtherName->Value.cbData);
            ok(other->pOtherName->Value.cbData == sizeof(PermanentIdentifierValue) &&
               !memcmp(other->pOtherName->Value.pbData, PermanentIdentifierValue, sizeof(PermanentIdentifierValue)),
               "value differs\n");
            ok(((ULONG_PTR)other->pOtherName & (sizeof(void *) - 1)) == 0, "unaligned %p\n", other->pOtherName);
        }
        ok(info->rgAltEntry[1].dwAltNameChoice == CERT_ALT_NAME_RFC822_NAME, "choice %lu\n",
           info->rgAltEntry[1].dwAltNameChoice);
        if (info->rgAltEntry[1].dwAltNameChoice == CERT_ALT_NAME_RFC822_NAME)
            ok(!wcscmp(info->rgAltEntry[1].pwszRfc822Name, L"a@example.com"), "rfc822 %ls\n",
               info->rgAltEntry[1].pwszRfc822Name);
    }
    LocalFree(info);
}
