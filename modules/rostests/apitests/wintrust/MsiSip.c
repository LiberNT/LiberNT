/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the digest of the MSI SIP
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#define COBJMACROS
#include <objbase.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <mssip.h>
#include <stdio.h>

static const GUID MsiSubject = { 0x000C10F1, 0x0000, 0x0000, { 0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 } };
static const CLSID MsiDatabase = { 0x000C1084, 0x0000, 0x0000, { 0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 } };
static const CLSID SubClass = { 0x4C4E5401, 0x1234, 0x5678, { 0x9A,0xBC,0xDE,0xF0,0x12,0x34,0x56,0x78 } };

#define NAME_LOW L"A\x0102"
#define NAME_HIGH L"A\x0201"
#define NAME_SIGNATURE L"\x0005" L"DigitalSignature"
#define NAME_SIGNATURE_EX L"\x0005" L"MsiDigitalSignatureEx"

static pCryptSIPCreateIndirectData pCreate;
static pCryptSIPVerifyIndirectData pVerify;
static HCRYPTPROV Provider;
static WCHAR MsiPath[MAX_PATH];

static
BOOL
WriteStream(IStorage *Storage, const WCHAR *Name, const void *Data, ULONG Size)
{
    IStream *Stream;
    ULONG Written;
    HRESULT hr;

    hr = IStorage_CreateStream(Storage, Name, STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &Stream);
    if (FAILED(hr))
        return FALSE;
    hr = IStream_Write(Stream, Data, Size, &Written);
    IStream_Release(Stream);
    return SUCCEEDED(hr) && Written == Size;
}

static
BOOL
CreateMsi(DWORD RootState)
{
    IStorage *Root, *Sub;
    HRESULT hr;
    BOOL Ret;

    hr = StgCreateDocfile(MsiPath, STGM_CREATE | STGM_DIRECT | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &Root);
    if (FAILED(hr))
        return FALSE;
    Ret = SUCCEEDED(IStorage_SetClass(Root, &MsiDatabase)) &&
          SUCCEEDED(IStorage_SetStateBits(Root, RootState, ~0u)) &&
          WriteStream(Root, NAME_LOW, "first", 5) &&
          WriteStream(Root, NAME_HIGH, "second", 6) &&
          WriteStream(Root, L"AB", "third", 5) &&
          WriteStream(Root, L"ABC", "", 0) &&
          WriteStream(Root, L"a", "fourth", 6);
    if (Ret)
    {
        hr = IStorage_CreateStorage(Root, L"Sub", STGM_CREATE | STGM_DIRECT | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                    0, 0, &Sub);
        Ret = SUCCEEDED(hr);
        if (Ret)
        {
            Ret = SUCCEEDED(IStorage_SetClass(Sub, &SubClass)) &&
                  SUCCEEDED(IStorage_SetStateBits(Sub, 5, ~0u)) &&
                  WriteStream(Sub, L"Inner", "inner", 5);
            IStorage_Release(Sub);
        }
    }
    IStorage_Release(Root);
    return Ret;
}

static
BOOL
EditMsi(const WCHAR *Name, const void *Data, ULONG Size, DWORD RootState)
{
    IStorage *Root;
    HRESULT hr;
    BOOL Ret = TRUE;

    hr = StgOpenStorage(MsiPath, NULL, STGM_DIRECT | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, NULL, 0, &Root);
    if (FAILED(hr))
        return FALSE;
    if (Name)
        Ret = WriteStream(Root, Name, Data, Size);
    if (Ret && RootState != ~0u)
        Ret = SUCCEEDED(IStorage_SetStateBits(Root, RootState, ~0u));
    IStorage_Release(Root);
    return Ret;
}

