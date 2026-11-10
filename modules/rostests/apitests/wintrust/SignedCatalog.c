/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test driver verification and trust helpers on catalogs and images signed at run time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mssip.h>
#include <mscat.h>
#include <stdio.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

typedef PCCERT_CONTEXT (WINAPI *PFN_FIND_ISSUER)(PCCERT_CONTEXT, DWORD, HCERTSTORE *, FILETIME *, DWORD, DWORD *, DWORD *);
typedef LONG (WINAPI *PFN_FILE_HASH)(LPCWSTR, DWORD, PVOID, BYTE *, DWORD *, ALG_ID *);
typedef HRESULT (WINAPI *PFN_FIND_CERTS)(PCERT_CHAIN, DWORD *, DWORD *, BYTE *, DWORD, LPCWSTR, DWORD);

#define TEST_CONTAINER L"LiberNT wintrust apitest"
#define TEST_OS_ATTR L"2:10.0,2:6.3,2:6.1"

static const GUID PeSubject = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };

static HCRYPTPROV Provider;
static PCCERT_CONTEXT Root, Leaf, Tsa, CodeSigner, RogueTsa, SigningCert;
static HCERTSTORE MachineRoot;
static WCHAR TempDir[MAX_PATH], CatalogPath[MAX_PATH], MemberPath[MAX_PATH], OtherPath[MAX_PATH], ImagePath[MAX_PATH];

static
BOOL
WriteBytes(const WCHAR *Path, const void *Data, DWORD Size)
{
    HANDLE File;
    DWORD Written;
    BOOL Ret;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(File, Data, Size, &Written, NULL) && Written == Size;
    CloseHandle(File);
    return Ret;
}

static
BOOL
ReadBytes(const WCHAR *Path, BYTE **Data, DWORD *Size)
{
    HANDLE File;
    DWORD Read;
    BOOL Ret = FALSE;

    *Data = NULL;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    *Size = GetFileSize(File, NULL);
    if (*Size != INVALID_FILE_SIZE && (*Data = HeapAlloc(GetProcessHeap(), 0, *Size)))
        Ret = ReadFile(File, *Data, *Size, &Read, NULL) && Read == *Size;
    CloseHandle(File);
    return Ret;
}

static
PCCERT_CONTEXT
CreateCertificate(const WCHAR *Subject, PCCERT_CONTEXT Issuer, BOOL CertificateAuthority, LPCSTR Usage)
{
    CERT_BASIC_CONSTRAINTS2_INFO Constraints = { CertificateAuthority, FALSE, 0 };
    CERT_ENHKEY_USAGE UsageInfo = { 1, (LPSTR *)&Usage };
    BYTE NameData[128], SerialData[8], ConstraintsData[16], UsageData[64], *Encoded = NULL;
    CERT_NAME_BLOB Name = { sizeof(NameData), NameData };
    CRYPT_ALGORITHM_IDENTIFIER Algorithm = { (LPSTR)szOID_RSA_SHA256RSA };
    CRYPT_KEY_PROV_INFO KeyInfo = { (LPWSTR)TEST_CONTAINER, (LPWSTR)MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, 0, 0, NULL, AT_SIGNATURE };
    CERT_EXTENSION Extension[2] = { { (LPSTR)szOID_BASIC_CONSTRAINTS2, TRUE, { sizeof(ConstraintsData), ConstraintsData } },
                                    { (LPSTR)szOID_ENHANCED_KEY_USAGE, FALSE, { sizeof(UsageData), UsageData } } };
    CERT_EXTENSIONS Extensions = { Usage ? 2 : 1, Extension };
    CERT_PUBLIC_KEY_INFO *PublicKey = NULL;
    PCCERT_CONTEXT Cert = NULL;
    static BYTE Sequence;
    SYSTEMTIME Time;
    CERT_INFO Info;
    DWORD Size = 0;

    if (!CertStrToNameW(X509_ASN_ENCODING, Subject, CERT_X500_NAME_STR, NULL, NameData, &Name.cbData, NULL) ||
        !CryptEncodeObject(X509_ASN_ENCODING, X509_BASIC_CONSTRAINTS2, &Constraints, ConstraintsData,
                           &Extension[0].Value.cbData) ||
        (Usage && !CryptEncodeObject(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &UsageInfo, UsageData,
                                     &Extension[1].Value.cbData)))
        return NULL;

    if (!Issuer)
    {
        SYSTEMTIME Start, End;
        FILETIME StartFile, EndFile;

        GetSystemTimeAsFileTime(&StartFile);
        ((ULARGE_INTEGER *)&StartFile)->QuadPart -= 2ULL * 24 * 3600 * 10000000;
        EndFile = StartFile;
        ((ULARGE_INTEGER *)&EndFile)->QuadPart += 700ULL * 24 * 3600 * 10000000;
        FileTimeToSystemTime(&StartFile, &Start);
        FileTimeToSystemTime(&EndFile, &End);
        return CertCreateSelfSignCertificate(Provider, &Name, 0, &KeyInfo, &Algorithm, &Start, &End, &Extensions);
    }

    ZeroMemory(&Info, sizeof(Info));
    Info.dwVersion = CERT_V3;
    GetSystemTime(&Time);
    SerialData[0] = 0x4c;
    memcpy(SerialData + 1, &Time.wMilliseconds, sizeof(Time.wMilliseconds));
    memcpy(SerialData + 3, &Time.wSecond, sizeof(Time.wSecond));
    SerialData[5] = 0x54;
    SerialData[6] = 0x01;
    SerialData[7] = ++Sequence;
    Info.SerialNumber.cbData = sizeof(SerialData);
    Info.SerialNumber.pbData = SerialData;
    Info.SignatureAlgorithm = Algorithm;
    Info.Issuer = Issuer->pCertInfo->Subject;
    GetSystemTimeAsFileTime(&Info.NotBefore);
    ((ULARGE_INTEGER *)&Info.NotBefore)->QuadPart -= 24ULL * 3600 * 10000000;
    Info.NotAfter = Info.NotBefore;
    ((ULARGE_INTEGER *)&Info.NotAfter)->QuadPart += 300ULL * 24 * 3600 * 10000000;
    Info.Subject = Name;
    Info.cExtension = Extensions.cExtension;
    Info.rgExtension = Extension;
    if (CryptExportPublicKeyInfo(Provider, AT_SIGNATURE, X509_ASN_ENCODING, NULL, &Size) &&
        (PublicKey = HeapAlloc(GetProcessHeap(), 0, Size)) &&
        CryptExportPublicKeyInfo(Provider, AT_SIGNATURE, X509_ASN_ENCODING, PublicKey, &Size))
    {
        Info.SubjectPublicKeyInfo = *PublicKey;
        Size = 0;
        if (CryptSignAndEncodeCertificate(Provider, AT_SIGNATURE, X509_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &Info,
                                          &Algorithm, NULL, NULL, &Size) &&
            (Encoded = HeapAlloc(GetProcessHeap(), 0, Size)) &&
            CryptSignAndEncodeCertificate(Provider, AT_SIGNATURE, X509_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &Info,
                                          &Algorithm, NULL, Encoded, &Size))
        {
            Cert = CertCreateCertificateContext(X509_ASN_ENCODING, Encoded, Size);
        }
    }
    if (Cert)
        CertSetCertificateContextProperty(Cert, CERT_KEY_PROV_INFO_PROP_ID, 0, &KeyInfo);
    HeapFree(GetProcessHeap(), 0, Encoded);
    HeapFree(GetProcessHeap(), 0, PublicKey);
    return Cert;
}

