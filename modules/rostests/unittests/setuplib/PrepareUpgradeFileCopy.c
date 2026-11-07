/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for the file plan Setup builds when it upgrades an installation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winuser.h>
#include <setupapi.h>
#include <strsafe.h>

#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>

#include <setuplib.h>

#define TEST_MAX_COPIES 32

extern HANDLE ProcessHeap;

PCWSTR WINAPI pSetupGetField(PINFCONTEXT Context, DWORD FieldIndex);

typedef struct _TEST_COPY
{
    WCHAR Source[MAX_PATH];
    WCHAR Target[MAX_PATH];
} TEST_COPY;

typedef struct _TEST_QUEUE
{
    ULONG Count;
    TEST_COPY Copies[TEST_MAX_COPIES];
} TEST_QUEUE, *PTEST_QUEUE;

static WCHAR TestRoot[MAX_PATH];

static const PCSTR TestSif =
    "[Version]\r\n"
    "Signature = \"$Windows NT$\"\r\n"
    "\r\n"
    "[SystemTrees]\r\n"
    "Windows = \"\\\"\r\n"
    "\"Program Files\" = \"\\Program Files\"\r\n"
    "\r\n"
    "[SystemTrees.Exclude]\r\n"
    "\"Windows\\system32\\config\" = 1\r\n"
    "\r\n"
    "[SystemTrees.Preserve]\r\n"
    "\"Windows\\keep.ini\" = 1\r\n"
    "\"Windows\\fresh.ini\" = 1\r\n";

static const PCSTR NewInventory =
    "; LiberNT new\n"
    "; Build 12\n"
    "; Image preinstall\n"
    "Other\\outside.txt\n"
    "Program Files\\App\\app.exe\n"
    "Windows\\a.dll\n"
    "Windows\\emptydir\\\n"
    "Windows\\fresh.ini\n"
    "Windows\\keep.ini\n"
    "Windows\\newdir\\b.dll\n"
    "Windows\\servicing\\\n"
    "Windows\\servicing\\LiberNT.files\n"
    "Windows\\system32\\config\\system\n";

#define OLD_INVENTORY_ENTRIES \
    "WINDOWS\\A.DLL\r\n" \
    "Windows\\keep.ini\r\n" \
    "Windows\\obsolete.dll\r\n" \
    "Windows\\olddir\\\r\n" \
    "Windows\\olddir2\\\r\n" \
    "Windows\\olddir2\\gone.txt\r\n" \
    "Windows\\servicing\\LiberNT.files\r\n" \
    "Windows\\system32\\config\\system\r\n"

static const PCSTR OldInventory =
    "; LiberNT old\r\n"
    "; Image preinstall\r\n"
    OLD_INVENTORY_ENTRIES;

static const PCSTR OtherImageInventory =
    "; LiberNT old\r\n"
    "; Image bootcd\r\n"
    OLD_INVENTORY_ENTRIES;

static HINF
WINAPI
TestOpenInf(
    PCWSTR FileName,
    PCWSTR InfClass,
    DWORD InfStyle,
    LCID LocaleId,
    PUINT ErrorLine)
{
    WCHAR Path[MAX_PATH];

    UNREFERENCED_PARAMETER(LocaleId);
    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%s", FileName)))
        return INVALID_HANDLE_VALUE;
    return SetupOpenInfFileW(Path, InfClass, InfStyle, ErrorLine);
}

static HSPFILEQ
WINAPI
TestQueueOpen(VOID)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TEST_QUEUE));
}

static BOOL
WINAPI
TestQueueClose(
    HSPFILEQ QueueHandle)
{
    return HeapFree(GetProcessHeap(), 0, QueueHandle);
}

static BOOL
WINAPI
TestQueueCopy(
    HSPFILEQ QueueHandle,
    PCWSTR SourceRootPath,
    PCWSTR SourcePath,
    PCWSTR SourceFileName,
    PCWSTR SourceDescription,
    PCWSTR SourceTagFile,
    PCWSTR TargetDirectory,
    PCWSTR TargetFileName,
    ULONG CopyStyle)
{
    PTEST_QUEUE Queue = (PTEST_QUEUE)QueueHandle;
    TEST_COPY* Copy;

    if (Queue->Count >= TEST_MAX_COPIES)
        return FALSE;

    Copy = &Queue->Copies[Queue->Count++];
    StringCchPrintfW(Copy->Source, ARRAYSIZE(Copy->Source), L"%s%s%s\\%s", SourceRootPath,
                     SourcePath ? L"\\" : L"", SourcePath ? SourcePath : L"", SourceFileName);
    StringCchPrintfW(Copy->Target, ARRAYSIZE(Copy->Target), L"%s\\%s", TargetDirectory,
                     TargetFileName ? TargetFileName : SourceFileName);
    return TRUE;
}