static
ULONG
ReadStream(const WCHAR *Name, BYTE *Data, ULONG Size)
{
    IStorage *Root;
    IStream *Stream;
    ULONG Read = 0;
    HRESULT hr;

    if (FAILED(StgOpenStorage(MsiPath, NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_DENY_WRITE, NULL, 0, &Root)))
        return ~0u;
    hr = IStorage_OpenStream(Root, Name, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &Stream);
    if (SUCCEEDED(hr))
    {
        hr = IStream_Read(Stream, Data, Size, &Read);
        IStream_Release(Stream);
    }
    IStorage_Release(Root);
    return SUCCEEDED(hr) ? Read : ~0u;
}

static
BOOL
SubStat(STATSTG *Stat)
{
    IStorage *Root, *Sub;
    HRESULT hr;

    if (FAILED(StgOpenStorage(MsiPath, NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_DENY_WRITE, NULL, 0, &Root)))
        return FALSE;
    hr = IStorage_OpenStorage(Root, L"Sub", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0, &Sub);
    if (SUCCEEDED(hr))
    {
        hr = IStorage_Stat(Sub, Stat, STATFLAG_NONAME);
        IStorage_Release(Sub);
    }
    IStorage_Release(Root);
    return SUCCEEDED(hr);
}

typedef struct _PIECE
{
    const void *Data;
    DWORD Size;
} PIECE;

static
DWORD
HashPieces(ALG_ID Algorithm, const PIECE *Pieces, DWORD Count, BYTE *Digest)
{
    HCRYPTHASH Hash;
    DWORD Size = 64, i;
    BOOL Ret;

    if (!CryptCreateHash(Provider, Algorithm, 0, 0, &Hash))
        return 0;
    Ret = TRUE;
    for (i = 0; Ret && i < Count; i++)
        Ret = !Pieces[i].Size || CryptHashData(Hash, Pieces[i].Data, Pieces[i].Size, 0);
    Ret = Ret && CryptGetHashParam(Hash, HP_HASHVAL, Digest, &Size, 0);
    CryptDestroyHash(Hash);
    return Ret ? Size : 0;
}

static
DWORD
ContentDigest(ALG_ID Algorithm, const BYTE *Prefix, DWORD PrefixSize, BOOL UnitOrder, BYTE *Digest)
{
    PIECE Pieces[] =
    {
        { Prefix, PrefixSize },
        { UnitOrder ? "first" : "second", UnitOrder ? 5 : 6 },
        { UnitOrder ? "second" : "first", UnitOrder ? 6 : 5 },
        { "third", 5 },
        { "inner", 5 },
        { &SubClass, sizeof(SubClass) },
        { "fourth", 6 },
        { &MsiDatabase, sizeof(MsiDatabase) },
    };

    return HashPieces(Algorithm, Pieces, RTL_NUMBER_OF(Pieces), Digest);
}

static
DWORD
MetadataDigest(ALG_ID Algorithm, DWORD RootState, BYTE *Digest)
{
    static const DWORD Zero[5] = { 0 };
    static const DWORD Sizes[] = { 6, 5, 5, 0, 5, 6 };
    static const DWORD SubState = 5;
    STATSTG Sub;
    PIECE Pieces[] =
    {
        { &MsiDatabase, sizeof(MsiDatabase) }, { &RootState, sizeof(DWORD) },
        { NAME_HIGH, 4 }, { &Sizes[0], 4 }, { Zero, 20 },
        { NAME_LOW, 4 }, { &Sizes[1], 4 }, { Zero, 20 },
        { L"AB", 4 }, { &Sizes[2], 4 }, { Zero, 20 },
        { L"ABC", 6 }, { &Sizes[3], 4 }, { Zero, 20 },
        { L"Sub", 6 }, { &SubClass, sizeof(SubClass) }, { &SubState, 4 }, { &Sub.ctime, 8 }, { &Sub.mtime, 8 },
        { L"Inner", 10 }, { &Sizes[4], 4 }, { Zero, 20 },
        { L"a", 2 }, { &Sizes[5], 4 }, { Zero, 20 },
    };

    if (!SubStat(&Sub))
        return 0;
    return HashPieces(Algorithm, Pieces, RTL_NUMBER_OF(Pieces), Digest);
}