static
BOOL
SignContent(LPCSTR ContentType, const BYTE *Content, DWORD ContentSize, PCCERT_CONTEXT Extra, BYTE **Signed,
            DWORD *SignedSize)
{
    static const BYTE OpusInfo[] = { 0x30, 0x00 };
    CRYPT_ATTR_BLOB OpusBlob = { sizeof(OpusInfo), (BYTE *)OpusInfo };
    CRYPT_ATTRIBUTE Attribute = { (LPSTR)SPC_SP_OPUS_INFO_OBJID, 1, &OpusBlob };
    CMSG_SIGNER_ENCODE_INFO Signer;
    CMSG_SIGNED_ENCODE_INFO SignedInfo;
    CERT_BLOB Certs[3];
    HCRYPTMSG Msg;
    BOOL Ret;

    ZeroMemory(&Signer, sizeof(Signer));
    Signer.cbSize = sizeof(Signer);
    Signer.pCertInfo = (SigningCert ? SigningCert : Leaf)->pCertInfo;
    Signer.hCryptProv = Provider;
    Signer.dwKeySpec = AT_SIGNATURE;
    Signer.HashAlgorithm.pszObjId = (LPSTR)szOID_NIST_sha256;
    Signer.cAuthAttr = 1;
    Signer.rgAuthAttr = &Attribute;
    Certs[0].cbData = (SigningCert ? SigningCert : Leaf)->cbCertEncoded;
    Certs[0].pbData = (SigningCert ? SigningCert : Leaf)->pbCertEncoded;
    Certs[1].cbData = Root->cbCertEncoded;
    Certs[1].pbData = Root->pbCertEncoded;
    if (Extra)
    {
        Certs[2].cbData = Extra->cbCertEncoded;
        Certs[2].pbData = Extra->pbCertEncoded;
    }
    ZeroMemory(&SignedInfo, sizeof(SignedInfo));
    SignedInfo.cbSize = sizeof(SignedInfo);
    SignedInfo.cSigners = 1;
    SignedInfo.rgSigners = &Signer;
    SignedInfo.cCertEncoded = Extra ? 3 : 2;
    SignedInfo.rgCertEncoded = Certs;

    *Signed = NULL;
    Msg = CryptMsgOpenToEncode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CMSG_SIGNED, &SignedInfo,
                               (LPSTR)ContentType, NULL);
    if (!Msg)
        return FALSE;
    *SignedSize = 0;
    Ret = CryptMsgUpdate(Msg, Content, ContentSize, TRUE) &&
          CryptMsgGetParam(Msg, CMSG_CONTENT_PARAM, 0, NULL, SignedSize) &&
          (*Signed = HeapAlloc(GetProcessHeap(), 0, *SignedSize)) &&
          CryptMsgGetParam(Msg, CMSG_CONTENT_PARAM, 0, *Signed, SignedSize);
    CryptMsgClose(Msg);
    return Ret;
}

static
BOOL
CreateSignedCatalog(VOID)
{
    WCHAR Cdf[MAX_PATH], Unsigned[MAX_PATH];
    char Text[1024];
    int Length;
    CRYPTCATCDF *CatalogCdf;
    CRYPTCATMEMBER *Member = NULL;
    CRYPTCATATTRIBUTE *Attribute = NULL;
    LPWSTR Tag = NULL;
    BYTE *Data = NULL, *Content = NULL, *Signed = NULL;
    DWORD DataSize = 0, ContentSize = 0, SignedSize = 0;
    HCRYPTMSG Msg;
    BOOL Ret = FALSE;

    swprintf(Cdf, RTL_NUMBER_OF(Cdf), L"%lslntwtsig.cdf", TempDir);
    swprintf(Unsigned, RTL_NUMBER_OF(Unsigned), L"%lslntwtsig-unsigned.cat", TempDir);
    Length = snprintf(Text, sizeof(Text), "[CatalogHeader]\r\nName=%ls\r\nResultDir=%ls\r\nPublicVersion=0x0000001\r\nEncodingType=0x00010001\r\n"
                  "CATATTR1=0x10010001:OSAttr:%ls\r\n[CatalogFiles]\r\nmember=%ls\r\nmemberATTR1=0x10010001:OSAttr:%ls\r\n",
            L"lntwtsig-unsigned.cat", TempDir, TEST_OS_ATTR, MemberPath, TEST_OS_ATTR);
    ok(Length > 0 && Length < (int)sizeof(Text), "CDF text length %d\n", Length);
    if (Length <= 0 || Length >= (int)sizeof(Text))
        return FALSE;
    if (!WriteBytes(Cdf, Text, (DWORD)Length))
        return FALSE;

    CatalogCdf = CryptCATCDFOpen(Cdf, NULL);
    if (!CatalogCdf)
        return FALSE;
    while ((Attribute = CryptCATCDFEnumCatAttributes(CatalogCdf, Attribute, NULL)))
        ;
    while ((Tag = CryptCATCDFEnumMembersByCDFTagEx(CatalogCdf, Tag, NULL, &Member, TRUE, NULL)))
    {
        Attribute = NULL;
        while ((Attribute = CryptCATCDFEnumAttributesWithCDFTag(CatalogCdf, Tag, Member, Attribute, NULL)))
            ;
    }
    CryptCATCDFClose(CatalogCdf);
    DeleteFileW(Cdf);

    if (!ReadBytes(Unsigned, &Data, &DataSize))
        return FALSE;
    Msg = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    if (Msg && CryptMsgUpdate(Msg, Data, DataSize, TRUE) &&
        CryptMsgGetParam(Msg, CMSG_CONTENT_PARAM, 0, NULL, &ContentSize) &&
        (Content = HeapAlloc(GetProcessHeap(), 0, ContentSize)) &&
        CryptMsgGetParam(Msg, CMSG_CONTENT_PARAM, 0, Content, &ContentSize) &&
        SignContent(szOID_CTL, Content, ContentSize, NULL, &Signed, &SignedSize))
    {
        Ret = WriteBytes(CatalogPath, Signed, SignedSize);
    }
    if (Msg)
        CryptMsgClose(Msg);
    HeapFree(GetProcessHeap(), 0, Signed);
    HeapFree(GetProcessHeap(), 0, Content);
    HeapFree(GetProcessHeap(), 0, Data);
    DeleteFileW(Unsigned);
    return Ret;
}

static
DWORD
ZerosToMisalign(DWORD FileSize, DWORD Misalign)
{
    return Misalign ? (Misalign + 8 - FileSize % 8) % 8 : 0;
}

