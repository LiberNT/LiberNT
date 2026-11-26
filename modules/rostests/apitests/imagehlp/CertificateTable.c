/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adding and removing PE certificates keeps the rest of the certificate table
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winver.h>
#include <imagehlp.h>

static DWORD Count(HANDLE h)
{
    DWORD n = 0xdeadbeef;
    if (!ImageEnumerateCertificates(h, CERT_SECTION_TYPE_ANY, &n, NULL, 0))
        return 0xffffffff;
    return n;
}

static DWORD Size(HANDLE h)
{
    LARGE_INTEGER s;
    GetFileSizeEx(h, &s);
    return s.LowPart;
}

START_TEST(CertificateTable)
{
    WCHAR Self[MAX_PATH], Path[MAX_PATH];
    BYTE Buffer[sizeof(WIN_CERTIFICATE) + 64];
    LPWIN_CERTIFICATE Cert = (LPWIN_CERTIFICATE)Buffer;
    DWORD Index, Base, AfterOne, AfterTwo;
    HANDLE h;

    GetModuleFileNameW(NULL, Self, MAX_PATH);
    GetTempPathW(MAX_PATH, Path);
    lstrcatW(Path, L"lntcert.exe");
    ok(CopyFileW(Self, Path, FALSE), "CopyFile %lu\n", GetLastError());
    h = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(h != INVALID_HANDLE_VALUE, "open %lu\n", GetLastError());
    Base = Size(h);
    ok(Count(h) == 0, "initial count %lu\n", Count(h));

    memset(Buffer, 0x5a, sizeof(Buffer));
    Cert->dwLength = sizeof(WIN_CERTIFICATE) - 1 + 40;
    Cert->wRevision = WIN_CERT_REVISION_2_0;
    Cert->wCertificateType = WIN_CERT_TYPE_PKCS_SIGNED_DATA;
    ok(ImageAddCertificate(h, Cert, &Index), "add 1 %lu\n", GetLastError());
    AfterOne = Size(h);
    Cert->dwLength = sizeof(WIN_CERTIFICATE) - 1 + 24;
    memset(Cert->bCertificate, 0x33, 24);
    ok(ImageAddCertificate(h, Cert, &Index), "add 2 %lu\n", GetLastError());
    AfterTwo = Size(h);
    ok(AfterOne > Base && AfterTwo > AfterOne, "sizes %lu, %lu, %lu\n", Base, AfterOne, AfterTwo);
    ok(Count(h) == 2, "count after two adds %lu\n", Count(h));

    ok(ImageRemoveCertificate(h, 0), "remove first %lu\n", GetLastError());
    ok(Count(h) == 1, "count after removing the first %lu\n", Count(h));
    {
        BYTE Out[256];
        DWORD Len = sizeof(Out);
        LPWIN_CERTIFICATE Got = (LPWIN_CERTIFICATE)Out;
        ok(ImageGetCertificateData(h, 0, Got, &Len), "get remaining %lu\n", GetLastError());
        ok(Got->dwLength == sizeof(WIN_CERTIFICATE) - 1 + 24 && Got->bCertificate[0] == 0x33,
           "remaining certificate length %lu first byte %02x\n", Got->dwLength, Got->bCertificate[0]);
    }
    ok(ImageRemoveCertificate(h, 0), "remove last %lu\n", GetLastError());
    ok(Count(h) == 0, "count after removing all %lu\n", Count(h));
    ok(Size(h) == Base, "final size %lu, original %lu\n", Size(h), Base);
    CloseHandle(h);
    DeleteFileW(Path);

    ok(CopyFileW(Self, Path, FALSE), "CopyFile %lu\n", GetLastError());
    h = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    {
        static const DWORD Lengths[3] = { 40, 21, 24 };
        static const BYTE Fill[3] = { 0x11, 0x22, 0x33 };
        BYTE Out[256];
        DWORD i, Len;
        LPWIN_CERTIFICATE Got = (LPWIN_CERTIFICATE)Out;

        for (i = 0; i < 3; i++)
        {
            memset(Buffer, Fill[i], sizeof(Buffer));
            Cert->dwLength = sizeof(WIN_CERTIFICATE) - 1 + Lengths[i];
            Cert->wRevision = WIN_CERT_REVISION_2_0;
            Cert->wCertificateType = WIN_CERT_TYPE_PKCS_SIGNED_DATA;
            ok(ImageAddCertificate(h, Cert, &Index) && Index == i, "add %lu: index %lu error %lu\n", i, Index, GetLastError());
        }
        ok(Count(h) == 3, "count after three adds %lu\n", Count(h));
        ok(ImageRemoveCertificate(h, 2), "remove the last %lu\n", GetLastError());
        ok(Count(h) == 2, "count after removing the last %lu\n", Count(h));
        for (i = 0; i < 2 && Count(h) == 2; i++)
        {
            Len = sizeof(Out);
            ok(ImageGetCertificateData(h, i, Got, &Len), "get %lu %lu\n", i, GetLastError());
            ok(Got->dwLength == sizeof(WIN_CERTIFICATE) - 1 + Lengths[i] && Got->bCertificate[0] == Fill[i],
               "certificate %lu: length %lu first byte %02x\n", i, Got->dwLength, Got->bCertificate[0]);
        }
        ok(ImageRemoveCertificate(h, 0), "remove the first %lu\n", GetLastError());
        ok(Count(h) == 1, "count after removing the first %lu\n", Count(h));
        Len = sizeof(Out);
        if (Count(h) == 1 && ImageGetCertificateData(h, 0, Got, &Len))
            ok(Got->dwLength == sizeof(WIN_CERTIFICATE) - 1 + Lengths[1] && Got->bCertificate[0] == Fill[1],
               "remaining certificate: length %lu first byte %02x\n", Got->dwLength, Got->bCertificate[0]);
    }
    CloseHandle(h);
    DeleteFileW(Path);
}
