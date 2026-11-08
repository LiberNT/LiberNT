/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the wintrust exports that have no SDK declaration, as measured on Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <mssip.h>
#include <stdio.h>

typedef BOOL (WINAPI *PFN_HASH_TAG)(DWORD, const BYTE *, LPWSTR *);
typedef VOID (WINAPI *PFN_FREE_TAG)(LPWSTR);
typedef LPWSTR (WINAPI *PFN_ENUM_BY_TAG)(CRYPTCATCDF *, LPWSTR, PFN_CDF_PARSE_ERROR_CALLBACK, CRYPTCATMEMBER **);
typedef VOID (WINAPI *PFN_WORKING_FLAGS)(DWORD *);
typedef BOOL (WINAPI *PFN_SIP_INFO)(void *);
typedef int (WINAPI *PFN_FIRST_PAGE)(LPCWSTR, LPCWSTR, PVOID, int);
typedef BOOL (WINAPI *PFN_DECODE)(DWORD, BYTE **, DWORD *, DWORD, DWORD, LPCSTR, const void *, DWORD, DWORD);
typedef VOID (WINAPI *PFN_FREE_DECODE)(DWORD, BYTE **);
typedef BOOL (WINAPI *PFN_AGENCY)(PCCERT_CONTEXT, DWORD *, SPC_SP_AGENCY_INFO *);
typedef BOOL (WINAPI *PFN_USAGE)(PCCERT_CONTEXT, LPCSTR);
typedef BOOL (WINAPI *PFN_IN_ROOT)(CRYPT_PROVIDER_DATA *, PCCERT_CONTEXT);
typedef BOOL (WINAPI *PFN_KNOWN)(CRYPT_PROVIDER_DATA *);
typedef BOOL (WINAPI *PFN_OPEN_STORES)(HCRYPTPROV, DWORD *, HCERTSTORE *, DWORD);
typedef BOOL (WINAPI *PFN_CHAINED)(PCCERT_CONTEXT, HCERTSTORE, DWORD);
typedef BOOL (WINAPI *PFN_CHAINED_STATE)(HANDLE, DWORD);
typedef BOOL (WINAPI *PFN_VERIFY_MEMBER)(HANDLE, CRYPTCATMEMBER *, HANDLE);
typedef HRESULT (WINAPI *PFN_PROVIDER)(CRYPT_PROVIDER_DATA *);
typedef ULONG_PTR (WINAPI *PFN_ANY3)(PVOID, PVOID, PVOID);
typedef ULONG_PTR (WINAPI *PFN_ANY4)(PVOID, PVOID, PVOID, PVOID);
typedef ULONG_PTR (WINAPI *PFN_ANY6)(PVOID, PVOID, PVOID, PVOID, PVOID, PVOID);
typedef HRESULT (WINAPI *PFN_BIO_POLICY)(PVOID, BOOLEAN *);

static HMODULE Wintrust;
static WCHAR TempDir[MAX_PATH], Self[MAX_PATH];

#define GET(type, name) ((type)GetProcAddress(Wintrust, name))

static
BOOL
WriteText(const WCHAR *Path, const char *Text)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD Written;
    BOOL Ret;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL);
    CloseHandle(File);
    return Ret;
}

static
PCCERT_CONTEXT
SelfSigned(const WCHAR *Subject, CERT_EXTENSIONS *Extensions)
{
    BYTE NameData[128];
    CERT_NAME_BLOB Name = { sizeof(NameData), NameData };

    if (!CertStrToNameW(X509_ASN_ENCODING, Subject, CERT_X500_NAME_STR, NULL, NameData, &Name.cbData, NULL))
        return NULL;
    return CertCreateSelfSignCertificate(0, &Name, 0, NULL, NULL, NULL, NULL, Extensions);
}

static
VOID
TestHashTag(VOID)
{
    PFN_HASH_TAG Construct = GET(PFN_HASH_TAG, "MsCatConstructHashTag");
    PFN_FREE_TAG Free = GET(PFN_FREE_TAG, "MsCatFreeHashTag");
    BYTE Digest[32];
    LPWSTR Tag = NULL;
    DWORD i;

    ok(Construct && Free, "MsCat hash tag functions are not exported\n");
    if (!Construct || !Free)
        return;
    for (i = 0; i < sizeof(Digest); i++)
        Digest[i] = (BYTE)(0xa0 + i);
    ok_int(Construct(20, Digest, &Tag), TRUE);
    ok(Tag && !wcscmp(Tag, L"A0A1A2A3A4A5A6A7A8A9AAABACADAEAFB0B1B2B3"), "tag %ls\n", Tag);
    Free(Tag);
    Tag = NULL;
    ok_int(Construct(32, Digest, &Tag), TRUE);
    ok(Tag && !wcscmp(Tag, L"A0A1A2A3A4A5A6A7A8A9AAABACADAEAFB0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF"), "tag %ls\n", Tag);
    Free(Tag);
}