static
BOOL
AppendZeros(const WCHAR *Path, DWORD Misalign)
{
    static const BYTE Zeros[8];
    HANDLE File;
    DWORD Count, Written;
    BOOL Ret;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Count = ZerosToMisalign(GetFileSize(File, NULL), Misalign);
    Ret = SetFilePointer(File, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER &&
          WriteFile(File, Zeros, Count, &Written, NULL) && Written == Count;
    CloseHandle(File);
    return Ret;
}

static
BOOL
ImageDigest(const WCHAR *Path, BYTE *Digest, DWORD *DigestSize)
{
    SIP_SUBJECTINFO Info;
    GUID Subject = PeSubject;
    SIP_INDIRECT_DATA *Indirect = NULL;
    DWORD Size = 0;
    BOOL Ret;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = Path;
    Info.DigestAlgorithm.pszObjId = (LPSTR)szOID_NIST_sha256;
    Info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    Info.hFile = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (Info.hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = CryptSIPCreateIndirectData(&Info, &Size, NULL) &&
          (Indirect = HeapAlloc(GetProcessHeap(), 0, Size)) &&
          CryptSIPCreateIndirectData(&Info, &Size, Indirect) &&
          Indirect->Digest.cbData <= *DigestSize;
    if (Ret)
    {
        memcpy(Digest, Indirect->Digest.pbData, Indirect->Digest.cbData);
        *DigestSize = Indirect->Digest.cbData;
    }
    CloseHandle(Info.hFile);
    HeapFree(GetProcessHeap(), 0, Indirect);
    return Ret;
}

static
BOOL
CreateSignedImage(LPCSTR Algorithm, DWORD Misalign, LPCSTR DigestOid, BYTE *Digest, DWORD *DigestSize)
{
    WCHAR Self[MAX_PATH];
    SIP_SUBJECTINFO Info;
    GUID Subject = PeSubject;
    SIP_INDIRECT_DATA *Indirect = NULL;
    BYTE *Encoded = NULL, *Signed = NULL;
    DWORD Size = 0, EncodedSize = 0, SignedSize = 0, Index = 0;
    BOOL Ret = FALSE;

    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));
    if (!CopyFileW(Self, ImagePath, FALSE) || !AppendZeros(ImagePath, Misalign))
        return FALSE;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = ImagePath;
    Info.DigestAlgorithm.pszObjId = (LPSTR)Algorithm;
    Info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    Info.hFile = CreateFileW(ImagePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Info.hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (CryptSIPCreateIndirectData(&Info, &Size, NULL) &&
        (Indirect = HeapAlloc(GetProcessHeap(), 0, Size)) &&
        CryptSIPCreateIndirectData(&Info, &Size, Indirect) &&
        Indirect->Digest.cbData <= *DigestSize &&
        (!DigestOid || (Indirect->DigestAlgorithm.pszObjId = (LPSTR)DigestOid)) &&
        CryptEncodeObject(X509_ASN_ENCODING, SPC_INDIRECT_DATA_CONTENT_STRUCT, Indirect, NULL, &EncodedSize) &&
        (Encoded = HeapAlloc(GetProcessHeap(), 0, EncodedSize)) &&
        CryptEncodeObject(X509_ASN_ENCODING, SPC_INDIRECT_DATA_CONTENT_STRUCT, Indirect, Encoded, &EncodedSize) &&
        SignContent(SPC_INDIRECT_DATA_OBJID, Encoded, EncodedSize, NULL, &Signed, &SignedSize))
    {
        memcpy(Digest, Indirect->Digest.pbData, Indirect->Digest.cbData);
        *DigestSize = Indirect->Digest.cbData;
        Ret = CryptSIPPutSignedDataMsg(&Info, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &Index, SignedSize, Signed);
    }
    CloseHandle(Info.hFile);
    HeapFree(GetProcessHeap(), 0, Signed);
    HeapFree(GetProcessHeap(), 0, Encoded);
    HeapFree(GetProcessHeap(), 0, Indirect);
    return Ret;
}

static
LONG
VerifyCatalog(GUID *Action, const WCHAR *Member, DRIVER_VER_INFO *Version)
{
    WINTRUST_CATALOG_INFO Catalog = { sizeof(Catalog) };
    WINTRUST_DATA Data = { sizeof(Data) };
    LONG Ret;

    Catalog.pcwszCatalogFilePath = CatalogPath;
    Catalog.pcwszMemberTag = L"member";
    Catalog.pcwszMemberFilePath = Member;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_CATALOG;
    Data.pCatalog = &Catalog;
    Data.pPolicyCallbackData = Version;
    Data.dwStateAction = WTD_STATEACTION_VERIFY;
    Ret = WinVerifyTrust(INVALID_HANDLE_VALUE, Action, &Data);
    Data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, Action, &Data);
    return Ret;
}

static
VOID
TestCatalogFile(VOID)
{
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_DATA Data = { sizeof(Data) };
    CRYPT_PROVIDER_DATA *ProvData;
    DWORD Step;

    File.pcwszFilePath = CatalogPath;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    Data.dwStateAction = WTD_STATEACTION_VERIFY;
    ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data), ERROR_SUCCESS);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    ok(ProvData != NULL, "no provider data\n");
    if (ProvData)
    {
        for (Step = 0; Step < ProvData->cdwTrustStepErrors; Step++)
            ok(!ProvData->padwTrustStepErrors[Step], "step %lu error 0x%lx\n", Step, ProvData->padwTrustStepErrors[Step]);
        ok_long(ProvData->csSigners, 1);
    }
    Data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
}

static
VOID
InitVersion(DRIVER_VER_INFO *Version, DWORD Size, DWORD Platform, DWORD LowMajor, DWORD LowMinor, DWORD HighMajor,
            DWORD HighMinor)
{
    ZeroMemory(Version, sizeof(*Version));
    Version->cbStruct = Size;
    Version->dwPlatform = Platform;
    Version->sOSVersionLow.dwMajor = LowMajor;
    Version->sOSVersionLow.dwMinor = LowMinor;
    Version->sOSVersionHigh.dwMajor = HighMajor;
    Version->sOSVersionHigh.dwMinor = HighMinor;
}

static
VOID
CheckDriverVersion(LONG Result, DRIVER_VER_INFO *Version, LONG ExpectedResult, const WCHAR *ExpectedVersion,
                   BOOL ExpectedSigner)
{
    ok_hex(Result, ExpectedResult);
    ok(!wcscmp(Version->wszVersion, ExpectedVersion), "version '%ls', expected '%ls'\n", Version->wszVersion, ExpectedVersion);
    if (ExpectedSigner)
    {
        ok(!wcscmp(Version->wszSignedBy, L"LiberNT wintrust test leaf"), "signed by '%ls'\n", Version->wszSignedBy);
        ok(Version->pcSignerCertContext && CertCompareCertificate(X509_ASN_ENCODING, Version->pcSignerCertContext->pCertInfo,
                                                                  Leaf->pCertInfo), "unexpected signer certificate\n");
    }
    else
    {
        ok(!Version->wszSignedBy[0], "signed by '%ls'\n", Version->wszSignedBy);
        ok(!Version->pcSignerCertContext, "unexpected signer certificate\n");
    }
    if (Version->pcSignerCertContext)
        CertFreeCertificateContext(Version->pcSignerCertContext);
}

