/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test version 2 catalogs and the CAT_MEMBERINFO2 encoding
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <mssip.h>
#include <mscat.h>
#include <stdio.h>

typedef struct
{
    GUID SubjectGuid;
    DWORD dwCertVersion;
} TEST_MEMBERINFO2;

typedef BOOL (WINAPI *PFN_ENCODE)(DWORD, LPCSTR, const void *, BYTE *, DWORD *);
typedef BOOL (WINAPI *PFN_DECODE)(DWORD, LPCSTR, const BYTE *, DWORD, DWORD, void *, DWORD *);

static const GUID PeSubject = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID CabSubject = { 0xC689AABA, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID FlatSubject = { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID OtherSubject = { 0x603BCC1F, 0x4B59, 0x4E08, { 0xB7,0x24,0xD2,0xC6,0x29,0x7E,0xF3,0x51 } };

static
VOID
TestMemberInfo2Encoding(HMODULE Wintrust)
{
    static const BYTE PeEncoded[] = { 0x80, 0x00 };
    static const BYTE CabEncoded[] = { 0x81, 0x00 };
    static const BYTE FlatEncoded[] = { 0x82, 0x00 };
    static const BYTE PeVersion1Encoded[] = { 0x85, 0x10, 0xb8, 0xaa, 0x89, 0xc6, 0x78, 0x8e, 0xd0, 0x11, 0x8c, 0x47,
                                              0x00, 0xc0, 0x4f, 0xc2, 0x95, 0xee, 0x02, 0x02, 0x01, 0x00 };
    static const BYTE OtherEncoded[] = { 0x85, 0x10, 0x1f, 0xcc, 0x3b, 0x60, 0x59, 0x4b, 0x08, 0x4e, 0xb7, 0x24,
                                         0xd2, 0xc6, 0x29, 0x7e, 0xf3, 0x51, 0x02, 0x02, 0x02, 0x00 };
    static const struct
    {
        const GUID *Subject;
        DWORD Version;
        const BYTE *Encoded;
        DWORD Size;
    } Cases[] =
    {
        { &PeSubject, 0x200, PeEncoded, sizeof(PeEncoded) },
        { &CabSubject, 0x200, CabEncoded, sizeof(CabEncoded) },
        { &FlatSubject, 0x200, FlatEncoded, sizeof(FlatEncoded) },
        { &PeSubject, 0x100, PeVersion1Encoded, sizeof(PeVersion1Encoded) },
        { &OtherSubject, 0x200, OtherEncoded, sizeof(OtherEncoded) },
    };
    PFN_ENCODE Encode = (PFN_ENCODE)GetProcAddress(Wintrust, "WVTAsn1CatMemberInfo2Encode");
    PFN_DECODE Decode = (PFN_DECODE)GetProcAddress(Wintrust, "WVTAsn1CatMemberInfo2Decode");
    TEST_MEMBERINFO2 Info, Decoded;
    BYTE Buffer[64];
    DWORD Size, i;

    ok(Encode != NULL && Decode != NULL, "WVTAsn1CatMemberInfo2Encode/Decode are not exported\n");
    if (!Encode || !Decode)
        return;

    for (i = 0; i < RTL_NUMBER_OF(Cases); i++)
    {
        Info.SubjectGuid = *Cases[i].Subject;
        Info.dwCertVersion = Cases[i].Version;
        Size = sizeof(Buffer);
        ok(Encode(X509_ASN_ENCODING, (LPCSTR)2223, &Info, Buffer, &Size), "case %lu: encode failed %lu\n", i, GetLastError());
        ok(Size == Cases[i].Size && !memcmp(Buffer, Cases[i].Encoded, Size), "case %lu: unexpected encoding of %lu bytes\n",
           i, Size);

        Size = sizeof(Decoded);
        memset(&Decoded, 0xcc, sizeof(Decoded));
        ok(Decode(X509_ASN_ENCODING, (LPCSTR)2223, Cases[i].Encoded, Cases[i].Size, 0, &Decoded, &Size),
           "case %lu: decode failed %lu\n", i, GetLastError());
        ok(Size == sizeof(Decoded), "case %lu: decoded size %lu\n", i, Size);
        ok(IsEqualGUID(&Decoded.SubjectGuid, Cases[i].Subject), "case %lu: wrong subject\n", i);
        ok(Decoded.dwCertVersion == Cases[i].Version, "case %lu: version %#lx\n", i, Decoded.dwCertVersion);
    }
}

static
BOOL
CreateFlatIndirectData(PCWSTR Path, SIP_INDIRECT_DATA **Indirect, DWORD *Size)
{
    SIP_SUBJECTINFO Info;
    GUID Subject = FlatSubject;
    BOOL Result;

    memset(&Info, 0, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = Path;
    Info.hFile = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Info.DigestAlgorithm.pszObjId = szOID_NIST_sha256;
    if (Info.hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    *Size = 0;
    *Indirect = NULL;
    Result = CryptSIPCreateIndirectData(&Info, Size, NULL) && (*Indirect = HeapAlloc(GetProcessHeap(), 0, *Size)) &&
             CryptSIPCreateIndirectData(&Info, Size, *Indirect);
    CloseHandle(Info.hFile);
    return Result;
}

static
VOID
TestVersion2Catalog(VOID)
{
    WCHAR Directory[MAX_PATH], Catalog[MAX_PATH], Member[MAX_PATH], Tag[65], LowerTag[65];
    SIP_INDIRECT_DATA *Indirect;
    CRYPTCATMEMBER *Found, *Enumerated;
    GUID Subject = FlatSubject;
    HANDLE File, Handle;
    DWORD Size, Written, i;

    GetTempPathW(RTL_NUMBER_OF(Directory), Directory);
    swprintf(Catalog, RTL_NUMBER_OF(Catalog), L"%lslntcatv2.cat", Directory);
    swprintf(Member, RTL_NUMBER_OF(Member), L"%lslntcatv2.bin", Directory);
    File = CreateFileW(Member, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "cannot create the member file\n");
    if (File == INVALID_HANDLE_VALUE)
        return;
    WriteFile(File, "LiberNT catalog member", 22, &Written, NULL);
    CloseHandle(File);

    if (!CreateFlatIndirectData(Member, &Indirect, &Size))
    {
        ok(FALSE, "CryptSIPCreateIndirectData failed %lu\n", GetLastError());
        DeleteFileW(Member);
        return;
    }
    ok(Indirect->Digest.cbData == 32, "digest size %lu\n", Indirect->Digest.cbData);
    for (i = 0; i < Indirect->Digest.cbData; i++)
    {
        swprintf(Tag + i * 2, 3, L"%02X", Indirect->Digest.pbData[i]);
        swprintf(LowerTag + i * 2, 3, L"%02x", Indirect->Digest.pbData[i]);
    }

    Handle = CryptCATOpen(Catalog, CRYPTCAT_OPEN_CREATENEW, 0, CRYPTCAT_VERSION_2, 0);
    ok(Handle != INVALID_HANDLE_VALUE, "CryptCATOpen failed %lu\n", GetLastError());
    if (Handle == INVALID_HANDLE_VALUE)
        return;
    ok(CryptCATPutMemberInfo(Handle, Member, Tag, &Subject, 0x200, Size, (BYTE *)Indirect) != NULL,
       "CryptCATPutMemberInfo failed %lu\n", GetLastError());
    ok(CryptCATPersistStore(Handle), "CryptCATPersistStore failed %lu\n", GetLastError());
    CryptCATClose(Handle);

    Handle = CryptCATOpen(Catalog, CRYPTCAT_OPEN_EXISTING, 0, 0, 0);
    ok(Handle != INVALID_HANDLE_VALUE, "CryptCATOpen failed %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        Enumerated = CryptCATEnumerateMember(Handle, NULL);
        ok(Enumerated != NULL, "CryptCATEnumerateMember failed %lu\n", GetLastError());
        if (Enumerated)
        {
            ok(!wcscmp(Enumerated->pwszReferenceTag, Tag), "tag %ls\n", Enumerated->pwszReferenceTag);
            ok(IsEqualGUID(&Enumerated->gSubjectType, &FlatSubject), "wrong subject\n");
            ok(Enumerated->dwCertVersion == 0x200, "version %#lx\n", Enumerated->dwCertVersion);
            ok(Enumerated->dwReserved == 0, "reserved %lu\n", Enumerated->dwReserved);
            ok(Enumerated->pwszFileName == NULL, "file name %ls\n", Enumerated->pwszFileName);
            ok(Enumerated->sEncodedMemberInfo.cbData == 2 && Enumerated->sEncodedMemberInfo.pbData[0] == 0x82 &&
               Enumerated->sEncodedMemberInfo.pbData[1] == 0, "unexpected member info encoding\n");
            ok(Enumerated->pIndirectData && Enumerated->pIndirectData->Digest.cbData == 32 &&
               !memcmp(Enumerated->pIndirectData->Digest.pbData, Indirect->Digest.pbData, 32), "wrong digest\n");
        }

        SetLastError(0xdeadbeef);
        Found = CryptCATGetMemberInfo(Handle, Tag);
        ok(Found != NULL, "CryptCATGetMemberInfo(%ls) failed %lu\n", Tag, GetLastError());
        SetLastError(0xdeadbeef);
        Found = CryptCATGetMemberInfo(Handle, LowerTag);
        ok(Found == NULL, "lowercase tag found %p\n", Found);
        ok(GetLastError() == 0xdeadbeef, "lowercase tag error %lu\n", GetLastError());
        CryptCATClose(Handle);
    }

    HeapFree(GetProcessHeap(), 0, Indirect);
    DeleteFileW(Catalog);
    DeleteFileW(Member);
}

static
BOOL
WriteText(PCWSTR Path, const char *Text)
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
DWORD
FileSize(PCWSTR Path)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD Size;

    if (File == INVALID_HANDLE_VALUE)
        return 0;
    Size = GetFileSize(File, NULL);
    CloseHandle(File);
    return Size;
}

static
CRYPTCATCDF *
OpenDefinition(PCWSTR Cdf, PCWSTR Cat, PCWSTR Member1, PCWSTR Member2, const char *Header)
{
    WCHAR Directory[MAX_PATH];
    char Text[2048];

    GetTempPathW(RTL_NUMBER_OF(Directory), Directory);
    DeleteFileW(Cat);
    snprintf(Text, sizeof(Text), "[CatalogHeader]\r\nName=lntwtv2.cat\r\nResultDir=%ls\r\n%sEncodingType=0x00010001\r\n"
             "CATATTR1=0x10010001:Note:first\r\n[CatalogFiles]\r\nm1=%ls\r\nm1ATTR1=0x10010001:MemberNote:mine\r\n"
             "<HASH>m2=%ls\r\n", Directory, Header, Member1, Member2);
    if (!WriteText(Cdf, Text))
        return NULL;
    return CryptCATCDFOpen((LPWSTR)Cdf, NULL);
}

static
VOID
CheckWrittenVersion2(PCWSTR Cat, DWORD ExpectedSize)
{
    CRYPTCATSTORE *Store;
    HANDLE Handle;

    ok_long(FileSize(Cat), ExpectedSize);
    Handle = CryptCATOpen((LPWSTR)Cat, CRYPTCAT_OPEN_EXISTING, 0, 0, 0);
    ok(Handle != INVALID_HANDLE_VALUE, "CryptCATOpen failed %lu\n", GetLastError());
    if (Handle == INVALID_HANDLE_VALUE)
        return;
    Store = CryptCATStoreFromHandle(Handle);
    ok(Store && Store->dwPublicVersion == CRYPTCAT_VERSION_2, "store version %#lx\n", Store ? Store->dwPublicVersion : 0);
    ok(CryptCATEnumerateMember(Handle, NULL) == NULL, "the catalog has members\n");
    CryptCATClose(Handle);
}

static
VOID
TestVersion2Definition(VOID)
{
    WCHAR Directory[MAX_PATH], Cdf[MAX_PATH], Cat[MAX_PATH], Member1[MAX_PATH], Member2[MAX_PATH];
    CRYPTCATATTRIBUTE *Attribute;
    CRYPTCATMEMBER *Member;
    CRYPTCATCDF *Definition;
    LPWSTR Tag;

    GetTempPathW(RTL_NUMBER_OF(Directory), Directory);
    swprintf(Cdf, RTL_NUMBER_OF(Cdf), L"%lslntwtv2.cdf", Directory);
    swprintf(Cat, RTL_NUMBER_OF(Cat), L"%lslntwtv2.cat", Directory);
    swprintf(Member1, RTL_NUMBER_OF(Member1), L"%lslntwtv2-m1.txt", Directory);
    swprintf(Member2, RTL_NUMBER_OF(Member2), L"%lslntwtv2-m2.txt", Directory);
    ok(WriteText(Member1, "member one\r\n") && WriteText(Member2, "member two\r\n"), "Cannot write the members\n");

    SetLastError(0xdeadbeef);
    ok(OpenDefinition(Cdf, Cat, Member1, Member2, "CatalogVersion=2\r\n") == NULL, "opened without HashAlgorithms\n");
    ok_err(ERROR_INVALID_PARAMETER);

    /* The old member API hands out the literal tags of a version 2 definition, then writes no catalog */
    Definition = OpenDefinition(Cdf, Cat, Member1, Member2, "CatalogVersion=2\r\nHashAlgorithms=SHA256\r\n");
    ok(Definition != NULL, "CryptCATCDFOpen failed %lu\n", GetLastError());
    if (Definition)
    {
        SetLastError(0xdeadbeef);
        Member = CryptCATCDFEnumMembers(Definition, NULL, NULL);
        ok(Member && !wcscmp(Member->pwszReferenceTag, L"m1"), "first member %ls\n", Member ? Member->pwszReferenceTag : NULL);
        ok_err(ERROR_SUCCESS);
        SetLastError(0xdeadbeef);
        Attribute = Member ? CryptCATCDFEnumAttributes(Definition, Member, NULL, NULL) : NULL;
        ok(Attribute && !wcscmp(Attribute->pwszReferenceTag, L"MemberNote"), "member attribute %ls\n",
           Attribute ? Attribute->pwszReferenceTag : NULL);
        ok_err(0xdeadbeef);
        ok(!Attribute || !CryptCATCDFEnumAttributes(Definition, Member, Attribute, NULL), "second member attribute\n");
        ok_err(0xdeadbeef);
        Member = Member ? CryptCATCDFEnumMembers(Definition, Member, NULL) : NULL;
        ok(Member && !wcscmp(Member->pwszReferenceTag, L"<HASH>m2"), "second member %ls\n",
           Member ? Member->pwszReferenceTag : NULL);
        SetLastError(0xdeadbeef);
        ok(!Member || !CryptCATCDFEnumMembers(Definition, Member, NULL), "third member\n");
        ok_err(ERROR_SUCCESS);
        SetLastError(0xdeadbeef);
        ok_int(CryptCATCDFClose(Definition), FALSE);
        ok_err(ERROR_SUCCESS);
        ok(GetFileAttributesW(Cat) != INVALID_FILE_ATTRIBUTES, "the empty catalog was removed\n");
        ok_long(FileSize(Cat), 0);
    }

    Definition = OpenDefinition(Cdf, Cat, Member1, Member2, "CatalogVersion=2\r\nHashAlgorithms=SHA256\r\n");
    ok(Definition != NULL, "CryptCATCDFOpen failed %lu\n", GetLastError());
    if (Definition)
    {
        Member = (CRYPTCATMEMBER *)0xdeadbeef;
        SetLastError(0xdeadbeef);
        ok(CryptCATCDFEnumMembersByCDFTagEx(Definition, NULL, NULL, &Member, TRUE, NULL) == NULL, "got a tag\n");
        ok_err(ERROR_INVALID_PARAMETER);
        SetLastError(0xdeadbeef);
        ok_int(CryptCATCDFClose(Definition), TRUE);
        ok_err(ERROR_SUCCESS);
        CheckWrittenVersion2(Cat, 104);
    }

    Definition = OpenDefinition(Cdf, Cat, Member1, Member2, "CatalogVersion=2\r\nHashAlgorithms=SHA256\r\n");
    ok(Definition != NULL, "CryptCATCDFOpen failed %lu\n", GetLastError());
    if (Definition)
    {
        SetLastError(0xdeadbeef);
        Attribute = CryptCATCDFEnumCatAttributes(Definition, NULL, NULL);
        ok(Attribute && !wcscmp(Attribute->pwszReferenceTag, L"Note"), "catalog attribute %ls\n",
           Attribute ? Attribute->pwszReferenceTag : NULL);
        ok_err(0xdeadbeef);
        ok(!Attribute || !CryptCATCDFEnumCatAttributes(Definition, Attribute, NULL), "second catalog attribute\n");
        ok_err(0xdeadbeef);
        ok(CryptCATCDFEnumMembersByCDFTagEx(Definition, NULL, NULL, &Member, TRUE, NULL) == NULL, "got a tag\n");
        ok_int(CryptCATCDFClose(Definition), TRUE);
        CheckWrittenVersion2(Cat, 160);
    }

    Definition = OpenDefinition(Cdf, Cat, Member1, Member2, "PublicVersion=0x0000001\r\n");
    ok(Definition != NULL, "CryptCATCDFOpen failed %lu\n", GetLastError());
    if (Definition)
    {
        Member = NULL;
        SetLastError(0xdeadbeef);
        Tag = CryptCATCDFEnumMembersByCDFTagEx(Definition, NULL, NULL, &Member, TRUE, NULL);
        ok(Tag && !wcscmp(Tag, L"m1"), "first tag %ls\n", Tag);
        ok_err(ERROR_SUCCESS);
        SetLastError(0xdeadbeef);
        Attribute = Tag ? CryptCATCDFEnumAttributesWithCDFTag(Definition, Tag, Member, NULL, NULL) : NULL;
        ok(Attribute && !wcscmp(Attribute->pwszReferenceTag, L"MemberNote"), "member attribute %ls\n",
           Attribute ? Attribute->pwszReferenceTag : NULL);
        ok_err(0xdeadbeef);
        ok(!Attribute || !CryptCATCDFEnumAttributesWithCDFTag(Definition, Tag, Member, Attribute, NULL),
           "second member attribute\n");
        ok_err(0xdeadbeef);
        CryptCATCDFClose(Definition);
    }

    DeleteFileW(Cdf);
    DeleteFileW(Cat);
    DeleteFileW(Member1);
    DeleteFileW(Member2);
}

START_TEST(CatalogV2)
{
    HMODULE Wintrust = GetModuleHandleW(L"wintrust.dll");

    TestMemberInfo2Encoding(Wintrust);
    TestVersion2Catalog();
    TestVersion2Definition();
}
