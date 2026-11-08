/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test SIP capabilities and the indirect data verification of the wintrust SIPs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <mssip.h>
#include <stdio.h>

typedef struct
{
    DWORD cbSize;
    DWORD dwVersion;
    BOOL isMultiSign;
    DWORD dwFlags;
} TEST_CAP_SET;

typedef BOOL (WINAPI *PFN_GET_CAPS)(SIP_SUBJECTINFO *, TEST_CAP_SET *);

static const GUID PeSubject = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID JavaSubject = { 0xC689AAB9, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID CabSubject = { 0xC689AABA, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID FlatSubject = { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID CatalogSubject = { 0xDE351A43, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID StorageSubject = { 0x9BA61D3F, 0xE73A, 0x11D0, { 0x8C,0xD2,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID UnknownSubject = { 0x12345678, 0x1234, 0x1234, { 0x12,0x34,0x12,0x34,0x12,0x34,0x12,0x34 } };

static
BOOL
CallGetCaps(PFN_GET_CAPS GetCaps, const GUID *Subject, DWORD SubjectSize, TEST_CAP_SET *Caps)
{
    SIP_SUBJECTINFO Info;
    GUID SubjectType = *Subject;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = SubjectSize;
    Info.pgSubjectType = &SubjectType;
    SetLastError(0xdeadbeef);
    return GetCaps(&Info, Caps);
}

static
VOID
TestGetCaps(const char *Module, PFN_GET_CAPS GetCaps)
{
    static const struct
    {
        const GUID *Subject;
        BOOL Supported;
        DWORD Flags;
    } Cases[] =
    {
        { &PeSubject, TRUE, SIP_CAP_FLAG_SEALING },
        { &CabSubject, TRUE, 0 },
        { &FlatSubject, TRUE, 0 },
        { &CatalogSubject, TRUE, 0 },
        { &StorageSubject, TRUE, 0 },
        { &JavaSubject, FALSE, 0 },
        { &UnknownSubject, FALSE, 0 },
    };
    TEST_CAP_SET Caps;
    BYTE Large[24];
    TEST_CAP_SET *LargeCaps = (TEST_CAP_SET *)Large;
    DWORD i;
    BOOL Ret;

    for (i = 0; i < RTL_NUMBER_OF(Cases); i++)
    {
        ZeroMemory(&Caps, sizeof(Caps));
        Caps.cbSize = sizeof(Caps);
        Ret = CallGetCaps(GetCaps, Cases[i].Subject, sizeof(SIP_SUBJECTINFO), &Caps);
        if (Cases[i].Supported)
        {
            ok(Ret, "%s case %lu: GetCaps failed with %lu\n", Module, i, GetLastError());
            ok_long(Caps.cbSize, sizeof(Caps));
            ok_long(Caps.dwVersion, 3);
            ok_int(Caps.isMultiSign, TRUE);
            ok_hex(Caps.dwFlags, Cases[i].Flags);
        }
        else
        {
            ok(!Ret, "%s case %lu: GetCaps succeeded\n", Module, i);
            ok_hex(GetLastError(), TRUST_E_SUBJECT_FORM_UNKNOWN);
            ok_long(Caps.dwVersion, 0);
        }
    }

    ZeroMemory(&Caps, sizeof(Caps));
    Caps.cbSize = sizeof(Caps);
    Caps.dwFlags = SIP_CAP_FLAG_SEALING;
    ok(!CallGetCaps(GetCaps, &PeSubject, sizeof(SIP_SUBJECTINFO), &Caps), "%s: nonzero input flags accepted\n", Module);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    ok_long(Caps.dwVersion, 0);

    ZeroMemory(&Caps, sizeof(Caps));
    Caps.cbSize = 12;
    ok(!CallGetCaps(GetCaps, &PeSubject, sizeof(SIP_SUBJECTINFO), &Caps), "%s: short caps accepted\n", Module);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);

    ZeroMemory(&Caps, sizeof(Caps));
    Caps.cbSize = sizeof(Caps);
    Caps.dwVersion = 2;
    ok(CallGetCaps(GetCaps, &PeSubject, sizeof(SIP_SUBJECTINFO), &Caps), "%s: input version 2 refused\n", Module);
    ok_long(Caps.dwVersion, 3);

    FillMemory(Large, sizeof(Large), 0xcc);
    LargeCaps->cbSize = 20;
    LargeCaps->dwFlags = 0;
    ok(CallGetCaps(GetCaps, &PeSubject, sizeof(SIP_SUBJECTINFO), LargeCaps), "%s: large caps refused\n", Module);
    ok_long(LargeCaps->cbSize, sizeof(Caps));
    ok_hex(Large[16], 0xcc);
}

static
SIP_INDIRECT_DATA *
CreateIndirect(const GUID *Subject, const WCHAR *Path, LPSTR Algorithm)
{
    SIP_SUBJECTINFO Info;
    GUID SubjectType = *Subject;
    SIP_INDIRECT_DATA *Data = NULL;
    DWORD Size = 0;
    BOOL Ret;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &SubjectType;
    Info.pwsFileName = Path;
    Info.hFile = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Info.DigestAlgorithm.pszObjId = Algorithm;
    Ret = Info.hFile != INVALID_HANDLE_VALUE && CryptSIPCreateIndirectData(&Info, &Size, NULL) &&
          (Data = HeapAlloc(GetProcessHeap(), 0, Size)) && CryptSIPCreateIndirectData(&Info, &Size, Data);
    if (Info.hFile != INVALID_HANDLE_VALUE)
        CloseHandle(Info.hFile);
    ok(Ret, "CryptSIPCreateIndirectData(%ls, %s) failed with %lu\n", Path, Algorithm, GetLastError());
    if (!Ret && Data)
    {
        HeapFree(GetProcessHeap(), 0, Data);
        Data = NULL;
    }
    return Data;
}

static
BOOL
VerifyIndirect(const GUID *Subject, const WCHAR *Path, SIP_INDIRECT_DATA *Data, DWORD Choice, PVOID AddInfo)
{
    SIP_SUBJECTINFO Info;
    GUID SubjectType = *Subject;
    BOOL Ret;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &SubjectType;
    Info.pwsFileName = Path;
    Info.hFile = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    Info.DigestAlgorithm = Data->DigestAlgorithm;
    Info.dwUnionChoice = Choice;
    if (Choice == MSSIP_ADDINFO_FLAT)
        Info.psFlat = AddInfo;
    SetLastError(0xdeadbeef);
    Ret = CryptSIPVerifyIndirectData(&Info, Data);
    if (Info.hFile != INVALID_HANDLE_VALUE)
        CloseHandle(Info.hFile);
    return Ret;
}

static
BOOL
WriteDataFile(const WCHAR *Path, const void *Data, DWORD Size)
{
    HANDLE File;
    DWORD Written;
    BOOL Ret;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(File, Data, Size, &Written, NULL);
    CloseHandle(File);
    return Ret;
}

static
BOOL
WriteTextFile(const WCHAR *Path, const char *Text)
{
    return WriteDataFile(Path, Text, (DWORD)strlen(Text));
}

static
BOOL
WriteCabinet(const WCHAR *Path, const char *Contents)
{
    static const BYTE Header[] = {
        'M','S','C','F',0,0,0,0,0x63,0,0,0,0,0,0,0,0x2c,0,0,0,0,0,0,0,3,1,1,0,1,0,0,0,0x34,0x12,0,0,
        0x42,0,0,0,1,0,0,0,0x19,0,0,0,0,0,0,0,0,0,0x21,0x5b,0,0x60,0x20,0,'a','.','t','x','t',0,
        0,0,0,0,0x19,0,0x19,0 };
    BYTE Cabinet[sizeof(Header) + 25];

    if (strlen(Contents) != 25)
        return FALSE;
    memcpy(Cabinet, Header, sizeof(Header));
    memcpy(Cabinet + sizeof(Header), Contents, 25);
    return WriteDataFile(Path, Cabinet, sizeof(Cabinet));
}

static
SIP_INDIRECT_DATA *
CreateCabinetIndirect(const WCHAR *Path, HANDLE File)
{
    GUID SubjectType = CabSubject;
    SIP_SUBJECTINFO Info;
    SIP_INDIRECT_DATA *Data;
    DWORD Size = 0;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &SubjectType;
    Info.pwsFileName = Path;
    Info.hFile = File;
    Info.DigestAlgorithm.pszObjId = (LPSTR)szOID_NIST_sha256;
    if (!CryptSIPCreateIndirectData(&Info, &Size, NULL) || !(Data = HeapAlloc(GetProcessHeap(), 0, Size)))
        return NULL;
    SetLastError(0xdeadbeef);
    if (CryptSIPCreateIndirectData(&Info, &Size, Data))
        return Data;
    HeapFree(GetProcessHeap(), 0, Data);
    return NULL;
}

static
BOOL
FileMatches(const WCHAR *Path, const BYTE *Expected, DWORD ExpectedSize)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE Buffer[256];
    DWORD Read = 0;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(File, Buffer, sizeof(Buffer), &Read, NULL);
    CloseHandle(File);
    return Read == ExpectedSize && !memcmp(Buffer, Expected, Read);
}

static
VOID
TestUnsignedImage(VOID)
{
    typedef BOOL (WINAPI *PFN_CERT_HEADER)(HANDLE, DWORD, LPWIN_CERTIFICATE);
    PFN_CERT_HEADER CertHeader = (PFN_CERT_HEADER)GetProcAddress(LoadLibraryW(L"imagehlp.dll"), "ImageGetCertificateHeader");
    GUID Subject = PeSubject;
    SIP_SUBJECTINFO Info;
    WIN_CERTIFICATE Header;
    WCHAR Self[MAX_PATH];
    DWORD Size = 0, Encoding = 0;
    BYTE Buffer[64];

    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = Self;
    Info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    Info.hFile = CreateFileW(Self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (CertHeader)
    {
        SetLastError(0xdeadbeef);
        ok(!CertHeader(Info.hFile, 0, &Header), "certificate found\n");
        ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    }
    SetLastError(0xdeadbeef);
    ok(!CryptSIPGetSignedDataMsg(&Info, &Encoding, 0, &Size, NULL), "signed message found\n");
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    Size = sizeof(Buffer);
    SetLastError(0xdeadbeef);
    ok(!CryptSIPGetSignedDataMsg(&Info, &Encoding, 0, &Size, Buffer), "signed message read\n");
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    CloseHandle(Info.hFile);
    Info.hFile = NULL;
    Info.pwsFileName = L"lntsip missing";
    SetLastError(0xdeadbeef);
    ok(!CryptSIPGetSignedDataMsg(&Info, &Encoding, 0, &Size, NULL), "signed message of a missing file\n");
    ok_hex(GetLastError(), ERROR_FILE_NOT_FOUND);
}

static
VOID
TestCabinetIndirectData(const WCHAR *Temp)
{
    static const BYTE Prepared[] = {
        0x4d,0x53,0x43,0x46,0x00,0x00,0x00,0x00,0x7b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x03,0x01,0x01,0x00,0x01,0x00,0x04,0x00,0x34,0x12,0x00,0x00,0x14,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x5a,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x19,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x21,0x5b,0x00,0x60,0x20,0x00,0x61,0x2e,0x74,0x78,0x74,0x00,0x00,0x00,0x00,0x00,0x19,0x00,
        0x19,0x00,0x63,0x61,0x62,0x20,0x6d,0x65,0x6d,0x62,0x65,0x72,0x20,0x6f,0x6e,0x65,0x20,0x63,0x6f,0x6e,0x74,0x65,0x6e,0x74,
        0x73,0x0d,0x0a };
    static const BYTE Digest[] = {
        0x6e,0xa7,0x05,0xc5,0x13,0xbb,0x9f,0x39,0x2e,0x16,0x9a,0x15,0x94,0x71,0x99,0xb0,
        0x3e,0xe8,0xa2,0x76,0x6b,0xed,0xce,0x7b,0x7c,0xc4,0xe3,0x37,0x25,0x97,0x0b,0x5a };
    WCHAR First[MAX_PATH], Copy[MAX_PATH], Second[MAX_PATH];
    SIP_INDIRECT_DATA *Data;
    LARGE_INTEGER Zero = { { 0 } }, Position;
    BYTE Original[256];
    DWORD OriginalSize = 0;
    HANDLE File;

    swprintf(First, RTL_NUMBER_OF(First), L"%lslntsipa.cab", Temp);
    swprintf(Copy, RTL_NUMBER_OF(Copy), L"%lslntsipc.cab", Temp);
    swprintf(Second, RTL_NUMBER_OF(Second), L"%lslntsipb.cab", Temp);
    if (!WriteCabinet(First, "cab member one contents\r\n") || !WriteCabinet(Copy, "cab member one contents\r\n") ||
        !WriteCabinet(Second, "cab member two contents\r\n"))
    {
        skip("Cannot write the cabinets\n");
        return;
    }
    File = CreateFileW(First, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ReadFile(File, Original, sizeof(Original), &OriginalSize, NULL);
    ok(!CreateCabinetIndirect(First, File), "CAB indirect data created through a read-only handle\n");
    ok_hex(GetLastError(), ERROR_ACCESS_DENIED);
    CloseHandle(File);
    ok(FileMatches(First, Original, OriginalSize), "the cabinet changed after a refused creation\n");

    SetFileAttributesW(First, FILE_ATTRIBUTE_READONLY);
    ok(!CreateCabinetIndirect(First, NULL), "CAB indirect data created for a read-only file\n");
    ok_hex(GetLastError(), ERROR_ACCESS_DENIED);
    SetFileAttributesW(First, FILE_ATTRIBUTE_NORMAL);

    File = CreateFileW(First, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Data = CreateCabinetIndirect(First, File);
    ok(Data != NULL, "CAB indirect data through a read-write handle failed with %lu\n", GetLastError());
    ok(SetFilePointerEx(File, Zero, &Position, FILE_CURRENT) && Position.QuadPart == sizeof(Prepared),
       "handle position %I64d\n", Position.QuadPart);
    CloseHandle(File);
    ok(FileMatches(First, Prepared, sizeof(Prepared)), "the cabinet was not prepared for a signature\n");
    if (Data)
    {
        ok(!strcmp(Data->Data.pszObjId, SPC_LINK_OBJID), "data %s\n", Data->Data.pszObjId);
        ok(Data->Digest.cbData == sizeof(Digest) && !memcmp(Data->Digest.pbData, Digest, sizeof(Digest)),
           "unexpected digest\n");
        ok(VerifyIndirect(&CabSubject, First, Data, MSSIP_ADDINFO_NONE, NULL), "CAB verify failed with %lu\n", GetLastError());
        ok(FileMatches(First, Prepared, sizeof(Prepared)), "the cabinet changed after a verification\n");
        ok(!VerifyIndirect(&CabSubject, Copy, Data, MSSIP_ADDINFO_NONE, NULL), "CAB verify of the unprepared copy succeeded\n");
        ok_hex(GetLastError(), TRUST_E_BAD_DIGEST);
        ok(FileMatches(Copy, Original, OriginalSize), "the unprepared copy changed after a verification\n");
        ok(!VerifyIndirect(&CabSubject, Second, Data, MSSIP_ADDINFO_NONE, NULL), "CAB verify of another cabinet succeeded\n");
        ok_hex(GetLastError(), TRUST_E_BAD_DIGEST);
        HeapFree(GetProcessHeap(), 0, Data);
    }

    Data = CreateCabinetIndirect(Copy, NULL);
    ok(Data != NULL, "CAB indirect data by path failed with %lu\n", GetLastError());
    ok(FileMatches(Copy, Prepared, sizeof(Prepared)), "the cabinet was not prepared by path\n");
    if (Data)
        HeapFree(GetProcessHeap(), 0, Data);
    DeleteFileW(First);
    DeleteFileW(Copy);
    DeleteFileW(Second);
    TestUnsignedImage();
}

static
VOID
TestVerifyIndirectData(VOID)
{
    WCHAR Self[MAX_PATH], Other[MAX_PATH], Temp[MAX_PATH], Member[MAX_PATH], OtherFlat[MAX_PATH];
    SIP_INDIRECT_DATA *Pe256, *Pe1, *Flat256;

    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));
    GetSystemDirectoryW(Other, RTL_NUMBER_OF(Other));
    wcscat(Other, L"\\kernel32.dll");
    GetTempPathW(RTL_NUMBER_OF(Temp), Temp);
    swprintf(Member, RTL_NUMBER_OF(Member), L"%lslntsipm.bin", Temp);
    swprintf(OtherFlat, RTL_NUMBER_OF(OtherFlat), L"%lslntsipo.bin", Temp);
    if (!WriteTextFile(Member, "member contents") || !WriteTextFile(OtherFlat, "other contents"))
    {
        skip("Cannot write the flat files\n");
        return;
    }

    Pe256 = CreateIndirect(&PeSubject, Self, szOID_NIST_sha256);
    Pe1 = CreateIndirect(&PeSubject, Self, szOID_OIWSEC_sha1);
    Flat256 = CreateIndirect(&FlatSubject, Member, szOID_NIST_sha256);

    if (Pe256)
    {
        ok(VerifyIndirect(&PeSubject, Self, Pe256, MSSIP_ADDINFO_NONE, NULL), "PE SHA-256 verify failed with %lu\n", GetLastError());
        ok(!VerifyIndirect(&PeSubject, Other, Pe256, MSSIP_ADDINFO_NONE, NULL), "PE SHA-256 verify of another file succeeded\n");
        ok_hex(GetLastError(), TRUST_E_BAD_DIGEST);
        HeapFree(GetProcessHeap(), 0, Pe256);
    }
    if (Pe1)
    {
        ok(VerifyIndirect(&PeSubject, Self, Pe1, MSSIP_ADDINFO_NONE, NULL), "PE SHA-1 verify failed with %lu\n", GetLastError());
        ok(!VerifyIndirect(&PeSubject, Other, Pe1, MSSIP_ADDINFO_NONE, NULL), "PE SHA-1 verify of another file succeeded\n");
        ok_hex(GetLastError(), TRUST_E_BAD_DIGEST);
        HeapFree(GetProcessHeap(), 0, Pe1);
    }
    if (Flat256)
    {
        MS_ADDINFO_FLAT Flat = { sizeof(Flat), Flat256 };

        ok(!VerifyIndirect(&FlatSubject, Member, Flat256, MSSIP_ADDINFO_NONE, NULL), "flat verify succeeded\n");
        ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
        ok(!VerifyIndirect(&FlatSubject, Member, Flat256, MSSIP_ADDINFO_FLAT, &Flat), "flat verify with flat info succeeded\n");
        ok_hex(GetLastError(), ERROR_SUCCESS);
        ok(!VerifyIndirect(&FlatSubject, OtherFlat, Flat256, MSSIP_ADDINFO_FLAT, &Flat), "flat verify of another file succeeded\n");
        ok_hex(GetLastError(), ERROR_SUCCESS);
        HeapFree(GetProcessHeap(), 0, Flat256);
    }

    DeleteFileW(Member);
    DeleteFileW(OtherFlat);
    TestCabinetIndirectData(Temp);
}

START_TEST(SipCaps)
{
    PFN_GET_CAPS WintrustGetCaps, Crypt32GetCaps;

    WintrustGetCaps = (PFN_GET_CAPS)GetProcAddress(GetModuleHandleW(L"wintrust.dll"), "CryptSIPGetCaps");
    Crypt32GetCaps = (PFN_GET_CAPS)GetProcAddress(GetModuleHandleW(L"crypt32.dll"), "CryptSIPGetCaps");
    ok(WintrustGetCaps != NULL, "wintrust.dll does not export CryptSIPGetCaps\n");
    ok(Crypt32GetCaps != NULL, "crypt32.dll does not export CryptSIPGetCaps\n");
    if (WintrustGetCaps)
        TestGetCaps("wintrust", WintrustGetCaps);
    if (Crypt32GetCaps)
        TestGetCaps("crypt32", Crypt32GetCaps);
    if (WintrustGetCaps)
    {
        TEST_CAP_SET Caps = { sizeof(Caps) };

        ok(!CallGetCaps(WintrustGetCaps, &PeSubject, FIELD_OFFSET(SIP_SUBJECTINFO, pgSubjectType), &Caps),
           "short subject info accepted\n");
        ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);
    }

    TestVerifyIndirectData();
}