static
VOID
TestCatalogs(VOID)
{
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2, Driver = DRIVER_ACTION_VERIFY;
    CRYPTCATATTRIBUTE *Attribute;
    CRYPTCATMEMBER *Member;
    DRIVER_VER_INFO Version;
    HANDLE Catalog;
    LONG Ret;

    Catalog = CryptCATOpen(CatalogPath, CRYPTCAT_OPEN_EXISTING, 0, 0, 0);
    ok(Catalog != INVALID_HANDLE_VALUE, "CryptCATOpen failed with %lu\n", GetLastError());
    if (Catalog != INVALID_HANDLE_VALUE)
    {
        Member = CryptCATGetMemberInfo(Catalog, (LPWSTR)L"member");
        ok(Member != NULL, "member not found\n");
        Attribute = Member ? CryptCATGetAttrInfo(Catalog, Member, (LPWSTR)L"OSAttr") : NULL;
        ok(Attribute && Attribute->cbValue == sizeof(TEST_OS_ATTR) &&
           !memcmp(Attribute->pbValue, TEST_OS_ATTR, sizeof(TEST_OS_ATTR)), "unexpected member OSAttr\n");
        Attribute = CryptCATGetCatAttrInfo(Catalog, (LPWSTR)L"OSAttr");
        ok(Attribute && Attribute->cbValue == sizeof(TEST_OS_ATTR) &&
           !memcmp(Attribute->pbValue, TEST_OS_ATTR, sizeof(TEST_OS_ATTR)), "unexpected catalog OSAttr\n");
        CryptCATClose(Catalog);
    }

    ok_hex(VerifyCatalog(&Generic, MemberPath, NULL), ERROR_SUCCESS);
    ok_hex(VerifyCatalog(&Generic, OtherPath, NULL), TRUST_E_BAD_DIGEST);

    /* Windows 11 verifies drivers only against a Microsoft root, before the structure, the member or the OS */
    InitVersion(&Version, sizeof(Version), 0, 0, 0, 0, 0);
    Ret = VerifyCatalog(&Driver, MemberPath, &Version);
    CheckDriverVersion(Ret, &Version, CERT_E_UNTRUSTEDROOT, L"", FALSE);

    InitVersion(&Version, sizeof(Version), VER_PLATFORM_WIN32_NT, 6, 1, 6, 1);
    Ret = VerifyCatalog(&Driver, MemberPath, &Version);
    CheckDriverVersion(Ret, &Version, CERT_E_UNTRUSTEDROOT, L"", FALSE);

    InitVersion(&Version, 8, 0, 0, 0, 0, 0);
    Ret = VerifyCatalog(&Driver, MemberPath, &Version);
    CheckDriverVersion(Ret, &Version, CERT_E_UNTRUSTEDROOT, L"", FALSE);

    InitVersion(&Version, sizeof(Version), 0, 0, 0, 0, 0);
    Ret = VerifyCatalog(&Driver, OtherPath, &Version);
    CheckDriverVersion(Ret, &Version, CERT_E_UNTRUSTEDROOT, L"", FALSE);

    ok_hex(VerifyCatalog(&Driver, MemberPath, NULL), CERT_E_UNTRUSTEDROOT);
}

static
VOID
TestImage(const BYTE *Digest, DWORD DigestSize)
{
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2, Driver = DRIVER_ACTION_VERIFY;
    PFN_FILE_HASH FileHash = (PFN_FILE_HASH)GetProcAddress(GetModuleHandleW(L"wintrust.dll"), "WTHelperGetFileHash");
    PFN_FIND_ISSUER FindIssuer = (PFN_FIND_ISSUER)GetProcAddress(GetModuleHandleW(L"wintrust.dll"),
                                                                 "WTHelperCertFindIssuerCertificate");
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_DATA Data = { sizeof(Data) };
    CRYPT_PROVIDER_DATA *ProvData;
    CRYPT_PROVIDER_SGNR *Signer;
    DRIVER_VER_INFO Version;
    BYTE Hash[64];
    DWORD Size;
    ALG_ID Algorithm;
    LONG Ret;

    File.pcwszFilePath = ImagePath;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    Data.dwStateAction = WTD_STATEACTION_VERIFY;
    Ret = WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
    ok_hex(Ret, ERROR_SUCCESS);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    ok(ProvData != NULL, "no provider data\n");
    if (ProvData)
    {
        ok_hex(WTHelperCertCheckValidSignature(ProvData), S_OK);
        Signer = WTHelperGetProvSignerFromChain(ProvData, 0, FALSE, 0);
        ok(Signer && Signer->csCertChain == 2, "unexpected signer chain\n");
        if (FindIssuer && Signer && Signer->csCertChain == 2)
        {
            DWORD Confidence = 0xdead, Error = 0xdead;
            PCCERT_CONTEXT Issuer;
            FILETIME Now;

            GetSystemTimeAsFileTime(&Now);
            Issuer = FindIssuer(Signer->pasCertChain[0].pCert, ProvData->chStores, ProvData->pahStores, &Now,
                                X509_ASN_ENCODING, &Confidence, &Error);
            ok(Issuer && CertCompareCertificate(X509_ASN_ENCODING, Issuer->pCertInfo, Root->pCertInfo), "unexpected issuer\n");
            ok_hex(Confidence, CERT_CONFIDENCE_SIG | CERT_CONFIDENCE_TIME | CERT_CONFIDENCE_TIMENEST);
            ok_hex(Error, ERROR_SUCCESS);
            if (Issuer)
                CertFreeCertificateContext(Issuer);

            Confidence = Error = 0xdead;
            Issuer = FindIssuer(Signer->pasCertChain[0].pCert, 0, NULL, &Now, X509_ASN_ENCODING, &Confidence, &Error);
            ok(!Issuer, "issuer found without stores\n");
            ok_hex(Confidence, 0xdead);
            ok_hex(Error, 0xdead);

            Confidence = Error = 0xdead;
            Issuer = FindIssuer(Signer->pasCertChain[1].pCert, ProvData->chStores, ProvData->pahStores, &Now,
                                X509_ASN_ENCODING, &Confidence, &Error);
            ok(!Issuer, "issuer found for the root\n");
            ok_hex(Confidence, 0);
            ok_hex(Error, ERROR_SUCCESS);
        }
    }
    Data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);

    InitVersion(&Version, sizeof(Version), VER_PLATFORM_WIN32_NT, 10, 0, 10, 0);
    Data.pPolicyCallbackData = &Version;
    Data.dwStateAction = WTD_STATEACTION_IGNORE;
    File.hFile = NULL;
    Ret = WinVerifyTrust(INVALID_HANDLE_VALUE, &Driver, &Data);
    CheckDriverVersion(Ret, &Version, CERT_E_UNTRUSTEDROOT, L"", FALSE);

    if (!FileHash)
    {
        skip("WTHelperGetFileHash is not exported\n");
        return;
    }
    Size = 0;
    Algorithm = 0;
    ok_hex(FileHash(ImagePath, 0, NULL, NULL, &Size, &Algorithm), ERROR_SUCCESS);
    ok_long(Size, DigestSize);
    ok_hex(Algorithm, CALG_OID_INFO_CNG_ONLY);
    Size = 4;
    ok_hex(FileHash(ImagePath, 0, NULL, Hash, &Size, &Algorithm), ERROR_MORE_DATA);
    ok_long(Size, DigestSize);
    Size = sizeof(Hash);
    ok_hex(FileHash(ImagePath, 0, NULL, Hash, &Size, &Algorithm), ERROR_SUCCESS);
    ok(Size == DigestSize && !memcmp(Hash, Digest, DigestSize), "unexpected file hash\n");
}

static
VOID
TestUnalignedImage(VOID)
{
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_DATA Data = { sizeof(Data) };
    WCHAR Self[MAX_PATH], Other[MAX_PATH];
    BYTE Digest[64], Before[64], After[64], *Image;
    DWORD DigestSize = sizeof(Digest), BeforeSize = sizeof(Before), AfterSize = sizeof(After), Unsigned, ImageSize;
    HANDLE Handle;
    IMAGE_NT_HEADERS *Nt;

    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));
    Handle = CreateFileW(Self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Unsigned = GetFileSize(Handle, NULL);
    CloseHandle(Handle);
    Unsigned += ZerosToMisalign(Unsigned, 1);

    /* Windows 11 hashes an unaligned image as if it were padded with zeros to 8 bytes */
    swprintf(Other, RTL_NUMBER_OF(Other), L"%lslntwtpad.exe", TempDir);
    ok(CopyFileW(Self, Other, FALSE) && AppendZeros(Other, 1), "Cannot copy the image (%lu)\n", GetLastError());
    ok(ImageDigest(Other, Before, &BeforeSize), "Cannot hash the image (%lu)\n", GetLastError());
    ok(AppendZeros(Other, 2), "Cannot pad the image (%lu)\n", GetLastError());
    ok(ImageDigest(Other, After, &AfterSize), "Cannot hash the image (%lu)\n", GetLastError());
    ok(BeforeSize == AfterSize && !memcmp(Before, After, AfterSize), "padding changed the digest\n");
    DeleteFileW(Other);

    DeleteFileW(ImagePath);
    ok(CreateSignedImage(szOID_NIST_sha256, 1, NULL, Digest, &DigestSize), "Cannot sign the image (%lu)\n", GetLastError());
    AfterSize = sizeof(After);
    ok(ImageDigest(ImagePath, After, &AfterSize), "Cannot hash the signed image (%lu)\n", GetLastError());
    ok(AfterSize == DigestSize && !memcmp(After, Digest, DigestSize), "signing changed the digest\n");
    if (ReadBytes(ImagePath, &Image, &ImageSize))
    {
        Nt = (IMAGE_NT_HEADERS *)(Image + ((IMAGE_DOS_HEADER *)Image)->e_lfanew);
        ok_long(Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress, (Unsigned + 7) & ~7);
        ok_long(Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress +
                Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size, ImageSize);
        HeapFree(GetProcessHeap(), 0, Image);
    }

    File.pcwszFilePath = ImagePath;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data), ERROR_SUCCESS);
}

