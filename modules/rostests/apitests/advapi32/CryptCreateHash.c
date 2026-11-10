/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the algorithms CryptCreateHash accepts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <wincrypt.h>

static
VOID
CheckCreateHash(HCRYPTPROV Prov, ALG_ID Algorithm, DWORD ExpectedError, ULONG Line)
{
    HCRYPTHASH Hash = 0;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = CryptCreateHash(Prov, Algorithm, 0, 0, &Hash);
    if (ExpectedError == ERROR_SUCCESS)
    {
        ok_(__FILE__, Line)(Ret, "CryptCreateHash(0x%x) failed with 0x%lx\n", Algorithm, GetLastError());
        if (Ret)
        {
            ok_(__FILE__, Line)(CryptHashData(Hash, (const BYTE *)"x", 1, 0), "CryptHashData failed with 0x%lx\n", GetLastError());
            CryptDestroyHash(Hash);
        }
        return;
    }
    ok_(__FILE__, Line)(!Ret, "CryptCreateHash(0x%x) succeeded\n", Algorithm);
    ok_(__FILE__, Line)(GetLastError() == ExpectedError, "CryptCreateHash(0x%x) error 0x%lx, expected 0x%lx\n",
                        Algorithm, GetLastError(), ExpectedError);
    if (Ret)
        CryptDestroyHash(Hash);
}

START_TEST(CryptCreateHash)
{
    HCRYPTPROV Prov;

    if (!CryptAcquireContextW(&Prov, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        skip("No AES provider (0x%lx)\n", GetLastError());
        return;
    }
    CheckCreateHash(Prov, CALG_SHA_256, ERROR_SUCCESS, __LINE__);
    CheckCreateHash(Prov, CALG_RSA_KEYX, ERROR_NOT_SUPPORTED, __LINE__);
    CheckCreateHash(Prov, CALG_RSA_SIGN, ERROR_NOT_SUPPORTED, __LINE__);
    CheckCreateHash(Prov, CALG_AES_128, ERROR_NOT_SUPPORTED, __LINE__);
    CheckCreateHash(Prov, CALG_RC4, ERROR_NOT_SUPPORTED, __LINE__);
    CheckCreateHash(Prov, ALG_CLASS_HASH | ALG_TYPE_ANY | 0x99, NTE_BAD_ALGID, __LINE__);
    CryptReleaseContext(Prov, 0);

    if (!CryptAcquireContextW(&Prov, NULL, MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        skip("No RSA full provider (0x%lx)\n", GetLastError());
        return;
    }
    CheckCreateHash(Prov, CALG_SHA_256, NTE_BAD_ALGID, __LINE__);
    CheckCreateHash(Prov, CALG_RSA_KEYX, ERROR_NOT_SUPPORTED, __LINE__);
    CheckCreateHash(Prov, CALG_AES_128, ERROR_NOT_SUPPORTED, __LINE__);
    CheckCreateHash(Prov, CALG_RC4, ERROR_NOT_SUPPORTED, __LINE__);
    CheckCreateHash(Prov, ALG_CLASS_HASH | ALG_TYPE_ANY | 0x99, NTE_BAD_ALGID, __LINE__);
    CryptReleaseContext(Prov, 0);
}
