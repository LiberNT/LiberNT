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

START_TEST(CatalogV2)
{
    HMODULE Wintrust = GetModuleHandleW(L"wintrust.dll");

    TestMemberInfo2Encoding(Wintrust);
    TestVersion2Catalog();
}