static
VOID
TestDigestAlgorithm(VOID)
{
    static const LPCSTR Oids[] = { "1.2.3.4", szOID_RSA_RSA };
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    CRYPT_PROVIDER_DATA *ProvData;
    BYTE Digest[64];
    DWORD DigestSize, i, Step;

    for (i = 0; i < RTL_NUMBER_OF(Oids); i++)
    {
        WINTRUST_FILE_INFO File = { sizeof(File) };
        WINTRUST_DATA Data = { sizeof(Data) };

        winetest_push_context("%s", Oids[i]);
        DeleteFileW(ImagePath);
        DigestSize = sizeof(Digest);
        ok(CreateSignedImage(szOID_NIST_sha256, 0, Oids[i], Digest, &DigestSize), "Cannot sign the image (%lu)\n",
           GetLastError());
        File.pcwszFilePath = ImagePath;
        Data.dwUIChoice = WTD_UI_NONE;
        Data.fdwRevocationChecks = WTD_REVOKE_NONE;
        Data.dwUnionChoice = WTD_CHOICE_FILE;
        Data.pFile = &File;
        Data.dwStateAction = WTD_STATEACTION_VERIFY;
        ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data), TRUST_E_BAD_DIGEST);
        ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
        ok(ProvData != NULL, "no provider data\n");
        for (Step = 0; ProvData && Step < ProvData->cdwTrustStepErrors; Step++)
            ok(ProvData->padwTrustStepErrors[Step] == (Step == TRUSTERROR_STEP_FINAL_OBJPROV ? TRUST_E_BAD_DIGEST : 0),
               "step %lu error 0x%lx\n", Step, ProvData->padwTrustStepErrors[Step]);
        Data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
        winetest_pop_context();
    }
}

static
VOID
TestSha1Image(VOID)
{
    PFN_FILE_HASH FileHash = (PFN_FILE_HASH)GetProcAddress(GetModuleHandleW(L"wintrust.dll"), "WTHelperGetFileHash");
    BYTE Digest[64], Hash[64];
    DWORD DigestSize = sizeof(Digest), Size = sizeof(Hash);
    ALG_ID Algorithm = 0;

    DeleteFileW(ImagePath);
    ok(CreateSignedImage(szOID_OIWSEC_sha1, 0, NULL, Digest, &DigestSize), "Cannot sign the image (%lu)\n", GetLastError());
    if (!FileHash)
        return;
    ok_hex(FileHash(ImagePath, 0, NULL, Hash, &Size, &Algorithm), ERROR_SUCCESS);
    ok_hex(Algorithm, CALG_SHA1);
    ok(Size == 20 && DigestSize == 20 && !memcmp(Hash, Digest, Size), "unexpected SHA-1 file hash\n");
}

static
VOID
TestFindCerts(VOID)
{
    PFN_FIND_CERTS FindCerts = (PFN_FIND_CERTS)GetProcAddress(GetModuleHandleW(L"wintrust.dll"), "FindCertsByIssuer");
    PCCERT_CONTEXT Added = NULL;
    PCERT_CHAIN Chains;
    DWORD Size = 0, Count = 0;
    HCERTSTORE My;

    if (!FindCerts)
        return;
    My = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"My");
    if (!My || !CertAddCertificateContextToStore(My, Leaf, CERT_STORE_ADD_REPLACE_EXISTING, &Added))
    {
        skip("Cannot add the leaf certificate to the My store\n");
        if (My)
            CertCloseStore(My, 0);
        return;
    }

    ok_hex(FindCerts(NULL, &Size, &Count, Root->pCertInfo->Subject.pbData, Root->pCertInfo->Subject.cbData, NULL,
                     AT_SIGNATURE), S_OK);
    ok_long(Count, 1);
    Chains = HeapAlloc(GetProcessHeap(), 0, Size ? Size : 1);
    if (Chains && FindCerts(Chains, &Size, &Count, Root->pCertInfo->Subject.pbData, Root->pCertInfo->Subject.cbData,
                            NULL, AT_SIGNATURE) == S_OK)
    {
        ok_long(Chains[0].cCerts, 2);
        ok(Chains[0].certs[0].cbData == Leaf->cbCertEncoded &&
           !memcmp(Chains[0].certs[0].pbData, Leaf->pbCertEncoded, Leaf->cbCertEncoded), "first certificate is not the leaf\n");
        ok(Chains[0].cCerts == 2 && Chains[0].certs[1].cbData == Root->cbCertEncoded &&
           !memcmp(Chains[0].certs[1].pbData, Root->pbCertEncoded, Root->cbCertEncoded), "second certificate is not the root\n");
    }
    HeapFree(GetProcessHeap(), 0, Chains);

    Size = Count = 0xdead;
    ok_hex(FindCerts(NULL, &Size, &Count, Leaf->pCertInfo->Subject.pbData, Leaf->pCertInfo->Subject.cbData, NULL,
                     AT_SIGNATURE), CRYPT_E_NOT_FOUND);
    ok_long(Count, 0);

    Size = 0;
    ok_hex(FindCerts(NULL, &Size, &Count, Root->pCertInfo->Subject.pbData, Root->pCertInfo->Subject.cbData,
                     L"CodeSigning", AT_SIGNATURE), S_OK);
    ok_long(Count, 1);

    CertDeleteCertificateFromStore(Added);
    CertCloseStore(My, 0);
}