static
VOID
TestVerifyMember(const WCHAR *CatalogPath, const WCHAR *MemberPath)
{
    PFN_VERIFY_MEMBER VerifyMember = GET(PFN_VERIFY_MEMBER, "CryptCATVerifyMember");
    CRYPTCATMEMBER *Member;
    HANDLE Catalog, File;

    ok(VerifyMember != NULL, "CryptCATVerifyMember is not exported\n");
    if (!VerifyMember)
        return;
    Catalog = CryptCATOpen((LPWSTR)CatalogPath, CRYPTCAT_OPEN_EXISTING, 0, 0, 0);
    Member = Catalog != INVALID_HANDLE_VALUE ? CryptCATGetMemberInfo(Catalog, (LPWSTR)L"m1") : NULL;
    File = CreateFileW(MemberPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(Member && File != INVALID_HANDLE_VALUE, "cannot open the catalog member\n");
    if (Member && File != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        ok_int(VerifyMember(Catalog, Member, File), FALSE);
        ok_hex(GetLastError(), 0xdeadbeef);
        SetLastError(0xdeadbeef);
        ok_int(VerifyMember(Catalog, Member, INVALID_HANDLE_VALUE), FALSE);
        ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
        SetLastError(0xdeadbeef);
        ok_int(VerifyMember(Catalog, Member, NULL), FALSE);
        ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
        SetLastError(0xdeadbeef);
        ok_int(VerifyMember(NULL, Member, File), FALSE);
        ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    }
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    if (Catalog != INVALID_HANDLE_VALUE)
        CryptCATClose(Catalog);
}

static
VOID
TestCdfByTag(VOID)
{
    PFN_ENUM_BY_TAG EnumByTag = GET(PFN_ENUM_BY_TAG, "CryptCATCDFEnumMembersByCDFTag");
    WCHAR Cdf[MAX_PATH], M1[MAX_PATH], M2[MAX_PATH], Cat[MAX_PATH];
    CRYPTCATMEMBER *Member = NULL;
    CRYPTCATCDF *Catalog;
    LPWSTR Tag = NULL;
    char Text[4096];

    ok(EnumByTag != NULL, "CryptCATCDFEnumMembersByCDFTag is not exported\n");
    if (!EnumByTag)
        return;
    SetLastError(0xdeadbeef);
    ok(!EnumByTag(NULL, NULL, NULL, &Member), "NULL CDF accepted\n");
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);

    swprintf(Cdf, RTL_NUMBER_OF(Cdf), L"%lslntwtint.cdf", TempDir);
    swprintf(Cat, RTL_NUMBER_OF(Cat), L"%lslntwtint.cat", TempDir);
    swprintf(M1, RTL_NUMBER_OF(M1), L"%lslntwtint-m1.txt", TempDir);
    swprintf(M2, RTL_NUMBER_OF(M2), L"%lslntwtint-m2.txt", TempDir);
    snprintf(Text, sizeof(Text), "[CatalogHeader]\r\nName=lntwtint.cat\r\nResultDir=%ls\r\nPublicVersion=0x0000001\r\n"
             "EncodingType=0x00010001\r\n[CatalogFiles]\r\nm1=%ls\r\nm2=%ls\r\n", TempDir, M1, M2);
    if (!WriteText(M1, "member one\r\n") || !WriteText(M2, "member two\r\n") || !WriteText(Cdf, Text) ||
        !(Catalog = CryptCATCDFOpen(Cdf, NULL)))
    {
        skip("Cannot open the CDF\n");
        return;
    }
    Tag = EnumByTag(Catalog, NULL, NULL, &Member);
    ok(Tag && !wcscmp(Tag, L"m1") && Member && !wcscmp(Member->pwszReferenceTag, L"m1"), "first member %ls\n", Tag);
    Tag = EnumByTag(Catalog, Tag, NULL, &Member);
    ok(Tag && !wcscmp(Tag, L"m2") && Member && !wcscmp(Member->pwszReferenceTag, L"m2"), "second member %ls\n", Tag);
    SetLastError(0xdeadbeef);
    Tag = EnumByTag(Catalog, Tag, NULL, &Member);
    ok(!Tag, "third member %ls\n", Tag);
    ok_hex(GetLastError(), ERROR_SUCCESS);
    ok_int(CryptCATCDFClose(Catalog), TRUE);
    TestVerifyMember(Cat, M1);
    DeleteFileW(Cdf);
    DeleteFileW(Cat);
    DeleteFileW(M1);
    DeleteFileW(M2);
}

