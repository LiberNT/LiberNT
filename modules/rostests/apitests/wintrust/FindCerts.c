/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the certificate chains FindCertsByIssuer returns and their buffer layout
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>

typedef HRESULT (WINAPI *PFN_FIND_CERTS)(PCERT_CHAIN, DWORD *, DWORD *, BYTE *, DWORD, LPCWSTR, DWORD);

#define TEST_CONTAINER L"LiberNT findcerts apitest"
#define ALIGN_PTR(x) (((x) + sizeof(DWORD_PTR) - 1) & ~(DWORD_PTR)(sizeof(DWORD_PTR) - 1))

static
VOID
CheckLayout(PCERT_CHAIN Chains, DWORD Size, PCCERT_CONTEXT Cert)
{
    BYTE *Base = (BYTE *)Chains;
    DWORD_PTR Container, Provider, Data;

    ok_long(Chains[0].cCerts, 1);
    ok((BYTE *)Chains[0].certs == Base + sizeof(CERT_CHAIN), "certs at offset %Id\n", (BYTE *)Chains[0].certs - Base);
    Container = sizeof(CERT_CHAIN) + sizeof(CERT_BLOB) + sizeof(CRYPT_KEY_PROV_INFO);
    Provider = Container + sizeof(TEST_CONTAINER);
    Data = ALIGN_PTR(Provider + sizeof(MS_ENHANCED_PROV_W));
    ok((BYTE *)Chains[0].keyLocatorInfo.pwszContainerName == Base + Container, "container at offset %Id\n",
       (BYTE *)Chains[0].keyLocatorInfo.pwszContainerName - Base);
    ok((BYTE *)Chains[0].keyLocatorInfo.pwszProvName == Base + Provider, "provider at offset %Id\n",
       (BYTE *)Chains[0].keyLocatorInfo.pwszProvName - Base);
    ok(!wcscmp(Chains[0].keyLocatorInfo.pwszContainerName, TEST_CONTAINER), "container %ls\n",
       Chains[0].keyLocatorInfo.pwszContainerName);
    ok(!wcscmp(Chains[0].keyLocatorInfo.pwszProvName, MS_ENHANCED_PROV_W), "provider %ls\n",
       Chains[0].keyLocatorInfo.pwszProvName);
    ok_long(Chains[0].keyLocatorInfo.dwProvType, PROV_RSA_FULL);
    ok_long(Chains[0].keyLocatorInfo.dwKeySpec, AT_SIGNATURE);
    ok_long(Chains[0].keyLocatorInfo.cProvParam, 0);
    ok(Chains[0].certs[0].pbData == Base + Data, "certificate at offset %Id\n", Chains[0].certs[0].pbData - Base);
    ok(Chains[0].certs[0].cbData == Cert->cbCertEncoded &&
       !memcmp(Chains[0].certs[0].pbData, Cert->pbCertEncoded, Cert->cbCertEncoded), "unexpected certificate\n");
    ok_long(Size, ALIGN_PTR(Data + Cert->cbCertEncoded));
}

static
VOID
TestIgnoredPurpose(HCRYPTPROV Provider, HCERTSTORE My, CRYPT_KEY_PROV_INFO *KeyInfo)
{
    PFN_FIND_CERTS FindCerts = (PFN_FIND_CERTS)GetProcAddress(GetModuleHandleW(L"wintrust.dll"), "FindCertsByIssuer");
    LPSTR Usages[] = { (LPSTR)szOID_PKIX_KP_CLIENT_AUTH };
    CERT_ENHKEY_USAGE Usage = { 1, Usages };
    BYTE NameData[128], UsageData[64];
    CERT_NAME_BLOB Name = { sizeof(NameData), NameData };
    CERT_EXTENSION Extension = { (LPSTR)szOID_ENHANCED_KEY_USAGE, FALSE, { sizeof(UsageData), UsageData } };
    CERT_EXTENSIONS Extensions = { 1, &Extension };
    PCCERT_CONTEXT Cert, Added = NULL;
    DWORD Size = 0, Count = 0;

    if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=LiberNT findcerts client auth", CERT_X500_NAME_STR, NULL, NameData,
                        &Name.cbData, NULL) ||
        !CryptEncodeObject(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &Usage, UsageData, &Extension.Value.cbData) ||
        !(Cert = CertCreateSelfSignCertificate(Provider, &Name, 0, KeyInfo, NULL, NULL, NULL, &Extensions)))
    {
        skip("Cannot create the client authentication certificate\n");
        return;
    }
    if (CertAddCertificateContextToStore(My, Cert, CERT_STORE_ADD_REPLACE_EXISTING, &Added))
    {
        ok_hex(FindCerts(NULL, &Size, &Count, Name.pbData, Name.cbData, L"1.3.6.1.5.5.7.3.3", AT_SIGNATURE), S_OK);
        ok_long(Count, 1);
        Size = 0;
        ok_hex(FindCerts(NULL, &Size, &Count, Name.pbData, Name.cbData, L"CodeSigning", AT_SIGNATURE), S_OK);
        ok_long(Count, 1);
        CertDeleteCertificateFromStore(Added);
    }
    CertFreeCertificateContext(Cert);
}