static
BOOL
SetupFixture(VOID)
{
    static const char MemberText[] = "LiberNT signed catalog member\r\n";
    static const char OtherText[] = "LiberNT other file\r\n";
    HCRYPTKEY Key;

    GetTempPathW(RTL_NUMBER_OF(TempDir), TempDir);
    swprintf(CatalogPath, RTL_NUMBER_OF(CatalogPath), L"%lslntwtsig.cat", TempDir);
    swprintf(MemberPath, RTL_NUMBER_OF(MemberPath), L"%lslntwtsig-member.txt", TempDir);
    swprintf(OtherPath, RTL_NUMBER_OF(OtherPath), L"%lslntwtsig-other.txt", TempDir);
    swprintf(ImagePath, RTL_NUMBER_OF(ImagePath), L"%lslntwtsig-image.exe", TempDir);
    if (!WriteBytes(MemberPath, MemberText, sizeof(MemberText) - 1) || !WriteBytes(OtherPath, OtherText, sizeof(OtherText) - 1))
        return FALSE;

    CryptAcquireContextW(&Provider, TEST_CONTAINER, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    if (!CryptAcquireContextW(&Provider, TEST_CONTAINER, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET))
        return FALSE;
    if (!CryptGenKey(Provider, AT_SIGNATURE, 2048 << 16, &Key))
        return FALSE;
    CryptDestroyKey(Key);

    Root = CreateCertificate(L"CN=LiberNT wintrust test root", NULL, TRUE, NULL);
    Leaf = Root ? CreateCertificate(L"CN=LiberNT wintrust test leaf", Root, FALSE, NULL) : NULL;
    Tsa = Root ? CreateCertificate(L"CN=LiberNT wintrust test TSA", Root, FALSE, szOID_PKIX_KP_TIMESTAMP_SIGNING) : NULL;
    CodeSigner = Root ? CreateCertificate(L"CN=LiberNT wintrust test code signer", Root, FALSE, szOID_PKIX_KP_CODE_SIGNING) : NULL;
    RogueTsa = CreateCertificate(L"CN=LiberNT wintrust test rogue TSA", NULL, FALSE, szOID_PKIX_KP_TIMESTAMP_SIGNING);
    if (!Leaf || !Tsa || !CodeSigner || !RogueTsa)
        return FALSE;
    MachineRoot = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
    return MachineRoot && CertAddCertificateContextToStore(MachineRoot, Root, CERT_STORE_ADD_REPLACE_EXISTING, NULL);
}

typedef struct _TIMESTAMP_CASE
{
    PCCERT_CONTEXT *Countersigner;
    BOOL Tamper;
    BOOL OmitCert;
    LONG Result;
    DWORD CounterInfoStep;
    DWORD CounterCertStep;
    DWORD SignatureStep;
    DWORD CertificateStep;
    DWORD CounterError;
    DWORD SignerType;
    BOOL SigningTimeUsed;
    DWORD ChainLength;
    DWORD LastCertError;
} TIMESTAMP_CASE;

static
BOOL
CreateTimestampedImage(PCCERT_CONTEXT Countersigner, const FILETIME *SigningTime, BOOL Tamper, BOOL OmitCert)
{
    BYTE TimeData[32], *Encoded = NULL, *Signed = NULL, *Final = NULL;
    CRYPT_ATTR_BLOB TimeBlob = { sizeof(TimeData), TimeData };
    CRYPT_ATTRIBUTE TimeAttribute = { (LPSTR)szOID_RSA_signingTime, 1, &TimeBlob };
    CMSG_SIGNER_ENCODE_INFO Countersign;
    WCHAR Self[MAX_PATH];
    SIP_SUBJECTINFO Info;
    GUID Subject = PeSubject;
    SIP_INDIRECT_DATA *Indirect = NULL;
    DWORD Size = 0, EncodedSize = 0, SignedSize = 0, FinalSize = 0, Index = 0, i;
    HCRYPTMSG Msg = NULL;
    BOOL Ret = FALSE;

    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));
    if (!CryptEncodeObject(X509_ASN_ENCODING, PKCS_UTC_TIME, SigningTime, TimeData, &TimeBlob.cbData) ||
        !CopyFileW(Self, ImagePath, FALSE))
        return FALSE;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = ImagePath;
    Info.DigestAlgorithm.pszObjId = (LPSTR)szOID_NIST_sha256;
    Info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    Info.hFile = CreateFileW(ImagePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Info.hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&Countersign, sizeof(Countersign));
    Countersign.cbSize = sizeof(Countersign);
    Countersign.pCertInfo = Countersigner->pCertInfo;
    Countersign.hCryptProv = Provider;
    Countersign.dwKeySpec = AT_SIGNATURE;
    Countersign.HashAlgorithm.pszObjId = (LPSTR)szOID_NIST_sha256;
    Countersign.cAuthAttr = 1;
    Countersign.rgAuthAttr = &TimeAttribute;
    if (CryptSIPCreateIndirectData(&Info, &Size, NULL) &&
        (Indirect = HeapAlloc(GetProcessHeap(), 0, Size)) &&
        CryptSIPCreateIndirectData(&Info, &Size, Indirect) &&
        CryptEncodeObject(X509_ASN_ENCODING, SPC_INDIRECT_DATA_CONTENT_STRUCT, Indirect, NULL, &EncodedSize) &&
        (Encoded = HeapAlloc(GetProcessHeap(), 0, EncodedSize)) &&
        CryptEncodeObject(X509_ASN_ENCODING, SPC_INDIRECT_DATA_CONTENT_STRUCT, Indirect, Encoded, &EncodedSize) &&
        SignContent(SPC_INDIRECT_DATA_OBJID, Encoded, EncodedSize, OmitCert ? NULL : Countersigner, &Signed, &SignedSize) &&
        (Msg = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL)) &&
        CryptMsgUpdate(Msg, Signed, SignedSize, TRUE) &&
        CryptMsgCountersign(Msg, 0, 1, &Countersign) &&
        CryptMsgGetParam(Msg, CMSG_ENCODED_MESSAGE, 0, NULL, &FinalSize) &&
        (Final = HeapAlloc(GetProcessHeap(), 0, FinalSize)) &&
        CryptMsgGetParam(Msg, CMSG_ENCODED_MESSAGE, 0, Final, &FinalSize))
    {
        for (i = FinalSize - 16; Tamper && i > 0; i--)
        {
            if (Final[i] == 0x04 && Final[i + 1] == 0x82 && Final[i + 2] == 0x01 && Final[i + 3] == 0x00)
            {
                Final[i + 10] ^= 1;
                break;
            }
        }
        Ret = CryptSIPPutSignedDataMsg(&Info, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &Index, FinalSize, Final);
    }
    if (Msg)
        CryptMsgClose(Msg);
    CloseHandle(Info.hFile);
    HeapFree(GetProcessHeap(), 0, Final);
    HeapFree(GetProcessHeap(), 0, Signed);
    HeapFree(GetProcessHeap(), 0, Encoded);
    HeapFree(GetProcessHeap(), 0, Indirect);
    return Ret;
}