static
VOID
TestSipInfo(VOID)
{
    static const GUID Expected[] =
    {
        { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0xC689AABA, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0xDE351A43, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0x9BA61D3F, 0xE73A, 0x11D0, { 0x8C,0xD2,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0x18B3C141, 0xAE0D, 0x40F9, { 0x94,0x65,0xE5,0x42,0xAF,0xC1,0xAB,0xC7 } },
    };
    PFN_SIP_INFO GetInfo = GET(PFN_SIP_INFO, "CryptSIPGetInfo");
    PFN_WORKING_FLAGS WorkingFlags = GET(PFN_WORKING_FLAGS, "CryptSIPGetRegWorkingFlags");
    struct { DWORD cbSize; DWORD cgSubjects; const GUID *pgSubjects; } Info;
    DWORD Flags = 0xcccccccc, Policy = 0xcccccccc, i;

    ok(GetInfo && WorkingFlags, "SIP information functions are not exported\n");
    if (!GetInfo || !WorkingFlags)
        return;
    ZeroMemory(&Info, sizeof(Info));
    ok_int(GetInfo(&Info), TRUE);
    ok_long(Info.cbSize, sizeof(Info));
    ok_long(Info.cgSubjects, RTL_NUMBER_OF(Expected));
    for (i = 0; Info.pgSubjects && i < RTL_NUMBER_OF(Expected) && i < Info.cgSubjects; i++)
        ok(IsEqualGUID(&Info.pgSubjects[i], &Expected[i]), "subject %lu is %08lx\n", i, Info.pgSubjects[i].Data1);

    WorkingFlags(&Flags);
    WintrustGetRegPolicyFlags(&Policy);
    ok_hex(Flags, Policy);
}

static
DWORD
ReferenceFirstPageHash(const WCHAR *Path, ALG_ID Algorithm, BYTE *Hash)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE Header[4096], Page[4096];
    DWORD Read = 0, Size = 0, Checksum, Security, Headers, Length;
    IMAGE_DOS_HEADER *Dos = (IMAGE_DOS_HEADER *)Header;
    IMAGE_NT_HEADERS *Nt;
    HCRYPTPROV Provider;
    HCRYPTHASH Context;

    if (File == INVALID_HANDLE_VALUE)
        return 0;
    if (!ReadFile(File, Header, sizeof(Header), &Read, NULL) || Read < sizeof(*Dos) || Dos->e_lfanew < 0 ||
        (DWORD)Dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > Read)
    {
        CloseHandle(File);
        return 0;
    }
    CloseHandle(File);
    Nt = (IMAGE_NT_HEADERS *)(Header + Dos->e_lfanew);
    if (Nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        IMAGE_NT_HEADERS64 *Nt64 = (IMAGE_NT_HEADERS64 *)Nt;

        Checksum = (BYTE *)&Nt64->OptionalHeader.CheckSum - Header;
        Security = (BYTE *)&Nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] - Header;
        Headers = Nt64->OptionalHeader.SizeOfHeaders;
    }
    else
    {
        IMAGE_NT_HEADERS32 *Nt32 = (IMAGE_NT_HEADERS32 *)Nt;

        Checksum = (BYTE *)&Nt32->OptionalHeader.CheckSum - Header;
        Security = (BYTE *)&Nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] - Header;
        Headers = Nt32->OptionalHeader.SizeOfHeaders;
    }
    if (Headers > Read)
        return 0;
    ZeroMemory(Page, sizeof(Page));
    memcpy(Page, Header, Checksum);
    memcpy(Page + Checksum, Header + Checksum + 4, Security - Checksum - 4);
    memcpy(Page + Security - 4, Header + Security + 8, Headers - Security - 8);
    Length = sizeof(Page) - 12;
    if (!CryptAcquireContextW(&Provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return 0;
    Size = 32;
    if (CryptCreateHash(Provider, Algorithm, 0, 0, &Context))
    {
        if (!CryptHashData(Context, Page, Length, 0) || !CryptGetHashParam(Context, HP_HASHVAL, Hash, &Size, 0))
            Size = 0;
        CryptDestroyHash(Context);
    }
    CryptReleaseContext(Provider, 0);
    return Size;
}

static
VOID
TestFirstPageHash(VOID)
{
    PFN_FIRST_PAGE FirstPage = GET(PFN_FIRST_PAGE, "ComputeFirstPageHash");
    BYTE Hash[64], Reference[32];
    DWORD Size;

    ok(FirstPage != NULL, "ComputeFirstPageHash is not exported\n");
    if (!FirstPage)
        return;
    Size = ReferenceFirstPageHash(Self, CALG_SHA_256, Reference);
    ok_long(Size, 32);
    FillMemory(Hash, sizeof(Hash), 0xcc);
    ok_int(FirstPage(L"SHA256", Self, Hash, 32), 32);
    ok(!memcmp(Hash, Reference, 32), "unexpected first page hash\n");
    FillMemory(Hash, sizeof(Hash), 0xcc);
    ok_int(FirstPage(L"SHA256", Self, Hash, 64), 32);
    ok_hex(Hash[0], 0xcc);
    ok_int(FirstPage(L"SHA256", Self, NULL, 0), 32);
    ok_int(FirstPage(L"SHA256", Self, Hash, 16), 32);
    ok_hex(Hash[0], 0xcc);
    Size = ReferenceFirstPageHash(Self, CALG_SHA1, Reference);
    ok_int(FirstPage(L"SHA1", Self, Hash, 20), 20);
    ok(Size == 20 && !memcmp(Hash, Reference, 20), "unexpected SHA-1 first page hash\n");
    ok_int(FirstPage(L"MD5", Self, Hash, 16), 0);
    ok_int(FirstPage(L"SHA256", NULL, Hash, 32), 0);
    ok_int(FirstPage(L"sha256", Self, NULL, 0), 0);
    ok_int(FirstPage(L"Sha1", Self, NULL, 0), 0);
    ok_int(FirstPage(L"SHA384", Self, NULL, 0), 0);
    ok_int(FirstPage(L"SHA-256", Self, NULL, 0), 0);
    SetLastError(0xdeadbeef);
    ok_int(FirstPage(L"SHA256", Self, Hash, 32), 32);
    ok_hex(GetLastError(), ERROR_SUCCESS);
    SetLastError(0xdeadbeef);
    ok_int(FirstPage(L"SHA256", Self, NULL, 0), 32);
    ok_hex(GetLastError(), 0xdeadbeef);
    SetLastError(0xdeadbeef);
    ok_int(FirstPage(L"SHA256", Self, Hash, 16), 32);
    ok_hex(GetLastError(), 0xdeadbeef);
    SetLastError(0xdeadbeef);
    ok_int(FirstPage(L"MD5", Self, NULL, 0), 0);
    ok_hex(GetLastError(), 0xdeadbeef);
}

static
VOID
TestDecode(VOID)
{
    PFN_DECODE Decode = GET(PFN_DECODE, "TrustDecode");
    PFN_FREE_DECODE FreeDecode = GET(PFN_FREE_DECODE, "TrustFreeDecode");
    BYTE NameData[128];
    DWORD NameSize = sizeof(NameData), Size = 0;
    BYTE *Out = NULL;

    ok(Decode && FreeDecode, "TrustDecode functions are not exported\n");
    if (!Decode || !FreeDecode ||
        !CertStrToNameW(X509_ASN_ENCODING, L"CN=LiberNT decode", CERT_X500_NAME_STR, NULL, NameData, &NameSize, NULL))
        return;
    ok_int(Decode(0, &Out, &Size, 0, X509_ASN_ENCODING, X509_NAME, NameData, NameSize, 0), TRUE);
    ok(Out && Size && ((CERT_NAME_INFO *)Out)->cRDN == 1, "unexpected decoded name\n");
    FreeDecode(0, &Out);
    ok(Out == NULL, "TrustFreeDecode kept %p\n", Out);
    Out = NULL;
    Size = 0;
    ok_int(Decode(0x77, &Out, &Size, 0x1234, X509_ASN_ENCODING, X509_NAME, NameData, NameSize, 0), TRUE);
    ok(Out && Size && ((CERT_NAME_INFO *)Out)->cRDN == 1, "module and error ids changed the decoding\n");
    FreeDecode(0, &Out);
    Out = (BYTE *)0x1;
    Size = 0xdead;
    SetLastError(0xdeadbeef);
    ok_int(Decode(0, &Out, &Size, 0x1234, X509_ASN_ENCODING, X509_NAME, NameData, 3, 0), FALSE);
    ok_hex(GetLastError(), CRYPT_E_ASN1_EOD);
    ok(Out == NULL, "failed decode kept %p\n", Out);
    ok_long(Size, 0);
}

static
VOID
TestAgencyAndUsage(VOID)
{
    PFN_AGENCY Agency = GET(PFN_AGENCY, "WTHelperGetAgencyInfo");
    PFN_USAGE Usage = GET(PFN_USAGE, "WTHelperCheckCertUsage");
    SPC_LINK Url = { SPC_URL_LINK_CHOICE };
    SPC_SP_AGENCY_INFO Info = { 0 };
    LPSTR Oids[] = { (LPSTR)szOID_PKIX_KP_CLIENT_AUTH };
    CERT_ENHKEY_USAGE Eku = { 1, Oids };
    BYTE Encoded[256], EkuData[64], Out[1024];
    DWORD EncodedSize = sizeof(Encoded), EkuSize = sizeof(EkuData), Size;
    CERT_EXTENSION Extension = { (LPSTR)SPC_SP_AGENCY_INFO_OBJID, FALSE, { 0, Encoded } };
    CERT_EXTENSION EkuExtension = { (LPSTR)szOID_ENHANCED_KEY_USAGE, FALSE, { 0, EkuData } };
    CERT_EXTENSIONS Extensions = { 1, &Extension }, EkuExtensions = { 1, &EkuExtension };
    PCCERT_CONTEXT WithAgency, Plain, ClientAuth;

    ok(Agency && Usage, "agency or usage helper is not exported\n");
    if (!Agency || !Usage)
        return;
    Url.pwszUrl = (LPWSTR)L"http://agency.example";
    Info.pPolicyInformation = &Url;
    Info.pwszPolicyDisplayText = (LPWSTR)L"Agency text";
    if (!CryptEncodeObject(X509_ASN_ENCODING, SPC_SP_AGENCY_INFO_OBJID, &Info, Encoded, &EncodedSize) ||
        !CryptEncodeObject(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &Eku, EkuData, &EkuSize))
    {
        skip("Cannot encode the extensions\n");
        return;
    }
    Extension.Value.cbData = EncodedSize;
    EkuExtension.Value.cbData = EkuSize;
    WithAgency = SelfSigned(L"CN=LiberNT agency", &Extensions);
    Plain = SelfSigned(L"CN=LiberNT plain", NULL);
    ClientAuth = SelfSigned(L"CN=LiberNT client auth", &EkuExtensions);
    if (!WithAgency || !Plain || !ClientAuth)
    {
        skip("Cannot create the certificates\n");
        return;
    }

    SetLastError(0xdeadbeef);
    ok_int(Agency(NULL, NULL, NULL), FALSE);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    Size = 0;
    SetLastError(0xdeadbeef);
    ok_int(Agency(WithAgency, &Size, NULL), FALSE);
    ok_hex(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    ok(Size != 0, "no size\n");
    Size = sizeof(Out);
    ok_int(Agency(WithAgency, &Size, (SPC_SP_AGENCY_INFO *)Out), TRUE);
    ok(((SPC_SP_AGENCY_INFO *)Out)->pwszPolicyDisplayText &&
       !wcscmp(((SPC_SP_AGENCY_INFO *)Out)->pwszPolicyDisplayText, L"Agency text"), "unexpected agency text\n");
    Size = sizeof(Out);
    SetLastError(0xdeadbeef);
    ok_int(Agency(Plain, &Size, (SPC_SP_AGENCY_INFO *)Out), FALSE);
    ok_hex(GetLastError(), 0xdeadbeef);
    ok_long(Size, 0);

    SetLastError(0xdeadbeef);
    ok_int(Usage(NULL, szOID_PKIX_KP_CODE_SIGNING), TRUE);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    ok_int(Usage(Plain, szOID_PKIX_KP_CODE_SIGNING), TRUE);
    ok_int(Usage(ClientAuth, szOID_PKIX_KP_CLIENT_AUTH), TRUE);
    SetLastError(0xdeadbeef);
    ok_int(Usage(ClientAuth, szOID_PKIX_KP_CODE_SIGNING), FALSE);
    ok_hex(GetLastError(), CERT_E_WRONG_USAGE);

    CertFreeCertificateContext(WithAgency);
    CertFreeCertificateContext(Plain);
    CertFreeCertificateContext(ClientAuth);
}

static
VOID
TestStores(VOID)
{
    static const WCHAR *const Names[] = { L"Trust", L"My" };
    static const DWORD Indexes[] = { 1, 3 };
    PFN_OPEN_STORES OpenStores = GET(PFN_OPEN_STORES, "TrustOpenStores");
    PFN_KNOWN OpenKnown = GET(PFN_KNOWN, "WTHelperOpenKnownStores");
    PFN_IN_ROOT InRoot = GET(PFN_IN_ROOT, "WTHelperIsInRootStore");
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_DATA Data = { sizeof(Data) };
    CRYPT_PROVIDER_DATA *ProvData, Fake;
    HCERTSTORE Stores[8], Memory;
    PCCERT_CONTEXT Inside, Outside;
    DWORD Count, i, Before;

    ok(OpenStores && OpenKnown && InRoot, "store helpers are not exported\n");
    if (!OpenStores || !OpenKnown || !InRoot)
        return;

    for (i = 0; i < RTL_NUMBER_OF(Names); i++)
    {
        HCERTSTORE Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, Names[i]);
        PCCERT_CONTEXT Marker = SelfSigned(L"CN=LiberNT store marker", NULL), Added = NULL, Found;

        if (!Store || !Marker || !CertAddCertificateContextToStore(Store, Marker, CERT_STORE_ADD_ALWAYS, &Added))
        {
            skip("Cannot add a marker to the %ls store\n", Names[i]);
            continue;
        }
        Count = RTL_NUMBER_OF(Stores);
        ok_int(OpenStores(0, &Count, Stores, 0), TRUE);
        ok_long(Count, 5);
        if (Count == 5)
            CertControlStore(Stores[Indexes[i]], 0, CERT_STORE_CTRL_RESYNC, NULL);
        Found = Count == 5 ? CertFindCertificateInStore(Stores[Indexes[i]], X509_ASN_ENCODING, 0, CERT_FIND_EXISTING,
                                                        Marker, NULL) : NULL;
        ok(Found != NULL, "the %ls marker is not in store %lu\n", Names[i], Indexes[i]);
        if (Found)
            CertFreeCertificateContext(Found);
        while (Count)
            CertCloseStore(Stores[--Count], 0);
        CertDeleteCertificateFromStore(Added);
        CertFreeCertificateContext(Marker);
        CertCloseStore(Store, 0);
    }

    SetLastError(0xdeadbeef);
    ok_int(OpenStores(0, NULL, NULL, 0), FALSE);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    Count = RTL_NUMBER_OF(Stores);
    SetLastError(0xdeadbeef);
    ok_int(OpenStores(0, &Count, Stores, 1), FALSE);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    Count = 0;
    SetLastError(0xdeadbeef);
    ok_int(OpenStores(0, &Count, NULL, 0), TRUE);
    ok_hex(GetLastError(), ERROR_MORE_DATA);
    ok_long(Count, 5);
    Count = 3;
    SetLastError(0xdeadbeef);
    ok_int(OpenStores(0, &Count, Stores, 0), FALSE);
    ok_hex(GetLastError(), ERROR_MORE_DATA);
    ok_long(Count, 5);

    File.pcwszFilePath = Self;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    Data.dwStateAction = WTD_STATEACTION_VERIFY;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    ok(ProvData != NULL, "no provider data\n");
    if (ProvData)
    {
        Before = ProvData->chStores;
        ok_int(OpenKnown(ProvData), TRUE);
        ok_long(ProvData->chStores, Before + 5);
    }
    Data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);

    Memory = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    Inside = SelfSigned(L"CN=LiberNT inside", NULL);
    Outside = SelfSigned(L"CN=LiberNT outside", NULL);
    if (Memory && Inside && Outside && CertAddCertificateContextToStore(Memory, Inside, CERT_STORE_ADD_ALWAYS, NULL))
    {
        ZeroMemory(&Fake, sizeof(Fake));
        Fake.cbStruct = sizeof(Fake);
        Fake.chStores = 1;
        Fake.pahStores = &Memory;
        ok_int(InRoot(&Fake, Inside), TRUE);
        SetLastError(0xdeadbeef);
        ok_int(InRoot(&Fake, Outside), FALSE);
        ok_hex(GetLastError(), CRYPT_E_NOT_FOUND);
    }
    if (Inside)
        CertFreeCertificateContext(Inside);
    if (Outside)
        CertFreeCertificateContext(Outside);
    if (Memory)
        CertCloseStore(Memory, 0);
}

