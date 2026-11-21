/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Check a paging write beyond initialized data and the zero fill before it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <apitest.h>

#define GAP_PREFIX 4099
#define GAP_SIZE (1024 * 1024)
#define GAP_PAGE 4096
#define GAP_WAIT_MS 10000

static DWORD WINAPI
FlushGapPage(PVOID Page)
{
    memset(Page, 0x5a, GAP_PAGE);
    return FlushViewOfFile(Page, GAP_PAGE) ? ERROR_SUCCESS : GetLastError();
}

static BOOL
FindGapVolume(PWSTR VolumeRoot)
{
    WCHAR Label[MAX_PATH];

    for (VolumeRoot[0] = L'C'; VolumeRoot[0] <= L'Z'; VolumeRoot[0]++)
    {
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL, NULL, 0) &&
            !wcscmp(Label, L"NTFSTEST"))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
FlushTailPage(HANDLE File)
{
    HANDLE Section;
    HANDLE Thread;
    PUCHAR View;
    DWORD Error = ERROR_TIMEOUT;

    Section = CreateFileMappingW(File, NULL, PAGE_READWRITE, 0, GAP_SIZE + GAP_PAGE, NULL);
    ok(Section != NULL, "creating section failed: %lu\n", GetLastError());
    if (!Section)
        return;
    View = MapViewOfFile(Section, FILE_MAP_WRITE, 0, GAP_SIZE, GAP_PAGE);
    ok(View != NULL, "mapping tail page failed: %lu\n", GetLastError());
    if (View)
    {
        Thread = CreateThread(NULL, 0, FlushGapPage, View, 0, NULL);
        ok(Thread != NULL, "creating writer thread failed: %lu\n", GetLastError());
        if (Thread)
        {
            ok(WaitForSingleObject(Thread, GAP_WAIT_MS) == WAIT_OBJECT_0, "flushing tail page did not finish\n");
            GetExitCodeThread(Thread, &Error);
            ok(Error == ERROR_SUCCESS, "flushing tail page failed: %lu\n", Error);
            CloseHandle(Thread);
        }
        UnmapViewOfFile(View);
    }
    CloseHandle(Section);
}

static VOID
CheckGapFile(PCWSTR Path, PUCHAR Data)
{
    LARGE_INTEGER Size;
    HANDLE File;
    DWORD Bytes;
    DWORD Index;

    File = CreateFileW(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopening file failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(GetFileSizeEx(File, &Size) && Size.QuadPart == GAP_SIZE + GAP_PAGE, "incorrect file size\n");
    if (!ReadFile(File, Data, GAP_SIZE + GAP_PAGE, &Bytes, NULL) || Bytes != GAP_SIZE + GAP_PAGE)
    {
        ok(FALSE, "reading the file failed: %lu bytes, error %lu\n", Bytes, GetLastError());
        CloseHandle(File);
        return;
    }
    for (Index = 0; Index < GAP_SIZE + GAP_PAGE; ++Index)
    {
        UCHAR Expected = Index < GAP_PREFIX ? 0xa5 : Index < GAP_SIZE ? 0 : 0x5a;

        if (Data[Index] != Expected)
        {
            ok(FALSE, "byte %lu is %02x, expected %02x\n", Index, Data[Index], Expected);
            break;
        }
    }
    CloseHandle(File);
}

static VOID
TestGapFile(PCWSTR Path, PUCHAR Data)
{
    HANDLE File;
    DWORD Bytes;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
    ok(File != INVALID_HANDLE_VALUE, "opening gap file failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    memset(Data, 0xa5, GAP_PREFIX);
    if (!WriteFile(File, Data, GAP_PREFIX, &Bytes, NULL) || Bytes != GAP_PREFIX || !FlushFileBuffers(File))
    {
        ok(FALSE, "writing prefix failed: %lu\n", GetLastError());
        CloseHandle(File);
        return;
    }
    FlushTailPage(File);
    CloseHandle(File);
    CheckGapFile(Path, Data);
}

START_TEST(PagingWriteGap)
{
    WCHAR VolumeRoot[] = L"?:\\", Path[MAX_PATH];
    PUCHAR Data;

    if (!FindGapVolume(VolumeRoot))
    {
        skip("No NTFSTEST volume\n");
        return;
    }
    if (!GetTempFileNameW(VolumeRoot, L"gap", 0, Path))
    {
        ok(FALSE, "creating gap file failed: %lu\n", GetLastError());
        return;
    }
    Data = VirtualAlloc(NULL, GAP_SIZE + GAP_PAGE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Data != NULL, "allocating read buffer failed\n");
    if (Data)
    {
        TestGapFile(Path, Data);
        VirtualFree(Data, 0, MEM_RELEASE);
    }
    DeleteFileW(Path);
}
