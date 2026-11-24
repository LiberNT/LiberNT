/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which signing statement types SignerSign accepts for a certificate
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <strsafe.h>
#include <wine/mssign.h>

#ifndef SIGNER_SUBJECT_FILE
#define SIGNER_SUBJECT_FILE 1
#endif

typedef HRESULT (WINAPI *PFN_SIGNER_SIGN)(SIGNER_SUBJECT_INFO *, SIGNER_CERT *, SIGNER_SIGNATURE_INFO *,
                                          SIGNER_PROVIDER_INFO *, const WCHAR *, CRYPT_ATTRIBUTES *, void *);

typedef enum
{
    NoExtensions,
    UsageOnly,
    IndividualPolicy,
    CommercialPolicy,
} CERT_KIND;

static PFN_SIGNER_SIGN pSignerSign;
static WCHAR Container[64];

static BOOL
Encode(LPCSTR Type, const void *Info, CRYPT_DATA_BLOB *Blob)
{
    Blob->pbData = NULL;
    Blob->cbData = 0;
    return CryptEncodeObjectEx(X509_ASN_ENCODING, Type, Info, CRYPT_ENCODE_ALLOC_FLAG, NULL, &Blob->pbData, &Blob->cbData);
}

static PCCERT_CONTEXT
CreateCertificate(HCRYPTPROV Provider, CERT_KIND Kind)
{
    static BYTE Usage = CERT_DIGITAL_SIGNATURE_KEY_USAGE | CERT_DATA_ENCIPHERMENT_KEY_USAGE;
    static LPSTR Individual = SPC_INDIVIDUAL_SP_KEY_PURPOSE_OBJID;
    static LPSTR Commercial = SPC_COMMERCIAL_SP_KEY_PURPOSE_OBJID;
    static LPSTR CodeSigning = szOID_PKIX_KP_CODE_SIGNING;
    CERT_POLICY_ID Policy = { 1, Kind == CommercialPolicy ? &Commercial : &Individual };
    CERT_KEY_USAGE_RESTRICTION_INFO Restriction = { 0 };
    CERT_ENHKEY_USAGE EnhancedUsage = { 1, &CodeSigning };
    CERT_EXTENSION Extension[2] = { { 0 } };
    CERT_EXTENSIONS Extensions = { 0, Extension };
    BYTE NameData[256];
    CERT_NAME_BLOB Name = { sizeof(NameData), NameData };
    PCCERT_CONTEXT Certificate;

    if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=LiberNT SignerSign test", CERT_X500_NAME_STR, NULL, NameData,
                        &Name.cbData, NULL))
        return NULL;

    if (Kind != NoExtensions)
    {
        Restriction.RestrictedKeyUsage.cbData = 1;
        Restriction.RestrictedKeyUsage.pbData = &Usage;
        if (Kind == IndividualPolicy || Kind == CommercialPolicy)
        {
            Restriction.cCertPolicyId = 1;
            Restriction.rgCertPolicyId = &Policy;
        }
        Extension[0].pszObjId = szOID_KEY_USAGE_RESTRICTION;
        Extension[0].fCritical = TRUE;
        Extension[1].pszObjId = szOID_ENHANCED_KEY_USAGE;
        Extension[1].fCritical = TRUE;
        if (!Encode(X509_KEY_USAGE_RESTRICTION, &Restriction, &Extension[0].Value) ||
            !Encode(X509_ENHANCED_KEY_USAGE, &EnhancedUsage, &Extension[1].Value))
            return NULL;
        Extensions.cExtension = 2;
    }

    Certificate = CertCreateSelfSignCertificate(Provider, &Name, 0, NULL, NULL, NULL, NULL, &Extensions);
    LocalFree(Extension[0].Value.pbData);
    LocalFree(Extension[1].Value.pbData);
    return Certificate;
}

