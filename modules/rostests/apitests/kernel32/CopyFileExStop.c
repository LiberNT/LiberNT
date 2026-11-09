/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for a CopyFileExW that stops or fails before the end
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include "precomp.h"

#define SOURCE_SIZE (6 * 1024 * 1024)
#define STOP_AT (2 * 1024 * 1024)
#define LOCK_AT (3 * 1024 * 1024 + 4096)

static WCHAR Source[MAX_PATH], Dest[MAX_PATH];
static LONGLONG StopAt, StoppedAt;
static LONGLONG FirstReport, FirstChunk;
static DWORD StopAction;

static DWORD CALLBACK
Progress(LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred, LARGE_INTEGER StreamSize,
         LARGE_INTEGER StreamBytesTransferred, DWORD StreamNumber, DWORD Reason, HANDLE SourceFile,
         HANDLE DestFile, LPVOID Data)
{
    if (FirstReport < 0)
        FirstReport = TotalBytesTransferred.QuadPart;
    if (Reason == CALLBACK_CHUNK_FINISHED && FirstChunk < 0)
        FirstChunk = TotalBytesTransferred.QuadPart;
    if (StopAt && TotalBytesTransferred.QuadPart >= StopAt)
    {
        StoppedAt = TotalBytesTransferred.QuadPart;
        return StopAction;
    }
    return PROGRESS_CONTINUE;
}

static BOOL
Copy(DWORD Flags, LONGLONG Stop, DWORD Action)
{
    StopAt = Stop;
    StopAction = Action;
    StoppedAt = FirstReport = FirstChunk = -1;
    SetLastError(0xdeadbeef);
    return CopyFileExW(Source, Dest, Progress, NULL, NULL, Flags);
}

static BOOL
DestMissing(void)
{
    return GetFileAttributesW(Dest) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
}

