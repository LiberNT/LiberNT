/*
 * PROJECT:     LiberNT Applications Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Segmented parallel HTTP downloads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rapps.h"
#include "download.h"
#include <process.h>

#define DL_MAX_WORKERS 4
#define DL_MIN_CHUNK (1024 * 1024)
#define DL_MAX_CHUNK (4 * 1024 * 1024)
#define DL_CHUNK_ALIGN (64 * 1024)
#define DL_READ_SIZE (64 * 1024)
#define DL_ATTEMPTS 4

struct DL_CHUNK
{
    ULONGLONG Offset;
    ULONG Length;
    PBYTE Data;
    BOOL Ready;
};

struct DL_JOB
{
    HINTERNET hSession;
    HINTERNET volatile hFirst;
    LPCWSTR Url;
    LPCWSTR Validator;
    DWORD Flags;
    const DOWNLOAD_SINK *Sink;
    DL_CHUNK *Chunks;
    ULONG ChunkCount;
    ULONG Next;
    ULONG Written;
    ULONG Window;
    LONG volatile Abort;
    DWORD Error;
    LONGLONG volatile Received;
    ULONGLONG Total;
    SRWLOCK Lock;
    CONDITION_VARIABLE Changed;
};

static BOOL
ParseNumber(PCWSTR &p, ULONGLONG &Value)
{
    ULONGLONG Digit;

    if (*p < L'0' || *p > L'9')
        return FALSE;
    for (Value = 0; *p >= L'0' && *p <= L'9'; p++)
    {
        Digit = *p - L'0';
        if (Value > (MAXULONGLONG - Digit) / 10)
            return FALSE;
        Value = Value * 10 + Digit;
    }
    return TRUE;
}

static BOOL
ParseContentRange(const CStringW &Value, ULONGLONG &First, ULONGLONG &Last, ULONGLONG &Total)
{
    PCWSTR p = Value;

    if (_wcsnicmp(p, L"bytes", 5))
        return FALSE;
    for (p += 5; *p == L' '; p++)
        ;
    if (!ParseNumber(p, First) || *p++ != L'-' || !ParseNumber(p, Last) || *p++ != L'/' ||
        !ParseNumber(p, Total))
        return FALSE;
    return First <= Last && Last < Total;
}

static BOOL
QueryHeader(HINTERNET hRequest, DWORD Level, CStringW &Value)
{
    DWORD Size = 0;
    PWSTR Buffer;
    BOOL Ok;

    Value.Empty();
    if (HttpQueryInfoW(hRequest, Level, NULL, &Size, NULL) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;
    Buffer = Value.GetBuffer(Size / sizeof(WCHAR) + 1);
    Ok = HttpQueryInfoW(hRequest, Level, Buffer, &Size, NULL);
    Value.ReleaseBuffer(Ok ? Size / sizeof(WCHAR) : 0);
    return Ok;
}

static BOOL
QueryStatus(HINTERNET hRequest, DWORD &Status)
{
    DWORD Size = sizeof(Status);

    return HttpQueryInfoW(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &Status, &Size, NULL);
}

HINTERNET
OpenHttpDownload(HINTERNET hSession, LPCWSTR Url, DWORD Flags, DOWNLOAD_RESOURCE &Resource)
{
    HINTERNET hRequest;
    CStringW Value;
    ULONGLONG First, Last, Total;
    PCWSTR p;
    DWORD Size, Error;
    PWSTR Buffer;
    BOOL Ok;

    Resource.Status = 0;
    Resource.Length = 0;
    Resource.AcceptsRanges = FALSE;
    Resource.Url = Url;
    Resource.Validator.Empty();

    hRequest = InternetOpenUrlW(hSession, Url, L"Range: bytes=0-\r\n", (DWORD)-1, Flags, 0);
    if (!hRequest)
        return NULL;
    if (!QueryStatus(hRequest, Resource.Status))
        goto fail;

    if (Resource.Status == HTTP_STATUS_PARTIAL_CONTENT && QueryHeader(hRequest, HTTP_QUERY_CONTENT_RANGE, Value) &&
        ParseContentRange(Value, First, Last, Total) && First == 0)
    {
        Resource.Length = Total;
        Resource.AcceptsRanges = TRUE;
    }
    else if (Resource.Status != HTTP_STATUS_OK)
    {
        InternetCloseHandle(hRequest);
        hRequest = InternetOpenUrlW(hSession, Url, NULL, 0, Flags, 0);
        if (!hRequest)
            return NULL;
        if (!QueryStatus(hRequest, Resource.Status))
            goto fail;
    }

    if (!Resource.AcceptsRanges && QueryHeader(hRequest, HTTP_QUERY_CONTENT_LENGTH, Value))
    {
        p = Value;
        if (!ParseNumber(p, Resource.Length))
            Resource.Length = 0;
    }

    Size = 0;
    if (!InternetQueryOptionW(hRequest, INTERNET_OPTION_URL, NULL, &Size) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && Size)
    {
        Buffer = Resource.Url.GetBuffer(Size / sizeof(WCHAR) + 1);
        Ok = InternetQueryOptionW(hRequest, INTERNET_OPTION_URL, Buffer, &Size);
        Resource.Url.ReleaseBuffer(Ok ? -1 : 0);
        if (!Ok || Resource.Url.IsEmpty())
            Resource.Url = Url;
    }

    if (QueryHeader(hRequest, HTTP_QUERY_ETAG, Value) && Value.Left(2) != L"W/")
        Resource.Validator = Value;
    else if (QueryHeader(hRequest, HTTP_QUERY_LAST_MODIFIED, Value))
        Resource.Validator = Value;

    return hRequest;

fail:
    Error = GetLastError();
    InternetCloseHandle(hRequest);
    SetLastError(Error);
    return NULL;
}

static DWORD
StreamDownload(
    HINTERNET hRequest,
    ULONGLONG Length,
    HANDLE hFile,
    PSHA_CTX Hash,
    const DOWNLOAD_SINK &Sink,
    ULONGLONG &Received)
{
    PBYTE Buffer;
    DWORD Read, Written, Error = ERROR_SUCCESS;

    Buffer = (PBYTE)HeapAlloc(GetProcessHeap(), 0, DL_READ_SIZE);
    if (!Buffer)
        return ERROR_NOT_ENOUGH_MEMORY;

    for (;;)
    {
        if (Sink.IsCancelled(Sink.Context))
        {
            Error = ERROR_CANCELLED;
            break;
        }
        if (!InternetReadFile(hRequest, Buffer, DL_READ_SIZE, &Read))
        {
            Error = GetLastError();
            break;
        }
        if (!Read)
            break;
        if (Hash)
            A_SHAUpdate(Hash, Buffer, Read);
        if (!WriteFile(hFile, Buffer, Read, &Written, NULL) || Written != Read)
        {
            Error = GetLastError();
            break;
        }
        Received += Read;
        InterlockedExchangeAdd64(Sink.Received, Read);
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    if (Error == ERROR_SUCCESS && Length && Received != Length)
        Error = ERROR_INTERNET_CONNECTION_ABORTED;
    return Error;
}

static BOOL
JobAborted(DL_JOB *Job)
{
    return InterlockedCompareExchange(&Job->Abort, FALSE, FALSE) != FALSE;
}

static VOID
JobFail(DL_JOB *Job, DWORD Error)
{
    AcquireSRWLockExclusive(&Job->Lock);
    if (!Job->Abort)
    {
        Job->Error = Error;
        InterlockedExchange(&Job->Abort, TRUE);
    }
    WakeAllConditionVariable(&Job->Changed);
    ReleaseSRWLockExclusive(&Job->Lock);
}

static HINTERNET
OpenRange(DL_JOB *Job, ULONGLONG First, ULONGLONG Last, DWORD &Error)
{
    HINTERNET hRequest;
    CStringW Headers, Value;
    ULONGLONG RangeFirst, RangeLast, Total;
    DWORD Status;

    Headers.Format(L"Range: bytes=%I64u-%I64u\r\n", First, Last);
    if (*Job->Validator)
    {
        Headers += L"If-Range: ";
        Headers += Job->Validator;
        Headers += L"\r\n";
    }

    hRequest = InternetOpenUrlW(Job->hSession, Job->Url, Headers, (DWORD)-1, Job->Flags, 0);
    if (!hRequest)
    {
        Error = GetLastError();
        return NULL;
    }
    if (!QueryStatus(hRequest, Status) || Status != HTTP_STATUS_PARTIAL_CONTENT ||
        !QueryHeader(hRequest, HTTP_QUERY_CONTENT_RANGE, Value) ||
        !ParseContentRange(Value, RangeFirst, RangeLast, Total) || RangeFirst != First || Total != Job->Total)
    {
        InternetCloseHandle(hRequest);
        Error = ERROR_HTTP_INVALID_SERVER_RESPONSE;
        return NULL;
    }
    return hRequest;
}

static DWORD
FetchChunk(DL_JOB *Job, ULONG Index, PBYTE Data)
{
    DL_CHUNK *Chunk = &Job->Chunks[Index];
    HINTERNET hRequest;
    ULONG Got = 0, Before, Failures = 0;
    DWORD Read, Error = ERROR_INTERNET_CONNECTION_ABORTED;
    BOOL Owned;

    while (Got < Chunk->Length)
    {
        if (JobAborted(Job))
            return ERROR_CANCELLED;
        if (Job->Sink->IsCancelled(Job->Sink->Context))
            return ERROR_CANCELLED;

        hRequest = (Index == 0 && Got == 0) ? (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&Job->hFirst, NULL)
                                            : NULL;
        Owned = (hRequest == NULL);
        if (Owned)
            hRequest = OpenRange(Job, Chunk->Offset + Got, Chunk->Offset + Chunk->Length - 1, Error);

        Before = Got;
        while (hRequest && Got < Chunk->Length)
        {
            if (JobAborted(Job) || Job->Sink->IsCancelled(Job->Sink->Context))
            {
                if (Owned)
                    InternetCloseHandle(hRequest);
                return ERROR_CANCELLED;
            }
            if (!InternetReadFile(hRequest, Data + Got, min((ULONG)DL_READ_SIZE, Chunk->Length - Got), &Read))
            {
                Error = GetLastError();
                break;
            }
            if (!Read)
            {
                Error = ERROR_INTERNET_CONNECTION_ABORTED;
                break;
            }
            Got += Read;
            InterlockedExchangeAdd64(&Job->Received, Read);
            InterlockedExchangeAdd64(Job->Sink->Received, Read);
        }
        if (hRequest && Owned)
            InternetCloseHandle(hRequest);

        if (Got > Before)
            Failures = 0;
        else if (++Failures >= DL_ATTEMPTS)
            return Error;
    }
    return ERROR_SUCCESS;
}

static unsigned int CALLBACK
DownloadWorker(void *Parameter)
{
    DL_JOB *Job = (DL_JOB *)Parameter;
    ULONG Index;
    PBYTE Data;
    DWORD Error;

    for (;;)
    {
        AcquireSRWLockExclusive(&Job->Lock);
        while (!Job->Abort && Job->Next < Job->ChunkCount && Job->Next >= Job->Written + Job->Window)
            SleepConditionVariableSRW(&Job->Changed, &Job->Lock, INFINITE, 0);
        if (Job->Abort || Job->Next >= Job->ChunkCount)
        {
            ReleaseSRWLockExclusive(&Job->Lock);
            return 0;
        }
        Index = Job->Next++;
        ReleaseSRWLockExclusive(&Job->Lock);

        Data = (PBYTE)HeapAlloc(GetProcessHeap(), 0, Job->Chunks[Index].Length);
        Error = Data ? FetchChunk(Job, Index, Data) : ERROR_NOT_ENOUGH_MEMORY;
        if (Error != ERROR_SUCCESS)
        {
            if (Data)
                HeapFree(GetProcessHeap(), 0, Data);
            JobFail(Job, Error);
            return 0;
        }

        AcquireSRWLockExclusive(&Job->Lock);
        Job->Chunks[Index].Data = Data;
        Job->Chunks[Index].Ready = TRUE;
        WakeAllConditionVariable(&Job->Changed);
        ReleaseSRWLockExclusive(&Job->Lock);
    }
}

static DWORD
SegmentedDownload(
    HINTERNET hSession,
    HINTERNET hFirst,
    const DOWNLOAD_RESOURCE &Resource,
    DWORD Flags,
    HANDLE hFile,
    PSHA_CTX Hash,
    const DOWNLOAD_SINK &Sink,
    ULONGLONG &Received)
{
    HANDLE Threads[DL_MAX_WORKERS];
    ULONG Workers, Started = 0, MaxConns, Index;
    ULONGLONG ChunkSize, Offset;
    DWORD Size, Written, Error = ERROR_SUCCESS;
    PBYTE Data;
    DL_JOB Job;

    ZeroMemory(&Job, sizeof(Job));
    ChunkSize = Resource.Length / (DL_MAX_WORKERS * 8);
    ChunkSize = max((ULONGLONG)DL_MIN_CHUNK, min((ULONGLONG)DL_MAX_CHUNK, ChunkSize));
    ChunkSize = (ChunkSize + DL_CHUNK_ALIGN - 1) & ~(ULONGLONG)(DL_CHUNK_ALIGN - 1);

    Job.ChunkCount = (ULONG)((Resource.Length + ChunkSize - 1) / ChunkSize);
    Job.Chunks = (DL_CHUNK *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Job.ChunkCount * sizeof(DL_CHUNK));
    if (!Job.Chunks)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (Index = 0, Offset = 0; Index < Job.ChunkCount; Index++, Offset += ChunkSize)
    {
        Job.Chunks[Index].Offset = Offset;
        Job.Chunks[Index].Length = (ULONG)min(ChunkSize, Resource.Length - Offset);
    }

    Workers = min((ULONG)DL_MAX_WORKERS, Job.ChunkCount);
    Size = sizeof(MaxConns);
    if (InternetQueryOptionW(NULL, INTERNET_OPTION_MAX_CONNS_PER_SERVER, &MaxConns, &Size) && MaxConns < Workers)
        InternetSetOptionW(NULL, INTERNET_OPTION_MAX_CONNS_PER_SERVER, &Workers, sizeof(Workers));

    Job.hSession = hSession;
    Job.hFirst = hFirst;
    Job.Url = Resource.Url;
    Job.Validator = Resource.Validator;
    Job.Flags = Flags;
    Job.Sink = &Sink;
    Job.Window = Workers * 2;
    Job.Total = Resource.Length;
    InitializeSRWLock(&Job.Lock);
    InitializeConditionVariable(&Job.Changed);

    for (Started = 0; Started < Workers; Started++)
    {
        Threads[Started] = (HANDLE)_beginthreadex(NULL, 0, DownloadWorker, &Job, 0, NULL);
        if (!Threads[Started])
            break;
    }
    if (!Started)
    {
        HeapFree(GetProcessHeap(), 0, Job.Chunks);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    for (Index = 0; Index < Job.ChunkCount; Index++)
    {
        AcquireSRWLockExclusive(&Job.Lock);
        while (!Job.Abort && !Job.Chunks[Index].Ready)
            SleepConditionVariableSRW(&Job.Changed, &Job.Lock, INFINITE, 0);
        if (Job.Abort)
        {
            ReleaseSRWLockExclusive(&Job.Lock);
            break;
        }
        Data = Job.Chunks[Index].Data;
        Job.Chunks[Index].Data = NULL;
        ReleaseSRWLockExclusive(&Job.Lock);

        if (Hash)
            A_SHAUpdate(Hash, Data, Job.Chunks[Index].Length);
        if (!WriteFile(hFile, Data, Job.Chunks[Index].Length, &Written, NULL) ||
            Written != Job.Chunks[Index].Length)
        {
            Error = GetLastError();
            HeapFree(GetProcessHeap(), 0, Data);
            JobFail(&Job, Error ? Error : ERROR_WRITE_FAULT);
            break;
        }
        HeapFree(GetProcessHeap(), 0, Data);

        AcquireSRWLockExclusive(&Job.Lock);
        Job.Written = Index + 1;
        WakeAllConditionVariable(&Job.Changed);
        ReleaseSRWLockExclusive(&Job.Lock);
    }

    WaitForMultipleObjects(Started, Threads, TRUE, INFINITE);
    while (Started)
        CloseHandle(Threads[--Started]);

    for (Index = 0; Index < Job.ChunkCount; Index++)
    {
        if (Job.Chunks[Index].Data)
            HeapFree(GetProcessHeap(), 0, Job.Chunks[Index].Data);
    }
    HeapFree(GetProcessHeap(), 0, Job.Chunks);

    Received = (ULONGLONG)Job.Received;
    return Job.Abort ? Job.Error : ERROR_SUCCESS;
}

DWORD
DownloadToFile(
    HINTERNET hSession,
    HINTERNET hRequest,
    const DOWNLOAD_RESOURCE &Resource,
    DWORD Flags,
    HANDLE hFile,
    PSHA_CTX Hash,
    const DOWNLOAD_SINK &Sink,
    ULONGLONG &Received)
{
    Received = 0;
    if (Resource.AcceptsRanges && Resource.Length >= 2 * DL_MIN_CHUNK)
        return SegmentedDownload(hSession, hRequest, Resource, Flags, hFile, Hash, Sink, Received);
    return StreamDownload(hRequest, Resource.Length, hFile, Hash, Sink, Received);
}