static
VOID
TestTimestamp(VOID)
{
    static const TIMESTAMP_CASE Cases[] =
    {
        { &Tsa, FALSE, FALSE, ERROR_SUCCESS, 0, 0, 0, 0, ERROR_SUCCESS, SGNR_TYPE_TIMESTAMP, TRUE, 2, ERROR_SUCCESS },
        { &Tsa, TRUE, FALSE, TRUST_E_COUNTER_SIGNER, STATUS_INVALID_SIGNATURE, 0, TRUST_E_COUNTER_SIGNER, 0,
          STATUS_INVALID_SIGNATURE, 0, FALSE, 2, ERROR_SUCCESS },
        { &CodeSigner, FALSE, FALSE, ERROR_SUCCESS, 0, 0, 0, 0, ERROR_SUCCESS, 0, FALSE, 2, ERROR_SUCCESS },
        { &Leaf, FALSE, FALSE, ERROR_SUCCESS, 0, 0, 0, 0, ERROR_SUCCESS, 0, FALSE, 2, ERROR_SUCCESS },
        { &RogueTsa, FALSE, FALSE, TRUST_E_TIME_STAMP, 0, 0, 0, 0, ERROR_SUCCESS, SGNR_TYPE_TIMESTAMP, TRUE, 1,
          CERT_E_UNTRUSTEDROOT },
        { &Tsa, FALSE, TRUE, TRUST_E_COUNTER_SIGNER, 0, CRYPT_E_NOT_FOUND, TRUST_E_COUNTER_SIGNER, TRUST_E_COUNTER_SIGNER,
          TRUST_E_NO_SIGNER_CERT, 0, FALSE, 0, ERROR_SUCCESS },
    };
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    CRYPT_PROVIDER_DATA *ProvData;
    CRYPT_PROVIDER_SGNR *Signer, *Counter;
    FILETIME SigningTime;
    ULARGE_INTEGER Time;
    DWORD i, Step;
    LONG Ret;

    GetSystemTimeAsFileTime(&SigningTime);
    Time.LowPart = SigningTime.dwLowDateTime;
    Time.HighPart = SigningTime.dwHighDateTime;
    Time.QuadPart -= Time.QuadPart % 10000000 + 3600ULL * 10000000;
    SigningTime.dwLowDateTime = Time.LowPart;
    SigningTime.dwHighDateTime = Time.HighPart;

    for (i = 0; i < RTL_NUMBER_OF(Cases); i++)
    {
        WINTRUST_FILE_INFO File = { sizeof(File) };
        WINTRUST_DATA Data = { sizeof(Data) };

        winetest_push_context("case %lu", i);
        DeleteFileW(ImagePath);
        ok(CreateTimestampedImage(*Cases[i].Countersigner, &SigningTime, Cases[i].Tamper, Cases[i].OmitCert),
           "Cannot create the image (0x%lx)\n", GetLastError());
        File.pcwszFilePath = ImagePath;
        Data.dwUIChoice = WTD_UI_NONE;
        Data.fdwRevocationChecks = WTD_REVOKE_NONE;
        Data.dwUnionChoice = WTD_CHOICE_FILE;
        Data.pFile = &File;
        Data.dwStateAction = WTD_STATEACTION_VERIFY;
        Ret = WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
        ok_hex(Ret, Cases[i].Result);
        ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
        ok(ProvData != NULL, "no provider data\n");
        if (ProvData)
        {
            for (Step = 0; Step < ProvData->cdwTrustStepErrors; Step++)
            {
                DWORD Expected = Step == TRUSTERROR_STEP_MSG_COUNTERSIGINFO ? Cases[i].CounterInfoStep :
                                 Step == TRUSTERROR_STEP_MSG_COUNTERSIGCERT ? Cases[i].CounterCertStep :
                                 Step == TRUSTERROR_STEP_FINAL_SIGPROV ? Cases[i].SignatureStep :
                                 Step == TRUSTERROR_STEP_FINAL_CERTPROV ? Cases[i].CertificateStep : 0;
                ok(ProvData->padwTrustStepErrors[Step] == Expected, "step %lu error 0x%lx, expected 0x%lx\n", Step,
                   ProvData->padwTrustStepErrors[Step], Expected);
            }
            ok_long(ProvData->csSigners, 1);
            Signer = ProvData->csSigners ? &ProvData->pasSigners[0] : NULL;
            if (Signer)
            {
                ok_long(Signer->csCertChain, 2);
                ok((CompareFileTime(&Signer->sftVerifyAsOf, &SigningTime) == 0) == Cases[i].SigningTimeUsed,
                   "signer verified as of %08lx%08lx\n", Signer->sftVerifyAsOf.dwHighDateTime,
                   Signer->sftVerifyAsOf.dwLowDateTime);
                ok_long(Signer->csCounterSigners, 1);
            }
            Counter = Signer && Signer->csCounterSigners ? &Signer->pasCounterSigners[0] : NULL;
            if (Counter)
            {
                ok_hex(Counter->dwSignerType, Cases[i].SignerType);
                ok_hex(Counter->dwError, Cases[i].CounterError);
                ok((CompareFileTime(&Counter->sftVerifyAsOf, &SigningTime) == 0) == Cases[i].SigningTimeUsed,
                   "countersigner verified as of %08lx%08lx\n", Counter->sftVerifyAsOf.dwHighDateTime,
                   Counter->sftVerifyAsOf.dwLowDateTime);
                ok(Counter->psSigner != NULL, "no countersigner info\n");
                ok((Counter->pChainContext != NULL) == (Cases[i].ChainLength != 0), "countersigner chain %p\n",
                   Counter->pChainContext);
                ok_long(Counter->csCertChain, Cases[i].ChainLength);
                if (Cases[i].ChainLength && Counter->csCertChain == Cases[i].ChainLength)
                {
                    ok_hex(Counter->pasCertChain[0].dwConfidence,
                           CERT_CONFIDENCE_SIG | CERT_CONFIDENCE_TIME | CERT_CONFIDENCE_TIMENEST);
                    ok_hex(Counter->pasCertChain[Cases[i].ChainLength - 1].dwError, Cases[i].LastCertError);
                }
                ok(WTHelperGetProvSignerFromChain(ProvData, 0, TRUE, 0) == Counter, "unexpected countersigner\n");
            }
        }
        Data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
        winetest_pop_context();
    }
}

static
DWORD
ElementError(PCCERT_CHAIN_CONTEXT Chain, DWORD Element)
{
    if (!Chain || Element >= Chain->rgpChain[0]->cElement)
        return 0xdeadbeef;
    return Chain->rgpChain[0]->rgpElement[Element]->TrustStatus.dwErrorStatus;
}

static
LONG
VerifyImage(GUID *Action, DWORD RevocationChecks, DWORD ProvFlags, WINTRUST_DATA *Data, WINTRUST_FILE_INFO *File)
{
    ZeroMemory(Data, sizeof(*Data));
    ZeroMemory(File, sizeof(*File));
    Data->cbStruct = sizeof(*Data);
    File->cbStruct = sizeof(*File);
    File->pcwszFilePath = ImagePath;
    Data->dwUIChoice = WTD_UI_NONE;
    Data->fdwRevocationChecks = RevocationChecks;
    Data->dwUnionChoice = WTD_CHOICE_FILE;
    Data->pFile = File;
    Data->dwStateAction = WTD_STATEACTION_VERIFY;
    Data->dwProvFlags = ProvFlags;
    return WinVerifyTrust(INVALID_HANDLE_VALUE, Action, Data);
}

static
VOID
CloseImage(GUID *Action, WINTRUST_DATA *Data)
{
    Data->dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, Action, Data);
}

static
VOID
CheckRevocation(DWORD RevocationChecks, DWORD ProvFlags, DWORD Leaf0, DWORD Counter0, ULONG Line)
{
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO File;
    WINTRUST_DATA Data;
    CRYPT_PROVIDER_DATA *ProvData;
    LONG Ret;

    Ret = VerifyImage(&Generic, RevocationChecks, ProvFlags, &Data, &File);
    ok_(__FILE__, Line)(Ret == ERROR_SUCCESS, "WinVerifyTrust returned 0x%lx\n", Ret);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    ok_(__FILE__, Line)(ProvData != NULL, "no provider data\n");
    if (ProvData && ProvData->csSigners)
    {
        ok_(__FILE__, Line)(ProvData->dwProvFlags == (CPD_USE_NT5_CHAIN_FLAG | ProvFlags), "provider flags 0x%lx\n",
                            ProvData->dwProvFlags);
        ok_(__FILE__, Line)(ElementError(ProvData->pasSigners[0].pChainContext, 0) == Leaf0, "signer leaf 0x%lx\n",
                            ElementError(ProvData->pasSigners[0].pChainContext, 0));
        ok_(__FILE__, Line)(ElementError(ProvData->pasSigners[0].pChainContext, 1) == 0, "signer root 0x%lx\n",
                            ElementError(ProvData->pasSigners[0].pChainContext, 1));
        if (ProvData->pasSigners[0].csCounterSigners)
            ok_(__FILE__, Line)(ElementError(ProvData->pasSigners[0].pasCounterSigners[0].pChainContext, 0) == Counter0,
                                "countersigner leaf 0x%lx\n",
                                ElementError(ProvData->pasSigners[0].pasCounterSigners[0].pChainContext, 0));
    }
    CloseImage(&Generic, &Data);
}