static BOOL
WINAPI
TestQueueDelete(
    HSPFILEQ QueueHandle,
    PCWSTR PathPart1,
    PCWSTR PathPart2)
{
    return FALSE;
}

static BOOL
WINAPI
TestQueueRename(
    HSPFILEQ QueueHandle,
    PCWSTR SourcePath,
    PCWSTR SourceFileName,
    PCWSTR TargetPath,
    PCWSTR TargetFileName)
{
    return FALSE;
}

static VOID
ToWin32Path(
    _In_ PCWSTR NtPath,
    _Out_writes_(cchPath) PWSTR Path,
    _In_ SIZE_T cchPath)
{
    StringCchCopyW(Path, cchPath, NtPath);
    if (wcsncmp(Path, L"\\??\\", 4) == 0)
        Path[1] = L'\\';
}

static BOOL
WINAPI
TestQueueCommit(
    HWND Owner,
    HSPFILEQ QueueHandle,
    PSP_FILE_CALLBACK_W MsgHandler,
    PVOID Context)
{
    PTEST_QUEUE Queue = (PTEST_QUEUE)QueueHandle;
    WCHAR Source[MAX_PATH], Target[MAX_PATH];
    BOOL Success = TRUE;
    ULONG Index;

    for (Index = 0; Index < Queue->Count; Index++)
    {
        ToWin32Path(Queue->Copies[Index].Source, Source, ARRAYSIZE(Source));
        ToWin32Path(Queue->Copies[Index].Target, Target, ARRAYSIZE(Target));
        Success &= CopyFileW(Source, Target, FALSE);
    }
    return Success;
}

static const SPINF_EXPORTS TestInfExports =
{
    SetupCloseInfFile,
    SetupFindFirstLineW,
    SetupFindNextLine,
    SetupGetFieldCount,
    SetupGetBinaryField,
    SetupGetIntField,
    SetupGetMultiSzFieldW,
    SetupGetStringFieldW,
    pSetupGetField,
    TestOpenInf
};

static const SPFILE_EXPORTS TestFileExports =
{
    TestQueueOpen,
    TestQueueClose,
    TestQueueCopy,
    TestQueueDelete,
    TestQueueRename,
    TestQueueCommit
};

static BOOL
BuildPath(
    _In_ PCWSTR Relative,
    _Out_writes_(cchPath) PWSTR Path,
    _In_ SIZE_T cchPath)
{
    return SUCCEEDED(StringCchPrintfW(Path, cchPath, L"%s\\%s", TestRoot, Relative));
}

static BOOL
WriteTestFile(
    _In_ PCWSTR Relative,
    _In_ PCSTR Text)
{
    WCHAR Path[MAX_PATH];
    PWCHAR Separator;
    HANDLE File;
    DWORD Written;
    BOOL Success;

    if (!BuildPath(Relative, Path, ARRAYSIZE(Path)))
        return FALSE;
    for (Separator = wcschr(Path + wcslen(TestRoot) + 1, L'\\'); Separator; Separator = wcschr(Separator + 1, L'\\'))
    {
        *Separator = UNICODE_NULL;
        CreateDirectoryW(Path, NULL);
        *Separator = L'\\';
    }

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Success = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL);
    CloseHandle(File);
    return Success;
}

static BOOL
CreateTestDirectory(
    _In_ PCWSTR Relative)
{
    WCHAR Path[MAX_PATH];

    return BuildPath(Relative, Path, ARRAYSIZE(Path)) && CreateDirectoryW(Path, NULL);
}

static DWORD
GetTestAttributes(
    _In_ PCWSTR Relative)
{
    WCHAR Path[MAX_PATH];

    if (!BuildPath(Relative, Path, ARRAYSIZE(Path)))
        return INVALID_FILE_ATTRIBUTES;
    return GetFileAttributesW(Path);
}

