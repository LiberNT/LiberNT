/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the $Recycle.Bin item pairs SHFileOperation writes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <sddl.h>
#include <strsafe.h>

#pragma pack(push, 1)
typedef struct _TEST_INFO_V2
{
    ULONGLONG Version;
    ULONGLONG FileSize;
    FILETIME DeletionTime;
    DWORD PathLength;
} TEST_INFO_V2;
#pragma pack(pop)

C_ASSERT(sizeof(TEST_INFO_V2) == 28);

static BOOL GetUserSidString(LPWSTR Sid, SIZE_T cch)
{
    HANDLE hToken;
    BYTE Buffer[256];
    DWORD Length;
    LPWSTR String;
    BOOL Ret = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;
    if (GetTokenInformation(hToken, TokenUser, Buffer, sizeof(Buffer), &Length) &&
        ConvertSidToStringSidW(((PTOKEN_USER)Buffer)->User.Sid, &String))
    {
        Ret = SUCCEEDED(StringCchCopyW(Sid, cch, String));
        LocalFree(String);
    }
    CloseHandle(hToken);
    return Ret;
}

static BOOL WriteTestFile(LPCWSTR Path, DWORD Size, BYTE Seed)
{
    BYTE Data[4096];
    DWORD Written;
    HANDLE hFile;
    BOOL Ret;

    if (Size > sizeof(Data))
        return FALSE;
    for (DWORD i = 0; i < Size; i++)
        Data[i] = (BYTE)(Seed + i * 7);

    hFile = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(hFile, Data, Size, &Written, NULL) && Written == Size;
    CloseHandle(hFile);
    return Ret;
}

static BOOL CheckTestFile(LPCWSTR Path, DWORD Size, BYTE Seed)
{
    BYTE Data[4096];
    DWORD Read;
    HANDLE hFile;
    BOOL Ret;

    hFile = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = ReadFile(hFile, Data, sizeof(Data), &Read, NULL) && Read == Size;
    CloseHandle(hFile);
    for (DWORD i = 0; Ret && i < Size; i++)
        Ret = Data[i] == (BYTE)(Seed + i * 7);
    return Ret;
}

static int RecycleItem(LPCWSTR Path)
{
    WCHAR From[MAX_PATH + 1] = {};
    StringCchCopyW(From, MAX_PATH, Path);
    SHFILEOPSTRUCTW Operation = { NULL, FO_DELETE, From, NULL,
                                  FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI };
    int Result = SHFileOperationW(&Operation);
    ok(!Operation.fAnyOperationsAborted, "Operation aborted for %ls\n", Path);
    return Result;
}

static BOOL FindInfo(LPCWSTR Folder, LPCWSTR Original, LPWSTR Suffix, SIZE_T cchSuffix,
                     BYTE *Info, DWORD cbInfo, DWORD *pcbRead)
{
    WCHAR Pattern[MAX_PATH], Path[MAX_PATH];
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    BOOL Found = FALSE;

    StringCchPrintfW(Pattern, _countof(Pattern), L"%s\\$I*", Folder);
    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        StringCchPrintfW(Path, _countof(Path), L"%s\\%s", Folder, FindData.cFileName);
        HANDLE hFile = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            continue;
        DWORD Read = 0;
        BOOL ReadOk = ReadFile(hFile, Info, cbInfo, &Read, NULL);
        CloseHandle(hFile);
        if (!ReadOk || Read < sizeof(TEST_INFO_V2))
            continue;
        TEST_INFO_V2 *Header = (TEST_INFO_V2 *)Info;
        if (Header->Version != 2 || Read < sizeof(*Header) + Header->PathLength * sizeof(WCHAR))
            continue;
        if (_wcsicmp((LPCWSTR)(Header + 1), Original) != 0)
            continue;
        StringCchCopyW(Suffix, cchSuffix, FindData.cFileName + 2);
        *pcbRead = Read;
        Found = TRUE;
    } while (!Found && FindNextFileW(hFind, &FindData));
    FindClose(hFind);
    return Found;
}