START_TEST(FindCerts)
{
    PFN_FIND_CERTS FindCerts = (PFN_FIND_CERTS)GetProcAddress(GetModuleHandleW(L"wintrust.dll"), "FindCertsByIssuer");
    CRYPT_KEY_PROV_INFO KeyInfo = { (LPWSTR)TEST_CONTAINER, (LPWSTR)MS_ENHANCED_PROV_W, PROV_RSA_FULL, 0, 0, NULL, AT_SIGNATURE };
    BYTE NameData[128], OtherData[128], Small[8];
    CERT_NAME_BLOB Name = { sizeof(NameData), NameData }, Other = { sizeof(OtherData), OtherData };
    PCCERT_CONTEXT Cert = NULL, Added;
    HCERTSTORE My = NULL;
    HCRYPTPROV Provider = 0;
    HCRYPTKEY Key;
    DWORD Size, Count, SmallSize;
    BYTE *Buffer;
    HRESULT Ret;

    ok(FindCerts != NULL, "FindCertsByIssuer is not exported\n");
    if (!FindCerts ||
        !CertStrToNameW(X509_ASN_ENCODING, L"CN=LiberNT findcerts apitest", CERT_X500_NAME_STR, NULL, NameData, &Name.cbData, NULL) ||
        !CertStrToNameW(X509_ASN_ENCODING, L"CN=LiberNT findcerts other", CERT_X500_NAME_STR, NULL, OtherData, &Other.cbData, NULL))
        return;

    CryptAcquireContextW(&Provider, TEST_CONTAINER, MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_DELETEKEYSET);
    if (CryptAcquireContextW(&Provider, TEST_CONTAINER, MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_NEWKEYSET) &&
        CryptGenKey(Provider, AT_SIGNATURE, 0, &Key))
    {
        CryptDestroyKey(Key);
        Cert = CertCreateSelfSignCertificate(Provider, &Name, 0, &KeyInfo, NULL, NULL, NULL, NULL);
    }
    My = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"My");
    if (!Cert || !My || !CertAddCertificateContextToStore(My, Cert, CERT_STORE_ADD_REPLACE_EXISTING, NULL))
    {
        skip("Cannot create the test certificate (%lu)\n", GetLastError());
    }
    else
    {
        Size = Count = 0xdead;
        ok_hex(FindCerts(NULL, &Size, &Count, Other.pbData, Other.cbData, NULL, AT_SIGNATURE), CRYPT_E_NOT_FOUND);
        ok_long(Size, 0);
        ok_long(Count, 0);

        Size = Count = 0xdead;
        ok_hex(FindCerts(NULL, &Size, &Count, Name.pbData, Name.cbData, NULL, AT_KEYEXCHANGE), CRYPT_E_NOT_FOUND);
        ok_long(Size, 0);
        ok_long(Count, 0);

        Size = 0;
        ok_hex(FindCerts(NULL, &Size, &Count, Name.pbData, Name.cbData, NULL, AT_SIGNATURE), S_OK);
        ok_long(Count, 1);
        Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size ? Size : 1);

        SmallSize = sizeof(Small);
        Count = 0;
        Ret = FindCerts((PCERT_CHAIN)Small, &SmallSize, &Count, Name.pbData, Name.cbData, NULL, AT_SIGNATURE);
        ok_hex(Ret, HRESULT_FROM_WIN32(ERROR_BAD_LENGTH));
        ok_long(SmallSize, Size);
        ok_long(Count, 1);

        if (Buffer)
        {
            ok_hex(FindCerts((PCERT_CHAIN)Buffer, &Size, &Count, Name.pbData, Name.cbData, NULL, AT_SIGNATURE), S_OK);
            ok_long(Count, 1);
            CheckLayout((PCERT_CHAIN)Buffer, Size, Cert);
            HeapFree(GetProcessHeap(), 0, Buffer);
        }

        Size = 0;
        ok_hex(FindCerts(NULL, &Size, &Count, Name.pbData, Name.cbData, L"1.3.6.1.5.5.7.3.3", AT_SIGNATURE), S_OK);
        ok_long(Count, 1);

        TestIgnoredPurpose(Provider, My, &KeyInfo);

        Added = CertFindCertificateInStore(My, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, Cert, NULL);
        if (Added)
            CertDeleteCertificateFromStore(Added);
    }

    if (My)
        CertCloseStore(My, 0);
    if (Cert)
        CertFreeCertificateContext(Cert);
    if (Provider)
        CryptReleaseContext(Provider, 0);
    CryptAcquireContextW(&Provider, TEST_CONTAINER, MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_DELETEKEYSET);
}