static BOOL
TestFileStartsWith(
    _In_ PCWSTR Relative,
    _In_ PCSTR Text)
{
    WCHAR Path[MAX_PATH];
    CHAR Buffer[64];
    DWORD Read = 0;
    HANDLE File;

    if (!BuildPath(Relative, Path, ARRAYSIZE(Path)))
        return FALSE;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(File, Buffer, sizeof(Buffer), &Read, NULL);
    CloseHandle(File);
    return Read >= strlen(Text) && memcmp(Buffer, Text, strlen(Text)) == 0;
}

static VOID
DeleteTree(
    _In_ PCWSTR Path)
{
    WIN32_FIND_DATAW Data;
    WCHAR Pattern[MAX_PATH], Child[MAX_PATH];
    HANDLE Find;

    StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", Path);
    Find = FindFirstFileW(Pattern, &Data);
    if (Find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
                continue;
            StringCchPrintfW(Child, ARRAYSIZE(Child), L"%s\\%s", Path, Data.cFileName);
            if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                DeleteTree(Child);
            else
                DeleteFileW(Child);
        } while (FindNextFileW(Find, &Data));
        FindClose(Find);
    }
    RemoveDirectoryW(Path);
}

static VOID
BuildTestTrees(
    _In_opt_ PCSTR Inventory)
{
    static const PCWSTR SourceFiles[] =
    {
        L"src\\Other\\outside.txt",
        L"src\\Program Files\\App\\app.exe",
        L"src\\Windows\\a.dll",
        L"src\\Windows\\fresh.ini",
        L"src\\Windows\\keep.ini",
        L"src\\Windows\\newdir\\b.dll",
        L"src\\Windows\\system32\\config\\system",
    };
    ULONG Index;

    DeleteTree(TestRoot);
    ok(CreateDirectoryW(TestRoot, NULL), "CreateDirectoryW(%S) failed with %lu\n", TestRoot, GetLastError());
    ok(WriteTestFile(L"txtsetup.sif", TestSif), "txtsetup.sif not written\n");

    for (Index = 0; Index < ARRAYSIZE(SourceFiles); Index++)
        ok(WriteTestFile(SourceFiles[Index], "new"), "%S not written\n", SourceFiles[Index]);
    ok(WriteTestFile(L"src\\Windows\\servicing\\LiberNT.files", NewInventory), "new inventory not written\n");

    ok(WriteTestFile(L"dst\\Windows\\a.dll", "old"), "old a.dll not written\n");
    ok(WriteTestFile(L"dst\\Windows\\keep.ini", "user"), "keep.ini not written\n");
    ok(WriteTestFile(L"dst\\Windows\\obsolete.dll", "old"), "obsolete.dll not written\n");
    ok(WriteTestFile(L"dst\\Windows\\user.txt", "user"), "user.txt not written\n");
    ok(WriteTestFile(L"dst\\Windows\\system32\\config\\system", "hive"), "hive not written\n");
    ok(CreateTestDirectory(L"dst\\Windows\\olddir"), "olddir not created\n");
    ok(WriteTestFile(L"dst\\Windows\\olddir2\\gone.txt", "old"), "gone.txt not written\n");
    ok(WriteTestFile(L"dst\\Windows\\olddir2\\data.txt", "user"), "data.txt not written\n");
    if (Inventory)
        ok(WriteTestFile(L"dst\\Windows\\servicing\\LiberNT.files", Inventory), "old inventory not written\n");
}

static BOOL
RunUpgradePlan(VOID)
{
    USETUP_DATA Data;
    WCHAR Path[MAX_PATH];
    UINT ErrorLine;
    BOOLEAN Prepared, Copied;

    ZeroMemory(&Data, sizeof(Data));
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\??\\%s\\txtsetup.sif", TestRoot);
    Data.SetupInf = SpInfOpenInfFile(Path, NULL, INF_STYLE_WIN4, MAKELCID(0x409, SORT_DEFAULT), &ErrorLine);
    ok(Data.SetupInf != INVALID_HANDLE_VALUE, "txtsetup.sif not opened, line %u\n", ErrorLine);
    if (Data.SetupInf == INVALID_HANDLE_VALUE)
        return FALSE;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\??\\%s\\src", TestRoot);
    RtlCreateUnicodeString(&Data.SourceRootPath, Path);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\??\\%s\\dst\\", TestRoot);
    RtlCreateUnicodeString(&Data.DestinationRootPath, Path);
    RtlCreateUnicodeString(&Data.InstallPath, L"\\Windows");
    Data.ComputerList = CreateGenericList();

    Prepared = PrepareUpgradeFileCopy(&Data, NULL);
    ok(Prepared, "PrepareUpgradeFileCopy failed\n");
    Copied = Prepared && DoFileCopy(&Data, NULL, NULL);
    ok(!Prepared || Copied, "DoFileCopy failed\n");

    DestroyGenericList(Data.ComputerList, FALSE);
    RtlFreeUnicodeString(&Data.InstallPath);
    RtlFreeUnicodeString(&Data.DestinationRootPath);
    RtlFreeUnicodeString(&Data.SourceRootPath);
    SpInfCloseInfFile(Data.SetupInf);
    return Copied;
}