static BOOL
SameAsSource(void)
{
    static UCHAR A[65536], B[65536];
    HANDLE S, D;
    DWORD ReadS, ReadD;
    BOOL Same = FALSE;

    S = CreateFileW(Source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    D = CreateFileW(Dest, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (S != INVALID_HANDLE_VALUE && D != INVALID_HANDLE_VALUE)
    {
        while (ReadFile(S, A, sizeof(A), &ReadS, NULL) && ReadFile(D, B, sizeof(B), &ReadD, NULL) &&
               ReadS == ReadD && !memcmp(A, B, ReadS))
        {
            if (!ReadS)
            {
                Same = TRUE;
                break;
            }
        }
    }
    if (S != INVALID_HANDLE_VALUE)
        CloseHandle(S);
    if (D != INVALID_HANDLE_VALUE)
        CloseHandle(D);
    return Same;
}

static void
test_Stop(DWORD Flags, DWORD Action)
{
    BOOL Ret;

    DeleteFileW(Dest);
    Ret = Copy(Flags, STOP_AT, Action);
    ok(!Ret && GetLastError() == ERROR_REQUEST_ABORTED, "Flags 0x%lx, action %lu: stopped copy returned %d (%lu)\n",
       Flags, Action, Ret, GetLastError());
    ok(StoppedAt >= STOP_AT && StoppedAt < SOURCE_SIZE, "Flags 0x%lx, action %lu: stopped at %I64d\n",
       Flags, Action, StoppedAt);
    trace("Flags 0x%lx, action %lu: stopped at %I64d\n", Flags, Action, StoppedAt);
    ok(DestMissing(), "Flags 0x%lx, action %lu: the stopped copy left its destination (%lu)\n",
       Flags, Action, GetLastError());

    Ret = Copy(Flags, 0, PROGRESS_CONTINUE);
    ok(Ret, "Flags 0x%lx, action %lu: copy after the stop failed (%lu)\n", Flags, Action, GetLastError());
    ok(FirstReport == 0, "Flags 0x%lx, action %lu: first report after the stop at %I64d\n",
       Flags, Action, FirstReport);
    ok(FirstChunk > 0 && FirstChunk <= STOP_AT / 2, "Flags 0x%lx, action %lu: first chunk after the stop ended at %I64d\n",
       Flags, Action, FirstChunk);
    ok(SameAsSource(), "Flags 0x%lx, action %lu: the destination differs from the source\n", Flags, Action);
}

static void
test_FailedRead(DWORD Flags)
{
    HANDLE Lock;
    BOOL Ret;

    DeleteFileW(Dest);
    Lock = CreateFileW(Source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(LockFile(Lock, LOCK_AT, 0, 4096, 0), "LockFile failed (%lu)\n", GetLastError());
    Ret = Copy(Flags, 0, PROGRESS_CONTINUE);
    ok(!Ret && GetLastError() == ERROR_LOCK_VIOLATION, "Flags 0x%lx: copy over a locked range returned %d (%lu)\n",
       Flags, Ret, GetLastError());
    ok(DestMissing(), "Flags 0x%lx: the failed copy left its destination (%lu)\n", Flags, GetLastError());
    UnlockFile(Lock, LOCK_AT, 0, 4096, 0);
    CloseHandle(Lock);

    Ret = Copy(Flags, 0, PROGRESS_CONTINUE);
    ok(Ret, "Flags 0x%lx: copy after the unlock failed (%lu)\n", Flags, GetLastError());
    ok(FirstReport == 0, "Flags 0x%lx: first report after the unlock at %I64d\n", Flags, FirstReport);
    ok(FirstChunk > 0 && FirstChunk <= STOP_AT / 2, "Flags 0x%lx: first chunk after the unlock ended at %I64d\n",
       Flags, FirstChunk);
    ok(SameAsSource(), "Flags 0x%lx: the destination differs from the source\n", Flags);
}

START_TEST(CopyFileExStop)
{
    static UCHAR Chunk[65536];
    WCHAR Dir[MAX_PATH], Root[MAX_PATH], FileSystem[MAX_PATH];
    HANDLE File;
    DWORD i, Written, Done;
    BOOL Wrote = TRUE;

    GetTempPathW(ARRAYSIZE(Dir), Dir);

    /* Windows resumes a restartable copy only when one of the files is on the network */
    if (!GetVolumePathNameW(Dir, Root, ARRAYSIZE(Root)) || GetDriveTypeW(Root) != DRIVE_FIXED)
    {
        skip("%S is not on a local disk\n", Dir);
        return;
    }
    if (!GetVolumeInformationW(Root, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)))
        FileSystem[0] = UNICODE_NULL;
    trace("Copying in %S on %S\n", Dir, FileSystem);
    StringCchPrintfW(Source, ARRAYSIZE(Source), L"%sCopyFileExStop-src.bin", Dir);
    StringCchPrintfW(Dest, ARRAYSIZE(Dest), L"%sCopyFileExStop-dst.bin", Dir);

    File = CreateFileW(Source, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Cannot create the source (%lu)\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    for (Written = 0; Written < SOURCE_SIZE; Written += sizeof(Chunk))
    {
        for (i = 0; i < sizeof(Chunk); i++)
            Chunk[i] = (UCHAR)((Written + i) * 7 % 251);
        if (!WriteFile(File, Chunk, sizeof(Chunk), &Done, NULL) || Done != sizeof(Chunk))
        {
            Wrote = FALSE;
            break;
        }
    }
    CloseHandle(File);
    ok(Wrote, "Cannot write the source (%lu)\n", GetLastError());
    if (!Wrote)
    {
        DeleteFileW(Source);
        return;
    }

    test_Stop(0, PROGRESS_STOP);
    test_Stop(COPY_FILE_RESTARTABLE, PROGRESS_STOP);
    test_Stop(0, PROGRESS_CANCEL);
    test_Stop(COPY_FILE_RESTARTABLE, PROGRESS_CANCEL);
    test_FailedRead(0);
    test_FailedRead(COPY_FILE_RESTARTABLE);

    DeleteFileW(Dest);
    DeleteFileW(Source);
}