static
SIP_INDIRECT_DATA *
CreateIndirect(LPCSTR Algorithm, HCRYPTPROV Prov, BOOL *Ret, DWORD *Error)
{
    SIP_SUBJECTINFO Info;
    GUID Subject = MsiSubject;
    SIP_INDIRECT_DATA *Data = NULL;
    DWORD Size = 0;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = MsiPath;
    Info.hProv = Prov;
    Info.DigestAlgorithm.pszObjId = (LPSTR)Algorithm;
    Info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    SetLastError(0xdeadbeef);
    *Ret = pCreate(&Info, &Size, NULL);
    if (*Ret)
    {
        Data = HeapAlloc(GetProcessHeap(), 0, Size);
        *Ret = Data && pCreate(&Info, &Size, Data);
    }
    *Error = GetLastError();
    if (!*Ret && Data)
    {
        HeapFree(GetProcessHeap(), 0, Data);
        Data = NULL;
    }
    return Data;
}

static
BOOL
VerifyIndirect(pCryptSIPVerifyIndirectData Verify, HCRYPTPROV Prov, SIP_INDIRECT_DATA *Data, DWORD *Error)
{
    SIP_SUBJECTINFO Info;
    GUID Subject = MsiSubject;
    BOOL Ret;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = MsiPath;
    Info.hFile = CreateFileW(MsiPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Info.hProv = Prov;
    Info.DigestAlgorithm = Data->DigestAlgorithm;
    Info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    SetLastError(0xdeadbeef);
    Ret = Verify(&Info, Data);
    *Error = GetLastError();
    if (Info.hFile != INVALID_HANDLE_VALUE)
        CloseHandle(Info.hFile);
    return Ret;
}

static
VOID
CheckIndirect(SIP_INDIRECT_DATA *Data, LPCSTR Algorithm, const BYTE *Digest, DWORD DigestSize, ULONG Line)
{
    static const BYTE SipInfo[] =
    {
        0x30, 0x24, 0x02, 0x01, 0x02, 0x04, 0x10, 0xf1, 0x10, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00,
        0x02, 0x01, 0x00, 0x02, 0x01, 0x00
    };

    ok_(__FILE__, Line)(!strcmp(Data->Data.pszObjId, SPC_SIGINFO_OBJID), "data OID %s\n", Data->Data.pszObjId);
    ok_(__FILE__, Line)(Data->Data.Value.cbData == sizeof(SipInfo) &&
                        !memcmp(Data->Data.Value.pbData, SipInfo, sizeof(SipInfo)), "unexpected SIP info\n");
    ok_(__FILE__, Line)(!strcmp(Data->DigestAlgorithm.pszObjId, Algorithm), "digest OID %s\n",
                        Data->DigestAlgorithm.pszObjId);
    ok_(__FILE__, Line)(Data->DigestAlgorithm.Parameters.cbData == 0, "parameters size %lu\n",
                        Data->DigestAlgorithm.Parameters.cbData);
    ok_(__FILE__, Line)(Data->Digest.cbData == DigestSize && !memcmp(Data->Digest.pbData, Digest, DigestSize),
                        "unexpected digest\n");
}

static
VOID
TestDigest(LPCSTR Algorithm, ALG_ID AlgId, HCRYPTPROV Prov)
{
    BYTE Content[64], Swapped[64], Metadata[64], Prefixed[64], Stream[80];
    DWORD ContentSize, SwappedSize, MetadataSize, PrefixedSize, Error;
    SIP_INDIRECT_DATA *Data;
    BOOL Ret;

    ok(CreateMsi(0x11), "cannot create the MSI\n");
    ContentSize = ContentDigest(AlgId, NULL, 0, FALSE, Content);
    SwappedSize = ContentDigest(AlgId, NULL, 0, TRUE, Swapped);
    ok(ContentSize && SwappedSize == ContentSize && memcmp(Content, Swapped, ContentSize),
       "the orders give the same digest\n");
    MetadataSize = MetadataDigest(AlgId, 0x11, Metadata);
    ok(MetadataSize == ContentSize, "metadata digest size %lu\n", MetadataSize);
    PrefixedSize = ContentDigest(AlgId, Metadata, MetadataSize, FALSE, Prefixed);

    Data = CreateIndirect(Algorithm, Prov, &Ret, &Error);
    ok(Ret, "create failed with 0x%lx\n", Error);
    if (!Data)
        return;
    CheckIndirect(Data, Algorithm, Prefixed, PrefixedSize, __LINE__);
    ok(ReadStream(NAME_SIGNATURE_EX, Stream, sizeof(Stream)) == MetadataSize && !memcmp(Stream, Metadata, MetadataSize),
       "unexpected MsiDigitalSignatureEx stream\n");
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify failed with 0x%lx\n", Error);

    ok(EditMsi(NAME_SIGNATURE, "signature", 9, ~0u), "cannot add the signature stream\n");
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify with a signature failed with 0x%lx\n", Error);

    ok(EditMsi(L"AB", "thirD", 5, ~0u), "cannot change a stream\n");
    ok(!VerifyIndirect(pVerify, Prov, Data, &Error), "verify of a changed stream succeeded\n");
    ok_hex(Error, TRUST_E_BAD_DIGEST);
    ok(EditMsi(L"AB", "third", 5, ~0u), "cannot restore a stream\n");
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify of a restored stream failed with 0x%lx\n", Error);

    ok(EditMsi(NULL, NULL, 0, 0x22), "cannot change the state bits\n");
    ok(!VerifyIndirect(pVerify, Prov, Data, &Error), "verify with other metadata succeeded\n");
    ok_hex(Error, TRUST_E_BAD_DIGEST);
    ok(EditMsi(NULL, NULL, 0, 0x11), "cannot restore the state bits\n");
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify with restored metadata failed with 0x%lx\n", Error);

    Data->Data.Value.pbData[4] = 1;
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify of SIP info version 1 failed with 0x%lx\n", Error);
    Data->Data.Value.pbData[4] = 3;
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify of SIP info version 3 failed with 0x%lx\n", Error);
    Data->Data.Value.pbData[4] = 2;

    ok(EditMsi(NAME_SIGNATURE_EX, Swapped, MetadataSize, ~0u), "cannot change the MsiDigitalSignatureEx stream\n");
    ok(!VerifyIndirect(pVerify, Prov, Data, &Error), "verify with a changed MsiDigitalSignatureEx succeeded\n");
    ok_hex(Error, TRUST_E_BAD_DIGEST);
    HeapFree(GetProcessHeap(), 0, Data);

    ok(CreateMsi(0x11), "cannot create the MSI\n");
    Data = CreateIndirect(Algorithm, Prov, &Ret, &Error);
    if (!Data)
        return;
    ok(CreateMsi(0x11), "cannot create the MSI\n");
    memcpy(Data->Digest.pbData, Content, ContentSize);
    ok(!VerifyIndirect(pVerify, Prov, Data, &Error), "verify of SIP info version 2 without MsiDigitalSignatureEx succeeded\n");
    ok_hex(Error, ERROR_BAD_FORMAT);
    Data->Data.Value.pbData[4] = 3;
    ok(!VerifyIndirect(pVerify, Prov, Data, &Error), "verify of SIP info version 3 without MsiDigitalSignatureEx succeeded\n");
    ok_hex(Error, ERROR_BAD_FORMAT);
    Data->Data.Value.pbData[4] = 0;
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify of SIP info version 0 failed with 0x%lx\n", Error);
    Data->Data.Value.pbData[4] = 1;
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify of the content digest failed with 0x%lx\n", Error);
    ok(ReadStream(NAME_SIGNATURE_EX, Stream, sizeof(Stream)) == ~0u, "verify added a MsiDigitalSignatureEx stream\n");
    ok(EditMsi(NULL, NULL, 0, 0x22), "cannot change the state bits\n");
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify of the content digest with other state bits failed with 0x%lx\n",
       Error);
    ok(EditMsi(L"Extra", "", 0, ~0u), "cannot add an empty stream\n");
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify of the content digest with an empty stream failed with 0x%lx\n",
       Error);
    ok(EditMsi(L"Extra", "x", 1, ~0u), "cannot fill the empty stream\n");
    ok(!VerifyIndirect(pVerify, Prov, Data, &Error), "verify of the content digest with a new stream succeeded\n");
    ok_hex(Error, TRUST_E_BAD_DIGEST);
    HeapFree(GetProcessHeap(), 0, Data);

    ok(CreateMsi(0x11), "cannot create the MSI\n");
    ok(EditMsi(NAME_SIGNATURE_EX, Swapped, SwappedSize, ~0u), "cannot add a MsiDigitalSignatureEx stream\n");
    Data = CreateIndirect(Algorithm, Prov, &Ret, &Error);
    ok(Ret, "create over a stale MsiDigitalSignatureEx failed with 0x%lx\n", Error);
    if (!Data)
        return;
    MetadataSize = MetadataDigest(AlgId, 0x11, Metadata);
    PrefixedSize = ContentDigest(AlgId, Metadata, MetadataSize, FALSE, Prefixed);
    CheckIndirect(Data, Algorithm, Prefixed, PrefixedSize, __LINE__);
    ok(ReadStream(NAME_SIGNATURE_EX, Stream, sizeof(Stream)) == MetadataSize && !memcmp(Stream, Metadata, MetadataSize),
       "stale MsiDigitalSignatureEx stream kept\n");
    ok(VerifyIndirect(pVerify, Prov, Data, &Error), "verify after a stale MsiDigitalSignatureEx failed with 0x%lx\n",
       Error);
    HeapFree(GetProcessHeap(), 0, Data);
}

