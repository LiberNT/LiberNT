/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stress test for restarting a USB video class stream: endpoint state changes and leaks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc_test.h"

#define RESTART_CYCLES 200
#define UVC_TAG_KS 'ssKK'
#define UVC_TAG_DRIVER 'cvuL'
#define UVC_TAG_USBPORT 'pbsu'
#define UVC_TAG_XHCI 'xhcu'

typedef struct _UVC_FRAME_REQUEST
{
    KSSTREAM_HEADER Header;
    KS_FRAME_INFO FrameInfo;
} UVC_FRAME_REQUEST;

typedef BOOL (WINAPI *GET_PERFORMANCE_INFO)(PVOID Info, DWORD Size);

typedef struct _UVC_PERFORMANCE_INFORMATION
{
    DWORD cb;
    SIZE_T CommitTotal;
    SIZE_T CommitLimit;
    SIZE_T CommitPeak;
    SIZE_T PhysicalTotal;
    SIZE_T PhysicalAvailable;
    SIZE_T SystemCache;
    SIZE_T KernelTotal;
    SIZE_T KernelPaged;
    SIZE_T KernelNonpaged;
    SIZE_T PageSize;
    DWORD HandleCount;
    DWORD ProcessCount;
    DWORD ThreadCount;
} UVC_PERFORMANCE_INFORMATION;

static SIZE_T
KernelPages(void)
{
    static GET_PERFORMANCE_INFO Query;
    UVC_PERFORMANCE_INFORMATION Info;

    if (!Query)
        Query = (GET_PERFORMANCE_INFO)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetPerformanceInfo");
    ZeroMemory(&Info, sizeof(Info));
    Info.cb = sizeof(Info);
    if (!Query || !Query(&Info, sizeof(Info)))
        return 0;
    return Info.KernelNonpaged + Info.KernelPaged;
}

#define POOL_TAG_INFORMATION_CLASS 22
#define POOL_SNAPSHOT_SIZE (1024 * 1024)

typedef LONG (WINAPI *QUERY_SYSTEM_INFORMATION)(ULONG Class, PVOID Buffer, ULONG Length, PULONG Returned);

typedef struct _UVC_POOL_TAG
{
    ULONG Tag;
    ULONG PagedAllocs;
    ULONG PagedFrees;
    SIZE_T PagedUsed;
    ULONG NonPagedAllocs;
    ULONG NonPagedFrees;
    SIZE_T NonPagedUsed;
} UVC_POOL_TAG;

typedef struct _UVC_POOL_TAGS
{
    ULONG Count;
    UVC_POOL_TAG Tags[1];
} UVC_POOL_TAGS;

static UVC_POOL_TAGS *
PoolSnapshot(void)
{
    QUERY_SYSTEM_INFORMATION Query = (QUERY_SYSTEM_INFORMATION)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    UVC_POOL_TAGS *Tags = HeapAlloc(GetProcessHeap(), 0, POOL_SNAPSHOT_SIZE);
    ULONG Returned = 0;
    LONG Status;

    if (!Query || !Tags)
        return NULL;
    Status = Query(POOL_TAG_INFORMATION_CLASS, Tags, POOL_SNAPSHOT_SIZE, &Returned);
    if (Status < 0)
    {
        trace("MEASURE pool tags status %lx\n", Status);
        HeapFree(GetProcessHeap(), 0, Tags);
        return NULL;
    }
    return Tags;
}

static void
PoolCompare(const UVC_POOL_TAGS *Before, const UVC_POOL_TAGS *After, ULONG Cycles)
{
    ULONG I, J, NewAllocs, NewFrees;

    for (I = 0; I < After->Count; I++)
    {
        const UVC_POOL_TAG *New = &After->Tags[I];
        SIZE_T OldUsed = 0;
        ULONG OldAllocs = 0, OldFrees = 0;

        for (J = 0; J < Before->Count; J++)
        {
            if (Before->Tags[J].Tag == New->Tag)
            {
                OldUsed = Before->Tags[J].PagedUsed + Before->Tags[J].NonPagedUsed;
                OldAllocs = Before->Tags[J].PagedAllocs + Before->Tags[J].NonPagedAllocs;
                OldFrees = Before->Tags[J].PagedFrees + Before->Tags[J].NonPagedFrees;
                break;
            }
        }
        NewAllocs = New->PagedAllocs + New->NonPagedAllocs - OldAllocs;
        NewFrees = New->PagedFrees + New->NonPagedFrees - OldFrees;
        if (New->PagedUsed + New->NonPagedUsed > OldUsed + 32 * 1024)
        {
            trace("MEASURE pool tag %.4s grew by %Iu bytes, allocs %lu frees %lu\n", (const char *)&New->Tag,
                  New->PagedUsed + New->NonPagedUsed - OldUsed, NewAllocs, NewFrees);
        }
        if (New->Tag == UVC_TAG_KS || New->Tag == UVC_TAG_DRIVER || New->Tag == UVC_TAG_USBPORT || New->Tag == UVC_TAG_XHCI)
        {
            ok(NewAllocs < NewFrees + Cycles, "Pool tag %.4s kept a block per cycle, allocs %lu frees %lu\n",
               (const char *)&New->Tag, NewAllocs, NewFrees);
        }
    }
}