START_TEST(PrepareUpgradeFileCopy)
{
    DWORD Attributes;

    ProcessHeap = GetProcessHeap();
    SpInfExports = TestInfExports;
    SpFileExports = TestFileExports;

    GetTempPathW(ARRAYSIZE(TestRoot), TestRoot);
    StringCchCatW(TestRoot, ARRAYSIZE(TestRoot), L"upgplan");

    BuildTestTrees(OldInventory);
    if (!RunUpgradePlan())
    {
        DeleteTree(TestRoot);
        return;
    }

    ok(TestFileStartsWith(L"dst\\Windows\\a.dll", "new"), "a.dll not replaced\n");
    ok(TestFileStartsWith(L"dst\\Windows\\newdir\\b.dll", "new"), "b.dll not copied into a new directory\n");
    ok(TestFileStartsWith(L"dst\\Program Files\\App\\app.exe", "new"), "second system tree not copied\n");
    ok(TestFileStartsWith(L"dst\\Windows\\keep.ini", "user"), "preserved file overwritten\n");
    ok(TestFileStartsWith(L"dst\\Windows\\fresh.ini", "new"), "missing preserved file not copied\n");
    ok(TestFileStartsWith(L"dst\\Windows\\system32\\config\\system", "hive"), "excluded file overwritten\n");
    ok(TestFileStartsWith(L"dst\\Windows\\user.txt", "user"), "file outside the inventories touched\n");
    ok(TestFileStartsWith(L"dst\\Windows\\olddir2\\data.txt", "user"), "file in an obsolete directory touched\n");
    ok(TestFileStartsWith(L"dst\\Windows\\servicing\\LiberNT.files", "; LiberNT new"), "inventory not replaced\n");
    ok(GetTestAttributes(L"dst\\Windows\\obsolete.dll") == INVALID_FILE_ATTRIBUTES, "obsolete file kept\n");
    ok(GetTestAttributes(L"dst\\Windows\\olddir2\\gone.txt") == INVALID_FILE_ATTRIBUTES, "obsolete file in a directory kept\n");
    ok(GetTestAttributes(L"dst\\Windows\\olddir") == INVALID_FILE_ATTRIBUTES, "empty obsolete directory kept\n");
    ok(GetTestAttributes(L"dst\\Other") == INVALID_FILE_ATTRIBUTES, "entry outside the system trees copied\n");
    Attributes = GetTestAttributes(L"dst\\Windows\\olddir2");
    ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY), "obsolete directory with data removed\n");
    Attributes = GetTestAttributes(L"dst\\Windows\\emptydir");
    ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY), "listed directory not created\n");

    BuildTestTrees(NULL);
    if (RunUpgradePlan())
    {
        ok(TestFileStartsWith(L"dst\\Windows\\a.dll", "new"), "a.dll not replaced without an old inventory\n");
        ok(TestFileStartsWith(L"dst\\Windows\\obsolete.dll", "old"), "unlisted file dropped without an old inventory\n");
        ok(TestFileStartsWith(L"dst\\Windows\\servicing\\LiberNT.files", "; LiberNT new"),
           "inventory not written without an old inventory\n");
    }

    BuildTestTrees(OtherImageInventory);
    if (RunUpgradePlan())
    {
        ok(TestFileStartsWith(L"dst\\Windows\\a.dll", "new"), "a.dll not replaced from another image\n");
        ok(TestFileStartsWith(L"dst\\Windows\\obsolete.dll", "old"), "file of another image dropped\n");
        ok(GetTestAttributes(L"dst\\Windows\\olddir") != INVALID_FILE_ATTRIBUTES, "directory of another image dropped\n");
        ok(TestFileStartsWith(L"dst\\Windows\\servicing\\LiberNT.files", "; LiberNT new"),
           "inventory not written from another image\n");
    }

    DeleteTree(TestRoot);
}