static void CheckItem(LPCWSTR Folder, LPCWSTR Original, ULONGLONG ExpectedSize,
                      const FILETIME *Before, const FILETIME *After, BOOL IsDirectory,
                      LPWSTR DataPath, SIZE_T cchDataPath, LPWSTR InfoPath, SIZE_T cchInfoPath)
{
    BYTE Info[sizeof(TEST_INFO_V2) + MAX_PATH * sizeof(WCHAR)];
    WCHAR Suffix[MAX_PATH];
    DWORD cbInfo = 0;

    DataPath[0] = InfoPath[0] = UNICODE_NULL;
    BOOL Found = FindInfo(Folder, Original, Suffix, _countof(Suffix), Info, sizeof(Info), &cbInfo);
    ok(Found, "No $I record names %ls\n", Original);
    if (!Found)
        return;

    TEST_INFO_V2 *Header = (TEST_INFO_V2 *)Info;
    DWORD PathLength = (DWORD)wcslen(Original) + 1;
    ok(Header->FileSize == ExpectedSize, "FileSize %I64u, expected %I64u\n", Header->FileSize, ExpectedSize);
    ok(Header->PathLength == PathLength, "PathLength %lu, expected %lu\n", Header->PathLength, PathLength);
    ok(cbInfo == sizeof(*Header) + PathLength * sizeof(WCHAR), "Record size %lu\n", cbInfo);
    ok(CompareFileTime(&Header->DeletionTime, Before) >= 0, "Deletion time before the operation\n");
    ok(CompareFileTime(&Header->DeletionTime, After) <= 0, "Deletion time after the operation\n");

    LPCWSTR Extension = PathFindExtensionW(PathFindFileNameW(Original));
    ok(wcslen(Suffix) == 6 + wcslen(Extension), "Suffix %ls\n", Suffix);
    ok(_wcsicmp(Suffix + 6, Extension) == 0, "Suffix %ls keeps no extension %ls\n", Suffix, Extension);
    BOOL Alphanumeric = TRUE;
    for (int i = 0; i < 6; i++)
        Alphanumeric = Alphanumeric && ((Suffix[i] >= L'A' && Suffix[i] <= L'Z') || (Suffix[i] >= L'0' && Suffix[i] <= L'9'));
    ok(Alphanumeric, "Suffix %ls is not six upper-case letters or digits\n", Suffix);

    StringCchPrintfW(InfoPath, cchInfoPath, L"%s\\$I%s", Folder, Suffix);
    StringCchPrintfW(DataPath, cchDataPath, L"%s\\$R%s", Folder, Suffix);
    DWORD Attributes = GetFileAttributesW(DataPath);
    ok(Attributes != INVALID_FILE_ATTRIBUTES, "No $R item %ls\n", DataPath);
    if (Attributes != INVALID_FILE_ATTRIBUTES)
        ok(!!(Attributes & FILE_ATTRIBUTE_DIRECTORY) == !!IsDirectory, "Attributes 0x%lx\n", Attributes);
}

static ULONGLONG QueryItemCount(LPCWSTR Root, ULONGLONG *pSize)
{
    SHQUERYRBINFO Info = { sizeof(Info) };
    HRESULT hr = SHQueryRecycleBinW(Root, &Info);
    ok_hr(hr, S_OK);
    *pSize = Info.i64Size;
    return Info.i64NumItems;
}