static
VOID
TestProvider(HCRYPTPROV Aes)
{
    SIP_INDIRECT_DATA *Data;
    DWORD Error;
    BOOL Ret;

    ok(CreateMsi(0), "cannot create the MSI\n");
    Data = CreateIndirect(szOID_NIST_sha256, 0, &Ret, &Error);
    ok(!Ret && !Data, "SHA-256 create without a provider succeeded\n");
    ok_hex(Error, NTE_BAD_ALGID);
    Data = CreateIndirect(szOID_NIST_sha256, Aes, &Ret, &Error);
    ok(Ret, "SHA-256 create failed with 0x%lx\n", Error);
    if (!Data)
        return;
    Ret = VerifyIndirect(pVerify, 0, Data, &Error);
    ok(!Ret, "SHA-256 verify without a provider succeeded\n");
    ok_hex(Error, NTE_BAD_ALGID);
    HeapFree(GetProcessHeap(), 0, Data);
}

static
VOID
TestDispatch(HCRYPTPROV Aes)
{
    SIP_DISPATCH_INFO Sip;
    SIP_INDIRECT_DATA *Data;
    GUID Subject;
    DWORD Error;
    BOOL Ret;

    ok(CreateMsi(0), "cannot create the MSI\n");
    ok(CryptSIPRetrieveSubjectGuid(MsiPath, NULL, &Subject), "cannot retrieve the subject (%lu)\n", GetLastError());
    ok(IsEqualGUID(&Subject, &MsiSubject), "unexpected subject\n");
    ZeroMemory(&Sip, sizeof(Sip));
    Sip.cbSize = sizeof(Sip);
    ok(CryptSIPLoad(&MsiSubject, 0, &Sip), "cannot load the MSI SIP (%lu)\n", GetLastError());
    if (!Sip.pfVerify)
        return;
    Data = CreateIndirect(szOID_NIST_sha256, Aes, &Ret, &Error);
    if (!Data)
        return;
    ok(VerifyIndirect(Sip.pfVerify, Aes, Data, &Error), "dispatched verify failed with 0x%lx\n", Error);
    ok(EditMsi(L"a", "Fourth", 6, ~0u), "cannot change a stream\n");
    ok(!VerifyIndirect(Sip.pfVerify, Aes, Data, &Error), "dispatched verify of a changed stream succeeded\n");
    ok_hex(Error, TRUST_E_BAD_DIGEST);
    HeapFree(GetProcessHeap(), 0, Data);
}