static
VOID
TestRevocation(VOID)
{
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2, Driver = DRIVER_ACTION_VERIFY;
    WINTRUST_FILE_INFO File;
    WINTRUST_DATA Data;
    CRYPT_PROVIDER_DATA *ProvData;
    FILETIME SigningTime;
    ULARGE_INTEGER Time;
    DWORD Saved = 0;
    LONG Ret;

    GetSystemTimeAsFileTime(&SigningTime);
    Time.LowPart = SigningTime.dwLowDateTime;
    Time.HighPart = SigningTime.dwHighDateTime;
    Time.QuadPart -= Time.QuadPart % 10000000 + 3600ULL * 10000000;
    SigningTime.dwLowDateTime = Time.LowPart;
    SigningTime.dwHighDateTime = Time.HighPart;

    /* The test certificates have no CRL distribution point, so a checked certificate is of unknown status */
    DeleteFileW(ImagePath);
    ok(CreateTimestampedImage(Tsa, &SigningTime, FALSE, FALSE), "Cannot create the image (0x%lx)\n", GetLastError());
    CheckRevocation(WTD_REVOKE_NONE, 0, CERT_TRUST_REVOCATION_STATUS_UNKNOWN, 0, __LINE__);
    CheckRevocation(WTD_REVOKE_NONE, WTD_REVOCATION_CHECK_NONE, 0, 0, __LINE__);
    CheckRevocation(WTD_REVOKE_NONE, WTD_REVOCATION_CHECK_END_CERT, CERT_TRUST_REVOCATION_STATUS_UNKNOWN,
                    CERT_TRUST_REVOCATION_STATUS_UNKNOWN, __LINE__);
    CheckRevocation(WTD_REVOKE_NONE, WTD_CACHE_ONLY_URL_RETRIEVAL, CERT_TRUST_REVOCATION_STATUS_UNKNOWN, 0, __LINE__);
    CheckRevocation(WTD_REVOKE_WHOLECHAIN, 0, CERT_TRUST_REVOCATION_STATUS_UNKNOWN, CERT_TRUST_REVOCATION_STATUS_UNKNOWN,
                    __LINE__);

    WintrustGetRegPolicyFlags(&Saved);
    ok_hex(Saved, WTPF_IGNOREREVOCATIONONTS | WTPF_OFFLINEOK_IND | WTPF_OFFLINEOK_COM | WTPF_OFFLINEOKNBU_IND |
                  WTPF_OFFLINEOKNBU_COM);
    ok(WintrustSetRegPolicyFlags(0), "WintrustSetRegPolicyFlags failed\n");
    Ret = VerifyImage(&Generic, WTD_REVOKE_NONE, 0, &Data, &File);
    ok_hex(Ret, CERT_E_REVOCATION_FAILURE);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    if (ProvData && ProvData->csSigners && ProvData->pasSigners[0].csCounterSigners)
        ok_hex(ElementError(ProvData->pasSigners[0].pasCounterSigners[0].pChainContext, 0),
               CERT_TRUST_REVOCATION_STATUS_UNKNOWN);
    CloseImage(&Generic, &Data);
    ok(WintrustSetRegPolicyFlags(WTPF_OFFLINEOK_IND), "WintrustSetRegPolicyFlags failed\n");
    ok_hex(VerifyImage(&Generic, WTD_REVOKE_NONE, 0, &Data, &File), ERROR_SUCCESS);
    CloseImage(&Generic, &Data);
    ok(WintrustSetRegPolicyFlags(WTPF_OFFLINEOKNBU_IND | WTPF_OFFLINEOKNBU_COM), "WintrustSetRegPolicyFlags failed\n");
    ok_hex(VerifyImage(&Generic, WTD_REVOKE_NONE, 0, &Data, &File), CERT_E_REVOCATION_FAILURE);
    CloseImage(&Generic, &Data);
    WintrustSetRegPolicyFlags(Saved);

    /* Only timestamp countersigners skip revocation checking by default */
    DeleteFileW(ImagePath);
    ok(CreateTimestampedImage(CodeSigner, &SigningTime, FALSE, FALSE), "Cannot create the image (0x%lx)\n", GetLastError());
    CheckRevocation(WTD_REVOKE_NONE, 0, CERT_TRUST_REVOCATION_STATUS_UNKNOWN, CERT_TRUST_REVOCATION_STATUS_UNKNOWN,
                    __LINE__);

    /* The chain asks for the usage of the action */
    DeleteFileW(ImagePath);
    SigningCert = Tsa;
    ok(CreateTimestampedImage(Tsa, &SigningTime, FALSE, FALSE), "Cannot create the image (0x%lx)\n", GetLastError());
    SigningCert = NULL;
    Ret = VerifyImage(&Generic, WTD_REVOKE_NONE, 0, &Data, &File);
    ok_hex(Ret, CERT_E_WRONG_USAGE);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    if (ProvData && ProvData->csSigners)
        ok_hex(ElementError(ProvData->pasSigners[0].pChainContext, 0),
               CERT_TRUST_IS_NOT_VALID_FOR_USAGE | CERT_TRUST_REVOCATION_STATUS_UNKNOWN);
    CloseImage(&Generic, &Data);
    Ret = VerifyImage(&Driver, WTD_REVOKE_NONE, 0, &Data, &File);
    ok_hex(Ret, CERT_E_UNTRUSTEDROOT);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    if (ProvData)
        ok_hex(ProvData->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV], CERT_E_UNTRUSTEDROOT);
    CloseImage(&Driver, &Data);
}

static
VOID
CleanupFixture(VOID)
{
    PCCERT_CONTEXT Added;

    if (MachineRoot && Root)
    {
        Added = CertFindCertificateInStore(MachineRoot, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, Root, NULL);
        if (Added)
            CertDeleteCertificateFromStore(Added);
    }
    if (MachineRoot)
        CertCloseStore(MachineRoot, 0);
    if (Leaf)
        CertFreeCertificateContext(Leaf);
    if (Tsa)
        CertFreeCertificateContext(Tsa);
    if (CodeSigner)
        CertFreeCertificateContext(CodeSigner);
    if (RogueTsa)
        CertFreeCertificateContext(RogueTsa);
    if (Root)
        CertFreeCertificateContext(Root);
    if (Provider)
        CryptReleaseContext(Provider, 0);
    CryptAcquireContextW(&Provider, TEST_CONTAINER, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    DeleteFileW(CatalogPath);
    DeleteFileW(MemberPath);
    DeleteFileW(OtherPath);
    DeleteFileW(ImagePath);
}

START_TEST(SignedCatalog)
{
    BYTE Digest[64];
    DWORD DigestSize = sizeof(Digest);

    if (!SetupFixture())
    {
        skip("Cannot create the signing fixture (%lu); the test needs administrator rights\n", GetLastError());
        CleanupFixture();
        return;
    }

    ok(CreateSignedCatalog(), "Cannot create the signed catalog (%lu)\n", GetLastError());
    TestCatalogs();
    TestCatalogFile();
    ok(CreateSignedImage(szOID_NIST_sha256, 0, NULL, Digest, &DigestSize), "Cannot sign the image (%lu)\n", GetLastError());
    TestImage(Digest, DigestSize);
    TestSha1Image();
    TestUnalignedImage();
    TestDigestAlgorithm();
    TestTimestamp();
    TestRevocation();
    TestFindCerts();

    CleanupFixture();
}