START_TEST(RecycleBin)
{
    WCHAR Temp[MAX_PATH], Root[4], Sid[256], Folder[MAX_PATH], BinRoot[MAX_PATH];
    WCHAR File[MAX_PATH], Dir[MAX_PATH], Child[MAX_PATH];
    WCHAR DataPath[MAX_PATH], InfoPath[MAX_PATH];
    FILETIME Before, After;
    ULONGLONG Count0, Count1, Count2, Size0, Size1, Size2;

    if (!GetTempPathW(_countof(Temp), Temp) || !GetUserSidString(Sid, _countof(Sid)))
    {
        skip("No temporary directory or user SID\n");
        return;
    }
    StringCchPrintfW(Root, _countof(Root), L"%c:\\", Temp[0]);
    if (GetDriveTypeW(Root) != DRIVE_FIXED)
    {
        skip("%ls is not a fixed drive\n", Root);
        return;
    }
    StringCchPrintfW(BinRoot, _countof(BinRoot), L"%s$Recycle.Bin", Root);
    StringCchPrintfW(Folder, _countof(Folder), L"%s\\%s", BinRoot, Sid);
    StringCchPrintfW(File, _countof(File), L"%srbtest_%lu.txt", Temp, GetCurrentProcessId());
    StringCchPrintfW(Dir, _countof(Dir), L"%srbtest_%lu.d", Temp, GetCurrentProcessId());

    Count0 = QueryItemCount(Root, &Size0);

    ok(WriteTestFile(File, 1234, 0x31), "Cannot create %ls\n", File);
    GetSystemTimeAsFileTime(&Before);
    ok_int(RecycleItem(File), 0);
    GetSystemTimeAsFileTime(&After);
    ok(GetFileAttributesW(File) == INVALID_FILE_ATTRIBUTES, "%ls still exists\n", File);

    DWORD Attributes = GetFileAttributesW(BinRoot);
    ok(Attributes != INVALID_FILE_ATTRIBUTES, "No %ls\n", BinRoot);
    ok((Attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM),
       "%ls attributes 0x%lx\n", BinRoot, Attributes);
    Attributes = GetFileAttributesW(Folder);
    ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY), "No folder %ls\n", Folder);

    CheckItem(Folder, File, 1234, &Before, &After, FALSE, DataPath, _countof(DataPath), InfoPath, _countof(InfoPath));
    if (DataPath[0])
        ok(CheckTestFile(DataPath, 1234, 0x31), "%ls does not hold the deleted data\n", DataPath);

    Count1 = QueryItemCount(Root, &Size1);
    ok(Count1 == Count0 + 1, "Item count %I64u, expected %I64u\n", Count1, Count0 + 1);
    ok(Size1 >= Size0 + 1234, "Size %I64u after %I64u\n", Size1, Size0);

    if (DataPath[0])
        ok(DeleteFileW(DataPath), "Cannot delete %ls (%lu)\n", DataPath, GetLastError());
    if (InfoPath[0])
        ok(DeleteFileW(InfoPath), "Cannot delete %ls (%lu)\n", InfoPath, GetLastError());
    Count2 = QueryItemCount(Root, &Size2);
    ok(Count2 == Count0, "Item count %I64u after removing the pair, expected %I64u\n", Count2, Count0);

    ok(CreateDirectoryW(Dir, NULL), "Cannot create %ls\n", Dir);
    StringCchPrintfW(Child, _countof(Child), L"%s\\a.bin", Dir);
    ok(WriteTestFile(Child, 100, 1), "Cannot create %ls\n", Child);
    StringCchPrintfW(Child, _countof(Child), L"%s\\b.bin", Dir);
    ok(WriteTestFile(Child, 200, 2), "Cannot create %ls\n", Child);
    GetSystemTimeAsFileTime(&Before);
    ok_int(RecycleItem(Dir), 0);
    GetSystemTimeAsFileTime(&After);
    ok(GetFileAttributesW(Dir) == INVALID_FILE_ATTRIBUTES, "%ls still exists\n", Dir);

    CheckItem(Folder, Dir, 300, &Before, &After, TRUE, DataPath, _countof(DataPath), InfoPath, _countof(InfoPath));
    if (DataPath[0])
    {
        StringCchPrintfW(Child, _countof(Child), L"%s\\b.bin", DataPath);
        ok(CheckTestFile(Child, 200, 2), "%ls does not hold the deleted data\n", Child);
        DeleteFileW(Child);
        StringCchPrintfW(Child, _countof(Child), L"%s\\a.bin", DataPath);
        DeleteFileW(Child);
        ok(RemoveDirectoryW(DataPath), "Cannot remove %ls (%lu)\n", DataPath, GetLastError());
    }
    if (InfoPath[0])
        ok(DeleteFileW(InfoPath), "Cannot delete %ls (%lu)\n", InfoPath, GetLastError());
}
