/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A metadata query does not wait for an unrelated file's data transfer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <winioctl.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"

/* Big enough that one transfer costs far more than any in-memory work. */
#define WRITER_BLOCK    (1024 * 1024)
#define WRITER_ROUNDS   16
#define APPEND_LIMIT    24

#define QUERY_COUNT     8

/* Nothing here may need this long; the test gives up and fails instead. */
#define ROUND_LIMIT_MS  4000

static WCHAR VolumeRoot[] = L"?:\\";
static LONGLONG Frequency;

typedef struct _WRITER
{
    WCHAR Path[MAX_PATH];
    HANDLE Stop;
    HANDLE Ready;
    HANDLE Thread;
    LONGLONG Ticks[WRITER_ROUNDS];
    ULONG Samples;
    ULONG Appended;
    BOOL Append;
    BOOL Fresh;
    DWORD Error;
} WRITER, *PWRITER;

static VOID
Report(_In_ PCSTR Format, ...)
{
    CHAR Line[256];
    va_list Arguments;

    va_start(Arguments, Format);
    StringCchVPrintfA(Line, ARRAYSIZE(Line), Format, Arguments);
    va_end(Arguments);
    OutputDebugStringA(Line);
}

/*
 * The dedicated test volume keeps the numbers clean, but the defect shows on any
 * NTFS volume, so fall back to the one the system lives on when it is absent.
 */
static BOOL
FindTestVolume(VOID)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR System[MAX_PATH];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            return TRUE;
        }
    }

    if (GetSystemDirectoryW(System, ARRAYSIZE(System)) == 0)
        return FALSE;

    VolumeRoot[0] = System[0];
    if (!GetVolumeInformationW(VolumeRoot, NULL, 0, NULL, NULL, NULL,
                               FileSystem, ARRAYSIZE(FileSystem)) ||
        wcscmp(FileSystem, L"NTFS"))
    {
        return FALSE;
    }
    return TRUE;
}

static LONGLONG
Microseconds(_In_ LONGLONG Ticks)
{
    return Frequency ? (Ticks * 1000000) / Frequency : 0;
}

static int
__cdecl
CompareTicks(const void *Left, const void *Right)
{
    LONGLONG A = *(const LONGLONG *)Left;
    LONGLONG B = *(const LONGLONG *)Right;

    if (A < B)
        return -1;
    return A > B ? 1 : 0;
}

static LONGLONG
Median(_Inout_ PLONGLONG Ticks, _In_ ULONG Count)
{
    if (Count == 0)
        return 0;
    qsort(Ticks, Count, sizeof(LONGLONG), CompareTicks);
    return Ticks[Count / 2];
}

static BOOL
MakeFile(_In_ PCWSTR Path, _In_ ULONG Size)
{
    HANDLE File;
    PUCHAR Buffer;
    ULONG Offset;
    BOOL Ok = TRUE;

    Buffer = VirtualAlloc(NULL, WRITER_BLOCK, MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL)
        return FALSE;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return FALSE;
    }

    FillMemory(Buffer, WRITER_BLOCK, 0xA5);
    for (Offset = 0; Offset < Size; Offset += WRITER_BLOCK)
    {
        DWORD Written = 0;
        ULONG Chunk = min(WRITER_BLOCK, Size - Offset);

        if (!WriteFile(File, Buffer, Chunk, &Written, NULL) || Written != Chunk)
        {
            Ok = FALSE;
            break;
        }
    }

    CloseHandle(File);
    VirtualFree(Buffer, 0, MEM_RELEASE);
    return Ok;
}

/*
 * Write one megabyte at a time, straight through to the device, and time each
 * transfer. This is what a file copy looks like to the filesystem once the
 * cache starts writing behind.
 */
