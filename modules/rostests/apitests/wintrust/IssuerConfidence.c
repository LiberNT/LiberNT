/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the confidence and error that the issuer certificate searches report
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>

typedef PCCERT_CONTEXT (WINAPI *PFN_TRUST_FIND_ISSUER)(PCCERT_CONTEXT, DWORD, DWORD, HCERTSTORE *, FILETIME *, DWORD *,
                                                       DWORD *, DWORD);
typedef PCCERT_CONTEXT (WINAPI *PFN_HELPER_FIND_ISSUER)(PCCERT_CONTEXT, DWORD, HCERTSTORE *, FILETIME *, DWORD,
                                                        DWORD *, DWORD *);

#define ISSUER_CONTAINER L"LiberNT confidence issuer"
#define LEAF_CONTAINER L"LiberNT confidence leaf"

static HCRYPTPROV IssuerKey, LeafKey;
static PCCERT_CONTEXT Issuer;
static BYTE Serial = 1;

static
HCRYPTPROV
CreateKey(const WCHAR *Container)
{
    HCRYPTPROV Provider = 0;
    HCRYPTKEY Key;

    CryptAcquireContextW(&Provider, Container, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    if (!CryptAcquireContextW(&Provider, Container, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET))
        return 0;
    if (!CryptGenKey(Provider, AT_SIGNATURE, 2048 << 16, &Key))
        return 0;
    CryptDestroyKey(Key);
    return Provider;
}

static
PCCERT_CONTEXT
CreateCert(const WCHAR *Subject, HCRYPTPROV Signer, HCRYPTPROV SubjectKey, CERT_EXTENSION *Extension,
           DWORD DaysBefore, DWORD DaysAfter)
{
    CRYPT_ALGORITHM_IDENTIFIER Algorithm = { (LPSTR)szOID_RSA_SHA256RSA };
    BYTE NameData[128], SerialData[4] = { 0x4c, 0x4e, 0x54, 0 }, *Encoded = NULL;
    CERT_NAME_BLOB Name = { sizeof(NameData), NameData };
    CERT_PUBLIC_KEY_INFO *PublicKey = NULL;
    PCCERT_CONTEXT Cert = NULL;
    ULARGE_INTEGER Time;
    FILETIME Now;
    CERT_INFO Info;
    DWORD Size = 0;

    if (!CertStrToNameW(X509_ASN_ENCODING, Subject, CERT_X500_NAME_STR, NULL, NameData, &Name.cbData, NULL) ||
        !CryptExportPublicKeyInfo(SubjectKey, AT_SIGNATURE, X509_ASN_ENCODING, NULL, &Size) ||
        !(PublicKey = HeapAlloc(GetProcessHeap(), 0, Size)) ||
        !CryptExportPublicKeyInfo(SubjectKey, AT_SIGNATURE, X509_ASN_ENCODING, PublicKey, &Size))
    {
        HeapFree(GetProcessHeap(), 0, PublicKey);
        return NULL;
    }
    SerialData[3] = Serial++;
    ZeroMemory(&Info, sizeof(Info));
    Info.dwVersion = CERT_V3;
    Info.SerialNumber.cbData = sizeof(SerialData);
    Info.SerialNumber.pbData = SerialData;
    Info.SignatureAlgorithm = Algorithm;
    Info.Issuer = Issuer ? Issuer->pCertInfo->Subject : Name;
    Info.Subject = Name;
    Info.SubjectPublicKeyInfo = *PublicKey;
    GetSystemTimeAsFileTime(&Now);
    Time.LowPart = Now.dwLowDateTime;
    Time.HighPart = Now.dwHighDateTime;
    Time.QuadPart -= DaysBefore * 864000000000ULL;
    Info.NotBefore.dwLowDateTime = Time.LowPart;
    Info.NotBefore.dwHighDateTime = Time.HighPart;
    Time.QuadPart += (DaysBefore + DaysAfter) * 864000000000ULL;
    Info.NotAfter.dwLowDateTime = Time.LowPart;
    Info.NotAfter.dwHighDateTime = Time.HighPart;
    Info.cExtension = Extension ? 1 : 0;
    Info.rgExtension = Extension;
    Size = 0;
    if (CryptSignAndEncodeCertificate(Signer, AT_SIGNATURE, X509_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &Info,
                                      &Algorithm, NULL, NULL, &Size) &&
        (Encoded = HeapAlloc(GetProcessHeap(), 0, Size)) &&
        CryptSignAndEncodeCertificate(Signer, AT_SIGNATURE, X509_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &Info,
                                      &Algorithm, NULL, Encoded, &Size))
    {
        Cert = CertCreateCertificateContext(X509_ASN_ENCODING, Encoded, Size);
    }
    HeapFree(GetProcessHeap(), 0, Encoded);
    HeapFree(GetProcessHeap(), 0, PublicKey);
    return Cert;
}

static
VOID
CheckConfidence(const char *Label, PCCERT_CONTEXT Leaf, DWORD ExpectedConfidence, DWORD ExpectedError)
{
    PFN_HELPER_FIND_ISSUER Helper = (PFN_HELPER_FIND_ISSUER)GetProcAddress(GetModuleHandleW(L"wintrust.dll"),
                                                                           "WTHelperCertFindIssuerCertificate");
    PFN_TRUST_FIND_ISSUER Trust = (PFN_TRUST_FIND_ISSUER)GetProcAddress(GetModuleHandleW(L"wintrust.dll"),
                                                                        "TrustFindIssuerCertificate");
    HCERTSTORE Store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    DWORD Confidence = 0xdead, Error = 0xdead;
    PCCERT_CONTEXT Found;
    FILETIME Now;

    if (!Leaf || !Store || !Helper || !Trust ||
        !CertAddCertificateContextToStore(Store, Issuer, CERT_STORE_ADD_ALWAYS, NULL))
    {
        skip("%s: cannot set up the search\n", Label);
        if (Store)
            CertCloseStore(Store, 0);
        return;
    }
    GetSystemTimeAsFileTime(&Now);
    Found = Helper(Leaf, 1, &Store, &Now, X509_ASN_ENCODING, &Confidence, &Error);
    ok(Found != NULL, "%s: issuer not found\n", Label);
    ok(Confidence == ExpectedConfidence, "%s: confidence %#lx, expected %#lx\n", Label, Confidence, ExpectedConfidence);
    ok(Error == ExpectedError, "%s: error %#lx, expected %#lx\n", Label, Error, ExpectedError);
    if (Found)
        CertFreeCertificateContext(Found);

    Confidence = Error = 0xdead;
    Found = Trust(Leaf, X509_ASN_ENCODING, 1, &Store, &Now, &Confidence, &Error, 0);
    ok(Found != NULL, "%s: TrustFindIssuerCertificate found nothing\n", Label);
    ok(Confidence == ExpectedConfidence, "%s: TrustFindIssuerCertificate confidence %#lx\n", Label, Confidence);
    ok(Error == ExpectedError, "%s: TrustFindIssuerCertificate error %#lx\n", Label, Error);
    if (Found)
        CertFreeCertificateContext(Found);
    CertFreeCertificateContext(Leaf);
    CertCloseStore(Store, 0);
}

START_TEST(IssuerConfidence)
{
    static const DWORD Base = CERT_CONFIDENCE_SIG | CERT_CONFIDENCE_TIME | CERT_CONFIDENCE_TIMENEST;
    BYTE Aki1Data[256], Aki2Data[512], KeyIdData[256];
    DWORD Aki1Size = sizeof(Aki1Data), Aki2Size = sizeof(Aki2Data), KeyIdSize = sizeof(KeyIdData);
    CERT_EXTENSION Aki1 = { (LPSTR)szOID_AUTHORITY_KEY_IDENTIFIER, FALSE, { 0, Aki1Data } };
    CERT_EXTENSION Aki2 = { (LPSTR)szOID_AUTHORITY_KEY_IDENTIFIER2, FALSE, { 0, Aki2Data } };
    CERT_EXTENSION KeyId = { (LPSTR)szOID_AUTHORITY_KEY_IDENTIFIER, FALSE, { 0, KeyIdData } };
    CERT_AUTHORITY_KEY_ID_INFO KeyInfo;
    CERT_AUTHORITY_KEY_ID2_INFO KeyInfo2;
    CERT_ALT_NAME_ENTRY Entry;
    BYTE KeyIdBytes[20];

    IssuerKey = CreateKey(ISSUER_CONTAINER);
    LeafKey = CreateKey(LEAF_CONTAINER);
    Issuer = IssuerKey && LeafKey ? CreateCert(L"CN=LiberNT confidence issuer", IssuerKey, IssuerKey, NULL, 10, 3650)
                                  : NULL;
    if (!Issuer)
    {
        skip("Cannot create the issuer certificate (%lu)\n", GetLastError());
        return;
    }

    ZeroMemory(&KeyInfo, sizeof(KeyInfo));
    KeyInfo.CertIssuer = Issuer->pCertInfo->Issuer;
    KeyInfo.CertSerialNumber = Issuer->pCertInfo->SerialNumber;
    ZeroMemory(&KeyInfo2, sizeof(KeyInfo2));
    Entry.dwAltNameChoice = CERT_ALT_NAME_DIRECTORY_NAME;
    Entry.DirectoryName = Issuer->pCertInfo->Issuer;
    KeyInfo2.AuthorityCertIssuer.cAltEntry = 1;
    KeyInfo2.AuthorityCertIssuer.rgAltEntry = &Entry;
    KeyInfo2.AuthorityCertSerialNumber = Issuer->pCertInfo->SerialNumber;
    if (!CryptEncodeObject(X509_ASN_ENCODING, X509_AUTHORITY_KEY_ID, &KeyInfo, Aki1Data, &Aki1Size) ||
        !CryptEncodeObject(X509_ASN_ENCODING, X509_AUTHORITY_KEY_ID2, &KeyInfo2, Aki2Data, &Aki2Size))
    {
        skip("Cannot encode the authority key identifiers\n");
        return;
    }
    Aki1.Value.cbData = Aki1Size;
    Aki2.Value.cbData = Aki2Size;
    ZeroMemory(&KeyInfo, sizeof(KeyInfo));
    FillMemory(KeyIdBytes, sizeof(KeyIdBytes), 0x5a);
    KeyInfo.KeyId.cbData = sizeof(KeyIdBytes);
    KeyInfo.KeyId.pbData = KeyIdBytes;
    CryptEncodeObject(X509_ASN_ENCODING, X509_AUTHORITY_KEY_ID, &KeyInfo, KeyIdData, &KeyIdSize);
    KeyId.Value.cbData = KeyIdSize;

    CheckConfidence("no identifier", CreateCert(L"CN=LiberNT leaf", IssuerKey, LeafKey, NULL, 1, 365), Base, 0);
    CheckConfidence("issuer and serial", CreateCert(L"CN=LiberNT leaf", IssuerKey, LeafKey, &Aki1, 1, 365),
                    Base | CERT_CONFIDENCE_AUTHIDEXT | CERT_CONFIDENCE_HYGIENE, 0);
    CheckConfidence("issuer and serial (v2)", CreateCert(L"CN=LiberNT leaf", IssuerKey, LeafKey, &Aki2, 1, 365),
                    Base | CERT_CONFIDENCE_AUTHIDEXT | CERT_CONFIDENCE_HYGIENE, 0);
    CheckConfidence("key identifier only", CreateCert(L"CN=LiberNT leaf", IssuerKey, LeafKey, &KeyId, 1, 365), Base, 0);
    CheckConfidence("bad signature", CreateCert(L"CN=LiberNT leaf", LeafKey, LeafKey, &Aki1, 1, 365),
                    CERT_CONFIDENCE_TIME | CERT_CONFIDENCE_TIMENEST | CERT_CONFIDENCE_AUTHIDEXT, TRUST_E_CERT_SIGNATURE);
    CheckConfidence("not nested", CreateCert(L"CN=LiberNT leaf", IssuerKey, LeafKey, &Aki1, 20, 365),
                    CERT_CONFIDENCE_SIG | CERT_CONFIDENCE_TIME | CERT_CONFIDENCE_AUTHIDEXT | CERT_CONFIDENCE_HYGIENE, 0);

    {
        PFN_TRUST_FIND_ISSUER Trust = (PFN_TRUST_FIND_ISSUER)GetProcAddress(GetModuleHandleW(L"wintrust.dll"),
                                                                            "TrustFindIssuerCertificate");
        HCERTSTORE Store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
        DWORD Confidence = 0xdead, Error = 0xdead, Flags[] = { 1, 2, 0x10, 0xffffffff }, i;
        FILETIME Now;

        GetSystemTimeAsFileTime(&Now);
        for (i = 0; Trust && i < RTL_NUMBER_OF(Flags); i++)
        {
            SetLastError(0xdeadbeef);
            ok(!Trust(Issuer, X509_ASN_ENCODING, 1, &Store, &Now, &Confidence, &Error, Flags[i]),
               "flags %#lx accepted\n", Flags[i]);
            ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
            ok(Confidence == 0xdead && Error == 0xdead, "flags %#lx wrote the outputs\n", Flags[i]);
        }
        CertCloseStore(Store, 0);
    }

    CertFreeCertificateContext(Issuer);
    CryptReleaseContext(IssuerKey, 0);
    CryptReleaseContext(LeafKey, 0);
    CryptAcquireContextW(&IssuerKey, ISSUER_CONTAINER, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    CryptAcquireContextW(&LeafKey, LEAF_CONTAINER, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
}