static BOOL
RestartOnce(HANDLE Filter, const KS_DATAFORMAT_VIDEOINFOHEADER *Format, PBYTE Buffer, DWORD Size, BOOL Read)
{
    UVC_FRAME_REQUEST Request;
    HANDLE Pin = NULL;
    DWORD Returned, Error;
    BOOL Result = TRUE;

    Error = UvcCreatePin(Filter, 0, Format, &Pin);
    if (Error)
    {
        ok(0, "KsCreatePin failed %lu\n", Error);
        return FALSE;
    }

    UvcSetPinState(Pin, KSSTATE_ACQUIRE);
    UvcSetPinState(Pin, KSSTATE_PAUSE);
    UvcSetPinState(Pin, KSSTATE_RUN);
    if (Read)
    {
        ZeroMemory(&Request, sizeof(Request));
        Request.Header.Size = sizeof(Request);
        Request.Header.FrameExtent = Size;
        Request.Header.Data = Buffer;
        Request.FrameInfo.ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
        Result = UvcIoctl(Pin, IOCTL_KS_READ_STREAM, NULL, 0, &Request, sizeof(Request), &Returned);
        ok(Result, "Reading a frame failed %lu\n", GetLastError());
    }
    UvcSetPinState(Pin, KSSTATE_PAUSE);
    UvcSetPinState(Pin, KSSTATE_STOP);
    CloseHandle(Pin);
    return Result;
}

START_TEST(KsRestart)
{
    KS_DATAFORMAT_VIDEOINFOHEADER Format;
    WCHAR Path[512];
    HANDLE Filter;
    PBYTE Buffer;
    DWORD Size = 160 * 120 * 2, Start;
    SIZE_T Before, After;
    UVC_POOL_TAGS *TagsBefore, *TagsAfter;
    ULONG Cycle;

    if (!UvcFindCamera(&UvcCategoryCapture, Path, ARRAYSIZE(Path)))
    {
        skip("No usb-uvc test camera\n");
        return;
    }

    Filter = UvcOpenFilter(Path);
    ok(Filter != INVALID_HANDLE_VALUE, "Opening the filter failed %lu\n", GetLastError());
    if (Filter == INVALID_HANDLE_VALUE)
        return;

    Buffer = VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    UvcBuildFormat(&Format, &UvcSubtypeYuy2, UVC_FOURCC('Y', 'U', 'Y', '2'), 160, 120, 333333);

    for (Cycle = 0; Cycle < 8; Cycle++)
        RestartOnce(Filter, &Format, Buffer, Size, TRUE);

    Sleep(500);
    TagsBefore = PoolSnapshot();
    Before = KernelPages();
    Start = GetTickCount();
    for (Cycle = 0; Cycle < RESTART_CYCLES; Cycle++)
    {
        if (!RestartOnce(Filter, &Format, Buffer, Size, Cycle % 4 == 0))
            break;
        if (Cycle % 20 == 0)
            trace("Cycle %lu, %lu ms\n", Cycle, GetTickCount() - Start);
    }
    ok(Cycle == RESTART_CYCLES, "Stopped after %lu cycles\n", Cycle);

    Sleep(500);
    After = KernelPages();
    trace("MEASURE kernel pool pages %Iu then %Iu, %lu ms\n", Before, After, GetTickCount() - Start);
    TagsAfter = PoolSnapshot();
    ok(TagsBefore != NULL && TagsAfter != NULL, "Pool tag snapshots failed\n");
    if (TagsBefore && TagsAfter)
        PoolCompare(TagsBefore, TagsAfter, RESTART_CYCLES);

    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Filter);
}