static DWORD
CALLBACK
WriterThread(_In_ PVOID Parameter)
{
    PWRITER Writer = Parameter;
    HANDLE File;
    PUCHAR Buffer;

    Buffer = VirtualAlloc(NULL, WRITER_BLOCK, MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL)
    {
        Writer->Error = ERROR_OUTOFMEMORY;
        return 0;
    }
    FillMemory(Buffer, WRITER_BLOCK, 0x5A);

    /*
     * Unbuffered, so each transfer is handed to the filesystem whole instead of
     * being broken up by the cache. This is the shape a paging write has.
     * Appending goes through the cache instead: the flush behind each block is
     * a paging write past the valid data, which is how a download grows a file.
     */
    File = CreateFileW(Writer->Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       (Writer->Append || Writer->Fresh) ? FILE_ATTRIBUTE_NORMAL :
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Writer->Error = GetLastError();
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return 0;
    }

    while (WaitForSingleObject(Writer->Stop, 0) == WAIT_TIMEOUT)
    {
        LARGE_INTEGER Before, After, Zero;
        DWORD Written = 0;

        /* An installer lays each file down with one large write into a file nothing has cached yet */
        if (Writer->Fresh)
        {
            CloseHandle(File);
            File = CreateFileW(Writer->Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (File == INVALID_HANDLE_VALUE)
            {
                Writer->Error = GetLastError();
                break;
            }
        }

        if (Writer->Append && Writer->Appended == APPEND_LIMIT)
            break;
        Zero.QuadPart = 0;
        if (!Writer->Append && !Writer->Fresh)
            SetFilePointerEx(File, Zero, NULL, FILE_BEGIN);

        QueryPerformanceCounter(&Before);
        if (!WriteFile(File, Buffer, WRITER_BLOCK, &Written, NULL) || Written != WRITER_BLOCK)
        {
            Writer->Error = GetLastError();
            break;
        }
        if (Writer->Append)
        {
            Writer->Appended++;
            if (!FlushFileBuffers(File))
            {
                Writer->Error = GetLastError();
                break;
            }
        }
        QueryPerformanceCounter(&After);

        if (Writer->Samples < WRITER_ROUNDS)
            Writer->Ticks[Writer->Samples++] = After.QuadPart - Before.QuadPart;
        /* The probe may only run once a transfer has actually been in flight. */
        if (Writer->Ready)
            SetEvent(Writer->Ready);
    }

    CloseHandle(File);
    VirtualFree(Buffer, 0, MEM_RELEASE);
    return 0;
}

/*
 * Time QUERY_COUNT opens of one small file. An open walks the directory and the
 * file's record, both of which the filesystem already holds in memory, so it
 * costs nothing but the locks it takes on the way. It is also what a shell does
 * most of, which is why a stalled one is so visible.
 */
static BOOL
QueryRound(_In_ PCWSTR Path, _Out_ PLONGLONG Result)
{
    LONGLONG Ticks[QUERY_COUNT];
    ULONG Index;
    DWORD Start = GetTickCount();

    *Result = 0;
    for (Index = 0; Index < QUERY_COUNT; Index++)
    {
        LARGE_INTEGER Before, After;
        HANDLE File;

        QueryPerformanceCounter(&Before);
        File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        QueryPerformanceCounter(&After);
        if (File == INVALID_HANDLE_VALUE)
            return FALSE;
        CloseHandle(File);

        Ticks[Index] = After.QuadPart - Before.QuadPart;
        if (GetTickCount() - Start > ROUND_LIMIT_MS)
            return FALSE;
    }

    *Result = Median(Ticks, QUERY_COUNT);
    return TRUE;
}

/* Every appended byte must read back from the device, so the valid data moved with each flush */
static VOID
CheckAppended(_In_ PCWSTR Path, _In_ ULONG Blocks)
{
    LARGE_INTEGER Size;
    HANDLE File;
    PUCHAR Buffer;
    ULONG Block, Index, Bad = 0;

    Buffer = VirtualAlloc(NULL, WRITER_BLOCK, MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL)
    {
        skip(FALSE, "VirtualAlloc failed: %lu\n", GetLastError());
        return;
    }
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopening %ls failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
    {
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return;
    }
    ok(GetFileSizeEx(File, &Size), "GetFileSizeEx failed: %lu\n", GetLastError());
    ok(Size.QuadPart == (LONGLONG)Blocks * WRITER_BLOCK, "size %I64d, expected %I64d\n",
       Size.QuadPart, (LONGLONG)Blocks * WRITER_BLOCK);
    for (Block = 0; Block < Blocks; Block++)
    {
        DWORD Read = 0;

        FillMemory(Buffer, WRITER_BLOCK, 0xEE);
        if (!ReadFile(File, Buffer, WRITER_BLOCK, &Read, NULL) || Read != WRITER_BLOCK)
        {
            ok(FALSE, "reading block %lu failed: %lu, %lu bytes\n", Block, GetLastError(), Read);
            break;
        }
        for (Index = 0; Index < WRITER_BLOCK; Index++)
        {
            if (Buffer[Index] != 0x5A && Bad++ == 0)
                ok(FALSE, "block %lu byte %lu reads 0x%02x, expected 0x5a\n", Block, Index, Buffer[Index]);
        }
    }
    ok(Bad == 0, "%lu appended bytes did not read back\n", Bad);
    CloseHandle(File);
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

static VOID
MeasureVolume(_In_ BOOL Append, _In_ BOOL Fresh)
{
    WCHAR SmallPath[MAX_PATH];
    WRITER Writer;
    LONGLONG Idle = 0, Loaded = 0, Transfer = 0;

    StringCchPrintfW(SmallPath, MAX_PATH, L"%sconcio_small.dat", VolumeRoot);
    StringCchPrintfW(Writer.Path, MAX_PATH, Fresh ? L"%sconcio_fresh.dat" : Append ? L"%sconcio_append.dat" : L"%sconcio_write.dat",
                     VolumeRoot);

    if (!MakeFile(SmallPath, 4096))
    {
        skip(FALSE, "could not lay down %ls: %lu\n", SmallPath, GetLastError());
        return;
    }

    /* Warm up, then measure what a query costs with the volume to itself. */
    if (!QueryRound(SmallPath, &Idle) || !QueryRound(SmallPath, &Idle))
    {
        ok(FALSE, "the unloaded query round did not finish\n");
        DeleteFileW(SmallPath);
        return;
    }

    Writer.Stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    Writer.Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    Writer.Samples = 0;
    Writer.Appended = 0;
    Writer.Append = Append;
    Writer.Fresh = Fresh;
    Writer.Error = ERROR_SUCCESS;
    Writer.Thread = NULL;
    if (Writer.Stop == NULL || Writer.Ready == NULL)
    {
        skip(FALSE, "CreateEventW failed: %lu\n", GetLastError());
        if (Writer.Stop)
            CloseHandle(Writer.Stop);
        if (Writer.Ready)
            CloseHandle(Writer.Ready);
        DeleteFileW(SmallPath);
        return;
    }

    Writer.Thread = CreateThread(NULL, 0, WriterThread, &Writer, 0, NULL);
    if (Writer.Thread == NULL)
    {
        skip(FALSE, "CreateThread failed: %lu\n", GetLastError());
        CloseHandle(Writer.Stop);
        CloseHandle(Writer.Ready);
        DeleteFileW(SmallPath);
        return;
    }

    if (WaitForSingleObject(Writer.Ready, ROUND_LIMIT_MS * 2) != WAIT_OBJECT_0)
        ok(FALSE, "the writer never completed a transfer\n");
    else if (!QueryRound(SmallPath, &Loaded))
        ok(FALSE, "the loaded query round did not finish within %u ms\n", ROUND_LIMIT_MS);

    SetEvent(Writer.Stop);
    if (WaitForSingleObject(Writer.Thread, ROUND_LIMIT_MS) == WAIT_OBJECT_0)
        ok_eq_hex(Writer.Error, ERROR_SUCCESS);
    Transfer = Median(Writer.Ticks, Writer.Samples);

    trace("%s: open %I64d us idle, %I64d us while a %u KB transfer of %I64d us runs\n",
          Fresh ? "fresh" : Append ? "append" : "overwrite", Microseconds(Idle), Microseconds(Loaded),
          WRITER_BLOCK / 1024, Microseconds(Transfer));
    Report("CONCIO %s open idle %I64d us loaded %I64d us transfer %I64d us\n",
           Fresh ? "fresh" : Append ? "append" : "overwrite", Microseconds(Idle), Microseconds(Loaded),
           Microseconds(Transfer));

    /*
     * The open needs no device at all, so it must never cost as much as the
     * transfer running beside it. It does when the filesystem holds one
     * volume-wide lock across that transfer: every other caller, a shell
     * listing a folder included, then waits for the device to finish.
     */
    if (Transfer > 0 && Writer.Samples != 0)
    {
        ok(Loaded * 2 < Transfer,
           "opening a file took %I64d us beside a %I64d us transfer of another file "
           "(%I64d us with the volume idle), so it waited for the transfer\n",
           Microseconds(Loaded), Microseconds(Transfer), Microseconds(Idle));
    }
    else
    {
        skip(FALSE, "the writer produced no timed transfer\n");
    }

    if (Append && Writer.Error == ERROR_SUCCESS)
        CheckAppended(Writer.Path, Writer.Appended);

    CloseHandle(Writer.Thread);
    CloseHandle(Writer.Stop);
    CloseHandle(Writer.Ready);
    DeleteFileW(Writer.Path);
    DeleteFileW(SmallPath);
}

START_TEST(ConcurrentIo)
{
    LARGE_INTEGER Counter;

    if (!QueryPerformanceFrequency(&Counter) || Counter.QuadPart == 0)
    {
        skip(FALSE, "no performance counter\n");
        return;
    }
    Frequency = Counter.QuadPart;

    if (!FindTestVolume())
    {
        skip(FALSE, "no NTFS volume to test on\n");
        return;
    }
    trace("testing on %ls\n", VolumeRoot);

    MeasureVolume(FALSE, FALSE);
    MeasureVolume(TRUE, FALSE);
    MeasureVolume(FALSE, TRUE);
}
