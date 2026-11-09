/*
 * PROJECT:     LiberNT Print Spooler DLL API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for Upload/InstallFromPackage/DeletePrinterDriverPackage
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winreg.h>
#include <winspool.h>
#include <strsafe.h>

#define ERROR_SECTION_NOT_FOUND (APPLICATION_ERROR_MASK | ERROR_SEVERITY_ERROR | 0x101)

#define TEST_DRIVER_NAME L"LiberNT Package Test Printer"
#define NEEDS_DRIVER_NAME L"LiberNT Package Needs Printer"
#define BROKEN_DRIVER_NAME L"LiberNT Package Broken Printer"

#if defined(_M_AMD64)
#define TEST_ENVIRONMENT L"Windows x64"
#elif defined(_M_ARM64)
#define TEST_ENVIRONMENT L"Windows ARM64"
#else
#define TEST_ENVIRONMENT L"Windows NT x86"
#endif
#define TEST_DRIVERS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Print\\Environments\\" TEST_ENVIRONMENT L"\\Drivers\\Version-3"

#define TEST_MODELS \
    "\"LiberNT Package Test Printer\"=TESTPKG.GPD,LiberNT_Package_Test\r\n" \
    "\"LiberNT Package Needs Printer\"=TESTNEEDS,LiberNT_Package_Needs\r\n" \
    "\"LiberNT Package Broken Printer\"=TESTBROKEN,LiberNT_Package_Broken\r\n"

static const char TestInf[] =
    "[Version]\r\n"
    "Signature=\"$Windows NT$\"\r\n"
    "Class=Printer\r\n"
    "ClassGUID={4D36E979-E325-11CE-BFC1-08002BE10318}\r\n"
    "Provider=LiberNT\r\n"
    "DriverVer=11/06/2026,1.0.0.0\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "\"LiberNT\"=Models,NTamd64,NTx86,NTarm64\r\n"
    "\r\n"
    "[Models.NTamd64]\r\n"
    TEST_MODELS
    "[Models.NTx86]\r\n"
    TEST_MODELS
    "[Models.NTarm64]\r\n"
    TEST_MODELS
    "\r\n"
    "[TESTPKG.GPD]\r\n"
    "CopyFiles=@TESTPKG.GPD,TESTPKG_FILES\r\n"
    "DataSection=TESTPKG_DATA\r\n"
    "\r\n"
    "[TESTNEEDS]\r\n"
    "CopyFiles=@TESTPKG.GPD\r\n"
    "DataFile=TESTPKG.GPD\r\n"
    "DataSection=TESTPKG_DATA\r\n"
    "Needs=NEEDS_A\r\n"
    "\r\n"
    "[NEEDS_A]\r\n"
    "Needs=NEEDS_B\r\n"
    "\r\n"
    "[NEEDS_B]\r\n"
    "Needs=NEEDS_A\r\n"
    "CopyFiles=TESTPKG_FILES\r\n"
    "\r\n"
    "[TESTBROKEN]\r\n"
    "CopyFiles=@TESTPKG.GPD,TESTPKG_FILES\r\n"
    "DataSection=TESTPKG_DATA\r\n"
    "Needs=NO_SUCH_SECTION\r\n"
    "\r\n"
    "[TESTPKG_DATA]\r\n"
    "DriverFile=TESTDRV.DLL\r\n"
    "ConfigFile=TESTUI.DLL\r\n"
    "HelpFile=TESTPKG.HLP\r\n"
    "\r\n"
    "[TESTPKG_FILES]\r\n"
    "TESTDRV.DLL\r\n"
    "TESTUI.DLL\r\n"
    "TESTPKG.HLP\r\n"
    "TESTPKG.DAT\r\n"
    "TESTREN.DAT,TESTPKG.DAT\r\n"
    "\r\n"
    "[DestinationDirs]\r\n"
    "DefaultDestDir=66000\r\n"
    "\r\n"
    "[SourceDisksNames]\r\n"
    "1=\"Disk\",,,\r\n"
    "\r\n"
    "[SourceDisksFiles]\r\n"
    "TESTPKG.GPD=1\r\n"
    "TESTDRV.DLL=1\r\n"
    "TESTUI.DLL=1\r\n"
    "TESTPKG.HLP=1\r\n"
    "TESTPKG.DAT=1\r\n";

static BOOL
WriteTestFile(PCWSTR Dir, PCWSTR Name, const void *Data, DWORD Size)
{
    WCHAR Path[MAX_PATH];
    HANDLE hFile;
    DWORD Written;
    BOOL Ret;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Dir, Name);
    hFile = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(hFile, Data, Size, &Written, NULL) && Written == Size;
    CloseHandle(hFile);
    return Ret;
}

static BOOL
CreateTestPackage(PWSTR Dir, PWSTR InfPath)
{
    WCHAR System[MAX_PATH], Source[MAX_PATH], Target[MAX_PATH];
    static const char Text[] = "LiberNT package test file\r\n";

    GetTempPathW(MAX_PATH, Dir);
    StringCchCatW(Dir, MAX_PATH, L"lntprnpkg");
    CreateDirectoryW(Dir, NULL);
    StringCchPrintfW(InfPath, MAX_PATH, L"%s\\lntprnpkg.inf", Dir);

    GetSystemDirectoryW(System, ARRAYSIZE(System));
    StringCchPrintfW(Source, ARRAYSIZE(Source), L"%s\\version.dll", System);
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\TESTDRV.DLL", Dir);
    if (!CopyFileW(Source, Target, FALSE))
        return FALSE;
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\TESTUI.DLL", Dir);
    if (!CopyFileW(Source, Target, FALSE))
        return FALSE;

    return WriteTestFile(Dir, L"lntprnpkg.inf", TestInf, sizeof(TestInf) - 1) &&
           WriteTestFile(Dir, L"TESTPKG.GPD", Text, sizeof(Text) - 1) &&
           WriteTestFile(Dir, L"TESTPKG.HLP", Text, sizeof(Text) - 1) &&
           WriteTestFile(Dir, L"TESTPKG.DAT", Text, sizeof(Text) - 1);
}

static VOID
DeleteTestPackage(PCWSTR Dir)
{
    static const PCWSTR Names[] = { L"lntprnpkg.inf", L"TESTPKG.GPD", L"TESTDRV.DLL", L"TESTUI.DLL",
                                    L"TESTPKG.HLP", L"TESTPKG.DAT" };
    WCHAR Path[MAX_PATH];
    UINT i;

    for (i = 0; i < ARRAYSIZE(Names); i++)
    {
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Dir, Names[i]);
        DeleteFileW(Path);
    }
    RemoveDirectoryW(Dir);
}

static BOOL
DriverFileExists(PCWSTR Name)
{
    WCHAR Dir[MAX_PATH], Path[MAX_PATH];
    DWORD Size = sizeof(Dir);

    if (!GetPrinterDriverDirectoryW(NULL, NULL, 1, (PBYTE)Dir, Size, &Size))
        return FALSE;
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\3\\%s", Dir, Name);
    return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

static HKEY
OpenDriverKey(PCWSTR DriverName)
{
    WCHAR Key[MAX_PATH];
    HKEY hKey;

    StringCchPrintfW(Key, ARRAYSIZE(Key), L"%s\\%s", TEST_DRIVERS_KEY, DriverName);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Key, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return NULL;
    return hKey;
}

static BOOL
FilesMatch(PCWSTR First, PCWSTR Second)
{
    char A[256], B[256];
    DWORD ReadA = 0, ReadB = 0;
    HANDLE hA, hB;

    hA = CreateFileW(First, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    hB = CreateFileW(Second, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hA != INVALID_HANDLE_VALUE)
    {
        ReadFile(hA, A, sizeof(A), &ReadA, NULL);
        CloseHandle(hA);
    }
    if (hB != INVALID_HANDLE_VALUE)
    {
        ReadFile(hB, B, sizeof(B), &ReadB, NULL);
        CloseHandle(hB);
    }
    return ReadA != 0 && ReadA == ReadB && memcmp(A, B, ReadA) == 0;
}

static VOID
DeleteTestDriver(VOID)
{
    static const PCWSTR Names[] = { L"TESTDRV.DLL", L"TESTUI.DLL", L"TESTPKG.GPD", L"TESTPKG.HLP", L"TESTPKG.DAT",
                                    L"TESTREN.DAT" };
    static const PCWSTR Drivers[] = { TEST_DRIVER_NAME, NEEDS_DRIVER_NAME, BROKEN_DRIVER_NAME };
    WCHAR Key[MAX_PATH], Dir[MAX_PATH], Path[MAX_PATH];
    DWORD Size = sizeof(Dir);
    UINT i;

    for (i = 0; i < ARRAYSIZE(Drivers); i++)
    {
        StringCchPrintfW(Key, ARRAYSIZE(Key), L"%s\\%s", TEST_DRIVERS_KEY, Drivers[i]);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, Key);
    }

    if (!GetPrinterDriverDirectoryW(NULL, NULL, 1, (PBYTE)Dir, Size, &Size))
        return;
    for (i = 0; i < ARRAYSIZE(Names); i++)
    {
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\3\\%s", Dir, Names[i]);
        DeleteFileW(Path);
    }
}

START_TEST(DriverPackage)
{
    WCHAR Dir[MAX_PATH], InfPath[MAX_PATH], StoreInf[MAX_PATH];
    PWSTR Dependent;
    ULONG Length;
    HRESULT hr;
    BOOL DatFound = FALSE;

    Length = ARRAYSIZE(StoreInf);
    hr = UploadPrinterDriverPackageW(NULL, NULL, NULL, 0, NULL, StoreInf, &Length);
    ok(hr == E_INVALIDARG, "UploadPrinterDriverPackageW returned 0x%lx\n", hr);
    hr = UploadPrinterDriverPackageW(NULL, L"C:\\x.inf", L"Invalid Env", 0, NULL, StoreInf, &Length);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_ENVIRONMENT), "UploadPrinterDriverPackageW returned 0x%lx\n", hr);
    hr = InstallPrinterDriverFromPackageW(NULL, NULL, TEST_DRIVER_NAME, NULL, 0);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), "InstallPrinterDriverFromPackageW returned 0x%lx\n", hr);
    hr = InstallPrinterDriverFromPackageW(NULL, L"C:\\x.inf", TEST_DRIVER_NAME, NULL, 0x80);
    ok(hr == E_INVALIDARG, "InstallPrinterDriverFromPackageW returned 0x%lx\n", hr);
    hr = DeletePrinterDriverPackageW(NULL, NULL, NULL);
    ok(hr == E_INVALIDARG, "DeletePrinterDriverPackageW returned 0x%lx\n", hr);

    if (!CreateTestPackage(Dir, InfPath))
    {
        skip("Failed to create the test package (%lu)\n", GetLastError());
        return;
    }

    Length = ARRAYSIZE(StoreInf);
    hr = UploadPrinterDriverPackageW(NULL, L"C:\\does-not-exist\\none.inf", NULL, 0, NULL, StoreInf, &Length);
    ok(FAILED(hr), "UploadPrinterDriverPackageW returned 0x%lx\n", hr);

    Length = 1;
    hr = UploadPrinterDriverPackageW(NULL, InfPath, NULL, UPDP_SILENT_UPLOAD, NULL, StoreInf, &Length);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER), "UploadPrinterDriverPackageW returned 0x%lx\n", hr);
    ok(Length > 1, "Length is %lu\n", Length);

    Length = ARRAYSIZE(StoreInf);
    hr = UploadPrinterDriverPackageW(NULL, InfPath, NULL, UPDP_SILENT_UPLOAD, NULL, StoreInf, &Length);
    ok(hr == S_OK, "UploadPrinterDriverPackageW returned 0x%lx\n", hr);
    if (hr != S_OK)
    {
        skip("The package could not be uploaded\n");
        DeleteTestPackage(Dir);
        return;
    }
    ok(Length == wcslen(StoreInf) + 1, "Length is %lu for %S\n", Length, StoreInf);
    ok(wcsstr(StoreInf, L"\\DriverStore\\FileRepository\\") != NULL, "Store path is %S\n", StoreInf);
    ok(GetFileAttributesW(StoreInf) != INVALID_FILE_ATTRIBUTES, "%S does not exist\n", StoreInf);
    trace("Store INF: %S\n", StoreInf);

    {
        WCHAR Again[MAX_PATH];
        Length = ARRAYSIZE(Again);
        hr = UploadPrinterDriverPackageW(NULL, InfPath, NULL, UPDP_CHECK_DRIVERSTORE, NULL, Again, &Length);
        ok(hr == S_OK, "UploadPrinterDriverPackageW returned 0x%lx\n", hr);
        if (hr == S_OK)
            ok(_wcsicmp(Again, StoreInf) == 0, "Second upload returned %S\n", Again);
    }

    {
        WCHAR StoreDir[MAX_PATH], StoreFile[MAX_PATH], SourceFile[MAX_PATH], Again[MAX_PATH];
        static const char Changed[] = "changed in the store\r\n";
        PWSTR Last;

        StringCchCopyW(StoreDir, ARRAYSIZE(StoreDir), StoreInf);
        Last = wcsrchr(StoreDir, L'\\');
        if (Last)
            *Last = UNICODE_NULL;
        StringCchPrintfW(StoreFile, ARRAYSIZE(StoreFile), L"%s\\TESTPKG.DAT", StoreDir);
        ok(DeleteFileW(StoreFile), "Deleting %S failed with %lu\n", StoreFile, GetLastError());
        ok(WriteTestFile(StoreDir, L"TESTPKG.HLP", Changed, sizeof(Changed) - 1), "Changing TESTPKG.HLP failed\n");

        Length = ARRAYSIZE(Again);
        hr = UploadPrinterDriverPackageW(NULL, InfPath, NULL, UPDP_UPLOAD_ALWAYS, NULL, Again, &Length);
        ok(hr == S_OK, "UploadPrinterDriverPackageW returned 0x%lx\n", hr);
        if (hr == S_OK)
            ok(_wcsicmp(Again, StoreInf) == 0, "Forced upload returned %S\n", Again);
        ok(GetFileAttributesW(StoreFile) != INVALID_FILE_ATTRIBUTES, "%S was not copied again\n", StoreFile);
        StringCchPrintfW(StoreFile, ARRAYSIZE(StoreFile), L"%s\\TESTPKG.HLP", StoreDir);
        StringCchPrintfW(SourceFile, ARRAYSIZE(SourceFile), L"%s\\TESTPKG.HLP", Dir);
        ok(FilesMatch(StoreFile, SourceFile), "%S was not copied again\n", StoreFile);
    }

    hr = InstallPrinterDriverFromPackageW(NULL, StoreInf, BROKEN_DRIVER_NAME, NULL, 0);
    ok(hr == HRESULT_FROM_WIN32(ERROR_SECTION_NOT_FOUND), "InstallPrinterDriverFromPackageW returned 0x%lx\n", hr);
    {
        HKEY hBroken = OpenDriverKey(BROKEN_DRIVER_NAME);
        ok(hBroken == NULL, "Driver %S is registered\n", BROKEN_DRIVER_NAME);
        if (hBroken)
            RegCloseKey(hBroken);
    }

    hr = InstallPrinterDriverFromPackageW(NULL, StoreInf, NEEDS_DRIVER_NAME, NULL, IPDFP_COPY_ALL_FILES);
    ok(hr == S_OK, "InstallPrinterDriverFromPackageW returned 0x%lx\n", hr);
    {
        WCHAR Value[MAX_PATH * 2];
        DWORD Size = sizeof(Value);
        HKEY hNeeds = OpenDriverKey(NEEDS_DRIVER_NAME);

        ok(hNeeds != NULL, "Driver %S is not registered\n", NEEDS_DRIVER_NAME);
        if (hNeeds)
        {
            ZeroMemory(Value, sizeof(Value));
            ok(RegQueryValueExW(hNeeds, L"Dependent Files", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS,
               "No Dependent Files\n");
            for (Dependent = Value; *Dependent && _wcsicmp(Dependent, L"TESTPKG.DAT"); Dependent += wcslen(Dependent) + 1);
            ok(*Dependent != UNICODE_NULL, "TESTPKG.DAT from NEEDS_B is not a dependent file\n");
            Size = sizeof(Value);
            ok(RegQueryValueExW(hNeeds, L"InfPath", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS &&
               !_wcsicmp(Value, StoreInf), "InfPath is %S\n", Value);
            RegCloseKey(hNeeds);
        }
    }

    hr = InstallPrinterDriverFromPackageW(NULL, StoreInf, L"No Such Printer", NULL, 0);
    ok(hr == HRESULT_FROM_WIN32(ERROR_UNKNOWN_PRINTER_DRIVER), "InstallPrinterDriverFromPackageW returned 0x%lx\n", hr);

    {
        WCHAR DriverDir[MAX_PATH];
        DWORD Size = sizeof(DriverDir);
        static const char Keep[] = "not the package file\r\n";

        if (GetPrinterDriverDirectoryW(NULL, NULL, 1, (PBYTE)DriverDir, Size, &Size))
            ok(WriteTestFile(DriverDir, L"TESTREN.DAT", Keep, sizeof(Keep) - 1), "Cannot write TESTREN.DAT\n");
    }

    hr = InstallPrinterDriverFromPackageW(NULL, StoreInf, TEST_DRIVER_NAME, NULL, IPDFP_COPY_ALL_FILES);
    ok(hr == S_OK, "InstallPrinterDriverFromPackageW returned 0x%lx\n", hr);

    ok(DriverFileExists(L"TESTDRV.DLL"), "TESTDRV.DLL was not copied\n");
    ok(DriverFileExists(L"TESTUI.DLL"), "TESTUI.DLL was not copied\n");
    ok(DriverFileExists(L"TESTPKG.GPD"), "TESTPKG.GPD was not copied\n");
    ok(DriverFileExists(L"TESTPKG.DAT"), "TESTPKG.DAT was not copied\n");

    ok(DriverFileExists(L"TESTREN.DAT"), "TESTREN.DAT was not copied\n");
    {
        WCHAR DriverDir[MAX_PATH], Installed[MAX_PATH], Packaged[MAX_PATH], Kept[MAX_PATH];
        DWORD Size = sizeof(DriverDir), Read = 0;
        WIN32_FIND_DATAW Find;
        HANDLE hFind, hKept;
        char Buffer[64] = "";

        if (GetPrinterDriverDirectoryW(NULL, NULL, 1, (PBYTE)DriverDir, Size, &Size))
        {
            StringCchPrintfW(Installed, ARRAYSIZE(Installed), L"%s\\3\\TESTREN.DAT", DriverDir);
            StringCchPrintfW(Packaged, ARRAYSIZE(Packaged), L"%s\\TESTPKG.DAT", Dir);
            ok(FilesMatch(Installed, Packaged), "TESTREN.DAT is not TESTPKG.DAT\n");

            StringCchPrintfW(Kept, ARRAYSIZE(Kept), L"%s\\TESTREN.DAT", DriverDir);
            hKept = CreateFileW(Kept, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            ok(hKept != INVALID_HANDLE_VALUE, "%S was deleted (%lu)\n", Kept, GetLastError());
            if (hKept != INVALID_HANDLE_VALUE)
            {
                ReadFile(hKept, Buffer, sizeof(Buffer) - 1, &Read, NULL);
                CloseHandle(hKept);
                ok(Read == 22 && !memcmp(Buffer, "not the package file", 20), "%S was overwritten\n", Kept);
            }
            DeleteFileW(Kept);

            StringCchPrintfW(Kept, ARRAYSIZE(Kept), L"%s\\pkgstage-*", DriverDir);
            hFind = FindFirstFileW(Kept, &Find);
            ok(hFind == INVALID_HANDLE_VALUE, "%S was left behind\n", hFind == INVALID_HANDLE_VALUE ? L"" : Find.cFileName);
            if (hFind != INVALID_HANDLE_VALUE)
                FindClose(hFind);
        }
    }

    {
        WCHAR Key[MAX_PATH], Value[MAX_PATH * 2];
        DWORD Size;
        HKEY hKey = NULL;

        StringCchPrintfW(Key, ARRAYSIZE(Key), L"%s\\%s", TEST_DRIVERS_KEY, TEST_DRIVER_NAME);
        ok(RegOpenKeyExW(HKEY_LOCAL_MACHINE, Key, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS,
           "Driver %S is not registered\n", TEST_DRIVER_NAME);
        if (!hKey)
        {
            skip("The driver is not registered\n");
            DeleteTestDriver();
            DeletePrinterDriverPackageW(NULL, StoreInf, NULL);
            DeleteTestPackage(Dir);
            return;
        }
        Size = sizeof(Value);
        ok(RegQueryValueExW(hKey, L"Driver", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS &&
           !_wcsicmp(Value, L"TESTDRV.DLL"), "Driver is %S\n", Value);
        Size = sizeof(Value);
        ok(RegQueryValueExW(hKey, L"Configuration File", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS &&
           !_wcsicmp(Value, L"TESTUI.DLL"), "Configuration File is %S\n", Value);
        Size = sizeof(Value);
        ok(RegQueryValueExW(hKey, L"Data File", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS &&
           !_wcsicmp(Value, L"TESTPKG.GPD"), "Data File is %S\n", Value);
        Size = sizeof(Value);
        ok(RegQueryValueExW(hKey, L"Help File", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS &&
           !_wcsicmp(Value, L"TESTPKG.HLP"), "Help File is %S\n", Value);
        Size = sizeof(Value);
        ZeroMemory(Value, sizeof(Value));
        ok(RegQueryValueExW(hKey, L"Dependent Files", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS,
           "No Dependent Files\n");
        for (Dependent = Value; *Dependent; Dependent += wcslen(Dependent) + 1)
        {
            if (!_wcsicmp(Dependent, L"TESTPKG.DAT"))
                DatFound = TRUE;
        }
        ok(DatFound, "TESTPKG.DAT is not a dependent file\n");
        for (Dependent = Value; *Dependent && _wcsicmp(Dependent, L"TESTREN.DAT"); Dependent += wcslen(Dependent) + 1);
        ok(*Dependent != UNICODE_NULL, "TESTREN.DAT is not a dependent file\n");
        Size = sizeof(Value);
        ok(RegQueryValueExW(hKey, L"InfPath", NULL, NULL, (PBYTE)Value, &Size) == ERROR_SUCCESS &&
           !_wcsicmp(Value, StoreInf), "InfPath is %S\n", Value);
        RegCloseKey(hKey);
    }

    hr = DeletePrinterDriverPackageW(NULL, StoreInf, NULL);
    ok(hr == HRESULT_FROM_WIN32(ERROR_PRINTER_DRIVER_PACKAGE_IN_USE), "DeletePrinterDriverPackageW returned 0x%lx\n", hr);

    DeleteTestDriver();

    hr = DeletePrinterDriverPackageW(NULL, StoreInf, NULL);
    ok(hr == S_OK, "DeletePrinterDriverPackageW returned 0x%lx\n", hr);
    ok(GetFileAttributesW(StoreInf) == INVALID_FILE_ATTRIBUTES, "%S still exists\n", StoreInf);

    DeleteTestPackage(Dir);
}