static HRESULT
SignCopy(PCCERT_CONTEXT Certificate, BOOL Commercial, BOOL Individual, BOOL *Embedded)
{
    WCHAR Self[MAX_PATH], Directory[MAX_PATH], Path[MAX_PATH];
    DWORD Index = 0, Encoding, ContentType, FormatType;
    SIGNER_FILE_INFO File = { sizeof(File), Path, NULL };
    SIGNER_SUBJECT_INFO Subject = { sizeof(Subject), &Index, SIGNER_SUBJECT_FILE };
    SIGNER_CERT_STORE_INFO Store = { sizeof(Store), Certificate, SIGNER_CERT_POLICY_CHAIN, NULL };
    SIGNER_CERT Signer = { sizeof(Signer), SIGNER_CERT_STORE };
    SIGNER_ATTR_AUTHCODE Authcode = { sizeof(Authcode), Commercial, Individual, L"LiberNT", NULL };
    SIGNER_SIGNATURE_INFO Signature = { sizeof(Signature), CALG_SHA1, SIGNER_AUTHCODE_ATTR };
    HRESULT hr;

    Subject.pSignerFileInfo = &File;
    Signer.pCertStoreInfo = &Store;
    Signature.pAttrAuthcode = &Authcode;
    *Embedded = FALSE;

    GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
    GetTempPathW(ARRAYSIZE(Directory), Directory);
    GetTempFileNameW(Directory, L"sgn", 0, Path);
    if (!CopyFileW(Self, Path, FALSE))
        return HRESULT_FROM_WIN32(GetLastError());

    hr = pSignerSign(&Subject, &Signer, &Signature, NULL, NULL, NULL, NULL);
    if (SUCCEEDED(hr))
    {
        *Embedded = CryptQueryObject(CERT_QUERY_OBJECT_FILE, Path, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                     CERT_QUERY_FORMAT_FLAG_BINARY, 0, &Encoding, &ContentType, &FormatType, NULL, NULL, NULL);
    }
    DeleteFileW(Path);
    return hr;
}

static void
TestStatement(PCCERT_CONTEXT Certificate, PCSTR Kind, BOOL Commercial, BOOL Individual, HRESULT Expected)
{
    BOOL Embedded;
    HRESULT hr;

    hr = SignCopy(Certificate, Commercial, Individual, &Embedded);
    ok(hr == Expected, "%s, commercial %d, individual %d: hr 0x%08lx, expected 0x%08lx\n", Kind, Commercial, Individual, hr, Expected);
    ok(Embedded == SUCCEEDED(Expected), "%s, commercial %d, individual %d: embedded %d\n", Kind, Commercial, Individual, Embedded);
}

START_TEST(SignerSign)
{
    static const PCSTR Kinds[] = { "no extensions", "usage only", "individual policy", "commercial policy" };
    HCRYPTPROV Provider = 0;
    HCRYPTKEY Key = 0;
    PCCERT_CONTEXT Certificate;
    HMODULE Module;
    int Kind;

    Module = LoadLibraryW(L"mssign32.dll");
    pSignerSign = Module ? (PFN_SIGNER_SIGN)GetProcAddress(Module, "SignerSign") : NULL;
    if (!pSignerSign)
    {
        skip("SignerSign is not available\n");
        return;
    }

    StringCchPrintfW(Container, ARRAYSIZE(Container), L"LiberNT SignerSign test %lu", GetCurrentProcessId());
    ok(CryptAcquireContextW(&Provider, Container, NULL, PROV_RSA_FULL, CRYPT_NEWKEYSET),
       "CryptAcquireContextW failed: %lu\n", GetLastError());
    if (!Provider)
        return;
    ok(CryptGenKey(Provider, AT_SIGNATURE, (2048 << 16) | CRYPT_EXPORTABLE, &Key), "CryptGenKey failed: %lu\n", GetLastError());

    for (Kind = NoExtensions; Kind <= CommercialPolicy; Kind++)
    {
        Certificate = CreateCertificate(Provider, (CERT_KIND)Kind);
        ok(Certificate != NULL, "%s: CertCreateSelfSignCertificate failed: %lu\n", Kinds[Kind], GetLastError());
        if (!Certificate)
            continue;
        TestStatement(Certificate, Kinds[Kind], FALSE, TRUE, Kind == CommercialPolicy ? TYPE_E_TYPEMISMATCH : S_OK);
        TestStatement(Certificate, Kinds[Kind], TRUE, FALSE, Kind == CommercialPolicy ? S_OK : TYPE_E_TYPEMISMATCH);
        TestStatement(Certificate, Kinds[Kind], TRUE, TRUE, E_INVALIDARG);
        TestStatement(Certificate, Kinds[Kind], FALSE, FALSE, S_OK);
        CertFreeCertificateContext(Certificate);
    }

    if (Key)
        CryptDestroyKey(Key);
    CryptReleaseContext(Provider, 0);
    CryptAcquireContextW(&Provider, Container, NULL, PROV_RSA_FULL, CRYPT_DELETEKEYSET);
    FreeLibrary(Module);
}