static
VOID
TestChainedToMicrosoft(VOID)
{
    PFN_CHAINED Chained = GET(PFN_CHAINED, "WTHelperIsChainedToMicrosoft");
    PFN_CHAINED_STATE ChainedState = GET(PFN_CHAINED_STATE, "WTHelperIsChainedToMicrosoftFromStateData");
    HCERTSTORE Memory = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    PCCERT_CONTEXT Own = SelfSigned(L"CN=LiberNT not Microsoft", NULL);

    ok(Chained && ChainedState, "Microsoft chain helpers are not exported\n");
    if (!Chained || !ChainedState || !Memory || !Own)
        return;
    ok_int(Chained(Own, Memory, 0), FALSE);
    SetLastError(0xdeadbeef);
    ok_int(Chained(Own, NULL, 0), FALSE);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok_int(ChainedState(NULL, 0), FALSE);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    CertFreeCertificateContext(Own);
    CertCloseStore(Memory, 0);
}

static
VOID
TestOfficePolicy(VOID)
{
    PFN_PROVIDER OfficeInit = GET(PFN_PROVIDER, "OfficeInitializePolicy");
    PFN_PROVIDER OfficeCleanup = GET(PFN_PROVIDER, "OfficeCleanupPolicy");
    GUID Office = OFFICESIGN_ACTION_VERIFY;
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_DATA Data = { sizeof(Data) };

    ok(OfficeInit && OfficeCleanup, "an Office policy export is missing\n");
    if (OfficeInit && OfficeCleanup)
    {
        ok_hex(OfficeInit(NULL), E_NOTIMPL);
        ok_hex(OfficeCleanup(NULL), E_NOTIMPL);
    }
    File.pcwszFilePath = Self;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Office, &Data), TRUST_E_PROVIDER_UNKNOWN);
}