static
VOID
TestBadInput(VOID)
{
    SIP_SUBJECTINFO Info;
    SIP_INDIRECT_DATA Small;
    GUID Subject = MsiSubject, Other = MsiDatabase;
    DWORD Size = 0;

    ok(CreateMsi(0), "cannot create the MSI\n");
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pgSubjectType = &Subject;
    Info.pwsFileName = MsiPath;
    Info.DigestAlgorithm.pszObjId = (LPSTR)szOID_RSA_RSA;
    SetLastError(0xdeadbeef);
    ok(!pCreate(&Info, &Size, NULL), "create with a non-hash OID succeeded\n");
    ok_hex(GetLastError(), ERROR_NOT_SUPPORTED);
    ok_long(Size, 0);

    Info.DigestAlgorithm.pszObjId = (LPSTR)szOID_OIWSEC_sha1;
    SetLastError(0xdeadbeef);
    ok(!pCreate(&Info, NULL, NULL), "create without a size succeeded\n");
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);

    Size = sizeof(Small);
    SetLastError(0xdeadbeef);
    ok(!pCreate(&Info, &Size, &Small), "create with a short buffer succeeded\n");
    ok_hex(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
    ok_long(Size, sizeof(SIP_INDIRECT_DATA) + sizeof(SPC_SIGINFO_OBJID) + 38 + sizeof(szOID_OIWSEC_sha1) + 20);

    Info.pgSubjectType = &Other;
    Size = 0;
    SetLastError(0xdeadbeef);
    ok(!pCreate(&Info, &Size, NULL), "create of another subject succeeded\n");
    ok_hex(GetLastError(), TRUST_E_SUBJECT_FORM_UNKNOWN);
    ok_long(Size, 0);

    Info.pgSubjectType = &Subject;
    Info.pwsFileName = L"C:\\lntmsisip-missing.msi";
    Size = 0;
    ok(pCreate(&Info, &Size, NULL), "size query of a missing file failed with 0x%lx\n", GetLastError());
    ok_long(Size, sizeof(SIP_INDIRECT_DATA) + sizeof(SPC_SIGINFO_OBJID) + 38 + sizeof(szOID_OIWSEC_sha1) + 20);
    Size = sizeof(Small);
    SetLastError(0xdeadbeef);
    ok(!pCreate(&Info, &Size, &Small), "create of a missing file succeeded\n");
    ok_hex(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
}

START_TEST(MsiSip)
{
    WCHAR TempDir[MAX_PATH];
    HMODULE Module;

    Module = LoadLibraryW(L"msisip.dll");
    if (!Module)
    {
        skip("msisip.dll is not available\n");
        return;
    }
    pCreate = (pCryptSIPCreateIndirectData)GetProcAddress(Module, "MsiSIPCreateIndirectData");
    pVerify = (pCryptSIPVerifyIndirectData)GetProcAddress(Module, "MsiSIPVerifyIndirectData");
    if (!pCreate || !pVerify || !CryptAcquireContextW(&Provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        skip("the MSI SIP functions are not available\n");
        FreeLibrary(Module);
        return;
    }
    GetTempPathW(RTL_NUMBER_OF(TempDir), TempDir);
    swprintf(MsiPath, RTL_NUMBER_OF(MsiPath), L"%lslntmsisip.msi", TempDir);

    TestDigest(szOID_NIST_sha256, CALG_SHA_256, Provider);
    TestDigest(szOID_OIWSEC_sha1, CALG_SHA1, 0);
    TestProvider(Provider);
    TestDispatch(Provider);
    TestBadInput();

    DeleteFileW(MsiPath);
    CryptReleaseContext(Provider, 0);
    FreeLibrary(Module);
}
