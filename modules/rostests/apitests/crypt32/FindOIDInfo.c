/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the CNG algorithm names of the hash algorithm OIDs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define CRYPT_OID_INFO_HAS_EXTRA_FIELDS
#include <apitest.h>
#include <wincrypt.h>

static void CheckHash(LPCSTR Oid, LPCWSTR Name)
{
    PCCRYPT_OID_INFO Info = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, (void *)Oid, CRYPT_HASH_ALG_OID_GROUP_ID);

    ok(Info != NULL, "%s: no OID info\n", Oid);
    if (!Info)
        return;
    ok(Info->pwszCNGAlgid && !wcscmp(Info->pwszCNGAlgid, Name), "%s: CNG algorithm %ls, expected %ls\n", Oid,
       Info->pwszCNGAlgid ? Info->pwszCNGAlgid : L"(null)", Name);
}

START_TEST(FindOIDInfo)
{
    CheckHash(szOID_OIWSEC_sha1, L"SHA1");
    CheckHash(szOID_RSA_MD5, L"MD5");
    CheckHash(szOID_RSA_MD4, L"MD4");
    CheckHash(szOID_RSA_MD2, L"MD2");
    CheckHash(szOID_NIST_sha256, L"SHA256");
    CheckHash(szOID_NIST_sha384, L"SHA384");
    CheckHash(szOID_NIST_sha512, L"SHA512");
}