static
VOID
TestFixedResults(VOID)
{
    PFN_ANY3 PersonalPages = GET(PFN_ANY3, "AddPersonalTrustDBPages");
    PFN_ANY4 Compact = GET(PFN_ANY4, "CatalogCompactHashDatabase");
    PFN_ANY3 AuthSha = GET(PFN_ANY3, "GetAuthenticodeSha256Hash");
    PFN_ANY3 FamilyCheck = GET(PFN_ANY3, "ConfigCiPackageFamilyNameCheck");
    PFN_ANY6 BioInfo = GET(PFN_ANY6, "WTGetBioSignatureInfo");
    PFN_ANY6 PluginInfo = GET(PFN_ANY6, "WTGetPluginSignatureInfo");
    PFN_ANY3 FirstPreferred = GET(PFN_ANY3, "WTIsFirstConfigCiResultPreferred");
    PFN_ANY4 SignerEvent = GET(PFN_ANY4, "WTLogConfigCiSignerEvent");
    BYTE Buffer[256], Out[16];
    HANDLE Handle;

    ok(PersonalPages && Compact && AuthSha && FamilyCheck && BioInfo && PluginInfo && FirstPreferred && SignerEvent,
       "an export is missing\n");
    if (!PersonalPages || !Compact || !AuthSha || !FamilyCheck || !BioInfo || !PluginInfo || !FirstPreferred ||
        !SignerEvent)
        return;

    ZeroMemory(Buffer, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    ok(!PersonalPages(Buffer, NULL, NULL), "AddPersonalTrustDBPages succeeded\n");
    ok_hex(GetLastError(), ERROR_CALL_NOT_IMPLEMENTED);
    ok(Compact((PVOID)L"C:\\lnt-missing", (PVOID)L"x", (PVOID)L"y", NULL) == TRUE, "CatalogCompactHashDatabase failed\n");

    Handle = CreateFileW(Self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    SetLastError(0xdeadbeef);
    ok_hex((HRESULT)AuthSha(Handle, Buffer, Buffer), E_INVALIDARG);
    ok_hex(GetLastError(), ERROR_INVALID_HANDLE);

    CloseHandle(Handle);

    ok_hex((DWORD)FamilyCheck(NULL, NULL, NULL), 0x10000000);
    ok_hex((DWORD)FamilyCheck((PVOID)L"Microsoft.WindowsNotepad_8wekyb3d8bbwe", NULL, NULL), 0x10000000);
    ok_hex((HRESULT)BioInfo(NULL, NULL, NULL, NULL, NULL, NULL), E_INVALIDARG);
    ok_hex((HRESULT)PluginInfo(NULL, NULL, NULL, NULL, NULL, NULL), E_INVALIDARG);
    ok_hex((HRESULT)FirstPreferred(NULL, NULL, NULL), E_INVALIDARG);
    ZeroMemory(Buffer, sizeof(Buffer));
    FillMemory(Out, sizeof(Out), 0xcc);
    ok_hex((HRESULT)FirstPreferred(Buffer, Buffer, Out), S_OK);
    ok_hex(Out[0], TRUE);
    ok_hex(Out[1], 0xcc);
    FillMemory(Buffer, 16, 0xff);
    FillMemory(Out, sizeof(Out), 0xcc);
    ok_hex((HRESULT)FirstPreferred(Buffer, Buffer + 64, Out), S_OK);
    ok_hex(Out[0], TRUE);
    ok_hex((HRESULT)SignerEvent(NULL, NULL, NULL, NULL), E_INVALIDARG);
}

static
VOID
TestConfigCi(VOID)
{
    static const GUID ConfigCiAction = { 0x6078065b, 0x8f22, 0x4b13, { 0xbd,0x9b,0x5b,0x76,0x27,0x76,0xf3,0x86 } };
    PFN_PROVIDER Final = GET(PFN_PROVIDER, "ConfigCiFinalPolicy");
    PFN_ANY3 Srp = GET(PFN_ANY3, "SrpCheckSmartlockerEAandProcessToken");
    PFN_BIO_POLICY BioPolicy = GET(PFN_BIO_POLICY, "WTValidateBioSignaturePolicy");
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2, Action = ConfigCiAction;
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_DATA Data = { sizeof(Data) };
    CRYPT_PROVIDER_DATA *ProvData;
    WCHAR Text[MAX_PATH];
    BYTE Buffer[256], Out[16];
    HANDLE Handle, Token = NULL;

    ok(Final && Srp && BioPolicy && GetProcAddress(Wintrust, "WTLogConfigCiScriptEvent"),
       "a Config CI export is missing\n");
    if (!Final || !Srp || !BioPolicy)
        return;

    File.pcwszFilePath = Self;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    Data.dwStateAction = WTD_STATEACTION_VERIFY;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    ok(ProvData != NULL, "no provider data\n");
    if (ProvData)
        ok_hex(Final(ProvData), E_INVALIDARG);
    Data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);

    swprintf(Text, RTL_NUMBER_OF(Text), L"%lslntwtci.txt", TempDir);
    if (WriteText(Text, "text\r\n"))
    {
        File.pcwszFilePath = Text;
        Data.dwStateAction = WTD_STATEACTION_IGNORE;
        Data.hWVTStateData = NULL;
        ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data), ERROR_SUCCESS);
        DeleteFileW(Text);
    }

    Handle = CreateFileW(Self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token);
    FillMemory(Out, sizeof(Out), 0xcc);
    SetLastError(0xdeadbeef);
    ok_int((int)Srp(Handle, Token, Out), 0);
    ok_hex(GetLastError(), 0xdeadbeef);
    ok_hex(Out[0], 0xcc);
    ok_int((int)Srp(Handle, NULL, Out), 0);
    if (Token)
        CloseHandle(Token);
    CloseHandle(Handle);

    ZeroMemory(Buffer, sizeof(Buffer));
    FillMemory(Out, sizeof(Out), 0xcc);
    ok_hex(BioPolicy(Buffer, (BOOLEAN *)Out), E_INVALIDARG);
    ok_hex(Out[0], FALSE);
    ok_hex(Out[1], 0xcc);
}

