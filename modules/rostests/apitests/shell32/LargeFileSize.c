/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the size the file system folder reports for files of 4 GiB and more
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <shlobj.h>
#include <shlguid.h>
#include <ntquery.h>
#include <winioctl.h>
#include <shlwapi.h>
#include <strsafe.h>

static BOOL
CreateSparseFile(PCWSTR Path, ULONGLONG Size)
{
    HANDLE File;
    DWORD Returned;
    LARGE_INTEGER End;
    BOOL Ok;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    End.QuadPart = (LONGLONG)Size;
    Ok = DeviceIoControl(File, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &Returned, NULL) &&
         SetFilePointerEx(File, End, NULL, FILE_BEGIN) && SetEndOfFile(File);
    CloseHandle(File);
    if (!Ok)
        DeleteFileW(Path);
    return Ok;
}

START_TEST(LargeFileSize)
{
    static const ULONGLONG BigSize = 5ULL * 1024 * 1024 * 1024 + 12345;
    static const ULONGLONG SmallSize = 0x50000000;
    SHCOLUMNID SizeId;
    WCHAR Dir[MAX_PATH], BigPath[MAX_PATH], SmallPath[MAX_PATH];
    IShellFolder *Desktop = NULL, *Folder = NULL;
    IShellFolder2 *Folder2 = NULL;
    PIDLIST_RELATIVE DirPidl = NULL, Big = NULL, Small = NULL;
    WIN32_FIND_DATAW FindData;
    SHELLDETAILS Details;
    WCHAR Text[64], ByteText[64], KBText[64];
    VARIANT Value;
    ULONG Eaten;
    HRESULT hr;

    SizeId.fmtid = FMTID_Storage;
    SizeId.pid = PID_STG_SIZE;
    CoInitialize(NULL);
    GetTempPathW(_countof(Dir), Dir);
    StringCchCatW(Dir, _countof(Dir), L"LargeFileSize");
    CreateDirectoryW(Dir, NULL);
    StringCchPrintfW(BigPath, _countof(BigPath), L"%s\\big.bin", Dir);
    StringCchPrintfW(SmallPath, _countof(SmallPath), L"%s\\small.bin", Dir);
    if (!CreateSparseFile(BigPath, BigSize) || !CreateSparseFile(SmallPath, SmallSize))
    {
        skip("Cannot create sparse files in %ls: %lu\n", Dir, GetLastError());
        DeleteFileW(BigPath);
        RemoveDirectoryW(Dir);
        CoUninitialize();
        return;
    }

    hr = SHGetDesktopFolder(&Desktop);
    ok_hr(hr, S_OK);
    hr = IShellFolder_ParseDisplayName(Desktop, NULL, NULL, Dir, &Eaten, &DirPidl, NULL);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = IShellFolder_BindToObject(Desktop, DirPidl, NULL, &IID_IShellFolder, (void **)&Folder);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = IShellFolder_QueryInterface(Folder, &IID_IShellFolder2, (void **)&Folder2);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = IShellFolder_ParseDisplayName(Folder, NULL, NULL, (LPWSTR)L"big.bin", &Eaten, &Big, NULL);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        hr = IShellFolder_ParseDisplayName(Folder, NULL, NULL, (LPWSTR)L"small.bin", &Eaten, &Small, NULL);
    ok_hr(hr, S_OK);

    if (SUCCEEDED(hr))
    {
        VariantInit(&Value);
        hr = IShellFolder2_GetDetailsEx(Folder2, Big, &SizeId, &Value);
        ok_hr(hr, S_OK);
        ok_int(V_VT(&Value), VT_UI8);
        ok(V_UI8(&Value) == BigSize, "PID_STG_SIZE is %I64u, expected %I64u\n", V_UI8(&Value), BigSize);
        VariantClear(&Value);

        hr = IShellFolder2_GetDetailsOf(Folder2, Big, 1, &Details);
        ok_hr(hr, S_OK);
        Text[0] = UNICODE_NULL;
        if (SUCCEEDED(hr))
            StrRetToBufW(&Details.str, Big, Text, _countof(Text));
        StrFormatByteSizeW((LONGLONG)BigSize, ByteText, _countof(ByteText));
        StrFormatKBSizeW((LONGLONG)BigSize, KBText, _countof(KBText));
        ok(!wcscmp(Text, ByteText) || !wcscmp(Text, KBText), "Size column is '%ls', expected '%ls' or '%ls'\n",
           Text, ByteText, KBText);

        ZeroMemory(&FindData, sizeof(FindData));
        hr = SHGetDataFromIDListW(Folder, Big, SHGDFIL_FINDDATA, &FindData, sizeof(FindData));
        ok_hr(hr, S_OK);
        ok_long(FindData.nFileSizeHigh, (LONG)(BigSize >> 32));
        ok_long(FindData.nFileSizeLow, (LONG)(ULONG)BigSize);

        hr = IShellFolder_CompareIDs(Folder, 1, Big, Small);
        ok(SUCCEEDED(hr) && (short)HRESULT_CODE(hr) > 0, "CompareIDs by size returned 0x%lx\n", hr);
        hr = IShellFolder_CompareIDs(Folder, 1, Small, Big);
        ok(SUCCEEDED(hr) && (short)HRESULT_CODE(hr) < 0, "CompareIDs by size returned 0x%lx\n", hr);
    }

    ILFree(Small);
    ILFree(Big);
    ILFree(DirPidl);
    if (Folder2)
        IShellFolder2_Release(Folder2);
    if (Folder)
        IShellFolder_Release(Folder);
    if (Desktop)
        IShellFolder_Release(Desktop);
    DeleteFileW(BigPath);
    DeleteFileW(SmallPath);
    RemoveDirectoryW(Dir);
    CoUninitialize();
}