static
VOID
TestProviderData(VOID)
{
    static const GUID PeSubject = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_BLOB_INFO Blob = { sizeof(Blob) };
    WINTRUST_CERT_INFO Cert = { sizeof(Cert) };
    WINTRUST_DATA Data = { sizeof(Data) };
    CRYPT_PROVIDER_DATA *ProvData;
    PCCERT_CONTEXT Context;
    BYTE *Image;
    FILETIME Now;
    DWORD Size, Choice, i;
    HANDLE Handle;

    Handle = CreateFileW(Self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Size = GetFileSize(Handle, NULL);
    Image = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Image || !ReadFile(Handle, Image, Size, &Size, NULL))
        Size = 0;
    CloseHandle(Handle);
    Context = SelfSigned(L"CN=LiberNT provider data", NULL);
    GetSystemTimeAsFileTime(&Now);

    for (Choice = 0; Choice < 3; Choice++)
    {
        ZeroMemory(&Data, sizeof(Data));
        Data.cbStruct = sizeof(Data);
        Data.dwUIChoice = WTD_UI_NONE;
        Data.fdwRevocationChecks = WTD_REVOKE_NONE;
        Data.dwStateAction = WTD_STATEACTION_VERIFY;
        if (Choice == 0)
        {
            File.pcwszFilePath = Self;
            Data.dwUnionChoice = WTD_CHOICE_FILE;
            Data.pFile = &File;
        }
        else if (Choice == 1)
        {
            Blob.gSubject = PeSubject;
            Blob.pcwszDisplayName = L"lntwtblob display";
            Blob.cbMemObject = Size;
            Blob.pbMemObject = Image;
            Data.dwUnionChoice = WTD_CHOICE_BLOB;
            Data.pBlob = &Blob;
        }
        else
        {
            Cert.psCertContext = (CERT_CONTEXT *)Context;
            Cert.psftVerifyAsOf = &Now;
            Data.dwUnionChoice = WTD_CHOICE_CERT;
            Data.pCert = &Cert;
        }
        ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data),
               Choice == 2 ? CERT_E_UNTRUSTEDROOT : TRUST_E_NOSIGNATURE);
        ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
        ok(ProvData != NULL, "choice %lu: no provider data\n", Choice);
        if (ProvData)
        {
            ok(ProvData->hProv != 0, "choice %lu: no provider\n", Choice);
            ok_hex(ProvData->dwRegSecuritySettings, 1);
            ok_hex(ProvData->dwEncoding, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING);
            ok(ProvData->pszUsageOID && !strcmp(ProvData->pszUsageOID, szOID_PKIX_KP_CODE_SIGNING),
               "choice %lu: usage %s\n", Choice, ProvData->pszUsageOID);
            ok(ProvData->psPfns->psUIpfns && ProvData->psPfns->psUIpfns->cbStruct == sizeof(CRYPT_PROVUI_FUNCS) &&
               ProvData->psPfns->psUIpfns->psUIData &&
               ProvData->psPfns->psUIpfns->psUIData->cbStruct == sizeof(CRYPT_PROVUI_DATA),
               "choice %lu: unexpected UI functions\n", Choice);
            ok_long(ProvData->cdwTrustStepErrors, TRUSTERROR_MAX_STEPS);
            ok_hex(ProvData->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV], 0);
            if (Choice < 2)
            {
                DWORD SipError = Choice ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_PARAMETER;

                ok(ProvData->pPDSip && IsEqualGUID(&ProvData->pPDSip->gSubject, &PeSubject) && !ProvData->pPDSip->pSip,
                   "choice %lu: unexpected SIP data\n", Choice);
                ok_hex(ProvData->padwTrustStepErrors[TRUSTERROR_STEP_SIP], SipError);
                ok_hex(ProvData->padwTrustStepErrors[TRUSTERROR_STEP_MSG_SIGNERCOUNT], SipError);
                for (i = TRUSTERROR_STEP_FINAL_OBJPROV; i <= TRUSTERROR_STEP_FINAL_CERTPROV; i++)
                    ok_hex(ProvData->padwTrustStepErrors[i], TRUST_E_NOSIGNATURE);
                ok_long(ProvData->csSigners, 0);
            }
            else
            {
                ok(!ProvData->pPDSip, "certificate choice has SIP data\n");
                for (i = 0; i < ProvData->cdwTrustStepErrors; i++)
                    ok(!ProvData->padwTrustStepErrors[i], "step %lu is %#lx\n", i, ProvData->padwTrustStepErrors[i]);
                ok(ProvData->csSigners == 1 && !CompareFileTime(&ProvData->pasSigners[0].sftVerifyAsOf, &Now),
                   "certificate signer time not taken from the caller\n");
            }
        }
        Data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
    }
    {
        static const struct { GUID Action; const char *Usage; } Actions[] = {
            { DRIVER_ACTION_VERIFY, szOID_WHQL_CRYPTO },
            { HTTPSPROV_ACTION, szOID_PKIX_KP_CODE_SIGNING },
            { WINTRUST_ACTION_GENERIC_CHAIN_VERIFY, szOID_PKIX_KP_CODE_SIGNING },
        };

        for (i = 0; i < RTL_NUMBER_OF(Actions); i++)
        {
            GUID Action = Actions[i].Action;

            ZeroMemory(&Data, sizeof(Data));
            Data.cbStruct = sizeof(Data);
            Data.dwUIChoice = WTD_UI_NONE;
            Data.fdwRevocationChecks = WTD_REVOKE_NONE;
            Data.dwStateAction = WTD_STATEACTION_VERIFY;
            File.pcwszFilePath = Self;
            Data.dwUnionChoice = WTD_CHOICE_FILE;
            Data.pFile = &File;
            WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data);
            ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
            ok(ProvData && ProvData->pszUsageOID && !strcmp(ProvData->pszUsageOID, Actions[i].Usage) &&
               ProvData->dwRegSecuritySettings == 1 && (!ProvData->pPDSip || !ProvData->pPDSip->pSip),
               "action %lu: usage %s\n", i, ProvData ? ProvData->pszUsageOID : NULL);
            Data.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data);
        }
    }
    if (Context)
        CertFreeCertificateContext(Context);
    HeapFree(GetProcessHeap(), 0, Image);
}

static
VOID
WINAPI
CdfError(DWORD Area, DWORD Local, WCHAR *Line)
{
    ok(FALSE, "unexpected CDF error %#lx %#lx on '%ls'\n", Area, Local, Line);
}

static
VOID
TestCatalogDetails(VOID)
{
    static const WCHAR Value[] = L"second";
    WCHAR Cdf[MAX_PATH], Cat[MAX_PATH], M1[MAX_PATH], M2[MAX_PATH], New[MAX_PATH];
    char Text[4096];
    CRYPTCATCDF *Catalog;
    CRYPTCATMEMBER *Member = NULL;
    CRYPTCATATTRIBUTE *Attr = NULL;
    CRYPTCATSTORE *Store;
    SIP_INDIRECT_DATA Indirect;
    GUID Flat = { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    BYTE Digest[20] = { 1, 2, 3 }, FlatValue[] = { 0xa2, 0x02, 0x80, 0x00 };
    HCATADMIN Admin = NULL;
    HANDLE Handle;
    DWORD Count;

    SetLastError(0xdeadbeef);
    ok(CryptCATAdminAcquireContext(&Admin, NULL, 0), "CryptCATAdminAcquireContext failed\n");
    ok_hex(GetLastError(), 0xdeadbeef);
    if (Admin)
        CryptCATAdminReleaseContext(Admin, 0);

    swprintf(Cdf, RTL_NUMBER_OF(Cdf), L"%lslntwtcd.cdf", TempDir);
    swprintf(Cat, RTL_NUMBER_OF(Cat), L"%lslntwtcd.cat", TempDir);
    swprintf(M1, RTL_NUMBER_OF(M1), L"%lslntwtcd-m1.txt", TempDir);
    swprintf(M2, RTL_NUMBER_OF(M2), L"%lslntwtcd-m2.txt", TempDir);
    snprintf(Text, sizeof(Text), "[CatalogHeader]\r\nName=lntwtcd.cat\r\nResultDir=%ls\r\nPublicVersion=0x0000005\r\n"
             "EncodingType=0x00010001\r\nCATATTR1=0x10010001:Note:first\r\nCATATTR2=0x10020001:Bad:!!!notbase64\r\n"
             "[CatalogFiles]\r\nm1=%ls\r\n<HASH>m2=%ls\r\n", TempDir, M1, M2);
    DeleteFileW(Cat);
    if (!WriteText(M1, "member one\r\n") || !WriteText(M2, "member two\r\n") || !WriteText(Cdf, Text) ||
        !(Catalog = CryptCATCDFOpen(Cdf, CdfError)))
    {
        skip("Cannot open the CDF\n");
        return;
    }
    while ((Attr = CryptCATCDFEnumCatAttributes(Catalog, Attr, CdfError)))
        if (!wcscmp(Attr->pwszReferenceTag, L"Bad"))
            ok(Attr->cbValue == 0, "invalid base64 gave %lu bytes\n", Attr->cbValue);
    Member = CryptCATCDFEnumMembers(Catalog, NULL, CdfError);
    ok(Member && !wcscmp(Member->pwszReferenceTag, L"m1"), "first member %ls\n", Member ? Member->pwszReferenceTag : NULL);
    Member = CryptCATCDFEnumMembers(Catalog, Member, CdfError);
    ok(Member && !wcscmp(Member->pwszReferenceTag, L"<HASH>m2"), "second member %ls\n",
       Member ? Member->pwszReferenceTag : NULL);
    ok_int(CryptCATCDFClose(Catalog), TRUE);

    Handle = CryptCATOpen(Cat, CRYPTCAT_OPEN_EXISTING | CRYPTCAT_OPEN_EXCLUDE_PAGE_HASHES, 0, 0, 0);
    ok(Handle != INVALID_HANDLE_VALUE, "CryptCATOpen failed %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        Store = CryptCATStoreFromHandle(Handle);
        ok(Store && Store->dwPublicVersion == CRYPTCAT_VERSION_1 && Store->fdwStoreFlags == CRYPTCAT_OPEN_EXCLUDE_PAGE_HASHES,
           "store version %#lx flags %#lx\n", Store ? Store->dwPublicVersion : 0, Store ? Store->fdwStoreFlags : 0);
        SetLastError(0xdeadbeef);
        Attr = CryptCATPutCatAttrInfo(Handle, (WCHAR *)L"Note", CRYPTCAT_ATTR_AUTHENTICATED | CRYPTCAT_ATTR_NAMEASCII |
                                      CRYPTCAT_ATTR_DATAASCII | CRYPTCAT_ATTR_DATAREPLACE, sizeof(Value), (BYTE *)Value);
        ok(Attr != NULL, "DATAREPLACE failed %lu\n", GetLastError());
        for (Count = 0, Attr = NULL; (Attr = CryptCATEnumerateCatAttr(Handle, Attr));)
            if (!wcscmp(Attr->pwszReferenceTag, L"Note"))
            {
                Count++;
                ok(Attr->cbValue == sizeof(Value) && !memcmp(Attr->pbValue, Value, sizeof(Value)), "Note was not replaced\n");
            }
        ok_long(Count, 1);
        ok_int(CryptCATPersistStore(Handle), TRUE);
        CryptCATClose(Handle);
    }
    Handle = CryptCATOpen(Cat, CRYPTCAT_OPEN_EXISTING, 0, 0, 0);
    if (Handle != INVALID_HANDLE_VALUE)
    {
        for (Count = 0, Attr = NULL; (Attr = CryptCATEnumerateCatAttr(Handle, Attr));)
            if (!wcscmp(Attr->pwszReferenceTag, L"Note"))
            {
                Count++;
                ok(Attr->cbValue == sizeof(Value) && !memcmp(Attr->pbValue, Value, sizeof(Value)),
                   "Note was not written back\n");
            }
        ok_long(Count, 1);
        CryptCATClose(Handle);
    }

    swprintf(New, RTL_NUMBER_OF(New), L"%lslntwtcn.cat", TempDir);
    DeleteFileW(New);
    Handle = CryptCATOpen(New, CRYPTCAT_OPEN_CREATENEW, 0, 0, 0);
    ok(Handle != INVALID_HANDLE_VALUE, "CryptCATOpen for creation failed %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        Store = CryptCATStoreFromHandle(Handle);
        ok(Store && Store->dwPublicVersion == CRYPTCAT_VERSION_1 && !Store->fdwStoreFlags, "new store %#lx %#lx\n",
           Store ? Store->dwPublicVersion : 0, Store ? Store->fdwStoreFlags : 0);
        ZeroMemory(&Indirect, sizeof(Indirect));
        Indirect.Data.pszObjId = (LPSTR)SPC_CAB_DATA_OBJID;
        Indirect.Data.Value.cbData = sizeof(FlatValue);
        Indirect.Data.Value.pbData = FlatValue;
        Indirect.DigestAlgorithm.pszObjId = (LPSTR)szOID_OIWSEC_sha1;
        Indirect.Digest.cbData = sizeof(Digest);
        Indirect.Digest.pbData = Digest;
        ok(CryptCATPutMemberInfo(Handle, (WCHAR *)L"fa.txt", (WCHAR *)L"0102030000000000000000000000000000000000", &Flat,
                                 1, sizeof(Indirect), (BYTE *)&Indirect) != NULL, "first member %lu\n", GetLastError());
        Digest[0] = 9;
        ok(CryptCATPutMemberInfo(Handle, (WCHAR *)L"fb.txt", (WCHAR *)L"0902030000000000000000000000000000000000", &Flat,
                                 1, sizeof(Indirect), (BYTE *)&Indirect) != NULL, "second member %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Member = CryptCATEnumerateMember(Handle, NULL);
        ok(Member && !wcscmp(Member->pwszReferenceTag, L"0902030000000000000000000000000000000000"), "newest member first\n");
        Member = Member ? CryptCATEnumerateMember(Handle, Member) : NULL;
        ok(Member && !wcscmp(Member->pwszReferenceTag, L"0102030000000000000000000000000000000000"), "oldest member last\n");
        ok(!(Member ? CryptCATEnumerateMember(Handle, Member) : NULL), "a third member\n");
        ok_hex(GetLastError(), 0xdeadbeef);
        CryptCATClose(Handle);
    }
    DeleteFileW(New);
    DeleteFileW(Cdf);
    DeleteFileW(Cat);
    DeleteFileW(M1);
    DeleteFileW(M2);
}

static
BOOL
DumpContains(const char *Needle)
{
    static char Text[32768];
    DWORD Read = 0, Length = (DWORD)strlen(Needle), i;
    HANDLE Handle = CreateFileW(L"C:\\TRUSTPOL.TXT", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(Handle, Text, sizeof(Text), &Read, NULL);
    CloseHandle(Handle);
    for (i = 0; i + Length <= Read; i++)
        if (!memcmp(Text + i, Needle, Length))
            return TRUE;
    return FALSE;
}

static
VOID
TestDump(VOID)
{
    static const char Start[] = "CRYPT_PROVIDER_DATA:\r\n\0+======================================================\r\n";
    PFN_PROVIDER Dump = GET(PFN_PROVIDER, "SoftpubDumpStructure");
    GUID Generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO File = { sizeof(File) };
    WINTRUST_DATA Data = { sizeof(Data) };
    CRYPT_PROVIDER_DATA *ProvData;
    char Buffer[sizeof(Start)];
    HANDLE Handle;
    DWORD Read = 0;

    ok(Dump != NULL, "SoftpubDumpStructure is not exported\n");
    if (!Dump)
        return;
    DeleteFileW(L"C:\\TRUSTPOL.TXT");
    File.pcwszFilePath = Self;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &File;
    Data.dwStateAction = WTD_STATEACTION_VERIFY;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
    ProvData = WTHelperProvDataFromStateData(Data.hWVTStateData);
    if (ProvData)
    {
        ok_hex(Dump(ProvData), S_OK);
        Handle = CreateFileW(L"C:\\TRUSTPOL.TXT", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        ok(Handle != INVALID_HANDLE_VALUE, "C:\\TRUSTPOL.TXT was not written (%lu)\n", GetLastError());
        if (Handle != INVALID_HANDLE_VALUE)
        {
            ReadFile(Handle, Buffer, sizeof(Start) - 1, &Read, NULL);
            ok(Read == sizeof(Start) - 1 && !memcmp(Buffer, Start, Read), "unexpected dump start\n");
            CloseHandle(Handle);
        }
        ok(DumpContains("\r\n\0|       |.. hFile:                    0x"), "no file handle line\n");
        ok(DumpContains("\r\n\0|       +-- pgKnownSubject:           \r\n"), "no known subject line\n");
        ok(DumpContains("\r\n\0|.. dwRegSecuritySettings:            0x00000001\r\n"), "no security settings line\n");
        ok(DumpContains("\r\n\0|.. dwEncoding:                       0x00010001\r\n"), "no encoding line\n");
        ok(DumpContains("\r\n\0|   |.. Step[32]:                     0x800b0100\r\n"), "no lowercase step line\n");
        ok(DumpContains("\r\n\0|       |.. psUIData:\r\n"), "no UI data block\n");
        ok_hex(Dump(ProvData), S_OK);
        Handle = CreateFileW(L"C:\\TRUSTPOL.TXT", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        ok_hex(Dump(ProvData), S_FALSE);
        ok_hex(GetLastError(), ERROR_SHARING_VIOLATION);
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
        DeleteFileW(L"C:\\TRUSTPOL.TXT");
    }
    Data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Generic, &Data);
}

START_TEST(Internals)
{
    Wintrust = GetModuleHandleW(L"wintrust.dll");
    GetTempPathW(RTL_NUMBER_OF(TempDir), TempDir);
    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));

    TestStores();
    TestHashTag();
    TestCdfByTag();
    TestSipInfo();
    TestFirstPageHash();
    TestDecode();
    TestAgencyAndUsage();
    TestChainedToMicrosoft();
    TestOfficePolicy();
    TestFixedResults();
    TestConfigCi();
    TestCatalogDetails();
    TestProviderData();
    TestDump();
}
